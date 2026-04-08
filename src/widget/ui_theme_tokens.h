#ifndef UI_THEME_TOKENS_H
#define UI_THEME_TOKENS_H

#include "../xconfig.h"

#include <QColor>
#include <QString>

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
};

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

    return UiThemeTokens();
}

#endif // UI_THEME_TOKENS_H
