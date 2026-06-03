#pragma once
// ─── VerifyOnBusDialog ───────────────────────────────────────────────────────
// Dialogue de l'action post-flash « Vérifier sur le bus CAN ».
//
// Après un flash ECU, on veut confirmer qu'une grandeur a bien bougé comme
// attendu sur le bus réel. Ce dialogue :
//   1. lance le serveur `socketspy-mcp` en sous-processus (QProcess) sur un
//      port TCP de loopback (127.0.0.1) — transport « --tcp <port> » défini dans
//      SocketSpy/api/src/mcp/main.cpp ;
//   2. parle le JSON-RPC 2.0 *délimité par lignes* (un objet JSON par ligne,
//      terminé par '\n') sur un QTcpSocket — exactement le cadre attendu par
//      McpServer::serve_tcp ;
//   3. compose les outils MCP existants (`can_monitor` avec decoded=true,
//      interrogé périodiquement par une machine à états pilotée par QTimer, et
//      optionnellement `can_script` pour la boucle de polling) afin de confirmer
//      que le signal a évolué comme attendu.
//
// Quand un RomDocument portant une DamosEntry OpenDAMOS sélectionnée est fourni,
// le champ « signal » est pré-rempli depuis DamosEntry.axes[].quantity
// (inputQuantity) et l'unité depuis DamosDataInfo.unit.
//
// Le dialogue porte un MaturityBadge(Beta) dans sa barre de titre (en-tête).
//
// Dépendances : Qt6::Widgets + Qt6::Network (QTcpSocket) + QProcess +
// QJsonDocument/QJsonObject. Inclut sub_program_registry.h (resolveExec pour
// socketspy-mcp), maturity_badge.h, rom_document.h, ecu/OpenDamos.hpp.

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QHash>
#include <cstdint>

#include "ecu/OpenDamos.hpp"   // ecu::DamosEntry

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPushButton;
class QPlainTextEdit;
class QLabel;
class QProcess;
class QTcpSocket;
class QTimer;

namespace ecu_studio {

class RomDocument;
class MaturityBadge;

// Dialogue modal-léger (non bloquant : il pilote QProcess/QTcpSocket de manière
// asynchrone) pour vérifier la valeur d'un signal CAN après un flash.
class VerifyOnBusDialog : public QDialog {
    Q_OBJECT
public:
    explicit VerifyOnBusDialog(RomDocument* doc = nullptr,
                               QWidget* parent = nullptr);
    ~VerifyOnBusDialog() override;

    // Pré-remplit signal/unité depuis une DamosEntry OpenDAMOS sélectionnée
    // (axes[].quantity → inputQuantity, data.unit → unité). Peut être appelé par
    // l'appelant lorsqu'une entrée est sélectionnée dans l'éditeur DAMOS.
    void prefillFromDamos(const ecu::DamosEntry& entry);

    // Comparateurs disponibles pour confronter la valeur mesurée à la cible.
    enum class Comparator {
        GreaterEqual,  // mesure >= cible
        LessEqual,     // mesure <= cible
        ApproxEqual,   // |mesure - cible| <= tolérance
        Greater,       // mesure >  cible
        Less,          // mesure <  cible
        NotEqual       // mesure != cible (hors tolérance)
    };

private slots:
    // Lance / arrête la séquence de vérification (bouton « Vérifier »).
    void onVerifyClicked();

    // ── Cycle de vie du sous-processus socketspy-mcp ─────────────────────────
    void onProcStarted();
    void onProcErrorOccurred(int /*QProcess::ProcessError*/ err);
    void onProcStderr();

    // ── Cycle de vie du socket TCP JSON-RPC ──────────────────────────────────
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketError();

    // ── Machine à états de polling ───────────────────────────────────────────
    void onPollTick();      // déclenche une requête can_monitor périodique
    void onOverallTimeout(); // expiration du timeout global → échec

private:
    void buildUi();
    void log(const QString& msg, bool error = false);

    // Détecte les interfaces CAN disponibles (can*, vcan*, slcan*) via
    // /sys/class/net ; renvoie une liste vide hors Linux.
    static QStringList detectInterfaces();
    void refreshInterfaces();

    void pickDbcFile();

    // ── Orchestration ────────────────────────────────────────────────────────
    void startVerification();   // valide la saisie, lance le proc + socket
    void stopVerification(bool success, const QString& reason);
    void resetUiToIdle();

    // ── Transport JSON-RPC 2.0 délimité par lignes ───────────────────────────
    // Envoie une requête JSON-RPC ; renvoie l'id alloué (corrélation réponse).
    int sendRequest(const QString& method, const QJsonObject& params);
    // Envoie un appel d'outil MCP (`tools/call` avec name + arguments).
    int callTool(const QString& toolName, const QJsonObject& arguments);
    // Traite une réponse JSON-RPC complète (un objet par ligne).
    void handleResponse(const QJsonObject& response);
    // Extrait le texte du premier bloc « content » d'un résultat tools/call
    // (le serveur encapsule le JSON de l'outil dans content[0].text).
    static QJsonObject extractToolPayload(const QJsonObject& result);

    // Décide si la valeur mesurée satisfait la cible selon le comparateur.
    bool valuePasses(double measured) const;
    static QString comparatorSymbol(Comparator cmp);

    // Émet la requête can_monitor (decoded=true) pour la fenêtre courante.
    void requestSample();

    // ── Données ──────────────────────────────────────────────────────────────
    RomDocument* m_doc{nullptr};

    QProcess*    m_proc{nullptr};
    QTcpSocket*  m_socket{nullptr};
    QTimer*      m_pollTimer{nullptr};
    QTimer*      m_overallTimer{nullptr};
    quint16      m_port{0};

    // Tampon d'octets reçus, découpé sur '\n' (cadre line-delimited).
    QByteArray   m_rxBuffer;
    int          m_nextId{1};

    // Corrélation id → nature de la requête (« initialize » | « can_monitor »).
    QHash<int, QString> m_pending;
    bool         m_initialized{false};
    bool         m_running{false};
    int          m_sampleCount{0};

    // ── Widgets ──────────────────────────────────────────────────────────────
    MaturityBadge*  m_badge{nullptr};
    QComboBox*      m_ifaceCombo{nullptr};
    QPushButton*    m_refreshBtn{nullptr};
    QLineEdit*      m_dbcEdit{nullptr};
    QPushButton*    m_dbcBrowseBtn{nullptr};
    QLineEdit*      m_signalEdit{nullptr};
    QDoubleSpinBox* m_targetSpin{nullptr};
    QComboBox*      m_cmpCombo{nullptr};
    QDoubleSpinBox* m_toleranceSpin{nullptr};
    QSpinBox*       m_timeoutSpin{nullptr};
    QLabel*         m_unitLabel{nullptr};
    QPushButton*    m_verifyBtn{nullptr};
    QPlainTextEdit* m_logView{nullptr};
    QLabel*         m_statusLabel{nullptr};
};

} // namespace ecu_studio
