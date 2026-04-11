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

QLineEdit *SettingsWorkspacePage::settingsSearchEdit() const
{
    return ui->settingsSearchEdit;
}

QListWidget *SettingsWorkspacePage::settingsCategoryList() const
{
    return ui->settingsCategoryList;
}

QFrame *SettingsWorkspacePage::detailCardHost() const
{
    return ui->settingsDetailCard;
}
