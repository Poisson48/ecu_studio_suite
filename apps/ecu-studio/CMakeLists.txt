find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent LinguistTools)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ── Sources SocketSpy réutilisées directement ─────────────────────────────────
set(SOCKETSPY_GUI_DIR "${CMAKE_SOURCE_DIR}/apps/socketspy/gui")

set(ECU_STUDIO_SOURCES
    src/main.cpp
    src/main_window.cpp
    src/panels/mpps_panel.cpp
    src/panels/hex_view_panel.cpp
    src/panels/map_editor_panel.cpp
    src/panels/project_panel.cpp
    src/panels/automods_panel.cpp
    src/panels/checksum_panel.cpp
    src/panels/compare_panel.cpp
    src/panels/git_panel.cpp
    src/panels/a2l_panel.cpp

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
        mppslib
        ecu_core
        ecu_shared
)

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
