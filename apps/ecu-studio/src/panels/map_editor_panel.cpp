#include "map_editor_panel.h"
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
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QClipboard>
#include <QColor>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>

#include "ecu/EcuCatalog.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/OpenDamos.hpp"

namespace ecu_studio {

namespace {

QString hex32(quint32 v) {
    return QString("0x%1").arg(v, 6, 16, QChar('0')).toUpper().replace("0X", "0x");
}

// Couleur froid→chaud (bleu → cyan → vert → jaune → rouge) pour t∈[0,1].
// Teintes assombries pour rester lisibles sur le thème sombre.
QColor heatColor(double t) {
    t = std::clamp(t, 0.0, 1.0);
    double r, g, b;
    if (t < 0.25)      { double u = t / 0.25;        r = 0;            g = u;            b = 1; }
    else if (t < 0.5)  { double u = (t - 0.25) / 0.25; r = 0;          g = 1;            b = 1 - u; }
    else if (t < 0.75) { double u = (t - 0.5) / 0.25;  r = u;          g = 1;            b = 0; }
    else               { double u = (t - 0.75) / 0.25; r = 1;          g = 1 - u;        b = 0; }
    // Assombrir pour le fond d'une cellule (texte clair par-dessus).
    return QColor(int(r * 150), int(g * 150), int(b * 150));
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
    m_mapTable->horizontalHeader()->setStretchLastSection(true);
    m_mapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    listLay->addWidget(m_mapTable);
    splitter->addWidget(listBox);

    auto* gridBox = new QGroupBox(tr("Édition"), this);
    auto* gridLay = new QVBoxLayout(gridBox);
    m_infoLabel = new QLabel(tr("Aucune map sélectionnée"), this);
    m_infoLabel->setStyleSheet("color:#7c8fa6;");
    gridLay->addWidget(m_infoLabel);

    // ── Barre d'opérations sur la sélection ────────────────────────────────
    auto* opRow = new QHBoxLayout;
    opRow->setSpacing(4);
    m_heatmapChk = new QCheckBox(tr("Heatmap"), this);
    m_heatmapChk->setChecked(true);
    m_heatmapChk->setToolTip(tr("Colore chaque cellule selon sa valeur (froid→chaud)."));
    opRow->addWidget(m_heatmapChk);
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
    gridLay->addWidget(m_grid, 1);

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
        auto span = constByteSpan(rom);

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

void MapEditorPanel::loadGrid(quint32 address) {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    auto span = constByteSpan(m_doc->rom());
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

    recolorHeatmap();
    m_selLabel->setText(tr("Sélection : —"));
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

    ecu::writeSwordBE(mutByteSpan(rom), off, value);
    m_doc->markModified(static_cast<qsizetype>(off), 2);

    // Relit la valeur réellement écrite (clamp éventuel) et la réaffiche.
    const int16_t written = ecu::readSwordBE(constByteSpan(m_doc->rom()), off);
    if (written != static_cast<int16_t>(value)) {
        m_loadingGrid = true;
        item->setText(QString::number(written));
        m_loadingGrid = false;
    }
    recolorHeatmap();
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
    auto result = ecu::applyPctToMap(mutByteSpan(rom), addr, pct);
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
        auto result = ecu::applyPctToMap(mutByteSpan(rom), m.address,
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

// ── Filtre de la liste des maps ───────────────────────────────────────────────

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

// ── Heatmap ───────────────────────────────────────────────────────────────────

void MapEditorPanel::recolorHeatmap() {
    if (!m_grid || m_curNx <= 0 || m_curNy <= 0) return;

    const QColor plain(0x11, 0x18, 0x27);   // fond normal du thème
    if (!m_heatmapChk || !m_heatmapChk->isChecked()) {
        for (int y = 0; y < m_curNy; ++y)
            for (int x = 0; x < m_curNx; ++x)
                if (auto* it = m_grid->item(y, x)) {
                    it->setBackground(plain);
                    it->setForeground(QColor(0xe5, 0xe7, 0xeb));
                }
        return;
    }

    double mn = std::numeric_limits<double>::max();
    double mx = std::numeric_limits<double>::lowest();
    for (int y = 0; y < m_curNy; ++y)
        for (int x = 0; x < m_curNx; ++x)
            if (auto* it = m_grid->item(y, x)) {
                const double v = it->text().toDouble();
                mn = std::min(mn, v);
                mx = std::max(mx, v);
            }
    const double span = (mx > mn) ? (mx - mn) : 1.0;

    for (int y = 0; y < m_curNy; ++y)
        for (int x = 0; x < m_curNx; ++x)
            if (auto* it = m_grid->item(y, x)) {
                const double t = (it->text().toDouble() - mn) / span;
                const QColor bg = heatColor(t);
                it->setBackground(bg);
                // Texte clair ou sombre selon la luminance du fond.
                const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
                it->setForeground(lum > 110 ? QColor(0x11, 0x18, 0x27) : QColor(0xf3, 0xf4, 0xf6));
            }
}

// ── Readout min/max/moy de la sélection ───────────────────────────────────────

void MapEditorPanel::onGridSelectionChanged() {
    const auto items = m_grid->selectedItems();
    if (items.isEmpty()) {
        m_selLabel->setText(tr("Sélection : —"));
        return;
    }
    double mn = std::numeric_limits<double>::max();
    double mx = std::numeric_limits<double>::lowest();
    double sum = 0.0;
    int n = 0;
    for (auto* it : items) {
        bool ok = false;
        const double v = it->text().toDouble(&ok);
        if (!ok) continue;
        mn = std::min(mn, v); mx = std::max(mx, v); sum += v; ++n;
    }
    if (n == 0) { m_selLabel->setText(tr("Sélection : —")); return; }
    m_selLabel->setText(tr("Sélection : %1 cellule(s)   min %2   max %3   moy %4")
        .arg(n).arg(mn).arg(mx).arg(sum / n, 0, 'f', 1));
}

// ── Opérations sur la sélection multi-cellules ────────────────────────────────

void MapEditorPanel::applyToSelection(const QString& label,
                                      const std::function<double(int, double)>& fn) {
    if (!m_doc || !m_doc->isLoaded() || m_curNx <= 0 || m_curNy <= 0) {
        setStatus(tr("Aucune map chargée."), true);
        return;
    }
    const auto items = m_grid->selectedItems();
    if (items.isEmpty()) {
        setStatus(tr("Sélectionnez d'abord des cellules."), true);
        return;
    }

    QByteArray& rom = m_doc->romMutable();
    auto romSpan = mutByteSpan(rom);
    int changed = 0, idx = 0;
    bool clamped = false;
    m_loadingGrid = true;
    for (auto* it : items) {
        bool ok = false;
        const double cur = it->text().toDouble(&ok);
        if (!ok) { ++idx; continue; }
        const double want = fn(idx++, cur);

        const std::size_t cellIdx = static_cast<std::size_t>(it->row()) * static_cast<std::size_t>(m_curNx)
                                   + static_cast<std::size_t>(it->column());
        const std::size_t off = m_dataOff + cellIdx * 2;
        if (off + 2 > static_cast<std::size_t>(rom.size())) continue;

        ecu::writeSwordBE(romSpan, off, want);
        const int16_t written = ecu::readSwordBE(constByteSpan(m_doc->rom()), off);
        if (static_cast<double>(written) != want) clamped = true;
        it->setText(QString::number(written));
        ++changed;
    }
    m_loadingGrid = false;

    if (changed > 0) {
        m_doc->markModified();
        recolorHeatmap();
        onGridSelectionChanged();
    }
    setStatus(tr("%1 : %2 cellule(s) modifiée(s)%3.")
        .arg(label).arg(changed).arg(clamped ? tr(" (valeurs bornées)") : QString()),
        clamped);
}

void MapEditorPanel::opSet() {
    bool ok = false;
    const double v = QInputDialog::getDouble(this, tr("Fixer"),
        tr("Nouvelle valeur :"), 0, -32768, 32767, 0, &ok);
    if (!ok) return;
    applyToSelection(tr("Fixer"), [v](int, double) { return v; });
}

void MapEditorPanel::opAdd() {
    bool ok = false;
    const double d = QInputDialog::getDouble(this, tr("Ajouter"),
        tr("Valeur à ajouter (peut être négative) :"), 0, -65535, 65535, 0, &ok);
    if (!ok) return;
    applyToSelection(tr("Ajout %1").arg(d), [d](int, double cur) { return cur + d; });
}

void MapEditorPanel::opMultiply() {
    bool ok = false;
    const double pct = QInputDialog::getDouble(this, tr("Multiplier"),
        tr("Pourcentage (ex. +15 = ×1.15) :"), 0, -99, 1000, 1, &ok);
    if (!ok) return;
    const double f = 1.0 + pct / 100.0;
    applyToSelection(tr("× %1 %").arg(pct), [f](int, double cur) { return cur * f; });
}

void MapEditorPanel::opInterpolate() {
    // Interpole linéairement chaque ligne sélectionnée entre sa 1re et sa
    // dernière cellule sélectionnée.
    if (!m_doc || !m_doc->isLoaded() || m_curNx <= 0) {
        setStatus(tr("Aucune map chargée."), true);
        return;
    }
    const auto items = m_grid->selectedItems();
    if (items.size() < 3) {
        setStatus(tr("Sélectionnez au moins 3 cellules sur une ligne."), true);
        return;
    }

    // Regroupe les colonnes sélectionnées par ligne.
    std::map<int, std::vector<int>> byRow;
    for (auto* it : items) byRow[it->row()].push_back(it->column());

    QByteArray& rom = m_doc->romMutable();
    auto romSpan = mutByteSpan(rom);
    int changed = 0;
    m_loadingGrid = true;
    for (auto& [row, cols] : byRow) {
        if (cols.size() < 3) continue;
        std::sort(cols.begin(), cols.end());
        const int c0 = cols.front(), c1 = cols.back();
        auto* a = m_grid->item(row, c0);
        auto* b = m_grid->item(row, c1);
        if (!a || !b) continue;
        const double v0 = a->text().toDouble(), v1 = b->text().toDouble();
        for (int c : cols) {
            const double t = double(c - c0) / double(c1 - c0);
            const double want = v0 + (v1 - v0) * t;
            const std::size_t cellIdx = static_cast<std::size_t>(row) * static_cast<std::size_t>(m_curNx)
                                      + static_cast<std::size_t>(c);
            const std::size_t off = m_dataOff + cellIdx * 2;
            if (off + 2 > static_cast<std::size_t>(rom.size())) continue;
            ecu::writeSwordBE(romSpan, off, want);
            const int16_t written = ecu::readSwordBE(constByteSpan(m_doc->rom()), off);
            if (auto* it = m_grid->item(row, c)) it->setText(QString::number(written));
            ++changed;
        }
    }
    m_loadingGrid = false;
    if (changed > 0) { m_doc->markModified(); recolorHeatmap(); onGridSelectionChanged(); }
    setStatus(tr("Interpolation : %1 cellule(s) modifiée(s).").arg(changed));
}

void MapEditorPanel::opSmooth() {
    // Lissage 3×3 : remplace chaque cellule sélectionnée par la moyenne de son
    // voisinage (lecture sur la grille d'origine pour éviter la propagation).
    if (!m_doc || !m_doc->isLoaded() || m_curNx <= 0 || m_curNy <= 0) {
        setStatus(tr("Aucune map chargée."), true);
        return;
    }
    const auto items = m_grid->selectedItems();
    if (items.isEmpty()) {
        setStatus(tr("Sélectionnez d'abord des cellules."), true);
        return;
    }

    // Snapshot des valeurs courantes.
    std::vector<std::vector<double>> snap(m_curNy, std::vector<double>(m_curNx, 0.0));
    for (int y = 0; y < m_curNy; ++y)
        for (int x = 0; x < m_curNx; ++x)
            if (auto* it = m_grid->item(y, x)) snap[y][x] = it->text().toDouble();

    QByteArray& rom = m_doc->romMutable();
    auto romSpan = mutByteSpan(rom);
    int changed = 0;
    m_loadingGrid = true;
    for (auto* it : items) {
        const int y = it->row(), x = it->column();
        double sum = 0.0; int cnt = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int ny = y + dy, nx = x + dx;
                if (ny < 0 || ny >= m_curNy || nx < 0 || nx >= m_curNx) continue;
                sum += snap[ny][nx]; ++cnt;
            }
        if (cnt == 0) continue;
        const double want = sum / cnt;
        const std::size_t cellIdx = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_curNx)
                                  + static_cast<std::size_t>(x);
        const std::size_t off = m_dataOff + cellIdx * 2;
        if (off + 2 > static_cast<std::size_t>(rom.size())) continue;
        ecu::writeSwordBE(romSpan, off, want);
        const int16_t written = ecu::readSwordBE(constByteSpan(m_doc->rom()), off);
        it->setText(QString::number(written));
        ++changed;
    }
    m_loadingGrid = false;
    if (changed > 0) { m_doc->markModified(); recolorHeatmap(); onGridSelectionChanged(); }
    setStatus(tr("Lissage : %1 cellule(s) modifiée(s).").arg(changed));
}

// ── Copier / coller TSV (compatible Excel / LibreOffice) ──────────────────────

void MapEditorPanel::copyRegionTsv() {
    const auto items = m_grid->selectedItems();
    if (items.isEmpty()) {
        // Par défaut : toute la map.
        if (m_curNx <= 0 || m_curNy <= 0) { setStatus(tr("Rien à copier."), true); return; }
    }
    int r0 = std::numeric_limits<int>::max(), r1 = -1;
    int c0 = std::numeric_limits<int>::max(), c1 = -1;
    if (items.isEmpty()) { r0 = 0; r1 = m_curNy - 1; c0 = 0; c1 = m_curNx - 1; }
    else for (auto* it : items) {
        r0 = std::min(r0, it->row()); r1 = std::max(r1, it->row());
        c0 = std::min(c0, it->column()); c1 = std::max(c1, it->column());
    }

    QString out;
    for (int y = r0; y <= r1; ++y) {
        for (int x = c0; x <= c1; ++x) {
            if (x != c0) out += '\t';
            auto* it = m_grid->item(y, x);
            out += it ? it->text() : QString();
        }
        out += '\n';
    }
    QApplication::clipboard()->setText(out);
    setStatus(tr("Région %1×%2 copiée (TSV).").arg(c1 - c0 + 1).arg(r1 - r0 + 1));
}

void MapEditorPanel::pasteRegionTsv() {
    if (!m_doc || !m_doc->isLoaded() || m_curNx <= 0 || m_curNy <= 0) {
        setStatus(tr("Aucune map chargée."), true);
        return;
    }
    auto* anchor = m_grid->currentItem();
    if (!anchor) {
        setStatus(tr("Cliquez une cellule de destination d'abord."), true);
        return;
    }
    const int baseRow = anchor->row(), baseCol = anchor->column();

    const QString clip = QApplication::clipboard()->text();
    const QStringList rows = clip.split('\n', Qt::SkipEmptyParts);
    if (rows.isEmpty()) { setStatus(tr("Presse-papiers vide."), true); return; }

    QByteArray& rom = m_doc->romMutable();
    auto romSpan = mutByteSpan(rom);
    int changed = 0;
    m_loadingGrid = true;
    for (int ry = 0; ry < rows.size(); ++ry) {
        const QStringList cells = rows[ry].split('\t');
        for (int rx = 0; rx < cells.size(); ++rx) {
            const int y = baseRow + ry, x = baseCol + rx;
            if (y >= m_curNy || x >= m_curNx) continue;
            bool ok = false;
            const double v = cells[rx].trimmed().toDouble(&ok);
            if (!ok) continue;
            const std::size_t cellIdx = static_cast<std::size_t>(y) * static_cast<std::size_t>(m_curNx)
                                      + static_cast<std::size_t>(x);
            const std::size_t off = m_dataOff + cellIdx * 2;
            if (off + 2 > static_cast<std::size_t>(rom.size())) continue;
            ecu::writeSwordBE(romSpan, off, v);
            const int16_t written = ecu::readSwordBE(constByteSpan(m_doc->rom()), off);
            if (auto* it = m_grid->item(y, x)) it->setText(QString::number(written));
            ++changed;
        }
    }
    m_loadingGrid = false;
    if (changed > 0) { m_doc->markModified(); recolorHeatmap(); onGridSelectionChanged(); }
    setStatus(tr("Collage : %1 cellule(s) modifiée(s).").arg(changed), changed == 0);
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

    auto span = constByteSpan(rom);
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
