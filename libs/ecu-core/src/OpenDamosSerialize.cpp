// OpenDamosSerialize.cpp — sérialisation d'un recipe open_damos vers JSON et
// écriture disque (serializeRecipe, saveRecipe). Extrait de OpenDamos.cpp.

#include "ecu/OpenDamos.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace ecu {

namespace {

// bytes → "AA BB CC" (séparateur espace pour lisibilité).
std::string formatHexBytes(const std::vector<std::uint8_t>& b) {
    std::string out;
    out.reserve(b.size() * 3);
    char buf[4];
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i) out.push_back(' ');
        std::snprintf(buf, sizeof(buf), "%02X", b[i]);
        out += buf;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Recipe serialization (used by the in-app DAMOS editor).
// ---------------------------------------------------------------------------

namespace {

const char* damosTypeToString(DamosType t) {
    switch (t) {
        case DamosType::Map:   return "MAP";
        case DamosType::Curve: return "CURVE";
        case DamosType::Value: return "VALUE";
        case DamosType::Unknown: break;
    }
    return "VALUE";
}

const char* damosDataTypeToString(DamosDataType t) {
    switch (t) {
        case DamosDataType::SByte:   return "SBYTE";
        case DamosDataType::UByte:   return "UBYTE";
        case DamosDataType::SWordBE: return "SWORD_BE";
        case DamosDataType::UWordBE: return "UWORD_BE";
        case DamosDataType::SLongBE: return "SLONG_BE";
        case DamosDataType::ULongBE: return "ULONG_BE";
    }
    return "SWORD_BE";
}

json entryToJson(const DamosEntry& e) {
    json c = json::object();
    c["name"]        = e.name;
    c["type"]        = damosTypeToString(e.type);
    if (!e.category.empty())    c["category"]    = e.category;
    if (!e.description.empty()) c["description"] = e.description;
    if (!e.defaultAddress.empty()) c["defaultAddress"] = e.defaultAddress;

    if (e.dims.nx > 0 || e.dims.ny > 0) {
        json d = json::object();
        d["nx"] = e.dims.nx;
        if (e.type == DamosType::Map) d["ny"] = e.dims.ny;
        c["dims"] = d;
    }

    if (!e.axes.empty()) {
        json arr = json::array();
        for (const auto& ax : e.axes) {
            json a = json::object();
            a["dataType"] = damosDataTypeToString(ax.dataType);
            if (!ax.unit.empty())     a["unit"]          = ax.unit;
            if (!ax.quantity.empty()) a["inputQuantity"] = ax.quantity;
            if (ax.factor != 1.0)     a["factor"]        = ax.factor;
            if (ax.offset != 0.0)     a["offset"]        = ax.offset;
            if (!ax.fingerprint.empty()) {
                json fp = json::array();
                for (auto v : ax.fingerprint) fp.push_back(v);
                a["fingerprint"] = std::move(fp);
            }
            arr.push_back(std::move(a));
        }
        c["axes"] = std::move(arr);
    }

    {
        json d = json::object();
        d["dataType"] = damosDataTypeToString(e.data.dataType);
        d["factor"]   = e.data.factor;
        d["offset"]   = e.data.offset;
        if (!e.data.unit.empty()) d["unit"] = e.data.unit;
        c["data"] = std::move(d);
    }

    if (e.stockRawValue)  c["stockRawValue"]  = *e.stockRawValue;
    if (e.stockPhysValue) c["stockPhysValue"] = *e.stockPhysValue;
    if (e.hasStage1)      c["stage1"]         = true;
    if (e.egrOff)         c["egrOff"]         = true;

    if (e.relocation.anchorMap || e.relocation.method ||
        e.relocation.valueRange || e.relocation.searchWindow) {
        json r = json::object();
        if (e.relocation.anchorMap)    r["anchorMap"]    = *e.relocation.anchorMap;
        if (e.relocation.method)       r["method"]       = *e.relocation.method;
        if (e.relocation.valueRange)   r["valueRange"]   =
            json::array({ (*e.relocation.valueRange)[0], (*e.relocation.valueRange)[1] });
        if (e.relocation.searchWindow) r["searchWindow"] =
            static_cast<std::int64_t>(*e.relocation.searchWindow);
        c["relocation"] = std::move(r);
    }

    return c;
}

} // namespace

std::string OpenDamos::serializeRecipe(const DamosRecipe& recipe) {
    json doc = json::object();
    doc["$schema"] = "./open_damos.schema.json";
    doc["ecu"]     = recipe.ecuId;
    doc["version"] = "1.0.0";
    doc["license"] = "CC0-1.0";

    json arr = json::array();
    for (const auto& e : recipe.characteristics)
        arr.push_back(entryToJson(e));
    doc["characteristics"] = std::move(arr);

    // Auto-mods : sérialisés seulement s'il y en a (préserve la concision).
    if (!recipe.autoMods.empty()) {
        json mods = json::array();
        for (const auto& a : recipe.autoMods) {
            json m = json::object();
            m["id"] = a.id;
            switch (a.type) {
                case DamosAutoModType::Pattern: m["type"] = "pattern"; break;
                case DamosAutoModType::Address: m["type"] = "address"; break;
                case DamosAutoModType::Unknown: break;
            }
            if (!a.description.empty()) m["description"] = a.description;
            if (!a.note.empty())        m["note"]        = a.note;
            if (a.type == DamosAutoModType::Pattern) {
                if (!a.search.empty())  m["search"]  = formatHexBytes(a.search);
                if (!a.replace.empty()) m["replace"] = formatHexBytes(a.replace);
            } else if (a.type == DamosAutoModType::Address) {
                if (a.address) {
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "0x%06llX",
                                  static_cast<unsigned long long>(*a.address));
                    m["address"] = buf;
                }
                if (!a.replace.empty()) m["bytes"] = formatHexBytes(a.replace);
            }
            if (!a.restore.empty()) m["restore"] = formatHexBytes(a.restore);
            mods.push_back(std::move(m));
        }
        doc["autoMods"] = std::move(mods);
    }

    return doc.dump(2);
}

std::expected<void, std::string>
OpenDamos::saveRecipe(const DamosRecipe& recipe, const QString& path) {
    QFileInfo fi(path);
    QDir().mkpath(fi.absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return std::unexpected(
            QStringLiteral("open_damos: cannot write %1").arg(path).toStdString());

    const std::string text = serializeRecipe(recipe);
    if (f.write(text.data(), static_cast<qint64>(text.size())) !=
        static_cast<qint64>(text.size()))
        return std::unexpected(
            QStringLiteral("open_damos: short write to %1").arg(path).toStdString());
    return {};
}

} // namespace ecu
