#ifndef MEDIA_WORKSPACE_PAGE_H
#define MEDIA_WORKSPACE_PAGE_H

#include <QFrame>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class MediaWorkspacePage;
}
QT_END_NAMESPACE

class MediaWorkspacePage : public QWidget
{
    Q_OBJECT

  public:
    explicit MediaWorkspacePage(QWidget *parent = nullptr);
    ~MediaWorkspacePage() override;

    QFrame *taskListColumnHost() const;
    QFrame *parameterCardHost() const;
    QFrame *previewCardHost() const;
    QFrame *resultCardHost() const;

  private:
    Ui::MediaWorkspacePage *ui;
};

#endif // MEDIA_WORKSPACE_PAGE_H
