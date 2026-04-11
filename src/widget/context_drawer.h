#ifndef CONTEXT_DRAWER_H
#define CONTEXT_DRAWER_H

#include <QHash>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui
{
class ContextDrawer;
}
class QFrame;
class QStackedWidget;
QT_END_NAMESPACE

class ContextDrawer : public QWidget
{
    Q_OBJECT

  public:
    explicit ContextDrawer(QWidget *parent = nullptr);
    ~ContextDrawer() override;

    bool registerPanel(const QString &name, QWidget *panel);
    bool showPanel(const QString &name);
    QString currentPanel() const;
    QFrame *drawerHeader() const;
    QFrame *drawerControls() const;
    QStackedWidget *panelStack() const;

  private:
    static QString normalizePanelName(const QString &name);

    Ui::ContextDrawer *ui;
    QHash<QString, QWidget *> panels_;
    QString currentPanel_;
};

#endif // CONTEXT_DRAWER_H
