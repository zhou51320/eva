#include "runtime/tool_result_envelope.h"

#include "runtime/recovery_engine.h"

#include <QFileInfo>
#include <QJsonDocument>

namespace eva::runtime
{
namespace
{
QString normalizedErrorType(QString type)
{
    type = type.trimmed();
    return type.isEmpty() ? QStringLiteral("unknown") : type;
}

QJsonArray defaultRecoveryHints(const QString &type)
{
    return recoveryHintsForFailure(type);
}
} // namespace

QString toolErrorTypeForMessage(const QString &message)
{
    return classifyFailureType(message);
}

QJsonObject makeToolError(const QString &type, const QString &message, const QJsonObject &details)
{
    QJsonObject error;
    error.insert(QStringLiteral("type"), normalizedErrorType(type));
    error.insert(QStringLiteral("message"), message);
    if (!details.isEmpty()) error.insert(QStringLiteral("details"), details);
    return error;
}

QJsonObject makeArtifact(const QString &path, qint64 size, const QString &type, const QString &label)
{
    QJsonObject artifact;
    artifact.insert(QStringLiteral("path"), path);
    artifact.insert(QStringLiteral("normalized_path"), QFileInfo(path).absoluteFilePath());
    if (size >= 0) artifact.insert(QStringLiteral("size"), QString::number(size));
    if (!type.trimmed().isEmpty()) artifact.insert(QStringLiteral("type"), type.trimmed());
    if (!label.trimmed().isEmpty()) artifact.insert(QStringLiteral("label"), label.trimmed());
    return artifact;
}

QJsonObject makeToolResultEnvelope(bool ok,
                                   const QString &summary,
                                   const QJsonObject &data,
                                   const QJsonArray &artifacts,
                                   const QJsonArray &warnings,
                                   const QJsonObject &error,
                                   const QJsonArray &recoveryHints)
{
    QJsonObject envelope;
    envelope.insert(QStringLiteral("ok"), ok);
    envelope.insert(QStringLiteral("summary"), summary);
    envelope.insert(QStringLiteral("data"), data);
    envelope.insert(QStringLiteral("artifacts"), artifacts);
    envelope.insert(QStringLiteral("warnings"), warnings);
    envelope.insert(QStringLiteral("error"), error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(error));
    envelope.insert(QStringLiteral("recovery_hints"), recoveryHints);
    return envelope;
}

QString toolResultEnvelopeToString(const QJsonObject &envelope)
{
    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

QString makeLegacyToolResultEnvelopeString(const QString &toolName,
                                           const QString &legacyText,
                                           bool ok,
                                           const QString &errorType)
{
    QJsonObject data;
    data.insert(QStringLiteral("tool"), toolName);
    data.insert(QStringLiteral("legacy_text"), legacyText);

    const QString summary = legacyText.left(240).trimmed();
    QJsonObject error;
    QJsonArray hints;
    if (!ok)
    {
        const QString type = normalizedErrorType(errorType.isEmpty() ? toolErrorTypeForMessage(legacyText) : errorType);
        error = makeToolError(type, legacyText.left(2000));
        hints = defaultRecoveryHints(type);
    }
    return toolResultEnvelopeToString(makeToolResultEnvelope(ok, summary, data, QJsonArray{}, QJsonArray{}, error, hints));
}

} // namespace eva::runtime
