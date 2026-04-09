#include "engineer_workspace_page.h"

#include "ui_engineer_workspace_page.h"

EngineerWorkspacePage::EngineerWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::EngineerWorkspacePage)
{
    ui->setupUi(this);
}

EngineerWorkspacePage::~EngineerWorkspacePage()
{
    delete ui;
}

QFrame *EngineerWorkspacePage::projectSummaryCard() const
{
    return ui->projectSummaryCard;
}

QFrame *EngineerWorkspacePage::quickActionsCard() const
{
    return ui->quickActionsCard;
}

QFrame *EngineerWorkspacePage::recentContextCard() const
{
    return ui->recentContextCard;
}
