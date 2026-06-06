#include "ecu/mcp/McpServer.hpp"

// ── Sockets : abstraction multiplateforme (POSIX / Winsock) ────────────────────
// Le transport stdio (serveStdio) n'utilise que std::cin / std::cout : il est
// portable tel quel. Seul le transport TCP (boucle locale) touche aux sockets ;
// on enveloppe ici les rares différences entre Winsock et POSIX pour qu'ECU
// Studio compile aussi sous Windows (MSVC).
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace ecu::mcp {

namespace {
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
inline void closeSocket(socket_t s) { ::closesocket(s); }
// Initialise / libère Winsock pour la durée de vie du process (une seule fois).
struct WinsockGuard {
    WinsockGuard()  { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockGuard() { WSACleanup(); }
};
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
inline void closeSocket(socket_t s) { ::close(s); }
#endif

// recv/send fonctionnent à l'identique sur les deux plateformes (au contraire de
// read/write qui ne s'appliquent pas à un SOCKET Windows).
inline long recvByte(socket_t s, char* c) {
    return static_cast<long>(::recv(s, c, 1, 0));
}
inline long sendAll(socket_t s, const char* p, std::size_t n) {
    return static_cast<long>(::send(s, p, static_cast<int>(n), 0));
}
} // namespace

static constexpr const char* kProtocolVersion = "2024-11-05";
static constexpr const char* kServerName      = "ecu-studio";
static constexpr const char* kServerVersion   = "1.0.0";

McpServer::McpServer()  = default;
McpServer::~McpServer() = default;

void McpServer::registerTool(Tool tool) {
    m_tools[tool.name] = std::move(tool);
}

// ---------------------------------------------------------------------------
// Dispatch JSON-RPC / MCP
// ---------------------------------------------------------------------------

json McpServer::makeError(int code, std::string_view message) const {
    return {{"code", code}, {"message", std::string(message)}};
}

json McpServer::handleInitialize(const json& /*params*/) {
    return {
        {"protocolVersion", kProtocolVersion},
        {"serverInfo", {{"name", kServerName}, {"version", kServerVersion}}},
        {"capabilities", {{"tools", {{"listChanged", false}}}}}
    };
}

json McpServer::handleToolsList(const json& /*params*/) {
    json arr = json::array();
    for (const auto& [name, tool] : m_tools) {
        arr.push_back({
            {"name",        tool.name},
            {"description", tool.description},
            {"inputSchema", tool.inputSchema}
        });
    }
    return {{"tools", arr}};
}

json McpServer::handleToolsCall(const json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        return {{"isError", true},
                {"content", json::array({{{"type", "text"},
                  {"text", "champ 'name' manquant ou invalide"}}})}};
    }
    const std::string name = params["name"].get<std::string>();
    auto it = m_tools.find(name);
    if (it == m_tools.end()) {
        return {{"isError", true},
                {"content", json::array({{{"type", "text"},
                  {"text", "outil inconnu : " + name}}})}};
    }
    const json& args = params.contains("arguments") ? params["arguments"]
                                                     : json::object();
    try {
        json result = it->second.handler(args);
        return {
            {"isError", false},
            {"content", json::array({{{"type", "text"},
              {"text", result.dump(2)}}})}
        };
    } catch (const std::exception& e) {
        return {{"isError", true},
                {"content", json::array({{{"type", "text"},
                  {"text", std::string("erreur outil : ") + e.what()}}})}};
    }
}

json McpServer::handleRequest(const json& req) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"]      = req.contains("id") ? req["id"] : json(nullptr);

    if (!req.contains("method") || !req["method"].is_string()) {
        response["error"] = makeError(-32600, "requête invalide : méthode absente");
        return response;
    }

    const std::string method = req["method"].get<std::string>();
    const json params = req.contains("params") ? req["params"] : json::object();

    // Notifications MCP : pas d'`id`, aucune réponse attendue.
    if (method == "notifications/initialized") {
        return json(nullptr);
    }

    try {
        if (method == "initialize") {
            response["result"] = handleInitialize(params);
        } else if (method == "ping") {
            response["result"] = json::object();
        } else if (method == "tools/list") {
            response["result"] = handleToolsList(params);
        } else if (method == "tools/call") {
            response["result"] = handleToolsCall(params);
        } else {
            response["error"] = makeError(-32601, "méthode introuvable : " + method);
        }
    } catch (const std::exception& e) {
        response["error"] = makeError(-32603, std::string("erreur interne : ") + e.what());
    }
    return response;
}

// ---------------------------------------------------------------------------
// Transport stdio
// ---------------------------------------------------------------------------

void McpServer::serveStdio() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        try {
            const json req  = json::parse(line);
            const json resp = handleRequest(req);
            if (resp.is_null()) continue;   // notification : aucune réponse
            std::cout << resp.dump() << "\n";
            std::cout.flush();
        } catch (const json::parse_error&) {
            json err;
            err["jsonrpc"] = "2.0";
            err["id"]      = nullptr;
            err["error"]   = makeError(-32700, "erreur d'analyse");
            std::cout << err.dump() << "\n";
            std::cout.flush();
        }
    }
}

// ---------------------------------------------------------------------------
// Transport TCP (boucle locale uniquement)
// ---------------------------------------------------------------------------

static void handleTcpConnection(socket_t clientFd, McpServer* server) {
    auto readLine = [&](std::string& out) -> bool {
        out.clear();
        char c;
        while (true) {
            long n = recvByte(clientFd, &c);
            if (n <= 0) return !out.empty();
            if (c == '\n') return true;
            out += c;
        }
    };

    std::string line;
    while (readLine(line)) {
        if (line.empty()) continue;
        try {
            const json req  = json::parse(line);
            const json resp = server->handleRequest(req);
            if (resp.is_null()) continue;
            const std::string out = resp.dump() + "\n";
            if (sendAll(clientFd, out.c_str(), out.size()) < 0) break;
        } catch (...) {
            const std::string err =
                R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"erreur d'analyse"}})"
                "\n";
            if (sendAll(clientFd, err.c_str(), err.size()) < 0) break;
        }
    }
    closeSocket(clientFd);
}

void McpServer::serveTcp(uint16_t port) {
#ifdef _WIN32
    static WinsockGuard wsaGuard;  // Winsock initialisé une fois, libéré à l'arrêt.
#endif
    socket_t serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == kInvalidSocket) throw std::runtime_error("socket() a échoué");

    int opt = 1;
    // setsockopt attend un const char* sous Winsock, un const void* sous POSIX :
    // le cast en const char* convient aux deux.
    ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    // Invariant de sécurité : se lier UNIQUEMENT à 127.0.0.1.
    if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        closeSocket(serverFd);
        throw std::runtime_error("inet_pton a échoué");
    }

    if (::bind(serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        closeSocket(serverFd);
        throw std::runtime_error("bind() a échoué sur 127.0.0.1:" + std::to_string(port));
    }
    if (::listen(serverFd, 8) < 0) {
        closeSocket(serverFd);
        throw std::runtime_error("listen() a échoué");
    }

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);
        socket_t clientFd = ::accept(serverFd,
                                     reinterpret_cast<sockaddr*>(&clientAddr),
                                     &clientLen);
        if (clientFd == kInvalidSocket) continue;
        std::thread([clientFd, this]() {
            handleTcpConnection(clientFd, this);
        }).detach();
    }
    closeSocket(serverFd);
}

} // namespace ecu::mcp
