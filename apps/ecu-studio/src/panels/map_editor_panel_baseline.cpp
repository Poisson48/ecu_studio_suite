// map_editor_panel_baseline.cpp — sélecteur de baseline du mode fantôme :
// snapshot d'origine, commit git (via git_blob_utils) ou fichier .bin externe.

#include "map_editor_panel.h"
#include "git_blob_utils.h"
#include "../rom_document.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace ecu_studio {

void MapEditorPanel::pickBaseline() {
    if (!m_doc || !m_doc->isLoaded()) {
        setStatus(tr("Chargez une ROM avant de choisir une baseline."), true);
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Baseline du mode fantôme"));
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(tr("La baseline est la ROM \"de référence\" comparée "
                                 "à l'édition courante en mode fantôme.")));

    // Option 1 : snapshot d'origine.
    auto* originBtn = new QPushButton(tr("Snapshot d'origine (à l'ouverture)"), &dlg);
    lay->addWidget(originBtn);

    // Option 2 : commit git.
    const QString romPath  = m_doc->path();
    const QString repoRoot = gitToplevelFor(romPath);
    auto* gitGroup = new QGroupBox(tr("Depuis un commit git"), &dlg);
    auto* gitLay   = new QVBoxLayout(gitGroup);
    auto* commitCombo = new QComboBox(gitGroup);
    auto* gitApplyBtn = new QPushButton(tr("Charger ce commit"), gitGroup);
    gitLay->addWidget(commitCombo);
    gitLay->addWidget(gitApplyBtn);
    QString relPath;
    if (repoRoot.isEmpty()) {
        commitCombo->setEnabled(false);
        gitApplyBtn->setEnabled(false);
        gitLay->addWidget(new QLabel(tr("(la ROM n'est pas dans un dépôt git — option indisponible)"),
                                     gitGroup));
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

    auto* info = new QLabel(tr("Baseline actuelle : %1").arg(m_doc->baselineLabel()), &dlg);
    info->setStyleSheet("color:#7c8fa6;");
    lay->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    connect(originBtn, &QPushButton::clicked, this, [this, &dlg]() {
        // On retourne au "snapshot à l'ouverture" : on relit la ROM depuis le
        // disque pour reproduire l'état du load. Si non possible, on garde le
        // contenu actuel comme nouvelle baseline (resetBaseline).
        const QString p = m_doc->path();
        QFile f(p);
        if (!p.isEmpty() && f.open(QIODevice::ReadOnly)) {
            m_doc->setBaselineFromBytes(f.readAll(), tr("fichier d'origine"));
        } else {
            m_doc->resetBaseline();
        }
        setStatus(tr("Baseline : snapshot d'origine."));
        dlg.accept();
    });
    connect(gitApplyBtn, &QPushButton::clicked, this, [&]() {
        if (repoRoot.isEmpty() || commitCombo->currentIndex() < 0) return;
        const QString sha = commitCombo->currentData().toString();
        const QByteArray bytes = gitBlobAt(repoRoot, sha, relPath);
        if (bytes.isEmpty()) {
            QMessageBox::warning(&dlg, tr("Baseline git"),
                tr("Impossible d'extraire %1 du commit %2 — vérifie que le "
                   "fichier était versionné à ce moment.").arg(relPath, sha.left(8)));
            return;
        }
        m_doc->setBaselineFromBytes(
            bytes, tr("commit %1").arg(sha.left(8)));
        setStatus(tr("Baseline : commit %1 (%2 octets).").arg(sha.left(8)).arg(bytes.size()));
        dlg.accept();
    });
    connect(fileBtn, &QPushButton::clicked, this, [&]() {
        const QString p = QFileDialog::getOpenFileName(
            &dlg, tr("Choisir une ROM de référence"), {},
            tr("ROM (*.bin *.hex);;Tous (*.*)"),
            nullptr, QFileDialog::DontUseNativeDialog);
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dlg, tr("Baseline"),
                tr("Impossible d'ouvrir %1").arg(p));
            return;
        }
        const QByteArray bytes = f.readAll();
        m_doc->setBaselineFromBytes(bytes, QFileInfo(p).fileName());
        setStatus(tr("Baseline : fichier %1 (%2 octets).")
                      .arg(QFileInfo(p).fileName()).arg(bytes.size()));
        dlg.accept();
    });

    dlg.exec();
}

} // namespace ecu_studio
