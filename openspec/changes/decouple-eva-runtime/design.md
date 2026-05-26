## Context

当前代码已经不是完全无分层状态，已有一些有价值的迁移基础：

- `AppBootstrap/AppContext/ConfigMigrator/DefaultModelFinder` 已经从启动流程中抽出一部分应用上下文和配置能力。
- `NetClient` 已经用 `RequestSnapshot` 包装 `xNet`，方向正确，但快照仍由 `Widget` 组装。
- `BackendCoordinator` 已经集中本地后端生命周期，但它仍持有 `Widget *owner` 并大量访问 UI/配置字段。
- `SessionController` 和 `ToolFlowController` 已存在，但仍以 `Widget *owner` 为核心，直接读写 UI 和 `Widget` 成员。
- `ToolExecutor` 目前只是 `xTool` 的薄封装，工具执行链路仍和 UI 信号强绑定。
- `main.cpp` 仍负责创建 Widget、Expend、Tool、Net、MCP、监控线程并手工连接大量信号。
- `acp_runtime` 已经证明 HTTP/WebUI 可以作为前端入口，但当前主要靠桥接主 EVA 窗口或自身简化运行时。

因此可行路线不是重写，而是把现有“伪分层”改成真正的运行时边界。

目标架构：

```txt
                 ┌──────────────────┐
                 │   Qt Widget UI    │
                 └────────┬─────────┘
                          │ Runtime API + events
┌──────────────┐  ┌───────▼──────────┐  ┌────────────────┐
│ ACP / WebUI  │──▶    EvaRuntime     ◀──│ Future UI/API   │
└──────────────┘  └───────┬──────────┘  └────────────────┘
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
  Session/Core      Service Layer       Storage
  Toolflow          Net/Backend/Tool    History/Vector
```

## Goals / Non-Goals

**Goals:**

- 建立可在 `QCoreApplication` 中运行的无窗口 `EvaRuntime`。
- 让 `EvaRuntime` 暴露稳定命令：
  - `loadLocal`
  - `connectRemote`
  - `resetConversation`
  - `sendMessage`
  - `stop`
  - `stateSnapshot`
- 让 `EvaRuntime` 暴露稳定事件：
  - runtime phase
  - backend log/state
  - output chunk
  - record add/update
  - tool start/result
  - conversation finished
  - error
- 让 Qt Widget 和 ACP/WebUI 都通过同一 runtime 使用能力。
- 迁移期间保留窗口版行为，避免一次性大爆炸式重构。

**Non-Goals:**

- 不在第一阶段重做全部 UI。
- 不在第一阶段重写 `xNet`、`xTool`、`xMcp` 的内部实现。
- 不在第一阶段引入新的前端框架。
- 不在第一阶段实现公网多用户认证。

## Decisions

### Decision 1: 先加 Runtime Facade，再逐步搬迁内部实现

先创建 `EvaRuntime` 作为外部唯一入口，即使内部第一版仍调用一部分现有 controller/service。之后每个子系统逐步去除对 `Widget` 的依赖。

理由：

- 可以尽早让 ACP 和 Widget 对齐到同一个接口。
- 每一步都有可运行基线。
- 避免直接拆 `Widget` 导致长时间不可编译。

### Decision 2: Runtime 使用命令/事件模型，不暴露 UI 控件或 Widget 成员

前端向 runtime 发送结构化命令，runtime 通过事件通知前端刷新。禁止 runtime 依赖 `QTextEdit`、`QLineEdit`、`QPushButton` 等控件。

理由：

- 这是 UI 可替换的核心边界。
- WebUI、Qt Widget、CLI 或后续其他 UI 可以共享同一能力。

### Decision 3: 状态集中到 RuntimeState

将 `is_load`、`is_run`、`turnActive_`、`toolInvocationActive_`、`ui_mode`、`ui_state`、KV 统计、当前模型、当前端点等收束到 `RuntimeState`。

建议状态：

- `RuntimePhase`: Unloaded / Loading / Ready / Running / ToolRunning / Recording / Error
- `RuntimeMode`: Local / Link
- `ConversationMode`: Chat / Complete

理由：

- 当前多组 bool 容易互相冲突。
- HTTP/API/UI 都需要一致的状态快照。

### Decision 4: Widget 先变成 Runtime 的一个前端适配器

Widget 不会马上删除，而是逐步改成：

- 从控件采集用户输入，转换为 `RuntimeCommand`。
- 监听 `RuntimeEvent`，刷新输出区、记录条、按钮状态和设置窗口。
- 只保留 UI 独有能力，例如布局、主题、快捷键、托盘和弹窗。

### Decision 5: ACP 后期直接内嵌 EvaRuntime

当前 ACP 可继续桥接主 EVA 或降级独立运行。Runtime 解耦完成后，`eva_acp` 应直接创建 `EvaRuntime`，无需启动窗口，也无需通过 ControlChannel 绕回 Widget。

ControlChannel 后续定位：

- 兼容旧窗口控制。
- 调试/远程控制通道。
- 非首选主路径。

## Migration Plan

### Phase 0: 固化边界和基线

- 保留当前 ACP WebUI 作为验证入口。
- 确定 Runtime 命令、事件、状态结构。
- 给现有核心流程补最小烟测脚本或手动验证清单。

### Phase 1: 创建 EvaRuntime 壳

- 新增 `src/runtime/eva_runtime.*`、`runtime_state.*`、`runtime_events.*`、`runtime_commands.*`。
- 把 AppBootstrap、配置路径、默认模型发现、线程创建入口放入 runtime bootstrap。
- Runtime 第一版可以内部调用现有 `NetClient`、`LocalServerManager`、`ToolExecutor`。

### Phase 2: 后端和网络从 Widget 中剥离

- 将 `BackendCoordinator` 改为不持有 `Widget *`，只依赖 settings/context/state sink。
- 将 `NetClient` 的输入从 Widget 组装改为 Runtime 组装。
- Runtime 直接处理后端 ready/error/log 事件。

### Phase 3: 会话和工具回环从 Widget 中剥离

- 将 `SessionController` 改为不持有 `Widget *`。
- 将消息数组、历史、compaction、KV 统计迁入 runtime/core。
- 将 `ToolFlowController` 输出改为 runtime events，不直接写 UI 记录条。

### Phase 4: Widget 前端化

- Widget 改为 RuntimeView/Presenter 模式。
- UI 状态由 RuntimeState 映射，不再自己维护运行真相。
- 确保五按钮行为仍符合中央教条。

### Phase 5: ACP 内嵌 Runtime

- `eva_acp` 创建 `EvaRuntime`。
- `/api/backend/*`、`/v1/models`、`/v1/chat/completions` 直接调用 runtime。
- bridge 模式降为兼容路径。

## Risks / Trade-offs

- [Risk] 范围大，容易出现长期半迁移状态 -> Mitigation: 每期只迁一条主链路，tasks 必须可验证。
- [Risk] Widget 当前承担 UI 和业务双重职责，抽离时容易遗漏行为 -> Mitigation: 先保留兼容层和事件镜像，观察一致后再删旧路径。
- [Risk] Tool/Expend/MCP 与 Widget 信号连接很多 -> Mitigation: 先将它们作为 runtime service 接入，不急于重写内部。
- [Risk] 无窗口运行会遇到依赖 GUI 类的代码 -> Mitigation: Runtime 只使用 QtCore/QtNetwork；必须用 GUI 的能力放到前端适配器或专门 service。
- [Risk] 单实例、托盘、快捷键、截图、录音等能力不全是后端能力 -> Mitigation: 明确区分 runtime 能力、桌面前端能力、系统工具能力。

## Open Questions

- 第一阶段是否接受 `EvaRuntime` 内部临时复用部分现有 Widget controller 逻辑，还是要求从第一版开始完全不包含 Widget 依赖？
