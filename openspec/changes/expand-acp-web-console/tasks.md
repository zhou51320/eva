## 1. 后端采样字段透传(前置验证)

- [x] 1.1 用 curl 验证 `/v1/chat/completions` 在 direct/link 路径是否转发请求体中的 `temperature`/`top_p`/`top_k`/`max_tokens`。
- [x] 1.2 若未转发,在 `acp_runtime.cpp`/`acp_http_server.cpp` 补最小透传:仅透传请求体显式提供的采样字段,缺省不覆盖运行层默认值。
- [x] 1.3 重建 `eva_acp` 并 curl 确认采样字段已生效(对比修改前后行为)。

## 2. 生成设置面板

- [x] 2.1 在 store 增加 settings 状态(temperature/top_p/top_k/max_tokens/system_prompt)与本地持久化(localStorage)。
- [x] 2.2 新增 Settings 面板组件:滑杆/输入控件 + 系统提示词文本域 + 重置为默认。
- [x] 2.3 在 `api.ts`/store 的发送逻辑中,把非空采样字段注入请求体,把系统提示词作为前置 `system` 消息。
- [x] 2.4 验证:改温度并发送,请求体含该值;填系统提示词,messages 以 system 开头;刷新后设置恢复。

## 3. 工具与能力面板

- [x] 3.1 在 store/类型中对 `capabilities`(configured_tools/tools/enabled_tools/tool_execution_route/tts 等)做防御式读取。
- [x] 3.2 新增 Tools & Capabilities 面板:逐工具展示 已配置/已启用/执行路径,缺字段降级为"未知/未配置"。
- [x] 3.3 对 knowledge/mcp 等不可用项显式门控并说明"需主程序桥接",不渲染伪造操作控件。
- [x] 3.4 验证:state 报告 `engineer` 已配置且 `not_attached` 时,面板如实呈现并随刷新更新。

## 4. 图片附件输入

- [x] 4.1 在 Composer 增加图片选择/预览/移除(读为 data URL,纯前端不落盘)。
- [x] 4.2 发送时:有附件则 user 消息 `content` 用多模态数组,无附件退回字符串。
- [x] 4.3 附件区标注适用范围(视觉模型/链接模式);纯文本路径不受影响。
- [x] 4.4 验证:带图发送的 content 为数组含 image_url;无图发送为字符串。

## 5. 面板呈现与侧栏改造

- [x] 5.1 移除侧栏所有永久 disabled 占位按钮,改为真实分组入口(对话/设置/工具与能力/连接状态)。
- [x] 5.2 新增连接与状态来源面板:区分 direct-runtime/bridge/degraded,展示 full_eva_stack/会话归属/输入模式/TTS。
- [x] 5.3 verify 每个入口点击后都能打开对应面板,无死占位、无可点击但无效的控件。

## 6. 构建与验证

- [x] 6.1 `cd webui && npm run build`,确认产物为单个自包含 `resource/acp_web/index.html`(无外部 src/href 引用)。
- [x] 6.2 重建 `eva_acp`,启动后 `GET /` 返回新产物;`scripts/smoke_acp_runtime.py --expect-source direct_runtime` 通过。
- [x] 6.3 curl 抓取首页确认含各新面板关键字符串(设置/工具与能力/附件/连接状态)。
- [x] 6.4 运行 `python3 scripts/update_feature_log.py` 写入本次实现摘要。
