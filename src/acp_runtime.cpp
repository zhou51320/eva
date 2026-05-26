#include "acp_runtime.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QSettings>
#include <QUrl>
#include <algorithm>

#include "app/config_migrator.h"
#include "app/default_model_finder.h"
#include "runtime/runtime_bootstrap.h"
#include "utils/devicemanager.h"
#include "utils/flowtracer.h"
#include "utils/openai_compat.h"
#include "utils/startuplogger.h"

namespace
{
QString canonicalPath(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) return canonical;
    return info.absoluteFilePath();
}

QString trimTail(const QString &text, int maxChars)
{
    if (text.size() <= maxChars) return text;
    return text.right(maxChars);
}

QString readStringSetting(QSettings &settings, const QString &key, const QString &fallback)
{
    const QString value = settings.value(key, fallback).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

QString normalizeEndpoint(const QString &rawEndpoint)
{
    QString clean = rawEndpoint.trimmed();
    if (clean.isEmpty()) return clean;

    QUrl url = QUrl::fromUserInput(clean);
    if (!url.isValid()) return clean;
    const QString scheme = url.scheme().trimmed().toLower();
    if (scheme.isEmpty())
    {
        const QString host = url.host().trimmed().toLower();
        const bool isLocal = (host == QStringLiteral("127.0.0.1") ||
                              host == QStringLiteral("localhost") ||
                              host == QStringLiteral("::1"));
        url.setScheme(isLocal ? QStringLiteral("http") : QStringLiteral("https"));
    }

    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.length() > 1) path.chop(1);
    if (path.toLower().endsWith(QStringLiteral("/v1")))
    {
        path.chop(3);
        if (path.isEmpty()) path = QStringLiteral("/");
        url.setPath(path);
    }
    return url.toString(QUrl::FullyDecoded).trimmed();
}
}

AcpRuntime::AcpRuntime(const LaunchOptions &options, QObject *parent)
    : QObject(parent),
      options_(options),
      serverManager_(new LocalServerManager(this, QCoreApplication::applicationDirPath())),
      bridgeClient_(new AcpBridgeClient(this))
{
    connect(serverManager_, &LocalServerManager::serverOutput, this, &AcpRuntime::onServerOutput);
    connect(serverManager_, &LocalServerManager::serverState, this, &AcpRuntime::onServerState);
    connect(serverManager_, &LocalServerManager::serverReady, this, &AcpRuntime::onServerReady);
    connect(serverManager_, &LocalServerManager::serverStopped, this, &AcpRuntime::onServerStopped);
    connect(serverManager_, &LocalServerManager::serverStartFailed, this, &AcpRuntime::onServerStartFailed);
}

bool AcpRuntime::initialize()
{
    ctx_ = RuntimeBootstrap::prepareContext();
    loadConfig();
    refreshBackendProbe();
    applyServerSettings();
    StartupLogger::log(QStringLiteral("[acp] runtime initialized"));
    return true;
}

QString AcpRuntime::bindHost() const
{
    return options_.bindHost;
}

quint16 AcpRuntime::bindPort() const
{
    return options_.bindPort;
}

QJsonObject AcpRuntime::healthPayload() const
{
    QJsonObject payload;
    QString health = QStringLiteral("ok");
    if (lifecycleState_ == QStringLiteral("error"))
        health = QStringLiteral("degraded");
    else if (lifecycleState_ == QStringLiteral("starting"))
        health = QStringLiteral("starting");
    payload.insert(QStringLiteral("status"), health);
    payload.insert(QStringLiteral("service"), QStringLiteral("eva-acp"));
    payload.insert(QStringLiteral("bind_host"), options_.bindHost);
    payload.insert(QStringLiteral("bind_port"), static_cast<int>(options_.bindPort));
    payload.insert(QStringLiteral("app_dir"), ctx_.appDir);
    payload.insert(QStringLiteral("config_path"), configPath());
    payload.insert(QStringLiteral("backend_state"), lifecycleState_);
    payload.insert(QStringLiteral("backend_ready"), backendReady_);
    return payload;
}

QJsonObject AcpRuntime::modelsPayload() const
{
    QJsonArray data;
    QString bridgeError;
    if (bridgeModeEnabled())
    {
        data = bridgeClient_->listModels(&bridgeError);
    }
    if (data.isEmpty())
    {
        if (isLinkMode())
        {
            const QJsonObject remote = remoteModelObject();
            if (!remote.isEmpty()) data.append(remote);
        }
        else
        {
            const QStringList models = discoverLocalModels();
            for (const QString &path : models)
            {
                data.append(localModelObject(path));
            }
        }
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("object"), QStringLiteral("list"));
    payload.insert(QStringLiteral("data"), data);
    if (!bridgeError.isEmpty()) payload.insert(QStringLiteral("bridge_error"), bridgeError);
    return payload;
}

QJsonObject AcpRuntime::backendStatePayload() const
{
    QString bridgeError;
    if (bridgeModeEnabled())
    {
        QJsonObject bridgeState = bridgeClient_->getState(&bridgeError);
        if (!bridgeState.isEmpty()) return bridgeState;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("mode"), isLinkMode() ? QStringLiteral("link") : QStringLiteral("local"));
    payload.insert(QStringLiteral("state"), lifecycleState_);
    payload.insert(QStringLiteral("ready"), backendReady_);
    payload.insert(QStringLiteral("endpoint"), backendEndpoint());
    payload.insert(QStringLiteral("current_model"), currentModelId());
    payload.insert(QStringLiteral("current_model_path"), isLinkMode() ? QString() : settings_.modelpath);
    payload.insert(QStringLiteral("backend_choice"), isLinkMode() ? QStringLiteral("link") : backendChoice_);
    payload.insert(QStringLiteral("backend_resolved"), isLinkMode() ? QStringLiteral("remote") : backendResolved_);
    payload.insert(QStringLiteral("available_backends"), isLinkMode() ? QJsonArray() : QJsonArray::fromStringList(DeviceManager::availableBackends()));
    payload.insert(QStringLiteral("backend_program_path"), isLinkMode() ? QString() : DeviceManager::programPath(QStringLiteral("llama-server-main")));
    payload.insert(QStringLiteral("server_running"), !isLinkMode() && serverManager_ && serverManager_->isRunning());
    payload.insert(QStringLiteral("port"), isLinkMode() ? QString() : backendPort_);
    payload.insert(QStringLiteral("nctx"), settings_.nctx);
    payload.insert(QStringLiteral("ngl"), settings_.ngl);
    payload.insert(QStringLiteral("nthread"), settings_.nthread);
    payload.insert(QStringLiteral("parallel"), settings_.hid_parallel);
    payload.insert(QStringLiteral("mmproj_path"), settings_.mmprojpath);
    payload.insert(QStringLiteral("lora_path"), settings_.lorapath);
    payload.insert(QStringLiteral("api_endpoint"), isLinkMode() ? apis_.api_endpoint : QString());
    payload.insert(QStringLiteral("api_model"), isLinkMode() ? apis_.api_model : QString());
    payload.insert(QStringLiteral("last_error"), bridgeError.isEmpty() ? lastError_ : bridgeError);
    payload.insert(QStringLiteral("last_output_tail"), lastOutput_);
    return payload;
}

bool AcpRuntime::loadBackend(const QJsonObject &request, QString *errorMessage)
{
    if (bridgeModeEnabled())
    {
        QJsonObject state = bridgeClient_->applyLoad(request, errorMessage);
        if (!state.isEmpty())
        {
            return true;
        }
        if (errorMessage && errorMessage->isEmpty())
        {
            *errorMessage = QStringLiteral("Bridge load failed.");
        }
        return false;
    }

    const QString requestedMode = request.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const bool wantsLinkMode =
        requestedMode == QStringLiteral("link") ||
        request.contains(QStringLiteral("api_endpoint")) ||
        request.contains(QStringLiteral("endpoint")) ||
        request.contains(QStringLiteral("api_model")) ||
        request.contains(QStringLiteral("api_key"));

    if (wantsLinkMode)
    {
        QString endpoint = apis_.api_endpoint;
        if (request.contains(QStringLiteral("api_endpoint")))
            endpoint = request.value(QStringLiteral("api_endpoint")).toString();
        else if (request.contains(QStringLiteral("endpoint")))
            endpoint = request.value(QStringLiteral("endpoint")).toString();
        endpoint = normalizeEndpoint(endpoint);
        if (endpoint.isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Remote api_endpoint is required in link mode.");
            return false;
        }

        const QUrl baseUrl = QUrl::fromUserInput(endpoint);
        if (!baseUrl.isValid())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Invalid remote endpoint: %1").arg(endpoint);
            return false;
        }

        if (request.contains(QStringLiteral("api_key")))
            apis_.api_key = request.value(QStringLiteral("api_key")).toString().trimmed();
        if (request.contains(QStringLiteral("api_model")))
            apis_.api_model = request.value(QStringLiteral("api_model")).toString().trimmed();

        apis_.api_endpoint = endpoint;
        apis_.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
        apis_.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
        apis_.is_local_backend = false;
        uiMode_ = LINK_MODE;
        lifecycleState_ = QStringLiteral("link");
        backendReady_ = true;
        readyEndpoint_.clear();
        backendResolved_ = QStringLiteral("remote");
        lastError_.clear();
        if (serverManager_ && serverManager_->isRunning())
        {
            serverManager_->stop();
        }
        saveConfig();
        return true;
    }

    uiMode_ = LOCAL_MODE;
    apis_.is_local_backend = true;
    if (request.contains(QStringLiteral("model_path")))
    {
        settings_.modelpath = request.value(QStringLiteral("model_path")).toString().trimmed();
    }
    else if (request.contains(QStringLiteral("modelPath")))
    {
        settings_.modelpath = request.value(QStringLiteral("modelPath")).toString().trimmed();
    }

    if (request.contains(QStringLiteral("mmproj_path")))
    {
        settings_.mmprojpath = request.value(QStringLiteral("mmproj_path")).toString().trimmed();
    }
    if (request.contains(QStringLiteral("lora_path")))
    {
        settings_.lorapath = request.value(QStringLiteral("lora_path")).toString().trimmed();
    }
    if (request.contains(QStringLiteral("backend")))
    {
        backendChoice_ = request.value(QStringLiteral("backend")).toString().trimmed().toLower();
    }
    else if (request.contains(QStringLiteral("device_backend")))
    {
        backendChoice_ = request.value(QStringLiteral("device_backend")).toString().trimmed().toLower();
    }
    if (backendChoice_.isEmpty()) backendChoice_ = QStringLiteral("auto");

    if (request.contains(QStringLiteral("port")))
    {
        backendPort_ = request.value(QStringLiteral("port")).toString().trimmed();
    }
    if (backendPort_.isEmpty()) backendPort_ = QStringLiteral(DEFAULT_SERVER_PORT);

    if (request.contains(QStringLiteral("nctx"))) settings_.nctx = request.value(QStringLiteral("nctx")).toInt(settings_.nctx);
    if (request.contains(QStringLiteral("ngl"))) settings_.ngl = request.value(QStringLiteral("ngl")).toInt(settings_.ngl);
    if (request.contains(QStringLiteral("nthread"))) settings_.nthread = request.value(QStringLiteral("nthread")).toInt(settings_.nthread);
    if (request.contains(QStringLiteral("parallel"))) settings_.hid_parallel = request.value(QStringLiteral("parallel")).toInt(settings_.hid_parallel);

    if (settings_.nctx <= 0) settings_.nctx = DEFAULT_NCTX;
    if (settings_.nthread <= 0) settings_.nthread = qMax(1u, std::thread::hardware_concurrency() / 2);
    if (settings_.hid_parallel <= 0) settings_.hid_parallel = DEFAULT_PARALLEL;

    if (settings_.modelpath.isEmpty())
    {
        const DefaultModelPaths paths = DefaultModelFinder::discover(ctx_.modelsDir);
        if (!paths.llmModel.isEmpty()) settings_.modelpath = paths.llmModel;
    }
    if (settings_.modelpath.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("No local model is configured.");
        return false;
    }
    if (!QFileInfo::exists(settings_.modelpath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured model path does not exist: %1").arg(settings_.modelpath);
        return false;
    }
    if (!settings_.mmprojpath.isEmpty() && !QFileInfo::exists(settings_.mmprojpath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured mmproj path does not exist: %1").arg(settings_.mmprojpath);
        return false;
    }
    if (!settings_.lorapath.isEmpty() && !QFileInfo::exists(settings_.lorapath))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Configured lora path does not exist: %1").arg(settings_.lorapath);
        return false;
    }

    bool ok = false;
    const quint16 portValue = backendPort_.toUShort(&ok);
    if (!ok || portValue == 0)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Invalid backend port: %1").arg(backendPort_);
        return false;
    }

    DeviceManager::setUserChoice(backendChoice_);
    refreshBackendProbe();
    const QString backendProgram = DeviceManager::programPath(QStringLiteral("llama-server-main"));
    if (backendProgram.isEmpty() || !QFileInfo::exists(backendProgram))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Backend executable is missing for device '%1'.").arg(backendChoice_);
        return false;
    }

    saveConfig();
    applyServerSettings();

    backendReady_ = false;
    readyEndpoint_.clear();
    lastError_.clear();
    lifecycleState_ = QStringLiteral("starting");
    FlowTracer::log(FlowChannel::Backend,
                    QStringLiteral("acp: ensure backend model=%1 backend=%2 port=%3")
                        .arg(settings_.modelpath, backendChoice_, backendPort_));
    serverManager_->ensureRunning();
    if (lifecycleState_ == QStringLiteral("error") && !lastError_.isEmpty())
    {
        if (errorMessage) *errorMessage = lastError_;
        return false;
    }
    return true;
}

void AcpRuntime::onServerOutput(const QString &chunk)
{
    appendLogTail(chunk);
}

void AcpRuntime::onServerState(const QString &line, SIGNAL_STATE type)
{
    appendLogTail(line + QLatin1Char('\n'));
    if (type == WRONG_SIGNAL)
    {
        lastError_ = trimTail(line, 2048);
        lifecycleState_ = QStringLiteral("error");
    }
}

void AcpRuntime::onServerReady(const QString &endpoint)
{
    backendReady_ = true;
    lifecycleState_ = QStringLiteral("running");
    readyEndpoint_ = endpoint;
    lastError_.clear();
    backendResolved_ = DeviceManager::lastResolvedDeviceFor(QStringLiteral("llama-server-main"));
    FlowTracer::log(FlowChannel::Backend, QStringLiteral("acp: backend ready %1").arg(endpoint));
}

void AcpRuntime::onServerStopped()
{
    backendReady_ = false;
    if (lifecycleState_ != QStringLiteral("error"))
    {
        lifecycleState_ = QStringLiteral("stopped");
    }
    readyEndpoint_.clear();
}

void AcpRuntime::onServerStartFailed(const QString &reason)
{
    backendReady_ = false;
    lifecycleState_ = QStringLiteral("error");
    readyEndpoint_.clear();
    lastError_ = trimTail(reason, 2048);
}

void AcpRuntime::loadConfig()
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    ConfigMigrator::migrate(settings);

    uiMode_ = static_cast<EVA_MODE>(settings.value(QStringLiteral("ui_mode"), 0).toInt());
    settings_.modelpath = settings.value(QStringLiteral("modelpath"), QString()).toString();
    settings_.mmprojpath = settings.value(QStringLiteral("mmprojpath"), QString()).toString();
    settings_.lorapath = settings.value(QStringLiteral("lorapath"), QString()).toString();
    settings_.ngl = settings.value(QStringLiteral("ngl"), DEFAULT_NGL).toInt();
    settings_.nthread = settings.value(QStringLiteral("nthread"), settings_.nthread).toInt();
    settings_.nctx = settings.value(QStringLiteral("nctx"), DEFAULT_NCTX).toInt();
    settings_.hid_batch = settings.value(QStringLiteral("hid_batch"), DEFAULT_BATCH).toInt();
    settings_.hid_parallel = settings.value(QStringLiteral("hid_parallel"), DEFAULT_PARALLEL).toInt();
    settings_.hid_use_mmap = settings.value(QStringLiteral("hid_use_mmap"), DEFAULT_USE_MMAP).toBool();
    settings_.hid_use_mlock = settings.value(QStringLiteral("hid_use_mlock"), DEFAULT_USE_MLOCCK).toBool();
    settings_.hid_flash_attn = settings.value(QStringLiteral("hid_flash_attn"), DEFAULT_FLASH_ATTN).toBool();
    settings_.reasoning_effort = readStringSetting(settings, QStringLiteral("reasoning_effort"), QStringLiteral("auto"));

    apis_.api_endpoint = normalizeEndpoint(settings.value(QStringLiteral("api_endpoint"), QString()).toString());
    apis_.api_key = settings.value(QStringLiteral("api_key"), QString()).toString().trimmed();
    apis_.api_model = settings.value(QStringLiteral("api_model"), QString()).toString().trimmed();
    {
        const QUrl baseUrl = QUrl::fromUserInput(apis_.api_endpoint);
        apis_.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
        apis_.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
        apis_.is_local_backend = (uiMode_ == LOCAL_MODE);
    }

    backendPort_ = readStringSetting(settings, QStringLiteral("port"), QStringLiteral(DEFAULT_SERVER_PORT));
    backendChoice_ = readStringSetting(settings, QStringLiteral("device_backend"), QStringLiteral("auto")).toLower();

    DeviceManager::clearProgramOverrides();
    settings.beginGroup(QStringLiteral("backend_overrides"));
    const QStringList overrideKeys = settings.childKeys();
    for (const QString &key : overrideKeys)
    {
        const QString overridePath = settings.value(key).toString();
        if (!overridePath.isEmpty()) DeviceManager::setProgramOverride(key, overridePath);
    }
    settings.endGroup();
    DeviceManager::setUserChoice(backendChoice_);

    if (!isLinkMode() && settings_.modelpath.isEmpty())
    {
        const DefaultModelPaths paths = DefaultModelFinder::discover(ctx_.modelsDir);
        settings_.modelpath = paths.llmModel;
    }

    if (isLinkMode())
    {
        lifecycleState_ = QStringLiteral("link");
        backendReady_ = !apis_.api_endpoint.isEmpty();
        backendResolved_ = QStringLiteral("remote");
    }
}

void AcpRuntime::saveConfig() const
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setIniCodec("utf-8");
    settings.setValue(QStringLiteral("ui_mode"), static_cast<int>(uiMode_));
    settings.setValue(QStringLiteral("modelpath"), settings_.modelpath);
    settings.setValue(QStringLiteral("mmprojpath"), settings_.mmprojpath);
    settings.setValue(QStringLiteral("lorapath"), settings_.lorapath);
    settings.setValue(QStringLiteral("ngl"), settings_.ngl);
    settings.setValue(QStringLiteral("nthread"), settings_.nthread);
    settings.setValue(QStringLiteral("nctx"), settings_.nctx);
    settings.setValue(QStringLiteral("hid_batch"), settings_.hid_batch);
    settings.setValue(QStringLiteral("hid_parallel"), settings_.hid_parallel);
    settings.setValue(QStringLiteral("hid_use_mmap"), settings_.hid_use_mmap);
    settings.setValue(QStringLiteral("hid_use_mlock"), settings_.hid_use_mlock);
    settings.setValue(QStringLiteral("hid_flash_attn"), settings_.hid_flash_attn);
    settings.setValue(QStringLiteral("reasoning_effort"), settings_.reasoning_effort);
    settings.setValue(QStringLiteral("port"), backendPort_);
    settings.setValue(QStringLiteral("device_backend"), backendChoice_);
    if (isLinkMode())
    {
        settings.setValue(QStringLiteral("api_endpoint"), apis_.api_endpoint);
        settings.setValue(QStringLiteral("api_key"), apis_.api_key);
        settings.setValue(QStringLiteral("api_model"), apis_.api_model);
    }
    settings.sync();
}

void AcpRuntime::applyServerSettings()
{
    if (!serverManager_) return;
    serverManager_->setSettings(settings_);
    serverManager_->setHost(QStringLiteral("127.0.0.1"));
    serverManager_->setPort(backendPort_);
    serverManager_->setModelPath(settings_.modelpath);
    serverManager_->setMmprojPath(settings_.mmprojpath);
    serverManager_->setLoraPath(settings_.lorapath);
}

QString AcpRuntime::configPath() const
{
    return QDir(ctx_.tempDir).filePath(QStringLiteral("eva_config.ini"));
}

QString AcpRuntime::backendEndpoint() const
{
    if (isLinkMode()) return apis_.api_endpoint;
    if (!readyEndpoint_.isEmpty()) return readyEndpoint_;
    return QStringLiteral("http://127.0.0.1:%1").arg(backendPort_);
}

QStringList AcpRuntime::discoverLocalModels() const
{
    QStringList models;
    const QString llmRoot = QDir(ctx_.modelsDir).filePath(QStringLiteral("llm"));
    if (QDir(llmRoot).exists())
    {
        QDirIterator it(llmRoot, QStringList() << QStringLiteral("*.gguf"), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            models.append(canonicalPath(it.next()));
        }
    }
    if (!settings_.modelpath.isEmpty() && QFileInfo::exists(settings_.modelpath))
    {
        const QString current = canonicalPath(settings_.modelpath);
        if (!current.isEmpty() && !models.contains(current)) models.append(current);
    }
    models.removeDuplicates();
    std::sort(models.begin(), models.end(), [](const QString &left, const QString &right)
    {
        return left.toLower() < right.toLower();
    });
    return models;
}

QJsonObject AcpRuntime::localModelObject(const QString &path) const
{
    QFileInfo info(path);
    const QString canonical = canonicalPath(path);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    QJsonObject model;
    model.insert(QStringLiteral("id"), relative.isEmpty() ? info.fileName() : relative);
    model.insert(QStringLiteral("object"), QStringLiteral("model"));
    model.insert(QStringLiteral("created"), static_cast<qint64>(info.lastModified().toSecsSinceEpoch()));
    model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-acp"));
    model.insert(QStringLiteral("path"), canonical);
    model.insert(QStringLiteral("current"), canonical == canonicalPath(settings_.modelpath));
    model.insert(QStringLiteral("source"), QStringLiteral("local"));
    return model;
}

QJsonObject AcpRuntime::remoteModelObject() const
{
    if (apis_.api_model.isEmpty()) return QJsonObject();
    QJsonObject model;
    model.insert(QStringLiteral("id"), apis_.api_model);
    model.insert(QStringLiteral("object"), QStringLiteral("model"));
    model.insert(QStringLiteral("owned_by"), QStringLiteral("eva-acp"));
    model.insert(QStringLiteral("current"), true);
    model.insert(QStringLiteral("source"), QStringLiteral("remote"));
    model.insert(QStringLiteral("endpoint"), apis_.api_endpoint);
    return model;
}

QString AcpRuntime::currentModelId() const
{
    if (isLinkMode()) return apis_.api_model;
    if (settings_.modelpath.isEmpty() || !QFileInfo::exists(settings_.modelpath)) return QString();
    const QFileInfo info(settings_.modelpath);
    const QString canonical = canonicalPath(settings_.modelpath);
    const QString relative = ctx_.modelsDir.isEmpty() ? info.fileName() : QDir(ctx_.modelsDir).relativeFilePath(canonical);
    return relative.isEmpty() ? info.fileName() : relative;
}

bool AcpRuntime::linkModeEnabled() const
{
    return isLinkMode();
}

bool AcpRuntime::bridgeModeEnabled() const
{
    if (!bridgeClient_) return false;
    if (bridgeClient_->isConnected()) return true;
    QString errorMessage;
    return const_cast<AcpBridgeClient *>(bridgeClient_)->ensureConnected(300, &errorMessage);
}

QString AcpRuntime::modelsEndpoint() const
{
    if (isLinkMode())
    {
        const QUrl base = QUrl::fromUserInput(apis_.api_endpoint);
        return OpenAiCompat::joinPath(base, OpenAiCompat::modelsPath(base)).toString();
    }
    return QString();
}

QString AcpRuntime::chatCompletionsEndpoint() const
{
    if (isLinkMode())
    {
        const QUrl base = QUrl::fromUserInput(apis_.api_endpoint);
        return OpenAiCompat::joinPath(base, OpenAiCompat::chatCompletionsPath(base)).toString();
    }
    return backendEndpoint() + QStringLiteral("/v1/chat/completions");
}

QString AcpRuntime::configuredApiKey() const
{
    return isLinkMode() ? apis_.api_key : QString();
}

QString AcpRuntime::configuredApiModel() const
{
    return isLinkMode() ? apis_.api_model : QString();
}

bool AcpRuntime::resetConversation(QString *errorMessage)
{
    if (bridgeClient_ && bridgeClient_->resetConversation(errorMessage)) return true;
    if (errorMessage) *errorMessage = QStringLiteral("Bridge reset unavailable.");
    return false;
}

QJsonObject AcpRuntime::chatCompletion(const QJsonObject &request, QString *errorMessage)
{
    return streamChatCompletion(request, std::function<void(const QString &, const QString &)>(), errorMessage);
}

QJsonObject AcpRuntime::streamChatCompletion(const QJsonObject &request,
                                             const std::function<void(const QString &role, const QString &chunk)> &onChunk,
                                             QString *errorMessage)
{
    if (bridgeClient_)
    {
        QString text;
        const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
        for (int i = messages.size() - 1; i >= 0; --i)
        {
            const QJsonObject message = messages.at(i).toObject();
            if (message.value(QStringLiteral("role")).toString() == QStringLiteral("user"))
            {
                text = message.value(QStringLiteral("content")).toString();
                if (!text.isEmpty()) break;
            }
        }
        if (text.trimmed().isEmpty())
        {
            if (errorMessage) *errorMessage = QStringLiteral("Bridge mode currently requires the latest user text.");
            return QJsonObject();
        }

        AcpBridgeClient::ChatResult result;
        if (!bridgeClient_->sendTextStreaming(text, onChunk, &result, errorMessage))
        {
            return QJsonObject();
        }

        QString stateError;
        const QJsonObject liveState = bridgeClient_->getState(&stateError, 1500);
        const QString liveModel = liveState.value(QStringLiteral("current_model")).toString();

        QJsonObject message;
        message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        message.insert(QStringLiteral("content"), result.assistantText);
        if (!result.reasoningText.isEmpty())
            message.insert(QStringLiteral("reasoning"), result.reasoningText);

        QJsonObject choice;
        choice.insert(QStringLiteral("index"), 0);
        choice.insert(QStringLiteral("message"), message);
        choice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

        QJsonArray choices;
        choices.append(choice);

        QJsonObject response;
        response.insert(QStringLiteral("id"), QStringLiteral("chatcmpl-bridge"));
        response.insert(QStringLiteral("object"), QStringLiteral("chat.completion"));
        response.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        response.insert(QStringLiteral("model"), liveModel.isEmpty() ? currentModelId() : liveModel);
        response.insert(QStringLiteral("choices"), choices);
        return response;
    }

    if (errorMessage) *errorMessage = QStringLiteral("Bridge mode unavailable.");
    return QJsonObject();
}

bool AcpRuntime::isLinkMode() const
{
    return uiMode_ == LINK_MODE && !apis_.api_endpoint.trimmed().isEmpty();
}

void AcpRuntime::appendLogTail(const QString &chunk)
{
    if (chunk.isEmpty()) return;
    lastOutput_.append(chunk);
    lastOutput_ = trimTail(lastOutput_, 4096);
}

void AcpRuntime::refreshBackendProbe()
{
    if (isLinkMode())
    {
        backendResolved_ = QStringLiteral("remote");
        return;
    }
    backendResolved_ = DeviceManager::effectiveBackend();
    DeviceManager::programPath(QStringLiteral("llama-server-main"));
    const QString lastResolved = DeviceManager::lastResolvedDeviceFor(QStringLiteral("llama-server-main"));
    if (!lastResolved.isEmpty()) backendResolved_ = lastResolved;
}
