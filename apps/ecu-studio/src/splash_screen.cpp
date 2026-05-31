#include "splash_screen.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace ecu_studio {

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    buildUi();

    // Centre sur l'écran principal.
    if (const QScreen* scr = QApplication::primaryScreen()) {
        const QRect sg = scr->geometry();
        move(sg.center() - rect().center());
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(220);
    connect(m_timer, &QTimer::timeout, this, &SplashScreen::onTick);
    m_timer->start();
}

void SplashScreen::buildUi() {
    setFixedSize(480, 260);
    setStyleSheet("QWidget { background: #0f1623; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(40, 32, 40, 28);
    root->setSpacing(0);

    // Nom de l'application.
    auto* nameLbl = new QLabel("ECU Studio", this);
    {
        QFont f = nameLbl->font();
        f.setPointSize(f.pointSize() + 22);
        f.setBold(true);
        nameLbl->setFont(f);
        nameLbl->setStyleSheet("color: #6366f1; letter-spacing: -1px;");
    }
    root->addWidget(nameLbl);

    // Version + accroche.
    auto* versionLbl = new QLabel(
        QString("v%1  \xc2\xb7  %2").arg(APP_VERSION, tr("Reprogrammation ECU")), this);
    {
        QFont f = versionLbl->font();
        f.setPointSize(f.pointSize() + 1);
        versionLbl->setFont(f);
        versionLbl->setStyleSheet("color: #4b5563; margin-top: 4px;");
    }
    root->addWidget(versionLbl);

    root->addStretch(1);

    // Barre de progression.
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, kSteps);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(4);
    m_progress->setStyleSheet(
        "QProgressBar { background: #1e2a3a; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background: #6366f1; border-radius: 2px; }");
    root->addWidget(m_progress);

    root->addSpacing(8);

    // Statut de chargement.
    m_statusLabel = new QLabel(tr("Chargement\xe2\x80\xa6"), this);
    m_statusLabel->setStyleSheet("color: #4b5563; font-size: 11px;");
    root->addWidget(m_statusLabel);

    root->addStretch(1);

    // Pied de page.
    auto* footerLbl = new QLabel(tr("100% local \xc2\xb7 aucune t\xc3\xa9l\xc3\xa9m\xc3\xa9trie"), this);
    footerLbl->setStyleSheet("color: #374151; font-size: 10px;");
    root->addWidget(footerLbl);
}

void SplashScreen::onTick() {
    static const char* const kMessages[] = {
        QT_TR_NOOP("Chargement de l'interface\xe2\x80\xa6"),
        QT_TR_NOOP("Initialisation du moteur ECU\xe2\x80\xa6"),
        QT_TR_NOOP("Chargement des d\xc3\xa9" "finitions ECU\xe2\x80\xa6"),
        QT_TR_NOOP("Pr\xc3\xa9paration des panneaux\xe2\x80\xa6"),
        QT_TR_NOOP("D\xc3\xa9tection des p\xc3\xa9riph\xc3\xa9riques MPPS\xe2\x80\xa6"),
        QT_TR_NOOP("D\xc3\xa9marrage\xe2\x80\xa6"),
    };

    if (m_step < kSteps) {
        m_statusLabel->setText(tr(kMessages[m_step]));
        m_progress->setValue(m_step + 1);
        ++m_step;
    } else {
        m_timer->stop();
    }
}

void SplashScreen::finish(QWidget* mainWindow) {
    if (mainWindow)
        mainWindow->activateWindow();
    close();
    deleteLater();
}

} // namespace ecu_studio
