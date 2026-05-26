## Context

当前实现已经形成三层结构：

```txt
Browser WebUI
  | HTTP + SSE
  v
eva_acp
  | framed JSON over localhost TCP
  v
EVA main Widget / ControlChannel
```

相关代码已经具备基础能力：

- `resource/acp_web/*` 提供无构建链路的网页控制台。
- `src/acp_http_server.cpp` 提供静态资源、`/health`、`/api/backend/*`、`/api/runtime/reset`、`/v1/models`、`/v1/chat/completions`。
- `src/acp_runtime.cpp` 在桥接模式下优先调用 `AcpBridgeClient`，否则可以直接托管本地/远端 OpenAI 兼容后端。
- `src/acp_bridge_client.cpp` 通过 `ControlChannel` 连接主 EVA 的 `DEFAULT_ACP_BRIDGE_PORT`。
- `src/widget/widget_link.cpp` 已处理 `bridge_get_state`、`bridge_list_models`、`bridge_apply_load`、`bridge_reset`、`bridge_send`，并广播 snapshot/output/ui_state/state_log 等事件。

主要约束：

- 项目继续使用 C++17 和 Qt 5.15。
- WebUI 保持浏览器原生 HTML/CSS/JS，不引入前端构建工具。
- ACP 只绑定本机默认地址，作为本地控制台入口；如未来开放局域网访问，需要另行加入认证和权限边界。
- 现阶段不改中央教条定义的五按钮和 EVA 主程序基本行为，而是把网页控制台对齐到这些行为。

## Goals / Non-Goals

**Goals:**

- 让网页控制台能够稳定完成首轮闭环：查看状态、列模型、装载本地模型、切换链接模式、重置、发送文本、接收流式输出。
- 明确状态源：主 EVA 正在运行且桥接可用时，运行状态、模型、会话记录、推理阶段都以主 EVA 为准。
- 明确协议边界：`eva_acp` 面向浏览器暴露 HTTP/OpenAI 兼容接口，面向 EVA 主程序使用 ControlChannel 桥接命令和事件。
- 将错误、忙碌、桥接断开、后端启动中等状态显式展示，避免 WebUI 误以为操作成功。
- 保留 ACP 适配层独立运行/降级能力，但不让它和主 EVA 同时竞争同一个状态源。

**Non-Goals:**

- 首轮不实现图片、文档、音频、截图输入。
- 首轮不实现知识库、MCP、工具调用面板、系统工程师、桌面控制器。
- 首轮不实现多用户协同、远程公网控制、认证登录。
- 首轮不替换 EVA 主程序现有窗口交互流程。

## Decisions

### Decision 1: 主 EVA 优先作为唯一运行状态源

当 `eva_acp` 能连接 `DEFAULT_ACP_BRIDGE_PORT` 时，状态、模型、装载、重置和发送均走桥接命令。`AcpRuntime` 自身维护的状态只作为桥接不可用时的降级状态。

理由：

- 主 EVA 已经拥有完整 UI 状态、上下文、工具调用、后端生命周期和输出流。
- WebUI 的目标是“控制 EVA”，不是复制一个新的 EVA 状态机。
- 这可以避免本地后端被主 EVA 和 ACP 各自启动、各自维护配置。
- 当前阶段选择“主 EVA 优先，ACP 可降级独立运行”：主程序存在时一切以主程序为准；主程序不存在时，ACP 可以临时作为本地/远端 OpenAI 兼容适配服务运行，但 UI 必须标明这是降级模式。

备选方案：

- 让 ACP 独立成为主运行时，再让 EVA UI 订阅 ACP。这个方向更像重构整机架构，风险和范围过大，不适合作为当前 WebUI 首轮。
- 强制要求主 EVA 先启动。这会让首轮架构更简单，但会失去 ACP 作为轻量 Web 入口的价值。

### Decision 2: ACP HTTP API 对浏览器保持稳定，内部桥接协议可迭代

浏览器只依赖 `/health`、`/api/backend/state`、`/api/backend/load`、`/api/runtime/reset`、`/v1/models`、`/v1/chat/completions`。桥接内部事件继续使用 framed JSON，但需要整理字段语义和错误返回。

理由：

- WebUI 和外部脚本都可以复用 OpenAI 兼容接口。
- 内部 bridge 命令可以按 EVA 主程序能力演进，不破坏浏览器入口。

备选方案：

- 直接让浏览器连接 ControlChannel。浏览器无法直接使用当前 TCP framing，也会把内部协议暴露给 UI，不利于兼容和安全。

### Decision 3: 文本对话首轮只提交最新 user 文本

桥接发送仍以 `bridge_send{text}` 驱动主 EVA 输入框和发送按钮。WebUI 保存的历史只用于展示，不试图覆盖主 EVA 的真实上下文。

理由：

- 主 EVA 目前的发送入口围绕 UI 输入框、记录列表和内部上下文运行。
- 直接注入完整 OpenAI messages 容易和主 EVA 已有上下文重复或冲突。

后续可扩展：

- 增加 `bridge_send_message`，支持 text/image/audio/document 等结构化输入。
- 增加 `bridge_set_context` 或 `bridge_new_session`，显式管理网页会话和 EVA 会话的映射。

### Decision 4: WebUI 会话先作为本地展示草稿，不承诺跨 EVA 会话一致

WebUI 的 `localStorage` 可以保存用户侧消息和展示用 assistant 文本，但状态面板必须标明真实运行状态来自 EVA/ACP。新建会话应调用 `/api/runtime/reset`，成功后再清空本地展示。

理由：

- 当前主 EVA 没有对外暴露多会话管理协议。
- 在未建立会话映射前，WebUI 不能假装自己拥有完整会话源。

### Decision 5: 首轮用轮询 + SSE，暂不引入 WebSocket

状态用轮询刷新和操作后刷新；对话输出使用 OpenAI 兼容 SSE。内部 ControlChannel 已经是双向事件流，ACP 可在后续把 snapshot/ui_state 转成浏览器事件。

理由：

- 当前 HTTP server 已经支持 SSE 转发和普通 JSON API。
- 不新增协议栈，先把状态正确性做扎实。

## Risks / Trade-offs

- [Risk] `bridgeModeEnabled()` 每次探测都可能触发短连接或阻塞 UI 请求 -> Mitigation: 后续实现连接缓存、退避重连和明确 bridge 状态字段。
- [Risk] `bridge_send` 只发送最新文本，WebUI 历史与主 EVA 上下文可能不一致 -> Mitigation: UI 明示“控制当前 EVA 会话”，首轮不提供独立多会话承诺。
- [Risk] 主 EVA 忙碌时装载/重置/发送失败，用户只看到泛化错误 -> Mitigation: bridge response 必须返回明确 busy/running/loading 阶段和中文错误。
- [Risk] 流式输出依赖主 EVA 的 output 广播，若最终 snapshot 覆盖不及时会出现空响应或重复片段 -> Mitigation: ACP 以事件流为主，结束后用 snapshot 只做兜底校正。
- [Risk] ACP 降级独立运行和桥接运行共享同一配置文件，可能造成配置覆盖 -> Mitigation: 明确桥接可用时以主 EVA 为准，降级模式的配置写入需谨慎隔离或在 UI 上标识。

## Migration Plan

1. 先按本设计调整 OpenSpec 任务并实现首轮闭环，不删除现有 ACP 入口。
2. 保持现有 HTTP 路由不变，修正字段和状态语义。
3. 保持 `DEFAULT_ACP_BRIDGE_PORT` 和 ControlChannel framing 不变。
4. 回滚策略：若 WebUI 行为异常，可继续使用 EVA 主窗口；ACP 可暂时不启动，不影响主程序核心聊天能力。

## Open Questions

无。

## Future Direction

- 最终方向已经独立记录为 OpenSpec change `decouple-eva-runtime`：把 EVA 从“窗口驱动应用”演进为“运行时驱动应用”，让 Qt Widget、ACP/WebUI 和后续其他 UI 都接入同一个无窗口 `EvaRuntime`。
- 本变更保留为过渡路径：在 `EvaRuntime` 尚未落地前，ACP 网页控制台仍可通过桥接主 EVA 或降级独立运行来验证 WebUI、HTTP API 和 OpenAI 兼容接口。
