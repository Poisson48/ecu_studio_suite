#include "project_panel.h"
#include "../rom_document.h"

#include "ecu/EcuCatalog.hpp"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ecu_studio {

namespace {
constexpr int IdRole = Qt::UserRole + 1;
constexpr int EcuRole = Qt::UserRole + 2;

QString projectsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/projects";
}
} // namespace

ProjectPanel::ProjectPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    const QString root = projectsRoot();
    QDir().mkpath(root);
    m_manager = std::make_unique<ecu::ProjectManager>(root);

    buildUi();
    refreshList();
    showList();
}

ProjectPanel::ProjectPanel(QWidget* parent)
    : ProjectPanel(nullptr, parent) {}

ProjectPanel::~ProjectPanel() = default;

void ProjectPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // ── Vue liste ──────────────────────────────────────────────────────────
    auto* listPage = new QWidget(this);
    auto* listLay  = new QVBoxLayout(listPage);
    listLay->setSpacing(8);
    listLay->setContentsMargins(0, 0, 0, 0);

    auto* listBox = new QGroupBox(tr("Projets"), listPage);
    auto* boxLay  = new QVBoxLayout(listBox);

    auto* toolRow = new QHBoxLayout;
    m_newBtn     = new QPushButton(tr("Nouveau projet"), listPage);
    m_newBtn->setObjectName("accentBtn");
    m_newBtn->setStyleSheet("QPushButton#accentBtn{background:#6366f1;color:#ffffff;"
                            "border:none;border-radius:4px;padding:6px 12px;font-weight:600;}"
                            "QPushButton#accentBtn:hover{background:#4f53d6;}");
    m_refreshBtn = new QPushButton(tr("Rafraîchir"), listPage);
    toolRow->addWidget(m_newBtn);
    toolRow->addStretch();
    toolRow->addWidget(m_refreshBtn);
    boxLay->addLayout(toolRow);

    m_list = new QListWidget(listPage);
    m_list->setStyleSheet("QListWidget{background:#111827;color:#e5e7eb;border:1px solid #1f2937;"
                          "border-radius:4px;}"
                          "QListWidget::item{padding:8px;border-bottom:1px solid #1f2937;}"
                          "QListWidget::item:selected{background:#1f2937;color:#ffffff;}");
    boxLay->addWidget(m_list, 1);

    m_emptyLabel = new QLabel(tr("Aucun projet — créez-en un avec « Nouveau projet »."), listPage);
    m_emptyLabel->setStyleSheet("color:#7c8fa6; font-size:13px;");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    boxLay->addWidget(m_emptyLabel);

    auto* actRow = new QHBoxLayout;
    m_openBtn   = new QPushButton(tr("Ouvrir"), listPage);
    m_openBtn->setObjectName("accentBtn");
    m_openBtn->setStyleSheet("QPushButton#accentBtn{background:#22c55e;color:#06210f;"
                             "border:none;border-radius:4px;padding:6px 12px;font-weight:600;}"
                             "QPushButton#accentBtn:hover{background:#1ea951;}"
                             "QPushButton#accentBtn:disabled{background:#1f2937;color:#4b5563;}");
    m_importBtn = new QPushButton(tr("Importer ROM..."), listPage);
    m_openBtn->setEnabled(false);
    m_importBtn->setEnabled(false);
    actRow->addWidget(m_openBtn);
    actRow->addWidget(m_importBtn);
    actRow->addStretch();
    boxLay->addLayout(actRow);

    listLay->addWidget(listBox, 1);
    m_stack->addWidget(listPage);

    // ── Vue formulaire ─────────────────────────────────────────────────────
    auto* formPage = new QWidget(this);
    auto* formOuter = new QVBoxLayout(formPage);
    formOuter->setSpacing(8);
    formOuter->setContentsMargins(0, 0, 0, 0);

    auto* formBox = new QGroupBox(tr("Nouveau projet"), formPage);
    auto* form    = new QFormLayout(formBox);
    form->setLabelAlignment(Qt::AlignRight);

    m_nameEdit = new QLineEdit(formPage);
    m_nameEdit->setPlaceholderText(tr("Nom du projet"));

    m_ecuCombo = new QComboBox(formPage);
    for (const auto& s : ecu::listEcus()) {
        const QString id   = QString::fromUtf8(s.id.data(), (qsizetype)s.id.size());
        const QString name = QString::fromUtf8(s.name.data(), (qsizetype)s.name.size());
        m_ecuCombo->addItem(name.isEmpty() ? id : QString("%1 (%2)").arg(name, id), id);
    }

    m_vehicleEdit = new QLineEdit(formPage);
    m_vehicleEdit->setPlaceholderText(tr("Marque / modèle"));
    m_immatEdit   = new QLineEdit(formPage);
    m_immatEdit->setPlaceholderText(tr("AB-123-CD"));
    m_yearEdit    = new QLineEdit(formPage);
    m_yearEdit->setPlaceholderText(tr("2018"));

    form->addRow(tr("Nom *"),        m_nameEdit);
    form->addRow(tr("ECU *"),        m_ecuCombo);
    form->addRow(tr("Véhicule"),     m_vehicleEdit);
    form->addRow(tr("Immatriculation"), m_immatEdit);
    form->addRow(tr("Année"),        m_yearEdit);

    m_formError = new QLabel(formPage);
    m_formError->setStyleSheet("color:#ef4444; font-size:12px;");
    m_formError->hide();
    form->addRow(QString(), m_formError);

    auto* formBtnRow = new QHBoxLayout;
    auto* cancelBtn  = new QPushButton(tr("Annuler"), formPage);
    auto* createBtn  = new QPushButton(tr("Créer"), formPage);
    createBtn->setObjectName("accentBtn");
    createBtn->setStyleSheet("QPushButton#accentBtn{background:#6366f1;color:#ffffff;"
                             "border:none;border-radius:4px;padding:6px 12px;font-weight:600;}"
                             "QPushButton#accentBtn:hover{background:#4f53d6;}");
    formBtnRow->addStretch();
    formBtnRow->addWidget(cancelBtn);
    formBtnRow->addWidget(createBtn);

    formOuter->addWidget(formBox);
    formOuter->addLayout(formBtnRow);
    formOuter->addStretch();
    m_stack->addWidget(formPage);

    // ── Connexions ─────────────────────────────────────────────────────────
    connect(m_newBtn,     &QPushButton::clicked, this, &ProjectPanel::newProject);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ProjectPanel::refreshList);
    connect(m_openBtn,    &QPushButton::clicked, this, &ProjectPanel::openProject);
    connect(m_importBtn,  &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) importRomFor(id);
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const bool has = !selectedProjectId().isEmpty();
        m_openBtn->setEnabled(has);
        m_importBtn->setEnabled(has);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        openProject();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &ProjectPanel::showList);
    connect(createBtn, &QPushButton::clicked, this, &ProjectPanel::submitForm);
}

void ProjectPanel::showList() {
    m_stack->setCurrentIndex(0);
}

void ProjectPanel::showForm() {
    m_nameEdit->clear();
    m_vehicleEdit->clear();
    m_immatEdit->clear();
    m_yearEdit->clear();
    if (m_ecuCombo->count() > 0) m_ecuCombo->setCurrentIndex(0);
    m_formError->hide();
    m_formError->clear();
    m_stack->setCurrentIndex(1);
    m_nameEdit->setFocus();
}

void ProjectPanel::refreshList() {
    m_list->clear();

    auto res = m_manager->list();
    if (!res) {
        m_emptyLabel->setText(tr("Erreur lecture des projets : %1").arg(res.error()));
        m_emptyLabel->show();
        m_list->hide();
        m_openBtn->setEnabled(false);
        m_importBtn->setEnabled(false);
        return;
    }

    const QList<ecu::ProjectMeta>& projects = *res;
    if (projects.isEmpty()) {
        m_emptyLabel->setText(tr("Aucun projet — créez-en un avec « Nouveau projet »."));
        m_emptyLabel->show();
        m_list->hide();
    } else {
        m_emptyLabel->hide();
        m_list->show();
        for (const auto& p : projects) {
            QString sub = p.ecu;
            if (!p.vehicle.isEmpty()) sub += " — " + p.vehicle;
            if (!p.immat.isEmpty())   sub += " — " + p.immat;
            if (!p.year.isEmpty())    sub += " (" + p.year + ")";
            const QString rom = p.hasRom
                ? tr("ROM: %1").arg(p.romName.isEmpty() ? tr("présente") : p.romName)
                : tr("ROM: aucune");

            auto* item = new QListWidgetItem(QString("%1\n%2  •  %3").arg(p.name, sub, rom), m_list);
            item->setData(IdRole, p.id);
            item->setData(EcuRole, p.ecu);
        }
        m_list->setCurrentRow(0);
    }

    const bool has = !selectedProjectId().isEmpty();
    m_openBtn->setEnabled(has);
    m_importBtn->setEnabled(has);
}

QString ProjectPanel::selectedProjectId() const {
    auto* item = m_list->currentItem();
    if (!item || m_list->isHidden()) return {};
    return item->data(IdRole).toString();
}

void ProjectPanel::newProject() {
    showForm();
}

void ProjectPanel::submitForm() {
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty()) {
        m_formError->setText(tr("Le nom du projet est obligatoire."));
        m_formError->show();
        m_nameEdit->setFocus();
        return;
    }
    if (m_ecuCombo->count() == 0 || m_ecuCombo->currentData().toString().isEmpty()) {
        m_formError->setText(tr("Sélectionnez un ECU."));
        m_formError->show();
        return;
    }

    ecu::CreateProjectParams params;
    params.name    = name;
    params.ecu     = m_ecuCombo->currentData().toString();
    params.vehicle = m_vehicleEdit->text().trimmed();
    params.immat   = m_immatEdit->text().trimmed();
    params.year    = m_yearEdit->text().trimmed();

    auto res = m_manager->create(params);
    if (!res) {
        m_formError->setText(tr("Création échouée : %1").arg(res.error()));
        m_formError->show();
        return;
    }

    showList();
    refreshList();

    // Sélectionne le projet fraîchement créé.
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == res->id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void ProjectPanel::openProject() {
    const QString id = selectedProjectId();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("Ouvrir projet"),
                                 tr("Sélectionnez d'abord un projet dans la liste."));
        return;
    }
    openProjectById(id);
}

void ProjectPanel::openProjectById(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Ouvrir projet"),
                             tr("Projet introuvable."));
        return;
    }

    auto path = m_manager->romPath(id);
    if (path && QFile::exists(*path)) {
        if (m_doc) {
            if (!m_doc->loadFromFile(*path)) {
                QMessageBox::warning(this, tr("Ouvrir projet"),
                                     tr("Impossible de charger la ROM : %1").arg(*path));
                return;
            }
            m_doc->setEcuId(meta->ecu);
        }
    } else {
        QMessageBox::information(this, tr("Ouvrir projet"),
            tr("Le projet « %1 » ne contient pas encore de ROM.\n"
               "Utilisez « Importer ROM... » pour en ajouter une.").arg(meta->name));
        // On signale tout de même l'ECU associé.
    }

    emit projectOpened(meta->ecu);
}

void ProjectPanel::importRomFor(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Importer ROM"), tr("Projet introuvable."));
        return;
    }

    const QString file = QFileDialog::getOpenFileName(
        this, tr("Importer une ROM"), {},
        tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Importer ROM"),
                             tr("Impossible d'ouvrir le fichier : %1").arg(file));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto res = m_manager->importRom(id, data, QFileInfo(file).fileName());
    if (!res) {
        QMessageBox::warning(this, tr("Importer ROM"),
                             tr("Import échoué : %1").arg(res.error()));
        return;
    }

    refreshList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

} // namespace ecu_studio
