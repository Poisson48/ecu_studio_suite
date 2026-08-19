#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QFile;
class QNetworkReply;
class QJsonArray;
class QTimer;

namespace ecu_drive {

// Mises à jour depuis les Releases GitHub (style ColoCourse) :
// check → notes → download APK → install PackageInstaller (Android).
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(State   state           READ state           NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion  READ currentVersion  CONSTANT)
    Q_PROPERTY(QString latestVersion   READ latestVersion   NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes    READ releaseNotes    NOTIFY changelogChanged)
    Q_PROPERTY(QString whatsNewNotes   READ whatsNewNotes   NOTIFY changelogChanged)
    Q_PROPERTY(bool    hasWhatsNew     READ hasWhatsNew     NOTIFY changelogChanged)
    Q_PROPERTY(bool    updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool    downloading     READ downloading     NOTIFY stateChanged)
    Q_PROPERTY(bool    readyToInstall  READ readyToInstall  NOTIFY stateChanged)
    Q_PROPERTY(qreal   progress        READ progress        NOTIFY progressChanged)
    Q_PROPERTY(QString progressLabel   READ progressLabel   NOTIFY progressChanged)
    Q_PROPERTY(QString lastError       READ lastError       NOTIFY stateChanged)
public:
    enum State {
        Idle,
        Checking,
        Available,
        Downloading,
        Ready,
        Failed,
    };
    Q_ENUM(State)

    explicit Updater(QObject* parent = nullptr);

    State   state() const { return m_state; }
    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString lastError() const { return m_lastError; }
    QVariantList changelog() const { return m_changelog; }
    QString whatsNewNotes() const { return m_whatsNewNotes; }
    bool    hasWhatsNew() const { return !m_whatsNewNotes.isEmpty(); }
    qreal   progress() const { return m_progress; }
    qint64  bytesReceived() const { return m_bytesReceived; }
    qint64  bytesTotal() const { return m_bytesTotal; }
    /** Libellé UI : « 42 % » ou « 12,3 Mo… » si taille inconnue (CDN GitHub). */
    QString progressLabel() const;
    /** Vrai tant qu'Android n'a pas encore livré d'octets (barre animée). */
    bool    progressIndeterminate() const;
    bool    canInstall() const;
    bool    updateAvailable() const { return m_state == Available; }
    bool    downloading() const { return m_state == Downloading; }
    bool    readyToInstall() const { return m_state == Ready; }

    static bool isNewer(const QString& candidate, const QString& current);
    static QString notesFromBody(const QString& body);
    /** Choisit l'URL de téléchargement APK ecu-drive dans les assets d'une release.
     *  Si sizeOut != nullptr, remplit la taille déclarée par GitHub (octets). */
    static QString pickApkAssetUrl(const QJsonArray& assets, qint64* sizeOut = nullptr);

public slots:
    void check();
    void download();
    void install();
    void dismiss();
    void acknowledgeNotes();

signals:
    void stateChanged();
    void progressChanged();
    void changelogChanged();

private:
    void setState(State s);
    void rebuildDerivedNotes();
    static QString formatEntries(const QVariantList& entries);
    void applyDownloadProgress(qint64 received, qint64 total);
    void flushDownloadIo();
    void stopDownloadPulse();

    QNetworkAccessManager   m_net;
    QPointer<QNetworkReply> m_reply;
    QPointer<QFile>         m_dlFile;
    QTimer*                 m_dlPulse = nullptr;
    qint64                  m_dlStartedMs = 0;

    State   m_state = Idle;
    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_whatsNewNotes;
    QVariantList m_changelog;
    QString m_apkUrl;
    QString m_releaseUrl;
    QString m_apkPath;
    QString m_lastError;
    qreal   m_progress = 0.0;
    qint64  m_bytesReceived = 0;
    qint64  m_bytesTotal = 0;
    qint64  m_apkExpectedBytes = 0;
};

} // namespace ecu_drive
