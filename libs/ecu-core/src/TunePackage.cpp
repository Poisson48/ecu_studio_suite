#include "ecu/TunePackage.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace ecu {
namespace {

void put16(QByteArray& b, uint16_t v) {
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}
void put32(QByteArray& b, uint32_t v) {
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff));
    b.append(char((v >> 24) & 0xff));
}

uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint32_t crc32Bytes(const QByteArray& data) {
    return static_cast<uint32_t>(
        ::crc32(0L, reinterpret_cast<const Bytef*>(data.constData()),
                static_cast<uInt>(data.size())));
}

struct ZipEntry {
    QString  path;
    uint16_t method = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localOffset = 0;
};

std::optional<uint32_t> findEOCD(std::span<const uint8_t> buf) {
    const auto sz = static_cast<int64_t>(buf.size());
    const int64_t start = std::max<int64_t>(0, sz - 4 - 65535);
    for (int64_t i = sz - 4; i >= start; --i) {
        if (le32(buf.data() + i) == 0x06054B50u)
            return static_cast<uint32_t>(i);
    }
    return std::nullopt;
}

std::expected<QByteArray, QString> inflateStoredOrDeflate(
    std::span<const uint8_t> payload, uint16_t method, uint32_t uncompSize)
{
    if (method == 0) {
        return QByteArray(reinterpret_cast<const char*>(payload.data()),
                          static_cast<int>(payload.size()));
    }
    if (method != 8)
        return std::unexpected(QStringLiteral("ZIP: méthode de compression non supportée (%1)").arg(method));

    QByteArray out;
    out.resize(static_cast<int>(uncompSize));
    z_stream strm{};
    strm.next_in   = const_cast<Bytef*>(payload.data());
    strm.avail_in  = static_cast<uInt>(payload.size());
    strm.next_out  = reinterpret_cast<Bytef*>(out.data());
    strm.avail_out = static_cast<uInt>(out.size());
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return std::unexpected(QStringLiteral("ZIP: inflateInit2 échoué"));
    const int rc = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (rc != Z_STREAM_END)
        return std::unexpected(QStringLiteral("ZIP: inflate échoué"));
    out.resize(static_cast<int>(strm.total_out));
    return out;
}

std::expected<QByteArray, QString> extractNamed(const QByteArray& zip, const QString& want) {
    const auto* raw = reinterpret_cast<const uint8_t*>(zip.constData());
    const std::span<const uint8_t> buf(raw, static_cast<std::size_t>(zip.size()));
    const auto eocdOff = findEOCD(buf);
    if (!eocdOff)
        return std::unexpected(QStringLiteral(
            "Fichier .ecutune invalide ou tronqué (ZIP EOCD introuvable).\n"
            "Réexporte depuis ECU Studio (Fichier → Exporter pour ECU Drive) "
            "et transfère par USB/adb — pas WhatsApp/SMS."));
    if (*eocdOff + 22u > buf.size())
        return std::unexpected(QStringLiteral(
            "Fichier .ecutune tronqué (ZIP EOCD incomplet). Re-transfère le fichier."));

    const uint32_t cdOffset = le32(buf.data() + *eocdOff + 16);
    const uint32_t cdSize   = le32(buf.data() + *eocdOff + 12);
    if (cdOffset + cdSize > buf.size())
        return std::unexpected(QStringLiteral("ZIP: central directory hors bornes"));

    uint32_t pos = cdOffset;
    while (pos + 46 <= cdOffset + cdSize) {
        const uint8_t* cd = buf.data() + pos;
        if (le32(cd) != 0x02014B50u) break;
        const uint16_t nameLen = le16(cd + 28);
        const uint16_t extraLen = le16(cd + 30);
        const uint16_t commentLen = le16(cd + 32);
        const uint32_t entrySize = 46u + nameLen + extraLen + commentLen;
        if (pos + entrySize > cdOffset + cdSize)
            return std::unexpected(QStringLiteral("ZIP: entrée CD hors bornes"));

        const QString path = QString::fromUtf8(
            reinterpret_cast<const char*>(cd + 46), nameLen);
        ZipEntry e;
        e.path = path;
        e.method = le16(cd + 10);
        e.compressedSize = le32(cd + 20);
        e.uncompressedSize = le32(cd + 24);
        e.localOffset = le32(cd + 42);

        if (path == want) {
            if (e.localOffset + 30u > buf.size())
                return std::unexpected(QStringLiteral("ZIP: local header hors bornes"));
            const uint8_t* lh = buf.data() + e.localOffset;
            if (le32(lh) != 0x04034B50u)
                return std::unexpected(QStringLiteral("ZIP: signature local invalide"));
            const uint16_t lName = le16(lh + 26);
            const uint16_t lExtra = le16(lh + 28);
            const uint32_t dataOff = e.localOffset + 30u + lName + lExtra;
            if (dataOff + e.compressedSize > buf.size())
                return std::unexpected(QStringLiteral("ZIP: payload hors bornes"));
            return inflateStoredOrDeflate(
                std::span<const uint8_t>(buf.data() + dataOff, e.compressedSize),
                e.method, e.uncompressedSize);
        }
        pos += entrySize;
    }
    return std::unexpected(QStringLiteral("ZIP: fichier manquant « %1 »").arg(want));
}

void appendStoreFile(QByteArray& out, QByteArray& central,
                     const QString& name, const QByteArray& data)
{
    const QByteArray nameUtf8 = name.toUtf8();
    const uint32_t crc = crc32Bytes(data);
    const uint32_t sz = static_cast<uint32_t>(data.size());
    const uint32_t localOff = static_cast<uint32_t>(out.size());

    // Local file header
    put32(out, 0x04034B50u);
    put16(out, 20);          // version needed
    put16(out, 0);           // flags
    put16(out, 0);           // method store
    put16(out, 0); put16(out, 0); // time/date
    put32(out, crc);
    put32(out, sz);
    put32(out, sz);
    put16(out, static_cast<uint16_t>(nameUtf8.size()));
    put16(out, 0);           // extra
    out.append(nameUtf8);
    out.append(data);

    // Central directory header
    put32(central, 0x02014B50u);
    put16(central, 20);      // version made by
    put16(central, 20);      // version needed
    put16(central, 0);
    put16(central, 0);
    put16(central, 0); put16(central, 0);
    put32(central, crc);
    put32(central, sz);
    put32(central, sz);
    put16(central, static_cast<uint16_t>(nameUtf8.size()));
    put16(central, 0); put16(central, 0); // extra, comment
    put16(central, 0); put16(central, 0); // disk, attrs
    put32(central, 0);       // external attrs
    put32(central, localOff);
    central.append(nameUtf8);
}

} // namespace

TunePackage TunePackageIo::make(const QByteArray& rom,
                                const QString& ecuId,
                                const QByteArray& recipeJson,
                                const QString& appVersion,
                                const QString& notes) {
    TunePackage pkg;
    pkg.rom = rom;
    pkg.recipeJson = recipeJson;
    pkg.notes = notes;
    pkg.manifest.formatVersion = 1;
    pkg.manifest.ecuId = ecuId;
    pkg.manifest.romMd5 = QString::fromLatin1(
        QCryptographicHash::hash(rom, QCryptographicHash::Md5).toHex());
    pkg.manifest.exportedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    pkg.manifest.appVersion = appVersion;
    pkg.manifest.notes = notes;
    return pkg;
}

std::expected<QByteArray, QString> TunePackageIo::writeZip(const TunePackage& pkg) {
    if (pkg.rom.isEmpty())
        return std::unexpected(QStringLiteral(".ecutune: ROM vide"));
    if (pkg.recipeJson.isEmpty())
        return std::unexpected(QStringLiteral(".ecutune: recipe.json vide"));
    if (pkg.manifest.ecuId.isEmpty())
        return std::unexpected(QStringLiteral(".ecutune: ecuId manquant"));

    QJsonObject man;
    man.insert(QStringLiteral("formatVersion"), pkg.manifest.formatVersion);
    man.insert(QStringLiteral("ecuId"), pkg.manifest.ecuId);
    man.insert(QStringLiteral("romMd5"), pkg.manifest.romMd5.isEmpty()
               ? QString::fromLatin1(QCryptographicHash::hash(
                     pkg.rom, QCryptographicHash::Md5).toHex())
               : pkg.manifest.romMd5);
    man.insert(QStringLiteral("exportedAt"),
               pkg.manifest.exportedAt.isEmpty()
                   ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate)
                   : pkg.manifest.exportedAt);
    man.insert(QStringLiteral("appVersion"), pkg.manifest.appVersion);
    if (!pkg.notes.isEmpty() || !pkg.manifest.notes.isEmpty())
        man.insert(QStringLiteral("notes"),
                   pkg.notes.isEmpty() ? pkg.manifest.notes : pkg.notes);

    const QByteArray manifestBytes = QJsonDocument(man).toJson(QJsonDocument::Indented);

    QByteArray out;
    QByteArray central;
    uint16_t entries = 0;
    appendStoreFile(out, central, QStringLiteral("manifest.json"), manifestBytes); ++entries;
    appendStoreFile(out, central, QStringLiteral("rom.bin"), pkg.rom); ++entries;
    appendStoreFile(out, central, QStringLiteral("recipe.json"), pkg.recipeJson); ++entries;
    const QString notes = pkg.notes.isEmpty() ? pkg.manifest.notes : pkg.notes;
    if (!notes.isEmpty()) {
        appendStoreFile(out, central, QStringLiteral("notes.txt"), notes.toUtf8());
        ++entries;
    }

    const uint32_t cdOffset = static_cast<uint32_t>(out.size());
    out.append(central);
    // EOCD
    put32(out, 0x06054B50u);
    put16(out, 0); put16(out, 0);
    put16(out, entries); put16(out, entries);
    put32(out, static_cast<uint32_t>(central.size()));
    put32(out, cdOffset);
    put16(out, 0); // comment
    return out;
}

std::expected<void, QString> TunePackageIo::writeZipFile(const QString& path,
                                                         const TunePackage& pkg) {
    auto bytes = writeZip(pkg);
    if (!bytes) return std::unexpected(bytes.error());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return std::unexpected(QStringLiteral("Impossible d'écrire %1").arg(path));
    if (f.write(*bytes) != bytes->size())
        return std::unexpected(QStringLiteral("Écriture incomplète : %1").arg(path));
    return {};
}

std::expected<TunePackage, QString> TunePackageIo::readZip(const QByteArray& zipBytes) {
    auto manBytes = extractNamed(zipBytes, QStringLiteral("manifest.json"));
    if (!manBytes) return std::unexpected(manBytes.error());
    auto rom = extractNamed(zipBytes, QStringLiteral("rom.bin"));
    if (!rom) return std::unexpected(rom.error());
    auto recipe = extractNamed(zipBytes, QStringLiteral("recipe.json"));
    if (!recipe) return std::unexpected(recipe.error());

    const QJsonDocument doc = QJsonDocument::fromJson(*manBytes);
    if (!doc.isObject())
        return std::unexpected(QStringLiteral("manifest.json invalide"));
    const QJsonObject o = doc.object();

    TunePackage pkg;
    pkg.rom = *rom;
    pkg.recipeJson = *recipe;
    pkg.manifest.formatVersion = o.value(QStringLiteral("formatVersion")).toInt(1);
    pkg.manifest.ecuId = o.value(QStringLiteral("ecuId")).toString();
    pkg.manifest.romMd5 = o.value(QStringLiteral("romMd5")).toString();
    pkg.manifest.exportedAt = o.value(QStringLiteral("exportedAt")).toString();
    pkg.manifest.appVersion = o.value(QStringLiteral("appVersion")).toString();
    pkg.manifest.notes = o.value(QStringLiteral("notes")).toString();
    pkg.notes = pkg.manifest.notes;

    if (auto notes = extractNamed(zipBytes, QStringLiteral("notes.txt")))
        pkg.notes = QString::fromUtf8(*notes);

    if (pkg.manifest.ecuId.isEmpty())
        return std::unexpected(QStringLiteral("manifest: ecuId manquant"));
    if (pkg.rom.isEmpty())
        return std::unexpected(QStringLiteral("rom.bin vide"));

    const QString md5 = QString::fromLatin1(
        QCryptographicHash::hash(pkg.rom, QCryptographicHash::Md5).toHex());
    if (!pkg.manifest.romMd5.isEmpty() && pkg.manifest.romMd5 != md5)
        return std::unexpected(QStringLiteral("romMd5 mismatch (fichier corrompu ?)"));

    return pkg;
}

std::expected<TunePackage, QString> TunePackageIo::readZipFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Impossible d'ouvrir %1").arg(path));
    return readZip(f.readAll());
}

} // namespace ecu
