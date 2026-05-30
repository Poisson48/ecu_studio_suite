#pragma once
#include <QWidget>
#include <QList>
#include <QString>
#include <QStackedWidget>

class QVBoxLayout;

namespace socketspy::gui {

class SidebarButton;

class SidebarNav : public QWidget {
    Q_OBJECT
public:
    explicit SidebarNav(QWidget* parent = nullptr);

    void addPanel(const QString& icon, const QString& label, QWidget* panel);
    void showPanel(QWidget* panel);
    void setPanelVisible(QWidget* panel, bool visible);
    int  panelCount() const;

    QStackedWidget* stack() const { return m_stack; }

signals:
    void currentPanelChanged(QWidget* panel);

private:
    void onButtonActivated(QWidget* panel);
    void onDetachRequested(QWidget* panel, const QString& title);
    void redock(QWidget* panel, int origIdx);

    struct Entry {
        QString        icon;
        QString        label;
        QWidget*       panel{nullptr};
        SidebarButton* btn{nullptr};
        bool           detached{false};
    };
    Entry* entryForPanel(QWidget* panel);

    QStackedWidget* m_stack{nullptr};
    QVBoxLayout*    m_btnLayout{nullptr};
    QList<Entry>    m_entries;

    friend class DetachedWindow;
};

} // namespace socketspy::gui
