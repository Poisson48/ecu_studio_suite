#include "hex_view_panel.h"
#include "hex_view.h"
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
