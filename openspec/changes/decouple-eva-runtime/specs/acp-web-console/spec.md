## MODIFIED Requirements

### Requirement: Console uses EVA runtime as primary backend

The ACP web console SHALL use `EvaRuntime` directly as its primary backend once runtime decoupling is available, and SHALL use the legacy ControlChannel bridge only as a compatibility path.

#### Scenario: Runtime is available in ACP

- **WHEN** `eva_acp` can create `EvaRuntime`
- **THEN** `GET /api/backend/state`, `GET /v1/models`, `POST /api/backend/load`, `POST /api/runtime/reset`, and `POST /v1/chat/completions` MUST call runtime APIs directly.

#### Scenario: Legacy bridge is selected

- **WHEN** runtime direct hosting is disabled or unavailable but a main EVA bridge is reachable
- **THEN** ACP MAY use ControlChannel bridge commands and MUST identify the state source as bridge mode.

#### Scenario: Neither runtime nor bridge is available

- **WHEN** neither direct runtime nor bridge mode is available
- **THEN** ACP MUST return a degraded state with clear error details.
