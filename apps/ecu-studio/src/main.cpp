#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QTranslator>
#include "main_window.h"
#include "splash_screen.h"
#include "welcome_screen.h"

int main(int argc, char* argv[]) {
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
