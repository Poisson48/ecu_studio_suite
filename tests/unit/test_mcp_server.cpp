// Test du round-trip JSON-RPC 2.0 du serveur MCP d'ECU Studio.
//
// On enregistre tous les outils, puis on exerce le protocole MCP de bout en
// bout via McpServer::handleRequest : initialize, tools/list, et un tools/call
// déterministe (list_recipes — aucune dépendance fichier). On vérifie que la
// réponse respecte la forme JSON-RPC 2.0 / MCP et que le résultat de l'outil
// se re-parse en JSON valide.

#include <gtest/gtest.h>

#include "ecu/mcp/McpServer.hpp"
#include "ecu/mcp/Tools.hpp"

using ecu::mcp::json;
using ecu::mcp::McpServer;

namespace {

// McpServer est non copiable / non déplaçable : on l'initialise sur place.
void setupServer(McpServer& s) {
    ecu::mcp::registerAllTools(s);
}

} // namespace

TEST(McpServer, RegistersAllTools) {
    McpServer s;
    setupServer(s);
    EXPECT_EQ(s.toolCount(), 22u);
}

TEST(McpServer, InitializeReturnsProtocol) {
    McpServer s;
    setupServer(s);
    const json req = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", json::object()}
    };
    const json resp = s.handleRequest(req);

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 1);
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_EQ(resp["result"]["protocolVersion"], "2024-11-05");
    EXPECT_EQ(resp["result"]["serverInfo"]["name"], "ecu-studio");
}

TEST(McpServer, ToolsListExposesEveryTool) {
    McpServer s;
    setupServer(s);
    const json req = {
        {"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}
    };
    const json resp = s.handleRequest(req);

    ASSERT_TRUE(resp.contains("result"));
    const json& tools = resp["result"]["tools"];
    ASSERT_TRUE(tools.is_array());
    EXPECT_EQ(tools.size(), 22u);

    // Chaque outil doit porter name / description / inputSchema.
    bool sawListRecipes = false;
    for (const auto& t : tools) {
        EXPECT_TRUE(t.contains("name"));
        EXPECT_TRUE(t.contains("description"));
        EXPECT_TRUE(t.contains("inputSchema"));
        if (t["name"] == "list_recipes") sawListRecipes = true;
    }
    EXPECT_TRUE(sawListRecipes);
}

TEST(McpServer, ToolsCallListRecipesRoundTrip) {
    McpServer s;
    setupServer(s);
    const json req = {
        {"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
        {"params", {{"name", "list_recipes"}, {"arguments", json::object()}}}
    };
    const json resp = s.handleRequest(req);

    EXPECT_EQ(resp["jsonrpc"], "2.0");
    EXPECT_EQ(resp["id"], 3);
    ASSERT_TRUE(resp.contains("result"));

    const json& result = resp["result"];
    EXPECT_FALSE(result["isError"].get<bool>());
    ASSERT_TRUE(result["content"].is_array());
    ASSERT_FALSE(result["content"].empty());
    EXPECT_EQ(result["content"][0]["type"], "text");

    // Le texte de l'outil doit être un JSON valide qui se re-parse (round-trip).
    const std::string text = result["content"][0]["text"].get<std::string>();
    const json payload = json::parse(text);
    ASSERT_TRUE(payload.contains("recipes"));
    ASSERT_TRUE(payload["recipes"].is_array());
    EXPECT_EQ(payload["total"].get<std::size_t>(), payload["recipes"].size());
    for (const auto& r : payload["recipes"]) {
        EXPECT_TRUE(r.contains("id"));
        EXPECT_TRUE(r.contains("risk"));
        EXPECT_TRUE(r.contains("opsCount"));
    }
}

TEST(McpServer, UnknownToolIsErrorNotCrash) {
    McpServer s;
    setupServer(s);
    const json req = {
        {"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"},
        {"params", {{"name", "does_not_exist"}, {"arguments", json::object()}}}
    };
    const json resp = s.handleRequest(req);
    ASSERT_TRUE(resp.contains("result"));
    EXPECT_TRUE(resp["result"]["isError"].get<bool>());
}

TEST(McpServer, UnknownMethodReturnsJsonRpcError) {
    McpServer s;
    setupServer(s);
    const json req = {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "no/such/method"}};
    const json resp = s.handleRequest(req);
    ASSERT_TRUE(resp.contains("error"));
    EXPECT_EQ(resp["error"]["code"], -32601);
}
