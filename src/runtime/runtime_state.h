#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

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
    QString currentTask;

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
    int kvUsedBeforeTurn = 0;
    int kvStreamedTurn = 0;
    int kvTurnTokens = 0;
    int kvCapacity = 0;
    int kvPercent = 0;
    int promptTokens = 0;
    int generatedTokens = 0;
    int reasoningTokens = 0;
    int messageCount = 0;
    QString systemPrompt;
    QStringList stopwords;
    int toolCallMode = DEFAULT_TOOL_CALL_MODE;
    QString pendingToolResult;
    QString pendingToolName;
    QString pendingToolCallId;
    QString lastToolCallName;
    bool compactionActive = false;
    bool compactionQueued = false;
    QString historySessionId;
    QJsonArray messages;

    QString lastError;
    QDateTime updatedAt;
    AppContext context;
    SETTINGS settings;
    COMPACTION_SETTINGS compactionSettings;
    APIS apis;
};

inline QJsonObject runtimeStateToJson(const RuntimeState &state)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("initialized"), state.initialized);
    obj.insert(QStringLiteral("mode"), runtimeModeName(state.mode));
    obj.insert(QStringLiteral("conversation_mode"), conversationModeName(state.conversationMode));
    obj.insert(QStringLiteral("phase"), runtimePhaseName(state.phase));
    obj.insert(QStringLiteral("current_task"), state.currentTask);
    obj.insert(QStringLiteral("state_source"), state.stateSource);
    obj.insert(QStringLiteral("current_model"), state.currentModel);
    obj.insert(QStringLiteral("current_model_path"), state.currentModelPath);
    obj.insert(QStringLiteral("endpoint"), state.endpoint);
    obj.insert(QStringLiteral("backend_choice"), state.backendChoice);
    obj.insert(QStringLiteral("backend_resolved"), state.backendResolved);
    obj.insert(QStringLiteral("backend_lifecycle"), backendLifecycleStateName(state.backendLifecycle));
    obj.insert(QStringLiteral("backend_ready"), state.backendReady);
    obj.insert(QStringLiteral("settings_model_path"), state.settings.modelpath);
    obj.insert(QStringLiteral("settings_mmproj_path"), state.settings.mmprojpath);
    obj.insert(QStringLiteral("settings_lora_path"), state.settings.lorapath);
    obj.insert(QStringLiteral("nctx"), state.settings.nctx);
    obj.insert(QStringLiteral("ngl"), state.settings.ngl);
    obj.insert(QStringLiteral("nthread"), state.settings.nthread);
    obj.insert(QStringLiteral("parallel"), state.settings.hid_parallel);
    obj.insert(QStringLiteral("temperature"), state.settings.temp);
    obj.insert(QStringLiteral("repeat"), state.settings.repeat);
    obj.insert(QStringLiteral("top_k"), state.settings.top_k);
    obj.insert(QStringLiteral("top_p"), state.settings.hid_top_p);
    obj.insert(QStringLiteral("n_predict"), state.settings.hid_npredict);
    obj.insert(QStringLiteral("reasoning_effort"), state.settings.reasoning_effort);
    obj.insert(QStringLiteral("api_endpoint"), state.apis.api_endpoint);
    obj.insert(QStringLiteral("api_model"), state.apis.api_model);
    obj.insert(QStringLiteral("api_chat_endpoint"), state.apis.api_chat_endpoint);
    obj.insert(QStringLiteral("api_completion_endpoint"), state.apis.api_completion_endpoint);
    obj.insert(QStringLiteral("turn_active"), state.turnActive);
    obj.insert(QStringLiteral("tool_active"), state.toolActive);
    obj.insert(QStringLiteral("active_turn_id"), QString::number(state.activeTurnId));
    obj.insert(QStringLiteral("slot_id"), state.slotId);
    obj.insert(QStringLiteral("kv_used"), state.kvUsed);
    obj.insert(QStringLiteral("kv_used_before_turn"), state.kvUsedBeforeTurn);
    obj.insert(QStringLiteral("kv_streamed_turn"), state.kvStreamedTurn);
    obj.insert(QStringLiteral("kv_turn_tokens"), state.kvTurnTokens);
    obj.insert(QStringLiteral("kv_capacity"), state.kvCapacity);
    obj.insert(QStringLiteral("kv_percent"), state.kvPercent);
    obj.insert(QStringLiteral("prompt_tokens"), state.promptTokens);
    obj.insert(QStringLiteral("generated_tokens"), state.generatedTokens);
    obj.insert(QStringLiteral("reasoning_tokens"), state.reasoningTokens);
    obj.insert(QStringLiteral("message_count"), state.messageCount);
    obj.insert(QStringLiteral("system_prompt_length"), state.systemPrompt.size());
    obj.insert(QStringLiteral("stopwords"), QJsonArray::fromStringList(state.stopwords));
    obj.insert(QStringLiteral("tool_call_mode"), state.toolCallMode);
    obj.insert(QStringLiteral("pending_tool_result_length"), state.pendingToolResult.size());
    obj.insert(QStringLiteral("pending_tool_name"), state.pendingToolName);
    obj.insert(QStringLiteral("pending_tool_call_id"), state.pendingToolCallId);
    obj.insert(QStringLiteral("last_tool_call_name"), state.lastToolCallName);
    obj.insert(QStringLiteral("compaction_active"), state.compactionActive);
    obj.insert(QStringLiteral("compaction_queued"), state.compactionQueued);
    obj.insert(QStringLiteral("compaction_enabled"), state.compactionSettings.enabled);
    obj.insert(QStringLiteral("compaction_trigger_ratio"), state.compactionSettings.trigger_ratio);
    obj.insert(QStringLiteral("compaction_reserve_tokens"), state.compactionSettings.reserve_tokens);
    obj.insert(QStringLiteral("compaction_keep_last_messages"), state.compactionSettings.keep_last_messages);
    obj.insert(QStringLiteral("history_session_id"), state.historySessionId);
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
