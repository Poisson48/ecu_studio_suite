// map_editor_panel_grid.cpp — affichage et édition de la grille de la map
// sélectionnée : remplissage des cellules, heatmap, mode fantôme, readout de
// sélection, application Stage 1 (pourcentage simple et complet), navigation
// Hex/3D.

#include "map_editor_panel.h"
#include "map_view_helpers.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMessageBox>
#include <QTableWidget>

#include <algorithm>
#include <limits>
#include <optional>
#include <span>

#include "ecu/EcuCatalog.hpp"
#include "ecu/RomPatcher.hpp"

namespace ecu_studio {

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

} // namespace ecu_studio
