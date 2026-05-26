## Why

当前 ACP 网页控制台已经具备“浏览器 WebUI -> eva_acp HTTP 适配层 -> EVA 主程序 ControlChannel 桥接”的雏形，但职责边界还不够清晰：WebUI、ACP 适配层和 EVA 主程序都在局部维护状态，容易出现装载状态、模型列表、会话记录和流式输出不一致的问题。

这次变更要把 ACP 网页控制台梳理成 EVA 的远程/本地控制入口：网页只负责交互，ACP 负责 HTTP/OpenAI 兼容与桥接协议转换，EVA 主程序作为最终状态源与执行者。

## What Changes

- 明确 ACP 网页控制台的 MVP 范围：状态查看、模型列表、装载/链接、重置、文本对话、流式输出、基础错误展示。
- 统一三层职责边界：
  - WebUI 只保存浏览器侧会话草稿和展示状态，不作为 EVA 运行状态源。
  - `eva_acp` 提供稳定 HTTP API、静态资源服务、OpenAI 兼容代理和 ControlChannel 桥接适配。
  - EVA 主程序负责模型装载、链接模式切换、上下文重置、推理执行和真实运行状态广播。
- 梳理 ACP 桥接事件与命令协议，减少“轮询状态”和“流式事件”之间的信息断裂。
- 保留独立运行能力：当主 EVA 未启动或桥接不可用时，ACP 可以报告 degraded 状态，并在可行时回退到自身托管本地/远端后端。
- 为后续扩展预留边界：附件输入、知识库、MCP、工具流、视觉/音频和机体控制面板暂不进入首轮实现。

## Capabilities

### New Capabilities

- `acp-web-console`: 定义 ACP 网页控制台对状态查看、模型装载、链接模式、文本对话、流式输出、重置和错误反馈的行为契约。

### Modified Capabilities

无。

## Impact

- 影响代码：
  - `src/acp_main.cpp`
  - `src/acp_runtime.h`
  - `src/acp_runtime.cpp`
  - `src/acp_http_server.h`
  - `src/acp_http_server.cpp`
  - `src/acp_bridge_client.h`
  - `src/acp_bridge_client.cpp`
  - `src/net/controlchannel.h`
  - `src/net/controlchannel.cpp`
  - `src/widget/widget_link.cpp`
  - `resource/acp_web/index.html`
  - `resource/acp_web/app.js`
  - `resource/acp_web/styles.css`
- 影响接口：
  - `GET /health`
  - `GET /api/backend/state`
  - `POST /api/backend/load`
  - `POST /api/runtime/reset`
  - `GET /v1/models`
  - `POST /v1/chat/completions`
  - ACP bridge command/event JSON frames
- 依赖保持现状：C++17、Qt 5.15、浏览器原生 HTML/CSS/JS，不新增前端构建链路。
