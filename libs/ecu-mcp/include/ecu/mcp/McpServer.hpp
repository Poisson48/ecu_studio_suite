#pragma once
//
// McpServer — serveur MCP (Model Context Protocol) JSON-RPC 2.0 pour ECU Studio.
//
// Architecture calquée sur le serveur MCP de SocketSpy
// (apps/socketspy/api/src/mcp/mcp_server.h) pour rester cohérent dans la suite :
//   - transport stdio (ligne par ligne, une requête JSON par ligne), c'est
//     ainsi que Claude Desktop / Claude Code lancent un serveur MCP ;
//   - transport TCP optionnel, LIÉ EXCLUSIVEMENT à la boucle locale 127.0.0.1 ;
//   - protocole MCP 2024-11-05 (initialize / tools/list / tools/call).
//
// Les outils exposent les API publiques de libs/ecu-core (EcuCatalog, RomPatcher,
// MapFinder, MapDiffer, ChecksumEngine, OpenDamos, OpenDamosRecipes) et opèrent
// sur des fichiers ROM désignés PAR CHEMIN, afin que le serveur fonctionne en
// mode « headless » (sans interface graphique).
//
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace ecu::mcp {

using json = nlohmann::json;

// Signature d'un handler d'outil : reçoit l'objet `arguments`, renvoie un objet
// résultat (sérialisé en texte dans la réponse MCP). Peut lever une exception :
// elle est convertie en réponse d'erreur MCP (isError=true).
using ToolHandler = std::function<json(const json& params)>;

struct Tool {
    std::string name;
    std::string description;
    json        inputSchema;   // JSON Schema des paramètres
    ToolHandler handler;
};

class McpServer {
public:
    McpServer();
    ~McpServer();

    // Enregistre un outil. À appeler au démarrage avant serve*().
    void registerTool(Tool tool);

    // Transport stdio (bloquant). Lit le JSON-RPC sur stdin, écrit sur stdout.
    void serveStdio();

    // Transport TCP sur 127.0.0.1:port (bloquant).
    // Invariant de sécurité : se lie UNIQUEMENT à la boucle locale.
    void serveTcp(uint16_t port);

    // Traite une requête JSON-RPC isolée et renvoie la réponse.
    // Renvoie un json null pour les notifications (aucune réponse à émettre).
    // Public pour que les threads de connexion TCP puissent l'appeler.
    json handleRequest(const json& request);

    std::size_t toolCount() const { return m_tools.size(); }

    McpServer(const McpServer&)            = delete;
    McpServer& operator=(const McpServer&) = delete;

private:
    json handleInitialize(const json& params);
    json handleToolsList(const json& params);
    json handleToolsCall(const json& params);
    json makeError(int code, std::string_view message) const;

    std::unordered_map<std::string, Tool> m_tools;
};

// Fabrique : crée et enregistre tous les outils ECU Studio sur le serveur.
void registerAllTools(McpServer& server);

} // namespace ecu::mcp
