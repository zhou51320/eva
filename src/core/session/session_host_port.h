#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "core/session/session_frontend_port.h"
#include "core/session/session_types.h"
#include "runtime/runtime_state.h"
#include "storage/history_store.h"
#include "xconfig.h"

// SessionController 面向宿主运行层的窄端口。
// Widget 在迁移期实现该端口；后续无窗口运行时可提供另一套实现。
class SessionHostPort : public SessionFrontendPort
{
public:
    ~SessionHostPort() override = default;

    virtual RuntimeState runtimeStateSnapshotForSession() const = 0;
    virtual bool runtimeSessionReady() const = 0;
    virtual void syncSessionRuntimeState(bool replaceMessages) = 0;

    virtual QJsonArray legacySessionMessages() const = 0;
    virtual void setLegacySessionMessages(const QJsonArray &messages) = 0;
    virtual int appendRuntimeSessionMessage(const QJsonObject &message) = 0;
    virtual bool replaceRuntimeSessionMessage(int index, const QJsonObject &message) = 0;

    virtual SETTINGS sessionSettingsSnapshot() const = 0;
    virtual QString sessionSystemPrompt() const = 0;
    virtual QStringList sessionStopwords() const = 0;
    virtual ConversationMode sessionConversationMode() const = 0;
    virtual int sessionToolCallMode() const = 0;
    virtual QJsonArray sessionFunctionTools() const = 0;
    virtual int sessionSlotId() const = 0;
    virtual quint64 sessionActiveTurnId() const = 0;
    virtual QString sessionModeName() const = 0;
    virtual QString sessionEndpointForHistory() const = 0;
    virtual QString sessionModelForHistory() const = 0;
    virtual int sessionContextSize() const = 0;

    virtual bool shouldBeginHistorySession() const = 0;
    virtual QString historySessionId() const = 0;
    virtual void beginHistorySession(const SessionMeta &meta) = 0;
    virtual void appendHistoryMessage(const QJsonObject &message) = 0;
    virtual void rewriteHistoryMessages(const QJsonArray &messages) = 0;

    virtual QString pendingToolResult() const = 0;
    virtual QString pendingToolName() const = 0;
    virtual QString pendingToolCallId() const = 0;
    virtual QString lastToolCallName() const = 0;
    virtual void clearPendingToolResult() = 0;

    virtual void noteBackendActivity() = 0;
    virtual void cancelSessionLazyUnload(const QString &reason) = 0;
    virtual void sendEndpointData(const ENDPOINT_DATA &data) = 0;
    virtual quint64 startSessionTurn(const QString &taskName, bool isToolLoop, bool continuingTool) = 0;
    virtual void finishSessionTurn(const QString &reason, bool success) = 0;
    virtual void logSessionFlow(FlowPhase phase, const QString &detail, SIGNAL_STATE state) = 0;

    virtual bool canAutoCompact() const = 0;
    virtual COMPACTION_SETTINGS sessionCompactionSettings() const = 0;
    virtual int resolvedContextLimitForSession() const = 0;
    virtual int kvUsedForSession() const = 0;
    virtual bool compactionInFlight() const = 0;
    virtual bool compactionQueued() const = 0;
    virtual void setCompactionQueued(bool queued) = 0;
    virtual void queueCompactionInput(const InputPack &input, const QString &reason) = 0;
    virtual void beginCompactionRequest(int fromIndex, int toIndex) = 0;
    virtual void finishCompactionRequest() = 0;
    virtual int compactionFromIndex() const = 0;
    virtual int compactionToIndex() const = 0;
    virtual void clearCompactionRange() = 0;
    virtual bool hasPendingCompactionInput() const = 0;
    virtual InputPack takePendingCompactionInput() = 0;
    virtual void resetKvAfterCompaction() = 0;
    virtual void finishNoPendingCompaction() = 0;
    virtual void setCurrentSessionTask(ConversationTask task) = 0;

    virtual bool appendCompactionSummary(const QJsonObject &summaryObj) const = 0;
    virtual int upsertCompactRecord(const QString &summary) = 0;
    virtual void remapRecordIndexesAfterCompaction(int compactMessageIndex) = 0;
    virtual void presentSystemMessageRecord(const QString &systemText, int messageIndex) = 0;
};
