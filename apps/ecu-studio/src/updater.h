#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

namespace ecu_studio {

// Vérificateur de mises à jour pour les installations AppImage.
//
// Le flux est sécurisé de bout en bout, comme SocketSpy :
//   1. télécharge release-manifest.json (version, filename, sha256) ;
//   2. télécharge release-manifest.json.sig et vérifie la signature Ed25519
//      du manifeste avec la clé publique embarquée ci-dessous ;
//   3. télécharge l'AppImage et vérifie son empreinte SHA-256 contre le
//      manifeste avant l'installation atomique.
//
// Aucun appel réseau n'est effectué tant que checkForUpdates() n'est pas
// explicitement invoqué.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    // Vrai lorsque l'application tourne dans une AppImage (variable APPIMAGE).
    bool isAppImage() const;

    // Étape 1 — télécharge et vérifie le manifeste signé.
    void checkForUpdates();

    // Étape 2 — télécharge l'AppImage, vérifie le SHA-256, puis installe
    // atomiquement. À appeler après confirmation via updateAvailable().
    void startDownload();
    void cancel();

signals:
    void updateAvailable(const QString& version);
    void upToDate();
    void checkError(const QString& msg);
    void downloadProgress(qint64 done, qint64 total);
    void downloadError(const QString& msg);
    void installReady();  // émis après installation atomique — proposer un redémarrage

private:
    enum class State { Idle, FetchManifest, FetchSig, Downloading };

    void onManifestReply(QNetworkReply* reply);
    void onSigReply(QNetworkReply* reply);
    void onAppImageReply(QNetworkReply* reply);

    bool verifySignature(const QByteArray& data, const QByteArray& sig) const;
    bool verifySha256(const QString& path, const QString& expected) const;
    bool atomicInstall(const QString& tmpPath) const;

    QNetworkAccessManager* m_nam{nullptr};
    QNetworkReply*         m_reply{nullptr};
    State                  m_state{State::Idle};

    QByteArray m_manifest;
    QString    m_version;
    QString    m_sha256;
    QString    m_filename;
    QString    m_tmpPath;
};

}  // namespace ecu_studio
