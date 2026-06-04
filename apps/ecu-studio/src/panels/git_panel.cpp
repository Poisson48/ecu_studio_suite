#include "git_panel.h"
#include "../rom_document.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QDir>
#include <QFileInfo>
#include <QFont>

namespace ecu_studio {

GitPanel::GitPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();
    updateActionStates();

    // L'état des flèches annuler/rétablir dépend aussi des éditions en cours.
    if (m_doc)
        connect(m_doc, &RomDocument::modifiedStateChanged, this,
                [this](bool) { emitNavState(); });
    emitNavState();
}

GitPanel::~GitPanel() = default;

void GitPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Projet ────────────────────────────────────────────────────────────
    auto* repoBox = new QGroupBox(tr("Versions"), this);
    auto* repoLay = new QVBoxLayout(repoBox);

    m_pathLabel = new QLabel(tr("Aucun projet ouvert"), this);
    m_pathLabel->setStyleSheet("color:#7c8fa6;");
    m_pathLabel->setWordWrap(true);
    repoLay->addWidget(m_pathLabel);

    // Sélecteur de variantes (branches) + création de nouvelle variante.
    auto* variantRow = new QHBoxLayout;
    variantRow->addWidget(new QLabel(tr("Variante :"), this));
    m_variantCombo = new QComboBox(this);
    m_variantCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    variantRow->addWidget(m_variantCombo, 1);
    m_newVariantBtn = new QPushButton(tr("Nouvelle variante…"), this);
    variantRow->addWidget(m_newVariantBtn);
    repoLay->addLayout(variantRow);

    auto* btnRow = new QHBoxLayout;
    m_commitBtn  = new QPushButton(tr("Enregistrer une version"), this);
    m_commitBtn->setObjectName("accentBtn");
    m_restoreBtn = new QPushButton(tr("Revenir à cette version"), this);
    m_renameBtn  = new QPushButton(tr("Renommer le message…"), this);
    m_renameBtn->setToolTip(tr("Modifier le message de la version sélectionnée "
                               "(double-clic sur la ligne) — y compris les "
                               "enregistrements automatiques « WIP on … »."));
    m_refreshBtn = new QPushButton(tr("Rafraîchir"), this);
    btnRow->addWidget(m_commitBtn);
    btnRow->addWidget(m_restoreBtn);
    btnRow->addWidget(m_renameBtn);
    btnRow->addWidget(m_refreshBtn);
    btnRow->addStretch();
    repoLay->addLayout(btnRow);
    root->addWidget(repoBox);

    // ── Versions enregistrées ──────────────────────────────────────────────
    auto* histBox = new QGroupBox(tr("Versions enregistrées"), this);
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
    connect(m_renameBtn,  &QPushButton::clicked, this, &GitPanel::renameSelected);
    connect(m_refreshBtn, &QPushButton::clicked, this, &GitPanel::refresh);
    connect(m_newVariantBtn, &QPushButton::clicked, this, &GitPanel::createVariant);
    connect(m_variantCombo,
            qOverload<int>(&QComboBox::activated),
            this, &GitPanel::switchVariant);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &GitPanel::updateActionStates);
    // Double-clic sur une ligne = éditer son message (édition « plus tard »).
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, [this](int, int) { renameSelected(); });

#ifndef ECU_GIT_AVAILABLE
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
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
    if (m_renameBtn) m_renameBtn->setEnabled(hasSelection);
    if (m_variantCombo)  m_variantCombo->setEnabled(hasRepo);
    if (m_newVariantBtn) m_newVariantBtn->setEnabled(hasRepo);
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
            setStatus(tr("Erreur projet : %1")
                          .arg(QString::fromStdString(r.error())), true);
        } else {
            setStatus(tr("Projet prêt"));
        }
    }
#else
    if (!dir.isEmpty())
        setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif

    refreshVariants();
    refresh();
    rebuildNav();
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
        setStatus(tr("Aucune version enregistrée"));
    else
        setStatus(tr("%1 version(s)").arg(commits.size()));
#endif

    updateActionStates();
}

void GitPanel::commitCurrent() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        setStatus(tr("Aucun projet ouvert"), true);
        return;
    }

    // Flush la ROM en mémoire sur disque AVANT d'enregistrer la version.
    if (m_doc && m_doc->isLoaded() && m_doc->isModified() && !m_doc->path().isEmpty()) {
        if (!m_doc->saveToFile(m_doc->path())) {
            QMessageBox::warning(this, tr("Enregistrer une version"),
                tr("Impossible de sauvegarder la ROM avant l'enregistrement.\n"
                   "Vérifiez que le fichier est accessible en écriture."));
            return;
        }
    }

    // Message suggéré : basé sur le nom du fichier ROM.
    const QString suggested = m_doc && !m_doc->name().isEmpty()
        ? tr("chg: %1").arg(m_doc->name())
        : tr("chg: rom");

    bool ok = false;
    const QString message = QInputDialog::getText(
        this, tr("Enregistrer une version"), tr("Description de la version :"),
        QLineEdit::Normal, suggested, &ok);
    if (!ok) return;
    if (message.trimmed().isEmpty()) {
        setStatus(tr("Description vide : enregistrement annulé"), true);
        return;
    }

    ecu::CommitResult result = m_git->commit(message.toStdString());
    if (result.nothing) {
        setStatus(tr("Aucune modification à enregistrer"));
        return;
    }
    if (!result.hash) {
        setStatus(tr("Échec de l'enregistrement"), true);
        QMessageBox::warning(this, tr("Enregistrer une version"),
                             tr("L'enregistrement de la version a échoué."));
        return;
    }

    setStatus(tr("Version %1 enregistrée")
                  .arg(QString::fromStdString(*result.hash).left(8)));
    refresh();
    rebuildNav();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
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
        setStatus(tr("Aucune version sélectionnée"), true);
        return;
    }

    if (QMessageBox::question(
            this, tr("Revenir à cette version"),
            tr("Revenir à la version %1 ?\n"
               "L'état actuel sera remplacé (une version de restauration est créée).")
                .arg(hash.left(8)),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    auto r = m_git->restore(hash.toStdString());
    if (!r) {
        setStatus(tr("Erreur lors du retour à la version : %1")
                      .arg(QString::fromStdString(r.error())), true);
        QMessageBox::warning(this, tr("Revenir à cette version"),
            tr("Le retour à la version a échoué :\n%1")
                .arg(QString::fromStdString(r.error())));
        return;
    }

    // restore() a réécrit rom.bin dans le dossier de travail : on le recharge.
    // En repli, on relit le blob directement depuis la version sélectionnée.
    bool reloaded = reloadWorkingRom();
    if (!reloaded && m_doc) {
        auto buf = m_git->readFileAtCommit(hash.toStdString(), "rom.bin");
        if (buf) {
            QByteArray data(reinterpret_cast<const char*>(buf->data()),
                            static_cast<qsizetype>(buf->size()));
            reloaded = m_doc->loadFromData(data, QStringLiteral("rom.bin"));
        }
    }

    setStatus(reloaded
                  ? tr("Revenu à la version %1").arg(hash.left(8))
                  : tr("Version restaurée, mais rechargement de la ROM impossible"),
              !reloaded);
    refresh();
    rebuildNav();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

void GitPanel::renameSelected() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        setStatus(tr("Aucun projet ouvert"), true);
        return;
    }

    const QString hash = selectedHash();
    if (hash.isEmpty()) {
        setStatus(tr("Aucune version sélectionnée"), true);
        return;
    }

    // Message actuel (1re ligne) proposé comme valeur par défaut.
    QString current;
    if (const int row = m_table->currentRow(); row >= 0 && m_table->item(row, 2))
        current = m_table->item(row, 2)->text();

    bool ok = false;
    const QString newMsg = QInputDialog::getText(
        this, tr("Renommer le message"),
        tr("Nouveau message de la version %1 :").arg(hash.left(8)),
        QLineEdit::Normal, current, &ok);
    if (!ok) return;
    if (newMsg.trimmed().isEmpty()) {
        setStatus(tr("Message vide : renommage annulé"), true);
        return;
    }
    if (newMsg == current) return;  // inchangé

    auto r = m_git->rewordCommit(hash.toStdString(), newMsg.toStdString());
    if (!r) {
        setStatus(tr("Échec du renommage : %1")
                      .arg(QString::fromStdString(r.error())), true);
        QMessageBox::warning(this, tr("Renommer le message"),
            tr("Le renommage du message a échoué :\n%1")
                .arg(QString::fromStdString(r.error())));
        return;
    }

    // L'arbre est inchangé (même rom.bin) : pas besoin de recharger la ROM.
    setStatus(tr("Message de la version mis à jour."));
    refresh();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

// ── Variantes (branches) ───────────────────────────────────────────────────

bool GitPanel::reloadWorkingRom() {
    if (!m_doc) return false;
    const QString romPath = QDir(m_repoPath).filePath("rom.bin");
    if (!QFileInfo::exists(romPath)) return false;
    return m_doc->loadFromFile(romPath);
}

void GitPanel::refreshVariants() {
    if (!m_variantCombo) return;

    QSignalBlocker block(m_variantCombo); // évite de déclencher switchVariant()
    m_variantCombo->clear();

#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        updateActionStates();
        return;
    }

    auto branches = m_git->listBranches();
    if (!branches) {
        updateActionStates();
        return;
    }

    int currentIndex = -1;
    for (const auto& name : branches->all) {
        const QString qname = QString::fromStdString(name);
        m_variantCombo->addItem(qname);
        if (name == branches->current)
            currentIndex = m_variantCombo->count() - 1;
    }
    if (currentIndex >= 0)
        m_variantCombo->setCurrentIndex(currentIndex);
#endif

    updateActionStates();
}

void GitPanel::switchVariant(int index) {
#ifdef ECU_GIT_AVAILABLE
    if (index < 0 || !m_variantCombo || m_repoPath.isEmpty() || !m_git)
        return;

    const QString name = m_variantCombo->itemText(index);
    if (name.isEmpty()) return;

    // Flush la ROM en mémoire sur disque AVANT de basculer : switchBranch()
    // auto-committe le travail en cours (WIP) de la variante quittée, donc on
    // veut que les modifications non sauvegardées y soient capturées.
    if (m_doc && m_doc->isLoaded() && m_doc->isModified() && !m_doc->path().isEmpty())
        m_doc->saveToFile(m_doc->path());

    auto r = m_git->switchBranch(name.toStdString());
    if (!r) {
        setStatus(tr("Erreur de bascule de variante : %1")
                      .arg(QString::fromStdString(r.error())), true);
        QMessageBox::warning(this, tr("Variante"),
            tr("Le changement de variante a échoué :\n%1")
                .arg(QString::fromStdString(r.error())));
        refreshVariants(); // resélectionne la variante réellement active
        return;
    }

    const bool reloaded = reloadWorkingRom();
    setStatus(reloaded
                  ? tr("Variante « %1 » active").arg(name)
                  : tr("Variante « %1 » active (ROM non rechargée)").arg(name),
              !reloaded);
    refresh();
    rebuildNav();
#else
    Q_UNUSED(index);
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

void GitPanel::createVariant() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) {
        setStatus(tr("Aucun projet ouvert"), true);
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Nouvelle variante"), tr("Nom de la variante :"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok) return;
    if (name.trimmed().isEmpty()) {
        setStatus(tr("Nom vide : variante non créée"), true);
        return;
    }

    auto r = m_git->createBranch(name.trimmed().toStdString());
    if (!r) {
        setStatus(tr("Erreur de création de variante : %1")
                      .arg(QString::fromStdString(r.error())), true);
        QMessageBox::warning(this, tr("Nouvelle variante"),
            tr("La création de la variante a échoué :\n%1")
                .arg(QString::fromStdString(r.error())));
        return;
    }

    // createBranch() bascule déjà sur la nouvelle variante : on recharge l'état.
    reloadWorkingRom();
    setStatus(tr("Variante « %1 » créée").arg(QString::fromStdString(*r)));
    refreshVariants();
    refresh();
    rebuildNav();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

// ── Annuler / Rétablir basés sur l'historique git ───────────────────────────

void GitPanel::rebuildNav() {
#ifdef ECU_GIT_AVAILABLE
    m_navChain.clear();
    m_navPos = 0;
    if (m_git) m_navChain = m_git->historyChain();
#endif
    emitNavState();
}

void GitPanel::emitNavState() {
    const bool repo = !m_repoPath.isEmpty();
    const bool dirty = m_doc && m_doc->isModified();
    // On peut annuler s'il existe une version plus ancienne, ou si des éditions
    // en cours peuvent être capturées en version.
    const bool canUndo = repo &&
        (m_navPos + 1 < static_cast<int>(m_navChain.size()) || dirty);
    const bool canRedo = repo && m_navPos > 0;
    emit navStateChanged(canUndo, canRedo);
}

bool GitPanel::loadVersionIntoDoc(const QString& hash) {
#ifdef ECU_GIT_AVAILABLE
    if (!m_doc || !m_git) return false;
    auto buf = m_git->readFileAtCommit(hash.toStdString(), "rom.bin");
    if (!buf || buf->empty()) {
        setStatus(tr("Impossible de lire la ROM de la version %1").arg(hash.left(8)), true);
        return false;
    }
    QByteArray data(reinterpret_cast<const char*>(buf->data()),
                    static_cast<qsizetype>(buf->size()));
    // Écrit le fichier de travail (cohérence disque) puis recharge via
    // loadFromFile pour conserver le chemin du document (loadFromData l'effacerait).
    const QString romPath = QDir(m_repoPath).filePath("rom.bin");
    QFile f(romPath);
    if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); }
    return m_doc->loadFromFile(romPath);
#else
    Q_UNUSED(hash);
    return false;
#endif
}

void GitPanel::undo() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) return;

    // Capture d'abord les éditions non enregistrées comme une version, afin de
    // ne rien perdre et de pouvoir les rétablir (git = pile d'annulation).
    if (m_doc && m_doc->isLoaded() && m_doc->isModified() && !m_doc->path().isEmpty()) {
        if (m_doc->saveToFile(m_doc->path())) {
            m_git->commit(tr("Édition (annulable)").toStdString());
            rebuildNav();                 // nouveau tip → m_navPos = 0
        }
    }

    if (m_navPos + 1 >= static_cast<int>(m_navChain.size())) {
        setStatus(tr("Rien à annuler — plus ancienne version atteinte."));
        emitNavState();
        return;
    }

    ++m_navPos;
    const QString hash = QString::fromStdString(m_navChain[static_cast<std::size_t>(m_navPos)]);
    if (loadVersionIntoDoc(hash))
        setStatus(tr("Annulé → version %1  (%2 sur %3)")
                      .arg(hash.left(8))
                      .arg(static_cast<int>(m_navChain.size()) - m_navPos)
                      .arg(m_navChain.size()));
    emitNavState();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

void GitPanel::redo() {
#ifdef ECU_GIT_AVAILABLE
    if (m_repoPath.isEmpty() || !m_git) return;

    if (m_navPos <= 0) {
        setStatus(tr("Rien à rétablir — version la plus récente."));
        emitNavState();
        return;
    }

    --m_navPos;
    const QString hash = QString::fromStdString(m_navChain[static_cast<std::size_t>(m_navPos)]);
    if (loadVersionIntoDoc(hash))
        setStatus(tr("Rétabli → version %1  (%2 sur %3)")
                      .arg(hash.left(8))
                      .arg(static_cast<int>(m_navChain.size()) - m_navPos)
                      .arg(m_navChain.size()));
    emitNavState();
#else
    setStatus(tr("Versions indisponibles (libgit2 non compilé)"), true);
#endif
}

} // namespace ecu_studio
