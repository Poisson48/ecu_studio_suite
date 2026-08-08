#pragma once
#include <QString>

namespace ecu_drive {

// Installe un APK via PackageInstaller / FileProvider (Android). Stub hors Android.
bool platformInstallApk(const QString& apkPath);

/** Toast Android (toujours visible) — no-op hors Android. */
void platformToast(const QString& message);

} // namespace ecu_drive
