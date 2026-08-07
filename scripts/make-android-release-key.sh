#!/usr/bin/env bash
# Génère LA clé de publication ECU Drive — une fois pour toutes.
#
# Android identifie l'app par sa signature. Deux APK signés par des clés
# différentes ne se mettent pas à jour l'un par-dessus l'autre.
#
#   bash scripts/make-android-release-key.sh
# puis suivez les instructions (3 secrets GitHub).
#
# ⚠️  SAUVEGARDEZ le .jks et le mot de passe hors du dépôt.
set -euo pipefail

OUT="${OUT:-$HOME/ecu-drive-release.jks}"
ALIAS="${ALIAS:-ecudrive}"

if [ -f "$OUT" ]; then
  echo "Un keystore existe déjà : $OUT" >&2
  echo "Ne le régénérez pas — ce serait une nouvelle identité d'app." >&2
  echo "Pour réafficher le secret : base64 -w0 \"$OUT\"" >&2
  exit 1
fi

STOREPASS="$(head -c 48 /dev/urandom | base64 | tr -d '/+=' | cut -c1-32)"

keytool -genkeypair \
  -keystore "$OUT" -alias "$ALIAS" \
  -storepass "$STOREPASS" -keypass "$STOREPASS" \
  -keyalg RSA -keysize 4096 -validity 10000 \
  -dname "CN=ECU Drive, O=Poisson48, C=FR" >/dev/null

chmod 600 "$OUT"

REPO="$(git -C "$(dirname "$0")/.." remote get-url origin 2>/dev/null || echo '<votre/dépôt>')"

cat <<EOF

Keystore créé : $OUT   (alias : $ALIAS)

1. Sauvegardez ce fichier et ce mot de passe ailleurs que dans le dépôt :

     $STOREPASS

2. Déclarez les trois secrets dans GitHub :

     gh secret set ANDROID_KEYSTORE_B64 --body "\$(base64 -w0 "$OUT")"
     gh secret set ANDROID_KEY_ALIAS    --body "$ALIAS"
     gh secret set ANDROID_KEYSTORE_PASS --body "$STOREPASS"

   (ou : $REPO → Settings → Secrets and variables → Actions)

3. La prochaine release tag v* publiera un ecu-drive-*-arm64.apk signé avec cette clé.
EOF
