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

QFrame *EngineerWorkspacePage::listColumnHost() const
{
    return ui->engineerListColumn;
}

QFrame *EngineerWorkspacePage::summaryCardHost() const
{
    return ui->engineerSummaryCard;
}

QFrame *EngineerWorkspacePage::detailCardHost() const
{
    return ui->engineerTaskDetailCard;
}

QFrame *EngineerWorkspacePage::outputCardHost() const
{
    return ui->engineerOutputCard;
}
