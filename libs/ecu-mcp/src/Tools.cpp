// Tools.cpp — enregistrement de tous les outils MCP d'ECU Studio.
//
// Les fabriques d'outils sont réparties par catégorie dans des unités dédiées :
//   - tools_common.cpp  : helpers partagés (ROM, conversions, formatage)
//   - tools_rom.cpp      : lecture / analyse (list_ecus, read_map, find_maps…)
//   - tools_recipes.cpp  : recettes + écriture (apply_stage1, apply_recipe…)
//   - tools_damos.cpp    : édition de recipe open_damos + auto-mods
// Voir tools_internal.h pour les déclarations internes partagées.

#include "ecu/mcp/Tools.hpp"
#include "tools_internal.h"

namespace ecu::mcp {

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
    // Édition DAMOS pilotable par l'IA — lecture, écriture, ajout/màj/suppression
    // d'entrées, capture d'empreintes depuis une ROM.
    server.registerTool(makeDamosReadTool());
    server.registerTool(makeDamosWriteTool());
    server.registerTool(makeDamosAddEntryTool());
    server.registerTool(makeDamosUpdateEntryTool());
    server.registerTool(makeDamosRemoveEntryTool());
    server.registerTool(makeDamosCaptureFingerprintTool());
    // AutoMods : patches en un clic versionnés dans le recipe.
    server.registerTool(makeDamosListAutoModsTool());
    server.registerTool(makeDamosAddAutoModTool());
    server.registerTool(makeDamosRemoveAutoModTool());
    server.registerTool(makeDamosApplyAutoModTool());
}

} // namespace ecu::mcp
