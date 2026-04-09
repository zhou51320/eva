#include "app_shell.h"

#include "ui_app_shell.h"

#include <QFrame>
#include <QStackedWidget>

#include <algorithm>

AppShell::AppShell(QWidget *parent)
    : QWidget(parent), ui(new Ui::AppShell)
{
    ui->setupUi(this);
}

AppShell::~AppShell()
{
    delete ui;
}

QString AppShell::normalizeRoute(const QString &route)
{
    return route.trimmed();
}

bool AppShell::registerPage(const QString &route, QWidget *page)
{
    const QString normalizedRoute = normalizeRoute(route);
    if (normalizedRoute.isEmpty() || page == nullptr)
        return false;

    if (pages_.contains(normalizedRoute))
        return pages_.value(normalizedRoute) == page;

    if (ui->workspaceStack->indexOf(page) < 0)
        ui->workspaceStack->addWidget(page);

    pages_.insert(normalizedRoute, page);
    return true;
}

bool AppShell::switchTo(const QString &route)
{
    const QString normalizedRoute = normalizeRoute(route);
    QWidget *const page = pages_.value(normalizedRoute, nullptr);
    if (page == nullptr)
        return false;

    ui->workspaceStack->setCurrentWidget(page);
    currentRoute_ = normalizedRoute;
    return true;
}

QString AppShell::currentRoute() const
{
    return currentRoute_;
}

QStringList AppShell::registeredRoutes() const
{
    QStringList routes = pages_.keys();
    std::sort(routes.begin(), routes.end());
    return routes;
}

AppShell::RouteSetupResult AppShell::ensurePlaceholderRoutes(const QStringList &routes,
                                                             const QString &defaultRoute,
                                                             const PageFactory &factory)
{
    RouteSetupResult result;

    for (const QString &route : routes)
    {
        const QString normalizedRoute = normalizeRoute(route);
        if (normalizedRoute.isEmpty())
            continue;

        if (pages_.contains(normalizedRoute))
        {
            result.registeredRoutes.append(normalizedRoute);
            continue;
        }

        QWidget *page = factory ? factory(normalizedRoute, this) : nullptr;
        if (registerPage(normalizedRoute, page))
        {
            result.registeredRoutes.append(normalizedRoute);
            continue;
        }

        if (page && page->parent() == this)
            delete page;
    }

    const QString normalizedDefaultRoute = normalizeRoute(defaultRoute);
    if (!normalizedDefaultRoute.isEmpty())
        switchTo(normalizedDefaultRoute);

    result.currentRoute = currentRoute_;
    result.registeredRoutes.removeDuplicates();
    return result;
}

QFrame *AppShell::navRail() const
{
    return ui->navRail;
}

QFrame *AppShell::sidePanel() const
{
    return ui->sidePanel;
}

QStackedWidget *AppShell::workspaceStack() const
{
    return ui->workspaceStack;
}

QFrame *AppShell::contextDrawer() const
{
    return ui->contextDrawer;
}
