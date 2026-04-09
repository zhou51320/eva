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

QTabWidget *MediaWorkspacePage::toolTabs() const
{
    return ui->toolTabs;
}

QFrame *MediaWorkspacePage::parameterHost() const
{
    return ui->parameterHost;
}

QFrame *MediaWorkspacePage::previewHost() const
{
    return ui->previewHost;
}
