#include "context_drawer.h"

#include "ui_context_drawer.h"

#include <QStackedWidget>

ContextDrawer::ContextDrawer(QWidget *parent)
    : QWidget(parent), ui(new Ui::ContextDrawer)
{
    ui->setupUi(this);
}

ContextDrawer::~ContextDrawer()
{
    delete ui;
}

QString ContextDrawer::normalizePanelName(const QString &name)
{
    return name.trimmed();
}

bool ContextDrawer::registerPanel(const QString &name, QWidget *panel)
{
    const QString normalizedName = normalizePanelName(name);
    if (normalizedName.isEmpty() || panel == nullptr)
        return false;

    if (panels_.contains(normalizedName))
        return panels_.value(normalizedName) == panel;

    if (ui->panelStack->indexOf(panel) < 0)
        ui->panelStack->addWidget(panel);

    panels_.insert(normalizedName, panel);
    return true;
}

bool ContextDrawer::showPanel(const QString &name)
{
    const QString normalizedName = normalizePanelName(name);
    QWidget *const panel = panels_.value(normalizedName, nullptr);
    if (panel == nullptr)
        return false;

    ui->panelStack->setCurrentWidget(panel);
    currentPanel_ = normalizedName;
    return true;
}

QString ContextDrawer::currentPanel() const
{
    return currentPanel_;
}

QStackedWidget *ContextDrawer::panelStack() const
{
    return ui->panelStack;
}
