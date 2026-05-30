#pragma once
#include <QString>
#include <QList>
#include <QHash>
#include <optional>
#include <cstdint>

namespace ecu {

// Byte size for each ASAP2 data type. Inline so it stays a single definition
// across translation units (C++17+ inline variable).
inline const QHash<QString, int> A2L_DATA_TYPE_SIZE = {
    {"UBYTE", 1}, {"SBYTE", 1},
    {"UWORD", 2}, {"SWORD", 2},
    {"ULONG", 4}, {"SLONG", 4},
    {"FLOAT32_IEEE", 4},
    {"FLOAT64_IEEE", 8},
    {"A_UINT64", 8}, {"A_INT64", 8},
};

// Resolved description of a single axis of a CURVE/MAP.
struct A2lAxis {
    QString      attribute;      // STD_AXIS | COM_AXIS | FIX_AXIS | RES_AXIS | CURVE_AXIS
    QString      inputQuantity;
    QString      conversion;     // referenced COMPU_METHOD name
    int          maxAxisPoints = 0;
    double       lowerLimit = 0.0;
    double       upperLimit = 0.0;
    QString      axisPtsRef;     // for COM_AXIS

    // Resolved (filled by enrichment).
    uint32_t     address = 0;    // address of the AXIS_PTS data (COM_AXIS)
    QString      dataType = "SWORD";
    int          byteSize = 2;
    float        factor = 1.0f;
    float        offset = 0.0f;
    QString      unit;

    // FIX_AXIS support.
    bool          hasFixAxisPar = false;
    double        fixAxisOffset = 0.0;
    double        fixAxisShift = 0.0;
    int           fixAxisCount = 0;
    QList<double> fixAxisList;   // FIX_AXIS_PAR_LIST values
};

// Public characteristic record. The first block of fields is the legacy API that
// existing callers rely on; the remainder is resolved metadata added by this port.
struct Characteristic {
    QString      name;
    QString      longIdentifier;
    uint32_t     address = 0;
    QString      type;           // VALUE, CURVE, MAP, VAL_BLK, ASCII, CUBOID...
    int          nx = 1, ny = 1;
    float        factor = 1.0f, offset = 0.0f;
    QString      unit;
    QString      axisXName, axisYName;
    QList<float> axisX, axisY;

    // --- Extended resolved metadata ---
    QString      recordLayout;
    QString      conversion;     // referenced COMPU_METHOD name
    double       lowerLimit = 0.0;
    double       upperLimit = 0.0;
    QString      dataType = "SWORD";   // FNC_VALUES data type
    int          byteSize = 2;
    QString      byteOrder = "BIG_ENDIAN";
    QString      bitMask;
    QString      format;
    QString      conversionType;       // RAT_FUNC, IDENTICAL, LINEAR, TAB_VERB...
    QList<A2lAxis> axisDefs;            // 0 (VALUE), 1 (CURVE), 2 (MAP)
};

class A2lParser {
public:
    // Parses the A2L file at `path`. Streams line by line so very large files
    // (50-200 MB) never need to be held in memory in full. Returns false if the
    // file cannot be opened.
    bool parse(const QString& path);

    const QList<Characteristic>& characteristics() const { return m_chars; }
    std::optional<Characteristic> findByAddress(uint32_t address) const;
    std::optional<Characteristic> findByName(const QString& name) const;

private:
    QList<Characteristic> m_chars;
};

} // namespace ecu
