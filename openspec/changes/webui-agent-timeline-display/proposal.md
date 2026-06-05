## Why

当前 WebUI 已经接收推理内容、回复内容和运行层工具事件，但展示时仍按“思考块、工具摘要、回复正文”的固定区域堆叠，用户很难看清一次智能体回合里“先思考、再输出、再调用工具、再继续输出”的真实顺序。

本变更要把 WebUI 对话升级为按事件发生顺序呈现的智能体时间线，让工具调用可以折叠查看参数、stdout/stderr、结果 envelope 和产物信息，提升调试与日常使用的直观性。

## What Changes

- WebUI assistant 消息新增按流式到达顺序构建的时间线段，支持 `thinking`、`answer`、`tool_call`、`runtime_event`、`artifact`、`error` 等段落类型。
- 思考内容不再只能作为整块显示在回复顶部，而是按出现位置分段显示；每段默认可折叠，流式生成时当前思考段保持可见。
- 回复输出按实际发生顺序分段渲染，支持一次 assistant 回合中出现多段回复文本。
- 工具调用改为可展开条目，折叠态显示工具名、状态、摘要和成功/失败统计；展开态显示调用参数、执行目录、stdout/stderr、返回 envelope、错误恢复建议和产物路径。
- 前端保留 `tool_output` 事件，不再在 `store.ts` 中直接丢弃；同时避免无限保存大输出，长 stdout/stderr 需要做前端裁剪或折叠。
- 保持 `/v1/chat/completions` 的 OpenAI 兼容响应主体不破坏；如需增强，只通过 EVA 专用 `delta.eva_event` / `delta.eva_tool` 字段补充信息。
- 不改变工具执行逻辑、模型推理逻辑、知识库/MCP 行为和 Qt Widget 原生对话窗口渲染。

## Capabilities

### New Capabilities

- `webui-agent-turn-timeline`: 定义 WebUI 在一次 assistant 回合中按事件顺序展示思考、回复、工具调用、工具输出、产物和错误信息的行为契约。

### Modified Capabilities

无。

## Impact

- 影响代码：
  - `webui/src/types.ts`
  - `webui/src/api.ts`
  - `webui/src/store.ts`
  - `webui/src/components/MessageItem.vue`
  - `webui/src/components/ChatThread.vue`
  - `webui/src/styles.css`
  - 可能涉及 `src/acp_http_server.cpp`、`src/acp_runtime.cpp` 的 EVA 专用流式事件补充，但不改变标准 OpenAI 字段语义。
- 影响接口：
  - `POST /v1/chat/completions` streaming 中现有 `delta.reasoning`、`delta.content`、`delta.eva_event`、`delta.eva_tool` 的前端解释方式。
- 影响数据：
  - 浏览器本地会话结构需要兼容旧的 `content/reasoning/toolSteps/runtimeEvents` 记录，并逐步迁移或即时派生为 timeline。
- 依赖：
  - 继续使用现有 Vue/TypeScript/Vite/markdown-it/highlight.js 栈，不新增状态管理库。
