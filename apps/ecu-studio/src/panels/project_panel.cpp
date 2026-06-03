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
constexpr int IdRole = Qt::UserRole + 1;
constexpr int EcuRole = Qt::UserRole + 2;
constexpr int SlugRole = Qt::UserRole + 3;

QString projectsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/projects";
}

QString humanSize(qint64 bytes) {
    if (bytes <= 0) return QObject::tr("—");
    if (bytes < 1024) return QString("%1 o").arg(bytes);
    const double kb = bytes / 1024.0;
    if (kb < 1024.0) return QString("%1 Ko").arg(kb, 0, 'f', 0);
    return QString("%1 Mo").arg(kb / 1024.0, 0, 'f', 2);
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
            // Texte indexé pour la recherche.
            item->setData(Qt::UserRole + 10,
                QStringList{ p.name, p.ecu, p.vehicle, p.immat, p.year }.join(' ').toLower());
        }
        m_list->setCurrentRow(0);
    }

    applyFilter();

    const QString id = selectedProjectId();
    const bool has = !id.isEmpty();
    m_openBtn->setEnabled(has);
    m_importBtn->setEnabled(has);
    m_duplicateBtn->setEnabled(has);
    m_renameBtn->setEnabled(has);
    m_deleteBtn->setEnabled(has);
    m_addSlotBtn->setEnabled(has);
    updateDetails(id);
}

void ProjectPanel::applyFilter() {
    const QString q = m_search ? m_search->text().trimmed().toLower() : QString();
    int firstVisible = -1;
    for (int i = 0; i < m_list->count(); ++i) {
        auto* it = m_list->item(i);
        const bool match = q.isEmpty()
            || it->data(Qt::UserRole + 10).toString().contains(q);
        it->setHidden(!match);
        if (match && firstVisible < 0) firstVisible = i;
    }
    // Si la sélection courante est masquée, sélectionne le premier visible.
    auto* cur = m_list->currentItem();
    if ((!cur || cur->isHidden()) && firstVisible >= 0)
        m_list->setCurrentRow(firstVisible);
}

QString ProjectPanel::selectedProjectId() const {
    auto* item = m_list->currentItem();
    if (!item || m_list->isHidden() || item->isHidden()) return {};
    return item->data(IdRole).toString();
}

QString ProjectPanel::selectedSlotSlug() const {
    if (!m_slotTree) return {};
    auto* it = m_slotTree->currentItem();
    return it ? it->data(0, SlugRole).toString() : QString();
}

void ProjectPanel::updateDetails(const QString& id) {
    m_slotTree->clear();
    if (id.isEmpty()) {
        m_detailCard->setText(tr("Sélectionnez un projet."));
        m_addSlotBtn->setEnabled(false);
        return;
    }
    auto meta = m_manager->get(id);
    if (!meta) {
        m_detailCard->setText(tr("Projet introuvable."));
        m_addSlotBtn->setEnabled(false);
        return;
    }

    auto row = [](const QString& k, const QString& v) {
        return QString("<tr><td style='color:#7c8fa6;padding-right:10px'>%1</td>"
                       "<td style='color:#e5e7eb'>%2</td></tr>")
            .arg(k, v.isEmpty() ? QStringLiteral("—") : v.toHtmlEscaped());
    };
    const QString created = meta->createdAt.isValid()
        ? meta->createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm")
        : tr("—");
    const QString romLine = meta->hasRom
        ? QString("%1 (%2)").arg(meta->romName.isEmpty() ? tr("rom.bin") : meta->romName,
                                 humanSize(meta->romSize))
        : tr("aucune");

    QString html = QString("<b style='font-size:14px;color:#ffffff'>%1</b><br/>")
                       .arg(meta->name.toHtmlEscaped());
    html += "<table cellspacing='0' cellpadding='1'>";
    html += row(tr("ECU"),       meta->ecu);
    html += row(tr("Véhicule"),  meta->vehicle);
    html += row(tr("Immat."),    meta->immat);
    html += row(tr("Année"),     meta->year);
    html += row(tr("ROM principale"), romLine);
    html += row(tr("Créé le"),   created);
    html += "</table>";
    m_detailCard->setText(html);

    // Slots ROM (catalogue + slots additionnels).
    if (meta->hasRom) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, tr("rom.bin (principale)"));
        it->setText(1, humanSize(meta->romSize));
        it->setText(2, meta->romImportedAt.isValid()
            ? meta->romImportedAt.toLocalTime().toString("yyyy-MM-dd HH:mm") : tr("—"));
        it->setData(0, SlugRole, QString());   // slug vide => ROM principale
    }
    for (const auto& s : m_manager->listRomSlots(id)) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, s.slug);
        it->setText(1, humanSize(s.size));
        it->setText(2, s.createdAt.isValid()
            ? s.createdAt.toLocalTime().toString("yyyy-MM-dd HH:mm") : tr("—"));
        it->setData(0, SlugRole, s.slug);
    }
    if (m_slotTree->topLevelItemCount() == 0) {
        auto* it = new QTreeWidgetItem(m_slotTree);
        it->setText(0, tr("(aucune ROM)"));
        it->setForeground(0, QColor(0x7c, 0x8f, 0xa6));
    }
    m_slotTree->resizeColumnToContents(0);
    m_addSlotBtn->setEnabled(true);
}

void ProjectPanel::showContextMenu(const QPoint& pos) {
    const QString id = selectedProjectId();
    if (id.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(tr("Ouvrir"),       this, [this, id]() { openProjectById(id); });
    menu.addAction(tr("Importer ROM..."), this, [this, id]() { importRomFor(id); });
    menu.addAction(tr("Ajouter ROM..."),  this, [this, id]() { addRomSlotFor(id); });
    menu.addSeparator();
    menu.addAction(tr("Modifier…"),    this, [this, id]() { editProject(id); });
    menu.addAction(tr("Dupliquer"),    this, [this, id]() { duplicateProject(id); });
    menu.addAction(tr("Supprimer"),    this, [this, id]() { deleteProject(id); });
    menu.exec(m_list->mapToGlobal(pos));
}

void ProjectPanel::addRomSlotFor(const QString& id) {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ajouter une ROM (slot)"), {},
        tr("ROM (*.bin *.ori *.mod *.hex *.ols);;Tous (*.*)"));
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ajouter ROM"),
                             tr("Impossible d'ouvrir le fichier : %1").arg(file));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    auto res = m_manager->addRomSlot(id, data, QFileInfo(file).fileName());
    if (!res) {
        QMessageBox::warning(this, tr("Ajouter ROM"),
                             tr("Échec : %1").arg(res.error()));
        return;
    }
    updateDetails(id);
}

void ProjectPanel::deleteProject(const QString& id) {
    auto meta = m_manager->get(id);
    const QString name = meta ? meta->name : id;
    if (QMessageBox::question(this, tr("Supprimer le projet"),
            tr("Supprimer définitivement le projet « %1 » et toutes ses ROMs ?").arg(name),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;
    m_manager->remove(id);
    refreshList();
}

void ProjectPanel::editProject(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Modifier"), tr("Projet introuvable."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Modifier le projet"));
    dlg.setMinimumWidth(420);

    auto* lay  = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;

    auto* nameEdit    = new QLineEdit(meta->name, &dlg);
    auto* ecuCombo    = new QComboBox(&dlg);
    int curIdx = -1;
    for (const auto& s : ecu::listEcus()) {
        const QString eid  = QString::fromUtf8(s.id.data(), (qsizetype)s.id.size());
        const QString name = QString::fromUtf8(s.name.data(), (qsizetype)s.name.size());
        ecuCombo->addItem(name.isEmpty() ? eid : QString("%1 (%2)").arg(name, eid), eid);
        if (eid == meta->ecu) curIdx = ecuCombo->count() - 1;
    }
    if (curIdx >= 0) ecuCombo->setCurrentIndex(curIdx);

    auto* vehicleEdit = new QLineEdit(meta->vehicle, &dlg);
    auto* immatEdit   = new QLineEdit(meta->immat,   &dlg);
    auto* yearEdit    = new QLineEdit(meta->year,    &dlg);
    auto* descEdit    = new QTextEdit(&dlg);
    descEdit->setPlainText(meta->description);
    descEdit->setMaximumHeight(110);

    form->addRow(tr("Nom *"),           nameEdit);
    form->addRow(tr("ECU"),             ecuCombo);
    form->addRow(tr("Véhicule"),        vehicleEdit);
    form->addRow(tr("Immatriculation"), immatEdit);
    form->addRow(tr("Année"),           yearEdit);
    form->addRow(tr("Description"),     descEdit);
    lay->addLayout(form);

    auto* err = new QLabel(&dlg);
    err->setStyleSheet("color:#ef4444; font-size:11px;");
    err->hide();
    lay->addWidget(err);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        const QString trimmed = nameEdit->text().trimmed();
        if (trimmed.isEmpty()) { err->setText(tr("Le nom est obligatoire.")); err->show(); return; }
        QJsonObject fields;
        fields["name"]        = trimmed;
        fields["ecu"]         = ecuCombo->currentData().toString();
        fields["vehicle"]     = vehicleEdit->text().trimmed();
        fields["immat"]       = immatEdit->text().trimmed();
        fields["year"]        = yearEdit->text().trimmed();
        fields["description"] = descEdit->toPlainText();
        auto res = m_manager->update(id, fields);
        if (!res) {
            err->setText(tr("Échec : %1").arg(res.error())); err->show(); return;
        }
        dlg.accept();
    });

    if (dlg.exec() == QDialog::Accepted) {
        refreshList();
        // Restaure la sélection sur le projet renommé.
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(IdRole).toString() == id) {
                m_list->setCurrentRow(i); break;
            }
        }
    }
}

void ProjectPanel::duplicateProject(const QString& id) {
    auto meta = m_manager->get(id);
    if (!meta) {
        QMessageBox::warning(this, tr("Dupliquer"), tr("Projet introuvable."));
        return;
    }

    bool ok = false;
    const QString newName = QInputDialog::getText(this, tr("Dupliquer le projet"),
        tr("Nom de la copie :"), QLineEdit::Normal,
        tr("%1 (copie)").arg(meta->name), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    ecu::CreateProjectParams params;
    params.name        = newName.trimmed();
    params.ecu         = meta->ecu;
    params.description = meta->description;
    params.vehicle     = meta->vehicle;
    params.immat       = meta->immat;
    params.year        = meta->year;

    auto created = m_manager->create(params);
    if (!created) {
        QMessageBox::warning(this, tr("Dupliquer"),
                             tr("Création échouée : %1").arg(created.error()));
        return;
    }

    // Copie la ROM principale si présente.
    if (auto src = m_manager->romPath(id); src && QFile::exists(*src)) {
        QFile f(*src);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray data = f.readAll();
            f.close();
            m_manager->importRom(created->id, data,
                                 meta->romName.isEmpty() ? tr("rom.bin") : meta->romName);
        }
    }
    // Copie les slots additionnels.
    for (const auto& s : m_manager->listRomSlots(id)) {
        if (auto sp = m_manager->romSlotPath(id, s.slug); sp && QFile::exists(*sp)) {
            QFile f(*sp);
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray data = f.readAll();
                f.close();
                m_manager->addRomSlot(created->id, data, s.slug);
            }
        }
    }

    refreshList();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(IdRole).toString() == created->id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
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
