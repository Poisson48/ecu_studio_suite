#include "project_panel.h"
#include "../rom_document.h"

#include "ecu/EcuCatalog.hpp"

#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonObject>
#include <QTextEdit>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace ecu_studio {

namespace {
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
    m_search = new QLineEdit(listPage);
    m_search->setPlaceholderText(tr("Rechercher (nom, ECU, véhicule, immat)…"));
    m_search->setClearButtonEnabled(true);
    m_refreshBtn = new QPushButton(tr("Rafraîchir"), listPage);
    toolRow->addWidget(m_newBtn);
    toolRow->addSpacing(10);
    toolRow->addWidget(m_search, 1);
    toolRow->addWidget(m_refreshBtn);
    boxLay->addLayout(toolRow);

    // Splitter : liste à gauche, carte de détail + slots ROM à droite.
    auto* split = new QSplitter(Qt::Horizontal, listPage);

    auto* leftWrap = new QWidget(listPage);
    auto* leftLay  = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);

    m_list = new QListWidget(leftWrap);
    m_list->setStyleSheet("QListWidget{background:#111827;color:#e5e7eb;border:1px solid #1f2937;"
                          "border-radius:4px;}"
                          "QListWidget::item{padding:8px;border-bottom:1px solid #1f2937;}"
                          "QListWidget::item:selected{background:#1f2937;color:#ffffff;}");
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLay->addWidget(m_list, 1);

    m_emptyLabel = new QLabel(tr("Aucun projet — créez-en un avec « Nouveau projet »."), leftWrap);
    m_emptyLabel->setStyleSheet("color:#7c8fa6; font-size:13px;");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->hide();
    leftLay->addWidget(m_emptyLabel);
    split->addWidget(leftWrap);

    // Carte de détail enrichie.
    auto* detailWrap = new QWidget(listPage);
    auto* detailLay  = new QVBoxLayout(detailWrap);
    detailLay->setContentsMargins(0, 0, 0, 0);
    detailLay->setSpacing(6);

    m_detailCard = new QLabel(tr("Sélectionnez un projet."), detailWrap);
    m_detailCard->setTextFormat(Qt::RichText);
    m_detailCard->setWordWrap(true);
    m_detailCard->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_detailCard->setStyleSheet("background:#0d1320; border:1px solid #1f2937; border-radius:6px;"
                                "padding:10px; color:#e5e7eb; font-size:12px;");
    detailLay->addWidget(m_detailCard);

    auto* slotsLbl = new QLabel(tr("ROMs du projet"), detailWrap);
    slotsLbl->setStyleSheet("color:#9ca3af; font-size:12px; font-weight:600;");
    detailLay->addWidget(slotsLbl);

    m_slotTree = new QTreeWidget(detailWrap);
    m_slotTree->setColumnCount(3);
    m_slotTree->setHeaderLabels({ tr("ROM"), tr("Taille"), tr("Date") });
    m_slotTree->setRootIsDecorated(false);
    m_slotTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_slotTree->setStyleSheet("QTreeWidget{background:#111827;color:#e5e7eb;border:1px solid #1f2937;"
                              "border-radius:4px;}");
    detailLay->addWidget(m_slotTree, 1);

    auto* slotBtnRow = new QHBoxLayout;
    m_addSlotBtn = new QPushButton(tr("Ajouter ROM..."), detailWrap);
    m_addSlotBtn->setToolTip(tr("Ajoute une ROM supplémentaire (slot) au projet."));
    m_addSlotBtn->setEnabled(false);
    slotBtnRow->addWidget(m_addSlotBtn);
    slotBtnRow->addStretch();
    detailLay->addLayout(slotBtnRow);

    split->addWidget(detailWrap);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setSizes({ 360, 360 });
    boxLay->addWidget(split, 1);

    auto* actRow = new QHBoxLayout;
    m_openBtn   = new QPushButton(tr("Ouvrir"), listPage);
    m_openBtn->setObjectName("accentBtn");
    m_openBtn->setStyleSheet("QPushButton#accentBtn{background:#22c55e;color:#06210f;"
                             "border:none;border-radius:4px;padding:6px 12px;font-weight:600;}"
                             "QPushButton#accentBtn:hover{background:#1ea951;}"
                             "QPushButton#accentBtn:disabled{background:#1f2937;color:#4b5563;}");
    m_importBtn    = new QPushButton(tr("Importer ROM..."), listPage);
    m_duplicateBtn = new QPushButton(tr("Dupliquer"), listPage);
    m_renameBtn    = new QPushButton(tr("Modifier…"), listPage);
    m_renameBtn->setToolTip(tr("Éditer les métadonnées du projet (nom, ECU, "
                               "véhicule, immat, année, description)."));
    m_deleteBtn    = new QPushButton(tr("Supprimer"), listPage);
    m_deleteBtn->setStyleSheet("QPushButton{color:#fca5a5;}");
    m_openBtn->setEnabled(false);
    m_importBtn->setEnabled(false);
    m_duplicateBtn->setEnabled(false);
    m_renameBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);
    actRow->addWidget(m_openBtn);
    actRow->addWidget(m_importBtn);
    actRow->addWidget(m_duplicateBtn);
    actRow->addWidget(m_renameBtn);
    actRow->addWidget(m_deleteBtn);
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
    connect(m_search,     &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });
    connect(m_importBtn,  &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) importRomFor(id);
    });
    connect(m_duplicateBtn, &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) duplicateProject(id);
    });
    connect(m_renameBtn, &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) editProject(id);
    });
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) deleteProject(id);
    });
    connect(m_addSlotBtn, &QPushButton::clicked, this, [this]() {
        const QString id = selectedProjectId();
        if (!id.isEmpty()) addRomSlotFor(id);
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this]() {
        const QString id = selectedProjectId();
        const bool has = !id.isEmpty();
        m_openBtn->setEnabled(has);
        m_importBtn->setEnabled(has);
        m_duplicateBtn->setEnabled(has);
        m_renameBtn->setEnabled(has);
        m_deleteBtn->setEnabled(has);
        m_addSlotBtn->setEnabled(has);
        updateDetails(id);
    });
    connect(m_list, &QListWidget::customContextMenuRequested,
            this, &ProjectPanel::showContextMenu);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        openProject();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &ProjectPanel::showList);
    connect(createBtn, &QPushButton::clicked, this, &ProjectPanel::submitForm);
}


} // namespace ecu_studio
