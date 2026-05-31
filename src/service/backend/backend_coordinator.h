#ifndef BACKEND_COORDINATOR_H
#define BACKEND_COORDINATOR_H

#include <QObject>
#include <QHostAddress>
#include <QString>
#include <functional>

#include "runtime/runtime_events.h"

class BackendCoordinatorPort;

// 后端协调器消费的运行时上下文。
// 不保存也不解析 Widget 指针；桌面版通过 BackendCoordinatorPort 适配器注入状态与动作，
// 无窗口 Runtime 可以注入自己的端口实现和事件槽。
struct BackendCoordinatorContext
{
    QObject *ownerObject = nullptr;
    BackendCoordinatorPort *port = nullptr;
    std::function<void(RuntimeEvent)> publishEvent;
    std::function<void(BackendLifecycleState, bool, QString, QString, QString)> updateBackendStatus;
};

// 后端协调器：集中本地后端生命周期、代理端口与惰性卸载逻辑。
// 具体前端状态/动作均通过 BackendCoordinatorPort 或 RuntimeEvent/Status sink 注入。
class BackendCoordinator : public QObject
{
    Q_OBJECT

  public:
    explicit BackendCoordinator(const BackendCoordinatorContext &context, QObject *parent = nullptr);
    explicit BackendCoordinator(QObject *owner);

    void setPort(BackendCoordinatorPort *port);
    void setEventSink(std::function<void(RuntimeEvent)> sink);
    void setBackendStatusSink(std::function<void(BackendLifecycleState, bool, QString, QString, QString)> sink);

    void ensureLocalServer(bool lazyWake = false, bool forceReload = false);
    QString pickFreeTcpPort(const QHostAddress &addr = QHostAddress::Any) const;
    void announcePortBusy(const QString &requestedPort, const QString &alternativePort);
    void initiatePortFallback();
    bool ensureProxyListening(const QString &host, const QString &port, QString *errorMessage);
    QString formatLocalEndpoint(const QString &host, const QString &port) const;
    void updateProxyBackend(const QString &backendHost, const QString &backendPort);
    void onProxyWakeRequested();
    void onProxyExternalActivity();
    void markBackendActivity();
    void scheduleLazyUnload();
    void cancelLazyUnload(const QString &reason);
    void performLazyUnload();
    void performLazyUnloadInternal(bool forced);
    bool lazyUnloadEnabled() const;
    void setLazyCountdownLabelDisplay(const QString &status);
    void updateLazyCountdownLabel();
    void onLazyUnloadNowClicked();
    void onServerReady(const QString &endpoint);
    void onServerOutput(const QString &chunk);
    bool processServerOutputLine(const QString &line);
    void onServerStartFailed(const QString &reason);
    bool shouldArmWin7CpuFallback() const;
    bool triggerWin7CpuFallback(const QString &reasonTag);
    void resetBackendFallbackState(const QString &reasonTag);
    QString pickNextBackendFallback(const QString &failedBackend) const;
    bool triggerBackendFallback(const QString &failedBackend, const QString &reasonTag);

    void publishBackendStatus(BackendLifecycleState lifecycle,
                              bool ready,
                              const QString &endpoint = QString(),
                              const QString &resolvedBackend = QString(),
                              const QString &error = QString()) const;

  private:
    QObject *ownerObject() const;
    BackendCoordinatorPort *port() const;
    void publishBackendEvent(RuntimeEventType type,
                             const QString &text,
                             SIGNAL_STATE state = USUAL_SIGNAL,
                             const QString &name = QString(),
                             const QString &error = QString()) const;

    BackendCoordinatorContext context_;
};

#endif // BACKEND_COORDINATOR_H
