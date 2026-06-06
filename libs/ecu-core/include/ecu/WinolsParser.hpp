#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>
#include <expected>

namespace ecu {

// A single map/calibration table as declared in a WinOLS project export.
struct WinolsMapDef {
    QString  name;
    uint32_t address = 0;
    int      nx      = 1;   // columns
    int      ny      = 1;   // rows (1 for CURVE, >1 for MAP)
};

// Result of a successful parse: the reconstructed ROM image and the list of
// map definitions discovered in the archive metadata (may be empty for raw
// binary / Intel HEX inputs that carry no map metadata).
struct WinolsParseResult {
    QByteArray            rom;
    QString               filename;
    QList<WinolsMapDef>   maps;
    QString               ecu;   // ECU détecté dans l'en-tête (.ols natif), sinon vide
};

// Synchronous parser — the JS original is async only because the unzipper
// library is stream-based; QuaZip/Qt's built-in ZIP support is synchronous.
class WinolsParser {
public:
    // Parses `data` whose original filename (used for extension sniffing) is
    // `filename`. Returns an error string on failure.
    std::expected<WinolsParseResult, QString>
    parse(const QByteArray& data, const QString& filename) const;

private:
    std::expected<WinolsParseResult, QString>
    parseZip(const QByteArray& data, const QString& filename) const;

    // Returns true when the leading bytes look like an Intel HEX record.
    bool looksLikeHex(const QByteArray& data) const;

    // Decodes Intel HEX into a flat binary image, honouring extended-segment
    // (type 0x02) and extended-linear (type 0x04) address records.
    QByteArray parseIntelHex(const QByteArray& data) const;
};

} // namespace ecu
