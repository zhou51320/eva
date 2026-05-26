# Widget Runtime Field Map

本文记录当前 `Widget` 中已经被运行时语义占用的字段，并给出迁移落点。目标是避免后续继续把同一个要素用多组变量控制。

## Runtime State

| Current owner | Current fields | Target |
| --- | --- | --- |
| `Widget` | `is_load`, `is_run`, `turnActive_`, `toolInvocationActive_`, `compactionInFlight_`, `compactionQueued_` | `RuntimeState::phase` and explicit active flags |
| `Widget` | `ui_mode`, `ui_state`, `currentTask_` | `RuntimeState::mode`, `RuntimeState::conversationMode`, `RuntimeState::task` |
| `Widget` | `activeTurnId_`, `nextTurnId_`, `currentSlotId_` | runtime turn/session state |
| `Widget` | `current_api`, `apis`, `ui_SETTINGS.modelpath`, `historypath`, `currentpath` | runtime endpoint/model state plus service configuration |
| `Widget` | `kvUsed_`, `kvUsedBeforeTurn_`, `kvStreamedTurn_`, `kvPromptTokensTurn_`, `lastReasoningTokens_`, `slotCtxMax_` | runtime token/KV counters |
| `Widget` | `backendOnline_`, `backendLifecycleState_`, `activeServerHost_`, `activeServerPort_`, `backendLifecycleTimer_` | runtime backend state |

## Service Configuration

| Current owner | Current fields | Target |
| --- | --- | --- |
| `Widget` | `ui_SETTINGS`, `compactionSettings_`, `schedulerSettings_` | runtime settings snapshots loaded from `EVA_TEMP/eva_config.ini` |
| `Widget` | `ui_DATES`, `date_map`, `custom1_date_system`, `custom2_date_system`, `ui_template` | runtime prompt/date configuration; Widget renders editor only |
| `Widget` | `ui_tool_call_mode`, `is_load_tool`, tool checkboxes, `ui_extra_prompt` | runtime tool configuration plus frontend editor state |
| `Widget` | `ui_engineer_ischecked`, docker settings, engineer workdir | tool/engineer service configuration |
| `Widget` | `scheduler_`, scheduled queues | scheduler service owned by runtime |

## Frontend State

| Current owner | Current fields | Target |
| --- | --- | --- |
| `Widget` | output widgets, record widgets, current record indexes | Widget frontend projection of runtime output/record events |
| `Widget` | button enabled/disabled state, loading animation, tray/window visibility | Widget-only frontend state |
| `Widget` | settings/date dialogs and line edits | Widget editors that produce runtime commands/settings snapshots |
| `Widget` | drag/drop thumbnails and draft input attachments | Widget input draft state until submitted as runtime command |
| `Widget` | QSS/theme/font/shortcut state | Widget-only frontend state |

## Runtime Services

| Existing class | Current coupling | Migration target |
| --- | --- | --- |
| `SessionController` | stores `Widget *` and directly reads UI/input/history fields | depend on runtime session state and emit runtime events |
| `ToolFlowController` | stores `Widget *` and mutates records/tool flags | consume model output and emit tool events/results |
| `BackendCoordinator` | stores `Widget *` and mutates UI/backend fields | consume runtime settings/state sink |
| `NetClient` | already accepts `RequestSnapshot`, but snapshot is built by `Widget` | runtime builds `RequestSnapshot` |
| `ToolExecutor` | thin `xTool` wrapper connected directly to Widget signals | runtime owns worker and translates tool events |
| `xMcp` / `Expend` | many direct signal links to Widget and Tool | runtime service boundary first, UI panels later |

## Migration Rule

New code must not add new runtime truth to `Widget`. If a field is needed by both Widget and ACP/WebUI, it belongs in `EvaRuntime` or a runtime-owned service state object, then Widget renders it from events/snapshots.
