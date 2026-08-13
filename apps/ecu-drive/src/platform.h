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
 * Vérifie d'abord les permissions Android natives (source de vérité), puis
 * redemande via Qt si besoin — ne traite plus Denied comme un refus définitif
 * avant d'avoir rappelé requestPermission.
 * Hors Android / sans Qt Permissions : done(true).
 */
void platformRequestBluetoothPermissions(std::function<void(bool granted)> done);

/**
 * Au démarrage : demande en une fois BT + stockage (+ localisation legacy)
 * si elles ne sont pas déjà accordées. done(true) si tout OK.
 */
void platformRequestStartupPermissions(std::function<void(bool allGranted)> done);

/** Ouvre la fiche app (Réglages → Autorisations). No-op hors Android. */
void platformOpenAppSettings();

/**
 * Copie un fichier vers Téléchargements (MediaStore / public Downloads).
 * Retourne le chemin ou content:// , ou QString() si échec. No-op hors Android.
 */
QString platformSaveToDownloads(const QString& localPath, const QString& displayName);

/**
 * URI du fichier passé à l'Activity (ACTION_VIEW), ou vide.
 * clear=true consomme l'intent (évite de recharger au resume).
 */
QString platformLaunchIntentUri(bool clear = true);

/** Partage un fichier local via le sheet Android (ACTION_SEND). false hors Android / échec. */
bool platformShareFile(const QString& path, const QString& mimeType = QStringLiteral("text/csv"));

/**
 * Empêche la mise en veille / extinction d'écran tant que l'Activity est visible
 * (FLAG_KEEP_SCREEN_ON). No-op hors Android. Pas de permission WAKE_LOCK.
 */
void platformKeepScreenOn(bool on = true);

/**
 * Foreground Service Android pour garder le polling/CSV vivant en arrière-plan.
 * No-op hors Android. Demande POST_NOTIFICATIONS si besoin (API 33+).
 */
void platformStartLoggingService(const QString& title = QString(),
                                 const QString& text = QString());
void platformStopLoggingService();

/** Alerte underboost : ToneGenerator + vibration (Android). Desktop = QApplication::beep. */
void platformAlertBeep();

} // namespace ecu_drive
