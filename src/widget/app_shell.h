#ifndef APP_SHELL_H
#define APP_SHELL_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QWidget>

class QPushButton;

#include <functional>

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

    using PageFactory = std::function<QWidget *(const QString &route, QWidget *parent)>;

    struct RouteSetupResult
    {
        QString currentRoute;
        QStringList registeredRoutes;
    };

    bool registerPage(const QString &route, QWidget *page);
    bool switchTo(const QString &route);
    QString currentRoute() const;
    QStringList registeredRoutes() const;

    RouteSetupResult ensurePlaceholderRoutes(const QStringList &routes,
                                             const QString &defaultRoute,
                                             const PageFactory &factory);

    QFrame *navRail() const;
    QFrame *sidePanel() const;
    QStackedWidget *workspaceStack() const;
    QFrame *contextDrawer() const;

  private:
    static QString normalizeRoute(const QString &route);

    Ui::AppShell *ui;
    QHash<QString, QWidget *> pages_;
    QHash<QString, QPushButton *> navButtons_;
    QString currentRoute_;
};

#endif // APP_SHELL_H
