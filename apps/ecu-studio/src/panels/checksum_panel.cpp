#include "checksum_panel.h"
#include "../rom_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextEdit>
#include <QDateTime>
#include <QFont>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace ecu_studio {

// ─────────────────────────────────────────────────────────────────────────────
//  Valeurs par défaut EDC16
//
//  TODO_REVERSE : ces offsets sont des valeurs « plausibles » génériques pour
//  la famille Bosch EDC16 et NE correspondent PAS à un binaire précis. Les vrais
//  offsets (région(s) sommée(s) + emplacement de stockage du checksum) doivent
//  être déterminés par reverse engineering pour chaque variante (EDC16C34,
//  EDC16U31, EDC16UC40, …) :
//    - EDC16 : flash 2 Mo (0x000000–0x1FFFFF), souvent plusieurs blocs avec un
//      mot checksum + son complément stocké en fin de bloc ou dans une table
//      de descripteurs ("checksum table").
//  Brancher ici les offsets réels une fois identifiés.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr quint32 kDefaultRegionStart = 0x010000; // TODO_REVERSE début région
constexpr quint32 kDefaultRegionEnd   = 0x1F0000; // TODO_REVERSE fin région (exclu)
constexpr quint32 kDefaultStoreOffset = 0x01FFF0; // TODO_REVERSE offset stockage
} // namespace

ChecksumPanel::ChecksumPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded, this, &ChecksumPanel::onRomLoaded);
        connect(m_doc, &RomDocument::romModified, this,
                [this](qsizetype, qsizetype) { updateEnabled(); });
    }
    onRomLoaded();
}

// Surcharge de commodité : délègue avec un document nul.
ChecksumPanel::ChecksumPanel(QWidget* parent)
    : ChecksumPanel(nullptr, parent) {}

void ChecksumPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Région & paramètres ─────────────────────────────────────────────────
    auto* paramBox = new QGroupBox(tr("Région & algorithme"), this);
    auto* grid     = new QGridLayout(paramBox);

    auto* hexRe = new QRegularExpression("(0x)?[0-9A-Fa-f]{1,8}");
    auto makeHexEdit = [&](quint32 def) {
        auto* e = new QLineEdit(this);
        e->setText(QString("0x%1").arg(def, 6, 16, QLatin1Char('0')).toUpper().replace("0X", "0x"));
        e->setValidator(new QRegularExpressionValidator(*hexRe, e));
        e->setMaximumWidth(140);
        return e;
    };

    grid->addWidget(new QLabel(tr("Début région:"), this), 0, 0);
    m_startEdit = makeHexEdit(kDefaultRegionStart);
    grid->addWidget(m_startEdit, 0, 1);

    grid->addWidget(new QLabel(tr("Fin région (exclu):"), this), 0, 2);
    m_endEdit = makeHexEdit(kDefaultRegionEnd);
    grid->addWidget(m_endEdit, 0, 3);

    grid->addWidget(new QLabel(tr("Offset stockage:"), this), 1, 0);
    m_storeEdit = makeHexEdit(kDefaultStoreOffset);
    grid->addWidget(m_storeEdit, 1, 1);

    grid->addWidget(new QLabel(tr("Algorithme:"), this), 1, 2);
    m_algoCombo = new QComboBox(this);
    m_algoCombo->addItem("Sum32", static_cast<int>(Algo::Sum32));
    m_algoCombo->addItem("Sum16", static_cast<int>(Algo::Sum16));
    m_algoCombo->addItem("Xor32", static_cast<int>(Algo::Xor32));
    grid->addWidget(m_algoCombo, 1, 3);

    grid->addWidget(new QLabel(tr("Endianness:"), this), 2, 0);
    m_endianCombo = new QComboBox(this);
    m_endianCombo->addItem(tr("Little-endian"), false);
    m_endianCombo->addItem(tr("Big-endian"), true);
    grid->addWidget(m_endianCombo, 2, 1);

    delete hexRe; // le validator détient sa propre copie

    root->addWidget(paramBox);

    // ── Actions ─────────────────────────────────────────────────────────────
    auto* actionsRow = new QHBoxLayout;
    m_verifyBtn  = new QPushButton(tr("Vérifier"), this);
    m_correctBtn = new QPushButton(tr("Corriger"), this);
    m_correctBtn->setObjectName("accentBtn");
    m_romLabel = new QLabel(this);
    m_romLabel->setStyleSheet("color:#7c8fa6;");
    actionsRow->addWidget(m_verifyBtn);
    actionsRow->addWidget(m_correctBtn);
    actionsRow->addStretch();
    actionsRow->addWidget(m_romLabel);
    root->addLayout(actionsRow);

    // ── Tableau des résultats ───────────────────────────────────────────────
    auto* resBox = new QGroupBox(tr("Résultats"), this);
    auto* resLay = new QVBoxLayout(resBox);
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels(
        { tr("Algorithme"), tr("Calculé"), tr("Stocké"), tr("État") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    resLay->addWidget(m_table);
    root->addWidget(resBox);

    // ── Journal ─────────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(tr("Journal"), this);
    auto* logLay = new QVBoxLayout(logBox);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->document()->setMaximumBlockCount(500);
    m_log->setFont(QFont("Monospace", 10));
    m_log->setStyleSheet("background:#111827; color:#e5e7eb; border:none;");
    logLay->addWidget(m_log);
    root->addWidget(logBox, 1);

    connect(m_verifyBtn,  &QPushButton::clicked, this, &ChecksumPanel::verify);
    connect(m_correctBtn, &QPushButton::clicked, this, &ChecksumPanel::runCorrection);
}

void ChecksumPanel::log(const QString& msg, bool error) {
    const QString time  = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString color = error ? "#ef4444" : "#7c8fa6";
    m_log->append(QString("<span style='color:%1'>%2</span> <span style='color:#f1f5f9'>%3</span>")
                  .arg(color, time, msg.toHtmlEscaped()));
}

void ChecksumPanel::onRomLoaded() {
    if (m_doc && m_doc->isLoaded()) {
        m_romLabel->setText(tr("ROM : %1 (%2 Ko)")
                            .arg(m_doc->name())
                            .arg(m_doc->rom().size() / 1024));
        log(tr("ROM chargée : %1 (%2 octets)")
            .arg(m_doc->name()).arg(m_doc->rom().size()));
    } else {
        m_romLabel->setText(tr("Aucune ROM chargée"));
    }
    updateEnabled();
}

void ChecksumPanel::updateEnabled() {
    const bool loaded = (m_doc && m_doc->isLoaded());
    m_verifyBtn->setEnabled(loaded);
    m_correctBtn->setEnabled(loaded);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Parsing / lecture des paramètres
// ─────────────────────────────────────────────────────────────────────────────
quint32 ChecksumPanel::parseHex(const QString& text, bool* ok) {
    QString s = text.trimmed();
    if (s.startsWith("0x", Qt::CaseInsensitive))
        s = s.mid(2);
    return s.toUInt(ok, 16);
}

bool ChecksumPanel::readParams(qsizetype& start, qsizetype& end,
                               qsizetype& storeOffset, bool& bigEndian, Algo& algo) {
    if (!m_doc || !m_doc->isLoaded()) {
        log(tr("Aucune ROM chargée"), true);
        return false;
    }
    const qsizetype size = m_doc->rom().size();

    bool okS = false, okE = false, okO = false;
    start       = static_cast<qsizetype>(parseHex(m_startEdit->text(), &okS));
    end         = static_cast<qsizetype>(parseHex(m_endEdit->text(),   &okE));
    storeOffset = static_cast<qsizetype>(parseHex(m_storeEdit->text(), &okO));

    if (!okS || !okE || !okO) {
        log(tr("Offsets invalides (attendu hexadécimal, ex. 0x010000)"), true);
        return false;
    }
    if (start < 0 || end < 0 || storeOffset < 0) {
        log(tr("Offsets négatifs non autorisés"), true);
        return false;
    }
    if (start >= end) {
        log(tr("Région vide : début (0x%1) >= fin (0x%2)")
            .arg(start, 0, 16).arg(end, 0, 16), true);
        return false;
    }
    if (end > size) {
        log(tr("Fin de région 0x%1 hors ROM (taille 0x%2)")
            .arg(end, 0, 16).arg(size, 0, 16), true);
        return false;
    }
    // L'emplacement de stockage est un mot 32 bits : il faut 4 octets.
    if (storeOffset + 4 > size) {
        log(tr("Offset de stockage 0x%1 hors ROM (taille 0x%2)")
            .arg(storeOffset, 0, 16).arg(size, 0, 16), true);
        return false;
    }

    bigEndian = m_endianCombo->currentData().toBool();
    algo      = static_cast<Algo>(m_algoCombo->currentData().toInt());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Algorithmes de checksum
//
//  Implémentation minimale et documentée pour la famille EDC16. Les EDC16 réels
//  combinent généralement une somme additive 32 bits sur un (des) bloc(s) flash
//  et stockent le checksum + son complément à un offset descripteur. On fournit
//  ici les briques additive/XOR, à brancher sur les vrais offsets (TODO_REVERSE).
// ─────────────────────────────────────────────────────────────────────────────
quint32 ChecksumPanel::computeChecksum(const QByteArray& rom, qsizetype start,
                                       qsizetype end, Algo algo, bool bigEndian) {
    const auto* p = reinterpret_cast<const quint8*>(rom.constData());

    auto word32 = [&](qsizetype i) -> quint32 {
        return bigEndian
            ? (quint32(p[i]) << 24) | (quint32(p[i + 1]) << 16) |
              (quint32(p[i + 2]) << 8) | quint32(p[i + 3])
            : quint32(p[i]) | (quint32(p[i + 1]) << 8) |
              (quint32(p[i + 2]) << 16) | (quint32(p[i + 3]) << 24);
    };
    auto word16 = [&](qsizetype i) -> quint32 {
        return bigEndian
            ? (quint32(p[i]) << 8) | quint32(p[i + 1])
            : quint32(p[i]) | (quint32(p[i + 1]) << 8);
    };

    quint32 acc = 0;
    switch (algo) {
    case Algo::Sum32:
        for (qsizetype i = start; i + 4 <= end; i += 4)
            acc += word32(i);
        break;
    case Algo::Sum16:
        for (qsizetype i = start; i + 2 <= end; i += 2)
            acc += word16(i);
        acc &= 0xFFFFu;
        break;
    case Algo::Xor32:
        for (qsizetype i = start; i + 4 <= end; i += 4)
            acc ^= word32(i);
        break;
    }
    return acc;
}

quint32 ChecksumPanel::readStored(const QByteArray& rom, qsizetype storeOffset,
                                  bool bigEndian) {
    const auto* p = reinterpret_cast<const quint8*>(rom.constData());
    return bigEndian
        ? (quint32(p[storeOffset]) << 24) | (quint32(p[storeOffset + 1]) << 16) |
          (quint32(p[storeOffset + 2]) << 8) | quint32(p[storeOffset + 3])
        : quint32(p[storeOffset]) | (quint32(p[storeOffset + 1]) << 8) |
          (quint32(p[storeOffset + 2]) << 16) | (quint32(p[storeOffset + 3]) << 24);
}

quint32 ChecksumPanel::computeCrc32(const QByteArray& rom, qsizetype start,
                                    qsizetype end) {
    // CRC-32 IEEE (polynôme réfléchi 0xEDB88320), fourni en option d'affichage.
    quint32 crc = 0xFFFFFFFFu;
    const auto* p = reinterpret_cast<const quint8*>(rom.constData());
    for (qsizetype i = start; i < end; ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xEDB88320u & (~((crc & 1u) - 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Vérification
// ─────────────────────────────────────────────────────────────────────────────
void ChecksumPanel::verify() {
    qsizetype start = 0, end = 0, store = 0;
    bool bigEndian = false;
    Algo algo = Algo::Sum32;
    if (!readParams(start, end, store, bigEndian, algo))
        return;

    const QByteArray& rom = m_doc->rom();

    const quint32 computed = computeChecksum(rom, start, end, algo, bigEndian);
    const quint32 stored   = readStored(rom, store, bigEndian);
    const quint32 crc      = computeCrc32(rom, start, end);

    log(tr("Vérification région [0x%1, 0x%2), stockage 0x%3, %4, %5")
        .arg(start, 0, 16).arg(end, 0, 16).arg(store, 0, 16)
        .arg(m_algoCombo->currentText())
        .arg(bigEndian ? tr("big-endian") : tr("little-endian")));

    auto hex32 = [](quint32 v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x");
    };

    struct Row { QString name; quint32 computed; quint32 stored; bool compare; };
    const QList<Row> rows = {
        { m_algoCombo->currentText(), computed, stored, true },
        { "CRC-32",                   crc,      stored, false },
    };

    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const Row& row = rows[r];
        auto* c0 = new QTableWidgetItem(row.name);
        auto* c1 = new QTableWidgetItem(hex32(row.computed));
        auto* c2 = new QTableWidgetItem(row.compare ? hex32(row.stored) : QString("—"));

        QString state;
        if (!row.compare) {
            state = tr("info");
        } else if (row.computed == row.stored) {
            state = tr("OK");
            c2->setForeground(QColor("#22c55e"));
        } else {
            state = tr("DIFFÉRENT");
            c1->setForeground(QColor("#ef4444"));
            c2->setForeground(QColor("#ef4444"));
        }
        auto* c3 = new QTableWidgetItem(state);

        m_table->setItem(r, 0, c0);
        m_table->setItem(r, 1, c1);
        m_table->setItem(r, 2, c2);
        m_table->setItem(r, 3, c3);
    }

    if (computed == stored)
        log(tr("[OK] Checksum %1 valide (%2)").arg(m_algoCombo->currentText(), hex32(computed)));
    else
        log(tr("[!] Checksum %1 invalide : calculé %2, stocké %3")
            .arg(m_algoCombo->currentText(), hex32(computed), hex32(stored)), true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Correction (slot public appelé par le menu Outils)
// ─────────────────────────────────────────────────────────────────────────────
void ChecksumPanel::runCorrection() {
    qsizetype start = 0, end = 0, store = 0;
    bool bigEndian = false;
    Algo algo = Algo::Sum32;
    if (!readParams(start, end, store, bigEndian, algo))
        return;

    const quint32 computed = computeChecksum(m_doc->rom(), start, end, algo, bigEndian);
    const quint32 stored   = readStored(m_doc->rom(), store, bigEndian);

    if (computed == stored) {
        log(tr("Rien à corriger : checksum déjà valide (0x%1)").arg(computed, 8, 16, QLatin1Char('0')));
        verify();
        return;
    }

    // Écriture in-place du checksum calculé à l'offset de stockage.
    QByteArray& rom = m_doc->romMutable();
    auto* p = reinterpret_cast<quint8*>(rom.data());
    if (bigEndian) {
        p[store + 0] = quint8((computed >> 24) & 0xFF);
        p[store + 1] = quint8((computed >> 16) & 0xFF);
        p[store + 2] = quint8((computed >> 8) & 0xFF);
        p[store + 3] = quint8(computed & 0xFF);
    } else {
        p[store + 0] = quint8(computed & 0xFF);
        p[store + 1] = quint8((computed >> 8) & 0xFF);
        p[store + 2] = quint8((computed >> 16) & 0xFF);
        p[store + 3] = quint8((computed >> 24) & 0xFF);
    }

    // TODO_REVERSE : certaines EDC16 stockent aussi le complément (~checksum)
    // juste après le checksum, ou dans une table de descripteurs. Le brancher
    // ici lorsque l'offset du complément est identifié.

    m_doc->markModified(store, 4);

    log(tr("[OK] Checksum corrigé à l'offset 0x%1 : 0x%2 -> 0x%3")
        .arg(store, 0, 16)
        .arg(stored,   8, 16, QLatin1Char('0'))
        .arg(computed, 8, 16, QLatin1Char('0')));

    verify();
}

} // namespace ecu_studio
