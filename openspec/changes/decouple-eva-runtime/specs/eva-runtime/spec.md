## ADDED Requirements

### Requirement: Runtime exposes windowless lifecycle

EVA SHALL provide an `EvaRuntime` that can initialize, run, and shut down without constructing the main Widget window.

#### Scenario: Runtime starts in a core application

- **WHEN** `EvaRuntime` is created under `QCoreApplication`
- **THEN** it MUST initialize application context, temp paths, configuration, backend discovery, and service objects without requiring QWidget controls.

#### Scenario: Runtime shuts down

- **WHEN** the host process requests runtime shutdown
- **THEN** runtime MUST stop active network requests, tool execution, local backend processes, MCP services, and worker threads cleanly.

### Requirement: Runtime exposes stable state snapshot

EVA SHALL expose a structured runtime state snapshot independent of any UI controls.

#### Scenario: State is requested

- **WHEN** a frontend requests the runtime state
- **THEN** runtime MUST return mode, phase, readiness, current model, endpoint, conversation mode, active turn state, backend details, KV usage, and last error.

#### Scenario: State changes

- **WHEN** runtime changes phase, mode, model, endpoint, backend readiness, or error state
- **THEN** runtime MUST emit a state event that frontends can render.

### Requirement: Runtime accepts structured commands

EVA SHALL accept structured commands for model loading, linked endpoint configuration, reset, send, and stop.

#### Scenario: Local load command

- **WHEN** a frontend sends a local load command with model path and backend settings
- **THEN** runtime MUST apply the settings, start or restart the local backend, and emit loading/ready/error events.

#### Scenario: Linked endpoint command

- **WHEN** a frontend sends a linked endpoint command
- **THEN** runtime MUST switch to link mode, persist relevant settings, and expose the remote endpoint/model in state.

#### Scenario: Send command

- **WHEN** a frontend sends a text message command while runtime is ready
- **THEN** runtime MUST build the model request, execute the turn, emit output events, handle tool loops if enabled, and finish with a completion event.

#### Scenario: Stop command

- **WHEN** a frontend sends a stop command while a turn or tool call is active
- **THEN** runtime MUST cancel active network/tool work and emit a stopped or error event.

### Requirement: Runtime publishes frontend-neutral events

EVA SHALL publish events that can be consumed by Qt Widget, ACP/WebUI, and future frontends without relying on Widget-specific record structures.

#### Scenario: Output is streamed

- **WHEN** model output arrives
- **THEN** runtime MUST emit output chunks with role/type metadata sufficient for a frontend to render assistant text and reasoning text.

#### Scenario: Tool flow runs

- **WHEN** a tool call is parsed and executed
- **THEN** runtime MUST emit tool start, tool output/result, and tool completion events.

#### Scenario: Runtime receives model tool calls

- **WHEN** the network layer reports OpenAI-style `tool_calls` or `function_call` payloads
- **THEN** runtime MUST parse the payload into frontend-neutral tool state and dispatch it through a runtime-owned tool driver when one is attached.
- **AND** runtime MUST report whether a tool driver is attached so frontends do not confuse configured tool schemas with executable tool capability.

#### Scenario: Backend logs are produced

- **WHEN** local backend emits lifecycle logs or errors
- **THEN** runtime MUST emit backend log/state events without directly writing to a UI text box.

### Requirement: Widget becomes a runtime frontend

The Qt Widget frontend SHALL use `EvaRuntime` as its source of runtime truth.

#### Scenario: User clicks send in Widget

- **WHEN** the user sends input from the Qt Widget
- **THEN** Widget MUST convert UI input into a runtime send command instead of directly owning the turn execution.

#### Scenario: Runtime emits events

- **WHEN** runtime emits state, output, record, tool, or error events
- **THEN** Widget MUST render those events while avoiding duplicate ownership of runtime state.

### Requirement: ACP can host runtime without main window

The ACP service SHALL be able to host `EvaRuntime` directly after runtime decoupling is complete.

#### Scenario: ACP starts without Widget

- **WHEN** `eva_acp` starts and no main EVA window is running
- **THEN** ACP MUST be able to create `EvaRuntime` and expose load, state, reset, stop, model list, and chat APIs.

#### Scenario: ACP is used as WebUI backend

- **WHEN** the browser uses ACP endpoints
- **THEN** ACP MUST call `EvaRuntime` directly rather than requiring ControlChannel bridge to a Widget instance.
