# Sub-Programs

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

The suite is a **hub** that launches two sub-programs that share one Qt6 dark theme and one sidebar navigation. See **[Architecture](Architecture)** for how they interconnect.

### ECU Studio — the ECU reprogramming app

> Repo: https://github.com/Poisson48/ecu_studio_suite · current release **v1.4.1** · status legend below.
> **Proven** = stable · **Beta** = new / works-but-unproven · **Incoming** = roadmap.

| Feature | Status | What it does |
|---------|--------|--------------|
| **ROM read / write (MPPS)** | Draft | Simulation mode only (`ECU_MPPS_SIMULATION` on by default); **not yet verified on real hardware**. |
| **Hex view** | Proven | Fast hex editor with search, byte-level diff overlay, and address jump. |
| **Map editor (2D)** | Proven | 2-D heatmap visualization; edit scalar, curve and table maps; CSV import/export. |
| **Map editor (3D)** | Beta | Pseudo-3D surface + heatmap with a *ghost* baseline overlay (original vs modified) works today; native OpenGL `Q3DSurface` rendering is **Incoming** (the AppImage currently uses the QPainter renderer). |
| **DAMOS editor** | Proven | Create and edit `open_damos` definitions in-app: add/remove characteristics, edit fields, detect maps from a ROM, manage one-click AutoMods, export A2L. See **[OpenDAMOS](OpenDAMOS)**. |
| **A2L browser & export** | Proven | Parse ASAP2 `.a2l` files; browse measurements/characteristics by ECU; export relocated `open_damos` maps to standard A2L. |
| **Checksum panel** | Proven | Compute and patch checksums for supported ECU families. |
| **Compare panel** | Proven | Side-by-side diff of two ROM files; byte-level delta, filterable by region. |
| **AutoMods panel** | Proven | Apply named calibration patches from a JSON recipe; batch apply / revert. |
| **Git versioning** | Proven | libgit2-backed ROM history — commit any state, browse the log, restore any previous version straight into the editor. |
| **Project manager** | Proven | `.ecuproj` files holding ROM path, ECU type, notes and flash log. |
| **CAN companion** | Beta | Launches SocketSpy side-by-side for live CAN monitoring during reprogramming — the verify half of the flash → verify loop. |
| **In-app auto-update** | Proven | The AppImage checks GitHub for new signed releases and updates itself (Ed25519-signed manifest + SHA-256). |
| **Bilingual UI + brand identity** | Proven | Complete French/English translations; the bundled EDC16C34 `open_damos` definition is fully in English. |

**Supported ECUs:**

| ECU | Protocol | Notes |
|-----|----------|-------|
| EDC16C34 | K-Line | Bosch diesel — Peugeot / Citroën |
| ME7.x | K-Line | Bosch petrol |
| MED17 | CAN (UDS) | Bosch petrol — direct injection |
| EDC17 | CAN (UDS) | Bosch diesel |

**MPPS V21 driver (`libs/mpps`):** K-Line and CAN physical protocols, configurable bitrate, block read/write/erase with progress, hardware checksum verification, a **simulation mode** (`-DECU_MPPS_SIMULATION=ON`) for hardware-free dev/CI, and protocol logging (`-DECU_MPPS_PROTOCOL_LOG=ON`).

### SocketSpy — the CAN bus analysis companion

> Repo: https://github.com/Poisson48/SocketSpy · site: https://poisson48.github.io/SocketSpy · current release **v0.8.7** · GPL-3.0.
> **Full feature documentation lives in the SocketSpy wiki:** https://github.com/Poisson48/SocketSpy/wiki

SocketSpy is the Linux SocketCAN analysis half of the suite. It is what closes the flash → verify loop: after ECU Studio flashes a change, SocketSpy proves on the live bus that it took effect.

| Capability | What it does |
|------------|--------------|
| **Live monitor** | Real-time CAN frame view, multi-bus. |
| **DBC decode** | Decode raw frames into named signals via DBC databases. |
| **Signal graphs** | Plot decoded signals over time. |
| **Transmit** | Send single frames or periodic frames. |
| **Protocol decoders** | CANopen, J1939, ISO-TP, UDS, OBD-II, NMEA2000. |
| **Lua scripting** | Script the bus — react, filter, transform. |
| **CAN simulator** | Synthesize a bus when you have no hardware. |
| **UDS tester + ECU simulator** | Drive diagnostic sessions, or impersonate an ECU. |
| **Frame fuzzer** | Fuzz frames to probe ECU behaviour. |
| **io_uring capture** | High-throughput zero-copy capture. |
| **MCP / JSON-RPC API** | Drive SocketSpy from an AI agent or external tool. |
| **BLF / MDF4 export** | Export captures to industry-standard formats. |
| **Capture diff** | Compare two captures to spot what changed. |
| **Signal Detective** | Auto-discover signals in unknown traffic. |
| **i18n** | Bilingual UI (English / French). |

> Some of the newest SocketSpy panels are **Beta** — see the SocketSpy wiki for per-feature status.

### How they work together
1. **ECU Studio** edits a map and flashes it through MPPS V21.
2. **SocketSpy** reads the live CAN bus and confirms the change took effect at the right operating point.

Both apps share `libs/can-core` (an alias over SocketSpy's CAN core), the Qt6 dark theme, and the sidebar nav — see **[Architecture](Architecture)** for the full picture.

### See also
- **[Architecture](Architecture)** — the hub and the flash → verify loop.
- **[OpenDAMOS](OpenDAMOS)** — the recipe format the DAMOS editor reads/writes.
- **[Getting Started](Getting-Started)** — install both and run the loop.
- **SocketSpy wiki** — https://github.com/Poisson48/SocketSpy/wiki

---

<a id="français"></a>

## Français

La suite est un **hub** qui lance deux sous-programmes partageant un thème sombre Qt6 et une navigation latérale. Voir **[Architecture](Architecture)** pour leur interconnexion.

### ECU Studio — l'app de reprogrammation ECU

> Dépôt : https://github.com/Poisson48/ecu_studio_suite · version actuelle **v1.4.1** · légende ci-dessous.
> **Proven** (éprouvé) = stable · **Beta** = neuf / fonctionne mais non éprouvé · **Incoming** (à venir) = feuille de route.

| Fonctionnalité | Statut | Ce que ça fait |
|----------------|--------|----------------|
| **Lecture / écriture ROM (MPPS)** | Brouillon | Mode simulation uniquement (`ECU_MPPS_SIMULATION` activé par défaut) ; **pas encore validé sur matériel réel**. |
| **Vue hexadécimale** | Proven | Éditeur hex rapide avec recherche, overlay de diff à l'octet et saut d'adresse. |
| **Éditeur de cartes (2D)** | Proven | Visualisation heatmap 2D ; édition scalaire/courbe/table ; import/export CSV. |
| **Éditeur de cartes (3D)** | Beta | Surface pseudo-3D + heatmap avec overlay *fantôme* (original vs modifié) fonctionnel ; le rendu OpenGL natif `Q3DSurface` est **Incoming** (l'AppImage utilise le rendu QPainter). |
| **Éditeur DAMOS** | Proven | Créer/éditer des définitions `open_damos` dans l'app : ajout/suppression de caractéristiques, édition, détection de cartes depuis une ROM, AutoMods en un clic, export A2L. Voir **[OpenDAMOS](OpenDAMOS)**. |
| **Navigateur & export A2L** | Proven | Parse les `.a2l` ASAP2 ; navigation measurements/characteristics par ECU ; export des cartes `open_damos` relocalisées en A2L standard. |
| **Panneau Checksum** | Proven | Calcule et patche les checksums des familles d'ECU supportées. |
| **Panneau Compare** | Proven | Diff côte à côte de deux ROM ; delta à l'octet, filtrable par région. |
| **Panneau AutoMods** | Proven | Applique des patches de calibration nommés depuis une recette JSON ; appliquer / annuler en lot. |
| **Versionnement git** | Proven | Historique ROM via libgit2 — committer tout état, parcourir le log, restaurer n'importe quelle version dans l'éditeur. |
| **Gestionnaire de projet** | Proven | Fichiers `.ecuproj` : chemin ROM, type d'ECU, notes et journal de flash. |
| **Compagnon CAN** | Beta | Lance SocketSpy côte à côte pour le monitoring CAN live pendant la reprogrammation — la moitié « vérification » de la boucle. |
| **Mise à jour intégrée** | Proven | L'AppImage vérifie GitHub pour de nouvelles releases signées et se met à jour (manifeste Ed25519 + SHA-256). |
| **UI bilingue + identité** | Proven | Traductions FR/EN complètes ; la définition `open_damos` EDC16C34 embarquée est entièrement en anglais. |

**ECU supportés :**

| ECU | Protocole | Notes |
|-----|-----------|-------|
| EDC16C34 | K-Line | Diesel Bosch — Peugeot / Citroën |
| ME7.x | K-Line | Essence Bosch |
| MED17 | CAN (UDS) | Essence Bosch — injection directe |
| EDC17 | CAN (UDS) | Diesel Bosch |

**Pilote MPPS V21 (`libs/mpps`) :** protocoles physiques K-Line et CAN, débit configurable, lecture/écriture/effacement par bloc avec progression, vérification matérielle du checksum, un **mode simulation** (`-DECU_MPPS_SIMULATION=ON`) pour le dev/CI sans matériel, et un log de protocole (`-DECU_MPPS_PROTOCOL_LOG=ON`).

### SocketSpy — le compagnon d'analyse du bus CAN

> Dépôt : https://github.com/Poisson48/SocketSpy · site : https://poisson48.github.io/SocketSpy · version actuelle **v0.8.7** · GPL-3.0.
> **La documentation complète des fonctionnalités est dans le wiki SocketSpy :** https://github.com/Poisson48/SocketSpy/wiki

SocketSpy est la moitié analyse SocketCAN sous Linux de la suite. C'est lui qui boucle la boucle flash → vérification : après qu'ECU Studio a flashé un changement, SocketSpy prouve sur le bus live qu'il a pris effet.

| Capacité | Ce que ça fait |
|----------|----------------|
| **Moniteur live** | Vue temps réel des trames CAN, multi-bus. |
| **Décodage DBC** | Décode les trames brutes en signaux nommés via des bases DBC. |
| **Graphes de signaux** | Trace les signaux décodés dans le temps. |
| **Émission** | Envoie des trames uniques ou périodiques. |
| **Décodeurs de protocoles** | CANopen, J1939, ISO-TP, UDS, OBD-II, NMEA2000. |
| **Scripting Lua** | Scripter le bus — réagir, filtrer, transformer. |
| **Simulateur CAN** | Synthétiser un bus sans matériel. |
| **Testeur UDS + simulateur d'ECU** | Mener des sessions diagnostic, ou se faire passer pour un ECU. |
| **Fuzzer de trames** | Fuzze les trames pour sonder le comportement d'un ECU. |
| **Capture io_uring** | Capture haut débit zéro-copie. |
| **API MCP / JSON-RPC** | Piloter SocketSpy depuis un agent IA ou un outil externe. |
| **Export BLF / MDF4** | Exporte les captures vers des formats industriels standard. |
| **Diff de captures** | Compare deux captures pour repérer ce qui a changé. |
| **Signal Detective** | Découverte automatique de signaux dans du trafic inconnu. |
| **i18n** | UI bilingue (anglais / français). |

> Certains des panneaux SocketSpy les plus récents sont en **Beta** — voir le wiki SocketSpy pour le statut par fonctionnalité.

### Comment ils travaillent ensemble
1. **ECU Studio** édite une carte et la flashe via MPPS V21.
2. **SocketSpy** lit le bus CAN live et confirme que le changement a pris effet au bon point de fonctionnement.

Les deux apps partagent `libs/can-core` (un alias du cœur CAN de SocketSpy), le thème sombre Qt6 et la navigation latérale — voir **[Architecture](Architecture)** pour le tableau complet.

### Voir aussi
- **[Architecture](Architecture)** — le hub et la boucle flash → vérification.
- **[OpenDAMOS](OpenDAMOS)** — le format de recette que l'éditeur DAMOS lit/écrit.
- **[Getting Started](Getting-Started)** — installer les deux et lancer la boucle.
- **Wiki SocketSpy** — https://github.com/Poisson48/SocketSpy/wiki
