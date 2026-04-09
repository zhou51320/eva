#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "widget/ui_theme_tokens.h"

TEST_CASE("ui theme tokens fall back to default theme for empty theme id")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QString());

    CHECK(tokens.themeId == QStringLiteral("modern_light"));
    CHECK(tokens.overlayResourcePath == QStringLiteral(":/QSS/theme_modern_light.qss"));
    CHECK(tokens.textPrimary == QColor("#1f2937"));
    CHECK(tokens.stateEva == QColor("#2563eb"));
}

TEST_CASE("ui theme tokens fall back to default theme for unknown theme id")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QStringLiteral("mystery-theme"));

    CHECK(tokens.themeId == QStringLiteral("modern_light"));
    CHECK(tokens.overlayResourcePath == QStringLiteral(":/QSS/theme_modern_light.qss"));
    CHECK(tokens.darkBase == false);
    CHECK(tokens.textSecondary == QColor("#64748b"));
}

TEST_CASE("ui theme tokens keep resolved theme id aligned with fallback token values")
{
    const UiThemeTokens fallback = resolveUiThemeTokens(QStringLiteral("unknown"));
    const UiThemeTokens empty = resolveUiThemeTokens(QStringLiteral("   "));
    const UiThemeTokens base = resolveUiThemeTokens(QStringLiteral("modern_light"));

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

TEST_CASE("theme stylesheet path includes legacy overlay when requested")
{
    CHECK(buildThemeStylesheetPaths(QStringLiteral("modern_light"), false) ==
          QStringList{QStringLiteral(":/QSS/theme_modern_light.qss")});
    CHECK(buildThemeStylesheetPaths(QStringLiteral("modern_light"), true) ==
          QStringList{QStringLiteral(":/QSS/theme_modern_light.qss"),
                      QStringLiteral(":/QSS/theme_modern_light_legacy.qss")});
}

TEST_CASE("ui theme tokens resolve modern light theme")
{
    const UiThemeTokens tokens = resolveUiThemeTokens(QStringLiteral("modern_light"));

    CHECK(tokens.themeId == QStringLiteral("modern_light"));
    CHECK(tokens.overlayResourcePath == QStringLiteral(":/QSS/theme_modern_light.qss"));
    CHECK_FALSE(tokens.darkBase);
}
