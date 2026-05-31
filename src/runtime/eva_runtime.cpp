#include "runtime/eva_runtime.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <QtGlobal>

#include "runtime/runtime_bootstrap.h"
#include "runtime/runtime_worker_host.h"
#include "service/net/net_client.h"
#include "service/tools/tool_executor.h"
#include "utils/cpuchecker.h"
#include "utils/gpuchecker.h"
#include "utils/openai_compat.h"
#include "xmcp.h"

namespace
{
QString normalizeRuntimeEndpoint(const QString &rawEndpoint)
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

QString modelLabelFromPath(const QString &path)
{
    const QFileInfo info(path);
    return info.fileName().isEmpty() ? path : info.fileName();
}

QJsonArray parseToolCallsPayload(const QString &payload)
{
    const QString trimmed = payload.trimmed();
    if (trimmed.isEmpty()) return QJsonArray();

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) return QJsonArray();
    if (doc.isArray()) return doc.array();
    if (!doc.isObject()) return QJsonArray();

    const QJsonObject object = doc.object();
    const QJsonValue toolCalls = object.value(QStringLiteral("tool_calls"));
    if (toolCalls.isArray()) return toolCalls.toArray();

    QJsonArray calls;
    const QJsonValue functionCall = object.value(QStringLiteral("function_call"));
    if (functionCall.isObject())
        calls.append(functionCall.toObject());
    else if (!object.isEmpty())
        calls.append(object);
    return calls;
}

QString toolNameFromCall(const QJsonObject &call)
{
    const QString directName = call.value(QStringLiteral("name")).toString().trimmed();
    if (!directName.isEmpty()) return directName;

    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    return function.value(QStringLiteral("name")).toString().trimmed();
}

bool shouldRuntimeDispatchTool(const QString &toolName)
{
    return !toolName.isEmpty() &&
           toolName != QStringLiteral("answer") &&
           toolName != QStringLiteral("response") &&
           toolName != QStringLiteral("system_engineer_proxy") &&
           toolName != QStringLiteral("schedule_task");
}

QJsonObject normalizeToolCallForDriver(const QJsonObject &call)
{
    QJsonObject normalized = call;
    const QJsonObject function = call.value(QStringLiteral("function")).toObject();
    if (!function.isEmpty())
    {
        const QString name = function.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) normalized.insert(QStringLiteral("name"), name);

        const QJsonValue functionArguments = function.value(QStringLiteral("arguments"));
        if (functionArguments.isObject())
        {
            normalized.insert(QStringLiteral("arguments"), functionArguments.toObject());
        }
        else if (functionArguments.isString())
        {
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(functionArguments.toString().toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject())
                normalized.insert(QStringLiteral("arguments"), doc.object());
        }
    }

    const QJsonValue arguments = normalized.value(QStringLiteral("arguments"));
    if (arguments.isString())
    {
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(arguments.toString().toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
            normalized.insert(QStringLiteral("arguments"), doc.object());
    }
    return normalized;
}
} // namespace

EvaRuntime::EvaRuntime(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<RuntimeMode>("RuntimeMode");
    qRegisterMetaType<ConversationMode>("ConversationMode");
    qRegisterMetaType<RuntimePhase>("RuntimePhase");
    qRegisterMetaType<RuntimeState>("RuntimeState");
    qRegisterMetaType<RuntimeEventType>("RuntimeEventType");
    qRegisterMetaType<RuntimeEvent>("RuntimeEvent");
    qRegisterMetaType<RuntimeLoadLocalCommand>("RuntimeLoadLocalCommand");
    qRegisterMetaType<RuntimeConnectRemoteCommand>("RuntimeConnectRemoteCommand");
    qRegisterMetaType<RuntimeSendMessageCommand>("RuntimeSendMessageCommand");
    qRegisterMetaType<RuntimeResetCommand>("RuntimeResetCommand");
    qRegisterMetaType<RequestSnapshot>("RequestSnapshot");
    qRegisterMetaType<SIGNAL_STATE>("SIGNAL_STATE");
    qRegisterMetaType<QColor>("QColor");
    qRegisterMetaType<RuntimeNetworkDriver *>("RuntimeNetworkDriver*");
    workers_ = new RuntimeWorkerHost(this);
}

EvaRuntime::~EvaRuntime()
{
    shutdown();
}

bool EvaRuntime::initialize(const AppContext &context, QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    state_.context = context;
    state_.initialized = true;
    state_.stateSource = QStringLiteral("runtime-shell");
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    if (workers_)
    {
        workers_->start();
    }
    setPhase(RuntimePhase::Unloaded);
    return true;
}

bool EvaRuntime::initialize(QString *errorMessage)
{
    return initialize(RuntimeBootstrap::prepareContext(), errorMessage);
}

void EvaRuntime::shutdown()
{
    if (!state_.initialized)
        return;
    setPhase(RuntimePhase::ShuttingDown);
    if (networkDriver_)
    {
        QMetaObject::invokeMethod(networkDriver_, "stop", Qt::QueuedConnection, Q_ARG(bool, true));
    }
    if (toolDriver_)
    {
        toolDriver_->cancelActiveRuntimeTool();
    }
    if (ToolExecutor *tool = qobject_cast<ToolExecutor *>(ownedToolExecutor_))
    {
        QMetaObject::invokeMethod(tool, "shutdownDockerSandbox", Qt::BlockingQueuedConnection);
    }
    if (xMcp *mcp = qobject_cast<xMcp *>(ownedMcpManager_))
    {
        QMetaObject::invokeMethod(mcp, "disconnectAll", Qt::QueuedConnection);
    }
    if (workers_)
    {
        workers_->stop();
    }
    if (ownsNetworkDriver_)
    {
        networkDriver_ = nullptr;
        ownsNetworkDriver_ = false;
    }
    toolDriver_ = nullptr;
    ownedToolExecutor_ = nullptr;
    ownedMcpManager_ = nullptr;
    state_.initialized = false;
    state_.backendReady = false;
    state_.turnActive = false;
    state_.toolActive = false;
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    emitState();
}

void EvaRuntime::attachNetworkDriver(RuntimeNetworkDriver *driver, bool takeOwnership)
{
    if (networkDriver_ == driver)
    {
        ownsNetworkDriver_ = ownsNetworkDriver_ || takeOwnership;
        return;
    }

    if (networkDriver_)
    {
        QObject::disconnect(networkDriver_, nullptr, this, nullptr);
    }

    networkDriver_ = driver;
    ownsNetworkDriver_ = takeOwnership;
    if (!networkDriver_)
        return;

    if (workers_ && !workers_->isRunning())
    {
        workers_->start();
    }
    if (workers_ && networkDriver_->thread() != workers_->netThread())
    {
        networkDriver_->moveToThread(workers_->netThread());
    }
    if (ownsNetworkDriver_ && workers_)
    {
        connect(workers_->netThread(), &QThread::finished,
                networkDriver_, &QObject::deleteLater, Qt::UniqueConnection);
    }

    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_tool_calls,
            this, &EvaRuntime::onNetworkToolCalls, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_state,
            this, &EvaRuntime::onNetworkState, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_output,
            this, &EvaRuntime::onNetworkOutput, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_pushover,
            this, &EvaRuntime::onNetworkFinished, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_kv_tokens,
            this, &EvaRuntime::onNetworkKvTokens, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_prompt_baseline,
            this, &EvaRuntime::onNetworkPromptBaseline, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_slot_id,
            this, &EvaRuntime::onNetworkSlotId, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_reasoning_tokens,
            this, &EvaRuntime::onNetworkReasoningTokens, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_speeds,
            this, &EvaRuntime::onNetworkSpeeds, Qt::QueuedConnection);
    connect(networkDriver_, &RuntimeNetworkDriver::net2ui_turn_counters,
            this, &EvaRuntime::onNetworkTurnCounters, Qt::QueuedConnection);
}

void EvaRuntime::attachToolDriver(RuntimeToolDriver *driver)
{
    toolDriver_ = driver;
    state_.toolDriverAttached = (toolDriver_ != nullptr);
    emitState();
}

NetClient *EvaRuntime::createNetworkClient()
{
    if (NetClient *existing = qobject_cast<NetClient *>(networkDriver_))
        return existing;

    NetClient *client = new NetClient;
    attachNetworkDriver(client, true);
    return client;
}

ToolExecutor *EvaRuntime::createToolExecutor(const QString &applicationDirPath)
{
    if (ToolExecutor *existing = qobject_cast<ToolExecutor *>(ownedToolExecutor_))
        return existing;

    ToolExecutor *tool = new ToolExecutor(applicationDirPath);
    moveOwnedWorkerObject(tool, workers_ ? workers_->toolThread() : nullptr);
    ownedToolExecutor_ = tool;
    attachToolDriver(tool);
    return tool;
}

xMcp *EvaRuntime::createMcpManager()
{
    if (xMcp *existing = qobject_cast<xMcp *>(ownedMcpManager_))
        return existing;

    xMcp *mcp = new xMcp;
    moveOwnedWorkerObject(mcp, workers_ ? workers_->mcpThread() : nullptr);
    ownedMcpManager_ = mcp;
    return mcp;
}

cpuChecker *EvaRuntime::createCpuChecker()
{
    cpuChecker *checker = new cpuChecker;
    moveOwnedWorkerObject(checker, workers_ ? workers_->monitorThread() : nullptr);
    return checker;
}

gpuChecker *EvaRuntime::createGpuChecker()
{
    gpuChecker *checker = new gpuChecker;
    moveOwnedWorkerObject(checker, workers_ ? workers_->monitorThread() : nullptr);
    return checker;
}

RuntimeState EvaRuntime::stateSnapshot() const
{
    return state_;
}

RequestSnapshot EvaRuntime::buildRequestSnapshot(const APIS &apis,
                                                const ENDPOINT_DATA &endpoint,
                                                const QJsonObject &words,
                                                int languageFlag,
                                                quint64 turnId) const
{
    RequestSnapshot snapshot;
    snapshot.apis = apis;
    if (snapshot.apis.api_endpoint.trimmed().isEmpty())
    {
        snapshot.apis.api_endpoint = state_.apis.api_endpoint.trimmed().isEmpty()
                                         ? state_.endpoint
                                         : state_.apis.api_endpoint;
    }
    if (snapshot.apis.api_model.trimmed().isEmpty())
    {
        snapshot.apis.api_model = state_.apis.api_model.trimmed().isEmpty()
                                      ? state_.currentModel
                                      : state_.apis.api_model;
    }
    if (snapshot.apis.api_chat_endpoint.trimmed().isEmpty())
        snapshot.apis.api_chat_endpoint = state_.apis.api_chat_endpoint;
    if (snapshot.apis.api_completion_endpoint.trimmed().isEmpty())
        snapshot.apis.api_completion_endpoint = state_.apis.api_completion_endpoint;
    snapshot.apis.is_local_backend = state_.mode == RuntimeMode::Local;
    snapshot.endpoint = endpoint;
    snapshot.wordsObj = words;
    snapshot.languageFlag = languageFlag;
    snapshot.turnId = turnId;
    return snapshot;
}

void EvaRuntime::publishEvent(RuntimeEvent event)
{
    event.state = state_;
    emit runtimeEvent(event);
}

void EvaRuntime::setSessionMessages(const QJsonArray &messages)
{
    state_.messages = messages;
    state_.messageCount = messages.size();
    emitState();
}

int EvaRuntime::appendSessionMessage(const QJsonObject &message)
{
    state_.messages.append(message);
    state_.messageCount = state_.messages.size();
    emitState();
    return state_.messages.size() - 1;
}

bool EvaRuntime::replaceSessionMessage(int index, const QJsonObject &message)
{
    if (index < 0 || index >= state_.messages.size())
        return false;
    state_.messages.replace(index, message);
    state_.messageCount = state_.messages.size();
    emitState();
    return true;
}

void EvaRuntime::updateSessionSnapshot(const QJsonArray &messages,
                                       const QString &historySessionId,
                                       bool compactionActive,
                                       quint64 activeTurnId)
{
    state_.messages = messages;
    state_.messageCount = messages.size();
    state_.historySessionId = historySessionId;
    state_.compactionActive = compactionActive;
    state_.activeTurnId = activeTurnId;
    emitState();
}

quint64 EvaRuntime::beginTurn(const QString &taskName, bool toolActive)
{
    quint64 turnId = state_.activeTurnId;
    if (turnId == 0 || !state_.turnActive)
    {
        turnId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        if (turnId == 0) turnId = 1;
    }
    state_.turnActive = true;
    state_.toolActive = toolActive;
    state_.activeTurnId = turnId;
    state_.currentTask = taskName;
    state_.phase = toolActive ? RuntimePhase::ToolRunning : RuntimePhase::Running;
    state_.lastError.clear();
    emitState();

    RuntimeEvent event;
    event.type = RuntimeEventType::StateChanged;
    event.state = state_;
    event.name = QStringLiteral("turn_started");
    event.payload.insert(QStringLiteral("task"), taskName);
    emit runtimeEvent(event);
    return turnId;
}

void EvaRuntime::finishTurn(const QString &reason, bool success)
{
    state_.turnActive = false;
    state_.toolActive = false;
    state_.activeTurnId = 0;
    state_.currentTask.clear();
    state_.phase = success && state_.lastError.isEmpty() ? RuntimePhase::Ready : RuntimePhase::Error;
    emitState();

    RuntimeEvent event;
    event.type = RuntimeEventType::StateChanged;
    event.state = state_;
    event.name = QStringLiteral("turn_finished");
    event.payload.insert(QStringLiteral("reason"), reason);
    event.payload.insert(QStringLiteral("success"), success);
    emit runtimeEvent(event);
}

void EvaRuntime::updateSessionRuntimeState(const QJsonArray &messages,
                                           const QString &historySessionId,
                                           ConversationMode conversationMode,
                                           const QString &systemPrompt,
                                           const QStringList &stopwords,
                                           int toolCallMode,
                                           RuntimeMode runtimeMode,
                                           const SETTINGS &settings,
                                           const APIS &apis,
                                           const QString &endpoint,
                                           const QString &currentTask,
                                           const QString &pendingToolResult,
                                           const QString &pendingToolName,
                                           const QString &pendingToolCallId,
                                           const QString &lastToolCallName,
                                           const COMPACTION_SETTINGS &compactionSettings,
                                           bool compactionActive,
                                           bool compactionQueued,
                                           int compactionFromIndex,
                                           int compactionToIndex,
                                           bool compactionPendingInput,
                                           const QString &compactionReason,
                                           bool turnActive,
                                           bool toolActive,
                                           quint64 activeTurnId,
                                           int slotId,
                                           int kvUsed,
                                           int kvUsedBeforeTurn,
                                           int kvStreamedTurn,
                                           int kvTurnTokens,
                                           int kvCapacity,
                                           int kvPercent,
                                           int promptTokens,
                                           int generatedTokens,
                                           int reasoningTokens)
{
    state_.messages = messages;
    state_.messageCount = messages.size();
    state_.historySessionId = historySessionId;
    state_.mode = runtimeMode;
    state_.conversationMode = conversationMode;
    state_.settings = settings;
    state_.apis = apis;
    const QString runtimeEndpoint = endpoint.trimmed();
    if (!runtimeEndpoint.isEmpty())
    {
        state_.endpoint = runtimeEndpoint;
    }
    else if (!state_.apis.api_endpoint.trimmed().isEmpty())
    {
        state_.endpoint = state_.apis.api_endpoint.trimmed();
    }
    if (runtimeMode == RuntimeMode::Link)
    {
        state_.currentModel = state_.apis.api_model.trimmed();
        state_.currentModelPath.clear();
        state_.backendChoice = QStringLiteral("link");
        state_.backendResolved = QStringLiteral("remote");
    }
    else if (!settings.modelpath.trimmed().isEmpty())
    {
        state_.currentModelPath = settings.modelpath.trimmed();
        state_.currentModel = modelLabelFromPath(state_.currentModelPath);
    }
    state_.currentTask = currentTask;
    state_.systemPrompt = systemPrompt;
    state_.stopwords = stopwords;
    state_.toolCallMode = toolCallMode;
    state_.pendingToolResult = pendingToolResult;
    state_.pendingToolName = pendingToolName;
    state_.pendingToolCallId = pendingToolCallId;
    state_.lastToolCallName = lastToolCallName;
    state_.compactionSettings = compactionSettings;
    state_.compactionActive = compactionActive;
    state_.compactionQueued = compactionQueued;
    state_.compactionFromIndex = compactionFromIndex;
    state_.compactionToIndex = compactionToIndex;
    state_.compactionPendingInput = compactionPendingInput;
    state_.compactionReason = compactionReason;
    state_.turnActive = turnActive;
    state_.toolActive = toolActive;
    state_.activeTurnId = activeTurnId;
    state_.slotId = slotId;
    state_.kvUsed = qMax(0, kvUsed);
    state_.kvUsedBeforeTurn = qMax(0, kvUsedBeforeTurn);
    state_.kvStreamedTurn = qMax(0, kvStreamedTurn);
    state_.kvTurnTokens = qMax(0, kvTurnTokens);
    state_.kvCapacity = qMax(0, kvCapacity);
    state_.kvPercent = qBound(0, kvPercent, 100);
    state_.promptTokens = qMax(0, promptTokens);
    state_.generatedTokens = qMax(0, generatedTokens);
    state_.reasoningTokens = qMax(0, reasoningTokens);
    if (compactionActive)
    {
        state_.phase = RuntimePhase::Running;
    }
    else if (toolActive)
    {
        state_.phase = RuntimePhase::ToolRunning;
    }
    else if (turnActive)
    {
        state_.phase = RuntimePhase::Running;
    }
    else if (state_.phase == RuntimePhase::Running || state_.phase == RuntimePhase::ToolRunning)
    {
        state_.phase = state_.backendReady || state_.mode == RuntimeMode::Link ? RuntimePhase::Ready : RuntimePhase::Unloaded;
    }
    emitState();
}

bool EvaRuntime::loadLocal(const RuntimeLoadLocalCommand &command, QString *errorMessage)
{
    const QString modelPath = command.modelPath.trimmed();
    if (modelPath.isEmpty())
    {
        const QString error = QStringLiteral("Runtime local load requires a model path.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("loadLocal"), error);
        return false;
    }
    if (!QFileInfo::exists(modelPath))
    {
        const QString error = QStringLiteral("Runtime local model path does not exist: %1").arg(modelPath);
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("loadLocal"), error);
        return false;
    }

    state_.mode = RuntimeMode::Local;
    state_.phase = RuntimePhase::Unloaded;
    state_.backendLifecycle = BackendLifecycleState::Stopped;
    state_.backendReady = false;
    state_.currentModelPath = QFileInfo(modelPath).absoluteFilePath();
    state_.currentModel = modelLabelFromPath(state_.currentModelPath);
    state_.backendChoice = command.backendChoice.trimmed().isEmpty() ? QStringLiteral("auto") : command.backendChoice.trimmed().toLower();
    state_.settings = command.settings;
    state_.settings.modelpath = state_.currentModelPath;
    state_.settings.mmprojpath = command.mmprojPath.trimmed();
    state_.settings.lorapath = command.loraPath.trimmed();
    state_.apis = APIS();
    state_.apis.is_local_backend = true;
    state_.endpoint = command.port.trimmed().isEmpty()
                          ? QStringLiteral("http://127.0.0.1:%1").arg(QStringLiteral(DEFAULT_SERVER_PORT))
                          : QStringLiteral("http://127.0.0.1:%1").arg(command.port.trimmed());
    state_.apis.api_endpoint = state_.endpoint;
    state_.apis.api_chat_endpoint = QStringLiteral(CHAT_ENDPOINT);
    state_.apis.api_completion_endpoint = QStringLiteral(COMPLETION_ENDPOINT);
    state_.lastError.clear();
    emitState();
    Q_UNUSED(errorMessage);
    return true;
}

bool EvaRuntime::connectRemote(const RuntimeConnectRemoteCommand &command, QString *errorMessage)
{
    const QString endpoint = normalizeRuntimeEndpoint(command.endpoint);
    if (endpoint.isEmpty())
    {
        const QString error = QStringLiteral("Runtime remote endpoint is required.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("connectRemote"), error);
        return false;
    }
    const QUrl baseUrl = QUrl::fromUserInput(endpoint);
    if (!baseUrl.isValid() || baseUrl.host().isEmpty())
    {
        const QString error = QStringLiteral("Runtime remote endpoint is invalid: %1").arg(endpoint);
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("connectRemote"), error);
        return false;
    }

    state_.mode = RuntimeMode::Link;
    state_.phase = RuntimePhase::Ready;
    state_.backendLifecycle = BackendLifecycleState::Running;
    state_.backendReady = true;
    state_.endpoint = endpoint;
    state_.currentModel = command.model.trimmed();
    state_.currentModelPath.clear();
    state_.backendChoice = QStringLiteral("link");
    state_.backendResolved = QStringLiteral("remote");
    state_.apis.api_endpoint = endpoint;
    state_.apis.api_key = command.apiKey.trimmed();
    state_.apis.api_model = state_.currentModel.isEmpty() ? QStringLiteral("default") : state_.currentModel;
    state_.apis.api_chat_endpoint = OpenAiCompat::chatCompletionsPath(baseUrl);
    state_.apis.api_completion_endpoint = OpenAiCompat::completionsPath(baseUrl);
    state_.apis.is_local_backend = false;
    state_.settings = command.sampling;
    state_.lastError.clear();
    emitState();
    return true;
}

bool EvaRuntime::resetConversation(const RuntimeResetCommand &command, QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    state_.turnActive = false;
    state_.toolActive = false;
    state_.activeTurnId = 0;
    state_.currentTask.clear();
    state_.slotId = -1;
    state_.kvUsed = 0;
    state_.kvUsedBeforeTurn = 0;
    state_.kvStreamedTurn = 0;
    state_.kvTurnTokens = 0;
    state_.kvPercent = 0;
    state_.promptTokens = 0;
    state_.generatedTokens = 0;
    state_.reasoningTokens = 0;
    state_.messageCount = 0;
    state_.compactionActive = false;
    state_.compactionQueued = false;
    state_.historySessionId.clear();
    state_.messages = QJsonArray();
    state_.lastError.clear();
    activeResponseText_.clear();
    if (command.clearHistory)
    {
        state_.phase = state_.backendReady || state_.mode == RuntimeMode::Link ? RuntimePhase::Ready : RuntimePhase::Unloaded;
    }
    emitState();
    RuntimeEvent event;
    event.type = RuntimeEventType::RecordUpdate;
    event.state = state_;
    event.name = QStringLiteral("conversation_reset");
    event.payload.insert(QStringLiteral("clear_history"), command.clearHistory);
    emit runtimeEvent(event);
    return true;
}

bool EvaRuntime::sendMessage(const RuntimeSendMessageCommand &command, QString *errorMessage)
{
    const bool hasStructuredEndpoint = !command.endpoint.messagesArray.isEmpty() ||
                                       !command.endpoint.input_prompt.trimmed().isEmpty() ||
                                       !command.endpoint.tools.isEmpty();
    if (command.text.trimmed().isEmpty() && command.frontendMessages.isEmpty() && !hasStructuredEndpoint)
    {
        const QString error = QStringLiteral("Runtime sendMessage requires text, frontendMessages, or endpoint data.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("sendMessage"), error);
        return false;
    }

    const quint64 turnId = command.turnId > 0 ? command.turnId : static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    const APIS apis = command.apis.api_endpoint.trimmed().isEmpty() &&
                              command.apis.api_model.trimmed().isEmpty() &&
                              command.apis.api_chat_endpoint.trimmed().isEmpty() &&
                              command.apis.api_completion_endpoint.trimmed().isEmpty()
                          ? state_.apis
                          : command.apis;
    const ENDPOINT_DATA endpoint = buildEndpointDataForMessage(command, turnId);
    const RequestSnapshot snapshot = buildRequestSnapshot(apis, endpoint, command.wordsObj, command.languageFlag, turnId);
    return dispatchSnapshot(snapshot, command.stream, errorMessage);
}

bool EvaRuntime::sendRequestSnapshot(const RequestSnapshot &snapshot, QString *errorMessage)
{
    return dispatchSnapshot(snapshot, true, errorMessage);
}

void EvaRuntime::stop()
{
    setStopRequested(true);
}

void EvaRuntime::setStopRequested(bool stop)
{
    if (!networkDriver_)
    {
        emitErrorEvent(RuntimeEventType::CommandRejected,
                       QStringLiteral("stop"),
                       QStringLiteral("EvaRuntime stop requires an attached network driver."));
        return;
    }
    QMetaObject::invokeMethod(networkDriver_, "stop", Qt::QueuedConnection, Q_ARG(bool, stop));
    if (stop && toolDriver_)
    {
        toolDriver_->cancelActiveRuntimeTool();
    }
    if (!stop)
    {
        return;
    }
    state_.turnActive = false;
    state_.toolActive = false;
    state_.activeTurnId = 0;
    state_.currentTask.clear();
    setPhase(state_.backendReady || state_.mode == RuntimeMode::Link ? RuntimePhase::Ready
                                                                     : RuntimePhase::Unloaded);
}

void EvaRuntime::setErrorState(const QString &error)
{
    setPhase(RuntimePhase::Error, error);
}

void EvaRuntime::updateBackendStatus(BackendLifecycleState lifecycle,
                                     bool ready,
                                     const QString &endpoint,
                                     const QString &resolvedBackend,
                                     const QString &error)
{
    state_.backendLifecycle = lifecycle;
    state_.backendReady = ready;
    if (!endpoint.trimmed().isEmpty())
    {
        state_.endpoint = endpoint.trimmed();
        state_.apis.api_endpoint = state_.endpoint;
    }
    if (!resolvedBackend.trimmed().isEmpty())
    {
        state_.backendResolved = resolvedBackend.trimmed();
    }
    state_.lastError = error;
    if (!error.trimmed().isEmpty())
    {
        state_.phase = RuntimePhase::Error;
    }
    else if (ready)
    {
        state_.phase = RuntimePhase::Ready;
    }
    else if (lifecycle == BackendLifecycleState::Starting || lifecycle == BackendLifecycleState::Restarting)
    {
        state_.phase = RuntimePhase::Loading;
    }
    else if (lifecycle == BackendLifecycleState::Stopped)
    {
        state_.phase = RuntimePhase::Unloaded;
    }
    emitState();
}

ENDPOINT_DATA EvaRuntime::buildEndpointDataForMessage(const RuntimeSendMessageCommand &command, quint64 turnId) const
{
    ENDPOINT_DATA endpoint = command.endpoint;
    if (endpoint.date_prompt.isEmpty())
        endpoint.date_prompt.clear();
    if (endpoint.input_prompt.trimmed().isEmpty())
        endpoint.input_prompt = command.text;
    if (endpoint.messagesArray.isEmpty())
        endpoint.messagesArray = command.frontendMessages;
    if (endpoint.messagesArray.isEmpty() && !command.text.trimmed().isEmpty())
    {
        QJsonObject userMessage;
        userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
        userMessage.insert(QStringLiteral("content"), command.text);
        endpoint.messagesArray.append(userMessage);
    }
    if (endpoint.tools.isEmpty())
        endpoint.tools = QJsonArray();
    if (endpoint.tool_call_mode == 0)
        endpoint.tool_call_mode = DEFAULT_TOOL_CALL_MODE;
    endpoint.is_complete_state = endpoint.is_complete_state || (state_.conversationMode == ConversationMode::Complete);
    if (endpoint.temp <= 0)
        endpoint.temp = static_cast<float>(state_.settings.temp);
    if (endpoint.repeat <= 0)
        endpoint.repeat = state_.settings.repeat;
    if (endpoint.top_k <= 0)
        endpoint.top_k = state_.settings.top_k;
    if (endpoint.top_p <= 0)
        endpoint.top_p = state_.settings.hid_top_p;
    if (endpoint.n_predict <= 0)
        endpoint.n_predict = state_.settings.hid_npredict;
    if (endpoint.reasoning_effort.trimmed().isEmpty())
        endpoint.reasoning_effort = state_.settings.reasoning_effort;
    if (endpoint.stopwords.isEmpty())
        endpoint.stopwords = state_.stopwords;
    if (endpoint.id_slot < 0)
        endpoint.id_slot = state_.slotId;
    endpoint.turn_id = turnId;
    return endpoint;
}

bool EvaRuntime::dispatchSnapshot(RequestSnapshot snapshot, bool streamRequested, QString *errorMessage)
{
    if (!state_.initialized)
    {
        const QString error = QStringLiteral("EvaRuntime is not initialized.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("send"), error);
        return false;
    }
    if (!networkDriver_)
    {
        const QString error = QStringLiteral("EvaRuntime send requires an attached network driver.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("send"), error);
        return false;
    }
    if (snapshot.apis.api_endpoint.trimmed().isEmpty())
    {
        const QString error = QStringLiteral("EvaRuntime send requires api_endpoint.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("send"), error);
        return false;
    }

    if (snapshot.turnId == 0)
    {
        snapshot.turnId = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
    }
    if (snapshot.endpoint.turn_id == 0)
    {
        snapshot.endpoint.turn_id = snapshot.turnId;
    }

    activeStreamRequested_ = streamRequested;
    activeResponseText_.clear();
    state_.turnActive = true;
    state_.toolActive = false;
    state_.activeTurnId = snapshot.turnId;
    state_.lastError.clear();
    setPhase(RuntimePhase::Running);

    QMetaObject::invokeMethod(networkDriver_, "send", Qt::QueuedConnection, Q_ARG(RequestSnapshot, snapshot));
    return true;
}

bool EvaRuntime::dispatchFirstToolCall(const QString &payload, QString *errorMessage)
{
    if (!toolDriver_)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Runtime tool driver is not attached.");
        return false;
    }

    const QJsonArray calls = parseToolCallsPayload(payload);
    if (calls.isEmpty() || !calls.first().isObject())
    {
        if (errorMessage) *errorMessage = QStringLiteral("Runtime tool call payload is empty or invalid.");
        return false;
    }

    const QJsonObject call = calls.first().toObject();
    const QString toolName = toolNameFromCall(call);
    if (!shouldRuntimeDispatchTool(toolName))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Runtime skips non-executable tool call: %1.").arg(toolName);
        return false;
    }

    return toolDriver_->executeToolCall(normalizeToolCallForDriver(call), state_.activeTurnId, errorMessage);
}

void EvaRuntime::setPhase(RuntimePhase phase, const QString &error)
{
    state_.phase = phase;
    state_.lastError = error;
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    emitState();
}

void EvaRuntime::emitState()
{
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    emit stateChanged(state_);
    RuntimeEvent event;
    event.type = RuntimeEventType::StateChanged;
    event.state = state_;
    emit runtimeEvent(event);
}

void EvaRuntime::emitErrorEvent(RuntimeEventType type, const QString &name, const QString &error)
{
    state_.lastError = error;
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    RuntimeEvent event;
    event.type = type;
    event.state = state_;
    event.name = name;
    event.error = error;
    emit runtimeEvent(event);
}

void EvaRuntime::emitMetricEvent(const QJsonObject &payload)
{
    RuntimeEvent event;
    event.type = RuntimeEventType::Metrics;
    event.state = state_;
    event.payload = payload;
    emit runtimeEvent(event);
}

void EvaRuntime::ensureWorkerHostStarted()
{
    if (workers_ && !workers_->isRunning())
    {
        workers_->start();
    }
}

void EvaRuntime::moveOwnedWorkerObject(QObject *object, QThread *thread)
{
    if (!object)
        return;

    ensureWorkerHostStarted();
    if (thread && object->thread() != thread)
    {
        object->moveToThread(thread);
    }
    if (thread)
    {
        connect(thread, &QThread::finished,
                object, &QObject::deleteLater, Qt::UniqueConnection);
    }
}

void EvaRuntime::onNetworkToolCalls(const QString &payload)
{
    state_.toolActive = true;
    state_.phase = RuntimePhase::ToolRunning;
    const QJsonArray calls = parseToolCallsPayload(payload);
    if (!calls.isEmpty() && calls.first().isObject())
    {
        const QJsonObject call = calls.first().toObject();
        state_.pendingToolName = toolNameFromCall(call);
        state_.pendingToolCallId = call.value(QStringLiteral("id")).toString();
        state_.lastToolCallName = state_.pendingToolName;
    }
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    RuntimeEvent event;
    event.type = RuntimeEventType::ToolStarted;
    event.state = state_;
    event.name = QStringLiteral("tool_calls_detected");
    event.text = payload;
    event.payload.insert(QStringLiteral("raw"), payload);
    event.payload.insert(QStringLiteral("tool_driver_attached"), toolDriver_ != nullptr);
    if (!calls.isEmpty()) event.payload.insert(QStringLiteral("calls"), calls);
    if (toolDriver_)
    {
        QString toolError;
        const bool dispatched = dispatchFirstToolCall(payload, &toolError);
        event.payload.insert(QStringLiteral("runtime_tool_dispatched"), dispatched);
        if (!toolError.isEmpty()) event.payload.insert(QStringLiteral("runtime_tool_error"), toolError);
    }
    emit runtimeEvent(event);
    emitState();
}

void EvaRuntime::onNetworkState(const QString &stateString, SIGNAL_STATE state)
{
    if (state == WRONG_SIGNAL)
    {
        state_.lastError = stateString;
        state_.phase = RuntimePhase::Error;
        state_.updatedAt = QDateTime::currentDateTimeUtc();
    }
    RuntimeEvent event;
    event.type = (state == WRONG_SIGNAL) ? RuntimeEventType::Error : RuntimeEventType::BackendLog;
    event.state = state_;
    event.text = stateString;
    event.payload.insert(QStringLiteral("signal_state"), static_cast<int>(state));
    if (state == WRONG_SIGNAL)
    {
        event.error = stateString;
    }
    emit runtimeEvent(event);
    if (state == WRONG_SIGNAL)
    {
        emitState();
    }
}

void EvaRuntime::onNetworkOutput(const QString &result, bool isWhile, const QColor &color)
{
    activeResponseText_ += result;
    RuntimeEvent event;
    event.type = RuntimeEventType::OutputChunk;
    event.state = state_;
    event.role = QStringLiteral("assistant");
    event.text = result;
    event.payload.insert(QStringLiteral("is_stream_chunk"), isWhile);
    event.payload.insert(QStringLiteral("requested_stream"), activeStreamRequested_);
    event.payload.insert(QStringLiteral("color"), color.name());
    emit runtimeEvent(event);
}

void EvaRuntime::onNetworkFinished()
{
    state_.turnActive = false;
    state_.toolActive = false;
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    state_.phase = state_.lastError.isEmpty() ? RuntimePhase::Ready : RuntimePhase::Error;
    RuntimeEvent event;
    event.type = RuntimeEventType::TurnFinished;
    event.state = state_;
    event.role = QStringLiteral("assistant");
    event.text = activeResponseText_;
    event.payload.insert(QStringLiteral("requested_stream"), activeStreamRequested_);
    emit runtimeEvent(event);
    emitState();
}

void EvaRuntime::onNetworkKvTokens(int usedTokens)
{
    const int streamed = qMax(0, usedTokens);
    state_.kvStreamedTurn = streamed;
    state_.generatedTokens = streamed;
    state_.kvTurnTokens = qMax(0, state_.promptTokens) + streamed;
    state_.kvUsed = qMax(0, state_.kvUsedBeforeTurn + streamed);
    if (state_.kvCapacity > 0)
    {
        state_.kvPercent = qBound(0, int(qRound(100.0 * double(state_.kvUsed) / double(state_.kvCapacity))), 100);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("kv_used"), usedTokens);
    emitMetricEvent(payload);
}

void EvaRuntime::onNetworkPromptBaseline(int promptTokens)
{
    const int prompt = qMax(0, promptTokens);
    state_.promptTokens = prompt;
    state_.kvUsedBeforeTurn = prompt;
    state_.kvTurnTokens = prompt + qMax(0, state_.kvStreamedTurn);
    state_.kvUsed = prompt + qMax(0, state_.kvStreamedTurn);
    if (state_.kvCapacity > 0)
    {
        state_.kvPercent = qBound(0, int(qRound(100.0 * double(state_.kvUsed) / double(state_.kvCapacity))), 100);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("prompt_tokens"), promptTokens);
    emitMetricEvent(payload);
}

void EvaRuntime::onNetworkSlotId(int slotId)
{
    state_.slotId = slotId;
    QJsonObject payload;
    payload.insert(QStringLiteral("slot_id"), slotId);
    emitMetricEvent(payload);
}

void EvaRuntime::onNetworkReasoningTokens(int count)
{
    state_.reasoningTokens = count;
    QJsonObject payload;
    payload.insert(QStringLiteral("reasoning_tokens"), count);
    emitMetricEvent(payload);
}

void EvaRuntime::onNetworkSpeeds(double promptPerSecond, double predictedPerSecond)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("prompt_per_second"), promptPerSecond);
    payload.insert(QStringLiteral("predicted_per_second"), predictedPerSecond);
    emitMetricEvent(payload);
}

void EvaRuntime::onNetworkTurnCounters(int cacheTokens, int promptTokens, int predictedTokens)
{
    const int cache = qMax(0, cacheTokens);
    const int prompt = qMax(0, promptTokens);
    const int predicted = qMax(0, predictedTokens);
    state_.promptTokens = prompt;
    state_.generatedTokens = predicted;
    state_.kvStreamedTurn = predicted;
    state_.kvUsedBeforeTurn = cache + prompt;
    state_.kvTurnTokens = prompt + predicted;
    state_.kvUsed = cache + prompt + predicted;
    if (state_.kvCapacity > 0)
    {
        state_.kvPercent = qBound(0, int(qRound(100.0 * double(state_.kvUsed) / double(state_.kvCapacity))), 100);
    }
    QJsonObject payload;
    payload.insert(QStringLiteral("cache_tokens"), cacheTokens);
    payload.insert(QStringLiteral("prompt_tokens"), promptTokens);
    payload.insert(QStringLiteral("predicted_tokens"), predictedTokens);
    emitMetricEvent(payload);
}
