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
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QStyledItemDelegate>

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

// Delegate qui peint manuellement le fond depuis Qt::BackgroundRole. Contourne
// le piège Qt classique : dès qu'une stylesheet global stylise QTableWidget::item,
// les setBackground() programmatiques sont ignorés. On peint nous-mêmes.
//
// Rôles supplémentaires :
//   Qt::UserRole       -> bool, cellule modifiée vs stock (liseré jaune)
//   Qt::UserRole + 1   -> double, valeur baseline si mode fantôme actif (NaN sinon)
//   Qt::UserRole + 2   -> bool, mode fantôme activé pour cette cellule
class HeatmapDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        const QVariant bg = idx.data(Qt::BackgroundRole);
        if (bg.canConvert<QBrush>())
            p->fillRect(opt.rect, bg.value<QBrush>());
        // Cellule modifiée vs stock : liseré jaune.
        if (idx.data(Qt::UserRole).toBool()) {
            p->save();
            QPen pen(QColor("#fbbf24"));
            pen.setWidth(2);
            p->setPen(pen);
            p->drawRect(opt.rect.adjusted(1, 1, -1, -1));
            p->restore();
        }
        // Sélection : voile semi-transparent par-dessus.
        if (opt.state & QStyle::State_Selected)
            p->fillRect(opt.rect, QColor(99, 102, 241, 90));

        // Texte centré, couleur depuis ForegroundRole.
        const QVariant fg = idx.data(Qt::ForegroundRole);
        if (fg.canConvert<QBrush>())
            p->setPen(fg.value<QBrush>().color());
        else
            p->setPen(QColor(0xe5, 0xe7, 0xeb));

        const bool ghost = idx.data(Qt::UserRole + 2).toBool();
        const QVariant baseVar = idx.data(Qt::UserRole + 1);
        const QString curText = idx.data(Qt::DisplayRole).toString();

        if (!ghost || !baseVar.isValid()) {
            p->drawText(opt.rect, Qt::AlignCenter, curText);
        } else {
            // Mode fantôme : valeur courante centrée, baseline en petit sous la
            // valeur courante, et un triangle de delta (↑/↓) à droite.
            const double base = baseVar.toDouble();
            const double cur  = curText.toDouble();
            const bool changed = (cur != base);

            QRect topRect    = opt.rect.adjusted(0, 0, 0, -opt.rect.height() / 2);
            QRect bottomRect = opt.rect.adjusted(0, opt.rect.height() / 2, 0, 0);

            QFont curFont = opt.font;
            curFont.setBold(changed);
            p->save();
            p->setFont(curFont);
            p->drawText(topRect, Qt::AlignCenter, curText);
            p->restore();

            // Baseline en bas, gris translucide.
            p->save();
            QFont baseFont = opt.font;
            baseFont.setPointSizeF(std::max(7.0, baseFont.pointSizeF() - 1.5));
            baseFont.setItalic(true);
            p->setFont(baseFont);
            p->setPen(QColor(160, 174, 192, 180));
            p->drawText(bottomRect, Qt::AlignCenter,
                        QString::number(base, 'f', 0));
            p->restore();

            // Triangle delta (↑ rouge si > / ↓ vert si <) en haut-droite.
            if (changed) {
                p->save();
                const int s = std::max(4, opt.rect.height() / 6);
                QPolygon tri;
                const int x = opt.rect.right() - s - 2;
                const int y = opt.rect.top() + s + 1;
                if (cur > base) {
                    tri << QPoint(x, y) << QPoint(x + 2*s, y) << QPoint(x + s, y - s);
                    p->setBrush(QColor(239, 68, 68));
                } else {
                    tri << QPoint(x, y - s) << QPoint(x + 2*s, y - s) << QPoint(x + s, y);
                    p->setBrush(QColor(34, 197, 94));
                }
                p->setPen(Qt::NoPen);
                p->drawPolygon(tri);
                p->restore();
            }
        }
    }
};

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
    m_ghostChk->setToolTip(tr("Superpose la valeur de la ROM d'origine sous la "
                              "valeur courante (en italique) + triangle de delta."));
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

void MapEditorPanel::refreshMaps() {
    m_entries.clear();

    if (!m_doc || !m_doc->isLoaded()) {
        rebuildMapTable();
        setStatus(tr("Aucune ROM chargée."));
        return;
    }

    const QByteArray& rom = m_doc->rom();
    auto span = constByteSpan(rom);

    // Priorité : open_damos — relocalise par empreinte et fournit factor/offset/stock.
    auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
    if (recipe) {
        QByteArrayView view(rom.constData(), rom.size());
        auto results = ecu::OpenDamos{}.relocate(*recipe, view);

        int byFingerprint = 0;
        for (const auto& r : results) {
            const bool fallback = (r.addressSource == ecu::AddressSource::DefaultFallback);
            if (!fallback) ++byFingerprint;

            MapEntry e;
            e.name      = QString::fromStdString(r.name);
            e.address   = static_cast<quint32>(r.address);
            e.score     = r.score;
            e.openDamos = true;
            e.fallback  = fallback;

            if (auto md = ecu::readMapData(span, e.address)) {
                e.nx = md->nx;
                e.ny = md->ny;
            }

            // Récupère factor/offset/stock depuis l'entrée du recipe.
            auto it = std::find_if(recipe->characteristics.begin(),
                                   recipe->characteristics.end(),
                                   [&](const ecu::DamosEntry& de) {
                                       return de.name == r.name;
                                   });
            if (it != recipe->characteristics.end()) {
                e.factor         = it->data.factor;
                e.offset         = it->data.offset;
                e.hasConversion  = (e.factor != 1.0 || e.offset != 0.0);
                e.stockRaw       = it->stockRawValue;
                e.stockPhys      = it->stockPhysValue;
                e.description    = it->description;
                e.unit           = it->data.unit;
                if (!it->axes.empty()) e.xAxisUnit = it->axes[0].unit;
                if (it->axes.size() > 1) e.yAxisUnit = it->axes[1].unit;
            }

            QString src;
            switch (r.addressSource) {
                case ecu::AddressSource::Fingerprint:    src = tr("empreinte"); break;
                case ecu::AddressSource::Anchor:         src = tr("ancre");     break;
                case ecu::AddressSource::DefaultFallback: src = tr("défaut");   break;
            }
            const QString mode = QString::fromStdString(r.matchMode);
            e.matchInfo = mode.isEmpty()
                ? QString("%1 (%2)").arg(src).arg(r.score, 0, 'f', 2)
                : QString("%1 %2 (%3)").arg(src, mode).arg(r.score, 0, 'f', 2);

            m_entries.push_back(std::move(e));
        }

        rebuildMapTable();
        setStatus(tr("open_damos : %1/%2 maps relocalisées par empreinte.")
                      .arg(byFingerprint).arg(results.size()));
        return;
    }

    // Repli : maps Stage 1 du catalogue ECU (adresses fixes).
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

void MapEditorPanel::loadGrid(quint32 address) {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    auto span = constByteSpan(m_doc->rom());
    auto md   = ecu::readMapData(span, address);
    if (!md) {
        m_loadingGrid = true;
        m_grid->clearContents();
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
    // clearContents() (vs clear()) préserve la config des en-têtes — sinon les
    // tailles utilisateur seraient remises à zéro à chaque sélection.
    m_grid->clearContents();
    m_grid->setRowCount(md->ny);
    m_grid->setColumnCount(md->nx);
    // Réapplique la taille de cellule mémorisée pour que toutes les maps
    // s'affichent avec la même grille.
    m_applyingCellSize = true;
    for (int c = 0; c < md->nx; ++c) m_grid->setColumnWidth(c, m_colWidth);
    for (int r = 0; r < md->ny; ++r) m_grid->setRowHeight(r, m_rowHeight);
    m_applyingCellSize = false;

    const MapEntry& entry = m_entries[static_cast<std::size_t>(m_currentRow)];
    const bool hasConv = entry.hasConversion;
    const double factor = entry.factor;
    const double offset = entry.offset;
    const QString xUnit = QString::fromStdString(entry.xAxisUnit);
    const QString yUnit = QString::fromStdString(entry.yAxisUnit);
    const QString dUnit = QString::fromStdString(entry.unit);

    // En-têtes = valeurs d'axes + unité (rpm, %, …) en suffixe.
    QStringList hHeaders, vHeaders;
    for (int x = 0; x < md->nx; ++x) {
        const QString v = QString::number(md->xAxis[static_cast<std::size_t>(x)]);
        hHeaders << (xUnit.isEmpty() ? v : QString("%1 %2").arg(v, xUnit));
    }
    for (int y = 0; y < md->ny; ++y) {
        const QString v = QString::number(md->yAxis[static_cast<std::size_t>(y)]);
        vHeaders << (yUnit.isEmpty() ? v : QString("%1 %2").arg(v, yUnit));
    }
    m_grid->setHorizontalHeaderLabels(hHeaders);
    m_grid->setVerticalHeaderLabels(vHeaders);

    // Min/max pour la heatmap, calculés depuis md->data directement.
    const bool useHeat = m_heatmapChk && m_heatmapChk->isChecked();
    int16_t mn = std::numeric_limits<int16_t>::max();
    int16_t mx = std::numeric_limits<int16_t>::lowest();
    if (useHeat) {
        for (int16_t v : md->data) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    }
    const double heatRange = (mx > mn) ? double(mx - mn) : 1.0;
    const QColor plainBg(0x11, 0x18, 0x27);
    const QColor plainFg(0xe5, 0xe7, 0xeb);

    // Mode fantôme : lit la même map à la même adresse depuis la baseline pour
    // injecter les valeurs d'origine dans UserRole+1 (le delegate les peint).
    const bool ghost = m_ghostChk && m_ghostChk->isChecked() && m_doc->hasBaseline();
    std::optional<ecu::MapData> baseMd;
    if (ghost) {
        auto bspan = constByteSpan(m_doc->baseline());
        if (auto bmd = ecu::readMapData(bspan, address)) baseMd = std::move(*bmd);
    }

    for (int y = 0; y < md->ny; ++y) {
        for (int x = 0; x < md->nx; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(md->nx)
                                  + static_cast<std::size_t>(x);
            const int16_t raw = md->data[idx];
            auto* item = new QTableWidgetItem(QString::number(raw));
            item->setTextAlignment(Qt::AlignCenter);

            // Mode fantôme : pousse la valeur baseline + flag d'activation.
            if (ghost && baseMd && idx < baseMd->data.size()) {
                item->setData(Qt::UserRole + 1, double(baseMd->data[idx]));
                item->setData(Qt::UserRole + 2, true);
            }

            // Tooltip : valeur physique si conversion disponible + stock si connu.
            if (hasConv) {
                const double phys = raw * factor + offset;
                const QString unitSuf = dUnit.isEmpty() ? QString() : QString(" %1").arg(dUnit);
                QString tip = QString("raw=%1  phys=%2%3").arg(raw).arg(phys, 0, 'f', 3).arg(unitSuf);
                if (entry.stockRaw) {
                    const double stockP = *entry.stockRaw * factor + offset;
                    tip += QString("\nstock raw=%1  phys=%2%3")
                               .arg(*entry.stockRaw).arg(stockP, 0, 'f', 3).arg(unitSuf);
                }
                item->setToolTip(tip);
            }

            // Heatmap inline — applique le fond avant insertion dans le grid.
            if (useHeat) {
                const double t = (double(raw) - mn) / heatRange;
                const QColor bg = heatColor(t);
                item->setBackground(bg);
                const double lum = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
                item->setForeground(lum > 110 ? QColor(0x11, 0x18, 0x27) : QColor(0xf3, 0xf4, 0xf6));
            } else {
                item->setBackground(plainBg);
                item->setForeground(plainFg);
            }
            // Marqueur cellule modifiée (bordure jaune via icône) — visible quel que soit le fond.
            if (hasConv && entry.stockRaw && raw != *entry.stockRaw)
                item->setData(Qt::UserRole, true);

            m_grid->setItem(y, x, item);
        }
    }
    m_loadingGrid = false;

    // Info label enrichi : nom + adresse + dimensions + formule de conversion.
    QString info = tr("%1 — %2 — %3×%4 — données @ %5")
        .arg(entry.name)
        .arg(hex32(address))
        .arg(md->nx).arg(md->ny)
        .arg(hex32(static_cast<quint32>(md->dataOff)));
    if (hasConv) {
        info += QString("   |   phys = raw × %1 + %2").arg(factor).arg(offset);
        if (!dUnit.isEmpty())
            info += QString(" %1").arg(dUnit);
        if (entry.stockRaw)
            info += tr("   |   stock raw=%1").arg(*entry.stockRaw);
    }
    if (!xUnit.isEmpty() || !yUnit.isEmpty())
        info += QString("   |   axes : X=%1  Y=%2")
                    .arg(xUnit.isEmpty() ? QStringLiteral("—") : xUnit,
                         yUnit.isEmpty() ? QStringLiteral("—") : yUnit);
    if (!entry.description.empty())
        info += QString("   |   %1").arg(QString::fromStdString(entry.description));
    m_infoLabel->setText(info);

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

void MapEditorPanel::view3d() {
    if (m_currentRow < 0 || m_currentRow >= static_cast<int>(m_entries.size())) {
        setStatus(tr("Sélectionnez une map d'abord."), true);
        return;
    }
    const MapEntry& e = m_entries[static_cast<std::size_t>(m_currentRow)];
    emit view3dRequested(e.address, e.name,
                         QString::fromStdString(e.xAxisUnit),
                         QString::fromStdString(e.yAxisUnit),
                         QString::fromStdString(e.unit));
}

// ── Sélecteur de baseline (mode fantôme) ──────────────────────────────────────

namespace {

// Retourne le toplevel git de répertoire de la ROM, ou "" si pas de repo.
QString gitToplevelFor(const QString& romPath) {
    if (romPath.isEmpty()) return {};
    QFileInfo fi(romPath);
    QProcess p;
    p.setWorkingDirectory(fi.absolutePath());
    p.start("git", { "rev-parse", "--show-toplevel" });
    if (!p.waitForFinished(2000)) return {};
    if (p.exitCode() != 0) return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

struct CommitInfo {
    QString sha;
    QString summary;   // first line of commit message + ago
};

// Renvoie les N derniers commits du dépôt contenant la ROM.
QList<CommitInfo> gitRecentCommits(const QString& repoRoot, int max = 40) {
    QList<CommitInfo> out;
    QProcess p;
    p.setWorkingDirectory(repoRoot);
    p.start("git", { "log", "--pretty=format:%H|%s|%ar",
                     QString("-n%1").arg(max) });
    if (!p.waitForFinished(3000)) return out;
    if (p.exitCode() != 0) return out;
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput())
                                  .split('\n', Qt::SkipEmptyParts);
    for (const auto& ln : lines) {
        const auto parts = ln.split('|');
        if (parts.size() < 3) continue;
        out.push_back({ parts[0], QString("%1  (%2)").arg(parts[1], parts[2]) });
    }
    return out;
}

// `git show <sha>:<relpath>` → bytes. relpath est le chemin de la ROM relatif
// au repo toplevel.
QByteArray gitBlobAt(const QString& repoRoot, const QString& sha,
                     const QString& relPath) {
    QProcess p;
    p.setWorkingDirectory(repoRoot);
    p.start("git", { "show", QString("%1:%2").arg(sha, relPath) });
    if (!p.waitForFinished(5000)) return {};
    if (p.exitCode() != 0) return {};
    return p.readAllStandardOutput();
}

} // namespace

void MapEditorPanel::pickBaseline() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Chargez une ROM avant de choisir une baseline."), true);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Baseline du mode fantôme"));
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(tr("La baseline est la ROM \"de référence\" comparée "
                                 "à l'édition courante en mode fantôme.")));

    // Option 1 : snapshot d'origine.
    auto* originBtn = new QPushButton(tr("Snapshot d'origine (à l'ouverture)"), &dlg);
    lay->addWidget(originBtn);

    // Option 2 : commit git.
    const QString romPath  = m_doc->path();
    const QString repoRoot = gitToplevelFor(romPath);
    auto* gitGroup = new QGroupBox(tr("Depuis un commit git"), &dlg);
    auto* gitLay   = new QVBoxLayout(gitGroup);
    auto* commitCombo = new QComboBox(gitGroup);
    auto* gitApplyBtn = new QPushButton(tr("Charger ce commit"), gitGroup);
    gitLay->addWidget(commitCombo);
    gitLay->addWidget(gitApplyBtn);
    QString relPath;
    if (repoRoot.isEmpty()) {
        commitCombo->setEnabled(false);
        gitApplyBtn->setEnabled(false);
        gitLay->addWidget(new QLabel(tr("(la ROM n'est pas dans un dépôt git — option indisponible)"),
                                     gitGroup));
    } else {
        QDir d(repoRoot);
        relPath = d.relativeFilePath(romPath);
        const auto commits = gitRecentCommits(repoRoot, 40);
        if (commits.isEmpty()) {
            commitCombo->setEnabled(false);
            gitApplyBtn->setEnabled(false);
            gitLay->addWidget(new QLabel(tr("(aucun commit listable — repo vide ?)"), gitGroup));
        } else {
            for (const auto& c : commits)
                commitCombo->addItem(QString("%1  %2").arg(c.sha.left(8), c.summary),
                                     c.sha);
        }
    }
    lay->addWidget(gitGroup);

    // Option 3 : fichier .bin externe.
    auto* fileBtn = new QPushButton(tr("Depuis un fichier .bin externe…"), &dlg);
    lay->addWidget(fileBtn);

    auto* info = new QLabel(tr("Baseline actuelle : %1").arg(m_doc->baselineLabel()), &dlg);
    info->setStyleSheet("color:#7c8fa6;");
    lay->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    connect(originBtn, &QPushButton::clicked, this, [this, &dlg]() {
        // On retourne au "snapshot à l'ouverture" : on relit la ROM depuis le
        // disque pour reproduire l'état du load. Si non possible, on garde le
        // contenu actuel comme nouvelle baseline (resetBaseline).
        const QString p = m_doc->path();
        QFile f(p);
        if (!p.isEmpty() && f.open(QIODevice::ReadOnly)) {
            m_doc->setBaselineFromBytes(f.readAll(), tr("fichier d'origine"));
        } else {
            m_doc->resetBaseline();
        }
        setStatus(tr("Baseline : snapshot d'origine."));
        dlg.accept();
    });
    connect(gitApplyBtn, &QPushButton::clicked, this, [&]() {
        if (repoRoot.isEmpty() || commitCombo->currentIndex() < 0) return;
        const QString sha = commitCombo->currentData().toString();
        const QByteArray bytes = gitBlobAt(repoRoot, sha, relPath);
        if (bytes.isEmpty()) {
            QMessageBox::warning(&dlg, tr("Baseline git"),
                tr("Impossible d'extraire %1 du commit %2 — vérifie que le "
                   "fichier était versionné à ce moment.").arg(relPath, sha.left(8)));
            return;
        }
        m_doc->setBaselineFromBytes(
            bytes, tr("commit %1").arg(sha.left(8)));
        setStatus(tr("Baseline : commit %1 (%2 octets).").arg(sha.left(8)).arg(bytes.size()));
        dlg.accept();
    });
    connect(fileBtn, &QPushButton::clicked, this, [&]() {
        const QString p = QFileDialog::getOpenFileName(
            &dlg, tr("Choisir une ROM de référence"), {},
            tr("ROM (*.bin *.hex);;Tous (*.*)"),
            nullptr, QFileDialog::DontUseNativeDialog);
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dlg, tr("Baseline"),
                tr("Impossible d'ouvrir %1").arg(p));
            return;
        }
        const QByteArray bytes = f.readAll();
        m_doc->setBaselineFromBytes(bytes, QFileInfo(p).fileName());
        setStatus(tr("Baseline : fichier %1 (%2 octets).")
                      .arg(QFileInfo(p).fileName()).arg(bytes.size()));
        dlg.accept();
    });

    dlg.exec();
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

        // Conversion physique et unités issues du recipe (même logique que refreshMaps).
        auto it = std::find_if(recipe->characteristics.begin(),
                               recipe->characteristics.end(),
                               [&](const ecu::DamosEntry& de) { return de.name == r.name; });
        if (it != recipe->characteristics.end()) {
            e.factor        = it->data.factor;
            e.offset        = it->data.offset;
            e.hasConversion = (e.factor != 1.0 || e.offset != 0.0);
            e.stockRaw      = it->stockRawValue;
            e.stockPhys     = it->stockPhysValue;
            e.description   = it->description;
            e.unit          = it->data.unit;
            if (!it->axes.empty()) e.xAxisUnit = it->axes[0].unit;
            if (it->axes.size() > 1) e.yAxisUnit = it->axes[1].unit;
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

    // Relocalise avec le recipe importé.
    const QByteArray& rom = m_doc->rom();
    QByteArrayView view(rom.constData(), rom.size());
    auto results = ecu::OpenDamos{}.relocate(*recipe, view);

    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1 && !e.openDamos) kept.push_back(e);
    m_entries = std::move(kept);

    auto span = constByteSpan(rom);
    int byFingerprint = 0;
    for (const auto& r : results) {
        const bool fallback = (r.addressSource == ecu::AddressSource::DefaultFallback);
        if (!fallback) ++byFingerprint;

        MapEntry e;
        e.name      = QString::fromStdString(r.name);
        e.address   = static_cast<quint32>(r.address);
        e.score     = r.score;
        e.openDamos = true;
        e.fallback  = fallback;

        if (auto md = ecu::readMapData(span, e.address)) {
            e.nx = md->nx;
            e.ny = md->ny;
        }

        auto it = std::find_if(recipe->characteristics.begin(),
                               recipe->characteristics.end(),
                               [&](const ecu::DamosEntry& de) { return de.name == r.name; });
        if (it != recipe->characteristics.end()) {
            e.factor        = it->data.factor;
            e.offset        = it->data.offset;
            e.hasConversion = (e.factor != 1.0 || e.offset != 0.0);
            e.stockRaw      = it->stockRawValue;
            e.stockPhys     = it->stockPhysValue;
            e.description   = it->description;
            e.unit          = it->data.unit;
            if (!it->axes.empty()) e.xAxisUnit = it->axes[0].unit;
            if (it->axes.size() > 1) e.yAxisUnit = it->axes[1].unit;
        }

        QString src;
        switch (r.addressSource) {
            case ecu::AddressSource::Fingerprint:     src = tr("empreinte"); break;
            case ecu::AddressSource::Anchor:          src = tr("ancre");     break;
            case ecu::AddressSource::DefaultFallback: src = tr("défaut");    break;
        }
        const QString mode = QString::fromStdString(r.matchMode);
        e.matchInfo = mode.isEmpty()
            ? QString("%1 (%2)").arg(src).arg(r.score, 0, 'f', 2)
            : QString("%1 %2 (%3)").arg(src, mode).arg(r.score, 0, 'f', 2);

        m_entries.push_back(std::move(e));
    }

    rebuildMapTable();
    setStatus(tr("Recipe importé : %1/%2 maps relocalisées par empreinte.")
                  .arg(byFingerprint).arg(results.size()));
}

} // namespace ecu_studio
