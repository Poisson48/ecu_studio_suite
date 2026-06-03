#include "hub/verify_on_bus_dialog.h"

#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QProcess>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDateTime>
#include <QRandomGenerator>
#include <cmath>

#include "rom_document.h"
#include "hub/maturity_badge.h"
#include "hub/sub_program_registry.h"

namespace ecu_studio {

namespace {
// Plage de ports loopback éphémères pour socketspy-mcp --tcp <port>. On en tire
// un au hasard à chaque ouverture pour éviter les collisions si plusieurs
// dialogues sont ouverts en même temps.
constexpr quint16 kPortMin = 49200;
constexpr quint16 kPortMax = 49899;

// Intervalle entre deux échantillons can_monitor (machine à états de polling).
constexpr int kPollIntervalMs = 750;
// Durée d'une fenêtre de capture can_monitor (court : on veut du quasi-direct).
constexpr int kSampleWindowMs = 400;
} // namespace

// ───────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ───────────────────────────────────────────────────────────────────────────
VerifyOnBusDialog::VerifyOnBusDialog(RomDocument* doc, QWidget* parent)
    : QDialog(parent), m_doc(doc) {
    setWindowTitle(tr("Vérifier sur le bus CAN"));
    setModal(false);
    setMinimumSize(560, 520);

    buildUi();
    refreshInterfaces();

    // Pré-remplissage opportuniste : si le document porte un ecuId, on tente de
    // charger la recette OpenDAMOS et de pré-remplir depuis sa première entrée
    // utile. L'appelant peut surcharger via prefillFromDamos().
    if (m_doc && m_doc->isLoaded() && !m_doc->ecuId().isEmpty()) {
        auto recipeRes = ecu::OpenDamos::loadRecipe(m_doc->ecuId());
        if (recipeRes) {
            const auto& recipe = recipeRes.value();
            if (!recipe.characteristics.empty()) {
                prefillFromDamos(recipe.characteristics.front());
            }
        }
    }
}

VerifyOnBusDialog::~VerifyOnBusDialog() {
    // Arrêt propre : on coupe le socket puis on termine le sous-processus.
    if (m_socket) {
        m_socket->abort();
    }
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(1500)) {
            m_proc->kill();
            m_proc->waitForFinished(500);
        }
    }
}

// ───────────────────────────────────────────────────────────────────────────
// UI
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ── En-tête (« barre de titre ») avec badge de maturité ──────────────────
    auto* header = new QHBoxLayout();
    auto* title = new QLabel(tr("Vérification post-flash sur le bus"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    header->addWidget(title);
    header->addStretch(1);
    m_badge = new MaturityBadge(MaturityBadge::Maturity::Beta, this);
    header->addWidget(m_badge, 0, Qt::AlignTop);
    root->addLayout(header);

    // ── Formulaire de configuration ──────────────────────────────────────────
    auto* cfgBox = new QGroupBox(tr("Cible"), this);
    auto* form = new QFormLayout(cfgBox);

    // Interface CAN + rafraîchir.
    auto* ifaceRow = new QHBoxLayout();
    m_ifaceCombo = new QComboBox(cfgBox);
    m_ifaceCombo->setEditable(true);
    m_ifaceCombo->setMinimumWidth(140);
    ifaceRow->addWidget(m_ifaceCombo, 1);
    m_refreshBtn = new QPushButton(tr("Rafraîchir"), cfgBox);
    ifaceRow->addWidget(m_refreshBtn);
    form->addRow(tr("Interface CAN :"), ifaceRow);

    // Fichier DBC.
    auto* dbcRow = new QHBoxLayout();
    m_dbcEdit = new QLineEdit(cfgBox);
    m_dbcEdit->setPlaceholderText(tr("/chemin/vers/base.dbc"));
    dbcRow->addWidget(m_dbcEdit, 1);
    m_dbcBrowseBtn = new QPushButton(tr("Parcourir…"), cfgBox);
    dbcRow->addWidget(m_dbcBrowseBtn);
    form->addRow(tr("Fichier DBC :"), dbcRow);

    // Nom du signal.
    m_signalEdit = new QLineEdit(cfgBox);
    m_signalEdit->setPlaceholderText(tr("Nom du signal (ex. Eng_nAvrg)"));
    form->addRow(tr("Signal :"), m_signalEdit);

    // Valeur cible + comparateur + unité.
    auto* targetRow = new QHBoxLayout();
    m_cmpCombo = new QComboBox(cfgBox);
    m_cmpCombo->addItem(tr("≥  (supérieur ou égal)"),
                        static_cast<int>(Comparator::GreaterEqual));
    m_cmpCombo->addItem(tr("≤  (inférieur ou égal)"),
                        static_cast<int>(Comparator::LessEqual));
    m_cmpCombo->addItem(tr("≈  (égal à la tolérance près)"),
                        static_cast<int>(Comparator::ApproxEqual));
    m_cmpCombo->addItem(tr(">  (strictement supérieur)"),
                        static_cast<int>(Comparator::Greater));
    m_cmpCombo->addItem(tr("<  (strictement inférieur)"),
                        static_cast<int>(Comparator::Less));
    m_cmpCombo->addItem(tr("≠  (différent, hors tolérance)"),
                        static_cast<int>(Comparator::NotEqual));
    targetRow->addWidget(m_cmpCombo);

    m_targetSpin = new QDoubleSpinBox(cfgBox);
    m_targetSpin->setRange(-1.0e9, 1.0e9);
    m_targetSpin->setDecimals(3);
    m_targetSpin->setValue(0.0);
    targetRow->addWidget(m_targetSpin, 1);

    m_unitLabel = new QLabel(QString(), cfgBox);
    m_unitLabel->setMinimumWidth(48);
    targetRow->addWidget(m_unitLabel);
    form->addRow(tr("Valeur cible :"), targetRow);

    // Tolérance (pour ≈ et ≠).
    m_toleranceSpin = new QDoubleSpinBox(cfgBox);
    m_toleranceSpin->setRange(0.0, 1.0e9);
    m_toleranceSpin->setDecimals(3);
    m_toleranceSpin->setValue(0.5);
    form->addRow(tr("Tolérance (±) :"), m_toleranceSpin);

    // Timeout global.
    m_timeoutSpin = new QSpinBox(cfgBox);
    m_timeoutSpin->setRange(1, 600);
    m_timeoutSpin->setValue(15);
    m_timeoutSpin->setSuffix(tr(" s"));
    form->addRow(tr("Timeout :"), m_timeoutSpin);

    root->addWidget(cfgBox);

    // ── Bouton Vérifier ──────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_verifyBtn = new QPushButton(tr("Vérifier"), this);
    m_verifyBtn->setObjectName(QStringLiteral("accentBtn"));
    m_verifyBtn->setDefault(true);
    btnRow->addWidget(m_verifyBtn);
    root->addLayout(btnRow);

    // ── Journal de résultats ─────────────────────────────────────────────────
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText(
        tr("Le journal de vérification s'affiche ici…"));
    root->addWidget(m_logView, 1);

    m_statusLabel = new QLabel(tr("Prêt."), this);
    root->addWidget(m_statusLabel);

    // ── Connexions ───────────────────────────────────────────────────────────
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &VerifyOnBusDialog::refreshInterfaces);
    connect(m_dbcBrowseBtn, &QPushButton::clicked,
            this, &VerifyOnBusDialog::pickDbcFile);
    connect(m_verifyBtn, &QPushButton::clicked,
            this, &VerifyOnBusDialog::onVerifyClicked);
}

void VerifyOnBusDialog::prefillFromDamos(const ecu::DamosEntry& entry) {
    // Signal : on prend la première quantity (inputQuantity) d'axe non vide.
    QString quantity;
    for (const auto& axis : entry.axes) {
        if (!axis.quantity.empty()) {
            quantity = QString::fromStdString(axis.quantity);
            break;
        }
    }
    if (!quantity.isEmpty()) {
        m_signalEdit->setText(quantity);
    }

    // Unité : depuis DamosDataInfo.unit (valeur de la grandeur) ; sinon depuis
    // l'unité du premier axe.
    QString unit = QString::fromStdString(entry.data.unit);
    if (unit.isEmpty()) {
        for (const auto& axis : entry.axes) {
            if (!axis.unit.empty()) {
                unit = QString::fromStdString(axis.unit);
                break;
            }
        }
    }
    if (m_unitLabel) m_unitLabel->setText(unit);

    // Valeur de référence si le DAMOS porte une valeur physique stock.
    if (entry.stockPhysValue.has_value()) {
        m_targetSpin->setValue(entry.stockPhysValue.value());
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Détection d'interfaces CAN (Linux)
// ───────────────────────────────────────────────────────────────────────────
QStringList VerifyOnBusDialog::detectInterfaces() {
    QStringList out;
    QDir netDir(QStringLiteral("/sys/class/net"));
    if (!netDir.exists()) return out;
    const auto entries = netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : entries) {
        if (name.startsWith(QStringLiteral("can")) ||
            name.startsWith(QStringLiteral("vcan")) ||
            name.startsWith(QStringLiteral("slcan"))) {
            out << name;
        }
    }
    out.sort();
    return out;
}

void VerifyOnBusDialog::refreshInterfaces() {
    const QString current = m_ifaceCombo->currentText();
    m_ifaceCombo->clear();
    const QStringList ifaces = detectInterfaces();
    m_ifaceCombo->addItems(ifaces);
    if (!current.isEmpty()) {
        const int idx = m_ifaceCombo->findText(current);
        if (idx >= 0) m_ifaceCombo->setCurrentIndex(idx);
        else m_ifaceCombo->setCurrentText(current);
    } else if (ifaces.isEmpty()) {
        // Valeur par défaut commode pour les tests virtuels.
        m_ifaceCombo->setCurrentText(QStringLiteral("vcan0"));
    }
}

void VerifyOnBusDialog::pickDbcFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choisir un fichier DBC"), QString(),
        tr("Bases de signaux DBC (*.dbc);;Tous les fichiers (*)"));
    if (!path.isEmpty()) {
        m_dbcEdit->setText(path);
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Journal
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::log(const QString& msg, bool error) {
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString line = QStringLiteral("[%1] %2").arg(ts, msg);
    if (error) {
        m_logView->appendHtml(
            QStringLiteral("<span style='color:#ef4444;'>%1</span>")
                .arg(line.toHtmlEscaped()));
    } else {
        m_logView->appendPlainText(line);
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Orchestration
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::onVerifyClicked() {
    if (m_running) {
        stopVerification(false, tr("Annulé par l'utilisateur."));
        return;
    }
    startVerification();
}

void VerifyOnBusDialog::startVerification() {
    // ── Validation de la saisie ──────────────────────────────────────────────
    const QString iface = m_ifaceCombo->currentText().trimmed();
    if (iface.isEmpty()) {
        log(tr("Aucune interface CAN sélectionnée."), true);
        return;
    }
    if (m_signalEdit->text().trimmed().isEmpty()) {
        log(tr("Le nom du signal est requis."), true);
        return;
    }

    // Résolution de l'exécutable socketspy-mcp via le registre du hub.
    const QString exe = SubProgramRegistry::resolveExec(QStringLiteral("socketspy-mcp"));
    if (exe.isEmpty()) {
        log(tr("Binaire « socketspy-mcp » introuvable. Buildez-le ou "
               "placez-le à côté d'ecu_studio."), true);
        return;
    }

    m_logView->clear();
    m_rxBuffer.clear();
    m_pending.clear();
    m_initialized = false;
    m_running = true;
    m_sampleCount = 0;
    m_nextId = 1;

    m_verifyBtn->setText(tr("Annuler"));
    m_statusLabel->setText(tr("Démarrage de socketspy-mcp…"));

    // Port loopback aléatoire dans la plage éphémère.
    m_port = static_cast<quint16>(
        kPortMin + (QRandomGenerator::global()->bounded(kPortMax - kPortMin)));

    log(tr("Lancement : %1 --tcp %2").arg(QFileInfo(exe).fileName())
            .arg(m_port));

    // ── Sous-processus socketspy-mcp ─────────────────────────────────────────
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::started,
            this, &VerifyOnBusDialog::onProcStarted);
    connect(m_proc, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError e) {
                onProcErrorOccurred(static_cast<int>(e));
            });
    connect(m_proc, &QProcess::readyReadStandardError,
            this, &VerifyOnBusDialog::onProcStderr);
    connect(m_proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (m_running) {
                    stopVerification(
                        false,
                        tr("socketspy-mcp s'est terminé prématurément "
                           "(code %1).").arg(code));
                }
            });

    m_proc->setProgram(exe);
    m_proc->setArguments({QStringLiteral("--tcp"), QString::number(m_port)});
    m_proc->start();

    // ── Timer de timeout global ──────────────────────────────────────────────
    if (!m_overallTimer) {
        m_overallTimer = new QTimer(this);
        m_overallTimer->setSingleShot(true);
        connect(m_overallTimer, &QTimer::timeout,
                this, &VerifyOnBusDialog::onOverallTimeout);
    }
    m_overallTimer->start(m_timeoutSpin->value() * 1000);
}

void VerifyOnBusDialog::stopVerification(bool success, const QString& reason) {
    m_running = false;

    if (m_pollTimer) m_pollTimer->stop();
    if (m_overallTimer) m_overallTimer->stop();

    if (m_socket) {
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_proc) {
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(1000)) {
                m_proc->kill();
                m_proc->waitForFinished(500);
            }
        }
        m_proc->deleteLater();
        m_proc = nullptr;
    }

    if (success) {
        log(tr("✔ VÉRIFICATION RÉUSSIE — %1").arg(reason));
        m_statusLabel->setText(tr("Réussite."));
    } else {
        log(tr("✘ ÉCHEC — %1").arg(reason), true);
        m_statusLabel->setText(tr("Échec."));
    }
    resetUiToIdle();
}

void VerifyOnBusDialog::resetUiToIdle() {
    m_verifyBtn->setText(tr("Vérifier"));
}

// ───────────────────────────────────────────────────────────────────────────
// Cycle de vie du sous-processus
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::onProcStarted() {
    if (!m_running) return;
    log(tr("socketspy-mcp démarré ; connexion TCP à 127.0.0.1:%1…").arg(m_port));
    m_statusLabel->setText(tr("Connexion au serveur MCP…"));

    // Le serveur ouvre son socket d'écoute peu après le démarrage : on tente la
    // connexion, et on relance via le signal d'erreur du socket si besoin.
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected,
            this, &VerifyOnBusDialog::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &VerifyOnBusDialog::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &VerifyOnBusDialog::onSocketError);

    // Petit délai pour laisser le serveur ouvrir son listen() avant de tenter.
    QTimer::singleShot(150, this, [this]() {
        if (m_socket && m_running) {
            m_socket->connectToHost(QHostAddress::LocalHost, m_port);
        }
    });
}

void VerifyOnBusDialog::onProcErrorOccurred(int err) {
    if (!m_running) return;
    const auto e = static_cast<QProcess::ProcessError>(err);
    if (e == QProcess::FailedToStart) {
        stopVerification(false, tr("Impossible de lancer socketspy-mcp : %1")
                                    .arg(m_proc ? m_proc->errorString()
                                                : QString()));
    }
}

void VerifyOnBusDialog::onProcStderr() {
    if (!m_proc) return;
    const QByteArray data = m_proc->readAllStandardError();
    const QString text = QString::fromLocal8Bit(data).trimmed();
    if (!text.isEmpty()) {
        log(tr("[mcp] %1").arg(text));
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Cycle de vie du socket JSON-RPC
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::onSocketConnected() {
    if (!m_running) return;
    log(tr("Connecté au serveur MCP. Poignée de main (initialize)…"));
    m_statusLabel->setText(tr("Handshake MCP…"));

    // Handshake JSON-RPC 2.0 : on envoie « initialize ».
    QJsonObject initParams;
    initParams.insert(QStringLiteral("protocolVersion"),
                      QStringLiteral("2024-11-05"));
    QJsonObject clientInfo;
    clientInfo.insert(QStringLiteral("name"), QStringLiteral("ecu-studio"));
    clientInfo.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    initParams.insert(QStringLiteral("clientInfo"), clientInfo);
    const int id = sendRequest(QStringLiteral("initialize"), initParams);
    m_pending.insert(id, QStringLiteral("initialize"));
}

void VerifyOnBusDialog::onSocketError() {
    if (!m_running || !m_socket) return;
    // En phase de connexion initiale, le serveur peut ne pas encore écouter :
    // on réessaie une fois après un court délai tant que le timeout global le
    // permet.
    if (!m_initialized && m_socket->state() != QAbstractSocket::ConnectedState) {
        log(tr("Connexion refusée, nouvelle tentative…"));
        QTimer::singleShot(250, this, [this]() {
            if (m_socket && m_running &&
                m_socket->state() == QAbstractSocket::UnconnectedState) {
                m_socket->connectToHost(QHostAddress::LocalHost, m_port);
            }
        });
        return;
    }
    stopVerification(false, tr("Erreur de socket : %1")
                                .arg(m_socket->errorString()));
}

void VerifyOnBusDialog::onSocketReadyRead() {
    if (!m_socket) return;
    m_rxBuffer += m_socket->readAll();

    // Cadre line-delimited : on découpe sur '\n', chaque ligne = un objet JSON.
    int nl;
    while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_rxBuffer.left(nl);
        m_rxBuffer.remove(0, nl + 1);
        if (line.trimmed().isEmpty()) continue;

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            log(tr("Réponse JSON invalide : %1").arg(perr.errorString()), true);
            continue;
        }
        handleResponse(doc.object());
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Transport JSON-RPC 2.0
// ───────────────────────────────────────────────────────────────────────────
int VerifyOnBusDialog::sendRequest(const QString& method,
                                   const QJsonObject& params) {
    const int id = m_nextId++;
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), id);
    req.insert(QStringLiteral("method"), method);
    if (!params.isEmpty()) {
        req.insert(QStringLiteral("params"), params);
    }
    const QByteArray payload =
        QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n';
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(payload);
        m_socket->flush();
    }
    return id;
}

int VerifyOnBusDialog::callTool(const QString& toolName,
                                const QJsonObject& arguments) {
    QJsonObject params;
    params.insert(QStringLiteral("name"), toolName);
    params.insert(QStringLiteral("arguments"), arguments);
    return sendRequest(QStringLiteral("tools/call"), params);
}

QJsonObject VerifyOnBusDialog::extractToolPayload(const QJsonObject& result) {
    // Le serveur encapsule le JSON de l'outil dans content[0].text (cf.
    // McpServer::handle_tools_call). On le re-parse en objet.
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    if (content.isEmpty()) return {};
    const QJsonObject first = content.first().toObject();
    const QString text = first.value(QStringLiteral("text")).toString();
    if (text.isEmpty()) return {};
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

void VerifyOnBusDialog::handleResponse(const QJsonObject& response) {
    // Erreur JSON-RPC ?
    if (response.contains(QStringLiteral("error"))) {
        const QJsonObject err = response.value(QStringLiteral("error")).toObject();
        stopVerification(false,
                         tr("Erreur JSON-RPC : %1")
                             .arg(err.value(QStringLiteral("message")).toString()));
        return;
    }

    const int id = response.value(QStringLiteral("id")).toInt(-1);
    const QString kind = m_pending.take(id);
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();

    if (kind == QStringLiteral("initialize")) {
        m_initialized = true;
        log(tr("Handshake MCP OK — démarrage du polling can_monitor "
               "(decoded=true)."));
        m_statusLabel->setText(tr("Polling du bus…"));

        // Machine à états de polling : une requête can_monitor toutes les
        // kPollIntervalMs, jusqu'à satisfaction ou timeout global.
        if (!m_pollTimer) {
            m_pollTimer = new QTimer(this);
            connect(m_pollTimer, &QTimer::timeout,
                    this, &VerifyOnBusDialog::onPollTick);
        }
        // Premier échantillon immédiat puis cadence.
        requestSample();
        m_pollTimer->start(kPollIntervalMs);
        return;
    }

    if (kind == QStringLiteral("can_monitor")) {
        const QJsonObject payload = extractToolPayload(result);
        const QJsonArray frames = payload.value(QStringLiteral("frames")).toArray();
        const QString wanted = m_signalEdit->text().trimmed();

        bool found = false;
        double measured = 0.0;

        // On cherche, parmi les trames décodées, un signal portant le nom voulu.
        for (const QJsonValue& fv : frames) {
            const QJsonObject frame = fv.toObject();
            const QJsonArray sigArr = frame.value(QStringLiteral("signals")).toArray();
            for (const QJsonValue& sv : sigArr) {
                const QJsonObject sig = sv.toObject();
                const QString name = sig.value(QStringLiteral("name")).toString();
                if (name.compare(wanted, Qt::CaseInsensitive) == 0) {
                    measured = sig.value(QStringLiteral("value")).toDouble();
                    found = true;
                }
            }
            if (found) break;
        }

        if (found) {
            const bool passes = valuePasses(measured);
            log(tr("Échantillon #%1 : %2 = %3 %4 — %5")
                    .arg(m_sampleCount)
                    .arg(wanted)
                    .arg(measured, 0, 'f', 3)
                    .arg(m_unitLabel->text())
                    .arg(passes ? tr("OK") : tr("hors critère")));
            if (passes) {
                stopVerification(
                    true,
                    tr("%1 = %2 %3 satisfait « %4 %5 ».")
                        .arg(wanted)
                        .arg(measured, 0, 'f', 3)
                        .arg(m_unitLabel->text())
                        .arg(comparatorSymbol(static_cast<Comparator>(
                            m_cmpCombo->currentData().toInt())))
                        .arg(m_targetSpin->value(), 0, 'f', 3));
                return;
            }
        } else {
            log(tr("Échantillon #%1 : signal « %2 » absent de la fenêtre.")
                    .arg(m_sampleCount).arg(wanted));
        }
        // Sinon, on attend le prochain tick (ou le timeout global).
        return;
    }
}

// ───────────────────────────────────────────────────────────────────────────
// Machine à états de polling
// ───────────────────────────────────────────────────────────────────────────
void VerifyOnBusDialog::onPollTick() {
    if (!m_running || !m_initialized) return;
    requestSample();
}

void VerifyOnBusDialog::requestSample() {
    ++m_sampleCount;

    QJsonObject args;
    args.insert(QStringLiteral("iface"), m_ifaceCombo->currentText().trimmed());
    args.insert(QStringLiteral("duration_ms"), kSampleWindowMs);
    args.insert(QStringLiteral("decoded"), true);
    const QString dbc = m_dbcEdit->text().trimmed();
    if (!dbc.isEmpty()) {
        args.insert(QStringLiteral("dbc_file"), dbc);
    }
    const int id = callTool(QStringLiteral("can_monitor"), args);
    m_pending.insert(id, QStringLiteral("can_monitor"));
}

void VerifyOnBusDialog::onOverallTimeout() {
    if (!m_running) return;
    stopVerification(
        false,
        tr("Timeout de %1 s atteint sans satisfaire le critère "
           "(%2 échantillon(s) examiné(s)).")
            .arg(m_timeoutSpin->value())
            .arg(m_sampleCount));
}

// ───────────────────────────────────────────────────────────────────────────
// Évaluation du critère
// ───────────────────────────────────────────────────────────────────────────
bool VerifyOnBusDialog::valuePasses(double measured) const {
    const auto cmp = static_cast<Comparator>(m_cmpCombo->currentData().toInt());
    const double target = m_targetSpin->value();
    const double tol = m_toleranceSpin->value();
    switch (cmp) {
        case Comparator::GreaterEqual: return measured >= target;
        case Comparator::LessEqual:    return measured <= target;
        case Comparator::ApproxEqual:  return std::abs(measured - target) <= tol;
        case Comparator::Greater:      return measured > target;
        case Comparator::Less:         return measured < target;
        case Comparator::NotEqual:     return std::abs(measured - target) > tol;
    }
    return false;
}

QString VerifyOnBusDialog::comparatorSymbol(Comparator cmp) {
    switch (cmp) {
        case Comparator::GreaterEqual: return QStringLiteral("≥");
        case Comparator::LessEqual:    return QStringLiteral("≤");
        case Comparator::ApproxEqual:  return QStringLiteral("≈");
        case Comparator::Greater:      return QStringLiteral(">");
        case Comparator::Less:         return QStringLiteral("<");
        case Comparator::NotEqual:     return QStringLiteral("≠");
    }
    return QString();
}

} // namespace ecu_studio
