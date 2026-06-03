#pragma once
#include <QByteArray>
#include <QList>
#include <QString>

namespace ecu_studio {

struct CommitInfo {
    QString sha;
    QString summary;   // première ligne du message + « il y a … »
};

// Toplevel git du répertoire contenant `romPath`, ou "" si hors dépôt.
QString gitToplevelFor(const QString& romPath);

// Les `max` derniers commits du dépôt `repoRoot`.
QList<CommitInfo> gitRecentCommits(const QString& repoRoot, int max = 40);

// `git show <sha>:<relPath>` → octets (vide en cas d'échec).
QByteArray gitBlobAt(const QString& repoRoot, const QString& sha,
                     const QString& relPath);

} // namespace ecu_studio
