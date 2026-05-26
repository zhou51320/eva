#include "runtime/runtime_worker_host.h"

#include <QString>

RuntimeWorkerHost::RuntimeWorkerHost(QObject *parent)
    : QObject(parent)
{
}

RuntimeWorkerHost::~RuntimeWorkerHost()
{
    stop();
}

void RuntimeWorkerHost::start()
{
    if (running_)
        return;
    startThread(netThread_, "eva-runtime-net");
    startThread(toolThread_, "eva-runtime-tool");
    startThread(mcpThread_, "eva-runtime-mcp");
    startThread(monitorThread_, "eva-runtime-monitor");
    running_ = true;
}

void RuntimeWorkerHost::stop(int waitMs)
{
    stopThread(monitorThread_, waitMs);
    stopThread(mcpThread_, waitMs);
    stopThread(toolThread_, waitMs);
    stopThread(netThread_, waitMs);
    running_ = false;
}

bool RuntimeWorkerHost::isRunning() const
{
    return running_;
}

QThread *RuntimeWorkerHost::netThread()
{
    return &netThread_;
}

QThread *RuntimeWorkerHost::toolThread()
{
    return &toolThread_;
}

QThread *RuntimeWorkerHost::mcpThread()
{
    return &mcpThread_;
}

QThread *RuntimeWorkerHost::monitorThread()
{
    return &monitorThread_;
}

void RuntimeWorkerHost::startThread(QThread &thread, const char *name)
{
    if (thread.isRunning())
        return;
    thread.setObjectName(QString::fromLatin1(name));
    thread.start();
}

void RuntimeWorkerHost::stopThread(QThread &thread, int waitMs)
{
    if (!thread.isRunning())
        return;
    thread.quit();
    thread.wait(waitMs);
}
