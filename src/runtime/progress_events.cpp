#include "runtime/progress_events.h"

#include <QDateTime>
#include <QJsonDocument>

namespace eva::runtime
{

QString progressEventKindName(ProgressEventKind kind)
{
    switch (kind)
    {
    case ProgressEventKind::TaskStarted: return QStringLiteral("task_started");
    case ProgressEventKind::PlanCreated: return QStringLiteral("plan_created");
    case ProgressEventKind::ToolStarted: return QStringLiteral("tool_started");
    case ProgressEventKind::ToolOutput: return QStringLiteral("tool_output");
    case ProgressEventKind::ToolFinished: return QStringLiteral("tool_finished");
    case ProgressEventKind::Recovering: return QStringLiteral("recovering");
    case ProgressEventKind::SkillLoading: return QStringLiteral("skill_loading");
    case ProgressEventKind::SkillRunning: return QStringLiteral("skill_running");
    case ProgressEventKind::ArtifactReady: return QStringLiteral("artifact_ready");
    case ProgressEventKind::TaskCompleted: return QStringLiteral("task_completed");
    case ProgressEventKind::TaskFailed: return QStringLiteral("task_failed");
    }
    return QStringLiteral("unknown");
}

QJsonObject makeProgressEvent(ProgressEventKind kind,
                              const QString &summary,
                              const QJsonObject &data,
                              quint64 turnId)
{
    QJsonObject event;
    event.insert(QStringLiteral("type"), progressEventKindName(kind));
    event.insert(QStringLiteral("summary"), summary);
    event.insert(QStringLiteral("data"), data);
    event.insert(QStringLiteral("turn_id"), static_cast<qint64>(turnId));
    event.insert(QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    return event;
}

QString progressEventLine(ProgressEventKind kind,
                          const QString &summary,
                          const QJsonObject &data,
                          quint64 turnId)
{
    return QStringLiteral("progress:%1 %2")
        .arg(progressEventKindName(kind),
             QString::fromUtf8(QJsonDocument(makeProgressEvent(kind, summary, data, turnId)).toJson(QJsonDocument::Compact)));
}

} // namespace eva::runtime
