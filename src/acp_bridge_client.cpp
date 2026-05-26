#include "acp_bridge_client.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>

#include "xconfig.h"

AcpBridgeClient::AcpBridgeClient(QObject *parent)
    : QObject(parent),
      channel_(new ControlChannel(this))
{
    connect(channel_, &ControlChannel::controllerStateChanged, this, [this](ControlChannel::ControllerState state, const QString &reason)
    {
        state_ = state;
        if (state == ControlChannel::ControllerState::Idle && !reason.isEmpty()) lastError_ = reason;
        if (pendingLoop_ && state == ControlChannel::ControllerState::Idle) pendingLoop_->quit();
        if (chatLoop_ && state == ControlChannel::ControllerState::Idle) chatLoop_->quit();
    });

    connect(channel_, &ControlChannel::controllerEventArrived, this, [this](const QJsonObject &payload)
    {
        const QString type = payload.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("bridge_response"))
        {
            const QString requestId = payload.value(QStringLiteral("request_id")).toString();
            if (pendingLoop_ && requestId == pendingRequestId_)
            {
                pendingResponse_ = payload;
                pendingLoop_->quit();
                return;
            }
        }
        if (type == QStringLiteral("snapshot") || type == QStringLiteral("hello_ack"))
        {
            latestSnapshot_ = payload.value(QStringLiteral("snapshot")).toObject();
        }
        if (!chatWaiting_) return;
        if (type == QStringLiteral("output"))
        {
            const QString role = payload.value(QStringLiteral("role")).toString();
            const QString text = payload.value(QStringLiteral("text")).toString();
            if (role == QStringLiteral("think"))
                chatReasoningText_.append(text);
            else
                chatAssistantText_.append(text);
            return;
        }
        if (type == QStringLiteral("ui_state"))
        {
            const QString phase = payload.value(QStringLiteral("phase")).toString();
            if (phase == QStringLiteral("normal") && chatLoop_)
            {
                chatLoop_->quit();
            }
            return;
        }
        if (type == QStringLiteral("state_log"))
        {
            const QString text = payload.value(QStringLiteral("text")).toString();
            if (!text.isEmpty()) lastError_ = text;
        }
    });
}

bool AcpBridgeClient::ensureConnected(int timeoutMs, QString *errorMessage)
{
    if (state_ == ControlChannel::ControllerState::Connected) return true;

    lastError_.clear();
    channel_->connectToHost(QStringLiteral("127.0.0.1"), static_cast<quint16>(DEFAULT_ACP_BRIDGE_PORT));

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(channel_, &ControlChannel::controllerStateChanged, &loop, [&](ControlChannel::ControllerState state, const QString &)
    {
        if (state == ControlChannel::ControllerState::Connected || state == ControlChannel::ControllerState::Idle)
            loop.quit();
    });
    timer.start(timeoutMs);
    loop.exec();

    if (state_ == ControlChannel::ControllerState::Connected) return true;
    if (errorMessage)
    {
        *errorMessage = lastError_.isEmpty() ? QStringLiteral("ACP bridge unavailable. Please start EVA main app first.") : lastError_;
    }
    return false;
}

bool AcpBridgeClient::isConnected() const
{
    return state_ == ControlChannel::ControllerState::Connected;
}

QJsonObject AcpBridgeClient::getState(QString *errorMessage, int timeoutMs)
{
    QJsonObject response = request(QStringLiteral("bridge_get_state"), QJsonObject(), errorMessage, timeoutMs);
    return response.value(QStringLiteral("state")).toObject();
}

QJsonArray AcpBridgeClient::listModels(QString *errorMessage, int timeoutMs)
{
    QJsonObject response = request(QStringLiteral("bridge_list_models"), QJsonObject(), errorMessage, timeoutMs);
    return response.value(QStringLiteral("models")).toArray();
}

QJsonObject AcpBridgeClient::applyLoad(const QJsonObject &payload, QString *errorMessage, int timeoutMs)
{
    QJsonObject response = request(QStringLiteral("bridge_apply_load"), payload, errorMessage, timeoutMs);
    return response.value(QStringLiteral("state")).toObject();
}

bool AcpBridgeClient::resetConversation(QString *errorMessage, int timeoutMs)
{
    return !request(QStringLiteral("bridge_reset"), QJsonObject(), errorMessage, timeoutMs).isEmpty();
}

bool AcpBridgeClient::sendText(const QString &text, ChatResult *result, QString *errorMessage, int timeoutMs)
{
    return sendTextStreaming(text, std::function<void(const QString &, const QString &)>(), result, errorMessage, timeoutMs);
}

bool AcpBridgeClient::sendTextStreaming(const QString &text,
                                       const std::function<void(const QString &role, const QString &chunk)> &onChunk,
                                       ChatResult *result,
                                       QString *errorMessage,
                                       int timeoutMs)
{
    if (!result)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Chat result receiver is required.");
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("text"), text);
    if (request(QStringLiteral("bridge_send"), payload, errorMessage, 3000).isEmpty())
    {
        return false;
    }

    chatAssistantText_.clear();
    chatReasoningText_.clear();
    lastError_.clear();
    chatWaiting_ = true;

    QMetaObject::Connection chunkConn;
    if (onChunk)
    {
        chunkConn = connect(channel_, &ControlChannel::controllerEventArrived, this, [this, onChunk](const QJsonObject &payload)
        {
            if (!chatWaiting_) return;
            if (payload.value(QStringLiteral("type")).toString() != QStringLiteral("output")) return;
            const QString role = payload.value(QStringLiteral("role")).toString();
            const QString textChunk = payload.value(QStringLiteral("text")).toString();
            if (!textChunk.isEmpty()) onChunk(role, textChunk);
        });
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    chatLoop_ = &loop;
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    chatLoop_ = nullptr;
    chatWaiting_ = false;
    if (chunkConn) disconnect(chunkConn);

    QString bridgeError;
    const QJsonObject state = getState(&bridgeError, 1500);
    if (!state.isEmpty())
    {
        const QJsonObject snapshot = state.value(QStringLiteral("snapshot")).toObject();
        const QString latestText = latestAssistantTextFromSnapshot(snapshot);
        if (!latestText.isEmpty()) chatAssistantText_ = latestText;
    }

    if (chatAssistantText_.isEmpty() && !bridgeError.isEmpty())
    {
        if (errorMessage) *errorMessage = bridgeError;
        return false;
    }

    result->assistantText = chatAssistantText_.isEmpty() ? QStringLiteral("(empty response)") : chatAssistantText_;
    result->reasoningText = chatReasoningText_;
    return true;
}

QJsonObject AcpBridgeClient::request(const QString &name, const QJsonObject &payload, QString *errorMessage, int timeoutMs)
{
    if (!ensureConnected(timeoutMs, errorMessage)) return QJsonObject();

    QJsonObject command = payload;
    command.insert(QStringLiteral("type"), QStringLiteral("command"));
    command.insert(QStringLiteral("name"), name);
    pendingRequestId_ = nextRequestId();
    command.insert(QStringLiteral("request_id"), pendingRequestId_);
    pendingResponse_ = QJsonObject();

    if (!channel_->sendToHost(command))
    {
        if (errorMessage) *errorMessage = QStringLiteral("Failed to send bridge command.");
        return QJsonObject();
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    pendingLoop_ = &loop;
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    pendingLoop_ = nullptr;

    if (pendingResponse_.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("Bridge request timed out: %1").arg(name);
        return QJsonObject();
    }
    if (!pendingResponse_.value(QStringLiteral("ok")).toBool())
    {
        if (errorMessage) *errorMessage = pendingResponse_.value(QStringLiteral("error")).toString();
        return QJsonObject();
    }
    return pendingResponse_;
}

QString AcpBridgeClient::nextRequestId()
{
    static quint64 counter = 0;
    ++counter;
    return QStringLiteral("req-%1-%2").arg(QDateTime::currentMSecsSinceEpoch()).arg(counter);
}

QJsonObject AcpBridgeClient::latestSnapshot() const
{
    return latestSnapshot_;
}

QString AcpBridgeClient::latestAssistantTextFromSnapshot(const QJsonObject &snapshot)
{
    const QJsonArray records = snapshot.value(QStringLiteral("records")).toArray();
    QString lastAssistant;
    for (const QJsonValue &value : records)
    {
        if (!value.isObject()) continue;
        const QJsonObject record = value.toObject();
        if (record.value(QStringLiteral("role")).toInt(-1) == 2)
        {
            const QString text = record.value(QStringLiteral("text")).toString();
            if (!text.isEmpty()) lastAssistant = text;
        }
    }
    return lastAssistant;
}
