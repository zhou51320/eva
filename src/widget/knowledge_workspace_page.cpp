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

QFrame *KnowledgeWorkspacePage::filterBarHost() const
{
    return ui->knowledgeFilterBar;
}

QFrame *KnowledgeWorkspacePage::collectionListHost() const
{
    return ui->knowledgeCollectionList;
}

QFrame *KnowledgeWorkspacePage::detailCardHost() const
{
    return ui->knowledgeDetailCard;
}

QFrame *KnowledgeWorkspacePage::statusCardHost() const
{
    return ui->knowledgeStatusCard;
}
