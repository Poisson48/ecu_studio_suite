#pragma once
// ─── MaturityBadge ───────────────────────────────────────────────────────────
// Petit widget « pilule » (pill) réutilisable, dérivé de QLabel, qui affiche le
// niveau de maturité d'une fonctionnalité : Proven / Beta / Incoming.
//
// Conçu pour être posé indifféremment :
//   • sur les tuiles du HubLauncher (vue d'accueil « hub ») ;
//   • sur n'importe quel panneau existant (CanPanel, DamosEditorPanel…) SANS
//     avoir à les modifier — d'où le constructeur par défaut + setMaturity().
//
// Il ne dépend QUE de l'énum Maturity de sub_program_registry.h (include, pas de
// lien : aucune dépendance vers HubLauncher, c'est l'inverse — le hub dépend de
// ce badge). La couleur provient des constantes de palette ECU Studio.
#include <QLabel>

#include "hub/sub_program_registry.h"   // SubProgramRegistry::Maturity
#include "ecu_studio/palette.hpp"        // couleurs de marque (dark theme)

class QWidget;

namespace ecu_studio {

// ── Couleurs des pilules de maturité ─────────────────────────────────────────
// Idéalement portées par ecu_studio/palette.hpp (fichier partagé). On les déclare
// ici sous garde #ifndef pour rester autonome : si une passe d'intégration les
// ajoute à la palette, ces définitions s'effacent automatiquement et c'est la
// version partagée qui prévaut.
namespace Palette {
#ifndef ECU_STUDIO_HAS_BADGE_COLORS
inline constexpr const char* kBadgeProven   = "#22c55e";  // vert  — éprouvé
inline constexpr const char* kBadgeBeta     = "#f59e0b";  // ambre — bêta
inline constexpr const char* kBadgeIncoming = "#6366f1";  // indigo — à venir
#endif
}  // namespace Palette

// Pilule de maturité. QLabel stylé via QSS inline (coins arrondis + couleur
// fonction du niveau). objectName = "maturityBadge" pour que theme.qss puisse la
// cibler ultérieurement (QLabel#maturityBadge { … }).
class MaturityBadge : public QLabel {
    Q_OBJECT
public:
    using Maturity = ecu_studio::Maturity;

    // Constructeur principal : badge initialisé à un niveau de maturité donné.
    explicit MaturityBadge(Maturity maturity, QWidget* parent = nullptr);
    // Constructeur par défaut : badge « Incoming » que l'on configure ensuite via
    // setMaturity(). Pratique pour le poser en Designer / sur un panneau existant.
    explicit MaturityBadge(QWidget* parent = nullptr);
    ~MaturityBadge() override = default;

    // Met à jour le niveau (texte + couleur). Idempotent.
    void setMaturity(Maturity maturity);
    Maturity maturity() const { return m_maturity; }

    // Libellé traduisible correspondant à un niveau (Proven/Beta/Incoming).
    static QString labelFor(Maturity maturity);

private:
    // Applique texte, couleur et feuille de style « pilule » selon m_maturity.
    void applyStyle();
    // Couleur de fond hex (#rrggbb) associée à un niveau, depuis la palette.
    static const char* colorFor(Maturity maturity);

    Maturity m_maturity{Maturity::Incoming};
};

}  // namespace ecu_studio
