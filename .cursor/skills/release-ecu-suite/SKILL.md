---
name: release-ecu-suite
description: Release proprement ecu_studio_suite : bump de version, commit, tag annoté, push — la CI GitHub Actions builde et publie automatiquement l'APK Android et le binaire Linux. Utiliser quand l'utilisateur demande de releaser, tagger, publier une mise à jour ou préparer une release.
---

# Release ECU Studio Suite

## Fonctionnement

Un push de tag `v*` déclenche `.github/workflows/release.yml` qui :
1. Builde `ecu_studio` (Linux)
2. Builde et signe l'APK `ecu_drive` (Android arm64)
3. Crée la GitHub Release avec les deux binaires en assets

L'app Android (`Updater`) vérifie les releases GitHub et propose le téléchargement/installation de l'APK dès qu'une nouvelle version est disponible.

## Étapes à faire localement

### 1. Vérifier l'état

```bash
cd /data/leo/ecu_studio_suite
git status --short
git log --oneline $(git describe --tags --abbrev=0)..HEAD
git tag --sort=-version:refname | head -3
```

### 2. Build local de vérification (optionnel mais recommandé)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DECU_MPPS_SIMULATION=ON -DECU_BUILD_TESTS=OFF -G Ninja
cmake --build build --target ecu_studio ecu_drive -j$(nproc)
```

Stopper si erreurs de compilation.

### 3. Déterminer la nouvelle version

Basé sur les commits depuis le dernier tag :
- `feat:` ou nouvelle fonctionnalité → bump MINOR
- `fix:` ou polish → bump PATCH
- Breaking change → bump MAJOR

### 4. Commiter les fichiers sources

Ne jamais stager : `build/`, `run_test.sh`, `src_test.cpp`, `test_ols_real.cpp`

```bash
git add apps/ libs/ CMakeLists.txt .github/ .cursor/  # ajuster selon les fichiers modifiés
git commit -m "feat|fix: <résumé court> (vX.Y.Z)

- changement 1
- changement 2"
```

### 5. Tag annoté + push → déclenche la CI

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z — <titre>

- changement 1
- changement 2"

git push origin main --tags
```

Le push du tag déclenche automatiquement le workflow CI. La GitHub Release est créée avec l'APK Android et le binaire Linux en quelques minutes.

### 6. Vérification

```bash
git log --oneline -3
git tag --sort=-version:refname | head -3
```

Surveiller la CI sur : https://github.com/Poisson48/ecu_studio_suite/actions

## Secrets GitHub requis pour signer l'APK

À configurer dans Settings → Secrets → Actions du repo :

| Secret | Contenu |
|--------|---------|
| `ANDROID_SIGNING_KEY` | Keystore base64 (`base64 -w0 release.keystore`) |
| `ANDROID_KEY_ALIAS` | Alias de la clé dans le keystore |
| `ANDROID_KEYSTORE_PASSWORD` | Mot de passe du keystore |
| `ANDROID_KEY_PASSWORD` | Mot de passe de la clé |

Sans ces secrets, l'APK ne sera pas signé et le job Android échouera (le Linux est indépendant).

## Règles

- Ne jamais forcer un push sur `main`
- Toujours tag annoté (`-a`), jamais un tag léger
- La version dans le commit et le tag doivent être identiques
- Mettre à jour `CMakeLists.txt` ligne `project(... VERSION X.Y.Z ...)` avec la nouvelle version
