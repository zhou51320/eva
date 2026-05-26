#pragma once

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

#include "net/controlchannel.h"

class AcpBridgeClient : public QObject
{
    Q_OBJECT

  public:
    struct ChatResult
    {
        QString assistantText;
        QString reasoningText;
    };

    explicit AcpBridgeClient(QObject *parent = nullptr);

    bool ensureConnected(int timeoutMs, QString *errorMessage);
    bool isConnected() const;
    QJsonObject getState(QString *errorMessage, int timeoutMs = 1500);
    QJsonArray listModels(QString *errorMessage, int timeoutMs = 1500);
    QJsonObject applyLoad(const QJsonObject &payload, QString *errorMessage, int timeoutMs = 3000);
    bool resetConversation(QString *errorMessage, int timeoutMs = 3000);
    bool stopRuntime(QString *errorMessage, int timeoutMs = 3000);
    bool sendText(const QString &text, ChatResult *result, QString *errorMessage, int timeoutMs = 600000);
    bool sendTextStreaming(const QString &text,
                           const std::function<void(const QString &role, const QString &chunk)> &onChunk,
                           ChatResult *result,
                           QString *errorMessage,
                           int timeoutMs = 600000);

  private:
    QJsonObject request(const QString &name, const QJsonObject &payload, QString *errorMessage, int timeoutMs);
    QString nextRequestId();
    QJsonObject latestSnapshot() const;
    static QString latestAssistantTextFromSnapshot(const QJsonObject &snapshot);

    ControlChannel *channel_ = nullptr;
    ControlChannel::ControllerState state_ = ControlChannel::ControllerState::Idle;
    QString lastError_;

    QString pendingRequestId_;
    QJsonObject pendingResponse_;
    QEventLoop *pendingLoop_ = nullptr;

    bool chatWaiting_ = false;
    bool chatFinished_ = false;
    qint64 chatTurnId_ = 0;
    QString chatAssistantText_;
    QString chatReasoningText_;
    QEventLoop *chatLoop_ = nullptr;
    QJsonObject latestSnapshot_;
};
