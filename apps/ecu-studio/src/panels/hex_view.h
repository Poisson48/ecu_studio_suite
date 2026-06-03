#pragma once
#include "../rom_document.h"

#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QByteArray>
#include <QList>
#include <QPair>

#include <algorithm>

namespace ecu_studio {

// ─────────────────────────────────────────────────────────────────────────────
//  HexView — vue hexadécimale à défilement virtuel.
//
//  Ne peint que les lignes visibles via QPainter ; la barre de défilement
//  verticale est dimensionnée sur le nombre TOTAL de lignes (1 pas = 1 ligne).
//  Conçue pour des ROMs de plusieurs Mo : aucun widget par octet, aucun cache
//  de texte ; le rendu est O(lignes visibles).
//
//  Sélection : un curseur (m_cursor) et une ancre (m_anchor). La plage
//  sélectionnée est [min(anchor,cursor), max(...)]. Maj+clic / Maj+flèches
//  étendent la sélection.
//
//  Octets modifiés : on conserve la ROM d'origine (m_original) et on surligne
//  en vert tout octet qui en diffère.
// ─────────────────────────────────────────────────────────────────────────────
class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr int kBytesPerRow = 16;

    explicit HexView(RomDocument* doc, QWidget* parent = nullptr);

    void    setHighlights(const QList<QPair<quint32, quint32>>& ranges);
    void    gotoOffset(quint32 offset, bool select = true);
    quint32 cursor() const { return m_cursor; }

    // Sélection ordonnée [begin, end] inclusif (begin==end => 1 octet).
    quint32 selBegin() const { return std::min(m_cursor, m_anchor); }
    quint32 selEnd()   const { return std::max(m_cursor, m_anchor); }
    qsizetype selLength() const { return romSize() == 0 ? 0 : qsizetype(selEnd()) - selBegin() + 1; }

    // Octets modifiés vs l'original (capturé au chargement).
    int  groupSize() const { return m_groupSize; }
    void setGroupSize(int g) { m_groupSize = std::clamp(g, 1, 4); viewport()->update(); }
    void setOffsetHex(bool hex) { m_offsetHex = hex; viewport()->update(); }
    void captureOriginal() { m_original = m_doc ? m_doc->rom() : QByteArray(); viewport()->update(); }
    bool isModified(qsizetype off) const {
        if (off < 0 || off >= m_original.size()) return false;
        if (!m_doc || off >= m_doc->rom().size()) return false;
        return m_doc->rom().at(off) != m_original.at(off);
    }

signals:
    void cursorChanged(quint32 offset);
    void contextMenuRequested(const QPoint& globalPos, quint32 offset);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    // ── Géométrie ──────────────────────────────────────────────────────────
    qsizetype romSize()   const { return m_doc ? m_doc->rom().size() : 0; }
    qsizetype totalRows() const {
        const qsizetype n = romSize();
        return n == 0 ? 0 : (n + kBytesPerRow - 1) / kBytesPerRow;
    }
    int  rowsPerPage() const {
        return std::max(1, int(viewport()->height() / m_rowH));
    }
    qsizetype firstVisibleRow() const { return verticalScrollBar()->value(); }

    // Colonnes en pixels (relatives au viewport, après marge gauche).
    qreal addrX()  const { return m_margin; }
    qreal addrW()  const { return 8 * m_charW; }                 // 8 chiffres hex
    qreal hexX()   const { return addrX() + addrW() + m_gap; }
    qreal hexColX(int col) const {                              // début d'un octet
        // groupe (m_groupSize octets) séparés par un espace supplémentaire
        const qreal grp = (col / m_groupSize) * m_charW;
        return hexX() + col * 3 * m_charW + grp;
    }
    qreal hexW()   const { return hexColX(kBytesPerRow - 1) + 2 * m_charW - hexX() + m_charW; }
    qreal asciiX() const { return hexX() + hexW() + m_gap; }

    void  recalcScrollBars();
    void  ensureVisible(quint32 offset);
    void  setCursor(quint32 offset, bool extendSel = false, bool resetNibble = true);
    qsizetype offsetAt(const QPoint& vpPos) const;  // -1 si hors zone octets
    bool  highlighted(qsizetype off) const;
    bool  selected(qsizetype off) const {
        return romSize() > 0 && off >= qsizetype(selBegin()) && off <= qsizetype(selEnd());
    }

    RomDocument* m_doc{nullptr};

    qreal m_charW{8};
    qreal m_rowH{16};
    qreal m_ascent{12};
    const int m_margin{10};
    const int m_gap{16};

    quint32 m_cursor{0};
    quint32 m_anchor{0};
    int     m_pendingNibble{-1};   // premier digit hex saisi (sinon -1)
    int     m_groupSize{1};        // 1=octet 2=mot 4=dword
    bool    m_offsetHex{true};

    QByteArray m_original;          // ROM au moment du chargement (pour diff)
    QList<QPair<quint32, quint32>> m_highlights;
};

} // namespace ecu_studio
