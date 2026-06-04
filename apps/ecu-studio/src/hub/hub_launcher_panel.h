#pragma once
// ─── HubLauncherPanel ────────────────────────────────────────────────────────
// Vue d'accueil de premier niveau du « hub » ECU Studio : une grille de tuiles,
// une par sous-programme spécialisé du catalogue (SubProgramRegistry::all()).
//
// Chaque tuile présente l'icône, le nom, la description et un MaturityBadge du
// sous-programme, plus un bouton « Lancer » qui démarre le binaire externe en
// sous-processus (pattern QProcess repris de CanPanel : un QProcess* membre par
// enfant en cours d'exécution, parenté à `this`, slot errorOccurred mettant à
// jour un QLabel de statut, garde state() != NotRunning).
//
// SocketSpy n'est qu'une tuile parmi les autres entrées du registre. Pour les
// tuiles pertinentes (sous-programmes « bus »), une affordance « Vérifier sur le
// bus » ouvre la VerifyOnBusDialog.
//
// Ce panel est le « puits d'intégration » des trois autres modules du hub
// (SubProgramRegistry, MaturityBadge, VerifyOnBusDialog) : il n'invente aucune
// API, il assemble.
#include <QWidget>
#include <QHash>
#include <QString>

#include "hub/sub_program_registry.h"   // SubProgram / SubProgramRegistry / Maturity

class QGridLayout;
class QLabel;
class QPushButton;
class QProcess;

namespace ecu_studio {

class RomDocument;
class ToolDownloader;

class HubLauncherPanel : public QWidget {
    Q_OBJECT
public:
    // Constructeur de commodité (main_window peut construire le panel avec son
    // seul parent). Délègue au constructeur principal avec un document nul.
    explicit HubLauncherPanel(QWidget* parent = nullptr);
    // Constructeur principal : reçoit le RomDocument partagé (utilisé pour
    // contextualiser la vérification sur bus quand une ROM est chargée).
    explicit HubLauncherPanel(RomDocument* doc, QWidget* parent = nullptr);
    ~HubLauncherPanel() override = default;

private:
    // Construit la grille de tuiles à partir de SubProgramRegistry::all().
    void buildUi();
    // Fabrique une tuile (cadre stylé) pour un sous-programme donné.
    QWidget* buildTile(const SubProgram& sp);

    // Lance le binaire d'un sous-programme en sous-processus (pattern CanPanel).
    void launch(const SubProgram& sp);
    // Télécharge la dernière release AppImage du sous-programme (downloadRepo)
    // dans le dossier des outils, puis réactive le lancement — sans compilation.
    void downloadTool(const SubProgram& sp);
    // Re-résout la disponibilité des binaires et met à jour boutons/tooltips.
    void refreshAvailability();
    // Ouvre la VerifyOnBusDialog pour un sous-programme « bus ».
    void verifyOnBus(const SubProgram& sp);

    // true si le sous-programme expose une affordance « Vérifier sur le bus ».
    static bool supportsBusVerify(const SubProgram& sp);

    void setStatus(const QString& msg);

    RomDocument* m_doc{nullptr};

    QGridLayout* m_grid{nullptr};
    QLabel*      m_statusLabel{nullptr};

    // Un QProcess* en cours d'exécution par identifiant de sous-programme,
    // parenté à `this`. Permet la garde state() != NotRunning par tuile.
    QHash<QString, QProcess*> m_procs;

    // Boutons « Lancer » / « Télécharger » par identifiant, pour basculer leur
    // état après un téléchargement réussi (sans reconstruire la grille).
    QHash<QString, QPushButton*> m_launchBtns;
    QHash<QString, QPushButton*> m_downloadBtns;

    // Téléchargeur partagé (un seul à la fois), créé paresseusement.
    ToolDownloader* m_downloader{nullptr};
};

} // namespace ecu_studio
