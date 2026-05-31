#include "hex_view_panel.h"
#include "../rom_document.h"

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

    // ── Barre d'outils ─────────────────────────────────────────────────────
    auto* tb = new QHBoxLayout;
    tb->setSpacing(6);

    auto* gotoLbl = new QLabel(tr("Aller à:"), this);
    m_gotoEdit = new QLineEdit(this);
    m_gotoEdit->setPlaceholderText(tr("offset (0x… ou déc)"));
    m_gotoEdit->setMaximumWidth(140);
    m_gotoEdit->setToolTip(tr("Saisir un offset puis Entrée pour s'y rendre."));

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("rechercher…"));
    m_searchEdit->setMaximumWidth(200);
    m_searchEdit->setToolTip(tr("Motif à rechercher (hex « DE AD BE EF » ou texte ASCII)."));

    m_searchMode = new QComboBox(this);
    m_searchMode->addItem(tr("Hex"));
    m_searchMode->addItem(tr("ASCII"));
    m_searchMode->setToolTip(tr("Interpréter la requête comme octets hex ou texte."));

    m_searchPrev = new QToolButton(this);
    m_searchPrev->setText("◀");
    m_searchPrev->setToolTip(tr("Occurrence précédente (Maj+F3)"));
    m_searchNext = new QToolButton(this);
    m_searchNext->setText("▶");
    m_searchNext->setToolTip(tr("Occurrence suivante (F3)"));

    auto* grpLbl = new QLabel(tr("Groupe:"), this);
    m_grouping = new QComboBox(this);
    m_grouping->addItem(tr("Octet"));   // 1
    m_grouping->addItem(tr("Mot"));     // 2
    m_grouping->addItem(tr("Dword"));   // 4
    m_grouping->setToolTip(tr("Espacement visuel des octets (1 / 2 / 4)."));

    auto* baseLbl = new QLabel(tr("Base:"), this);
    m_base = new QComboBox(this);
    m_base->addItem(tr("Hex"));
    m_base->addItem(tr("Déc"));
    m_base->setToolTip(tr("Base d'affichage des offsets (colonne d'adresse)."));

    tb->addWidget(gotoLbl);
    tb->addWidget(m_gotoEdit);
    tb->addSpacing(10);
    tb->addWidget(m_searchEdit);
    tb->addWidget(m_searchMode);
    tb->addWidget(m_searchPrev);
    tb->addWidget(m_searchNext);
    tb->addSpacing(10);
    tb->addWidget(grpLbl);
    tb->addWidget(m_grouping);
    tb->addWidget(baseLbl);
    tb->addWidget(m_base);
    tb->addStretch();
    root->addLayout(tb);

    // ── Vue hex ────────────────────────────────────────────────────────────
    m_view = new HexView(m_doc, this);
    m_view->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_view, 1);

    // ── Inspecteur de sélection ────────────────────────────────────────────
    m_inspector = new QLabel(this);
    m_inspector->setStyleSheet("color:#9ca3af; font-family:monospace; font-size:11px;"
                               "background:#0d1320; border:1px solid #1f2937;"
                               "border-radius:4px; padding:4px 8px;");
    m_inspector->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_inspector);

    // ── Ligne de statut ────────────────────────────────────────────────────
    m_status = new QLabel(tr("Aucune ROM chargée"), this);
    m_status->setStyleSheet("color:#7c8fa6; font-family:monospace; font-size:12px;");
    root->addWidget(m_status);

    // ── Connexions barre d'outils ──────────────────────────────────────────
    connect(m_gotoEdit, &QLineEdit::returnPressed, this, &HexViewPanel::onGotoSubmitted);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() { doSearch(true); });
    connect(m_searchNext, &QToolButton::clicked, this, [this]() { doSearch(true); });
    connect(m_searchPrev, &QToolButton::clicked, this, [this]() { doSearch(false); });
    connect(m_grouping, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &HexViewPanel::onGroupingChanged);
    connect(m_base, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &HexViewPanel::onBaseChanged);

    // ── Connexions vue ─────────────────────────────────────────────────────
    connect(m_view, &HexView::cursorChanged, this, [this](quint32 off) {
        updateStatus(off);
        updateInspector(off);
    });
    connect(m_view, &HexView::contextMenuRequested, this,
            [this](const QPoint& gp, quint32 off) { showContextMenu(gp, off); });

    // ── Réactions au document partagé ──────────────────────────────────────
    connect(m_doc, &RomDocument::romLoaded, this, [this]() {
        m_view->verticalScrollBar()->setValue(0);
        m_view->captureOriginal();          // baseline pour le diff
        m_lastMatch = -1;
        m_view->gotoOffset(0);
        m_view->viewport()->update();
        if (m_doc->isLoaded()) { updateStatus(0); updateInspector(0); }
        else                   { m_status->setText(tr("Aucune ROM chargée")); m_inspector->clear(); }
    });
    connect(m_doc, &RomDocument::romModified, this,
            [this](qsizetype, qsizetype) {
                m_view->viewport()->update();
                updateStatus(m_view->cursor());
                updateInspector(m_view->cursor());
            });

    if (m_doc->isLoaded()) {
        m_view->captureOriginal();
        m_view->gotoOffset(0);
        updateStatus(0);
        updateInspector(0);
    }
}

void HexViewPanel::onGotoSubmitted() {
    const QString t = m_gotoEdit->text().trimmed();
    if (t.isEmpty()) return;
    bool ok = false;
    quint32 off = t.startsWith("0x", Qt::CaseInsensitive)
        ? t.mid(2).toUInt(&ok, 16)
        : t.toUInt(&ok, m_base->currentIndex() == 0 ? 16 : 10);
    if (!ok) {
        // Tentative hex implicite en mode Hex.
        off = t.toUInt(&ok, 16);
    }
    if (!ok) { m_status->setText(tr("Offset invalide : %1").arg(t)); return; }
    gotoOffset(off);
}

bool HexViewPanel::buildSearchPattern(QByteArray& out, QString& err) const {
    const QString q = m_searchEdit->text();
    if (q.trimmed().isEmpty()) { err = tr("Requête vide."); return false; }

    if (m_searchMode->currentIndex() == 1) {        // ASCII
        out = q.toUtf8();
        return !out.isEmpty();
    }
    // Hex : tolère espaces / virgules / 0x.
    QString clean = q;
    clean.remove("0x", Qt::CaseInsensitive);
    clean.replace(QRegularExpression(R"([\s,]+)"), "");
    if (clean.isEmpty() || clean.size() % 2 != 0) {
        err = tr("Hex invalide (nombre de chiffres pair attendu).");
        return false;
    }
    out.clear();
    out.reserve(clean.size() / 2);
    for (int i = 0; i < clean.size(); i += 2) {
        bool ok = false;
        const int byte = clean.mid(i, 2).toInt(&ok, 16);
        if (!ok) { err = tr("Hex invalide."); return false; }
        out.append(char(byte));
    }
    return true;
}

void HexViewPanel::doSearch(bool forward) {
    if (!m_doc->isLoaded()) { m_status->setText(tr("Aucune ROM chargée")); return; }
    QByteArray pattern; QString err;
    if (!buildSearchPattern(pattern, err)) { m_status->setText(err); return; }

    const QByteArray& rom = m_doc->rom();
    qsizetype found = -1;
    if (forward) {
        const qsizetype from = (m_lastMatch >= 0 ? m_lastMatch + 1 : qsizetype(m_view->cursor()) + 1);
        found = rom.indexOf(pattern, from);
        if (found < 0) found = rom.indexOf(pattern, 0);   // boucle
    } else {
        const qsizetype before = (m_lastMatch >= 0 ? m_lastMatch - 1 : qsizetype(m_view->cursor()) - 1);
        found = rom.lastIndexOf(pattern, before);
        if (found < 0) found = rom.lastIndexOf(pattern);   // boucle
    }

    if (found < 0) {
        m_status->setText(tr("Motif introuvable (%1 octet(s)).").arg(pattern.size()));
        return;
    }
    m_lastMatch = found;
    m_view->gotoOffset(quint32(found));
    m_status->setText(tr("Trouvé à 0x%1 (%2 octet(s)).")
        .arg(quint32(found), 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x")
        + QString(" — %1 o").arg(pattern.size()));
}

void HexViewPanel::onGroupingChanged(int index) {
    const int g = index == 0 ? 1 : index == 1 ? 2 : 4;
    m_view->setGroupSize(g);
}

void HexViewPanel::onBaseChanged(int index) {
    m_view->setOffsetHex(index == 0);
    updateStatus(m_view->cursor());
}

void HexViewPanel::showContextMenu(const QPoint& globalPos, quint32 offset) {
    if (!m_doc->isLoaded()) return;
    QMenu menu(this);
    const qsizetype len = m_view->selLength();
    menu.addAction(tr("Copier en hex (%1 o)").arg(len), this, [this, offset]() { copyHex(offset); });
    menu.addAction(tr("Copier en tableau C"), this, [this, offset]() { copyCArray(offset); });
    menu.addAction(tr("Copier l'adresse"), this, [this, offset]() { copyAddress(offset); });
    menu.addSeparator();
    menu.addAction(tr("Remplir la sélection…"), this, [this, offset]() { fillSelection(offset); });
    menu.addAction(tr("Coller (hex)…"), this, [this, offset]() { pasteHex(offset); });
    menu.exec(globalPos);
}

void HexViewPanel::copyHex(quint32 /*offset*/) {
    const QByteArray& rom = m_doc->rom();
    const qsizetype b = m_view->selBegin();
    const qsizetype e = std::min<qsizetype>(m_view->selEnd(), rom.size() - 1);
    QString out;
    out.reserve(int((e - b + 1) * 3));
    for (qsizetype i = b; i <= e; ++i) {
        if (i != b) out += ' ';
        out += QString("%1").arg(quint8(rom.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    QApplication::clipboard()->setText(out);
    m_status->setText(tr("%1 octet(s) copié(s) en hex.").arg(e - b + 1));
}

void HexViewPanel::copyCArray(quint32 /*offset*/) {
    const QByteArray& rom = m_doc->rom();
    const qsizetype b = m_view->selBegin();
    const qsizetype e = std::min<qsizetype>(m_view->selEnd(), rom.size() - 1);
    QString out = QStringLiteral("uint8_t data[%1] = {\n    ").arg(e - b + 1);
    for (qsizetype i = b; i <= e; ++i) {
        out += QString("0x%1").arg(quint8(rom.at(i)), 2, 16, QLatin1Char('0')).toUpper().replace("0X", "0x");
        if (i != e) out += ", ";
        if ((i - b + 1) % 12 == 0 && i != e) out += "\n    ";
    }
    out += "\n};";
    QApplication::clipboard()->setText(out);
    m_status->setText(tr("%1 octet(s) copié(s) en tableau C.").arg(e - b + 1));
}

void HexViewPanel::copyAddress(quint32 offset) {
    QApplication::clipboard()->setText(
        QString("0x%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x"));
    m_status->setText(tr("Adresse copiée."));
}

void HexViewPanel::fillSelection(quint32 /*offset*/) {
    bool ok = false;
    const QString in = QInputDialog::getText(this, tr("Remplir la sélection"),
        tr("Valeur de remplissage (octet hex, ex. FF) :"), QLineEdit::Normal, "00", &ok);
    if (!ok) return;
    bool conv = false;
    const int val = in.trimmed().toInt(&conv, 16);
    if (!conv || val < 0 || val > 0xFF) { m_status->setText(tr("Octet hex invalide.")); return; }

    QByteArray& rom = m_doc->romMutable();
    const qsizetype b = m_view->selBegin();
    const qsizetype e = std::min<qsizetype>(m_view->selEnd(), rom.size() - 1);
    for (qsizetype i = b; i <= e; ++i) rom[i] = char(val);
    m_doc->markModified(b, e - b + 1);
    m_status->setText(tr("%1 octet(s) remplis avec 0x%2.")
        .arg(e - b + 1).arg(val, 2, 16, QLatin1Char('0')));
}

void HexViewPanel::pasteHex(quint32 offset) {
    const QString clip = QApplication::clipboard()->text();
    QString clean = clip;
    clean.remove("0x", Qt::CaseInsensitive);
    clean.replace(QRegularExpression(R"([\s,]+)"), "");
    if (clean.isEmpty() || clean.size() % 2 != 0) {
        m_status->setText(tr("Presse-papiers : hex invalide."));
        return;
    }
    QByteArray bytes;
    for (int i = 0; i < clean.size(); i += 2) {
        bool ok = false;
        bytes.append(char(clean.mid(i, 2).toInt(&ok, 16)));
        if (!ok) { m_status->setText(tr("Presse-papiers : hex invalide.")); return; }
    }
    QByteArray& rom = m_doc->romMutable();
    qsizetype written = 0;
    for (qsizetype i = 0; i < bytes.size() && offset + i < quint32(rom.size()); ++i) {
        rom[offset + i] = bytes.at(i);
        ++written;
    }
    if (written > 0) {
        m_doc->markModified(offset, written);
        m_status->setText(tr("%1 octet(s) collé(s) à 0x%2.")
            .arg(written).arg(offset, 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x"));
    }
}

void HexViewPanel::updateStatus(quint32 offset) {
    if (!m_doc->isLoaded() || m_doc->rom().isEmpty()) {
        m_status->setText(tr("Aucune ROM chargée"));
        return;
    }
    const qsizetype n = m_doc->rom().size();
    if (qsizetype(offset) >= n) offset = quint32(n - 1);
    const quint8 v = quint8(m_doc->rom().at(offset));
    const bool hexBase = m_base->currentIndex() == 0;
    const QString offStr = hexBase
        ? QString("0x") + QString("%1").arg(offset, 8, 16, QLatin1Char('0')).toUpper()
        : QString::number(offset);
    const QString valHex = QString("%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
    const qsizetype selLen = m_view->selLength();
    const QString selStr = selLen > 1 ? tr("   sél %1 o").arg(selLen) : QString();
    m_status->setText(tr("offset %1   valeur 0x%2   déc %3%4   taille %5 o (%6 Ko)")
                      .arg(offStr)
                      .arg(valHex)
                      .arg(v)
                      .arg(selStr)
                      .arg(n)
                      .arg(n / 1024));
}

void HexViewPanel::updateInspector(quint32 offset) {
    if (!m_doc->isLoaded() || m_doc->rom().isEmpty()) { m_inspector->clear(); return; }
    const QByteArray& rom = m_doc->rom();
    const qsizetype n = rom.size();
    if (qsizetype(offset) >= n) offset = quint32(n - 1);

    const quint8  u8  = quint8(rom.at(offset));
    const qint8   s8  = qint8(u8);
    auto rd = [&](qsizetype i) -> quint32 { return i < n ? quint8(rom.at(i)) : 0; };

    // Lectures little-endian et big-endian sur 16 bits.
    const quint16 u16le = quint16(rd(offset) | (rd(offset + 1) << 8));
    const quint16 u16be = quint16((rd(offset) << 8) | rd(offset + 1));
    const quint32 u32le = rd(offset) | (rd(offset + 1) << 8) | (rd(offset + 2) << 16) | (rd(offset + 3) << 24);
    const quint32 u32be = (rd(offset) << 24) | (rd(offset + 1) << 16) | (rd(offset + 2) << 8) | rd(offset + 3);

    const QChar ascii = (u8 >= 0x20 && u8 < 0x7f) ? QChar(u8) : QChar('.');

    m_inspector->setText(tr(
        "u8 %1  s8 %2  ascii '%3'   "
        "u16 LE %4 / BE %5   "
        "u32 LE %6 / BE %7")
        .arg(u8).arg(s8).arg(ascii)
        .arg(u16le).arg(u16be)
        .arg(u32le).arg(u32be));
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
