# Convention DAMOS → OpenDAMOS

> Brief destiné à un **agent dédié** à la conversion d'un DAMOS Bosch (A2L / ASAP2) en recette **OpenDAMOS**.
> Le format cible fait foi : [`OPENDAMOS_STANDARD.md`](OPENDAMOS_STANDARD.md). Schéma de validation : [`../../ressources/open_damos.schema.json`](../../ressources/open_damos.schema.json).

## Mission de l'agent

À partir d'un DAMOS (un `.a2l` ASAP2, généré pour **un** firmware précis) **et** de la ROM de référence correspondante, produire un `open_damos.json` valide vis-à-vis du schéma, dans lequel chaque carte porte l'**empreinte de ses axes** (`fingerprint`) lue dans la ROM. But : que la recette se relocalise toute seule sur les autres firmwares de la même famille, sans le DAMOS d'origine.

**Entrées requises :**
1. `damos.a2l` — le DAMOS source.
2. `ori.BIN` — la ROM **exacte** pour laquelle ce DAMOS a été généré (les adresses A2L doivent y tomber juste).

**Sortie :** `ressources/<ecu>/open_damos.json` conforme au standard.

> Sans la ROM de référence, l'agent **ne peut pas** remplir `fingerprint` (l'empreinte se **lit** dans la ROM à `defaultAddress`). Une recette sans fingerprints est inutilisable pour la relocalisation — l'agent doit le signaler plutôt que d'inventer des valeurs.

## Correspondance des concepts

| DAMOS / A2L (ASAP2)                          | OpenDAMOS                                  |
|----------------------------------------------|--------------------------------------------|
| `CHARACTERISTIC` type `MAP`                  | caractéristique `type: "MAP"` (2 axes)     |
| `CHARACTERISTIC` type `CURVE`                | `type: "CURVE"` (1 axe)                     |
| `CHARACTERISTIC` type `VALUE`                | `type: "VALUE"` (0 axe, + `relocation`)     |
| Nom du `CHARACTERISTIC`                       | `name` (repris **tel quel**)               |
| Adresse (ECU_ADDRESS)                         | `defaultAddress` (`"0x…"`)                  |
| `NUMBER` / dimensions des `AXIS_DESCR`        | `dims.nx`, `dims.ny`                        |
| `AXIS_DESCR` X (puis Y)                        | `axes[0]`, `axes[1]`                        |
| `INPUT_QUANTITY` d'un axe                      | `axes[i].inputQuantity`                     |
| `COMPU_METHOD` (RAT_FUNC linéaire) d'un axe    | `axes[i].factor`, `axes[i].offset`         |
| Unité d'un axe                                | `axes[i].unit`                              |
| Type des points d'axe (`AXIS_PTS` dans `RECORD_LAYOUT`) | `axes[i].dataType`              |
| Valeurs des points d'axe **lues dans la ROM** | `axes[i].fingerprint` (raw)                |
| `COMPU_METHOD` de la valeur (FNC_VALUES)       | `data.factor`, `data.offset`               |
| Unité de la valeur                            | `data.unit`                                 |
| Type des cellules (`FNC_VALUES` dans `RECORD_LAYOUT`) | `data.dataType`                     |
| `RECORD_LAYOUT`                               | entrée de `recordLayouts` + `recordLayout` |
| `FUNCTION` / `GROUP` d'appartenance            | `category` (mappé, voir ci-dessous)        |

### Types de données

| RECORD_LAYOUT (ASAP2) | OpenDAMOS `dataType` |
|-----------------------|----------------------|
| `SBYTE`               | `SBYTE`              |
| `UBYTE`               | `UBYTE`              |
| `SWORD` (big-endian)  | `SWORD_BE`           |
| `UWORD` (big-endian)  | `UWORD_BE`           |
| `SLONG` (big-endian)  | `SLONG_BE`           |
| `ULONG` (big-endian)  | `ULONG_BE`           |

EDC16/EDC17 Bosch = big-endian. Vérifier `BYTE_ORDER`/`MOD_COMMON` du A2L ; si little-endian, le standard v1 ne couvre **que** le big-endian → signaler.

### `COMPU_METHOD` → factor/offset

ASAP2 `RAT_FUNC` donne `f(x) = (a·x² + b·x + c)/(d·x² + e·x + f)`. Pour une conversion linéaire (cas courant), `a=d=e=0`, et la forme `phys = raw·factor + offset` se déduit des coefficients. N'accepter que le **linéaire** ; un `COMPU_METHOD` non linéaire ou `COMPU_VTAB` (table) **DOIT** être signalé (non couvert par factor/offset seuls).

> ⚠ Convention de signe : selon l'écriture du `RAT_FUNC`, `factor` peut être l'inverse du coefficient brut. **Toujours vérifier** : `raw_lu · factor + offset` doit redonner une valeur physique plausible dans l'unité annoncée.

## Procédure (par caractéristique)

1. **Parser** le `CHARACTERISTIC` : name, type, adresse, dims, `AXIS_DESCR`, `RECORD_LAYOUT`, `COMPU_METHOD`.
2. **Renseigner** `name`, `type`, `defaultAddress`, `dims`, et les `axes[].{inputQuantity,unit,dataType,factor,offset}` + `data.{dataType,factor,offset,unit}`.
3. **Lire la ROM** à `defaultAddress` pour extraire l'empreinte :
   - Sauter l'en-tête de dimensions inline (`headerBytes` du layout : 4 pour MAP, 2 pour CURVE, 0 pour VALUE).
   - Lire `nx` points d'axe X (puis `ny` points d'axe Y pour une MAP) au `dataType` de l'axe, en big-endian.
   - Mettre ces entiers **bruts** dans `axes[i].fingerprint`. **Ne pas** appliquer factor/offset au fingerprint.
4. **Vérifier** : `len(fingerprint X) == nx`, `len(fingerprint Y) == ny`, axes monotones/plausibles. Si l'en-tête lu ne matche pas `dims`, l'adresse ou le layout est faux → signaler.
5. **VALUE** : pas de fingerprint. Choisir une `anchorMap` (une MAP de la recette vivant dans la même région mémoire, idéalement la plus proche en adresse), poser `method: "anchor-delta"`, une `valueRange` physique plausible, et lire `stockRawValue`/`stockPhysValue` dans la ROM.
6. **Layout** : ajouter le `recordLayout` au catalogue `recordLayouts` (dédupliqué par nom) et le référencer.
7. **Catégorie** : déduire `category` du nom/fonction (voir mapping ci-dessous).

### Mapping `category` (heuristique)

| Indice dans le nom / la fonction                          | `category` |
|-----------------------------------------------------------|------------|
| Driver's wish couple, conversion couple→injection, rail base | `stage1`   |
| Limiteurs, plafonds (qLim, pMax, trqLim)                  | `safety`   |
| Smoke limiter, lambda fumée                               | `smoke`    |
| Timing injection (phiMI…)                                 | `timing`   |
| Débit/correction air, MAF (AFSCD, dmAir…)                 | `air`      |
| Densité carburant, FlSys                                  | `fuel`     |
| Map inverse driver's wish, propulsion                     | `driver`   |
| EGR (nMin, taux EGR)                                      | `egr`      |
| Overrun / popbang (AirCtl_nOvrRun…)                       | `popbang`  |
| Autre / non tuning                                        | `info`     |

## Métadonnées de recette

Remplir l'en-tête : `ecu` (id famille minuscules), `name`, `version` (`"1.0.0"` au premier jet), `license: "CC0-1.0"`, `description` (compat véhicules), et `baseline { source, damos, note }` pointant la ROM et le DAMOS sources. Mettre `"$schema": "../open_damos.schema.json"`.

## Garde-fous (DOIT respecter)

- **Ne jamais inventer** une `fingerprint` : elle se lit dans la ROM. Pas de ROM → pas de fingerprint → le dire.
- **Valider** la sortie contre `ressources/open_damos.schema.json` avant de rendre.
- **Round-trip de cohérence** : pour chaque carte, `raw_lu · data.factor + data.offset` doit donner une valeur dans l'unité `data.unit` plausible physiquement ; sinon factor/offset sont faux.
- **Signaler** plutôt que deviner : COMPU_METHOD non linéaire, COMPU_VTAB, little-endian, carte sans en-tête de dimensions inline (non relocalisable par fingerprint), adresse qui ne matche pas les dims.
- **Idempotence** : relancer l'agent sur le même couple (DAMOS, ROM) doit produire le même JSON (ordre des caractéristiques = ordre du DAMOS).

## Critère de réussite

1. Le `open_damos.json` valide contre le schéma.
2. Rechargé par `OpenDamos::relocate` sur la **ROM de référence**, **toutes** les MAP/CURVE matchent en mode `fingerprint` avec score élevé et `delta = 0` (puisque c'est le firmware baseline).
3. Sur un **second** firmware de la même famille, la majorité des cartes se relocalisent par fingerprint (delta ≠ 0, score correct). C'est la preuve que la recette est portable.

> Tests de référence côté `open-car-reprog` : `node tests/open-damos.test.js` (relocalisation sur 2 ROMs + test négatif anti-faux-positif).
