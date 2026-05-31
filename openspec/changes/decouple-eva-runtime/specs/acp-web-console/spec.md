## MODIFIED Requirements

### Requirement: Console uses EVA runtime or main EVA bridge instead of model bypass

The ACP web console SHALL route user operations through an EVA-owned runtime path. It SHALL prefer the main EVA bridge when the windowed app is reachable because that path currently owns tools, knowledge, MCP, TTS, and other Widget-hosted abilities; otherwise it SHALL use `EvaRuntime` directly when available. It MUST NOT silently bypass EVA by forwarding chat directly to a configured model endpoint.

#### Scenario: Runtime is available in ACP

- **WHEN** `eva_acp` can create `EvaRuntime` and no main EVA bridge is reachable
- **THEN** `GET /api/backend/state`, `GET /v1/models`, `POST /api/backend/load`, `POST /api/runtime/reset`, `POST /api/runtime/stop`, and `POST /v1/chat/completions` MUST call runtime APIs directly.

#### Scenario: Main EVA bridge is reachable while direct runtime exists

- **WHEN** `eva_acp` can reach the main window bridge and can also host `EvaRuntime`
- **THEN** Web chat, load, reset, stop, and state operations MUST prefer the main EVA bridge and report `chat_route` as `eva_bridge` so tools, knowledge, MCP, TTS, and other Widget-hosted abilities stay available.
- **AND** the state MUST report the Widget as conversation owner, because the bridge sends the latest WebUI user text into the main EVA conversation rather than treating the browser-local message list as authoritative context.

#### Scenario: Main EVA bridge command fails

- **WHEN** the main EVA bridge is reachable but a chat, load, reset, stop, or state command fails
- **THEN** ACP MUST return the bridge failure clearly and MUST NOT fall back to direct runtime for that command, because direct runtime does not own the Widget-hosted tool, knowledge, MCP, and TTS context.

#### Scenario: Legacy bridge is selected

- **WHEN** runtime direct hosting is disabled or unavailable but a main EVA bridge is reachable
- **THEN** ACP MAY use ControlChannel bridge commands and MUST identify the state source as bridge mode.

#### Scenario: Chat route is unavailable

- **WHEN** neither `EvaRuntime` nor the main EVA bridge can handle chat
- **THEN** `POST /v1/chat/completions` MUST fail with a clear error instead of forwarding directly to the configured model endpoint.

#### Scenario: Neither runtime nor bridge is available

- **WHEN** neither direct runtime nor bridge mode is available
- **THEN** ACP MUST return a degraded state with clear error details.
