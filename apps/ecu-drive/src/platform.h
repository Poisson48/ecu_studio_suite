#pragma once
#include <QString>

namespace ecu_drive {

// Installe un APK via PackageInstaller (Android). Stub hors Android.
bool platformInstallApk(const QString& apkPath);

} // namespace ecu_drive
