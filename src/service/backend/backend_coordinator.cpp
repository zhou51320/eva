#include "service/backend/backend_coordinator.h"

#include "service/backend/backend_coordinator_port.h"

#include <QTcpServer>
#include <utility>

BackendCoordinator::BackendCoordinator(const BackendCoordinatorContext &context, QObject *parent)
    : QObject(parent ? parent : context.ownerObject),
      context_(context)
{
    if (context_.port) context_.port->setCoordinator(this);
}

BackendCoordinator::BackendCoordinator(QObject *owner)
    : BackendCoordinator(BackendCoordinatorContext{owner, nullptr, {}, {}}, owner)
{
}

void BackendCoordinator::setPort(BackendCoordinatorPort *port)
{
    context_.port = port;
    if (context_.port) context_.port->setCoordinator(this);
}

void BackendCoordinator::setEventSink(std::function<void(RuntimeEvent)> sink)
{
    context_.publishEvent = std::move(sink);
}

void BackendCoordinator::setBackendStatusSink(std::function<void(BackendLifecycleState, bool, QString, QString, QString)> sink)
{
    context_.updateBackendStatus = std::move(sink);
}

QObject *BackendCoordinator::ownerObject() const
{
    return context_.ownerObject ? context_.ownerObject : parent();
}

BackendCoordinatorPort *BackendCoordinator::port() const
{
    return context_.port;
}

void BackendCoordinator::publishBackendEvent(RuntimeEventType type,
                                             const QString &text,
                                             SIGNAL_STATE state,
                                             const QString &name,
                                             const QString &error) const
{
    if (!context_.publishEvent) return;

    RuntimeEvent event;
    event.type = type;
    event.text = text;
    event.name = name;
    event.error = error;
    event.payload.insert(QStringLiteral("signal_state"), static_cast<int>(state));
    context_.publishEvent(event);
}

void BackendCoordinator::publishBackendStatus(BackendLifecycleState lifecycle,
                                              bool ready,
                                              const QString &endpoint,
                                              const QString &resolvedBackend,
                                              const QString &error) const
{
    if (context_.updateBackendStatus)
    {
        context_.updateBackendStatus(lifecycle, ready, endpoint, resolvedBackend, error);
    }
}

#define IF_PORT_RETURN(defaultValue, call) \
    do { if (!port()) return defaultValue; return port()->call; } while (false)

#define IF_PORT_VOID(call) \
    do { if (!port()) return; port()->call; } while (false)

void BackendCoordinator::ensureLocalServer(bool lazyWake, bool forceReload) { IF_PORT_VOID(ensureLocalServer(lazyWake, forceReload)); }
QString BackendCoordinator::pickFreeTcpPort(const QHostAddress &addr) const { IF_PORT_RETURN(QString(), pickFreeTcpPort(addr)); }
void BackendCoordinator::announcePortBusy(const QString &requestedPort, const QString &alternativePort) { IF_PORT_VOID(announcePortBusy(requestedPort, alternativePort)); }
void BackendCoordinator::initiatePortFallback() { IF_PORT_VOID(initiatePortFallback()); }
bool BackendCoordinator::ensureProxyListening(const QString &host, const QString &portValue, QString *errorMessage) { IF_PORT_RETURN(false, ensureProxyListening(host, portValue, errorMessage)); }
QString BackendCoordinator::formatLocalEndpoint(const QString &host, const QString &portValue) const { IF_PORT_RETURN(QString(), formatLocalEndpoint(host, portValue)); }
void BackendCoordinator::updateProxyBackend(const QString &backendHost, const QString &backendPort) { IF_PORT_VOID(updateProxyBackend(backendHost, backendPort)); }
void BackendCoordinator::onProxyWakeRequested() { IF_PORT_VOID(onProxyWakeRequested()); }
void BackendCoordinator::onProxyExternalActivity() { IF_PORT_VOID(onProxyExternalActivity()); }
void BackendCoordinator::markBackendActivity() { IF_PORT_VOID(markBackendActivity()); }
void BackendCoordinator::scheduleLazyUnload() { IF_PORT_VOID(scheduleLazyUnload()); }
void BackendCoordinator::cancelLazyUnload(const QString &reason) { IF_PORT_VOID(cancelLazyUnload(reason)); }
void BackendCoordinator::performLazyUnload() { IF_PORT_VOID(performLazyUnload()); }
void BackendCoordinator::performLazyUnloadInternal(bool forced) { IF_PORT_VOID(performLazyUnloadInternal(forced)); }
bool BackendCoordinator::lazyUnloadEnabled() const { IF_PORT_RETURN(false, lazyUnloadEnabled()); }
void BackendCoordinator::setLazyCountdownLabelDisplay(const QString &status) { IF_PORT_VOID(setLazyCountdownLabelDisplay(status)); }
void BackendCoordinator::updateLazyCountdownLabel() { IF_PORT_VOID(updateLazyCountdownLabel()); }
void BackendCoordinator::onLazyUnloadNowClicked() { IF_PORT_VOID(onLazyUnloadNowClicked()); }
void BackendCoordinator::onServerReady(const QString &endpoint) { IF_PORT_VOID(onServerReady(endpoint)); }
void BackendCoordinator::onServerOutput(const QString &chunk) { IF_PORT_VOID(onServerOutput(chunk)); }
bool BackendCoordinator::processServerOutputLine(const QString &line) { IF_PORT_RETURN(false, processServerOutputLine(line)); }
void BackendCoordinator::onServerStartFailed(const QString &reason) { IF_PORT_VOID(onServerStartFailed(reason)); }
bool BackendCoordinator::shouldArmWin7CpuFallback() const { IF_PORT_RETURN(false, shouldArmWin7CpuFallback()); }
bool BackendCoordinator::triggerWin7CpuFallback(const QString &reasonTag) { IF_PORT_RETURN(false, triggerWin7CpuFallback(reasonTag)); }
void BackendCoordinator::resetBackendFallbackState(const QString &reasonTag) { IF_PORT_VOID(resetBackendFallbackState(reasonTag)); }
QString BackendCoordinator::pickNextBackendFallback(const QString &failedBackend) const { IF_PORT_RETURN(QString(), pickNextBackendFallback(failedBackend)); }
bool BackendCoordinator::triggerBackendFallback(const QString &failedBackend, const QString &reasonTag) { IF_PORT_RETURN(false, triggerBackendFallback(failedBackend, reasonTag)); }

#undef IF_PORT_RETURN
#undef IF_PORT_VOID
