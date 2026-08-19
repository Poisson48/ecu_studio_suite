---
name: release-ecu-suite
description: Release proprement ecu_studio_suite : build Release, bump de version, commit, tag annoté, push. Utiliser quand l'utilisateur demande de releaser, tagger, publier une mise à jour ou préparer une release.
---

# Release ECU Studio Suite

## Contexte projet

- Repo : `/data/leo/ecu_studio_suite`
- Apps : `ecu_studio` (PC) et `ecu_drive` (Android/PC)
- Versioning : `v<MAJOR>.<MINOR>.<PATCH>` — dernier tag via `git tag --sort=-version:refname | head -1`
- Schéma de bump : PATCH pour fix/polish, MINOR pour nouvelle feature, MAJOR pour rupture

## Étapes

### 1. Vérifier l'état

```bash
git status --short
git log --oneline $(git describe --tags --abbrev=0)..HEAD
git tag --sort=-version:refname | head -3
```

Relever : fichiers non commités, commits depuis le dernier tag, version actuelle.

### 2. Build Release propre

```bash
cd /data/leo/ecu_studio_suite
cmake -B build -DCMAKE_BUILD_TYPE=Release -DECU_MPPS_SIMULATION=ON -DECU_BUILD_TESTS=OFF -G Ninja
cmake --build build --target ecu_studio ecu_drive -j$(nproc)
```

Si "no work to do" et des fichiers ont changé : `rm -rf build` puis relancer.

Vérifier les binaires produits :
```bash
ls -lh build/apps/ecu-studio/ecu_studio build/apps/ecu-drive/ecu_drive
```

**Stopper si erreurs de compilation.**

### 3. Déterminer la nouvelle version

Règle de bump basée sur les commits depuis le dernier tag :
- `feat:` ou nouvelle lib/fonctionnalité → MINOR
- `fix:` ou polish → PATCH
- Breaking change → MAJOR

Incrémenter et noter `NEW_VERSION=vX.Y.Z`.

### 4. Commiter

Stager uniquement les fichiers sources pertinents (pas `build/`, pas les fichiers de test `run_test.sh`, `src_test.cpp`, `test_ols_real.cpp`).

```bash
git add apps/ libs/ CMakeLists.txt  # ajuster selon les fichiers modifiés
```

Message de commit en français, format :
```
feat|fix: <résumé court> (<NEW_VERSION>)

<liste des changements significatifs, une ligne par item>
```

Inclure la version dans la première ligne (ex: `feat: security access (v1.8.0)`).

### 5. Tag annoté

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z — <titre>

- changement 1
- changement 2
..."
```

### 6. Push

```bash
git push origin main --tags
```

Si pas de remote configuré, indiquer à l'utilisateur la commande à lancer.

### 7. Vérification finale

```bash
git log --oneline -3
git tag --sort=-version:refname | head -3
ls -lh build/apps/ecu-studio/ecu_studio build/apps/ecu-drive/ecu_drive
```

Confirmer à l'utilisateur : version taguée, binaires buildés, taille des exécutables.

## Règles

- Ne jamais commiter `build/`, `*.o`, `*.a`, `run_test.sh`, `src_test.cpp`, `test_ols_real.cpp`
- Ne jamais forcer un push sur `main`
- Si le build échoue : corriger avant de commiter
- Toujours utiliser un tag annoté (`-a`), jamais un tag léger
- La version dans le message de commit et le tag doivent être identiques
