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
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.provider.Settings;
import android.util.Log;

/**
 * Foreground Service lié à une session de conduite : empêche Android de tuer
 * / endormir le process Qt pendant le polling ELM / écriture CSV en arrière-plan.
 *
 * - startForeground (type connectedDevice) → process exempté des kill agressifs
 * - PARTIAL_WAKE_LOCK → CPU reste actif écran éteint (timers Qt / BT)
 */
public class LoggingService extends Service {

    private static final String TAG = "EcuDriveLogSvc";
    public static final String CHANNEL_ID = "ecu_drive_logging";
    public static final int NOTIF_ID = 48049;
    public static final String EXTRA_TITLE = "title";
    public static final String EXTRA_TEXT = "text";
    public static final String ACTION_STOP = "org.poisson48.ecudrive.STOP_LOGGING";
    public static final String ACTION_UPDATE = "org.poisson48.ecudrive.UPDATE_LOGGING";

    private PowerManager.WakeLock mWakeLock;
    private String mTitle = "ECU Drive";
    private String mText = "Logging OBD en cours";

    @Override
    public void onCreate() {
        super.onCreate();
        ensureChannel(this);
        acquireWakeLock();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            releaseWakeLock();
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }

        if (intent != null) {
            if (ACTION_UPDATE.equals(intent.getAction())) {
                String t = intent.getStringExtra(EXTRA_TEXT);
                if (t != null && !t.isEmpty())
                    mText = t;
                String ti = intent.getStringExtra(EXTRA_TITLE);
                if (ti != null && !ti.isEmpty())
                    mTitle = ti;
            } else {
                String title = intent.getStringExtra(EXTRA_TITLE);
                String text = intent.getStringExtra(EXTRA_TEXT);
                if (title != null && !title.isEmpty())
                    mTitle = title;
                if (text != null && !text.isEmpty())
                    mText = text;
            }
        }

        acquireWakeLock();
        promoteForeground();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        releaseWakeLock();
        try {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } catch (Exception ignored) {
        }
        super.onDestroy();
    }

    @Override
    public void onTaskRemoved(Intent rootIntent) {
        // Ne pas arrêter : la session / le CSV doivent survivre au swipe récent.
        Log.i(TAG, "onTaskRemoved — FGS + wake lock maintenus");
        promoteForeground();
        super.onTaskRemoved(rootIntent);
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void promoteForeground() {
        Notification notif = buildNotification(mTitle, mText);
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
        // Met à jour la notif si déjà en foreground.
        try {
            NotificationManager nm = getSystemService(NotificationManager.class);
            if (nm != null)
                nm.notify(NOTIF_ID, notif);
        } catch (Exception ignored) {
        }
    }

    private void acquireWakeLock() {
        if (mWakeLock != null && mWakeLock.isHeld())
            return;
        try {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm == null)
                return;
            mWakeLock = pm.newWakeLock(
                    PowerManager.PARTIAL_WAKE_LOCK,
                    "ecu_drive:logging");
            mWakeLock.setReferenceCounted(false);
            mWakeLock.acquire(); // session entière — libéré dans onDestroy
            Log.i(TAG, "PARTIAL_WAKE_LOCK acquis");
        } catch (Exception e) {
            Log.e(TAG, "WakeLock échoué", e);
        }
    }

    private void releaseWakeLock() {
        try {
            if (mWakeLock != null && mWakeLock.isHeld()) {
                mWakeLock.release();
                Log.i(TAG, "PARTIAL_WAKE_LOCK relâché");
            }
        } catch (Exception e) {
            Log.w(TAG, "WakeLock release", e);
        }
        mWakeLock = null;
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
                .setCategory(Notification.CATEGORY_SERVICE)
                .setPriority(Notification.PRIORITY_LOW);
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
                "Session conduite / logging",
                NotificationManager.IMPORTANCE_LOW);
        ch.setDescription("Maintient le logging OBD actif écran éteint");
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

    /** Met à jour le texte de la notification sans redémarrer le service. */
    public static void update(Context ctx, String title, String text) {
        if (ctx == null)
            return;
        Intent i = new Intent(ctx, LoggingService.class);
        i.setAction(ACTION_UPDATE);
        if (title != null)
            i.putExtra(EXTRA_TITLE, title);
        if (text != null)
            i.putExtra(EXTRA_TEXT, text);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
                ctx.startForegroundService(i);
            else
                ctx.startService(i);
        } catch (Exception e) {
            Log.e(TAG, "update LoggingService échoué", e);
        }
    }

    public static void stop(Context ctx) {
        if (ctx == null)
            return;
        try {
            Intent i = new Intent(ctx, LoggingService.class);
            i.setAction(ACTION_STOP);
            ctx.startService(i);
        } catch (Exception e) {
            Log.w(TAG, "stop via ACTION_STOP échoué, fallback stopService", e);
            try {
                ctx.stopService(new Intent(ctx, LoggingService.class));
            } catch (Exception e2) {
                Log.e(TAG, "stop LoggingService échoué", e2);
            }
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

    /** true si l'app est déjà exemptée d'optimisation batterie. */
    public static boolean isIgnoringBatteryOptimizations(Context ctx) {
        if (ctx == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return true;
        try {
            PowerManager pm = (PowerManager) ctx.getSystemService(Context.POWER_SERVICE);
            if (pm == null)
                return true;
            return pm.isIgnoringBatteryOptimizations(ctx.getPackageName());
        } catch (Exception e) {
            Log.w(TAG, "isIgnoringBatteryOptimizations", e);
            return true;
        }
    }

    /**
     * Demande l'exemption d'optimisation batterie (nécessaire sur beaucoup
     * d'OEM pour garder le BT + timers actifs écran éteint).
     * @return true si une UI système a été ouverte
     */
    public static boolean requestIgnoreBatteryOptimizations(android.app.Activity activity) {
        if (activity == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.M)
            return false;
        try {
            if (isIgnoringBatteryOptimizations(activity))
                return false;
            Intent i = new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
            i.setData(Uri.parse("package:" + activity.getPackageName()));
            activity.startActivity(i);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "REQUEST_IGNORE_BATTERY_OPTIMIZATIONS indisponible, fallback settings", e);
            try {
                Intent i = new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS);
                activity.startActivity(i);
                return true;
            } catch (Exception e2) {
                Log.e(TAG, "battery settings échoué", e2);
                return false;
            }
        }
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
