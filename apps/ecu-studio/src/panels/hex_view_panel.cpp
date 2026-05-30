#include "hex_view_panel.h"
#include "rom_document.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QFontMetricsF>
#include <QFontDatabase>
#include <algorithm>

namespace ecu_studio {

// ─────────────────────────────────────────────────────────────────────────────
//  HexView — vue hexadécimale à défilement virtuel.
//
//  Ne peint que les lignes visibles via QPainter ; la barre de défilement
//  verticale est dimensionnée sur le nombre TOTAL de lignes (1 pas = 1 ligne).
//  Conçue pour des ROMs de plusieurs Mo : aucun widget par octet, aucun cache
//  de texte ; le rendu est O(lignes visibles).
// ─────────────────────────────────────────────────────────────────────────────
class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr int kBytesPerRow = 16;

    explicit HexView(RomDocument* doc, QWidget* parent = nullptr);

    void    setHighlights(const QList<QPair<quint32, quint32>>& ranges);
    void    gotoOffset(quint32 offset);
    quint32 cursor() const { return m_cursor; }

signals:
    void cursorChanged(quint32 offset);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

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
        // groupe de 8 octets séparés par un espace supplémentaire
        const qreal grp = (col / 8) * m_charW;
        return hexX() + col * 3 * m_charW + grp;
    }
    qreal hexW()   const { return hexColX(kBytesPerRow - 1) + 2 * m_charW - hexX() + m_charW; }
    qreal asciiX() const { return hexX() + hexW() + m_gap; }

    void  recalcScrollBars();
    void  ensureVisible(quint32 offset);
    void  setCursor(quint32 offset, bool resetNibble = true);
    qsizetype offsetAt(const QPoint& vpPos) const;  // -1 si hors zone octets
    bool  highlighted(qsizetype off) const;

    RomDocument* m_doc{nullptr};

    qreal m_charW{8};
    qreal m_rowH{16};
    qreal m_ascent{12};
    const int m_margin{10};
    const int m_gap{16};

    quint32 m_cursor{0};
    int     m_pendingNibble{-1};   // premier digit hex saisi (sinon -1)

    QList<QPair<quint32, quint32>> m_highlights;
};

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

void HexView::gotoOffset(quint32 offset) {
    if (romSize() == 0) return;
    setCursor(offset);
}

void HexView::setCursor(quint32 offset, bool resetNibble) {
    const qsizetype n = romSize();
    if (n == 0) { m_cursor = 0; return; }
    m_cursor = quint32(std::clamp<qsizetype>(offset, 0, n - 1));
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
        if (off >= 0) setCursor(quint32(off));
        setFocus(Qt::MouseFocusReason);
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void HexView::keyPressEvent(QKeyEvent* e) {
    const qsizetype n = romSize();
    if (n == 0) { QAbstractScrollArea::keyPressEvent(e); return; }

    const qsizetype page = qsizetype(rowsPerPage()) * kBytesPerRow;

    switch (e->key()) {
    case Qt::Key_Left:     setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - 1)));            return;
    case Qt::Key_Right:    setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + 1)));        return;
    case Qt::Key_Up:       setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - kBytesPerRow))); return;
    case Qt::Key_Down:     setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + kBytesPerRow))); return;
    case Qt::Key_PageUp:   setCursor(quint32(std::max<qsizetype>(0, qsizetype(m_cursor) - page)));         return;
    case Qt::Key_PageDown: setCursor(quint32(std::min<qsizetype>(n - 1, qsizetype(m_cursor) + page)));     return;
    case Qt::Key_Home:     setCursor(0);                  return;
    case Qt::Key_End:      setCursor(quint32(n - 1));     return;
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
    const QColor accent  (0x63, 0x66, 0xf1);   // #6366f1
    const QColor hlBg    (0x63, 0x66, 0xf1, 60);
    const QColor modBg   (0x22, 0xc5, 0x5e, 70);
    const QColor curBg   (0x63, 0x66, 0xf1, 130);

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

        // Adresse.
        p.setPen(addrCol);
        p.drawText(QPointF(addrX(), ty),
                   QString("%1").arg(quint32(rowOff), 8, 16, QLatin1Char('0')).toUpper());

        for (int col = 0; col < kBytesPerRow; ++col) {
            const qsizetype off = rowOff + col;
            if (off >= n) break;

            const quint8 b = quint8(rom.at(off));
            const qreal  bx = hexColX(col);

            // Fonds (highlight de map, octet sous le curseur).
            if (highlighted(off))
                p.fillRect(QRectF(bx, y, 2 * m_charW + (col == 7 ? 0 : m_charW), m_rowH), hlBg);
            if (off == qsizetype(m_cursor))
                p.fillRect(QRectF(bx - m_charW / 4.0, y, 2 * m_charW + m_charW / 2.0, m_rowH),
                           hasFocus() ? curBg : hlBg);

            // Octet hex.
            p.setPen(off == qsizetype(m_cursor) ? accent
                                                : (b == 0 ? zeroCol : hexCol));
            p.drawText(QPointF(bx, ty),
                       QString("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper());

            // ASCII.
            const qreal ax = asciiX() + col * m_charW;
            const QChar ch = (b >= 0x20 && b < 0x7f) ? QChar(b) : QChar('.');
            p.setPen(off == qsizetype(m_cursor) ? accent
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

// ─────────────────────────────────────────────────────────────────────────────
//  HexViewPanel
// ─────────────────────────────────────────────────────────────────────────────
HexViewPanel::HexViewPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc), m_ownsDoc(false) {
    if (!m_doc) { m_doc = new RomDocument(this); m_ownsDoc = true; }
    buildUi();
}

HexViewPanel::HexViewPanel(QWidget* parent)
    : QWidget(parent) {
    m_doc = new RomDocument(this);
    m_ownsDoc = true;
    buildUi();
}

void HexViewPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

    m_view = new HexView(m_doc, this);
    m_view->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_view, 1);

    m_status = new QLabel(tr("Aucune ROM chargée"), this);
    m_status->setStyleSheet("color:#7c8fa6; font-family:monospace; font-size:12px;");
    root->addWidget(m_status);

    // Curseur de la vue -> ligne de statut.
    connect(m_view, &HexView::cursorChanged, this,
            [this](quint32 off) { updateStatus(off); });

    // Réactions au document partagé.
    connect(m_doc, &RomDocument::romLoaded, this, [this]() {
        m_view->verticalScrollBar()->setValue(0);
        m_view->gotoOffset(0);
        m_view->viewport()->update();
        if (m_doc->isLoaded()) updateStatus(0);
        else                   m_status->setText(tr("Aucune ROM chargée"));
    });
    connect(m_doc, &RomDocument::romModified, this,
            [this](qsizetype, qsizetype) {
                m_view->viewport()->update();
                updateStatus(m_view->cursor());
            });

    if (m_doc->isLoaded()) { m_view->gotoOffset(0); updateStatus(0); }
}

void HexViewPanel::updateStatus(quint32 offset) {
    if (!m_doc->isLoaded() || m_doc->rom().isEmpty()) {
        m_status->setText(tr("Aucune ROM chargée"));
        return;
    }
    const qsizetype n = m_doc->rom().size();
    if (qsizetype(offset) >= n) offset = quint32(n - 1);
    const quint8 v = quint8(m_doc->rom().at(offset));
    const QString offHex = QString("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
    const QString valHex = QString("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
    m_status->setText(tr("offset 0x%1   value 0x%2   dec %3   taille %4 o (%5 Ko)")
                      .arg(offHex)
                      .arg(valHex)
                      .arg(v)
                      .arg(n)
                      .arg(n / 1024));
}

void HexViewPanel::loadRom(const QString& path) {
    m_doc->loadFromFile(path);
}

void HexViewPanel::loadRom(const QByteArray& data) {
    m_doc->loadFromData(data, QStringLiteral("ROM"));
}

void HexViewPanel::gotoOffset(quint32 offset) {
    m_view->gotoOffset(offset);
    m_view->setFocus();
}

void HexViewPanel::setMapHighlights(const QList<QPair<quint32, quint32>>& ranges) {
    m_view->setHighlights(ranges);
}

} // namespace ecu_studio

#include "hex_view_panel.moc"
