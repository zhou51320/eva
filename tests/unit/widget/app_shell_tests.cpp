#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <QApplication>
#include <QLabel>

#include "widget/app_shell.h"
#include "widget/context_drawer.h"
#include "widget/chat_workspace_page.h"

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

TEST_CASE("AppShell placeholder route setup keeps default chat route")
{
    ensureQtApp();

    AppShell shell;
    const QStringList routes = {
        QStringLiteral("chat"),
        QStringLiteral("engineer"),
        QStringLiteral("knowledge"),
        QStringLiteral("media"),
        QStringLiteral("settings")};

    const auto result = shell.ensurePlaceholderRoutes(
        routes,
        QStringLiteral("chat"),
        [](const QString &route, QWidget *parent) -> QWidget * {
            QLabel *label = new QLabel(route, parent);
            label->setObjectName(route + QStringLiteral("PrimaryPlaceholder"));
            label->setAlignment(Qt::AlignCenter);
            return label;
        });

    CHECK(result.currentRoute == QStringLiteral("chat"));
    CHECK(result.registeredRoutes == routes);
    CHECK(shell.currentRoute() == QStringLiteral("chat"));

    for (const QString &route : routes)
    {
        INFO(route.toStdString());
        CHECK(shell.switchTo(route));
        QWidget *page = shell.workspaceStack()->currentWidget();
        REQUIRE(page != nullptr);
        CHECK(page->objectName() == route + QStringLiteral("PrimaryPlaceholder"));
    }
}

TEST_CASE("AppShell placeholder setup reports only successfully registered routes")
{
    ensureQtApp();

    AppShell shell;
    QWidget existingChat;
    REQUIRE(shell.registerPage(QStringLiteral("chat"), &existingChat));

    const auto result = shell.ensurePlaceholderRoutes(
        {QStringLiteral(" chat "), QStringLiteral("missing"), QStringLiteral("engineer")},
        QStringLiteral("chat"),
        [](const QString &route, QWidget *parent) -> QWidget * {
            if (route == QStringLiteral("missing"))
                return nullptr;
            return new QLabel(route, parent);
        });

    CHECK(result.currentRoute == QStringLiteral("chat"));
    CHECK(result.registeredRoutes == QStringList{QStringLiteral("chat"), QStringLiteral("engineer")});
    CHECK_FALSE(shell.switchTo(QStringLiteral("missing")));
}

TEST_CASE("ContextDrawer switches engineering panels")
{
    ensureQtApp();

    ContextDrawer drawer;
    QWidget environmentPanel;
    QWidget skillsPanel;

    CHECK(drawer.registerPanel(QStringLiteral("environment"), &environmentPanel));
    CHECK(drawer.registerPanel(QStringLiteral("skills"), &skillsPanel));

    CHECK(drawer.showPanel(QStringLiteral("environment")));
    CHECK(drawer.currentPanel() == QStringLiteral("environment"));
    CHECK(drawer.panelStack()->currentWidget() == &environmentPanel);

    CHECK(drawer.showPanel(QStringLiteral("skills")));
    CHECK(drawer.currentPanel() == QStringLiteral("skills"));
    CHECK(drawer.panelStack()->currentWidget() == &skillsPanel);

    CHECK_FALSE(drawer.showPanel(QStringLiteral("missing")));
    CHECK(drawer.currentPanel() == QStringLiteral("skills"));
}

TEST_CASE("ChatWorkspacePage exposes header and composer hosts")
{
    ensureQtApp();

    ChatWorkspacePage page;

    REQUIRE(page.headerHost() != nullptr);
    REQUIRE(page.sessionListHost() != nullptr);
    REQUIRE(page.messageHost() != nullptr);
    REQUIRE(page.composerHost() != nullptr);
}
