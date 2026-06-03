## ADDED Requirements

### Requirement: 生成设置面板

ACP 网页控制台 SHALL 提供一个生成设置面板,允许用户调整温度、top_p、top_k、最大生成 tokens 与系统提示词,并将其作为标准 OpenAI 字段随对话请求发送。设置 MUST 持久化在浏览器本地。eva_acp 在直连与桥接聊天路径上 MUST 转发请求体中显式提供的采样字段。

#### Scenario: 调整采样参数并发送
- **WHEN** 用户在设置面板把温度改为 0.9 并发送一条消息
- **THEN** 发往 `/v1/chat/completions` 的请求体包含 `temperature: 0.9`
- **AND** 该值被聊天路径转发到底层模型而非被忽略

#### Scenario: 设置系统提示词
- **WHEN** 用户填写了系统提示词并发送消息
- **THEN** 请求 `messages` 数组以一条 `role: "system"` 消息开头,内容为该提示词

#### Scenario: 设置本地持久化
- **WHEN** 用户修改设置后刷新页面
- **THEN** 设置面板恢复用户上次保存的值

### Requirement: 工具与能力面板

控制台 SHALL 提供工具与能力面板,从 `/api/backend/state` 的 `capabilities` 实时展示各内置工具(calculator、engineer、controller、knowledge、mcp、stablediffusion 等)的"已配置 / 已启用 / 执行路径"状态。该面板 MUST NOT 为运行层不支持执行的工具提供伪造的操作控件。

#### Scenario: 展示真实工具状态
- **WHEN** 后端状态报告 `configured_tools_list: ["engineer"]` 且 `tool_execution_route: "not_attached"`
- **THEN** 面板显示 engineer 为"已配置",其余为"未配置",并标明工具执行未挂载

#### Scenario: 能力随状态刷新更新
- **WHEN** 刷新或重新获取后端状态后某工具的启用状态变化
- **THEN** 面板展示同步更新为最新状态

### Requirement: 图片附件输入

对话输入区 SHALL 允许用户附加一张或多张图片,并按 OpenAI 多模态 `content` 数组(`image_url` 为 data URL)随消息发送。当无附件时,请求 MUST 退回为纯文本 `content` 字符串以保持兼容。

#### Scenario: 携带图片发送
- **WHEN** 用户附加一张图片并发送消息
- **THEN** 该 user 消息的 `content` 是包含 `{type:"text"}` 与 `{type:"image_url", image_url:{url:<dataURL>}}` 的数组

#### Scenario: 无附件时保持纯文本
- **WHEN** 用户不附加任何图片发送消息
- **THEN** 该 user 消息的 `content` 是普通字符串

### Requirement: 能力驱动的面板呈现

控制台侧栏 SHALL NOT 包含永久禁用的占位按钮;每个可见入口 MUST 打开一个真实面板。对于由主程序拥有、当前不可用的能力(如未桥接时的知识库、MCP),面板 MUST 显式门控并说明原因,而非呈现可点击但无效的控件。

#### Scenario: degraded 模式下的门控
- **WHEN** 运行在 direct-runtime 降级模式且 `capabilities.knowledge` 为 false
- **THEN** 知识库相关视图显示"需主程序桥接才可用"的说明而非可操作控件

#### Scenario: 无死占位入口
- **WHEN** 用户查看侧栏
- **THEN** 不存在永久 disabled 的占位按钮;每个入口点击后都能打开对应面板

### Requirement: 连接与状态来源呈现

控制台 SHALL 清晰区分并展示当前运行来源:direct-runtime、bridge、degraded,以及 `full_eva_stack`、会话归属、输入模式与 TTS 配置状态,使用户始终知道操作作用于哪一层。

#### Scenario: 区分桥接与直连
- **WHEN** `state_source` 为 `bridge`
- **THEN** 状态区显示"主程序桥接"且 `full_eva_stack` 反映完整能力

#### Scenario: 直连降级提示
- **WHEN** `state_source` 为 `direct_runtime` 且 `bridge_available` 为 false
- **THEN** 状态区标明为直连/降级运行,并提示部分主程序能力不可用
