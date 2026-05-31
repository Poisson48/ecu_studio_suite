#pragma once
//
// Outils MCP d'ECU Studio — fabriques renvoyant un ecu::mcp::Tool prêt à
// enregistrer. Chaque outil expose une API publique de libs/ecu-core et opère
// sur des fichiers ROM désignés par chemin (mode headless).
//
// Sûreté : les outils de lecture/analyse sont libres ; les outils d'écriture
// (apply_stage1, apply_recipe, correct_checksum, relocate_open_damos qui écrit
// un A2L) écrivent TOUJOURS dans un fichier de sortie distinct, jamais sur la
// ROM source ni sur un périphérique connecté.
//
#include "ecu/mcp/McpServer.hpp"

namespace ecu::mcp {

// Lecture / analyse (sans effet de bord sur disque).
Tool makeListEcusTool();
Tool makeGetEcuTool();
Tool makeReadMapTool();
Tool makeReadValueTool();
Tool makeFindMapsTool();
Tool makeCompareRomsTool();
Tool makeVerifyChecksumTool();
Tool makeListRecipesTool();

// Écriture (vers un chemin de sortie distinct).
Tool makeApplyStage1Tool();
Tool makeApplyRecipeTool();
Tool makeCorrectChecksumTool();
Tool makeRelocateOpenDamosTool();

} // namespace ecu::mcp
