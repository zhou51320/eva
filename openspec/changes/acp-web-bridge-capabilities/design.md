## Context

桥接架构:Browser → eva_acp(HTTP)→ ControlChannel(framed JSON)→ 主程序 Widget。主程序 `buildAcpBridgeState()` 已上报每个工具的启用状态(来自 `ui_*_ischecked`),`sendBridgeText()` 已驱动主程序执行(含工具/知识/MCP)。缺的是从网页**开关**这些能力。

已确认的关键事实(决定实现方式):

- 工具启用标志为 `ui_calculator/engineer/MCPtools/knowledge/controller/stablediffusion_ischecked`(`widget.h`)。
- 工具复选框位于 `date_ui`,`date_ui` 在构造函数 `set_DateDialog()`(`widget.cpp:239`)创建、**全程常驻**(仅析构时 delete),因此可安全 `setChecked()`。
- 复选框 `stateChanged → tool_change()`(`widget_date.cpp:38-65`),`tool_change()` 重算 `is_load_tool` 并 `ui_extra_prompt = create_extra_prompt()`(`widget_settings_slots.cpp:224-247`)。`stateChanged` 对程序化 `setChecked()` 同样触发。
- "确定"入口 `set_date()`(`widget_date.cpp:330`)= `get_date()`(把 `ui_extra_prompt` 合并进 `ui_DATES.date_prompt`)+ 技能/控制台刷新 + `auto_save_user()` 持久化 + `on_reset_clicked()` 重置上下文。
- 额外的复选框 lambda 仅 `autosave()`/非模态刷新,无模态弹窗 → 程序化驱动不会卡线程。

## Goals / Non-Goals

**Goals:**

- 让网页在桥接模式下开关主程序工具/知识/MCP,且复用主程序生产路径保证下一轮对话生效。
- 非桥接模式明确门控,不伪造成功。
- 不改主程序工具执行/知识/MCP 的内部实现。

**Non-Goals:**

- 不在 web/direct-runtime 重新实现工具执行(那是 `decouple-eva-runtime` 的范围)。
- 不做知识库文档管理、MCP 服务器配置(仍在主程序内完成);本变更只做"启用开关"。
- 不引入新的事件类型(工具过程可视化为后续变更)。

## Decisions

### Decision 1: 复用 `tool_change()`+`set_date()` 生产路径,驱动常驻 date_ui 复选框

`applyBridgeCapabilities(payload)`:按 payload 对相应 `date_ui->*_checkbox->setChecked()`,再调 `set_date()`。等价于用户在"约定"对话框勾选后点确定。

理由:

- `date_ui` 常驻,可安全驱动;`stateChanged` 触发 `tool_change()` 自动重算 `is_load_tool`/`create_extra_prompt`;`set_date()` 完成系统提示合并、持久化、重置。
- 避免手工复刻 `is_load_tool` 推导、`create_extra_prompt()`、提示词合并等隐性步骤(易漏)。
- 与既有 `applyAcpBridgeLoad`(驱动 line edit + `set_api()`)同构,风格一致。

备选:直接写 `ui_*_ischecked` 标志 + 手工复刻尾部副作用——更脆弱、易漏 `create_extra_prompt`/合并,弃用。

### Decision 2: 端点桥接门控,direct 模式显式拒绝

`POST /api/runtime/tools` 仅在 bridge 模式有效;direct/降级模式返回明确错误(能力由主程序拥有,direct runtime 尚未承载工具执行)。

理由:与现有架构一致;避免给用户"已启用"的假象。

### Decision 3: 开关后回传最新 bridge state

命令成功返回 `buildAcpBridgeState()`,网页据此刷新 capabilities,确保一致性。

## Risks / Trade-offs

- [Risk] `set_date()` 会 `on_reset_clicked()` 重置上下文 → 每次开关清空当前对话。Mitigation: 这与 Qt 中改"约定"的既有行为一致;网页 UI 提示"切换能力会重置当前对话"。
- [Risk] 启用 knowledge/mcp 但未在主程序配置文档/服务器 → 工具无实际效果。Mitigation: 本变更只负责"开关",资源配置仍在主程序;面板说明。
- [Risk] 无法 headless 验证桥接真实效果(需带模型的主程序在跑)。Mitigation: 编译 + 验证 direct 门控;桥接效果由用户在运行中的主程序上验证。
- [Risk] 改 `widget.h` 触发大范围重编译。Mitigation: 仅新增一个方法声明,接受重编译成本。

## Migration Plan

1. 主程序加 `applyBridgeCapabilities` + 命令分发;`AcpBridgeClient.setCapabilities`;eva_acp 端点 + direct 门控;webui 开关。
2. 构建 `eva` 与 `eva_acp`;`npm run build` 刷新产物。
3. 验证:direct 模式 curl `POST /api/runtime/tools` 应被门控;桥接效果用户实测。
4. 回滚:端点/命令为纯增量,可单独还原;webui 可回退。

## Open Questions

无。
