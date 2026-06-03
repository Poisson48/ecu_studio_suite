#include "hub/hub_launcher_panel.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "../rom_document.h"
#include "hub/maturity_badge.h"
#include "ecu_studio/palette.hpp"

// VerifyOnBusDialog est l'un des trois autres modules du hub. Au moment où ce
// fichier est écrit, son en-tête peut ne pas encore exister (il sera fourni par
// la passe d'intégration). On l'inclut donc conditionnellement : présent, on
// l'utilise réellement ; absent, le bouton « Vérifier sur le bus » dégrade
// proprement en affichant un statut. Le code d'appel reste identique des deux
// côtés, si bien qu'aucune retouche n'est nécessaire une fois le module intégré.
#if defined(__has_include)
#  if __has_include("hub/verify_on_bus_dialog.h")
#    include "hub/verify_on_bus_dialog.h"
#    define ECU_HAS_VERIFY_ON_BUS_DIALOG 1
#  endif
#endif

namespace ecu_studio {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
HubLauncherPanel::HubLauncherPanel(QWidget* parent)
    : HubLauncherPanel(nullptr, parent) {}

HubLauncherPanel::HubLauncherPanel(RomDocument* doc, QWidget* parent)
    : QWidget(parent), m_doc(doc) {
    buildUi();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI
// ─────────────────────────────────────────────────────────────────────────────
void HubLauncherPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── En-tête ───────────────────────────────────────────────────────────────
    auto* title = new QLabel(tr("Lanceur"), this);
    {
        QFont f = title->font();
        f.setPointSize(f.pointSize() + 6);
        f.setBold(true);
        title->setFont(f);
    }
    root->addWidget(title);

    auto* subtitle = new QLabel(
        tr("Outils spécialisés de la suite ECU Studio. Lancez un sous-programme "
           "ou vérifiez sa disponibilité sur le bus."),
        this);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(QString::fromLatin1(Palette::kDeadGray)));
    root->addWidget(subtitle);

    // ── Grille de tuiles (dans une zone défilable) ───────────────────────────
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* gridHost = new QWidget(scroll);
    m_grid = new QGridLayout(gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(12);
    m_grid->setVerticalSpacing(12);

    constexpr int kColumns = 3;
    int idx = 0;
    for (const SubProgram& sp : SubProgramRegistry::all()) {
        QWidget* tile = buildTile(sp);
        const int row = idx / kColumns;
        const int col = idx % kColumns;
        m_grid->addWidget(tile, row, col);
        ++idx;
    }
    // Pousse les tuiles en haut à gauche : colonnes et ligne finale extensibles.
    for (int c = 0; c < kColumns; ++c)
        m_grid->setColumnStretch(c, 1);
    m_grid->setRowStretch(m_grid->rowCount(), 1);

    scroll->setWidget(gridHost);
    root->addWidget(scroll, 1);

    // ── Statut ────────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);
}

QWidget* HubLauncherPanel::buildTile(const SubProgram& sp) {
    auto* tile = new QFrame(this);
    tile->setObjectName(QStringLiteral("hubTile"));
    tile->setFrameShape(QFrame::StyledPanel);
    tile->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    tile->setStyleSheet(QStringLiteral(
        "QFrame#hubTile {"
        "  background-color: #1f2937;"
        "  border: 1px solid #2a3a52;"
        "  border-radius: 10px;"
        "}"));

    auto* col = new QVBoxLayout(tile);
    col->setContentsMargins(14, 14, 14, 14);
    col->setSpacing(8);

    // ── Ligne icône + nom + badge ────────────────────────────────────────────
    auto* head = new QHBoxLayout();
    head->setSpacing(8);

    auto* iconLabel = new QLabel(sp.icon, tile);
    {
        QFont f = iconLabel->font();
        f.setPointSize(f.pointSize() + 8);
        iconLabel->setFont(f);
    }
    head->addWidget(iconLabel);

    auto* nameLabel = new QLabel(sp.name, tile);
    {
        QFont f = nameLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        nameLabel->setFont(f);
    }
    head->addWidget(nameLabel);
    head->addStretch(1);

    auto* badge = new MaturityBadge(sp.maturity, tile);
    head->addWidget(badge, 0, Qt::AlignTop);

    col->addLayout(head);

    // ── Description ───────────────────────────────────────────────────────────
    auto* desc = new QLabel(sp.description, tile);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #9ca3af;"));
    desc->setMinimumHeight(34);
    col->addWidget(desc, 1);

    // ── Boutons d'action ──────────────────────────────────────────────────────
    auto* actions = new QHBoxLayout();
    actions->setSpacing(8);

    auto* launchBtn = new QPushButton(tr("Lancer"), tile);
    launchBtn->setObjectName(QStringLiteral("accentBtn"));
    connect(launchBtn, &QPushButton::clicked, this,
            [this, sp]() { launch(sp); });

    // Résolution du binaire : si introuvable, on désactive le bouton et on
    // affiche un indice de build (pattern « binary not found » de CanPanel).
    const QString exec = SubProgramRegistry::resolveExec(sp.execName);
    if (sp.external && exec.isEmpty()) {
        launchBtn->setEnabled(false);
        launchBtn->setToolTip(
            tr("Binaire introuvable — buildez avec -DECU_BUILD_SOCKETSPY=ON, "
               "ou placez l'exécutable « %1 » à côté d'ecu_studio.")
                .arg(sp.execName));
    } else if (!exec.isEmpty()) {
        launchBtn->setToolTip(exec);
    }
    actions->addWidget(launchBtn);

    // Affordance « Vérifier sur le bus » pour les sous-programmes concernés.
    if (supportsBusVerify(sp)) {
        auto* verifyBtn = new QPushButton(tr("Vérifier sur le bus"), tile);
        verifyBtn->setToolTip(
            tr("Sonde le bus CAN pour confirmer la présence de l'ECU avant de "
               "lancer ce sous-programme."));
        connect(verifyBtn, &QPushButton::clicked, this,
                [this, sp]() { verifyOnBus(sp); });
        actions->addWidget(verifyBtn);
    }

    actions->addStretch(1);
    col->addLayout(actions);

    return tile;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lancement d'un sous-programme (pattern QProcess de CanPanel)
// ─────────────────────────────────────────────────────────────────────────────
void HubLauncherPanel::launch(const SubProgram& sp) {
    const QString path = SubProgramRegistry::resolveExec(sp.execName);
    if (path.isEmpty()) {
        setStatus(tr("Le binaire « %1 » est introuvable. Buildez la suite avec "
                     "-DECU_BUILD_SOCKETSPY=ON, ou placez l'exécutable à côté "
                     "d'ecu_studio.")
                      .arg(sp.execName));
        return;
    }

    // Garde : un seul processus par sous-programme. S'il tourne déjà, on ne
    // relance pas (state() != NotRunning).
    QProcess*& proc = m_procs[sp.id];
    if (proc && proc->state() != QProcess::NotRunning) {
        setStatus(tr("%1 est déjà lancé.").arg(sp.name));
        return;
    }

    if (!proc) {
        proc = new QProcess(this);
        connect(proc, &QProcess::errorOccurred, this,
                [this, id = sp.id, name = sp.name](QProcess::ProcessError) {
                    QProcess* p = m_procs.value(id, nullptr);
                    setStatus(tr("Échec du lancement de %1 : %2")
                                  .arg(name,
                                       p ? p->errorString()
                                         : tr("erreur inconnue")));
                });
    }

    proc->setProgram(path);
    proc->setArguments(sp.args);
    proc->start();
    setStatus(tr("%1 lancé (%2).").arg(sp.name, path));
}

// ─────────────────────────────────────────────────────────────────────────────
// Vérification sur le bus
// ─────────────────────────────────────────────────────────────────────────────
void HubLauncherPanel::verifyOnBus(const SubProgram& sp) {
#ifdef ECU_HAS_VERIFY_ON_BUS_DIALOG
    // VerifyOnBusDialog(RomDocument*, QWidget*) — on lui passe le document ROM
    // partagé (utilisé pour pré-remplir signal/unité depuis le recipe OpenDAMOS).
    // `sp` n'est consommé que par la branche de repli ci-dessous.
    Q_UNUSED(sp);
    VerifyOnBusDialog dlg(m_doc, this);
    dlg.exec();
#else
    // Le module VerifyOnBusDialog n'est pas encore intégré : dégradation
    // gracieuse. Une fois l'en-tête présent, la branche réelle ci-dessus est
    // compilée automatiquement (cf. __has_include en tête de fichier).
    setStatus(tr("Vérification sur le bus indisponible pour %1 "
                 "(module non intégré).")
                  .arg(sp.name));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
bool HubLauncherPanel::supportsBusVerify(const SubProgram& sp) {
    // Les sous-programmes « bus » (analyse CAN / diagnostic) exposent la
    // vérification. On les reconnaît par identifiant stable du registre.
    return sp.id == QLatin1String("socketspy") ||
           sp.id == QLatin1String("socketspy-mcp") ||
           sp.id == QLatin1String("obd-scanner");
}

void HubLauncherPanel::setStatus(const QString& msg) {
    if (m_statusLabel)
        m_statusLabel->setText(msg);
}

} // namespace ecu_studio
