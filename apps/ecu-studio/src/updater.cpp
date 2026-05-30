#include "updater.h"

#include <cerrno>
#include <cstdio>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QVersionNumber>

namespace ecu_studio {

// Nom de l'asset AppImage attaché aux releases (voir release.yml).
static constexpr const char* kAssetName = "ECU_Studio-x86_64.AppImage";

// API GitHub — dernière release publiée du dépôt.
static constexpr const char* kLatestReleaseUrl =
    "https://api.github.com/repos/Poisson48/ecu_studio_suite/releases/latest";

Updater::Updater(QObject* parent) : QObject(parent) {
    m_nam = new QNetworkAccessManager(this);
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    // Charge explicitement les certificats CA du système pour que HTTPS
    // fonctionne dans les environnements AppImage où le backend TLS de Qt
    // pourrait ne pas les trouver automatiquement.
    QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
    auto certs = QSslConfiguration::systemCaCertificates();
    if (!certs.isEmpty()) {
        cfg.setCaCertificates(certs);
        QSslConfiguration::setDefaultConfiguration(cfg);
    }
}

bool Updater::isAppImage() const {
    return !qgetenv("APPIMAGE").isEmpty();
}

void Updater::checkForUpdates() {
    if (m_state != State::Idle) return;

    if (!QSslSocket::supportsSsl()) {
        emit checkError(tr("TLS/SSL indisponible — impossible de contacter le serveur de "
                           "mise à jour. Vérifiez l'installation d'OpenSSL du système."));
        return;
    }

    m_state = State::FetchRelease;

    QNetworkRequest req{QUrl(kLatestReleaseUrl)};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, "ECU-Studio-Updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this,
            [this]() { onReleaseReply(m_reply); });
}

void Updater::cancel() {
    if (m_reply) m_reply->abort();
    m_state = State::Idle;
}

// ── helpers privés ────────────────────────────────────────────────────────────

void Updater::onReleaseReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_reply = nullptr;
    m_state = State::Idle;

    if (reply->error() != QNetworkReply::NoError) {
        emit checkError(tr("Erreur réseau : %1").arg(reply->errorString()));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        emit checkError(tr("Réponse GitHub mal formée."));
        return;
    }
    QJsonObject obj = doc.object();

    // tag_name de la forme "v1.2.3" — on retire le préfixe "v".
    QString tag = obj.value("tag_name").toString();
    m_version = tag.startsWith('v') ? tag.mid(1) : tag;
    if (m_version.isEmpty()) {
        emit checkError(tr("Aucune version publiée trouvée."));
        return;
    }

    // Recherche de l'asset AppImage dans la liste des fichiers attachés.
    m_downloadUrl.clear();
    for (const QJsonValue& a : obj.value("assets").toArray()) {
        QJsonObject asset = a.toObject();
        if (asset.value("name").toString() == QString(kAssetName)) {
            m_downloadUrl = asset.value("browser_download_url").toString();
            break;
        }
    }

    QString current = QString(APP_VERSION);
    if (QVersionNumber::fromString(m_version) > QVersionNumber::fromString(current))
        emit updateAvailable(m_version);
    else
        emit upToDate();
}

void Updater::startDownload() {
    if (m_state != State::Idle || m_version.isEmpty()) return;

    QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (appImagePath.isEmpty()) {
        emit downloadError(tr("Chemin APPIMAGE introuvable."));
        return;
    }
    if (m_downloadUrl.isEmpty()) {
        emit downloadError(tr("Aucune AppImage attachée à la release."));
        return;
    }

    // Stocke le téléchargement dans le même répertoire pour garantir le même
    // système de fichiers (renommage atomique).
    m_tmpPath = QFileInfo(appImagePath).absolutePath() + "/ECU_Studio-update.AppImage.tmp";

    m_state = State::Downloading;
    QNetworkRequest req{QUrl(m_downloadUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ECU-Studio-Updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);

    connect(m_reply, &QNetworkReply::downloadProgress, this,
            &Updater::downloadProgress);
    connect(m_reply, &QNetworkReply::finished, this,
            [this]() { onAppImageReply(m_reply); });
}

void Updater::onAppImageReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_reply = nullptr;
    m_state = State::Idle;

    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(m_tmpPath);
        emit downloadError(tr("Échec du téléchargement : %1").arg(reply->errorString()));
        return;
    }

    QFile tmp(m_tmpPath);
    if (!tmp.open(QIODevice::WriteOnly)) {
        emit downloadError(tr("Impossible d'écrire le fichier temporaire."));
        return;
    }
    tmp.write(reply->readAll());
    tmp.close();

    if (!atomicInstall(m_tmpPath)) {
        QFile::remove(m_tmpPath);
        emit downloadError(tr("Installation échouée — vérifiez les permissions."));
        return;
    }

    emit installReady();
}

bool Updater::atomicInstall(const QString& tmpPath) const {
    QString appImagePath = QString::fromLocal8Bit(qgetenv("APPIMAGE"));
    if (appImagePath.isEmpty()) return false;

    QFile::Permissions exec =
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
        QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;
    QFile::setPermissions(tmpPath, exec);

    // rename() POSIX remplace atomiquement la destination — contrairement à
    // QFile::rename qui refuse d'écraser un fichier existant.
    if (::rename(tmpPath.toLocal8Bit().constData(),
                 appImagePath.toLocal8Bit().constData()) == 0)
        return true;

    // Repli inter-périphériques (EXDEV) : copie dans le même répertoire,
    // puis renommage.
    if (errno != EXDEV) return false;
    QString staged = appImagePath + ".new";
    QFile::remove(staged);
    if (!QFile::copy(tmpPath, staged)) return false;
    QFile::remove(tmpPath);
    QFile::setPermissions(staged, exec);
    return ::rename(staged.toLocal8Bit().constData(),
                    appImagePath.toLocal8Bit().constData()) == 0;
}

}  // namespace ecu_studio
