#include "updater.h"
#include "platform.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace ecu_drive {

namespace {

constexpr const char* kReleasesApi =
    "https://api.github.com/repos/Poisson48/ecu_studio_suite/releases?per_page=30";

constexpr const char* kSeenNotesKey = "updater/seenNotesVersion";

#ifndef APP_VERSION
#  define APP_VERSION "0.0.0"
#endif

QString stripV(QString v)
{
    if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
        v.remove(0, 1);
    return v;
}

} // namespace

Updater::Updater(QObject* parent)
    : QObject(parent)
{}

QString Updater::currentVersion() const
{
    return QStringLiteral(APP_VERSION);
}

bool Updater::canInstall() const
{
#ifdef Q_OS_ANDROID
    return true;
#else
    return false;
#endif
}

QString Updater::notesFromBody(const QString& body)
{
    QStringList kept;
    for (const QString& line : body.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed == QStringLiteral("---"))
            break;
        QString clean = line;
        while (clean.startsWith(QLatin1Char('#')))
            clean.remove(0, 1);
        kept << clean.trimmed();
    }
    while (!kept.isEmpty() && kept.last().isEmpty())
        kept.removeLast();
    return kept.join(QLatin1Char('\n')).trimmed();
}

QString Updater::pickApkAssetUrl(const QJsonArray& assets, qint64* sizeOut)
{
    QString apkUrl;
    qint64 apkSize = 0;
    for (const QJsonValue& a : assets) {
        const QJsonObject asset = a.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(QStringLiteral(".apk"), Qt::CaseInsensitive))
            continue;
        const QString url = asset.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty())
            continue;
        const qint64 sz = asset.value(QStringLiteral("size")).toVariant().toLongLong();
        if (apkUrl.isEmpty()
            || name.contains(QStringLiteral("ecu-drive"), Qt::CaseInsensitive)
            || name.contains(QStringLiteral("ecu_drive"), Qt::CaseInsensitive)) {
            apkUrl = url;
            apkSize = sz;
            if (name.contains(QStringLiteral("ecu-drive"), Qt::CaseInsensitive)
                || name.contains(QStringLiteral("ecu_drive"), Qt::CaseInsensitive))
                break;
        }
    }
    if (sizeOut)
        *sizeOut = apkSize;
    return apkUrl;
}

bool Updater::isNewer(const QString& candidate, const QString& current)
{
    const auto parts = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v.remove(0, 1);
        QList<int> out;
        for (const QString& p : v.split(QLatin1Char('.'))) {
            int digits = 0;
            while (digits < p.size() && p.at(digits).isDigit())
                ++digits;
            out << p.left(digits).toInt();
        }
        return out;
    };

    const QList<int> a = parts(candidate);
    const QList<int> b = parts(current);
    if (a.isEmpty())
        return false;

    for (int i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

static QString stripMarkdown(const QString& md)
{
    QString s = md;
    // titres
    s.replace(QRegularExpression(QStringLiteral("^#{1,6}\\s*"), QRegularExpression::MultilineOption), QString());
    // gras/italique
    s.replace(QRegularExpression(QStringLiteral("\\*{1,3}([^*]+)\\*{1,3}")), QStringLiteral("\\1"));
    s.replace(QRegularExpression(QStringLiteral("_{1,3}([^_]+)_{1,3}")), QStringLiteral("\\1"));
    // code inline
    s.replace(QRegularExpression(QStringLiteral("`([^`]*)`")), QStringLiteral("\\1"));
    // liens [text](url)
    s.replace(QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)")), QStringLiteral("\\1"));
    // tirets de liste → bullet
    s.replace(QRegularExpression(QStringLiteral("^[-*+]\\s+"), QRegularExpression::MultilineOption), QStringLiteral("• "));
    // lignes horizontales
    s.replace(QRegularExpression(QStringLiteral("^[-*_]{3,}\\s*$"), QRegularExpression::MultilineOption), QStringLiteral("————"));
    return s.trimmed();
}

QString Updater::formatEntries(const QVariantList& entries)
{
    QStringList blocks;
    for (const QVariant& v : entries) {
        const QVariantMap m = v.toMap();
        const QString ver = m.value(QStringLiteral("version")).toString();
        const QString notes = stripMarkdown(m.value(QStringLiteral("notes")).toString().trimmed());
        if (ver.isEmpty())
            continue;
        if (notes.isEmpty())
            blocks << QStringLiteral("Version %1").arg(ver);
        else
            blocks << QStringLiteral("Version %1\n\n%2").arg(ver, notes);
    }
    return blocks.join(QStringLiteral("\n\n————————————\n\n")).trimmed();
}

void Updater::rebuildDerivedNotes()
{
    const QString current = currentVersion();
    QSettings settings;
    const QString seen = stripV(
        settings.value(QLatin1String(kSeenNotesKey), QString()).toString());

    QVariantList pending;
    QVariantList whatsNew;

    for (const QVariant& v : m_changelog) {
        const QVariantMap m = v.toMap();
        const QString ver = m.value(QStringLiteral("version")).toString();
        if (ver.isEmpty())
            continue;
        if (isNewer(ver, current))
            pending.append(m);
        else if (seen.isEmpty() || isNewer(ver, seen))
            whatsNew.append(m);
    }

    m_releaseNotes = formatEntries(pending);
    if (seen.isEmpty()) {
        settings.setValue(QLatin1String(kSeenNotesKey), current);
        m_whatsNewNotes.clear();
    } else {
        m_whatsNewNotes = formatEntries(whatsNew);
    }
    emit changelogChanged();
}

void Updater::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (s == Available)
        qInfo() << "[Updater] version" << m_latestVersion
                << "disponible (installée :" << currentVersion() << ")";
    else if (s == Failed)
        qWarning() << "[Updater] échec" << m_lastError << m_apkUrl;
    emit stateChanged();
}

void Updater::check()
{
    if (m_state == Checking || m_state == Downloading)
        return;

    m_lastError.clear();
    setState(Checking);

    QNetworkRequest req{ QUrl(QString::fromLatin1(kReleasesApi)) };
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "ECU-Drive");

    QNetworkReply* reply = m_net.get(req);
    m_reply = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            m_lastError = reply->errorString();
            qWarning() << "[Updater] check réseau:" << m_lastError;
            setState(Failed);
            return;
        }

        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        if (arr.isEmpty()) {
            m_lastError = QStringLiteral("Réponse GitHub vide ou invalide.");
            setState(Failed);
            return;
        }

        m_changelog.clear();
        m_apkUrl.clear();
        m_releaseUrl.clear();
        m_latestVersion.clear();

        const QString current = currentVersion();
        QString bestNewer;

        for (const QJsonValue& v : arr) {
            const QJsonObject obj = v.toObject();
            if (obj.value(QStringLiteral("draft")).toBool())
                continue;
            if (obj.value(QStringLiteral("prerelease")).toBool())
                continue;

            const QString tag = obj.value(QStringLiteral("tag_name")).toString();
            const QString ver = stripV(tag);
            if (ver.isEmpty())
                continue;

            const QString notes = notesFromBody(
                obj.value(QStringLiteral("body")).toString());
            const QString published =
                obj.value(QStringLiteral("published_at")).toString();

            QVariantMap entry;
            entry.insert(QStringLiteral("version"), ver);
            entry.insert(QStringLiteral("notes"), notes);
            entry.insert(QStringLiteral("publishedAt"), published);
            m_changelog.append(entry);

            if (isNewer(ver, current)) {
                if (bestNewer.isEmpty() || isNewer(ver, bestNewer)) {
                    bestNewer = ver;
                    m_releaseUrl = obj.value(QStringLiteral("html_url")).toString();
                    m_apkExpectedBytes = 0;
                    m_apkUrl = pickApkAssetUrl(obj.value(QStringLiteral("assets")).toArray(),
                                              &m_apkExpectedBytes);
                }
            }
        }

        rebuildDerivedNotes();

        if (bestNewer.isEmpty()) {
            setState(Idle);
            return;
        }

        m_latestVersion = bestNewer;
        if (m_apkUrl.isEmpty()) {
            m_lastError = QStringLiteral("Release %1 sans APK ecu-drive.").arg(bestNewer);
            qWarning() << "[Updater]" << m_lastError;
        }
        setState(Available);
    });
}

QString Updater::progressLabel() const
{
    const auto mo = [](qint64 b) {
        return QString::number(qreal(b) / (1024.0 * 1024.0), 'f', 1);
    };
    const qint64 sec = m_dlStartedMs > 0
        ? qMax(qint64(0), (QDateTime::currentMSecsSinceEpoch() - m_dlStartedMs) / 1000)
        : 0;

    if (m_bytesTotal > 0 && m_bytesReceived > 0) {
        return QStringLiteral("%1 % (%2 / %3 Mo)")
            .arg(int(m_progress * 100 + 0.5))
            .arg(mo(m_bytesReceived), mo(m_bytesTotal));
    }
    if (m_bytesReceived > 0)
        return QStringLiteral("%1 Mo…").arg(mo(m_bytesReceived));

    // Android / Qt : souvent 0 octet signalé jusqu'à la fin — au moins un chrono.
    if (m_bytesTotal > 0)
        return QStringLiteral("0 / %1 Mo — %2 s…").arg(mo(m_bytesTotal)).arg(sec);
    if (m_state == Downloading)
        return QStringLiteral("%1 s…").arg(sec);
    return QStringLiteral("0 %");
}

bool Updater::progressIndeterminate() const
{
    // Taille GitHub connue → estimation douce (barre déterminée).
    // Sinon → barre indéterminée animée.
    return m_state == Downloading && m_bytesReceived <= 0 && m_bytesTotal <= 0;
}

void Updater::stopDownloadPulse()
{
    if (m_dlPulse)
        m_dlPulse->stop();
}

void Updater::applyDownloadProgress(qint64 received, qint64 total)
{
    if (received > m_bytesReceived)
        m_bytesReceived = received;
    if (total > 0)
        m_bytesTotal = total;
    else if (m_bytesTotal <= 0 && m_apkExpectedBytes > 0)
        m_bytesTotal = m_apkExpectedBytes;

    if (m_bytesTotal > 0 && m_bytesReceived > 0) {
        m_progress = qBound(0.0, qreal(m_bytesReceived) / qreal(m_bytesTotal), 0.99);
    } else if (m_bytesReceived > 0) {
        constexpr qreal kExpect = 40.0 * 1024.0 * 1024.0;
        m_progress = 0.90 * (1.0 - std::exp(-qreal(m_bytesReceived) / kExpect));
    } else if (m_state == Downloading && m_bytesTotal > 0 && m_dlStartedMs > 0) {
        // Estimation douce tant que le stack Android bufferise (évite 0 % figé).
        const qreal expectMs = qMax(10000.0,
            qreal(m_bytesTotal) / (2.0 * 1024.0 * 1024.0) * 1000.0);
        const qreal t = qreal(QDateTime::currentMSecsSinceEpoch() - m_dlStartedMs)
                        / expectMs;
        m_progress = qBound(0.01, 0.85 * (1.0 - std::exp(-1.8 * t)), 0.85);
    } else {
        m_progress = 0.0;
    }
    emit progressChanged();
}

void Updater::flushDownloadIo()
{
    if (m_state != Downloading)
        return;
    if (m_reply && m_dlFile && m_dlFile->isOpen()) {
        if (m_reply->bytesAvailable() > 0)
            m_dlFile->write(m_reply->readAll());

        qint64 total = m_bytesTotal;
        if (total <= 0) {
            const QVariant cl = m_reply->header(QNetworkRequest::ContentLengthHeader);
            if (cl.isValid() && cl.toLongLong() > 0)
                total = cl.toLongLong();
        }
        applyDownloadProgress(m_dlFile->size(), total);
        return;
    }
    // Même sans I/O : avancer l'estimation temps (évite barre figée à 0 %).
    applyDownloadProgress(m_bytesReceived, m_bytesTotal);
}

void Updater::download()
{
    if (m_apkUrl.isEmpty()) {
        m_lastError = QStringLiteral("Pas d'APK dans la release — ouverture GitHub.");
        setState(Failed);
        if (!m_releaseUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_releaseUrl));
        return;
    }
    if (m_state == Downloading)
        return;

    if (m_reply) {
        m_reply->abort();
        m_reply.clear();
    }
    stopDownloadPulse();
    if (m_dlFile) {
        if (m_dlFile->isOpen())
            m_dlFile->close();
        m_dlFile->deleteLater();
        m_dlFile.clear();
    }

    m_lastError.clear();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!QDir().mkpath(dir)) {
        m_lastError = QStringLiteral("Cache inaccessible (%1)").arg(dir);
        setState(Failed);
        return;
    }
    m_apkPath = dir + QStringLiteral("/ecu-drive-") + m_latestVersion
              + QStringLiteral(".apk");
    QFile::remove(m_apkPath);

    QNetworkRequest req{ QUrl(m_apkUrl) };
    req.setRawHeader("User-Agent", "ECU-Drive");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    // HTTP/2 sous Qt Android bufferise souvent tout le corps → progress 0 puis 100.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_progress = 0.0;
    m_bytesReceived = 0;
    m_bytesTotal = m_apkExpectedBytes > 0 ? m_apkExpectedBytes : 0;
    m_dlStartedMs = QDateTime::currentMSecsSinceEpoch();
    emit progressChanged();
    setState(Downloading);

    QNetworkReply* reply = m_net.get(req);
    m_reply = reply;

    auto* out = new QFile(m_apkPath);
    if (!out->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QStringLiteral("Impossible d'écrire %1").arg(m_apkPath);
        delete out;
        reply->abort();
        setState(Failed);
        return;
    }
    m_dlFile = out;

    if (!m_dlPulse) {
        m_dlPulse = new QTimer(this);
        m_dlPulse->setInterval(200);
        connect(m_dlPulse, &QTimer::timeout, this, &Updater::flushDownloadIo);
    }
    m_dlPulse->start();

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() {
        const QVariant cl = reply->header(QNetworkRequest::ContentLengthHeader);
        if (cl.isValid() && cl.toLongLong() > 0)
            applyDownloadProgress(m_bytesReceived, cl.toLongLong());
    });

    connect(reply, &QNetworkReply::readyRead, this, &Updater::flushDownloadIo);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
        // Sur Android received peut rester 0 jusqu'à la fin : on croise avec le fichier.
        const qint64 fileSz = (m_dlFile && m_dlFile->isOpen()) ? m_dlFile->size() : 0;
        applyDownloadProgress(qMax(received, fileSz), total);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        stopDownloadPulse();
        reply->deleteLater();
        if (m_reply == reply)
            m_reply.clear();

        flushDownloadIo();
        if (m_dlFile) {
            if (m_dlFile->isOpen())
                m_dlFile->close();
            m_dlFile->deleteLater();
            m_dlFile.clear();
        }

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_apkPath);
            m_lastError = reply->errorString();
            qWarning() << "[Updater] download:" << m_lastError;
            setState(Failed);
            return;
        }

        const qint64 sz = QFileInfo(m_apkPath).size();
        if (sz < 1024) {
            QFile::remove(m_apkPath);
            m_lastError = QStringLiteral("APK téléchargé invalide (%1 o).").arg(sz);
            setState(Failed);
            return;
        }

        qInfo() << "[Updater] APK prêt" << m_apkPath << sz << "octets";
        m_bytesReceived = sz;
        if (m_bytesTotal <= 0)
            m_bytesTotal = sz;
        m_progress = 1.0;
        emit progressChanged();
        setState(Ready);
    });
}

void Updater::install()
{
    if (canInstall() && m_state == Ready && !m_apkPath.isEmpty()) {
        qInfo() << "[Updater] install APK" << m_apkPath;
        if (platformInstallApk(m_apkPath)) {
            // L'UI système (install / autoriser sources) prend le relais.
            return;
        }
        m_lastError = QStringLiteral(
            "Installation refusée. Autorise « installer des apps » pour ECU Drive "
            "dans les réglages Android, ou ouvre la page GitHub.");
        setState(Failed);
        if (!m_releaseUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_releaseUrl));
        return;
    }

    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
}

void Updater::dismiss()
{
    stopDownloadPulse();
    if (m_reply)
        m_reply->abort();
    if (m_dlFile) {
        if (m_dlFile->isOpen())
            m_dlFile->close();
        m_dlFile->deleteLater();
        m_dlFile.clear();
    }
    setState(Idle);
}

void Updater::acknowledgeNotes()
{
    QSettings settings;
    settings.setValue(QLatin1String(kSeenNotesKey), currentVersion());
    m_whatsNewNotes.clear();
    emit changelogChanged();
}

} // namespace ecu_drive
