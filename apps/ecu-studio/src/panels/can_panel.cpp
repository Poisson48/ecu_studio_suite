#include "can_panel.h"

#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>
#include <QMessageBox>
#include <QStandardPaths>

#ifdef ECU_HAVE_CANCORE
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <cerrno>
#include <cstring>
#include <chrono>

using socketspy::core::CanFrame;
using socketspy::core::FrameFlags;
using socketspy::core::IfaceHandle;
#endif

namespace ecu_studio {

// ───────────────────────────────────────────────────────────────────────────
// Détection d'interfaces — renvoie vide hors Linux (/sys/class/net absent).
// ───────────────────────────────────────────────────────────────────────────
QStringList CanPanel::detectInterfaces() {
    QStringList out;
    QDir netDir("/sys/class/net");
    if (!netDir.exists()) return out;
    for (const QString& name : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name.startsWith("can") || name.startsWith("vcan") ||
            name.startsWith("slcan")) {
            out << name;
        }
    }
    out.sort();
    return out;
}

#ifdef ECU_HAVE_CANCORE
// ───────────────────────────────────────────────────────────────────────────
// CanCaptureThread — boucle de lecture SocketCAN bâtie sur cancore.
// ───────────────────────────────────────────────────────────────────────────
CanCaptureThread::CanCaptureThread(QString iface, QObject* parent)
    : QThread(parent), m_iface(std::move(iface)) {}

CanCaptureThread::~CanCaptureThread() {
    stop();
    wait();
}

void CanCaptureThread::stop() {
    m_stop.store(true, std::memory_order_relaxed);
}

void CanCaptureThread::run() {
    IfaceHandle h = socketspy::core::can_open(m_iface.toStdString());
    if (!h.valid()) {
        emit errorOccurred(tr("can_open(%1) a échoué : %2")
                               .arg(m_iface)
                               .arg(QString::fromLocal8Bit(strerror(errno))));
        return;
    }

    // Active les trames CAN FD (sans effet sur un bus classique).
    socketspy::core::can_set_fd_mode(h, true);

    // Timeout de réception de 100 ms pour pouvoir vérifier m_stop régulièrement.
    struct timeval tv{0, 100'000};
    ::setsockopt(h.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    quint64 frameCount = 0;
    auto statDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);

    while (!m_stop.load(std::memory_order_relaxed)) {
        canfd_frame raw{};
        ssize_t n = ::read(h.fd, &raw, sizeof(raw));
        auto now = std::chrono::steady_clock::now();

        if (n == static_cast<ssize_t>(sizeof(can_frame)) ||
            n == static_cast<ssize_t>(sizeof(canfd_frame))) {
            const bool isFd = (n == static_cast<ssize_t>(sizeof(canfd_frame)));

            CanFrame f{};
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          now.time_since_epoch())
                          .count();
            f.timestamp_us = static_cast<uint64_t>(us);
            f.id           = raw.can_id & CAN_EFF_MASK;
            f.dlc          = raw.len;
            f.iface_idx    = h.idx;

            if (isFd) {
                f.flags = static_cast<uint8_t>(FrameFlags::FD);
                if (raw.flags & CANFD_BRS) f.flags |= static_cast<uint8_t>(FrameFlags::BRS);
                if (raw.flags & CANFD_ESI) f.flags |= static_cast<uint8_t>(FrameFlags::ESI);
            } else {
                f.flags = 0;
                if (raw.can_id & CAN_RTR_FLAG)
                    f.flags |= static_cast<uint8_t>(FrameFlags::RTR);
            }
            if (raw.can_id & CAN_ERR_FLAG)
                f.flags |= static_cast<uint8_t>(FrameFlags::Error);

            std::memcpy(f.data, raw.data,
                        std::min<size_t>(raw.len, sizeof(f.data)));
            ++frameCount;
            emit frameReceived(f);
        }

        if (now >= statDeadline) {
            emit statsUpdated(frameCount);
            frameCount   = 0;
            statDeadline = now + std::chrono::seconds(1);
        }
    }

    socketspy::core::can_close(h);
}
#endif // ECU_HAVE_CANCORE

// ───────────────────────────────────────────────────────────────────────────
// CanPanel
// ───────────────────────────────────────────────────────────────────────────
CanPanel::CanPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    refreshInterfaces();
}

CanPanel::~CanPanel() {
#ifdef ECU_HAVE_CANCORE
    stopCapture();
#endif
}

void CanPanel::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ── Barre de contrôle ────────────────────────────────────────────────────
    auto* bar = new QHBoxLayout();
    bar->addWidget(new QLabel(tr("Interface :"), this));

    m_ifaceCombo = new QComboBox(this);
    m_ifaceCombo->setMinimumWidth(120);
    bar->addWidget(m_ifaceCombo);

    m_refreshBtn = new QPushButton(tr("Rafraîchir"), this);
    bar->addWidget(m_refreshBtn);

    m_startBtn = new QPushButton(tr("Démarrer"), this);
    bar->addWidget(m_startBtn);

    m_stopBtn = new QPushButton(tr("Arrêter"), this);
    m_stopBtn->setEnabled(false);
    bar->addWidget(m_stopBtn);

    m_clearBtn = new QPushButton(tr("Effacer"), this);
    bar->addWidget(m_clearBtn);

    m_aggregateChk = new QCheckBox(tr("Agréger par ID"), this);
    m_aggregateChk->setChecked(true);
    bar->addWidget(m_aggregateChk);

    bar->addStretch(1);

    // Bouton de lancement de l'app SocketSpy complète.
    m_launchBtn = new QPushButton(tr("Ouvrir SocketSpy"), this);
    m_launchBtn->setToolTip(
        tr("Lance l'application SocketSpy complète (analyse CAN avancée)"));
    bar->addWidget(m_launchBtn);

    root->addLayout(bar);

    // ── Table des trames ─────────────────────────────────────────────────────
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {tr("Horodatage"), tr("ID"), tr("Type"), tr("DLC"), tr("Données"), tr("Count")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(m_table, 1);

    // ── Statut ───────────────────────────────────────────────────────────────
    m_statusLabel = new QLabel(this);
    root->addWidget(m_statusLabel);

    connect(m_refreshBtn, &QPushButton::clicked, this, &CanPanel::refreshInterfaces);
    connect(m_launchBtn,  &QPushButton::clicked, this, &CanPanel::launchSocketSpy);

#ifdef ECU_HAVE_CANCORE
    connect(m_startBtn, &QPushButton::clicked, this, &CanPanel::startCapture);
    connect(m_stopBtn,  &QPushButton::clicked, this, &CanPanel::stopCapture);
    connect(m_clearBtn, &QPushButton::clicked, this, &CanPanel::clearFrames);
#else
    // Sans cancore (hors Linux) : la capture embarquée n'est pas disponible.
    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
    m_clearBtn->setEnabled(false);
    m_aggregateChk->setEnabled(false);
#endif
}

void CanPanel::refreshInterfaces() {
    const QString current = m_ifaceCombo->currentText();
    m_ifaceCombo->clear();
    const QStringList ifaces = detectInterfaces();
    m_ifaceCombo->addItems(ifaces);

    const bool any = !ifaces.isEmpty();
    m_ifaceCombo->setEnabled(any);
#ifdef ECU_HAVE_CANCORE
    m_startBtn->setEnabled(any);
    if (!any) {
        m_statusLabel->setText(tr("Aucune interface CAN"));
    } else {
        const int idx = m_ifaceCombo->findText(current);
        if (idx >= 0) m_ifaceCombo->setCurrentIndex(idx);
        m_statusLabel->setText(
            tr("%1 interface(s) CAN détectée(s) — prêt").arg(ifaces.size()));
    }
#else
    m_statusLabel->setText(
        tr("Aucune interface CAN (SocketCAN indisponible sur cette plateforme)"));
#endif
}

// ───────────────────────────────────────────────────────────────────────────
// Lancement de l'app SocketSpy complète
// ───────────────────────────────────────────────────────────────────────────
QString CanPanel::resolveSocketSpyPath() {
    const QString exe =
#ifdef Q_OS_WIN
        "socketspy.exe";
#else
        "socketspy";
#endif
    // Emplacements candidats : à côté d'ecu_studio, puis dans l'arbre de build.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/" + exe,
        appDir + "/../socketspy/" + exe,            // build/apps/.. layout
        appDir + "/../apps/socketspy/gui/" + exe,
        appDir + "/../../apps/socketspy/gui/" + exe,
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c)) return QFileInfo(c).absoluteFilePath();
    }
    // Dernier recours : dans le PATH.
    const QString inPath = QStandardPaths::findExecutable(exe);
    return inPath; // chaîne vide si introuvable
}

void CanPanel::launchSocketSpy() {
    const QString path = resolveSocketSpyPath();
    if (path.isEmpty()) {
        QMessageBox::information(
            this, tr("SocketSpy"),
            tr("Le binaire SocketSpy est introuvable.\n\n"
               "Pour l'activer, buildez la suite avec -DECU_BUILD_SOCKETSPY=ON "
               "(nécessite Qt6 SerialBus/Charts/Bluetooth), ou placez l'exécutable "
               "« socketspy » à côté d'ecu_studio."));
        return;
    }

    if (m_socketspyProc &&
        m_socketspyProc->state() != QProcess::NotRunning) {
        m_statusLabel->setText(tr("SocketSpy est déjà lancé."));
        return;
    }

    if (!m_socketspyProc) {
        m_socketspyProc = new QProcess(this);
        connect(m_socketspyProc, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError) {
                    m_statusLabel->setText(
                        tr("Échec du lancement de SocketSpy : %1")
                            .arg(m_socketspyProc->errorString()));
                });
    }
    m_socketspyProc->setProgram(path);
    m_socketspyProc->start();
    m_statusLabel->setText(tr("SocketSpy lancé (%1).").arg(path));
}

#ifdef ECU_HAVE_CANCORE
void CanPanel::startCapture() {
    if (m_capture) return;
    const QString iface = m_ifaceCombo->currentText();
    if (iface.isEmpty()) {
        m_statusLabel->setText(tr("Aucune interface CAN"));
        return;
    }

    m_capture = new CanCaptureThread(iface, this);
    connect(m_capture, &CanCaptureThread::frameReceived, this, &CanPanel::onFrame);
    connect(m_capture, &CanCaptureThread::statsUpdated, this, [this](quint64 fps) {
        m_statusLabel->setText(tr("Capture %1 — %2 trames/s")
                                   .arg(m_ifaceCombo->currentText())
                                   .arg(fps));
    });
    connect(m_capture, &CanCaptureThread::errorOccurred, this, [this](const QString& msg) {
        m_statusLabel->setText(msg);
        stopCapture();
    });

    m_capture->start();

    m_startBtn->setEnabled(false);
    m_stopBtn->setEnabled(true);
    m_refreshBtn->setEnabled(false);
    m_ifaceCombo->setEnabled(false);
    m_statusLabel->setText(tr("Capture en cours sur %1…").arg(iface));
}

void CanPanel::stopCapture() {
    if (!m_capture) return;
    m_capture->stop();
    m_capture->wait();
    m_capture->deleteLater();
    m_capture = nullptr;

    m_startBtn->setEnabled(!m_ifaceCombo->currentText().isEmpty());
    m_stopBtn->setEnabled(false);
    m_refreshBtn->setEnabled(true);
    m_ifaceCombo->setEnabled(true);
}

void CanPanel::clearFrames() {
    m_table->setRowCount(0);
    m_rowById.clear();
}

void CanPanel::onFrame(const CanFrame& f) {
    const bool aggregate = m_aggregateChk->isChecked();

    QString type;
    if (f.flags & static_cast<uint8_t>(FrameFlags::Error))      type = "ERR";
    else if (f.flags & static_cast<uint8_t>(FrameFlags::FD))    type = "FD";
    else if (f.flags & static_cast<uint8_t>(FrameFlags::RTR))   type = "RTR";
    else                                                        type = "STD";

    const QString idStr =
        "0x" + QString("%1").arg(f.id, 0, 16).toUpper();

    QString dataStr;
    for (uint8_t i = 0; i < f.dlc && i < sizeof(f.data); ++i) {
        if (i) dataStr += ' ';
        dataStr += QString("%1").arg(f.data[i], 2, 16, QChar('0')).toUpper();
    }

    const QString tsStr = QString::number(f.timestamp_us / 1000.0, 'f', 3);

    auto setCell = [this](int row, int col, const QString& text) {
        m_table->setItem(row, col, new QTableWidgetItem(text));
    };

    if (aggregate) {
        auto it = m_rowById.find(f.id);
        int row;
        if (it == m_rowById.end()) {
            row = m_table->rowCount();
            m_table->insertRow(row);
            m_rowById.insert(f.id, row);
            setCell(row, 5, "1");
        } else {
            row = it.value();
            const int count =
                m_table->item(row, 5) ? m_table->item(row, 5)->text().toInt() : 0;
            setCell(row, 5, QString::number(count + 1));
        }
        setCell(row, 0, tsStr);
        setCell(row, 1, idStr);
        setCell(row, 2, type);
        setCell(row, 3, QString::number(f.dlc));
        setCell(row, 4, dataStr);
    } else {
        constexpr int kMaxRows = 2000;
        if (m_table->rowCount() >= kMaxRows) m_table->removeRow(0);
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        setCell(row, 0, tsStr);
        setCell(row, 1, idStr);
        setCell(row, 2, type);
        setCell(row, 3, QString::number(f.dlc));
        setCell(row, 4, dataStr);
        setCell(row, 5, "—");
        m_table->scrollToBottom();
    }
}
#endif // ECU_HAVE_CANCORE

} // namespace ecu_studio
