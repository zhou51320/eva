#include "widget.h"

#include "ui_widget.h"
#include "runtime/eva_runtime.h"
#include "service/backend/backend_coordinator.h"
#include "core/toolflow/tool_flow_controller.h"

#include <QColor>
#include <QJsonObject>
#include <QtGlobal>
#include <utility>

namespace
{
SIGNAL_STATE signalStateFromPayload(const QJsonObject &payload, SIGNAL_STATE fallback)
{
    const int raw = payload.value(QStringLiteral("signal_state")).toInt(static_cast<int>(fallback));
    if (raw < static_cast<int>(USUAL_SIGNAL) || raw > static_cast<int>(MATRIX_SIGNAL))
    {
        return fallback;
    }
    return static_cast<SIGNAL_STATE>(raw);
}

QColor colorFromPayload(const QJsonObject &payload)
{
    const QString name = payload.value(QStringLiteral("color")).toString();
    const QColor parsed(name);
    return parsed.isValid() ? parsed : QColor(0, 0, 0);
}

QString taskNameForRuntime(ConversationTask task)
{
    switch (task)
    {
    case ConversationTask::ChatReply: return QStringLiteral("chat");
    case ConversationTask::Completion: return QStringLiteral("completion");
    case ConversationTask::ToolLoop: return QStringLiteral("tool-loop");
    case ConversationTask::Compaction: return QStringLiteral("compaction");
    }
    return QStringLiteral("unknown");
}
} // namespace

void Widget::setRuntime(EvaRuntime *runtime)
{
    runtime_ = runtime;
    if (!backendCoordinator_) return;

    // 后端协调器只面向运行时事件槽发布后端事件，Widget 负责把运行时事件投影回界面。
    backendCoordinator_->setEventSink([runtime](RuntimeEvent event)
                                      {
                                          if (!runtime) return;
                                          runtime->publishEvent(std::move(event));
                                      });
    backendCoordinator_->setBackendStatusSink([runtime](BackendLifecycleState lifecycle,
                                                        bool ready,
                                                        const QString &endpoint,
                                                        const QString &resolvedBackend,
                                                        const QString &error)
                                             {
                                                 if (!runtime) return;
                                                 runtime->updateBackendStatus(lifecycle, ready, endpoint, resolvedBackend, error);
                                             });
}

void Widget::syncRuntimeSessionMirror(bool replaceMessages,
                                      bool projectActivity,
                                      bool projectIdentity,
                                      bool projectConfig,
                                      bool projectCounters)
{
    if (!runtime_) return;
    const RuntimeState currentState = runtime_->stateSnapshot();
    const QJsonArray sessionMessages = legacySessionMessages();
    if (replaceMessages)
    {
        runtime_->setSessionMessages(sessionMessages);
    }

    const bool useRuntimeCounters = currentState.initialized && !projectCounters;
    const int cap = qMax(0, resolvedContextLimitForUi());
    const int mirroredSlotId = useRuntimeCounters ? currentState.slotId : currentSlotId_;
    const int mirroredKvUsed = useRuntimeCounters ? currentState.kvUsed : kvUsed_;
    const int mirroredKvUsedBeforeTurn = useRuntimeCounters ? currentState.kvUsedBeforeTurn : kvUsedBeforeTurn_;
    const int mirroredKvStreamedTurn = useRuntimeCounters ? currentState.kvStreamedTurn : kvStreamedTurn_;
    const int mirroredKvTurnTokens = useRuntimeCounters ? currentState.kvTurnTokens : kvTokensTurn_;
    const int mirroredPromptTokens = useRuntimeCounters ? currentState.promptTokens : kvPromptTokensTurn_;
    const int mirroredGeneratedTokens = useRuntimeCounters ? currentState.generatedTokens : kvStreamedTurn_;
    const int mirroredReasoningTokens = useRuntimeCounters ? currentState.reasoningTokens : lastReasoningTokens_;
    int percent = useRuntimeCounters ? currentState.kvPercent : 0;
    if (!useRuntimeCounters && cap > 0)
    {
        percent = qBound(0, int(qRound(100.0 * double(qMax(0, kvUsed_)) / double(cap))), 100);
    }
    RuntimeMode mirroredMode = ui_mode == LINK_MODE ? RuntimeMode::Link : RuntimeMode::Local;
    APIS mirroredApis = apis;
    QString runtimeEndpoint = current_api;
    if (mirroredMode == RuntimeMode::Link)
        runtimeEndpoint = mirroredApis.api_endpoint;
    else if (runtimeEndpoint.trimmed().isEmpty())
        runtimeEndpoint = formatLocalEndpoint(activeServerHost_, activeServerPort_);
    if (!projectIdentity && currentState.initialized)
    {
        mirroredMode = currentState.mode;
        mirroredApis = currentState.apis;
        runtimeEndpoint = currentState.endpoint;
    }
    ConversationMode mirroredConversation = ui_state == COMPLETE_STATE ? ConversationMode::Complete : ConversationMode::Chat;
    SETTINGS mirroredSettings = ui_SETTINGS;
    QString mirroredSystemPrompt = ui_DATES.date_prompt;
    QStringList mirroredStopwords = ui_DATES.extra_stop_words;
    int mirroredToolCallMode = ui_tool_call_mode;
    COMPACTION_SETTINGS mirroredCompactionSettings = compactionSettings_;
    if (!projectConfig && currentState.initialized)
    {
        mirroredConversation = currentState.conversationMode;
        mirroredSettings = currentState.settings;
        mirroredSystemPrompt = currentState.systemPrompt;
        mirroredStopwords = currentState.stopwords;
        mirroredToolCallMode = currentState.toolCallMode;
        mirroredCompactionSettings = currentState.compactionSettings;
    }
    bool mirroredTurnActive = runtime_ ? currentState.turnActive : turnActive_;
    bool mirroredToolActive = runtime_ ? currentState.toolActive : toolInvocationActive_;
    quint64 mirroredTurnId = runtime_ ? currentState.activeTurnId : activeTurnId_;
    QString mirroredTask = taskNameForRuntime(currentTask_);
    bool mirroredCompactionActive = compactionInFlight();
    bool mirroredCompactionQueued = compactionQueued();
    if (!projectActivity && currentState.initialized)
    {
        mirroredTurnActive = currentState.turnActive;
        mirroredToolActive = currentState.toolActive;
        mirroredTurnId = currentState.activeTurnId;
        if (!currentState.currentTask.isEmpty())
            mirroredTask = currentState.currentTask;
        mirroredCompactionActive = currentState.compactionActive;
        mirroredCompactionQueued = currentState.compactionQueued;
    }
    runtime_->updateSessionRuntimeState(sessionMessages,
                                        history_ ? history_->sessionId() : QString(),
                                        mirroredConversation,
                                        mirroredSystemPrompt,
                                        mirroredStopwords,
                                        mirroredToolCallMode,
                                        mirroredMode,
                                        mirroredSettings,
                                        mirroredApis,
                                        runtimeEndpoint,
                                        mirroredTask,
                                        tool_result,
                                        lastToolPendingName_,
                                        pendingToolCallId_,
                                        lastToolCallName_,
                                        mirroredCompactionSettings,
                                        mirroredCompactionActive,
                                        mirroredCompactionQueued,
                                        compactionFromIndex_,
                                        compactionToIndex_,
                                        compactionPendingHasInput_,
                                        compactionReason_,
                                        mirroredTurnActive,
                                        mirroredToolActive,
                                        mirroredTurnId,
                                        mirroredSlotId,
                                        mirroredKvUsed,
                                        mirroredKvUsedBeforeTurn,
                                        mirroredKvStreamedTurn,
                                        mirroredKvTurnTokens,
                                        cap,
                                        percent,
                                        mirroredPromptTokens,
                                        mirroredGeneratedTokens,
                                        mirroredReasoningTokens);
}

quint64 Widget::runtimeActiveTurnIdForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.activeTurnId : activeTurnId_;
}

bool Widget::runtimeTurnActiveForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.turnActive : turnActive_;
}

bool Widget::runtimeToolActiveForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.toolActive : toolInvocationActive_;
}

bool Widget::runtimeBusyForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
    {
        return state.turnActive ||
               state.toolActive ||
               state.compactionActive ||
               state.phase == RuntimePhase::Running ||
               state.phase == RuntimePhase::ToolRunning;
    }
    return is_run || runtimeTurnActiveForUi() || runtimeToolActiveForUi();
}

RuntimeMode Widget::runtimeModeForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
        return state.mode;
    return ui_mode == LINK_MODE ? RuntimeMode::Link : RuntimeMode::Local;
}

ConversationMode Widget::runtimeConversationModeForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
        return state.conversationMode;
    return ui_state == COMPLETE_STATE ? ConversationMode::Complete : ConversationMode::Chat;
}

bool Widget::runtimeBackendReadyForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
        return state.backendReady;
    if (ui_mode == LINK_MODE)
        return !apis.api_endpoint.trimmed().isEmpty();
    return backendOnline_ || is_load;
}

bool Widget::runtimeEndpointReadyForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
    {
        if (state.mode == RuntimeMode::Link)
            return !state.endpoint.trimmed().isEmpty() || !state.apis.api_endpoint.trimmed().isEmpty();
        // 本地惰性卸载后，前端代理端点仍然可用，发送动作会先唤醒后端。
        // 因此“可发送端点”和“后端进程正在运行”不能再复用同一个 bool。
        if (state.backendReady)
            return true;
        return state.backendLifecycle == BackendLifecycleState::Sleeping &&
               (!state.endpoint.trimmed().isEmpty() || !state.apis.api_endpoint.trimmed().isEmpty());
    }
    return is_load || ui_mode == LINK_MODE;
}

bool Widget::ensureRuntimeReadyForSend()
{
    if (runtimeModeForUi() != RuntimeMode::Local)
    {
        pendingSendAfterWake_ = false;
        return true;
    }

    const bool serverRunning = serverManager && serverManager->isRunning();
    const bool backendReady = serverRunning && runtimeEndpointReadyForUi() && !lazyUnloaded_ && !lazyWakeInFlight_;
    if (backendReady)
    {
        pendingSendAfterWake_ = false;
        return true;
    }

    if (!pendingSendAfterWake_)
    {
        pendingSendAfterWake_ = true;
        reflash_state(QStringLiteral("ui:") + jtr("pop wake hint"), SIGNAL_SIGNAL);
    }
    if (serverManager && !lazyWakeInFlight_)
    {
        ensureLocalServer(true);
    }
    return false;
}

void Widget::projectRuntimePushingState()
{
    is_run = true;
    ui_state_pushing();
    syncRuntimeSessionMirror(false, true);
}

void Widget::projectRuntimeIdleState(bool clearTurnId)
{
    turnActive_ = false;
    is_run = false;
    if (clearTurnId && !runtime_)
        activeTurnId_ = 0;
    ui_state_normal();
    syncRuntimeSessionMirror(false, true);
}

void Widget::projectRuntimeErrorIdleState(const QString &error, bool clearTurnId)
{
    turnActive_ = false;
    toolInvocationActive_ = false;
    is_run = false;
    if (clearTurnId && !runtime_)
        activeTurnId_ = 0;

    syncRuntimeSessionMirror(false, true);
    if (runtime_)
    {
        runtime_->setErrorState(error);
    }
}

void Widget::projectRuntimeTurnObserved()
{
    if (!runtimeTurnActiveForUi())
        turnActive_ = true;
    syncRuntimeSessionMirror(false, true);
}

void Widget::projectRuntimeBackendOfflineState(bool clearLoad)
{
    backendOnline_ = false;
    if (clearLoad)
        is_load = false;
    syncRuntimeSessionMirror(false);
}

void Widget::projectRuntimeLocalLoadingState()
{
    ui_mode = LOCAL_MODE;
    is_load = false;
    backendOnline_ = false;
    current_api.clear();
    apis.is_local_backend = true;
    apis.api_chat_endpoint = QStringLiteral(CHAT_ENDPOINT);
    apis.api_completion_endpoint = QStringLiteral(COMPLETION_ENDPOINT);

    emit ui2expend_mode(LOCAL_MODE);
    syncRuntimeSessionMirror(false, false, true, true);
}

void Widget::projectRuntimeLocalReadyState(const APIS &localApis, const QString &frontendEndpoint)
{
    ui_mode = LOCAL_MODE;
    is_load = true;
    backendOnline_ = true;
    apis = localApis;
    current_api = frontendEndpoint.trimmed();
    if (current_api.isEmpty())
        current_api = formatLocalEndpoint(activeServerHost_, activeServerPort_);

    emit ui2expend_apis(apis);
    emit ui2expend_mode(LOCAL_MODE);
    syncRuntimeSessionMirror(false, false, true, true);
}

void Widget::projectRuntimeLinkReadyState(const APIS &linkApis)
{
    ui_mode = LINK_MODE;
    is_load = false;
    backendOnline_ = false;
    apis = linkApis;
    current_api = apis.api_endpoint +
                  (runtimeConversationModeForUi() == ConversationMode::Chat ? apis.api_chat_endpoint
                                                                            : apis.api_completion_endpoint);
    if (current_api.trimmed().isEmpty())
        current_api = sessionEndpointForHistory();

    emit ui2expend_apis(apis);
    emit ui2expend_mode(LINK_MODE);
    syncRuntimeSessionMirror(false, false, true, true);
}

void Widget::projectRuntimeControlLinkState()
{
    ui_mode = LINK_MODE;
    is_load = false;
    backendOnline_ = false;
    syncRuntimeSessionMirror(false, false, true, true);
}

bool Widget::runtimeBackendTransitioningForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
    {
        return state.backendLifecycle == BackendLifecycleState::Starting ||
               state.backendLifecycle == BackendLifecycleState::Restarting ||
               state.backendLifecycle == BackendLifecycleState::Waking;
    }
    return isBackendLifecycleTransitioning();
}

int Widget::runtimeKvUsedForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.kvUsed : kvUsed_;
}

int Widget::runtimePromptTokensForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.promptTokens : kvPromptTokensTurn_;
}

int Widget::runtimeStreamedTokensForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.generatedTokens : kvStreamedTurn_;
}

int Widget::runtimeReasoningTokensForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.reasoningTokens : lastReasoningTokens_;
}

QString Widget::controlPhaseForUi() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? runtimePhaseName(state.phase) : controlUiPhase_;
}

QString Widget::takeDraftText(bool allowText)
{
    if (!allowText || !ui || !ui->input || !ui->input->textEdit)
        return QString();
    const QString text = ui->input->textEdit->toPlainText().toUtf8().data();
    ui->input->textEdit->clear();
    return text;
}

QStringList Widget::draftImageFilePaths() const
{
    return (ui && ui->input) ? ui->input->imageFilePaths() : QStringList();
}

QStringList Widget::draftDocumentFilePaths() const
{
    return (ui && ui->input) ? ui->input->documentFilePaths() : QStringList();
}

QStringList Widget::draftAudioFilePaths() const
{
    return (ui && ui->input) ? ui->input->wavFilePaths() : QStringList();
}

void Widget::clearDraftAttachments()
{
    if (ui && ui->input)
        ui->input->clearThumbnails();
}

bool Widget::shouldAttachControllerFrame() const
{
    return ui_controller_ischecked;
}

QString Widget::captureControllerFrameImagePath()
{
    return captureControllerFrame().imagePath;
}

void Widget::rememberControllerFrameForModel(const QString &imagePath)
{
    lastControllerImagePathForModel_ = imagePath;
}

void Widget::presentUserMessageRecord(const QString &text, int messageIndex)
{
    if (text.isEmpty() || engineerProxyRuntime_.active)
        return;
    if (runtime_)
    {
        RuntimeEvent event;
        event.type = RuntimeEventType::RecordAdd;
        event.role = QStringLiteral("user");
        event.text = text;
        event.payload.insert(QStringLiteral("record_role"), QStringLiteral("user"));
        event.payload.insert(QStringLiteral("message_index"), messageIndex);
        runtime_->publishEvent(event);
        return;
    }

    flushPendingStream();
    const int recIdx = recordCreate(RecordRole::User);
    appendRoleHeader(QStringLiteral("user"));
    reflash_output(text, false, textColorForRole(RecordRole::User));
    recordAppendText(recIdx, text);
    if (messageIndex >= 0 && recIdx >= 0 && recIdx < recordEntries_.size())
    {
        recordEntries_[recIdx].msgIndex = messageIndex;
    }
}

void Widget::presentToolMessageRecord(const QString &toolName, const QString &text, int messageIndex)
{
    if (text.isEmpty() || engineerProxyRuntime_.active)
        return;
    flushPendingStream();
    int recIdx = currentToolRecordIndex_;
    if (recIdx < 0)
    {
        recIdx = recordCreate(RecordRole::Tool, toolName);
    }
    appendRoleHeader(QStringLiteral("tool"));
    reflash_output(text, false, textColorForRole(RecordRole::Tool));
    recordAppendText(recIdx, text);
    if (recIdx >= 0 && recIdx < recordEntries_.size())
    {
        recordEntries_[recIdx].msgIndex = messageIndex;
    }
    if (recIdx == currentToolRecordIndex_)
    {
        currentToolRecordIndex_ = -1;
    }
}

bool Widget::outputDocumentEmpty() const
{
    return !ui || !ui->output || !ui->output->document() || ui->output->document()->isEmpty();
}

void Widget::showSessionWarning(const QString &message, SIGNAL_STATE state)
{
    reflash_state(message, state);
}

RuntimeState Widget::runtimeStateSnapshotForSession() const
{
    return runtime_ ? runtime_->stateSnapshot() : RuntimeState();
}

bool Widget::runtimeSessionReady() const
{
    return runtime_ && runtime_->stateSnapshot().initialized;
}

void Widget::syncSessionRuntimeState(bool replaceMessages)
{
    syncRuntimeSessionMirror(replaceMessages);
}

QJsonArray Widget::legacySessionMessages() const
{
    if (runtimeSessionReady())
    {
        const QJsonArray runtimeMessages = runtime_->stateSnapshot().messages;
        if (!runtimeMessages.isEmpty() || ui_messagesArray.isEmpty())
            return runtimeMessages;
    }
    return ui_messagesArray;
}

void Widget::setLegacySessionMessages(const QJsonArray &messages)
{
    ui_messagesArray = messages;
    if (runtimeSessionReady())
        runtime_->setSessionMessages(messages);
}

int Widget::appendRuntimeSessionMessage(const QJsonObject &message)
{
    if (runtimeSessionReady())
    {
        if (runtime_->stateSnapshot().messages.isEmpty() && !ui_messagesArray.isEmpty())
        {
            runtime_->setSessionMessages(ui_messagesArray);
        }
        const int index = runtime_->appendSessionMessage(message);
        ui_messagesArray = runtime_->stateSnapshot().messages;
        return index;
    }
    ui_messagesArray.append(message);
    return ui_messagesArray.size() - 1;
}

bool Widget::replaceRuntimeSessionMessage(int index, const QJsonObject &message)
{
    if (runtimeSessionReady())
    {
        const bool ok = runtime_->replaceSessionMessage(index, message);
        ui_messagesArray = runtime_->stateSnapshot().messages;
        return ok;
    }
    if (index < 0 || index >= ui_messagesArray.size())
        return false;
    ui_messagesArray.replace(index, message);
    return true;
}

SETTINGS Widget::sessionSettingsSnapshot() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.settings : ui_SETTINGS;
}

APIS Widget::sessionApisSnapshot() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.apis : apis;
}

QString Widget::sessionSystemPrompt() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.systemPrompt : ui_DATES.date_prompt;
}

QStringList Widget::sessionStopwords() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.stopwords : ui_DATES.extra_stop_words;
}

ConversationMode Widget::sessionConversationMode() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
        return state.conversationMode;
    return ui_state == COMPLETE_STATE ? ConversationMode::Complete : ConversationMode::Chat;
}

int Widget::sessionToolCallMode() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.toolCallMode : ui_tool_call_mode;
}

QJsonArray Widget::sessionFunctionTools() const
{
    return buildFunctionTools();
}

int Widget::sessionSlotId() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.slotId : currentSlotId_;
}

quint64 Widget::sessionActiveTurnId() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.activeTurnId : activeTurnId_;
}

QString Widget::sessionModeName() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized)
        return runtimeModeName(state.mode);
    return ui_mode == LINK_MODE ? QStringLiteral("link") : QStringLiteral("local");
}

QString Widget::sessionEndpointForHistory() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized && !state.endpoint.isEmpty())
        return state.endpoint;
    if (state.initialized && state.mode == RuntimeMode::Link)
        return state.apis.api_endpoint + (state.conversationMode == ConversationMode::Chat ? state.apis.api_chat_endpoint
                                                                                           : state.apis.api_completion_endpoint);
    if (ui_mode == LINK_MODE)
        return apis.api_endpoint + ((ui_state == CHAT_STATE) ? apis.api_chat_endpoint : apis.api_completion_endpoint);
    return formatLocalEndpoint(activeServerHost_, activeServerPort_);
}

QString Widget::sessionModelForHistory() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    if (state.initialized && !state.currentModel.isEmpty())
        return state.currentModel;
    if (state.initialized)
        return state.mode == RuntimeMode::Link ? state.apis.api_model : state.settings.modelpath;
    return ui_mode == LINK_MODE ? apis.api_model : ui_SETTINGS.modelpath;
}

int Widget::sessionContextSize() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.settings.nctx : ui_SETTINGS.nctx;
}

bool Widget::shouldBeginHistorySession() const
{
    return history_ && sessionConversationMode() == ConversationMode::Chat && history_->sessionId().isEmpty();
}

QString Widget::historySessionId() const
{
    return history_ ? history_->sessionId() : QString();
}

void Widget::beginHistorySession(const SessionMeta &meta)
{
    if (history_)
        history_->begin(meta);
}

void Widget::appendHistoryMessage(const QJsonObject &message)
{
    if (history_)
        history_->appendMessage(message);
}

void Widget::rewriteHistoryMessages(const QJsonArray &messages)
{
    if (history_ && sessionConversationMode() == ConversationMode::Chat)
        history_->rewriteAllMessages(messages);
}

QString Widget::pendingToolResult() const
{
    return tool_result;
}

QString Widget::pendingToolName() const
{
    return lastToolPendingName_;
}

QString Widget::pendingToolCallId() const
{
    return pendingToolCallId_;
}

QString Widget::lastToolCallName() const
{
    return lastToolCallName_;
}

void Widget::clearPendingToolResult()
{
    tool_result.clear();
    syncRuntimeSessionMirror(true);
}

void Widget::noteBackendActivity()
{
    markBackendActivity();
}

void Widget::cancelSessionLazyUnload(const QString &reason)
{
    cancelLazyUnload(reason);
}

void Widget::sendEndpointData(const ENDPOINT_DATA &data)
{
    emit_send(data);
}

quint64 Widget::startSessionTurn(const QString &taskName, bool isToolLoop, bool continuingTool)
{
    quint64 turnId = 0;
    if (runtime_)
    {
        turnId = runtime_->beginTurn(taskName, isToolLoop);
    }
    else if (activeTurnId_ == 0 || !turnActive_)
    {
        activeTurnId_ = nextTurnId_++;
        turnActive_ = true;
        turnId = activeTurnId_;
    }
    else
    {
        turnId = activeTurnId_;
    }
    toolInvocationActive_ = false;
    const QString detail = QStringLiteral("task=%1 mode=%2 tool_cont=%3").arg(taskName, sessionModeName(), continuingTool ? QStringLiteral("yes") : QStringLiteral("no"));
    logFlow(FlowPhase::Start, detail, SIGNAL_SIGNAL);
    emit ui2tool_turn(turnId);
    syncRuntimeSessionMirror(true, true);
    return turnId;
}

void Widget::finishSessionTurn(const QString &reason, bool success)
{
    const quint64 runtimeTurnId = runtimeActiveTurnIdForUi();
    if (!runtime_ && activeTurnId_ == 0 && runtimeTurnId == 0 && !runtimeTurnActiveForUi())
        return;
    if (!runtime_ && activeTurnId_ == 0)
        activeTurnId_ = runtimeTurnId;
    const QString detail = QStringLiteral("%1 kvUsed=%2").arg(reason).arg(runtimeKvUsedForUi());
    logFlow(FlowPhase::Finish, detail, success ? SIGNAL_SIGNAL : WRONG_SIGNAL);
    if (runtime_)
        runtime_->finishTurn(reason, success);
    setToolFlowInvocationActive(false);
    projectRuntimeIdleState(true);
    syncRuntimeSessionMirror(true, true);
}

void Widget::logSessionFlow(FlowPhase phase, const QString &detail, SIGNAL_STATE state)
{
    logFlow(phase, detail, state);
}

bool Widget::canAutoCompact() const
{
    return sessionConversationMode() == ConversationMode::Chat &&
           !compactionInFlight() &&
           !compactionQueued() &&
           !engineerProxyRuntime_.active;
}

COMPACTION_SETTINGS Widget::sessionCompactionSettings() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionSettings : compactionSettings_;
}

int Widget::resolvedContextLimitForSession() const
{
    return resolvedContextLimitForUi();
}

int Widget::kvUsedForSession() const
{
    return runtimeKvUsedForUi();
}

bool Widget::compactionInFlight() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionActive : compactionInFlight_;
}

bool Widget::compactionQueued() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionQueued : compactionQueued_;
}

void Widget::setCompactionQueued(bool queued)
{
    compactionQueued_ = queued;
    syncRuntimeSessionMirror(true, true);
}

void Widget::queueCompactionInput(const InputPack &input, const QString &reason)
{
    compactionPendingInput_ = input;
    compactionPendingHasInput_ = true;
    compactionQueued_ = true;
    compactionReason_ = reason;
    syncRuntimeSessionMirror(true, true);
}

void Widget::beginCompactionRequest(int fromIndex, int toIndex)
{
    compactionFromIndex_ = fromIndex;
    compactionToIndex_ = toIndex;
    compactionInFlight_ = true;
    compactionQueued_ = false;
    compactionHeaderPrinted_ = false;
    currentCompactIndex_ = -1;
    temp_assistant_history.clear();
    pendingAssistantHeaderReset_ = true;
    currentThinkIndex_ = -1;
    currentAssistantIndex_ = -1;
    syncRuntimeSessionMirror(true, true);
}

void Widget::finishCompactionRequest()
{
    compactionInFlight_ = false;
    compactionHeaderPrinted_ = false;
    syncRuntimeSessionMirror(true, true);
}

int Widget::compactionFromIndex() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionFromIndex : compactionFromIndex_;
}

int Widget::compactionToIndex() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionToIndex : compactionToIndex_;
}

void Widget::clearCompactionRange()
{
    compactionFromIndex_ = -1;
    compactionToIndex_ = -1;
    compactionReason_.clear();
    syncRuntimeSessionMirror(true);
}

bool Widget::hasPendingCompactionInput() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.compactionPendingInput : compactionPendingHasInput_;
}

InputPack Widget::takePendingCompactionInput()
{
    const InputPack input = compactionPendingInput_;
    compactionPendingHasInput_ = false;
    compactionPendingInput_ = InputPack();
    syncRuntimeSessionMirror(true);
    return input;
}

void Widget::resetKvAfterCompaction()
{
    currentSlotId_ = -1;
    kvUsed_ = 0;
    kvUsedBeforeTurn_ = 0;
    kvStreamedTurn_ = 0;
    updateKvBarUi();
    syncRuntimeSessionMirror(true);
}

void Widget::finishNoPendingCompaction()
{
    normal_finish_pushover();
}

void Widget::setCurrentSessionTask(ConversationTask task)
{
    currentTask_ = task;
}

bool Widget::appendCompactionSummary(const QJsonObject &summaryObj) const
{
    if (!history_)
        return false;
    const QString sessionId = history_->sessionId();
    if (sessionId.isEmpty())
        return false;
    const QString baseDir = QDir(applicationDirPath).filePath(QStringLiteral("EVA_TEMP/compaction/%1").arg(sessionId));
    QDir().mkpath(baseDir);
    QFile f(QDir(baseDir).filePath(QStringLiteral("summary.jsonl")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    QJsonDocument doc(summaryObj);
    QByteArray line = doc.toJson(QJsonDocument::Compact);
    line.append('\n');
    f.write(line);
    f.close();
    return true;
}

int Widget::upsertCompactRecord(const QString &summary)
{
    if (currentCompactIndex_ < 0)
    {
        currentCompactIndex_ = appendCompactRecord(summary);
    }
    else
    {
        updateRecordEntryContent(currentCompactIndex_, summary);
    }
    return currentCompactIndex_;
}

void Widget::remapRecordIndexesAfterCompaction(int compactMessageIndex)
{
    for (RecordEntry &entry : recordEntries_)
    {
        entry.msgIndex = -1;
    }
    if (currentCompactIndex_ >= 0 && currentCompactIndex_ < recordEntries_.size() && compactMessageIndex >= 0)
    {
        recordEntries_[currentCompactIndex_].msgIndex = compactMessageIndex;
    }
}

void Widget::presentSystemMessageRecord(const QString &systemText, int messageIndex)
{
    const bool needRecord = sessionConversationMode() == ConversationMode::Chat;
    const bool engineerProxyWasActive = engineerProxyRuntime_.active;
    engineerProxyRuntime_.active = false;
    if (needRecord && (lastSystemRecordIndex_ < 0 || outputDocumentEmpty()))
    {
        const int idx = recordCreate(RecordRole::System);
        appendRoleHeader(QStringLiteral("system"));
        reflash_output_tool_highlight(systemText, themeTextPrimary());
        recordAppendText(idx, systemText);
        lastSystemRecordIndex_ = idx;
        if (messageIndex >= 0 && idx >= 0 && idx < recordEntries_.size())
        {
            recordEntries_[idx].msgIndex = messageIndex;
        }
        logFlow(FlowPhase::Build, QStringLiteral("system header inserted"), SIGNAL_SIGNAL);
    }
    engineerProxyRuntime_.active = engineerProxyWasActive;
}

void Widget::flushToolFlowStream()
{
    flushPendingStream();
}

void Widget::clearToolFlowCallMarkers()
{
    lastToolCallName_.clear();
    lastToolPendingName_.clear();
    pendingToolCallId_.clear();
}

void Widget::syncToolFlowRuntimeState(bool replaceMessages)
{
    syncRuntimeSessionMirror(replaceMessages);
}

QJsonArray Widget::takeToolFlowPendingCalls()
{
    const QJsonArray calls = pendingToolCallsPayload_;
    pendingToolCallsPayload_ = QJsonArray();
    return calls;
}

QString Widget::assistantStreamText() const
{
    return temp_assistant_history;
}

void Widget::setAssistantStreamText(const QString &text)
{
    temp_assistant_history = text;
}

void Widget::resetAssistantStreamIndexes()
{
    currentThinkIndex_ = -1;
    currentAssistantIndex_ = -1;
}

bool Widget::compactionActiveForToolFlow() const
{
    return compactionInFlight();
}

void Widget::handleToolFlowCompactionReply(const QString &summaryText, const QString &reasoningText)
{
    handleCompactionReply(summaryText, reasoningText);
}

int Widget::promptTokensForToolFlow() const
{
    return runtimePromptTokensForUi();
}

int Widget::generatedTokensForToolFlow() const
{
    return runtimeStreamedTokensForUi();
}

int Widget::reasoningTokensForToolFlow() const
{
    return runtimeReasoningTokensForUi();
}

int Widget::usedTokensForToolFlow() const
{
    return runtimeKvUsedForUi();
}

int Widget::turnTokensForToolFlow() const
{
    const RuntimeState state = runtimeStateSnapshotForSession();
    return state.initialized ? state.kvTurnTokens : kvTokensTurn_;
}

void Widget::logToolFlow(FlowPhase phase, const QString &detail, SIGNAL_STATE state)
{
    logFlow(phase, detail, state);
}

int Widget::toolCallModeForToolFlow() const
{
    return sessionToolCallMode();
}

bool Widget::engineerProxyActiveForToolFlow() const
{
    return engineerProxyRuntime_.active;
}

void Widget::handleToolFlowEngineerAssistant(const QString &message, const QString &reasoning)
{
    handleEngineerAssistantMessage(message, reasoning);
}

int Widget::appendToolFlowAssistantMessage(const QJsonObject &message)
{
    const int index = appendRuntimeSessionMessage(message);
    if (history_ && sessionConversationMode() == ConversationMode::Chat)
    {
        history_->appendMessage(message);
    }
    syncRuntimeSessionMirror(true);
    return index;
}

void Widget::bindToolFlowReasoningRecord(int messageIndex)
{
    if (currentThinkIndex_ >= 0 && currentThinkIndex_ < recordEntries_.size())
    {
        recordEntries_[currentThinkIndex_].msgIndex = messageIndex;
    }
}

void Widget::bindToolFlowAssistantRecord(int messageIndex)
{
    if (currentAssistantIndex_ >= 0 && currentAssistantIndex_ < recordEntries_.size())
    {
        recordEntries_[currentAssistantIndex_].msgIndex = messageIndex;
    }
}

void Widget::publishToolFlowAssistantRecord(const QString &text, const QString &reasoning, int messageIndex)
{
    if (!runtime_)
        return;
    RuntimeEvent event;
    event.type = RuntimeEventType::RecordAdd;
    event.role = QStringLiteral("assistant");
    event.text = text;
    event.payload.insert(QStringLiteral("record_role"), QStringLiteral("assistant"));
    event.payload.insert(QStringLiteral("message_index"), messageIndex);
    if (!reasoning.isEmpty())
    {
        event.payload.insert(QStringLiteral("reasoning"), reasoning);
    }
    runtime_->publishEvent(event);
}

void Widget::refreshToolFlowAssistantRecordIfNeeded(const QString &text)
{
    const bool hasToolCallBlock = text.contains(QStringLiteral("<tool_call>")) &&
                                  text.contains(QStringLiteral("</tool_call>"));
    if (hasToolCallBlock && currentAssistantIndex_ >= 0)
    {
        updateRecordEntryContent(currentAssistantIndex_, text);
    }
}

bool Widget::completeModeForToolFlow() const
{
    return sessionConversationMode() == ConversationMode::Complete;
}

bool Widget::toolsEnabledForToolFlow() const
{
    return is_load_tool;
}

void Widget::finishNormalToolFlow()
{
    normal_finish_pushover();
}

void Widget::resetConversationAfterToolFlowCompletion()
{
    on_reset_clicked();
}

void Widget::setToolFlowCurrentCall(const mcp::json &call, const QString &callId)
{
    tools_call = call;
    pendingToolCallId_ = callId;
}

mcp::json Widget::currentToolFlowCall() const
{
    return tools_call;
}

void Widget::rememberToolFlowPendingName(const QString &toolName)
{
    lastToolCallName_ = toolName;
    lastToolPendingName_ = toolName;
}

void Widget::clearToolFlowPendingCallId()
{
    pendingToolCallId_.clear();
}

void Widget::showToolFlowClicked(const QString &toolName)
{
    reflash_state(QStringLiteral("ui:") + jtr("clicked") + QStringLiteral(" ") + toolName, SIGNAL_SIGNAL);
}

void Widget::publishToolFlowStarted(const QString &toolName)
{
    if (!runtime_)
        return;
    RuntimeEvent event;
    event.type = RuntimeEventType::ToolStarted;
    event.name = toolName;
    event.payload.insert(QStringLiteral("source"), QStringLiteral("toolflow"));
    runtime_->publishEvent(event);
}

void Widget::createLegacyToolFlowRecordIfNeeded(const QString &toolName)
{
    if (!runtime_ && toolName != QStringLiteral("answer") && toolName != QStringLiteral("response"))
    {
        currentToolRecordIndex_ = recordCreate(RecordRole::Tool, toolName);
    }
}

void Widget::startToolFlowEngineerProxy()
{
    startEngineerProxyTool(tools_call);
}

void Widget::handleToolFlowScheduleCall()
{
    handleScheduleToolCall(tools_call);
}

void Widget::markToolFlowAssistantHeaderReset()
{
    pendingAssistantHeaderReset_ = true;
}

void Widget::setToolFlowInvocationActive(bool active)
{
    toolInvocationActive_ = active;
    if (runtime_ && active && !runtimeToolActiveForUi())
    {
        runtime_->beginTurn(QStringLiteral("tool-loop"), true);
    }
    else if (!active && runtime_)
    {
        activeTurnId_ = runtimeActiveTurnIdForUi();
    }
    syncRuntimeSessionMirror(true, true);
}

quint64 Widget::activeToolFlowTurnId() const
{
    return runtimeActiveTurnIdForUi();
}

void Widget::emitToolFlowTurn(quint64 turnId)
{
    emit ui2tool_turn(turnId);
}

void Widget::emitToolFlowExec()
{
    emit ui2tool_exec(tools_call);
}

QString Widget::lastAssistantMessageTextForToolFlow() const
{
    const QJsonArray sessionMessages = legacySessionMessages();
    if (sessionMessages.isEmpty())
        return QString();
    return sessionMessages.last().toObject().value(QStringLiteral("content")).toString();
}

mcp::json Widget::parseTextToolCallForToolFlow(const QString &text)
{
    return XMLparser(text);
}

void Widget::noteToolFlowBackendActivity()
{
    markBackendActivity();
}

void Widget::scheduleToolFlowLazyUnload()
{
    scheduleLazyUnload();
}

void Widget::setToolFlowPendingCalls(const QJsonArray &calls)
{
    pendingToolCallsPayload_ = calls;
    syncRuntimeSessionMirror(true);
}

void Widget::clearToolFlowPendingCalls()
{
    pendingToolCallsPayload_ = QJsonArray();
    pendingToolCallId_.clear();
    syncRuntimeSessionMirror(true);
}

void Widget::renderToolFlowCallPayload(const QString &displayText)
{
    if (displayText.isEmpty())
        return;
    if (!temp_assistant_history.isEmpty() && !temp_assistant_history.endsWith('\n'))
    {
        QString text = displayText;
        text.prepend('\n');
        reflash_output(text, true, themeTextPrimary());
        return;
    }
    reflash_output(displayText, true, themeTextPrimary());
}

void Widget::setToolFlowResultFromRaw(const QString &toolResult)
{
    if (toolResult.contains(QStringLiteral("<ylsdamxssjxxdd:showdraw>")))
    {
        wait_to_show_images_filepath.append(toolResult.split(QStringLiteral("<ylsdamxssjxxdd:showdraw>"))[1]);
        tool_result = QStringLiteral("stablediffusion ") + jtr("call successful, image save at") + QStringLiteral(" ") + toolResult.split(QStringLiteral("<ylsdamxssjxxdd:showdraw>"))[1];
    }
    else
    {
        tool_result = truncateString(toolResult, DEFAULT_MAX_INPUT);
    }
    setToolFlowInvocationActive(false);
}

QString Widget::toolFlowResult() const
{
    return tool_result;
}

int Widget::toolFlowImageResultCount() const
{
    return wait_to_show_images_filepath.size();
}

QString Widget::pendingToolFlowName() const
{
    return lastToolPendingName_;
}

void Widget::publishToolFlowFinished()
{
    if (!runtime_)
        return;
    RuntimeEvent event;
    event.type = RuntimeEventType::ToolFinished;
    event.name = lastToolPendingName_;
    event.text = tool_result;
    event.payload.insert(QStringLiteral("result_length"), tool_result.size());
    event.payload.insert(QStringLiteral("image_count"), wait_to_show_images_filepath.size());
    runtime_->publishEvent(event);
}

void Widget::handleToolFlowEngineerObservation(const QString &observation)
{
    handleEngineerToolResult(observation);
}

void Widget::clearToolFlowResult()
{
    tool_result.clear();
    syncRuntimeSessionMirror(true);
}

void Widget::continueToolFlowSend()
{
    on_send_clicked();
}

void Widget::handleRuntimeEvent(const RuntimeEvent &event)
{
    switch (event.type)
    {
    case RuntimeEventType::BackendLog:
        reflash_state(event.text, signalStateFromPayload(event.payload, USUAL_SIGNAL));
        break;
    case RuntimeEventType::Error:
        reflash_state(event.error.isEmpty() ? event.text : event.error,
                      signalStateFromPayload(event.payload, WRONG_SIGNAL));
        break;
    case RuntimeEventType::OutputChunk:
        reflash_output(event.text,
                       event.payload.value(QStringLiteral("is_stream_chunk")).toBool(true),
                       colorFromPayload(event.payload));
        break;
    case RuntimeEventType::ToolStarted:
        if (event.name == QStringLiteral("tool_calls_detected"))
        {
            const bool runtimeDispatched = event.payload.value(QStringLiteral("runtime_tool_dispatched")).toBool(false);
            toolFlowController_->recvToolCalls(event.text, runtimeDispatched);
        }
        else if (!event.name.isEmpty() &&
                 event.name != QStringLiteral("answer") &&
                 event.name != QStringLiteral("response") &&
                 currentToolRecordIndex_ < 0)
        {
            currentToolRecordIndex_ = recordCreate(RecordRole::Tool, event.name);
        }
        break;
    case RuntimeEventType::TurnFinished:
        recv_pushover();
        break;
    case RuntimeEventType::Metrics:
        if (event.payload.contains(QStringLiteral("cache_tokens")) ||
            event.payload.contains(QStringLiteral("predicted_tokens")))
        {
            recv_turn_counters(event.payload.value(QStringLiteral("cache_tokens")).toInt(-1),
                               event.payload.value(QStringLiteral("prompt_tokens")).toInt(-1),
                               event.payload.value(QStringLiteral("predicted_tokens")).toInt(-1));
        }
        else if (event.payload.contains(QStringLiteral("kv_used")))
        {
            recv_kv_from_net(event.payload.value(QStringLiteral("kv_used")).toInt());
        }
        else if (event.payload.contains(QStringLiteral("prompt_tokens")))
        {
            recv_prompt_baseline(event.payload.value(QStringLiteral("prompt_tokens")).toInt());
        }

        if (event.payload.contains(QStringLiteral("slot_id")))
        {
            onSlotAssigned(event.payload.value(QStringLiteral("slot_id")).toInt(-1));
        }
        if (event.payload.contains(QStringLiteral("reasoning_tokens")))
        {
            recv_reasoning_tokens(event.payload.value(QStringLiteral("reasoning_tokens")).toInt());
        }
        if (event.payload.contains(QStringLiteral("prompt_per_second")) ||
            event.payload.contains(QStringLiteral("predicted_per_second")))
        {
            recv_net_speeds(event.payload.value(QStringLiteral("prompt_per_second")).toDouble(),
                            event.payload.value(QStringLiteral("predicted_per_second")).toDouble());
        }
        break;
    case RuntimeEventType::StateChanged:
    case RuntimeEventType::RecordAdd:
        if (event.role == QStringLiteral("user"))
        {
            flushPendingStream();
            const int recIdx = recordCreate(RecordRole::User);
            appendRoleHeader(QStringLiteral("user"));
            reflash_output(event.text, false, textColorForRole(RecordRole::User));
            recordAppendText(recIdx, event.text);
            const int msgIndex = event.payload.value(QStringLiteral("message_index")).toInt(-1);
            if (msgIndex >= 0 && recIdx >= 0 && recIdx < recordEntries_.size())
            {
                recordEntries_[recIdx].msgIndex = msgIndex;
            }
        }
        break;
    case RuntimeEventType::RecordUpdate:
    case RuntimeEventType::ToolFinished:
    case RuntimeEventType::CommandRejected:
        break;
    }
}
