#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "core/session/session_host_port.h"
#include "core/session/session_types.h"
#include "runtime/runtime_state.h"
#include "xconfig.h"

// 会话控制器：负责会话编排、消息组织、压缩与发送流程。
class SessionController : public QObject
{
    Q_OBJECT
public:
    explicit SessionController(QObject *owner);
    
    // 会话与请求构建
    ENDPOINT_DATA prepareEndpointData();
    void beginSessionIfNeeded();

    // 输入与消息构建
    bool buildDocumentAttachment(const QString &path, DocumentAttachment &attachment);
    QString formatDocumentPayload(const DocumentAttachment &doc) const;
    QString describeDocumentList(const QVector<DocumentAttachment> &docs) const;
    void collectUserInputs(InputPack &pack, bool attachControllerFrame);

    // 会话流
    void handleChatReply(ENDPOINT_DATA &data, const InputPack &in);
    void handleCompletion(ENDPOINT_DATA &data);
    void handleToolLoop(ENDPOINT_DATA &data);
    void logCurrentTask(ConversationTask task);
    void startTurnFlow(ConversationTask task, bool continuingTool);
    void finishTurnFlow(const QString &reason, bool success);
    void ensureSystemHeader(const QString &systemText);

    // 上下文压缩
    bool shouldTriggerCompaction() const;
    bool startCompactionIfNeeded(const InputPack &pendingInput);
    void startCompactionRun(const QString &reason);
    QString extractMessageTextForCompaction(const QJsonObject &msg) const;
    QString buildCompactionSourceText(int fromIndex, int toIndex) const;
    bool appendCompactionSummaryFile(const QJsonObject &summaryObj) const;
    void applyCompactionSummary(const QString &summaryText);
    void handleCompactionReply(const QString &summaryText, const QString &reasoningText);
    void resumeSendAfterCompaction();

private:
    struct PendingToolState
    {
        QString result;
        QString pendingName;
        QString callId;
        QString lastName;

        bool hasResult() const { return !result.isEmpty(); }
        QString effectiveName() const { return pendingName.isEmpty() ? lastName : pendingName; }
    };

    SessionHostPort *hostPort() const;
    SessionFrontendPort *frontendPort() const;
    RuntimeState runtimeState() const;
    PendingToolState pendingToolState() const;
    void clearPendingToolResult();
    COMPACTION_SETTINGS compactionSettings() const;
    QJsonArray sessionMessages() const;
    int appendConversationMessage(const QJsonObject &message, bool persistHistory = true);
    void replaceConversationMessages(const QJsonArray &messages, bool rewriteHistory = false);
    void replaceConversationMessage(int index, const QJsonObject &message);
    void syncRuntimeSessionState() const;

    SessionHostPort *host_ = nullptr; // 不拥有，会话宿主适配端口
};
