#pragma once
//
// TunePackage — format portable `.ecutune` (ZIP) pour ECU Drive / export desktop.
// Contenu :
//   manifest.json  — métadonnées (ecuId, romMd5, version format…)
//   rom.bin        — image ROM
//   recipe.json    — snapshot OpenDAMOS (indépendant du cache recettes)
//   notes.txt      — optionnel
//
#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>

namespace ecu {

struct TunePackageManifest {
    int     formatVersion = 1;
    QString ecuId;
    QString romMd5;
    QString exportedAt;   // ISO-8601
    QString appVersion;
    QString notes;
};

struct TunePackage {
    TunePackageManifest manifest;
    QByteArray          rom;
    QByteArray          recipeJson; // UTF-8 open_damos snapshot
    QString             notes;
};

class TunePackageIo {
public:
    // Écrit un ZIP store-only (.ecutune).
    static std::expected<QByteArray, QString>
    writeZip(const TunePackage& pkg);

    static std::expected<void, QString>
    writeZipFile(const QString& path, const TunePackage& pkg);

    // Lit un ZIP `.ecutune` (store ou deflate).
    static std::expected<TunePackage, QString>
    readZip(const QByteArray& zipBytes);

    static std::expected<TunePackage, QString>
    readZipFile(const QString& path);

    // Construit un package depuis ROM + recipe déjà sérialisée.
    static TunePackage make(const QByteArray& rom,
                            const QString& ecuId,
                            const QByteArray& recipeJson,
                            const QString& appVersion = {},
                            const QString& notes = {});
};

} // namespace ecu
