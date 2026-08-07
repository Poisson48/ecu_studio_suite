package org.poisson48.ecudrive;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

// Installation APK depuis ECU Drive (mises à jour GitHub Releases).
// PackageInstaller + confirmation système — pas de FileProvider.
public class UpdateHelper {

    public static final String ACTION_INSTALL_STATUS = "org.poisson48.ecudrive.INSTALL_STATUS";

    public static boolean installApk(Context ctx, String apkPath) {
        if (ctx == null || apkPath == null)
            return false;

        File apk = new File(apkPath);
        if (!apk.isFile() || apk.length() == 0)
            return false;

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
            if (session != null)
                session.abandon();
            return false;
        } finally {
            if (session != null)
                session.close();
        }
    }

    public static class InstallReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context ctx, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                                            PackageInstaller.STATUS_FAILURE);
            if (status != PackageInstaller.STATUS_PENDING_USER_ACTION)
                return;

            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm == null)
                return;
            confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(confirm);
        }
    }
}
