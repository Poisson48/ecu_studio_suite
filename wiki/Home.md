<!--
  PUBLISHING THIS WIKI TO GITHUB
  ------------------------------
  GitHub wikis are a separate git repository: <repo>.wiki.git.
  To publish these pages to https://github.com/Poisson48/ecu_studio_suite/wiki :

    # 1. Enable the wiki once in the repo Settings (Features -> Wikis), then create
    #    a first page in the web UI so the wiki repo exists.
    # 2. Clone the wiki repo and copy these files in:
    git clone https://github.com/Poisson48/ecu_studio_suite.wiki.git
    cp /home/valou/leo/ecu-docs/wiki/*.md ecu_studio_suite.wiki/
    cd ecu_studio_suite.wiki
    git add . && git commit -m "Add ECU Studio Suite wiki" && git push

  Notes:
  - GitHub uses the file name as the page title and as the link target.
    A file named `Getting-Started.md` is reached as `.../wiki/Getting-Started`
    and linked as [Getting Started](Getting-Started). Keep the names as-is.
  - `Home.md` is the wiki landing page. `_Sidebar.md` / `_Footer.md` (optional)
    render on every page if you add them later.
  - Wiki links are relative to the wiki root: use [Text](Page-Name), no `.md`.
-->

# ECU Studio Suite — Wiki

[English](#english) · [Français](#français)

---

<a id="english"></a>

## English

**ECU Studio Suite** is a 100% local, Qt6/C++23 automotive software suite for Linux. There is **no telemetry, no network calls, no cloud** — everything runs on your machine. The suite is a **hub** that launches specialized sub-programs and interconnects them. The flagship loop: **ECU Studio flashes a map into an ECU, then SocketSpy verifies live on the CAN bus that the change actually took effect at the right operating point.**

- **Website:** https://poisson48.github.io/ecu_studio_suite/
- **ECU Studio repo:** https://github.com/Poisson48/ecu_studio_suite
- **SocketSpy repo:** https://github.com/Poisson48/SocketSpy · **site:** https://poisson48.github.io/SocketSpy · **wiki:** https://github.com/Poisson48/SocketSpy/wiki
- **License:** GPL-3.0

### The two sub-programs at a glance

| Sub-program | Role |
|-------------|------|
| **ECU Studio** | ECU reprogramming, 2D + 3D map editor, DAMOS editor, A2L browser, hex view, checksums, MPPS V21 flashing, ROM compare, git versioning, AutoMods, and OpenDAMOS. |
| **SocketSpy** | Linux SocketCAN analysis — live monitor, DBC decode, signal graphs, transmit, protocol decoders (CANopen / J1939 / ISO-TP / UDS / OBD-II / NMEA2000), Lua scripting, CAN simulator, MCP/JSON-RPC API, io_uring capture, frame fuzzer, UDS tester + ECU simulator, BLF/MDF4 export, capture diff, multi-bus, Signal Detective, i18n. |

### Wiki pages

| Page | What's inside |
|------|---------------|
| **[Getting Started](Getting-Started)** | Download the AppImage, build from source, run, and connect real MPPS / CAN hardware. |
| **[Architecture](Architecture)** | The hub, the sub-programs, the shared libraries, and the flash → verify interconnection loop. |
| **[OpenDAMOS](OpenDAMOS)** | The free, relocatable DAMOS: axis-fingerprint relocation, supported ECUs and maturity, the A2L → OpenDAMOS converter. |
| **[Sub-Programs](Sub-Programs)** | ECU Studio and SocketSpy feature-by-feature, with a link to the SocketSpy wiki. |
| **[FAQ](FAQ)** | Common questions: hardware, legality, platforms, hub status, troubleshooting. |

### Maturity legend (used across this wiki)

| Badge | Meaning |
|-------|---------|
| **Proven** | Verified / shipped and stable. |
| **Beta** | New, works but unproven (OpenDAMOS COM_AXIS, the hub launcher, the flash → verify loop, brand-new SocketSpy panels). |
| **Incoming** | On the roadmap (full interconnection, more ECUs, EDC16CP33 complete). |

---

<a id="français"></a>

## Français

**ECU Studio Suite** est une suite logicielle automobile 100 % locale, en Qt6/C++23, pour Linux. **Aucune télémétrie, aucun appel réseau, aucun cloud** — tout tourne sur votre machine. La suite est un **hub** qui lance des sous-programmes spécialisés et les interconnecte. La boucle phare : **ECU Studio flashe une cartographie dans un calculateur, puis SocketSpy vérifie en direct sur le bus CAN que le changement a bien pris effet au bon point de fonctionnement.**

- **Site :** https://poisson48.github.io/ecu_studio_suite/
- **Dépôt ECU Studio :** https://github.com/Poisson48/ecu_studio_suite
- **Dépôt SocketSpy :** https://github.com/Poisson48/SocketSpy · **site :** https://poisson48.github.io/SocketSpy · **wiki :** https://github.com/Poisson48/SocketSpy/wiki
- **Licence :** GPL-3.0

### Les deux sous-programmes en un coup d'œil

| Sous-programme | Rôle |
|----------------|------|
| **ECU Studio** | Reprogrammation ECU, éditeur de cartographies 2D + 3D, éditeur DAMOS, navigateur A2L, vue hexadécimale, checksums, flash MPPS V21, comparaison de ROM, versionnement git, AutoMods et OpenDAMOS. |
| **SocketSpy** | Analyse SocketCAN sous Linux — moniteur live, décodage DBC, graphes de signaux, émission, décodeurs de protocoles (CANopen / J1939 / ISO-TP / UDS / OBD-II / NMEA2000), scripting Lua, simulateur CAN, API MCP/JSON-RPC, capture io_uring, fuzzer de trames, testeur UDS + simulateur d'ECU, export BLF/MDF4, diff de captures, multi-bus, Signal Detective, i18n. |

### Pages du wiki

| Page | Contenu |
|------|---------|
| **[Getting Started](Getting-Started)** | Télécharger l'AppImage, compiler depuis les sources, lancer, et brancher le vrai matériel MPPS / CAN. |
| **[Architecture](Architecture)** | Le hub, les sous-programmes, les bibliothèques partagées et la boucle d'interconnexion flash → vérification. |
| **[OpenDAMOS](OpenDAMOS)** | Le DAMOS libre et relocalisable : relocalisation par empreinte d'axes, ECU supportés et maturité, le convertisseur A2L → OpenDAMOS. |
| **[Sub-Programs](Sub-Programs)** | ECU Studio et SocketSpy fonctionnalité par fonctionnalité, avec un lien vers le wiki SocketSpy. |
| **[FAQ](FAQ)** | Questions fréquentes : matériel, légalité, plateformes, statut du hub, dépannage. |

### Légende de maturité (utilisée dans tout ce wiki)

| Badge | Signification |
|-------|---------------|
| **Proven** (éprouvé) | Vérifié / livré et stable. |
| **Beta** | Nouveau, fonctionne mais non éprouvé (OpenDAMOS COM_AXIS, le lanceur hub, la boucle flash → vérification, panneaux SocketSpy tout neufs). |
| **Incoming** (à venir) | Sur la feuille de route (interconnexion complète, plus d'ECU, EDC16CP33 complet). |

---

> Comment publier ce wiki sur GitHub : voir le commentaire en haut de ce fichier (`Home.md`). En résumé : le wiki GitHub est un dépôt séparé `ecu_studio_suite.wiki.git` — clonez-le, copiez ces fichiers `.md`, committez, poussez.
