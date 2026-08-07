#!/usr/bin/env bash
# Build + signature de l'APK arm64 ECU Drive.
# Prérequis : Qt Android + NDK (CI : .github/workflows/android.yml).
#
# Variables utiles :
#   SDK_ROOT / QT_ROOT / QT_VER / NDK_VER             chemins du kit
#   VERSION_NAME / VERSION_CODE                      version de l'APK
#   KEYSTORE / KEYALIAS / STOREPASS                  clé de signature (défaut : debug)
#   ANDROID_KEYSTORE_B64                             clé CI (base64), prioritaire
#   OUT                                              chemin de l'APK final
set -euo pipefail

SDK_ROOT="${SDK_ROOT:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
NDK_VER="${NDK_VER:-27.3.13750724}"
QT_VER="${QT_VER:-6.8.3}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
QT_ANDROID="${QT_ANDROID:-$QT_ROOT/$QT_VER/android_arm64_v8a}"
QT_HOST="${QT_HOST:-$QT_ROOT/$QT_VER/gcc_64}"
BUILD_TOOLS_VER="${BUILD_TOOLS_VER:-35.0.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/ecu-drive-arm64.apk}"

export ANDROID_SDK_ROOT="$SDK_ROOT"
export ANDROID_NDK_ROOT="${ANDROID_NDK_ROOT:-$SDK_ROOT/ndk/$NDK_VER}"
# Certains toolchains Qt lisent ANDROID_NDK (sans _ROOT).
export ANDROID_NDK="$ANDROID_NDK_ROOT"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"

if [ ! -d "$ANDROID_NDK_ROOT" ]; then
  echo "NDK introuvable: $ANDROID_NDK_ROOT" >&2
  echo "NDKs présents:" >&2
  ls -la "$SDK_ROOT/ndk" 2>/dev/null || true
  exit 1
fi
echo "Using NDK: $ANDROID_NDK_ROOT"

VERSION_NAME="${VERSION_NAME:-0.1.0}"
VERSION_CODE="${VERSION_CODE:-1}"

# Aligner AndroidManifest (aapt) et APP_VERSION CMake (updater).
sed -i -E \
  -e "s/android:versionName=\"[^\"]*\"/android:versionName=\"$VERSION_NAME\"/" \
  -e "s/android:versionCode=\"[^\"]*\"/android:versionCode=\"$VERSION_CODE\"/" \
  "$ROOT/apps/ecu-drive/android/AndroidManifest.xml"

rm -rf "$ROOT/build-android"
"$QT_ANDROID/bin/qt-cmake" \
  -S "$ROOT" -B "$ROOT/build-android" -G Ninja \
  -DQT_HOST_PATH="$QT_HOST" \
  -DANDROID_SDK_ROOT="$SDK_ROOT" \
  -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
  -DANDROID_NDK="$ANDROID_NDK_ROOT" \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_ANDROID_ABIS="arm64-v8a" \
  -DQT_ANDROID_COMPILE_SDK_VERSION=35 \
  -DECU_BUILD_ECU_STUDIO=OFF \
  -DECU_BUILD_ECU_DRIVE=ON \
  -DECU_BUILD_TESTS=OFF \
  -DECU_BUILD_SOCKETSPY=OFF \
  -DECU_MPPS_SIMULATION=ON \
  -DECU_VERSION_OVERRIDE="$VERSION_NAME"

cmake --build "$ROOT/build-android" --target apk -j"$(nproc)"

UNSIGNED="$(find "$ROOT/build-android" -name '*-release-unsigned.apk' | head -1)"
[ -n "$UNSIGNED" ] || { echo "APK non signé introuvable dans build-android/" >&2; exit 1; }

if [ -n "${ANDROID_KEYSTORE_B64:-}" ]; then
  KEYSTORE="$(mktemp -t ecu-drive-keystore.XXXXXX.jks)"
  trap 'rm -f "$KEYSTORE"' EXIT
  printf '%s' "$ANDROID_KEYSTORE_B64" | base64 -d > "$KEYSTORE"
  KEYALIAS="${KEYALIAS:?ANDROID_KEYSTORE_B64 fourni sans KEYALIAS}"
  STOREPASS="${STOREPASS:?ANDROID_KEYSTORE_B64 fourni sans STOREPASS}"
  echo "== signature avec la clé de publication (alias $KEYALIAS) =="
else
  KEYSTORE="${KEYSTORE:-$HOME/.android/debug.keystore}"
  KEYALIAS="${KEYALIAS:-androiddebugkey}"
  STOREPASS="${STOREPASS:-android}"
  if [ ! -f "$KEYSTORE" ]; then
    echo "== keystore de debug généré : $KEYSTORE =="
    mkdir -p "$(dirname "$KEYSTORE")"
    keytool -genkeypair -keystore "$KEYSTORE" -alias "$KEYALIAS" \
      -storepass "$STOREPASS" -keypass "$STOREPASS" \
      -keyalg RSA -keysize 2048 -validity 10000 \
      -dname "CN=ECU Drive Debug, O=Poisson48, C=FR" >/dev/null
  fi
  echo "== signature avec le keystore de debug local : $KEYSTORE =="
fi

BT="$SDK_ROOT/build-tools/$BUILD_TOOLS_VER"
"$BT/zipalign" -f -p 4 "$UNSIGNED" "$ROOT/build-android/aligned.apk"
"$BT/apksigner" sign \
  --ks "$KEYSTORE" --ks-key-alias "$KEYALIAS" \
  --ks-pass "pass:$STOREPASS" --key-pass "pass:$STOREPASS" \
  --out "$OUT" "$ROOT/build-android/aligned.apk"
"$BT/apksigner" verify "$OUT"

echo
echo "APK signé : $OUT  (v$VERSION_NAME, code $VERSION_CODE)"
echo "Installation : adb install -r $OUT"
