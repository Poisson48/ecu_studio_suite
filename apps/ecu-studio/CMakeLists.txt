find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network LinguistTools)
# OpenSSL::Crypto — vérification de signature Ed25519 des mises à jour (updater).
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ── Sources SocketSpy réutilisées directement ─────────────────────────────────
set(SOCKETSPY_GUI_DIR "${CMAKE_SOURCE_DIR}/apps/socketspy/gui")
set(SOCKETSPY_CORE_DIR "${CMAKE_SOURCE_DIR}/apps/socketspy/core")

# ── cancore : lib de capture SocketCAN de SocketSpy ───────────────────────────
# On reconstruit ici un petit target statique à partir des sources de cancore
# (ring_buffer + can_socket) plutôt que de tirer tout le sous-projet SocketSpy
# (qui réclamerait SerialBus/Charts/Bluetooth). cancore ne dépend que de la libc
# et, sous Linux, de SocketCAN — aucun lien Qt requis. Compilé uniquement sous
# Linux. Si ECU_BUILD_SOCKETSPY=ON, le sous-projet définit déjà `cancore`, donc
# on ne crée notre cible que si elle n'existe pas, pour éviter toute collision.
set(ECU_HAVE_CANCORE FALSE)
if(UNIX AND NOT APPLE AND EXISTS "${SOCKETSPY_CORE_DIR}/src/can_socket.cpp")
    if(TARGET cancore)
        set(ECU_CANCORE_TARGET cancore)
    else()
        add_library(ecu_cancore STATIC
            ${SOCKETSPY_CORE_DIR}/src/ring_buffer.cpp
            ${SOCKETSPY_CORE_DIR}/src/can_socket.cpp
        )
        target_include_directories(ecu_cancore PUBLIC ${SOCKETSPY_CORE_DIR}/include)
        set(ECU_CANCORE_TARGET ecu_cancore)
    endif()
    set(ECU_HAVE_CANCORE TRUE)
endif()

set(ECU_STUDIO_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/splash_screen.cpp
    src/welcome_screen.cpp
    src/rom_document.cpp
    src/panels/mpps_panel.cpp
    src/panels/hex_view_panel.cpp
    src/panels/map_editor_panel.cpp
    src/panels/project_panel.cpp
    src/panels/automods_panel.cpp
    src/panels/checksum_panel.cpp
    src/panels/compare_panel.cpp
    src/panels/git_panel.cpp
    src/panels/a2l_panel.cpp
    src/panels/can_panel.cpp

    # Système de mise à jour intégré (vérifie les releases GitHub)
    src/updater.cpp
    src/update_dialog.cpp

    # SidebarNav de SocketSpy — copié pour que AUTOMOC le traite correctement
    src/sidebar_nav.cpp
)

add_executable(ecu_studio ${ECU_STUDIO_SOURCES})

target_include_directories(ecu_studio
    PRIVATE
        src
        src/panels
        # SocketSpy: gui_palette, dbc_compat (headers seulement)
        ${SOCKETSPY_GUI_DIR}/include
        ${SOCKETSPY_GUI_DIR}/src
        # Libs ECU Studio
        ${CMAKE_SOURCE_DIR}/libs/mpps/include
        ${CMAKE_SOURCE_DIR}/libs/ecu-core/include
        ${CMAKE_SOURCE_DIR}/libs/shared/include
)

target_compile_definitions(ecu_studio PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(ecu_studio
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        mppslib
        ecu_core
        ecu_shared
        OpenSSL::Crypto
)

# ── Moniteur CAN embarqué (cancore) ───────────────────────────────────────────
# Lie la lib cancore et active le code SocketCAN du CanPanel si disponible.
if(ECU_HAVE_CANCORE)
    target_link_libraries(ecu_studio PRIVATE ${ECU_CANCORE_TARGET})
    target_compile_definitions(ecu_studio PRIVATE ECU_HAVE_CANCORE)
endif()

# Le panneau CAN peut lancer le binaire complet SocketSpy s'il a été buildé
# (ECU_BUILD_SOCKETSPY=ON). On passe son nom au code pour la résolution de chemin.
if(ECU_BUILD_SOCKETSPY AND TARGET socketspy)
    target_compile_definitions(ecu_studio PRIVATE ECU_HAVE_SOCKETSPY_APP)
endif()

# ── Ressources : réutilise le thème de SocketSpy ─────────────────────────────
configure_file(
    ${SOCKETSPY_GUI_DIR}/resources/theme.qss
    ${CMAKE_CURRENT_BINARY_DIR}/theme.qss
    COPYONLY
)

qt_add_resources(ecu_studio "ecu_studio_resources"
    PREFIX "/"
    BASE ${CMAKE_CURRENT_BINARY_DIR}
    FILES ${CMAKE_CURRENT_BINARY_DIR}/theme.qss
)

# ── i18n ─────────────────────────────────────────────────────────────────────
qt_add_translations(ecu_studio
    TS_FILES
        i18n/ecu_studio_fr.ts
        i18n/ecu_studio_en.ts
    RESOURCE_PREFIX "/i18n"
    QM_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/translations"
)

install(TARGETS ecu_studio RUNTIME DESTINATION bin)
