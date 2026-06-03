## ADDED Requirements

### Requirement: 网页开关主程序工具与能力

桥接可用时,ACP 网页控制台 SHALL 允许用户开关主程序的工具与扩展能力(calculator、engineer、controller、stablediffusion、knowledge、mcp)。开关 MUST 通过 ACP 桥接命令应用到正在运行的主程序,并复用主程序既有的工具应用路径,使下一轮对话即按新配置执行。

#### Scenario: 桥接模式下启用知识库
- **WHEN** 桥接可用且用户在网页打开"知识库"开关
- **THEN** eva_acp 向主程序发送 `bridge_set_capabilities` 使 `knowledge` 启用
- **AND** 主程序重算工具提示词并合并进系统提示,后续对话按启用知识库执行

#### Scenario: 开关后状态回传
- **WHEN** 一次能力开关成功应用
- **THEN** 响应返回最新 bridge state,其 `capabilities` 反映新的启用集合

#### Scenario: 主程序忙时拒绝
- **WHEN** 主程序正在推理(busy)时收到能力开关
- **THEN** 返回明确的"命令被阻止"错误,不改变当前能力

### Requirement: 非桥接模式的能力门控

当桥接不可用(direct-runtime 或降级模式)时,能力开关端点 SHALL 返回明确的"需主程序桥接"错误,且 MUST NOT 静默假装成功。网页 SHALL 在此模式下把工具面板呈现为只读并说明原因。

#### Scenario: direct 模式开关被门控
- **WHEN** eva_acp 处于 direct-runtime 模式,收到 `POST /api/runtime/tools`
- **THEN** 返回非 2xx 且错误信息说明该操作需要桥接主程序

#### Scenario: 网页只读呈现
- **WHEN** `capabilities.full_eva_stack` 为 false
- **THEN** 工具面板的开关为只读/禁用,并显示需桥接的说明

### Requirement: 能力查询一致性

网页查询到的工具启用状态 SHALL 来自主程序真实状态(bridge state 的 `capabilities`),开关成功后 SHALL 与主程序保持一致。

#### Scenario: 开关后刷新一致
- **WHEN** 用户开关某能力成功后网页刷新状态
- **THEN** 面板显示的启用集合与主程序当前实际启用集合一致
