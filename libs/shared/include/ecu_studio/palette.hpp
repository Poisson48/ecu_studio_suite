#pragma once
// Réexporte la palette de SocketSpy + couleurs ECU-spécifiques
#include "gui_palette.h"

namespace ecu_studio::Palette {
    using namespace socketspy::gui::Palette;
    inline constexpr const char* kRomRead    = "#22c55e";  // lecture OK
    inline constexpr const char* kRomWrite   = "#f59e0b";  // écriture en cours
    inline constexpr const char* kRomError   = "#ef4444";  // erreur flash
    inline constexpr const char* kMapChanged = "#6366f1";  // cellule modifiée
}
