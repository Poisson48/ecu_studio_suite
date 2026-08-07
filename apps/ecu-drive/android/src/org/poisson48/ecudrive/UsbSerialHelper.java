package org.poisson48.ecudrive;

import android.content.Context;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.util.Log;

import java.util.HashMap;

/**
 * Pont léger USB Host pour ELM327 OTG.
 * Qt SerialPort sur Android est fragile : ce helper liste les périphériques
 * et demande la permission USB. L'ouverture série complète peut s'appuyer
 * ensuite sur le driver natif Qt ou un module usb-serial (phase 2).
 */
public class UsbSerialHelper {
    private static final String TAG = "EcuDriveUsb";

    public static String[] listUsbDevices(Context ctx) {
        UsbManager mgr = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
        if (mgr == null) return new String[0];
        HashMap<String, UsbDevice> map = mgr.getDeviceList();
        String[] out = new String[map.size()];
        int i = 0;
        for (UsbDevice d : map.values()) {
            out[i++] = String.format("%s vid=%04x pid=%04x",
                    d.getDeviceName(), d.getVendorId(), d.getProductId());
            Log.i(TAG, "USB device: " + out[i - 1]);
        }
        return out;
    }

    public static boolean hasPermission(Context ctx, String deviceName) {
        UsbManager mgr = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
        if (mgr == null) return false;
        for (UsbDevice d : mgr.getDeviceList().values()) {
            if (d.getDeviceName().equals(deviceName))
                return mgr.hasPermission(d);
        }
        return false;
    }
}
