#include "settings_workspace_page.h"

#include "ui_settings_workspace_page.h"

SettingsWorkspacePage::SettingsWorkspacePage(QWidget *parent)
    : QWidget(parent), ui(new Ui::SettingsWorkspacePage)
{
    ui->setupUi(this);
}

SettingsWorkspacePage::~SettingsWorkspacePage()
{
    delete ui;
}

QLineEdit *SettingsWorkspacePage::searchEdit() const
{
    return ui->searchEdit;
}

QListWidget *SettingsWorkspacePage::categoryList() const
{
    return ui->categoryList;
}

QFrame *SettingsWorkspacePage::detailHost() const
{
    return ui->detailHost;
}
