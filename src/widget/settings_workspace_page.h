#ifndef SETTINGS_WORKSPACE_PAGE_H
#define SETTINGS_WORKSPACE_PAGE_H

#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class SettingsWorkspacePage;
}
QT_END_NAMESPACE

class SettingsWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit SettingsWorkspacePage(QWidget *parent = nullptr);
    ~SettingsWorkspacePage() override;

    QLineEdit *settingsSearchEdit() const;
    QListWidget *settingsCategoryList() const;
    QFrame *detailCardHost() const;

  private:
    Ui::SettingsWorkspacePage *ui;
};

#endif // SETTINGS_WORKSPACE_PAGE_H
