#pragma once
#include <QString>
#include <functional>

namespace ecu_drive {

// Installe un APK via PackageInstaller / FileProvider (Android). Stub hors Android.
bool platformInstallApk(const QString& apkPath);

/** Toast Android (toujours visible) — no-op hors Android. */
void platformToast(const QString& message);

/**
 * Demande les permissions Bluetooth (et localisation si besoin API < 31).
 * Appelle done(granted) de façon synchrone si déjà décidé, sinon après le dialogue système.
 * Hors Android / sans Qt Permissions : done(true).
 */
void platformRequestBluetoothPermissions(std::function<void(bool granted)> done);

/**
 * URI du fichier passé à l'Activity (ACTION_VIEW), ou vide.
 * clear=true consomme l'intent (évite de recharger au resume).
 */
QString platformLaunchIntentUri(bool clear = true);

/** Partage un fichier local via le sheet Android (ACTION_SEND). false hors Android / échec. */
bool platformShareFile(const QString& path, const QString& mimeType = QStringLiteral("text/csv"));

} // namespace ecu_drive
