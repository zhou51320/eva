#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include "service/net/request_snapshot.h"

// 运行层网络驱动接口。
// 约束：EvaRuntime 只依赖这个抽象信号/槽，不直接包含 NetClient/xNet，
// 这样 Widget、ACP 或未来服务端入口可以替换不同网络实现。
class RuntimeNetworkDriver : public QObject
{
    Q_OBJECT

  public:
    explicit RuntimeNetworkDriver(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    ~RuntimeNetworkDriver() override = default;

  public slots:
    virtual void send(const RequestSnapshot &snapshot) = 0;
    virtual void stop(bool stop) = 0;

  signals:
    void net2ui_tool_calls(const QString &payload);
    void net2ui_state(const QString &state_string, SIGNAL_STATE state = USUAL_SIGNAL);
    void net2ui_output(const QString &result, bool is_while = true, QColor color = QColor(0, 0, 0));
    void net2ui_pushover();
    void net2ui_kv_tokens(int usedTokens);
    void net2ui_prompt_baseline(int promptTokens);
    void net2ui_slot_id(int slotId);
    void net2ui_reasoning_tokens(int count);
    void net2ui_speeds(double prompt_per_second, double predicted_per_second);
    void net2ui_turn_counters(int cacheTokens, int promptTokens, int predictedTokens);
};

Q_DECLARE_METATYPE(RuntimeNetworkDriver *)
