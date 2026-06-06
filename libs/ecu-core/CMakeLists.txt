find_package(Qt6 REQUIRED COMPONENTS Core Sql)
find_package(nlohmann_json QUIET)   # optionnel

# libgit2 (optionnel) — active GitManager (historique/undo-redo des ROMs).
# Sous Linux on passe par pkg-config ; sous Windows (vcpkg) il n'y a pas de
# pkg-config fiable, on retombe alors sur le package CMake config de vcpkg.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBGIT2 libgit2)
endif()
if(NOT LIBGIT2_FOUND)
    # vcpkg expose libgit2 sous différents noms de package/cible selon les
    # versions : on tente les variantes connues et on garde la première qui sort
    # une cible importable.
    find_package(libgit2 CONFIG QUIET)
    if(NOT TARGET libgit2::libgit2)
        find_package(unofficial-libgit2 CONFIG QUIET)
    endif()
    foreach(_git2_target libgit2::libgit2 unofficial::libgit2 libgit2package libgit2)
        if(TARGET ${_git2_target})
            set(LIBGIT2_FOUND TRUE)
            set(LIBGIT2_INCLUDE_DIRS "")
            set(LIBGIT2_LIBRARIES ${_git2_target})
            break()
        endif()
    endforeach()
endif()
if(NOT LIBGIT2_FOUND)
    # Dernier recours : on localise l'en-tête + la lib directement dans
    # CMAKE_PREFIX_PATH (couvre vcpkg, où la lib s'appelle git2.lib/libgit2).
    find_path(LIBGIT2_INCLUDE_DIRS NAMES git2.h)
    find_library(LIBGIT2_LIBRARIES NAMES git2 libgit2 git2.dll)
    if(LIBGIT2_INCLUDE_DIRS AND LIBGIT2_LIBRARIES)
        set(LIBGIT2_FOUND TRUE)
    endif()
endif()
if(LIBGIT2_FOUND)
    message(STATUS "ECU Studio : libgit2 trouvé (${LIBGIT2_LIBRARIES}) — GitManager activé.")
else()
    message(STATUS "ECU Studio : libgit2 INTROUVABLE — Versions/undo-redo désactivés.")
endif()

set(ECU_CORE_SOURCES
    src/EcuCatalog.cpp
    src/VehicleTemplates.cpp
    src/RomPatcher.cpp
    src/MapFinder.cpp
    src/MapDiffer.cpp
    src/ProjectManager.cpp
    src/A2lParser.cpp
    src/ChecksumEngine.cpp
)

list(APPEND ECU_CORE_SOURCES src/OpenDamos.cpp)
list(APPEND ECU_CORE_SOURCES src/OpenDamosRelocate.cpp)
list(APPEND ECU_CORE_SOURCES src/OpenDamosSerialize.cpp)
list(APPEND ECU_CORE_SOURCES src/OpenDamosRecipes.cpp)
list(APPEND ECU_CORE_SOURCES src/OpenDamosA2lExport.cpp)
list(APPEND ECU_CORE_SOURCES src/ReportGenerator.cpp)
list(APPEND ECU_CORE_SOURCES src/WinolsParser.cpp)
list(APPEND ECU_CORE_SOURCES src/OlsImport.cpp)

# GitManager optionnel (libgit2 requis)
if(LIBGIT2_FOUND)
    list(APPEND ECU_CORE_SOURCES src/GitManager.cpp)
endif()

add_library(ecu_core STATIC ${ECU_CORE_SOURCES})

target_include_directories(ecu_core
    PUBLIC  include
    PRIVATE src
            ${LIBGIT2_INCLUDE_DIRS}
)

find_package(ZLIB REQUIRED)

target_link_libraries(ecu_core
    PUBLIC  Qt6::Core Qt6::Sql
    PRIVATE ZLIB::ZLIB
)
if(nlohmann_json_FOUND)
    target_link_libraries(ecu_core PUBLIC nlohmann_json::nlohmann_json)
endif()
if(LIBGIT2_FOUND)
    target_link_libraries(ecu_core PRIVATE ${LIBGIT2_LIBRARIES})
    # PUBLIC : la macro doit être visible par les consommateurs (ecu-studio) qui
    # gardent `#ifdef ECU_GIT_AVAILABLE` autour de l'API GitManager.
    target_compile_definitions(ecu_core PUBLIC ECU_GIT_AVAILABLE)
endif()

target_compile_options(ecu_core PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>)
