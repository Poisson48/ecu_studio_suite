# Standard OpenDAMOS — v1

> DAMOS libre, relocalisable, dérivé d'un DAMOS Bosch / A2L.
> Format de référence implémenté par `ecu-studio-suite` (`libs/ecu-core`, classe `ecu::OpenDamos`) et par `open-car-reprog` (`src/open-damos.js`).
> Schéma machine : [`ressources/open_damos.schema.json`](../../ressources/open_damos.schema.json).
> Licence des recettes recommandée : **CC0-1.0**.

Ce document est **normatif**. Il décrit ce qu'est un fichier OpenDAMOS, champ par champ, et fait foi pour tout outil (ou agent) qui produit ou consomme une recette. Le brief de conversion DAMOS → OpenDAMOS est dans [`CONVENTION_DAMOS_VERS_OPENDAMOS.md`](CONVENTION_DAMOS_VERS_OPENDAMOS.md).

Les mots **DOIT**, **NE DOIT PAS**, **DEVRAIT**, **PEUT** ont le sens habituel (RFC 2119).

---

## 1. Motivation

Un DAMOS Bosch (un `.a2l` ASAP2) est une licence de calibration générée pour **une** version firmware précise. Les adresses y sont **absolues** :

```
AccPed_trqEngHiGear_MAP = 0x1C1448
```

Quand le constructeur publie un firmware update, il réorganise la table mémoire : toutes les adresses bougent et le DAMOS devient obsolète. Il faut alors racheter / reverser le DAMOS du nouveau firmware.

**OpenDAMOS** lève cette contrainte. Au lieu de hardcoder l'adresse, chaque carte est décrite par l'**empreinte de ses axes** (les valeurs de l'axe RPM, pédale, pression…). Ces axes ne changent pas — ou très peu — d'un firmware à l'autre de la même famille. Un scan de la ROM retrouve donc la carte **quel que soit l'offset** où le constructeur l'a déplacée. Une recette OpenDAMOS s'applique ainsi à **toute** la famille d'ECU, sans DAMOS dédié.

L'adresse d'origine (`defaultAddress`) reste présente, mais comme **point de départ et de désambiguïsation**, pas comme vérité.

---

## 2. Vue d'ensemble

Une recette OpenDAMOS est **un fichier JSON UTF-8**, conventionnellement nommé `open_damos.json`, vivant dans `ressources/<ecu>/open_damos.json`.

```json
{
  "$schema": "../open_damos.schema.json",
  "ecu": "edc16c34",
  "name": "open_damos — EDC16C34 Bosch PSA",
  "version": "1.3.0",
  "license": "CC0-1.0",
  "baseline": { "source": "...", "damos": "...", "note": "..." },
  "recordLayouts": { "Kf_Xs16_Ys16_Ws16": { "type": "MAP", "headerBytes": 4, ... } },
  "characteristics": [ /* MAP / CURVE / VALUE */ ],
  "autoMods": [ /* patches optionnels */ ]
}
```

Hiérarchie :

```
recipe
├── métadonnées (ecu, name, version, license, homepage, description)
├── baseline ............ firmware de référence (documentaire)
├── recordLayouts ....... catalogue de layouts mémoire réutilisables
├── characteristics[] ... le cœur — une entrée par carte (MAP/CURVE/VALUE)
│   ├── identité ........ name, type, category, description
│   ├── localisation .... recordLayout, defaultAddress, dims
│   ├── axes[] .......... unit, dataType, factor, offset, fingerprint  ← l'empreinte
│   ├── data ............ dataType, factor, offset, unit              ← conversion cellules
│   ├── relocation ...... (VALUE seulement) ancrage
│   └── tuning .......... stage1, egrOff, stockRawValue, stockPhysValue
└── autoMods[] .......... patches pattern/address (optionnel)
```

---

## 3. En-tête de recette (niveau racine)

| Champ            | Type             | Oblig. | Description |
|------------------|------------------|:------:|-------------|
| `ecu`            | string           |   —    | Identifiant de la famille d'ECU, **minuscules** (ex. `edc16c34`). Alias historique accepté : `ecuId`. **DEVRAIT** être présent. |
| `characteristics`| array            | **oui**| Liste des caractéristiques. Seul champ **obligatoire**. |
| `name`           | string           |   —    | Nom lisible. |
| `version`        | string (semver)  |   —    | Version de la recette `x.y.z`. **DEVRAIT** être présent. |
| `license`        | string           |   —    | **DEVRAIT** être `CC0-1.0`. |
| `homepage`       | string           |   —    | URL doc/projet. |
| `description`    | string           |   —    | Description et compatibilité véhicules. |
| `baseline`       | object           |   —    | Firmware de référence (voir §4). |
| `recordLayouts`  | object           |   —    | Catalogue de layouts (voir §5). |
| `autoMods`       | array            |   —    | Patches (voir §10). |
| `$schema`        | string           |   —    | Pointeur vers le schéma. **DEVRAIT** valoir `"../open_damos.schema.json"`. |

> **Compatibilité ascendante** : un consommateur **DOIT** ignorer silencieusement les champs qu'il ne connaît pas (`additionalProperties: true` partout). La seule erreur fatale au chargement est l'absence du tableau `characteristics`.

---

## 4. `baseline` (documentaire)

Décrit d'où sortent les adresses et les empreintes. N'est **pas** utilisé pour la relocalisation — purement traçabilité.

| Champ    | Type   | Description |
|----------|--------|-------------|
| `source` | string | Chemin de la ROM de référence (ex. `ressources/edc16c34/ori.BIN`). |
| `damos`  | string | Chemin du DAMOS / A2L source dont la recette est dérivée. |
| `note`   | string | Notes libres. |

---

## 5. `recordLayouts` (catalogue de layouts mémoire)

Objet indexé par **nom de layout**. Chaque caractéristique référence un layout via `recordLayout`. Décrit comment les octets bruts sont agencés en ROM.

```json
"recordLayouts": {
  "Kf_Xs16_Ys16_Ws16": { "type": "MAP",   "headerBytes": 4, "headerType": "UWORD_BE", "axisDataType": "SWORD_BE", "dataType": "SWORD_BE" },
  "Kl_Xs16_Ws16":       { "type": "CURVE", "headerBytes": 2, "headerType": "UWORD_BE", "axisDataType": "SWORD_BE", "dataType": "SWORD_BE" },
  "Kw_Ws16":            { "type": "VALUE", "dataType": "SWORD_BE" }
}
```

| Champ          | Type    | Description |
|----------------|---------|-------------|
| `type`         | enum    | `MAP` \| `CURVE` \| `VALUE`. |
| `headerBytes`  | integer | Taille de l'en-tête **inline** portant les dimensions (`nx`[, `ny`]). 2 octets par dimension (UWORD_BE) → 2 pour CURVE, 4 pour MAP, 0 pour VALUE. |
| `headerType`   | enum    | Type des champs de dimension (typiquement `UWORD_BE`). |
| `axisDataType` | enum    | Type par défaut des points d'axe. |
| `dataType`     | enum    | Type par défaut des cellules. |

> La convention de nommage `Kf_/Kl_/Kw_` + `Xs16/Ys16/Ws16` est celle de Bosch (Kennfeld/Kennlinie/Kennwert ; `s16` = signed 16 bits). **PEUT** être réutilisée mais n'est pas imposée.
>
> L'en-tête inline `(nx, ny)` en `UWORD_BE` juste **avant** les axes est ce qui rend la détection par fingerprint robuste : le scanner valide d'abord les dimensions, puis l'empreinte. Une carte **sans** en-tête de dimensions inline n'est pas relocalisable par cette méthode (voir §11, limites).

---

## 6. `characteristics[]` — une caractéristique

C'est le cœur du format. Trois `type` :

- **`MAP`** — table 2D, **2 axes** (X puis Y), `dims.nx`×`dims.ny` cellules.
- **`CURVE`** — courbe 1D, **1 axe** (X), `dims.nx` cellules.
- **`VALUE`** — scalaire, **0 axe**, 1 cellule.

| Champ            | Type             | Oblig. | S'applique à | Description |
|------------------|------------------|:------:|--------------|-------------|
| `name`           | string           | **oui**| tous         | Identifiant **unique** dans la recette. Repris tel quel du DAMOS (ex. `AccPed_trqEngHiGear_MAP`). |
| `type`           | enum             | **oui**| tous         | `MAP` \| `CURVE` \| `VALUE`. |
| `category`       | string           |   —    | tous         | Classement libre : `stage1`, `safety`, `smoke`, `timing`, `air`, `fuel`, `driver`, `egr`, `popbang`, `info`… |
| `description`    | string           |   —    | tous         | Description humaine. |
| `recordLayout`   | string           |   —    | tous         | Nom d'un layout déclaré dans `recordLayouts`. |
| `defaultAddress` | hex string / int |   —    | tous         | Adresse dans le firmware **baseline**. Point de départ + désambiguïsation, **pas** une vérité. Accepte `"0x1C1448"` ou un entier décimal. |
| `dims`           | object           |   —    | MAP, CURVE   | `{ "nx": …, "ny": … }`. `nx` pour CURVE ; `nx`+`ny` pour MAP ; absent/0 pour VALUE. |
| `axes`           | array            |   —    | MAP, CURVE   | 2 axes (MAP), 1 axe (CURVE), 0 (VALUE). Voir §7. |
| `data`           | object           |   —    | tous         | Conversion des cellules. Voir §8. |
| `relocation`     | object           |   —    | VALUE        | Ancrage. Voir §9. |
| `stockRawValue`  | integer          |   —    | VALUE        | Valeur brute d'origine (vérification). |
| `stockPhysValue` | number           |   —    | VALUE        | Valeur physique d'origine (vérification). |
| `stage1`         | bool / object    |   —    | tous         | Recommandations Stage 1. Voir §10. |
| `egrOff`         | bool / object    |   —    | tous         | Lien EGR OFF. Voir §10. |

**Règles de cohérence (DOIT) :**

- `MAP` ⇒ exactement **2** entrées dans `axes` et `dims.ny ≥ 1`.
- `CURVE` ⇒ exactement **1** entrée dans `axes` et `dims.nx ≥ 1`, pas de `ny`.
- `VALUE` ⇒ `axes` vide ou absent, pas de `dims`. **DEVRAIT** porter une `relocation` (sinon retombe sur `defaultAddress`).
- `len(axes[0].fingerprint)` **DEVRAIT** valoir `dims.nx` ; `len(axes[1].fingerprint)` **DEVRAIT** valoir `dims.ny`.

---

## 7. `axes[]` — un axe et son empreinte

L'axe X est `axes[0]`, l'axe Y `axes[1]`.

```json
{
  "inputQuantity": "Eng_nAvrg",
  "unit": "rpm",
  "dataType": "SWORD_BE",
  "factor": 1,
  "offset": 0,
  "fingerprint": [400, 650, 750, 1000, 1250, 1500, 1750, 2000,
                  2250, 2500, 2750, 3000, 3500, 4000, 5000, 5300]
}
```

| Champ          | Type     | Défaut    | Description |
|----------------|----------|-----------|-------------|
| `fingerprint`  | int[]    | `[]`      | **Le champ essentiel.** Valeurs **brutes** (raw, telles qu'en ROM) des points d'axe, lues dans le firmware baseline. C'est l'empreinte recherchée lors de la relocalisation. |
| `inputQuantity`| string   | `""`      | Grandeur d'entrée (ex. `Eng_nAvrg`, `AccPed_rChkdVal`). Informatif, repris du DAMOS. |
| `unit`         | string   | `""`      | Unité physique (`rpm`, `%`, `kg/h`…). |
| `dataType`     | enum     | `SWORD_BE`| Encodage des points d'axe. |
| `factor`       | number   | `1`       | `phys = raw·factor + offset`. |
| `offset`       | number   | `0`       | |

> **Important** : `fingerprint` contient les valeurs **brutes**, pas physiques. Pour un axe RPM stocké directement en rpm, `factor = 1` et raw = phys. Pour un axe en `%` stocké en `0.01220703125`/LSB, le `fingerprint` reste en LSB (entiers), la conversion se fait via `factor`.

---

## 8. `data` — conversion des cellules

```json
"data": { "dataType": "SWORD_BE", "factor": 0.1, "offset": 0, "unit": "Nm" }
```

| Champ      | Type   | Défaut     | Description |
|------------|--------|------------|-------------|
| `dataType` | enum   | `SWORD_BE` | Encodage des cellules. |
| `factor`   | number | `1`        | `phys = raw·factor + offset`. |
| `offset`   | number | `0`        | |
| `unit`     | string | `""`       | Unité physique des cellules (`Nm`, `mg/cyc`, `hPa`…). |

---

## 9. `relocation` — ancrage des VALUE

Une `VALUE` scalaire n'a pas d'axe à fingerprinter. On la retrouve par **ancrage** sur une MAP voisine déjà relocalisée : le **delta** d'adresse trouvé pour la MAP est appliqué à la VALUE (les deux vivent dans la même région mémoire chez le constructeur).

```json
"relocation": {
  "anchorMap": "AccPed_trqEngLoGear_MAP",
  "method": "anchor-delta",
  "valueRange": [6000, 10000],
  "searchWindow": 4096
}
```

| Champ          | Type     | Description |
|----------------|----------|-------------|
| `anchorMap`    | string   | Nom de la MAP ancre. Son delta est appliqué à cette VALUE. |
| `method`       | string   | `anchor-delta` (delta de l'ancre) ou `value-range-search` (recherche d'une valeur plausible). |
| `valueRange`   | [num,num]| Plage physique plausible `[min, max]` — sanity-check après ancrage / cible de `value-range-search`. |
| `searchWindow` | integer  | Taille (octets) de la fenêtre de recherche autour de l'ancre. |

> L'ancrage est **moins fiable** que le fingerprinting d'axes. Si la valeur relocalisée sort de `valueRange`, le consommateur retombe sur `defaultAddress` avec un avertissement. Les VALUE relocalisées par ancrage (EGR OFF, popbang…) **DEVRAIENT** être vérifiées manuellement avant flash.

---

## 10. Tuning : `stage1`, `egrOff`, `stockRawValue`, `stockPhysValue`

- **`stage1`** — recommandations de gain, en %. La **présence** du champ signale que la carte fait partie d'un Stage 1.
  ```json
  "stage1": { "defaultPct": 15, "safePct": 10, "sportPct": 18 }
  ```
  `defaultPct` = gain conseillé, `safePct` = prudent, `sportPct` = agressif. Peut aussi valoir `true`.

- **`egrOff`** — marque une carte/valeur liée à la désactivation EGR. Booléen, ou objet documentaire `{ "recommendedRawValue": …, "note": "…" }` (un objet **vaut** `true`).

- **`stockRawValue` / `stockPhysValue`** — valeur d'origine d'une VALUE, pour vérifier qu'on a bien relocalisé au bon endroit.

---

## 11. `autoMods[]` — patches optionnels

Patches « en un clic » versionnés avec la recette. Deux saveurs.

**`pattern`** — cherche/remplace une séquence d'octets :
```json
{ "id": "egr-off-pattern", "type": "pattern", "description": "…",
  "search": "AA BB CC DD", "replace": "EE FF 00 11", "restore": "AA BB CC DD" }
```

**`address`** — écrit des octets à une adresse fixe :
```json
{ "id": "vmax-raise", "type": "address", "description": "…",
  "address": "0x1D0AE6", "bytes": "FF FF", "restore": "27 10" }
```

| Champ         | Type      | S'applique à | Description |
|---------------|-----------|--------------|-------------|
| `id`          | string    | tous         | Identifiant du patch. |
| `type`        | enum      | tous         | `pattern` \| `address`. |
| `description` | string    | tous         | |
| `note`        | string    | tous         | |
| `search`      | hex bytes | pattern      | Séquence à trouver. **Obligatoire** si `pattern`. |
| `replace`     | hex bytes | pattern      | Séquence de remplacement. **Obligatoire** si `pattern`. |
| `address`     | hex / int | address      | Où écrire. **Obligatoire** si `address`. |
| `bytes`       | hex bytes | address      | Octets à écrire. **Alias** de `replace`. |
| `restore`     | hex bytes | tous         | Octets d'origine, pour annuler (optionnel). |

> Octets hex : `"AA BB CC"` ou `"AABBCC"`. Tout caractère non hexadécimal est ignoré au parsing.

---

## 12. Types de données (`dataType`)

| Valeur     | Octets | Signé | Endian | C++ (`DamosDataType`) |
|------------|:------:|:-----:|:------:|-----------------------|
| `SBYTE`    | 1      | oui   | —      | `SByte`   |
| `UBYTE`    | 1      | non   | —      | `UByte`   |
| `SWORD_BE` | 2      | oui   | big    | `SWordBE` |
| `UWORD_BE` | 2      | non   | big    | `UWordBE` |
| `SLONG_BE` | 4      | oui   | big    | `SLongBE` |
| `ULONG_BE` | 4      | non   | big    | `ULongBE` |
| `SWORD_LE` | 2      | oui   | little | `SWordLE` |
| `UWORD_LE` | 2      | non   | little | `UWordLE` |
| `SLONG_LE` | 4      | oui   | little | `SLongLE` |
| `ULONG_LE` | 4      | non   | little | `ULongLE` |

Tout type inconnu **DOIT** être traité comme `SWORD_BE` (comportement de `parseDamosDataType`).

**Endianness.** Les ECU Bosch EDC16/EDC17 sont **big-endian** (`*_BE`). Les Continental SID/MEDC17 sont **little-endian** (`*_LE`). Une recette **PEUT** déclarer un `byteOrder` (`"BE"` | `"LE"`, défaut `"BE"`) à la racine et/ou par `recordLayout`, qui fixe l'endianness des types non suffixés ; un `dataType` explicitement suffixé (`SWORD_LE`…) prime toujours.

---

## 13. Algorithme de relocalisation (informatif)

Pour situer le rôle de chaque champ. Implémentation : `OpenDamos::relocate` (C++), `src/open-damos.js` (JS).

Pour chaque **MAP / CURVE** :
1. Scan de la ROM par pas de 2 octets.
2. À chaque offset, l'en-tête `(nx[, ny])` en `UWORD_BE` doit matcher `dims`.
3. Si oui, lecture des axes et comparaison au `fingerprint` via un matcher à **deux passes** :
   - **Strict** : chaque point lu doit être à `max(absTol=100, relTol=5%·attendu)` de l'attendu ; **≥ 85 %** de matchs ⇒ match strict.
   - **Bag-of-values** (fallback) : combien de valeurs du fingerprint existent *quelque part* dans l'axe (à tolérance) ; **≥ 70 %** + min/max cohérents (±15 %) ⇒ match bag (score plus bas).
4. Score = (axeX + axeY)/2, bonus +0.1 si les deux axes sont strict.
5. **Désambiguïsation** : attribution greedy dans l'ordre des caractéristiques ; chacune prend le candidat libre le plus proche de son `defaultAddress`.

Pour chaque **VALUE** : ancrage via `relocation.anchorMap` (delta de l'ancre), sanity-check contre `relocation.valueRange`.

Chaque résultat porte un `addressSource` ∈ {`fingerprint`, `anchor`, `default-fallback`} et un score 0…1.

**Limites :** cartes sans en-tête de dimensions inline → non détectables par fingerprint (utiliser un ancrage) ; firmwares très divergents (axes redessinés) → ajouter une variante de fingerprint ou un DAMOS custom.

**Mode COM_AXIS (axes séparés, ECU little-endian type SID807).** Certains ECU (Continental SID807, MEDC17…) ne stockent **pas** d'en-tête `(nx, ny)` inline devant la carte : les axes vivent dans des blocs `AXIS_PTS` indépendants (COM_AXIS), partagés entre plusieurs cartes, précédés de leur propre compte. Le scan inline ci-dessus ne s'y applique pas. Le **mode COM_AXIS** (distinct) consiste à : (1) scanner les blocs d'axes autonomes en lisant les points au `dataType` LE de la recette, (2) matcher les `fingerprint` sur ces blocs, (3) retrouver les cartes qui les référencent via les adresses d'axes comme ancres (ou via l'A2L source). Les types de données LE (`SWORD_LE`…) et le `byteOrder` de la recette sont en place ; l'orchestration COM_AXIS complète est un mode à part entière (hors v1 du scan inline).

---

## 14. Exemple complet (MAP)

```json
{
  "name": "AccPed_trqEngHiGear_MAP",
  "category": "stage1",
  "description": "Driver's wish — couple moteur demandé par la pédale en rapports hauts (4e/5e).",
  "type": "MAP",
  "recordLayout": "Kf_Xs16_Ys16_Ws16",
  "defaultAddress": "0x1C1448",
  "dims": { "nx": 16, "ny": 10 },
  "axes": [
    {
      "inputQuantity": "Eng_nAvrg", "unit": "rpm",
      "dataType": "SWORD_BE", "factor": 1, "offset": 0,
      "fingerprint": [400, 650, 750, 1000, 1250, 1500, 1750, 2000,
                      2250, 2500, 2750, 3000, 3500, 4000, 5000, 5300]
    },
    {
      "inputQuantity": "AccPed_rChkdVal", "unit": "%",
      "dataType": "SWORD_BE", "factor": 0.01220703125, "offset": 0,
      "fingerprint": [90, 1229, 2212, 2849, 3195, 3932, 4751, 5734, 6554, 8192]
    }
  ],
  "data": { "dataType": "SWORD_BE", "factor": 0.1, "offset": 0, "unit": "Nm" },
  "stage1": { "defaultPct": 15, "safePct": 10, "sportPct": 18 }
}
```

---

## 15. Implémentations de référence

| Élément             | C++ (`ecu-studio-suite`)                          | JS (`open-car-reprog`)            |
|---------------------|---------------------------------------------------|----------------------------------|
| Modèle / parsing    | `libs/ecu-core/include/ecu/OpenDamos.hpp`, `src/OpenDamos.cpp` | `src/open-damos.js`   |
| Sérialisation       | `libs/ecu-core/src/OpenDamosSerialize.cpp`        | —                                |
| Export A2L (ASAP2)  | `libs/ecu-core/src/OpenDamosA2lExport.cpp`        | `src/open-damos-a2l-export.js`   |
| Recettes / AutoMods | `libs/ecu-core/src/OpenDamosRecipes.cpp`          | `src/open-damos-recipes.js`      |
| Éditeur UI          | `apps/ecu-studio/src/panels/damos_editor_panel.cpp` | —                              |
| Recette d'exemple   | `ressources/edc16c34/open_damos.json` (v1.3.0)    | idem                             |

> Note d'implémentation : le parseur C++ actuel lit le sous-ensemble nécessaire à l'édition/relocalisation (il **ignore** `recordLayouts` et `baseline`, qui restent documentaires côté C++ mais sont **normatifs** dans le format et utilisés par l'export A2L JS). Tout producteur **DOIT** néanmoins émettre `recordLayouts` quand des caractéristiques le référencent.

---

## 16. Versionnement du standard

- **Standard** : ce document, v1. Tout ajout rétro-compatible (nouveau champ optionnel) reste v1. Une rupture (champ obligatoire, sémantique changée) ⇒ v2.
- **Recette** : champ `version` (semver) propre à chaque `open_damos.json`, indépendant de la version du standard.
