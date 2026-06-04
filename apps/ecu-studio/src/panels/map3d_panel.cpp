#include "map3d_panel.h"
#include "map3d_view.h"
#include "rom_source_picker.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QFile>

#include <algorithm>
#include <cstdint>
#include <span>

#include "ecu/EcuCatalog.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"
#include "ecu/OpenDamos.hpp"

#ifdef ECU_HAVE_DATAVIZ
#include <Q3DSurface>
#include <Q3DCamera>
#include <Q3DScene>
#include <Q3DTheme>
#include <QSurfaceDataProxy>
#include <QSurface3DSeries>
#include <QValue3DAxis>
#include <QLinearGradient>
#include <QVector3D>
#endif

namespace ecu_studio {

namespace {
QString hex32(quint32 v) {
    return QString("0x%1").arg(v, 6, 16, QChar('0')).toUpper().replace("0X", "0x");
}
} // namespace

Map3dPanel::Map3dPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded,  this, &Map3dPanel::refreshMaps);
        connect(m_doc, &RomDocument::ecuChanged, this,
                [this](const QString&) { refreshMaps(); });
        // Mise à jour live : si la map affichée a été modifiée, on la relit.
        connect(m_doc, &RomDocument::romModified, this,
                [this](qsizetype, qsizetype) { reloadCurrent(); });
    }
    refreshMaps();
}

// ── Construction de la vue selon le backend disponible ────────────────────────
#ifdef ECU_HAVE_DATAVIZ

void Map3dPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    auto* ctlBox = new QGroupBox(tr("Visualisation 3D"), this);
    auto* ctl    = new QHBoxLayout(ctlBox);
    ctl->addWidget(new QLabel(tr("Map :"), this));
    m_mapCombo = new QComboBox(this);
    m_mapCombo->setMinimumWidth(260);
    ctl->addWidget(m_mapCombo, 1);
    m_searchBtn = new QPushButton(tr("Chercher maps"), this);
    ctl->addWidget(m_searchBtn);
    m_heatChk = new QCheckBox(tr("Heatmap 2D"), this);
    ctl->addWidget(m_heatChk);
    m_ghostChk = new QCheckBox(tr("Fantôme"), this);
    m_ghostChk->setToolTip(tr("Superpose la baseline en wireframe cyan sous la "
                              "surface courante. Choisis la baseline via "
                              "« Baseline… » : un commit git donne un fantôme entre "
                              "la ROM modifiée courante et ce commit."));
    ctl->addWidget(m_ghostChk);
    m_baselineBtn = new QPushButton(tr("Baseline…"), this);
    m_baselineBtn->setToolTip(tr("ROM de référence du mode fantôme : commit git, "
                                 "fichier .bin externe ou snapshot d'origine."));
    ctl->addWidget(m_baselineBtn);
    ctl->addStretch();
    root->addWidget(ctlBox);

    m_infoLabel = new QLabel(tr("Aucune map sélectionnée"), this);
    m_infoLabel->setStyleSheet("color:#7c8fa6;");
    root->addWidget(m_infoLabel);

    m_surface = new Q3DSurface();
    auto* container = QWidget::createWindowContainer(m_surface, this);
    container->setMinimumSize(360, 320);
    m_view = container;
    root->addWidget(m_view, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    connect(m_mapCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &Map3dPanel::onMapSelected);
    connect(m_searchBtn, &QPushButton::clicked, this, &Map3dPanel::searchMaps);
    connect(m_heatChk, &QCheckBox::toggled, this, &Map3dPanel::toggleHeatmap);
    connect(m_ghostChk, &QCheckBox::toggled, this, [this](bool) {
        if (m_currentAddr != 0) render(m_currentAddr);
    });
    connect(m_baselineBtn, &QPushButton::clicked, this, &Map3dPanel::pickBaseline);
}

void Map3dPanel::viewSetSurface(const SurfaceData& data) {
    if (!m_surface) return;

    auto* proxy = new QSurfaceDataProxy();
    auto* arr   = new QSurfaceDataArray;
    arr->reserve(data.ny);
    for (int gy = 0; gy < data.ny; ++gy) {
        auto* row = new QSurfaceDataRow(data.nx);
        for (int gx = 0; gx < data.nx; ++gx) {
            const std::size_t idx = static_cast<std::size_t>(gy) *
                                    static_cast<std::size_t>(data.nx) +
                                    static_cast<std::size_t>(gx);
            const double v = data.z[idx];
            (*row)[gx].setPosition(
                QVector3D(static_cast<float>(gx), static_cast<float>(v),
                          static_cast<float>(gy)));
        }
        *arr << row;
    }
    proxy->resetArray(arr);

    if (m_series) m_surface->removeSeries(m_series);
    m_series = new QSurface3DSeries(proxy);
    m_series->setDrawMode(QSurface3DSeries::DrawSurfaceAndWireframe);
    m_series->setFlatShadingEnabled(false);

    // Gradient froid → chaud.
    QLinearGradient grad;
    grad.setColorAt(0.0,  QColor(20, 40, 120));
    grad.setColorAt(0.25, QColor(30, 140, 200));
    grad.setColorAt(0.5,  QColor(40, 190, 90));
    grad.setColorAt(0.75, QColor(240, 200, 40));
    grad.setColorAt(1.0,  QColor(220, 40, 40));
    m_series->setBaseGradient(grad);
    m_series->setColorStyle(Q3DTheme::ColorStyleRangeGradient);

    m_surface->addSeries(m_series);
    if (m_surface->axisX()) {
        m_surface->axisX()->setTitle(data.xAxisTitle.isEmpty() ? tr("X") : data.xAxisTitle);
        m_surface->axisX()->setTitleVisible(true);
    }
    if (m_surface->axisZ()) {
        m_surface->axisZ()->setTitle(data.yAxisTitle.isEmpty() ? tr("Y") : data.yAxisTitle);
        m_surface->axisZ()->setTitleVisible(true);
    }
    if (m_surface->axisY()) {
        m_surface->axisY()->setTitle(data.title);
        m_surface->axisY()->setTitleVisible(true);
    }
}

void Map3dPanel::viewSetHeatmap(bool on) {
    if (!m_surface) return;
    // Vue de dessus (caméra zénithale) pour approximer la heatmap.
    m_surface->scene()->activeCamera()->setCameraPreset(
        on ? Q3DCamera::CameraPresetDirectlyAbove
           : Q3DCamera::CameraPresetIsometricRight);
}

void Map3dPanel::viewClear() {
    if (m_surface && m_series) {
        m_surface->removeSeries(m_series);
        m_series = nullptr;
    }
}

#else  // ── Repli QPainter (universel) ──────────────────────────────────────────

void Map3dPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    auto* ctlBox = new QGroupBox(tr("Visualisation 3D"), this);
    auto* ctl    = new QHBoxLayout(ctlBox);
    ctl->addWidget(new QLabel(tr("Map :"), this));
    m_mapCombo = new QComboBox(this);
    m_mapCombo->setMinimumWidth(260);
    ctl->addWidget(m_mapCombo, 1);
    m_searchBtn = new QPushButton(tr("Chercher maps"), this);
    ctl->addWidget(m_searchBtn);
    m_heatChk = new QCheckBox(tr("Heatmap 2D"), this);
    ctl->addWidget(m_heatChk);
    m_ghostChk = new QCheckBox(tr("Fantôme"), this);
    m_ghostChk->setToolTip(tr("Superpose la baseline en wireframe cyan sous la "
                              "surface courante. Choisis la baseline via "
                              "« Baseline… » : un commit git donne un fantôme entre "
                              "la ROM modifiée courante et ce commit."));
    ctl->addWidget(m_ghostChk);
    m_baselineBtn = new QPushButton(tr("Baseline…"), this);
    m_baselineBtn->setToolTip(tr("ROM de référence du mode fantôme : commit git, "
                                 "fichier .bin externe ou snapshot d'origine."));
    ctl->addWidget(m_baselineBtn);
    ctl->addStretch();
    root->addWidget(ctlBox);

    m_infoLabel = new QLabel(tr("Aucune map sélectionnée"), this);
    m_infoLabel->setStyleSheet("color:#7c8fa6;");
    root->addWidget(m_infoLabel);

    auto* view = new Map3dViewPainter(this);
    m_view = view;
    root->addWidget(m_view, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    connect(m_mapCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &Map3dPanel::onMapSelected);
    connect(m_searchBtn, &QPushButton::clicked, this, &Map3dPanel::searchMaps);
    connect(m_heatChk, &QCheckBox::toggled, this, &Map3dPanel::toggleHeatmap);
    connect(m_ghostChk, &QCheckBox::toggled, this, [this](bool) {
        if (m_currentAddr != 0) render(m_currentAddr);
    });
    connect(m_baselineBtn, &QPushButton::clicked, this, &Map3dPanel::pickBaseline);
    // Édition par clic court sur un sommet (signal du painter).
    connect(view, &Map3dViewPainter::cellClicked,
            this, &Map3dPanel::onCellClicked);
}

void Map3dPanel::viewSetSurface(const SurfaceData& data) {
    static_cast<Map3dViewPainter*>(m_view)->setSurface(data);
}
void Map3dPanel::viewSetHeatmap(bool on) {
    static_cast<Map3dViewPainter*>(m_view)->setHeatmap(on);
}
void Map3dPanel::viewClear() {
    static_cast<Map3dViewPainter*>(m_view)->clearSurface();
}

#endif // ECU_HAVE_DATAVIZ

// ── Logique commune (indépendante du backend) ─────────────────────────────────

void Map3dPanel::setStatus(const QString& msg, bool error) {
    m_statusLabel->setStyleSheet(error ? "color:#ef4444; font-size:11px;"
                                       : "color:#7c8fa6; font-size:11px;");
    m_statusLabel->setText(msg);
}

void Map3dPanel::refreshMaps() {
    m_entries.clear();
    m_currentAddr = 0;

    if (m_doc && m_doc->isLoaded()) {
        auto span = constByteSpan(m_doc->rom());

        // open_damos : si un recipe est disponible pour cet ECU, on relocalise et
        // on récupère les unités (axes + data) pour chaque characteristic — sinon
        // la 3D resterait sans unités même quand MapEditor en a.
        auto recipe = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
        if (recipe) {
            QByteArrayView view(m_doc->rom().constData(), m_doc->rom().size());
            auto results = ecu::OpenDamos{}.relocate(*recipe, view);
            for (const auto& r : results) {
                MapEntry e;
                e.name    = QString::fromStdString(r.name);
                e.address = static_cast<quint32>(r.address);
                if (auto md = ecu::readMapData(span, e.address)) {
                    e.nx = md->nx;
                    e.ny = md->ny;
                }
                auto it = std::find_if(recipe->characteristics.begin(),
                                       recipe->characteristics.end(),
                                       [&](const ecu::DamosEntry& de) {
                                           return de.name == r.name;
                                       });
                if (it != recipe->characteristics.end()) {
                    e.dataUnit = QString::fromStdString(it->data.unit);
                    if (!it->axes.empty())
                        e.xUnit = QString::fromStdString(it->axes[0].unit);
                    if (it->axes.size() > 1)
                        e.yUnit = QString::fromStdString(it->axes[1].unit);
                }
                m_entries.push_back(std::move(e));
            }
        } else {
            // Fallback : catalogue Stage 1 (pas d'unités).
            auto ecu = ecu::getEcu(m_doc->ecuId().toStdString());
            if (ecu && ecu->stage1Maps) {
                for (const auto& m : *ecu->stage1Maps) {
                    MapEntry e;
                    e.name    = QString::fromUtf8(m.name.data(),
                                                  static_cast<int>(m.name.size()));
                    e.address = m.address;
                    e.stage1  = true;
                    if (auto md = ecu::readMapData(span, m.address)) {
                        e.nx = md->nx;
                        e.ny = md->ny;
                    }
                    m_entries.push_back(std::move(e));
                }
            }
        }
    }

    rebuildCombo();

    if (m_entries.empty()) {
        viewClear();
        m_infoLabel->setText(tr("Aucune map sélectionnée"));
        if (m_doc && m_doc->isLoaded())
            setStatus(tr("Aucune map connue pour cet ECU — utilisez « Chercher maps »."));
        else
            setStatus(tr("Aucune ROM chargée."));
    } else {
        setStatus(tr("%1 map(s) listée(s).").arg(m_entries.size()));
        // Affiche la première map automatiquement.
        m_mapCombo->setCurrentIndex(0);
        onMapSelected(0);
    }
}

void Map3dPanel::rebuildCombo() {
    QSignalBlocker block(m_mapCombo);
    m_mapCombo->clear();
    for (const auto& e : m_entries) {
        const QString size = (e.nx > 0 && e.ny > 0)
            ? QString("%1×%2").arg(e.nx).arg(e.ny) : QString("—");
        m_mapCombo->addItem(QString("%1  [%2]  %3")
                                .arg(e.name, size, hex32(e.address)));
    }
}

void Map3dPanel::onMapSelected(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    render(m_entries[static_cast<std::size_t>(index)].address);
}

void Map3dPanel::showMap(quint32 address, const QString& name,
                         const QString& xUnit, const QString& yUnit,
                         const QString& dataUnit) {
    // Sélectionne l'entrée correspondante si elle existe déjà — et MAJ les unités
    // si elles sont fournies (cas d'un appel depuis MapEditor avec recipe chargé).
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        auto& existing = m_entries[static_cast<std::size_t>(i)];
        if (existing.address == address) {
            if (!xUnit.isEmpty())    existing.xUnit    = xUnit;
            if (!yUnit.isEmpty())    existing.yUnit    = yUnit;
            if (!dataUnit.isEmpty()) existing.dataUnit = dataUnit;
            if (!name.isEmpty())     existing.name     = name;
            m_mapCombo->setCurrentIndex(i);
            // Re-render si déjà sur cet index (le setCurrentIndex ne déclenche
            // pas onMapSelected si l'index ne change pas).
            if (static_cast<int>(m_currentAddr) == static_cast<int>(address)) render(address);
            return;
        }
    }
    MapEntry e;
    e.address  = address;
    e.name     = name.isEmpty() ? tr("map @ %1").arg(hex32(address)) : name;
    e.xUnit    = xUnit;
    e.yUnit    = yUnit;
    e.dataUnit = dataUnit;
    if (m_doc && m_doc->isLoaded()) {
        if (auto md = ecu::readMapData(constByteSpan(m_doc->rom()), address)) {
            e.nx = md->nx;
            e.ny = md->ny;
        }
    }
    m_entries.push_back(std::move(e));
    rebuildCombo();
    m_mapCombo->setCurrentIndex(static_cast<int>(m_entries.size()) - 1);
}

void Map3dPanel::pickBaseline() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Chargez une ROM avant de choisir une baseline."), true);
        return;
    }

    const PickedRom r = pickRomSource(this, tr("Baseline du mode fantôme"),
                                      m_doc->path(),
                                      tr("Snapshot d'origine (à l'ouverture)"));
    if (!r.ok) return;

    if (r.firstOption) {
        // Snapshot d'origine : relit la ROM depuis le disque pour reproduire
        // l'état du chargement ; sinon retombe sur l'état courant (resetBaseline).
        QFile f(m_doc->path());
        if (!m_doc->path().isEmpty() && f.open(QIODevice::ReadOnly))
            m_doc->setBaselineFromBytes(f.readAll(), tr("fichier d'origine"));
        else
            m_doc->resetBaseline();
        setStatus(tr("Baseline : snapshot d'origine."));
    } else {
        m_doc->setBaselineFromBytes(r.bytes, r.label);
        setStatus(tr("Baseline : %1 (%2 octets).").arg(r.label).arg(r.bytes.size()));
    }

    // Active le fantôme et redessine pour montrer immédiatement la comparaison.
    if (m_ghostChk && !m_ghostChk->isChecked())
        m_ghostChk->setChecked(true);       // déclenche render() via toggled
    else if (m_currentAddr != 0)
        render(m_currentAddr);
}

void Map3dPanel::reloadCurrent() {
    if (m_currentAddr != 0) render(m_currentAddr);
}

void Map3dPanel::onCellClicked(int gx, int gy, double currentValue) {
    if (!m_doc || !m_doc->isLoaded() || m_currentAddr == 0) return;
    auto md = ecu::readMapData(constByteSpan(m_doc->rom()), m_currentAddr);
    if (!md) { setStatus(QString::fromStdString(md.error()), true); return; }
    if (gx < 0 || gy < 0 || gx >= md->nx || gy >= md->ny) return;

    bool ok = false;
    const double v = QInputDialog::getDouble(this,
        tr("Éditer cellule [%1, %2]").arg(gx).arg(gy),
        tr("Nouvelle valeur (raw, courante = %1) :").arg(currentValue, 0, 'f', 0),
        currentValue, -32768, 32767, 0, &ok);
    if (!ok) return;

    const std::size_t idx = static_cast<std::size_t>(gy) *
                            static_cast<std::size_t>(md->nx) +
                            static_cast<std::size_t>(gx);
    const std::size_t off = md->dataOff + idx * 2;
    QByteArray& rom = m_doc->romMutable();
    if (off + 2 > static_cast<std::size_t>(rom.size())) return;
    ecu::writeSwordBE(mutByteSpan(rom), off, v);
    m_doc->markModified(static_cast<qsizetype>(off), 2);
    setStatus(tr("Cellule [%1,%2] modifiée : %3 → %4")
                  .arg(gx).arg(gy).arg(currentValue, 0, 'f', 0).arg(v, 0, 'f', 0));
    render(m_currentAddr);  // refresh surface
}

void Map3dPanel::render(quint32 address) {
    if (!m_doc || !m_doc->isLoaded()) {
        viewClear();
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }

    auto md = ecu::readMapData(constByteSpan(m_doc->rom()), address);
    if (!md) {
        viewClear();
        m_currentAddr = 0;
        m_infoLabel->setText(tr("Map illisible à %1").arg(hex32(address)));
        setStatus(QString::fromStdString(md.error()), true);
        return;
    }

    m_currentAddr = address;

    SurfaceData s;
    s.nx = md->nx;
    s.ny = md->ny;
    s.z.resize(static_cast<std::size_t>(md->nx) * static_cast<std::size_t>(md->ny));
    for (int gy = 0; gy < md->ny; ++gy)
        for (int gx = 0; gx < md->nx; ++gx) {
            const std::size_t idx = static_cast<std::size_t>(gy) *
                                    static_cast<std::size_t>(md->nx) +
                                    static_cast<std::size_t>(gx);
            s.z[idx] = static_cast<double>(md->data[idx]);
        }

    // Mode fantôme : lit la même map dans la baseline et remplit baselineZ.
    if (m_ghostChk && m_ghostChk->isChecked() && m_doc->hasBaseline()) {
        auto bspan = constByteSpan(m_doc->baseline());
        if (auto bmd = ecu::readMapData(bspan, address);
            bmd && bmd->nx == md->nx && bmd->ny == md->ny) {
            s.baselineZ.resize(s.z.size());
            for (std::size_t i = 0; i < s.z.size(); ++i)
                s.baselineZ[i] = static_cast<double>(bmd->data[i]);
        }
    }
    // Récupère les unités depuis l'entrée (si fournies par open_damos).
    QString name, xUnit, yUnit, dataUnit;
    for (const auto& e : m_entries)
        if (e.address == address) {
            name = e.name; xUnit = e.xUnit; yUnit = e.yUnit; dataUnit = e.dataUnit;
            break;
        }

    for (int x = 0; x < md->nx; ++x) {
        const QString v = QString::number(md->xAxis[static_cast<std::size_t>(x)]);
        s.xLabels << (xUnit.isEmpty() ? v : QString("%1 %2").arg(v, xUnit));
    }
    for (int y = 0; y < md->ny; ++y) {
        const QString v = QString::number(md->yAxis[static_cast<std::size_t>(y)]);
        s.yLabels << (yUnit.isEmpty() ? v : QString("%1 %2").arg(v, yUnit));
    }

    s.title = name.isEmpty() ? hex32(address) : name;
    if (!dataUnit.isEmpty()) s.title += QString(" [%1]").arg(dataUnit);
    // Titres d'axes (utilisés par le backend Q3DSurface s'il est dispo).
    s.xAxisTitle = xUnit.isEmpty() ? tr("X") : QString("X (%1)").arg(xUnit);
    s.yAxisTitle = yUnit.isEmpty() ? tr("Y") : QString("Y (%1)").arg(yUnit);

    viewSetSurface(s);
    viewSetHeatmap(m_heatChk && m_heatChk->isChecked());

    QString info = tr("%1 — %2 — %3×%4 — données @ %5")
        .arg(s.title)
        .arg(hex32(address))
        .arg(md->nx).arg(md->ny)
        .arg(hex32(static_cast<quint32>(md->dataOff)));
    if (!xUnit.isEmpty() || !yUnit.isEmpty() || !dataUnit.isEmpty())
        info += tr("   |   X=%1  Y=%2  data=%3")
                    .arg(xUnit.isEmpty()    ? QStringLiteral("—") : xUnit,
                         yUnit.isEmpty()    ? QStringLiteral("—") : yUnit,
                         dataUnit.isEmpty() ? QStringLiteral("—") : dataUnit);
    m_infoLabel->setText(info);
    setStatus(tr("Map affichée en 3D."));
}

void Map3dPanel::toggleHeatmap(bool on) {
    viewSetHeatmap(on);
}

void Map3dPanel::searchMaps() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Aucune ROM chargée."), true);
        return;
    }
    setStatus(tr("Recherche heuristique de maps en cours..."));

    auto span = constByteSpan(m_doc->rom());
    ecu::FindMapsOptions opts;
    auto candidates = ecu::findMaps(span, opts);

    // Conserve les maps Stage 1, ajoute les candidates heuristiques.
    std::vector<MapEntry> kept;
    for (auto& e : m_entries)
        if (e.stage1) kept.push_back(e);
    m_entries = std::move(kept);

    for (const auto& c : candidates) {
        MapEntry e;
        e.address = static_cast<quint32>(c.address);
        e.nx      = c.nx;
        e.ny      = c.ny;
        e.stage1  = false;
        e.name    = tr("map @ %1").arg(hex32(e.address));
        m_entries.push_back(std::move(e));
    }

    rebuildCombo();
    setStatus(tr("%1 candidate(s) détectée(s) (%2 entrée(s) au total).")
                  .arg(candidates.size()).arg(m_entries.size()));
    if (!m_entries.empty()) {
        m_mapCombo->setCurrentIndex(0);
        onMapSelected(0);
    }
}

} // namespace ecu_studio
