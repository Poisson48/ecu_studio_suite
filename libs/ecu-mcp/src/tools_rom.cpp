// tools_rom.cpp — outils MCP de lecture/analyse de ROM (sans effet de bord
// disque) : catalogue ECU, lecture de map/valeur, détection de maps, diff de
// ROMs, vérification de checksum.

#include "ecu/mcp/Tools.hpp"
#include "tools_internal.h"

#include "ecu/ChecksumEngine.hpp"
#include "ecu/EcuCatalog.hpp"
#include "ecu/MapDiffer.hpp"
#include "ecu/MapFinder.hpp"
#include "ecu/RomPatcher.hpp"

#include <stdexcept>

namespace ecu::mcp {

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

} // namespace ecu::mcp
