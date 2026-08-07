#include "platform.h"

#ifdef Q_OS_ANDROID
#  include <QCoreApplication>
#  include <QJniObject>
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

#else

bool platformInstallApk(const QString&) { return false; }

#endif

} // namespace ecu_drive
