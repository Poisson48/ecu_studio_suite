---
name: release-ecu-suite
description: Release proprement ecu_studio_suite : bump de version, commit, tag annoté, push. La CI GitHub Actions existante builde automatiquement l'AppImage Linux, l'APK Android signé et crée la GitHub Release. Utiliser quand l'utilisateur demande de releaser, tagger, publier une mise à jour ou préparer une release.
---

# Release ECU Studio Suite

## Pipeline CI existant

Push d'un tag `v*` → `.github/workflows/release.yml` :
- **`apk`** (via `android.yml`) — APK arm64 signé avec `ANDROID_KEYSTORE_B64`
- **`appimage`** — AppImage Linux x86-64, signée avec `UPDATE_SIGNING_KEY`
- **`publish`** — GitHub Release avec les deux assets + `release-manifest.json.sig`

L'app Android (`Updater`) vérifie les releases GitHub et propose l'installation en 1 tap.

Repo : `Poisson48/ecu_studio_suite` — CI : https://github.com/Poisson48/ecu_studio_suite/actions

## Étapes locales

### 1. Vérifier l'état

```bash
cd /data/leo/ecu_studio_suite
git status --short
git log --oneline $(git describe --tags --abbrev=0)..HEAD
git tag --sort=-version:refname | head -3
```

### 2. Build local de vérification (optionnel)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DECU_MPPS_SIMULATION=ON -DECU_BUILD_TESTS=OFF -G Ninja
cmake --build build --target ecu_studio ecu_drive -j$(nproc)
```

Stopper si erreurs de compilation.

### 3. Bump de version

Basé sur les commits depuis le dernier tag :
- `feat:` → MINOR  |  `fix:` → PATCH  |  breaking → MAJOR

Mettre à jour la ligne `project(... VERSION X.Y.Z ...)` dans `CMakeLists.txt`.

### 4. Commiter

Ne jamais stager : `build/`, `run_test.sh`, `src_test.cpp`, `test_ols_real.cpp`

```bash
git add apps/ libs/ CMakeLists.txt .cursor/
git commit -m "feat|fix: <résumé> (vX.Y.Z)

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

### 6. Surveiller la CI

https://github.com/Poisson48/ecu_studio_suite/actions

La release GitHub est créée automatiquement en ~15 min avec l'APK et l'AppImage.

## Secrets requis (déjà configurés)

| Secret | Usage |
|--------|-------|
| `ANDROID_KEYSTORE_B64` | Keystore base64 pour signer l'APK |
| `ANDROID_KEY_ALIAS` | Alias de la clé |
| `ANDROID_KEYSTORE_PASS` | Mot de passe keystore |
| `UPDATE_SIGNING_KEY` | Clé privée PEM pour signer le manifeste de mise à jour |

## Règles

- Ne jamais forcer un push sur `main`
- Toujours tag annoté (`-a`), jamais un tag léger
- Version dans `CMakeLists.txt`, commit et tag doivent être identiques
- Ne pas modifier `.github/workflows/` sans tester la CI
