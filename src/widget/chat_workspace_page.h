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

    QFrame *topBarHost() const;
    QFrame *sessionMetaHost() const;
    QFrame *messageViewportHost() const;
    QFrame *composerCardHost() const;

  private:
    Ui::ChatWorkspacePage *ui;
};

#endif // CHAT_WORKSPACE_PAGE_H
