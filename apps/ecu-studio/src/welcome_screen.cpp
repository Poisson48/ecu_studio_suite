#include "welcome_screen.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ecu_studio {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

QString projectsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/projects";
}

QFrame* makeSep(QWidget* parent) {
    auto* sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #2a3a52;");
    sep->setFixedHeight(1);
    return sep;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
    auto* lbl = new QLabel(text.toUpper(), parent);
    QFont f = lbl->font();
    f.setPointSize(f.pointSize() - 1);
    f.setBold(true);
    lbl->setFont(f);
    lbl->setStyleSheet("color: #4b5563; letter-spacing: 1px;");
    return lbl;
}

} // namespace

WelcomeScreen::WelcomeScreen(QWidget* parent)
    : QDialog(parent, Qt::Dialog)
{
    const QString root = projectsRoot();
    QDir().mkpath(root);
    m_manager = std::make_unique<ecu::ProjectManager>(root);

    setWindowTitle(tr("Bienvenue dans ECU Studio"));
    setFixedSize(820, 560);
    setModal(false);
    buildUi();

    // Centre sur le parent (ou l'écran si aucun parent).
    if (parent) {
        const QRect pr = parent->geometry();
        move(pr.center() - rect().center());
    }
}

bool WelcomeScreen::shouldShow() {
    QSettings s;
    return s.value("welcome/show", true).toBool();
}

void WelcomeScreen::buildUi() {
    setStyleSheet(R"(
        QDialog { background: #111827; }
        QLabel  { color: #e5e7eb; }
    )");

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildLeftPanel());

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setStyleSheet("color: #1f2d42;");
    divider->setFixedWidth(1);
    root->addWidget(divider);

    root->addWidget(buildRightPanel());
}

// ── Panneau gauche : marque + liens + démarrer ───────────────────────────────

QWidget* WelcomeScreen::buildLeftPanel() {
    auto* w = new QWidget(this);
    w->setFixedWidth(320);
    w->setStyleSheet("QWidget { background: #0d1117; }");

    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(28, 28, 28, 20);
    lay->setSpacing(0);

    auto* logoLbl = new QLabel(w);
    QPixmap pm(":/ecu_studio_logo.png");
    if (!pm.isNull()) {
        logoLbl->setPixmap(pm.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        // Repli texte si la ressource manque.
        logoLbl->setText("ECU Studio");
        QFont f = logoLbl->font();
        f.setPointSize(f.pointSize() + 16);
        f.setBold(true);
        logoLbl->setFont(f);
        logoLbl->setStyleSheet("color: #6366f1; background: transparent;");
    }
    logoLbl->setStyleSheet(logoLbl->styleSheet() + "background: transparent;");
    lay->addWidget(logoLbl);

    auto* tagLbl = new QLabel(tr("Reprogrammation ECU \xc2\xb7 MPPS"), w);
    tagLbl->setStyleSheet("color: #6b7280; font-size: 12px; background: transparent;");
    lay->addWidget(tagLbl);

    lay->addSpacing(6);

    auto* verLbl = new QLabel(QString("v%1").arg(APP_VERSION), w);
    verLbl->setStyleSheet("color: #374151; font-size: 11px; background: transparent;");
    lay->addWidget(verLbl);

    lay->addSpacing(24);
    lay->addWidget(makeSep(w));
    lay->addSpacing(16);

    lay->addWidget(makeSectionTitle(tr("Ressources"), w));
    lay->addSpacing(10);

    const struct { const char* icon; const char* label; const char* url; } links[] = {
        { "\xf0\x9f\x93\x96", QT_TR_NOOP("Documentation"),
                "https://github.com/Poisson48/ecu_studio_suite/wiki" },
        { "\xf0\x9f\x92\xbb", QT_TR_NOOP("Code source (GitHub)"),
                "https://github.com/Poisson48/ecu_studio_suite" },
        { "\xf0\x9f\x90\x9b", QT_TR_NOOP("Signaler un bug"),
                "https://github.com/Poisson48/ecu_studio_suite/issues/new" },
    };

    for (const auto& lk : links) {
        auto* lbl = makeLink(QString::fromUtf8(lk.icon), tr(lk.label), lk.url, w);
        lay->addWidget(lbl);
        lay->addSpacing(4);
    }

    lay->addStretch();
    lay->addWidget(makeSep(w));
    lay->addSpacing(12);

    // ── Sélecteur de langue ──────────────────────────────────────────────────
    lay->addWidget(makeSectionTitle(tr("Langue / Language"), w));
    lay->addSpacing(8);

    QSettings langSet;
    const QString curLang = langSet.value("language", "fr").toString();

    const QString btnStyle = R"(
        QPushButton {
            background: #1a2235; color: #9ca3af;
            border: 1px solid #1f2d42; border-radius: 4px;
            padding: 4px 14px; font-size: 11px; font-weight: 600;
        }
        QPushButton:checked {
            background: #3730a3; color: #e0e7ff;
            border-color: #6366f1;
        }
        QPushButton:hover:!checked { border-color: #6366f1; color: #e5e7eb; }
    )";

    auto* langGroup = new QButtonGroup(w);
    auto* langRow   = new QHBoxLayout;
    langRow->setSpacing(6);

    for (const auto& [label, code] : {
             std::pair<const char*, const char*>{"FR  Fran\xc3\xa7" "ais", "fr"},
             std::pair<const char*, const char*>{"EN  English",            "en"},
         }) {
        auto* btn = new QPushButton(QString::fromUtf8(label), w);
        btn->setCheckable(true);
        btn->setChecked(curLang == code);
        btn->setStyleSheet(btnStyle);
        langGroup->addButton(btn);
        const QString codeStr = QString::fromUtf8(code);
        connect(btn, &QPushButton::clicked, this, [this, codeStr]() {
            QSettings s;
            s.setValue("language", codeStr);
            emit languageChanged(codeStr);
        });
        langRow->addWidget(btn);
    }
    langRow->addStretch();
    lay->addLayout(langRow);

    lay->addSpacing(12);
    lay->addWidget(makeSep(w));
    lay->addSpacing(12);

    // « Ne plus afficher » + fermer.
    auto* check = new QCheckBox(tr("Ne plus afficher au d\xc3\xa9marrage"), w);
    check->setStyleSheet(
        "QCheckBox { color: #4b5563; font-size: 11px; background: transparent; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }");
    QSettings s;
    check->setChecked(!s.value("welcome/show", true).toBool());
    connect(check, &QCheckBox::toggled, this, [](bool checked) {
        QSettings ss;
        ss.setValue("welcome/show", !checked);
    });
    lay->addWidget(check);

    lay->addSpacing(8);

    auto* closeBtn = new QPushButton(tr("Commencer  \xe2\x86\x92"), w);
    closeBtn->setStyleSheet(R"(
        QPushButton {
            background: #6366f1;
            color: #ffffff;
            border: none;
            border-radius: 6px;
            padding: 8px 18px;
            font-weight: bold;
        }
        QPushButton:hover { background: #4f46e5; }
        QPushButton:pressed { background: #4338ca; }
    )");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(closeBtn);

    return w;
}

// ── Panneau droit : projets récents + démarrage rapide ───────────────────────

QWidget* WelcomeScreen::buildRightPanel() {
    auto* w = new QWidget(this);
    w->setStyleSheet("QWidget { background: #111827; }");

    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(28, 28, 28, 20);
    lay->setSpacing(0);

    lay->addWidget(makeSectionTitle(tr("Projets r\xc3\xa9" "cents"), w));
    lay->addSpacing(10);

    m_recentList = new QListWidget(w);
    m_recentList->setObjectName("welcomeRecentList");
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setStyleSheet(R"(
        QListWidget {
            background: #1a2235;
            border: 1px solid #1f2d42;
            border-radius: 8px;
            padding: 4px;
        }
        QListWidget::item { padding: 0px; border-radius: 6px; margin: 1px 0px; }
        QListWidget::item:selected {
            background: rgba(99,102,241,0.35);
            border: 1px solid #6366f1;
        }
        QListWidget::item:hover:!selected { background: #1f2d42; }
    )");
    refreshRecentProjects();
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this,         &WelcomeScreen::onItemDoubleClicked);
    lay->addWidget(m_recentList, 1);

    lay->addSpacing(10);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    auto* btnOpen = new QPushButton(tr("Ouvrir la s\xc3\xa9lection"), w);
    btnOpen->setStyleSheet(R"(
        QPushButton {
            background: #1a2235; color: #9ca3af;
            border: 1px solid #1f2d42; border-radius: 6px;
            padding: 6px 14px; font-size: 12px;
        }
        QPushButton:hover { border-color: #6366f1; color: #e5e7eb; }
    )");
    connect(btnOpen, &QPushButton::clicked, this, &WelcomeScreen::onOpenSelectedProject);
    btnRow->addWidget(btnOpen);
    btnRow->addStretch();
    lay->addLayout(btnRow);

    lay->addSpacing(16);
    lay->addWidget(makeSep(w));
    lay->addSpacing(16);

    lay->addWidget(makeSectionTitle(tr("D\xc3\xa9marrage rapide"), w));
    lay->addSpacing(10);

    auto* quickRow = new QHBoxLayout;
    quickRow->setSpacing(8);

    auto makeTile = [&](const QString& icon, const QString& label) -> QPushButton* {
        auto* btn = new QPushButton(w);
        btn->setFixedSize(110, 76);
        btn->setStyleSheet(R"(
            QPushButton {
                background: #1a2235; border: 1px solid #1f2d42;
                border-radius: 8px;
            }
            QPushButton:hover { border-color: #6366f1; background: #1f2d42; }
            QPushButton:pressed { background: #111827; }
        )");
        auto* vl = new QVBoxLayout(btn);
        vl->setContentsMargins(4, 8, 4, 8);
        vl->setSpacing(4);
        vl->setAlignment(Qt::AlignCenter);

        auto* iconLbl = new QLabel(icon, btn);
        QFont f = iconLbl->font();
        f.setPointSize(f.pointSize() + 6);
        iconLbl->setFont(f);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setStyleSheet("background: transparent;");

        auto* textLbl = new QLabel(label, btn);
        textLbl->setAlignment(Qt::AlignCenter);
        textLbl->setWordWrap(true);
        textLbl->setStyleSheet(
            "color: #9ca3af; font-size: 10px; font-weight: 600; background: transparent;");
        vl->addWidget(iconLbl);
        vl->addWidget(textLbl);
        return btn;
    };

    auto* tileNew  = makeTile(QString::fromUtf8("\xf0\x9f\x93\x81"), tr("Nouveau projet"));
    auto* tileRom  = makeTile(QString::fromUtf8("\xf0\x9f\x97\x83"), tr("Ouvrir ROM"));
    auto* tileProj = makeTile(QString::fromUtf8("\xf0\x9f\x93\x82"), tr("Ouvrir projet"));
    auto* tileMpps = makeTile(QString::fromUtf8("\xf0\x9f\x94\x8c"), tr("Scanner MPPS"));

    connect(tileNew,  &QPushButton::clicked, this, [this]() { emit newProjectRequested();  accept(); });
    connect(tileRom,  &QPushButton::clicked, this, [this]() { emit openRomRequested();     accept(); });
    connect(tileProj, &QPushButton::clicked, this, [this]() { emit openProjectRequested(); accept(); });
    connect(tileMpps, &QPushButton::clicked, this, [this]() { emit scanMppsRequested();    accept(); });

    quickRow->addWidget(tileNew);
    quickRow->addWidget(tileRom);
    quickRow->addWidget(tileProj);
    quickRow->addWidget(tileMpps);
    quickRow->addStretch();
    lay->addLayout(quickRow);

    return w;
}

QLabel* WelcomeScreen::makeLink(const QString& icon, const QString& label,
                                const QString& url, QWidget* parent)
{
    auto* lbl = new QLabel(
        QString("%1 <a href=\"%2\" style=\"color:#6366f1; text-decoration:none;\">%3</a>")
            .arg(icon, url, label),
        parent);
    lbl->setTextFormat(Qt::RichText);
    lbl->setOpenExternalLinks(true);
    lbl->setStyleSheet("background: transparent; font-size: 12px;");
    lbl->setCursor(Qt::PointingHandCursor);
    return lbl;
}

void WelcomeScreen::refreshRecentProjects() {
    if (!m_recentList) return;
    m_recentList->clear();

    auto listed = m_manager->list();
    QList<ecu::ProjectMeta> entries;
    if (listed) entries = *listed;

    // Tri par date de création décroissante (les plus récents en tête).
    std::sort(entries.begin(), entries.end(),
              [](const ecu::ProjectMeta& a, const ecu::ProjectMeta& b) {
                  return a.createdAt > b.createdAt;
              });

    if (entries.isEmpty()) {
        auto* item = new QListWidgetItem(m_recentList);
        item->setFlags(Qt::NoItemFlags);
        auto* lbl = new QLabel(tr("Aucun projet r\xc3\xa9" "cent"), m_recentList);
        lbl->setStyleSheet("color: #4b5563; padding: 12px 16px; background: transparent;");
        lbl->setAlignment(Qt::AlignCenter);
        m_recentList->setItemWidget(item, lbl);
        item->setSizeHint(QSize(0, 46));
        return;
    }

    for (const ecu::ProjectMeta& e : entries) {
        auto* item = new QListWidgetItem(m_recentList);
        item->setData(kIdRole, e.id);

        auto* row = new QWidget;
        row->setAttribute(Qt::WA_TranslucentBackground);
        row->setStyleSheet("background: transparent;");
        auto* rl  = new QHBoxLayout(row);
        rl->setContentsMargins(12, 8, 12, 8);
        rl->setSpacing(12);

        auto* col = new QWidget(row);
        auto* cl  = new QVBoxLayout(col);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(2);

        auto* nameLbl = new QLabel(e.name, col);
        QFont f = nameLbl->font(); f.setBold(true); nameLbl->setFont(f);
        nameLbl->setStyleSheet("color: #f1f5f9; background: transparent;");

        QString subtitle = e.ecu;
        if (e.hasRom && !e.romName.isEmpty())
            subtitle += subtitle.isEmpty() ? e.romName : QStringLiteral("  \xc2\xb7  ") + e.romName;
        auto* pathLbl = new QLabel(subtitle, col);
        pathLbl->setStyleSheet("color: #6b7280; font-size: 10px; background: transparent;");
        pathLbl->setTextFormat(Qt::PlainText);

        cl->addWidget(nameLbl);
        cl->addWidget(pathLbl);
        rl->addWidget(col, 1);

        if (e.createdAt.isValid()) {
            auto* dateLbl = new QLabel(e.createdAt.toString("yyyy-MM-dd"), row);
            dateLbl->setStyleSheet(
                "color: #4b5563; font-size: 10px; background: transparent;");
            dateLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rl->addWidget(dateLbl);
        }

        item->setSizeHint(row->sizeHint() + QSize(0, 6));
        m_recentList->setItemWidget(item, row);
    }
}

void WelcomeScreen::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    const QString id = item->data(kIdRole).toString();
    if (!id.isEmpty()) {
        emit openRecentProjectRequested(id);
        accept();
    }
}

void WelcomeScreen::onOpenSelectedProject() {
    auto* item = m_recentList->currentItem();
    if (!item) return;
    const QString id = item->data(kIdRole).toString();
    if (!id.isEmpty()) {
        emit openRecentProjectRequested(id);
        accept();
    }
}

} // namespace ecu_studio
