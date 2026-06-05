## Context

EVA 当前已经有模型后端、工具 schema、`skill_call`、Skills 管理、ACP/WebUI、Qt UI 和 `EvaRuntime` 雏形，但执行任务时仍大量依赖模型自由发挥：模型要自行判断下一步、拼接 shell、处理路径/编码/依赖、复制资产、分类错误、寻找产物并决定是否完成。这导致 EVA 在复杂文件生成、代码修改、Skill 执行、Windows 路径和依赖失败场景下不如成熟 agent harness 稳定。

本设计将 EVA 从“模型 + 工具集合”升级为“本地 agent operating system”：模型负责推理和决策，runtime 负责工程化执行、状态约束、结构化 observation、失败恢复、产物确认和 UI 反馈。Skills 只是 runtime 的插件能力之一，不再支配整体设计。

## Goals / Non-Goals

**Goals:**

- 建立 Agent Prompt Protocol：明确任务执行循环、工具策略、失败恢复、完成 gate、artifact verification 和 Windows/Win7 优先兼容。
- 建立 Tool Result Envelope v2：所有重点工具返回结构化成功/失败、artifact metadata、error type 和 recovery hints。
- 升级 Execution Runtime：命令执行具备 cwd/env/timeout/shell/cancel/stdout/stderr/exit_code/progress/retry detection，并适配 Windows/Win7 差异。
- 建立 Workspace + Artifact Manager：统一 allowed roots、路径规范化、文件操作、临时 run 目录、artifact confirm 和 UI artifact-ready 事件。
- 建立 Recovery Engine MVP：对常见失败分类并提供可执行恢复策略，避免重复相同失败动作。
- 将 Skill Runtime 设计为插件层：兼容旧 `SKILL.md`，支持可选 manifest/entrypoints、run dir、资产 staging、依赖检查、执行和 artifact 输出。
- 建立 Progress Event Bus：Qt UI 和 ACP/WebUI 能展示任务阶段、工具执行、恢复、Skill 使用和产物就绪。
- 将 Win7 Compatibility Pack 纳入一级设计：shell、编码、路径、TLS/proxy、portable runtime 和依赖诊断都不能作为后期补丁。

**Non-Goals:**

- 不一次性重写所有工具、所有 UI 或所有 Skills。
- 不克隆某个具体产品；只吸收 OpenCode/Claude Code/Codex/Pi Agent 等成熟 agent harness 的工程化模式。
- 不在第一阶段引入远程多用户、公网任务执行或复杂权限认证系统。
- 不要求现有 Skills 立即迁移到 manifest；旧 Skills 必须继续可用。
- 不把 shell 完全禁用；shell 保留为 escape hatch，但 routine 文件/路径/artifact 操作优先结构化工具。

## Architecture

目标结构：

```text
EVA UI / API
  -> TaskController
  -> Agent Prompt Protocol
  -> ModelAdapter
  -> ToolRouter
  -> Execution Runtime
  -> Workspace / Artifact Manager
  -> Recovery Engine
  -> Skill Runtime Plugin
  -> Progress Event Bus
```

关键原则：

- TaskController 控制任务状态和 final answer gate。
- ModelAdapter 统一 native tool call、XML/JSON fallback 和 legacy parser。
- ToolRouter 内部统一成 `ToolInvocation { name, args, call_id }`。
- ToolResult 使用 envelope v2，使模型看到稳定 observation。
- Recovery Engine 不替模型做所有决策，但提供分类、阻止重复失败并生成 recovery hints。
- Artifact Manager 是生成类任务完成的依据，而不是模型文本。
- Progress Event Bus 面向 Qt/WebUI/bridge/direct runtime 中立。

## Decisions

### Decision 1: Runtime-first, Skill-second

Skills 暴露了 EVA runtime 弱的问题，但不应成为架构中心。实现顺序是：prompt protocol → tool envelope → command runner → workspace/artifact → recovery → skill runtime → progress/UI → Win7 pack 深化。

Alternatives considered:

- **先做重型 Skill runner**：会继续建立在脆弱命令、路径和产物确认之上。
- **只修 prompt**：能减少绕路，但无法解决 Windows/Win7 命令、依赖、artifact 等工程问题。

### Decision 2: Final answer must pass verification gates

生成文件、修改代码、运行命令类任务在最终答复前必须拥有 observation：artifact exists、测试/构建结果、或明确的未验证说明。没有 observation 时不能说“已完成”。

Alternatives considered:

- **只靠 prompt 建议验证**：模型仍可能过早结束。
- **所有任务强制测试**：过度；信息查询或纯设计讨论不需要工具验证。

### Decision 3: Tool Result Envelope v2 is additive

先为高频工具提供 envelope v2 或 wrapper，不强制一次性迁移所有工具。旧工具返回可被 adapter 包装成 envelope。

Envelope:

```json
{
  "ok": true,
  "summary": "Created report.pptx",
  "data": {},
  "artifacts": [],
  "warnings": [],
  "error": null,
  "recovery_hints": []
}
```

Failure:

```json
{
  "ok": false,
  "summary": "Command failed: npm not found",
  "data": {},
  "artifacts": [],
  "warnings": [],
  "error": { "type": "dependency_missing", "message": "npm not found", "details": {} },
  "recovery_hints": [ { "action": "check_runtime", "args": { "name": "node" } } ]
}
```

### Decision 4: Command runner becomes an execution primitive, not a text shortcut

`execute_command` should expose cwd、env、timeout、shell、expected outputs、cancel/progress and structured result. On Windows/Win7, it must avoid assuming modern PowerShell/bash behavior.

Alternatives considered:

- **继续让模型拼完整 shell**：灵活但不稳定，尤其中文路径、空格、cmd/PowerShell 差异。
- **完全禁止命令执行**：不现实；开发任务、构建、脚本型 Skill 都依赖命令。

### Decision 5: Workspace and artifact management are separate from Skills

路径、allowed roots、stat/copy/glob、artifact confirm、temp run dirs 是 runtime 通用能力。Skill runtime 应复用这些能力，而不是自建一套。

### Decision 6: Recovery Engine provides hints and guardrails first

第一版 Recovery Engine 不需要自动执行所有修复；它应先分类错误、生成恢复建议、标记重复失败，并在 prompt/tool observation 中引导模型改变策略。

### Decision 7: Skill Runtime remains backward compatible

新 Skill 可声明 manifest/entrypoints/dependencies/outputs；旧 Skill 仍通过 `SKILL.md` 使用。`skill_call` 返回 root、tree、instructions、entrypoint hints。`skill_run` 可作为可选增强，不可破坏旧路径。

### Decision 8: Win7 compatibility is a product differentiator

Win7 兼容不是末尾补丁。runtime 要显式处理：旧 PowerShell、cmd fallback、Unicode 输出、路径分隔符/空格/中文、TLS/proxy、老 Node/Python 版本、VC runtime 缺失和 portable helper binaries。

## Risks / Trade-offs

- [Risk] 范围过大 → Mitigation: 分阶段落地，每阶段有单独验证和兼容开关。
- [Risk] Envelope v2 与旧工具并存增加复杂度 → Mitigation: 用 adapter 包装旧返回，先迁移高频工具。
- [Risk] Final answer gate 过严导致模型卡住 → Mitigation: 允许明确报告“未验证/被阻塞”，但不能假装完成。
- [Risk] 自动恢复可能误操作 → Mitigation: destructive/shared-state/network-heavy 操作仍需权限或用户确认。
- [Risk] Win7 runtime 依赖难维护 → Mitigation: 先做诊断和 fallback 策略，再决定是否 bundle portable runtime。

## Migration Plan

1. Prompt Protocol MVP：更新系统提示词，明确执行循环、工具优先级、失败恢复、artifact verification、final answer gate。
2. Tool Envelope Adapter：为现有 tool result 增加 envelope wrapper，并先覆盖 command/file/skill/artifact 相关工具。
3. Command Runner v2：增加 cwd/env/timeout/shell/stdout/stderr/exit_code/cancel/progress/error type/recovery hints。
4. Workspace/Artifact MVP：实现 stat/list/glob/copy/artifact_confirm 和 allowed roots；生成任务必须确认 artifact。
5. Recovery MVP：分类 path/dependency/syntax/permission/timeout/network/artifact_missing，阻止重复失败并提示恢复。
6. Skill Plugin Runtime：扩展 `skill_call` metadata，支持 manifest/entrypoints；实现最小 `skill_run` run dir + staging + command entrypoint。
7. Progress Events：接入 Qt UI、ACP WebUI、bridge/direct runtime，显示任务阶段、工具执行、恢复和 artifact-ready。
8. Win7 Compatibility Pack：完善 Windows/Win7 command/path/encoding/proxy/runtime diagnostics；必要时引入 portable runtime。
9. Verification：用 PPTX Skill、代码修改、依赖缺失、路径失败和 artifact missing 场景做回归。

Rollback strategy:

- 保留旧 prompt/tool/skill_call 路径。
- Envelope v2、final answer gate、skill_run、progress UI 均可 feature flag 关闭。
- 结构化工具失败时可回退 shell，但必须保留 observation 和未验证说明。

## Open Questions

- Skill manifest 首选 `SKILL.md` frontmatter、`skill.yaml`，还是两者都支持？
- `skill_run` 是否作为模型可见工具暴露，还是先作为 runtime 内部动作由 `skill_call`/ToolRouter 间接触发？
- Artifact Manager 是否需要跨会话持久 artifact registry，还是第一版只在当前会话显示？
- Win7 portable Node/Python 是否纳入安装包，还是先提供诊断和用户自带 runtime 支持？
- Final answer gate 的严格程度是否按任务类型配置？
