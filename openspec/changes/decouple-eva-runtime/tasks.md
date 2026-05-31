## 1. Architecture Baseline

- [x] 1.1 Document the current Widget-owned runtime fields and map each field to runtime state, frontend state, or service configuration.
- [x] 1.2 Define `RuntimeMode`, `RuntimePhase`, `ConversationMode`, `RuntimeState`, `RuntimeCommand`, and `RuntimeEvent` structs/enums.
- [x] 1.3 Add a narrow `EvaRuntime` interface with initialize, shutdown, stateSnapshot, loadLocal, connectRemote, resetConversation, sendMessage, and stop methods.

## 2. Runtime Bootstrap

- [x] 2.1 Move reusable startup/config/bootstrap logic from `main.cpp` into a runtime bootstrap helper.
- [ ] 2.2 Create worker thread ownership inside runtime for net, tool, MCP, backend, CPU/GPU monitor where applicable. `EvaRuntime` now creates/owns NetClient, ToolExecutor, xMcp, and CPU/GPU monitor objects on RuntimeWorkerHost threads; remaining gap is Widget-owned BackendCoordinator/lazy-local-backend orchestration.
- [x] 2.3 Create the desktop Widget-to-runtime binding in `main.cpp`, while preserving legacy Net/Tool/MCP wiring until the extraction tasks migrate those paths.

## 3. Backend and Network Extraction

- [x] 3.1 Refactor `BackendCoordinator` so it no longer stores `Widget *` and instead consumes runtime settings/state/event sinks. Current implementation no longer includes `widget/widget.h` or casts its owner to `Widget`; desktop-specific access is isolated behind `BackendCoordinatorPort`.
- [x] 3.2 Move local load/link mode state transitions into runtime.
- [x] 3.3 Move `RequestSnapshot` construction from Widget/session code into runtime.
- [x] 3.4 Ensure runtime can send non-streaming and streaming chat through `NetClient` without Widget dependencies.

## 4. Session and Toolflow Extraction

- [x] 4.1 Refactor `SessionController` to depend on runtime/session state rather than `Widget *`.
- [ ] 4.2 Move `ui_messagesArray`, history ownership, compaction state, KV counters, and active turn state into runtime/session state. Current state: common session-message accessors, active-turn reads, compaction queued/in-flight/range metadata, and KV UI reads prefer `EvaRuntime`; KV counter projection now only updates runtime from explicit counter paths. Widget still keeps compatibility caches for message fallback, history persistence, compaction pending `InputPack`, and backend log-derived KV parsing.
- [x] 4.3 Refactor `ToolFlowController` to emit runtime tool events instead of directly modifying Widget records. Toolflow uses ports, and runtime now drives the executable tool dispatch while Widget keeps record projection/result continuation during migration.
- [x] 4.4 Keep Widget record rendering as a frontend projection of runtime events.

## 5. Widget Frontend Adapter

- [x] 5.1 Add a Widget-to-runtime adapter that converts button/input actions into runtime commands.
- [x] 5.2 Add runtime-to-Widget event handlers for UI state, output, records, tool status, backend logs, and errors.
- [ ] 5.3 Remove duplicate Widget ownership of runtime truth after each migrated path is verified. Current state: direct public `ui_messagesArray` reads outside the runtime adapter have been removed, generic runtime mirror sync no longer overwrites runtime KV counters unless the caller is an explicit counter projection, and compaction range/reason metadata is mirrored into runtime. Remaining duplicates are Widget compatibility caches for messages, KV counters, active turn/tool flags, compaction pending input/UI bookkeeping, and history store ownership.
- [x] 5.4 Preserve central five-button behavior and UI state rules during migration.

## 6. ACP Direct Runtime Hosting

- [x] 6.1 Update `eva_acp` to create `EvaRuntime` when direct runtime mode is enabled.
- [x] 6.2 Route ACP state, load, reset, stop, models, and chat endpoints to runtime direct APIs.
- [x] 6.3 Keep ControlChannel bridge as compatibility fallback and label state source clearly.
- [x] 6.4 Update WebUI state rendering to distinguish direct-runtime, bridge, and degraded modes.

## 7. Verification

- [x] 7.1 Build windowed EVA and `eva_acp`.
- [ ] 7.2 Smoke-test windowed EVA: local load, link mode, send, stream, stop, reset, tools, knowledge, MCP. Use `smoke-7.2.md` for the target-environment evidence checklist.
- [x] 7.3 Smoke-test ACP direct runtime mode without opening the main Widget window.
- [x] 7.4 Smoke-test ACP bridge fallback with main Widget window running.
- [x] 7.5 Run OpenSpec validation and update `docs/功能迭代.md` after implementation milestones.
