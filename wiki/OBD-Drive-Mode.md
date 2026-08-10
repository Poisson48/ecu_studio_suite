# OBD Drive Mode

[English](#english) · [Français](#français) · back to **[Home](Home)**

---

<a id="english"></a>

## English

**OBD drive mode** (ECU Studio **v1.6.6+**, status **Beta**) is a one-button workflow to validate a flashed tune **while driving**, using a cheap ELM327 adapter. It compares live OBD PIDs to the values **expected from your OpenDAMOS maps** (boost / smoke) — all **100% local**, no cloud.

### What you need
- ECU Studio AppImage **≥ v1.6.6**
- An **ELM327** (USB or Bluetooth/serial) that speaks AT commands
- A **ROM loaded** in ECU Studio with an OpenDAMOS definition that includes the maps you care about (e.g. boost / smoke limiters)
- A vehicle with working OBD-II (Mode 01 PIDs)

### Quick start (drive session)
1. Open your project / ROM in ECU Studio.
2. Open the **OBD** panel.
3. Select the serial port (or Bluetooth RFCOMM device) for the ELM327.
4. Optionally enable **auto CSV** logging.
5. Press **▶ Start drive session** (French UI: **▶ Lancer session conduite**).

That single action:
- connects the adapter
- starts live PID polling
- runs **measured vs expected** validation against OpenDAMOS
- shows a large **green / red turbo banner** (OK vs underboost / mismatch)
- can write an enhanced CSV (measured, expected, delta) for later review

While driving, the UI stays minimal so you can glance at the banner without digging through tabs.

### What is compared
| Live OBD | Expected source | Notes |
|----------|-----------------|-------|
| MAP / boost-related PIDs | OpenDAMOS boost / pressure maps | Expected boost ≈ MAP (kPa abs) × 10 → mbar via `MapSampler` / `TuneValidation` (PID 0x0B is already absolute — do not add baro) |
| Related limiters (e.g. smoke) | Matching OpenDAMOS characteristics | Relocated by axis fingerprint, then bilinear-sampled at the live operating point |

Exact PIDs and map names depend on the ECU recipe; the service relocates OpenDAMOS maps into the loaded ROM, then samples them at the current RPM / load from OBD.

### Other OBD tools in the same panel
- **Live** tab — classic PID dashboard
- **Validation** tab — detailed measured / expected / delta table (when not in full drive UI)
- **Diagnostic** — DTC Mode 03 + 07, freeze frame
- **CSV replay** — reload a previous drive log
- Optional **CAN continuous validation** via SocketSpy MCP when SocketSpy is running

### Safety
- Watch the road first — the banner is a glanceable aid, not a racing dash.
- Tuning and emissions: your responsibility; see **[FAQ](FAQ)**.
- Prefer a passenger / co-driver for first sessions while you learn the UI.

### Privacy
Logs and ROMs stay on disk. There is **no upload**. The only optional network use is the AppImage auto-update from GitHub (user-triggered).

### See also
- **[Getting Started](Getting-Started)** — install and first hardware
- **[Architecture](Architecture)** — how validation sits in the hub
- **[OpenDAMOS](OpenDAMOS)** — recipes that feed expected values
- **[Sub-Programs](Sub-Programs)** — full feature list
- **[FAQ](FAQ)** — ELM327 tips and legality

---

<a id="français"></a>

## Français

Le **mode conduite OBD** (ECU Studio **v1.6.6+**, statut **Beta**) est un flux en un bouton pour valider une carto flashée **en roulant**, avec un adaptateur ELM327 bon marché. Il compare les PID OBD live aux valeurs **attendues depuis vos cartes OpenDAMOS** (boost / fumée) — le tout **100 % local**, sans cloud.

### Ce qu’il faut
- AppImage ECU Studio **≥ v1.6.6**
- Un **ELM327** (USB ou Bluetooth/série) qui parle commandes AT
- Une **ROM chargée** avec une définition OpenDAMOS incluant les cartes concernées (ex. boost / limiteurs de fumée)
- Un véhicule avec OBD-II fonctionnel (PID Mode 01)

### Démarrage rapide (session conduite)
1. Ouvrez votre projet / ROM dans ECU Studio.
2. Ouvrez le panneau **OBD**.
3. Sélectionnez le port série (ou RFCOMM Bluetooth) de l’ELM327.
4. Activez éventuellement le **CSV auto**.
5. Appuyez sur **▶ Lancer session conduite**.

Cette seule action :
- connecte l’adaptateur
- lance le polling des PID
- lance la validation **mesuré vs attendu** OpenDAMOS
- affiche un grand **bandeau turbo vert / rouge** (OK vs underboost / écart)
- peut écrire un CSV enrichi (mesuré, attendu, delta) pour relecture

En conduite, l’UI reste minimale pour lire le bandeau d’un coup d’œil.

### Ce qui est comparé
| OBD live | Source attendue | Notes |
|----------|-----------------|-------|
| PID liés MAP / boost | Cartes pression / boost OpenDAMOS | Échantillonnage via `MapSampler` / `TuneValidation` |
| Limiteurs associés (ex. fumée) | Caractéristiques OpenDAMOS correspondantes | Relocalisation par empreinte d’axes, puis échantillonnage bilinéaire au point de fonctionnement live |

### Autres outils du même panneau
- Onglet **Live** — tableau de bord PID classique
- Onglet **Validation** — tableau détaillé mesuré / attendu / delta
- **Diagnostic** — DTC Mode 03 + 07, freeze frame
- **Replay CSV** — recharger un log de conduite
- Validation CAN continue optionnelle via MCP SocketSpy

### Sécurité
- La route d’abord — le bandeau est un aide-mémoire, pas un compteur de course.
- Tuning et émissions : votre responsabilité ; voir **[FAQ](FAQ)**.
- Préférez un copilote pour les premières sessions.

### Vie privée
Logs et ROM restent sur le disque. **Aucun envoi**. Le seul réseau optionnel est la mise à jour AppImage depuis GitHub (déclenchée par l’utilisateur).

### Voir aussi
- **[Getting Started](Getting-Started)** — installation et premier matériel
- **[Architecture](Architecture)** — place de la validation dans le hub
- **[OpenDAMOS](OpenDAMOS)** — recettes qui alimentent les valeurs attendues
- **[Sub-Programs](Sub-Programs)** — liste complète des fonctions
- **[FAQ](FAQ)** — ELM327 et légalité
