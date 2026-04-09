#ifndef MEDIA_WORKSPACE_PAGE_H
#define MEDIA_WORKSPACE_PAGE_H

#include <QFrame>
#include <QTabWidget>
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

    QTabWidget *toolTabs() const;
    QFrame *parameterHost() const;
    QFrame *previewHost() const;

  private:
    Ui::MediaWorkspacePage *ui;
};

#endif // MEDIA_WORKSPACE_PAGE_H
