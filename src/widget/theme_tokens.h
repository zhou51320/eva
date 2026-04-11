#ifndef UI_THEME_TOKENS_H
#define UI_THEME_TOKENS_H

#include "../xconfig.h"

#include <QColor>
#include <QString>
#include <QStringList>

struct UiThemeTokens
{
    QString themeId = QStringLiteral("unit01");
    QString overlayResourcePath;
    bool darkBase = false;
    QColor textPrimary = NORMAL_BLACK;
    QColor textSecondary = THINK_GRAY;
    QColor stateSignal = SYSTEM_BLUE;
    QColor stateSuccess = QColor(0, 200, 0);
    QColor stateWrong = QColor(200, 0, 0);
    QColor stateEva = SYSTEM_BLUE;
    QColor stateTool = TOOL_BLUE;
    QColor stateSync = LCL_ORANGE;
    QColor systemRole = SYSTEM_BLUE;
    QColor assistantRole = LCL_ORANGE;
    QColor shellBackground = QColor("#F4F7FB");
    QColor cardBackground = QColor("#FFFFFF");
    QColor cardBorder = QColor("#E5E7EB");
    QColor accentSoft = QColor("#EFF6FF");
    QColor accentStrong = QColor("#2563EB");
    QColor messageUser = QColor("#EFF6FF");
    QColor messageAssistant = QColor("#F3F4F6");
    int shellRadius = 22;
    int cardRadius = 18;
    int pillRadius = 999;
};

inline QStringList buildThemeStylesheetPaths(const QString &themeId, bool useLegacyFallback)
{
    const QString trimmedId = themeId.trimmed();
    const QString effective = trimmedId.isEmpty() ? QStringLiteral("modern_light") : trimmedId;
    QStringList paths;
    paths << QStringLiteral(":/QSS/theme_%1.qss").arg(effective);
    if (useLegacyFallback)
    {
        paths << QStringLiteral(":/QSS/theme_%1_legacy.qss").arg(effective);
    }
    return paths;
}

inline UiThemeTokens resolveUiThemeTokens(const QString &themeId)
{
    const QString requestedThemeId = themeId.trimmed();

    if (requestedThemeId == QStringLiteral("unit00"))
    {
        UiThemeTokens tokens;
        tokens.themeId = QStringLiteral("unit00");
        tokens.overlayResourcePath = QStringLiteral(":/QSS/theme_unit00.qss");
        return tokens;
    }

    if (requestedThemeId == QStringLiteral("unit02"))
    {
        UiThemeTokens tokens;
        tokens.themeId = QStringLiteral("unit02");
        tokens.overlayResourcePath = QStringLiteral(":/QSS/theme_unit02.qss");
        tokens.darkBase = true;
        tokens.textPrimary = QColor("#ffe3d9");
        tokens.textSecondary = QColor("#ffbca7");
        tokens.stateSignal = QColor("#8dbdff");
        tokens.stateSuccess = QColor("#86ffb1");
        tokens.stateWrong = QColor("#ff9a9a");
        tokens.stateEva = QColor("#ffc6ff");
        tokens.stateTool = QColor("#7fd8ff");
        tokens.stateSync = QColor("#ffc67c");
        tokens.systemRole = tokens.stateSignal;
        tokens.assistantRole = tokens.stateSync;
        return tokens;
    }

    if (requestedThemeId == QStringLiteral("unit03"))
    {
        UiThemeTokens tokens;
        tokens.themeId = QStringLiteral("unit03");
        tokens.overlayResourcePath = QStringLiteral(":/QSS/theme_unit03.qss");
        tokens.darkBase = true;
        tokens.textPrimary = QColor("#e9edff");
        tokens.textSecondary = QColor("#b9c3ff");
        tokens.stateSignal = QColor("#9bb4ff");
        tokens.stateSuccess = QColor("#8dffd2");
        tokens.stateWrong = QColor("#ff9fc0");
        tokens.stateEva = QColor("#d8bdff");
        tokens.stateTool = QColor("#84ddff");
        tokens.stateSync = QColor("#ffd185");
        tokens.systemRole = tokens.stateSignal;
        tokens.assistantRole = tokens.stateSync;
        return tokens;
    }

    if (requestedThemeId == QStringLiteral("modern_light"))
    {
        UiThemeTokens tokens;
        tokens.themeId = QStringLiteral("modern_light");
        tokens.overlayResourcePath = QStringLiteral(":/QSS/theme_modern_light.qss");
        tokens.darkBase = false;
        tokens.textPrimary = QColor("#1f2937");
        tokens.textSecondary = QColor("#64748b");
        tokens.stateSignal = QColor("#2563eb");
        tokens.stateSuccess = QColor("#16a34a");
        tokens.stateWrong = QColor("#dc2626");
        tokens.stateEva = QColor("#2563eb");
        tokens.stateTool = QColor("#0ea5e9");
        tokens.stateSync = QColor("#f59e0b");
        tokens.systemRole = tokens.stateSignal;
        tokens.assistantRole = tokens.stateSync;
        tokens.shellBackground = QColor("#F4F7FB");
        tokens.cardBackground = QColor("#FFFFFF");
        tokens.cardBorder = QColor("#E5E7EB");
        tokens.accentSoft = QColor("#EFF6FF");
        tokens.accentStrong = QColor("#2563EB");
        tokens.messageUser = QColor("#EFF6FF");
        tokens.messageAssistant = QColor("#F3F4F6");
        tokens.shellRadius = 22;
        tokens.cardRadius = 18;
        tokens.pillRadius = 999;
        return tokens;
    }

    return resolveUiThemeTokens(QStringLiteral("modern_light"));
}

#endif // UI_THEME_TOKENS_H
