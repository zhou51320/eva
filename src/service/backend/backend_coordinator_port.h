#ifndef BACKEND_COORDINATOR_PORT_H
#define BACKEND_COORDINATOR_PORT_H

#include <QHostAddress>
#include <QString>
#include <QtGlobal>

class BackendCoordinator;

// BackendCoordinator 使用的后端宿主端口。
// 桌面 Widget 通过适配器实现该端口；无窗口 Runtime 可注入自己的实现，
// 避免协调器直接 include 或 qobject_cast 到具体前端类型。
class BackendCoordinatorPort
{
  public:
    virtual ~BackendCoordinatorPort() = default;
    virtual void setCoordinator(BackendCoordinator *coordinator) { Q_UNUSED(coordinator); }

    virtual void ensureLocalServer(bool lazyWake = false, bool forceReload = false) = 0;
    virtual QString pickFreeTcpPort(const QHostAddress &addr = QHostAddress::Any) const = 0;
    virtual void announcePortBusy(const QString &requestedPort, const QString &alternativePort) = 0;
    virtual void initiatePortFallback() = 0;
    virtual bool ensureProxyListening(const QString &host, const QString &port, QString *errorMessage) = 0;
    virtual QString formatLocalEndpoint(const QString &host, const QString &port) const = 0;
    virtual void updateProxyBackend(const QString &backendHost, const QString &backendPort) = 0;
    virtual void onProxyWakeRequested() = 0;
    virtual void onProxyExternalActivity() = 0;
    virtual void markBackendActivity() = 0;
    virtual void scheduleLazyUnload() = 0;
    virtual void cancelLazyUnload(const QString &reason) = 0;
    virtual void performLazyUnload() = 0;
    virtual void performLazyUnloadInternal(bool forced) = 0;
    virtual bool lazyUnloadEnabled() const = 0;
    virtual void setLazyCountdownLabelDisplay(const QString &status) = 0;
    virtual void updateLazyCountdownLabel() = 0;
    virtual void onLazyUnloadNowClicked() = 0;
    virtual void onServerReady(const QString &endpoint) = 0;
    virtual void onServerOutput(const QString &chunk) = 0;
    virtual bool processServerOutputLine(const QString &line) = 0;
    virtual void onServerStartFailed(const QString &reason) = 0;
    virtual bool shouldArmWin7CpuFallback() const = 0;
    virtual bool triggerWin7CpuFallback(const QString &reasonTag) = 0;
    virtual void resetBackendFallbackState(const QString &reasonTag) = 0;
    virtual QString pickNextBackendFallback(const QString &failedBackend) const = 0;
    virtual bool triggerBackendFallback(const QString &failedBackend, const QString &reasonTag) = 0;
};

#endif // BACKEND_COORDINATOR_PORT_H
