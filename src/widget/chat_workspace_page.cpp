#include "chat_workspace_page.h"

#include "ui_chat_workspace_page.h"

ChatWorkspacePage::ChatWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ChatWorkspacePage)
{
    ui->setupUi(this);
}

ChatWorkspacePage::~ChatWorkspacePage()
{
    delete ui;
}

QFrame *ChatWorkspacePage::headerHost() const
{
    return ui->headerHost;
}

QFrame *ChatWorkspacePage::sessionListHost() const
{
    return ui->sessionListHost;
}

QFrame *ChatWorkspacePage::messageHost() const
{
    return ui->messageHost;
}

QFrame *ChatWorkspacePage::composerHost() const
{
    return ui->composerHost;
}
