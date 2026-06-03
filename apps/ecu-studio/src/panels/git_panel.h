#pragma once
#include <QWidget>
#include <QString>
#include <memory>
#include "ecu/GitManager.hpp"

class QTableWidget;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QComboBox;

namespace ecu_studio {

class RomDocument;

// Panneau de versionnage Git : affiche l'historique des commits du dépôt du
// projet courant (le dossier qui contient rom.bin) et permet de committer
// l'état actuel ou de restaurer une version antérieure dans le RomDocument.
//
// S'appuie sur ecu::GitManager (libgit2). Lorsque libgit2 n'est pas disponible
// à la compilation (ECU_GIT_AVAILABLE non défini), le panneau se compile mais
// désactive toutes les actions et affiche un message d'indisponibilité.
class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(RomDocument* doc, QWidget* parent = nullptr);
    ~GitPanel() override;

public slots:
    // Définit le dossier du dépôt (celui qui contient rom.bin). Appelé par la
    // fenêtre principale à l'ouverture d'un projet. Chaîne vide = aucun projet.
    void setRepoPath(const QString& dir);

    // Recharge la liste des commits depuis le dépôt.
    void refresh();

    // Committe l'état courant du dépôt (demande un message).
    void commitCurrent();

    // Restaure le commit sélectionné puis recharge la ROM dans le document.
    void restoreSelected();

    // Recharge la liste des variantes (branches) et sélectionne la courante.
    void refreshVariants();

    // Bascule sur la variante sélectionnée dans le sélecteur et recharge l'état.
    void switchVariant(int index);

    // Demande un nom puis crée + bascule sur une nouvelle variante.
    void createVariant();

signals:
    void statusMessage(const QString& msg);

private:
    void buildUi();
    void updateActionStates();
    void setStatus(const QString& msg, bool error = false);
    QString selectedHash() const;

    // Recharge la ROM du dossier de travail dans le document (après bascule de
    // variante ou restauration). Renvoie true si le rechargement a réussi.
    bool reloadWorkingRom();

    RomDocument* m_doc{nullptr};
    QString      m_repoPath;

#ifdef ECU_GIT_AVAILABLE
    std::unique_ptr<ecu::GitManager> m_git;
#endif

    QLabel*          m_pathLabel{nullptr};
    QComboBox*       m_variantCombo{nullptr};
    QPushButton*     m_newVariantBtn{nullptr};
    QTableWidget*    m_table{nullptr};
    QPushButton*     m_commitBtn{nullptr};
    QPushButton*     m_restoreBtn{nullptr};
    QPushButton*     m_refreshBtn{nullptr};
    QLabel*          m_statusLabel{nullptr};
};

} // namespace ecu_studio
