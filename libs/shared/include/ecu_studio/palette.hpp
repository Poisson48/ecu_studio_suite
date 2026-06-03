#pragma once
// Réexporte la palette de SocketSpy + couleurs ECU-spécifiques
#include "gui_palette.h"

namespace ecu_studio::Palette {
    using namespace socketspy::gui::Palette;
    inline constexpr const char* kRomRead    = "#22c55e";  // lecture OK
    inline constexpr const char* kRomWrite   = "#f59e0b";  // écriture en cours
    inline constexpr const char* kRomError   = "#ef4444";  // erreur flash
    inline constexpr const char* kMapChanged = "#6366f1";  // cellule modifiée

    // ── Badges de maturité du hub (réutilise les teintes de signaux SocketSpy
    // pour rester cohérent avec le thème sombre). Le macro garde permet à
    // hub/maturity_badge.h d'effacer ses copies locales de repli.
    inline constexpr const char* kBadgeProven   = "#22c55e";  // vert  (kSigGreen)
    inline constexpr const char* kBadgeBeta     = "#f59e0b";  // ambre (kSigAmber)
    inline constexpr const char* kBadgeIncoming = "#4b5563";  // gris  (kDeadGray)
}
#define ECU_STUDIO_HAS_BADGE_COLORS 1
