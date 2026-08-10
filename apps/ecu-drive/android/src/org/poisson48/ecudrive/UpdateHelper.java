package org.poisson48.ecudrive;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

// Installation APK + permissions runtime + export fichiers (ECU Drive).
public class UpdateHelper {

    private static final String TAG = "EcuDriveUpdate";
    public static final String ACTION_INSTALL_STATUS = "org.poisson48.ecudrive.INSTALL_STATUS";
    public static final int REQ_RUNTIME_PERMS = 48048;

    public static boolean installApk(Context ctx, String apkPath) {
        if (ctx == null || apkPath == null)
            return false;

        File apk = new File(apkPath);
        if (!apk.isFile() || apk.length() == 0) {
            Log.e(TAG, "APK manquant ou vide: " + apkPath);
            return false;
        }

        // Android 8+ : sans cette autorisation, l'install est silencieuse / échoue.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            PackageManager pm = ctx.getPackageManager();
            if (!pm.canRequestPackageInstalls()) {
                Log.w(TAG, "REQUEST_INSTALL_PACKAGES non accordé — ouverture des réglages");
                try {
                    Intent settings = new Intent(
                            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + ctx.getPackageName()));
                    settings.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    ctx.startActivity(settings);
                } catch (Exception e) {
                    Log.e(TAG, "Impossible d'ouvrir les réglages d'install", e);
                }
                return false;
            }
        }

        // Chemin fiable : Intent ACTION_VIEW + FileProvider (UI système visible).
        try {
            Uri uri = FileProvider.getUriForFile(
                    ctx, ctx.getPackageName() + ".fileprovider", apk);
            Intent view = new Intent(Intent.ACTION_VIEW);
            view.setDataAndType(uri, "application/vnd.android.package-archive");
            view.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                    | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            ctx.startActivity(view);
            Log.i(TAG, "ACTION_VIEW lancé pour " + apkPath);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "ACTION_VIEW échoué, fallback PackageInstaller", e);
        }

        return installViaPackageInstaller(ctx, apk);
    }

    private static boolean installViaPackageInstaller(Context ctx, File apk) {
        PackageInstaller.Session session = null;
        try {
            PackageInstaller installer = ctx.getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                    PackageInstaller.SessionParams.MODE_FULL_INSTALL);

            int sessionId = installer.createSession(params);
            session = installer.openSession(sessionId);

            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = session.openWrite("ecu-drive", 0, apk.length())) {
                byte[] buffer = new byte[65536];
                int read;
                while ((read = in.read(buffer)) > 0)
                    out.write(buffer, 0, read);
                session.fsync(out);
            }

            Intent status = new Intent(ACTION_INSTALL_STATUS).setPackage(ctx.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                flags |= PendingIntent.FLAG_MUTABLE;

            PendingIntent pending = PendingIntent.getBroadcast(ctx, sessionId, status, flags);
            session.commit(pending.getIntentSender());
            return true;

        } catch (Exception e) {
            Log.e(TAG, "PackageInstaller échoué", e);
            if (session != null)
                session.abandon();
            return false;
        } finally {
            if (session != null)
                session.close();
        }
    }

    /** Partage un fichier local (CSV logs) via le sheet système. */
    public static boolean shareFile(Context ctx, String path, String mimeType) {
        if (ctx == null || path == null)
            return false;
        File f = new File(path);
        if (!f.isFile()) {
            Log.e(TAG, "shareFile manquant: " + path);
            return false;
        }
        try {
            Uri uri = FileProvider.getUriForFile(
                    ctx, ctx.getPackageName() + ".fileprovider", f);
            Intent send = new Intent(Intent.ACTION_SEND);
            send.setType(mimeType != null ? mimeType : "*/*");
            send.putExtra(Intent.EXTRA_STREAM, uri);
            send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_ACTIVITY_NEW_TASK);
            Intent chooser = Intent.createChooser(send, "Partager le log");
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(chooser);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "shareFile échoué", e);
            return false;
        }
    }

    /**
     * URI du fichier ouvert via ACTION_VIEW / ACTION_SEND.
     * clear=true : consomme l'intent pour éviter un rechargement au resume.
     */
    public static String launchContentUri(android.app.Activity activity, boolean clear) {
        if (activity == null)
            return null;
        Intent intent = activity.getIntent();
        if (intent == null)
            return null;
        String action = intent.getAction();
        Uri uri = intent.getData();
        if (uri == null && Intent.ACTION_SEND.equals(action)) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                uri = intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class);
            else
                uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
        }
        if (uri == null)
            return null;
        if (action != null
                && !Intent.ACTION_VIEW.equals(action)
                && !Intent.ACTION_SEND.equals(action))
            return null;
        String out = uri.toString();
        if (clear) {
            intent.setData(null);
            intent.removeExtra(Intent.EXTRA_STREAM);
            activity.setIntent(intent);
        }
        return out;
    }

    /** API 31+ : SCAN+CONNECT. Avant : localisation (requis pour le discovery Classic). */
    public static boolean hasBluetoothPermissions(Context ctx) {
        if (ctx == null)
            return false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN)
                    == PackageManager.PERMISSION_GRANTED
                && ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return ctx.checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED
            || ctx.checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean hasStoragePermissions(Context ctx) {
        if (ctx == null)
            return false;
        // Android 10+ : écriture Downloads via MediaStore sans WRITE.
        // On garde READ (≤32) / WRITE (≤29) pour les chemins legacy + file pickers Qt.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            return true;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return ctx.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    /** Permissions runtime manquantes pour BT + fichiers + localisation legacy. */
    public static String[] missingRuntimePermissions(Context ctx) {
        ArrayList<String> need = new ArrayList<>();
        if (ctx == null)
            return need.toArray(new String[0]);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(android.Manifest.permission.BLUETOOTH_SCAN);
            if (ctx.checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(android.Manifest.permission.BLUETOOTH_CONNECT);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (ctx.checkSelfPermission(android.Manifest.permission.ACCESS_FINE_LOCATION)
                    != PackageManager.PERMISSION_GRANTED
                && ctx.checkSelfPermission(android.Manifest.permission.ACCESS_COARSE_LOCATION)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(android.Manifest.permission.ACCESS_FINE_LOCATION);
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && Build.VERSION.SDK_INT <= Build.VERSION_CODES.Q) {
            if (ctx.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(android.Manifest.permission.WRITE_EXTERNAL_STORAGE);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && Build.VERSION.SDK_INT <= 32) {
            if (ctx.checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED)
                need.add(android.Manifest.permission.READ_EXTERNAL_STORAGE);
        }
        return need.toArray(new String[0]);
    }

    public static boolean hasAllRuntimePermissions(Context ctx) {
        return missingRuntimePermissions(ctx).length == 0;
    }

    /**
     * Affiche le dialogue système pour toutes les permissions manquantes.
     * @return true si un dialogue a été montré.
     */
    public static boolean requestMissingRuntimePermissions(Activity activity) {
        if (activity == null)
            return false;
        String[] missing = missingRuntimePermissions(activity);
        if (missing.length == 0)
            return false;
        Log.i(TAG, "requestPermissions count=" + missing.length);
        activity.requestPermissions(missing, REQ_RUNTIME_PERMS);
        return true;
    }

    /**
     * Copie un fichier vers Téléchargements (MediaStore API 29+, dossier public avant).
     * @return chemin / URI affichable, ou null.
     */
    public static String copyFileToDownloads(Context ctx, String srcPath, String displayName) {
        if (ctx == null || srcPath == null || displayName == null)
            return null;
        File src = new File(srcPath);
        if (!src.isFile() || src.length() == 0) {
            Log.e(TAG, "copyFileToDownloads: source invalide " + srcPath);
            return null;
        }
        String name = displayName;
        if (!name.toLowerCase().endsWith(".csv"))
            name = name + ".csv";

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                ContentValues values = new ContentValues();
                values.put(MediaStore.Downloads.DISPLAY_NAME, name);
                values.put(MediaStore.Downloads.MIME_TYPE, "text/csv");
                values.put(MediaStore.Downloads.IS_PENDING, 1);
                ContentResolver resolver = ctx.getContentResolver();
                Uri uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
                if (uri == null)
                    return null;
                try (InputStream in = new FileInputStream(src);
                     OutputStream out = resolver.openOutputStream(uri)) {
                    if (out == null)
                        return null;
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) >= 0)
                        out.write(buf, 0, n);
                    out.flush();
                }
                values.clear();
                values.put(MediaStore.Downloads.IS_PENDING, 0);
                resolver.update(uri, values, null, null);
                Log.i(TAG, "MediaStore Downloads OK " + uri);
                return uri.toString();
            }

            File dir = Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_DOWNLOADS);
            if (dir == null)
                return null;
            //noinspection ResultOfMethodCallIgnored
            dir.mkdirs();
            File dest = new File(dir, name);
            try (InputStream in = new FileInputStream(src);
                 OutputStream out = new FileOutputStream(dest)) {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) >= 0)
                    out.write(buf, 0, n);
                out.flush();
            }
            Intent scan = new Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE);
            scan.setData(Uri.fromFile(dest));
            ctx.sendBroadcast(scan);
            Log.i(TAG, "Legacy Downloads OK " + dest.getAbsolutePath());
            return dest.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "copyFileToDownloads", e);
            return null;
        }
    }

    public static void openAppDetailsSettings(Context ctx) {
        if (ctx == null)
            return;
        try {
            Intent settings = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    Uri.parse("package:" + ctx.getPackageName()));
            settings.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(settings);
        } catch (Exception e) {
            Log.e(TAG, "openAppDetailsSettings", e);
        }
    }

    public static class InstallReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                                            PackageInstaller.STATUS_FAILURE);
            if (status != PackageInstaller.STATUS_PENDING_USER_ACTION)
                return;

            Intent confirm;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class);
            else
                confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm == null)
                return;
            confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(confirm);
        }
    }
}
