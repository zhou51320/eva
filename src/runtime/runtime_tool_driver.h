#pragma once

#include <QJsonObject>
#include <QString>

// 运行层工具驱动窄接口。
// EvaRuntime 只依赖该接口调度工具，不直接依赖 Widget 或 xTool 的具体实现。
class RuntimeToolDriver
{
  public:
    virtual ~RuntimeToolDriver() = default;

    virtual bool executeToolCall(const QJsonObject &call, quint64 turnId, QString *errorMessage = nullptr) = 0;
    virtual void cancelActiveRuntimeTool() = 0;
};

