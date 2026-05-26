## Why

EVA 最初目标是让 UI 可以替换，而模型装载、对话、工具、知识库、MCP、语音、文生图等能力保持为稳定后端能力。当前实现虽然已有 `AppBootstrap`、`NetClient`、`BackendCoordinator`、`ToolExecutor` 等分层雏形，但 `Widget` 和 `main.cpp` 仍然承担运行时中枢职责，导致新的 WebUI 只能通过 ACP 中间层控制现有窗口，而不能直接使用一套无窗口运行层。

这次变更要把 EVA 从“窗口驱动应用”演进为“运行时驱动应用”：先建立无 UI 的 `EvaRuntime` 能力核心，再让 Qt Widget、ACP/WebUI 和后续其他 UI 都作为前端接入同一运行层。

## What Changes

- 新增 `EvaRuntime` 作为无窗口运行层入口，负责装载、链接、重置、发送、停止、状态快照和事件输出。
- 将当前 `main.cpp` 中对象创建、线程装配、配置加载和信号连接整理为可复用的 runtime bootstrap。
- 将 `Widget` 中与 UI 无关的状态和流程逐步迁移到 runtime/controller：
  - 会话状态与消息数组
  - 后端生命周期
  - 网络请求与 SSE 输出
  - 工具调用回环
  - 运行状态机
  - 配置读写和默认模型发现
- 让 Qt Widget 前端只做显示、输入采集和用户交互。
- 让 ACP 服务直接持有 `EvaRuntime`，在无主窗口场景下也能启动完整 EVA 能力，并继续暴露 HTTP/OpenAI 兼容接口。
- 保留现有窗口版启动方式，迁移期间通过兼容层保证功能逐步搬迁。

## Capabilities

### New Capabilities

- `eva-runtime`: 定义 EVA 无窗口运行层的状态、命令、事件、生命周期和前端接入契约。

### Modified Capabilities

- `acp-web-console`: ACP 网页控制台从“控制主 EVA 窗口的中间层”演进为“可直接接入 EvaRuntime 的 Web 前端入口”。

## Impact

- 影响架构：
  - EVA 从 Widget 中枢转为 Runtime 中枢。
  - Qt Widget 和 WebUI 变成并列前端。
  - ACP 后期可无窗口启动完整 EVA 能力。
- 影响代码：
  - `src/main.cpp`
  - `src/acp_main.cpp`
  - `src/acp_runtime.*`
  - `src/widget/*`
  - `src/core/session/*`
  - `src/core/toolflow/*`
  - `src/service/net/*`
  - `src/service/backend/*`
  - `src/service/tools/*`
  - `src/expend/*`
  - `src/storage/*`
- 依赖保持现状：C++17、Qt 5.15、无新增前端构建链路。
- 风险较高，需要分阶段迁移，每阶段保持可编译、可运行、可回滚。
