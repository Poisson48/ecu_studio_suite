// map_editor_panel_ops.cpp — opérations sur la sélection multi-cellules
// (fixer/ajouter/multiplier/interpoler/lisser) et copier/coller TSV.

#include "map_editor_panel.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QTableWidget>

#include <algorithm>
#include <limits>
#include <map>
#include <span>
#include <vector>

#include "ecu/RomPatcher.hpp"

namespace ecu_studio {

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

} // namespace ecu_studio
