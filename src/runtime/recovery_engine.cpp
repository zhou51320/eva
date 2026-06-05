#include "runtime/recovery_engine.h"

#include "runtime/tool_result_envelope.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace eva::runtime
{
namespace
{
QString cleanType(QString type)
{
    type = type.trimmed().toLower();
    if (type == QStringLiteral("network_proxy")) return QStringLiteral("network");
    if (type == QStringLiteral("dependency_missing")) return QStringLiteral("dependency");
    if (type == QStringLiteral("missing_artifact")) return QStringLiteral("artifact_missing");
    return type.isEmpty() ? QStringLiteral("unknown") : type;
}

void addHint(QJsonArray &hints,
             const QString &action,
             const QString &message,
             const QJsonObject &args = QJsonObject{})
{
    QJsonObject hint;
    hint.insert(QStringLiteral("action"), action);
    hint.insert(QStringLiteral("message"), message);
    if (!args.isEmpty()) hint.insert(QStringLiteral("args"), args);
    hints.append(hint);
}

QString canonicalRuntimeName(const QString &name)
{
    QString n = name.trimmed().toLower();
    if (n == QStringLiteral("nodejs")) return QStringLiteral("node");
    if (n == QStringLiteral("python3")) return QStringLiteral("python");
    return n;
}

QStringList executableCandidates(const QString &runtime)
{
    const QString n = canonicalRuntimeName(runtime);
    if (n == QStringLiteral("node")) return {QStringLiteral("node"), QStringLiteral("node.exe")};
    if (n == QStringLiteral("npm")) return {QStringLiteral("npm"), QStringLiteral("npm.cmd"), QStringLiteral("npm.exe")};
    if (n == QStringLiteral("python")) return {QStringLiteral("python3"), QStringLiteral("python"), QStringLiteral("py"), QStringLiteral("python.exe")};
    if (n == QStringLiteral("pip")) return {QStringLiteral("pip3"), QStringLiteral("pip"), QStringLiteral("pip.exe")};
    if (n == QStringLiteral("git")) return {QStringLiteral("git"), QStringLiteral("git.exe")};
    return {runtime.trimmed()};
}
} // namespace

QString classifyFailureType(const QString &message, const QString &explicitType)
{
    const QString explicitClean = cleanType(explicitType);
    if (explicitClean != QStringLiteral("unknown")) return explicitClean;

    const QString lower = message.toLower();
    if (lower.contains(QStringLiteral("timed out")) || lower.contains(QStringLiteral("timeout")) ||
        lower.contains(QStringLiteral("operation timed")))
        return QStringLiteral("timeout");
    if (lower.contains(QStringLiteral("permission denied")) || lower.contains(QStringLiteral("access denied")) ||
        lower.contains(QStringLiteral("not writable")) || lower.contains(QStringLiteral("read-only")) ||
        lower.contains(QStringLiteral("denied")))
        return QStringLiteral("permission");
    if (lower.contains(QStringLiteral("command not found")) || lower.contains(QStringLiteral("not recognized")) ||
        lower.contains(QStringLiteral("no such command")) || lower.contains(QStringLiteral("module not found")) ||
        lower.contains(QStringLiteral("no module named")) || lower.contains(QStringLiteral("cannot find module")) ||
        lower.contains(QStringLiteral("npm err")) || lower.contains(QStringLiteral("pip: command")) ||
        lower.contains(QStringLiteral("python: can't open file")))
        return QStringLiteral("dependency");
    if (lower.contains(QStringLiteral("syntaxerror")) || lower.contains(QStringLiteral("syntax error")) ||
        lower.contains(QStringLiteral("parse error")) || lower.contains(QStringLiteral("unexpected token")) ||
        lower.contains(QStringLiteral("invalid syntax")))
        return QStringLiteral("syntax");
    if (lower.contains(QStringLiteral("proxy")) || lower.contains(QStringLiteral("network")) ||
        lower.contains(QStringLiteral("tls")) || lower.contains(QStringLiteral("ssl")) ||
        lower.contains(QStringLiteral("certificate")) || lower.contains(QStringLiteral("cert")) ||
        lower.contains(QStringLiteral("econnreset")) || lower.contains(QStringLiteral("enotfound")) ||
        lower.contains(QStringLiteral("connection refused")))
        return QStringLiteral("network");
    if (lower.contains(QStringLiteral("codec")) || lower.contains(QStringLiteral("decode")) ||
        lower.contains(QStringLiteral("encoding")) || lower.contains(QStringLiteral("utf-8")) ||
        lower.contains(QStringLiteral("gbk")))
        return QStringLiteral("encoding");
    if (lower.contains(QStringLiteral("unsupported")) || lower.contains(QStringLiteral("not supported")) ||
        lower.contains(QStringLiteral("win7")) || lower.contains(QStringLiteral("windows 7")) ||
        lower.contains(QStringLiteral("requires windows")))
        return QStringLiteral("unsupported_platform");
    if ((lower.contains(QStringLiteral("artifact")) || lower.contains(QStringLiteral("expected output"))) &&
        (lower.contains(QStringLiteral("missing")) || lower.contains(QStringLiteral("not found"))))
        return QStringLiteral("artifact_missing");
    if (lower.contains(QStringLiteral("no such file")) || lower.contains(QStringLiteral("not found")) ||
        lower.contains(QStringLiteral("invalid path")) || lower.contains(QStringLiteral("cannot find the path")) ||
        lower.contains(QStringLiteral("file exists")) || lower.contains(QStringLiteral("directory missing")))
        return QStringLiteral("path");
    return QStringLiteral("unknown");
}

QJsonArray recoveryHintsForFailure(const QString &type, const QJsonObject &details)
{
    QJsonArray hints;
    const QString t = cleanType(type);
    if (t == QStringLiteral("path"))
    {
        addHint(hints, QStringLiteral("stat_path"), QStringLiteral("Use stat_file on the path or cwd before retrying command execution."));
        addHint(hints, QStringLiteral("list_parent"), QStringLiteral("List the parent directory or normalize the relative path to locate the intended target."));
        addHint(hints, QStringLiteral("copy_into_workspace"), QStringLiteral("If the source is an approved Skill asset, copy it into the workspace with copy_file before running commands."));
    }
    else if (t == QStringLiteral("dependency"))
    {
        addHint(hints, QStringLiteral("check_runtime"), QStringLiteral("Check runtime availability and version for node/npm/python/pip/git before retrying."));
        addHint(hints, QStringLiteral("install_local"), QStringLiteral("If installation is allowed, install into the workspace/run-local cache rather than a global location."));
        addHint(hints, QStringLiteral("report_blocker"), QStringLiteral("If the runtime is unavailable or unsupported, report a clear dependency blocker."));
    }
    else if (t == QStringLiteral("syntax"))
    {
        addHint(hints, QStringLiteral("inspect_source"), QStringLiteral("Read the file or command snippet around the syntax error and patch it before retrying."));
    }
    else if (t == QStringLiteral("permission"))
    {
        addHint(hints, QStringLiteral("check_allowed_roots"), QStringLiteral("Confirm the target is under the workspace, artifact output, temp run, or approved cache root."));
        addHint(hints, QStringLiteral("avoid_global_mutation"), QStringLiteral("Avoid mutating global/system locations unless the user explicitly approves it."));
    }
    else if (t == QStringLiteral("timeout"))
    {
        addHint(hints, QStringLiteral("inspect_partial_output"), QStringLiteral("Inspect stdout/stderr produced before timeout."));
        addHint(hints, QStringLiteral("change_strategy"), QStringLiteral("Retry only with a changed strategy, smaller scope, or explicit longer timeout."));
    }
    else if (t == QStringLiteral("network"))
    {
        addHint(hints, QStringLiteral("check_proxy"), QStringLiteral("Check HTTP_PROXY/HTTPS_PROXY/ALL_PROXY, TLS, certificates, and local network reachability."));
        addHint(hints, QStringLiteral("retry_with_proxy"), QStringLiteral("If a proxy is configured, retry dependency fetches with the proxy environment."));
    }
    else if (t == QStringLiteral("artifact_missing"))
    {
        addHint(hints, QStringLiteral("search_outputs"), QStringLiteral("Search approved output locations for the expected artifact before reporting success or failure."));
        addHint(hints, QStringLiteral("inspect_command_output"), QStringLiteral("Inspect command stdout/stderr for the actual output path."));
        addHint(hints, QStringLiteral("artifact_confirm"), QStringLiteral("Confirm the discovered artifact with artifact_confirm before claiming completion."));
    }
    else if (t == QStringLiteral("encoding"))
    {
        addHint(hints, QStringLiteral("preserve_diagnostics"), QStringLiteral("Retry with platform-local encoding or capture raw output so localized diagnostics remain readable."));
    }
    else if (t == QStringLiteral("unsupported_platform"))
    {
        addHint(hints, QStringLiteral("report_platform_blocker"), QStringLiteral("Report the platform blocker and list compatible prerequisites or alternatives."));
    }
    else
    {
        addHint(hints, QStringLiteral("change_strategy"), QStringLiteral("Inspect the observation and change strategy before retrying."));
    }

    if (!details.isEmpty())
    {
        QJsonObject contextHint;
        contextHint.insert(QStringLiteral("action"), QStringLiteral("inspect_details"));
        contextHint.insert(QStringLiteral("message"), QStringLiteral("Use the structured failure details to choose the next safe check."));
        contextHint.insert(QStringLiteral("args"), details);
        hints.append(contextHint);
    }
    return hints;
}

ClassifiedFailure classifyFailure(const QString &message, const QString &explicitType, const QJsonObject &details)
{
    ClassifiedFailure failure;
    failure.type = classifyFailureType(message, explicitType);
    failure.message = message;
    failure.details = details;
    failure.recoveryHints = recoveryHintsForFailure(failure.type, details);
    return failure;
}

QJsonObject classifiedFailureToError(const ClassifiedFailure &failure)
{
    return makeToolError(failure.type, failure.message, failure.details);
}

QJsonObject repeatedFailureWarning(const QString &actionKey, bool repeated)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("repeated"), repeated);
    obj.insert(QStringLiteral("action_key"), actionKey);
    if (repeated)
    {
        obj.insert(QStringLiteral("message"), QStringLiteral("This action repeats an unchanged failed command/tool call; inspect context or change strategy before retrying."));
        obj.insert(QStringLiteral("requires_new_evidence"), true);
    }
    return obj;
}

QJsonArray runtimeDiagnostics(const QStringList &runtimeNames)
{
    QJsonArray diagnostics;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &runtime : runtimeNames)
    {
        const QString name = canonicalRuntimeName(runtime);
        if (name.isEmpty()) continue;
        QJsonObject item;
        item.insert(QStringLiteral("name"), name);
        QString found;
        for (const QString &candidate : executableCandidates(name))
        {
            found = QStandardPaths::findExecutable(candidate);
            if (!found.isEmpty()) break;
        }
        item.insert(QStringLiteral("available"), !found.isEmpty());
        item.insert(QStringLiteral("path"), QDir::toNativeSeparators(found));
        if (name == QStringLiteral("node") || name == QStringLiteral("npm"))
            item.insert(QStringLiteral("win7_note"), QStringLiteral("Modern Node/npm releases may not support Windows 7; prefer a compatible portable/runtime-local version when needed."));
        if (name == QStringLiteral("python") || name == QStringLiteral("pip"))
            item.insert(QStringLiteral("win7_note"), QStringLiteral("Verify Python version and VC runtime compatibility on Windows 7."));
        diagnostics.append(item);
    }

    QJsonObject portablePolicy;
    portablePolicy.insert(QStringLiteral("name"), QStringLiteral("portable_runtime_policy"));
    portablePolicy.insert(QStringLiteral("mode"), QStringLiteral("diagnose_only"));
    portablePolicy.insert(QStringLiteral("message"), QStringLiteral("First implementation diagnoses Node/Python/helper availability and prefers workspace/run-local paths; it does not bundle portable runtimes yet."));
    diagnostics.append(portablePolicy);

    QJsonObject network;
    network.insert(QStringLiteral("name"), QStringLiteral("network_proxy"));
    network.insert(QStringLiteral("http_proxy_set"), env.contains(QStringLiteral("HTTP_PROXY")) || env.contains(QStringLiteral("http_proxy")));
    network.insert(QStringLiteral("https_proxy_set"), env.contains(QStringLiteral("HTTPS_PROXY")) || env.contains(QStringLiteral("https_proxy")));
    network.insert(QStringLiteral("all_proxy_set"), env.contains(QStringLiteral("ALL_PROXY")) || env.contains(QStringLiteral("all_proxy")));
    network.insert(QStringLiteral("tls_note"), QStringLiteral("Dependency fetch failures should surface proxy/TLS/certificate diagnostics instead of a generic command failure."));
    diagnostics.append(network);
    return diagnostics;
}

QJsonObject artifactMissingRecoveryPlan(const QStringList &expectedOutputs, const QStringList &approvedSearchRoots)
{
    QJsonObject plan;
    plan.insert(QStringLiteral("error_type"), QStringLiteral("artifact_missing"));
    plan.insert(QStringLiteral("expected_outputs"), QJsonArray::fromStringList(expectedOutputs));
    plan.insert(QStringLiteral("approved_search_roots"), QJsonArray::fromStringList(approvedSearchRoots));
    plan.insert(QStringLiteral("next_checks"), recoveryHintsForFailure(QStringLiteral("artifact_missing")));
    return plan;
}

} // namespace eva::runtime
