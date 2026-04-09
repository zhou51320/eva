#include "app_shell.h"

#include "ui_app_shell.h"

#include <QFrame>
#include <QStackedWidget>

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
