#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringLiteral>

#include "runtime/recovery_engine.h"
#include "runtime/tool_result_envelope.h"

namespace
{
QJsonObject parseObject(const QString &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    REQUIRE(doc.isObject());
    return doc.object();
}
} // namespace

TEST_CASE("legacy tool result envelope preserves text in data")
{
    const QJsonObject obj = parseObject(eva::runtime::makeLegacyToolResultEnvelopeString(
        QStringLiteral("read_file"), QStringLiteral("hello")));
    CHECK(obj.value(QStringLiteral("ok")).toBool());
    CHECK(obj.value(QStringLiteral("summary")).toString() == QStringLiteral("hello"));
    CHECK(obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("legacy_text")).toString() == QStringLiteral("hello"));
    CHECK(obj.value(QStringLiteral("error")).isNull());
}

TEST_CASE("failed envelope classifies path errors and adds recovery hints")
{
    const QJsonObject obj = parseObject(eva::runtime::makeLegacyToolResultEnvelopeString(
        QStringLiteral("read_file"), QStringLiteral("No such file or directory"), false));
    CHECK_FALSE(obj.value(QStringLiteral("ok")).toBool());
    CHECK(obj.value(QStringLiteral("error")).toObject().value(QStringLiteral("type")).toString() == QStringLiteral("path"));
    CHECK(obj.value(QStringLiteral("recovery_hints")).toArray().size() >= 1);
}

TEST_CASE("recovery engine classifies common failure families")
{
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("Permission denied writing output")) == QStringLiteral("permission"));
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("SyntaxError: unexpected token")) == QStringLiteral("syntax"));
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("TLS certificate verify failed behind proxy")) == QStringLiteral("network"));
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("Artifact missing: deck.pptx")) == QStringLiteral("artifact_missing"));
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("Windows 7 is not supported by this Node runtime")) == QStringLiteral("unsupported_platform"));
    CHECK(eva::runtime::classifyFailureType(QStringLiteral("GBK decode failed")) == QStringLiteral("encoding"));
    CHECK(eva::runtime::recoveryHintsForFailure(QStringLiteral("path")).size() >= 2);
    CHECK(eva::runtime::repeatedFailureWarning(QStringLiteral("cmd"), true).value(QStringLiteral("requires_new_evidence")).toBool());
}

TEST_CASE("win7 runtime diagnostics include runtimes and proxy/tls context")
{
    const QJsonArray diagnostics = eva::runtime::runtimeDiagnostics(QStringList{QStringLiteral("node"), QStringLiteral("npm"), QStringLiteral("python"), QStringLiteral("pip"), QStringLiteral("git")});
    CHECK(diagnostics.size() >= 6);
    bool sawNode = false;
    bool sawProxy = false;
    for (const QJsonValue &value : diagnostics)
    {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("name")).toString() == QStringLiteral("node"))
        {
            sawNode = true;
            CHECK(item.contains(QStringLiteral("available")));
            CHECK(item.value(QStringLiteral("win7_note")).toString().contains(QStringLiteral("Windows 7")));
        }
        if (item.value(QStringLiteral("name")).toString() == QStringLiteral("network_proxy"))
        {
            sawProxy = true;
            CHECK(item.value(QStringLiteral("tls_note")).toString().contains(QStringLiteral("certificate")));
        }
    }
    CHECK(sawNode);
    CHECK(sawProxy);
}
