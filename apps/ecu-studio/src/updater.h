#pragma once
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

namespace ecu_studio {

// Vérificateur de mises à jour pour les installations AppImage.
// Interroge l'API GitHub Releases de Poisson48/ecu_studio_suite, compare la
// version publiée à APP_VERSION et, si une AppImage plus récente existe,
// propose de la télécharger puis de l'installer de façon atomique.
//
// Aucun appel réseau n'est effectué tant que checkForUpdates() n'est pas
// explicitement invoqué.
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject* parent = nullptr);

    // Vrai lorsque l'application tourne dans une AppImage (variable APPIMAGE).
    bool isAppImage() const;

    // Étape 1 — interroge l'API GitHub pour la dernière release.
    void checkForUpdates();

    // Étape 2 — télécharge l'AppImage puis l'installe atomiquement.
    // À appeler après confirmation de l'utilisateur via updateAvailable().
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
    enum class State { Idle, FetchRelease, Downloading };

    void onReleaseReply(QNetworkReply* reply);
    void onAppImageReply(QNetworkReply* reply);

    bool atomicInstall(const QString& tmpPath) const;

    QNetworkAccessManager* m_nam{nullptr};
    QNetworkReply*         m_reply{nullptr};
    State                  m_state{State::Idle};

    QString m_version;
    QString m_downloadUrl;
    QString m_tmpPath;
};

}  // namespace ecu_studio
