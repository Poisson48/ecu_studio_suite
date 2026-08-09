package org.poisson48.ecudrive;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

// Installation APK depuis ECU Drive (mises à jour GitHub Releases).
public class UpdateHelper {

    private static final String TAG = "EcuDriveUpdate";
    public static final String ACTION_INSTALL_STATUS = "org.poisson48.ecudrive.INSTALL_STATUS";

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
