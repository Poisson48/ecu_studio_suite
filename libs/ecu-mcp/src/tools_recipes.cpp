// tools_recipes.cpp — outils MCP de recettes et d'écriture (vers un fichier de
// sortie distinct) : listage des recettes open_damos, application Stage 1,
// application de recette, correction de checksum, relocalisation open_damos +
// export A2L.

#include "ecu/mcp/Tools.hpp"
#include "tools_internal.h"

#include "ecu/ChecksumEngine.hpp"
#include "ecu/EcuCatalog.hpp"
#include "ecu/OpenDamos.hpp"
#include "ecu/OpenDamosA2lExport.hpp"
#include "ecu/OpenDamosRecipes.hpp"
#include "ecu/RomPatcher.hpp"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <span>
#include <stdexcept>

namespace ecu::mcp {

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

} // namespace ecu::mcp
