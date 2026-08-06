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
| **`libs/ecu-core`** | The C++23 port of `open_car_reprog`. Modules: `EcuCatalog`, `RomPatcher`, `MapFinder`, `MapDiffer`, `A2lParser`, `ProjectManager`, `VehicleTemplates`, `OpenDamos` (recipe import + relocation), `GitManager`, plus **`MapSampler` / `TuneValidation`** (bilinear sample of relocated maps vs live OBD for drive-mode validation) and OBD freeze-frame helpers in `Obd2`. |
| **`libs/mpps`** | USB driver for the MPPS V21 programmer (FTDI-based, libusb on Linux). K-Line and CAN physical protocols, block read/write/erase with progress, hardware checksum verification, a **simulation mode** for hardware-free development/CI, and an optional protocol log. |
| **`libs/can-core`** | A thin alias over **SocketSpy's** CAN core (SocketCAN access, protocol decoders). This is the shared seam that lets ECU Studio reason about the same CAN stack SocketSpy uses. |
| **`libs/shared`** | The shared Qt6 color palette and UI utilities — the visual glue that makes the two apps look like one suite. |

### The flash → verify interconnection loop (flagship)

This is the loop the whole suite is built around. It closes the gap between *"I changed a number in a map"* and *"the engine actually behaves differently at that operating point."*

There are **two verify paths** after flash:

```
   ┌──────────────┐   1. edit map / OpenDAMOS     ┌──────────────┐
   │  ECU Studio  │ ───────────────────────────► │  ROM on ECU  │
   │              │   2. flash via MPPS V21       └──────┬───────┘
   └──────┬───────┘                                     │ 3. runs new calib
          │                                             │
     ┌────┴────────────────────┐                        │
     │                         │                        │
     ▼                         ▼                        ▼
┌────────────┐          ┌────────────┐          ┌────────────┐
│ OBD panel  │ 4a ELM327│ SocketSpy  │ 4b CAN   │  vehicle   │
│ drive mode │◄─────────│ DBC/UDS/…  │◄─────────│  sensors   │
│ MapSampler │ 5a ✓/✗   │            │ 5b ✓/✗   └────────────┘
└────────────┘          └────────────┘
```

1. In **ECU Studio**, edit a map or apply an [OpenDAMOS](OpenDAMOS) recipe / AutoMod.
2. Flash via **MPPS V21** (`libs/mpps`).
3. The ECU runs the new calibration.
4a. **Road (v1.6.6+):** **[OBD Drive Mode](OBD-Drive-Mode)** — ELM327 live PIDs vs OpenDAMOS expected (`MapSampler` + `TuneValidation` in `libs/ecu-core`).
4b. **Bench / deep CAN:** launch **SocketSpy**, decode DBC / UDS / OBD-II; optional continuous validation via MCP.
5. **Confirm** at the right RPM / load — verified, not assumed.

Shared CAN reasoning still goes through `libs/can-core` (SocketSpy’s stack). OBD sampling and map expected values live in `libs/ecu-core`.

### Design principles
- **Free forever & private** — GPL-3.0, no account; no telemetry, no cloud. Everything stays on your machine.
- **One look, two tools** — shared theme and sidebar so the suite feels unified.
- **Verify without a full CAN stack** — a cheap ELM327 is enough for drive-mode validation.
- **Hardware-optional development** — MPPS simulation mode and SocketCAN `vcan` let you run the stack without hardware.
- **Open formats** — `.ecuproj` projects, standard A2L export, and the CC0 [OpenDAMOS](OpenDAMOS) recipe format.

### Platform support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Primary target | libusb, SocketCAN, full feature set. |
| Windows (cross-compile) | Supported | MinGW toolchain; ftd2xx for MPPS on Windows. |
| macOS | Not tested | SocketCAN unavailable. |

### See also
- **[OBD Drive Mode](OBD-Drive-Mode)** — road validation path (4a).
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
| **`libs/ecu-core`** | Le portage C++23 de `open_car_reprog`. Modules : `EcuCatalog`, `RomPatcher`, `MapFinder`, `MapDiffer`, `A2lParser`, `ProjectManager`, `VehicleTemplates`, `OpenDamos`, `GitManager`, plus **`MapSampler` / `TuneValidation`** (échantillonnage bilinéaire des cartes relocalisées vs OBD live pour le mode conduite) et aides freeze-frame OBD dans `Obd2`. |
| **`libs/mpps`** | Pilote USB du programmateur MPPS V21 (FTDI, libusb sous Linux). Protocoles physiques K-Line et CAN, lecture/écriture/effacement par bloc avec progression, vérification matérielle du checksum, un **mode simulation** pour le dev/CI sans matériel, et un log de protocole optionnel. |
| **`libs/can-core`** | Un alias léger du cœur CAN de **SocketSpy** (accès SocketCAN, décodeurs de protocoles). C'est la couture partagée qui permet à ECU Studio de raisonner sur la même pile CAN que SocketSpy. |
| **`libs/shared`** | La palette de couleurs Qt6 partagée et les utilitaires UI — la colle visuelle qui fait que les deux apps ressemblent à une seule suite. |

### La boucle d'interconnexion flash → vérification (phare)

C'est la boucle autour de laquelle toute la suite est construite. Elle comble l'écart entre *« j'ai changé un nombre dans une carte »* et *« le moteur se comporte vraiment différemment à ce point de fonctionnement »*.

Il y a **deux chemins de vérification** après le flash :

```
   ┌──────────────┐   1. éditer / OpenDAMOS       ┌──────────────┐
   │  ECU Studio  │ ───────────────────────────► │  ROM ECU     │
   │              │   2. flash MPPS V21           └──────┬───────┘
   └──────┬───────┘                                     │ 3. nouvelle calib
          │                                             │
     ┌────┴────────────────────┐                        │
     ▼                         ▼                        ▼
┌────────────┐          ┌────────────┐          ┌────────────┐
│ Panneau OBD│ 4a ELM327│ SocketSpy  │ 4b CAN   │  véhicule  │
│ mode condu.│◄─────────│ DBC/UDS/…  │◄─────────│  capteurs  │
│ MapSampler │ 5a ✓/✗   │            │ 5b ✓/✗   └────────────┘
└────────────┘          └────────────┘
```

1. Dans **ECU Studio**, éditez une carte ou appliquez une recette [OpenDAMOS](OpenDAMOS) / AutoMod.
2. Flashez via **MPPS V21** (`libs/mpps`).
3. L'ECU exécute la nouvelle calibration.
4a. **Route (v1.6.6+) :** **[OBD Drive Mode](OBD-Drive-Mode)** — PID ELM327 vs attendu OpenDAMOS (`MapSampler` + `TuneValidation` dans `libs/ecu-core`).
4b. **Banc / CAN profond :** lancez **SocketSpy** (DBC / UDS / OBD-II) ; validation continue optionnelle via MCP.
5. **Confirmez** au bon régime / charge — vérifié, pas supposé.

### Principes de conception
- **Gratuit pour toujours & privé** — GPL-3.0, pas de compte ; aucune télémétrie, aucun cloud.
- **Un look, deux outils** — thème et sidebar partagés.
- **Vérifier sans pile CAN complète** — un ELM327 suffit pour le mode conduite.
- **Développement sans matériel** — simulation MPPS et `vcan` SocketCAN.
- **Formats ouverts** — `.ecuproj`, export A2L, recettes CC0 [OpenDAMOS](OpenDAMOS).

### Plateformes

| Plateforme | Statut | Notes |
|------------|--------|-------|
| Linux x86_64 | Cible principale | libusb, SocketCAN, jeu complet. |
| Windows (cross-compile) | Supporté | Toolchain MinGW ; ftd2xx pour MPPS sous Windows. |
| macOS | Non testé | SocketCAN indisponible. |

### Voir aussi
- **[OBD Drive Mode](OBD-Drive-Mode)** — chemin de validation route (4a).
- **[Sub-Programs](Sub-Programs)** — la liste complète des fonctionnalités de chaque app.
- **[OpenDAMOS](OpenDAMOS)** — le format de recette qui circule à l'étape 1.
- **[Getting Started](Getting-Started)** — compiler la suite et monter la boucle.
