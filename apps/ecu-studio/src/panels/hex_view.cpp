// hex_view.cpp — implémentation de HexView (vue hexadécimale à défilement
// virtuel). Extrait de hex_view_panel.cpp.
#include "hex_view.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QResizeEvent>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

namespace ecu_studio {

HexView::HexView(RomDocument* doc, QWidget* parent)
    : QAbstractScrollArea(parent), m_doc(doc) {
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(11);
    f.setStyleHint(QFont::Monospace);
    setFont(f);

    QFontMetricsF fm(f);
    m_charW  = fm.horizontalAdvance(QLatin1Char('0'));
    m_rowH   = std::ceil(fm.height()) + 2;
    m_ascent = fm.ascent();

    viewport()->setBackgroundRole(QPalette::NoRole);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setCursor(Qt::IBeamCursor);

    horizontalScrollBar()->setRange(0, 0);
    recalcScrollBars();
}

void HexView::recalcScrollBars() {
    const qsizetype rows = totalRows();
    const int page = rowsPerPage();
    verticalScrollBar()->setRange(0, std::max<qsizetype>(0, rows - page));
    verticalScrollBar()->setPageStep(page);
    verticalScrollBar()->setSingleStep(1);
}

void HexView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    recalcScrollBars();
}

void HexView::setHighlights(const QList<QPair<quint32, quint32>>& ranges) {
    m_highlights = ranges;
    viewport()->update();
}

bool HexView::highlighted(qsizetype off) const {
    for (const auto& r : m_highlights) {
        const qsizetype begin = r.first;
        const qsizetype end   = qsizetype(r.first) + r.second;
        if (off >= begin && off < end) return true;
    }
    return false;
}

void HexView::gotoOffset(quint32 offset, bool /*select*/) {
    if (romSize() == 0) return;
    setCursor(offset, /*extendSel=*/false);
}

void HexView::setCursor(quint32 offset, bool extendSel, bool resetNibble) {
    const qsizetype n = romSize();
    if (n == 0) { m_cursor = m_anchor = 0; return; }
    m_cursor = quint32(std::clamp<qsizetype>(offset, 0, n - 1));
    if (!extendSel) m_anchor = m_cursor;
    if (resetNibble) m_pendingNibble = -1;
    ensureVisible(m_cursor);
    viewport()->update();
    emit cursorChanged(m_cursor);
}

void HexView::ensureVisible(quint32 offset) {
    const qsizetype row  = offset / kBytesPerRow;
    const qsizetype top  = firstVisibleRow();
    const int       page = rowsPerPage();
    if (row < top)                 verticalScrollBar()->setValue(int(row));
    else if (row >= top + page)    verticalScrollBar()->setValue(int(row - page + 1));
}

qsizetype HexView::offsetAt(const QPoint& vpPos) const {
    const qsizetype row = firstVisibleRow() + qsizetype(vpPos.y() / m_rowH);
    if (row < 0 || row >= totalRows()) return -1;

    const qreal x = vpPos.x();
    // Zone hex ?
    if (x >= hexX() && x < asciiX() - m_gap / 2.0) {
        for (int col = 0; col < kBytesPerRow; ++col) {
            const qreal x0 = hexColX(col);
            if (x >= x0 && x < x0 + 3 * m_charW) {
                const qsizetype off = row * kBytesPerRow + col;
                return off < romSize() ? off : -1;
            }
        }
        return -1;
    }
    // Zone ASCII ?
    if (x >= asciiX()) {
        const int col = std::clamp(int((x - asciiX()) / m_charW), 0, kBytesPerRow - 1);
        const qsizetype off = row * kBytesPerRow + col;
        return off < romSize() ? off : -1;
    }
    return -1;
}

void HexView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        const qsizetype off = offsetAt(e->pos());
        if (off >= 0) setCursor(quint32(off), e->modifiers() & Qt::ShiftModifier);
        setFocus(Qt::MouseFocusReason);
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void HexView::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        const qsizetype off = offsetAt(e->pos());
        if (off >= 0) setCursor(quint32(off), /*extendSel=*/true);
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void HexView::contextMenuEvent(QContextMenuEvent* e) {
    const qsizetype off = offsetAt(e->pos());
    const quint32 target = off >= 0 ? quint32(off) : m_cursor;
    if (off >= 0 && !selected(off)) setCursor(target);
    emit contextMenuRequested(e->globalPos(), target);
}

void HexView::keyPressEvent(QKeyEvent* e) {
    const qsizetype n = romSize();
    if (n == 0) { QAbstractScrollArea::keyPressEvent(e); return; }

    const bool ext  = e->modifiers() & Qt::ShiftModifier;
    const qsizetype page = qsizetype(rowsPerPage()) * kBytesPerRow;

    switch (e->key()) {
    case Qt::Key_Left:     setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - 1)), ext);            return;
    case Qt::Key_Right:    setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + 1)), ext);        return;
    case Qt::Key_Up:       setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - kBytesPerRow)), ext); return;
    case Qt::Key_Down:     setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + kBytesPerRow)), ext); return;
    case Qt::Key_PageUp:   setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - page)), ext);         return;
    case Qt::Key_PageDown: setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + page)), ext);     return;
    case Qt::Key_Home:     setCursor(0, ext);                  return;
    case Qt::Key_End:      setCursor(quint32(n - 1), ext);     return;
    default: break;
    }

    // Édition : deux chiffres hexadécimaux -> un octet.
    const QString t = e->text().toLower();
    if (t.size() == 1) {
        const QChar c = t.at(0);
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c.unicode() - '0';
        else if (c >= 'a' && c <= 'f') digit = 10 + (c.unicode() - 'a');

        if (digit >= 0 && m_doc) {
            const quint8 cur = quint8(m_doc->rom().at(m_cursor));
            quint8 val;
            if (m_pendingNibble < 0) {
                // Premier digit : haut nibble (bas nibble conservé).
                val = quint8((digit << 4) | (cur & 0x0F));
                m_pendingNibble = digit;
                m_doc->romMutable()[m_cursor] = char(val);
                m_doc->markModified(m_cursor, 1); // déclenche repaint + status
            } else {
                // Second digit : bas nibble, octet complet -> avance le curseur.
                val = quint8((m_pendingNibble << 4) | digit);
                m_doc->romMutable()[m_cursor] = char(val);
                m_doc->markModified(m_cursor, 1);
                const quint32 next = quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + 1));
                m_pendingNibble = -1;
                setCursor(next);
            }
            return;
        }
    }

    QAbstractScrollArea::keyPressEvent(e);
}

void HexView::paintEvent(QPaintEvent* e) {
    QPainter p(viewport());
    p.fillRect(e->rect(), QColor(0x11, 0x18, 0x27));   // bg #111827
    p.setFont(font());

    const qsizetype n = romSize();
    if (n == 0) {
        p.setPen(QColor(0x7c, 0x8f, 0xa6));
        p.drawText(viewport()->rect(), Qt::AlignCenter,
                   tr("Aucune ROM chargée"));
        return;
    }

    const QColor addrCol (0x7c, 0x8f, 0xa6);   // gris-bleu
    const QColor hexCol  (0xe5, 0xe7, 0xeb);   // texte clair
    const QColor asciiCol(0x9c, 0xa3, 0xaf);
    const QColor zeroCol (0x4b, 0x55, 0x63);   // octets 0x00 atténués
    const QColor modCol  (0x22, 0xc5, 0x5e);   // octets modifiés (texte vert)
    const QColor accent  (0x63, 0x66, 0xf1);   // #6366f1
    const QColor hlBg    (0x63, 0x66, 0xf1, 60);
    const QColor selBg   (0x63, 0x66, 0xf1, 90);
    const QColor modBg   (0x22, 0xc5, 0x5e, 55);
    const QColor curBg   (0x63, 0x66, 0xf1, 150);

    const QByteArray& rom = m_doc->rom();
    const qsizetype top   = firstVisibleRow();
    const int       rows  = rowsPerPage() + 1;
    const qreal     asc   = m_ascent;

    for (int vr = 0; vr < rows; ++vr) {
        const qsizetype row = top + vr;
        if (row >= totalRows()) break;
        const qreal y  = vr * m_rowH;
        const qreal ty = y + asc + 1;
        const qsizetype rowOff = row * kBytesPerRow;

        // Adresse (hex ou décimal selon la base choisie).
        p.setPen(addrCol);
        const QString addrTxt = m_offsetHex
            ? QString("%1").arg(quint32(rowOff), 8, 16, QLatin1Char('0')).toUpper()
            : QString("%1").arg(quint32(rowOff), 8, 10, QLatin1Char('0'));
        p.drawText(QPointF(addrX(), ty), addrTxt);

        for (int col = 0; col < kBytesPerRow; ++col) {
            const qsizetype off = rowOff + col;
            if (off >= n) break;

            const quint8 b = quint8(rom.at(off));
            const qreal  bx = hexColX(col);
            const bool   isMod = isModified(off);

            // Fonds : highlight de map, sélection, octet modifié, curseur.
            if (highlighted(off))
                p.fillRect(QRectF(bx, y, 2 * m_charW + (col % m_groupSize == m_groupSize - 1 ? 0 : m_charW), m_rowH), hlBg);
            if (isMod)
                p.fillRect(QRectF(bx, y, 2 * m_charW + (col % m_groupSize == m_groupSize - 1 ? 0 : m_charW), m_rowH), modBg);
            if (selected(off))
                p.fillRect(QRectF(bx - m_charW / 4.0, y, 2 * m_charW + m_charW / 2.0, m_rowH),
                           off == qsizetype(m_cursor) && hasFocus() ? curBg : selBg);

            // Octet hex.
            p.setPen(off == qsizetype(m_cursor) ? accent
                     : isMod                    ? modCol
                     : (b == 0 ? zeroCol : hexCol));
            p.drawText(QPointF(bx, ty),
                       QString("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper());

            // ASCII.
            const qreal ax = asciiX() + col * m_charW;
            const QChar ch = (b >= 0x20 && b < 0x7f) ? QChar(b) : QChar('.');
            p.setPen(off == qsizetype(m_cursor) ? accent
                     : isMod                    ? modCol
                     : (b >= 0x20 && b < 0x7f ? asciiCol : zeroCol));
            p.drawText(QPointF(ax, ty), ch);
        }
    }

    // Séparateurs verticaux discrets.
    p.setPen(QColor(0x1f, 0x29, 0x37));
    const qreal h = viewport()->height();
    p.drawLine(QPointF(hexX() - m_gap / 2.0, 0),   QPointF(hexX() - m_gap / 2.0, h));
    p.drawLine(QPointF(asciiX() - m_gap / 2.0, 0), QPointF(asciiX() - m_gap / 2.0, h));
}


} // namespace ecu_studio
