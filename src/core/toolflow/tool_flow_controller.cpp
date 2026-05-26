#include "core/toolflow/tool_flow_controller.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>
#include <QStringList>
#include <QtGlobal>

#include <exception>

ToolFlowController::ToolFlowController(QObject *owner)
    : QObject(owner), host_(dynamic_cast<ToolFlowHostPort *>(owner))
{
}

ToolFlowHostPort *ToolFlowController::hostPort() const
{
    return host_;
}

void ToolFlowController::recvPushover()
{
    ToolFlowHostPort *host = hostPort();
    if (!host)
        return;

    host->flushToolFlowStream();
    host->clearToolFlowCallMarkers();
    host->syncToolFlowRuntimeState(true);
    const QJsonArray toolCallsSnapshot = host->takeToolFlowPendingCalls();
    // Separate all reasoning (<think>...</think>) blocks from final content; capture both roles
    QString finalText = host->assistantStreamText();
    const QString tBegin = QString(DEFAULT_THINK_BEGIN);
    const QString tEnd = QString(DEFAULT_THINK_END);
    QStringList reasonings;
    int searchPos = 0;
    while (true)
    {
        int s = finalText.indexOf(tBegin, searchPos);
        if (s == -1) break;
        int e = finalText.indexOf(tEnd, s + tBegin.size());
        if (e == -1) break; // unmatched tail -> leave as is
        const int rStart = s + tBegin.size();
        reasonings << finalText.mid(rStart, e - rStart);
        // remove the whole <think>...</think> segment from finalText
        finalText.remove(s, (e + tEnd.size()) - s);
        searchPos = s; // continue scanning from removal point
    }
    const QString reasoningText = reasonings.join("");
    // 压缩请求回合：将输出作为摘要处理，避免走常规 assistant/tool 链路
    if (host->compactionActiveForToolFlow())
    {
        host->handleToolFlowCompactionReply(finalText, reasoningText);
        host->setAssistantStreamText(QString());
        host->resetAssistantStreamIndexes();
        return;
    }
    // 重要：不要在这里打印 assistant_len/reasoning_len（字符长度），它很容易被误解为 token 数。
    // 链接模式下我们只关心 token 口径（来自 usage/timings 的校准结果，以及 UI 侧的 KV 汇总）。
    {
        host->logToolFlow(FlowPhase::NetDone,
                          QStringLiteral("tokens prompt=%1 gen=%2 reasoning=%3 turn=%4 used=%5")
                              .arg(qMax(0, host->promptTokensForToolFlow()))
                              .arg(qMax(0, host->generatedTokensForToolFlow()))
                              .arg(qMax(0, host->reasoningTokensForToolFlow()))
                              .arg(qMax(0, host->turnTokensForToolFlow()))
                              .arg(qMax(0, host->usedTokensForToolFlow())),
                          SIGNAL_SIGNAL);
    }
    // Persist assistant message (with optional reasoning) into UI/history
    QJsonObject roleMessage;
    roleMessage.insert("role", DEFAULT_MODEL_NAME);
    roleMessage.insert("content", finalText);
    if (!reasoningText.isEmpty())
    {
        roleMessage.insert("reasoning_content", reasoningText);
    }
    if (host->toolCallModeForToolFlow() == TOOL_CALL_FUNCTION && !toolCallsSnapshot.isEmpty())
    {
        roleMessage.insert(QStringLiteral("tool_calls"), toolCallsSnapshot);
    }
    if (host->engineerProxyActiveForToolFlow())
    {
        host->handleToolFlowEngineerAssistant(finalText, reasoningText);
        host->setAssistantStreamText(QString());
        host->resetAssistantStreamIndexes();
        return;
    }
    const int asstMsgIndex = host->appendToolFlowAssistantMessage(roleMessage);
    if (!reasoningText.isEmpty())
        host->bindToolFlowReasoningRecord(asstMsgIndex);
    host->bindToolFlowAssistantRecord(asstMsgIndex);
    host->publishToolFlowAssistantRecord(finalText, reasoningText, asstMsgIndex);

    // 输出区增强：若模型输出包含完整的 <tool_call>...</tool_call> 工具调用块，
    // 则在“流式输出结束”这一刻对该 assistant 记录块做一次重渲染：
    // - 让工具名/参数名/关键字段以更醒目的颜色显示（用户更容易看懂模型在调用什么）；
    // - 同时解决“<tool_call> JSON 跨 chunk 分片”导致的高亮不完整问题（这里用完整 finalText 重绘一次即可）。
    // 仅当包含 tool_call 标签时才触发，避免对普通对话输出产生额外开销。
    host->refreshToolFlowAssistantRecordIfNeeded(finalText);
    host->resetAssistantStreamIndexes();
    host->setAssistantStreamText(QString());

    if (host->completeModeForToolFlow()) // 补完模式的回答只输出一次
    {
        host->finishNormalToolFlow();
        host->resetConversationAfterToolFlowCompletion(); // 自动重置
    }
    else
    {
        // 工具链开关开启时，尝试解析工具 JSON
        if (host->toolsEnabledForToolFlow())
        {
            if (host->toolCallModeForToolFlow() == TOOL_CALL_FUNCTION)
            {
                auto parseArguments = [](const QJsonValue &value) -> mcp::json {
                    if (value.isObject())
                    {
                        const QJsonDocument doc(value.toObject());
                        const QByteArray bytes = doc.toJson(QJsonDocument::Compact);
                        try
                        {
                            mcp::json parsed = mcp::json::parse(bytes.constData(), bytes.constData() + bytes.size());
                            return parsed.is_object() ? parsed : mcp::json::object();
                        }
                        catch (const std::exception &)
                        {
                            return mcp::json::object();
                        }
                    }
                    if (value.isString())
                    {
                        const QByteArray bytes = value.toString().toUtf8();
                        try
                        {
                            mcp::json parsed = mcp::json::parse(bytes.constData(), bytes.constData() + bytes.size());
                            return parsed.is_object() ? parsed : mcp::json::object();
                        }
                        catch (const std::exception &)
                        {
                            return mcp::json::object();
                        }
                    }
                    return mcp::json::object();
                };

                if (toolCallsSnapshot.isEmpty())
                {
                    host->finishNormalToolFlow();
                }
                else
                {
                    QJsonObject callObj;
                    for (const auto &item : toolCallsSnapshot)
                    {
                        if (item.isObject())
                        {
                            callObj = item.toObject();
                            break;
                        }
                    }
                    if (callObj.isEmpty())
                    {
                        host->finishNormalToolFlow();
                    }
                    else
                    {
                        QString tools_name;
                        QJsonValue argsValue;
                        const QJsonValue functionVal = callObj.value(QStringLiteral("function"));
                        if (functionVal.isObject())
                        {
                            const QJsonObject functionObj = functionVal.toObject();
                            tools_name = functionObj.value(QStringLiteral("name")).toString();
                            argsValue = functionObj.value(QStringLiteral("arguments"));
                        }
                        else
                        {
                            tools_name = callObj.value(QStringLiteral("name")).toString();
                            argsValue = callObj.value(QStringLiteral("arguments"));
                        }
                        const QString toolCallId = callObj.value(QStringLiteral("id")).toString();

                        if (tools_name.isEmpty())
                        {
                            host->finishNormalToolFlow();
                        }
                        else
                        {
                            mcp::json call = mcp::json::object();
                            call["name"] = tools_name.toStdString();
                            call["arguments"] = parseArguments(argsValue);
                            host->setToolFlowCurrentCall(call, toolCallId);
                            host->rememberToolFlowPendingName(tools_name); // 保留工具名，供工具返回时附加截图等场景
                            host->syncToolFlowRuntimeState(true);
                            host->showToolFlowClicked(tools_name);
                            host->publishToolFlowStarted(tools_name);

                            // 记录区：工具“触发即显示”，不必等工具执行完成再出现记录块。
                            // - 这里只创建记录块（图标/徽标），不输出内容；
                            // - 工具返回时在 handleToolLoop() 中复用该记录块写入 tool_result。
                            host->createLegacyToolFlowRecordIfNeeded(tools_name);
                            // 工具层面指出结束
                            if (tools_name == QStringLiteral("system_engineer_proxy"))
                            {
                                host->startToolFlowEngineerProxy();
                                return;
                            }
                            if (tools_name == QStringLiteral("schedule_task"))
                            {
                                host->handleToolFlowScheduleCall();
                                return;
                            }
                            if (tools_name == QStringLiteral("answer") || tools_name == QStringLiteral("response"))
                            {
                                host->clearToolFlowPendingCallId();
                                host->syncToolFlowRuntimeState(true);
                                host->finishNormalToolFlow();
                            }
                            else
                            {
                                host->logToolFlow(FlowPhase::ToolParsed, QStringLiteral("name=%1").arg(tools_name), SIGNAL_SIGNAL);
                                host->markToolFlowAssistantHeaderReset();
                                host->setToolFlowInvocationActive(true);
                                host->emitToolFlowTurn(host->activeToolFlowTurnId());
                                host->logToolFlow(FlowPhase::ToolStart, QStringLiteral("name=%1").arg(tools_name), SIGNAL_SIGNAL);
                                host->emitToolFlowExec();
                                // use tool; decoding remains paused
                            }
                        }
                    }
                }
            }
            else
            {
                const QString tool_str = host->lastAssistantMessageTextForToolFlow();
                const mcp::json call = host->parseTextToolCallForToolFlow(tool_str);
                host->setToolFlowCurrentCall(call, QString());
                if (call.empty())
                {
                    host->finishNormalToolFlow();
                }
                else
                {
                    if (call.contains("name") && call.contains("arguments"))
                    {
                        QString tools_name = QString::fromStdString(call.value("name", ""));
                        host->rememberToolFlowPendingName(tools_name); // 保留工具名，供工具返回时附加截图等场景
                        host->syncToolFlowRuntimeState(true);
                        host->showToolFlowClicked(tools_name);
                        host->publishToolFlowStarted(tools_name);

                        // 记录区：工具“触发即显示”，不必等工具执行完成再出现记录块。
                        // - 这里只创建记录块（图标/徽标），不输出内容；
                        // - 工具返回时在 handleToolLoop() 中复用该记录块写入 tool_result。
                        host->createLegacyToolFlowRecordIfNeeded(tools_name);
                        // 工具层面指出结束
                        if (tools_name == QStringLiteral("system_engineer_proxy"))
                        {
                            host->startToolFlowEngineerProxy();
                            return;
                        }
                        if (tools_name == QStringLiteral("schedule_task"))
                        {
                            host->handleToolFlowScheduleCall();
                            return;
                        }
                        if (tools_name == QStringLiteral("answer") || tools_name == QStringLiteral("response"))
                        {
                            host->finishNormalToolFlow();
                        }
                        else
                        {
                            host->logToolFlow(FlowPhase::ToolParsed, QStringLiteral("name=%1").arg(tools_name), SIGNAL_SIGNAL);
                            host->markToolFlowAssistantHeaderReset();
                            host->setToolFlowInvocationActive(true);
                            host->emitToolFlowTurn(host->activeToolFlowTurnId());
                            host->logToolFlow(FlowPhase::ToolStart, QStringLiteral("name=%1").arg(tools_name), SIGNAL_SIGNAL);
                            host->emitToolFlowExec();
                            // use tool; decoding remains paused
                        }
                    }
                }
            }
        }
        else
        {
            host->finishNormalToolFlow();
        }
    }
    host->noteToolFlowBackendActivity();
    host->scheduleToolFlowLazyUnload();
}

void ToolFlowController::recvToolCalls(const QString &payload)
{
    ToolFlowHostPort *host = hostPort();
    if (!host || host->toolCallModeForToolFlow() != TOOL_CALL_FUNCTION)
    {
        return;
    }

    host->clearToolFlowPendingCalls();

    const QString trimmed = payload.trimmed();
    if (trimmed.isEmpty())
        return;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &err);
    QJsonArray calls;
    if (err.error == QJsonParseError::NoError)
    {
        if (doc.isArray())
        {
            calls = doc.array();
        }
        else if (doc.isObject())
        {
            const QJsonObject obj = doc.object();
            const QJsonValue toolCallsVal = obj.value(QStringLiteral("tool_calls"));
            const QJsonValue functionCallVal = obj.value(QStringLiteral("function_call"));
            if (toolCallsVal.isArray())
            {
                calls = toolCallsVal.toArray();
            }
            else if (functionCallVal.isObject())
            {
                calls.append(functionCallVal.toObject());
            }
            else if (!obj.isEmpty())
            {
                calls.append(obj);
            }
        }
    }

    if (!calls.isEmpty())
    {
        host->setToolFlowPendingCalls(calls);
    }
    else
    {
        qWarning() << "function_call tool_calls payload parse failed:" << err.errorString();
    }

    // 让工具调用信息作为模型输出展示
    host->flushToolFlowStream();
    QString displayText;
    if (!calls.isEmpty())
    {
        QJsonDocument outDoc(calls);
        displayText = QString::fromUtf8(outDoc.toJson(QJsonDocument::Compact));
    }
    else
    {
        displayText = trimmed;
    }
    if (displayText.isEmpty())
        return;
    host->renderToolFlowCallPayload(displayText);
}

void ToolFlowController::recvToolPushover(QString tool_result_)
{
    ToolFlowHostPort *host = hostPort();
    if (!host)
        return;
    host->setToolFlowResultFromRaw(tool_result_);
    host->logToolFlow(FlowPhase::ToolResult,
                      QStringLiteral("len=%1 images=%2").arg(host->toolFlowResult().size()).arg(host->toolFlowImageResultCount()),
                      SIGNAL_SIGNAL);
    host->syncToolFlowRuntimeState(true);
    host->publishToolFlowFinished();
    host->logToolFlow(FlowPhase::ContinueTurn, QStringLiteral("feed tool result to model"), SIGNAL_SIGNAL);

    if (host->engineerProxyActiveForToolFlow())
    {
        const QString observation = host->toolFlowResult();
        host->handleToolFlowEngineerObservation(observation);
        host->clearToolFlowResult();
        return;
    }

    host->continueToolFlowSend(); // 触发发送继续预测下一个词
}
