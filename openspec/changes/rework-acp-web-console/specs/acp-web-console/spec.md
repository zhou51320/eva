## ADDED Requirements

### Requirement: Console exposes service health

The ACP web console SHALL expose the ACP service state and bridge availability through HTTP so the WebUI can distinguish ready, starting, degraded, and unavailable states.

#### Scenario: Health is requested

- **WHEN** the browser requests `GET /health`
- **THEN** the response MUST include the ACP service status, bind address, bind port, backend state, and backend readiness.

#### Scenario: Bridge is unavailable

- **WHEN** the main EVA bridge cannot be reached
- **THEN** the response MUST identify the service as degraded or otherwise include a clear bridge/backend error instead of reporting a false ready state.

### Requirement: Console uses EVA as primary runtime source

The ACP runtime SHALL use the main EVA bridge as the primary source for runtime state, model list, load operations, resets, and chat execution whenever the bridge is connected.

#### Scenario: Bridge mode is available

- **WHEN** `eva_acp` can connect to the main EVA ACP bridge
- **THEN** `GET /api/backend/state`, `GET /v1/models`, `POST /api/backend/load`, `POST /api/runtime/reset`, and `POST /v1/chat/completions` MUST use bridge commands rather than ACP-local runtime state.

#### Scenario: Bridge mode is unavailable

- **WHEN** the main EVA ACP bridge is unavailable
- **THEN** the ACP runtime MUST either return a clear degraded error or use the documented ACP-local fallback path without mixing fallback state with main EVA state.

### Requirement: Console loads local and linked runtimes

The ACP web console SHALL allow users to load a local GGUF model or switch to a linked OpenAI-compatible endpoint through one runtime load API.

#### Scenario: Local model is loaded

- **WHEN** the browser posts `mode=local` with a valid local model path to `POST /api/backend/load`
- **THEN** EVA MUST enter loading state, apply the requested backend settings, start or restart the local backend as needed, and expose the resulting runtime state.

#### Scenario: Linked endpoint is applied

- **WHEN** the browser posts `mode=link` with endpoint, optional key, and model name to `POST /api/backend/load`
- **THEN** EVA MUST switch to linked mode, stop local backend ownership where applicable, and expose the linked endpoint and model in runtime state.

#### Scenario: Load request is invalid

- **WHEN** the browser posts an invalid model path, invalid port, missing endpoint, or unsupported backend choice
- **THEN** the API MUST return a non-2xx response with a clear error message and MUST NOT report the load as accepted.

### Requirement: Console resets the active EVA conversation

The ACP web console SHALL reset the active EVA conversation through a runtime reset API.

#### Scenario: Reset succeeds

- **WHEN** the browser posts `POST /api/runtime/reset` while EVA is idle
- **THEN** EVA MUST reset the current conversation and the API MUST return success with updated runtime state.

#### Scenario: Reset is blocked

- **WHEN** the browser posts `POST /api/runtime/reset` while EVA is actively running a turn or tool call
- **THEN** the API MUST return a non-2xx response with a busy/blocked message.

### Requirement: Console sends text chat through OpenAI-compatible API

The ACP web console SHALL send user text through `POST /v1/chat/completions` and receive OpenAI-compatible chat responses.

#### Scenario: Non-streaming chat succeeds

- **WHEN** the browser posts a valid chat completion request with `stream=false`
- **THEN** the response MUST contain `choices[0].message.content` with the assistant text produced by EVA.

#### Scenario: Streaming chat succeeds

- **WHEN** the browser posts a valid chat completion request with `stream=true`
- **THEN** the response MUST use `text/event-stream` and emit OpenAI-compatible `data:` chunks until `data: [DONE]`.

#### Scenario: Chat is blocked

- **WHEN** the browser sends chat while EVA is busy, unloaded, or bridge execution fails
- **THEN** the API MUST return a clear error response and the WebUI MUST render the failure in the assistant message slot.

### Requirement: Console renders runtime state and model list

The WebUI SHALL render current mode, current model, endpoint, backend state, model list, and recent errors from ACP APIs.

#### Scenario: Runtime state refreshes

- **WHEN** the user opens the WebUI or clicks refresh
- **THEN** the UI MUST request health, backend state, and model list, then update the visible status fields.

#### Scenario: No models are available

- **WHEN** the model list is empty
- **THEN** the UI MUST show an empty state and MUST NOT silently select a nonexistent local model.

### Requirement: Console preserves first-round extension boundaries

The ACP web console SHALL keep first-round controls focused on runtime and text chat while showing disabled placeholders for future modules.

#### Scenario: Future module is visible but unavailable

- **WHEN** the user sees knowledge base, MCP, tools, attachments, visual/audio, or machine-control entries
- **THEN** those entries MUST be disabled or clearly non-interactive until their bridge protocols are specified.
