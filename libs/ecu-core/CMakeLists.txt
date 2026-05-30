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
)

list(APPEND ECU_CORE_SOURCES src/OpenDamos.cpp)

# GitManager optionnel (libgit2 requis)
if(LIBGIT2_FOUND)
    list(APPEND ECU_CORE_SOURCES src/GitManager.cpp)
    add_compile_definitions(ECU_GIT_AVAILABLE)
endif()

add_library(ecu_core STATIC ${ECU_CORE_SOURCES})

target_include_directories(ecu_core
    PUBLIC  include
    PRIVATE src
            ${LIBGIT2_INCLUDE_DIRS}
)

target_link_libraries(ecu_core
    PUBLIC  Qt6::Core Qt6::Sql
)
if(nlohmann_json_FOUND)
    target_link_libraries(ecu_core PUBLIC nlohmann_json::nlohmann_json)
endif()
if(LIBGIT2_FOUND)
    target_link_libraries(ecu_core PRIVATE ${LIBGIT2_LIBRARIES})
endif()

target_compile_options(ecu_core PRIVATE -Wall -Wextra -Wpedantic)
