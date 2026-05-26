#pragma once

#include "runtime/runtime_network_driver.h"
#include "service/net/request_snapshot.h"

class xNet;

// 网络客户端：封装 xNet，使用 RequestSnapshot 进行一次性发送。
class NetClient : public RuntimeNetworkDriver
{
    Q_OBJECT
public:
    explicit NetClient(QObject *parent = nullptr);
    ~NetClient() override;

public slots:
    void send(const RequestSnapshot &snapshot) override;
    void stop(bool stop) override;

private:
    void ensureNet();

    xNet *net_ = nullptr;
};
