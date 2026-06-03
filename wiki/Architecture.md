# Architecture

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

### The hub model

ECU Studio Suite is built as a **hub** that launches specialized sub-programs and interconnects them. Today the two sub-programs are **ECU Studio** (the main app, which also acts as the launcher) and **SocketSpy** (the CAN analysis companion). They share the same Qt6 dark theme and the same sidebar-navigation component, so moving between them feels like one product.

> Status: the hub launcher and the full flash → verify interconnection are **Beta** — they work today and are actively hardened. Deeper, automatic interconnection is **Incoming** on the roadmap.

```
                ┌──────────────────────────────────────────────┐
                │              ECU Studio Suite (hub)           │
                │   shared Qt6 dark theme · shared sidebar nav  │
                └───────────────┬───────────────┬──────────────┘
                                │ launches       │ launches
                  ┌─────────────▼──────┐   ┌─────▼───────────────────┐
                  │     ECU Studio      │   │        SocketSpy        │
                  │  (apps/ecu-studio)  │   │     (apps/socketspy,    │
                  │                     │   │      git submodule)     │
                  │ ROM · maps · DAMOS  │   │ live CAN · DBC · UDS    │
                  │ A2L · checksums     │   │ decoders · Lua · sim    │
                  │ MPPS flash · git    │   │ fuzzer · MCP/JSON-RPC   │
                  └─────────┬───────────┘   └──────────┬──────────────┘
                            │                          │
                  ┌─────────▼──────────────────────────▼──────────────┐
                  │                 Shared libraries                   │
                  │  libs/ecu-core  ·  libs/mpps  ·  libs/can-core  ·  │
                  │                 libs/shared                        │
                  └────────────────────────────────────────────────────┘
```

### Repository layout

```
ecu_studio_suite/
├── apps/
│   ├── ecu-studio/         Main ECU Studio app — also the hub launcher
│   │   ├── src/panels/     Feature panels (mpps, hex_view, map_editor, git, a2l, damos_editor…)
│   │   └── i18n/           Qt translations (fr / en)
│   └── socketspy/          SocketSpy (git submodule) — CAN spy, shared theme + sidebar
├── libs/
│   ├── ecu-core/           ECU business logic (catalog, patcher, parser, OpenDamos, git…)
│   ├── mpps/               MPPS V21 USB driver (libusb + simulation mode)
│   ├── can-core/           Thin alias over SocketSpy's CAN core (SocketCAN, decoders)
│   └── shared/             Qt6 palette + shared utilities
├── tests/                  GTest unit + integration tests, ROM/A2L fixtures
├── tools/reverse/          MPPS protocol reverse-engineering tools
└── build.sh · CMakeLists.txt · vcpkg.json
```

### Shared libraries

| Library | Role |
|---------|------|
| **`libs/ecu-core`** | The C++23 port of `open_car_reprog`. Modules: `EcuCatalog` (SQLite ECU database), `RomPatcher` (checksum-aware binary patching), `MapFinder` (heuristic map detection), `MapDiffer` (structural map diff), `A2lParser` (full ASAP2), `ProjectManager` (`.ecuproj`), `VehicleTemplates`, `OpenDamos` (recipe import + relocation), `GitManager` (libgit2 wrapper). |
| **`libs/mpps`** | USB driver for the MPPS V21 programmer (FTDI-based, libusb on Linux). K-Line and CAN physical protocols, block read/write/erase with progress, hardware checksum verification, a **simulation mode** for hardware-free development/CI, and an optional protocol log. |
| **`libs/can-core`** | A thin alias over **SocketSpy's** CAN core (SocketCAN access, protocol decoders). This is the shared seam that lets ECU Studio reason about the same CAN stack SocketSpy uses. |
| **`libs/shared`** | The shared Qt6 color palette and UI utilities — the visual glue that makes the two apps look like one suite. |

### The flash → verify interconnection loop (flagship)

This is the loop the whole suite is built around. It closes the gap between *"I changed a number in a map"* and *"the engine actually behaves differently at that operating point."*

```
   ┌──────────────┐   1. edit map / apply        ┌──────────────┐
   │              │      OpenDAMOS recipe         │              │
   │  ECU Studio  │ ───────────────────────────► │   The ROM    │
   │              │   2. flash via MPPS V21       │  on the ECU  │
   └──────┬───────┘ ───────────────────────────► └──────┬───────┘
          │                                              │ 3. ECU runs the
          │ launches SocketSpy                           │    new calibration
          ▼                                              ▼
   ┌──────────────┐   4. read live signals       ┌──────────────┐
   │              │      (DBC / UDS / OBD-II)     │              │
   │  SocketSpy   │ ◄─────────────────────────── │  CAN bus     │
   │              │   5. confirm the change took  │              │
   └──────────────┘      effect at the right RPM  └──────────────┘
                         / load point  ✓ / ✗
```

1. In **ECU Studio**, edit a map directly, or apply an [OpenDAMOS](OpenDAMOS) recipe / AutoMod (e.g. Stage 1, EGR off).
2. Flash the modified ROM into the ECU over **MPPS V21** (`libs/mpps`).
3. The ECU now runs the new calibration.
4. Launch **SocketSpy** (ECU Studio has a CAN-companion launcher) and read the live signals on the CAN bus — decoded via DBC, or queried via UDS / OBD-II.
5. **Confirm**: the modified value shows up on the bus at the correct operating point (the right RPM / load). The change is verified, not assumed.

The technical seam that makes this honest is `libs/can-core`, which aliases SocketSpy's CAN stack so both halves of the loop speak the same protocol layer.

### Design principles
- **100% local** — no telemetry, no network calls, no cloud. Everything is on your machine.
- **One look, two tools** — shared theme and sidebar so the suite feels unified.
- **Hardware-optional development** — MPPS simulation mode and SocketCAN `vcan` let you run the whole stack without any hardware.
- **Open formats** — `.ecuproj` projects, standard A2L export, and the CC0 [OpenDAMOS](OpenDAMOS) recipe format.

### Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Primary target | libusb, SocketCAN, full feature set. |
| Windows (cross-compile) | Supported | MinGW toolchain; ftd2xx for MPPS on Windows. |
| macOS | Not tested | SocketCAN unavailable. |

### See also
- **[Sub-Programs](Sub-Programs)** — the full feature list of each app.
- **[OpenDAMOS](OpenDAMOS)** — the recipe format flowing through step 1.
- **[Getting Started](Getting-Started)** — build the suite and bring up the loop.

---

<a id="français"></a>

## Français

### Le modèle hub

ECU Studio Suite est conçue comme un **hub** qui lance des sous-programmes spécialisés et les interconnecte. Aujourd'hui les deux sous-programmes sont **ECU Studio** (l'application principale, qui fait aussi office de lanceur) et **SocketSpy** (le compagnon d'analyse CAN). Ils partagent le même thème sombre Qt6 et le même composant de navigation latérale : passer de l'un à l'autre donne l'impression d'un seul produit.

> Statut : le lanceur hub et l'interconnexion complète flash → vérification sont en **Beta** — ils fonctionnent aujourd'hui et sont en cours de durcissement. Une interconnexion automatique plus poussée est **Incoming** sur la feuille de route.

```
                ┌──────────────────────────────────────────────┐
                │              ECU Studio Suite (hub)           │
                │  thème sombre Qt6 partagé · navigation latér. │
                └───────────────┬───────────────┬──────────────┘
                                │ lance          │ lance
                  ┌─────────────▼──────┐   ┌─────▼───────────────────┐
                  │     ECU Studio      │   │        SocketSpy        │
                  │  (apps/ecu-studio)  │   │     (apps/socketspy,    │
                  │                     │   │    sous-module git)     │
                  │ ROM · cartes ·DAMOS │   │ CAN live · DBC · UDS    │
                  │ A2L · checksums     │   │ décodeurs · Lua · sim   │
                  │ flash MPPS · git    │   │ fuzzer · MCP/JSON-RPC   │
                  └─────────┬───────────┘   └──────────┬──────────────┘
                            │                          │
                  ┌─────────▼──────────────────────────▼──────────────┐
                  │             Bibliothèques partagées                │
                  │  libs/ecu-core  ·  libs/mpps  ·  libs/can-core  ·  │
                  │                 libs/shared                        │
                  └────────────────────────────────────────────────────┘
```

### Arborescence du dépôt

```
ecu_studio_suite/
├── apps/
│   ├── ecu-studio/         App principale ECU Studio — aussi le lanceur hub
│   │   ├── src/panels/     Panneaux (mpps, hex_view, map_editor, git, a2l, damos_editor…)
│   │   └── i18n/           Traductions Qt (fr / en)
│   └── socketspy/          SocketSpy (sous-module git) — spy CAN, thème + sidebar partagés
├── libs/
│   ├── ecu-core/           Logique métier ECU (catalog, patcher, parser, OpenDamos, git…)
│   ├── mpps/               Pilote USB MPPS V21 (libusb + mode simulation)
│   ├── can-core/           Alias léger du cœur CAN de SocketSpy (SocketCAN, décodeurs)
│   └── shared/             Palette Qt6 + utilitaires partagés
├── tests/                  Tests GTest unitaires + intégration, fixtures ROM/A2L
├── tools/reverse/          Outils de reverse du protocole MPPS
└── build.sh · CMakeLists.txt · vcpkg.json
```

### Bibliothèques partagées

| Bibliothèque | Rôle |
|--------------|------|
| **`libs/ecu-core`** | Le portage C++23 de `open_car_reprog`. Modules : `EcuCatalog` (base ECU SQLite), `RomPatcher` (patch binaire conscient du checksum), `MapFinder` (détection heuristique de cartes), `MapDiffer` (diff structurel), `A2lParser` (ASAP2 complet), `ProjectManager` (`.ecuproj`), `VehicleTemplates`, `OpenDamos` (import de recette + relocalisation), `GitManager` (wrapper libgit2). |
| **`libs/mpps`** | Pilote USB du programmateur MPPS V21 (FTDI, libusb sous Linux). Protocoles physiques K-Line et CAN, lecture/écriture/effacement par bloc avec progression, vérification matérielle du checksum, un **mode simulation** pour le dev/CI sans matériel, et un log de protocole optionnel. |
| **`libs/can-core`** | Un alias léger du cœur CAN de **SocketSpy** (accès SocketCAN, décodeurs de protocoles). C'est la couture partagée qui permet à ECU Studio de raisonner sur la même pile CAN que SocketSpy. |
| **`libs/shared`** | La palette de couleurs Qt6 partagée et les utilitaires UI — la colle visuelle qui fait que les deux apps ressemblent à une seule suite. |

### La boucle d'interconnexion flash → vérification (phare)

C'est la boucle autour de laquelle toute la suite est construite. Elle comble l'écart entre *« j'ai changé un nombre dans une carte »* et *« le moteur se comporte vraiment différemment à ce point de fonctionnement »*.

```
   ┌──────────────┐   1. éditer la carte /        ┌──────────────┐
   │              │      appliquer une recette     │              │
   │  ECU Studio  │ ───────────────────────────► │   La ROM     │
   │              │   2. flasher via MPPS V21      │  dans l'ECU  │
   └──────┬───────┘ ───────────────────────────► └──────┬───────┘
          │                                              │ 3. l'ECU exécute
          │ lance SocketSpy                              │    la nouvelle calib
          ▼                                              ▼
   ┌──────────────┐   4. lire les signaux live    ┌──────────────┐
   │              │      (DBC / UDS / OBD-II)      │              │
   │  SocketSpy   │ ◄─────────────────────────── │  Bus CAN     │
   │              │   5. confirmer l'effet au bon  │              │
   └──────────────┘      régime / charge  ✓ / ✗   └──────────────┘
```

1. Dans **ECU Studio**, éditez une carte directement, ou appliquez une recette [OpenDAMOS](OpenDAMOS) / un AutoMod (ex. Stage 1, EGR off).
2. Flashez la ROM modifiée dans l'ECU via **MPPS V21** (`libs/mpps`).
3. L'ECU exécute désormais la nouvelle calibration.
4. Lancez **SocketSpy** (ECU Studio a un lanceur compagnon CAN) et lisez les signaux en direct sur le bus CAN — décodés via DBC, ou interrogés via UDS / OBD-II.
5. **Confirmez** : la valeur modifiée apparaît sur le bus au bon point de fonctionnement (le bon régime / la bonne charge). Le changement est vérifié, pas supposé.

La couture technique qui rend cela honnête est `libs/can-core`, qui aliase la pile CAN de SocketSpy pour que les deux moitiés de la boucle parlent la même couche de protocole.

### Principes de conception
- **100 % local** — aucune télémétrie, aucun appel réseau, aucun cloud. Tout est sur votre machine.
- **Un look, deux outils** — thème et sidebar partagés pour une suite unifiée.
- **Développement sans matériel** — le mode simulation MPPS et le `vcan` SocketCAN permettent de faire tourner toute la pile sans matériel.
- **Formats ouverts** — projets `.ecuproj`, export A2L standard, et le format de recette CC0 [OpenDAMOS](OpenDAMOS).

### Plateformes

| Plateforme | Statut | Notes |
|------------|--------|-------|
| Linux x86_64 | Cible principale | libusb, SocketCAN, jeu complet. |
| Windows (cross-compile) | Supporté | Toolchain MinGW ; ftd2xx pour MPPS sous Windows. |
| macOS | Non testé | SocketCAN indisponible. |

### Voir aussi
- **[Sub-Programs](Sub-Programs)** — la liste complète des fonctionnalités de chaque app.
- **[OpenDAMOS](OpenDAMOS)** — le format de recette qui circule à l'étape 1.
- **[Getting Started](Getting-Started)** — compiler la suite et monter la boucle.
