#include "ecu/mcp/Tools.hpp"

#include "ecu/ChecksumEngine.hpp"
#include "ecu/EcuCatalog.hpp"
#include "ecu/MapDiffer.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/OpenDamosA2lExport.hpp"
#include "ecu/OpenDamosRecipes.hpp"
#include "ecu/RomPatcher.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ecu::mcp {

// ---------------------------------------------------------------------------
// Helpers fichiers ROM (par chemin)
// ---------------------------------------------------------------------------

namespace {

std::string requireString(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_string())
        throw std::runtime_error(std::string("paramètre requis manquant : ") + key);
    return params[key].get<std::string>();
}

double requireNumber(const json& params, const char* key) {
    if (!params.contains(key) || !params[key].is_number())
        throw std::runtime_error(std::string("paramètre numérique requis manquant : ") + key);
    return params[key].get<double>();
}

// Lit un fichier ROM entier en mémoire. Lève si introuvable.
std::vector<uint8_t> loadRom(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("fichier ROM introuvable : " + path);
    const std::streamsize size = f.tellg();
    if (size <= 0) throw std::runtime_error("fichier ROM vide : " + path);
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("échec de lecture du fichier ROM : " + path);
    return buf;
}

// Écrit un buffer dans un fichier de sortie (chemin distinct). Lève en cas d'échec.
void writeRom(const std::string& path, const std::vector<uint8_t>& buf) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("impossible d'ouvrir le fichier de sortie : " + path);
    f.write(reinterpret_cast<const char*>(buf.data()),
            static_cast<std::streamsize>(buf.size()));
    if (!f) throw std::runtime_error("échec d'écriture du fichier de sortie : " + path);
}

const char* addrSourceStr(ecu::AddressSource s) {
    switch (s) {
        case ecu::AddressSource::Fingerprint:     return "fingerprint";
        case ecu::AddressSource::Anchor:          return "anchor";
        case ecu::AddressSource::DefaultFallback: return "default-fallback";
    }
    return "unknown";
}

const char* riskStr(ecu::RecipeRisk r) {
    switch (r) {
        case ecu::RecipeRisk::Low:    return "low";
        case ecu::RecipeRisk::Medium: return "medium";
        case ecu::RecipeRisk::High:   return "high";
    }
    return "unknown";
}

const char* damosTypeStr(ecu::DamosType t) {
    switch (t) {
        case ecu::DamosType::Map:   return "MAP";
        case ecu::DamosType::Curve: return "CURVE";
        case ecu::DamosType::Value: return "VALUE";
        default:                    return "UNKNOWN";
    }
}

json hexAddr(std::size_t a) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "0x%zX", a);
    return std::string(buf);
}

} // namespace

// ---------------------------------------------------------------------------
// list_ecus — catalogue ECU (EcuCatalog::listEcus)
// ---------------------------------------------------------------------------

Tool makeListEcusTool() {
    Tool t;
    t.name        = "list_ecus";
    t.description =
        "Liste tous les calculateurs (ECU) connus du catalogue ECU Studio : id, "
        "nom, famille, carburant, application, et la disponibilité d'un A2L, de "
        "maps Stage 1 et de paramètres pop&bang. Point d'entrée pour obtenir les "
        "ids d'ECU à passer aux autres outils.";
    t.inputSchema = {{"type", "object"}, {"properties", json::object()},
                     {"required", json::array()}};
    t.handler = [](const json&) -> json {
        json arr = json::array();
        for (const auto& e : ecu::listEcus()) {
            arr.push_back({
                {"id",          std::string(e.id)},
                {"name",        std::string(e.name)},
                {"family",      std::string(e.family)},
                {"fuel",        std::string(e.fuel)},
                {"application", std::string(e.application)},
                {"hasA2l",      e.hasA2l},
                {"hasStage1",   e.hasStage1},
                {"hasPopbang",  e.hasPopbang},
            });
        }
        return {{"ecus", arr}, {"total", arr.size()}};
    };
    return t;
}

// ---------------------------------------------------------------------------
// get_ecu — détail d'un ECU (EcuCatalog::getEcu), incl. les maps Stage 1
// ---------------------------------------------------------------------------

Tool makeGetEcuTool() {
    Tool t;
    t.name        = "get_ecu";
    t.description =
        "Renvoie le détail d'un ECU du catalogue : métadonnées, liste des maps "
        "Stage 1 (nom, adresse, % par défaut, libellé) et les paramètres "
        "pop&bang. Utiliser l'id renvoyé par list_ecus.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"ecu_id", {{"type", "string"},
                        {"description", "Identifiant ECU (cf. list_ecus)"}}}
        }},
        {"required", {"ecu_id"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string id = requireString(p, "ecu_id");
        auto e = ecu::getEcu(id);
        if (!e) throw std::runtime_error("ECU introuvable : " + id);

        json stage1 = json::array();
        if (e->stage1Maps) {
            for (const auto& m : *e->stage1Maps) {
                stage1.push_back({
                    {"name",       std::string(m.name)},
                    {"address",    m.address},
                    {"addressHex", hexAddr(m.address)},
                    {"defaultPct", m.defaultPct},
                    {"label",      std::string(m.label)},
                });
            }
        }
        json popbang = json::object();
        if (e->popbangParams) {
            const auto& pb = *e->popbangParams;
            popbang = {
                {"nOvrRun", {{"address", pb.nOvrRun.address},
                             {"addressHex", hexAddr(pb.nOvrRun.address)},
                             {"min", pb.nOvrRun.min}, {"max", pb.nOvrRun.max},
                             {"label", std::string(pb.nOvrRun.label)}}},
                {"qOvrRun", {{"address", pb.qOvrRun.address},
                             {"addressHex", hexAddr(pb.qOvrRun.address)},
                             {"min", pb.qOvrRun.min}, {"max", pb.qOvrRun.max},
                             {"label", std::string(pb.qOvrRun.label)}}},
            };
        }
        return {
            {"id",          std::string(e->id)},
            {"name",        std::string(e->name)},
            {"family",      std::string(e->family)},
            {"fuel",        std::string(e->fuel)},
            {"application", std::string(e->application)},
            {"a2l",         e->a2l ? json(std::string(*e->a2l)) : json(nullptr)},
            {"stage1Maps",  stage1},
            {"popbang",     popbang},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// read_map — lit une cartographie (RomPatcher::readMapData) à une adresse
// ---------------------------------------------------------------------------

Tool makeReadMapTool() {
    Tool t;
    t.name        = "read_map";
    t.description =
        "Lit une cartographie dans un fichier ROM à l'adresse donnée : "
        "dimensions nx/ny, axes X/Y et grille de valeurs (SWORD big-endian). "
        "L'en-tête MAP est de 4 octets (nx UWORD_BE, ny UWORD_BE).";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"},
                          {"description", "Chemin du fichier ROM"}}},
            {"address",  {{"type", "integer"},
                          {"description", "Adresse de l'en-tête de la map (octets)"}}}
        }},
        {"required", {"rom_path", "address"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string path = requireString(p, "rom_path");
        const auto address = static_cast<std::size_t>(requireNumber(p, "address"));
        const auto rom = loadRom(path);
        auto md = ecu::readMapData(rom, address);
        if (!md) throw std::runtime_error(md.error());
        return {
            {"address",    address},
            {"addressHex", hexAddr(address)},
            {"nx",         md->nx},
            {"ny",         md->ny},
            {"xAxis",      md->xAxis},
            {"yAxis",      md->yAxis},
            {"data",       md->data},
            {"dataOffset", md->dataOff},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// read_value — lit un scalaire SWORD BE (RomPatcher::readValue)
// ---------------------------------------------------------------------------

Tool makeReadValueTool() {
    Tool t;
    t.name        = "read_value";
    t.description =
        "Lit une valeur scalaire (SWORD big-endian, 2 octets) dans un fichier "
        "ROM à l'adresse donnée. Renvoie la valeur brute.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin du fichier ROM"}}},
            {"address",  {{"type", "integer"}, {"description", "Adresse (octets)"}}}
        }},
        {"required", {"rom_path", "address"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string path = requireString(p, "rom_path");
        const auto address = static_cast<std::size_t>(requireNumber(p, "address"));
        const auto rom = loadRom(path);
        auto v = ecu::readValue(rom, address);
        if (!v) throw std::runtime_error(v.error());
        return {{"address", address}, {"addressHex", hexAddr(address)},
                {"rawValue", *v}};
    };
    return t;
}

// ---------------------------------------------------------------------------
// find_maps — détecte les cartographies candidates (MapFinder::findMaps)
// ---------------------------------------------------------------------------

Tool makeFindMapsTool() {
    Tool t;
    t.name        = "find_maps";
    t.description =
        "Analyse une ROM et détecte les cartographies candidates par heuristique "
        "(axes monotones, plage de données, lissage). Renvoie adresse, dimensions, "
        "résumé des axes/données et un score, triés par pertinence.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin du fichier ROM"}}},
            {"min_n",    {{"type", "integer"}, {"description", "Taille d'axe min (défaut 4)"}}},
            {"max_n",    {{"type", "integer"}, {"description", "Taille d'axe max (défaut 32)"}}},
            {"limit",    {{"type", "integer"}, {"description", "Nb max de candidats (défaut 100)"}}}
        }},
        {"required", {"rom_path"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string path = requireString(p, "rom_path");
        const auto rom = loadRom(path);
        ecu::FindMapsOptions opts;
        if (p.contains("min_n") && p["min_n"].is_number_integer())
            opts.minN = p["min_n"].get<int>();
        if (p.contains("max_n") && p["max_n"].is_number_integer())
            opts.maxN = p["max_n"].get<int>();
        if (p.contains("limit") && p["limit"].is_number_integer())
            opts.limit = static_cast<std::size_t>(p["limit"].get<long long>());
        auto cands = ecu::findMaps(rom, opts);
        json arr = json::array();
        for (const auto& c : cands) {
            arr.push_back({
                {"address",    c.address},
                {"addressHex", hexAddr(c.address)},
                {"nx",         c.nx},
                {"ny",         c.ny},
                {"blockSize",  c.blockSize},
                {"axisX",      {{"min", c.axisX.min}, {"max", c.axisX.max}, {"dir", c.axisX.dir}}},
                {"axisY",      {{"min", c.axisY.min}, {"max", c.axisY.max}, {"dir", c.axisY.dir}}},
                {"data",       {{"min", c.data.min}, {"max", c.data.max}}},
                {"smoothness", c.smoothness},
                {"score",      c.score},
            });
        }
        return {{"candidates", arr}, {"total", arr.size()}};
    };
    return t;
}

// ---------------------------------------------------------------------------
// compare_roms — diff binaire de deux ROMs (MapDiffer::diffIntervals)
// ---------------------------------------------------------------------------

Tool makeCompareRomsTool() {
    Tool t;
    t.name        = "compare_roms";
    t.description =
        "Compare deux fichiers ROM (même taille) et renvoie les intervalles "
        "d'octets modifiés (start/end). Utile pour auditer un tune : comparer le "
        "stock et la version modifiée pour localiser les zones changées.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_a", {{"type", "string"}, {"description", "Chemin ROM A (ex: stock)"}}},
            {"rom_b", {{"type", "string"}, {"description", "Chemin ROM B (ex: modifiée)"}}}
        }},
        {"required", {"rom_a", "rom_b"}}
    };
    t.handler = [](const json& p) -> json {
        const auto a = loadRom(requireString(p, "rom_a"));
        const auto b = loadRom(requireString(p, "rom_b"));
        auto intervals = ecu::diffIntervals(a, b);
        json arr = json::array();
        std::size_t totalBytes = 0;
        for (const auto& iv : intervals) {
            arr.push_back({{"start", iv.start}, {"startHex", hexAddr(iv.start)},
                           {"end", iv.end}, {"endHex", hexAddr(iv.end)},
                           {"length", iv.end - iv.start}});
            totalBytes += iv.end - iv.start;
        }
        return {
            {"sizeA",          a.size()},
            {"sizeB",          b.size()},
            {"intervals",      arr},
            {"intervalCount",  arr.size()},
            {"bytesChanged",   totalBytes},
            {"identical",      arr.empty()},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// verify_checksum — vérifie le checksum MPPS (ChecksumEngine::verifyMpps)
// ---------------------------------------------------------------------------

Tool makeVerifyChecksumTool() {
    Tool t;
    t.name        = "verify_checksum";
    t.description =
        "Vérifie le checksum CRC-16/ARC d'une image flash MPPS (EDC 32K/64K, "
        "détection par la taille). Renvoie le checksum calculé, le checksum "
        "stocké et leur validité. Lecture seule.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin du fichier ROM/flash"}}}
        }},
        {"required", {"rom_path"}}
    };
    t.handler = [](const json& p) -> json {
        const auto rom = loadRom(requireString(p, "rom_path"));
        auto r = ecu::ChecksumEngine::verifyMpps(rom);
        if (!r) throw std::runtime_error(
            "taille d'image non reconnue comme layout MPPS EDC (32K/64K)");
        return {
            {"computed",    r->computed},
            {"computedHex", hexAddr(r->computed)},
            {"stored",      r->stored},
            {"storedHex",   hexAddr(r->stored)},
            {"valid",       r->valid},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// list_recipes — recettes open_damos prédéfinies (OpenDamosRecipes::listRecipes)
// ---------------------------------------------------------------------------

Tool makeListRecipesTool() {
    Tool t;
    t.name        = "list_recipes";
    t.description =
        "Liste les recettes d'auto-tune open_damos prédéfinies : id, nom, "
        "catégorie, description, niveau de risque et nombre d'opérations. "
        "Passer l'id à apply_recipe pour l'appliquer.";
    t.inputSchema = {{"type", "object"}, {"properties", json::object()},
                     {"required", json::array()}};
    t.handler = [](const json&) -> json {
        json arr = json::array();
        for (const auto& r : ecu::listRecipes()) {
            arr.push_back({
                {"id",          r.id},
                {"name",        r.name},
                {"category",    r.category},
                {"description", r.description},
                {"risk",        riskStr(r.risk)},
                {"opsCount",    r.opsCount},
            });
        }
        return {{"recipes", arr}, {"total", arr.size()}};
    };
    return t;
}

// ---------------------------------------------------------------------------
// apply_stage1 — applique un % aux maps Stage 1 d'un ECU (ÉCRITURE → out_path)
// ---------------------------------------------------------------------------

Tool makeApplyStage1Tool() {
    Tool t;
    t.name        = "apply_stage1";
    t.description =
        "Applique un pourcentage aux cartographies Stage 1 d'un ECU du catalogue "
        "(applyPctToMap sur chaque map). Écrit le résultat dans out_path (copie ; "
        "jamais sur la ROM source ni un périphérique). Si pct est omis, le % par "
        "défaut de chaque map du catalogue est utilisé.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin de la ROM source (non modifiée)"}}},
            {"ecu_id",   {{"type", "string"}, {"description", "Identifiant ECU (cf. list_ecus)"}}},
            {"out_path", {{"type", "string"}, {"description", "Chemin du fichier de sortie modifié"}}},
            {"pct",      {{"type", "number"}, {"description", "Pourcentage à appliquer (sinon défaut catalogue)"}}}
        }},
        {"required", {"rom_path", "ecu_id", "out_path"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string ecuId   = requireString(p, "ecu_id");
        const std::string outPath = requireString(p, "out_path");
        auto e = ecu::getEcu(ecuId);
        if (!e) throw std::runtime_error("ECU introuvable : " + ecuId);
        if (!e->stage1Maps || e->stage1Maps->empty())
            throw std::runtime_error("cet ECU n'a pas de maps Stage 1 : " + ecuId);

        const bool hasPct = p.contains("pct") && p["pct"].is_number();
        const double pct  = hasPct ? p["pct"].get<double>() : 0.0;

        auto rom = loadRom(requireString(p, "rom_path"));
        json maps = json::array();
        long long totalCells = 0;
        for (const auto& m : *e->stage1Maps) {
            const double usePct = hasPct ? pct : static_cast<double>(m.defaultPct);
            std::span<uint8_t> romSpan(rom.data(), rom.size());
            auto res = ecu::applyPctToMap(romSpan, m.address, usePct);
            json entry = {
                {"name",       std::string(m.name)},
                {"address",    m.address},
                {"addressHex", hexAddr(m.address)},
                {"pct",        usePct},
            };
            if (!res) {
                entry["error"] = res.error();
            } else {
                entry["cellsChanged"] = res->size();
                totalCells += static_cast<long long>(res->size());
            }
            maps.push_back(entry);
        }
        writeRom(outPath, rom);
        return {
            {"ecu",          ecuId},
            {"outPath",      outPath},
            {"maps",         maps},
            {"totalCells",   totalCells},
            {"appliedPct",   hasPct ? json(pct) : json("(défaut catalogue par map)")},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// apply_recipe — applique une recette open_damos (ÉCRITURE → out_path)
// ---------------------------------------------------------------------------

Tool makeApplyRecipeTool() {
    Tool t;
    t.name        = "apply_recipe";
    t.description =
        "Applique une recette open_damos (cf. list_recipes) à une ROM : charge "
        "l'open_damos de l'ECU depuis le répertoire ressources/, relocalise les "
        "entrées par empreinte d'axes, puis exécute chaque opération. Écrit le "
        "résultat dans out_path (copie). Requiert ressources/<ecu>/open_damos.json "
        "accessible depuis le répertoire de travail du serveur.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path",  {{"type", "string"}, {"description", "Chemin de la ROM source"}}},
            {"ecu_id",    {{"type", "string"}, {"description", "Identifiant ECU (pour charger l'open_damos)"}}},
            {"recipe_id", {{"type", "string"}, {"description", "Identifiant de recette (cf. list_recipes)"}}},
            {"out_path",  {{"type", "string"}, {"description", "Chemin du fichier de sortie modifié"}}}
        }},
        {"required", {"rom_path", "ecu_id", "recipe_id", "out_path"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string ecuId    = requireString(p, "ecu_id");
        const std::string recipeId = requireString(p, "recipe_id");
        const std::string outPath  = requireString(p, "out_path");
        const ecu::Recipe* recipe  = ecu::getRecipe(recipeId);
        if (!recipe) throw std::runtime_error("recette introuvable : " + recipeId);

        auto rom = loadRom(requireString(p, "rom_path"));
        std::span<uint8_t> romSpan(rom.data(), rom.size());
        auto res = ecu::applyRecipe(*recipe, romSpan,
                                    QString::fromStdString(ecuId));
        if (!res) throw std::runtime_error(res.error());

        json ops = json::array();
        for (const auto& op : res->operations) {
            json o = {{"entry", op.entry}, {"method", op.method},
                      {"addressSource", op.addressSource}};
            if (op.address)      o["address"]      = *op.address;
            if (op.physValue)    o["physValue"]    = *op.physValue;
            if (op.rawValue)     o["rawValue"]     = *op.rawValue;
            if (op.prevRaw)      o["prevRaw"]      = *op.prevRaw;
            if (op.cellsChanged) o["cellsChanged"] = *op.cellsChanged;
            if (op.pct)          o["pct"]          = *op.pct;
            if (op.error)        o["error"]        = *op.error;
            ops.push_back(o);
        }
        writeRom(outPath, rom);
        return {
            {"ecu",          ecuId},
            {"recipe",       recipeId},
            {"outPath",      outPath},
            {"ok",           res->ok},
            {"bytesChanged", res->bytesChanged},
            {"operations",   ops},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// correct_checksum — corrige le checksum MPPS (ÉCRITURE → out_path)
// ---------------------------------------------------------------------------

Tool makeCorrectChecksumTool() {
    Tool t;
    t.name        = "correct_checksum";
    t.description =
        "Recalcule et corrige le checksum CRC-16/ARC MPPS (EDC 32K/64K) d'une "
        "image flash. Écrit l'image corrigée dans out_path (copie ; jamais sur "
        "un périphérique). Renvoie le nombre de checksums corrigés.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path", {{"type", "string"}, {"description", "Chemin de l'image source"}}},
            {"out_path", {{"type", "string"}, {"description", "Chemin de l'image corrigée"}}}
        }},
        {"required", {"rom_path", "out_path"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string outPath = requireString(p, "out_path");
        auto rom = loadRom(requireString(p, "rom_path"));

        auto before = ecu::ChecksumEngine::verifyMpps(rom);
        if (!before) throw std::runtime_error(
            "taille d'image non reconnue comme layout MPPS EDC (32K/64K)");

        std::span<uint8_t> romSpan(rom.data(), rom.size());
        auto fixed = ecu::ChecksumEngine::correctMpps(romSpan);
        if (!fixed) throw std::runtime_error("correction impossible : layout MPPS inconnu");

        auto after = ecu::ChecksumEngine::verifyMpps(rom);
        writeRom(outPath, rom);
        return {
            {"outPath",       outPath},
            {"checksumsFixed", *fixed},
            {"wasValid",      before->valid},
            {"nowValid",      after && after->valid},
            {"storedBefore",  hexAddr(before->stored)},
            {"storedAfter",   after ? hexAddr(after->stored) : json(nullptr)},
        };
    };
    return t;
}

// ---------------------------------------------------------------------------
// relocate_open_damos — relocalise les maps open_damos et exporte un A2L
// ---------------------------------------------------------------------------

Tool makeRelocateOpenDamosTool() {
    Tool t;
    t.name        = "relocate_open_damos";
    t.description =
        "Charge l'open_damos d'un ECU depuis ressources/<ecu>/open_damos.json, "
        "relocalise chaque caractéristique dans la ROM par empreinte d'axes, et "
        "renvoie pour chacune l'adresse résolue, la source d'adresse (fingerprint/"
        "anchor/default-fallback) et un score de confiance. Si a2l_out_path est "
        "fourni, écrit en plus un A2L ASAP2 calé sur ce firmware.";
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"rom_path",     {{"type", "string"}, {"description", "Chemin de la ROM"}}},
            {"ecu_id",       {{"type", "string"}, {"description", "Identifiant ECU (open_damos à charger)"}}},
            {"resource_dir", {{"type", "string"}, {"description", "Racine ressources/ (optionnel)"}}},
            {"a2l_out_path", {{"type", "string"}, {"description", "Chemin de sortie A2L (optionnel)"}}}
        }},
        {"required", {"rom_path", "ecu_id"}}
    };
    t.handler = [](const json& p) -> json {
        const std::string ecuId = requireString(p, "ecu_id");
        QString baseDir;
        if (p.contains("resource_dir") && p["resource_dir"].is_string())
            baseDir = QString::fromStdString(p["resource_dir"].get<std::string>());

        auto recipe = ecu::OpenDamos::loadRecipe(QString::fromStdString(ecuId), baseDir);
        if (!recipe) throw std::runtime_error(
            "chargement open_damos impossible : " + recipe.error());

        const auto rom = loadRom(requireString(p, "rom_path"));
        QByteArray romQ(reinterpret_cast<const char*>(rom.data()),
                        static_cast<qsizetype>(rom.size()));

        ecu::OpenDamos od;
        auto results = od.relocate(*recipe, romQ);

        json arr = json::array();
        int resolved = 0;
        for (const auto& r : results) {
            const bool ok = r.addressSource != ecu::AddressSource::DefaultFallback;
            if (ok) ++resolved;
            json entry = {
                {"name",          r.name},
                {"type",          damosTypeStr(r.type)},
                {"category",      r.category},
                {"address",       r.address},
                {"addressHex",    hexAddr(r.address)},
                {"addressSource", addrSourceStr(r.addressSource)},
                {"score",         r.score},
                {"matchMode",     r.matchMode},
            };
            if (r.warning) entry["warning"] = *r.warning;
            arr.push_back(entry);
        }

        json out = {
            {"ecu",            ecuId},
            {"characteristics", arr},
            {"total",          arr.size()},
            {"resolved",       resolved},
        };

        if (p.contains("a2l_out_path") && p["a2l_out_path"].is_string()) {
            const std::string a2lPath = p["a2l_out_path"].get<std::string>();
            auto exp = ecu::OpenDamosA2lExport::exportA2l(*recipe, results);
            if (!exp) throw std::runtime_error("export A2L impossible : " + exp.error());
            QFile f(QString::fromStdString(a2lPath));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                throw std::runtime_error("écriture A2L impossible : " + a2lPath);
            f.write(exp->a2l.toUtf8());
            f.close();
            out["a2lOutPath"] = a2lPath;
        }
        return out;
    };
    return t;
}

// ---------------------------------------------------------------------------
// Enregistrement de tous les outils
// ---------------------------------------------------------------------------

void registerAllTools(McpServer& server) {
    // Lecture / analyse
    server.registerTool(makeListEcusTool());
    server.registerTool(makeGetEcuTool());
    server.registerTool(makeReadMapTool());
    server.registerTool(makeReadValueTool());
    server.registerTool(makeFindMapsTool());
    server.registerTool(makeCompareRomsTool());
    server.registerTool(makeVerifyChecksumTool());
    server.registerTool(makeListRecipesTool());
    // Écriture (vers fichier de sortie distinct)
    server.registerTool(makeApplyStage1Tool());
    server.registerTool(makeApplyRecipeTool());
    server.registerTool(makeCorrectChecksumTool());
    server.registerTool(makeRelocateOpenDamosTool());
}

} // namespace ecu::mcp
