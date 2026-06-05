#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace eva::runtime
{

struct ClassifiedFailure
{
    QString type = QStringLiteral("unknown");
    QString message;
    QJsonObject details;
    QJsonArray recoveryHints;
};

QString classifyFailureType(const QString &message, const QString &explicitType = QString());
QJsonArray recoveryHintsForFailure(const QString &type, const QJsonObject &details = QJsonObject{});
ClassifiedFailure classifyFailure(const QString &message,
                                  const QString &explicitType = QString(),
                                  const QJsonObject &details = QJsonObject{});
QJsonObject classifiedFailureToError(const ClassifiedFailure &failure);
QJsonObject repeatedFailureWarning(const QString &actionKey, bool repeated);
QJsonArray runtimeDiagnostics(const QStringList &runtimeNames);
QJsonObject artifactMissingRecoveryPlan(const QStringList &expectedOutputs, const QStringList &approvedSearchRoots);

} // namespace eva::runtime
