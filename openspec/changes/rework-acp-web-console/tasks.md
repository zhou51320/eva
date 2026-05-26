## 1. Runtime Boundary

- [x] 1.1 Audit `AcpRuntime::bridgeModeEnabled()` and make bridge availability explicit in backend state instead of hiding it behind fallback behavior.
- [x] 1.2 Define a single normalized backend-state payload for bridge mode and ACP-local fallback mode, including mode, phase, ready, model, endpoint, error, and bridge status.
- [x] 1.3 Ensure ACP-local fallback cannot be mistaken for main EVA state when the bridge is unavailable.

## 2. Bridge Protocol

- [x] 2.1 Review `bridge_get_state`, `bridge_list_models`, `bridge_apply_load`, `bridge_reset`, and `bridge_send` responses for consistent `ok`, `accepted`, `state`, `models`, and `error` fields.
- [x] 2.2 Add or normalize busy/loading/error responses from `Widget::applyAcpBridgeLoad`, `Widget::resetAcpBridgeConversation`, and `Widget::sendBridgeText`.
- [x] 2.3 Ensure output, ui_state, state_log, snapshot, and record events carry enough information for ACP to finish streaming reliably.

## 3. HTTP and OpenAI Compatibility

- [x] 3.1 Make `/health` and `/api/backend/state` report bridge availability and degraded states clearly.
- [x] 3.2 Make `/api/backend/load` return accurate accepted/error responses for local and linked mode.
- [x] 3.3 Make `/api/runtime/reset` preserve busy/error semantics from the bridge.
- [x] 3.4 Make `/v1/models` prefer bridge models when available and return documented empty/error behavior when unavailable.
- [x] 3.5 Make `/v1/chat/completions` handle non-streaming and streaming bridge chat with OpenAI-compatible response shapes.

## 4. WebUI Behavior

- [x] 4.1 Update status rendering so health, bridge state, runtime phase, ready state, current model, endpoint, and last error are visible and not conflated.
- [x] 4.2 Update load controls so local/link mode submit the right payload and show accepted/loading/error feedback.
- [x] 4.3 Update chat rendering so streaming, completion, empty response, and failed request states are distinguishable.
- [x] 4.4 Keep disabled future modules visible but clearly non-interactive until their protocols are specified.
- [x] 4.5 Keep WebUI local session storage scoped to display history and reset it only after `/api/runtime/reset` succeeds or intentionally degrades.

## 5. Verification

- [ ] 5.1 Build `eva` and `eva_acp` with the existing CMake workflow.
- [ ] 5.2 Smoke-test WebUI startup, `/health`, `/api/backend/state`, `/v1/models`, and static resource serving.
- [ ] 5.3 Smoke-test bridge mode with main EVA running: list models, load local, switch link mode, reset, send non-streaming chat, send streaming chat.
- [ ] 5.4 Smoke-test bridge-unavailable behavior with main EVA stopped.
- [x] 5.5 Run `python3 scripts/update_feature_log.py` with a concise implementation summary after code changes are complete.
