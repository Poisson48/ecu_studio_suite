// tools_damos.cpp — outils MCP d'édition de recipe open_damos pilotable par
// l'IA : lecture/écriture/ajout/màj/suppression de characteristics, capture
// d'empreinte depuis une ROM, et gestion des auto-mods (patches en un clic).

#include "tools_internal.h"

#include "ecu/OpenDamos.hpp"
#include "ecu/RomPatcher.hpp"

#include <QFile>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ecu::mcp {

namespace {

// ── Helpers chemin / chargement / sauvegarde de recipe ────────────────────────

// Résout le chemin disque pour un ECU + recipe. Si "path" est fourni, on l'utilise
// tel quel ; sinon on construit ressources/<ecu>/open_damos.json relatif au CWD.
QString resolveDamosPath(const json& p) {
    if (p.contains("path") && p["path"].is_string())
        return QString::fromStdString(p["path"].get<std::string>());
    const std::string ecuId = requireString(p, "ecu_id");
    return QString("ressources/%1/open_damos.json").arg(QString::fromStdString(ecuId));
}

// Charge un recipe depuis un chemin direct.
ecu::DamosRecipe loadRecipeFromPath(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("recipe introuvable : " + path.toStdString());
    auto parsed = ecu::OpenDamos::parseRecipe(f.readAll().toStdString());
    if (!parsed) throw std::runtime_error("recipe invalide : " + parsed.error());
    return std::move(*parsed);
}

void saveRecipeToPath(const ecu::DamosRecipe& r, const QString& path) {
    auto res = ecu::OpenDamos::saveRecipe(r, path);
    if (!res) throw std::runtime_error("écriture recipe impossible : " + res.error());
}

ecu::DamosType parseTypeStr(const std::string& s) {
    if (s == "MAP")   return ecu::DamosType::Map;
    if (s == "CURVE") return ecu::DamosType::Curve;
    if (s == "VALUE") return ecu::DamosType::Value;
    return ecu::DamosType::Unknown;
}

ecu::DamosDataType parseDataTypeStr(const std::string& s) {
    if (s == "SBYTE")    return ecu::DamosDataType::SByte;
    if (s == "UBYTE")    return ecu::DamosDataType::UByte;
    if (s == "SWORD_BE") return ecu::DamosDataType::SWordBE;
    if (s == "UWORD_BE") return ecu::DamosDataType::UWordBE;
    if (s == "SLONG_BE") return ecu::DamosDataType::SLongBE;
    if (s == "ULONG_BE") return ecu::DamosDataType::ULongBE;
    return ecu::DamosDataType::SWordBE;
}

// Convertit un payload JSON en DamosEntry.
ecu::DamosEntry entryFromJson(const json& j) {
    ecu::DamosEntry e;
    if (j.contains("name") && j["name"].is_string())
        e.name = j["name"].get<std::string>();
    if (j.contains("type") && j["type"].is_string())
        e.type = parseTypeStr(j["type"].get<std::string>());
    if (j.contains("category") && j["category"].is_string())
        e.category = j["category"].get<std::string>();
    if (j.contains("description") && j["description"].is_string())
        e.description = j["description"].get<std::string>();
    if (j.contains("defaultAddress")) {
        const auto& v = j["defaultAddress"];
        if (v.is_string())   e.defaultAddress = v.get<std::string>();
        else if (v.is_number_integer())
            e.defaultAddress = std::to_string(v.get<std::int64_t>());
    }
    if (j.contains("dims") && j["dims"].is_object()) {
        e.dims.nx = j["dims"].value("nx", 0);
        e.dims.ny = j["dims"].value("ny", 0);
    }
    if (j.contains("axes") && j["axes"].is_array()) {
        for (const auto& a : j["axes"]) {
            ecu::DamosAxis ax;
            if (a.contains("dataType") && a["dataType"].is_string())
                ax.dataType = parseDataTypeStr(a["dataType"].get<std::string>());
            if (a.contains("unit") && a["unit"].is_string())
                ax.unit = a["unit"].get<std::string>();
            if (a.contains("inputQuantity") && a["inputQuantity"].is_string())
                ax.quantity = a["inputQuantity"].get<std::string>();
            ax.factor = a.value("factor", 1.0);
            ax.offset = a.value("offset", 0.0);
            if (a.contains("fingerprint") && a["fingerprint"].is_array())
                for (const auto& v : a["fingerprint"])
                    if (v.is_number())
                        ax.fingerprint.push_back(v.get<std::int64_t>());
            e.axes.push_back(std::move(ax));
        }
    }
    if (j.contains("data") && j["data"].is_object()) {
        const auto& d = j["data"];
        if (d.contains("dataType") && d["dataType"].is_string())
            e.data.dataType = parseDataTypeStr(d["dataType"].get<std::string>());
        e.data.factor = d.value("factor", 1.0);
        e.data.offset = d.value("offset", 0.0);
        if (d.contains("unit") && d["unit"].is_string())
            e.data.unit = d["unit"].get<std::string>();
    }
    if (j.contains("stockRawValue") && j["stockRawValue"].is_number())
        e.stockRawValue = j["stockRawValue"].get<std::int64_t>();
    if (j.contains("stockPhysValue") && j["stockPhysValue"].is_number())
        e.stockPhysValue = j["stockPhysValue"].get<double>();
    return e;
}

const json kDamosPathSchema = {
    {"ecu_id", {{"type", "string"}, {"description", "ECU id — résout vers ressources/<ecu>/open_damos.json"}}},
    {"path",   {{"type", "string"}, {"description", "Chemin explicite (alternative à ecu_id)"}}}
};

// ── Helpers auto-mods ─────────────────────────────────────────────────────────

// Parse une chaîne hex ("AA BB CC" ou "AABBCC") en vecteur d'octets.
std::vector<std::uint8_t> parseHexBytesStr(const std::string& s) {
    std::vector<std::uint8_t> out;
    std::string acc;
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

ecu::DamosAutoModType parseAutoModType(const std::string& s) {
    if (s == "pattern") return ecu::DamosAutoModType::Pattern;
    if (s == "address") return ecu::DamosAutoModType::Address;
    return ecu::DamosAutoModType::Unknown;
}

ecu::DamosAutoMod autoModFromJson(const json& j) {
    ecu::DamosAutoMod a;
    if (j.contains("id") && j["id"].is_string())
        a.id = j["id"].get<std::string>();
    if (j.contains("type") && j["type"].is_string())
        a.type = parseAutoModType(j["type"].get<std::string>());
    if (j.contains("description") && j["description"].is_string())
        a.description = j["description"].get<std::string>();
    if (j.contains("note") && j["note"].is_string())
        a.note = j["note"].get<std::string>();
    if (j.contains("search") && j["search"].is_string())
        a.search = parseHexBytesStr(j["search"].get<std::string>());
    if (j.contains("replace") && j["replace"].is_string())
        a.replace = parseHexBytesStr(j["replace"].get<std::string>());
    if (j.contains("bytes") && j["bytes"].is_string() && a.replace.empty())
        a.replace = parseHexBytesStr(j["bytes"].get<std::string>());
    if (j.contains("restore") && j["restore"].is_string())
        a.restore = parseHexBytesStr(j["restore"].get<std::string>());
    if (j.contains("address")) {
        const auto& av = j["address"];
        if (av.is_string()) {
            std::string s = av.get<std::string>();
            if (s.size() > 1 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')))
                s = s.substr(2);
            try { a.address = std::stoull(s, nullptr, 16); } catch (...) {}
        } else if (av.is_number_integer()) {
            a.address = static_cast<std::uint64_t>(av.get<std::int64_t>());
        }
    }
    return a;
}

} // namespace

// ── Outils DAMOS : éditeur de recipe ──────────────────────────────────────────

Tool makeDamosReadTool() {
    Tool t;
    t.name = "damos_read";
    t.description = "Lit un recipe open_damos.json et renvoie son contenu complet "
                    "(parsé puis re-sérialisé) — utile pour inspecter ou éditer "
                    "via l'IA avant d'appeler damos_write.";
    t.inputSchema = {{"type", "object"}, {"properties", kDamosPathSchema}};
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        const auto r = loadRecipeFromPath(path);
        return json::parse(ecu::OpenDamos::serializeRecipe(r));
    };
    return t;
}

Tool makeDamosWriteTool() {
    Tool t;
    t.name = "damos_write";
    t.description = "Écrit (overwrite) un recipe open_damos.json complet. "
                    "Le payload 'recipe' doit suivre le schéma JSON open_damos.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"recipe", {{"type", "object"}, {"description", "Recipe JSON complet"}}}
        }},
        {"required", {"recipe"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        if (!p.contains("recipe") || !p["recipe"].is_object())
            throw std::runtime_error("paramètre 'recipe' manquant ou invalide");
        // Re-parse pour valider, puis sauvegarde.
        const std::string rawJson = p["recipe"].dump();
        auto parsed = ecu::OpenDamos::parseRecipe(rawJson);
        if (!parsed) throw std::runtime_error("recipe invalide : " + parsed.error());
        saveRecipeToPath(*parsed, path);
        return { {"saved", true}, {"path", path.toStdString()},
                 {"characteristics", parsed->characteristics.size()} };
    };
    return t;
}

Tool makeDamosAddEntryTool() {
    Tool t;
    t.name = "damos_add_entry";
    t.description = "Ajoute une nouvelle characteristic à un recipe existant et "
                    "sauvegarde. Crée le recipe si absent.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"entry",  {{"type", "object"}, {"description", "Nouvelle entrée"}}}
        }},
        {"required", {"entry"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        ecu::DamosRecipe r;
        QFile f(path);
        if (f.exists()) r = loadRecipeFromPath(path);
        else if (p.contains("ecu_id") && p["ecu_id"].is_string())
            r.ecuId = p["ecu_id"].get<std::string>();

        if (!p.contains("entry") || !p["entry"].is_object())
            throw std::runtime_error("paramètre 'entry' manquant");
        r.characteristics.push_back(entryFromJson(p["entry"]));
        saveRecipeToPath(r, path);
        return { {"added", true}, {"path", path.toStdString()},
                 {"total", r.characteristics.size()} };
    };
    return t;
}

Tool makeDamosUpdateEntryTool() {
    Tool t;
    t.name = "damos_update_entry";
    t.description = "Met à jour une characteristic existante (lookup par name) "
                    "avec les champs fournis et sauvegarde. Les champs absents "
                    "du payload sont préservés.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"name",   {{"type", "string"}, {"description", "Nom de la characteristic à modifier"}}},
            {"fields", {{"type", "object"}, {"description", "Champs à appliquer (merge sur l'entrée)"}}}
        }},
        {"required", {"name", "fields"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        auto r = loadRecipeFromPath(path);
        const std::string name = requireString(p, "name");
        auto it = std::find_if(r.characteristics.begin(), r.characteristics.end(),
                               [&](const ecu::DamosEntry& e) { return e.name == name; });
        if (it == r.characteristics.end())
            throw std::runtime_error("characteristic introuvable : " + name);
        if (!p.contains("fields") || !p["fields"].is_object())
            throw std::runtime_error("paramètre 'fields' manquant");

        // Fusion JSON : sérialise l'entrée → merge avec fields → reparse.
        json before = json::parse(ecu::OpenDamos::serializeRecipe(
            ecu::DamosRecipe{ r.ecuId, { *it }, {} }))["characteristics"][0];
        json merged = before;
        merged.merge_patch(p["fields"]);
        *it = entryFromJson(merged);

        saveRecipeToPath(r, path);
        return { {"updated", name}, {"path", path.toStdString()} };
    };
    return t;
}

Tool makeDamosRemoveEntryTool() {
    Tool t;
    t.name = "damos_remove_entry";
    t.description = "Supprime une characteristic d'un recipe par son nom et sauvegarde.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"name",   {{"type", "string"}, {"description", "Nom à supprimer"}}}
        }},
        {"required", {"name"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        auto r = loadRecipeFromPath(path);
        const std::string name = requireString(p, "name");
        const auto before = r.characteristics.size();
        r.characteristics.erase(
            std::remove_if(r.characteristics.begin(), r.characteristics.end(),
                           [&](const ecu::DamosEntry& e) { return e.name == name; }),
            r.characteristics.end());
        if (before == r.characteristics.size())
            throw std::runtime_error("characteristic introuvable : " + name);
        saveRecipeToPath(r, path);
        return { {"removed", name}, {"remaining", r.characteristics.size()} };
    };
    return t;
}

Tool makeDamosCaptureFingerprintTool() {
    Tool t;
    t.name = "damos_capture_fingerprint";
    t.description = "Lit la map située à une adresse dans la ROM et renvoie ses "
                    "axes (X et Y le cas échéant) sous forme de tableaux exploitables "
                    "comme 'fingerprint' open_damos, ainsi que les dimensions nx/ny.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin de la ROM à scanner"}}},
            {"address",  {{"type", "integer"}, {"description", "Adresse de la map (entier)"}}}
        }},
        {"required", {"rom_path", "address"}}
    };
    t.handler = [](const json& p) -> json {
        const auto rom = loadRom(requireString(p, "rom_path"));
        const std::uint64_t addr =
            static_cast<std::uint64_t>(requireNumber(p, "address"));
        std::span<const std::uint8_t> span(rom.data(), rom.size());
        auto md = ecu::readMapData(span, static_cast<std::size_t>(addr));
        if (!md) throw std::runtime_error("lecture map impossible : " + md.error());

        json xs = json::array(), ys = json::array();
        for (auto v : md->xAxis) xs.push_back(v);
        for (auto v : md->yAxis) ys.push_back(v);
        return {
            {"address", addr},
            {"nx",      md->nx},
            {"ny",      md->ny},
            {"xAxis",   xs},
            {"yAxis",   ys},
            {"dataOff", md->dataOff}
        };
    };
    return t;
}

// ── Outils AutoMods : patches en un clic versionnés dans le recipe ────────────

Tool makeDamosListAutoModsTool() {
    Tool t;
    t.name = "damos_list_automods";
    t.description = "Liste les auto-mods (patches en un clic) embarqués dans un "
                    "recipe open_damos. Renvoie id, type, description, et un "
                    "résumé du payload (adresse ou tailles search/replace).";
    t.inputSchema = {{"type", "object"}, {"properties", kDamosPathSchema}};
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        const auto r = loadRecipeFromPath(path);
        json arr = json::array();
        for (const auto& a : r.autoMods) {
            const char* typeStr = a.type == ecu::DamosAutoModType::Pattern ? "pattern"
                                 : a.type == ecu::DamosAutoModType::Address ? "address" : "?";
            json e = { {"id", a.id}, {"type", typeStr},
                       {"description", a.description}, {"note", a.note} };
            if (a.address) e["address"] = *a.address;
            if (!a.search.empty())  e["searchBytes"]  = a.search.size();
            if (!a.replace.empty()) e["replaceBytes"] = a.replace.size();
            if (!a.restore.empty()) e["restoreBytes"] = a.restore.size();
            arr.push_back(std::move(e));
        }
        return { {"path", path.toStdString()}, {"autoMods", arr}, {"total", r.autoMods.size()} };
    };
    return t;
}

Tool makeDamosAddAutoModTool() {
    Tool t;
    t.name = "damos_add_automod";
    t.description = "Ajoute un auto-mod (pattern ou address) à un recipe et sauvegarde. "
                    "Format hex pour les octets : 'AA BB CC' ou 'AABBCC'.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"automod",{{"type", "object"}, {"description", "Auto-mod à ajouter"}}}
        }},
        {"required", {"automod"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        ecu::DamosRecipe r;
        QFile f(path);
        if (f.exists()) r = loadRecipeFromPath(path);
        else if (p.contains("ecu_id") && p["ecu_id"].is_string())
            r.ecuId = p["ecu_id"].get<std::string>();

        if (!p.contains("automod") || !p["automod"].is_object())
            throw std::runtime_error("paramètre 'automod' manquant");
        r.autoMods.push_back(autoModFromJson(p["automod"]));
        saveRecipeToPath(r, path);
        return { {"added", true}, {"total", r.autoMods.size()},
                 {"path", path.toStdString()} };
    };
    return t;
}

Tool makeDamosRemoveAutoModTool() {
    Tool t;
    t.name = "damos_remove_automod";
    t.description = "Supprime un auto-mod par son id et sauvegarde.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", kDamosPathSchema.at("ecu_id")},
            {"path",   kDamosPathSchema.at("path")},
            {"id",     {{"type", "string"}, {"description", "Id de l'auto-mod"}}}
        }},
        {"required", {"id"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        auto r = loadRecipeFromPath(path);
        const std::string id = requireString(p, "id");
        const auto before = r.autoMods.size();
        r.autoMods.erase(
            std::remove_if(r.autoMods.begin(), r.autoMods.end(),
                           [&](const ecu::DamosAutoMod& a) { return a.id == id; }),
            r.autoMods.end());
        if (before == r.autoMods.size())
            throw std::runtime_error("auto-mod introuvable : " + id);
        saveRecipeToPath(r, path);
        return { {"removed", id}, {"remaining", r.autoMods.size()} };
    };
    return t;
}

Tool makeDamosApplyAutoModTool() {
    Tool t;
    t.name = "damos_apply_automod";
    t.description = "Applique un auto-mod (pattern ou address) à une ROM. Écrit "
                    "le résultat dans rom_out_path (la ROM source n'est pas modifiée). "
                    "Pour un pattern, recherche 'search' dans la ROM puis écrit 'replace' "
                    "à cette position. Pour une address, écrit 'bytes' à 'address'.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id",       kDamosPathSchema.at("ecu_id")},
            {"path",         kDamosPathSchema.at("path")},
            {"id",           {{"type", "string"}, {"description", "Id de l'auto-mod à appliquer"}}},
            {"rom_path",     {{"type", "string"}, {"description", "ROM source"}}},
            {"rom_out_path", {{"type", "string"}, {"description", "Chemin de sortie"}}}
        }},
        {"required", {"id", "rom_path", "rom_out_path"}}
    };
    t.handler = [](const json& p) -> json {
        const QString path = resolveDamosPath(p);
        const auto r = loadRecipeFromPath(path);
        const std::string id = requireString(p, "id");
        const ecu::DamosAutoMod* a = nullptr;
        for (const auto& x : r.autoMods) if (x.id == id) { a = &x; break; }
        if (!a) throw std::runtime_error("auto-mod introuvable : " + id);

        auto rom = loadRom(requireString(p, "rom_path"));
        json result = { {"id", id} };

        if (a->type == ecu::DamosAutoModType::Pattern) {
            if (a->search.empty() || a->replace.size() != a->search.size())
                throw std::runtime_error("pattern invalide (search vide ou taille != replace)");
            // Recherche naïve.
            std::int64_t off = -1;
            for (std::size_t i = 0; i + a->search.size() <= rom.size(); ++i) {
                if (std::equal(a->search.begin(), a->search.end(), rom.begin() + i)) {
                    off = static_cast<std::int64_t>(i); break;
                }
            }
            if (off < 0) throw std::runtime_error("motif introuvable dans la ROM");
            for (std::size_t i = 0; i < a->replace.size(); ++i)
                rom[static_cast<std::size_t>(off) + i] = a->replace[i];
            result["mode"]   = "pattern";
            result["offset"] = off;
            result["bytes"]  = a->replace.size();
        } else if (a->type == ecu::DamosAutoModType::Address) {
            if (!a->address || a->replace.empty())
                throw std::runtime_error("address invalide (address ou bytes manquant)");
            const std::uint64_t off = *a->address;
            if (off + a->replace.size() > rom.size())
                throw std::runtime_error("adresse hors ROM");
            for (std::size_t i = 0; i < a->replace.size(); ++i)
                rom[static_cast<std::size_t>(off) + i] = a->replace[i];
            result["mode"]    = "address";
            result["address"] = off;
            result["bytes"]   = a->replace.size();
        } else {
            throw std::runtime_error("type d'auto-mod non supporté");
        }

        writeRom(requireString(p, "rom_out_path"), rom);
        result["wrote"] = p["rom_out_path"];
        return result;
    };
    return t;
}

} // namespace ecu::mcp
