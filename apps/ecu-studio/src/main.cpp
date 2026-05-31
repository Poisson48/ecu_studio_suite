#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QTranslator>

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

    // Langue : préférence enregistrée d'abord, sinon français (langue de base).
    QSettings settings;
    QTranslator translator;
    const QString lang = settings.value("language", QString()).toString();
    if (!lang.isEmpty() && translator.load(":/i18n/ecu_studio_" + lang))
        app.installTranslator(&translator);

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
