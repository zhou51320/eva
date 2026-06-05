#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "service/tools/tool_registry.h"

TEST_CASE("ToolRegistry exposes stable capability metadata")
{
    ToolRegistry::setLanguage(EVA_LANG_EN);

    const auto &defs = ToolRegistry::entries();
    REQUIRE(defs.size() >= 10);

    const QJsonObject manifest = ToolRegistry::capabilityManifest();
    CHECK(manifest.value(QStringLiteral("manifest_version")).toInt() == 1);
    CHECK(manifest.value(QStringLiteral("tool_count")).toInt() == defs.size());

    const QJsonArray tools = manifest.value(QStringLiteral("tools")).toArray();
    CHECK(tools.size() == defs.size());
}

TEST_CASE("ToolRegistry capabilityByName is case-insensitive and includes risk metadata")
{
    ToolRegistry::setLanguage(EVA_LANG_EN);

    const QJsonObject executeCapability = ToolRegistry::capabilityByName(QStringLiteral("EXECUTE_COMMAND"));
    REQUIRE_FALSE(executeCapability.isEmpty());
    CHECK(executeCapability.value(QStringLiteral("name")).toString() == QStringLiteral("execute_command"));
    CHECK(executeCapability.value(QStringLiteral("schema_version")).toInt() >= 1);
    CHECK(executeCapability.value(QStringLiteral("timeout_ms")).toInt() >= 120000);
    CHECK(executeCapability.value(QStringLiteral("high_risk")).toBool());
    CHECK_FALSE(executeCapability.value(QStringLiteral("description")).toString().isEmpty());

    const QJsonObject calculatorCapability = ToolRegistry::capabilityByName(QStringLiteral("calculator"));
    REQUIRE_FALSE(calculatorCapability.isEmpty());
    CHECK(calculatorCapability.value(QStringLiteral("timeout_ms")).toInt() <= executeCapability.value(QStringLiteral("timeout_ms")).toInt());
    CHECK_FALSE(calculatorCapability.value(QStringLiteral("high_risk")).toBool());

    const QJsonObject missingCapability = ToolRegistry::capabilityByName(QStringLiteral("unknown_tool"));
    CHECK(missingCapability.isEmpty());
}


TEST_CASE("execute_command schema exposes runner v2 and Windows compatibility hints")
{
    ToolRegistry::setLanguage(EVA_LANG_EN);

    const QJsonObject executeCapability = ToolRegistry::capabilityByName(QStringLiteral("execute_command"));
    REQUIRE_FALSE(executeCapability.isEmpty());

    QJsonParseError err{};
    const QJsonDocument schemaDoc = QJsonDocument::fromJson(executeCapability.value(QStringLiteral("schema")).toString().toUtf8(), &err);
    REQUIRE(err.error == QJsonParseError::NoError);
    const QJsonObject props = schemaDoc.object().value(QStringLiteral("properties")).toObject();
    CHECK(props.contains(QStringLiteral("content")));
    CHECK(props.contains(QStringLiteral("command")));
    CHECK(props.contains(QStringLiteral("cwd")));
    CHECK(props.contains(QStringLiteral("env")));
    CHECK(props.contains(QStringLiteral("timeout_ms")));
    CHECK(props.contains(QStringLiteral("shell")));
    CHECK(props.contains(QStringLiteral("expected_outputs")));
    CHECK(props.contains(QStringLiteral("label")));

    const QString description = executeCapability.value(QStringLiteral("description")).toString();
    CHECK(description.contains(QStringLiteral("cwd")));
    CHECK(description.contains(QStringLiteral("env")));
    CHECK(description.contains(QStringLiteral("timeout_ms")));
    CHECK(description.contains(QStringLiteral("shell")));
    CHECK(description.contains(QStringLiteral("expected_outputs")));
    CHECK(description.contains(QStringLiteral("repeated_failed_command")));
}

TEST_CASE("workspace artifact tools expose structured schemas and descriptions")
{
    ToolRegistry::setLanguage(EVA_LANG_EN);

    const QStringList names = {
        QStringLiteral("stat_file"),
        QStringLiteral("copy_file"),
        QStringLiteral("artifact_confirm")
    };
    for (const QString &name : names)
    {
        const QJsonObject capability = ToolRegistry::capabilityByName(name);
        REQUIRE_FALSE(capability.isEmpty());
        CHECK(capability.value(QStringLiteral("schema_version")).toInt() >= 1);
        CHECK_FALSE(capability.value(QStringLiteral("description")).toString().isEmpty());

        QJsonParseError err{};
        const QJsonDocument schemaDoc = QJsonDocument::fromJson(capability.value(QStringLiteral("schema")).toString().toUtf8(), &err);
        REQUIRE(err.error == QJsonParseError::NoError);
        CHECK(schemaDoc.object().value(QStringLiteral("properties")).isObject());
    }

    const QJsonObject statCapability = ToolRegistry::capabilityByName(QStringLiteral("stat_file"));
    CHECK(statCapability.value(QStringLiteral("schema")).toString().contains(QStringLiteral("path")));
    CHECK(statCapability.value(QStringLiteral("description")).toString().contains(QStringLiteral("normalized path")));

    const QJsonObject copyCapability = ToolRegistry::capabilityByName(QStringLiteral("copy_file"));
    CHECK(copyCapability.value(QStringLiteral("schema")).toString().contains(QStringLiteral("source")));
    CHECK(copyCapability.value(QStringLiteral("schema")).toString().contains(QStringLiteral("destination")));
    CHECK(copyCapability.value(QStringLiteral("high_risk")).toBool());

    const QJsonObject artifactCapability = ToolRegistry::capabilityByName(QStringLiteral("artifact_confirm"));
    CHECK(artifactCapability.value(QStringLiteral("schema")).toString().contains(QStringLiteral("source_tool")));
    CHECK(artifactCapability.value(QStringLiteral("description")).toString().contains(QStringLiteral("open/download hints")));
}
