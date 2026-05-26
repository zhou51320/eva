#ifndef BACKEND_COORDINATOR_H
#define BACKEND_COORDINATOR_H

#include <QObject>
#include <QHostAddress>
#include <QString>
#include <functional>

#include "runtime/runtime_events.h"

// 后端协调器消费的运行时上下文。
// 这里不保存 Widget 指针：后续无窗口 Runtime 可以只注入事件槽和运行时状态；
// 当前窗口版仍通过 ownerObject 作为 Qt 信号/定时器接收者完成兼容过渡。
struct BackendCoordinatorContext
{
    QObject *ownerObject = nullptr;
    std::function<void(RuntimeEvent)> publishEvent;
    std::function<void(BackendLifecycleState, bool, QString, QString, QString)> updateBackendStatus;
};

// 后端协调器：集中本地后端生命周期、代理端口与惰性卸载逻辑
// 注意：当前仍有旧 Widget 适配调用，新增代码应优先通过 BackendCoordinatorContext
// 发布 RuntimeEvent，并逐步把配置/状态迁入运行时上下文。
class BackendCoordinator : public QObject
{
    Q_OBJECT

  public:
    explicit BackendCoordinator(const BackendCoordinatorContext &context, QObject *parent = nullptr);
    explicit BackendCoordinator(QObject *owner);

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

  private:
    QObject *ownerObject() const;
    void publishBackendEvent(RuntimeEventType type,
                             const QString &text,
                             SIGNAL_STATE state = USUAL_SIGNAL,
                             const QString &name = QString(),
                             const QString &error = QString()) const;
    void publishBackendStatus(BackendLifecycleState lifecycle,
                              bool ready,
                              const QString &endpoint = QString(),
                              const QString &resolvedBackend = QString(),
                              const QString &error = QString()) const;

    BackendCoordinatorContext context_;
};

#endif // BACKEND_COORDINATOR_H
