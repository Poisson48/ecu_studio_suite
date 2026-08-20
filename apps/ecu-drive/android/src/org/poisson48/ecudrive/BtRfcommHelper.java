package org.poisson48.ecudrive;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.os.ParcelUuid;
import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Method;
import java.util.Set;
import java.util.UUID;

/**
 * RFCOMM natif pour clones ELM327.
 * Qt Android refuse connectToService(adresse, canal) (« Connecting to port
 * is not supported ») : on passe par BluetoothSocket (UUID SPP, puis canal 1/2).
 */
public final class BtRfcommHelper {
    private static final String TAG = "EcuDriveBt";
    private static final UUID SPP =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private static BluetoothSocket sSocket;
    private static InputStream sIn;
    private static OutputStream sOut;

    private BtRfcommHelper() {}

    public static synchronized void cancelDiscovery() {
        try {
            BluetoothAdapter a = BluetoothAdapter.getDefaultAdapter();
            if (a != null && a.isDiscovering())
                a.cancelDiscovery();
        } catch (Exception e) {
            Log.w(TAG, "cancelDiscovery", e);
        }
    }

    /**
     * mode 0 = UUID SPP, 1 = RFCOMM canal 1, 2 = RFCOMM canal 2,
     * 3 = premier UUID en cache (appareil appairé).
     * Appeler hors thread UI : BluetoothSocket.connect() est bloquant.
     */
    public static String connect(String address, int mode) {
        close();
        cancelDiscovery();
        try {
            BluetoothAdapter adapter = BluetoothAdapter.getDefaultAdapter();
            if (adapter == null)
                return "Bluetooth indisponible";
            if (!adapter.isEnabled())
                return "Bluetooth désactivé";
            if (address == null || address.isEmpty())
                return "Adresse Bluetooth vide";

            BluetoothDevice device = adapter.getRemoteDevice(address.toUpperCase(java.util.Locale.US));
            BluetoothSocket socket;
            if (mode <= 0) {
                socket = device.createRfcommSocketToServiceRecord(SPP);
            } else if (mode == 3) {
                ParcelUuid[] uuids = device.getUuids();
                if (uuids == null || uuids.length == 0)
                    return "Aucun service BT en cache — appaire le module";
                socket = device.createRfcommSocketToServiceRecord(uuids[0].getUuid());
            } else {
                Method m = device.getClass().getMethod("createRfcommSocket", int.class);
                socket = (BluetoothSocket) m.invoke(device, Integer.valueOf(mode));
            }
            socket.connect();
            synchronized (BtRfcommHelper.class) {
                sSocket = socket;
                sIn = socket.getInputStream();
                sOut = socket.getOutputStream();
            }
            Log.i(TAG, "RFCOMM OK mode=" + mode + " " + address);
            return "OK";
        } catch (Exception e) {
            close();
            String msg = e.getMessage();
            if (msg == null || msg.isEmpty())
                msg = e.getClass().getSimpleName();
            Log.w(TAG, "RFCOMM fail mode=" + mode + " " + address + " : " + msg);
            return msg;
        }
    }

    public static synchronized void close() {
        try {
            if (sIn != null) sIn.close();
        } catch (Exception ignored) {}
        try {
            if (sOut != null) sOut.close();
        } catch (Exception ignored) {}
        try {
            if (sSocket != null) sSocket.close();
        } catch (Exception ignored) {}
        sIn = null;
        sOut = null;
        sSocket = null;
    }

    public static synchronized int available() {
        try {
            if (sIn == null) return -1;
            return sIn.available();
        } catch (Exception e) {
            return -1;
        }
    }

    public static synchronized int read(byte[] buf) {
        try {
            if (sIn == null || buf == null || buf.length == 0) return -1;
            int n = sIn.available();
            if (n <= 0) return 0;
            return sIn.read(buf, 0, Math.min(buf.length, n));
        } catch (Exception e) {
            return -1;
        }
    }

    public static synchronized int write(byte[] buf) {
        try {
            if (sOut == null || buf == null) return -1;
            sOut.write(buf);
            sOut.flush();
            return buf.length;
        } catch (Exception e) {
            return -1;
        }
    }

    public static synchronized boolean isConnected() {
        return sSocket != null && sSocket.isConnected();
    }

    public static String[] listBonded() {
        try {
            BluetoothAdapter a = BluetoothAdapter.getDefaultAdapter();
            if (a == null) return new String[0];
            Set<BluetoothDevice> set = a.getBondedDevices();
            if (set == null || set.isEmpty()) return new String[0];
            String[] out = new String[set.size()];
            int i = 0;
            for (BluetoothDevice d : set) {
                String name = d.getName();
                if (name == null) name = "";
                out[i++] = d.getAddress() + "|" + name;
            }
            return out;
        } catch (Exception e) {
            Log.w(TAG, "listBonded", e);
            return new String[0];
        }
    }
}
