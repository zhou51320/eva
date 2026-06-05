#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <QStringLiteral>

#include "prompt.h"
#include "xconfig.h"

TEST_CASE("system prompt includes agent runtime protocol")
{
    REQUIRE(promptx::loadPromptLibrary());
    const QString system = promptx::systemPromptTemplate();
    CHECK(system.contains(QStringLiteral("[EVA Agent Runtime Protocol]")));
    CHECK(system.contains(QStringLiteral("understand the goal")));
    CHECK(system.contains(QStringLiteral("Completion policy")));
    CHECK(system.contains(QStringLiteral("Windows/Win7 policy")));
}

TEST_CASE("runtime protocol is not duplicated across reloads")
{
    REQUIRE(promptx::loadPromptLibrary());
    REQUIRE(promptx::loadPromptLibrary());
    const QString marker = QStringLiteral("[EVA Agent Runtime Protocol]");
    const QString system = promptx::systemPromptTemplate();
    CHECK(system.indexOf(marker) >= 0);
    CHECK(system.indexOf(marker) == system.lastIndexOf(marker));
}

TEST_CASE("prompt language switches runtime protocol text")
{
    promptx::setPromptLanguage(EVA_LANG_EN);
    QString system = promptx::systemPromptTemplate();
    QString wunder = promptx::wunderSystemPromptTemplate();
    CHECK(system.contains(QStringLiteral("understand the goal")));
    CHECK(wunder.contains(QStringLiteral("understand the goal")));
    CHECK_FALSE(system.contains(QStringLiteral("理解目标")));
    CHECK_FALSE(wunder.contains(QStringLiteral("理解目标")));

    promptx::setPromptLanguage(EVA_LANG_ZH);
    system = promptx::systemPromptTemplate();
    wunder = promptx::wunderSystemPromptTemplate();
    CHECK(system.contains(QStringLiteral("[EVA Agent Runtime Protocol]")));
    CHECK(wunder.contains(QStringLiteral("[EVA Agent Runtime Protocol]")));
    CHECK(system.contains(QStringLiteral("理解目标")));
    CHECK(wunder.contains(QStringLiteral("理解目标")));
    CHECK(system.contains(QStringLiteral("Windows/Win7 策略")));
    CHECK(wunder.contains(QStringLiteral("Windows/Win7 策略")));
    CHECK_FALSE(system.contains(QStringLiteral("understand the goal")));
    CHECK_FALSE(wunder.contains(QStringLiteral("understand the goal")));

    promptx::setPromptLanguage(EVA_LANG_EN);
    system = promptx::systemPromptTemplate();
    wunder = promptx::wunderSystemPromptTemplate();
    CHECK(system.contains(QStringLiteral("understand the goal")));
    CHECK(wunder.contains(QStringLiteral("understand the goal")));
    CHECK(system.contains(QStringLiteral("Windows/Win7 policy")));
    CHECK(wunder.contains(QStringLiteral("Windows/Win7 policy")));
    CHECK_FALSE(system.contains(QStringLiteral("理解目标")));
    CHECK_FALSE(wunder.contains(QStringLiteral("理解目标")));
}

TEST_CASE("tool protocol reminds verification before answer")
{
    promptx::setPromptLanguage(EVA_LANG_EN);
    const QString toolPrompt = promptx::extraPromptTemplate();
    CHECK(toolPrompt.contains(QStringLiteral("Prefer structured file/path/artifact tools")));
    CHECK(toolPrompt.contains(QStringLiteral("Do not call answer until")));
}
