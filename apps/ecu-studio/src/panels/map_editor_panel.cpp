#include "map_editor_panel.h"
#include "../rom_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QMessageBox>

#include <cstdint>
#include <span>
#include <string>

#include "ecu/EcuCatalog.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/OpenDamos.hpp"

namespace ecu_studio {

namespace {

// Construit un span const sur la ROM du document.
std::span<const uint8_t> constSpan(const QByteArray& rom) {
    return { reinterpret_cast<const uint8_t*>(rom.constData()),
             static_cast<std::size_t>(rom.size()) };
}

// Construit un span mutable sur la ROM du document.
std::span<uint8_t> mutSpan(QByteArray& rom) {
    return { reinterpret_cast<uint8_t*>(rom.data()),
             static_cast<std::size_t>(rom.size()) };
}

QString hex32(quint32 v) {
    return QString("0x%1").arg(v, 6, 16, QChar('0')).toUpper().replace("0X", "0x");
}

} // namespace

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
    stageLay->addWidget(m_pctSpin);

    m_applyPctBtn = new QPushButton(tr("Appliquer %"), this);
    stageLay->addWidget(m_applyPctBtn);

    m_applyStage1Btn = new QPushButton(tr("Appliquer Stage 1 complet"), this);
    m_applyStage1Btn->setObjectName("accentBtn");
    stageLay->addWidget(m_applyStage1Btn);

    m_gotoHexBtn = new QPushButton(tr("Voir dans Hex"), this);
    stageLay->addWidget(m_gotoHexBtn);

    m_openDamosBtn = new QPushButton(tr("open_damos (auto-localiser)"), this);
    m_openDamosBtn->setObjectName("accentBtn");
    m_openDamosBtn->setToolTip(tr("Relocalise automatiquement les maps connues par "
                                  "empreinte d'axe (aucun DAMOS dédié requis)."));
    stageLay->addWidget(m_openDamosBtn);

    stageLay->addStretch();
    root->addWidget(stageBox);

    // ── Listes / grille ───────────────────────────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    auto* listBox = new QGroupBox(tr("Maps"), this);
    auto* listLay = new QVBoxLayout(listBox);
    m_mapTable = new QTableWidget(this);
    m_mapTable->setColumnCount(4);
    m_mapTable->setHorizontalHeaderLabels(
        { tr("Nom"), tr("Adresse"), tr("Taille"), tr("Score") });
    m_mapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mapTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mapTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_mapTable->verticalHeader()->setVisible(false);
    m_mapTable->horizontalHeader()->setStretchLastSection(true);
    m_mapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    listLay->addWidget(m_mapTable);
    splitter->addWidget(listBox);

    auto* gridBox = new QGroupBox(tr("Édition"), this);
    auto* gridLay = new QVBoxLayout(gridBox);
    m_infoLabel = new QLabel(tr("Aucune map sélectionnée"), this);
    m_infoLabel->setStyleSheet("color:#7c8fa6;");
    gridLay->addWidget(m_infoLabel);
    m_grid = new QTableWidget(this);
    m_grid->horizontalHeader()->setVisible(true);
    m_grid->verticalHeader()->setVisible(true);
    gridLay->addWidget(m_grid, 1);
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
    connect(m_applyPctBtn,    &QPushButton::clicked, this, &MapEditorPanel::applyPercent);
    connect(m_applyStage1Btn, &QPushButton::clicked, this, &MapEditorPanel::applyFullStage1);
    connect(m_gotoHexBtn,     &QPushButton::clicked, this, &MapEditorPanel::gotoHex);
    connect(m_openDamosBtn,   &QPushButton::clicked, this, &MapEditorPanel::runOpenDamos);
}

void MapEditorPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444; font-size:11px;"
                                       : "color:#7c8fa6; font-size:11px;");
    m_statusLabel->setText(msg);
}

void MapEditorPanel::refreshMaps() {
    m_entries.clear();

    if (m_doc && m_doc->isLoaded()) {
        const QByteArray& rom = m_doc->rom();
        auto span = constSpan(rom);

        // (a) Maps connues du catalogue Stage 1 pour l'ECU du document.
        auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
        if (ecu && ecu->stage1Maps) {
            for (const auto& m : *ecu->stage1Maps) {
                MapEntry e;
                e.name       = QString::fromUtf8(m.name.data(), static_cast<int>(m.name.size()));
                e.address    = m.address;
                e.score      = -1.0;
                e.stage1     = true;
                e.defaultPct = m.defaultPct;
                // Lit les dimensions si l'adresse est valide.
                auto md = ecu::readMapData(span, m.address);
                if (md) { e.nx = md->nx; e.ny = md->ny; }
                m_entries.push_back(std::move(e));
            }
        }
    }

    rebuildMapTable();

    if (m_entries.empty()) {
        if (m_doc && m_doc->isLoaded())
            setStatus(tr("Aucune map connue pour cet ECU — utilisez « Chercher maps »."));
        else
            setStatus(tr("Aucune ROM chargée."));
    } else {
        setStatus(tr("%1 map(s) listée(s).").arg(m_entries.size()));
    }
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

void MapEditorPanel::loadGrid(quint32 address) {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    auto span = constSpan(m_doc->rom());
    auto md   = ecu::readMapData(span, address);
    if (!md) {
        m_loadingGrid = true;
        m_grid->clear();
        m_grid->setRowCount(0);
        m_grid->setColumnCount(0);
        m_loadingGrid = false;
        m_curNx = m_curNy = 0;
        m_infoLabel->setText(tr("Map illisible à %1").arg(hex32(address)));
        setStatus(QString::fromStdString(md.error()), true);
        return;
    }

    m_currentAddr = address;
    m_dataOff     = md->dataOff;
    m_curNx       = md->nx;
    m_curNy       = md->ny;

    m_loadingGrid = true;
    m_grid->clear();
    m_grid->setRowCount(md->ny);
    m_grid->setColumnCount(md->nx);

    // En-têtes = valeurs d'axes.
    QStringList hHeaders, vHeaders;
    for (int x = 0; x < md->nx; ++x)
        hHeaders << QString::number(md->xAxis[static_cast<std::size_t>(x)]);
    for (int y = 0; y < md->ny; ++y)
        vHeaders << QString::number(md->yAxis[static_cast<std::size_t>(y)]);
    m_grid->setHorizontalHeaderLabels(hHeaders);
    m_grid->setVerticalHeaderLabels(vHeaders);

    for (int y = 0; y < md->ny; ++y) {
        for (int x = 0; x < md->nx; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(md->nx)
                                  + static_cast<std::size_t>(x);
            auto* item = new QTableWidgetItem(QString::number(md->data[idx]));
            item->setTextAlignment(Qt::AlignCenter);
            m_grid->setItem(y, x, item);
        }
    }
    m_loadingGrid = false;

    m_infoLabel->setText(tr("%1 — %2 — %3×%4 — données @ %5")
        .arg(m_entries[static_cast<std::size_t>(m_currentRow)].name)
        .arg(hex32(address))
        .arg(md->nx).arg(md->ny)
        .arg(hex32(static_cast<quint32>(md->dataOff))));
    setStatus(tr("Map chargée."));
}

void MapEditorPanel::onCellChanged(QTableWidgetItem* item) {
    if (m_loadingGrid || !item || !m_doc || !m_doc->isLoaded())
        return;
    if (m_curNx <= 0 || m_curNy <= 0)
        return;

    bool ok = false;
    const double value = item->text().toDouble(&ok);
    if (!ok) {
        setStatus(tr("Valeur invalide."), true);
        return;
    }

    const int row = item->row();
    const int col = item->column();
    const std::size_t idx = static_cast<std::size_t>(row) * static_cast<std::size_t>(m_curNx)
                          + static_cast<std::size_t>(col);
    const std::size_t off = m_dataOff + idx * 2;

    QByteArray& rom = m_doc->romMutable();
    if (off + 2 > static_cast<std::size_t>(rom.size())) {
        setStatus(tr("Offset hors ROM."), true);
        return;
    }

    ecu::writeSwordBE(mutSpan(rom), off, value);
    m_doc->markModified(static_cast<qsizetype>(off), 2);

    // Relit la valeur réellement écrite (clamp éventuel) et la réaffiche.
    const int16_t written = ecu::readSwordBE(constSpan(m_doc->rom()), off);
    if (written != static_cast<int16_t>(value)) {
        m_loadingGrid = true;
        item->setText(QString::number(written));
        m_loadingGrid = false;
    }
    setStatus(tr("Cellule [%1,%2] @ %3 = %4")
        .arg(row).arg(col).arg(hex32(static_cast<quint32>(off))).arg(written));
}

void MapEditorPanel::applyPercent() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }
    if (m_currentRow < 0 || m_currentRow >= static_cast<int>(m_entries.size())) {
        setStatus(tr("Sélectionnez une map d'abord."), true);
        return;
    }

    const quint32 addr = m_entries[static_cast<std::size_t>(m_currentRow)].address;
    const double  pct  = m_pctSpin->value();

    QByteArray& rom = m_doc->romMutable();
    auto result = ecu::applyPctToMap(mutSpan(rom), addr, pct);
    if (!result) {
        setStatus(QString::fromStdString(result.error()), true);
        return;
    }

    if (!result->empty())
        m_doc->markModified();

    setStatus(tr("%1 % appliqué à %2 — %3 cellule(s) modifiée(s).")
        .arg(pct).arg(hex32(addr)).arg(result->size()));

    // Recharge la grille si c'est la map affichée.
    if (addr == m_currentAddr)
        loadGrid(addr);
}

void MapEditorPanel::applyFullStage1() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    auto ecuEntry = ecu::getEcu(m_doc->ecuId().toStdString());
    if (!ecuEntry || !ecuEntry->stage1Maps || ecuEntry->stage1Maps->empty()) {
        setStatus(tr("Aucune map Stage 1 pour cet ECU."), true);
        return;
    }

    if (QMessageBox::question(this, tr("Stage 1 complet"),
            tr("Appliquer le pourcentage par défaut à toutes les maps Stage 1 ?"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    QByteArray& rom = m_doc->romMutable();
    int applied = 0, failed = 0;
    std::size_t totalCells = 0;

    for (const auto& m : *ecuEntry->stage1Maps) {
        auto result = ecu::applyPctToMap(mutSpan(rom), m.address,
                                         static_cast<double>(m.defaultPct));
        if (result) {
            totalCells += result->size();
            ++applied;
        } else {
            ++failed;
        }
    }

    if (totalCells > 0)
        m_doc->markModified();

    setStatus(tr("Stage 1 : %1 map(s) appliquée(s), %2 échec(s), %3 cellule(s).")
        .arg(applied).arg(failed).arg(totalCells), failed > 0);

    // Recharge la grille courante pour refléter les changements.
    if (m_currentAddr != 0 && m_curNx > 0)
        loadGrid(m_currentAddr);
}

void MapEditorPanel::gotoHex() {
    if (m_currentRow < 0 || m_currentRow >= static_cast<int>(m_entries.size())) {
        setStatus(tr("Sélectionnez une map d'abord."), true);
        return;
    }
    emit gotoAddressRequested(m_entries[static_cast<std::size_t>(m_currentRow)].address);
}

void MapEditorPanel::runMapFinder() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    setStatus(tr("Recherche heuristique de maps en cours..."));

    auto span = constSpan(m_doc->rom());
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

    // (2) Relocalise chaque caractéristique en scannant la ROM.
    const QByteArray& rom = m_doc->rom();
    QByteArrayView view(rom.constData(), rom.size());
    auto results = ecu::OpenDamos{}.relocate(*recipe, view);

    // (3) Réinjecte les résultats dans la liste : on conserve les maps Stage 1
    // du catalogue puis on ajoute les maps relocalisées par open_damos.
    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1 && !e.openDamos) kept.push_back(e);
    m_entries = std::move(kept);

    auto span = constSpan(rom);
    int byFingerprint = 0;
    int total = 0;

    for (const auto& r : results) {
        ++total;
        const bool fallback = (r.addressSource == ecu::AddressSource::DefaultFallback);
        if (!fallback) ++byFingerprint;

        MapEntry e;
        e.name      = QString::fromStdString(r.name);
        e.address   = static_cast<quint32>(r.address);
        e.score     = r.score;
        e.stage1    = false;
        e.openDamos = true;
        e.fallback  = fallback;

        // Dimensions lues directement depuis la ROM à l'adresse résolue.
        if (auto md = ecu::readMapData(span, e.address)) {
            e.nx = md->nx;
            e.ny = md->ny;
        }

        // Libellé de la colonne « Score » : source + mode + confiance.
        QString src;
        switch (r.addressSource) {
            case ecu::AddressSource::Fingerprint: src = tr("empreinte"); break;
            case ecu::AddressSource::Anchor:      src = tr("ancre");     break;
            case ecu::AddressSource::DefaultFallback: src = tr("défaut"); break;
        }
        const QString mode = QString::fromStdString(r.matchMode);
        e.matchInfo = mode.isEmpty()
            ? QString("%1 (%2)").arg(src).arg(r.score, 0, 'f', 2)
            : QString("%1 %2 (%3)").arg(src, mode).arg(r.score, 0, 'f', 2);

        m_entries.push_back(std::move(e));
    }

    rebuildMapTable();

    setStatus(tr("open_damos : %1/%2 maps relocalisées par empreinte.")
                  .arg(byFingerprint)
                  .arg(total));
}

} // namespace ecu_studio
