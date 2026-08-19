#include "drive_controller.h"
#include "updater.h"
#include "platform.h"

#include "elm/Elm327.hpp"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("ECU Drive"));
    QGuiApplication::setOrganizationName(QStringLiteral("Poisson48"));
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/ecu_studio_logo.png")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    QString tuneArg, ecuArg;
    bool smokeUi = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--smoke-ui")) { smokeUi = true; }
        else if ((args[i] == QLatin1String("--tune") || args[i] == QLatin1String("-t")) && i + 1 < args.size())
            tuneArg = args[++i];
        else if (args[i] == QLatin1String("--ecu") && i + 1 < args.size())
            ecuArg = args[++i];
    }

    ecu_drive::platformKeepScreenOn(true);

    elm::Elm327* elm = new elm::Elm327(&app);
    ecu_drive::Updater* updater = new ecu_drive::Updater(&app);
    ecu_drive::DriveController* drive = new ecu_drive::DriveController(elm, updater, &app);

    if (!ecuArg.isEmpty()) drive->setAutoEcuId(ecuArg);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("Drive"),   drive);
    engine.rootContext()->setContextProperty(QStringLiteral("Updater"), updater);
    engine.addImportPath(QStringLiteral("qrc:/"));

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/EcuDrive/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    if (smokeUi) {
        QTimer::singleShot(400, &app, &QGuiApplication::quit);
        return app.exec();
    }

    if (!tuneArg.isEmpty())
        QTimer::singleShot(50, drive, [drive, tuneArg]() { drive->loadTuneFile(tuneArg); });

    // Intent Android (fichier ouvert depuis Fichiers)
    QTimer::singleShot(300, drive, [drive]() {
        const QString uri = ecu_drive::platformLaunchIntentUri(true);
        if (!uri.isEmpty()) drive->loadTuneFile(uri);
    });

    return app.exec();
}
