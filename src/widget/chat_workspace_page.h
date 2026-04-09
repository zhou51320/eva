#ifndef CHAT_WORKSPACE_PAGE_H
#define CHAT_WORKSPACE_PAGE_H

#include <QFrame>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class ChatWorkspacePage;
}
QT_END_NAMESPACE

class ChatWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit ChatWorkspacePage(QWidget *parent = nullptr);
    ~ChatWorkspacePage() override;

    QFrame *headerHost() const;
    QFrame *sessionListHost() const;
    QFrame *messageHost() const;
    QFrame *composerHost() const;

  private:
    Ui::ChatWorkspacePage *ui;
};

#endif // CHAT_WORKSPACE_PAGE_H
