#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QNetworkReply;
class QJsonArray;

namespace ecu_drive {

// Mises à jour depuis les Releases GitHub (style ColoCourse) :
// check → notes → download APK → install PackageInstaller (Android).
class Updater : public QObject {
    Q_OBJECT
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
    bool    canInstall() const;
    bool    updateAvailable() const { return m_state == Available; }
    bool    downloading() const { return m_state == Downloading; }
    bool    readyToInstall() const { return m_state == Ready; }

    static bool isNewer(const QString& candidate, const QString& current);
    static QString notesFromBody(const QString& body);
    /** Choisit l'URL de téléchargement APK ecu-drive dans les assets d'une release. */
    static QString pickApkAssetUrl(const QJsonArray& assets);

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

    QNetworkAccessManager   m_net;
    QPointer<QNetworkReply> m_reply;

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
};

} // namespace ecu_drive
