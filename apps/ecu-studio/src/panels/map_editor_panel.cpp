// map_editor_panel.cpp — cœur du panneau : construction de l'UI et gestion du
// catalogue de maps (liste, filtre, recherche heuristique, open_damos).
//
// Les autres responsabilités sont réparties dans :
//   - map_editor_panel_grid.cpp     : affichage/édition de la grille, heatmap
//   - map_editor_panel_ops.cpp      : opérations multi-cellules, copier/coller
//   - map_editor_panel_baseline.cpp : sélecteur de baseline (mode fantôme)
// Helpers extraits : map_view_helpers.* (hex32/heatColor/HeatmapDelegate),
//   open_damos_relocator.* (construction des MapEntry), git_blob_utils.*.

#include "map_editor_panel.h"
#include "map_view_helpers.h"
#include "open_damos_relocator.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QFile>
#include <QFileDialog>

#include <span>

#include "ecu/EcuCatalog.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/OpenDamos.hpp"

namespace ecu_studio {

MapEditorPanel::MapEditorPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded,  this, &MapEditorPanel::refreshMaps);
        connect(m_doc, &RomDocument::ecuChanged, this, [this](const QString&) { refreshMaps(); });
    }
    refreshMaps();
}

void MapEditorPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Stage 1 ───────────────────────────────────────────────────────────
    auto* stageBox = new QGroupBox(tr("Stage 1"), this);
    auto* stageLay = new QHBoxLayout(stageBox);
    stageLay->addWidget(new QLabel(tr("Pourcentage:"), this));
    m_pctSpin = new QDoubleSpinBox(this);
    m_pctSpin->setRange(-50.0, 50.0);
    m_pctSpin->setDecimals(1);
    m_pctSpin->setSingleStep(0.5);
    m_pctSpin->setValue(15.0);
    m_pctSpin->setSuffix(" %");
    // PlusMinus : Qt dessine du texte "+/−" à la place de flèches (les flèches
    // QSS rendaient mal sur le thème sombre). Stylable via la couleur du widget.
    m_pctSpin->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    stageLay->addWidget(m_pctSpin);

    m_applyPctBtn = new QPushButton(tr("Appliquer %"), this);
    stageLay->addWidget(m_applyPctBtn);

    m_applyStage1Btn = new QPushButton(tr("Appliquer Stage 1 complet"), this);
    m_applyStage1Btn->setObjectName("accentBtn");
    stageLay->addWidget(m_applyStage1Btn);

    m_gotoHexBtn = new QPushButton(tr("Voir dans Hex"), this);
    stageLay->addWidget(m_gotoHexBtn);

    m_view3dBtn = new QPushButton(tr("Voir en 3D"), this);
    m_view3dBtn->setToolTip(tr("Affiche la map sélectionnée en surface 3D (panneau 3D)."));
    stageLay->addWidget(m_view3dBtn);

    stageLay->addStretch();
    root->addWidget(stageBox);

    // ── Auto-localisation DAMOS (séparé pour bien le voir) ────────────────
    auto* damosBox = new QGroupBox(tr("Auto-localisation DAMOS"), this);
    auto* damosLay = new QHBoxLayout(damosBox);
    damosLay->addWidget(new QLabel(tr("Source :"), this));

    m_openDamosBtn = new QPushButton(tr("open_damos (auto-localiser)"), this);
    m_openDamosBtn->setObjectName("accentBtn");
    m_openDamosBtn->setToolTip(tr("Relocalise automatiquement les maps connues par "
                                  "empreinte d'axe (aucun DAMOS dédié requis)."));
    damosLay->addWidget(m_openDamosBtn);

    m_importRecipeBtn = new QPushButton(tr("Importer recipe DAMOS…"), this);
    m_importRecipeBtn->setToolTip(tr("Charge un fichier open_damos.json externe."));
    damosLay->addWidget(m_importRecipeBtn);

    auto* findMapsBtn = new QPushButton(tr("Auto-détection heuristique"), this);
    findMapsBtn->setToolTip(tr("Scanne la ROM avec ecu::findMaps : détecte les maps "
                               "inconnues par signature d'en-tête (sans DAMOS)."));
    connect(findMapsBtn, &QPushButton::clicked, this, &MapEditorPanel::runMapFinder);
    damosLay->addWidget(findMapsBtn);

    damosLay->addStretch();
    root->addWidget(damosBox);

    // ── Listes / grille ───────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* listBox = new QGroupBox(tr("Maps"), this);
    auto* listLay = new QVBoxLayout(listBox);

    m_mapFilter = new QLineEdit(this);
    m_mapFilter->setPlaceholderText(tr("Filtrer les maps…"));
    m_mapFilter->setClearButtonEnabled(true);
    m_mapFilter->setToolTip(tr("Filtre par nom ou adresse."));
    listLay->addWidget(m_mapFilter);

    m_mapTable = new QTableWidget(this);
    m_mapTable->setColumnCount(4);
    m_mapTable->setHorizontalHeaderLabels(
        { tr("Nom"), tr("Adresse"), tr("Taille"), tr("Score") });
    m_mapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mapTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mapTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mapTable->verticalHeader()->setVisible(false);
    // Toutes les colonnes redimensionnables par l'utilisateur (drag des bordures
    // d'en-tête). Largeurs initiales pour lire le nom complet par défaut.
    m_mapTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_mapTable->horizontalHeader()->setStretchLastSection(false);
    m_mapTable->setColumnWidth(0, 220);  // Nom
    m_mapTable->setColumnWidth(1, 90);   // Adresse
    m_mapTable->setColumnWidth(2, 70);   // Taille
    m_mapTable->setColumnWidth(3, 110);  // Score
    m_mapTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_mapTable->setWordWrap(false);
    m_mapTable->setTextElideMode(Qt::ElideRight);
    listLay->addWidget(m_mapTable);
    splitter->addWidget(listBox);

    auto* gridBox = new QGroupBox(tr("Édition"), this);
    auto* gridLay = new QVBoxLayout(gridBox);
    m_infoLabel = new QLabel(tr("Aucune map sélectionnée"), this);
    m_infoLabel->setStyleSheet("color:#7c8fa6;");
    // Wrap : un commentaire long ne doit pas élargir le panneau (donc pas pousser la
    // limite du splitter), il doit passer à la ligne.
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_infoLabel->setMinimumWidth(0);
    gridLay->addWidget(m_infoLabel);

    // ── Barre d'opérations sur la sélection ────────────────────────────────
    auto* opRow = new QHBoxLayout;
    opRow->setSpacing(4);
    m_heatmapChk = new QCheckBox(tr("Heatmap"), this);
    m_heatmapChk->setChecked(true);
    m_heatmapChk->setToolTip(tr("Colore chaque cellule selon sa valeur (froid→chaud)."));
    opRow->addWidget(m_heatmapChk);

    m_ghostChk = new QCheckBox(tr("Fantôme"), this);
    m_ghostChk->setChecked(false);
    m_ghostChk->setToolTip(tr("Superpose la valeur de la baseline sous la valeur "
                              "courante (en italique) + triangle de delta. "
                              "Choisis la baseline via « Baseline… » : un commit git "
                              "donne un fantôme entre la ROM modifiée courante et ce "
                              "commit."));
    opRow->addWidget(m_ghostChk);
    m_baselineBtn = new QPushButton(tr("Baseline…"), this);
    m_baselineBtn->setToolTip(tr("Choisir la ROM de référence du mode fantôme : "
                                 "commit git, fichier .bin externe ou snapshot d'origine."));
    opRow->addWidget(m_baselineBtn);
    opRow->addSpacing(8);

    auto mkOp = [&](const QString& txt, const QString& tip, void (MapEditorPanel::*slot)()) {
        auto* b = new QToolButton(this);
        b->setText(txt);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, slot);
        opRow->addWidget(b);
        return b;
    };
    mkOp(tr("Fixer"),     tr("Fixe une valeur sur la sélection"),               &MapEditorPanel::opSet);
    mkOp(tr("+/−"),       tr("Ajoute une valeur brute à la sélection"),         &MapEditorPanel::opAdd);
    mkOp(tr("× %"),       tr("Multiplie la sélection par (1 + %/100)"),         &MapEditorPanel::opMultiply);
    mkOp(tr("Interp."),   tr("Interpole linéairement chaque ligne sélectionnée"), &MapEditorPanel::opInterpolate);
    mkOp(tr("Lisser"),    tr("Lissage 3×3 (moyenne des voisins) sur la sélection"), &MapEditorPanel::opSmooth);
    opRow->addSpacing(8);
    mkOp(tr("Copier"),    tr("Copie la région sélectionnée (TSV, Excel)"),      &MapEditorPanel::copyRegionTsv);
    mkOp(tr("Coller"),    tr("Colle un TSV à partir de la cellule active"),     &MapEditorPanel::pasteRegionTsv);
    opRow->addStretch();
    gridLay->addLayout(opRow);

    m_grid = new QTableWidget(this);
    m_grid->horizontalHeader()->setVisible(true);
    m_grid->verticalHeader()->setVisible(true);
    m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Cellules redimensionnables par l'utilisateur (drag des bordures d'en-tête).
    // La taille choisie est mémorisée dans m_colWidth/m_rowHeight et réappliquée
    // à chaque map pour ne pas changer entre deux sélections.
    m_grid->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_grid->horizontalHeader()->setDefaultSectionSize(m_colWidth);
    m_grid->horizontalHeader()->setMinimumSectionSize(24);
    m_grid->verticalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_grid->verticalHeader()->setDefaultSectionSize(m_rowHeight);
    m_grid->verticalHeader()->setMinimumSectionSize(14);
    // Largeur de l'en-tête vertical figée : empêche le grid de "glisser" quand les
    // labels d'axe Y changent de longueur entre deux maps.
    m_grid->verticalHeader()->setFixedWidth(72);
    m_grid->setShowGrid(true);
    // Delegate custom — peint les fonds heatmap (le QSS global les masquerait).
    m_grid->setItemDelegate(new HeatmapDelegate(m_grid));
    gridLay->addWidget(m_grid, 1);

    // Quand l'utilisateur drag une bordure d'en-tête, propage la nouvelle taille à
    // toutes les colonnes/lignes du grid et mémorise comme défaut pour les futures maps.
    connect(m_grid->horizontalHeader(), &QHeaderView::sectionResized, this,
            [this](int /*idx*/, int /*oldSize*/, int newSize) {
                if (m_applyingCellSize || newSize <= 0) return;
                m_colWidth = newSize;
                m_applyingCellSize = true;
                m_grid->horizontalHeader()->setDefaultSectionSize(newSize);
                for (int c = 0; c < m_grid->columnCount(); ++c)
                    m_grid->setColumnWidth(c, newSize);
                m_applyingCellSize = false;
            });
    connect(m_grid->verticalHeader(), &QHeaderView::sectionResized, this,
            [this](int /*idx*/, int /*oldSize*/, int newSize) {
                if (m_applyingCellSize || newSize <= 0) return;
                m_rowHeight = newSize;
                m_applyingCellSize = true;
                m_grid->verticalHeader()->setDefaultSectionSize(newSize);
                for (int r = 0; r < m_grid->rowCount(); ++r)
                    m_grid->setRowHeight(r, newSize);
                m_applyingCellSize = false;
            });

    m_selLabel = new QLabel(tr("Sélection : —"), this);
    m_selLabel->setStyleSheet("color:#9ca3af; font-size:11px; font-family:monospace;");
    gridLay->addWidget(m_selLabel);

    splitter->addWidget(gridBox);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 340, 640 });
    root->addWidget(splitter, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    connect(m_mapTable, &QTableWidget::itemSelectionChanged,
            this, &MapEditorPanel::onMapSelectionChanged);
    connect(m_grid, &QTableWidget::itemChanged,
            this, &MapEditorPanel::onCellChanged);
    connect(m_grid, &QTableWidget::itemSelectionChanged,
            this, &MapEditorPanel::onGridSelectionChanged);
    connect(m_mapFilter, &QLineEdit::textChanged, this, [this](const QString&) { applyMapFilter(); });
    connect(m_heatmapChk, &QCheckBox::toggled, this, [this](bool) { recolorHeatmap(); });
    connect(m_ghostChk,   &QCheckBox::toggled, this, [this](bool) {
        if (m_currentAddr != 0) loadGrid(m_currentAddr);
    });
    connect(m_baselineBtn, &QPushButton::clicked, this, &MapEditorPanel::pickBaseline);
    if (m_doc)
        connect(m_doc, &RomDocument::baselineChanged, this, [this]() {
            if (m_currentAddr != 0 && m_ghostChk && m_ghostChk->isChecked())
                loadGrid(m_currentAddr);
        });
    connect(m_applyPctBtn,    &QPushButton::clicked, this, &MapEditorPanel::applyPercent);
    connect(m_applyStage1Btn, &QPushButton::clicked, this, &MapEditorPanel::applyFullStage1);
    connect(m_gotoHexBtn,     &QPushButton::clicked, this, &MapEditorPanel::gotoHex);
    connect(m_view3dBtn,      &QPushButton::clicked, this, &MapEditorPanel::view3d);
    connect(m_openDamosBtn,    &QPushButton::clicked, this, &MapEditorPanel::runOpenDamos);
    connect(m_importRecipeBtn, &QPushButton::clicked, this, &MapEditorPanel::importOpenDamosRecipe);
}

void MapEditorPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444; font-size:11px;"
                                       : "color:#7c8fa6; font-size:11px;");
    m_statusLabel->setText(msg);
}

// ── Catalogue de maps ─────────────────────────────────────────────────────────

void MapEditorPanel::refreshMaps() {
    m_entries.clear();

    if (!m_doc || !m_doc->isLoaded()) {
        rebuildMapTable();
        setStatus(tr("Aucune ROM chargée."));
        return;
    }

    // Priorité : open_damos — relocalise par empreinte et fournit factor/offset/stock.
    auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
    if (recipe) {
        int byFingerprint = 0;
        m_entries = buildRelocatedEntries(*recipe, m_doc->rom(), &byFingerprint);
        rebuildMapTable();
        setStatus(tr("open_damos : %1/%2 maps relocalisées par empreinte.")
                      .arg(byFingerprint).arg(m_entries.size()));
        return;
    }

    // Repli : maps Stage 1 du catalogue ECU (adresses fixes).
    auto span = constByteSpan(m_doc->rom());
    auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
    if (ecu && ecu->stage1Maps) {
        for (const auto& m : *ecu->stage1Maps) {
            MapEntry e;
            e.name       = QString::fromUtf8(m.name.data(), static_cast<int>(m.name.size()));
            e.address    = m.address;
            e.score      = -1.0;
            e.stage1     = true;
            e.defaultPct = m.defaultPct;
            auto md = ecu::readMapData(span, m.address);
            if (md) { e.nx = md->nx; e.ny = md->ny; }
            m_entries.push_back(std::move(e));
        }
    }

    rebuildMapTable();
    setStatus(m_entries.empty()
        ? tr("Aucune map connue — utilisez « Chercher maps » ou importez un recipe DAMOS.")
        : tr("%1 map(s) Stage 1.").arg(m_entries.size()));
}

void MapEditorPanel::rebuildMapTable() {
    m_currentRow = -1;
    m_mapTable->clearSelection();
    m_mapTable->setRowCount(static_cast<int>(m_entries.size()));

    for (int row = 0; row < static_cast<int>(m_entries.size()); ++row) {
        const MapEntry& e = m_entries[static_cast<std::size_t>(row)];

        auto* nameItem = new QTableWidgetItem(e.name);
        auto* addrItem = new QTableWidgetItem(hex32(e.address));
        const QString sizeStr = (e.nx > 0 && e.ny > 0)
            ? QString("%1×%2").arg(e.nx).arg(e.ny)
            : QString("—");
        auto* sizeItem = new QTableWidgetItem(sizeStr);
        QString scoreStr;
        if (e.openDamos) {
            scoreStr = e.matchInfo;
        } else if (e.score >= 0.0) {
            scoreStr = QString::number(e.score, 'f', 1);
        } else {
            scoreStr = e.stage1 ? tr("Stage1") : QString("—");
        }
        auto* scoreItem = new QTableWidgetItem(scoreStr);

        m_mapTable->setItem(row, 0, nameItem);
        m_mapTable->setItem(row, 1, addrItem);
        m_mapTable->setItem(row, 2, sizeItem);
        m_mapTable->setItem(row, 3, scoreItem);
    }

    applyMapFilter();   // conserve le filtre actif après reconstruction
}

void MapEditorPanel::onMapSelectionChanged() {
    auto rows = m_mapTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        m_currentRow = -1;
        return;
    }
    int row = rows.first().row();
    if (row < 0 || row >= static_cast<int>(m_entries.size())) {
        m_currentRow = -1;
        return;
    }
    m_currentRow = row;
    loadGrid(m_entries[static_cast<std::size_t>(row)].address);
}

void MapEditorPanel::applyMapFilter() {
    const QString q = m_mapFilter->text().trimmed().toLower();
    int visible = 0;
    for (int row = 0; row < m_mapTable->rowCount(); ++row) {
        bool match = q.isEmpty();
        if (!match) {
            for (int c = 0; c < m_mapTable->columnCount() && !match; ++c) {
                auto* it = m_mapTable->item(row, c);
                if (it && it->text().toLower().contains(q)) match = true;
            }
        }
        m_mapTable->setRowHidden(row, !match);
        if (match) ++visible;
    }
    if (!q.isEmpty())
        setStatus(tr("%1 map(s) correspondante(s).").arg(visible));
}

void MapEditorPanel::runMapFinder() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    setStatus(tr("Recherche heuristique de maps en cours..."));

    auto span = constByteSpan(m_doc->rom());
    ecu::FindMapsOptions opts;
    auto candidates = ecu::findMaps(span, opts);

    // Conserve les maps connues (Stage 1) en tête, ajoute les candidates.
    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1) kept.push_back(e);
    m_entries = std::move(kept);

    for (const auto& c : candidates) {
        MapEntry e;
        e.address = static_cast<quint32>(c.address);
        e.nx      = c.nx;
        e.ny      = c.ny;
        e.score   = c.score;
        e.stage1  = false;
        e.name    = tr("map @ %1").arg(hex32(e.address));
        m_entries.push_back(std::move(e));
    }

    rebuildMapTable();
    setStatus(tr("%1 candidate(s) détectée(s) (%2 entrée(s) au total).")
        .arg(candidates.size()).arg(m_entries.size()));
}

void MapEditorPanel::runOpenDamos() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    setStatus(tr("open_damos : relocalisation par empreinte en cours..."));

    // (1) Charge la recette open_damos pour l'ECU du document.
    auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
    if (!recipe) {
        setStatus(tr("open_damos : échec du chargement de la recette — %1")
                      .arg(QString::fromStdString(recipe.error())),
                  true);
        return;
    }

    // (2-3) Relocalise et réinjecte : on conserve les maps Stage 1 du catalogue
    // puis on ajoute les maps relocalisées par open_damos.
    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1 && !e.openDamos) kept.push_back(e);

    int byFingerprint = 0;
    auto relocated = buildRelocatedEntries(*recipe, m_doc->rom(), &byFingerprint);
    const int total = static_cast<int>(relocated.size());

    m_entries = std::move(kept);
    for (auto& e : relocated) m_entries.push_back(std::move(e));

    rebuildMapTable();
    setStatus(tr("open_damos : %1/%2 maps relocalisées par empreinte.")
                  .arg(byFingerprint).arg(total));
}

void MapEditorPanel::importOpenDamosRecipe() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Chargez une ROM d'abord."), true);
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer un recipe open_damos"),
        {}, tr("Recipe DAMOS (*.json);;Tous (*.*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setStatus(tr("Impossible d'ouvrir : %1").arg(path), true);
        return;
    }
    const std::string text = f.readAll().toStdString();

    auto recipe = ecu::OpenDamos::parseRecipe(text);
    if (!recipe) {
        setStatus(tr("Recipe invalide : %1")
                      .arg(QString::fromStdString(recipe.error())), true);
        return;
    }

    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1 && !e.openDamos) kept.push_back(e);

    int byFingerprint = 0;
    auto relocated = buildRelocatedEntries(*recipe, m_doc->rom(), &byFingerprint);
    const int total = static_cast<int>(relocated.size());

    m_entries = std::move(kept);
    for (auto& e : relocated) m_entries.push_back(std::move(e));

    rebuildMapTable();
    setStatus(tr("Recipe importé : %1/%2 maps relocalisées par empreinte.")
                  .arg(byFingerprint).arg(total));
}

} // namespace ecu_studio
