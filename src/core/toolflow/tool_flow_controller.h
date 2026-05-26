#pragma once

#include <QObject>
#include <QString>

#include "core/toolflow/tool_flow_host_port.h"

// 工具流控制器：负责工具调用解析、工具回环与工具结果回注。
class ToolFlowController : public QObject
{
    Q_OBJECT
public:
    explicit ToolFlowController(QObject *owner);

    void recvPushover();
    void recvToolCalls(const QString &payload);
    void recvToolPushover(QString toolResult);

private:
    ToolFlowHostPort *hostPort() const;

    ToolFlowHostPort *host_ = nullptr; // 不拥有，工具流宿主适配端口
};
