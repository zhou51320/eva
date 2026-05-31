#pragma once

#include <QObject>
#include <QThread>

// RuntimeWorkerHost 集中运行层线程生命周期。
// EvaRuntime 创建/接管的服务对象会 moveToThread 到这些线程。
class RuntimeWorkerHost : public QObject
{
  public:
    explicit RuntimeWorkerHost(QObject *parent = nullptr);
    ~RuntimeWorkerHost() override;

    void start();
    void stop(int waitMs = 3000);
    bool isRunning() const;

    QThread *netThread();
    QThread *toolThread();
    QThread *mcpThread();
    QThread *monitorThread();

  private:
    void startThread(QThread &thread, const char *name);
    void stopThread(QThread &thread, int waitMs);

    QThread netThread_;
    QThread toolThread_;
    QThread mcpThread_;
    QThread monitorThread_;
    bool running_ = false;
};
