#include "update_dialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QUrl>
#include <QVBoxLayout>

namespace ecu_studio {

UpdateDialog::UpdateDialog(Updater* updater, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
      m_updater(updater) {
    setWindowTitle(tr("Mise à jour"));
    setMinimumWidth(400);
    setModal(true);

    m_status = new QLabel(tr("Recherche de mises à jour…"), this);
    m_status->setWordWrap(true);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 0);  // indéterminé
    m_progress->setVisible(false);

    m_actionBtn = new QPushButton(tr("Télécharger"), this);
    m_actionBtn->setVisible(false);
    m_closeBtn = new QPushButton(tr("Fermer"), this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_actionBtn);
    btnRow->addWidget(m_closeBtn);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);
    root->addWidget(m_status);
    root->addWidget(m_progress);
    root->addLayout(btnRow);

    connect(m_closeBtn,  &QPushButton::clicked, this, &QDialog::accept);
    connect(m_actionBtn, &QPushButton::clicked, this, &UpdateDialog::onActionClicked);

    connect(m_updater, &Updater::updateAvailable,  this, &UpdateDialog::onUpdateAvailable);
    connect(m_updater, &Updater::upToDate,         this, &UpdateDialog::onUpToDate);
    connect(m_updater, &Updater::checkError,       this, &UpdateDialog::onCheckError);
    connect(m_updater, &Updater::downloadProgress, this, &UpdateDialog::onDownloadProgress);
    connect(m_updater, &Updater::downloadError,    this, &UpdateDialog::onDownloadError);
    connect(m_updater, &Updater::installReady,     this, &UpdateDialog::onInstallReady);
}

void UpdateDialog::startCheck() {
    setPhase(Phase::Checking);
    m_updater->checkForUpdates();
}

// ── gestionnaires des signaux de l'Updater ───────────────────────────────────

void UpdateDialog::onUpdateAvailable(const QString& version) {
    m_status->setText(tr("La version <b>%1</b> est disponible.").arg(version));
    m_isAppImage = m_updater->isAppImage();
    setPhase(Phase::Available);
}

void UpdateDialog::onUpToDate() {
    m_status->setText(tr("ECU Studio est à jour."));
    setPhase(Phase::Done);
}

void UpdateDialog::onCheckError(const QString& msg) {
    m_status->setText(tr("Échec de la vérification : %1").arg(msg));
    setPhase(Phase::Error);
}

void UpdateDialog::onDownloadProgress(qint64 done, qint64 total) {
    if (total > 0) {
        m_progress->setRange(0, static_cast<int>(total / 1024));
        m_progress->setValue(static_cast<int>(done / 1024));
    }
}

void UpdateDialog::onDownloadError(const QString& msg) {
    m_status->setText(tr("Échec du téléchargement : %1").arg(msg));
    setPhase(Phase::Error);
}

void UpdateDialog::onInstallReady() {
    m_status->setText(
        tr("Mise à jour installée. Redémarrez ECU Studio pour appliquer la nouvelle version."));
    setPhase(Phase::Done);
}

void UpdateDialog::onActionClicked() {
    if (m_phase == Phase::Available) {
        if (m_isAppImage) {
            setPhase(Phase::Downloading);
            m_updater->startDownload();
        } else {
            QDesktopServices::openUrl(
                QUrl("https://github.com/Poisson48/ecu_studio_suite/releases/latest"));
        }
    }
}

// ── machine à états de l'interface ───────────────────────────────────────────

void UpdateDialog::setPhase(Phase phase) {
    m_phase = phase;
    switch (phase) {
        case Phase::Checking:
            m_progress->setRange(0, 0);
            m_progress->setVisible(true);
            m_actionBtn->setVisible(false);
            break;

        case Phase::Available:
            m_progress->setVisible(false);
            m_actionBtn->setText(m_isAppImage ? tr("Télécharger && installer")
                                              : tr("Ouvrir la page de téléchargement"));
            m_actionBtn->setVisible(true);
            break;

        case Phase::Downloading:
            m_status->setText(tr("Téléchargement de la mise à jour…"));
            m_progress->setRange(0, 0);
            m_progress->setVisible(true);
            m_actionBtn->setEnabled(false);
            break;

        case Phase::Done:
            m_progress->setVisible(false);
            m_actionBtn->setVisible(false);
            break;

        case Phase::Error:
            m_progress->setVisible(false);
            m_actionBtn->setVisible(false);
            break;
    }
}

}  // namespace ecu_studio
