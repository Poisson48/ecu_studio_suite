#pragma once
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <memory>
#include "ecu/ProjectManager.hpp"

class QVBoxLayout;

namespace ecu_studio {

// Écran d'accueil affiché au premier lancement (et depuis le menu Aide). Présente
// les projets ECU récents (via ecu::ProjectManager) ainsi que les grandes actions
// de démarrage. Adapté du WelcomeScreen de SocketSpy au domaine ECU Studio.
class WelcomeScreen : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeScreen(QWidget* parent = nullptr);

    // false si l'utilisateur a coché « Ne plus afficher ».
    static bool shouldShow();

    void refreshRecentProjects();

signals:
    void newProjectRequested();
    void openRomRequested();
    void openProjectRequested();
    void openRecentProjectRequested(const QString& projectId);
    void scanMppsRequested();
    void languageChanged(const QString& code);

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onOpenSelectedProject();

private:
    void buildUi();
    QWidget* buildLeftPanel();
    QWidget* buildRightPanel();

    static QLabel* makeLink(const QString& icon, const QString& label,
                            const QString& url, QWidget* parent);

    std::unique_ptr<ecu::ProjectManager> m_manager;
    QListWidget* m_recentList{nullptr};
};

} // namespace ecu_studio
