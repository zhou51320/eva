#pragma once

#include <QObject>
#include <QString>

#include "app/app_context.h"
#include "runtime/runtime_commands.h"
#include "runtime/runtime_events.h"
#include "runtime/runtime_network_driver.h"
#include "runtime/runtime_state.h"
#include "service/net/request_snapshot.h"

class RuntimeWorkerHost;

// EVA 无窗口运行层入口。
// 当前第一版只建立稳定边界；实际装载/推理会在后续任务中逐步从 Widget 迁入。
class EvaRuntime : public QObject
{
    Q_OBJECT

  public:
    explicit EvaRuntime(QObject *parent = nullptr);
    ~EvaRuntime() override;

    bool initialize(const AppContext &context, QString *errorMessage = nullptr);
    bool initialize(QString *errorMessage = nullptr);
    void shutdown();

    void attachNetworkDriver(RuntimeNetworkDriver *driver, bool takeOwnership = false);
    RuntimeState stateSnapshot() const;
    RequestSnapshot buildRequestSnapshot(const APIS &apis,
                                         const ENDPOINT_DATA &endpoint,
                                         const QJsonObject &words,
                                         int languageFlag,
                                         quint64 turnId) const;
    void publishEvent(RuntimeEvent event);
    void setSessionMessages(const QJsonArray &messages);
    int appendSessionMessage(const QJsonObject &message);
    bool replaceSessionMessage(int index, const QJsonObject &message);
    void updateSessionSnapshot(const QJsonArray &messages,
                               const QString &historySessionId = QString(),
                               bool compactionActive = false,
                               quint64 activeTurnId = 0);
    quint64 beginTurn(const QString &taskName = QString(), bool toolActive = false);
    void finishTurn(const QString &reason = QString(), bool success = true);
    void updateSessionRuntimeState(const QJsonArray &messages,
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
                                   int reasoningTokens);

  public slots:
    bool loadLocal(const RuntimeLoadLocalCommand &command, QString *errorMessage = nullptr);
    bool connectRemote(const RuntimeConnectRemoteCommand &command, QString *errorMessage = nullptr);
    bool resetConversation(const RuntimeResetCommand &command = RuntimeResetCommand(), QString *errorMessage = nullptr);
    bool sendMessage(const RuntimeSendMessageCommand &command, QString *errorMessage = nullptr);
    bool sendRequestSnapshot(const RequestSnapshot &snapshot, QString *errorMessage = nullptr);
    void stop();
    void setStopRequested(bool stop);
    void setErrorState(const QString &error);
    void updateBackendStatus(BackendLifecycleState lifecycle,
                             bool ready,
                             const QString &endpoint = QString(),
                             const QString &resolvedBackend = QString(),
                             const QString &error = QString());

  signals:
    void stateChanged(const RuntimeState &state);
    void runtimeEvent(const RuntimeEvent &event);

  private:
    ENDPOINT_DATA buildEndpointDataForMessage(const RuntimeSendMessageCommand &command, quint64 turnId) const;
    bool dispatchSnapshot(RequestSnapshot snapshot, bool streamRequested, QString *errorMessage);
    void setPhase(RuntimePhase phase, const QString &error = QString());
    void emitState();
    void emitErrorEvent(RuntimeEventType type, const QString &name, const QString &error);
    void emitMetricEvent(const QJsonObject &payload);

  private slots:
    void onNetworkToolCalls(const QString &payload);
    void onNetworkState(const QString &stateString, SIGNAL_STATE state);
    void onNetworkOutput(const QString &result, bool isWhile, const QColor &color);
    void onNetworkFinished();
    void onNetworkKvTokens(int usedTokens);
    void onNetworkPromptBaseline(int promptTokens);
    void onNetworkSlotId(int slotId);
    void onNetworkReasoningTokens(int count);
    void onNetworkSpeeds(double promptPerSecond, double predictedPerSecond);
    void onNetworkTurnCounters(int cacheTokens, int promptTokens, int predictedTokens);

  private:
    RuntimeState state_;
    RuntimeWorkerHost *workers_ = nullptr;
    RuntimeNetworkDriver *networkDriver_ = nullptr;
    bool ownsNetworkDriver_ = false;
    bool activeStreamRequested_ = true;
    QString activeResponseText_;
};
