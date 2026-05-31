#ifndef BACKEND_COORDINATOR_WIDGET_PORT_H
#define BACKEND_COORDINATOR_WIDGET_PORT_H

#include "service/backend/backend_coordinator_port.h"
#include "runtime/runtime_events.h"

class Widget;
class BackendCoordinator;

// Widget 兼容适配层：隔离所有桌面前端字段访问，保持 BackendCoordinator
// 只依赖 BackendCoordinatorPort。
class WidgetBackendCoordinatorPort : public BackendCoordinatorPort
{
  public:
    explicit WidgetBackendCoordinatorPort(Widget *widget);
    void setCoordinator(BackendCoordinator *coordinator) override;

    void ensureLocalServer(bool lazyWake = false, bool forceReload = false) override;
    QString pickFreeTcpPort(const QHostAddress &addr = QHostAddress::Any) const override;
    void announcePortBusy(const QString &requestedPort, const QString &alternativePort) override;
    void initiatePortFallback() override;
    bool ensureProxyListening(const QString &host, const QString &port, QString *errorMessage) override;
    QString formatLocalEndpoint(const QString &host, const QString &port) const override;
    void updateProxyBackend(const QString &backendHost, const QString &backendPort) override;
    void onProxyWakeRequested() override;
    void onProxyExternalActivity() override;
    void markBackendActivity() override;
    void scheduleLazyUnload() override;
    void cancelLazyUnload(const QString &reason) override;
    void performLazyUnload() override;
    void performLazyUnloadInternal(bool forced) override;
    bool lazyUnloadEnabled() const override;
    void setLazyCountdownLabelDisplay(const QString &status) override;
    void updateLazyCountdownLabel() override;
    void onLazyUnloadNowClicked() override;
    void onServerReady(const QString &endpoint) override;
    void onServerOutput(const QString &chunk) override;
    bool processServerOutputLine(const QString &line) override;
    void onServerStartFailed(const QString &reason) override;
    bool shouldArmWin7CpuFallback() const override;
    bool triggerWin7CpuFallback(const QString &reasonTag) override;
    void resetBackendFallbackState(const QString &reasonTag) override;
    QString pickNextBackendFallback(const QString &failedBackend) const override;
    bool triggerBackendFallback(const QString &failedBackend, const QString &reasonTag) override;

  private:
    void publishBackendStatus(BackendLifecycleState lifecycle,
                              bool ready,
                              const QString &endpoint = QString(),
                              const QString &resolvedBackend = QString(),
                              const QString &error = QString()) const;

    Widget *w_ = nullptr;
    BackendCoordinator *coordinator_ = nullptr;
};

#endif // BACKEND_COORDINATOR_WIDGET_PORT_H
