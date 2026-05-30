#include "updater.h"

#include <cerrno>
#include <cstdio>

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QVersionNumber>

namespace ecu_studio {

// Clé publique Ed25519 utilisée pour vérifier release-manifest.json.sig.
// La clé privée correspondante est conservée dans le secret GitHub
// UPDATE_SIGNING_KEY et n'est jamais distribuée.
static constexpr const char* kPublicKeyPem = R"(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAf7RMrzyYCm862/V/QK2D0oAVkW2Gv2MPf6PlaUNkzAE=
-----END PUBLIC KEY-----
)";

static constexpr const char* kManifestUrl =
    "https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/"
    "release-manifest.json";

static constexpr const char* kSigUrl =
    "https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/"
    "release-manifest.json.sig";

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

    m_state = State::FetchManifest;

    QNetworkRequest req{QUrl(kManifestUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ECU-Studio-Updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this,
            [this]() { onManifestReply(m_reply); });
}

void Updater::cancel() {
    if (m_reply) m_reply->abort();
    m_state = State::Idle;
}

// ── helpers privés ────────────────────────────────────────────────────────────

void Updater::onManifestReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        m_state = State::Idle;
        emit checkError(tr("Erreur réseau : %1").arg(reply->errorString()));
        return;
    }

    m_manifest = reply->readAll();
    m_state = State::FetchSig;

    QNetworkRequest req{QUrl(kSigUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "ECU-Studio-Updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this,
            [this]() { onSigReply(m_reply); });
}

void Updater::onSigReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_reply = nullptr;
    m_state = State::Idle;

    if (reply->error() != QNetworkReply::NoError) {
        emit checkError(tr("Erreur réseau : %1").arg(reply->errorString()));
        return;
    }

    QByteArray sig = reply->readAll();

    if (!verifySignature(m_manifest, sig)) {
        emit checkError(tr("Échec de la vérification de signature — mise à jour rejetée."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(m_manifest);
    if (!doc.isObject()) {
        emit checkError(tr("Manifeste de mise à jour mal formé."));
        return;
    }
    QJsonObject obj = doc.object();
    m_version  = obj.value("version").toString();
    m_sha256   = obj.value("sha256").toString();
    m_filename = obj.value("filename").toString();

    if (m_version.isEmpty() || m_sha256.isEmpty() || m_filename.isEmpty()) {
        emit checkError(tr("Manifeste de mise à jour incomplet."));
        return;
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

    // Stocke le téléchargement dans le même répertoire pour garantir le même
    // système de fichiers (renommage atomique).
    m_tmpPath = QFileInfo(appImagePath).absolutePath() + "/ECU_Studio-update.AppImage.tmp";

    QString downloadUrl =
        QString("https://github.com/Poisson48/ecu_studio_suite/releases/download/v%1/%2")
            .arg(m_version, m_filename);

    m_state = State::Downloading;
    QNetworkRequest req{QUrl(downloadUrl)};
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

    if (!verifySha256(m_tmpPath, m_sha256)) {
        QFile::remove(m_tmpPath);
        emit downloadError(tr("Empreinte SHA-256 incorrecte — fichier corrompu."));
        return;
    }

    if (!atomicInstall(m_tmpPath)) {
        QFile::remove(m_tmpPath);
        emit downloadError(tr("Installation échouée — vérifiez les permissions."));
        return;
    }

    emit installReady();
}

// ── crypto ─────────────────────────────────────────────────────────────────────

bool Updater::verifySignature(const QByteArray& data, const QByteArray& sig) const {
    BIO* bio = BIO_new_mem_buf(kPublicKeyPem, -1);
    EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    // Ed25519 : signature « one-shot » via EVP_DigestVerify (digest interne).
    if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
        ok = (EVP_DigestVerify(ctx,
                               reinterpret_cast<const unsigned char*>(sig.constData()),
                               static_cast<std::size_t>(sig.size()),
                               reinterpret_cast<const unsigned char*>(data.constData()),
                               static_cast<std::size_t>(data.size())) == 1);
    }
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}

bool Updater::verifySha256(const QString& path, const QString& expected) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result().toHex().toLower() == expected.toLower().trimmed();
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
