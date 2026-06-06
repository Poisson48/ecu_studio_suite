// DamosImport.cpp — voir DamosImport.hpp. Port C++ de
// scripts/damos_a2l_to_opendamos.py. Aucune exception : toute lecture hors-bornes
// est ignorée et la caractéristique comptée dans `skipped`.

#include "ecu/DamosImport.hpp"

#include <cstdint>
#include <optional>

namespace ecu {

namespace {

// TriCore/PowerPC exposent le même flash à plusieurs adresses miroir
// (0x80xxxxxx caché / 0xa0xxxxxx non caché). On normalise sur les 29 bits de
// poids faible pour que les adresses A2L indexent l'image ROM physique.
constexpr std::uint32_t kAddrMask = 0x1FFFFFFFu;

bool isLittleEndian(const Characteristic& c) {
    return c.byteOrder.contains(QStringLiteral("LITTLE"), Qt::CaseInsensitive)
        || c.byteOrder.contains(QStringLiteral("MSB_LAST"), Qt::CaseInsensitive);
}

// Type A2L textuel ("SWORD", "UBYTE", …) + endianness → DamosDataType.
// nullopt pour les types non entiers (FLOAT32_IEEE, …) qu'on ne sait pas
// représenter en empreinte.
std::optional<DamosDataType> damosTypeFromA2l(const QString& base, bool le) {
    const QString b = base.toUpper();
    if (b == "UBYTE") return DamosDataType::UByte;
    if (b == "SBYTE") return DamosDataType::SByte;
    if (b == "UWORD") return le ? DamosDataType::UWordLE : DamosDataType::UWordBE;
    if (b == "SWORD") return le ? DamosDataType::SWordLE : DamosDataType::SWordBE;
    if (b == "ULONG") return le ? DamosDataType::ULongLE : DamosDataType::ULongBE;
    if (b == "SLONG") return le ? DamosDataType::SLongLE : DamosDataType::SLongBE;
    return std::nullopt;
}

bool isSigned(DamosDataType t) {
    switch (t) {
        case DamosDataType::SByte:
        case DamosDataType::SWordBE: case DamosDataType::SWordLE:
        case DamosDataType::SLongBE: case DamosDataType::SLongLE: return true;
        default: return false;
    }
}

bool isLE(DamosDataType t) {
    switch (t) {
        case DamosDataType::SWordLE: case DamosDataType::UWordLE:
        case DamosDataType::SLongLE: case DamosDataType::ULongLE: return true;
        default: return false;
    }
}

// Lit une valeur typée à l'offset `off`. nullopt si hors-bornes.
std::optional<std::int64_t> readOne(QByteArrayView rom, std::int64_t off,
                                    DamosDataType t) {
    const std::int64_t sz = static_cast<std::int64_t>(damosTypeSize(t));
    if (off < 0 || off + sz > rom.size()) return std::nullopt;
    const auto* p = reinterpret_cast<const std::uint8_t*>(rom.data()) + off;
    std::uint64_t raw = 0;
    if (isLE(t))
        for (std::int64_t i = sz - 1; i >= 0; --i) raw = (raw << 8) | p[i];
    else
        for (std::int64_t i = 0; i < sz; ++i)      raw = (raw << 8) | p[i];

    if (isSigned(t) && sz < 8) {
        const int bits = static_cast<int>(sz * 8);
        const std::uint64_t signBit = std::uint64_t(1) << (bits - 1);
        if (raw & signBit)                         // étend le signe
            raw |= ~((std::uint64_t(1) << bits) - 1);
    }
    return static_cast<std::int64_t>(raw);
}

// Lit `count` valeurs consécutives. nullopt si une seule est hors-bornes.
std::optional<std::vector<std::int64_t>>
readMany(QByteArrayView rom, std::int64_t off, int count, DamosDataType t) {
    const std::int64_t sz = static_cast<std::int64_t>(damosTypeSize(t));
    std::vector<std::int64_t> out;
    out.reserve(count > 0 ? count : 0);
    for (int k = 0; k < count; ++k) {
        auto v = readOne(rom, off + sz * k, t);
        if (!v) return std::nullopt;
        out.push_back(*v);
    }
    return out;
}

DamosType mapType(const QString& t) {
    if (t == "MAP")   return DamosType::Map;
    if (t == "CURVE") return DamosType::Curve;
    if (t == "VALUE") return DamosType::Value;
    return DamosType::Unknown;
}

} // namespace

DamosRecipe damosToOpenDamos(const QList<Characteristic>& chars,
                             QByteArrayView                rom,
                             const QString&                ecuId,
                             DamosImportStats*             stats) {
    DamosRecipe recipe;
    recipe.ecuId = ecuId.toStdString();

    int converted = 0, skipped = 0;
    QStringList warnings;
    auto skip = [&](const Characteristic& c, const QString& why) {
        ++skipped;
        warnings << QStringLiteral("%1 : %2").arg(c.name, why);
    };

    for (const Characteristic& c : chars) {
        const DamosType dt = mapType(c.type);
        if (dt == DamosType::Unknown) { skip(c, QStringLiteral("type non géré (%1)").arg(c.type)); continue; }

        const bool le = isLittleEndian(c);
        const auto fdtOpt = damosTypeFromA2l(c.dataType, le);
        if (!fdtOpt) { skip(c, QStringLiteral("type de données non entier (%1)").arg(c.dataType)); continue; }
        const DamosDataType fdt = *fdtOpt;
        const std::int64_t addr = c.address & kAddrMask;

        DamosEntry e;
        e.name           = c.name.toStdString();
        e.type           = dt;
        e.description    = c.longIdentifier.toStdString();
        e.defaultAddress = QStringLiteral("0x%1").arg(addr, 0, 16).toStdString();
        e.data.dataType  = fdt;
        e.data.factor    = c.factor;
        e.data.offset    = c.offset;
        e.data.unit      = c.unit.toStdString();
        if (dt == DamosType::Map)   { e.dims.nx = c.nx; e.dims.ny = c.ny; }
        if (dt == DamosType::Curve) { e.dims.nx = c.nx; }

        // VALUE : pas d'empreinte (relocalisé par ancre / plage de valeurs).
        if (dt == DamosType::Value) {
            recipe.characteristics.push_back(std::move(e));
            ++converted;
            continue;
        }

        const bool comAxis = !c.axisDefs.isEmpty()
            && c.axisDefs.first().attribute == QStringLiteral("COM_AXIS");

        bool ok = true;
        if (comAxis) {
            // Chaque axe vit dans son propre bloc AXIS_PTS, à son adresse.
            for (int i = 0; i < c.axisDefs.size() && ok; ++i) {
                const A2lAxis& ax = c.axisDefs[i];
                const auto adtOpt = damosTypeFromA2l(ax.dataType, le);
                if (!adtOpt) { skip(c, QStringLiteral("axe non entier (%1)").arg(ax.dataType)); ok = false; break; }
                int count = ax.maxAxisPoints > 0 ? ax.maxAxisPoints
                                                 : (i == 0 ? c.nx : c.ny);
                const std::int64_t aoff = static_cast<std::int64_t>(ax.address) & kAddrMask;
                auto fp = readMany(rom, aoff, count, *adtOpt);
                if (!fp) { skip(c, QStringLiteral("axe COM_AXIS hors ROM @0x%1").arg(aoff, 0, 16)); ok = false; break; }

                DamosAxis da;
                da.dataType    = *adtOpt;
                da.fingerprint = std::move(*fp);
                da.unit        = ax.unit.toStdString();
                da.quantity    = ax.inputQuantity.toStdString();
                da.factor      = ax.factor;
                da.offset      = ax.offset;
                da.address     = aoff;
                e.axes.push_back(std::move(da));
            }
            e.comAxis = true;
        } else {
            // En-tête inline : (nx[,ny]) en champs de la taille de l'élément,
            // puis les valeurs d'axe X puis Y, lues avec le type FNC.
            const std::int64_t sz  = static_cast<std::int64_t>(damosTypeSize(fdt));
            const std::int64_t hdr = (dt == DamosType::Map) ? 2 * sz : sz;
            const std::int64_t xo  = addr + hdr;
            auto x = readMany(rom, xo, c.nx, fdt);
            auto y = (dt == DamosType::Map && c.ny > 0)
                         ? readMany(rom, xo + sz * c.nx, c.ny, fdt)
                         : std::optional<std::vector<std::int64_t>>(std::vector<std::int64_t>{});
            if (!x || !y) { skip(c, QStringLiteral("empreinte inline hors ROM @0x%1").arg(addr, 0, 16)); ok = false; }
            else {
                const int nAxes = (dt == DamosType::Map && c.ny > 0) ? 2 : 1;
                for (int i = 0; i < nAxes; ++i) {
                    DamosAxis da;
                    da.dataType    = fdt;     // inline : lu et rescanné avec le type FNC
                    da.fingerprint = (i == 0) ? *x : *y;
                    if (i < c.axisDefs.size()) {
                        da.unit     = c.axisDefs[i].unit.toStdString();
                        da.quantity = c.axisDefs[i].inputQuantity.toStdString();
                        da.factor   = c.axisDefs[i].factor;
                        da.offset   = c.axisDefs[i].offset;
                    }
                    e.axes.push_back(std::move(da));
                }
            }
        }

        if (!ok) continue;
        recipe.characteristics.push_back(std::move(e));
        ++converted;
    }

    if (stats) { stats->converted = converted; stats->skipped = skipped; stats->warnings = warnings; }
    return recipe;
}

} // namespace ecu
