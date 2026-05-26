#include "core/session/session_controller.h"

#include "prompt_builder.h"
#include "utils/flowtracer.h"

#include <doc2md/document_converter.h>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QtGlobal>

namespace
{
QString joinAttachmentNames(const QStringList &paths, int maxCount)
{
    if (paths.isEmpty()) return QString();
    const int limit = qMax(1, maxCount);
    QStringList names;
    names.reserve(qMin(paths.size(), limit) + 1);
    for (int i = 0; i < paths.size() && i < limit; ++i)
    {
        QFileInfo info(paths[i]);
        QString name = info.fileName();
        if (name.isEmpty()) name = paths[i];
        names.append(name);
    }
    if (paths.size() > limit)
    {
        names.append(QStringLiteral("...+%1").arg(paths.size() - limit));
    }
    return names.join(QStringLiteral(", "));
}
}

SessionController::SessionController(QObject *owner)
    : QObject(owner), host_(dynamic_cast<SessionHostPort *>(owner))
{
}

SessionHostPort *SessionController::hostPort() const
{
    return host_;
}

SessionFrontendPort *SessionController::frontendPort() const
{
    return host_;
}

RuntimeState SessionController::runtimeState() const
{
    const SessionHostPort *host = hostPort();
    if (host && host->runtimeStateSnapshotForSession().initialized)
        return host->runtimeStateSnapshotForSession();
    return RuntimeState();
}

SessionController::PendingToolState SessionController::pendingToolState() const
{
    PendingToolState tool;
    SessionHostPort *host = hostPort();
    const RuntimeState state = runtimeState();
    if (state.initialized)
    {
        tool.result = state.pendingToolResult;
        tool.pendingName = state.pendingToolName;
        tool.callId = state.pendingToolCallId;
        tool.lastName = state.lastToolCallName;
    }
    if (host)
    {
        if (tool.result.isEmpty()) tool.result = host->pendingToolResult();
        if (tool.pendingName.isEmpty()) tool.pendingName = host->pendingToolName();
        if (tool.callId.isEmpty()) tool.callId = host->pendingToolCallId();
        if (tool.lastName.isEmpty()) tool.lastName = host->lastToolCallName();
    }
    return tool;
}

void SessionController::clearPendingToolResult()
{
    if (SessionHostPort *host = hostPort())
        host->clearPendingToolResult();
    syncRuntimeSessionState();
}

COMPACTION_SETTINGS SessionController::compactionSettings() const
{
    SessionHostPort *host = hostPort();
    const RuntimeState state = runtimeState();
    if (state.initialized)
        return state.compactionSettings;
    return host ? host->sessionCompactionSettings() : COMPACTION_SETTINGS();
}

QJsonArray SessionController::sessionMessages() const
{
    SessionHostPort *host = hostPort();
    if (!host)
        return QJsonArray();
    const RuntimeState state = runtimeState();
    if (state.initialized)
    {
        const QJsonArray runtimeMessages = state.messages;
        if (!runtimeMessages.isEmpty() || host->legacySessionMessages().isEmpty())
            return runtimeMessages;
    }
    return host->legacySessionMessages();
}

int SessionController::appendConversationMessage(const QJsonObject &message, bool persistHistory)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return -1;

    const int index = host->appendRuntimeSessionMessage(message);

    if (persistHistory)
        host->appendHistoryMessage(message);
    syncRuntimeSessionState();
    return index;
}

void SessionController::replaceConversationMessages(const QJsonArray &messages, bool rewriteHistory)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    host->setLegacySessionMessages(messages);
    if (host->runtimeSessionReady())
        host->syncSessionRuntimeState(true);
    if (rewriteHistory)
        host->rewriteHistoryMessages(messages);
    syncRuntimeSessionState();
}

void SessionController::replaceConversationMessage(int index, const QJsonObject &message)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    QJsonArray messages = sessionMessages();
    if (index < 0 || index >= messages.size())
        return;
    messages.replace(index, message);
    if (host->runtimeSessionReady())
    {
        if (!host->replaceRuntimeSessionMessage(index, message))
            host->setLegacySessionMessages(messages);
    }
    else
    {
        host->setLegacySessionMessages(messages);
    }
    syncRuntimeSessionState();
}

void SessionController::syncRuntimeSessionState() const
{
    if (SessionHostPort *host = hostPort())
        host->syncSessionRuntimeState(true);
}

ENDPOINT_DATA SessionController::prepareEndpointData()
{
    const RuntimeState state = runtimeState();
    const bool hasRuntime = state.initialized;
    SessionHostPort *host = hostPort();
    const SETTINGS settings = hasRuntime ? state.settings : (host ? host->sessionSettingsSnapshot() : SETTINGS());
    ENDPOINT_DATA d;
    d.date_prompt = hasRuntime ? state.systemPrompt : (host ? host->sessionSystemPrompt() : QString());
    d.stopwords = hasRuntime ? state.stopwords : (host ? host->sessionStopwords() : QStringList());
    d.is_complete_state = hasRuntime ? (state.conversationMode == ConversationMode::Complete)
                                     : (host && host->sessionConversationMode() == ConversationMode::Complete);
    d.temp = settings.temp;
    d.repeat = settings.repeat;
    d.top_k = settings.top_k;
    d.top_p = settings.hid_top_p;
    d.n_predict = settings.hid_npredict;
    d.reasoning_effort = sanitizeReasoningEffort(settings.reasoning_effort);
    d.messagesArray = sessionMessages();
    d.tool_call_mode = hasRuntime ? state.toolCallMode : (host ? host->sessionToolCallMode() : DEFAULT_TOOL_CALL_MODE);
    d.tools = (d.tool_call_mode == TOOL_CALL_FUNCTION && host) ? host->sessionFunctionTools() : QJsonArray();
    d.id_slot = hasRuntime ? state.slotId : (host ? host->sessionSlotId() : -1);
    d.turn_id = hasRuntime ? state.activeTurnId : (host ? host->sessionActiveTurnId() : 0);
    return d;
}

void SessionController::beginSessionIfNeeded()
{
    SessionHostPort *host = hostPort();
    if (!host || !host->shouldBeginHistorySession())
        return;
    SessionMeta meta;
    meta.id = QString::number(QDateTime::currentMSecsSinceEpoch());
    meta.title = "";
    const RuntimeState state = runtimeState();
    const bool hasRuntime = state.initialized;
    meta.endpoint = hasRuntime && !state.endpoint.isEmpty() ? state.endpoint : host->sessionEndpointForHistory();
    meta.model = hasRuntime && !state.currentModel.isEmpty() ? state.currentModel : host->sessionModelForHistory();
    meta.system = hasRuntime ? state.systemPrompt : host->sessionSystemPrompt();
    meta.n_ctx = hasRuntime ? state.settings.nctx : host->sessionContextSize();
    meta.slot_id = hasRuntime ? state.slotId : host->sessionSlotId();
    meta.startedAt = QDateTime::currentDateTime();
    host->beginHistorySession(meta);
    QJsonObject systemMessage;
    systemMessage.insert("role", DEFAULT_SYSTEM_NAME);
    systemMessage.insert("content", meta.system);
    host->appendHistoryMessage(systemMessage);
    syncRuntimeSessionState();
}

bool SessionController::buildDocumentAttachment(const QString &path, DocumentAttachment &attachment)
{
    const QFileInfo info(path);
    const QString absolutePath = info.exists() ? info.absoluteFilePath() : path;
    const QByteArray encoded = QFile::encodeName(absolutePath);
    if (encoded.isEmpty())
    {
        if (SessionFrontendPort *frontend = frontendPort())
            frontend->showSessionWarning(QStringLiteral("ui:invalid document path -> ") + absolutePath, WRONG_SIGNAL);
        return false;
    }
    const std::string pathStr(encoded.constData(), static_cast<size_t>(encoded.size()));
    const doc2md::ConversionResult result = doc2md::convertFile(pathStr);
    for (const std::string &warn : result.warnings)
    {
        if (SessionFrontendPort *frontend = frontendPort())
            frontend->showSessionWarning(QStringLiteral("[doc2md] %1").arg(QString::fromStdString(warn)), USUAL_SIGNAL);
    }
    if (!result.success)
    {
        if (SessionFrontendPort *frontend = frontendPort())
            frontend->showSessionWarning(QStringLiteral("ui:doc parse failed -> ") + absolutePath, WRONG_SIGNAL);
        return false;
    }
    attachment.path = absolutePath;
    attachment.displayName = info.fileName().isEmpty() ? absolutePath : info.fileName();
    attachment.markdown = QString::fromUtf8(result.markdown.data(), static_cast<int>(result.markdown.size()));
    return true;
}

QString SessionController::formatDocumentPayload(const DocumentAttachment &doc) const
{
    QString name = doc.displayName;
    if (name.isEmpty())
    {
        const QFileInfo info(doc.path);
        name = info.fileName().isEmpty() ? doc.path : info.fileName();
    }
    return QStringLiteral("### Document: %1\n%2").arg(name, doc.markdown);
}

QString SessionController::describeDocumentList(const QVector<DocumentAttachment> &docs) const
{
    if (docs.isEmpty())
        return QString();
    QStringList names;
    names.reserve(docs.size());
    for (const DocumentAttachment &doc : docs)
    {
        QString name = doc.displayName;
        if (name.isEmpty())
        {
            const QFileInfo info(doc.path);
            name = info.fileName().isEmpty() ? doc.path : info.fileName();
        }
        names.append(name);
    }
    return names.join(QStringLiteral(", "));
}

void SessionController::collectUserInputs(InputPack &pack, bool attachControllerFrame)
{
    SessionFrontendPort *frontend = frontendPort();
    if (!frontend)
        return;
    pack.text.clear();
    // Only collect user text when we are NOT in a tool loop. The current task
    // is already logged by on_send_clicked(); do not log here to avoid
    // duplicate/misleading "current task" lines.
    pack.text = frontend->takeDraftText(!pendingToolState().hasResult());
    pack.images = frontend->draftImageFilePaths();
    pack.documents.clear();
    const QStringList documentPaths = frontend->draftDocumentFilePaths();
    if (!documentPaths.isEmpty())
    {
        pack.documents.reserve(documentPaths.size());
        for (const QString &docPath : documentPaths)
        {
            DocumentAttachment attachment;
            if (buildDocumentAttachment(docPath, attachment))
            {
                pack.documents.append(attachment);
            }
        }
    }
    if (attachControllerFrame && frontend->shouldAttachControllerFrame())
    {
        // 桌面控制器开启时：为模型附带最新截屏（仅原图，不再附带坐标叠加图）
        const QString imagePath = frontend->captureControllerFrameImagePath();
        if (!imagePath.isEmpty())
        {
            // 记录“最后一次发给模型的控制器截图”，用于后续将 bbox 等信息叠加后落盘（EVA_TEMP/overlay）
            frontend->rememberControllerFrameForModel(imagePath);
            pack.images.append(imagePath);
        }
    }
    pack.wavs = frontend->draftAudioFilePaths();
    frontend->clearDraftAttachments();
}

void SessionController::handleChatReply(ENDPOINT_DATA &data, const InputPack &in)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    host->noteBackendActivity();
    host->cancelSessionLazyUnload(QStringLiteral("handle chat reply"));

    // 记录本轮用户输入对应的第一条消息索引，便于记录条锚点定位
    int firstUserMsgIndex = -1;
    const auto markUserIndex = [&](int idx) {
        if (firstUserMsgIndex < 0) firstUserMsgIndex = idx;
    };

    // 输出区展示文本（用户输入 + 附件摘要）
    QString displayText = in.text;
    QStringList attachmentLines;
    if (!in.documents.isEmpty())
    {
        QString docLabel = describeDocumentList(in.documents);
        if (docLabel.isEmpty()) docLabel = QString::number(in.documents.size());
        attachmentLines.append(QStringLiteral("[DOC] ") + docLabel);
    }
    if (!in.images.isEmpty())
    {
        QString imageLabel = joinAttachmentNames(in.images, 6);
        if (imageLabel.isEmpty()) imageLabel = QString::number(in.images.size());
        attachmentLines.append(QStringLiteral("[IMG] ") + imageLabel);
    }
    if (!in.wavs.isEmpty())
    {
        QString audioLabel = joinAttachmentNames(in.wavs, 6);
        if (audioLabel.isEmpty()) audioLabel = QString::number(in.wavs.size());
        attachmentLines.append(QStringLiteral("[AUDIO] ") + audioLabel);
    }
    if (!attachmentLines.isEmpty())
    {
        if (!displayText.isEmpty()) displayText.append('\n');
        displayText.append(attachmentLines.join(QStringLiteral("\n")));
    }

    // user message assembly
    const bool hasMixedContent = !in.images.isEmpty() || !in.documents.isEmpty();
    if (!hasMixedContent)
    {
        QJsonObject roleMessage;
        roleMessage.insert("role", DEFAULT_USER_NAME);
        roleMessage.insert("content", in.text);
        markUserIndex(appendConversationMessage(roleMessage));
    }
    else
    {
        QJsonObject message;
        message["role"] = DEFAULT_USER_NAME;
        QJsonArray contentArray;
        if (!in.text.isEmpty())
        {
            QJsonObject textMessage;
            textMessage.insert("type", "text");
            textMessage.insert("text", in.text);
            contentArray.append(textMessage);
        }
        if (!in.documents.isEmpty())
        {
            for (const DocumentAttachment &doc : in.documents)
            {
                if (doc.markdown.isEmpty())
                    continue;
                QJsonObject docObject;
                docObject["type"] = "text";
                docObject["text"] = formatDocumentPayload(doc);
                contentArray.append(docObject);
            }
        }
        if (!in.images.isEmpty())
        {
            // 附带图片时：只发送图片本体，不再额外插入“图片文件名/尺寸”等元信息文本，避免干扰模型决策。
            QJsonArray locals;
            for (const QString &imagePath : in.images)
            {
                QFile imageFile(imagePath);
                if (!imageFile.open(QIODevice::ReadOnly))
                {
                    qDebug() << "Failed to open image file";
                    continue;
                }
                const QByteArray imageData = imageFile.readAll();
                const QByteArray base64Data = imageData.toBase64();
                // 按文件后缀选择 MIME，避免固定写死导致部分后端/模型解析异常。
                const QString ext = QFileInfo(imagePath).suffix().toLower();
                QString mimeType = QStringLiteral("image/png");
                if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg"))
                    mimeType = QStringLiteral("image/jpeg");
                else if (ext == QStringLiteral("png"))
                    mimeType = QStringLiteral("image/png");
                else if (ext == QStringLiteral("webp"))
                    mimeType = QStringLiteral("image/webp");
                else if (ext == QStringLiteral("gif"))
                    mimeType = QStringLiteral("image/gif");
                const QString base64String = QStringLiteral("data:%1;base64,").arg(mimeType) + base64Data;
                QJsonObject imageObject;
                imageObject["type"] = QStringLiteral("image_url");
                QJsonObject imageUrlObject;
                imageUrlObject["url"] = base64String;
                imageObject["image_url"] = imageUrlObject;
                contentArray.append(imageObject);

                // 历史/本地恢复用：记录图片的本地路径，避免把 base64 落盘到 messages.jsonl 导致文件臃肿。
                // 注意：该字段属于 EVA 的本地扩展字段，发给模型前会在 prompt_builder 中被移除。
                locals.append(QFileInfo(imagePath).absoluteFilePath());
            }
            if (!locals.isEmpty())
            {
                message.insert(QStringLiteral("local_images"), locals);
            }
        }
        message["content"] = contentArray;
        markUserIndex(appendConversationMessage(message));
    }
    if (!in.wavs.isEmpty())
    {
        QJsonObject message;
        message["role"] = DEFAULT_USER_NAME;
        QJsonArray contentArray;
        for (int i = 0; i < in.wavs.size(); ++i)
        {
            QString filePath = in.wavs[i];
            QFile audioFile(filePath);
            if (!audioFile.open(QIODevice::ReadOnly))
            {
                qDebug() << "Failed to open audio file:" << filePath;
                continue;
            }
            QByteArray audioData = audioFile.readAll();
            QByteArray base64Data = audioData.toBase64();
            QFileInfo fileInfo(filePath);
            QString extension = fileInfo.suffix().toLower();
            QString mimeType = "audio/mpeg";
            if (extension == "wav")
                mimeType = "audio/wav";
            else if (extension == "ogg")
                mimeType = "audio/ogg";
            else if (extension == "flac")
                mimeType = "audio/flac";
            QString base64String = QString("data:%1;base64,").arg(mimeType) + base64Data;
            QJsonObject audioObject;
            audioObject["type"] = "audio_url";
            QJsonObject audioUrlObject;
            audioUrlObject["url"] = base64String;
            audioObject["audio_url"] = audioUrlObject;
            contentArray.append(audioObject);
        }
        message["content"] = contentArray;
        markUserIndex(appendConversationMessage(message));
    }

    // 输出区：显示用户输入（包含附件摘要），避免只看到模型输出。
    if (SessionFrontendPort *frontend = frontendPort())
        frontend->presentUserMessageRecord(displayText, firstUserMsgIndex);
    const PendingToolState tool = pendingToolState();
    if (tool.hasResult())
    {
        QJsonObject toolMessage;
        toolMessage["role"] = QStringLiteral("tool");
        toolMessage["content"] = tool.result;
        toolMessage["tool"] = tool.pendingName;
        if (!tool.callId.isEmpty())
        {
            toolMessage.insert(QStringLiteral("tool_call_id"), tool.callId);
        }
        appendConversationMessage(toolMessage);
        clearPendingToolResult();
    }

    // 把 message array 统一按照模型格式重新打包（去掉 UI-only 字段）
    data.messagesArray = promptx::buildOaiChatMessages(sessionMessages(),
                                                       data.date_prompt,
                                                       DEFAULT_SYSTEM_NAME,
                                                       DEFAULT_USER_NAME,
                                                       DEFAULT_MODEL_NAME);

    // 发送
    host->sendEndpointData(data);
}

void SessionController::handleCompletion(ENDPOINT_DATA &data)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    host->noteBackendActivity();
    host->cancelSessionLazyUnload(QStringLiteral("handle completion"));

    QJsonObject roleMessage;
    roleMessage.insert("role", DEFAULT_USER_NAME);
    roleMessage.insert("content", frontendPort() ? frontendPort()->takeDraftText(true) : QString());
    appendConversationMessage(roleMessage, false);

    data.messagesArray = sessionMessages();

    host->sendEndpointData(data);
}

void SessionController::handleToolLoop(ENDPOINT_DATA &data)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    host->noteBackendActivity();
    host->cancelSessionLazyUnload(QStringLiteral("handle tool loop"));

    // 插入 tool 结果作为 tool 消息（文本模式下会在 net 层兼容为 user 前缀）
    QJsonObject toolMessage;
    toolMessage.insert("role", QStringLiteral("tool"));
    const PendingToolState tool = pendingToolState();
    toolMessage.insert("content", tool.result);
    const QString toolName = tool.effectiveName();
    if (!toolName.isEmpty())
    {
        toolMessage.insert(QStringLiteral("tool"), toolName);
    }
    if (!tool.callId.isEmpty())
    {
        toolMessage.insert(QStringLiteral("tool_call_id"), tool.callId);
    }
    const int toolMsgIndex = appendConversationMessage(toolMessage);

    // 输出区：补齐 tool 结果显示（与记录条绑定）。
    if (SessionFrontendPort *frontend = frontendPort())
        frontend->presentToolMessageRecord(toolName, tool.result, toolMsgIndex);
    clearPendingToolResult();

    data.messagesArray = promptx::buildOaiChatMessages(sessionMessages(),
                                                       data.date_prompt,
                                                       DEFAULT_SYSTEM_NAME,
                                                       DEFAULT_USER_NAME,
                                                       DEFAULT_MODEL_NAME);

    host->sendEndpointData(data);
}

void SessionController::logCurrentTask(ConversationTask task)
{
    Q_UNUSED(task);
    // 如需可视化当前任务，可在此处扩展统一输出
}

void SessionController::startTurnFlow(ConversationTask task, bool continuingTool)
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    const QString taskName = (task == ConversationTask::ChatReply) ? QStringLiteral("chat")
                               : (task == ConversationTask::Completion) ? QStringLiteral("completion")
                               : (task == ConversationTask::ToolLoop) ? QStringLiteral("tool-loop")
                                                                       : QStringLiteral("compaction");
    host->startSessionTurn(taskName, task == ConversationTask::ToolLoop, continuingTool);
}

void SessionController::finishTurnFlow(const QString &reason, bool success)
{
    if (SessionHostPort *host = hostPort())
        host->finishSessionTurn(reason, success);
}

void SessionController::ensureSystemHeader(const QString &systemText)
{
    // Ensure first message is system
    QJsonArray messages = sessionMessages();
    int systemMessageIndex = 0;
    if (messages.isEmpty() || messages.first().toObject().value(QStringLiteral("role")).toString() != QStringLiteral(DEFAULT_SYSTEM_NAME))
    {
        QJsonObject systemMessage;
        systemMessage.insert(QStringLiteral("role"), DEFAULT_SYSTEM_NAME);
        systemMessage.insert(QStringLiteral("content"), systemText);
        if (messages.isEmpty())
            systemMessageIndex = appendConversationMessage(systemMessage, false);
        else
        {
            replaceConversationMessage(0, systemMessage);
            systemMessageIndex = 0;
        }
        messages = sessionMessages();
    }

    if (SessionHostPort *host = hostPort())
        host->presentSystemMessageRecord(systemText, messages.isEmpty() ? systemMessageIndex : 0);
    syncRuntimeSessionState();
}

bool SessionController::shouldTriggerCompaction() const
{
    SessionHostPort *host = hostPort();
    if (!host)
        return false;
    const COMPACTION_SETTINGS compaction = compactionSettings();
    if (!compaction.enabled)
        return false;
    if (!host->canAutoCompact())
        return false;
    if (sessionMessages().isEmpty())
        return false;

    const int cap = host->resolvedContextLimitForSession();
    if (cap <= 0)
        return false; // 未知上限时不自动压缩
    const int used = qMax(0, host->kvUsedForSession());
    if (used <= 0)
        return false;

    const double ratio = static_cast<double>(used) / static_cast<double>(cap);
    if (ratio >= compaction.trigger_ratio)
        return true;
    if (used >= (cap - compaction.reserve_tokens))
        return true;
    return false;
}

bool SessionController::startCompactionIfNeeded(const InputPack &pendingInput)
{
    if (!shouldTriggerCompaction())
        return false;
    if (SessionHostPort *host = hostPort())
    {
        const int cap = host->resolvedContextLimitForSession();
        const QString reason = QStringLiteral("auto kvUsed=%1 cap=%2").arg(host->kvUsedForSession()).arg(cap);
        host->queueCompactionInput(pendingInput, reason);
    }
    return true;
}

void SessionController::startCompactionRun(const QString &reason)
{
    SessionHostPort *host = hostPort();
    if (!host || host->compactionInFlight())
        return;
    if (sessionMessages().isEmpty())
    {
        host->setCompactionQueued(false);
        resumeSendAfterCompaction();
        return;
    }

    // 计算压缩范围：保留 system + 最后 N 条，其余做摘要
    int startIdx = 0;
    const QJsonArray messages = sessionMessages();
    if (!messages.isEmpty())
    {
        const QJsonObject first = messages.first().toObject();
        if (first.value(QStringLiteral("role")).toString() == QStringLiteral(DEFAULT_SYSTEM_NAME))
        {
            startIdx = 1;
        }
    }
    const COMPACTION_SETTINGS compaction = compactionSettings();
    const int keepTail = qMax(1, compaction.keep_last_messages);
    const int total = messages.size();
    const int toIdx = total - keepTail;
    if (toIdx <= startIdx)
    {
        // 无可压缩内容，直接继续发送
        host->setCompactionQueued(false);
        resumeSendAfterCompaction();
        return;
    }

    const QString sourceText = buildCompactionSourceText(startIdx, toIdx);
    if (sourceText.trimmed().isEmpty())
    {
        host->setCompactionQueued(false);
        resumeSendAfterCompaction();
        return;
    }

    // 准备压缩请求（不启用工具调用，避免进入工具链）
    host->beginCompactionRequest(startIdx, toIdx);

    ENDPOINT_DATA data = prepareEndpointData();
    data.date_prompt = QStringLiteral("你是上下文压缩器。请将用户与助手的历史对话压缩成可用于继续对话的摘要。"
                                      "必须保留：重要事实、关键决策、待办事项、角色/项目/时间/数值信息。"
                                      "不要输出多余解释，不要虚构内容。只输出摘要正文。");
    const QString userPrompt = QStringLiteral("以下是待压缩对话片段（role: content）。请输出不超过 %1 字符的摘要：\n\n%2")
                                   .arg(compaction.max_summary_chars)
                                   .arg(sourceText);
    QJsonArray compactionMessages;
    QJsonObject userMsg;
    userMsg.insert(QStringLiteral("role"), QStringLiteral(DEFAULT_USER_NAME));
    userMsg.insert(QStringLiteral("content"), userPrompt);
    compactionMessages.append(userMsg);
    data.messagesArray = compactionMessages;
    data.tool_call_mode = TOOL_CALL_TEXT;
    data.tools = QJsonArray();
    data.temp = compaction.temp;
    data.n_predict = compaction.n_predict;
    data.id_slot = -1; // 压缩请求不复用主会话 slot

    if (SessionFrontendPort *frontend = frontendPort())
        frontend->showSessionWarning(QStringLiteral("ui:上下文压缩中... (%1)").arg(reason), EVA_SIGNAL);
    host->syncSessionRuntimeState(true);
    host->sendEndpointData(data);
}

QString SessionController::extractMessageTextForCompaction(const QJsonObject &msg) const
{
    const QJsonValue contentVal = msg.value(QStringLiteral("content"));
    QString text;
    if (contentVal.isString())
    {
        text = contentVal.toString();
    }
    else if (contentVal.isArray())
    {
        const QJsonArray parts = contentVal.toArray();
        for (const QJsonValue &pv : parts)
        {
            if (!pv.isObject())
                continue;
            const QJsonObject po = pv.toObject();
            const QString type = po.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("text"))
            {
                text.append(po.value(QStringLiteral("text")).toString());
            }
        }
    }
    if (text.isEmpty())
        return QString();

    QString trimmed = text.trimmed();
    const COMPACTION_SETTINGS compaction = compactionSettings();
    if (trimmed.size() > compaction.max_message_chars)
    {
        trimmed = trimmed.left(compaction.max_message_chars);
        trimmed.append(QStringLiteral("..."));
    }
    return trimmed;
}

QString SessionController::buildCompactionSourceText(int fromIndex, int toIndex) const
{
    const QJsonArray messages = sessionMessages();
    if (fromIndex < 0)
        fromIndex = 0;
    if (toIndex > messages.size())
        toIndex = messages.size();
    if (fromIndex >= toIndex)
        return QString();

    QStringList lines;
    int totalChars = 0;
    const COMPACTION_SETTINGS compaction = compactionSettings();
    for (int i = fromIndex; i < toIndex; ++i)
    {
        const QJsonObject msg = messages.at(i).toObject();
        if (msg.isEmpty())
            continue;
        QString role = msg.value(QStringLiteral("role")).toString();
        if (role == QStringLiteral("model"))
            role = QStringLiteral("assistant");
        if (role == QStringLiteral("assistant"))
            role = QStringLiteral("assistant");
        if (role == QStringLiteral("tool"))
        {
            const QString toolName = msg.value(QStringLiteral("tool")).toString().trimmed();
            if (!toolName.isEmpty())
                role = QStringLiteral("tool:%1").arg(toolName);
        }
        if (role == QStringLiteral("compact"))
            role = QStringLiteral("summary");

        const QString content = extractMessageTextForCompaction(msg);
        if (content.isEmpty())
            continue;
        QString line = QStringLiteral("%1: %2").arg(role, content);

        const int nextTotal = totalChars + line.size();
        if (compaction.max_source_chars > 0 && nextTotal > compaction.max_source_chars)
        {
            const int remain = compaction.max_source_chars - totalChars;
            if (remain <= 0)
                break;
            line = line.left(remain);
            lines << line;
            totalChars = compaction.max_source_chars;
            break;
        }
        lines << line;
        totalChars += line.size() + 1;
    }
    return lines.join(QStringLiteral("\n"));
}

bool SessionController::appendCompactionSummaryFile(const QJsonObject &summaryObj) const
{
    const SessionHostPort *host = hostPort();
    return host ? host->appendCompactionSummary(summaryObj) : false;
}

void SessionController::applyCompactionSummary(const QString &summaryText)
{
    // 生成 compact 消息并重建 messagesArray（保留 system + compact + 尾部消息）
    const QJsonArray currentMessages = sessionMessages();
    QJsonArray newMessages;
    bool hasSystem = false;
    if (!currentMessages.isEmpty())
    {
        const QJsonObject first = currentMessages.first().toObject();
        if (first.value(QStringLiteral("role")).toString() == QStringLiteral(DEFAULT_SYSTEM_NAME))
        {
            newMessages.append(first);
            hasSystem = true;
        }
    }
    QJsonObject compactMsg;
    compactMsg.insert(QStringLiteral("role"), QStringLiteral("compact"));
    compactMsg.insert(QStringLiteral("content"), summaryText);
    compactMsg.insert(QStringLiteral("range_from"), hostPort() ? hostPort()->compactionFromIndex() : -1);
    compactMsg.insert(QStringLiteral("range_to"), hostPort() ? hostPort()->compactionToIndex() : -1);
    compactMsg.insert(QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate));
    newMessages.append(compactMsg);

    const int compactToIndex = hostPort() ? hostPort()->compactionToIndex() : 0;
    for (int i = qMax(0, compactToIndex); i < currentMessages.size(); ++i)
    {
        newMessages.append(currentMessages.at(i));
    }
    replaceConversationMessages(newMessages, true);

    const int compactMsgIndex = hasSystem ? 1 : 0;
    if (SessionHostPort *host = hostPort())
        host->remapRecordIndexesAfterCompaction(compactMsgIndex < sessionMessages().size() ? compactMsgIndex : -1);
}

void SessionController::handleCompactionReply(const QString &summaryText, const QString &reasoningText)
{
    Q_UNUSED(reasoningText);
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    host->finishCompactionRequest();

    QString summary = summaryText;
    summary.replace(QString(DEFAULT_THINK_BEGIN), QString());
    summary.replace(QString(DEFAULT_THINK_END), QString());
    summary = summary.trimmed();
    if (summary.isEmpty())
    {
        summary = QStringLiteral("（压缩结果为空）");
    }
    const COMPACTION_SETTINGS compaction = compactionSettings();
    if (compaction.max_summary_chars > 0 && summary.size() > compaction.max_summary_chars)
    {
        summary = summary.left(compaction.max_summary_chars);
        summary.append(QStringLiteral("..."));
    }

    // 如果压缩过程没有流式输出（或被静默），则此处补一个紫色记录块
    host->upsertCompactRecord(summary);

    // 写入 compaction 摘要文件（JSONL）
    QJsonObject summaryObj;
    summaryObj.insert(QStringLiteral("role"), QStringLiteral("compact"));
    summaryObj.insert(QStringLiteral("summary"), summary);
    summaryObj.insert(QStringLiteral("range_from"), host->compactionFromIndex());
    summaryObj.insert(QStringLiteral("range_to"), host->compactionToIndex());
    summaryObj.insert(QStringLiteral("kv_used"), host->kvUsedForSession());
    summaryObj.insert(QStringLiteral("ctx_cap"), host->resolvedContextLimitForSession());
    summaryObj.insert(QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate));
    appendCompactionSummaryFile(summaryObj);

    // 应用压缩结果到会话历史
    applyCompactionSummary(summary);

    // 压缩后建议新 slot 开启，避免 KV 历史残留
    host->resetKvAfterCompaction();

    // 清理本轮压缩状态
    host->clearCompactionRange();

    // 继续发送原始用户请求（若存在）
    resumeSendAfterCompaction();
}

void SessionController::resumeSendAfterCompaction()
{
    SessionHostPort *host = hostPort();
    if (!host)
        return;
    if (!host->hasPendingCompactionInput())
    {
        host->syncSessionRuntimeState(true);
        host->finishNoPendingCompaction();
        return;
    }

    const InputPack input = host->takePendingCompactionInput();

    host->setCurrentSessionTask(ConversationTask::ChatReply);
    startTurnFlow(ConversationTask::ChatReply, false);
    logCurrentTask(ConversationTask::ChatReply);
    ENDPOINT_DATA data = prepareEndpointData();
    handleChatReply(data, input);
}
