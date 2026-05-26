#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "core/session/session_types.h"
#include "mcp_json.h"
#include "xconfig.h"

// ToolFlowController 面向工具流宿主的窄端口。
// Widget 在迁移期实现该端口；无窗口运行时后续可提供等价实现。
class ToolFlowHostPort
{
public:
    virtual ~ToolFlowHostPort() = default;

    virtual void flushToolFlowStream() = 0;
    virtual void clearToolFlowCallMarkers() = 0;
    virtual void syncToolFlowRuntimeState(bool replaceMessages) = 0;
    virtual QJsonArray takeToolFlowPendingCalls() = 0;

    virtual QString assistantStreamText() const = 0;
    virtual void setAssistantStreamText(const QString &text) = 0;
    virtual void resetAssistantStreamIndexes() = 0;
    virtual bool compactionActiveForToolFlow() const = 0;
    virtual void handleToolFlowCompactionReply(const QString &summaryText, const QString &reasoningText) = 0;

    virtual int promptTokensForToolFlow() const = 0;
    virtual int generatedTokensForToolFlow() const = 0;
    virtual int reasoningTokensForToolFlow() const = 0;
    virtual int usedTokensForToolFlow() const = 0;
    virtual int turnTokensForToolFlow() const = 0;
    virtual void logToolFlow(FlowPhase phase, const QString &detail, SIGNAL_STATE state) = 0;

    virtual int toolCallModeForToolFlow() const = 0;
    virtual bool engineerProxyActiveForToolFlow() const = 0;
    virtual void handleToolFlowEngineerAssistant(const QString &message, const QString &reasoning) = 0;
    virtual int appendToolFlowAssistantMessage(const QJsonObject &message) = 0;
    virtual void bindToolFlowReasoningRecord(int messageIndex) = 0;
    virtual void bindToolFlowAssistantRecord(int messageIndex) = 0;
    virtual void publishToolFlowAssistantRecord(const QString &text, const QString &reasoning, int messageIndex) = 0;
    virtual void refreshToolFlowAssistantRecordIfNeeded(const QString &text) = 0;

    virtual bool completeModeForToolFlow() const = 0;
    virtual bool toolsEnabledForToolFlow() const = 0;
    virtual void finishNormalToolFlow() = 0;
    virtual void resetConversationAfterToolFlowCompletion() = 0;

    virtual void setToolFlowCurrentCall(const mcp::json &call, const QString &callId) = 0;
    virtual mcp::json currentToolFlowCall() const = 0;
    virtual void rememberToolFlowPendingName(const QString &toolName) = 0;
    virtual void clearToolFlowPendingCallId() = 0;
    virtual void showToolFlowClicked(const QString &toolName) = 0;
    virtual void publishToolFlowStarted(const QString &toolName) = 0;
    virtual void createLegacyToolFlowRecordIfNeeded(const QString &toolName) = 0;
    virtual void startToolFlowEngineerProxy() = 0;
    virtual void handleToolFlowScheduleCall() = 0;
    virtual void markToolFlowAssistantHeaderReset() = 0;
    virtual void setToolFlowInvocationActive(bool active) = 0;
    virtual quint64 activeToolFlowTurnId() const = 0;
    virtual void emitToolFlowTurn(quint64 turnId) = 0;
    virtual void emitToolFlowExec() = 0;
    virtual QString lastAssistantMessageTextForToolFlow() const = 0;
    virtual mcp::json parseTextToolCallForToolFlow(const QString &text) = 0;

    virtual void noteToolFlowBackendActivity() = 0;
    virtual void scheduleToolFlowLazyUnload() = 0;
    virtual void setToolFlowPendingCalls(const QJsonArray &calls) = 0;
    virtual void clearToolFlowPendingCalls() = 0;
    virtual void renderToolFlowCallPayload(const QString &displayText) = 0;

    virtual void setToolFlowResultFromRaw(const QString &toolResult) = 0;
    virtual QString toolFlowResult() const = 0;
    virtual int toolFlowImageResultCount() const = 0;
    virtual QString pendingToolFlowName() const = 0;
    virtual void publishToolFlowFinished() = 0;
    virtual void handleToolFlowEngineerObservation(const QString &observation) = 0;
    virtual void clearToolFlowResult() = 0;
    virtual void continueToolFlowSend() = 0;
};
