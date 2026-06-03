## Context

`rework-acp-web-console` 已交付文本对话闭环,本次在其新前端基座(`webui/`,Vite + Vue3 + TS,编译为单个自包含 `resource/acp_web/index.html`)上扩展。

当前后端现实(决定本设计的硬约束):

- eva_acp HTTP 路由只有 8 个:`/health`、`/v1/models`、`/v1/chat/completions`、`/api/backend/state`、`/api/runtime/{reset,stop}`、`/api/backend/load`、`/`。无 knowledge/mcp/tools 执行端点。
- `AcpRuntime` 只暴露 chat/load/reset/stop/state;知识库、MCP、工具执行、机体控制等由主程序拥有,仅在 `full_eva_stack`(桥接)时完整可用。
- `/api/backend/state` 的 `capabilities` 已经携带各工具的 configured/enabled/执行路径与 TTS 状态等真实信息。
- `/v1/chat/completions` 是 OpenAI 兼容入口,采样参数与多模态 `content` 都是标准请求字段。

约束:C++17 + Qt5.15;WebUI 无构建后不可引 CDN,build target 降级保证 Win7 浏览器可用,产物为单文件。

## Goals / Non-Goals

**Goals:**

- 让每个侧栏入口都成为可打开的真实面板,消除所有 disabled 占位项。
- 把"用前端就能驱动的真功能"做实:采样参数 + 系统提示词、图片附件。
- 把"主程序拥有的能力"如实呈现并门控,不伪造控件。
- 维持单文件 / Win7 / 离线约束。

**Non-Goals:**

- 不在 web 层重新实现知识库检索、MCP 调用、工具执行、机体控制、视觉推理。
- 不新增 knowledge/mcp/tools 执行类 HTTP 端点。
- 不引入多用户、公网访问、认证。
- 不改主程序五按钮与窗口交互。

## Decisions

### Decision 1: 把"未接入面板"统一为能力/工具的真实呈现,而非各自造功能

知识库、MCP、技能、系统工程师(engineer)、机体控制(controller)、视觉(stablediffusion)在后端都是 `capabilities` 里的工具条目。本设计用一个**工具与能力面板**读取 `/api/backend/state` 真实展示其 configured/enabled/执行路径,并对不可用项门控说明。

理由:与既有架构决策一致(主 EVA 拥有这些能力,ACP 不复制状态机);无需新后端端点即可让所有面板"可用且真实"。

备选:为每个面板新增执行端点并接管运行时——范围过大、与 `decouple-eva-runtime` 重叠、首版易半成品,弃用。

### Decision 2: 采样参数与系统提示词走标准 OpenAI 请求字段

设置面板把 temperature/top_p/top_k/max_tokens 注入 `/v1/chat/completions` 请求体,系统提示词作为前置 `system` 消息。需要确认 eva_acp 在直连/桥接路径会转发这些字段;若未转发,则在 `acp_runtime.cpp`/`acp_http_server.cpp` 补最小透传(只透传显式提供的字段,缺省不覆盖运行层默认值)。

理由:这是 OpenAI 兼容的标准做法,前端改动为主,后端改动最小且不破坏现有契约。

备选:新增 `/api/runtime/settings` 端点持久化到运行层——更重,且与"link 模式每请求生效"语义不一致,首版不做。

### Decision 3: 图片附件用 OpenAI 多模态 content 数组

有附件时 user 消息 `content` 变为 `[{type:text},{type:image_url,image_url:{url:dataURL}}]`,无附件时退回字符串。图片读为 data URL,纯前端,不落盘。

理由:标准格式,面向视觉模型/链接模式即可工作;对纯文本路径零影响(退回字符串)。

权衡:直连/桥接若不支持多模态则图片可能被忽略——UI 标注"需视觉模型/链接模式",不阻断纯文本。

### Decision 4: 面板布局——左侧栏分组 + 主区切换 + 运行时抽屉保留

侧栏分"对话 / 设置 / 工具与能力 / 连接状态"等真实分组;装载/模型/原始状态保留在右侧运行时抽屉。所有面板状态由能力字段驱动渲染。

理由:延续首版交互,改动聚焦在"填实占位",降低回归风险。

## Risks / Trade-offs

- [Risk] 采样字段在某条聊天路径未被转发 → Mitigation: 实现前用 curl 验证转发行为,必要时加最小透传并冒烟。
- [Risk] 多模态在 direct/bridge 被忽略,用户以为生效 → Mitigation: 附件区显式标注适用范围;纯文本路径不受影响。
- [Risk] 能力字段命名/结构随后端演进变化 → Mitigation: 前端对 capabilities 做防御式读取(缺字段降级为"未知/未配置")。
- [Risk] 单文件体积随面板增长变大 → Mitigation: 复用已引入的 markdown/highlight,不新增重型依赖;体积仍在本地服务可接受范围。

## Migration Plan

1. 在 `webui/` 增量加面板与附件,`npm run build` 刷新 `resource/acp_web/index.html`。
2. 若采样透传需要后端改动,改 `acp_runtime.cpp`/`acp_http_server.cpp` 并重建 `eva_acp`。
3. 冒烟:构建产物自包含校验 + `scripts/smoke_acp_runtime.py` + 手动接口验证采样字段透传。
4. 回滚:产物与后端改动相互独立;前端可单独回退到首版 index.html,后端透传改动为纯增量可单独还原。

## Open Questions

无(实现期遇到采样字段不透传等具体问题,按 Decision 2 就地处理)。
