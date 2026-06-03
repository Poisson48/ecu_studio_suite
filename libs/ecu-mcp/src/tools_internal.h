#pragma once
//
// Déclarations internes partagées entre les unités de traduction d'outils MCP
// (tools_rom.cpp, tools_recipes.cpp, tools_damos.cpp) après éclatement de
// l'ancien Tools.cpp monolithique. Non installé : usage interne à ecu-mcp.
//
#include "ecu/mcp/McpServer.hpp"

#include "ecu/OpenDamos.hpp"          // ecu::AddressSource, ecu::DamosType
#include "ecu/OpenDamosRecipes.hpp"   // ecu::RecipeRisk

#include <cstdint>
#include <string>
#include <vector>

namespace ecu::mcp {

// ── Helpers communs (définis dans tools_common.cpp) ───────────────────────────

// Extrait un paramètre string/number requis du payload JSON (lève si absent).
std::string requireString(const json& params, const char* key);
double       requireNumber(const json& params, const char* key);

// Charge / écrit un fichier ROM entier (lèvent en cas d'échec).
std::vector<uint8_t> loadRom(const std::string& path);
void                 writeRom(const std::string& path, const std::vector<uint8_t>& buf);

// Conversions enum → libellé pour la sérialisation JSON.
const char* addrSourceStr(ecu::AddressSource s);
const char* riskStr(ecu::RecipeRisk r);
const char* damosTypeStr(ecu::DamosType t);

// Adresse → chaîne hexadécimale "0x…" (valeur JSON).
json hexAddr(std::size_t a);

// ── Fabriques d'outils DAMOS / AutoMod (définies dans tools_damos.cpp) ─────────
// Les fabriques de lecture/analyse et d'écriture sont déclarées dans le header
// public ecu/mcp/Tools.hpp ; celles ci-dessous sont internes.
Tool makeDamosReadTool();
Tool makeDamosWriteTool();
Tool makeDamosAddEntryTool();
Tool makeDamosUpdateEntryTool();
Tool makeDamosRemoveEntryTool();
Tool makeDamosCaptureFingerprintTool();
Tool makeDamosListAutoModsTool();
Tool makeDamosAddAutoModTool();
Tool makeDamosRemoveAutoModTool();
Tool makeDamosApplyAutoModTool();

} // namespace ecu::mcp
