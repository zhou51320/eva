#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <QHash>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class AppShell;
}
class QFrame;
class QStackedWidget;
QT_END_NAMESPACE

class AppShell : public QWidget
{
    Q_OBJECT

  public:
    explicit AppShell(QWidget *parent = nullptr);
    ~AppShell() override;

    bool registerPage(const QString &route, QWidget *page);
    bool switchTo(const QString &route);
    QString currentRoute() const;

    QFrame *navRail() const;
    QFrame *sidePanel() const;
    QStackedWidget *workspaceStack() const;
    QFrame *contextDrawer() const;

  private:
    static QString normalizeRoute(const QString &route);

    Ui::AppShell *ui;
    QHash<QString, QWidget *> pages_;
    QString currentRoute_;
};

#endif // APP_SHELL_H
