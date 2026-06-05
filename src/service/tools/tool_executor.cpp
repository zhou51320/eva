#include "service/tools/tool_executor.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaObject>

#include <exception>

namespace
{
RuntimeEventType progressTypeFromName(const QString &name)
{
    if (name == QStringLiteral("task_started")) return RuntimeEventType::TaskStarted;
    if (name == QStringLiteral("plan_created")) return RuntimeEventType::PlanCreated;
    if (name == QStringLiteral("tool_started")) return RuntimeEventType::ToolStarted;
    if (name == QStringLiteral("tool_output")) return RuntimeEventType::ToolOutput;
    if (name == QStringLiteral("tool_finished")) return RuntimeEventType::ToolFinished;
    if (name == QStringLiteral("recovering")) return RuntimeEventType::Recovering;
    if (name == QStringLiteral("skill_loading")) return RuntimeEventType::SkillLoading;
    if (name == QStringLiteral("skill_running")) return RuntimeEventType::SkillRunning;
    if (name == QStringLiteral("artifact_ready")) return RuntimeEventType::ArtifactReady;
    if (name == QStringLiteral("task_completed")) return RuntimeEventType::TaskCompleted;
    if (name == QStringLiteral("task_failed")) return RuntimeEventType::TaskFailed;
    return RuntimeEventType::BackendLog;
}

QJsonObject parseJsonObject(const QString &text)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError && doc.isObject()) return doc.object();
    return QJsonObject();
}
} // namespace

ToolExecutor::ToolExecutor(const QString &applicationDirPath)
    : xTool(applicationDirPath)
{
    connect(this, &xTool::tool2ui_state, this, [this](const QString &line, SIGNAL_STATE state) {
        emit runtimeEventReady(eventFromStateLine(line, state));
    });
    connect(this, &xTool::tool2ui_terminalCommandStarted, this, [this](const QString &command, const QString &cwd) {
        RuntimeEvent event;
        event.type = RuntimeEventType::ToolStarted;
        event.name = QStringLiteral("execute_command");
        event.text = command;
        event.payload.insert(QStringLiteral("tool_name"), QStringLiteral("execute_command"));
        event.payload.insert(QStringLiteral("command"), command);
        event.payload.insert(QStringLiteral("cwd"), cwd);
        emit runtimeEventReady(event);
    });
    connect(this, &xTool::tool2ui_terminalStdout, this, [this](const QString &chunk) {
        RuntimeEvent event;
        event.type = RuntimeEventType::ToolOutput;
        event.name = QStringLiteral("execute_command");
        event.text = chunk;
        event.payload.insert(QStringLiteral("stream"), QStringLiteral("stdout"));
        emit runtimeEventReady(event);
    });
    connect(this, &xTool::tool2ui_terminalStderr, this, [this](const QString &chunk) {
        RuntimeEvent event;
        event.type = RuntimeEventType::ToolOutput;
        event.name = QStringLiteral("execute_command");
        event.text = chunk;
        event.payload.insert(QStringLiteral("stream"), QStringLiteral("stderr"));
        emit runtimeEventReady(event);
    });
    connect(this, &xTool::tool2ui_terminalCommandFinished, this, [this](int exitCode, bool interrupted) {
        RuntimeEvent event;
        event.type = RuntimeEventType::ToolFinished;
        event.name = QStringLiteral("execute_command");
        event.payload.insert(QStringLiteral("exit_code"), exitCode);
        event.payload.insert(QStringLiteral("interrupted"), interrupted);
        emit runtimeEventReady(event);
    });
    connect(this, &xTool::tool2ui_pushover, this, [this](const QString &message) {
        const int newline = message.indexOf(QLatin1Char('\n'));
        if (newline < 0) return;
        const QJsonObject envelope = parseJsonObject(message.mid(newline + 1));
        if (envelope.isEmpty()) return;
        RuntimeEvent finished;
        finished.type = RuntimeEventType::ToolFinished;
        finished.name = message.left(newline).section(QLatin1Char(' '), 0, 0).trimmed();
        finished.text = envelope.value(QStringLiteral("summary")).toString();
        finished.payload.insert(QStringLiteral("envelope"), envelope);
        emit runtimeEventReady(finished);

        const QJsonArray artifacts = envelope.value(QStringLiteral("artifacts")).toArray();
        if (!artifacts.isEmpty())
        {
            RuntimeEvent artifact;
            artifact.type = RuntimeEventType::ArtifactReady;
            artifact.name = finished.name;
            artifact.text = QStringLiteral("%1 artifact(s) ready").arg(artifacts.size());
            artifact.payload.insert(QStringLiteral("artifacts"), artifacts);
            emit runtimeEventReady(artifact);
        }
        if (!envelope.value(QStringLiteral("ok")).toBool(true) && !envelope.value(QStringLiteral("recovery_hints")).toArray().isEmpty())
        {
            RuntimeEvent recovering;
            recovering.type = RuntimeEventType::Recovering;
            recovering.name = finished.name;
            recovering.text = envelope.value(QStringLiteral("summary")).toString();
            recovering.payload.insert(QStringLiteral("error"), envelope.value(QStringLiteral("error")).toObject());
            recovering.payload.insert(QStringLiteral("recovery_hints"), envelope.value(QStringLiteral("recovery_hints")).toArray());
            emit runtimeEventReady(recovering);
        }
    });
}

RuntimeEvent ToolExecutor::eventFromProgressLine(const QString &line, SIGNAL_STATE state) const
{
    Q_UNUSED(state);
    RuntimeEvent event;
    const int space = line.indexOf(QLatin1Char(' '));
    const QString typeName = line.mid(QStringLiteral("progress:").size(), space - QStringLiteral("progress:").size()).trimmed();
    event.type = progressTypeFromName(typeName);
    event.name = typeName;
    if (space >= 0)
    {
        event.payload = parseJsonObject(line.mid(space + 1));
        event.text = event.payload.value(QStringLiteral("summary")).toString();
    }
    return event;
}

RuntimeEvent ToolExecutor::eventFromStateLine(const QString &line, SIGNAL_STATE state) const
{
    if (line.startsWith(QStringLiteral("progress:"))) return eventFromProgressLine(line, state);

    RuntimeEvent event;
    event.type = (state == WRONG_SIGNAL) ? RuntimeEventType::Error : RuntimeEventType::BackendLog;
    event.text = line;
    event.payload.insert(QStringLiteral("signal_state"), static_cast<int>(state));
    if (state == WRONG_SIGNAL) event.error = line;
    if (line.startsWith(QStringLiteral("tool:start ")))
    {
        event.type = RuntimeEventType::ToolStarted;
        event.name = line.mid(QStringLiteral("tool:start ").size()).trimmed();
        event.payload.insert(QStringLiteral("tool_name"), event.name);
    }
    else if (line.contains(QStringLiteral("repeated previous failed command")) || line.contains(QStringLiteral("recover")))
    {
        event.type = RuntimeEventType::Recovering;
        event.name = QStringLiteral("execute_command");
    }
    return event;
}

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
