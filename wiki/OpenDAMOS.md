# OpenDAMOS

[English](#english) · [Français](#français) · back to **[Home](Home)**

> In-repo spec (normative): [`docs/opendamos/OPENDAMOS_STANDARD.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/OPENDAMOS_STANDARD.md) ·
> conversion brief: [`docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md) ·
> JSON schema: [`ressources/open_damos.schema.json`](https://github.com/Poisson48/ecu_studio_suite/blob/main/ressources/open_damos.schema.json)

---

<a id="english"></a>

## English

### What it is

A manufacturer **DAMOS** (a Bosch `.a2l` / ASAP2 calibration license) is generated for **one** exact firmware version. Its addresses are **absolute**:

```
AccPed_trqEngHiGear_MAP = 0x1C1448
```

When the manufacturer ships a firmware update, it reshuffles the memory layout: every address moves and the DAMOS becomes obsolete. You then have to buy or reverse-engineer the DAMOS for the new firmware all over again.

**OpenDAMOS** removes that constraint. Instead of hardcoding an address, each map is described by the **fingerprint of its axes** — the raw values of the RPM axis, the pedal axis, the pressure axis, and so on. Those axes do not change (or barely change) between firmware versions of the same family. A scan of the ROM therefore finds the map **wherever** the manufacturer moved it. One OpenDAMOS recipe applies to the **whole** ECU family, with no per-firmware DAMOS.

OpenDAMOS recipes are **free, CC0-1.0** JSON files. The original address (`defaultAddress`) is kept, but only as a **starting point and tie-breaker**, never as the source of truth.

### Fingerprint relocation, in brief

For every **MAP** / **CURVE**:

1. **Scan** the ROM in 2-byte steps.
2. At each offset, the inline dimension header `(nx[, ny])` must match the recipe's `dims`, read in the axes' endianness (`UWORD_BE` for big-endian Bosch EDC16, `UWORD_LE` for little-endian EDC17 TriCore).
3. If it matches, read the axis points and compare them to the recipe's `fingerprint` with a **two-pass matcher**:
   - **Strict** — each point within `max(absTol 100, relTol 5%)` of expected; **≥ 85 %** matches ⇒ strict match.
   - **Bag-of-values** (fallback) — how many fingerprint values appear *somewhere* in the axis; **≥ 70 %** plus coherent min/max (±15 %) ⇒ bag match (lower score).
4. **Score** = (axisX + axisY) / 2, with a +0.1 bonus if both axes are strict.
5. **Disambiguate** greedily in characteristic order; each map takes the free candidate closest to its `defaultAddress`.

Every result carries an `addressSource` ∈ {`fingerprint`, `anchor`, `default-fallback`} and a score 0…1, so you can see *how* a map was found before you trust it.

**Scalars (`VALUE`)** have no axis to fingerprint, so they are **anchored**: the address delta found for a neighbouring map is applied to the scalar, then sanity-checked against a plausible `valueRange`. Anchoring is less reliable than axis fingerprinting — anchored values (EGR off, pop & bang…) **should be checked manually before flashing**.

### COM_AXIS mode (separate axes)

Some ECUs (PSA Valeo, Continental SID, some EDC17) do **not** store an inline `(nx, ny)` header in front of the map. Their axes live in independent `AXIS_PTS` blocks (COM_AXIS), often shared between maps, and the data block is a bare `nx·ny` grid with no signature. A COM_AXIS characteristic carries `comAxis: true` and each axis carries its own `address` + `fingerprint`. Relocation scans each axis fingerprint independently, finds the **(offsetX, offsetY) pair that share the same address delta** (consensus delta), and anchors the headerless data block to that same delta.

### Supported ECUs and maturity

| ECU family | Mode | Maturity | Notes |
|------------|------|----------|-------|
| **Bosch EDC16** | Big-endian, inline header | **Proven** | Verified across firmware variants. |
| **Bosch EDC17** | Little-endian, inline header (TriCore) | **Proven** | Verified across firmware variants. |
| **COM_AXIS ECUs** (PSA Valeo, Continental SID807) | Separate axes (COM_AXIS) | **Beta** | Works 11/12 on Valeo V46 (score 1.0); no cross-firmware test yet. |
| **EDC16CP33 (complete)** | Big-endian, inline header | **Incoming** | On the roadmap. |

See the [maturity legend](Home#english) on the Home page: **Proven** = verified/shipped & stable · **Beta** = works but unproven · **Incoming** = roadmap.

**Limits.** A map with no inline dimension header is not detectable by fingerprint (use anchoring). Very divergent firmware (axes redesigned) needs an extra fingerprint variant or a custom DAMOS. A COM_AXIS map whose axes are too short/generic **and** whose data is flat falls back to `default-fallback`.

### The converter — turn a manufacturer DAMOS into an open recipe

`scripts/damos_a2l_to_opendamos.py` takes a manufacturer DAMOS (an A2L) **plus the matching ROM** and emits an open `open_damos.json` recipe: it reads the absolute addresses from the A2L, reads the actual axis values from the ROM at those addresses, and writes them out as fingerprints. From then on the recipe is firmware-independent.

```bash
python scripts/damos_a2l_to_opendamos.py \
    --a2l   path/to/manufacturer.a2l \
    --rom   path/to/reference.bin \
    --ecu   edc16c34 \
    --out   ressources/edc16c34/open_damos.json
```

The output validates against [`ressources/open_damos.schema.json`](https://github.com/Poisson48/ecu_studio_suite/blob/main/ressources/open_damos.schema.json). The conversion conventions are documented in [`CONVENTION_DAMOS_VERS_OPENDAMOS.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md).

### Recipe shape (essentials)

```json
{
  "$schema": "../open_damos.schema.json",
  "ecu": "edc16c34",
  "name": "open_damos — EDC16C34 Bosch PSA",
  "version": "1.3.0",
  "license": "CC0-1.0",
  "recordLayouts": { "Kf_Xs16_Ys16_Ws16": { "type": "MAP", "headerBytes": 4, "...": "..." } },
  "characteristics": [
    {
      "name": "AccPed_trqEngHiGear_MAP",
      "type": "MAP",
      "defaultAddress": "0x1C1448",
      "dims": { "nx": 16, "ny": 10 },
      "axes": [
        { "unit": "rpm", "dataType": "SWORD_BE", "factor": 1,
          "fingerprint": [400, 650, 750, 1000, 1250, 1500, 1750, 2000, 2250, 2500, 2750, 3000, 3500, 4000, 5000, 5300] },
        { "unit": "%", "dataType": "SWORD_BE", "factor": 0.01220703125,
          "fingerprint": [90, 1229, 2212, 2849, 3195, 3932, 4751, 5734, 6554, 8192] }
      ],
      "data": { "dataType": "SWORD_BE", "factor": 0.1, "unit": "Nm" },
      "stage1": { "defaultPct": 15, "safePct": 10, "sportPct": 18 }
    }
  ],
  "autoMods": [ /* one-click pattern / address patches */ ]
}
```

`characteristics` is the only **required** field. Consumers silently ignore unknown fields (forward-compatible). Three characteristic types: **MAP** (2 axes), **CURVE** (1 axis), **VALUE** (scalar). Full field-by-field reference: [`OPENDAMOS_STANDARD.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/OPENDAMOS_STANDARD.md).

### Where it lives in the code

| Piece | C++ (`ecu_studio_suite`) | JS (`open_car_reprog`) |
|-------|--------------------------|------------------------|
| Model / parsing | `libs/ecu-core/include/ecu/OpenDamos.hpp`, `src/OpenDamos.cpp` | `src/open-damos.js` |
| Relocation | `OpenDamos::relocate`, `findComAxis` | `src/open-damos.js` |
| A2L export | `libs/ecu-core/src/OpenDamosA2lExport.cpp` | `src/open-damos-a2l-export.js` |
| Recipes / AutoMods | `libs/ecu-core/src/OpenDamosRecipes.cpp` | `src/open-damos-recipes.js` |
| UI editor | `apps/ecu-studio/src/panels/damos_editor_panel.cpp` | — |
| Example recipe | `ressources/edc16c34/open_damos.json` (v1.3.0) | idem |

You edit OpenDAMOS recipes directly in ECU Studio's **DAMOS editor** panel — see **[Sub-Programs](Sub-Programs)**.

### See also
- **[Architecture](Architecture)** — OpenDAMOS is step 1 of the flash → verify loop.
- **[Sub-Programs](Sub-Programs)** — the DAMOS editor and AutoMods panels.
- **[Getting Started](Getting-Started)** — apply a recipe on your first run.

---

<a id="français"></a>

## Français

### Ce que c'est

Un **DAMOS** constructeur (une licence de calibration Bosch `.a2l` / ASAP2) est généré pour **une** version firmware précise. Ses adresses sont **absolues** :

```
AccPed_trqEngHiGear_MAP = 0x1C1448
```

Quand le constructeur publie une mise à jour firmware, il réorganise la table mémoire : toutes les adresses bougent et le DAMOS devient obsolète. Il faut alors racheter ou reverser le DAMOS du nouveau firmware.

**OpenDAMOS** lève cette contrainte. Au lieu de hardcoder l'adresse, chaque carte est décrite par l'**empreinte de ses axes** — les valeurs brutes de l'axe RPM, de l'axe pédale, de l'axe pression, etc. Ces axes ne changent pas (ou très peu) d'un firmware à l'autre de la même famille. Un scan de la ROM retrouve donc la carte **quel que soit l'offset** où le constructeur l'a déplacée. Une recette OpenDAMOS s'applique à **toute** la famille d'ECU, sans DAMOS dédié.

Les recettes OpenDAMOS sont des fichiers JSON **libres, CC0-1.0**. L'adresse d'origine (`defaultAddress`) reste présente, mais comme **point de départ et de désambiguïsation**, jamais comme vérité.

### Relocalisation par empreinte, en bref

Pour chaque **MAP** / **CURVE** :

1. **Scan** de la ROM par pas de 2 octets.
2. À chaque offset, l'en-tête inline de dimensions `(nx[, ny])` doit matcher le `dims` de la recette, lu dans l'endianness des axes (`UWORD_BE` pour Bosch EDC16 big-endian, `UWORD_LE` pour EDC17 TriCore little-endian).
3. Si ça matche, lecture des points d'axe et comparaison au `fingerprint` via un matcher **à deux passes** :
   - **Strict** — chaque point à `max(absTol 100, relTol 5 %)` de l'attendu ; **≥ 85 %** de matchs ⇒ match strict.
   - **Bag-of-values** (repli) — combien de valeurs du fingerprint existent *quelque part* dans l'axe ; **≥ 70 %** + min/max cohérents (±15 %) ⇒ match bag (score plus bas).
4. **Score** = (axeX + axeY) / 2, bonus +0,1 si les deux axes sont strict.
5. **Désambiguïsation** greedy dans l'ordre des caractéristiques ; chaque carte prend le candidat libre le plus proche de son `defaultAddress`.

Chaque résultat porte un `addressSource` ∈ {`fingerprint`, `anchor`, `default-fallback`} et un score 0…1 : vous voyez *comment* une carte a été trouvée avant de lui faire confiance.

**Scalaires (`VALUE`)** : pas d'axe à fingerprinter, ils sont donc **ancrés** : le delta d'adresse trouvé pour une carte voisine est appliqué au scalaire, puis vérifié contre une plage plausible `valueRange`. L'ancrage est moins fiable que le fingerprinting d'axes — les valeurs ancrées (EGR off, pop & bang…) **doivent être vérifiées manuellement avant flash**.

### Mode COM_AXIS (axes séparés)

Certains ECU (PSA Valeo, Continental SID, certains EDC17) ne stockent **pas** d'en-tête `(nx, ny)` inline devant la carte. Leurs axes vivent dans des blocs `AXIS_PTS` indépendants (COM_AXIS), souvent partagés entre cartes, et le bloc de données est une simple grille `nx·ny` sans signature. Une caractéristique COM_AXIS porte `comAxis: true` et chaque axe porte son propre `address` + `fingerprint`. La relocalisation scanne chaque empreinte d'axe indépendamment, trouve le **couple (offsetX, offsetY) qui partage le même delta d'adresse** (consensus de delta), et ancre le bloc de données headerless à ce même delta.

### ECU supportés et maturité

| Famille d'ECU | Mode | Maturité | Notes |
|---------------|------|----------|-------|
| **Bosch EDC16** | Big-endian, en-tête inline | **Proven** | Vérifié sur plusieurs variantes firmware. |
| **Bosch EDC17** | Little-endian, en-tête inline (TriCore) | **Proven** | Vérifié sur plusieurs variantes firmware. |
| **ECU COM_AXIS** (PSA Valeo, Continental SID807) | Axes séparés (COM_AXIS) | **Beta** | 11/12 sur Valeo V46 (score 1.0) ; pas encore de test cross-firmware. |
| **EDC16CP33 (complet)** | Big-endian, en-tête inline | **Incoming** | Sur la feuille de route. |

Voir la [légende de maturité](Home#français) sur la page d'accueil : **Proven** (éprouvé) = vérifié/livré et stable · **Beta** = fonctionne mais non éprouvé · **Incoming** (à venir) = feuille de route.

**Limites.** Une carte sans en-tête de dimensions inline n'est pas détectable par fingerprint (utiliser l'ancrage). Un firmware très divergent (axes redessinés) demande une variante de fingerprint ou un DAMOS custom. Une carte COM_AXIS dont les axes sont trop courts/génériques **et** dont les données sont plates retombe sur `default-fallback`.

### Le convertisseur — transformer un DAMOS constructeur en recette libre

`scripts/damos_a2l_to_opendamos.py` prend un DAMOS constructeur (un A2L) **plus la ROM correspondante** et produit une recette libre `open_damos.json` : il lit les adresses absolues dans l'A2L, lit les vraies valeurs d'axes dans la ROM à ces adresses, et les écrit comme fingerprints. Dès lors la recette devient indépendante du firmware.

```bash
python scripts/damos_a2l_to_opendamos.py \
    --a2l   chemin/vers/constructeur.a2l \
    --rom   chemin/vers/reference.bin \
    --ecu   edc16c34 \
    --out   ressources/edc16c34/open_damos.json
```

La sortie valide contre [`ressources/open_damos.schema.json`](https://github.com/Poisson48/ecu_studio_suite/blob/main/ressources/open_damos.schema.json). Les conventions de conversion sont dans [`CONVENTION_DAMOS_VERS_OPENDAMOS.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md).

### Forme d'une recette (l'essentiel)

```json
{
  "$schema": "../open_damos.schema.json",
  "ecu": "edc16c34",
  "name": "open_damos — EDC16C34 Bosch PSA",
  "version": "1.3.0",
  "license": "CC0-1.0",
  "recordLayouts": { "Kf_Xs16_Ys16_Ws16": { "type": "MAP", "headerBytes": 4, "...": "..." } },
  "characteristics": [
    {
      "name": "AccPed_trqEngHiGear_MAP",
      "type": "MAP",
      "defaultAddress": "0x1C1448",
      "dims": { "nx": 16, "ny": 10 },
      "axes": [
        { "unit": "rpm", "dataType": "SWORD_BE", "factor": 1,
          "fingerprint": [400, 650, 750, 1000, 1250, 1500, 1750, 2000, 2250, 2500, 2750, 3000, 3500, 4000, 5000, 5300] },
        { "unit": "%", "dataType": "SWORD_BE", "factor": 0.01220703125,
          "fingerprint": [90, 1229, 2212, 2849, 3195, 3932, 4751, 5734, 6554, 8192] }
      ],
      "data": { "dataType": "SWORD_BE", "factor": 0.1, "unit": "Nm" },
      "stage1": { "defaultPct": 15, "safePct": 10, "sportPct": 18 }
    }
  ],
  "autoMods": [ /* patches pattern / address en un clic */ ]
}
```

`characteristics` est le seul champ **obligatoire**. Les consommateurs ignorent silencieusement les champs inconnus (rétro-compatible). Trois types de caractéristiques : **MAP** (2 axes), **CURVE** (1 axe), **VALUE** (scalaire). Référence champ par champ : [`OPENDAMOS_STANDARD.md`](https://github.com/Poisson48/ecu_studio_suite/blob/main/docs/opendamos/OPENDAMOS_STANDARD.md).

### Où ça vit dans le code

| Élément | C++ (`ecu_studio_suite`) | JS (`open_car_reprog`) |
|---------|--------------------------|------------------------|
| Modèle / parsing | `libs/ecu-core/include/ecu/OpenDamos.hpp`, `src/OpenDamos.cpp` | `src/open-damos.js` |
| Relocalisation | `OpenDamos::relocate`, `findComAxis` | `src/open-damos.js` |
| Export A2L | `libs/ecu-core/src/OpenDamosA2lExport.cpp` | `src/open-damos-a2l-export.js` |
| Recettes / AutoMods | `libs/ecu-core/src/OpenDamosRecipes.cpp` | `src/open-damos-recipes.js` |
| Éditeur UI | `apps/ecu-studio/src/panels/damos_editor_panel.cpp` | — |
| Recette d'exemple | `ressources/edc16c34/open_damos.json` (v1.3.0) | idem |

On édite les recettes OpenDAMOS directement dans le panneau **éditeur DAMOS** d'ECU Studio — voir **[Sub-Programs](Sub-Programs)**.

### Voir aussi
- **[Architecture](Architecture)** — OpenDAMOS est l'étape 1 de la boucle flash → vérification.
- **[Sub-Programs](Sub-Programs)** — les panneaux éditeur DAMOS et AutoMods.
- **[Getting Started](Getting-Started)** — appliquer une recette dès le premier lancement.
