#include "ecu/ProjectManager.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QUuid>
#include <algorithm>

namespace ecu {

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

QString newUuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void gitCommit(const QString& dir, const QString& message)
{
    // Fire-and-forget: best effort, never throws, never blocks the caller visibly.
    // We run `git init` + `git add -A` + `git commit` in sequence via QProcess.
    auto run = [&](const QStringList& args) {
        QProcess p;
        p.setWorkingDirectory(dir);
        p.start("git", args);
        p.waitForFinished(5000);
    };
    run({"init"});
    run({"add", "-A"});
    run({"commit", "--allow-empty", "-m", message});
}

} // namespace

// ── ProjectMeta serialisation ─────────────────────────────────────────────────

ProjectMeta ProjectManager::fromJson(const QJsonObject& o)
{
    ProjectMeta m;
    m.id            = o["id"].toString();
    m.name          = o["name"].toString();
    m.ecu           = o["ecu"].toString();
    m.description   = o["description"].toString();
    m.vehicle       = o["vehicle"].toString();
    m.immat         = o["immat"].toString();
    m.year          = o["year"].toString();
    m.createdAt     = QDateTime::fromString(o["createdAt"].toString(), Qt::ISODate);
    m.hasRom        = o["hasRom"].toBool(false);
    m.romName       = o["romName"].toString();
    m.romSize       = static_cast<qint64>(o["romSize"].toDouble(0));
    const QString ts = o["romImportedAt"].toString();
    if (!ts.isEmpty())
        m.romImportedAt = QDateTime::fromString(ts, Qt::ISODate);
    return m;
}

QJsonObject ProjectManager::toJson(const ProjectMeta& m)
{
    QJsonObject o;
    o["id"]          = m.id;
    o["name"]        = m.name;
    o["ecu"]         = m.ecu;
    o["description"] = m.description;
    o["vehicle"]     = m.vehicle;
    o["immat"]       = m.immat;
    o["year"]        = m.year;
    o["createdAt"]   = m.createdAt.toString(Qt::ISODate);
    o["hasRom"]      = m.hasRom;
    o["romName"]     = m.romName;
    o["romSize"]     = static_cast<double>(m.romSize);
    if (m.romImportedAt.isValid())
        o["romImportedAt"] = m.romImportedAt.toString(Qt::ISODate);
    return o;
}

bool ProjectManager::writeMeta(const QString& id, const ProjectMeta& meta) const
{
    QFile f(projectDir(id).filePath("meta.json"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(toJson(meta)).toJson());
    return true;
}

// ── Constructor ───────────────────────────────────────────────────────────────

ProjectManager::ProjectManager(const QString& projectsDir)
    : m_dir(projectsDir)
{
    QDir().mkpath(m_dir);
}

// ── Path helpers ──────────────────────────────────────────────────────────────

QDir ProjectManager::projectDir(const QString& id) const
{
    return QDir(m_dir + "/" + id);
}

QDir ProjectManager::slotsDir(const QString& id) const
{
    return QDir(projectDir(id).filePath("roms"));
}

QString ProjectManager::sanitizeSlug(const QString& name) const
{
    QString s = name.isEmpty() ? "rom" : name;
    // Strip known binary/calibration file extensions before slugifying.
    s.remove(QRegularExpression(R"(\.(bin|hex|ols)$)", QRegularExpression::CaseInsensitiveOption));
    s.replace(QRegularExpression(R"([^a-zA-Z0-9._-]+)"), "-");
    s.remove(QRegularExpression(R"(^-+|-+$)"));
    s = s.left(60);
    return s.isEmpty() ? "rom" : s;
}

void ProjectManager::ensureSlotsGitignored(const QString& id) const
{
    const QString giPath = projectDir(id).filePath(".gitignore");
    constexpr QLatin1StringView kLine{"roms/"};

    QString current;
    {
        QFile f(giPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            current = QString::fromUtf8(f.readAll());
    }

    const QStringList lines = current.split('\n');
    const bool already = std::any_of(lines.cbegin(), lines.cend(),
        [&](const QString& l) { return l.trimmed() == kLine; });

    if (!already) {
        QFile f(giPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream ts(&f);
            if (!current.isEmpty()) {
                QString trimmed = current;
                while (trimmed.endsWith('\n')) trimmed.chop(1);
                ts << trimmed << '\n';
            }
            ts << kLine << '\n';
        }
    }
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

std::expected<QList<ProjectMeta>, QString> ProjectManager::list() const
{
    QDir base(m_dir);
    if (!base.exists())
        return QList<ProjectMeta>{};

    QList<ProjectMeta> result;
    for (const QFileInfo& entry : base.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(entry.filePath() + "/meta.json");
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isNull() || !doc.isObject())
            continue;
        result.append(fromJson(doc.object()));
    }

    std::sort(result.begin(), result.end(), [](const ProjectMeta& a, const ProjectMeta& b) {
        return b.createdAt < a.createdAt;
    });

    return result;
}

std::optional<ProjectMeta> ProjectManager::get(const QString& id) const
{
    QFile f(projectDir(id).filePath("meta.json"));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject())
        return std::nullopt;
    return fromJson(doc.object());
}

std::expected<ProjectMeta, QString> ProjectManager::create(const CreateProjectParams& p)
{
    const QString id  = newUuid();
    const QDir    dir = projectDir(id);

    if (!QDir().mkpath(dir.absolutePath()))
        return std::unexpected(QString("Cannot create project directory: %1").arg(dir.absolutePath()));

    ProjectMeta meta;
    meta.id          = id;
    meta.name        = p.name;
    meta.ecu         = p.ecu;
    meta.description = p.description;
    meta.vehicle     = p.vehicle;
    meta.immat       = p.immat;
    meta.year        = p.year;
    meta.createdAt   = QDateTime::currentDateTimeUtc();
    meta.hasRom      = false;

    if (!writeMeta(id, meta))
        return std::unexpected(QString("Cannot write meta.json for project %1").arg(id));

    gitCommit(dir.absolutePath(), "Initial project");

    return meta;
}

std::expected<ProjectMeta, QString> ProjectManager::update(const QString& id,
                                                           const QJsonObject& fields)
{
    auto existing = get(id);
    if (!existing)
        return std::unexpected(QString("Project not found: %1").arg(id));

    QJsonObject obj = toJson(*existing);
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        obj[it.key()] = it.value();

    ProjectMeta updated = fromJson(obj);
    if (!writeMeta(id, updated))
        return std::unexpected(QString("Cannot write meta.json for project %1").arg(id));

    return updated;
}

void ProjectManager::remove(const QString& id)
{
    QDir(projectDir(id).absolutePath()).removeRecursively();
}

// ── ROM management ────────────────────────────────────────────────────────────

std::expected<ProjectMeta, QString>
ProjectManager::importRom(const QString& id,
                          const QByteArray& data,
                          const QString& originalName)
{
    const QDir dir = projectDir(id);

    auto writeFile = [&](const QString& name, const QByteArray& bytes) -> bool {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(bytes);
        return true;
    };

    if (!writeFile("rom.bin", data))
        return std::unexpected(QString("Cannot write rom.bin for project %1").arg(id));

    // First import only: preserve the factory dump so it can never be overwritten.
    const QString backupFilePath = dir.filePath("rom.original.bin");
    if (!QFile::exists(backupFilePath)) {
        if (!writeFile("rom.original.bin", data))
            return std::unexpected(QString("Cannot write rom.original.bin for project %1").arg(id));
    }

    auto existing = get(id);
    if (!existing)
        return std::unexpected(QString("Project not found: %1").arg(id));

    existing->hasRom       = true;
    existing->romName      = originalName;
    existing->romSize      = static_cast<qint64>(data.size());
    existing->romImportedAt = QDateTime::currentDateTimeUtc();

    if (!writeMeta(id, *existing))
        return std::unexpected(QString("Cannot write meta.json for project %1").arg(id));

    gitCommit(dir.absolutePath(), QString("Import ROM: %1").arg(originalName));

    return *existing;
}

std::expected<void, QString>
ProjectManager::patchRom(const QString& id, qint64 offset, const QByteArray& data)
{
    QFile f(projectDir(id).filePath("rom.bin"));
    if (!f.open(QIODevice::ReadWrite))
        return std::unexpected(QString("Cannot open rom.bin for project %1").arg(id));
    if (!f.seek(offset))
        return std::unexpected(
            QString("Seek to offset %1 failed in rom.bin for project %2").arg(offset).arg(id));
    const qint64 written = f.write(data);
    if (written != data.size())
        return std::unexpected(
            QString("Partial write to rom.bin for project %1: wrote %2 of %3 bytes")
                .arg(id).arg(written).arg(data.size()));
    return {};
}

std::optional<QString> ProjectManager::romPath(const QString& id) const
{
    const QString p = projectDir(id).filePath("rom.bin");
    return QFile::exists(p) ? std::optional{p} : std::nullopt;
}

std::optional<QString> ProjectManager::backupPath(const QString& id) const
{
    const QString p = projectDir(id).filePath("rom.original.bin");
    return QFile::exists(p) ? std::optional{p} : std::nullopt;
}

// ── ROM slots ─────────────────────────────────────────────────────────────────

QList<RomSlotInfo> ProjectManager::listRomSlots(const QString& id) const
{
    const QDir dir = slotsDir(id);
    if (!dir.exists())
        return {};

    QList<RomSlotInfo> result;
    for (const QFileInfo& fi :
         dir.entryInfoList({"*.bin"}, QDir::Files, QDir::NoSort)) {
        RomSlotInfo info;
        info.slug      = fi.completeBaseName();
        info.size      = fi.size();
        info.createdAt = fi.lastModified().toUTC();
        result.append(info);
    }

    std::sort(result.begin(), result.end(), [](const RomSlotInfo& a, const RomSlotInfo& b) {
        return b.createdAt < a.createdAt;
    });

    return result;
}

std::expected<RomSlotInfo, QString>
ProjectManager::addRomSlot(const QString& id,
                           const QByteArray& data,
                           const QString& name)
{
    const QDir dir = slotsDir(id);
    if (!QDir().mkpath(dir.absolutePath()))
        return std::unexpected(QString("Cannot create roms/ directory for project %1").arg(id));

    ensureSlotsGitignored(id);

    const QString baseSlug = sanitizeSlug(name);
    QString slug = baseSlug;
    int n = 1;
    while (QFile::exists(dir.filePath(slug + ".bin")))
        slug = QString("%1-%2").arg(baseSlug).arg(n++);

    QFile f(dir.filePath(slug + ".bin"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return std::unexpected(
            QString("Cannot write ROM slot '%1' for project %2").arg(slug).arg(id));
    f.write(data);

    return RomSlotInfo{slug, static_cast<qint64>(data.size()), QDateTime::currentDateTimeUtc()};
}

std::optional<QString>
ProjectManager::romSlotPath(const QString& id, const QString& slug) const
{
    const QString p = slotsDir(id).filePath(slug + ".bin");
    return QFile::exists(p) ? std::optional{p} : std::nullopt;
}

void ProjectManager::deleteRomSlot(const QString& id, const QString& slug)
{
    const auto p = romSlotPath(id, slug);
    if (p)
        QFile::remove(*p);
}

} // namespace ecu
