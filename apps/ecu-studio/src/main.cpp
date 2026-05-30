#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ECU Studio");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("ecu-studio-suite");

    QSettings settings;
    QTranslator translator;
    const QString lang = settings.value("language", QString()).toString();
    if (!lang.isEmpty() && translator.load(":/i18n/ecu_studio_" + lang))
        app.installTranslator(&translator);

    // Réutilise le thème SocketSpy directement
    QFile qss(":/theme.qss");
    if (qss.open(QFile::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    ecu_studio::MainWindow window;
    window.show();

    return app.exec();
}
