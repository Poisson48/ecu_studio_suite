#include "compare_panel.h"
#include "../rom_document.h"
#include "../byte_span.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>

#include <span>
#include <cstdint>

#include "ecu/MapDiffer.hpp"

namespace ecu_studio {

namespace {
QString hex32(quint64 v) {
    return QStringLiteral("0x") +
           QString("%1").arg(v, 8, 16, QChar('0')).toUpper();
}
} // namespace

ComparePanel::ComparePanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();

    if (m_doc) {
        connect(m_doc, &RomDocument::romLoaded,   this, &ComparePanel::onRomLoadedChanged);
        connect(m_doc, &RomDocument::romModified, this, [this](qsizetype, qsizetype) {
            refresh();
        });
    }
    onRomLoadedChanged();
}

ComparePanel::ComparePanel(QWidget* parent)
    : ComparePanel(nullptr, parent) {}

ComparePanel::~ComparePanel() = default;

void ComparePanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── ROMs comparées ────────────────────────────────────────────────────
    auto* romBox = new QGroupBox(tr("ROMs comparées"), this);
    auto* romLay = new QVBoxLayout(romBox);

    m_romALabel = new QLabel(this);
    m_romALabel->setStyleSheet("color:#e5e7eb;");
    romLay->addWidget(m_romALabel);

    auto* rowB = new QHBoxLayout;
    m_romBLabel = new QLabel(this);
    m_romBLabel->setStyleSheet("color:#e5e7eb;");
    m_romBLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_chooseBtn = new QPushButton(tr("Choisir ROM B..."), this);
    m_chooseBtn->setObjectName("accentBtn");
    rowB->addWidget(m_romBLabel, 1);
    rowB->addWidget(m_chooseBtn);
    romLay->addLayout(rowB);
    root->addWidget(romBox);

    // ── Résumé ────────────────────────────────────────────────────────────
    m_summary = new QLabel(this);
    m_summary->setStyleSheet("color:#7c8fa6;");
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);

    // ── Intervalles modifiés ──────────────────────────────────────────────
    auto* diffBox = new QGroupBox(tr("Plages modifiées"), this);
    auto* diffLay = new QVBoxLayout(diffBox);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Début"), tr("Fin"), tr("Longueur") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFont(QFont("Monospace", 10));
    diffLay->addWidget(m_table);
    root->addWidget(diffBox, 1);

    connect(m_chooseBtn, &QPushButton::clicked, this, &ComparePanel::openComparison);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        onIntervalActivated(row);
    });
}

void ComparePanel::onRomLoadedChanged() {
    const bool hasA = m_doc && m_doc->isLoaded();
    m_chooseBtn->setEnabled(hasA);

    if (hasA) {
        m_romALabel->setText(tr("ROM A : %1 (%2 octets)")
                                 .arg(m_doc->name())
                                 .arg(m_doc->rom().size()));
    } else {
        m_romALabel->setText(tr("ROM A : aucune ROM chargée"));
    }
    refresh();
}

void ComparePanel::openComparison() {
    if (!m_doc || !m_doc->isLoaded()) {
        QMessageBox::information(this, tr("Comparer ROMs"),
            tr("Aucune ROM chargée dans le document. Chargez d'abord une ROM (A)."));
        return;
    }

    const QString f = QFileDialog::getOpenFileName(
        this, tr("ROM à comparer (B)"), {},
        tr("ROM (*.bin *.ori *.mod);;Tous (*.*)"));
    if (f.isEmpty()) return;

    QFile file(f);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Comparer ROMs"),
            tr("Impossible d'ouvrir %1").arg(f));
        return;
    }

    m_romB     = file.readAll();
    m_romBName = QFileInfo(f).fileName();
    refresh();
}

void ComparePanel::refresh() {
    const bool hasA = m_doc && m_doc->isLoaded();
    const bool hasB = !m_romB.isEmpty();

    if (m_romBName.isEmpty()) {
        m_romBLabel->setText(tr("ROM B : aucune"));
    } else {
        m_romBLabel->setText(tr("ROM B : %1 (%2 octets)")
                                 .arg(m_romBName)
                                 .arg(m_romB.size()));
    }

    m_table->setRowCount(0);

    if (!hasA || !hasB) {
        m_summary->setText(tr("Sélectionnez une seconde ROM (B) pour comparer."));
        return;
    }

    const QByteArray& romA = m_doc->rom();

    const std::vector<ecu::DiffInterval> intervals =
        ecu::diffIntervals(constByteSpan(romA), constByteSpan(m_romB));

    std::size_t totalChanged = 0;
    for (const auto& iv : intervals)
        totalChanged += (iv.end - iv.start);

    m_table->setRowCount(static_cast<int>(intervals.size()));
    for (int r = 0; r < static_cast<int>(intervals.size()); ++r) {
        const auto& iv  = intervals[static_cast<std::size_t>(r)];
        const auto  len = iv.end - iv.start;

        auto* startItem = new QTableWidgetItem(hex32(iv.start));
        auto* endItem   = new QTableWidgetItem(hex32(iv.end));
        auto* lenItem   = new QTableWidgetItem(QString::number(len));
        // L'adresse de début est conservée pour le saut hex editor.
        startItem->setData(Qt::UserRole, static_cast<quint32>(iv.start));

        m_table->setItem(r, 0, startItem);
        m_table->setItem(r, 1, endItem);
        m_table->setItem(r, 2, lenItem);
    }

    // ── Résumé ────────────────────────────────────────────────────────────
    QString sizeNote;
    if (romA.size() != m_romB.size()) {
        sizeNote = tr("  ⚠ Tailles différentes (A=%1, B=%2) — comparaison "
                      "sur les %3 octets communs ; le surplus est marqué comme modifié.")
                       .arg(romA.size())
                       .arg(m_romB.size())
                       .arg(std::min<qsizetype>(romA.size(), m_romB.size()));
    }

    if (intervals.empty()) {
        m_summary->setText(tr("ROMs identiques — aucun octet modifié.%1").arg(sizeNote));
    } else {
        m_summary->setText(tr("%1 octet(s) modifié(s) répartis sur %2 plage(s).%3")
                               .arg(totalChanged)
                               .arg(intervals.size())
                               .arg(sizeNote));
    }
}

void ComparePanel::onIntervalActivated(int row) {
    if (row < 0) return;
    QTableWidgetItem* item = m_table->item(row, 0);
    if (!item) return;
    const quint32 address = item->data(Qt::UserRole).toUInt();
    emit gotoAddressRequested(address);
}

} // namespace ecu_studio
