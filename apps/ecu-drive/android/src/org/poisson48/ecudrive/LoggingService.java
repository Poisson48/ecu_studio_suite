package org.poisson48.ecudrive;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.media.AudioManager;
import android.media.ToneGenerator;
import android.os.Build;
import android.os.IBinder;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;

/**
 * Foreground Service lié à une session de conduite : empêche Android de tuer
 * le process Qt pendant le polling ELM / écriture CSV en arrière-plan.
 */
public class LoggingService extends Service {

    private static final String TAG = "EcuDriveLogSvc";
    public static final String CHANNEL_ID = "ecu_drive_logging";
    public static final int NOTIF_ID = 48049;
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_TEXT = "text";
    public static final String ACTION_STOP = "org.poisson48.ecudrive.STOP_LOGGING";

    @Override
    public void onCreate() {
        super.onCreate();
        ensureChannel(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }

        String title = intent != null ? intent.getStringExtra(EXTRA_TITLE) : null;
        String text = intent != null ? intent.getStringExtra(EXTRA_TEXT) : null;
        if (title == null || title.isEmpty())
            title = "ECU Drive";
        if (text == null || text.isEmpty())
            text = "Logging OBD en cours";

        Notification notif = buildNotification(title, text);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                startForeground(NOTIF_ID, notif,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIF_ID, notif,
                        ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);
            } else {
                startForeground(NOTIF_ID, notif);
            }
        } catch (Exception e) {
            Log.e(TAG, "startForeground échoué", e);
            try {
                startForeground(NOTIF_ID, notif);
            } catch (Exception e2) {
                Log.e(TAG, "startForeground fallback échoué", e2);
            }
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        try {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } catch (Exception ignored) {
        }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private Notification buildNotification(String title, String text) {
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch == null) {
            launch = new Intent(this, org.qtproject.qt.android.bindings.QtActivity.class);
            launch.setAction(Intent.ACTION_MAIN);
            launch.addCategory(Intent.CATEGORY_LAUNCHER);
        }
        launch.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);

        int piFlags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            piFlags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent content = PendingIntent.getActivity(this, 0, launch, piFlags);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            b = new Notification.Builder(this, CHANNEL_ID);
        } else {
            b = new Notification.Builder(this);
        }
        b.setContentTitle(title)
                .setContentText(text)
                .setSmallIcon(android.R.drawable.ic_menu_compass)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setContentIntent(content)
                .setCategory(Notification.CATEGORY_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP)
            b.setVisibility(Notification.VISIBILITY_PUBLIC);
        return b.build();
    }

    public static void ensureChannel(Context ctx) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O || ctx == null)
            return;
        NotificationManager nm = ctx.getSystemService(NotificationManager.class);
        if (nm == null)
            return;
        NotificationChannel ch = nm.getNotificationChannel(CHANNEL_ID);
        if (ch != null)
            return;
        ch = new NotificationChannel(
                CHANNEL_ID,
                "Session conduite",
                NotificationManager.IMPORTANCE_LOW);
        ch.setDescription("Logging OBD en arrière-plan");
        ch.setShowBadge(false);
        nm.createNotificationChannel(ch);
    }

    /** Démarre le FGS (à appeler depuis le thread UI Android). */
    public static void start(Context ctx, String title, String text) {
        if (ctx == null)
            return;
        ensureChannel(ctx);
        Intent i = new Intent(ctx, LoggingService.class);
        i.putExtra(EXTRA_TITLE, title != null ? title : "ECU Drive");
        i.putExtra(EXTRA_TEXT, text != null ? text : "Logging OBD en cours");
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ctx.startForegroundService(i);
            else
                ctx.startService(i);
        } catch (Exception e) {
            Log.e(TAG, "start LoggingService échoué", e);
        }
    }

    public static void stop(Context ctx) {
        if (ctx == null)
            return;
        try {
            ctx.stopService(new Intent(ctx, LoggingService.class));
        } catch (Exception e) {
            Log.e(TAG, "stop LoggingService échoué", e);
        }
    }

    /** POST_NOTIFICATIONS (API 33+) — requis pour afficher la notif FGS. */
    public static boolean hasNotificationPermission(Context ctx) {
        if (ctx == null)
            return false;
        if (Build.VERSION.SDK_INT < 33)
            return true;
        return ctx.checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                == PackageManager.PERMISSION_GRANTED;
    }

    public static boolean requestNotificationPermission(android.app.Activity activity) {
        if (activity == null)
            return false;
        if (Build.VERSION.SDK_INT < 33)
            return false;
        if (hasNotificationPermission(activity))
            return false;
        activity.requestPermissions(
                new String[]{android.Manifest.permission.POST_NOTIFICATIONS},
                48050);
        return true;
    }

    /**
     * Alerte underboost : ton court + vibration.
     * QApplication.beep() est un no-op sur Qt Android.
     */
    public static void alertBeep(Context ctx) {
        if (ctx == null)
            return;
        try {
            ToneGenerator tg = new ToneGenerator(AudioManager.STREAM_ALARM, 80);
            tg.startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD, 280);
            // Libérer après le ton (évite fuite).
            new android.os.Handler(ctx.getMainLooper()).postDelayed(tg::release, 400);
        } catch (Exception e) {
            Log.w(TAG, "ToneGenerator échoué", e);
        }
        try {
            Vibrator vib;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                VibratorManager vm = ctx.getSystemService(VibratorManager.class);
                vib = vm != null ? vm.getDefaultVibrator() : null;
            } else {
                vib = (Vibrator) ctx.getSystemService(Context.VIBRATOR_SERVICE);
            }
            if (vib == null || !vib.hasVibrator())
                return;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vib.vibrate(VibrationEffect.createOneShot(180, VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                vib.vibrate(180);
            }
        } catch (Exception e) {
            Log.w(TAG, "Vibrate échoué", e);
        }
    }
}
