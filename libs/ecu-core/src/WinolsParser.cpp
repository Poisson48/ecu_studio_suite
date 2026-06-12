#include "ecu/WinolsParser.hpp"
#include "ecu/OlsImport.hpp"

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <expected>
#include <optional>
#include <span>

namespace ecu {

// ---------------------------------------------------------------------------
// Minimal ZIP parser — no external library.
//
// A ZIP archive ends with an End-of-Central-Directory (EOCD) record
// (signature 0x06054B50).  We scan backwards for it, then walk the Central
// Directory to enumerate entries.  Each entry carries a local-file-header
// offset; we jump there and either memcpy (method 0, stored) or inflate
// (method 8, deflated) the payload.
//
// We deliberately ignore ZIP64 extensions because WinOLS exports are ECU ROM
// images that are always well under 4 GiB.
// ---------------------------------------------------------------------------

namespace {

// Little-endian helpers operating on raw bytes so we never risk UB from
// misaligned casts.
[[nodiscard]] static uint16_t le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
[[nodiscard]] static uint32_t le32(const uint8_t* p) noexcept
{
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) <<  8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// ZIP signatures.
inline constexpr uint32_t kSigLocal  = 0x04034B50u; // local file header
inline constexpr uint32_t kSigCenDir = 0x02014B50u; // central directory header
inline constexpr uint32_t kSigEOCD   = 0x06054B50u; // end of central directory

// Central-directory entry offsets (into the 46-byte fixed part).
inline constexpr int kCdMethod           = 10;
inline constexpr int kCdCompressedSize   = 20;
inline constexpr int kCdUncompressedSize = 24;
inline constexpr int kCdFileNameLen      = 28;
inline constexpr int kCdExtraLen         = 30;
inline constexpr int kCdCommentLen       = 32;
inline constexpr int kCdLocalOffset      = 42;
inline constexpr int kCdFixedSize        = 46;

// Local file-header offsets.
inline constexpr int kLfhFileNameLen = 26;
inline constexpr int kLfhExtraLen    = 28;
inline constexpr int kLfhFixedSize   = 30;

struct ZipEntry {
    QString  path;
    uint16_t method           = 0; // 0=stored, 8=deflated
    uint32_t compressedSize   = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localOffset      = 0; // byte offset of local file header in archive
};

// Locate and validate the End-Of-Central-Directory record.
// Returns the offset of the EOCD record, or nullopt if not found.
[[nodiscard]] static std::optional<uint32_t>
findEOCD(std::span<const uint8_t> buf) noexcept
{
    // The comment field can be up to 65535 bytes, so search that far back.
    const auto sz = static_cast<int64_t>(buf.size());
    const int64_t searchStart = std::max<int64_t>(0, sz - 4 - 65535);

    for (int64_t i = sz - 4; i >= searchStart; --i) {
        if (le32(buf.data() + i) == kSigEOCD)
            return static_cast<uint32_t>(i);
    }
    return std::nullopt;
}

// Enumerate all central-directory entries.
[[nodiscard]] static std::expected<QList<ZipEntry>, QString>
readCentralDirectory(std::span<const uint8_t> buf, uint32_t eocdOffset)
{
    const uint8_t* eocd = buf.data() + eocdOffset;

    // EOCD layout (offsets relative to signature):
    //  4  disk num, 6 start disk, 8 entries on disk, 10 total entries,
    // 12  CD size (4), 16 CD offset (4), 20 comment length (2).
    if (eocdOffset + 22u > buf.size())
        return std::unexpected(u"ZIP EOCD truncated"_qs);

    const uint32_t cdOffset = le32(eocd + 16);
    const uint32_t cdSize   = le32(eocd + 12);

    if (cdOffset + cdSize > buf.size())
        return std::unexpected(u"ZIP central directory out of bounds"_qs);

    QList<ZipEntry> entries;
    uint32_t pos = cdOffset;

    while (pos + kCdFixedSize <= cdOffset + cdSize) {
        const uint8_t* cd = buf.data() + pos;

        if (le32(cd) != kSigCenDir)
            break; // end of valid entries

        const uint16_t nameLen    = le16(cd + kCdFileNameLen);
        const uint16_t extraLen   = le16(cd + kCdExtraLen);
        const uint16_t commentLen = le16(cd + kCdCommentLen);

        const uint32_t entrySize = kCdFixedSize + nameLen + extraLen + commentLen;
        if (pos + entrySize > cdOffset + cdSize)
            return std::unexpected(u"ZIP central directory entry out of bounds"_qs);

        ZipEntry e;
        e.method           = le16(cd + kCdMethod);
        e.compressedSize   = le32(cd + kCdCompressedSize);
        e.uncompressedSize = le32(cd + kCdUncompressedSize);
        e.localOffset      = le32(cd + kCdLocalOffset);
        e.path             = QString::fromUtf8(
            reinterpret_cast<const char*>(cd + kCdFixedSize),
            nameLen);

        entries.append(std::move(e));
        pos += entrySize;
    }

    return entries;
}

// Decompress one entry.  We jump to the local header to find the actual data
// offset (the extra field length can differ from the central-directory copy).
[[nodiscard]] static std::expected<QByteArray, QString>
extractEntry(std::span<const uint8_t> buf, const ZipEntry& entry)
{
    const uint32_t lhStart = entry.localOffset;
    if (lhStart + kLfhFixedSize > buf.size())
        return std::unexpected(u"ZIP local header out of bounds for: "_qs + entry.path);

    const uint8_t* lh = buf.data() + lhStart;
    if (le32(lh) != kSigLocal)
        return std::unexpected(u"ZIP local header signature missing for: "_qs + entry.path);

    const uint16_t lhNameLen  = le16(lh + kLfhFileNameLen);
    const uint16_t lhExtraLen = le16(lh + kLfhExtraLen);
    const uint32_t dataStart  = lhStart + kLfhFixedSize + lhNameLen + lhExtraLen;

    if (dataStart + entry.compressedSize > buf.size())
        return std::unexpected(u"ZIP compressed data out of bounds for: "_qs + entry.path);

    const uint8_t* src = buf.data() + dataStart;

    if (entry.method == 0) { // stored — direct copy
        return QByteArray(reinterpret_cast<const char*>(src),
                          static_cast<qsizetype>(entry.compressedSize));
    }

    if (entry.method == 8) { // deflated
        QByteArray out(static_cast<qsizetype>(entry.uncompressedSize), Qt::Uninitialized);

        z_stream zs{};
        zs.next_in   = const_cast<Bytef*>(src);
        zs.avail_in  = entry.compressedSize;
        zs.next_out  = reinterpret_cast<Bytef*>(out.data());
        zs.avail_out = static_cast<uInt>(entry.uncompressedSize);

        // Negative window-bits: raw deflate (no zlib wrapper), as ZIP requires.
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
            return std::unexpected(u"zlib inflateInit2 failed for: "_qs + entry.path);

        const int ret = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);

        if (ret != Z_STREAM_END)
            return std::unexpected(u"zlib inflate failed for: "_qs + entry.path);

        return out;
    }

    return std::unexpected(
        u"Unsupported ZIP compression method %1 for: "_qs
            .arg(entry.method) + entry.path);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// WinolsParser — public API
// ---------------------------------------------------------------------------

std::expected<WinolsParseResult, QString>
WinolsParser::parse(const QByteArray& data, const QString& filename) const
{
    const auto* bytes = reinterpret_cast<const uint8_t*>(data.constData());

    // Detect ZIP by the PK magic (same check as the JS original).
    if (data.size() >= 2 && bytes[0] == 0x50 && bytes[1] == 0x4B)
        return parseZip(data, filename);

    // Native WinOLS project (.ols): length-prefixed "WinOLS File" magic. Extract
    // the real ROM image (and best-effort map names) via OlsImport — sinon le
    // fallback binaire brut plus bas chargerait tout le conteneur multi-Mo.
    if (data.startsWith(QByteArray("\x0b\x00\x00\x00WinOLS File", 15))) {
        auto romRes = ecu::olsExtractRom(data, filename);
        if (!romRes)
            return std::unexpected(QString::fromStdString(romRes.error()));

        QString outName = filename;
        static const QRegularExpression kOlsSuffix(u"\\.ols$"_qs,
            QRegularExpression::CaseInsensitiveOption);
        outName.remove(kOlsSuffix);
        outName += u".bin"_qs;

        WinolsParseResult res;
        res.rom      = std::move(*romRes);
        res.filename = outName;
        if (auto ecuId = ecu::olsReadEcuId(data)) res.ecu = *ecuId;
        if (auto mapsRes = ecu::olsExtractMaps(data, filename)) {
            for (const auto& m : *mapsRes) {
                WinolsMapDef d;
                d.name    = QString::fromStdString(m.name);
                d.address = static_cast<uint32_t>(m.offset);
                d.nx      = m.nx > 0 ? m.nx : 1;
                d.ny      = m.ny > 0 ? m.ny : 1;
                res.maps.append(d);
            }
        }
        return res;
    }

    const QString ext = filename.toLower();

    // Intel HEX: either the extension says so, or the file starts with ':' and
    // the leading bytes pattern-match a valid record.
    if (ext.endsWith(u".hex"_qs) || (data.size() > 0 && bytes[0] == 0x3A && looksLikeHex(data))) {
        QString outName = filename;
        // Strip .hex suffix — the reconstructed flat binary is a .bin.
        static const QRegularExpression kHexSuffix(u"\\.hex$"_qs,
            QRegularExpression::CaseInsensitiveOption);
        outName.remove(kHexSuffix);
        outName += u".bin"_qs;
        return WinolsParseResult{ parseIntelHex(data), outName, {}, /*ecu*/ {} };
    }

    // Raw binary fallback.
    const QString outName = ext.endsWith(u".bin"_qs) ? filename : filename + u".bin"_qs;
    return WinolsParseResult{ data, outName, {}, /*ecu*/ {} };
}

std::expected<WinolsParseResult, QString>
WinolsParser::parseZip(const QByteArray& data, const QString& /*filename*/) const
{
    const auto buf = std::span(
        reinterpret_cast<const uint8_t*>(data.constData()),
        static_cast<std::size_t>(data.size()));

    const auto eocdOpt = findEOCD(buf);
    if (!eocdOpt)
        return std::unexpected(u"No EOCD record found — not a valid ZIP"_qs);

    auto entriesRes = readCentralDirectory(buf, *eocdOpt);
    if (!entriesRes)
        return std::unexpected(entriesRes.error());

    QList<ZipEntry>& entries = *entriesRes;

    // Filter out directory entries (path ends with '/'), then sort largest
    // uncompressed size first — the dominant binary is the ROM image.
    entries.removeIf([](const ZipEntry& e) { return e.path.endsWith(u'/'); });

    if (entries.isEmpty())
        return std::unexpected(u"No files found in ZIP"_qs);

    std::ranges::sort(entries, [](const ZipEntry& a, const ZipEntry& b) {
        return a.uncompressedSize > b.uncompressedSize;
    });

    const ZipEntry& romEntry = entries.first();
    auto romRes = extractEntry(buf, romEntry);
    if (!romRes)
        return std::unexpected(romRes.error());

    // WinOLS ZIP exports do not embed a machine-readable map list — the map
    // definitions live in the companion .ols XML that is separate from the ROM
    // payload.  We surface an empty list here; callers that also have the XML
    // can populate it via A2lParser / OpenDamos.
    return WinolsParseResult{ std::move(*romRes), romEntry.path, {}, /*ecu*/ {} };
}

bool WinolsParser::looksLikeHex(const QByteArray& data) const
{
    // Sample up to 80 bytes and check for the Intel HEX record pattern:
    //   ':' followed by at least 10 hex digits (2 count + 4 addr + 2 type = 8 min,
    //   but we use 10 to require at least one data byte).
    const QByteArray sample = data.first(std::min(data.size(), qsizetype{80}));
    const QString text = QString::fromLatin1(sample);

    static const QRegularExpression kHexRecord(u"^:[0-9A-Fa-f]{10,}"_qs);
    return kHexRecord.match(text).hasMatch();
}

QByteArray WinolsParser::parseIntelHex(const QByteArray& data) const
{
    struct Segment {
        uint32_t   addr;
        QByteArray payload;
    };

    // Normalise les adresses sur les 29 bits de poids faible : un HEX TriCore/PPC
    // basé en 0x80000000 produirait sinon une image de ~2 Gio (OOM/crash). Garde-fou
    // dur sur la taille pour qu'aucune entrée malformée ne fasse exploser la RAM.
    constexpr uint32_t kAddrMask = 0x1FFFFFFFu;
    constexpr uint32_t kMaxImage = 64u * 1024 * 1024;   // 64 Mio (aucune ROM ECU au-delà)

    const QString text = QString::fromLatin1(data);
    const QStringList lines = text.split(QRegularExpression(u"\\r?\\n"_qs));

    uint32_t maxAddr = 0;
    uint32_t extAddr = 0; // accumulated from type-02 / type-04 records
    QList<Segment> segments;

    for (const QString& line : lines) {
        if (!line.startsWith(u':'))
            continue;

        // Decode the entire line (minus the leading ':') as hex bytes.
        const QByteArray raw = QByteArray::fromHex(line.sliced(1).toLatin1());
        if (raw.size() < 5)
            continue; // trop court : count(1)+addr(2)+type(1)+checksum(1) minimum

        // Valide le checksum Intel HEX : la somme de TOUS les octets (données +
        // octet de checksum) vaut 0 mod 256. Un record corrompu est ignoré —
        // crucial pour ne JAMAIS reconstruire une ROM fausse depuis un HEX abîmé.
        uint8_t sum = 0;
        for (char c : raw) sum += static_cast<uint8_t>(c);
        if (sum != 0)
            continue;

        const auto* b = reinterpret_cast<const uint8_t*>(raw.constData());
        const uint8_t  count = b[0];
        const uint16_t addr  = static_cast<uint16_t>((b[1] << 8) | b[2]);
        const uint8_t  type  = b[3];

        if (type == 0x00) { // Data record
            if (raw.size() < 5 + count)
                continue; // 4 octets d'en-tête + count données + 1 checksum
            const uint32_t fullAddr = (extAddr + addr) & kAddrMask;
            if (fullAddr > kMaxImage || fullAddr + count > kMaxImage)
                continue; // hors garde-fou : on n'alloue jamais au-delà
            segments.append(Segment{ fullAddr, raw.sliced(4, count) });
            maxAddr = std::max(maxAddr, fullAddr + count);

        } else if (type == 0x02) { // Extended segment address (bits 19:4)
            extAddr = static_cast<uint32_t>((b[4] << 8) | b[5]) << 4;

        } else if (type == 0x04) { // Extended linear address (upper 16 bits)
            extAddr = static_cast<uint32_t>((b[4] << 8) | b[5]) << 16;

        } else if (type == 0x01) { // End-of-file record
            break;
        }
        // type 0x03 (start segment) and 0x05 (start linear) are ignored —
        // they carry the CPU entry point, not memory content.
    }

    // Build a flat image filled with 0xFF (unprogrammed flash default) then
    // overlay every data segment at its resolved address — chaque copie est bornée.
    QByteArray rom(static_cast<qsizetype>(maxAddr), static_cast<char>(0xFF));
    for (const Segment& seg : segments) {
        if (seg.addr >= static_cast<uint32_t>(rom.size()))
            continue;
        const qsizetype n =
            std::min<qsizetype>(seg.payload.size(), rom.size() - seg.addr);
        std::memcpy(rom.data() + seg.addr, seg.payload.constData(),
                    static_cast<std::size_t>(n));
    }

    return rom;
}

} // namespace ecu
