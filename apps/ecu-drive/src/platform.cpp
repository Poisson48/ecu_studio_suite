#include "platform.h"

#include <QCoreApplication>
#include <QFileInfo>

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

void platformRequestBluetoothPermissions(std::function<void(bool granted)> done)
{
    if (!done)
        return;

    // Source de vérité = PackageManager Android (pas seulement l'état Qt).
    // Qt renvoyait parfois Denied alors que « Appareils à proximité » était déjà ON,
    // et l'ancien code abandonnait sans rappeler requestPermission.
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
    // Toujours redemander si pas accordé côté Android — y compris après Denied
    // (l'utilisateur a pu activer dans Réglages, ou le dialogue n'a jamais été montré).
    qApp->requestPermission(bt, qApp, [finish](const QPermission&) {
        if (androidHasBluetoothPermissions()) {
            finish(true);
            return;
        }
        // API < 31 : le discovery Classic exige souvent la localisation.
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
    done(false);
#endif
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

#else

bool platformInstallApk(const QString&) { return false; }
void platformToast(const QString&) {}
void platformOpenAppSettings() {}
void platformRequestBluetoothPermissions(std::function<void(bool granted)> done)
{
    if (done) done(true);
}
QString platformLaunchIntentUri(bool) { return {}; }
bool platformShareFile(const QString&, const QString&) { return false; }

#endif

} // namespace ecu_drive
