#include "rom_source_picker.h"
#include "git_blob_utils.h"

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
#include <QObject>
#include <QPushButton>
#include <QVBoxLayout>

namespace ecu_studio {

PickedRom pickRomSource(QWidget* parent, const QString& title,
                        const QString& romPath, const QString& firstOptionLabel) {
    PickedRom result;

    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(560);
    auto* lay = new QVBoxLayout(&dlg);

    lay->addWidget(new QLabel(QObject::tr("Choisissez la source de cette ROM :"), &dlg));

    // Option 1 : spécifique à l'appelant (affichée seulement si un libellé est
    // fourni) — ex. « ROM courante du document » ou « Snapshot d'origine ».
    if (!firstOptionLabel.isEmpty()) {
        auto* firstBtn = new QPushButton(firstOptionLabel, &dlg);
        lay->addWidget(firstBtn);
        QObject::connect(firstBtn, &QPushButton::clicked, &dlg, [&]() {
            result.ok = true;
            result.firstOption = true;
            result.bytes.clear();
            result.label.clear();
            dlg.accept();
        });
    }

    // Option 2 : commit git du dépôt du projet.
    const QString repoRoot = romPath.isEmpty() ? QString() : gitToplevelFor(romPath);
    auto* gitGroup = new QGroupBox(QObject::tr("Depuis un commit git"), &dlg);
    auto* gitLay   = new QVBoxLayout(gitGroup);
    auto* commitCombo = new QComboBox(gitGroup);
    auto* gitApplyBtn = new QPushButton(QObject::tr("Utiliser ce commit"), gitGroup);
    gitLay->addWidget(commitCombo);
    gitLay->addWidget(gitApplyBtn);
    QString relPath;
    if (repoRoot.isEmpty()) {
        commitCombo->setEnabled(false);
        gitApplyBtn->setEnabled(false);
        gitLay->addWidget(new QLabel(
            QObject::tr("(la ROM n'est pas dans un dépôt git — option indisponible)"), gitGroup));
    } else {
        QDir d(repoRoot);
        relPath = d.relativeFilePath(romPath);
        const auto commits = gitRecentCommits(repoRoot, 40);
        if (commits.isEmpty()) {
            commitCombo->setEnabled(false);
            gitApplyBtn->setEnabled(false);
            gitLay->addWidget(new QLabel(
                QObject::tr("(aucun commit listable — repo vide ?)"), gitGroup));
        } else {
            for (const auto& c : commits)
                commitCombo->addItem(QString("%1  %2").arg(c.sha.left(8), c.summary), c.sha);
        }
    }
    lay->addWidget(gitGroup);

    // Option 3 : fichier .bin externe.
    auto* fileBtn = new QPushButton(QObject::tr("Depuis un fichier .bin externe…"), &dlg);
    lay->addWidget(fileBtn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QObject::connect(gitApplyBtn, &QPushButton::clicked, &dlg, [&]() {
        if (repoRoot.isEmpty() || commitCombo->currentIndex() < 0) return;
        const QString sha = commitCombo->currentData().toString();
        const QByteArray bytes = gitBlobAt(repoRoot, sha, relPath);
        if (bytes.isEmpty()) {
            QMessageBox::warning(&dlg, title,
                QObject::tr("Impossible d'extraire %1 du commit %2 — le fichier "
                            "était-il versionné à ce moment ?").arg(relPath, sha.left(8)));
            return;
        }
        result.ok = true;
        result.firstOption = false;
        result.bytes = bytes;
        result.label = QObject::tr("commit %1").arg(sha.left(8));
        dlg.accept();
    });
    QObject::connect(fileBtn, &QPushButton::clicked, &dlg, [&]() {
        const QString p = QFileDialog::getOpenFileName(
            &dlg, QObject::tr("Choisir une ROM"), {},
            QObject::tr("ROM (*.bin *.ori *.mod *.hex);;Tous (*.*)"));
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(&dlg, title, QObject::tr("Impossible d'ouvrir %1").arg(p));
            return;
        }
        result.ok = true;
        result.firstOption = false;
        result.bytes = f.readAll();
        result.label = QFileInfo(p).fileName();
        dlg.accept();
    });

    dlg.exec();
    return result;
}

} // namespace ecu_studio
