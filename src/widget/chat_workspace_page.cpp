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

QFrame *ChatWorkspacePage::topBarHost() const
{
    return ui->chatTopBar;
}

QFrame *ChatWorkspacePage::sessionMetaHost() const
{
    return ui->chatSessionMetaBar;
}

QFrame *ChatWorkspacePage::messageViewportHost() const
{
    return ui->chatMessageViewport;
}

QFrame *ChatWorkspacePage::composerCardHost() const
{
    return ui->chatComposerCard;
}
