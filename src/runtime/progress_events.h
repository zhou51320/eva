#pragma once

#include <QJsonObject>
#include <QString>

namespace eva::runtime
{

enum class ProgressEventKind
{
    TaskStarted,
    PlanCreated,
    ToolStarted,
    ToolOutput,
    ToolFinished,
    Recovering,
    SkillLoading,
    SkillRunning,
    ArtifactReady,
    TaskCompleted,
    TaskFailed,
};

QString progressEventKindName(ProgressEventKind kind);
QJsonObject makeProgressEvent(ProgressEventKind kind,
                              const QString &summary,
                              const QJsonObject &data = QJsonObject{},
                              quint64 turnId = 0);
QString progressEventLine(ProgressEventKind kind,
                          const QString &summary,
                          const QJsonObject &data = QJsonObject{},
                          quint64 turnId = 0);

} // namespace eva::runtime
