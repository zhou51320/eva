## Context

当前 WebUI 的消息模型是 `ChatMessage.content`、`reasoning`、`toolSteps`、`runtimeEvents` 几组并列字段。`api.ts` 已能从 SSE 中识别 `delta.content`、`delta.reasoning`、`delta.reasoning_content`、`delta.eva_tool` 和 `delta.eva_event`；`store.ts` 将 content/reasoning 汇总到 assistant 草稿，把 `eva_tool` 放入工具名数组，并且直接忽略 `tool_output` 事件。

后端 ACP 流式层已经把运行层事件以 `delta.eva_event` 传给浏览器。`RuntimeEvent` 中的 `tool_started`、`tool_output`、`tool_finished`、`artifact_ready`、`recovering`、`task_failed` 等事件包含工具名、摘要、命令、cwd、stdout/stderr、exit code、envelope、artifacts 和 recovery hints 等信息。现有问题主要在前端事件归档和渲染，不在推理或工具执行本身。

## Goals / Non-Goals

**Goals:**

- WebUI 按同一次 assistant 回合内的流式到达顺序展示思考、回复输出、工具调用、工具输出、产物和错误。
- 工具调用折叠态简洁，展开态能看到排查所需的关键细节。
- 保留旧会话数据可读性，并控制本地存储中的工具输出体积。
- 不破坏 OpenAI 兼容接口，对 EVA 私有增强字段保持向后兼容。

**Non-Goals:**

- 不重写工具执行、MCP、知识库或系统工程师能力。
- 不改变 Qt Widget 原生对话窗口的渲染。
- 不把浏览器本地会话变成 EVA 运行状态源。
- 不新增前端框架、状态管理库或后端长连接协议。

## Decisions

### Decision 1: 在前端引入 `ChatSegment` 时间线模型，同时保留旧字段

为 `ChatMessage` 增加可选 `segments?: ChatSegment[]`。段类型建议包括 `thinking`、`answer`、`tool_call`、`runtime_event`、`artifact`、`error`。旧的 `content`、`reasoning`、`toolSteps`、`runtimeEvents` 继续维护，用于发送历史、兼容旧 localStorage、复制回复和统计。

理由：时间线是展示结构，不应反向破坏已有 API 消费路径。并列保留可以小步迁移，避免一次性改动所有组件。

备选：只在 `MessageItem.vue` 中根据旧字段即时拼装。该方案无法表达“回复-工具-回复”这种中途插入顺序，也无法保留 `tool_output` 的具体归属，弃用。

### Decision 2: 由 `store.ts` 在流式回调中构建时间线

`api.ts` 增加面向事件片段的回调，例如 `onStreamPart({ kind, text, event })`，或在现有回调旁补充 typed part。`store.ts` 作为会话状态唯一写入点，按到达顺序追加或合并段：

- 连续相邻的 `reasoning` 合并到当前 `thinking` 段。
- 连续相邻的 `content` 合并到当前 `answer` 段。
- `tool_started` 创建或激活 `tool_call` 段。
- `tool_output` 附加到最近的同名未完成工具段，无法匹配时创建独立工具事件段。
- `tool_finished` 标记工具段状态并附加结果 envelope。
- `artifact_ready` 优先挂到最近同名工具段，否则作为独立 artifact 段。
- `recovering`、`task_failed`、`error` 等事件以醒目段显示，不再隐藏。

理由：`store.ts` 拿到的是完整流顺序，最适合建立 timeline；组件只负责纯渲染。

备选：让 `api.ts` 直接返回最终 timeline。这样会把会话状态和 UI 约束混进传输层，不利于后续 Widget/ACP 复用，弃用。

### Decision 3: 工具详情使用现有 `RuntimeEvent.payload`，必要时做小型后端补充

第一阶段优先消费现有事件字段：`tool_started.payload.command/cwd/tool_name`、`tool_output.payload.stream`、`tool_finished.payload.exit_code/interrupted/envelope`、`artifact_ready.payload.artifacts`、`recovering.payload.recovery_hints`。如果某些文本工具调用只有 `eva_tool` 名称而缺少参数，再在 ACP/运行层补充 EVA 私有 `eva_event` payload，但不改变标准 OpenAI 字段。

理由：现有运行层已经有较多结构化信息，先把这些信息展示出来，收益最大且风险最低。

备选：解析 assistant 文本中的 `<tool_call>` JSON 来恢复参数。该方式会受到分片、清洗和桥接路径影响，只能作为补充，不作为主数据源。

### Decision 4: MessageItem 优先渲染 timeline，旧消息走兼容视图

`MessageItem.vue` 若发现 `message.segments` 非空，则按段顺序渲染：

- `thinking` 使用轻量 `details`，生成中自动展开，完成后默认折叠。
- `answer` 使用现有 markdown 渲染和代码块复制能力。
- `tool_call` 使用工具条目组件或局部模板，折叠态显示工具名、状态、摘要、输出计数；展开态显示 JSON/命令/输出/结果。
- `artifact` 使用现有产物卡样式。
- `error` 使用错误色块和可复制详情。

旧消息没有 `segments` 时继续走现有 `reasoning + toolSteps/runtimeEvents + content` 布局，或在读取时派生一个兼容 timeline。

### Decision 5: 限制工具输出的本地保存体积

每个工具段应限制 stdout/stderr 保存量，例如按流分别保留尾部 32 KiB 或 300 行，并记录 `truncated` 标记和省略字节/行数。UI 展开时显示“已截断”提示。

理由：WebUI 当前把最多 50 个会话写入 localStorage；不限制工具输出会很快撑爆存储并拖慢渲染。

## Risks / Trade-offs

- [Risk] 缺少稳定 tool call id 时，多工具并发输出可能归属错误。Mitigation: 先按最近同名未完成工具归属；后端若能提供 call id，再无缝加入 `toolCallId` 匹配。
- [Risk] 时间线字段与旧字段双写可能出现不一致。Mitigation: `store.ts` 统一写入，测试覆盖 content/reasoning 汇总与 segments 追加。
- [Risk] 大量工具输出导致滚动和 localStorage 变慢。Mitigation: 输出裁剪、默认折叠、只对可见消息渲染完整详情。
- [Risk] 不同后端的 reasoning 字段名称不同。Mitigation: 继续沿用现有 `reasoning/reasoning_content` 归一逻辑。

## Migration Plan

1. 扩展 `types.ts` 的消息段类型，保留旧字段。
2. 调整 `api.ts` 和 `store.ts`，流式过程中同步维护旧字段与 `segments`。
3. 改造 `MessageItem.vue`，优先渲染 timeline，并保留旧消息 fallback。
4. 补充工具详情样式和输出裁剪逻辑。
5. 如现有事件字段不足，最小化补充 ACP `eva_event` payload。
6. 构建 WebUI，执行至少一组模拟流式单元/手动验证：思考-回复-工具-回复、多工具失败、长输出截断、旧会话显示。
7. 回滚时只需忽略 `segments` 渲染并恢复旧组件路径；旧字段仍在。

## Open Questions

- 是否需要在后端立即补充稳定 `tool_call_id`，还是先按工具名和活动段匹配。
- 工具输出裁剪阈值使用固定值，还是写入 `xconfig.h` 或 WebUI 设置项。
