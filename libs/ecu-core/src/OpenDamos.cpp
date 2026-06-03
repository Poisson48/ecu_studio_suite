#include "ecu/OpenDamos.hpp"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

using json = nlohmann::json;

namespace ecu {

// ---------------------------------------------------------------------------
// Data type helpers.
// ---------------------------------------------------------------------------

std::size_t damosTypeSize(DamosDataType t) {
    switch (t) {
        case DamosDataType::SByte:
        case DamosDataType::UByte:   return 1;
        case DamosDataType::SWordBE:
        case DamosDataType::UWordBE: return 2;
        case DamosDataType::SLongBE:
        case DamosDataType::ULongBE: return 4;
    }
    return 2;
}

DamosDataType parseDamosDataType(const std::string& s) {
    if (s == "SBYTE")    return DamosDataType::SByte;
    if (s == "UBYTE")    return DamosDataType::UByte;
    if (s == "SWORD_BE") return DamosDataType::SWordBE;
    if (s == "UWORD_BE") return DamosDataType::UWordBE;
    if (s == "SLONG_BE") return DamosDataType::SLongBE;
    if (s == "ULONG_BE") return DamosDataType::ULongBE;
    return DamosDataType::SWordBE;
}

namespace {


// --- JSON parsing helpers --------------------------------------------------

std::string jstr(const json& j, const char* key, const std::string& def = {}) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return def;
}

DamosType parseType(const std::string& s) {
    if (s == "MAP")   return DamosType::Map;
    if (s == "CURVE") return DamosType::Curve;
    if (s == "VALUE") return DamosType::Value;
    return DamosType::Unknown;
}

// Parse "AA BB CC" ou "AABBCC" → bytes. Ignore tout caractère non-hex.
std::vector<std::uint8_t> parseHexBytes(const std::string& s) {
    std::vector<std::uint8_t> out;
    std::string acc;
    acc.reserve(2);
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            acc.push_back(c);
            if (acc.size() == 2) {
                out.push_back(static_cast<std::uint8_t>(std::stoul(acc, nullptr, 16)));
                acc.clear();
            }
        }
    }
    return out;
}


DamosEntry parseEntry(const json& c) {
    DamosEntry e;
    e.name        = jstr(c, "name");
    e.type        = parseType(jstr(c, "type"));
    e.category    = jstr(c, "category");
    e.description = jstr(c, "description");

    // defaultAddress may be a hex string or a number.
    if (c.contains("defaultAddress")) {
        const auto& da = c["defaultAddress"];
        if (da.is_string()) e.defaultAddress = da.get<std::string>();
        else if (da.is_number_integer())
            e.defaultAddress = std::to_string(da.get<std::int64_t>());
    }

    if (c.contains("dims") && c["dims"].is_object()) {
        e.dims.nx = c["dims"].value("nx", 0);
        e.dims.ny = c["dims"].value("ny", 0);
    }

    if (c.contains("axes") && c["axes"].is_array()) {
        for (const auto& a : c["axes"]) {
            DamosAxis ax;
            ax.dataType = parseDamosDataType(jstr(a, "dataType", "SWORD_BE"));
            ax.unit     = jstr(a, "unit");
            ax.quantity = jstr(a, "inputQuantity");
            ax.factor   = a.value("factor", 1.0);
            ax.offset   = a.value("offset", 0.0);
            if (a.contains("fingerprint") && a["fingerprint"].is_array())
                for (const auto& v : a["fingerprint"])
                    if (v.is_number())
                        ax.fingerprint.push_back(v.get<std::int64_t>());
            e.axes.push_back(std::move(ax));
        }
    }

    if (c.contains("data") && c["data"].is_object()) {
        const auto& d = c["data"];
        e.data.dataType = parseDamosDataType(jstr(d, "dataType", "SWORD_BE"));
        e.data.factor   = d.value("factor", 1.0);
        e.data.offset   = d.value("offset", 0.0);
        e.data.unit     = jstr(d, "unit");
    }

    if (c.contains("relocation") && c["relocation"].is_object()) {
        const auto& r = c["relocation"];
        if (r.contains("anchorMap") && r["anchorMap"].is_string())
            e.relocation.anchorMap = r["anchorMap"].get<std::string>();
        if (r.contains("method") && r["method"].is_string())
            e.relocation.method = r["method"].get<std::string>();
        if (r.contains("valueRange") && r["valueRange"].is_array() &&
            r["valueRange"].size() == 2) {
            e.relocation.valueRange =
                std::array<double, 2>{ r["valueRange"][0].get<double>(),
                                       r["valueRange"][1].get<double>() };
        }
        if (r.contains("searchWindow") && r["searchWindow"].is_number())
            e.relocation.searchWindow =
                static_cast<std::size_t>(r["searchWindow"].get<std::int64_t>());
    }

    if (c.contains("stockRawValue") && c["stockRawValue"].is_number())
        e.stockRawValue = c["stockRawValue"].get<std::int64_t>();
    if (c.contains("stockPhysValue") && c["stockPhysValue"].is_number())
        e.stockPhysValue = c["stockPhysValue"].get<double>();

    e.hasStage1 = c.contains("stage1");
    // egrOff peut être un booléen simple ou un objet {recommendedRawValue, note}.
    // Un objet vaut true (présence = applicable).
    if (c.contains("egrOff")) {
        const auto& v = c["egrOff"];
        if (v.is_boolean())     e.egrOff = v.get<bool>();
        else if (v.is_object()) e.egrOff = true;
    }

    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// Recipe loading.
// ---------------------------------------------------------------------------

std::expected<DamosRecipe, std::string>
OpenDamos::parseRecipe(const std::string& text) {
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded())
        return std::unexpected("open_damos: invalid JSON");

    DamosRecipe recipe;
    recipe.ecuId = jstr(doc, "ecu");
    if (recipe.ecuId.empty()) recipe.ecuId = jstr(doc, "ecuId");

    if (!doc.contains("characteristics") || !doc["characteristics"].is_array())
        return std::unexpected("open_damos: missing 'characteristics' array");

    for (const auto& c : doc["characteristics"])
        if (c.is_object())
            recipe.characteristics.push_back(parseEntry(c));

    // Auto-mods optionnels — patches en un clic versionnés avec le recipe.
    if (doc.contains("autoMods") && doc["autoMods"].is_array()) {
        for (const auto& m : doc["autoMods"]) {
            if (!m.is_object()) continue;
            DamosAutoMod a;
            a.id          = jstr(m, "id");
            a.description = jstr(m, "description");
            a.note        = jstr(m, "note");
            const std::string typeStr = jstr(m, "type");
            if      (typeStr == "pattern") a.type = DamosAutoModType::Pattern;
            else if (typeStr == "address") a.type = DamosAutoModType::Address;
            a.search  = parseHexBytes(jstr(m, "search"));
            a.replace = parseHexBytes(jstr(m, "replace"));
            // Pour "address", `bytes` est un alias de `replace`.
            if (a.replace.empty()) a.replace = parseHexBytes(jstr(m, "bytes"));
            a.restore = parseHexBytes(jstr(m, "restore"));
            if (m.contains("address")) {
                const auto& av = m["address"];
                if (av.is_string()) {
                    QString s = QString::fromStdString(av.get<std::string>()).trimmed();
                    if (s.startsWith("0x", Qt::CaseInsensitive)) s = s.mid(2);
                    bool ok = false;
                    const std::uint64_t v = s.toULongLong(&ok, 16);
                    if (ok) a.address = v;
                } else if (av.is_number_integer()) {
                    a.address = static_cast<std::uint64_t>(av.get<std::int64_t>());
                }
            }
            recipe.autoMods.push_back(std::move(a));
        }
    }

    return recipe;
}
std::expected<DamosRecipe, std::string>
OpenDamos::loadRecipe(const QString& ecu, const QString& baseDir) {
    const QString rel = ecu + QStringLiteral("/open_damos.json");

    // Construit la liste des emplacements candidats : explicit baseDir, puis CWD,
    // puis remontées depuis le dossier de l'exécutable. Permet de lancer l'app
    // depuis build_debug/ sans devoir copier les recettes.
    QStringList candidates;
    if (!baseDir.isEmpty())
        candidates << QDir(baseDir).filePath(rel);
    candidates << QDir(QStringLiteral("ressources")).filePath(rel);

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        QDir d(appDir);
        // Remonte jusqu'à 5 niveaux pour trouver ressources/ (build_debug/apps/ecu-studio → racine repo).
        for (int i = 0; i < 6; ++i) {
            candidates << d.filePath(QStringLiteral("ressources/") + rel);
            if (!d.cdUp()) break;
        }
    }

    QString tried;
    for (const QString& p : candidates) {
        QFile f(p);
        if (!f.exists()) { tried += QStringLiteral("\n  - ") + p; continue; }
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return std::unexpected(
                QStringLiteral("open_damos: cannot open %1").arg(p).toStdString());
        const QByteArray data = f.readAll();
        return parseRecipe(data.toStdString());
    }

    return std::unexpected(
        QStringLiteral("open_damos: recipe not found for ECU '%1'. Tried:%2")
            .arg(ecu, tried).toStdString());
}

} // namespace ecu
