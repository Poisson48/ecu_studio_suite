#include "git_blob_utils.h"

#include <QFileInfo>
#include <QProcess>

namespace ecu_studio {

QString gitToplevelFor(const QString& romPath) {
    if (romPath.isEmpty()) return {};
    QFileInfo fi(romPath);
    QProcess p;
    p.setWorkingDirectory(fi.absolutePath());
    p.start("git", { "rev-parse", "--show-toplevel" });
    if (!p.waitForFinished(2000)) return {};
    if (p.exitCode() != 0) return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

QList<CommitInfo> gitRecentCommits(const QString& repoRoot, int max) {
    QList<CommitInfo> out;
    QProcess p;
    p.setWorkingDirectory(repoRoot);
    p.start("git", { "log", "--pretty=format:%H|%s|%ar",
                     QString("-n%1").arg(max) });
    if (!p.waitForFinished(3000)) return out;
    if (p.exitCode() != 0) return out;
    const QStringList lines = QString::fromUtf8(p.readAllStandardOutput())
                                  .split('\n', Qt::SkipEmptyParts);
    for (const auto& ln : lines) {
        const auto parts = ln.split('|');
        if (parts.size() < 3) continue;
        out.push_back({ parts[0], QString("%1  (%2)").arg(parts[1], parts[2]) });
    }
    return out;
}

QByteArray gitBlobAt(const QString& repoRoot, const QString& sha,
                     const QString& relPath) {
    QProcess p;
    p.setWorkingDirectory(repoRoot);
    p.start("git", { "show", QString("%1:%2").arg(sha, relPath) });
    if (!p.waitForFinished(5000)) return {};
    if (p.exitCode() != 0) return {};
    return p.readAllStandardOutput();
}

} // namespace ecu_studio
