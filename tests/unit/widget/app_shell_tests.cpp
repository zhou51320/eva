#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <QApplication>
#include <QLabel>

#include "widget/app_shell.h"

namespace
{
QApplication *ensureQtApp()
{
    static int argc = 0;
    static char **argv = nullptr;
    static QApplication app(argc, argv);
    return &app;
}
} // namespace

TEST_CASE("AppShell exposes the four main shell containers")
{
    ensureQtApp();

    AppShell shell;

    CHECK(shell.navRail() != nullptr);
    CHECK(shell.sidePanel() != nullptr);
    CHECK(shell.workspaceStack() != nullptr);
    CHECK(shell.contextDrawer() != nullptr);
}

TEST_CASE("AppShell normalizes routes consistently across registration and switching")
{
    ensureQtApp();

    AppShell shell;
    QWidget chatPage;
    QWidget engineerPage;

    CHECK(shell.currentRoute().isEmpty());

    CHECK_FALSE(shell.registerPage(QStringLiteral("   "), &chatPage));
    CHECK_FALSE(shell.registerPage(QStringLiteral("chat"), nullptr));

    CHECK(shell.registerPage(QStringLiteral(" chat "), &chatPage));
    CHECK(shell.registerPage(QStringLiteral("engineer"), &engineerPage));
    CHECK(shell.registerPage(QStringLiteral("chat"), &chatPage));
    CHECK_FALSE(shell.registerPage(QStringLiteral("chat"), &engineerPage));

    CHECK(shell.switchTo(QStringLiteral("chat")));
    CHECK(shell.currentRoute() == QStringLiteral("chat"));
    CHECK(shell.workspaceStack()->currentWidget() == &chatPage);

    CHECK(shell.switchTo(QStringLiteral(" engineer ")));
    CHECK(shell.currentRoute() == QStringLiteral("engineer"));
    CHECK(shell.workspaceStack()->currentWidget() == &engineerPage);

    CHECK_FALSE(shell.switchTo(QStringLiteral(" missing ")));
    CHECK(shell.currentRoute() == QStringLiteral("engineer"));
    CHECK(shell.workspaceStack()->currentWidget() == &engineerPage);
}
