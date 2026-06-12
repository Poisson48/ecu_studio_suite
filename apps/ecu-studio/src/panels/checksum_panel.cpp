#include "checksum_panel.h"
#include "../rom_document.h"
#include "../byte_span.h"
#include "ecu/ChecksumEngine.hpp"
#include "ecu/Edc17Checksum.hpp"

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
#include <QMessageBox>
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
    // Modes à VRAI moteur (reverse-engineered) en premier. EDC16 par défaut ;
    // onRomLoaded() bascule automatiquement sur EDC17 si la ROM en est une.
    m_algoCombo->addItem(tr("MPPS CRC-16/ARC (EDC16)"),
                         static_cast<int>(Algo::MppsCrc16Arc));
    m_algoCombo->addItem(tr("EDC17/MED17 (blocs Bosch — auto)"),
                         static_cast<int>(Algo::Edc17));
    // Modes génériques secondaires (conservés mais non par défaut).
    m_algoCombo->addItem(tr("Sum32 (générique)"), static_cast<int>(Algo::Sum32));
    m_algoCombo->addItem(tr("Sum16 (générique)"), static_cast<int>(Algo::Sum16));
    m_algoCombo->addItem(tr("Xor32 (générique)"), static_cast<int>(Algo::Xor32));
    m_algoCombo->setCurrentIndex(0); // MPPS CRC-16/ARC par défaut
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
    connect(m_algoCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onAlgoChanged(); });

    onAlgoChanged(); // reflète l'état initial (mode MPPS par défaut)
}

bool ChecksumPanel::isMppsMode() const {
    return static_cast<Algo>(m_algoCombo->currentData().toInt())
           == Algo::MppsCrc16Arc;
}

bool ChecksumPanel::isEdc17Mode() const {
    return static_cast<Algo>(m_algoCombo->currentData().toInt()) == Algo::Edc17;
}

// En mode MPPS / EDC17 les régions sont fixées par le moteur (auto-détectées).
// On grise les champs manuels ; ils restent actifs pour les modes génériques.
void ChecksumPanel::onAlgoChanged() {
    const bool generic = !isMppsMode() && !isEdc17Mode();
    m_startEdit->setEnabled(generic);
    m_endEdit->setEnabled(generic);
    m_storeEdit->setEnabled(generic);
    m_endianCombo->setEnabled(generic);
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

        // Auto-détection EDC17/MED17 : si la ROM porte une table de blocs Bosch,
        // on sélectionne le moteur EDC17 (sinon on garde MPPS EDC16 par défaut).
        if (ecu::edc17Verify(constByteSpan(m_doc->rom())).isEdc17 && !isEdc17Mode()) {
            const int idx = m_algoCombo->findData(static_cast<int>(Algo::Edc17));
            if (idx >= 0) {
                m_algoCombo->setCurrentIndex(idx);
                log(tr("EDC17/MED17 détecté (table de blocs Bosch) — moteur sélectionné."));
            }
        }
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
    case Algo::MppsCrc16Arc:
        // Mode MPPS : traité par le moteur réel (verifyMpps/runCorrectionMpps),
        // jamais via ce calcul générique. Présent pour l'exhaustivité du switch.
        break;
    case Algo::Edc17:
        // Mode EDC17/MED17 : traité par le moteur ecu::edc17* (verifyEdc17/
        // runCorrectionEdc17), jamais via ce calcul générique. Idem.
        break;
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
    if (isMppsMode()) {
        verifyMpps();
        return;
    }
    if (isEdc17Mode()) {
        verifyEdc17();
        return;
    }

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
    if (isMppsMode()) {
        runCorrectionMpps();
        return;
    }
    if (isEdc17Mode()) {
        runCorrectionEdc17();
        return;
    }

    // Mode générique (Sum/Xor) : NON validé pour un ECU précis. Les offsets par
    // défaut sont « plausibles » (TODO_REVERSE), pas l'algorithme réel de l'ECU.
    // Écrire ce checksum puis flasher peut BRICKER l'ECU → confirmation explicite.
    if (QMessageBox::warning(
            this, tr("Checksum générique — non validé"),
            tr("L'algorithme « %1 » et ces offsets ne sont PAS validés pour un ECU "
               "précis (valeurs génériques par défaut). Corriger avec un mauvais "
               "algorithme/offset puis flasher peut rendre l'ECU NON DÉMARRABLE.\n\n"
               "À n'utiliser que si tu connais l'algorithme et les offsets EXACTS de "
               "ton ECU. Continuer ?").arg(m_algoCombo->currentText()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

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

// ─────────────────────────────────────────────────────────────────────────────
//  Mode MPPS CRC-16/ARC — moteur réel ecu::ChecksumEngine
//
//  Algorithme reverse-engineered des modules MPPS Check001/Check045 :
//  CRC-16/ARC (poly 0x8005 réfléchi = 0xA001, init 0), checksum stocké en
//  big-endian. La région et l'offset de stockage sont fixés par la TAILLE de la
//  ROM (32 Ko : [0x0030,0x7FEA) stocké @ 0x7FEA ; 64 Ko : [0x8041,0xFFFA)
//  stocké @ 0xFFFA). Cf. docs/mpps-checksums.md §3.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
QString hex16(quint16 v) {
    return QString("0x%1").arg(v, 4, 16, QLatin1Char('0')).toUpper().replace("0X", "0x");
}
// Nom lisible de la disposition auto-détectée.
QString layoutName(ecu::EdcLayout l) {
    switch (l) {
    case ecu::EdcLayout::Edc32k:   return ChecksumPanel::tr("EDC16 32 Ko");
    case ecu::EdcLayout::Edc64k:   return ChecksumPanel::tr("EDC16 64 Ko");
    case ecu::EdcLayout::Unknown:
    default:                       return ChecksumPanel::tr("inconnue");
    }
}
} // namespace

void ChecksumPanel::verifyMpps() {
    if (!m_doc || !m_doc->isLoaded()) {
        log(tr("Aucune ROM chargée"), true);
        return;
    }
    const QByteArray& rom    = m_doc->rom();
    const std::size_t size   = static_cast<std::size_t>(rom.size());
    const ecu::EdcLayout lay = ecu::mppsLayoutForSize(size);

    if (lay == ecu::EdcLayout::Unknown) {
        log(tr("[!] Taille 0x%1 (%2 Ko) non reconnue comme disposition EDC16 MPPS "
               "(attendu 32 Ko ou 64 Ko). Vérification MPPS impossible.")
            .arg(size, 0, 16).arg(size / 1024), true);
        log(tr("Note : le flash signé EDC17 (MD5+RSA, Check008) n'est PAS pris en "
               "charge par ce moteur."));
        m_table->setRowCount(0);
        return;
    }

    const auto region = ecu::mppsRegionForSize(size);
    const auto res     = ecu::ChecksumEngine::verifyMpps(constByteSpan(rom));
    if (!region || !res) { // garde-fou : ne devrait pas arriver après la détection
        log(tr("[!] Disposition EDC16 détectée mais région indisponible."), true);
        return;
    }

    log(tr("Vérification MPPS CRC-16/ARC — disposition %1, région [0x%2, 0x%3), "
           "stockage 0x%4 (big-endian)")
        .arg(layoutName(lay))
        .arg(region->start, 0, 16).arg(region->end, 0, 16)
        .arg(region->checksumOff, 0, 16));

    m_table->setRowCount(1);
    auto* c0 = new QTableWidgetItem(tr("MPPS CRC-16/ARC"));
    auto* c1 = new QTableWidgetItem(hex16(res->computed));
    auto* c2 = new QTableWidgetItem(hex16(res->stored));
    QString state;
    if (res->valid) {
        state = tr("OK");
        c2->setForeground(QColor("#22c55e"));
    } else {
        state = tr("DIFFÉRENT");
        c1->setForeground(QColor("#ef4444"));
        c2->setForeground(QColor("#ef4444"));
    }
    auto* c3 = new QTableWidgetItem(state);
    m_table->setItem(0, 0, c0);
    m_table->setItem(0, 1, c1);
    m_table->setItem(0, 2, c2);
    m_table->setItem(0, 3, c3);

    if (res->valid)
        log(tr("[OK] Checksum MPPS valide (%1)").arg(hex16(res->computed)));
    else
        log(tr("[!] Checksum MPPS invalide : calculé %1, stocké %2")
            .arg(hex16(res->computed), hex16(res->stored)), true);
}

void ChecksumPanel::runCorrectionMpps() {
    if (!m_doc || !m_doc->isLoaded()) {
        log(tr("Aucune ROM chargée"), true);
        return;
    }
    const std::size_t size   = static_cast<std::size_t>(m_doc->rom().size());
    const ecu::EdcLayout lay = ecu::mppsLayoutForSize(size);

    if (lay == ecu::EdcLayout::Unknown) {
        log(tr("[!] Taille 0x%1 (%2 Ko) non reconnue comme disposition EDC16 MPPS "
               "(attendu 32 Ko ou 64 Ko). Correction MPPS impossible.")
            .arg(size, 0, 16).arg(size / 1024), true);
        return;
    }

    const auto region = ecu::mppsRegionForSize(size);
    if (!region) {
        log(tr("[!] Disposition EDC16 détectée mais région indisponible."), true);
        return;
    }

    QByteArray& rom = m_doc->romMutable();
    const auto fixed = ecu::ChecksumEngine::correctMpps(mutByteSpan(rom));
    if (!fixed) { // taille inconnue — déjà filtré, garde-fou
        log(tr("[!] Correction MPPS impossible : taille non reconnue."), true);
        return;
    }

    if (*fixed == 0) {
        log(tr("Rien à corriger : checksum MPPS déjà valide."));
    } else {
        // Le moteur a réécrit le checksum 16 bits big-endian à checksumOff.
        m_doc->markModified(static_cast<qsizetype>(region->checksumOff), 2);
        log(tr("[OK] %n checksum(s) MPPS corrigé(s) à l'offset 0x%1 "
               "(disposition %2).", "", *fixed)
            .arg(region->checksumOff, 0, 16)
            .arg(layoutName(lay)));
    }

    verifyMpps(); // re-vérification après correction
}

// ── Mode EDC17/MED17 (table de blocs Bosch — moteur ecu::edc17*) ──────────────

namespace {
QString edc17AlgoName(ecu::Edc17Algo a) {
    switch (a) {
        case ecu::Edc17Algo::Crc32: return QStringLiteral("CRC32");
        case ecu::Edc17Algo::Add32: return QStringLiteral("ADD32");
        case ecu::Edc17Algo::Add16: return QStringLiteral("ADD16");
        default:                    return QStringLiteral("?");
    }
}
} // namespace

void ChecksumPanel::verifyEdc17() {
    if (!m_doc || !m_doc->isLoaded()) { log(tr("Aucune ROM chargée"), true); return; }

    const ecu::Edc17Result r = ecu::edc17Verify(constByteSpan(m_doc->rom()));
    if (!r.isEdc17) {
        log(tr("[!] Aucune table de blocs EDC17/MED17 détectée dans cette ROM."), true);
        m_table->setRowCount(0);
        return;
    }

    auto hx = [](std::uint32_t v) {
        return QString("0x%1").arg(v, 8, 16, QLatin1Char('0')).toUpper().replace("0X", "0x");
    };

    m_table->setRowCount(r.total());
    int ri = 0;
    for (const auto& b : r.blocks) {
        for (const auto& cs : b.cs) {
            auto* c0 = new QTableWidgetItem(
                QString("blk 0x%1 · %2 [0x%3..0x%4]")
                    .arg(b.id & 0xFF, 2, 16, QLatin1Char('0'))
                    .arg(edc17AlgoName(cs.algo))
                    .arg(cs.startAddr, 0, 16).arg(cs.endAddr, 0, 16));
            auto* c1 = new QTableWidgetItem(cs.inBounds ? hx(cs.computed) : QStringLiteral("—"));
            auto* c2 = new QTableWidgetItem(
                cs.algo == ecu::Edc17Algo::Crc32 ? hx(0x35015001u) : hx(cs.expected));
            QString state;
            if (!cs.inBounds) {
                state = tr("hors ROM");
            } else if (cs.valid) {
                state = tr("OK"); c2->setForeground(QColor("#22c55e"));
            } else {
                state = tr("DIFFÉRENT");
                c1->setForeground(QColor("#ef4444")); c2->setForeground(QColor("#ef4444"));
            }
            m_table->setItem(ri, 0, c0); m_table->setItem(ri, 1, c1);
            m_table->setItem(ri, 2, c2); m_table->setItem(ri, 3, new QTableWidgetItem(state));
            ++ri;
        }
    }
    log(tr("EDC17/MED17 : %1 bloc(s), %2/%3 structure(s) valides (%4 hors ROM).")
            .arg(static_cast<int>(r.blocks.size()))
            .arg(r.validCount()).arg(r.inBoundsCount())
            .arg(r.total() - r.inBoundsCount()));
}

void ChecksumPanel::runCorrectionEdc17() {
    if (!m_doc || !m_doc->isLoaded()) { log(tr("Aucune ROM chargée"), true); return; }

    QByteArray& rom = m_doc->romMutable();
    const auto fixed = ecu::edc17Correct(mutByteSpan(rom));
    if (!fixed) {
        log(tr("[!] Pas une ROM EDC17/MED17 (aucune table de blocs) — rien corrigé."), true);
        return;
    }
    if (*fixed == 0) {
        log(tr("Rien à corriger : tous les checksums EDC17/MED17 sont déjà valides."));
    } else {
        m_doc->markModified();   // plusieurs régions touchées → versionné en git
        log(tr("[OK] %n checksum(s) EDC17/MED17 corrigé(s) (CRC32 + ADD32).", "", *fixed));
    }
    verifyEdc17();   // re-vérifie et rafraîchit le tableau
}

} // namespace ecu_studio
