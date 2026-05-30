#include "mpps_panel.h"
#include "../byte_span.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QFile>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

namespace ecu_studio {

MppsPanel::MppsPanel(QWidget* parent) : QWidget(parent) {
    buildUi();
    scanDevices();
}

void MppsPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // ── Connexion ─────────────────────────────────────────────────────────
    auto* connBox = new QGroupBox(tr("Connexion"), this);
    auto* connLay = new QVBoxLayout(connBox);

    auto* row1 = new QHBoxLayout;
    m_deviceCombo = new QComboBox(this);
    m_deviceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_scanBtn    = new QPushButton(tr("Scanner"), this);
    m_connectBtn = new QPushButton(tr("Connecter"), this);
    m_connectBtn->setObjectName("accentBtn");
    row1->addWidget(m_deviceCombo, 1);
    row1->addWidget(m_scanBtn);
    row1->addWidget(m_connectBtn);
    connLay->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    m_statusDot  = new QLabel("\xe2\x97\x8f", this);
    m_statusDot->setStyleSheet("color:#4b5563; font-size:16px;");
    m_statusText = new QLabel(tr("Non connecté"), this);
    m_statusText->setStyleSheet("color:#7c8fa6;");
    m_ecuLabel   = new QLabel(this);
    m_ecuLabel->setStyleSheet("color:#22c55e; font-weight:600;");
    row2->addWidget(m_statusDot);
    row2->addWidget(m_statusText);
    row2->addStretch();
    row2->addWidget(m_ecuLabel);
    connLay->addLayout(row2);
    root->addWidget(connBox);

    // ── Opérations ────────────────────────────────────────────────────────
    auto* opBox = new QGroupBox(tr("Opérations"), this);
    auto* opLay = new QVBoxLayout(opBox);

    auto* row3 = new QHBoxLayout;
    row3->addWidget(new QLabel(tr("Protocole:"), this));
    m_protocolCombo = new QComboBox(this);
    m_protocolCombo->addItems({ "K-Line", "CAN", "Auto" });
    row3->addWidget(m_protocolCombo);
    row3->addStretch();
    opLay->addLayout(row3);

    auto* row4 = new QHBoxLayout;
    m_readBtn  = new QPushButton(tr("Lire ROM"), this);
    m_writeBtn = new QPushButton(tr("Écrire ROM..."), this);
    m_readBtn->setEnabled(false);
    m_writeBtn->setEnabled(false);
    row4->addWidget(m_readBtn);
    row4->addWidget(m_writeBtn);
    row4->addStretch();
    opLay->addLayout(row4);

    m_progress      = new QProgressBar(this);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet("color:#7c8fa6; font-size:11px;");
    m_progress->hide();
    m_progressLabel->hide();
    opLay->addWidget(m_progress);
    opLay->addWidget(m_progressLabel);
    root->addWidget(opBox);

    // ── Journal ───────────────────────────────────────────────────────────
    auto* logBox = new QGroupBox(tr("Journal"), this);
    auto* logLay = new QVBoxLayout(logBox);
    m_log = new QTextEdit(this);
    m_log->setReadOnly(true);
    m_log->document()->setMaximumBlockCount(500);
    m_log->setFont(QFont("Monospace", 10));
    m_log->setStyleSheet("background:#111827; color:#e5e7eb; border:none;");
    logLay->addWidget(m_log);
    root->addWidget(logBox, 1);

    // Connexions
    connect(m_scanBtn,    &QPushButton::clicked, this, &MppsPanel::scanDevices);
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        if (m_device && m_device->isConnected()) disconnectDevice();
        else                                     connectDevice();
    });
    connect(m_readBtn,  &QPushButton::clicked, this, &MppsPanel::readRom);
    connect(m_writeBtn, &QPushButton::clicked, this, [this]() { writeRom(); });
    connect(this, &MppsPanel::progressUpdated, this, [this](int pct, const QString& msg) {
        m_progress->setValue(pct);
        m_progressLabel->setText(msg);
    }, Qt::QueuedConnection);
}

void MppsPanel::log(const QString& msg, bool error) {
    const QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString color = error ? "#ef4444" : "#7c8fa6";
    m_log->append(QString("<span style='color:%1'>%2</span> <span style='color:#f1f5f9'>%3</span>")
                  .arg(color, time, msg.toHtmlEscaped()));
}

void MppsPanel::setOperating(bool on) {
    m_scanBtn->setEnabled(!on);
    m_connectBtn->setEnabled(!on);
    m_readBtn->setEnabled(!on && m_device && m_device->isConnected());
    m_writeBtn->setEnabled(!on && m_device && m_device->isConnected());
    m_progress->setVisible(on);
    m_progressLabel->setVisible(on);
    if (!on) m_progress->setValue(0);
}

void MppsPanel::scanDevices() {
    m_deviceCombo->clear();
    m_devices = mpps::MppsDevice::enumerate();
    if (m_devices.empty()) {
        m_deviceCombo->addItem(tr("Aucun périphérique trouvé"));
        log(tr("Scan : aucun périphérique MPPS/FTDI détecté"));
    } else {
        for (const auto& d : m_devices)
            m_deviceCombo->addItem(QString::fromStdString(d.chipType + " — " + d.path));
        log(tr("Scan : %1 périphérique(s) trouvé(s)").arg(m_devices.size()));
    }
}

void MppsPanel::connectDevice() {
    if (m_devices.empty()) { log(tr("Aucun périphérique à connecter"), true); return; }

    int idx = m_deviceCombo->currentIndex();
    if (idx < 0 || idx >= (int)m_devices.size()) return;

    setOperating(true);
    log(tr("Connexion à %1...").arg(m_deviceCombo->currentText()));

#ifdef ECU_MPPS_SIMULATION
    m_device = mpps::MppsDevice::openSimulation();
#else
    m_device = mpps::MppsDevice::open(m_devices[idx]);
#endif

    auto result = m_device->connect();
    setOperating(false);

    if (!result) {
        log(tr("Erreur connexion: %1").arg(QString::fromStdString(mpps::to_string(result.error()))), true);
        m_device.reset();
        return;
    }

    const auto& info = *result;
    m_statusDot->setStyleSheet("color:#22c55e; font-size:16px;");
    m_statusText->setText(tr("Connecté — %1 — fw %2")
                          .arg(QString::fromStdString(info.chipType),
                               QString::fromStdString(info.firmwareVersion)));
    m_connectBtn->setText(tr("Déconnecter"));
    m_readBtn->setEnabled(true);
    m_writeBtn->setEnabled(true);
    log(tr("[OK] Connexion %1").arg(QString::fromStdString(info.chipType)));

    // Identifier l'ECU
    auto ecuId = m_device->identifyEcu();
    if (ecuId) {
        m_ecuLabel->setText(QString::fromStdString(*ecuId));
        log(tr("[OK] ECU : %1").arg(QString::fromStdString(*ecuId)));
    }

    emit deviceStatusChanged(m_statusText->text());
}

void MppsPanel::disconnectDevice() {
    if (m_device) m_device->disconnect();
    m_device.reset();
    m_statusDot->setStyleSheet("color:#4b5563; font-size:16px;");
    m_statusText->setText(tr("Déconnecté"));
    m_ecuLabel->clear();
    m_connectBtn->setText(tr("Connecter"));
    m_readBtn->setEnabled(false);
    m_writeBtn->setEnabled(false);
    log(tr("Déconnecté"));
    emit deviceStatusChanged(tr("Aucun périphérique"));
}

void MppsPanel::readRom() {
    if (!m_device || !m_device->isConnected()) return;
    setOperating(true);
    log(tr("Lecture ROM 2Mo démarrée..."));
    m_progress->setRange(0, 100);

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        setOperating(false);
    });

    auto future = QtConcurrent::run([this]() {
        auto cb = [this](uint32_t done, uint32_t total, const std::string& msg) {
            emit progressUpdated((int)(done * 100 / total), QString::fromStdString(msg));
        };
        auto result = m_device->readFullRom(0x200000, cb);
        if (result) {
            QByteArray data(reinterpret_cast<const char*>(result->data()), (qsizetype)result->size());
            emit romReadComplete(data);
            log(tr("[OK] ROM lue (%1 Ko)").arg(result->size() / 1024));
        } else {
            emit operationFailed(QString::fromStdString(mpps::to_string(result.error())));
            log(tr("[ERREUR] Lecture : %1").arg(QString::fromStdString(mpps::to_string(result.error()))), true);
        }
    });
    watcher->setFuture(future);
}

void MppsPanel::writeRom(const QByteArray& rom) {
    if (!m_device || !m_device->isConnected()) return;

    QByteArray data = rom;
    if (data.isEmpty()) {
        QString f = QFileDialog::getOpenFileName(this, tr("ROM à écrire"), {},
                                                 tr("ROM (*.bin *.mod);;Tous (*.*)"));
        if (f.isEmpty()) return;
        QFile file(f);
        if (!file.open(QIODevice::ReadOnly)) {
            log(tr("[ERREUR] Impossible d'ouvrir %1").arg(f), true);
            return;
        }
        data = file.readAll();
    }

    if (QMessageBox::warning(this, tr("Confirmation"),
            tr("Écriture de %1 Ko sur l'ECU.\nUne interruption risque de bricker l'ECU.\nContinuer ?")
               .arg(data.size() / 1024),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    m_pendingWrite = data;
    setOperating(true);
    log(tr("Écriture ROM %1 Ko...").arg(data.size() / 1024));
    m_progress->setRange(0, 100);

    auto* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        setOperating(false);
    });

    auto future = QtConcurrent::run([this]() {
        auto cb = [this](uint32_t done, uint32_t total, const std::string& msg) {
            emit progressUpdated((int)(done * 100 / total), QString::fromStdString(msg));
        };
        auto result = m_device->writeFullRom(constByteSpan(m_pendingWrite), cb);
        if (result) {
            log(tr("[OK] ROM écrite avec succès"));
        } else {
            emit operationFailed(QString::fromStdString(mpps::to_string(result.error())));
            log(tr("[ERREUR] Écriture : %1").arg(QString::fromStdString(mpps::to_string(result.error()))), true);
        }
    });
    watcher->setFuture(future);
}

} // namespace ecu_studio
