## 1. 主程序桥接命令

- [x] 1.1 在 `widget.h` 声明 `bool applyBridgeCapabilities(const QJsonObject &payload, QString *errorMessage);`(置于 `applyAcpBridgeLoad` 附近)。
- [x] 1.2 在 `widget_link.cpp` 实现 `applyBridgeCapabilities`:guard(busy / date_ui 为空);按 payload 对相应 `date_ui->*_checkbox->setChecked()`;调 `set_date()`;返回 true。
- [x] 1.3 在 `handleAcpBridgeCommand` 分发器加 `bridge_set_capabilities` 分支:调用 `applyBridgeCapabilities`,成功返回 `accepted` + `buildAcpBridgeState()`,失败返回 `error` + state。

## 2. 桥接客户端与 eva_acp

- [x] 2.1 `acp_bridge_client.{h,cpp}` 新增 `QJsonObject setCapabilities(const QJsonObject &payload, QString *errorMessage, int timeoutMs)`,复用 `request("bridge_set_capabilities", ...)`。
- [x] 2.2 `acp_runtime.{h,cpp}` 新增 `bool setCapabilities(const QJsonObject &request, QString *errorMessage)`:bridge 模式转发;direct/降级返回"需主程序桥接"错误。
- [x] 2.3 `acp_http_server.cpp` 加 `POST /api/runtime/tools` 路由,调 `setCapabilities`,返回 state 或错误;按需加入 bridge-required 列表。

## 3. WebUI 工具开关

- [x] 3.1 `api.ts` 加 `setTools(payload)`(POST /api/runtime/tools)。
- [x] 3.2 `store.ts` 加 `setTool(key, value)`:调 api → 刷新状态;失败写 feedback。
- [x] 3.3 `RuntimeDrawer.vue` 工具面板:桥接(`full_eva_stack`)时渲染可交互开关并提示"切换会重置当前对话";非桥接只读 + 门控说明。

## 4. 构建与验证

- [x] 4.1 构建 `eva`(含 widget.h 改动)与 `eva_acp`;`cd webui && npm run build`。
- [x] 4.2 direct 模式 curl `POST /api/runtime/tools` 确认被门控(非 2xx + 明确错误)。
- [x] 4.3 桥接效果说明:需用户在运行中的主程序上验证开关 knowledge/mcp 后对话真实使用;记录验证步骤。
- [x] 4.4 `python3 scripts/update_feature_log.py` 写入实现摘要。
