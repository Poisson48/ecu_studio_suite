find_package(Qt6 REQUIRED COMPONENTS Core Sql)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGIT2 libgit2)  # optionnel
find_package(nlohmann_json QUIET)   # optionnel

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

target_compile_options(ecu_core PRIVATE -Wall -Wextra -Wpedantic)
