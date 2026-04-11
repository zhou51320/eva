#include "media_workspace_page.h"

#include "ui_media_workspace_page.h"

MediaWorkspacePage::MediaWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::MediaWorkspacePage)
{
    ui->setupUi(this);
}

MediaWorkspacePage::~MediaWorkspacePage()
{
    delete ui;
}

QFrame *MediaWorkspacePage::taskListColumnHost() const
{
    return ui->mediaTaskListColumn;
}

QFrame *MediaWorkspacePage::parameterCardHost() const
{
    return ui->mediaParameterCard;
}

QFrame *MediaWorkspacePage::previewCardHost() const
{
    return ui->mediaPreviewCard;
}

QFrame *MediaWorkspacePage::resultCardHost() const
{
    return ui->mediaResultCard;
}
