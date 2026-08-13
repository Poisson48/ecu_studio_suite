#include "platform.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#ifdef Q_OS_ANDROID
#  include <QJniObject>
#  include <QJniEnvironment>
#  if QT_CONFIG(permissions)
#    include <QPermissions>
#  endif
#endif

namespace ecu_drive {

#ifdef Q_OS_ANDROID

namespace {

constexpr const char* kUpdateHelper = "org/poisson48/ecudrive/UpdateHelper";

QJniObject androidContext()
{
    return QJniObject{ QNativeInterface::QAndroidApplication::context() };
}

QJniObject androidActivity()
{
    return QJniObject::callStaticObjectMethod(
        "org/qtproject/qt/android/QtNative", "activity",
        "()Landroid/app/Activity;");
}

bool androidHasBluetoothPermissions()
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kUpdateHelper, "hasBluetoothPermissions",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

bool androidHasAllRuntimePermissions()
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kUpdateHelper, "hasAllRuntimePermissions",
        "(Landroid/content/Context;)Z",
        ctx.object());
}

bool androidRequestMissingRuntimePermissions()
{
    const QJniObject activity = androidActivity();
    if (!activity.isValid())
        return false;
    return QJniObject::callStaticMethod<jboolean>(
        kUpdateHelper, "requestMissingRuntimePermissions",
        "(Landroid/app/Activity;)Z",
        activity.object<jobject>());
}

void pollUntilPermissionsSettled(std::function<void(bool)> done)
{
    auto* timer = new QTimer(qApp);
    timer->setInterval(350);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, done, ticks = 0]() mutable {
        ++ticks;
        if (androidHasAllRuntimePermissions()) {
            timer->stop();
            timer->deleteLater();
            done(true);
            return;
        }
        // ~20 s : dialogue traité ou ignoré
        if (ticks >= 57) {
            timer->stop();
            timer->deleteLater();
            done(androidHasAllRuntimePermissions());
        }
    });
    timer->start();
}

} // namespace

bool platformInstallApk(const QString& apkPath)
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;

    const QJniObject jPath = QJniObject::fromString(apkPath);
    return QJniObject::callStaticMethod<jboolean>(
        kUpdateHelper, "installApk",
        "(Landroid/content/Context;Ljava/lang/String;)Z",
        ctx.object(), jPath.object<jstring>());
}

void platformToast(const QString& message)
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;
    const QJniObject jMsg = QJniObject::fromString(message);
    QJniObject toast = QJniObject::callStaticObjectMethod(
        "android/widget/Toast", "makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
        ctx.object(),
        jMsg.object<jstring>(),
        jint(1) /* LENGTH_LONG */);
    if (toast.isValid())
        toast.callMethod<void>("show", "()V");
}

void platformOpenAppSettings()
{
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kUpdateHelper, "openAppDetailsSettings",
        "(Landroid/content/Context;)V",
        ctx.object());
}

QString platformSaveToDownloads(const QString& localPath, const QString& displayName)
{
    if (localPath.isEmpty() || !QFileInfo::exists(localPath))
        return {};
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return {};
    const QJniObject jPath = QJniObject::fromString(localPath);
    const QJniObject jName = QJniObject::fromString(
        displayName.isEmpty() ? QFileInfo(localPath).fileName() : displayName);
    const QJniObject jOut = QJniObject::callStaticObjectMethod(
        kUpdateHelper, "copyFileToDownloads",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        ctx.object(), jPath.object<jstring>(), jName.object<jstring>());
    if (!jOut.isValid())
        return {};
    return jOut.toString();
}

void platformRequestBluetoothPermissions(std::function<void(bool granted)> done)
{
    if (!done)
        return;

    if (androidHasBluetoothPermissions()) {
        done(true);
        return;
    }

#if QT_CONFIG(permissions)
    auto finish = [done](bool /*ignored*/) {
        done(androidHasBluetoothPermissions());
    };

    QBluetoothPermission bt;
    bt.setCommunicationModes(QBluetoothPermission::Access);
    qApp->requestPermission(bt, qApp, [finish](const QPermission&) {
        if (androidHasBluetoothPermissions()) {
            finish(true);
            return;
        }
        QLocationPermission loc;
        loc.setAccuracy(QLocationPermission::Precise);
        if (qApp->checkPermission(loc) == Qt::PermissionStatus::Granted) {
            finish(false);
            return;
        }
        qApp->requestPermission(loc, qApp, [finish](const QPermission&) {
            finish(androidHasBluetoothPermissions());
        });
    });
#else
    // Fallback : dialogue système natif (toutes perms runtime).
    if (!androidRequestMissingRuntimePermissions()) {
        done(androidHasBluetoothPermissions());
        return;
    }
    pollUntilPermissionsSettled([done](bool) {
        done(androidHasBluetoothPermissions());
    });
#endif
}

void platformRequestStartupPermissions(std::function<void(bool allGranted)> done)
{
    if (!done)
        return;

    if (androidHasAllRuntimePermissions()) {
        done(true);
        return;
    }

    // Un seul dialogue système pour BT + stockage (+ localisation si besoin).
    if (!androidRequestMissingRuntimePermissions()) {
        done(androidHasAllRuntimePermissions());
        return;
    }
    pollUntilPermissionsSettled(std::move(done));
}

QString platformLaunchIntentUri(bool clear)
{
    const QJniObject activity = androidActivity();
    if (!activity.isValid())
        return {};
    const QJniObject jUri = QJniObject::callStaticObjectMethod(
        kUpdateHelper, "launchContentUri",
        "(Landroid/app/Activity;Z)Ljava/lang/String;",
        activity.object<jobject>(), jboolean(clear ? JNI_TRUE : JNI_FALSE));
    if (!jUri.isValid())
        return {};
    return jUri.toString();
}

bool platformShareFile(const QString& path, const QString& mimeType)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return false;
    const QJniObject ctx = androidContext();
    if (!ctx.isValid())
        return false;
    const QJniObject jPath = QJniObject::fromString(path);
    const QJniObject jMime = QJniObject::fromString(
        mimeType.isEmpty() ? QStringLiteral("*/*") : mimeType);
    return QJniObject::callStaticMethod<jboolean>(
        kUpdateHelper, "shareFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Z",
        ctx.object(), jPath.object<jstring>(), jMime.object<jstring>());
}

void platformKeepScreenOn(bool on)
{
    auto apply = [on]() {
        const QJniObject activity = androidActivity();
        if (!activity.isValid())
            return false;
        const QJniObject window = activity.callObjectMethod(
            "getWindow", "()Landroid/view/Window;");
        if (!window.isValid())
            return false;
        // android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
        constexpr jint kFlagKeepScreenOn = 0x00000080;
        if (on)
            window.callMethod<void>("addFlags", "(I)V", kFlagKeepScreenOn);
        else
            window.callMethod<void>("clearFlags", "(I)V", kFlagKeepScreenOn);
        return true;
    };

    // Window flags must be touched on the Android UI thread.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([apply]() {
        if (apply())
            return;
        // Activity pas encore prête au tout premier frame — un seul retry.
        QTimer::singleShot(200, qApp, [apply]() {
            QNativeInterface::QAndroidApplication::runOnAndroidMainThread([apply]() {
                apply();
            });
        });
    });
}

constexpr const char* kLoggingService = "org/poisson48/ecudrive/LoggingService";

void platformStartLoggingService(const QString& title, const QString& text)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([title, text]() {
        const QJniObject activity = androidActivity();
        const QJniObject ctx = androidContext();
        if (!ctx.isValid())
            return;
        if (activity.isValid()) {
            QJniObject::callStaticMethod<jboolean>(
                kLoggingService, "requestNotificationPermission",
                "(Landroid/app/Activity;)Z",
                activity.object<jobject>());
        }
        const QJniObject jTitle = QJniObject::fromString(
            title.isEmpty() ? QStringLiteral("ECU Drive") : title);
        const QJniObject jText = QJniObject::fromString(
            text.isEmpty() ? QStringLiteral("Logging OBD en cours") : text);
        QJniObject::callStaticMethod<void>(
            kLoggingService, "start",
            "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
            ctx.object(), jTitle.object<jstring>(), jText.object<jstring>());
    });
}

void platformStopLoggingService()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        const QJniObject ctx = androidContext();
        if (!ctx.isValid())
            return;
        QJniObject::callStaticMethod<void>(
            kLoggingService, "stop",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
}

void platformAlertBeep()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        const QJniObject ctx = androidContext();
        if (!ctx.isValid())
            return;
        QJniObject::callStaticMethod<void>(
            kLoggingService, "alertBeep",
            "(Landroid/content/Context;)V",
            ctx.object());
    });
}

#else

bool platformInstallApk(const QString&) { return false; }
void platformToast(const QString&) {}
void platformOpenAppSettings() {}
QString platformSaveToDownloads(const QString&, const QString&) { return {}; }
void platformRequestBluetoothPermissions(std::function<void(bool granted)> done)
{
    if (done) done(true);
}
void platformRequestStartupPermissions(std::function<void(bool allGranted)> done)
{
    if (done) done(true);
}
QString platformLaunchIntentUri(bool) { return {}; }
bool platformShareFile(const QString&, const QString&) { return false; }
void platformKeepScreenOn(bool) {}
void platformStartLoggingService(const QString&, const QString&) {}
void platformStopLoggingService() {}
void platformAlertBeep()
{
    QApplication::beep();
}

#endif

} // namespace ecu_drive
