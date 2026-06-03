## Why

ACP/WebUI 目前只能开关 tools/knowledge/MCP，无法独立查看和管理已安装 Skills；而 Qt UI 已经具备基于 `SkillManager` 的安装、启用和删除能力。随着 EVA 逐步将 Qt UI 与 ACP/WebUI 变成并列前端，Skills 需要从“系统工程师工具下的隐藏资源”提升为 Web 控制台中的独立管理面。

## What Changes

- 在 ACP/WebUI 中新增独立 Skills 管理能力，用于查看已安装 Skills、启用/禁用、删除和刷新列表。
- 通过主程序 bridge 复用现有 `SkillManager` 作为权威数据源，保持 Qt UI 与 WebUI 的 Skills 状态一致。
- 在 ACP HTTP API 中新增 Skills 管理端点，供 WebUI 调用。
- WebUI 控制台新增 Skills 页签或等价独立区域，明确区分：
  - Tools：系统工程师、知识库、MCP 等执行能力开关。
  - Skills：系统工程师能力下可挂载的技能包资源。
- 第一版不支持 zip 上传导入、不实现在线市场、不重构 `EvaRuntime`、不让 direct runtime 独立执行 Skills。
- 第一版不隐式启用系统工程师；当 engineer 未启用时，WebUI 只提示 Skills 需要 engineer 才会注入和被调用。

## Capabilities

### New Capabilities
- `acp-web-skills-management`: ACP/WebUI can list, enable/disable, remove, and refresh installed Skills through the main EVA bridge.

### Modified Capabilities

None.

## Impact

- ACP HTTP server: add Skills routes under `/api/runtime/skills`.
- ACP runtime/bridge client: add calls that forward Skills management requests to the main EVA process when bridge mode is available.
- Qt Widget bridge command handling: expose current `SkillManager` state and operations through bridge commands.
- WebUI API/store/types: add Skills types and actions.
- WebUI Runtime drawer/control panel: add a dedicated Skills management surface without overwriting unrelated current UI changes.
- No new external dependencies are expected.
