#include "compare_panel.h"
#include "git_blob_utils.h"
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
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDir>

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
    // Par défaut, l'opérande A suit la ROM courante du document.
    m_a.fromDoc = true;

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

    auto* rowA = new QHBoxLayout;
    m_romALabel = new QLabel(this);
    m_romALabel->setStyleSheet("color:#e5e7eb;");
    m_romALabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_chooseABtn = new QPushButton(tr("Choisir ROM A…"), this);
    rowA->addWidget(m_romALabel, 1);
    rowA->addWidget(m_chooseABtn);
    romLay->addLayout(rowA);

    auto* rowB = new QHBoxLayout;
    m_romBLabel = new QLabel(this);
    m_romBLabel->setStyleSheet("color:#e5e7eb;");
    m_romBLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_chooseBBtn = new QPushButton(tr("Choisir ROM B…"), this);
    m_chooseBBtn->setObjectName("accentBtn");
    rowB->addWidget(m_romBLabel, 1);
    rowB->addWidget(m_chooseBBtn);
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

    connect(m_chooseABtn, &QPushButton::clicked, this, [this]() {
        if (pickSource(tr("ROM A à comparer"), m_a)) refresh();
    });
    connect(m_chooseBBtn, &QPushButton::clicked, this, &ComparePanel::openComparison);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int) {
        onIntervalActivated(row);
    });
}

void ComparePanel::onRomLoadedChanged() {
    // Les boutons de choix restent toujours actifs : on peut comparer un fichier
    // ou deux commits même sans document chargé. Les options « document » et
    // « commit git » du dialogue s'activent selon leur disponibilité.
    refresh();
}

QByteArray ComparePanel::slotBytes(const Slot& slot) const {
    if (slot.fromDoc)
        return (m_doc && m_doc->isLoaded()) ? m_doc->rom() : QByteArray();
    return slot.bytes;
}

QString ComparePanel::slotText(const QString& which, const Slot& slot) const {
    const QByteArray b = slotBytes(slot);
    if (slot.fromDoc) {
        if (b.isEmpty())
            return tr("ROM %1 : document (aucune ROM chargée)").arg(which);
        const QString name = m_doc ? m_doc->name() : tr("courante");
        return tr("ROM %1 : %2 (document, %3 octets)").arg(which, name).arg(b.size());
    }
    if (slot.label.isEmpty())
        return tr("ROM %1 : aucune").arg(which);
    return tr("ROM %1 : %2 (%3 octets)").arg(which, slot.label).arg(b.size());
}

bool ComparePanel::pickSource(const QString& title, Slot& slot) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(tr("Choisissez la source de cette ROM à comparer :"), &dlg));

    // Option 1 : ROM courante du document.
    auto* docBtn = new QPushButton(tr("ROM courante du document"), &dlg);
    docBtn->setEnabled(m_doc && m_doc->isLoaded());
    lay->addWidget(docBtn);

    // Option 2 : commit git du dépôt du projet.
    const QString romPath  = m_doc ? m_doc->path() : QString();
    const QString repoRoot = gitToplevelFor(romPath);
    auto* gitGroup = new QGroupBox(tr("Depuis un commit git"), &dlg);
    auto* gitLay   = new QVBoxLayout(gitGroup);
    auto* commitCombo = new QComboBox(gitGroup);
    auto* gitApplyBtn = new QPushButton(tr("Utiliser ce commit"), gitGroup);
    gitLay->addWidget(commitCombo);
    gitLay->addWidget(gitApplyBtn);
    QString relPath;
    if (repoRoot.isEmpty()) {
        commitCombo->setEnabled(false);
        gitApplyBtn->setEnabled(false);
        gitLay->addWidget(new QLabel(
            tr("(la ROM n'est pas dans un dépôt git — option indisponible)"), gitGroup));
    } else {
        QDir d(repoRoot);
        relPath = d.relativeFilePath(romPath);
        const auto commits = gitRecentCommits(repoRoot, 40);
        if (commits.isEmpty()) {
            commitCombo->setEnabled(false);
            gitApplyBtn->setEnabled(false);
            gitLay->addWidget(new QLabel(tr("(aucun commit listable — repo vide ?)"), gitGroup));
        } else {
            for (const auto& c : commits)
                commitCombo->addItem(QString("%1  %2").arg(c.sha.left(8), c.summary),
                                     c.sha);
        }
    }
    lay->addWidget(gitGroup);

    // Option 3 : fichier .bin externe.
    auto* fileBtn = new QPushButton(tr("Depuis un fichier .bin externe…"), &dlg);
    lay->addWidget(fileBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    bool chosen = false;

    connect(docBtn, &QPushButton::clicked, &dlg, [&]() {
        slot.fromDoc = true;
        slot.bytes.clear();
        slot.label.clear();
        chosen = true;
        dlg.accept();
    });
    connect(gitApplyBtn, &QPushButton::clicked, &dlg, [&]() {
        if (repoRoot.isEmpty() || commitCombo->currentIndex() < 0) return;
        const QString sha = commitCombo->currentData().toString();
        const QByteArray bytes = gitBlobAt(repoRoot, sha, relPath);
        if (bytes.isEmpty()) {
            QMessageBox::warning(&dlg, title,
                tr("Impossible d'extraire %1 du commit %2 — le fichier était-il "
                   "versionné à ce moment ?").arg(relPath, sha.left(8)));
            return;
        }
        slot.fromDoc = false;
        slot.bytes   = bytes;
        slot.label   = tr("commit %1").arg(sha.left(8));
        chosen = true;
        dlg.accept();
    });
    connect(fileBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString p = QFileDialog::getOpenFileName(
            &dlg, tr("ROM à comparer"), {},
            tr("ROM (*.bin *.ori *.mod *.hex);;Tous (*.*)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dlg, title, tr("Impossible d'ouvrir %1").arg(p));
            return;
        }
        slot.fromDoc = false;
        slot.bytes   = f.readAll();
        slot.label   = QFileInfo(p).fileName();
        chosen = true;
        dlg.accept();
    });

    dlg.exec();
    return chosen;
}

void ComparePanel::openComparison() {
    if (pickSource(tr("ROM B à comparer"), m_b))
        refresh();
}

void ComparePanel::refresh() {
    m_romALabel->setText(slotText(QStringLiteral("A"), m_a));
    m_romBLabel->setText(slotText(QStringLiteral("B"), m_b));

    m_table->setRowCount(0);

    const QByteArray romA = slotBytes(m_a);
    const QByteArray romB = slotBytes(m_b);

    if (romA.isEmpty() || romB.isEmpty()) {
        m_summary->setText(tr("Sélectionnez deux ROMs (A et B) à comparer — "
                              "document, commit git ou fichier .bin."));
        return;
    }

    const std::vector<ecu::DiffInterval> intervals =
        ecu::diffIntervals(constByteSpan(romA), constByteSpan(romB));

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
    if (romA.size() != romB.size()) {
        sizeNote = tr("  ⚠ Tailles différentes (A=%1, B=%2) — comparaison "
                      "sur les %3 octets communs ; le surplus est marqué comme modifié.")
                       .arg(romA.size())
                       .arg(romB.size())
                       .arg(std::min<qsizetype>(romA.size(), romB.size()));
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
