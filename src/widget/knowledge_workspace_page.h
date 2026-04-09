#ifndef KNOWLEDGE_WORKSPACE_PAGE_H
#define KNOWLEDGE_WORKSPACE_PAGE_H

#include <QFrame>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class KnowledgeWorkspacePage;
}
QT_END_NAMESPACE

class KnowledgeWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit KnowledgeWorkspacePage(QWidget *parent = nullptr);
    ~KnowledgeWorkspacePage() override;

    QFrame *sidebarHost() const;
    QFrame *listHost() const;
    QFrame *detailHost() const;

  private:
    Ui::KnowledgeWorkspacePage *ui;
};

#endif // KNOWLEDGE_WORKSPACE_PAGE_H
