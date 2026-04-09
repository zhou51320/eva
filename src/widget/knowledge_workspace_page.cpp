#include "knowledge_workspace_page.h"

#include "ui_knowledge_workspace_page.h"

KnowledgeWorkspacePage::KnowledgeWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::KnowledgeWorkspacePage)
{
    ui->setupUi(this);
}

KnowledgeWorkspacePage::~KnowledgeWorkspacePage()
{
    delete ui;
}

QFrame *KnowledgeWorkspacePage::sidebarHost() const
{
    return ui->sidebarHost;
}

QFrame *KnowledgeWorkspacePage::listHost() const
{
    return ui->listHost;
}

QFrame *KnowledgeWorkspacePage::detailHost() const
{
    return ui->detailHost;
}
