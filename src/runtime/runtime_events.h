#pragma once

#include <QJsonObject>
#include <QString>

#include "runtime/runtime_state.h"

enum class RuntimeEventType
{
    StateChanged,
    BackendLog,
    OutputChunk,
    RecordAdd,
    RecordUpdate,
    TaskStarted,
    PlanCreated,
    ToolStarted,
    ToolOutput,
    ToolFinished,
    Recovering,
    SkillLoading,
    SkillRunning,
    ArtifactReady,
    TaskCompleted,
    TaskFailed,
    TurnFinished,
    Metrics,
    Error,
    CommandRejected,
};

inline QString runtimeEventTypeName(RuntimeEventType type)
{
    switch (type)
    {
    case RuntimeEventType::StateChanged: return QStringLiteral("state_changed");
    case RuntimeEventType::BackendLog: return QStringLiteral("backend_log");
    case RuntimeEventType::OutputChunk: return QStringLiteral("output_chunk");
    case RuntimeEventType::RecordAdd: return QStringLiteral("record_add");
    case RuntimeEventType::RecordUpdate: return QStringLiteral("record_update");
    case RuntimeEventType::TaskStarted: return QStringLiteral("task_started");
    case RuntimeEventType::PlanCreated: return QStringLiteral("plan_created");
    case RuntimeEventType::ToolStarted: return QStringLiteral("tool_started");
    case RuntimeEventType::ToolOutput: return QStringLiteral("tool_output");
    case RuntimeEventType::ToolFinished: return QStringLiteral("tool_finished");
    case RuntimeEventType::Recovering: return QStringLiteral("recovering");
    case RuntimeEventType::SkillLoading: return QStringLiteral("skill_loading");
    case RuntimeEventType::SkillRunning: return QStringLiteral("skill_running");
    case RuntimeEventType::ArtifactReady: return QStringLiteral("artifact_ready");
    case RuntimeEventType::TaskCompleted: return QStringLiteral("task_completed");
    case RuntimeEventType::TaskFailed: return QStringLiteral("task_failed");
    case RuntimeEventType::TurnFinished: return QStringLiteral("turn_finished");
    case RuntimeEventType::Metrics: return QStringLiteral("metrics");
    case RuntimeEventType::Error: return QStringLiteral("error");
    case RuntimeEventType::CommandRejected: return QStringLiteral("command_rejected");
    }
    return QStringLiteral("unknown");
}

// 前端中立事件。Widget/WebUI/CLI 都只能依赖该事件语义，不依赖 UI 控件。
struct RuntimeEvent
{
    RuntimeEventType type = RuntimeEventType::StateChanged;
    RuntimeState state;
    QString role;
    QString text;
    QString name;
    QString error;
    QJsonObject payload;
};

inline QJsonObject runtimeEventToJson(const RuntimeEvent &event)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("type"), runtimeEventTypeName(event.type));
    obj.insert(QStringLiteral("role"), event.role);
    obj.insert(QStringLiteral("text"), event.text);
    obj.insert(QStringLiteral("name"), event.name);
    obj.insert(QStringLiteral("error"), event.error);
    obj.insert(QStringLiteral("payload"), event.payload);
    obj.insert(QStringLiteral("state"), runtimeStateToJson(event.state));
    return obj;
}

Q_DECLARE_METATYPE(RuntimeEventType)
Q_DECLARE_METATYPE(RuntimeEvent)
