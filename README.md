<div align="center">

# ECU Studio Suite

**A 100% local, Qt6 automotive software suite for Linux — flash an ECU map, then verify it live on the CAN bus.**

[![Release](https://img.shields.io/github/v/release/Poisson48/ecu_studio_suite?label=ECU%20Studio)](https://github.com/Poisson48/ecu_studio_suite/releases/latest)
[![SocketSpy](https://img.shields.io/badge/SocketSpy-v0.8.7-blue)](https://github.com/Poisson48/SocketSpy)
[![License](https://img.shields.io/badge/license-GPL--3.0-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20x86__64-lightgrey)](#platform-support--plateformes)
[![Qt6](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B&logoColor=white)](https://en.cppreference.com/)
[![No telemetry](https://img.shields.io/badge/telemetry-none-success)](#)

**[Website](https://poisson48.github.io/ecu_studio_suite/)** · **[Demo gallery](https://poisson48.github.io/ecu_studio_suite/demo.html)** · **[Wiki](https://github.com/Poisson48/ecu_studio_suite/wiki)** · **[SocketSpy](https://github.com/Poisson48/SocketSpy)** · **[SocketSpy site](https://poisson48.github.io/SocketSpy)**

[English](#english) · [Français](#français)

</div>

---

<a id="english"></a>

# English

## What the suite is

**ECU Studio Suite** is a fully offline, Qt6/C++23 automotive software suite built for Linux. **No telemetry, no network calls, no cloud** — everything runs on your machine.

It is built as a **HUB** that launches specialized sub-programs and interconnects them. Each tool is excellent on its own, but the whole point is that they work *together*: the flagship loop is **ECU Studio flashes a map → SocketSpy verifies live on the CAN bus that the change took effect at the right operating point.**

```
                         ┌──────────────────────────┐
                         │      ECU Studio Suite     │
                         │           (HUB)           │
                         └────────────┬─────────────┘
                  launches            │            launches
            ┌────────────────────────┼────────────────────────┐
            ▼                                                  ▼
   ┌──────────────────┐                              ┌──────────────────┐
   │    ECU Studio    │   flash a map ───────────▶   │     SocketSpy    │
   │  (reprogramming) │   ◀─────────── verify live   │  (CAN analysis)  │
   └──────────────────┘                              └──────────────────┘
```

## The hub + sub-programs vision

The suite is a hub launcher around two flagship sub-programs that share the same Qt6 dark theme and sidebar:

| Sub-program | Role | Repo |
|-------------|------|------|
| **ECU Studio** | ECU reprogramming, 2D/3D map editor, DAMOS editor, A2L, hex view, checksums, MPPS flashing, compare, git versioning, AutoMods, OpenDAMOS | *(this repo)* |
| **[SocketSpy](https://github.com/Poisson48/SocketSpy)** | Linux SocketCAN analysis — live monitor, DBC decode, signal graphs, protocol decoders, UDS tester, Lua scripting, simulator, and more | [Poisson48/SocketSpy](https://github.com/Poisson48/SocketSpy) |

## The interconnection loop

The flagship workflow — the reason the suite exists — closes the loop between *changing* an ECU and *seeing the effect*:

1. **ECU Studio** opens a ROM, you edit a map (e.g. boost or fuel at a given RPM × load point).
2. **ECU Studio** flashes the patched map to the ECU over MPPS V21.
3. **ECU Studio launches SocketSpy** side-by-side.
4. **SocketSpy** decodes the live CAN bus (DBC / UDS / OBD-II) and shows the relevant signal on a graph.
5. You confirm the change took effect **at the right operating point** — closing the flash → verify loop.

> **Maturity:** the individual tools are Proven; the end-to-end **flash → verify loop** and the **hub launcher** are **Beta** (work, but newly wired together). Full bidirectional interconnection is **Incoming**.

## Feature overview

**Maturity tags:** ✅ **Proven** (verified / shipped & stable) · 🧪 **Beta** (works, but new / unproven) · 🔜 **Incoming** (roadmap)

### ECU Studio — reprogramming & map editing

| Feature | Maturity | Notes |
|---------|:--------:|-------|
| ROM read / write (MPPS V21 over USB) | ✅ Proven | Full-ROM and block-level flash; progress bar, abort |
| Hex view | ✅ Proven | Fast editor, search, byte-level diff overlay, address jump |
| 2D map editor | ✅ Proven | Scalar / curve / table maps; CSV import/export |
| DAMOS editor | ✅ Proven | Create & edit `open_damos` in-app, detect maps from ROM, export A2L |
| A2L parser & export | ✅ Proven | Parse ASAP2 `.a2l`, browse by ECU, export relocated maps to A2L |
| Checksum panel | ✅ Proven | Compute & patch checksums for supported ECU families |
| Compare panel | ✅ Proven | Side-by-side ROM diff, byte-level delta, region filter |
| AutoMods panel | ✅ Proven | Apply named calibration patches from JSON recipe; batch apply / revert |
| Git versioning | ✅ Proven | libgit2-backed ROM history — commit, browse, restore any version |
| Project manager | ✅ Proven | `.ecuproj` files: ROM path, ECU type, notes, flash log |
| In-app auto-update | ✅ Proven | Ed25519-signed manifest + SHA-256 verification |
| Bilingual UI (EN / FR) | ✅ Proven | Complete French / English translations |
| 3D map view (ghost overlay) | 🧪 Beta | Pseudo-3D + heatmap with baseline ghost overlay; native `Q3DSurface` OpenGL coming |
| CAN companion (launch SocketSpy) | 🧪 Beta | Launch SocketSpy side-by-side during reprogramming |

### SocketSpy — Linux CAN bus analysis (v0.8.7)

| Feature | Maturity |
|---------|:--------:|
| Live monitor, DBC decode, signal graphs, transmit | ✅ Proven |
| Protocol decoders: CANopen / J1939 / ISO-TP / UDS / OBD-II / NMEA2000 | ✅ Proven |
| Lua scripting, CAN simulator, frame fuzzer | ✅ Proven |
| UDS tester + ECU simulator | ✅ Proven |
| BLF / MDF4 export, capture diff, multi-bus, i18n | ✅ Proven |
| io_uring capture, MCP / JSON-RPC API, Signal Detective | 🧪 Beta |

> Full details, screenshots and downloads: **[SocketSpy repo](https://github.com/Poisson48/SocketSpy)** · **[SocketSpy site](https://poisson48.github.io/SocketSpy)**.

### OpenDAMOS — firmware-portable map recipes

A free, **CC0** DAMOS that relocates ECU maps by **axis fingerprint** instead of hardcoded addresses, so one recipe works across firmware variants of the same ECU family. A converter (`scripts/damos_a2l_to_opendamos.py`) turns a manufacturer DAMOS (A2L) + reference ROM into an open recipe.

| Coverage | Maturity |
|----------|:--------:|
| Bosch EDC16 (big-endian inline) | ✅ Proven (verified across firmware variants) |
| Bosch EDC17 (little-endian inline) | ✅ Proven (verified across firmware variants) |
| COM_AXIS ECUs — PSA Valeo, Continental SID807 | 🧪 Beta (11/12 on Valeo, no cross-firmware test yet) |
| EDC16CP33 complete | 🔜 Incoming |

> Read the spec: **[OpenDAMOS standard](docs/opendamos/OPENDAMOS_STANDARD.md)** · conversion brief: **[DAMOS → OpenDAMOS convention](docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md)**.

### Suite-level — hub & interconnection

| Capability | Maturity |
|------------|:--------:|
| Hub launcher | 🧪 Beta |
| Flash → verify loop (ECU Studio → SocketSpy) | 🧪 Beta |
| Full bidirectional interconnection | 🔜 Incoming |
| More ECUs (broader catalog) | 🔜 Incoming |

### Supported ECUs (ECU Studio)

| ECU | Protocol | Notes |
|-----|----------|-------|
| EDC16C34 | K-Line | Bosch diesel — Peugeot / Citroën |
| ME7.x | K-Line | Bosch petrol |
| MED17 | CAN (UDS) | Bosch petrol — direct injection |
| EDC17 | CAN (UDS) | Bosch diesel |

## Quick start

### Download (recommended)

**[⬇ Download ECU Studio (AppImage)](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** — v1.3.0, ~37 MB, Linux x86_64.

```bash
chmod +x ECU_Studio-x86_64.AppImage
./ECU_Studio-x86_64.AppImage
```

Self-contained AppImage — Qt6, libusb and every runtime library are bundled, so it runs on a clean PC with **nothing installed** and no root access. All builds are on the [releases page](https://github.com/Poisson48/ecu_studio_suite/releases/latest).

For SocketSpy, see the **[SocketSpy repo](https://github.com/Poisson48/SocketSpy)**.

### Build from source (developers)

```bash
# Ubuntu / Debian dependencies
sudo apt install \
    qt6-base-dev qt6-charts-dev qt6-serialbus-dev qt6-serialport-dev \
    libusb-1.0-0-dev libgit2-dev liblua5.4-dev \
    nlohmann-json3-dev libgtest-dev cmake ninja-build

# Clone with the SocketSpy submodule, then build (simulation mode, no hardware)
git clone --recurse-submodules https://github.com/Poisson48/ecu_studio_suite
cd ecu_studio_suite
bash build.sh
./build/apps/ecu-studio/ecu_studio
```

`build.sh` checks dependencies and enables MPPS simulation mode by default. Key CMake options: `ECU_BUILD_ECU_STUDIO`, `ECU_BUILD_SOCKETSPY`, `ECU_BUILD_TESTS`, `ECU_MPPS_SIMULATION`, `ECU_MPPS_PROTOCOL_LOG`.

### Real hardware (Linux)

```bash
sudo cp libs/60-mpps.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Plug in the MPPS V21, then run ECU Studio and hit Refresh in the MPPS panel.
```

Linux uses libusb directly — no FTDI D2XX driver needed. Requires kernel ≥ 5.4 with SocketCAN.

## Documentation

| Resource | Link |
|----------|------|
| **Website** | https://poisson48.github.io/ecu_studio_suite/ |
| **Demo gallery** | https://poisson48.github.io/ecu_studio_suite/demo.html |
| **Wiki** — guides, architecture, OpenDAMOS, FAQ | [github.com/Poisson48/ecu_studio_suite/wiki](https://github.com/Poisson48/ecu_studio_suite/wiki) |
| **OpenDAMOS** — standard & convention | [docs/opendamos/](docs/opendamos/) — [standard](docs/opendamos/OPENDAMOS_STANDARD.md) · [convention](docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md) |
| **Knowledge wiki** — gap analysis | [docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) |
| **Knowledge wiki** — MCP / AI integration | [docs/MCP.md](docs/MCP.md) |
| **Knowledge wiki** — MPPS reverse engineering | [reverse](docs/mpps-reverse.md) · [checksums](docs/mpps-checksums.md) · [decryption](docs/mpps-decryption.md) |
| **Knowledge wiki** — ECU research database | [docs/ecu-research/](docs/ecu-research/) |
| **SocketSpy** — repo & site | [github.com/Poisson48/SocketSpy](https://github.com/Poisson48/SocketSpy) · [poisson48.github.io/SocketSpy](https://poisson48.github.io/SocketSpy) |

## Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Primary target | libusb, SocketCAN, full feature set |
| Windows (cross-compile) | Supported | MinGW toolchain; ftd2xx for MPPS |
| macOS | Not tested | SocketCAN unavailable |

## License

**GPL-3.0** — see [LICENSE](LICENSE). OpenDAMOS recipes are recommended to be licensed **CC0-1.0**.

---

<a id="français"></a>

# Français

## Ce qu'est la suite

**ECU Studio Suite** est une suite logicielle automobile 100 % locale, en Qt6/C++23, conçue pour Linux. **Aucune télémétrie, aucun appel réseau, aucun cloud** — tout tourne sur votre machine.

Elle est bâtie comme un **HUB** qui lance des sous-programmes spécialisés et les interconnecte. Chaque outil est excellent seul, mais l'essentiel est qu'ils fonctionnent *ensemble* : la boucle phare, c'est **ECU Studio flashe une cartographie → SocketSpy vérifie en direct sur le bus CAN que le changement a bien pris effet au bon point de fonctionnement.**

```
                         ┌──────────────────────────┐
                         │      ECU Studio Suite     │
                         │           (HUB)           │
                         └────────────┬─────────────┘
                  lance               │               lance
            ┌────────────────────────┼────────────────────────┐
            ▼                                                  ▼
   ┌──────────────────┐                              ┌──────────────────┐
   │    ECU Studio    │   flashe une carte ──────▶   │     SocketSpy    │
   │ (reprogrammation)│   ◀────── vérifie en direct  │   (analyse CAN)  │
   └──────────────────┘                              └──────────────────┘
```

## La vision hub + sous-programmes

La suite est un lanceur (hub) autour de deux sous-programmes phares qui partagent le même thème sombre Qt6 et la même barre latérale :

| Sous-programme | Rôle | Dépôt |
|----------------|------|-------|
| **ECU Studio** | Reprogrammation ECU, éditeur de cartos 2D/3D, éditeur DAMOS, A2L, vue hex, checksums, flash MPPS, comparaison, versionnement git, AutoMods, OpenDAMOS | *(ce dépôt)* |
| **[SocketSpy](https://github.com/Poisson48/SocketSpy)** | Analyse SocketCAN sous Linux — moniteur live, décodage DBC, graphes de signaux, décodeurs de protocoles, testeur UDS, scripts Lua, simulateur, et plus | [Poisson48/SocketSpy](https://github.com/Poisson48/SocketSpy) |

## La boucle d'interconnexion

Le workflow phare — la raison d'être de la suite — boucle entre *modifier* un ECU et *en voir l'effet* :

1. **ECU Studio** ouvre une ROM, vous éditez une carto (par ex. la suralimentation ou l'injection à un point RPM × charge donné).
2. **ECU Studio** flashe la carto modifiée dans l'ECU via MPPS V21.
3. **ECU Studio lance SocketSpy** côte à côte.
4. **SocketSpy** décode le bus CAN en direct (DBC / UDS / OBD-II) et affiche le signal pertinent sur un graphe.
5. Vous confirmez que le changement a pris effet **au bon point de fonctionnement** — la boucle flash → vérification est bouclée.

> **Maturité :** les outils individuels sont Éprouvés ; la **boucle flash → vérification** de bout en bout et le **lanceur (hub)** sont en **Bêta** (fonctionnels, mais tout juste reliés). L'interconnexion bidirectionnelle complète est **À venir**.

## Vue d'ensemble des fonctionnalités

**Étiquettes de maturité :** ✅ **Éprouvé** (vérifié / livré & stable) · 🧪 **Bêta** (fonctionne, mais récent / non confirmé) · 🔜 **À venir** (feuille de route)

### ECU Studio — reprogrammation & édition de cartos

| Fonctionnalité | Maturité | Notes |
|----------------|:--------:|-------|
| Lecture / écriture ROM (MPPS V21 par USB) | ✅ Éprouvé | Flash ROM complète et par bloc ; barre de progression, abandon |
| Vue hexadécimale | ✅ Éprouvé | Éditeur rapide, recherche, diff octet par octet, saut d'adresse |
| Éditeur de cartos 2D | ✅ Éprouvé | Cartos scalaires / courbes / tables ; import/export CSV |
| Éditeur DAMOS | ✅ Éprouvé | Créer & éditer `open_damos` dans l'appli, détecter les cartos depuis la ROM, exporter en A2L |
| Parseur & export A2L | ✅ Éprouvé | Parser ASAP2 `.a2l`, parcourir par ECU, exporter les cartos relocalisées en A2L |
| Panneau checksum | ✅ Éprouvé | Calculer & corriger les checksums des familles d'ECU supportées |
| Panneau de comparaison | ✅ Éprouvé | Diff ROM côte à côte, delta octet par octet, filtre par région |
| Panneau AutoMods | ✅ Éprouvé | Appliquer des patchs de calibration nommés depuis une recette JSON ; appliquer / annuler en lot |
| Versionnement git | ✅ Éprouvé | Historique ROM via libgit2 — commit, parcours, restauration de toute version |
| Gestionnaire de projet | ✅ Éprouvé | Fichiers `.ecuproj` : chemin ROM, type d'ECU, notes, journal de flash |
| Mise à jour automatique intégrée | ✅ Éprouvé | Manifeste signé Ed25519 + vérification SHA-256 |
| Interface bilingue (EN / FR) | ✅ Éprouvé | Traductions françaises / anglaises complètes |
| Vue 3D des cartos (overlay fantôme) | 🧪 Bêta | Pseudo-3D + carte de chaleur avec overlay fantôme de la base ; `Q3DSurface` OpenGL natif à venir |
| Compagnon CAN (lancer SocketSpy) | 🧪 Bêta | Lancer SocketSpy côte à côte pendant la reprogrammation |

### SocketSpy — analyse du bus CAN sous Linux (v0.8.7)

| Fonctionnalité | Maturité |
|----------------|:--------:|
| Moniteur live, décodage DBC, graphes de signaux, émission | ✅ Éprouvé |
| Décodeurs : CANopen / J1939 / ISO-TP / UDS / OBD-II / NMEA2000 | ✅ Éprouvé |
| Scripts Lua, simulateur CAN, fuzzer de trames | ✅ Éprouvé |
| Testeur UDS + simulateur d'ECU | ✅ Éprouvé |
| Export BLF / MDF4, diff de captures, multi-bus, i18n | ✅ Éprouvé |
| Capture io_uring, API MCP / JSON-RPC, Signal Detective | 🧪 Bêta |

> Détails, captures et téléchargements : **[dépôt SocketSpy](https://github.com/Poisson48/SocketSpy)** · **[site SocketSpy](https://poisson48.github.io/SocketSpy)**.

### OpenDAMOS — recettes de cartos portables entre firmwares

Un DAMOS libre, **CC0**, qui relocalise les cartos d'ECU par **empreinte d'axe** au lieu d'adresses codées en dur, de sorte qu'une seule recette fonctionne sur toutes les variantes de firmware d'une même famille d'ECU. Un convertisseur (`scripts/damos_a2l_to_opendamos.py`) transforme un DAMOS constructeur (A2L) + une ROM de référence en recette ouverte.

| Couverture | Maturité |
|------------|:--------:|
| Bosch EDC16 (big-endian inline) | ✅ Éprouvé (vérifié sur plusieurs variantes de firmware) |
| Bosch EDC17 (little-endian inline) | ✅ Éprouvé (vérifié sur plusieurs variantes de firmware) |
| ECU COM_AXIS — PSA Valeo, Continental SID807 | 🧪 Bêta (11/12 sur Valeo, pas encore de test inter-firmware) |
| EDC16CP33 complet | 🔜 À venir |

> Lire la spécification : **[standard OpenDAMOS](docs/opendamos/OPENDAMOS_STANDARD.md)** · brief de conversion : **[convention DAMOS → OpenDAMOS](docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md)**.

### Niveau suite — hub & interconnexion

| Capacité | Maturité |
|----------|:--------:|
| Lanceur (hub) | 🧪 Bêta |
| Boucle flash → vérification (ECU Studio → SocketSpy) | 🧪 Bêta |
| Interconnexion bidirectionnelle complète | 🔜 À venir |
| Davantage d'ECU (catalogue élargi) | 🔜 À venir |

### ECU supportés (ECU Studio)

| ECU | Protocole | Notes |
|-----|-----------|-------|
| EDC16C34 | K-Line | Bosch diesel — Peugeot / Citroën |
| ME7.x | K-Line | Bosch essence |
| MED17 | CAN (UDS) | Bosch essence — injection directe |
| EDC17 | CAN (UDS) | Bosch diesel |

## Démarrage rapide

### Téléchargement (recommandé)

**[⬇ Télécharger ECU Studio (AppImage)](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** — v1.3.0, ~37 Mo, Linux x86_64.

```bash
chmod +x ECU_Studio-x86_64.AppImage
./ECU_Studio-x86_64.AppImage
```

AppImage autonome — Qt6, libusb et toutes les bibliothèques d'exécution sont embarquées : ça tourne sur un PC vierge avec **rien d'installé** et sans accès root. Tous les builds sont sur la [page des releases](https://github.com/Poisson48/ecu_studio_suite/releases/latest).

Pour SocketSpy, voir le **[dépôt SocketSpy](https://github.com/Poisson48/SocketSpy)**.

### Compilation depuis les sources (développeurs)

```bash
# Dépendances Ubuntu / Debian
sudo apt install \
    qt6-base-dev qt6-charts-dev qt6-serialbus-dev qt6-serialport-dev \
    libusb-1.0-0-dev libgit2-dev liblua5.4-dev \
    nlohmann-json3-dev libgtest-dev cmake ninja-build

# Cloner avec le sous-module SocketSpy, puis compiler (mode simulation, sans matériel)
git clone --recurse-submodules https://github.com/Poisson48/ecu_studio_suite
cd ecu_studio_suite
bash build.sh
./build/apps/ecu-studio/ecu_studio
```

`build.sh` vérifie les dépendances et active le mode simulation MPPS par défaut. Options CMake clés : `ECU_BUILD_ECU_STUDIO`, `ECU_BUILD_SOCKETSPY`, `ECU_BUILD_TESTS`, `ECU_MPPS_SIMULATION`, `ECU_MPPS_PROTOCOL_LOG`.

### Matériel réel (Linux)

```bash
sudo cp libs/60-mpps.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# Branchez le MPPS V21, lancez ECU Studio et cliquez sur Rafraîchir dans le panneau MPPS.
```

Linux utilise libusb directement — aucun pilote FTDI D2XX requis. Nécessite un noyau ≥ 5.4 avec SocketCAN.

## Documentation

| Ressource | Lien |
|-----------|------|
| **Site web** | https://poisson48.github.io/ecu_studio_suite/ |
| **Galerie de démos** | https://poisson48.github.io/ecu_studio_suite/demo.html |
| **Wiki** — guides, architecture, OpenDAMOS, FAQ | [github.com/Poisson48/ecu_studio_suite/wiki](https://github.com/Poisson48/ecu_studio_suite/wiki) |
| **OpenDAMOS** — standard & convention | [docs/opendamos/](docs/opendamos/) — [standard](docs/opendamos/OPENDAMOS_STANDARD.md) · [convention](docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md) |
| **Wiki technique** — analyse des écarts | [docs/GAP_ANALYSIS.md](docs/GAP_ANALYSIS.md) |
| **Wiki technique** — MCP / intégration IA | [docs/MCP.md](docs/MCP.md) |
| **Wiki technique** — reverse MPPS | [reverse](docs/mpps-reverse.md) · [checksums](docs/mpps-checksums.md) · [déchiffrement](docs/mpps-decryption.md) |
| **Wiki technique** — base de recherche ECU | [docs/ecu-research/](docs/ecu-research/) |
| **SocketSpy** — dépôt & site | [github.com/Poisson48/SocketSpy](https://github.com/Poisson48/SocketSpy) · [poisson48.github.io/SocketSpy](https://poisson48.github.io/SocketSpy) |

## Plateformes

| Plateforme | Statut | Notes |
|------------|--------|-------|
| Linux x86_64 | Cible principale | libusb, SocketCAN, ensemble complet de fonctionnalités |
| Windows (cross-compile) | Supporté | Chaîne MinGW ; ftd2xx pour le MPPS |
| macOS | Non testé | SocketCAN indisponible |

## Licence

**GPL-3.0** — voir [LICENSE](LICENSE). Les recettes OpenDAMOS sont recommandées sous **CC0-1.0**.

---

<div align="center">

**ECU Studio Suite** — flashez une carto, vérifiez-la en direct. 100 % local. ·
[Website](https://poisson48.github.io/ecu_studio_suite/) · [SocketSpy](https://github.com/Poisson48/SocketSpy)

</div>
