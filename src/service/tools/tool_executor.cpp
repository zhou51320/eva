#include "service/tools/tool_executor.h"

#include <QJsonDocument>
#include <QMetaObject>

#include <exception>

// ToolExecutor 目前仅作为 xTool 的轻量包装，
// 后续将把工具注册与执行流程逐步下沉到该模块。

bool ToolExecutor::executeToolCall(const QJsonObject &call, quint64 turnId, QString *errorMessage)
{
    if (call.isEmpty())
    {
        if (errorMessage) *errorMessage = QStringLiteral("Tool call is empty.");
        return false;
    }

    const QByteArray raw = QJsonDocument(call).toJson(QJsonDocument::Compact);
    mcp::json parsed;
    try
    {
        parsed = mcp::json::parse(raw.constData());
    }
    catch (const std::exception &error)
    {
        if (errorMessage) *errorMessage = QStringLiteral("Tool call parse failed: %1").arg(QString::fromUtf8(error.what()));
        return false;
    }

    recv_turn(turnId);
    QMetaObject::invokeMethod(this, [this, parsed]()
    {
        Exec(parsed);
    }, Qt::QueuedConnection);
    return true;
}

void ToolExecutor::cancelActiveRuntimeTool()
{
    cancelExecuteCommand();
    cancelActiveTool();
}
