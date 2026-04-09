#ifndef ENGINEER_WORKSPACE_PAGE_H
#define ENGINEER_WORKSPACE_PAGE_H

#include <QFrame>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class EngineerWorkspacePage;
}
QT_END_NAMESPACE

class EngineerWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit EngineerWorkspacePage(QWidget *parent = nullptr);
    ~EngineerWorkspacePage() override;

    QFrame *projectSummaryCard() const;
    QFrame *quickActionsCard() const;
    QFrame *recentContextCard() const;

  private:
    Ui::EngineerWorkspacePage *ui;
};

#endif // ENGINEER_WORKSPACE_PAGE_H
