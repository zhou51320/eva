## Why

EVA 已经具备模型对话、工具调用、Skills、ACP/WebUI、Qt UI 和运行层解耦雏形，但当前能力仍更像“模型 + 工具列表”：模型需要自行理解提示词、选择工具、拼接 shell、处理 Windows/Win7 路径、安装依赖、复制 Skill 资产、判断失败原因并确认产物。同一模型在 OpenCode、Claude Code、Codex、Pi Agent 等成熟 agent 产品中表现更稳，核心差距不是模型本身，而是缺少足够工程化的 agent runtime / harness。

本 change 的目标不再是修补 Skills，而是把 EVA 升级为面向 Windows/Win7 可用的本地 Agent Runtime 2.0：让模型通过更可靠的 prompt protocol、tool protocol、execution runtime、workspace/artifact 管理、recovery engine、progress events 和 Skill plugin runtime 完成任务，并让生成类、代码类和文件类任务有可验证闭环。

## What Changes

- 将 EVA agent 系统提示词从“人格/能力描述”升级为“执行协议”：明确任务循环、工具使用原则、失败恢复、产物确认、完成标准和 Win7/Windows 优先兼容策略。
- 引入 Tool Result Envelope v2：工具返回统一的 `ok`、`summary`、`data`、`artifacts`、`warnings`、`error`、`recovery_hints`，让模型能基于结构化 observation 恢复。
- 升级 command runner / execution runtime：命令执行显式包含 cwd、env、timeout、shell、取消、stdout/stderr/exit_code、长任务进度、重复失败检测和 Windows/Win7 兼容处理。
- 建立 Workspace + Artifact Manager：统一 allowed roots、路径规范化、stat/list/glob/copy、临时 run 目录、artifact confirm、open/download/show 事件。
- 建立 Recovery Engine MVP：按 path、dependency、syntax、permission、timeout、network、artifact_missing、unsupported_platform 等错误类型提供恢复提示并阻止无意义重复。
- 将 Skill Runtime 降级为 Agent Runtime 的插件层：`skill_call` 负责发现/说明/manifest，`skill_run` 或等价内部路径负责 run dir、资产 staging、依赖检查、entrypoint 执行和 artifact 返回。
- 建立 Progress Event Bus：Qt UI、ACP WebUI、bridge/direct runtime 共享 `task_started`、`plan_created`、`tool_started`、`recovering`、`artifact_ready`、`task_completed` 等事件。
- 将 Win7 Compatibility Pack 作为一级目标：命令 shell、编码、路径、TLS/proxy、portable Node/Python、依赖诊断和老系统 fallback 从设计阶段纳入。

## Capabilities

### New Capabilities

- `agent-runtime`: EVA provides an explicit task execution protocol with verification gates, structured tool usage, standardized recovery, and final answer constraints.
- `tool-runtime`: EVA tools return structured envelopes and recovery hints, with command execution upgraded to a robust runtime primitive.
- `workspace-artifacts`: EVA manages allowed roots, path normalization, generated artifacts, and artifact-ready events.
- `skill-runtime`: EVA treats Skills as runtime plugins with optional manifests, entrypoints, isolated run directories, and structured outputs.
- `win7-compat`: EVA provides Windows/Win7-aware command, path, dependency, encoding, proxy, and runtime compatibility behavior.

### Modified Capabilities

None.

## Impact

- 影响代码：
  - 系统提示词与 agent loop：`src/prompt*`、engineer mode、tool prompt 注入、ACP/direct runtime prompt 构建。
  - 工具协议与执行：`execute_command`、文件读写/搜索、PTC、tool router、tool response serialization。
  - Runtime/controller：`src/runtime/*`、`src/acp_runtime.*`、bridge/direct runtime 状态与事件。
  - Workspace/artifact：文件路径处理、allowed roots、artifact metadata、WebUI/Qt 打开/下载/定位入口。
  - Skills：`src/skill/*`、Skill metadata parsing、`skill_call`、可选 `skill_run`、ACP/WebUI Skills API。
  - UI：Qt Widget 状态区、ACP WebUI 工具/Skill 进度、artifact cards。
- 影响接口：新增或扩展内部工具 schema、统一 tool result envelope、progress event schema、Skill manifest/entrypoint schema。
- 兼容策略：保留旧工具和旧 `skill_call` 路径；新 runtime 能力按阶段启用；旧 Skills 不强制迁移。
- 风险：范围大，必须分阶段落地，先建立 prompt/tool/command/artifact 最小闭环，再扩展 Skill runtime 和 Win7 compatibility pack。
