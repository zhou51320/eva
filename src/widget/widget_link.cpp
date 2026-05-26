#include "ui_widget.h"
#include "widget.h"
#include "runtime/eva_runtime.h"
#include "../utils/textparse.h"
#include "../utils/flowtracer.h"
#include "../utils/openai_compat.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QUrl>
#include <QHostInfo>
#include <QFileInfo>
#include <QTextCharFormat>
#include <algorithm>

namespace
{
QString previewForLog(const QString &text, int limit = 120)
{
    QString trimmed = text;
    trimmed.replace("\n", "\\n");
    trimmed.replace("\r", "\\r");
    if (trimmed.size() > limit)
    {
        trimmed = trimmed.left(limit) + QStringLiteral("…");
    }
    return trimmed;
}

QString normalizeLinkEndpoint(const QString &rawEndpoint)
{
    // Remove whitespace, infer scheme when missing, and drop a trailing /v1 to avoid duplicating the version segment
    QString clean = TextParse::removeAllWhitespace(rawEndpoint);
    QUrl url = QUrl::fromUserInput(clean);
    const QString host = url.host();
    const bool isLocal = isLoopbackHost(host);
    const QString scheme = url.scheme().toLower();
    if (scheme.isEmpty())
        url.setScheme(isLocal ? QStringLiteral("http") : QStringLiteral("https"));

    QString path = url.path();
    // Collapse连续的斜杠，避免用户多输“////”导致路径异常
    QString collapsed;
    collapsed.reserve(path.size());
    bool prevSlash = false;
    for (const QChar ch : path)
    {
        if (ch == QLatin1Char('/'))
        {
            if (!prevSlash) collapsed.append(ch);
            prevSlash = true;
        }
        else
        {
            collapsed.append(ch);
            prevSlash = false;
        }
    }
    path = collapsed;
    while (path.endsWith('/') && path.length() > 1) path.chop(1);
    const QString lowerPath = path.toLower();
    if (lowerPath.endsWith(QStringLiteral("/v1")))
    {
        const int slashPos = path.lastIndexOf('/');
        QString basePath = path.left(slashPos);
        if (basePath.isEmpty())
            basePath = QStringLiteral("/");
        // 根路径不保留尾部斜杠，避免与后续路径拼接产生“//”
        if (basePath == QStringLiteral("/"))
        {
            url.setPath(QString());
        }
        else
        {
            url.setPath(basePath);
        }
    }
    else
    {
        // 仅去掉尾部斜杠并应用规整后的路径
        // 根路径不保留尾部斜杠，避免与后续路径拼接产生“//”
        if (path.isEmpty() || path == QStringLiteral("/"))
        {
            url.setPath(QString());
        }
        else
        {
            url.setPath(path);
        }
    }
    return url.toString(QUrl::RemoveFragment);
}
} // namespace

//-------------------------------------------------------------------------
//----------------------------------链接相关--------------------------------
//-------------------------------------------------------------------------

// 应用api设置
void Widget::set_api()
{
    // 纯请求式：不再使用本地嵌入模型进程（xbot）
    historypath = ""; // 重置
    linkProfile_ = LinkProfile::Api;
    controlAwaitingHello_ = false;

    // 获取设置值
    // Sanitize endpoint/key/model: strip whitespace, normalize scheme, strip trailing /v1
    QString clean_endpoint = normalizeLinkEndpoint(api_endpoint_LineEdit->text());
    const QString clean_key = TextParse::removeAllWhitespace(api_key_LineEdit->text());
    const QString clean_model = TextParse::removeAllWhitespace(api_model_LineEdit->text());
    // Reflect cleaned values in UI
    api_endpoint_LineEdit->setText(clean_endpoint);
    api_key_LineEdit->setText(clean_key);
    api_model_LineEdit->setText(clean_model);
    apis.api_endpoint = clean_endpoint;
    apis.api_key = clean_key;
    apis.api_model = clean_model;
    apis.is_local_backend = false;
    // 根据 base url 自动选择 OpenAI 兼容接口路径风格：
    // - 默认（OpenAI/llama.cpp 等）：base 不含版本号，接口固定为 /v1/...
    // - 火山方舟 Ark：base 自带 /api/v3，接口直接使用 /chat/completions、/models 等
    //   若仍然额外追加 /v1，会被拼成 /api/v3/v1/... 从而请求失败
    {
        const QUrl baseUrl = QUrl::fromUserInput(apis.api_endpoint);
        apis.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
        apis.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
    }
    if (runtime_)
    {
        RuntimeConnectRemoteCommand command;
        command.endpoint = apis.api_endpoint;
        command.apiKey = apis.api_key;
        command.model = apis.api_model;
        command.sampling = ui_SETTINGS;
        QString runtimeError;
        if (!runtime_->connectRemote(command, &runtimeError))
        {
            qWarning().noquote() << QStringLiteral("EvaRuntime connectRemote failed:") << runtimeError;
        }
    }

    projectRuntimeLinkReadyState(apis);
    // 进入链接模式后：
    // 1) 终止当前的流式请求（若有）
    emit ui2net_stop(true);
    // 2) 停止本地 llama.cpp server 后端，避免占用资源/混淆来源
    if (serverManager && serverManager->isRunning())
    {
        serverManager->stop();
        // reflash_state("ui:backend stopped", SIGNAL_SIGNAL);
    }
    setBackendLifecycleState(BackendLifecycleState::Stopped, QStringLiteral("switch link mode"), SIGNAL_SIGNAL, false);
    reflash_state("ui:" + jtr("eva link"), EVA_SIGNAL);
    EVA_title = jtr("current api") + " " + sessionEndpointForHistory();
    reflash_state("ui:" + EVA_title, USUAL_SIGNAL);
    this->setWindowTitle(EVA_title);
    trayIcon->setToolTip(EVA_title);
    setBaseWindowIcon(QIcon(":/logo/eva.png"));

    {
        // 切换链接模式后立即同步评估页上下文上限（未探测到时标记为未知）
        SETTINGS snap = ui_SETTINGS;
        if (runtimeModeForUi() == RuntimeMode::Link) snap.nctx = (slotCtxMax_ > 0 ? slotCtxMax_ : 0);
        emit ui2expend_settings(snap);
    }
    // Reset LINK-mode memory/state since endpoint/key/model changed
    // Reset KV counters when switching to LINK mode to avoid leaking local state
    kvTokensTurn_ = 0;
    kvPromptTokensTurn_ = 0;
    kvUsed_ = 0;
    kvUsedBeforeTurn_ = 0;
    kvStreamedTurn_ = 0;
    lastReasoningTokens_ = 0;
    projectRuntimeIdleState(true);
    sawPromptPast_ = false;
    sawFinalPast_ = false;
    currentSlotId_ = -1;
    slotCtxMax_ = 0;
    enforcePredictLimit();
    updateKvBarUi();
    fetchRemoteContextLimit();
    flushPendingStream();
    ui->output->clear();
    // Reset record bar to avoid residual nodes when switching to LINK mode
    recordClear();
    // Create record BEFORE printing header/content so docFrom anchors at header area
    int __idx = recordCreate(RecordRole::System);
    appendRoleHeader(QStringLiteral("system"));
    reflash_output_tool_highlight(ui_DATES.date_prompt, themeTextPrimary());
    recordAppendText(__idx, ui_DATES.date_prompt);
    lastSystemRecordIndex_ = __idx;
    // 重置对话消息并注入系统指令
    ui_messagesArray = QJsonArray();
    QJsonObject systemMessage;
    systemMessage.insert("role", DEFAULT_SYSTEM_NAME);
    systemMessage.insert("content", ui_DATES.date_prompt);
    ui_messagesArray.append(systemMessage);
    recordEntries_[__idx].msgIndex = 0;
    // start a new persistent history session in LINK mode
    if (history_)
    {
        SessionMeta meta;
        meta.id = QString::number(QDateTime::currentMSecsSinceEpoch());
        meta.title = "";
        meta.endpoint = sessionEndpointForHistory();
        meta.model = sessionModelForHistory();
        meta.system = ui_DATES.date_prompt;
        meta.n_ctx = sessionContextSize();
        meta.slot_id = -1;
        meta.startedAt = QDateTime::currentDateTime();
        history_->begin(meta);
        history_->appendMessage(systemMessage);
        currentSlotId_ = -1;
    }
    syncRuntimeSessionMirror(true);
    auto_save_user();
}

// 链接模式下工具返回结果时延迟发送
void Widget::tool_testhandleTimeout()
{
    // Ensure latest LINK apis before pushing (users may edit endpoint/key/model after linking)
    if (runtimeModeForUi() == RuntimeMode::Link)
    {
        QString clean_endpoint = normalizeLinkEndpoint(api_endpoint_LineEdit->text());
        const QString clean_key = TextParse::removeAllWhitespace(api_key_LineEdit->text());
        const QString clean_model = TextParse::removeAllWhitespace(api_model_LineEdit->text());
        if (clean_endpoint != apis.api_endpoint || clean_key != apis.api_key || clean_model != apis.api_model)
        {
            apis.api_endpoint = clean_endpoint;
            apis.api_key = clean_key;
            apis.api_model = clean_model;
            apis.is_local_backend = false;
            // 允许用户在不重新“装载/确认”的情况下修改端点：
            // 这里需要同步更新各厂商的 OpenAI 兼容路径风格，避免 Ark(/api/v3) 被误拼为 /api/v3/v1/...
            {
                const QUrl baseUrl = QUrl::fromUserInput(apis.api_endpoint);
                apis.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
                apis.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
            }
        }
    }
    ENDPOINT_DATA data;
    data.date_prompt = ui_DATES.date_prompt;
    data.stopwords = ui_DATES.extra_stop_words;
    data.is_complete_state = runtimeConversationModeForUi() == ConversationMode::Complete;
    const SETTINGS settings = sessionSettingsSnapshot();
    data.temp = settings.temp;
    data.repeat = settings.repeat;
    data.top_k = settings.top_k;
    data.top_p = settings.hid_top_p;
    data.n_predict = settings.hid_npredict;
    data.messagesArray = ui_messagesArray;
    data.id_slot = currentSlotId_;

    emit_send(data);
}

void Widget::send_testhandleTimeout()
{
    on_send_clicked();
}

// 链接模式切换时某些控件可见状态
void Widget::change_api_dialog(bool enable)
{
    // 链接模式隐藏“后端设置”整组；保留“采样设置”和“状态设置”
    // enable==0 -> LINK_MODE: backend controls hidden; enable==1 -> LOCAL_MODE: show backend controls
    if (settings_ui && settings_ui->backend_box)
    {
        settings_ui->backend_box->setVisible(enable);
        if (settings_dialog && settings_dialog->isVisible())
        {
            applySettingsDialogSizing();
        }
    }
}

// Probe remote /v1/models to determine max context for current model (LINK mode)
void Widget::fetchRemoteContextLimit()
{
    if (runtimeModeForUi() != RuntimeMode::Link) return;
    const APIS stateApis = sessionApisSnapshot();
    QUrl base = QUrl::fromUserInput(stateApis.api_endpoint);
    if (!base.isValid()) return;
    // 无论是否本机，优先读取 /props 获取实际运行时 n_ctx；若缺失则回退 /v1/models
    fetchPropsContextLimit(true, true);
}

void Widget::fetchModelsContextLimit(bool isLocalEndpoint)
{
    Q_UNUSED(isLocalEndpoint);
    if (runtimeModeForUi() != RuntimeMode::Link) return;
    const APIS stateApis = sessionApisSnapshot();
    QUrl base = QUrl::fromUserInput(stateApis.api_endpoint);
    if (!base.isValid()) return;
    // /v1/models 是最常见的 OpenAI 兼容路径；但火山方舟 Ark 的 base 已经包含 /api/v3，
    // 因此 models 路径应为 /models（最终拼成 /api/v3/models）
    const QUrl url = OpenAiCompat::joinPath(base, OpenAiCompat::modelsPath(base));

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!apis.api_key.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + apis.api_key.toUtf8());

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *rp = nam->get(req);
    connect(rp, &QNetworkReply::finished, this, [this, nam, rp, base]()
            {
        rp->deleteLater();
        nam->deleteLater();
        if (rp->error() != QNetworkReply::NoError)
        {
            return;
        }
        const QByteArray body = rp->readAll();
        QJsonParseError perr{};
        QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error != QJsonParseError::NoError)
        {
            return;
        }
        int maxCtx = -1;
        int firstCtx = -1;
        QString firstAlias;
        QStringList dbgLines;
    auto tryPick = [&](const QJsonObject &o) {
            // Try common fields from various providers
            const char *keys[] = {"max_model_len","context_length","max_input_tokens","max_context_length","max_input_length","prompt_token_limit","input_token_limit"};
            for (auto k : keys) {
                if (o.contains(k)) { int v = o.value(k).toInt(-1); if (v > 0) return v; }
            }
            // DashScope-style: extra_info.default_envs.max_input_tokens / max_tokens
            if (o.contains("extra_info") && o.value("extra_info").isObject()) {
                const QJsonObject extra = o.value("extra_info").toObject();
                const char *ekeys[] = {"max_input_tokens","context_length","max_context_length"};
                for (auto k : ekeys) {
                    if (extra.contains(k)) { int v = extra.value(k).toInt(-1); if (v > 0) return v; }
                }
                if (extra.contains("default_envs") && extra.value("default_envs").isObject()) {
                    const QJsonObject envs = extra.value("default_envs").toObject();
                    const char *dkeys[] = {"max_input_tokens","context_length","max_context_length","max_tokens"};
                    for (auto k : dkeys) {
                        int v = envs.value(k).toInt(-1);
                        if (v > 0) return v;
                    }
                }
            }
            // Nested meta fields (llama.cpp returns meta.n_ctx_train)
            if (o.contains("meta") && o.value("meta").isObject()) {
                const QJsonObject meta = o.value("meta").toObject();
                const char *mkeys[] = {"n_ctx_train","n_ctx","context_length","max_context_length"};
                for (auto k : mkeys) {
                    int v = meta.value(k).toInt(-1);
                    if (v > 0) return v;
                }
            }
            // Some providers expose details.context_length
            if (o.contains("details") && o.value("details").isObject()) {
                const QJsonObject det = o.value("details").toObject();
                int v = det.value("context_length").toInt(-1);
                if (v > 0) return v;
            }
            // Also look under nested objects
            if (o.contains("capabilities") && o.value("capabilities").isObject()) {
                const QJsonObject cap = o.value("capabilities").toObject();
                const int v = cap.value("context_length").toInt(-1); if (v > 0) return v;
            }
            if (o.contains("limits") && o.value("limits").isObject()) {
                const QJsonObject lim = o.value("limits").toObject();
                const int v = lim.value("max_input_tokens").toInt(-1); if (v > 0) return v;
            }
            return -1;
        };
        auto baseName = [](QString s) {
            QFileInfo fi(s);
            QString name = fi.fileName();
            if (name.isEmpty()) name = s;
            if (name.endsWith(QStringLiteral(".gguf"), Qt::CaseInsensitive)) name.chop(5);
            return name;
        };
        auto matchModel = [&](const QString &idRaw) {
            if (apis.api_model.isEmpty()) return false;
            QString id = idRaw;
            if (id.isEmpty()) return false;
            if (id == apis.api_model) return true;
            // accept provider-prefixed ids like provider:model
            if (id.endsWith(":" + apis.api_model) || id.endsWith("/" + apis.api_model)) return true;
            const QString idBase = baseName(id);
            const QString targetBase = baseName(apis.api_model);
            if (!idBase.isEmpty() && idBase == targetBase) return true;
            if (id.contains(targetBase) || targetBase.contains(idBase)) return true;
            return false;
        };
        auto updateAlias = [&](const QString &alias) {
            if (!alias.isEmpty() && alias != apis.api_model) {
                applyDiscoveredAlias(alias, QStringLiteral("v1/models"));
            }
        };
        if (doc.isObject())
        {
            const QJsonObject root = doc.object();
            if (root.contains("data") && root.value("data").isArray())
            {
                const QJsonArray arr = root.value("data").toArray();
                for (const auto &v : arr)
                {
                    if (!v.isObject()) continue;
                    const QJsonObject m = v.toObject();
                    const QString mid = m.value("id").toString();
                    const QString altModel = m.value("model").toString();
                    const QString name = m.value("name").toString();
                    const int ctxCandidate = tryPick(m);
                    if (firstAlias.isEmpty())
                    {
                        firstAlias = !mid.isEmpty() ? mid : (!altModel.isEmpty() ? altModel : name);
                        firstCtx = ctxCandidate;
                    }
                    dbgLines << QStringLiteral("[data] id=%1 model=%2 name=%3 ctx=%4")
                                    .arg(mid, altModel, name, QString::number(ctxCandidate));
                    if ((!mid.isEmpty() && matchModel(mid)) || (!altModel.isEmpty() && matchModel(altModel)) || (!name.isEmpty() && matchModel(name)))
                    {
                        const QString alias = !mid.isEmpty() ? mid : (!altModel.isEmpty() ? altModel : name);
                        updateAlias(alias);
                        maxCtx = ctxCandidate;
                        if (maxCtx > 0) break;
                    }
                }
            }
            // Some providers might return "models" array (llama-server legacy) or a single object
            if (maxCtx <= 0 && root.contains("models") && root.value("models").isArray())
            {
                const QJsonArray arr = root.value("models").toArray();
                for (const auto &v : arr)
                {
                    if (!v.isObject()) continue;
                    const QJsonObject m = v.toObject();
                    const QString mid = m.value("model").toString();
                    const QString name = m.value("name").toString();
                    const int ctxCandidate = tryPick(m);
                    if (firstAlias.isEmpty())
                    {
                        firstAlias = !mid.isEmpty() ? mid : name;
                        firstCtx = ctxCandidate;
                    }
                    dbgLines << QStringLiteral("[models] id=%1 name=%2 ctx=%3")
                                    .arg(mid, name, QString::number(ctxCandidate));
                    if ((!mid.isEmpty() && matchModel(mid)) || (!name.isEmpty() && matchModel(name)))
                    {
                        const QString alias = !mid.isEmpty() ? mid : name;
                        updateAlias(alias);
                        maxCtx = ctxCandidate;
                        if (maxCtx > 0) break;
                    }
                }
            }
            // Some providers might return a single object for the model
            if (maxCtx <= 0)
            {
                maxCtx = tryPick(root);
                dbgLines << QStringLiteral("[root] ctx=%1").arg(maxCtx);
                if (firstAlias.isEmpty() && root.contains("id")) firstAlias = root.value("id").toString();
                if (firstAlias.isEmpty() && root.contains("model")) firstAlias = root.value("model").toString();
                if (firstAlias.isEmpty() && root.contains("name")) firstAlias = root.value("name").toString();
                if (firstCtx <= 0) firstCtx = maxCtx;
            }
        }
        // Fallback: if没有匹配到但只有一个候选，直接采用首个模型的别名与上下文
        if (maxCtx <= 0 && !firstAlias.isEmpty() && firstCtx > 0)
        {
            updateAlias(firstAlias);
            maxCtx = firstCtx;
            dbgLines << QStringLiteral("[fallback-first] alias=%1 ctx=%2").arg(firstAlias).arg(firstCtx);
        }
        if (maxCtx > 0)
        {
            slotCtxMax_ = maxCtx;
            enforcePredictLimit();
            updateKvBarUi();
            // Notify Expend (evaluation tab) to refresh displayed n_ctx
            SETTINGS snap = ui_SETTINGS;
            if (runtimeModeForUi() == RuntimeMode::Link) snap.nctx = (slotCtxMax_ > 0 ? slotCtxMax_ : 0);
            emit ui2expend_settings(snap);
            const QString log = QStringLiteral("net:n_ctx via /v1/models = %1").arg(maxCtx);
            FlowTracer::log(FlowChannel::Net, dbgLines.join(QStringLiteral(" | ")), 0);
            FlowTracer::log(FlowChannel::Net, log, 0);
        }
        else
        {
            // Fallback: query single model detail if list lacks context fields (e.g. DashScope)
            const QString modelId = apis.api_model.trimmed();
            if (modelId.isEmpty()) return;
            const QString encodedModel = QString::fromUtf8(QUrl::toPercentEncoding(modelId));
            const QUrl detailUrl = OpenAiCompat::joinPath(base, OpenAiCompat::modelsPath(base) + QStringLiteral("/") + encodedModel);
            if (!detailUrl.isValid()) return;

            QNetworkRequest req(detailUrl);
            req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
            if (!apis.api_key.isEmpty())
                req.setRawHeader("Authorization", QByteArray("Bearer ") + apis.api_key.toUtf8());

            auto *detailNam = new QNetworkAccessManager(this);
            QNetworkReply *detailRp = detailNam->get(req);
            connect(detailRp, &QNetworkReply::finished, this, [this, detailNam, detailRp, tryPick]() {
                detailRp->deleteLater();
                detailNam->deleteLater();
                if (detailRp->error() != QNetworkReply::NoError) return;
                QJsonParseError derr{};
                const QByteArray body = detailRp->readAll();
                QJsonDocument ddoc = QJsonDocument::fromJson(body, &derr);
                if (derr.error != QJsonParseError::NoError || !ddoc.isObject()) return;
                const QJsonObject obj = ddoc.object();
                const QString alias = obj.value(QStringLiteral("id")).toString();
                if (!alias.isEmpty() && alias != apis.api_model)
                {
                    applyDiscoveredAlias(alias, QStringLiteral("v1/models/{id}"));
                }
                const int ctx = tryPick(obj);
                if (ctx > 0)
                {
                    applyDiscoveredContext(ctx, QStringLiteral("v1/models/{id}"));
                }
            });
        }
    });
}

// MindIE: 通过自研接口 /v1/config 或 Triton 风格 /v2/models/{model}/config 获取最大上下文（maxSeqLen/max_seq_len）
// - /v2/models/{model}/config：按模型返回配置，更精确
// - /v1/config：返回服务启动时加载的静态配置（通常是首个模型）
void Widget::fetchMindieContextLimit(bool fallbackModels)
{
    if (runtimeModeForUi() != RuntimeMode::Link) return;
    const APIS stateApis = sessionApisSnapshot();
    QUrl base = QUrl::fromUserInput(stateApis.api_endpoint);
    if (!base.isValid()) return;

    auto fallback = [this, fallbackModels]() {
        if (fallbackModels)
        {
            fetchModelsContextLimit(true);
        }
    };

    auto requestV1Config = [this, base, fallback]() {
        const QUrl url = OpenAiCompat::joinPath(base, QStringLiteral("/v1/config"));
        if (!url.isValid())
        {
            fallback();
            return;
        }
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!apis.api_key.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + apis.api_key.toUtf8());

        auto *nam = new QNetworkAccessManager(this);
        QNetworkReply *rp = nam->get(req);
        connect(rp, &QNetworkReply::finished, this, [this, nam, rp, fallback]()
                {
            rp->deleteLater();
            nam->deleteLater();
            if (rp->error() != QNetworkReply::NoError)
            {
                fallback();
                return;
            }
            const QByteArray body = rp->readAll();
            QJsonParseError perr{};
            QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
            if (perr.error != QJsonParseError::NoError || !doc.isObject())
            {
                fallback();
                return;
            }
            const QJsonObject root = doc.object();
            const QString alias = root.value(QStringLiteral("modelName")).toString();
            const int nctx = root.value(QStringLiteral("maxSeqLen")).toInt(-1);
            if (!alias.isEmpty())
            {
                applyDiscoveredAlias(alias, QStringLiteral("v1/config"));
            }
            if (nctx > 0)
            {
                applyDiscoveredContext(nctx, QStringLiteral("v1/config"));
                return;
            }
            fallback();
        });
    };

    // 优先尝试 /v2/models/{model}/config（需要用户填写 model）
    const QString model = apis.api_model.trimmed();
    if (model.isEmpty())
    {
        requestV1Config();
        return;
    }
    const QString encodedModel = QString::fromUtf8(QUrl::toPercentEncoding(model));
    const QUrl url = OpenAiCompat::joinPath(
        base, QStringLiteral("/v2/models/") + encodedModel + QStringLiteral("/config"));
    if (!url.isValid())
    {
        requestV1Config();
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!apis.api_key.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + apis.api_key.toUtf8());

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *rp = nam->get(req);
    connect(rp, &QNetworkReply::finished, this, [this, nam, rp, requestV1Config]()
            {
        rp->deleteLater();
        nam->deleteLater();
        if (rp->error() != QNetworkReply::NoError)
        {
            requestV1Config();
            return;
        }
        const QByteArray body = rp->readAll();
        QJsonParseError perr{};
        QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
        {
            requestV1Config();
            return;
        }
        const QJsonObject root = doc.object();
        const QString alias = root.value(QStringLiteral("model_name")).toString();
        int nctx = root.value(QStringLiteral("max_seq_len")).toInt(-1);
        if (nctx <= 0) nctx = root.value(QStringLiteral("maxSeqLen")).toInt(-1);
        if (!alias.isEmpty())
        {
            applyDiscoveredAlias(alias, QStringLiteral("v2/models/config"));
        }
        if (nctx > 0)
        {
            applyDiscoveredContext(nctx, QStringLiteral("v2/models/config"));
            return;
        }
        requestV1Config();
    });
}

// Fallback: GET /props from llama.cpp tools/server to obtain runtime n_ctx
void Widget::fetchPropsContextLimit(bool allowLinkMode, bool fallbackModels)
{
    if (runtimeModeForUi() != RuntimeMode::Local && !allowLinkMode) return;
    const APIS stateApis = sessionApisSnapshot();
    QUrl base = QUrl::fromUserInput(stateApis.api_endpoint);
    if (!base.isValid()) return;
    QUrl url(base);
    QString path = url.path();
    if (!path.endsWith('/')) path += '/';
    path += QLatin1String("props");
    url.setPath(path);

    QNetworkRequest req(url);
    if (!apis.api_key.isEmpty())
        req.setRawHeader("Authorization", QByteArray("Bearer ") + apis.api_key.toUtf8());

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *rp = nam->get(req);
    connect(rp, &QNetworkReply::finished, this, [this, nam, rp, allowLinkMode, fallbackModels]()
            {
        rp->deleteLater();
        nam->deleteLater();
        bool gotCtx = false;
        auto fallback = [&]() {
            if (fallbackModels && !gotCtx)
            {
                fetchMindieContextLimit(true);
            }
        };
        if (rp->error() == QNetworkReply::NoError)
        {
            const QByteArray body = rp->readAll();
            QJsonParseError perr{};
            QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject())
            {
                const QJsonObject root = doc.object();
                const QString alias = root.value(QStringLiteral("model_alias")).toString();
                int nctx = -1;
                if (root.contains("default_generation_settings") && root.value("default_generation_settings").isObject())
                {
                    const QJsonObject dgs = root.value("default_generation_settings").toObject();
                    nctx = dgs.value("n_ctx").toInt(-1);
                    if (nctx <= 0 && dgs.contains("params") && dgs.value("params").isObject())
                    {
                        const QJsonObject params = dgs.value("params").toObject();
                        nctx = params.value("n_ctx").toInt(nctx);
                    }
                }
                if (!alias.isEmpty() && alias != apis.api_model)
                {
                    applyDiscoveredAlias(alias, QStringLiteral("props"));
                }
                if (nctx > 0)
                {
                    gotCtx = true;
                    applyDiscoveredContext(nctx, QStringLiteral("props"));
                }
                else
                {
                    // FlowTracer::log(FlowChannel::Net,
                    //                 QStringLiteral("net:/props missing n_ctx, body=%1").arg(QString::fromUtf8(body)),
                    //                 0);
                }
            }
            else
            {
                // FlowTracer::log(FlowChannel::Net,
                //                 QStringLiteral("net:/props parse error=%1 body=%2")
                //                     .arg(perr.errorString(), QString::fromUtf8(body)),
                //                 0);
            }
        }
        else
        {
            // FlowTracer::log(FlowChannel::Net,
            //                 QStringLiteral("net:/props http fail=%1").arg(rp->error()),
            //                 0);
        }
        fallback();
    });
}

void Widget::applyDiscoveredAlias(const QString &alias, const QString &sourceTag)
{
    if (alias.isEmpty() || alias == apis.api_model) return;
    apis.api_model = alias;
    api_model_LineEdit->setText(alias);
    emit ui2expend_apis(apis);
    syncRuntimeSessionMirror(false, false, true, true);
    FlowTracer::log(FlowChannel::Net,
                    QStringLiteral("net:model via %1 = %2").arg(sourceTag, alias),
                    0);
}

void Widget::applyDiscoveredContext(int nctx, const QString &sourceTag)
{
    if (nctx <= 0) return;
    slotCtxMax_ = nctx;
    enforcePredictLimit();
    updateKvBarUi();
    // Notify Expend (evaluation tab) with latest effective n_ctx
    SETTINGS snap = sessionSettingsSnapshot();
    if (runtimeModeForUi() == RuntimeMode::Link) snap.nctx = (slotCtxMax_ > 0 ? slotCtxMax_ : 0);
    emit ui2expend_settings(snap);
    FlowTracer::log(FlowChannel::Net,
                    QStringLiteral("net:n_ctx via %1 = %2").arg(sourceTag).arg(nctx),
                    0);
}

int Widget::resolvedContextLimitForUi() const
{
    // 统一目标：返回“单个槽(slot)可用的上下文上限”，用于 KV 记忆条、提示、以及 n_predict 上限等 UI 逻辑。
    // - LINK 模式：优先用探测值；若未探测到则返回 0 表示未知（避免用本地默认值误导用户）。
    // - LOCAL 模式：优先用日志/快照探测到的 slotCtxMax_；但当并发开启时，部分后端可能会上报“总 n_ctx”
    //   （= 单槽 * 并发），这会导致 UI 把“总上下文”误当成“单槽记忆容量”。因此这里做一次矫正。
    const RuntimeState runtimeState = runtimeStateSnapshotForSession();
    const bool hasRuntime = runtimeState.initialized;
    const RuntimeMode mode = hasRuntime ? runtimeState.mode : (ui_mode == LINK_MODE ? RuntimeMode::Link : RuntimeMode::Local);
    const SETTINGS settings = hasRuntime ? runtimeState.settings : ui_SETTINGS;
    const int parallel = (settings.hid_parallel > 0) ? settings.hid_parallel : 1;
    const int configuredSlot = (settings.nctx > 0) ? settings.nctx : DEFAULT_NCTX;

    if (mode == RuntimeMode::Link)
    {
        return (slotCtxMax_ > 0) ? slotCtxMax_ : 0;
    }

    if (slotCtxMax_ > 0)
    {
        int cap = slotCtxMax_;
        if (parallel > 1)
        {
            const int expectedTotal = configuredSlot * parallel;
            if (cap == expectedTotal) cap = configuredSlot;
        }
        return cap;
    }

    return configuredSlot;
}

QString Widget::resolvedContextLabelForUi() const
{
    const int cap = resolvedContextLimitForUi();
    return (cap > 0) ? QString::number(cap) : QStringLiteral("未知");
}

QString Widget::resolvedModelLabelForUi() const
{
    const RuntimeState runtimeState = runtimeStateSnapshotForSession();
    if (runtimeState.initialized && !runtimeState.currentModel.trimmed().isEmpty())
        return runtimeState.currentModel.trimmed();

    // LINK 模式下优先展示用户填写/探测到的模型名，否则用“未知”占位，避免误用本地默认模型名
    const bool linkMode = runtimeState.initialized ? runtimeState.mode == RuntimeMode::Link : ui_mode == LINK_MODE;
    if (linkMode)
    {
        const QString linkModel = runtimeState.initialized ? runtimeState.apis.api_model.trimmed()
                                                           : apis.api_model.trimmed();
        if (!linkModel.isEmpty()) return linkModel;
        return QStringLiteral("未知");
    }
    const SETTINGS settings = runtimeState.initialized ? runtimeState.settings : ui_SETTINGS;
    QString modelLabel = QFileInfo(settings.modelpath).fileName();
    if (modelLabel.isEmpty()) modelLabel = jtr("unknown model");
    return modelLabel;
}

//-------------------------------------------------------------------------
//---------------------------机体控制/镜像-----------------------------------
//-------------------------------------------------------------------------

bool Widget::isControllerActive() const
{
    return linkProfile_ == LinkProfile::Control && controlChannel_ && controlClient_.state == ControlChannel::ControllerState::Connected && !controlAwaitingHello_;
}

bool Widget::isHostControlled() const
{
    return controlChannel_ && controlHost_.active;
}

void Widget::setupControlChannel()
{
    if (controlChannel_) return;
    controlChannel_ = new ControlChannel(this);
    connect(controlChannel_, &ControlChannel::hostClientChanged, this, &Widget::handleControlHostClientChanged);
    connect(controlChannel_, &ControlChannel::hostCommandArrived, this, &Widget::handleControlHostCommand);
    connect(controlChannel_, &ControlChannel::controllerEventArrived, this, &Widget::handleControlControllerEvent);
    connect(controlChannel_, &ControlChannel::controllerStateChanged, this, &Widget::handleControlControllerState);
}

void Widget::setupAcpBridgeChannel()
{
    if (acpBridgeChannel_) return;
    acpBridgeChannel_ = new ControlChannel(this);
    connect(acpBridgeChannel_, &ControlChannel::hostClientChanged, this, &Widget::handleAcpBridgeClientChanged);
    connect(acpBridgeChannel_, &ControlChannel::hostCommandArrived, this, &Widget::handleAcpBridgeCommand);
}

void Widget::ensureAcpBridgeHost()
{
    setupAcpBridgeChannel();
    if (!acpBridgeChannel_) return;
    if (acpBridgeChannel_->startHost(static_cast<quint16>(DEFAULT_ACP_BRIDGE_PORT), QHostAddress::LocalHost))
    {
        FlowTracer::log(FlowChannel::Net, QStringLiteral("[acp-bridge] listening on 127.0.0.1:%1").arg(DEFAULT_ACP_BRIDGE_PORT), runtimeActiveTurnIdForUi());
    }
    else
    {
        FlowTracer::log(FlowChannel::Net, QStringLiteral("[acp-bridge] listen failed %1").arg(DEFAULT_ACP_BRIDGE_PORT), runtimeActiveTurnIdForUi());
    }
}

void Widget::handleAcpBridgeClientChanged(bool connected, const QString &reason)
{
    acpBridgeConnected_ = connected;
    FlowTracer::log(FlowChannel::Net,
                    QStringLiteral("[acp-bridge] client %1 (%2)")
                        .arg(connected ? QStringLiteral("connected") : QStringLiteral("disconnected"), reason),
                    runtimeActiveTurnIdForUi());
}

void Widget::sendAcpBridgeResponse(const QJsonObject &payload)
{
    if (!acpBridgeConnected_ || !acpBridgeChannel_) return;
    acpBridgeChannel_->sendToController(payload);
}

void Widget::sendToRemotePeers(const QJsonObject &payload)
{
    if (isHostControlled() && controlChannel_)
    {
        controlChannel_->sendToController(payload);
    }
    if (acpBridgeConnected_ && acpBridgeChannel_)
    {
        acpBridgeChannel_->sendToController(payload);
    }
}

QJsonObject Widget::buildAcpBridgeState() const
{
    const RuntimeState runtimeState = runtimeStateSnapshotForSession();
    const bool hasRuntime = runtimeState.initialized;
    const bool linkMode = hasRuntime ? runtimeState.mode == RuntimeMode::Link : ui_mode == LINK_MODE;
    const SETTINGS settings = hasRuntime ? runtimeState.settings : ui_SETTINGS;
    const APIS stateApis = hasRuntime ? runtimeState.apis : apis;
    QJsonObject state;
    state.insert(QStringLiteral("mode"), hasRuntime ? runtimeModeName(runtimeState.mode)
                                                    : (linkMode ? QStringLiteral("link") : QStringLiteral("local")));
    state.insert(QStringLiteral("state_source"), QStringLiteral("bridge"));
    state.insert(QStringLiteral("bridge_available"), true);
    state.insert(QStringLiteral("direct_runtime"), false);
    if (hasRuntime)
        state.insert(QStringLiteral("runtime_state"), runtimeStateToJson(runtimeState));
    state.insert(QStringLiteral("state"), hasRuntime ? backendLifecycleStateName(runtimeState.backendLifecycle)
                                                     : backendLifecycleStateName(backendLifecycleState_));
    state.insert(QStringLiteral("ready"), hasRuntime ? runtimeState.backendReady
                                                     : runtimeBackendReadyForUi());
    state.insert(QStringLiteral("endpoint"), hasRuntime && !runtimeState.endpoint.isEmpty() ? runtimeState.endpoint : sessionEndpointForHistory());
    state.insert(QStringLiteral("current_model"), hasRuntime && !runtimeState.currentModel.isEmpty() ? runtimeState.currentModel : resolvedModelLabelForUi());
    state.insert(QStringLiteral("current_model_path"), hasRuntime ? runtimeState.currentModelPath : (linkMode ? QString() : ui_SETTINGS.modelpath));
    state.insert(QStringLiteral("backend_choice"), hasRuntime ? runtimeState.backendChoice : (linkMode ? QStringLiteral("link") : ui_device_backend));
    state.insert(QStringLiteral("backend_resolved"), hasRuntime ? runtimeState.backendResolved : (linkMode ? QStringLiteral("remote") : runtimeDeviceBackend_));
    state.insert(QStringLiteral("server_running"), !linkMode && serverManager && serverManager->isRunning());
    state.insert(QStringLiteral("port"), linkMode ? QString() : ui_port);
    state.insert(QStringLiteral("nctx"), settings.nctx);
    state.insert(QStringLiteral("ngl"), settings.ngl);
    state.insert(QStringLiteral("nthread"), settings.nthread);
    state.insert(QStringLiteral("parallel"), settings.hid_parallel);
    state.insert(QStringLiteral("mmproj_path"), settings.mmprojpath);
    state.insert(QStringLiteral("lora_path"), settings.lorapath);
    state.insert(QStringLiteral("api_endpoint"), linkMode ? stateApis.api_endpoint : QString());
    state.insert(QStringLiteral("api_model"), linkMode ? stateApis.api_model : QString());
    state.insert(QStringLiteral("current_task"), hasRuntime ? runtimeState.currentTask : QString());
    state.insert(QStringLiteral("ui_state"), hasRuntime ? conversationModeName(runtimeState.conversationMode)
                                                        : conversationModeName(runtimeConversationModeForUi()));
    state.insert(QStringLiteral("is_run"), runtimeBusyForUi());
    state.insert(QStringLiteral("snapshot"), buildControlSnapshot());
    return state;
}

QJsonArray Widget::buildAcpBridgeModels() const
{
    QJsonArray models;
    const RuntimeState runtimeState = runtimeStateSnapshotForSession();
    const bool hasRuntime = runtimeState.initialized;
    const bool linkMode = hasRuntime ? runtimeState.mode == RuntimeMode::Link : ui_mode == LINK_MODE;
    const SETTINGS settings = hasRuntime ? runtimeState.settings : ui_SETTINGS;
    const APIS stateApis = hasRuntime ? runtimeState.apis : apis;
    if (linkMode)
    {
        if (!stateApis.api_model.isEmpty())
        {
            QJsonObject model;
            model.insert(QStringLiteral("id"), stateApis.api_model);
            model.insert(QStringLiteral("object"), QStringLiteral("model"));
            model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-bridge"));
            model.insert(QStringLiteral("current"), true);
            model.insert(QStringLiteral("source"), QStringLiteral("remote"));
            model.insert(QStringLiteral("endpoint"), stateApis.api_endpoint);
            models.append(model);
        }
        return models;
    }

    QStringList paths;
    const QString llmRoot = QDir(applicationDirPath).filePath(QStringLiteral("EVA_MODELS/llm"));
    if (QDir(llmRoot).exists())
    {
        QDirIterator it(llmRoot, QStringList() << QStringLiteral("*.gguf"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            paths.append(QFileInfo(it.next()).absoluteFilePath());
        }
    }
    if (!settings.modelpath.isEmpty() && QFileInfo::exists(settings.modelpath) && !paths.contains(settings.modelpath))
    {
        paths.append(settings.modelpath);
    }
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right)
    {
        return left.toLower() < right.toLower();
    });
    for (const QString &path : paths)
    {
        QFileInfo info(path);
        QJsonObject model;
        model.insert(QStringLiteral("id"), info.fileName());
        model.insert(QStringLiteral("object"), QStringLiteral("model"));
        model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-bridge"));
        model.insert(QStringLiteral("current"), path == settings.modelpath);
        model.insert(QStringLiteral("source"), QStringLiteral("local"));
        model.insert(QStringLiteral("path"), path);
        models.append(model);
    }
    return models;
}

bool Widget::applyAcpBridgeLoad(const QJsonObject &payload, QString *errorMessage)
{
    const QString mode = payload.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (mode == QStringLiteral("link"))
    {
        const QString endpoint = payload.value(QStringLiteral("api_endpoint")).toString().trimmed();
        const QString model = payload.value(QStringLiteral("api_model")).toString().trimmed();
        if (endpoint.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Remote api_endpoint is required.");
            return false;
        }
        if (api_endpoint_LineEdit) api_endpoint_LineEdit->setText(endpoint);
        if (api_key_LineEdit) api_key_LineEdit->setText(payload.value(QStringLiteral("api_key")).toString().trimmed());
        if (api_model_LineEdit) api_model_LineEdit->setText(model);
        set_api();
        return true;
    }

    QString modelPath = payload.value(QStringLiteral("model_path")).toString().trimmed();
    if (modelPath.isEmpty()) modelPath = ui_SETTINGS.modelpath;
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Local model path is missing or invalid.");
        return false;
    }

    if (payload.contains(QStringLiteral("nthread"))) ui_SETTINGS.nthread = qMax(1, payload.value(QStringLiteral("nthread")).toInt(ui_SETTINGS.nthread));
    if (payload.contains(QStringLiteral("nctx"))) ui_SETTINGS.nctx = qMax(1, payload.value(QStringLiteral("nctx")).toInt(ui_SETTINGS.nctx));
    if (payload.contains(QStringLiteral("ngl"))) ui_SETTINGS.ngl = payload.value(QStringLiteral("ngl")).toInt(ui_SETTINGS.ngl);
    if (payload.contains(QStringLiteral("parallel"))) ui_SETTINGS.hid_parallel = qMax(1, payload.value(QStringLiteral("parallel")).toInt(ui_SETTINGS.hid_parallel));
    if (payload.contains(QStringLiteral("port"))) ui_port = payload.value(QStringLiteral("port")).toString().trimmed();
    if (payload.contains(QStringLiteral("backend")))
    {
        ui_device_backend = payload.value(QStringLiteral("backend")).toString().trimmed().toLower();
        if (ui_device_backend.isEmpty()) ui_device_backend = QStringLiteral("auto");
    }
    DeviceManager::setUserChoice(ui_device_backend);
    currentpath = historypath = modelPath;
    ui_SETTINGS.modelpath = modelPath;
    projectRuntimeLocalLoadingState();
    if (proxyServer_) proxyServer_->setBackendAvailable(false);
    slotCtxMax_ = 0;
    if (runtime_)
    {
        RuntimeLoadLocalCommand command;
        command.modelPath = ui_SETTINGS.modelpath;
        command.mmprojPath = ui_SETTINGS.mmprojpath;
        command.loraPath = ui_SETTINGS.lorapath;
        command.backendChoice = ui_device_backend;
        command.port = ui_port;
        command.settings = ui_SETTINGS;
        QString runtimeError;
        if (!runtime_->loadLocal(command, &runtimeError))
        {
            qWarning().noquote() << QStringLiteral("EvaRuntime loadLocal failed:") << runtimeError;
        }
    }
    ui_state_loading();
    ensureLocalServer(false, true);
    return true;
}

bool Widget::resetAcpBridgeConversation(QString *errorMessage)
{
    if (runtimeBusyForUi())
    {
        if (errorMessage) *errorMessage = jtr("control command blocked");
        return false;
    }
    on_reset_clicked();
    broadcastControlSnapshot();
    return true;
}

bool Widget::sendBridgeText(const QString &text, QString *errorMessage)
{
    if (runtimeBusyForUi())
    {
        if (errorMessage) *errorMessage = jtr("control command blocked");
        return false;
    }
    if (text.trimmed().isEmpty())
    {
        if (errorMessage) *errorMessage = jtr("control send missing");
        return false;
    }
    struct DraftBackup
    {
        QString text;
        QStringList attachments;
    };
    DraftBackup backup;
    if (ui && ui->input && ui->input->textEdit)
    {
        backup.text = ui->input->textEdit->toPlainText();
        QStringList paths = ui->input->imageFilePaths();
        paths.append(ui->input->documentFilePaths());
        paths.append(ui->input->wavFilePaths());
        backup.attachments = paths;
    }
    if (ui && ui->input && ui->input->textEdit)
    {
        ui->input->textEdit->setPlainText(text);
        ui->input->clearThumbnails();
        on_send_clicked();
        if (!backup.text.isEmpty() || !backup.attachments.isEmpty())
        {
            ui->input->textEdit->setPlainText(backup.text);
            ui->input->clearThumbnails();
            if (!backup.attachments.isEmpty())
                ui->input->addFiles(backup.attachments);
        }
        return true;
    }
    if (errorMessage) *errorMessage = QStringLiteral("Input editor is unavailable.");
    return false;
}

void Widget::handleAcpBridgeCommand(const QJsonObject &payload)
{
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("command")) return;
    const QString name = payload.value(QStringLiteral("name")).toString();
    const QString requestId = payload.value(QStringLiteral("request_id")).toString();

    QJsonObject response;
    response.insert(QStringLiteral("type"), QStringLiteral("bridge_response"));
    response.insert(QStringLiteral("name"), name);
    if (!requestId.isEmpty()) response.insert(QStringLiteral("request_id"), requestId);

    if (name == QStringLiteral("bridge_get_state"))
    {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("state"), buildAcpBridgeState());
        sendAcpBridgeResponse(response);
        return;
    }
    if (name == QStringLiteral("bridge_list_models"))
    {
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("models"), buildAcpBridgeModels());
        sendAcpBridgeResponse(response);
        return;
    }
    if (name == QStringLiteral("bridge_apply_load"))
    {
        QString errorMessage;
        const bool ok = applyAcpBridgeLoad(payload, &errorMessage);
        response.insert(QStringLiteral("ok"), ok);
        if (ok)
        {
            response.insert(QStringLiteral("accepted"), true);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
            response.insert(QStringLiteral("models"), buildAcpBridgeModels());
        }
        else
        {
            response.insert(QStringLiteral("error"), errorMessage);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
        }
        sendAcpBridgeResponse(response);
        return;
    }
    if (name == QStringLiteral("bridge_reset"))
    {
        QString errorMessage;
        const bool ok = resetAcpBridgeConversation(&errorMessage);
        response.insert(QStringLiteral("ok"), ok);
        if (ok)
        {
            response.insert(QStringLiteral("accepted"), true);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
        }
        else
        {
            response.insert(QStringLiteral("error"), errorMessage);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
        }
        sendAcpBridgeResponse(response);
        return;
    }
    if (name == QStringLiteral("bridge_stop"))
    {
        if (runtimeBusyForUi())
        {
            reflash_state(jtr("control stop"), SIGNAL_SIGNAL);
            emit ui2net_stop(true);
            emit ui2tool_cancelActive();
        }
        response.insert(QStringLiteral("ok"), true);
        response.insert(QStringLiteral("accepted"), true);
        response.insert(QStringLiteral("state"), buildAcpBridgeState());
        sendAcpBridgeResponse(response);
        return;
    }
    if (name == QStringLiteral("bridge_send"))
    {
        QString errorMessage;
        const bool ok = sendBridgeText(payload.value(QStringLiteral("text")).toString(), &errorMessage);
        response.insert(QStringLiteral("ok"), ok);
        if (ok)
        {
            response.insert(QStringLiteral("accepted"), true);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
        }
        else
        {
            response.insert(QStringLiteral("error"), errorMessage);
            response.insert(QStringLiteral("state"), buildAcpBridgeState());
        }
        sendAcpBridgeResponse(response);
        return;
    }

    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), QStringLiteral("Unknown bridge command."));
    sendAcpBridgeResponse(response);
}

void Widget::setControlHostEnabled(bool enabled)
{
    if (enabled)
    {
        setupControlChannel();
        if (!controlChannel_)
        {
            reflash_state(jtr("control listen fail"), WRONG_SIGNAL);
            return;
        }
        if (controlHostAllowed_)
        {
            // Already hosting; nothing to change
            return;
        }
        if (controlChannel_->startHost(static_cast<quint16>(DEFAULT_CONTROL_PORT)))
        {
            controlHostAllowed_ = true;
            reflash_state(jtr("control listen ok").arg(QString::number(DEFAULT_CONTROL_PORT)), SIGNAL_SIGNAL);
        }
        else
        {
            controlHostAllowed_ = false;
            reflash_state(jtr("control listen fail"), WRONG_SIGNAL);
        }
        return;
    }

    if (!controlHostAllowed_)
    {
        return;
    }
    controlHostAllowed_ = false;
    if (!controlChannel_) return;
    if (isHostControlled())
    {
        QJsonObject bye;
        bye.insert(QStringLiteral("type"), QStringLiteral("released"));
        controlChannel_->sendToController(bye);
        controlHost_.active = false;
        controlHost_.peer.clear();
    }
    controlChannel_->stopHost();
}

QJsonObject Widget::buildControlSnapshot() const
{
    QJsonObject snap;
    if (ui && ui->output) snap.insert(QStringLiteral("output"), ui->output->toPlainText());
    if (ui && ui->state) snap.insert(QStringLiteral("state_log"), ui->state->toPlainText());

    const RuntimeState runtimeState = runtimeStateSnapshotForSession();
    const bool hasRuntime = runtimeState.initialized;
    const int cap = hasRuntime ? runtimeState.kvCapacity : resolvedContextLimitForUi();
    const bool capKnown = cap > 0;
    int used = qMax(0, hasRuntime ? runtimeState.kvUsed : kvUsed_);
    if (capKnown && used > cap) used = cap;
    int percent = hasRuntime ? runtimeState.kvPercent : ((capKnown && cap > 0) ? int(qRound(100.0 * double(used) / double(cap))) : 0);
    if (capKnown && used > 0 && percent == 0) percent = 1;

    const QString modelLabel = hasRuntime && !runtimeState.currentModel.isEmpty() ? runtimeState.currentModel : resolvedModelLabelForUi();
    if (hasRuntime)
        snap.insert(QStringLiteral("runtime_state"), runtimeStateToJson(runtimeState));

    snap.insert(QStringLiteral("kv_used"), used);
    snap.insert(QStringLiteral("kv_cap"), cap);
    snap.insert(QStringLiteral("kv_percent"), percent);
    snap.insert(QStringLiteral("ui_state"), hasRuntime ? conversationModeName(runtimeState.conversationMode)
                                                       : conversationModeName(runtimeConversationModeForUi()));
    snap.insert(QStringLiteral("is_run"), runtimeBusyForUi());
    const quint64 turnId = runtimeActiveTurnIdForUi();
    snap.insert(QStringLiteral("turn_id"), static_cast<qint64>(turnId));
    snap.insert(QStringLiteral("active_turn_id"), static_cast<qint64>(turnId));
    snap.insert(QStringLiteral("phase"), hasRuntime ? runtimePhaseName(runtimeState.phase) : controlUiPhase_);
    snap.insert(QStringLiteral("current_task"), hasRuntime ? runtimeState.currentTask : QString());
    snap.insert(QStringLiteral("message_count"), hasRuntime ? runtimeState.messageCount : ui_messagesArray.size());
    snap.insert(QStringLiteral("record_count"), recordEntries_.size());
    snap.insert(QStringLiteral("title"), windowTitle());
    snap.insert(QStringLiteral("mode"), hasRuntime ? runtimeModeName(runtimeState.mode)
                                                   : runtimeModeName(runtimeModeForUi()));
    snap.insert(QStringLiteral("model_name"), modelLabel);
    snap.insert(QStringLiteral("endpoint"), hasRuntime && !runtimeState.endpoint.isEmpty() ? runtimeState.endpoint : sessionEndpointForHistory());
    snap.insert(QStringLiteral("monitor"), buildControlMonitor());
    snap.insert(QStringLiteral("records"), buildControlRecords());
    return snap;
}

QJsonObject Widget::buildControlMonitor() const
{
    QJsonObject mon;
    if (ui && ui->cpu_bar)
    {
        mon.insert(QStringLiteral("cpu"), ui->cpu_bar->value());
        mon.insert(QStringLiteral("cpu2"), ui->cpu_bar->m_secondValue);
    }
    if (ui && ui->mem_bar)
    {
        mon.insert(QStringLiteral("mem"), ui->mem_bar->value());
        mon.insert(QStringLiteral("mem2"), ui->mem_bar->m_secondValue);
    }
    if (ui && ui->vram_bar)
    {
        mon.insert(QStringLiteral("vram"), ui->vram_bar->value());
        mon.insert(QStringLiteral("vram2"), ui->vram_bar->m_secondValue);
    }
    if (ui && ui->vcore_bar)
    {
        mon.insert(QStringLiteral("vcore"), ui->vcore_bar->value());
    }
    return mon;
}

QJsonArray Widget::buildControlRecords() const
{
    QJsonArray arr;
    for (const RecordEntry &e : recordEntries_)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), static_cast<int>(e.role));
        obj.insert(QStringLiteral("text"), e.text);
        if (e.role == RecordRole::Tool && !e.toolName.isEmpty())
        {
            obj.insert(QStringLiteral("tool"), e.toolName);
        }
        if (e.msgIndex >= 0)
        {
            obj.insert(QStringLiteral("msg_index"), e.msgIndex);
        }
        arr.append(obj);
    }
    return arr;
}

void Widget::broadcastControlSnapshot()
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    const quint64 turnId = runtimeActiveTurnIdForUi();
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("snapshot"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(turnId));
    payload.insert(QStringLiteral("snapshot"), buildControlSnapshot());
    sendToRemotePeers(payload);
    const int recordCount = recordEntries_.size();
    const int outputLen = (ui && ui->output) ? ui->output->toPlainText().size() : 0;
    const int stateLen = (ui && ui->state) ? ui->state->toPlainText().size() : 0;
    FlowTracer::log(FlowChannel::Session,
                    QStringLiteral("[control] host snapshot push records=%1 output=%2 state=%3")
                        .arg(recordCount)
                        .arg(outputLen)
                        .arg(stateLen),
                    turnId);
}

void Widget::broadcastControlMonitor()
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("monitor"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("monitor"), buildControlMonitor());
    sendToRemotePeers(payload);
}

void Widget::broadcastControlRecordClear()
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("record_clear"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), controlPhaseForUi());
    sendToRemotePeers(payload);
}

void Widget::broadcastControlRecordAdd(RecordRole role, const QString &toolName)
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("record_add"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), controlPhaseForUi());
    payload.insert(QStringLiteral("index"), qMax(0, recordEntries_.size() - 1));
    payload.insert(QStringLiteral("record_count"), recordEntries_.size());
    payload.insert(QStringLiteral("role"), static_cast<int>(role));
    if (!toolName.isEmpty()) payload.insert(QStringLiteral("tool"), toolName);
    sendToRemotePeers(payload);
}

void Widget::broadcastControlRecordUpdate(int index, const QString &deltaText)
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("record_update"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), controlPhaseForUi());
    payload.insert(QStringLiteral("index"), index);
    payload.insert(QStringLiteral("delta"), deltaText);
    if (index >= 0 && index < recordEntries_.size())
    {
        payload.insert(QStringLiteral("text"), recordEntries_[index].text);
        payload.insert(QStringLiteral("role"), static_cast<int>(recordEntries_[index].role));
        if (!recordEntries_[index].toolName.isEmpty())
            payload.insert(QStringLiteral("tool"), recordEntries_[index].toolName);
        if (recordEntries_[index].msgIndex >= 0)
            payload.insert(QStringLiteral("msg_index"), recordEntries_[index].msgIndex);
    }
    sendToRemotePeers(payload);
}

void Widget::broadcastControlOutput(const QString &result, bool isStream, const QColor &color, const QString &roleHint, int thinkActiveFlag)
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("output"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), controlPhaseForUi());
    payload.insert(QStringLiteral("text"), result);
    payload.insert(QStringLiteral("stream"), isStream);
    payload.insert(QStringLiteral("color"), color.name(QColor::HexArgb));
    if (!roleHint.isEmpty()) payload.insert(QStringLiteral("role"), roleHint);
    if (thinkActiveFlag >= 0) payload.insert(QStringLiteral("think_active"), thinkActiveFlag);
    sendToRemotePeers(payload);
    // FlowTracer::log(FlowChannel::Session,
    //                 QStringLiteral("[control] host stream role=%1 stream=%2 think=%3 text=%4")
    //                     .arg(roleHint.isEmpty() ? QStringLiteral("-") : roleHint)
    //                     .arg(isStream ? QStringLiteral("yes") : QStringLiteral("no"))
    //                     .arg(thinkActiveFlag)
    //                     .arg(previewForLog(result)),
    //                 runtimeActiveTurnIdForUi());
}

void Widget::broadcastControlState(const QString &stateString, SIGNAL_STATE level)
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("state_log"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), controlPhaseForUi());
    payload.insert(QStringLiteral("text"), stateString);
    payload.insert(QStringLiteral("level"), static_cast<int>(level));
    sendToRemotePeers(payload);
}

void Widget::appendControlStateLog(const QString &text, SIGNAL_STATE level, const QString &prefix, bool mirrorToModelInfo)
{
    QString line = text;
    if (level != MATRIX_SIGNAL)
    {
        line.replace("\n", "\\n");
        line.replace("\r", "\\r");
    }
    const QString composed = prefix.isEmpty() ? line : prefix + QStringLiteral(" ") + line;
    QTextCharFormat fmt;
    fmt.setForeground(themeStateColor(level));
    appendStateLine(composed, fmt);
    if (mirrorToModelInfo)
    {
        logControlInfoToModelInfo(composed);
    }
}

void Widget::logControlInfoToModelInfo(const QString &line)
{
    const QString withBreak = line.endsWith(QChar('\n')) ? line : line + QStringLiteral("\n");
    emit ui2expend_llamalog(withBreak);
}

void Widget::broadcastControlKv(int used, int cap, int percent)
{
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("kv"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("used"), used);
    payload.insert(QStringLiteral("cap"), cap);
    payload.insert(QStringLiteral("percent"), percent);
    sendToRemotePeers(payload);
}

void Widget::broadcastControlUiPhase(const QString &phase)
{
    controlUiPhase_ = phase;
    if (!isHostControlled() && !acpBridgeConnected_) return;
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("ui_state"));
    payload.insert(QStringLiteral("event_seq"), static_cast<qint64>(++controlEventSeq_));
    payload.insert(QStringLiteral("timestamp_ms"), static_cast<qint64>(QDateTime::currentMSecsSinceEpoch()));
    payload.insert(QStringLiteral("turn_id"), static_cast<qint64>(runtimeActiveTurnIdForUi()));
    payload.insert(QStringLiteral("phase"), phase);
    payload.insert(QStringLiteral("is_run"), runtimeBusyForUi());
    payload.insert(QStringLiteral("state"), conversationModeName(runtimeConversationModeForUi()));
    payload.insert(QStringLiteral("snapshot"), buildControlSnapshot());
    sendToRemotePeers(payload);
}

void Widget::applyControlMonitor(const QJsonObject &mon)
{
    if (mon.isEmpty()) return;
    if (!ui) return;
    if (ui->cpu_bar)
    {
        ui->cpu_bar->setValue(mon.value(QStringLiteral("cpu")).toInt(ui->cpu_bar->value()));
        ui->cpu_bar->setSecondValue(mon.value(QStringLiteral("cpu2")).toInt(ui->cpu_bar->m_secondValue));
    }
    if (ui->mem_bar)
    {
        ui->mem_bar->setValue(mon.value(QStringLiteral("mem")).toInt(ui->mem_bar->value()));
        ui->mem_bar->setSecondValue(mon.value(QStringLiteral("mem2")).toInt(ui->mem_bar->m_secondValue));
    }
    if (ui->vram_bar)
    {
        ui->vram_bar->setValue(mon.value(QStringLiteral("vram")).toInt(ui->vram_bar->value()));
        ui->vram_bar->setSecondValue(mon.value(QStringLiteral("vram2")).toInt(ui->vram_bar->m_secondValue));
    }
    if (ui->vcore_bar)
    {
        ui->vcore_bar->setValue(mon.value(QStringLiteral("vcore")).toInt(ui->vcore_bar->value()));
    }
}

void Widget::applyControlRecordClear()
{
    recordClear();
}

void Widget::applyControlRecordAdd(RecordRole role, const QString &toolName)
{
    if (role == RecordRole::Tool) lastToolCallName_ = toolName;
    recordCreate(role, toolName);
}

void Widget::applyControlRecordUpdate(int index, const QString &deltaText)
{
    recordAppendText(index, deltaText);
}

void Widget::handleControlHostClientChanged(bool connected, const QString &reason)
{
    Q_UNUSED(reason);
    if (!connected)
    {
        controlHost_.active = false;
        controlHost_.peer.clear();
        appendControlStateLog(jtr("control disconnected"), SIGNAL_SIGNAL, jtr("control peer prefix"), true);
        broadcastControlState(jtr("control disconnected"), SIGNAL_SIGNAL);
        return;
    }
    controlHost_.peer = controlChannel_ ? controlChannel_->hostPeer() : QString();
}

void Widget::handleControlHostCommand(const QJsonObject &payload)
{
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello"))
    {
        if (controlHost_.active)
        {
            QJsonObject busy;
            busy.insert(QStringLiteral("type"), QStringLiteral("reject"));
            busy.insert(QStringLiteral("reason"), QStringLiteral("busy"));
            controlChannel_->sendToController(busy);
            return;
        }
        controlHost_.active = true;
        controlHost_.peer = controlChannel_ ? controlChannel_->hostPeer() : QString();
        reflash_state(jtr("control connected").arg(controlHost_.peer), SIGNAL_SIGNAL);
        const QString modeLabel = runtimeModeForUi() == RuntimeMode::Link ? QStringLiteral("链接") : QStringLiteral("本地");
        const QString stateLabel = runtimeConversationModeForUi() == ConversationMode::Chat ? QStringLiteral("对话") : QStringLiteral("补完");
        const QString runLabel = runtimeBusyForUi() ? QStringLiteral("推理中") : QStringLiteral("空闲");
        const QString modelLabel = resolvedModelLabelForUi();
        const int cap = resolvedContextLimitForUi();
        const bool capKnown = cap > 0;
        int used = qMax(0, runtimeKvUsedForUi());
        if (capKnown && cap > 0 && used > cap) used = cap;
        const int percent = (capKnown && cap > 0) ? int(qRound(100.0 * double(used) / double(cap))) : 0;
        const QString capLabel = resolvedContextLabelForUi();
        const QString percentLabel = capKnown ? QString::number(percent) : QStringLiteral("-");
        QString infoLine = QStringLiteral("控制端 %1 已接入 | 模式:%2 | 状态:%3 | 运行:%4 | 模型:%5 | KV:%6/%7(%8%)")
                               .arg(controlHost_.peer.isEmpty() ? QStringLiteral("-") : controlHost_.peer)
                               .arg(modeLabel)
                               .arg(stateLabel)
                               .arg(runLabel)
                               .arg(modelLabel)
                               .arg(used)
                               .arg(capLabel)
                               .arg(percentLabel);
        const QString endpoint = sessionEndpointForHistory();
        if (!endpoint.isEmpty()) infoLine += QStringLiteral(" | 端点:") + endpoint;
        appendControlStateLog(infoLine, SIGNAL_SIGNAL, jtr("control peer prefix"), true);
        QJsonObject ack;
        ack.insert(QStringLiteral("type"), QStringLiteral("hello_ack"));
        ack.insert(QStringLiteral("snapshot"), buildControlSnapshot());
        ack.insert(QStringLiteral("peer"), QHostInfo::localHostName());
        controlChannel_->sendToController(ack);
        return;
    }
    if (!isHostControlled()) return;
    if (type != QStringLiteral("command")) return;
    const QString name = payload.value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("release"))
    {
        controlHost_.active = false;
        reflash_state(jtr("control host exit"), SIGNAL_SIGNAL);
        QJsonObject bye;
        bye.insert(QStringLiteral("type"), QStringLiteral("released"));
        controlChannel_->sendToController(bye);
        return;
    }
    if (name == QStringLiteral("stop"))
    {
        if (runtimeBusyForUi())
        {
            reflash_state(jtr("control stop"), SIGNAL_SIGNAL);
            emit ui2net_stop(true);
            emit ui2tool_cancelActive();
        }
        return;
    }
    if (name == QStringLiteral("reset"))
    {
        reflash_state(jtr("control reset"), SIGNAL_SIGNAL);
        on_reset_clicked();
        broadcastControlSnapshot();
        return;
    }
    if (name == QStringLiteral("send"))
    {
        QString errorMessage;
        if (!sendBridgeText(payload.value(QStringLiteral("text")).toString(), &errorMessage))
        {
            QJsonObject warn;
            warn.insert(QStringLiteral("type"), QStringLiteral("state_log"));
            warn.insert(QStringLiteral("text"), errorMessage);
            warn.insert(QStringLiteral("level"), static_cast<int>(WRONG_SIGNAL));
            controlChannel_->sendToController(warn);
        }
        return;
    }
}

void Widget::handleControlControllerEvent(const QJsonObject &payload)
{
    const QString type = payload.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("reject"))
    {
        reflash_state(jtr("control busy"), WRONG_SIGNAL);
        releaseControl(false);
        return;
    }
    if (type == QStringLiteral("hello_ack") || type == QStringLiteral("snapshot"))
    {
        controlAwaitingHello_ = false;
        if (payload.contains(QStringLiteral("peer"))) controlClient_.peer = payload.value(QStringLiteral("peer")).toString();
        const QJsonObject snap = payload.value(QStringLiteral("snapshot")).toObject();
        applyControlSnapshot(snap);
        reflash_state(jtr("control snapshot applied"), SIGNAL_SIGNAL);
        const QString modeLabel = (snap.value(QStringLiteral("mode")).toString() == QStringLiteral("link")) ? QStringLiteral("链接") : QStringLiteral("本地");
        const QString stateLabel = (snap.value(QStringLiteral("ui_state")).toString() == QStringLiteral("complete")) ? QStringLiteral("补完") : QStringLiteral("对话");
        const QString runLabel = snap.value(QStringLiteral("is_run")).toBool(false) ? QStringLiteral("推理中") : QStringLiteral("空闲");
        const int cap = snap.value(QStringLiteral("kv_cap")).toInt(0);
        bool capKnown = cap > 0;
        int used = snap.value(QStringLiteral("kv_used")).toInt(0);
        if (capKnown && cap > 0 && used > cap) used = cap;
        const int percent = (capKnown && cap > 0) ? int(qRound(100.0 * double(used) / double(cap))) : 0;
        QString modelLabel = snap.value(QStringLiteral("model_name")).toString();
        if (modelLabel.isEmpty()) modelLabel = resolvedModelLabelForUi();
        if (modelLabel.isEmpty()) modelLabel = QStringLiteral("-");
        const QString capLabel = capKnown ? QString::number(cap) : QStringLiteral("未知");
        const QString percentLabel = capKnown ? QString::number(percent) : QStringLiteral("-");
        const QString endpoint = snap.value(QStringLiteral("endpoint")).toString();
        QString infoLine = QStringLiteral("目标 %1 | 模式:%2 | 状态:%3 | 运行:%4 | 模型:%5 | KV:%6/%7(%8%)")
                               .arg(controlClient_.peer.isEmpty() ? QStringLiteral("-") : controlClient_.peer)
                               .arg(modeLabel)
                               .arg(stateLabel)
                               .arg(runLabel)
                               .arg(modelLabel)
                               .arg(used)
                               .arg(capLabel)
                               .arg(percentLabel);
        if (!endpoint.isEmpty()) infoLine += QStringLiteral(" | 端点:") + endpoint;
        appendControlStateLog(infoLine, SIGNAL_SIGNAL, jtr("control peer prefix"), true);
        applyControlUiLock();
        return;
    }
    if (!isControllerActive()) return;
    if (type == QStringLiteral("output"))
    {
        const QString text = payload.value(QStringLiteral("text")).toString();
        const bool stream = payload.value(QStringLiteral("stream")).toBool();
        QColor c(themeTextPrimary());
        const QString cstr = payload.value(QStringLiteral("color")).toString();
        if (!cstr.isEmpty())
        {
            QColor parsed(cstr);
            if (parsed.isValid()) c = parsed;
        }
        const QString role = payload.value(QStringLiteral("role")).toString();
        const int thinkFlag = payload.value(QStringLiteral("think_active")).toInt(-1);
        if (!role.isEmpty()) controlStreamRole_ = role;
        FlowTracer::log(FlowChannel::Session,
                        QStringLiteral("[control] controller recv role=%1 stream=%2 think=%3 text=%4")
                            .arg(role.isEmpty() ? QStringLiteral("-") : role)
                            .arg(stream ? QStringLiteral("yes") : QStringLiteral("no"))
                            .arg(thinkFlag)
                            .arg(previewForLog(text)),
                        runtimeActiveTurnIdForUi());
        // Mirror host output verbatim to avoid re-parsing <think> on controller side
        if (stream) flushPendingStream();
        QString plain = text;
        plain.replace(QString(DEFAULT_THINK_BEGIN), QString());
        plain.replace(QString(DEFAULT_THINK_END), QString());
        output_scroll(plain, c);
        if (role == QStringLiteral("think"))
        {
            controlThinkActive_ = (thinkFlag != 0);
        }
        else if (!role.isEmpty() || thinkFlag == 0)
        {
            controlThinkActive_ = false;
        }
        return;
    }
    if (type == QStringLiteral("state_log"))
    {
        const QString text = payload.value(QStringLiteral("text")).toString();
        const int lv = payload.value(QStringLiteral("level")).toInt(static_cast<int>(USUAL_SIGNAL));
        appendControlStateLog(text, static_cast<SIGNAL_STATE>(lv), jtr("control peer prefix"));
        return;
    }
    if (type == QStringLiteral("kv"))
    {
        kvUsed_ = payload.value(QStringLiteral("used")).toInt(kvUsed_);
        slotCtxMax_ = payload.value(QStringLiteral("cap")).toInt(slotCtxMax_);
        updateKvBarUi();
        return;
    }
    if (type == QStringLiteral("monitor"))
    {
        const QJsonObject mon = payload.value(QStringLiteral("monitor")).toObject();
        applyControlMonitor(mon);
        return;
    }
    if (type == QStringLiteral("record_clear"))
    {
        applyControlRecordClear();
        return;
    }
    if (type == QStringLiteral("record_add"))
    {
        const int roleInt = payload.value(QStringLiteral("role")).toInt(static_cast<int>(RecordRole::System));
        const QString toolName = payload.value(QStringLiteral("tool")).toString();
        applyControlRecordAdd(static_cast<RecordRole>(roleInt), toolName);
        return;
    }
    if (type == QStringLiteral("record_update"))
    {
        const int idx = payload.value(QStringLiteral("index")).toInt(-1);
        const QString delta = payload.value(QStringLiteral("delta")).toString();
        applyControlRecordUpdate(idx, delta);
        return;
    }
    if (type == QStringLiteral("ui_state"))
    {
        controlClient_.remoteRunning = payload.value(QStringLiteral("is_run")).toBool(controlClient_.remoteRunning);
        const QString stateStr = payload.value(QStringLiteral("state")).toString();
        controlClient_.remoteUiState = (stateStr == QStringLiteral("complete")) ? COMPLETE_STATE : CHAT_STATE;
        applyControlUiLock();
        return;
    }
    if (type == QStringLiteral("released"))
    {
        appendControlStateLog(jtr("control disconnected"), SIGNAL_SIGNAL, jtr("control peer prefix"), true);
        reflash_state(jtr("control disconnected"), SIGNAL_SIGNAL);
        releaseControl(false);
        return;
    }
}

void Widget::handleControlControllerState(ControlChannel::ControllerState state, const QString &reason)
{
    controlClient_.state = state;
    if (state == ControlChannel::ControllerState::Connected)
    {
        controlAwaitingHello_ = true;
        QJsonObject hello;
        hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
        hello.insert(QStringLiteral("token"), controlToken_);
        hello.insert(QStringLiteral("peer"), QHostInfo::localHostName());
        if (controlChannel_) controlChannel_->sendToHost(hello);
    }
    else if (state == ControlChannel::ControllerState::Idle)
    {
        if (linkProfile_ == LinkProfile::Control && !reason.isEmpty())
        {
            if (reason == QStringLiteral("refused"))
            {
                reflash_state(jtr("control refused disabled"), WRONG_SIGNAL);
            }
            else
            {
                reflash_state(jtr("control refused generic"), WRONG_SIGNAL);
            }
        }
        if (linkProfile_ == LinkProfile::Control) releaseControl(false);
    }
}

void Widget::applyControlSnapshot(const QJsonObject &snap)
{
    if (!ui) return;
    controlThinkActive_ = false;
    controlStreamRole_.clear();
    recordClear();
    if (streamFlushTimer_ && streamFlushTimer_->isActive()) streamFlushTimer_->stop();
    streamPending_.clear();
    streamPendingChars_ = 0;
    temp_assistant_history.clear();
    pendingAssistantHeaderReset_ = false;
    turnThinkActive_ = false;
    turnThinkHeaderPrinted_ = false;
    turnAssistantHeaderPrinted_ = false;
    currentThinkIndex_ = -1;
    currentAssistantIndex_ = -1;
    is_stop_output_scroll = false;
    if (ui->output) resetOutputDocument();
    if (ui->state) resetStateDocument();

    const auto renderRoleLabel = [&](RecordRole role) -> QString
    {
        switch (role)
        {
        case RecordRole::System: return QStringLiteral("system");
        case RecordRole::User: return QStringLiteral("user");
        case RecordRole::Assistant: return QStringLiteral("assistant");
        case RecordRole::Think: return QStringLiteral("think");
        case RecordRole::Tool: return QStringLiteral("tool");
        case RecordRole::Compact: return QStringLiteral("compact");
        }
        return QString();
    };
    const auto renderColor = [&](RecordRole role) -> QColor
    {
        if (role == RecordRole::Think) return themeThinkColor();
        if (role == RecordRole::Tool) return themeStateColor(TOOL_SIGNAL);
        if (role == RecordRole::Compact) return themeStateColor(EVA_SIGNAL);
        return themeTextPrimary();
    };

    bool renderedFromRecords = false;
    const QJsonArray recs = snap.value(QStringLiteral("records")).toArray();
    if (ui->output && !recs.isEmpty())
    {
        for (const auto &v : recs)
        {
            if (!v.isObject()) continue;
            const QJsonObject ro = v.toObject();
            const int roleInt = ro.value(QStringLiteral("role")).toInt(static_cast<int>(RecordRole::System));
            const QString text = ro.value(QStringLiteral("text")).toString();
            const QString toolName = ro.value(QStringLiteral("tool")).toString();
            const int msgIndex = ro.value(QStringLiteral("msg_index")).toInt(-1);
            const RecordRole role = static_cast<RecordRole>(roleInt);
            const QString header = renderRoleLabel(role);
            const int idx = recordCreate(role, toolName);
            if (!header.isEmpty()) appendRoleHeader(header);
            if (!text.isEmpty()) reflash_output(text, 0, renderColor(role));
            recordAppendText(idx, text);
            recordEntries_[idx].msgIndex = msgIndex;
            if (role == RecordRole::System) lastSystemRecordIndex_ = idx;
            renderedFromRecords = true;
        }
    }
    if (!renderedFromRecords && ui->output)
    {
        ui->output->setPlainText(snap.value(QStringLiteral("output")).toString());
    }
    if (ui->state) ui->state->setPlainText(snap.value(QStringLiteral("state_log")).toString());
    kvUsed_ = snap.value(QStringLiteral("kv_used")).toInt(kvUsed_);
    slotCtxMax_ = snap.value(QStringLiteral("kv_cap")).toInt(slotCtxMax_);
    updateKvBarUi();
    if (renderedFromRecords && ui->recordBar)
    {
        for (int i = 0; i < recordEntries_.size(); ++i)
        {
            QString tip = recordEntries_[i].text;
            if (tip.size() > 600) tip = tip.left(600) + "...";
            ui->recordBar->updateNode(i, tip);
        }
    }

    applyControlMonitor(snap.value(QStringLiteral("monitor")).toObject());
    FlowTracer::log(FlowChannel::Session,
                    QStringLiteral("[control] controller snapshot applied records=%1 rendered_from_records=%2 output=%3")
                        .arg(recs.size())
                        .arg(renderedFromRecords ? 1 : 0)
                        .arg((ui && ui->output) ? ui->output->toPlainText().size() : 0),
                    runtimeActiveTurnIdForUi());
    const QString stateStr = snap.value(QStringLiteral("ui_state")).toString();
    controlClient_.remoteUiState = (stateStr == QStringLiteral("complete")) ? COMPLETE_STATE : CHAT_STATE;
    controlClient_.remoteRunning = snap.value(QStringLiteral("is_run")).toBool(false);
    const QString title = snap.value(QStringLiteral("title")).toString();
    if (!title.isEmpty())
    {
        this->setWindowTitle(title);
        trayIcon->setToolTip(title);
    }
}

void Widget::applyControlUiLock()
{
    if (!ui) return;
    blockLocalMonitor_ = isControllerActive();
    const QString dateLabel = jtr("date");
    const QString releaseLabel = jtr("control release");
    if (isControllerActive())
    {
        const bool canSend = !controlClient_.remoteRunning;
        ui->send->setEnabled(canSend);
        ui->reset->setEnabled(true);
        // Merge load/date/set into a single “解除” control to match controller UI spec
        if (ui->load) ui->load->setVisible(false);
        if (ui->set) ui->set->setVisible(false);
        if (ui->date)
        {
            ui->date->setVisible(true);
            ui->date->setText(releaseLabel);
            ui->date->setToolTip(releaseLabel);
        }
        ui->date->setEnabled(true);
        ui->set->setEnabled(true);
        ui->load->setEnabled(true);
        if (ui->input && ui->input->textEdit) ui->input->textEdit->setReadOnly(false);
    }
    else
    {
        if (ui->load) ui->load->setVisible(true);
        if (ui->set) ui->set->setVisible(true);
        if (ui->date)
        {
            ui->date->setVisible(true);
            ui->date->setText(dateLabel);
            ui->date->setToolTip(dateLabel);
        }
        if (ui->set)
        {
            ui->set->setText(QString());
            ui->set->setToolTip(jtr("set"));
        }
    }
}

void Widget::beginControlLink()
{
    const int tabIndex = (linkTabWidget) ? linkTabWidget->currentIndex() : 0;
    if (tabIndex == 0)
    {
        linkProfile_ = LinkProfile::Api;
        controlAwaitingHello_ = false;
        set_api();
        return;
    }
    linkProfile_ = LinkProfile::Control;
    const QString rawHost = control_host_LineEdit ? control_host_LineEdit->text() : QString();
    const QString host = TextParse::removeAllWhitespace(rawHost);
    const QString portText = control_port_LineEdit ? TextParse::removeAllWhitespace(control_port_LineEdit->text()) : QString();
    bool ok = false;
    const int port = portText.toInt(&ok);
    if (host.isEmpty())
    {
        reflash_state(jtr("control invalid host"), WRONG_SIGNAL);
        return;
    }
    if (!ok || port <= 0 || port > 65535)
    {
        reflash_state(jtr("control invalid port"), WRONG_SIGNAL);
        return;
    }
    setupControlChannel();
    if (!controlChannel_)
    {
        reflash_state(jtr("control listen fail"), WRONG_SIGNAL);
        return;
    }
    controlTargetHost_ = host;
    controlTargetPort_ = static_cast<quint16>(port);
    controlToken_ = control_token_LineEdit ? control_token_LineEdit->text() : QString();
    projectRuntimeControlLinkState();
    controlClient_.remoteRunning = false;
    controlClient_.remoteUiState = runtimeConversationModeForUi() == ConversationMode::Complete ? COMPLETE_STATE : CHAT_STATE;
    controlAwaitingHello_ = true;
    if (controlChannel_) controlChannel_->connectToHost(controlTargetHost_, controlTargetPort_);
    reflash_state(jtr("control connect").arg(QStringLiteral("%1:%2").arg(controlTargetHost_).arg(controlTargetPort_)), SIGNAL_SIGNAL);
    ui_state_normal();
}

void Widget::releaseControl(bool notifyRemote)
{
    if (isControllerActive() && controlChannel_ && notifyRemote)
    {
        QJsonObject bye;
        bye.insert(QStringLiteral("type"), QStringLiteral("command"));
        bye.insert(QStringLiteral("name"), QStringLiteral("release"));
        controlChannel_->sendToHost(bye);
    }
    controlClient_.state = ControlChannel::ControllerState::Idle;
    controlClient_.peer.clear();
    controlClient_.remoteRunning = false;
    controlAwaitingHello_ = false;
    linkProfile_ = LinkProfile::Api;
    if (controlChannel_) controlChannel_->disconnectFromHost();
    ui_state_normal();
    applyControlUiLock();
}
