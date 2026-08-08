#include "drive_window.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QTimer>
#include <QMetaObject>


static const char* kDarkQss = R"(
QWidget { background-color: #0f1520; color: #e6edf3; font-size: 14px; }
QPushButton {
  background: #1e293b; border: 1px solid #334155; border-radius: 8px;
  padding: 8px 14px; color: #e6edf3;
}
QPushButton:hover { background: #334155; }
QPushButton#accentBtn {
  background: #2563eb; border: none; font-weight: 700;
}
QPushButton#accentBtn:hover { background: #3b82f6; }
QPushButton#accentBtn:disabled {
  color: #64748b; background: #1e293b; border: 1px solid #334155;
}
QPushButton:disabled { color: #64748b; background: #111827; }
QComboBox, QLineEdit {
  background: #111827; border: 1px solid #334155; border-radius: 6px;
  padding: 6px; min-height: 28px;
}
QCheckBox { spacing: 8px; }
QLabel { background: transparent; }
QScrollArea { background: transparent; border: none; }
QMessageBox { background: #0f1520; }
)";

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ECU Drive"));
    QApplication::setOrganizationName(QStringLiteral("Poisson48"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/ecu_studio_logo.png")));
    app.setStyleSheet(QString::fromUtf8(kDarkQss));

    QString tuneArg;
    bool smokeUi = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == QLatin1String("--smoke-ui")) {
            smokeUi = true;
            continue;
        }
        if ((args[i] == QLatin1String("--tune") || args[i] == QLatin1String("-t"))
            && i + 1 < args.size()) {
            tuneArg = args[++i];
        }
    }

    ecu_drive::DriveWindow w;
    w.show();
    if (smokeUi) {
        // Enchaîne Conduite ↔ Capteurs sans matériel (CI / repro crash Android).
        QTimer::singleShot(50, &w, [&w]() {
            QMetaObject::invokeMethod(&w, "showSensorsPage");
        });
        QTimer::singleShot(150, &w, [&w]() {
            QMetaObject::invokeMethod(&w, "showDrivePage");
        });
        QTimer::singleShot(250, &w, [&w]() {
            QMetaObject::invokeMethod(&w, "showSensorsPage");
        });
        QTimer::singleShot(400, &app, &QApplication::quit);
        return app.exec();
    }
    if (!tuneArg.isEmpty()) {
        const QString path = tuneArg;
        QTimer::singleShot(50, &w, [path, &w]() { w.loadTuneFile(path); });
    }
    return app.exec();
}
