#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace ecu_studio {

// Télécharge l'AppImage de la dernière release GitHub d'un dépôt « Owner/Name »
// en lisant son release-manifest.json (version / filename / sha256), vérifie le
// SHA-256, puis écrit le binaire (rendu exécutable) à la destination demandée.
// Réutilise le schéma réseau de l'updater (certificats CA système, redirections
// « no less safe »). Évite à l'utilisateur de compiler le sous-programme.
class ToolDownloader : public QObject {
    Q_OBJECT
public:
    explicit ToolDownloader(QObject* parent = nullptr);

    // repo = « Owner/Name ». destPath = chemin final du binaire (rendu +x).
    // Un seul téléchargement à la fois par instance.
    void start(const QString& repo, const QString& destPath);
    bool busy() const { return m_busy; }

signals:
    void progress(qint64 received, qint64 total);
    void finished(const QString& path);
    void failed(const QString& error);

private:
    void onManifest(QNetworkReply* reply);
    void onBinary(QNetworkReply* reply);
    bool verifySha256(const QByteArray& data, const QString& expected) const;
    void fail(const QString& msg);

    QNetworkAccessManager* m_nam{nullptr};
    bool    m_busy{false};
    QString m_repo;
    QString m_dest;
    QString m_version;
    QString m_filename;
    QString m_sha256;
};

} // namespace ecu_studio
