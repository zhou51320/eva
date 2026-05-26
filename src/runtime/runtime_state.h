#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include "app/app_context.h"
#include "xconfig.h"

// 运行模式：与旧 EVA_MODE 对齐，但不再绑定任何具体 UI 控件。
enum class RuntimeMode
{
    Local,
    Link,
};

// 对话模式：与旧 EVA_STATE 对齐，用于表达同一运行层的对话/补完行为。
enum class ConversationMode
{
    Chat,
    Complete,
};

// 运行阶段：收敛 is_load/is_run/turnActive_/toolInvocationActive_ 等多组旧状态。
enum class RuntimePhase
{
    Unloaded,
    Loading,
    Ready,
    Running,
    ToolRunning,
    Recording,
    Error,
    ShuttingDown,
};

inline QString runtimeModeName(RuntimeMode mode)
{
    return mode == RuntimeMode::Link ? QStringLiteral("link") : QStringLiteral("local");
}

inline QString conversationModeName(ConversationMode mode)
{
    return mode == ConversationMode::Complete ? QStringLiteral("complete") : QStringLiteral("chat");
}

inline QString runtimePhaseName(RuntimePhase phase)
{
    switch (phase)
    {
    case RuntimePhase::Unloaded: return QStringLiteral("unloaded");
    case RuntimePhase::Loading: return QStringLiteral("loading");
    case RuntimePhase::Ready: return QStringLiteral("ready");
    case RuntimePhase::Running: return QStringLiteral("running");
    case RuntimePhase::ToolRunning: return QStringLiteral("tool_running");
    case RuntimePhase::Recording: return QStringLiteral("recording");
    case RuntimePhase::Error: return QStringLiteral("error");
    case RuntimePhase::ShuttingDown: return QStringLiteral("shutting_down");
    }
    return QStringLiteral("unknown");
}

// 前端/API 读取的统一运行快照。该结构只描述事实，不保存 UI 控件状态。
struct RuntimeState
{
    bool initialized = false;
    RuntimeMode mode = RuntimeMode::Local;
    ConversationMode conversationMode = ConversationMode::Chat;
    RuntimePhase phase = RuntimePhase::Unloaded;

    QString stateSource = QStringLiteral("runtime");
    QString currentModel;
    QString currentModelPath;
    QString endpoint;
    QString backendChoice = QStringLiteral("auto");
    QString backendResolved;
    BackendLifecycleState backendLifecycle = BackendLifecycleState::Stopped;
    bool backendReady = false;
    bool turnActive = false;
    bool toolActive = false;
    quint64 activeTurnId = 0;
    int slotId = -1;

    int kvUsed = 0;
    int kvCapacity = 0;
    int kvPercent = 0;
    int promptTokens = 0;
    int generatedTokens = 0;
    int reasoningTokens = 0;

    QString lastError;
    QDateTime updatedAt;
    AppContext context;
    SETTINGS settings;
    APIS apis;
};

inline QJsonObject runtimeStateToJson(const RuntimeState &state)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("initialized"), state.initialized);
    obj.insert(QStringLiteral("mode"), runtimeModeName(state.mode));
    obj.insert(QStringLiteral("conversation_mode"), conversationModeName(state.conversationMode));
    obj.insert(QStringLiteral("phase"), runtimePhaseName(state.phase));
    obj.insert(QStringLiteral("state_source"), state.stateSource);
    obj.insert(QStringLiteral("current_model"), state.currentModel);
    obj.insert(QStringLiteral("current_model_path"), state.currentModelPath);
    obj.insert(QStringLiteral("endpoint"), state.endpoint);
    obj.insert(QStringLiteral("backend_choice"), state.backendChoice);
    obj.insert(QStringLiteral("backend_resolved"), state.backendResolved);
    obj.insert(QStringLiteral("backend_lifecycle"), backendLifecycleStateName(state.backendLifecycle));
    obj.insert(QStringLiteral("backend_ready"), state.backendReady);
    obj.insert(QStringLiteral("api_endpoint"), state.apis.api_endpoint);
    obj.insert(QStringLiteral("api_model"), state.apis.api_model);
    obj.insert(QStringLiteral("api_chat_endpoint"), state.apis.api_chat_endpoint);
    obj.insert(QStringLiteral("api_completion_endpoint"), state.apis.api_completion_endpoint);
    obj.insert(QStringLiteral("turn_active"), state.turnActive);
    obj.insert(QStringLiteral("tool_active"), state.toolActive);
    obj.insert(QStringLiteral("active_turn_id"), QString::number(state.activeTurnId));
    obj.insert(QStringLiteral("slot_id"), state.slotId);
    obj.insert(QStringLiteral("kv_used"), state.kvUsed);
    obj.insert(QStringLiteral("kv_capacity"), state.kvCapacity);
    obj.insert(QStringLiteral("kv_percent"), state.kvPercent);
    obj.insert(QStringLiteral("prompt_tokens"), state.promptTokens);
    obj.insert(QStringLiteral("generated_tokens"), state.generatedTokens);
    obj.insert(QStringLiteral("reasoning_tokens"), state.reasoningTokens);
    obj.insert(QStringLiteral("last_error"), state.lastError);
    obj.insert(QStringLiteral("updated_at"), state.updatedAt.toString(Qt::ISODateWithMs));
    obj.insert(QStringLiteral("app_dir"), state.context.appDir);
    obj.insert(QStringLiteral("temp_dir"), state.context.tempDir);
    obj.insert(QStringLiteral("models_dir"), state.context.modelsDir);
    return obj;
}

Q_DECLARE_METATYPE(RuntimeMode)
Q_DECLARE_METATYPE(ConversationMode)
Q_DECLARE_METATYPE(RuntimePhase)
Q_DECLARE_METATYPE(RuntimeState)
