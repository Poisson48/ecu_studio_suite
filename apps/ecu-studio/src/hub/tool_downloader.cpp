#include "hub/tool_downloader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>

namespace ecu_studio {

namespace {
QNetworkRequest makeRequest(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ECU-Studio-ToolDownloader");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}
} // namespace

ToolDownloader::ToolDownloader(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    // Charge explicitement les CA système (cf. updater) pour fiabiliser HTTPS
    // dans l'AppImage où le backend TLS de Qt peut ne pas les trouver seul.
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    auto certs = QSslConfiguration::systemCaCertificates();
    if (!certs.isEmpty()) {
        cfg.setCaCertificates(certs);
        QSslConfiguration::setDefaultConfiguration(cfg);
    }
}

void ToolDownloader::fail(const QString& msg) {
    m_busy = false;
    emit failed(msg);
}

void ToolDownloader::start(const QString& repo, const QString& destPath) {
    if (m_busy) return;
    if (!QSslSocket::supportsSsl()) {
        emit failed(tr("TLS/SSL indisponible — impossible de contacter GitHub."));
        return;
    }
    m_busy     = true;
    m_repo     = repo;
    m_dest     = destPath;
    m_version.clear();
    m_filename.clear();
    m_sha256.clear();

    const QString manifestUrl =
        QStringLiteral("https://github.com/%1/releases/latest/download/release-manifest.json")
            .arg(repo);
    QNetworkReply* reply = m_nam->get(makeRequest(manifestUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onManifest(reply); });
}

void ToolDownloader::onManifest(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("Erreur réseau (manifeste) : %1").arg(reply->errorString()));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) { fail(tr("Manifeste de release illisible.")); return; }
    const QJsonObject obj = doc.object();
    m_version  = obj.value("version").toString();
    m_filename = obj.value("filename").toString();
    m_sha256   = obj.value("sha256").toString();
    if (m_version.isEmpty() || m_filename.isEmpty()) {
        fail(tr("Manifeste de release incomplet."));
        return;
    }

    const QString url =
        QStringLiteral("https://github.com/%1/releases/download/v%2/%3")
            .arg(m_repo, m_version, m_filename);
    QNetworkReply* bin = m_nam->get(makeRequest(url));
    connect(bin, &QNetworkReply::downloadProgress, this, &ToolDownloader::progress);
    connect(bin, &QNetworkReply::finished, this, [this, bin]() { onBinary(bin); });
}

void ToolDownloader::onBinary(QNetworkReply* reply) {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("Échec du téléchargement : %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    if (!m_sha256.isEmpty() && !verifySha256(data, m_sha256)) {
        fail(tr("Empreinte SHA-256 incorrecte — fichier corrompu."));
        return;
    }

    QFile f(m_dest);
    if (!f.open(QIODevice::WriteOnly)) {
        fail(tr("Écriture impossible : %1").arg(m_dest));
        return;
    }
    f.write(data);
    f.close();
    // Rend le binaire exécutable (AppImage).
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner |
                     QFileDevice::ExeGroup | QFileDevice::ExeOther);

    m_busy = false;
    emit finished(m_dest);
}

bool ToolDownloader::verifySha256(const QByteArray& data, const QString& expected) const {
    const QByteArray got =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return got.toLower() == expected.toLower().trimmed().toUtf8();
}

} // namespace ecu_studio
