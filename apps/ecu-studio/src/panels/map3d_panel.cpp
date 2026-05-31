#include "map3d_panel.h"
#include "map3d_view.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>

#include <algorithm>
#include <cstdint>
#include <span>

#include "ecu/EcuCatalog.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"

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
        m_surface->axisX()->setTitle(tr("X"));
        m_surface->axisX()->setTitleVisible(true);
    }
    if (m_surface->axisZ()) {
        m_surface->axisZ()->setTitle(tr("Y"));
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
        auto ecu  = ecu::getEcu(m_doc->ecuId().toStdString());
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

void Map3dPanel::showMap(quint32 address) {
    // Sélectionne l'entrée correspondante si elle existe déjà, sinon l'ajoute.
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (m_entries[static_cast<std::size_t>(i)].address == address) {
            m_mapCombo->setCurrentIndex(i);  // déclenche onMapSelected → render
            return;
        }
    }
    MapEntry e;
    e.address = address;
    e.name    = tr("map @ %1").arg(hex32(address));
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

void Map3dPanel::reloadCurrent() {
    if (m_currentAddr != 0) render(m_currentAddr);
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
    for (int x = 0; x < md->nx; ++x)
        s.xLabels << QString::number(md->xAxis[static_cast<std::size_t>(x)]);
    for (int y = 0; y < md->ny; ++y)
        s.yLabels << QString::number(md->yAxis[static_cast<std::size_t>(y)]);

    QString name;
    for (const auto& e : m_entries)
        if (e.address == address) { name = e.name; break; }
    s.title = name.isEmpty() ? hex32(address) : name;

    viewSetSurface(s);
    viewSetHeatmap(m_heatChk && m_heatChk->isChecked());

    m_infoLabel->setText(tr("%1 — %2 — %3×%4 — données @ %5")
        .arg(s.title)
        .arg(hex32(address))
        .arg(md->nx).arg(md->ny)
        .arg(hex32(static_cast<quint32>(md->dataOff))));
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
