#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QNetworkReply;

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
    QVariantList changelog() const { return m_changelog; }
    QString whatsNewNotes() const { return m_whatsNewNotes; }
    bool    hasWhatsNew() const { return !m_whatsNewNotes.isEmpty(); }
    qreal   progress() const { return m_progress; }
    bool    canInstall() const;
    bool    updateAvailable() const { return m_state == Available; }
    bool    downloading() const { return m_state == Downloading; }
    bool    readyToInstall() const { return m_state == Ready; }

    static bool isNewer(const QString& candidate, const QString& current);
    static QString notesFromBody(const QString& body);

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
    qreal   m_progress = 0.0;
};

} // namespace ecu_drive
