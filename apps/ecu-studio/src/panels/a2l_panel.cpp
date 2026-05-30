#include "a2l_panel.h"
#include "../rom_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>

#include <QTextStream>

#include "ecu/EcuCatalog.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/OpenDamosA2lExport.hpp"

namespace ecu_studio {

namespace {
// Colonnes du tableau.
enum Column { ColName = 0, ColType, ColAddress, ColDims, ColUnit, ColCount };
} // namespace

A2lPanel::A2lPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    // Si une nouvelle ROM/ECU est sélectionnée, proposer le chargement auto.
    if (m_doc) {
        connect(m_doc, &RomDocument::ecuChanged, this,
                [this](const QString&) { maybeOfferAutoLoad(); });
        connect(m_doc, &RomDocument::romLoaded, this,
                [this]() { maybeOfferAutoLoad(); });
    }

    // Tentative de chargement auto au démarrage si un ECU est déjà connu.
    maybeOfferAutoLoad();
}

void A2lPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Barre d'outils ───────────────────────────────────────────────────────
    auto* toolRow = new QHBoxLayout;
    m_loadBtn = new QPushButton(tr("Charger A2L..."), this);
    m_loadBtn->setObjectName("accentBtn");
    toolRow->addWidget(m_loadBtn);

    m_exportBtn = new QPushButton(tr("Exporter A2L..."), this);
    m_exportBtn->setToolTip(
        tr("Génère un A2L décrivant les maps open_damos relocalisées pour la ROM courante"));
    toolRow->addWidget(m_exportBtn);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filtrer (nom, type, unité, adresse)..."));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toolRow->addWidget(m_filterEdit, 1);
    root->addLayout(toolRow);

    // ── Chemin chargé ────────────────────────────────────────────────────────
    m_pathLabel = new QLabel(tr("Aucun fichier A2L chargé"), this);
    m_pathLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_pathLabel);

    // ── Tableau ──────────────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(ColCount);
    m_table->setHorizontalHeaderLabels({
        tr("Nom"), tr("Type"), tr("Adresse"), tr("Dimensions"), tr("Unité")
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColAddress, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColDims, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    // ── Pied : compteur ──────────────────────────────────────────────────────
    m_countLabel = new QLabel(tr("0 caractéristique"), this);
    m_countLabel->setStyleSheet("color:#7c8fa6;");
    root->addWidget(m_countLabel);

    connect(m_loadBtn,    &QPushButton::clicked,   this, &A2lPanel::loadA2l);
    connect(m_exportBtn,  &QPushButton::clicked,   this, &A2lPanel::exportA2l);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &A2lPanel::applyFilter);
    connect(m_table,      &QTableWidget::cellDoubleClicked,
            this, &A2lPanel::onCellDoubleClicked);
}

void A2lPanel::loadA2l() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Charger un fichier A2L"), {},
        tr("Fichiers A2L (*.a2l);;Tous les fichiers (*.*)"));
    if (path.isEmpty()) return;
    loadA2lFile(path);
}

bool A2lPanel::loadA2lFile(const QString& path) {
    if (path.isEmpty()) return false;

    ecu::A2lParser parser;
    if (!parser.parse(path)) {
        QMessageBox::warning(this, tr("Erreur A2L"),
            tr("Impossible d'ouvrir ou d'analyser le fichier :\n%1").arg(path));
        return false;
    }

    m_parser = std::move(parser);
    m_loaded = true;
    m_pathLabel->setText(tr("Fichier : %1").arg(path));

    populateTable();
    applyFilter(m_filterEdit->text());

    const int n = static_cast<int>(m_parser.characteristics().size());
    emit a2lLoaded(path, n);
    return true;
}

void A2lPanel::exportA2l() {
    // L'export tire sa valeur des maps open_damos relocalisées pour le firmware
    // courant : il faut donc une ROM chargée et un ECU connu.
    if (!m_doc || !m_doc->isLoaded()) {
        QMessageBox::information(this, tr("Exporter A2L"),
            tr("Aucune ROM n'est chargée. Chargez d'abord un firmware pour "
               "exporter un A2L décrivant ses maps relocalisées."));
        return;
    }

    const QString ecuId = m_doc->ecuId();
    if (ecuId.isEmpty()) {
        QMessageBox::information(this, tr("Exporter A2L"),
            tr("Aucun ECU n'est associé à la ROM courante. Identifiez d'abord "
               "l'ECU pour disposer d'un recipe open_damos."));
        return;
    }

    // 1) Charger le recipe open_damos de l'ECU.
    auto recipeRes = ecu::OpenDamos::loadRecipe(ecuId);
    if (!recipeRes) {
        QMessageBox::warning(this, tr("Exporter A2L"),
            tr("Impossible de charger le recipe open_damos pour l'ECU « %1 » :\n%2")
                .arg(ecuId, QString::fromStdString(recipeRes.error())));
        return;
    }
    const ecu::DamosRecipe& recipe = *recipeRes;

    // 2) Relocaliser chaque caractéristique sur la ROM courante (adresses résolues).
    ecu::OpenDamos damos;
    const std::vector<ecu::RelocResult> reloc =
        damos.relocate(recipe, m_doc->rom());

    // 3) Générer le texte A2L complet (en-tête + RECORD_LAYOUTs + COMPU_METHODs
    //    + CHARACTERISTICs + pied) à partir du recipe et des adresses résolues.
    auto exportRes = ecu::OpenDamosA2lExport::exportA2l(recipe, reloc);
    if (!exportRes) {
        QMessageBox::warning(this, tr("Exporter A2L"),
            tr("Échec de la génération de l'A2L :\n%1")
                .arg(QString::fromStdString(exportRes.error())));
        return;
    }

    // 4) Choisir le fichier de destination et écrire.
    const QString defaultName = ecuId + QStringLiteral(".a2l");
    QString path = QFileDialog::getSaveFileName(
        this, tr("Exporter un fichier A2L"), defaultName,
        tr("Fichiers A2L (*.a2l);;Tous les fichiers (*.*)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".a2l"), Qt::CaseInsensitive))
        path += QStringLiteral(".a2l");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Exporter A2L"),
            tr("Impossible d'écrire le fichier :\n%1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << exportRes->a2l;
    file.close();

    const int n = static_cast<int>(recipe.characteristics.size());
    QMessageBox::information(this, tr("Exporter A2L"),
        tr("A2L exporté avec succès : %n caractéristique(s).\n%1",
           nullptr, n).arg(path));
}

void A2lPanel::populateTable() {
    const auto& chars = m_parser.characteristics();

    // Désactiver le tri pendant le remplissage (évite la réorganisation à chaque
    // insertion et la corruption des index de lignes).
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(static_cast<int>(chars.size()));

    for (int row = 0; row < static_cast<int>(chars.size()); ++row) {
        const ecu::Characteristic& c = chars[row];

        auto* nameItem = new QTableWidgetItem(c.name);
        if (!c.longIdentifier.isEmpty())
            nameItem->setToolTip(c.longIdentifier);

        auto* typeItem = new QTableWidgetItem(c.type);

        // Adresse en hexadécimal ; valeur numérique stockée pour un tri correct.
        const QString addrStr =
            QStringLiteral("0x%1").arg(c.address, 8, 16, QLatin1Char('0')).toUpper()
                .replace(QStringLiteral("0X"), QStringLiteral("0x"));
        auto* addrItem = new QTableWidgetItem(addrStr);
        addrItem->setData(Qt::UserRole, c.address);
        addrItem->setTextAlignment(Qt::AlignCenter);

        // Dimensions : nx[ x ny] uniquement pour les courbes/maps.
        QString dims;
        if (c.ny > 1)      dims = QStringLiteral("%1 x %2").arg(c.nx).arg(c.ny);
        else if (c.nx > 1) dims = QString::number(c.nx);
        auto* dimsItem = new QTableWidgetItem(dims);
        dimsItem->setTextAlignment(Qt::AlignCenter);

        auto* unitItem = new QTableWidgetItem(c.unit);
        unitItem->setTextAlignment(Qt::AlignCenter);

        m_table->setItem(row, ColName,    nameItem);
        m_table->setItem(row, ColType,    typeItem);
        m_table->setItem(row, ColAddress, addrItem);
        m_table->setItem(row, ColDims,    dimsItem);
        m_table->setItem(row, ColUnit,    unitItem);
    }

    m_table->setSortingEnabled(true);
}

void A2lPanel::applyFilter(const QString& text) {
    const QString needle = text.trimmed().toLower();
    int visible = 0;
    const int total = m_table->rowCount();

    for (int row = 0; row < total; ++row) {
        bool match = needle.isEmpty();
        if (!match) {
            for (int col = 0; col < ColCount && !match; ++col) {
                if (auto* item = m_table->item(row, col)) {
                    if (item->text().toLower().contains(needle))
                        match = true;
                }
            }
        }
        m_table->setRowHidden(row, !match);
        if (match) ++visible;
    }

    if (needle.isEmpty()) {
        m_countLabel->setText(tr("%n caractéristique(s)", nullptr, total));
    } else {
        m_countLabel->setText(
            tr("%1 / %2 caractéristique(s)").arg(visible).arg(total));
    }
}

void A2lPanel::onCellDoubleClicked(int row, int /*column*/) {
    if (row < 0) return;
    auto* addrItem = m_table->item(row, ColAddress);
    if (!addrItem) return;

    bool ok = false;
    const quint32 address = addrItem->data(Qt::UserRole).toUInt(&ok);
    if (ok)
        emit gotoAddressRequested(address);
}

void A2lPanel::maybeOfferAutoLoad() {
    // Option pratique : si l'ECU du document référence un fichier A2L existant,
    // proposer de le charger automatiquement.
    if (!m_doc) return;
    const QString ecuId = m_doc->ecuId();
    if (ecuId.isEmpty()) return;

    auto entry = ecu::getEcu(ecuId.toStdString());
    if (!entry || !entry->a2l.has_value()) return;

    const QString a2lPath = QString::fromUtf8(
        entry->a2l->data(), static_cast<int>(entry->a2l->size()));
    if (a2lPath.isEmpty() || !QFile::exists(a2lPath)) return;

    // Déjà chargé ? Ne pas redemander.
    if (m_loaded && m_pathLabel->text().contains(a2lPath)) return;

    const auto reply = QMessageBox::question(
        this, tr("Charger A2L"),
        tr("Un fichier A2L est associé à l'ECU « %1 » :\n%2\n\nLe charger ?")
            .arg(ecuId, a2lPath),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (reply == QMessageBox::Yes)
        loadA2lFile(a2lPath);
}

} // namespace ecu_studio
