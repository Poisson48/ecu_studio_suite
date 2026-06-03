# ── ecu-mcp ───────────────────────────────────────────────────────────────────
# Serveur MCP (Model Context Protocol) JSON-RPC 2.0 pour ECU Studio.
# Expose les API publiques de libs/ecu-core comme outils MCP, afin que Claude
# (Desktop / Code) puisse piloter le tuning ECU. Transport stdio (lancement par
# Claude) et TCP loopback. Mode headless : opère sur des fichiers ROM par chemin.
find_package(nlohmann_json REQUIRED)

add_library(ecu_mcp STATIC
    src/McpServer.cpp
    src/Tools.cpp
    src/tools_common.cpp
    src/tools_rom.cpp
    src/tools_recipes.cpp
    src/tools_damos.cpp
)

target_include_directories(ecu_mcp
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(ecu_mcp
    PUBLIC  nlohmann_json::nlohmann_json
    PRIVATE ecu_core
)

target_compile_options(ecu_mcp PRIVATE -Wall -Wextra -Wpedantic)
