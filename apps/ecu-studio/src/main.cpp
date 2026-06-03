#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPixmap>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "ecu/mcp/McpServer.hpp"
#include "main_window.h"
#include "splash_screen.h"
#include "welcome_screen.h"

namespace {

// Détecte le flag `--mcp` (lancement en serveur MCP headless). Renvoie aussi le
// port TCP si `--mcp-tcp <port>` est fourni (sinon transport stdio, par défaut).
bool wantsMcp(int argc, char* argv[], bool& useTcp, uint16_t& tcpPort) {
    bool mcp = false;
    useTcp   = false;
    tcpPort  = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mcp") == 0) {
            mcp = true;
        } else if (std::strcmp(argv[i], "--mcp-tcp") == 0 && i + 1 < argc) {
            mcp     = true;
            useTcp  = true;
            tcpPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
    }
    return mcp;
}

// Lance le serveur MCP (Model Context Protocol) JSON-RPC 2.0 d'ECU Studio. Mode
// headless : on n'utilise qu'un QCoreApplication (aucun affichage requis) car
// libs/ecu-core dépend de Qt Core (QString, QByteArray…).
int runMcp(int argc, char* argv[], bool useTcp, uint16_t tcpPort) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("ecu-studio-mcp");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("ecu-studio-suite");

    ecu::mcp::McpServer server;
    ecu::mcp::registerAllTools(server);

    if (useTcp) {
        std::cerr << "ecu_studio --mcp : serveur MCP TCP sur 127.0.0.1:"
                  << tcpPort << " (" << server.toolCount() << " outils)\n";
        server.serveTcp(tcpPort);
    } else {
        std::cerr << "ecu_studio --mcp : serveur MCP stdio ("
                  << server.toolCount() << " outils)\n";
        server.serveStdio();
    }
    return 0;
}

// Installe l'icône + le .desktop dans ~/.local/share au premier lancement Linux.
// Sans ça, le compositeur Wayland (GNOME, KDE…) affiche une icône vide même si
// QApplication::setWindowIcon() est appelé : il associe les fenêtres à leur
// .desktop via StartupWMClass / AppID, et lit l'icône depuis le thème hicolor.
// On écrit le .desktop avec un Exec= pointant sur l'exécutable courant pour que
// l'entrée du menu d'apps marche aussi en mode dev.
void ensureLinuxDesktopIntegration() {
#ifdef Q_OS_LINUX
    const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataHome.isEmpty()) return;

    const QString iconDir   = dataHome + QStringLiteral("/icons/hicolor/256x256/apps");
    const QString appsDir   = dataHome + QStringLiteral("/applications");
    const QString iconPath  = iconDir  + QStringLiteral("/ecu-studio.png");
    const QString desktopPath = appsDir + QStringLiteral("/ecu-studio.desktop");

    QDir().mkpath(iconDir);
    QDir().mkpath(appsDir);

    // Icône : extraite de la ressource embarquée et redimensionnée à 256×256.
    // Le logo source fait ~1068px ; un fichier hors-taille déposé dans le dossier
    // hicolor « 256x256 » est ignoré par les loaders d'icônes (GNOME/KDE) → icône
    // vide (visible notamment dans l'AppImage). On le scale donc explicitement.
    // Réécrit à chaque lancement pour prendre en compte une nouvelle version.
    if (QFile::exists(iconPath)) QFile::remove(iconPath);
    QPixmap logo(QStringLiteral(":/ecu_studio_logo.png"));
    if (!logo.isNull()) {
        logo.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(iconPath, "PNG");
        QFile::setPermissions(iconPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                        QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }

    // .desktop : régénéré à chaque lancement (Exec= peut changer entre dev et prod).
    const QString exePath = QCoreApplication::applicationFilePath();
    const QString desktopContent = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=ECU Studio\n"
        "GenericName=ECU Reprogramming\n"
        "Comment=Reprogrammation et tuning d'ECU\n"
        "Exec=%1 %u\n"
        "Icon=ecu-studio\n"
        "Terminal=false\n"
        "Categories=Development;Engineering;\n"
        "StartupNotify=true\n"
        "StartupWMClass=ecu_studio\n"
        "Keywords=ECU;tuning;reprogramming;OBD;DAMOS;\n"
    ).arg(exePath);

    QFile df(desktopPath);
    if (df.open(QIODevice::WriteOnly | QIODevice::Text)) {
        df.write(desktopContent.toUtf8());
        df.close();
    }

    // Rafraîchit les caches XDG (best-effort, silencieux si binaires absents).
    QProcess::startDetached(QStringLiteral("update-desktop-database"),
                            { appsDir });
    QProcess::startDetached(QStringLiteral("gtk-update-icon-cache"),
                            { QStringLiteral("-q"), QStringLiteral("-t"),
                              dataHome + QStringLiteral("/icons/hicolor") });
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    // Mode serveur MCP (Claude Desktop / Claude Code) : pilotage headless du
    // tuning ECU via JSON-RPC 2.0. Traité avant la GUI (aucun écran requis).
    {
        bool     useTcp  = false;
        uint16_t tcpPort = 0;
        if (wantsMcp(argc, argv, useTcp, tcpPort))
            return runMcp(argc, argv, useTcp, tcpPort);
    }

    bool headless     = false;
    int  exitAfterSec = -1;

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--exit-after" && i + 1 < argc) {
            exitAfterSec = QString::fromLocal8Bit(argv[++i]).toInt();
        }
    }

    if (headless)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    app.setApplicationName("ECU Studio");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("ecu-studio-suite");
    // setDesktopFileName : indispensable sur Linux/Wayland — le compositeur
    // associe la fenêtre au .desktop installé via StartupWMClass/AppID.
    app.setDesktopFileName(QStringLiteral("ecu-studio"));
    // Icône d'application (X11, certains compositeurs, fenêtres enfants).
    app.setWindowIcon(QIcon(":/ecu_studio_logo.png"));
    // Installe le .desktop + l'icône dans ~/.local/share au premier lancement
    // (Linux uniquement). Permet à GNOME/KDE d'afficher l'icône dans la barre
    // des tâches via StartupWMClass=ecu_studio.
    ensureLinuxDesktopIntegration();

    // Réutilise le thème SocketSpy directement
    QFile qss(":/theme.qss");
    if (qss.open(QFile::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // Splash de démarrage (ignoré en mode headless / CI).
    ecu_studio::SplashScreen* splash = nullptr;
    if (!headless) {
        splash = new ecu_studio::SplashScreen();
        splash->show();
        app.processEvents();
    }

    ecu_studio::MainWindow window;

    if (!headless) {
        window.show();
        if (splash) {
            QTimer::singleShot(1500, splash, [splash, &window]() {
                splash->finish(&window);
                if (ecu_studio::WelcomeScreen::shouldShow())
                    window.showWelcome();
            });
        } else if (ecu_studio::WelcomeScreen::shouldShow()) {
            QTimer::singleShot(0, &window, [&window]() { window.showWelcome(); });
        }
    }

    if (exitAfterSec > 0)
        QTimer::singleShot(exitAfterSec * 1000, &app, &QApplication::quit);

    return app.exec();
}
