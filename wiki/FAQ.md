# FAQ

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

### What is ECU Studio Suite, in one sentence?
A 100% local, Qt6/C++23 automotive software suite for Linux that flashes an ECU map and then verifies the change live on the CAN bus — built as a hub (**ECU Studio**) that launches a companion (**SocketSpy**). See **[Home](Home)** and **[Architecture](Architecture)**.

### Does it phone home / collect telemetry?
No. There is **no telemetry, no network calls, no cloud**. The only network access is the optional in-app auto-update, which fetches signed AppImage releases from GitHub (Ed25519-signed manifest + SHA-256 verification) and is user-triggered.

### Do I need to compile anything?
No. Download the self-contained **[AppImage](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)**, `chmod +x`, and run — Qt6, libusb and every runtime library are bundled, no root needed. Building from source is only for developers. See **[Getting Started](Getting-Started)**.

### Do I need hardware to try it?
No. ECU Studio has an MPPS **simulation mode** (`-DECU_MPPS_SIMULATION=ON`), and SocketSpy has a **CAN simulator** plus you can use a virtual SocketCAN bus (`vcan0`). You can run the entire flash → verify loop without any hardware. See **[Getting Started](Getting-Started)**.

### What hardware does it support for flashing?
The **MPPS V21** programmer (FTDI-based) over USB, via `libs/mpps`. On Linux it uses **libusb** directly — no FTDI D2XX driver needed. Install the udev rule once for non-root access (see **[Getting Started](Getting-Started)**).

### Which ECUs are supported?
ECU Studio currently targets **EDC16C34** and **ME7.x** (K-Line), and **MED17** and **EDC17** (CAN/UDS). For the firmware-independent OpenDAMOS recipes, coverage is: Bosch **EDC16** and **EDC17** = **Proven**; **COM_AXIS** ECUs (PSA Valeo, Continental SID807) = **Beta**; **EDC16CP33 complete** = **Incoming**. See **[Sub-Programs](Sub-Programs)** and **[OpenDAMOS](OpenDAMOS)**.

### What is OpenDAMOS and why should I care?
A manufacturer DAMOS hardcodes absolute addresses and breaks on every firmware update. OpenDAMOS describes each map by the **fingerprint of its axes** instead, so one free (CC0) recipe relocates the maps across firmware variants of the same ECU family. There is a converter that turns a manufacturer DAMOS (A2L) + ROM into an open recipe. Full details: **[OpenDAMOS](OpenDAMOS)**.

### What's the difference between ECU Studio and SocketSpy?
**ECU Studio** is the tuning side — ROM editing, maps, DAMOS, checksums, flashing. **SocketSpy** is the analysis side — live CAN monitoring, protocol decoders, UDS, Lua, simulation. They share a theme and sidebar and are designed to be used together: edit/flash in ECU Studio, verify in SocketSpy. See **[Sub-Programs](Sub-Programs)**.

### Where is SocketSpy documented?
In its own wiki: **https://github.com/Poisson48/SocketSpy/wiki** (site: https://poisson48.github.io/SocketSpy). The **[Sub-Programs](Sub-Programs)** page summarizes its capabilities.

### What does "Beta" vs "Proven" vs "Incoming" mean on the status boards?
- **Proven** — verified / shipped and stable.
- **Beta** — new, works but unproven (OpenDAMOS COM_AXIS, the hub launcher, the flash → verify loop, brand-new SocketSpy panels).
- **Incoming** — on the roadmap (full interconnection, more ECUs, EDC16CP33 complete).

### Does the 3D map view use OpenGL?
Today the AppImage uses a pseudo-3D **QPainter** renderer with a ghost baseline overlay (original vs modified). Native OpenGL `Q3DSurface` rendering is **Incoming**. See **[Sub-Programs](Sub-Programs)**.

### Which platforms are supported?
**Linux x86_64** is the primary, full-feature target (libusb, SocketCAN). **Windows** is supported via cross-compile (MinGW; ftd2xx for MPPS). **macOS** is not tested (no SocketCAN). Linux needs a kernel ≥ 5.4 with SocketCAN.

### What's the license?
**GPL-3.0** for the suite (ECU Studio and SocketSpy). OpenDAMOS **recipes** are recommended to be **CC0-1.0** (public domain).

### Is ECU tuning legal?
Modifying engine calibration can affect emissions compliance, warranty, insurance and roadworthiness, and the rules vary by country. This software is a tool; using it on a vehicle is your responsibility. Check your local regulations and only flash hardware you own or are authorized to modify.

### How do I publish this wiki to GitHub?
The GitHub wiki is a separate repo (`ecu_studio_suite.wiki.git`). Clone it, copy these `.md` files in, commit and push. Full instructions are in the comment at the top of **[Home](Home)** (`Home.md`).

### My MPPS V21 isn't detected — what now?
Make sure you installed the udev rule (`libs/60-mpps.rules`), reloaded udev, then plugged the device in and hit **Refresh** in the MPPS panel. No FTDI driver is needed on Linux. Build/run details are in **[Getting Started](Getting-Started)**.

---

<a id="français"></a>

## Français

### ECU Studio Suite en une phrase ?
Une suite logicielle automobile 100 % locale, en Qt6/C++23, pour Linux, qui flashe une cartographie d'ECU puis vérifie le changement en direct sur le bus CAN — conçue comme un hub (**ECU Studio**) qui lance un compagnon (**SocketSpy**). Voir **[Home](Home)** et **[Architecture](Architecture)**.

### Est-ce que ça « téléphone à la maison » / collecte de la télémétrie ?
Non. **Aucune télémétrie, aucun appel réseau, aucun cloud**. Le seul accès réseau est la mise à jour intégrée optionnelle, qui récupère des releases AppImage signées depuis GitHub (manifeste Ed25519 + vérification SHA-256) et qui est déclenchée par l'utilisateur.

### Faut-il compiler quelque chose ?
Non. Téléchargez l'**[AppImage](https://github.com/Poisson48/ecu_studio_suite/releases/latest/download/ECU_Studio-x86_64.AppImage)** autonome, `chmod +x`, et lancez — Qt6, libusb et toutes les bibliothèques runtime sont embarquées, sans root. Compiler depuis les sources, c'est seulement pour les développeurs. Voir **[Getting Started](Getting-Started)**.

### Faut-il du matériel pour l'essayer ?
Non. ECU Studio a un **mode simulation** MPPS (`-DECU_MPPS_SIMULATION=ON`), et SocketSpy a un **simulateur CAN** ; vous pouvez aussi utiliser un bus SocketCAN virtuel (`vcan0`). Toute la boucle flash → vérification tourne sans matériel. Voir **[Getting Started](Getting-Started)**.

### Quel matériel est supporté pour le flash ?
Le programmateur **MPPS V21** (FTDI) en USB, via `libs/mpps`. Sous Linux il utilise **libusb** directement — pas de pilote FTDI D2XX. Installez la règle udev une fois pour l'accès sans root (voir **[Getting Started](Getting-Started)**).

### Quels ECU sont supportés ?
ECU Studio cible aujourd'hui **EDC16C34** et **ME7.x** (K-Line), et **MED17** et **EDC17** (CAN/UDS). Pour les recettes OpenDAMOS indépendantes du firmware : Bosch **EDC16** et **EDC17** = **Proven** ; ECU **COM_AXIS** (PSA Valeo, Continental SID807) = **Beta** ; **EDC16CP33 complet** = **Incoming**. Voir **[Sub-Programs](Sub-Programs)** et **[OpenDAMOS](OpenDAMOS)**.

### Qu'est-ce qu'OpenDAMOS et pourquoi ça compte ?
Un DAMOS constructeur hardcode des adresses absolues et casse à chaque mise à jour firmware. OpenDAMOS décrit chaque carte par l'**empreinte de ses axes**, donc une seule recette libre (CC0) relocalise les cartes sur les variantes firmware d'une même famille d'ECU. Un convertisseur transforme un DAMOS constructeur (A2L) + ROM en recette libre. Détails complets : **[OpenDAMOS](OpenDAMOS)**.

### Quelle différence entre ECU Studio et SocketSpy ?
**ECU Studio** est le côté tuning — édition ROM, cartes, DAMOS, checksums, flash. **SocketSpy** est le côté analyse — monitoring CAN live, décodeurs de protocoles, UDS, Lua, simulation. Ils partagent thème et sidebar et sont faits pour être utilisés ensemble : éditer/flasher dans ECU Studio, vérifier dans SocketSpy. Voir **[Sub-Programs](Sub-Programs)**.

### Où est documenté SocketSpy ?
Dans son propre wiki : **https://github.com/Poisson48/SocketSpy/wiki** (site : https://poisson48.github.io/SocketSpy). La page **[Sub-Programs](Sub-Programs)** résume ses capacités.

### Que signifient « Beta » / « Proven » / « Incoming » sur les tableaux de statut ?
- **Proven** (éprouvé) — vérifié / livré et stable.
- **Beta** — neuf, fonctionne mais non éprouvé (OpenDAMOS COM_AXIS, le lanceur hub, la boucle flash → vérification, panneaux SocketSpy tout neufs).
- **Incoming** (à venir) — sur la feuille de route (interconnexion complète, plus d'ECU, EDC16CP33 complet).

### La vue 3D utilise-t-elle OpenGL ?
Aujourd'hui l'AppImage utilise un rendu pseudo-3D **QPainter** avec un overlay fantôme (original vs modifié). Le rendu OpenGL natif `Q3DSurface` est **Incoming**. Voir **[Sub-Programs](Sub-Programs)**.

### Quelles plateformes sont supportées ?
**Linux x86_64** est la cible principale et complète (libusb, SocketCAN). **Windows** est supporté en cross-compile (MinGW ; ftd2xx pour MPPS). **macOS** n'est pas testé (pas de SocketCAN). Linux requiert un noyau ≥ 5.4 avec SocketCAN.

### Quelle licence ?
**GPL-3.0** pour la suite (ECU Studio et SocketSpy). Les **recettes** OpenDAMOS sont recommandées en **CC0-1.0** (domaine public).

### Le tuning d'ECU est-il légal ?
Modifier la calibration moteur peut affecter la conformité aux émissions, la garantie, l'assurance et l'homologation routière, et les règles varient selon les pays. Ce logiciel est un outil ; l'utiliser sur un véhicule est sous votre responsabilité. Vérifiez la réglementation locale et ne flashez que du matériel que vous possédez ou êtes autorisé à modifier.

### Comment publier ce wiki sur GitHub ?
Le wiki GitHub est un dépôt séparé (`ecu_studio_suite.wiki.git`). Clonez-le, copiez ces fichiers `.md`, committez, poussez. Les instructions complètes sont dans le commentaire en haut de **[Home](Home)** (`Home.md`).

### Mon MPPS V21 n'est pas détecté — que faire ?
Vérifiez que vous avez installé la règle udev (`libs/60-mpps.rules`), rechargé udev, puis branché le périphérique et cliqué **Refresh** dans le panneau MPPS. Aucun pilote FTDI nécessaire sous Linux. Détails build/run dans **[Getting Started](Getting-Started)**.
