## Context

EVA currently manages Skills in the Qt settings/contract dialog. `SkillManager` loads installed Skills from `EVA_SKILLS`, parses `SKILL.md` metadata, tracks enabled state in memory, imports zip archives, removes Skills, and composes the prompt block used by the system engineer flow. The ACP/WebUI control panel currently exposes backend loading and tool capability toggles, but it has no independent Skills surface.

The near-term architecture still treats the main Qt process as the owner of tool execution and Skills state when ACP is bridged to a running EVA instance. Direct `eva_acp` runtime does not yet host the full engineer/tool execution chain, so this change must not pretend that direct runtime can execute or manage Skills independently.

The working tree already contains unrelated UI changes. Implementation must be surgical and avoid rewriting existing WebUI message/rendering behavior.

## Goals / Non-Goals

**Goals:**

- Add a dedicated ACP/WebUI Skills management surface.
- Let WebUI list installed Skills with metadata and enabled state.
- Let WebUI enable/disable and remove installed Skills through the main EVA bridge.
- Let WebUI refresh Skills state on demand.
- Keep Qt UI and WebUI synchronized by reusing `SkillManager` as the single source of truth in bridge mode.
- Keep Skills conceptually separate from tools while documenting that enabled Skills are only injected/usable when the system engineer capability is active.

**Non-Goals:**

- No zip upload/import endpoint in the first version.
- No online marketplace or remote Skill installation.
- No editing `SKILL.md` from WebUI.
- No `EvaRuntime` ownership refactor.
- No direct-runtime Skills execution or standalone `skill_call` hosting.
- No implicit enabling of the system engineer tool when a Skill is enabled.

## Decisions

### Use bridge-backed SkillManager as the first-version authority

The WebUI Skills API will route management operations to the main EVA bridge when available. The Qt process will expose `SkillManager` state and operations through new bridge commands.

Alternatives considered:

- **Duplicate Skill scanning in `AcpRuntime`**: rejected because it would split enabled state and removal behavior from Qt UI, especially while execution still depends on the main process.
- **Move `SkillManager` into `EvaRuntime` now**: rejected as too broad for this change and likely to conflict with the larger runtime decoupling work.

### Add a dedicated HTTP Skills API

ACP HTTP should expose `/api/runtime/skills` separately from `/api/runtime/tools`. `GET` lists Skills; `POST` performs management actions such as `set_enabled`, `remove`, and `refresh`.

Alternatives considered:

- **Overload `/api/runtime/tools`**: rejected because tools and Skills have different lifecycle and UX semantics.
- **Expose multiple narrow endpoints immediately**: acceptable, but a single action endpoint is simpler for the existing lightweight HTTP router and first-version scope.

### Keep upload/import out of scope

The first version manages already installed Skills only. Browser zip upload requires request size limits, multipart/base64 handling, validation, and safe extraction flows; those are better handled in a follow-up change.

### WebUI gets its own Skills tab/area

The drawer/control panel should add a distinct Skills surface instead of listing Skills under the existing Tools section. The Tools section remains for execution capabilities such as engineer, knowledge, MCP, controller, Stable Diffusion, and calculator.

When engineer is disabled, the Skills surface should still allow management but must tell users that enabled Skills will only be injected/usable after enabling the system engineer capability.

### No implicit engineer toggling

Enabling a Skill must not automatically enable the engineer capability. WebUI can offer a clear button or hint to enable engineer via the existing tool toggle, but changing Skills must not silently alter tool execution permissions.

## Risks / Trade-offs

- **Bridge unavailable** → Skills API returns a clear unavailable/error state; WebUI shows read-only/unavailable messaging instead of attempting direct runtime management.
- **State drift between WebUI and Qt UI** → all operations call `SkillManager` in the main process and refresh state after changes.
- **Deleting a Skill is destructive** → WebUI should use an explicit confirmation before calling remove.
- **Conversation prompt already built before a toggle** → UI copy should indicate Skills affect subsequent prompt rebuild/reset behavior; implementation should reuse existing `skillsChanged`/`rebuildSkillPrompts` hooks.
- **HTTP action endpoint can become too broad** → limit accepted `op` values to the first-version actions and reject unknown operations.
