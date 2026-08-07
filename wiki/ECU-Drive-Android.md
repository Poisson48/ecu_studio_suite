# ECU Drive (Android)

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

**ECU Drive** (**Beta**) is the simplified Qt6 companion for validating a flashed tune **on the road without a PC**. It imports a portable **`.ecutune`** package, connects an **ELM327** (Bluetooth SPP or USB OTG), and compares live OBD PIDs to OpenDAMOS expected values — same `TuneValidator` as ECU Studio.

> **Maturity:** 🧪 **Beta** — shipped as a signed APK on GitHub Releases; works on device/emulator, not yet field-hardened.

### Download
**[GitHub Releases](https://github.com/Poisson48/ecu_studio_suite/releases/latest)** — asset `ecu-drive-*-arm64.apk` (multi-ABI arm64 + x86_64).

### Workflow
1. On desktop **ECU Studio** → *File → Export for ECU Drive (.ecutune)…*
2. Copy the `.ecutune` file to the phone
3. Open **ECU Drive** → Import → Connect ELM (BT scan or USB) → **▶ Start drive session**
4. After the drive: session summary + CSV under app data (Share opens the folder)

### Build (desktop Linux — iterate fast)
```bash
cmake -B build -DECU_BUILD_ECU_DRIVE=ON -DECU_BUILD_TESTS=ON
cmake --build build --target ecu_drive -j$(nproc)
./build/apps/ecu-drive/ecu_drive
```

### Build (Android APK)
Requires Qt 6 for Android + NDK. Example:
```bash
cmake -B build-android -DANDROID=ON \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DECU_BUILD_ECU_STUDIO=OFF -DECU_BUILD_TESTS=OFF -DECU_BUILD_ECU_DRIVE=ON
cmake --build build-android --target ecu_drive
# Then qt-cmake / androiddeployqt to produce the APK (see CI workflow).
```

### Chinese blue ELM327 (Bluetooth Classic)
Primary path. Pair the dongle in system Bluetooth settings first (**PIN `1234` or `0000`**). ECU Drive scans **Classic / SPP only** (not BLE), prefers names like OBDII / ELM / V-LINK, then connects via SerialPort UUID with RFCOMM channel 1→2 fallback. Contact ON so the adapter is powered.

### Auto-updates
On launch, ECU Drive checks GitHub Releases for a newer `ecu-drive*-arm64.apk` (or any `ecu-drive*.apk`), shows notes, downloads, and installs via Android PackageInstaller. Each `v*` tag runs the **Release** workflow which builds a signed APK (`scripts/build-android-drive.sh`) and attaches it to the GitHub Release. One-time keystore setup: `bash scripts/make-android-release-key.sh` then set the three `ANDROID_*` secrets.

### USB OTG
Secondary. Host permission + VID filter (CH340/FTDI/CP210x/QBD). `UsbSerialHelper` lists devices; full serial open depends on Android `QSerialPort` / usb-serial bridge.

### Privacy
100% local. No telemetry. GPL-3.0.

---

<a id="français"></a>

## Français

**ECU Drive** (**Bêta**) est le compagnon Qt6 simplifié pour valider une carto **en roulant sans PC**. Il importe un package **`.ecutune`**, se connecte à un **ELM327** (Bluetooth SPP ou USB OTG), et compare les PID OBD live aux valeurs OpenDAMOS attendues — même `TuneValidator` qu’ECU Studio.

> **Maturité :** 🧪 **Bêta** — APK signé sur les Releases GitHub ; fonctionne sur appareil/émulateur, pas encore durci sur le terrain.

### Téléchargement
**[Releases GitHub](https://github.com/Poisson48/ecu_studio_suite/releases/latest)** — asset `ecu-drive-*-arm64.apk` (multi-ABI arm64 + x86_64).
1. Sur **ECU Studio** → *Fichier → Exporter pour ECU Drive (.ecutune)…*
2. Copie le `.ecutune` sur le téléphone
3. **ECU Drive** → Importer → Connecter ELM (scan BT ou USB) → **▶ Lancer session conduite**
4. Après : résumé + CSV (Partager ouvre le dossier)

### Module bleu chinois (Bluetooth classique)
Chemin principal. **Appairer d’abord** le dongle dans les réglages Bluetooth du téléphone (**PIN `1234` ou `0000`**). ECU Drive ne scanne que le **BT classique / SPP** (pas le BLE), priorise les noms OBDII / ELM / V-LINK, puis se connecte via UUID SerialPort avec repli canaux RFCOMM 1 puis 2. Contact ON pour alimenter l’adaptateur.

### Mises à jour automatiques
Au démarrage, ECU Drive interroge les Releases GitHub : si un `ecu-drive*-arm64.apk` (ou `ecu-drive*.apk`) plus récent existe, bannière + notes → téléchargement → installation PackageInstaller. Chaque tag `v*` lance le workflow **Release** qui compile l’APK signé (`scripts/build-android-drive.sh`) et le joint à la Release. Clé une fois : `bash scripts/make-android-release-key.sh` puis les 3 secrets `ANDROID_*`.

### USB OTG
Secondaire. Permission host + filtre VID (CH340/FTDI/CP210x/QBD). `UsbSerialHelper` liste les périphériques ; l’ouverture série complète dépend du support `QSerialPort` Android / pont usb-serial.

### Vie privée
100 % local. Aucune télémétrie. GPL-3.0.
