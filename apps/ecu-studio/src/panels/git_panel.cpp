#include "git_panel.h"
#include "../rom_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>

namespace ecu_studio {

GitPanel::GitPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();
    updateActionStates();
}

GitPanel::~GitPanel() = default;

void GitPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Dépôt ─────────────────────────────────────────────────────────────
    auto* repoBox = new QGroupBox(tr("Dépôt"), this);
    auto* repoLay = new QVBoxLayout(repoBox);

    m_pathLabel = new QLabel(tr("Aucun projet ouvert"), this);
    m_pathLabel->setStyleSheet("color:#7c8fa6;");
    m_pathLabel->setWordWrap(true);
    repoLay->addWidget(m_pathLabel);

    auto* btnRow = new QHBoxLayout;
    m_commitBtn  = new QPushButton(tr("Commit..."), this);
    m_commitBtn->setObjectName("accentBtn");
    m_restoreBtn = new QPushButton(tr("Restaurer"), this);
    m_refreshBtn = new QPushButton(tr("Rafraîchir"), this);
    btnRow->addWidget(m_commitBtn);
    btnRow->addWidget(m_restoreBtn);
    btnRow->addWidget(m_refreshBtn);
    btnRow->addStretch();
    repoLay->addLayout(btnRow);
    root->addWidget(repoBox);

    // ── Historique ────────────────────────────────────────────────────────
    auto* histBox = new QGroupBox(tr("Historique"), this);
    auto* histLay = new QVBoxLayout(histBox);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Hash"), tr("Date"), tr("Message") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(1, 150);
    m_table->setAlternatingRowColors(true);
    histLay->addWidget(m_table);
    root->addWidget(histBox, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    root->addWidget(m_statusLabel);

    connect(m_commitBtn,  &QPushButton::clicked, this, &GitPanel::commitCurrent);
    connect(m_restoreBtn, &QPushButton::clicked, this, &GitPanel::restoreSelected);
    connect(m_refreshBtn, &QPushButton::clicked, this, &GitPanel::refresh);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &GitPanel::updateActionStates);

#ifndef ECU_GIT_AVAILABLE
    setStatus(tr("Support Git indisponible (libgit2 non compilé)"), true);
#endif
}

void GitPanel::setStatus(const QString& msg, bool error) {
    if (m_statusLabel) {
        m_statusLabel->setStyleSheet(error ? "color:#ef4444; font-size:11px;"
                                           : "color:#7c8fa6; font-size:11px;");
        m_statusLabel->setText(msg);
    }
    emit statusMessage(msg);
}

QString GitPanel::selectedHash() const {
    if (!m_table) return {};
    int row = m_table->currentRow();
    if (row < 0) return {};
    auto* item = m_table->item(row, 0);
    if (!item) return {};
    // On stocke le hash complet en UserRole (la colonne n'affiche que le court).
    QString full = item->data(Qt::UserRole).toString();
    return full.isEmpty() ? item->text() : full;
}

void GitPanel::updateActionStates() {
#ifdef ECU_GIT_AVAILABLE
    const bool hasRepo = !m_repoPath.isEmpty() && m_git != nullptr;
#else
    const bool hasRepo = false;
#endif
    const bool hasSelection = hasRepo && m_table && m_table->currentRow() >= 0;

    m_commitBtn->setEnabled(hasRepo);
    m_refreshBtn->setEnabled(hasRepo);
    m_restoreBtn->setEnabled(hasSelection);
}

void GitPanel::setRepoPath(const QString& dir) {
    m_repoPath = dir;

    if (dir.isEmpty()) {
        m_pathLabel->setText(tr("Aucun projet ouvert"));
        m_pathLabel->setStyleSheet("color:#7c8fa6;");
    } else {
        m_pathLabel->setText(QDir::toNativeSeparators(dir));
        m_pathLabel->setStyleSheet("color:#22c55e;");
    }

#ifdef ECU_GIT_AVAILABLE
    m_git.reset();
    if (!dir.isEmpty()) {
        m_git = std::make_unique<ecu::GitManager>(dir.toStdString());
        // S'assure qu'un dépôt existe (init est idempotent côté ouverture).
        auto r = m_git->init();
        if (!r) {
            setStatus(tr("Erreur dépôt : %1")
                          .arg(QString::fromStdString(r.error())), true);
        } else {
            setStatus(tr("Dépôt prêt"));
        }
    }
#else
    if (!dir.isEmpty())
        setStatus(tr("Support Git indisponible (libgit2 non compilé)"), true);
#endif

    refresh();
    updateActionStates();
}

void GitPanel::refresh() {
    if (!m_table) return;
    m_table->setRowCount(0);

#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        updateActionStates();
        return;
    }

    std::vector<ecu::GitCommit> commits = m_git->log();
    m_table->setRowCount(static_cast<int>(commits.size()));

    int row = 0;
    for (const auto& c : commits) {
        const QString fullHash = QString::fromStdString(c.hash);
        const QString shortHash = fullHash.left(8);

        auto* hashItem = new QTableWidgetItem(shortHash);
        hashItem->setData(Qt::UserRole, fullHash);
        hashItem->setFont(QFont("Monospace"));
        m_table->setItem(row, 0, hashItem);

        m_table->setItem(row, 1,
            new QTableWidgetItem(QString::fromStdString(c.date)));

        // Le message peut être multi-ligne : on n'affiche que la 1re ligne.
        QString msg = QString::fromStdString(c.message);
        int nl = msg.indexOf('\n');
        if (nl >= 0) msg = msg.left(nl);
        m_table->setItem(row, 2, new QTableWidgetItem(msg.trimmed()));
        ++row;
    }

    if (commits.empty())
        setStatus(tr("Aucun commit"));
    else
        setStatus(tr("%1 commit(s)").arg(commits.size()));
#endif

    updateActionStates();
}

void GitPanel::commitCurrent() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        setStatus(tr("Aucun projet ouvert"), true);
        return;
    }

    bool ok = false;
    const QString message = QInputDialog::getText(
        this, tr("Nouveau commit"), tr("Message du commit :"),
        QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    if (message.trimmed().isEmpty()) {
        setStatus(tr("Message vide : commit annulé"), true);
        return;
    }

    ecu::CommitResult result = m_git->commit(message.toStdString());
    if (result.nothing) {
        setStatus(tr("Aucune modification à committer"));
        return;
    }
    if (!result.hash) {
        setStatus(tr("Échec du commit"), true);
        QMessageBox::warning(this, tr("Commit"),
                             tr("Le commit a échoué."));
        return;
    }

    setStatus(tr("Commit %1 créé")
                  .arg(QString::fromStdString(*result.hash).left(8)));
    refresh();
#else
    setStatus(tr("Support Git indisponible (libgit2 non compilé)"), true);
#endif
}

void GitPanel::restoreSelected() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        setStatus(tr("Aucun projet ouvert"), true);
        return;
    }

    const QString hash = selectedHash();
    if (hash.isEmpty()) {
        setStatus(tr("Aucun commit sélectionné"), true);
        return;
    }

    if (QMessageBox::question(
            this, tr("Restaurer"),
            tr("Restaurer la ROM au commit %1 ?\n"
               "L'état actuel sera remplacé (un commit de restauration est créé).")
                .arg(hash.left(8)),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    auto r = m_git->restore(hash.toStdString());
    if (!r) {
        setStatus(tr("Erreur restauration : %1")
                      .arg(QString::fromStdString(r.error())), true);
        QMessageBox::warning(this, tr("Restaurer"),
            tr("La restauration a échoué :\n%1")
                .arg(QString::fromStdString(r.error())));
        return;
    }

    // restore() a réécrit rom.bin dans le dossier du dépôt : on le recharge.
    bool reloaded = false;
    if (m_doc) {
        const QString romPath = QDir(m_repoPath).filePath("rom.bin");
        if (QFileInfo::exists(romPath)) {
            reloaded = m_doc->loadFromFile(romPath);
        } else {
            // Repli : relire le blob directement depuis le commit.
            auto buf = m_git->readFileAtCommit(hash.toStdString(), "rom.bin");
            if (buf) {
                QByteArray data(reinterpret_cast<const char*>(buf->data()),
                                static_cast<qsizetype>(buf->size()));
                reloaded = m_doc->loadFromData(data, QStringLiteral("rom.bin"));
            }
        }
    }

    setStatus(reloaded
                  ? tr("Restauré au commit %1").arg(hash.left(8))
                  : tr("Restauré, mais rechargement de la ROM impossible"),
              !reloaded);
    refresh();
#else
    setStatus(tr("Support Git indisponible (libgit2 non compilé)"), true);
#endif
}

} // namespace ecu_studio
