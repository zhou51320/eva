## Why

ACP 网页控制台首版已具备稳定的文本对话闭环,但侧栏仍有一排 disabled 占位项(知识库、MCP、技能、系统工程师、视觉/音频、机体控制),以及没有暴露给用户的生成参数和附件输入。这些"未接入面板"让控制台看起来功能残缺。本次变更要在不伪造后端能力的前提下,把控制台补齐成"第一版全部功能可用"的状态:每个面板都打开即用,真实反映运行层能力。

## What Changes

- 新增**生成设置面板**:温度、top_p、top_k、最大生成 tokens、系统提示词等,作为标准 OpenAI 字段注入 `/v1/chat/completions` 请求;并补充 eva_acp 在直连/桥接路径下转发这些采样字段的能力(若当前未转发)。
- 新增**工具与能力面板**:从 `/api/backend/state` 的 `capabilities` 实时展示 calculator / engineer(系统工程师)/ controller(机体控制)/ knowledge(知识库)/ mcp / stablediffusion(视觉)等工具的"已配置 / 已启用 / 执行路径"状态,统一替代原先一排死占位项。
- 新增**图片附件**:输入区支持附加图片,按 OpenAI 多模态 `content` 数组(`image_url` data URL)发送,面向视觉模型/链接模式。
- 新增**能力与连接面板**:清晰区分 direct-runtime / bridge / degraded,展示 `full_eva_stack`、会话归属、输入模式、TTS 状态,并对"需主程序桥接才可用"的能力给出明确说明,而非提供假控件。
- **移除**侧栏所有 disabled 占位按钮,改为可打开的真实面板;能力不可用时面板内显式门控并说明原因。
- 不在 web 层重新实现知识库/MCP/工具执行/机体控制等主程序拥有的能力——这些仍由主 EVA 通过桥接提供,本变更只负责真实呈现与门控。

## Capabilities

### New Capabilities

- `acp-web-panels`: 定义 ACP 网页控制台扩展面板的行为契约——生成设置(采样参数与系统提示词)、工具与能力的真实状态展示与门控、图片附件输入、direct/bridge/degraded 能力呈现,以及"无死占位、能力驱动"的面板规则。

### Modified Capabilities

无(`rework-acp-web-console` 引入的 `acp-web-console` 能力尚未 archive 进 `openspec/specs/`,本变更以新增面板能力的方式扩展,不改其既有请求/状态契约)。

## Impact

- 影响代码:
  - `webui/`(Vite + Vue3 源码:新增 Settings / Tools / Capabilities 面板、附件输入,改造侧栏与抽屉)
  - `resource/acp_web/index.html`(构建产物,随 `npm run build` 更新)
  - `src/acp_http_server.cpp`、`src/acp_runtime.cpp`(仅在采样字段未被转发时,补充将 OpenAI 采样参数透传到聊天路径)
- 影响接口:
  - `POST /v1/chat/completions`:请求体新增可选采样字段(temperature/top_p/top_k/max_tokens)与前置 system message;响应契约不变。
  - 其余 HTTP 接口保持不变;不新增 knowledge/mcp/tools 执行端点。
- 约束保持:C++17 + Qt 5.15;WebUI 经 Vite 构建为单个自包含 `index.html`,build target 降级保证 Win7 浏览器可用,无 CDN、离线可用。
