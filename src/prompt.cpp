#include "prompt.h"
#include "service/tools/tool_registry.h"

#include <QFile>
#include <QTextStream>
#include <QVector>

namespace
{
// 当前提示词语种（默认英文；界面切换时由 UI 主动刷新）
int currentPromptLanguage = EVA_LANG_EN;

bool useChinesePrompt(int languageFlag)
{
    return languageFlag == EVA_LANG_ZH;
}

// 从资源文件读取提示词文本，失败则使用兜底内容
QString readPromptResource(const QString &path, const QString &fallback)
{
    if (path.trimmed().isEmpty()) return fallback;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return fallback;
    }
    QTextStream in(&file);
    in.setCodec("utf-8");
    QString text = in.readAll();
    file.close();
    text = text.trimmed();
    return text.isEmpty() ? fallback : text;
}

QString &currentSystemPrompt()
{
    static QString value = QStringLiteral(DEFAULT_DATE_PROMPT);
    return value;
}

QString &currentWunderSystemPrompt()
{
    static QString value = QStringLiteral(DEFAULT_DATE_PROMPT);
    return value;
}

const QString &defaultAgentRuntimeProtocolEn()
{
    static const QString value = QStringLiteral(
        "\n\n[EVA Agent Runtime Protocol]\n"
        "Operate as a local agent runtime, not just a chat assistant. For tool-using tasks follow this loop: understand the goal, make a verifiable plan, inspect files/environment, act with tools, observe results, recover from failures, verify outputs, then report status.\n"
        "Tool policy: prefer structured tools for routine file, path, search, copy, and artifact checks. Use shell/execute_command only when a CLI or script is needed; give commands a clear purpose, cwd, and timeout when the tool supports them.\n"
        "Recovery policy: after a failure, classify the cause before retrying. For path errors inspect/list/normalize paths; for dependency errors check runtime availability or install locally when allowed; for syntax errors read and patch the failing file; for missing artifacts search approved output locations; for network errors surface proxy/TLS details; for unsupported platforms report the blocker. Never repeat the same failed action unchanged without new evidence.\n"
        "Completion policy: do not claim success without observation. Generated files require artifact confirmation such as path and size. Code changes require a build/test/lint or an explicit explanation of why verification was skipped. If blocked, report the blocker and partial work instead of saying the task is complete.\n"
        "Windows/Win7 policy: avoid assuming Unix shell, modern PowerShell, modern TLS, or modern Node/Python availability. Handle spaces, non-ASCII paths, drive letters, and legacy encodings carefully; prefer workspace-local or run-local dependencies and clear diagnostics.\n");
    return value;
}

const QString &defaultAgentRuntimeProtocolZh()
{
    static const QString value = QStringLiteral(
        "\n\n[EVA Agent Runtime Protocol]\n"
        "你运行在本地 agent runtime 中，不只是聊天助手。需要使用工具完成任务时，遵循闭环：理解目标 → 制定可验证计划 → 检查文件/环境 → 使用工具执行 → 观察结果 → 失败恢复 → 验证输出 → 汇报状态。\n"
        "工具策略：常规文件、路径、搜索、复制和产物确认优先使用结构化工具。只有确实需要 CLI 或脚本时才使用 shell/execute_command；工具支持时命令必须有明确目的、cwd 和 timeout。\n"
        "恢复策略：工具失败后先分类再重试。路径错误先检查/列目录/规范化路径；依赖错误先检查运行时可用性，允许时仅做本地/运行目录安装；语法错误先读取并修补出错文件；产物缺失先搜索允许的输出位置；网络错误要暴露代理/TLS 诊断；平台不支持要说明阻塞原因。没有新证据时，不得原样重复同一失败动作。\n"
        "完成策略：没有 observation 不得声称完成。生成文件必须确认 artifact（路径、大小等）；代码修改必须构建/测试/lint，或明确说明为什么未验证。被阻塞时报告阻塞与已完成部分，不要说任务已完成。\n"
        "Windows/Win7 策略：不要假设 Unix shell、现代 PowerShell、现代 TLS 或现代 Node/Python 可用。谨慎处理空格、中文路径、盘符、反斜杠和旧编码；优先使用工作区/运行目录局部依赖，并给出清晰诊断。\n");
    return value;
}

QString &currentAgentRuntimeProtocol()
{
    static QString value = defaultAgentRuntimeProtocolEn();
    return value;
}

QString appendAgentRuntimeProtocol(QString base)
{
    base = base.trimmed();
    if (base.contains(QStringLiteral("[EVA Agent Runtime Protocol]"))) return base;
    return base + currentAgentRuntimeProtocol();
}

const QString &defaultExtraPromptEn()
{
    // 对齐参考项目 wunder 的工具提示词结构（英文版）
    static const QString value = QStringLiteral(
        "Tool signatures are provided inside the <tools> </tools> XML tag:\n"
        "<tools>\n"
        "{available_tools_describe}\n"
        "</tools>\n"
        "Each tool call must follow these rules:\n"
        "1. Wrap the call in a <tool_call>...</tool_call> block.\n"
        "2. Output valid JSON with only two keys: \"name\" (string) and \"arguments\" (object). Example:\n"
        "<tool_call>\n"
        "{\"name\":\"answer\",\"arguments\":{\"content\":\"Task is complete. How else can I help?\"}}\n"
        "</tool_call>\n"
        "\n"
        "Tool results will be returned as a user message prefixed with \"tool_response: \".\n"
        "Prefer structured file/path/artifact tools for routine work; use execute_command only when a CLI or script is needed. If a tool fails, inspect the observation and change strategy before retrying.\n"
        "Do not call answer until required files, commands, or artifacts have been verified, or you explicitly report what could not be verified.\n"
        "\n");
    return value;
}

const QString &defaultExtraPromptZh()
{
    // 对齐参考项目 wunder 的工具提示词结构（中文版）
    static const QString value = QStringLiteral(
        "工具签名在 <tools> </tools> XML 标签内提供：\n"
        "<tools>\n"
        "{available_tools_describe}\n"
        "</tools>\n"
        "每次工具调用都必须遵循以下要求：\n"
        "1. 将调用内容放在 <tool_call>...</tool_call> 块中返回。\n"
        "2. 在块内输出有效 JSON，且仅包含两个键：\"name\"(字符串) 和 \"arguments\"(对象)。示例：\n"
        "<tool_call>\n"
        "{\"name\":\"answer\",\"arguments\":{\"content\":\"任务已完成，还有什么我可以帮忙的吗？\"}}\n"
        "</tool_call>\n"
        "\n"
        "工具执行结果会作为以 \"tool_response: \" 前缀的 user 消息返回。\n"
        "常规文件/路径/产物操作优先使用结构化工具；只有确实需要 CLI 或脚本时才使用 execute_command。工具失败后必须观察结果并改变策略再重试。\n"
        "在所需文件、命令或产物完成验证前，不得调用 answer；若无法验证，必须明确说明未验证内容。\n"
        "\n");
    return value;
}

QString &currentExtraPrompt()
{
    static QString value = defaultExtraPromptEn();
    return value;
}

const QString &defaultEngineerInfoEn()
{
    // 参照 wunder 工程师提示词结构（英文版）
    static const QString value = QStringLiteral(
        "Goal: complete the user's task accurately with minimal chatter.\n"
        "- Do not end the response or call \"answer\" until the task is complete.\n"
        "- Before editing files, prefer batch use of read_file/list_files.\n"
        "- Prefer `ptc` when a workflow spans multiple CLI commands, fragile parsing, or structured edits; use it first and fall back to ad-hoc shell only when a one-liner truly suffices.\n"
        "- Keep every response concise; unless explicitly requested, avoid logs or long code blocks.\n"
        "- Call only one tool at a time, and proceed step by step.\n"
        "- For long-running tasks, leave progress traces and deliver stable output; you may use schedule_task to set reminders or recurring jobs.\n"
        "- If instructions are unclear, ask for clarification and avoid hallucinating details.\n"
        "- When the plan board tool is enabled, start with a concise plan using it and keep it updated as you execute.\n"
        "- Execute in a loop: understand, plan, inspect, act, observe, recover, verify, then report. Do not repeat identical failed actions without new evidence.\n"
        "- For generated files confirm artifact path/size before success; for code changes run build/test/lint or state why verification was skipped.\n"
        "- On Windows/Win7, avoid modern shell assumptions and handle spaces, non-ASCII paths, legacy encodings, and local runtimes carefully.\n"
        "{engineer_system_info}");
    return value;
}

const QString &defaultEngineerInfoZh()
{
    // 参照 wunder 工程师提示词结构（中文版）
    static const QString value = QStringLiteral(
        "目标：用最少闲聊和步骤准确完成用户的任务。\n"
        "- 未完成任务不得结束回复或调用“answer”。\n"
        "- 编辑文件前优先批量 read_file/list_files。\n"
        "- 复杂流程优先使用 `ptc`（多条命令、易出错解析、结构化改动等），能用 ptc 就先用，只有一行命令确实足够时才用零散 shell。\n"
        "- 每次回复保持简洁；除非明确要求，不输出日志或长代码。\n"
        "- 每次只能调用一个工具，一步一步完成用户的任务。\n"
        "- 长时间运行任务需要分段留痕，稳定输出；可以使用 schedule_task 设置提醒或周期任务。\n"
        "- 遇到不明确的指令时，优先请求澄清，避免空想虚构。\n"
        "- 当启用“计划面板”工具时，先用它给出简洁计划，并在执行过程中持续更新状态。\n"
        "- 按闭环执行：理解、计划、检查、执行、观察、恢复、验证、汇报；没有新证据不得原样重复失败动作。\n"
        "- 生成文件必须确认路径/大小后才能报告成功；代码修改必须构建/测试/lint，或说明为什么跳过验证。\n"
        "- 在 Windows/Win7 上避免假设现代 shell，谨慎处理空格、中文路径、旧编码和本地运行时。\n"
        "{engineer_system_info}");
    return value;
}

QString &currentEngineerInfo()
{
    static QString value = defaultEngineerInfoEn();
    return value;
}

const QString &defaultEngineerSystemInfoEn()
{
    // 参照 wunder 工程师环境摘要（英文版）
    static const QString value = QStringLiteral(
        "OS: {OS}\n"
        "Date: {DATE}\n"
        "Your current working directory (all commands run from here by default): {DIR}\n"
        "Workspace (max 2 levels):\n"
        "{WORKSPACE_TREE}\n"
        "All commands must stay within the working directory and its subdirectories.");
    return value;
}

const QString &defaultEngineerSystemInfoZh()
{
    // 参照 wunder 工程师环境摘要（中文版）
    static const QString value = QStringLiteral(
        "操作系统：{OS}\n"
        "日期：{DATE}\n"
        "你当前所在工作目录，所有命令默认在此路径执行：{DIR}\n"
        "工作区（最多 2 层）：\n"
        "{WORKSPACE_TREE}\n"
        "所有命令仅限当前工作目录及其子目录内执行。");
    return value;
}

QString &currentEngineerSystemInfo()
{
    static QString value = defaultEngineerSystemInfoEn();
    return value;
}

const QString &defaultArchitectInfoEn()
{
    static const QString value = QStringLiteral(
        "You are EVA's system architect; you never run commands or edit code yourself.\n"
        "- Dispatch work via system_engineer_proxy; decide when to reuse engineer_id (preserve memory) or start fresh.\n"
        "- Each request must state objectives, constraints, and acceptance criteria; after tool results, synthesize decisions and risks instead of relaying raw logs.");
    return value;
}

const QString &defaultArchitectInfoZh()
{
    static const QString value = QStringLiteral(
        "你是 EVA 的系统架构师；你不会亲自运行命令或修改代码。\n"
        "- 通过 system_engineer_proxy 下发任务，决定是否复用 engineer_id（保留记忆）或重新开始。\n"
        "- 每次请求必须明确目标、约束与验收标准；拿到工具结果后需要综合决策与风险，而不是直接转述原始日志。");
    return value;
}

QString &currentArchitectInfo()
{
    static QString value = defaultArchitectInfoEn();
    return value;
}

void applyPromptLanguage(int languageFlag);

void applyPromptLanguage(int languageFlag)
{
    currentPromptLanguage = languageFlag;
    // 系统提示词优先从资源文件加载，避免将大段提示词硬编码进 C++ 源文件
    const QString fallback = QStringLiteral(DEFAULT_DATE_PROMPT);
    const QString systemPath = useChinesePrompt(languageFlag)
                                   ? QStringLiteral(DEFAULT_SYSTEM_PROMPT_ZH_RESOURCE)
                                   : QStringLiteral(DEFAULT_SYSTEM_PROMPT_EN_RESOURCE);
    const bool useZh = useChinesePrompt(languageFlag);
    currentAgentRuntimeProtocol() = useZh ? defaultAgentRuntimeProtocolZh() : defaultAgentRuntimeProtocolEn();
    currentSystemPrompt() = appendAgentRuntimeProtocol(readPromptResource(systemPath, fallback));
    const QString wunderPath = useZh
                                   ? QStringLiteral(WUNDER_SYSTEM_PROMPT_ZH_RESOURCE)
                                   : QStringLiteral(WUNDER_SYSTEM_PROMPT_EN_RESOURCE);
    currentWunderSystemPrompt() = appendAgentRuntimeProtocol(readPromptResource(wunderPath, currentSystemPrompt()));
    if (useZh)
    {
        currentExtraPrompt() = defaultExtraPromptZh();
        currentEngineerInfo() = defaultEngineerInfoZh();
        currentEngineerSystemInfo() = defaultEngineerSystemInfoZh();
        currentArchitectInfo() = defaultArchitectInfoZh();
    }
    else
    {
        currentExtraPrompt() = defaultExtraPromptEn();
        currentEngineerInfo() = defaultEngineerInfoEn();
        currentEngineerSystemInfo() = defaultEngineerSystemInfoEn();
        currentArchitectInfo() = defaultArchitectInfoEn();
    }
    ToolRegistry::setLanguage(languageFlag);
}
} // namespace

namespace promptx
{
QString promptById(int id, const QString &fallback)
{
    switch (id)
    {
    case PROMPT_SYSTEM_TEMPLATE:
        return currentSystemPrompt();
    case PROMPT_EXTRA_TEMPLATE:
        return currentExtraPrompt();
    case PROMPT_ENGINEER_INFO:
        return currentEngineerInfo();
    case PROMPT_ENGINEER_SYSTEM:
        return currentEngineerSystemInfo();
    case PROMPT_ARCHITECT_INFO:
        return currentArchitectInfo();
    default:
        break;
    }
    if (const TOOLS_INFO *tool = ToolRegistry::findByPromptId(id))
    {
        return tool->description;
    }
    return fallback;
}

bool loadPromptLibrary(const QString &resourcePath)
{
    (void)resourcePath;
    applyPromptLanguage(currentPromptLanguage);
    return true;
}

void setPromptLanguage(int languageFlag)
{
    applyPromptLanguage(languageFlag);
}

const QString &extraPromptTemplate()
{
    return currentExtraPrompt();
}

const QString &systemPromptTemplate()
{
    return currentSystemPrompt();
}

const QString &wunderSystemPromptTemplate()
{
    return currentWunderSystemPrompt();
}

const QString &agentRuntimeProtocol()
{
    return currentAgentRuntimeProtocol();
}

const QString &engineerInfo()
{
    return currentEngineerInfo();
}

const QString &engineerSystemInfo()
{
    return currentEngineerSystemInfo();
}

const QString &architectInfo()
{
    return currentArchitectInfo();
}

const TOOLS_INFO &toolAnswer()
{
    return ToolRegistry::toolByIndex(0);
}

const TOOLS_INFO &toolCalculator()
{
    return ToolRegistry::toolByIndex(1);
}

const TOOLS_INFO &toolController()
{
    return ToolRegistry::toolByIndex(2);
}

const TOOLS_INFO &toolMcpList()
{
    return ToolRegistry::toolByIndex(3);
}

const TOOLS_INFO &toolKnowledge()
{
    return ToolRegistry::toolByIndex(4);
}

const TOOLS_INFO &toolStableDiffusion()
{
    return ToolRegistry::toolByIndex(5);
}

const TOOLS_INFO &toolExecuteCommand()
{
    return ToolRegistry::toolByIndex(6);
}

const TOOLS_INFO &toolPtc()
{
    return ToolRegistry::toolByIndex(7);
}

const TOOLS_INFO &toolListFiles()
{
    return ToolRegistry::toolByIndex(8);
}

const TOOLS_INFO &toolSearchContent()
{
    return ToolRegistry::toolByIndex(9);
}

const TOOLS_INFO &toolReadFile()
{
    return ToolRegistry::toolByIndex(10);
}

const TOOLS_INFO &toolWriteFile()
{
    return ToolRegistry::toolByIndex(11);
}

const TOOLS_INFO &toolReplaceInFile()
{
    return ToolRegistry::toolByIndex(12);
}

const TOOLS_INFO &toolEditInFile()
{
    return ToolRegistry::toolByIndex(13);
}

const TOOLS_INFO &toolEngineerProxy()
{
    return ToolRegistry::toolByIndex(14);
}

const TOOLS_INFO &toolMonitor()
{
    return ToolRegistry::toolByIndex(15);
}

const TOOLS_INFO &toolSkillCall()
{
    return ToolRegistry::toolByIndex(16);
}

const TOOLS_INFO &toolSkillRun()
{
    return ToolRegistry::toolByIndex(17);
}

const TOOLS_INFO &toolScheduleTask()
{
    return ToolRegistry::toolByIndex(18);
}

const TOOLS_INFO &toolStatFile()
{
    return ToolRegistry::toolByIndex(19);
}

const TOOLS_INFO &toolCopyFile()
{
    return ToolRegistry::toolByIndex(20);
}

const TOOLS_INFO &toolArtifactConfirm()
{
    return ToolRegistry::toolByIndex(21);
}
} // namespace promptx
