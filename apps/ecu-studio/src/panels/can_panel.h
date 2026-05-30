#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <cstdint>

#ifdef ECU_HAVE_CANCORE
#include <QThread>
#include <atomic>
#include "cancore.h"
#endif

class QComboBox;
class QPushButton;
class QTableWidget;
class QLabel;
class QCheckBox;
class QProcess;

namespace ecu_studio {

#ifdef ECU_HAVE_CANCORE
// Thread de capture SocketCAN minimal, bâti sur la lib `cancore` de SocketSpy
// (socketspy::core::can_open / can_close / can_set_fd_mode). Volontairement
// autonome — il n'embarque pas tout le MonitorPanel de SocketSpy, pour ne tirer
// que Qt6::Core + cancore (pas de SerialBus/Charts/Bluetooth).
class CanCaptureThread : public QThread {
    Q_OBJECT
public:
    explicit CanCaptureThread(QString iface, QObject* parent = nullptr);
    ~CanCaptureThread() override;

    void stop();

signals:
    void frameReceived(socketspy::core::CanFrame frame);
    void statsUpdated(quint64 framesPerSec);
    void errorOccurred(QString message);

protected:
    void run() override;

private:
    QString           m_iface;
    std::atomic<bool> m_stop{false};
};
#endif // ECU_HAVE_CANCORE

// Panel « CAN » : moniteur SocketCAN live intégré à ECU Studio.
//
// Permet d'observer le trafic du bus CAN depuis ECU Studio — utile pour
// surveiller le bus pendant un flash ECU. Réutilise la lib `cancore` de
// SocketSpy pour la capture bas niveau.
//
// En complément, un bouton « Ouvrir SocketSpy » lance l'application SocketSpy
// complète (analyse CAN avancée : DBC, UDS, graphes…) en sous-processus, si le
// binaire a été buildé (ECU_BUILD_SOCKETSPY=ON) ou est présent à côté d'ecu_studio.
//
// Dégradation gracieuse : hors Linux (ou sans interface CAN détectée), le panel
// affiche « Aucune interface CAN » et désactive la capture embarquée.
class CanPanel : public QWidget {
    Q_OBJECT
public:
    explicit CanPanel(QWidget* parent = nullptr);
    ~CanPanel() override;

private:
    void buildUi();
    // Liste les interfaces CAN disponibles via /sys/class/net (can*, vcan*, slcan*).
    static QStringList detectInterfaces();
    void refreshInterfaces();

    // Lance l'app SocketSpy complète en sous-processus (résolution de chemin
    // près d'ecu_studio puis dans l'arbre de build).
    void launchSocketSpy();
    // Résout le chemin du binaire socketspy, ou chaîne vide si introuvable.
    static QString resolveSocketSpyPath();

#ifdef ECU_HAVE_CANCORE
    void startCapture();
    void stopCapture();
    void onFrame(const socketspy::core::CanFrame& f);
    void clearFrames();

    CanCaptureThread* m_capture{nullptr};
    // Index de ligne dans la table par CAN ID (mode « agrégé »), pour éviter de
    // remplir la table à l'infini sur un bus chargé.
    QHash<quint32, int> m_rowById;
#endif

    QComboBox*    m_ifaceCombo{nullptr};
    QPushButton*  m_refreshBtn{nullptr};
    QPushButton*  m_startBtn{nullptr};
    QPushButton*  m_stopBtn{nullptr};
    QPushButton*  m_clearBtn{nullptr};
    QPushButton*  m_launchBtn{nullptr};
    QCheckBox*    m_aggregateChk{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel*       m_statusLabel{nullptr};
    QProcess*     m_socketspyProc{nullptr};
};

} // namespace ecu_studio
