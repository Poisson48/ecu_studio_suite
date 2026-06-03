#pragma once
//
// OlsImport — extract the raw ECU ROM image (and a best-effort label catalogue)
// from a WinOLS ".ols" project file.
//
// A .ols is a proprietary, multi-block WinOLS project. It begins with a
// length-prefixed magic ("WinOLS File"), a header carrying the manufacturer /
// ECU / eprom-size metadata, then a long run of map-definition records (the
// DAMOS labels), and finally the raw eprom image as a length-prefixed block.
//
// Strategy (see OlsImport.cpp for details):
//   1. Read the eprom size from the header (an ASCII hex token, e.g. "1EE000").
//   2. The eprom block is the in-bounds block prefixed by that size whose
//      interior contains the *fewest* copies of the size value — the map-record
//      region is riddled with that value (one per record), the binary ROM is
//      not. That uniquely selects the ROM blob.
//
// The OLS 5.x binary record layout that links a label string to its ROM address
// is not publicly documented; olsExtractMaps() therefore recovers label *names*
// reliably but leaves address/dims unresolved (0). Map relocation is done by
// fingerprint scan over the extracted ROM instead (see OpenDamos::relocate).
//
#include <QByteArray>
#include <QString>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace ecu {

// A map/characteristic label declared in a WinOLS project. `name` is reliable;
// `offset`/`nx`/`ny` are best-effort (0 when unresolved for OLS 5.x).
struct OlsMapEntry {
    std::string name;
    std::string description;
    std::size_t offset = 0;
    int         nx     = 0;
    int         ny     = 0;
};

// Extract the raw ECU ROM image from a WinOLS .ols project file on disk.
// Returns an error string on failure (no exceptions are thrown).
std::expected<QByteArray, std::string>
olsExtractRom(const QString& olsPath);

// Same, operating on an in-memory .ols buffer (filename used only in messages).
std::expected<QByteArray, std::string>
olsExtractRom(const QByteArray& ols, const QString& filename = {});

// Best-effort extraction of the label catalogue (DAMOS map names). Reliable for
// `name`; address/dims are left at 0 (see header note above).
std::expected<std::vector<OlsMapEntry>, std::string>
olsExtractMaps(const QString& olsPath);

} // namespace ecu
