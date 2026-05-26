#include "runtime/eva_runtime.h"

#include <QDateTime>
#include <QFileInfo>
#include <QUrl>

#include "runtime/runtime_bootstrap.h"
#include "runtime/runtime_worker_host.h"
#include "utils/openai_compat.h"

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
    if (workers_)
    {
        workers_->stop();
    }
    state_.initialized = false;
    state_.backendReady = false;
    state_.turnActive = false;
    state_.toolActive = false;
    state_.updatedAt = QDateTime::currentDateTimeUtc();
    emitState();
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
    snapshot.endpoint = endpoint;
    snapshot.wordsObj = words;
    snapshot.languageFlag = languageFlag;
    snapshot.turnId = turnId;
    return snapshot;
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
    state_.lastError.clear();
    emitState();
    return rejectPendingMigration(QStringLiteral("loadLocal"), errorMessage);
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
    return rejectPendingMigration(QStringLiteral("connectRemote"), errorMessage);
}

bool EvaRuntime::resetConversation(const RuntimeResetCommand &command, QString *errorMessage)
{
    Q_UNUSED(command);
    return rejectPendingMigration(QStringLiteral("resetConversation"), errorMessage);
}

bool EvaRuntime::sendMessage(const RuntimeSendMessageCommand &command, QString *errorMessage)
{
    if (command.text.trimmed().isEmpty())
    {
        const QString error = QStringLiteral("Runtime sendMessage requires non-empty text.");
        if (errorMessage) *errorMessage = error;
        emitErrorEvent(RuntimeEventType::CommandRejected, QStringLiteral("sendMessage"), error);
        return false;
    }
    return rejectPendingMigration(QStringLiteral("sendMessage"), errorMessage);
}

void EvaRuntime::stop()
{
    emitErrorEvent(RuntimeEventType::CommandRejected,
                   QStringLiteral("stop"),
                   QStringLiteral("EvaRuntime stop is declared but not wired to NetClient/ToolExecutor yet."));
}

bool EvaRuntime::rejectPendingMigration(const QString &commandName, QString *errorMessage)
{
    const QString error = QStringLiteral("EvaRuntime command '%1' is declared but pending migration from Widget.").arg(commandName);
    if (errorMessage) *errorMessage = error;
    emitErrorEvent(RuntimeEventType::CommandRejected, commandName, error);
    return false;
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
