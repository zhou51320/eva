#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "widget/ui_theme_tokens.h"

TEST_CASE("ui theme tokens fall back to default theme for empty theme id")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QString());

    CHECK(tokens.themeId == QStringLiteral("unit01"));
    CHECK(tokens.overlayResourcePath.isEmpty());
    CHECK(tokens.textPrimary == NORMAL_BLACK);
    CHECK(tokens.stateEva == SYSTEM_BLUE);
}

TEST_CASE("ui theme tokens fall back to default theme for unknown theme id")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QStringLiteral("mystery-theme"));

    CHECK(tokens.themeId == QStringLiteral("unit01"));
    CHECK(tokens.overlayResourcePath.isEmpty());
    CHECK(tokens.darkBase == false);
    CHECK(tokens.textSecondary == THINK_GRAY);
}

TEST_CASE("ui theme tokens keep resolved theme id aligned with fallback token values")
{
    const UiThemeTokens fallback = resolveUiThemeTokens(QStringLiteral("unknown"));
    const UiThemeTokens empty = resolveUiThemeTokens(QStringLiteral("   "));
    const UiThemeTokens base = resolveUiThemeTokens(QStringLiteral("unit01"));

    CHECK(fallback.themeId == base.themeId);
    CHECK(empty.themeId == base.themeId);
    CHECK(fallback.overlayResourcePath == base.overlayResourcePath);
    CHECK(empty.overlayResourcePath == base.overlayResourcePath);
    CHECK(fallback.textPrimary == base.textPrimary);
    CHECK(fallback.textSecondary == base.textSecondary);
    CHECK(fallback.stateSignal == base.stateSignal);
    CHECK(fallback.stateSuccess == base.stateSuccess);
    CHECK(fallback.stateWrong == base.stateWrong);
    CHECK(fallback.stateEva == base.stateEva);
    CHECK(fallback.stateTool == base.stateTool);
    CHECK(fallback.stateSync == base.stateSync);
    CHECK(fallback.systemRole == base.systemRole);
    CHECK(fallback.assistantRole == base.assistantRole);
    CHECK(empty.textPrimary == base.textPrimary);
    CHECK(empty.stateEva == base.stateEva);
}

TEST_CASE("ui theme tokens preserve known non-default themes")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QStringLiteral("unit02"));

    CHECK(tokens.themeId == QStringLiteral("unit02"));
    CHECK(tokens.overlayResourcePath == QStringLiteral(":/QSS/theme_unit02.qss"));
    CHECK(tokens.darkBase);
    CHECK(tokens.systemRole == tokens.stateSignal);
    CHECK(tokens.assistantRole == tokens.stateSync);
}
