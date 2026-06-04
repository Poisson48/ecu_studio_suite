#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace ecu_studio {

// ───────────────────────────────────────────────────────────────────────────
// Catalogue pur-données des sous-programmes spécialisés du hub ECU Studio
// (SocketSpy, SocketSpy MCP, scanner OBD…).
//
// Ce module n'embarque AUCUNE dépendance Qt UI : uniquement QtCore
// (QString / QStringList / QList / QCoreApplication / QFileInfo /
// QStandardPaths). Il ne dépend d'aucun autre module nouveau du hub, ce qui
// permet de le construire et de le tester unitairement en isolation.
// ───────────────────────────────────────────────────────────────────────────

// Niveau de maturité d'un sous-programme, affiché sous forme de badge.
enum class Maturity {
    Proven,    // Éprouvé — stable, recommandé en production.
    Beta,      // Bêta — fonctionnel mais en cours de stabilisation.
    Incoming   // À venir — annoncé, pas encore disponible.
};

// Description statique d'un sous-programme lançable depuis le hub.
struct SubProgram {
    QString     id;           // Identifiant stable (slug), ex. « socketspy ».
    QString     name;         // Nom lisible affiché à l'utilisateur.
    QString     icon;         // Icône emoji/UTF-8 (cf. pattern SidebarNav).
    QString     description;  // Description courte d'une ligne.
    Maturity    maturity;     // Niveau de maturité (badge).
    QString     execName;     // Nom de l'exécutable (sans extension OS).
    QStringList args;         // Arguments par défaut passés au lancement.
    bool        external;     // true si binaire externe (résolu via resolveExec).
    // Dépôt GitHub « Owner/Name » publiant une release AppImage téléchargeable
    // (release-manifest.json). Vide = pas de téléchargement automatique (l'user
    // doit fournir le binaire). Sert au bouton « Télécharger » du hub.
    QString     downloadRepo;
};

// Accesseurs statiques sur le catalogue (aucune instance à créer).
class SubProgramRegistry {
public:
    // Liste complète et ordonnée des sous-programmes connus.
    static const QList<SubProgram>& all();

    // Recherche par identifiant ; renvoie true et remplit `out` si trouvé.
    static bool byId(const QString& id, SubProgram& out);

    // Résout le chemin absolu de l'exécutable d'un sous-programme en
    // reproduisant la recherche multi-layout de CanPanel::resolveSocketSpyPath
    // (appDir, ../socketspy, ../apps/socketspy/gui, ../../apps/socketspy/gui,
    // puis QStandardPaths::findExecutable). Renvoie une chaîne vide si
    // introuvable. `execName` est attendu sans extension : « .exe » est ajouté
    // automatiquement sous Windows.
    static QString resolveExec(const QString& execName);

    // Dossier inscriptible où sont déposés les binaires téléchargés depuis le
    // hub (<AppDataLocation>/tools). resolveExec l'inspecte en priorité, ce qui
    // permet de lancer un sous-programme téléchargé sans recompiler la suite.
    static QString toolsDir();

    // Libellé lisible (traduisible) du niveau de maturité.
    static QString maturityLabel(Maturity maturity);

private:
    SubProgramRegistry() = delete;
};

} // namespace ecu_studio
