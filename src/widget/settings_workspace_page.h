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

    QLineEdit *searchEdit() const;
    QListWidget *categoryList() const;
    QFrame *detailHost() const;

  private:
    Ui::SettingsWorkspacePage *ui;
};

#endif // SETTINGS_WORKSPACE_PAGE_H
