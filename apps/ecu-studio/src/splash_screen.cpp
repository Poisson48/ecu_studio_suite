#include "splash_screen.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
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
    setFixedSize(520, 280);
    setStyleSheet("QWidget { background: #0f1623; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 24);
    root->setSpacing(0);

    // Bandeau : logo à gauche + accroche/version à droite. Le logo contient
    // déjà le wordmark "ECU STUDIO" donc on ne le redouble pas en texte.
    auto* head = new QHBoxLayout;
    head->setSpacing(20);

    auto* logoLbl = new QLabel(this);
    QPixmap pm(":/ecu_studio_logo.png");
    if (!pm.isNull())
        logoLbl->setPixmap(pm.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    head->addWidget(logoLbl, 0, Qt::AlignTop);

    auto* textCol = new QVBoxLayout;
    textCol->setSpacing(4);
    auto* tagLbl = new QLabel(tr("Reprogrammation ECU"), this);
    {
        QFont f = tagLbl->font();
        f.setPointSize(f.pointSize() + 6);
        f.setBold(true);
        tagLbl->setFont(f);
        tagLbl->setStyleSheet("color: #e5e7eb;");
    }
    textCol->addWidget(tagLbl);

    auto* versionLbl = new QLabel(QString("v%1").arg(APP_VERSION), this);
    versionLbl->setStyleSheet("color: #6366f1; font-size: 12px;");
    textCol->addWidget(versionLbl);
    textCol->addStretch();
    head->addLayout(textCol, 1);

    root->addLayout(head);

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
