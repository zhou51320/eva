## Why

桥接模式下,eva_acp 连接的是正在运行的主程序,只要主程序里勾选了知识库/MCP/工具,网页发消息时这一轮推理就已经在主程序内部真实执行了 RAG/MCP/工具调用。但目前**这些能力只能在 Qt 窗口里勾选**——网页只能读取启用状态、无法开关。要让 WebUI 成为真正能替代 Qt 前端的交互入口(项目终极方向:运行层驱动、多前端并列),必须把"能力开关"也通过桥接暴露给网页。

## What Changes

- 主程序新增桥接命令 `bridge_set_capabilities`:接收要开关的工具键(calculator/engineer/mcp/knowledge/controller/stablediffusion),复用既有的 `tool_change()` + `set_date()` 生产路径应用(自动重算 `is_load_tool`、`create_extra_prompt`、合并系统提示词、持久化、重置上下文),返回最新 bridge state。
- `AcpBridgeClient` 新增 `setCapabilities()`。
- eva_acp 新增 `POST /api/runtime/tools`:桥接模式转发到 `bridge_set_capabilities`;direct/降级模式明确门控返回"需主程序桥接"。
- WebUI"工具与能力"面板:桥接(`full_eva_stack`)时工具状态变为**可交互开关**,调用新端点;非桥接保持只读 + 门控说明。
- 不在 web 层重新实现工具/知识/MCP 执行;执行仍由主程序拥有,本变更只把"开关控制权"交给网页。

## Capabilities

### New Capabilities

- `acp-web-bridge-capabilities`: 定义网页通过 ACP 桥接查询/开关主程序工具与扩展能力(知识库、MCP、系统工程师、机体控制、视觉、计算器)的行为契约,以及桥接可用/不可用时的门控规则。

### Modified Capabilities

无(扩展能力,不改既有请求/状态契约)。

## Impact

- 影响代码:
  - `src/widget/widget.h`、`src/widget/widget_link.cpp`(新增 `applyBridgeCapabilities` + `bridge_set_capabilities` 分发)
  - `src/acp_bridge_client.{h,cpp}`(`setCapabilities`)
  - `src/acp_runtime.{h,cpp}`(`setCapabilities` + direct 门控)
  - `src/acp_http_server.cpp`(`POST /api/runtime/tools`)
  - `webui/`(工具面板可交互开关)+ `resource/acp_web/index.html`(构建产物)
- 影响接口:
  - 新增 `POST /api/runtime/tools`;新增桥接命令 `bridge_set_capabilities`。
  - 其余接口不变。
- 约束:C++17 + Qt5.15;沿用"桥接命令驱动主程序 UI"的既有模式;无新增第三方依赖。
