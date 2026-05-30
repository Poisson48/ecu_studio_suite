#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <expected>
#include <optional>

namespace ecu {

struct ProjectMeta {
    QString   id;
    QString   name;
    QString   ecu;
    QString   description;
    QString   vehicle;
    QString   immat;
    QString   year;
    QDateTime createdAt;
    bool      hasRom = false;
    QString   romName;
    qint64    romSize   = 0;
    QDateTime romImportedAt;
};

struct RomSlotInfo {
    QString   slug;
    qint64    size;
    QDateTime createdAt;
};

struct CreateProjectParams {
    QString name;
    QString ecu;
    QString description;
    QString vehicle;
    QString immat;
    QString year;
};

class ProjectManager {
public:
    explicit ProjectManager(const QString& projectsDir);

    QDir projectDir(const QString& id) const;

    std::expected<QList<ProjectMeta>, QString> list() const;
    std::optional<ProjectMeta>                 get(const QString& id) const;
    std::expected<ProjectMeta, QString>        create(const CreateProjectParams& p);
    std::expected<ProjectMeta, QString>        update(const QString& id, const QJsonObject& fields);
    void                                       remove(const QString& id);

    std::expected<ProjectMeta, QString> importRom(const QString& id,
                                                  const QByteArray& data,
                                                  const QString& originalName);
    std::expected<void, QString>        patchRom(const QString& id,
                                                 qint64 offset,
                                                 const QByteArray& data);
    std::optional<QString>              romPath(const QString& id) const;
    std::optional<QString>              backupPath(const QString& id) const;

    QList<RomSlotInfo>                  listRomSlots(const QString& id) const;
    std::expected<RomSlotInfo, QString> addRomSlot(const QString& id,
                                                   const QByteArray& data,
                                                   const QString& name);
    std::optional<QString>              romSlotPath(const QString& id, const QString& slug) const;
    void                                deleteRomSlot(const QString& id, const QString& slug);

private:
    QString m_dir;

    QDir    slotsDir(const QString& id) const;
    QString sanitizeSlug(const QString& name) const;
    void    ensureSlotsGitignored(const QString& id) const;

    static ProjectMeta   fromJson(const QJsonObject& obj);
    static QJsonObject   toJson(const ProjectMeta& meta);
    bool                 writeMeta(const QString& id, const ProjectMeta& meta) const;
};

} // namespace ecu
