## 1. Bridge Skills Contract

- [x] 1.1 Add bridge-side Skills payload helpers that serialize `SkillManager::SkillRecord` fields needed by WebUI.
- [x] 1.2 Add bridge command handling for listing/refreshed Skills state through the main EVA process.
- [x] 1.3 Add bridge command handling for `set_enabled` without changing the engineer/tool capability state.
- [x] 1.4 Add bridge command handling for removing a Skill and returning a clear error for missing/invalid ids.

## 2. ACP Runtime and HTTP API

- [x] 2.1 Add `AcpRuntime` methods for listing Skills and applying Skills actions through `AcpBridgeClient`.
- [x] 2.2 Add `AcpBridgeClient` request helpers for the new Skills bridge commands with timeout/error propagation.
- [x] 2.3 Add `GET /api/runtime/skills` to return bridge-backed Skills state or a clear unavailable response.
- [x] 2.4 Add `POST /api/runtime/skills` to accept only first-version operations: `set_enabled`, `remove`, and `refresh`.
- [x] 2.5 Ensure unsupported methods or unknown Skills operations return explicit errors and do not mutate state.

## 3. WebUI Data Layer

- [x] 3.1 Add TypeScript types for Skill records and Skills API payloads.
- [x] 3.2 Add API functions to fetch Skills state and apply Skills actions.
- [x] 3.3 Add store state/actions for refreshing Skills, toggling a Skill, and removing a Skill.
- [x] 3.4 Keep Skills refresh separate from existing model/backend refresh to avoid introducing bridge command concurrency regressions.

## 4. WebUI Skills Surface

- [x] 4.1 Add a dedicated Skills tab or section in the runtime drawer/control panel separate from Tools.
- [x] 4.2 Render installed Skills with id, description/license metadata, enabled switch, and path/detail text.
- [x] 4.3 Show a clear unavailable message when bridge-backed Skills management is not available.
- [x] 4.4 Show a hint when engineer is disabled that enabled Skills require system engineer to be injected and used.
- [x] 4.5 Require explicit user confirmation before removing a Skill.
- [x] 4.6 Do not add browser zip upload/import controls in this first version.

## 5. Verification

- [x] 5.1 Verify OpenSpec artifacts with `openspec status --change acp-web-skills-management`.
- [x] 5.2 Build or type-check the WebUI after TypeScript changes.
- [x] 5.3 Build the affected C++ target or run the existing project verification command for ACP changes.
- [ ] 5.4 Manually verify bridge mode can list, toggle, refresh, and remove installed Skills from WebUI without changing engineer state implicitly.
