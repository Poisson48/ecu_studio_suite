#include "rom_document.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>

namespace ecu_studio {

namespace {
// Écrit une sauvegarde UNE SEULE FOIS : si elle existe déjà, on n'y touche pas
// (l'original est immuable). Renvoie le chemin si la sauvegarde est en place.
QString writeBackupOnce(const QString& backupPath, const QByteArray& bytes) {
    if (QFile::exists(backupPath)) return backupPath;   // déjà préservé — jamais réécrit
    QFile f(backupPath);
    if (!f.open(QIODevice::WriteOnly)) return {};
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    if (!ok) { QFile::remove(backupPath); return {}; }
    return backupPath;
}
} // namespace

RomDocument::RomDocument(QObject* parent) : QObject(parent) {}

bool RomDocument::loadFromFile(const QString& path, bool managed) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    m_rom  = f.readAll();
    m_path = path;
    m_name = QFileInfo(path).fileName();
    m_managed = managed;
    m_baseline      = m_rom;       // snapshot pour mode fantôme
    m_baselineLabel = QStringLiteral("au chargement");
    m_modified = false;

    // Protection de l'original : pour un fichier EXTERNE (non géré), on dépose une
    // copie immuable « <fichier>.orig » à côté, AVANT toute édition. Ainsi, même si
    // la copie de travail est écrasée plus tard, l'original reste récupérable.
    // (Le flux projet est déjà protégé par rom.original.bin.)
    m_originalBackup.clear();
    if (!managed)
        m_originalBackup = writeBackupOnce(path + QStringLiteral(".orig"), m_rom);

    emit modifiedStateChanged(false);
    emit baselineChanged();
    emit romLoaded();
    return true;
}

bool RomDocument::loadFromData(const QByteArray& data, const QString& name) {
    if (data.isEmpty()) return false;
    m_rom  = data;
    m_path.clear();
    m_name = name;
    m_managed = false;
    m_baseline      = m_rom;       // snapshot pour mode fantôme
    m_baselineLabel = QStringLiteral("au chargement");
    m_modified = false;

    // ROM sans fichier source (import WinOLS, lecture MPPS, …) : on préserve quand
    // même l'original dans AppData/originals, indexé par un hash du contenu (donc
    // dédupliqué et jamais écrasé).
    m_originalBackup.clear();
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/originals");
    if (QDir().mkpath(dir)) {
        const QString sha = QString::fromLatin1(
            QCryptographicHash::hash(m_rom, QCryptographicHash::Sha256)
                .toHex().left(8));
        const QString base = QFileInfo(name).completeBaseName();
        const QString backup = QStringLiteral("%1/%2.%3.orig.bin")
                                   .arg(dir, base.isEmpty() ? QStringLiteral("rom") : base, sha);
        m_originalBackup = writeBackupOnce(backup, m_rom);
    }

    emit modifiedStateChanged(false);
    emit baselineChanged();
    emit romLoaded();
    return true;
}

bool RomDocument::saveToFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(m_rom);
    m_path = path;
    m_name = QFileInfo(path).fileName();
    if (m_modified) { m_modified = false; emit modifiedStateChanged(false); }
    return true;
}

void RomDocument::resetBaseline() {
    m_baseline = m_rom;
    m_baselineLabel = QStringLiteral("état courant");
    emit baselineChanged();
}

void RomDocument::setBaselineFromBytes(const QByteArray& bytes, const QString& label) {
    m_baseline = bytes;
    m_baselineLabel = label.isEmpty() ? QStringLiteral("source externe") : label;
    emit baselineChanged();
}

void RomDocument::setEcuId(const QString& id) {
    if (m_ecuId == id) return;
    m_ecuId = id;
    emit ecuChanged(id);
}

void RomDocument::markModified(qsizetype offset, qsizetype length) {
    if (!m_modified) { m_modified = true; emit modifiedStateChanged(true); }
    emit romModified(offset, length);
}

} // namespace ecu_studio
