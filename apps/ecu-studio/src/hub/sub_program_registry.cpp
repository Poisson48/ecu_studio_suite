#include "sub_program_registry.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>

namespace ecu_studio {

// ───────────────────────────────────────────────────────────────────────────
// Catalogue statique. Construit une seule fois (initialisation paresseuse via
// une statique locale) puis renvoyé par référence constante. L'ordre de la
// liste est celui d'affichage dans le hub.
// ───────────────────────────────────────────────────────────────────────────
const QList<SubProgram>& SubProgramRegistry::all() {
    static const QList<SubProgram> catalog = {
        SubProgram{
            QStringLiteral("socketspy"),
            QStringLiteral("SocketSpy"),
            QStringLiteral("🛰️"),
            QObject::tr("Analyseur de bus CAN/SocketCAN temps réel."),
            Maturity::Proven,
            QStringLiteral("socketspy"),
            QStringList{},
            /*external=*/true,
            /*downloadRepo=*/QStringLiteral("Poisson48/SocketSpy"),
        },
        SubProgram{
            QStringLiteral("socketspy-mcp"),
            QStringLiteral("SocketSpy MCP"),
            QStringLiteral("🔌"),
            QObject::tr("Serveur MCP exposant SocketSpy aux agents IA."),
            Maturity::Beta,
            QStringLiteral("socketspy-mcp"),
            QStringList{QStringLiteral("--stdio")},
            /*external=*/true,
            /*downloadRepo=*/QString(),
        },
        SubProgram{
            QStringLiteral("obd-scanner"),
            QStringLiteral("OBD Scanner"),
            QStringLiteral("🩺"),
            QObject::tr("Scanner de diagnostic OBD-II (codes défaut, PIDs)."),
            Maturity::Incoming,
            QStringLiteral("obd-scanner"),
            QStringList{},
            /*external=*/true,
            /*downloadRepo=*/QString(),
        },
    };
    return catalog;
}

bool SubProgramRegistry::byId(const QString& id, SubProgram& out) {
    for (const SubProgram& sp : all()) {
        if (sp.id == id) {
            out = sp;
            return true;
        }
    }
    return false;
}

// Reproduit fidèlement CanPanel::resolveSocketSpyPath : recherche multi-layout
// à côté d'ecu_studio puis dans l'arbre de build, et enfin dans le PATH.
QString SubProgramRegistry::resolveExec(const QString& execName) {
    if (execName.isEmpty())
        return QString();

    // Ajoute l'extension OS si nécessaire (execName est fourni sans suffixe).
    QString exe = execName;
#ifdef Q_OS_WIN
    if (!exe.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
        exe += QStringLiteral(".exe");
#endif

    // Emplacements candidats : d'abord le dossier des binaires téléchargés
    // (writable, survit aux AppImage en lecture seule), puis à côté d'ecu_studio
    // et dans l'arbre de build.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        toolsDir() + QStringLiteral("/") + exe,                     // binaire téléchargé
        appDir + QStringLiteral("/") + exe,
        appDir + QStringLiteral("/../socketspy/") + exe,            // build/apps/.. layout
        appDir + QStringLiteral("/../apps/socketspy/gui/") + exe,
        appDir + QStringLiteral("/../../apps/socketspy/gui/") + exe,
    };
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }

    // Dernier recours : dans le PATH.
    const QString inPath = QStandardPaths::findExecutable(exe);
    return inPath; // chaîne vide si introuvable
}

QString SubProgramRegistry::toolsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/tools");
}

QString SubProgramRegistry::maturityLabel(Maturity maturity) {
    switch (maturity) {
    case Maturity::Proven:
        return QObject::tr("Éprouvé");
    case Maturity::Beta:
        return QObject::tr("Bêta");
    case Maturity::Incoming:
        return QObject::tr("À venir");
    }
    return QString();
}

} // namespace ecu_studio
