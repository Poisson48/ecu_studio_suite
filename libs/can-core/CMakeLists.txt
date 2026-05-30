# can-core réutilise directement cancore de SocketSpy
# Ce lib est un alias pour ne pas casser les includes internes

add_library(can_core INTERFACE)

target_include_directories(can_core
    INTERFACE
        ${CMAKE_SOURCE_DIR}/apps/socketspy/core/include
        ${CMAKE_SOURCE_DIR}/apps/socketspy/protocols/include
)

# Les implémentations sont dans les libs de SocketSpy — les linker via l'app
