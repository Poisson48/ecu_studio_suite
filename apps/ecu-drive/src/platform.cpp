#include "platform.h"

#ifdef Q_OS_ANDROID
#  include <QCoreApplication>
#  include <QJniObject>
#  include <QJniEnvironment>
#endif

namespace ecu_drive {

#ifdef Q_OS_ANDROID

namespace {

constexpr const char* kUpdateHelper = "org/poisson48/ecudrive/UpdateHelper";

QJniObject androidContext()
{
    return QJniObject{ QNativeInterface::QAndroidApplication::context() };
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
    // Toast.makeText(context, text, LENGTH_LONG).show();
    QJniObject toast = QJniObject::callStaticObjectMethod(
        "android/widget/Toast", "makeText",
        "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;",
        ctx.object(),
        jMsg.object<jstring>(),
        jint(1) /* LENGTH_LONG */);
    if (toast.isValid())
        toast.callMethod<void>("show", "()V");
}

#else

bool platformInstallApk(const QString&) { return false; }
void platformToast(const QString&) {}

#endif

} // namespace ecu_drive
