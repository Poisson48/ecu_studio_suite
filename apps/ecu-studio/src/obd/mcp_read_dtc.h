#pragma once
//
// Outil MCP read_dtc — enregistré depuis la couche app (pas depuis libs/ecu-mcp)
// car Elm327 vit dans apps/ecu-studio et dépend de Qt SerialPort.
//
// clear_dtc n'est volontairement PAS exposé : le contrat de sûreté MCP interdit
// toute écriture sur un périphérique connecté.
//
#include "ecu/mcp/McpServer.hpp"

namespace ecu_studio {

ecu::mcp::Tool makeReadDtcTool();

} // namespace ecu_studio
