#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace eva::runtime
{

QString toolErrorTypeForMessage(const QString &message);

QJsonObject makeToolError(const QString &type, const QString &message, const QJsonObject &details = QJsonObject{});
QJsonObject makeArtifact(const QString &path,
                         qint64 size = -1,
                         const QString &type = QString(),
                         const QString &label = QString());

QJsonObject makeToolResultEnvelope(bool ok,
                                   const QString &summary,
                                   const QJsonObject &data = QJsonObject{},
                                   const QJsonArray &artifacts = QJsonArray{},
                                   const QJsonArray &warnings = QJsonArray{},
                                   const QJsonObject &error = QJsonObject{},
                                   const QJsonArray &recoveryHints = QJsonArray{});

QString toolResultEnvelopeToString(const QJsonObject &envelope);
QString makeLegacyToolResultEnvelopeString(const QString &toolName,
                                           const QString &legacyText,
                                           bool ok = true,
                                           const QString &errorType = QString());

} // namespace eva::runtime
