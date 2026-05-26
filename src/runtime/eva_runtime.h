#pragma once

#include <QObject>
#include <QString>

#include "app/app_context.h"
#include "runtime/runtime_commands.h"
#include "runtime/runtime_events.h"
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

    RuntimeState stateSnapshot() const;
    RequestSnapshot buildRequestSnapshot(const APIS &apis,
                                         const ENDPOINT_DATA &endpoint,
                                         const QJsonObject &words,
                                         int languageFlag,
                                         quint64 turnId) const;

  public slots:
    bool loadLocal(const RuntimeLoadLocalCommand &command, QString *errorMessage = nullptr);
    bool connectRemote(const RuntimeConnectRemoteCommand &command, QString *errorMessage = nullptr);
    bool resetConversation(const RuntimeResetCommand &command = RuntimeResetCommand(), QString *errorMessage = nullptr);
    bool sendMessage(const RuntimeSendMessageCommand &command, QString *errorMessage = nullptr);
    void stop();

  signals:
    void stateChanged(const RuntimeState &state);
    void runtimeEvent(const RuntimeEvent &event);

  private:
    bool rejectPendingMigration(const QString &commandName, QString *errorMessage);
    void setPhase(RuntimePhase phase, const QString &error = QString());
    void emitState();
    void emitErrorEvent(RuntimeEventType type, const QString &name, const QString &error);

    RuntimeState state_;
    RuntimeWorkerHost *workers_ = nullptr;
};
