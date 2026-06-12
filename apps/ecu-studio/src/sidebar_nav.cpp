#include "sidebar_nav.h"
#include "nav_icons.h"

#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFont>
#include <QIcon>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace socketspy::gui {

// ─────────────────────────────────────────────────────────────────────────────
// DetachedWindow — floating host for a detached panel
// ─────────────────────────────────────────────────────────────────────────────
class DetachedWindow : public QWidget {
public:
    DetachedWindow(QWidget* panel, const QString& title, int origIdx, SidebarNav* nav)
        : QWidget(nullptr, Qt::Window)
        , m_panel(panel), m_origIdx(origIdx), m_nav(nav)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(title + " \xe2\x80\x94 SocketSpy");
        resize(900, 600);

        // Header bar with title + redock button
        auto* header = new QWidget(this);
        header->setFixedHeight(28);
        header->setStyleSheet("background:#1e1e35; border-bottom:1px solid #2a2a45;");
        auto* hlay = new QHBoxLayout(header);
        hlay->setContentsMargins(8, 0, 4, 0);
        hlay->setSpacing(4);

        auto* titleLabel = new QLabel(title, header);
        titleLabel->setStyleSheet("color:#9ca3af; font-size:11px; font-weight:600;");
        hlay->addWidget(titleLabel, 1);

        auto* redockBtn = new QPushButton(QString::fromUtf8("\xe2\xac\xa1  Redock"), header);
        redockBtn->setFixedHeight(20);
        redockBtn->setStyleSheet(R"(
            QPushButton {
                background: rgba(99,102,241,0.2); color: #a5b4fc;
                border: 1px solid rgba(99,102,241,0.4); border-radius: 4px;
                font-size: 10px; padding: 0 8px;
            }
            QPushButton:hover { background: rgba(99,102,241,0.35); }
        )");
        connect(redockBtn, &QPushButton::clicked, this, [this]() { close(); });
        hlay->addWidget(redockBtn);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        lay->addWidget(header);
        panel->setParent(this);
        lay->addWidget(panel);
        panel->show();
    }

protected:
    void closeEvent(QCloseEvent* ev) override {
        m_panel->setParent(nullptr);
        m_nav->redock(m_panel, m_origIdx);
        ev->accept();
    }

private:
    QWidget*    m_panel;
    int         m_origIdx;
    SidebarNav* m_nav;
};

// ─────────────────────────────────────────────────────────────────────────────
// SidebarButton — icon+label button with drag-to-detach and context menu
// ─────────────────────────────────────────────────────────────────────────────
class SidebarButton : public QToolButton {
    Q_OBJECT
public:
    SidebarButton(const QString& icon, const QString& label, QWidget* panel,
                  bool beta = false, QWidget* parent = nullptr)
        : QToolButton(parent), m_panel(panel), m_label(label), m_beta(beta)
    {
        setCheckable(true);
        setToolTip(beta ? label + tr(" — BETA (non vérifié, à valider)") : label);
        setFixedWidth(64);
        setMinimumHeight(54);

        // `icon` peut être un id d'icône vectorielle (ex. "hex", "maps") rendu
        // par navIcon(), ou — pour compatibilité — un caractère emoji affiché en
        // texte. On préfère l'icône dessinée : pas de carré « tofu » sans police
        // emoji, et un rendu identique Linux/Windows.
        QIcon drawn = ecu_studio::navIcon(icon);
        if (!drawn.isNull()) {
            setIcon(drawn);
            setIconSize(QSize(24, 24));
            setText(label);
            setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        } else {
            setText(icon + "\n" + label);
            setToolButtonStyle(Qt::ToolButtonTextOnly);
        }

        QFont f = font();
        f.setPointSize(8);
        setFont(f);

        applyStyle();

        connect(this, &QToolButton::clicked, this, [this]() {
            emit panelActivated(m_panel);
        });
    }

    void setDetached(bool d) {
        m_detached = d;
        setEnabled(!d);
    }

signals:
    void panelActivated(QWidget* panel);
    void detachTriggered(QWidget* panel, const QString& label);

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) m_dragStart = e->pos();
        QToolButton::mousePressEvent(e);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!m_detached && (e->buttons() & Qt::LeftButton)
                && (e->pos() - m_dragStart).manhattanLength() > 24) {
            emit detachTriggered(m_panel, m_label);
            return;
        }
        QToolButton::mouseMoveEvent(e);
    }

    void contextMenuEvent(QContextMenuEvent* e) override {
        QMenu menu(this);
        auto* act = menu.addAction(tr("Detach to window"));
        act->setEnabled(!m_detached);
        if (menu.exec(e->globalPos()) == act)
            emit detachTriggered(m_panel, m_label);
    }

    // Pastille « BETA » (ambre) en coin haut-droit pour les features non
    // validées (matériel non testé / relocalisation partielle).
    void paintEvent(QPaintEvent* e) override {
        QToolButton::paintEvent(e);
        if (!m_beta) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QFont bf = font();
        bf.setPointSizeF(6.0);
        bf.setBold(true);
        p.setFont(bf);
        const QString txt = QStringLiteral("BETA");
        const QFontMetrics fm(bf);
        const int w = fm.horizontalAdvance(txt) + 6;
        const int h = fm.height() + 1;
        const QRect badge(width() - w - 3, 3, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xf5, 0x9e, 0x0b));          // ambre
        p.drawRoundedRect(badge, 3, 3);
        p.setPen(QColor(0x1a, 0x1a, 0x2a));            // texte sombre
        p.drawText(badge, Qt::AlignCenter, txt);
    }

private:
    void applyStyle() {
        setStyleSheet(R"(
            QToolButton {
                border: none;
                border-radius: 6px;
                padding: 4px 2px;
                color: #9ca3af;
                text-align: center;
            }
            QToolButton:hover    { background: rgba(255,255,255,0.07); color: #e5e7eb; }
            QToolButton:checked  { background: rgba(99,102,241,0.28);  color: #a5b4fc; }
            QToolButton:disabled { color: #4b5563; }
        )");
    }

    QWidget* m_panel;
    QString  m_label;
    QPoint   m_dragStart;
    bool     m_detached{false};
    bool     m_beta{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// SidebarNav
// ─────────────────────────────────────────────────────────────────────────────
SidebarNav::SidebarNav(QWidget* parent) : QWidget(parent) {
    setObjectName("SidebarNav");
    setFixedWidth(68);
    setStyleSheet("QWidget#SidebarNav { background:#16162a; border-right:1px solid #2a2a45; }");

    m_stack = new QStackedWidget(this);

    auto* scrollContent = new QWidget;
    scrollContent->setStyleSheet("background:transparent;");

    m_btnLayout = new QVBoxLayout(scrollContent);
    m_btnLayout->setContentsMargins(2, 8, 2, 8);
    m_btnLayout->setSpacing(2);
    m_btnLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(scrollContent);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { width:4px; background:transparent; }"
        "QScrollBar::handle:vertical { background:#3a3a5c; border-radius:2px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(scroll);
}

void SidebarNav::addPanel(const QString& icon, const QString& label, QWidget* panel, bool beta) {
    auto* btn = new SidebarButton(icon, label, panel, beta);
    m_btnLayout->insertWidget(m_btnLayout->count() - 1, btn);
    m_stack->addWidget(panel);

    m_entries.append({icon, label, panel, btn, false});

    connect(btn, &SidebarButton::panelActivated,  this, &SidebarNav::onButtonActivated);
    connect(btn, &SidebarButton::detachTriggered, this, &SidebarNav::onDetachRequested);

    if (m_entries.size() == 1) {
        btn->setChecked(true);
        m_stack->setCurrentWidget(panel);
    }
}

void SidebarNav::addSeparator(const QString& title) {
    auto* w   = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(8, 7, 8, 1);
    lay->setSpacing(3);

    auto* line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet("background:#2a2a45; border:none;");
    lay->addWidget(line);

    if (!title.isEmpty()) {
        auto* lbl = new QLabel(title);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(
            "color:#5b6478; font-size:8px; font-weight:700; letter-spacing:1px;");
        lay->addWidget(lbl);
    }
    // Insère avant le stretch final (comme addPanel).
    m_btnLayout->insertWidget(m_btnLayout->count() - 1, w);
}

void SidebarNav::showPanel(QWidget* panel) {
    for (auto& e : m_entries)
        e.btn->setChecked(e.panel == panel && !e.detached);
    if (m_stack->indexOf(panel) >= 0)
        m_stack->setCurrentWidget(panel);
}

void SidebarNav::setPanelVisible(QWidget* panel, bool visible) {
    for (auto& e : m_entries) {
        if (e.panel != panel) continue;
        e.btn->setVisible(visible);
        if (!visible && m_stack->currentWidget() == panel) {
            for (auto& other : m_entries) {
                if (other.panel != panel && !other.detached
                        && other.btn->isVisible()
                        && m_stack->indexOf(other.panel) >= 0) {
                    onButtonActivated(other.panel);
                    break;
                }
            }
        }
        break;
    }
}

int SidebarNav::panelCount() const { return m_entries.size(); }

void SidebarNav::onButtonActivated(QWidget* panel) {
    for (auto& e : m_entries)
        e.btn->setChecked(e.panel == panel);
    if (m_stack->indexOf(panel) >= 0)
        m_stack->setCurrentWidget(panel);
    emit currentPanelChanged(panel);
}

void SidebarNav::onDetachRequested(QWidget* panel, const QString& title) {
    Entry* e = entryForPanel(panel);
    if (!e || e->detached) return;

    int origIdx = m_stack->indexOf(panel);
    e->detached = true;
    e->btn->setDetached(true);

    auto* win = new DetachedWindow(panel, title, origIdx, this);
    win->show();

    // Switch to first available non-detached panel
    for (auto& other : m_entries) {
        if (!other.detached && m_stack->indexOf(other.panel) >= 0) {
            onButtonActivated(other.panel);
            break;
        }
    }
}

void SidebarNav::redock(QWidget* panel, int origIdx) {
    Entry* e = entryForPanel(panel);
    if (!e) return;

    e->detached = false;
    e->btn->setDetached(false);

    m_stack->insertWidget(qMin(origIdx, m_stack->count()), panel);
    showPanel(panel);
}

SidebarNav::Entry* SidebarNav::entryForPanel(QWidget* panel) {
    for (auto& e : m_entries)
        if (e.panel == panel) return &e;
    return nullptr;
}

} // namespace socketspy::gui

#include "sidebar_nav.moc"
