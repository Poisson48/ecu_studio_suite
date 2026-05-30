#pragma once
#include <QWidget>
#include <QString>
#include <memory>
#include "ecu/ProjectManager.hpp"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace ecu_studio {

class RomDocument;

// Panel « Projet » : gestion des projets ECU (création, import de ROM, ouverture).
// S'appuie sur ecu::ProjectManager pour la persistance et alimente le RomDocument
// partagé lorsqu'un projet est ouvert.
class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(RomDocument* doc, QWidget* parent = nullptr);
    // Surcharge de commodité (main_window construit le panel avec son parent
    // seul). Délègue au constructeur principal avec un document nul.
    explicit ProjectPanel(QWidget* parent = nullptr);
    ~ProjectPanel() override;

public slots:
    void newProject();   // affiche le formulaire de création
    void openProject();  // ouvre le projet sélectionné dans la liste

signals:
    // Émis quand un projet est ouvert : indique l'ECU associé pour que les
    // autres panels se synchronisent.
    void projectOpened(const QString& ecuId);

private:
    void buildUi();
    void refreshList();
    void showList();
    void showForm();
    void submitForm();
    void importRomFor(const QString& id);
    void openProjectById(const QString& id);
    QString selectedProjectId() const;

    RomDocument* m_doc{nullptr};
    std::unique_ptr<ecu::ProjectManager> m_manager;

    QStackedWidget* m_stack{nullptr};

    // Vue liste
    QListWidget* m_list{nullptr};
    QLabel*      m_emptyLabel{nullptr};
    QPushButton* m_newBtn{nullptr};
    QPushButton* m_openBtn{nullptr};
    QPushButton* m_importBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};

    // Vue formulaire
    QLineEdit* m_nameEdit{nullptr};
    QComboBox* m_ecuCombo{nullptr};
    QLineEdit* m_vehicleEdit{nullptr};
    QLineEdit* m_immatEdit{nullptr};
    QLineEdit* m_yearEdit{nullptr};
    QLabel*    m_formError{nullptr};
};

} // namespace ecu_studio
