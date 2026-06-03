# Tests — OLS Import & OpenDAMOS Conversion

Dossier de test pour l'agent en charge de l'extension OLS et de la conversion DAMOS → OpenDAMOS.

## Fichiers présents

| Fichier | ECU | Fabricant | Endian | Format |
|---|---|---|---|---|
| `RENAULT_2.0_DCI_EDC16CP33.ols` | EDC16CP33 | Bosch | Big-endian | WinOLS project |
| `Citroen_DS4_1.6HDI_SID807.a2l` | SID807 | Continental | **Little-endian** | ASAP2 A2L |
| `Citroen_DS4_1.6HDI_SID807.ULP` | SID807 | Continental | — | ROM Motorola S-Record |
| `../../ressources/edc16c34/9663944680.bin` | EDC16C34 | Bosch | Big-endian | ROM binaire brute |
| `../../ressources/edc16c34/open_damos.json` | EDC16C34 | Bosch | BE | Recette OpenDAMOS v1.3.0 |

## Contexte projet

La recette OpenDAMOS actuelle (`edc16c34`) a été construite manuellement à partir du DAMOS Bosch EDC16C34 (110hp PSA DV6TED4). Deux nouveaux ECUs PSA sont disponibles dans ce dossier de test. L'objectif est de :

1. **Parser les fichiers OLS WinOLS** pour en extraire la ROM binaire et les définitions de cartes.
2. **Générer automatiquement** les `open_damos.json` correspondants via la convention `CONVENTION_DAMOS_VERS_OPENDAMOS.md`.
3. **Étendre le standard** pour supporter les ECUs little-endian (Continental SID807, MED17, etc.).

---

## Tâche 1 — OLS WinOLS : extraire ROM + cartes

### Format OLS (WinOLS)

Un fichier `.ols` est un projet WinOLS binaire propriétaire. Il contient :
- La ROM de l'ECU (binaire brut, parfois compressée)
- Les définitions de cartes (offset, nom, dims, type, axes)
- Les métadonnées ECU (SW, HW, constructeur)

### Structure connue (reverse-engineering communautaire)

L'OLS est un format multi-blocs avec un en-tête de type :

```
[OLS Magic] [Version] [Bloc ECU info] [Bloc ROM] [Bloc Maps] ...
```

Les offsets et la structure exacte varient selon la version WinOLS (5.x, 6.x, 7.x). Références :
- https://github.com/flashtec/ols-tools (parseur Python partiel)
- https://github.com/opengarage/ecutools (lecture basique OLS)
- Fichier `RENAULT_2.0_DCI_EDC16CP33.ols` comme fichier de test (8.7MB)

### Ce que l'agent doit implémenter

```
libs/ecu-core/src/OlsImport.cpp + include/ecu/OlsImport.hpp
```

Fonctions minimales :

```cpp
namespace ecu {
    // Extraire la ROM binaire d'un fichier OLS
    std::expected<QByteArray, std::string>
    olsExtractRom(const QString& olsPath);

    // Extraire les définitions de cartes (optionnel, si le format est documenté)
    struct OlsMapEntry { std::string name; std::size_t offset; int nx; int ny; /* ... */ };
    std::expected<std::vector<OlsMapEntry>, std::string>
    olsExtractMaps(const QString& olsPath);
}
```

Si le format OLS est trop opaque pour `olsExtractMaps`, implémenter **au minimum** `olsExtractRom` — cela suffit pour que la relocalisation par fingerprint fonctionne sur la ROM extraite.

---

## Tâche 2 — Support little-endian dans OpenDAMOS

### Problème

Le standard OpenDAMOS v1 ne définit que des types big-endian :
`SWORD_BE`, `UWORD_BE`, `SLONG_BE`, `ULONG_BE`.

Le Continental SID807 (`Citroen_DS4_1.6HDI_SID807.a2l`) est **little-endian** (`BYTE_ORDER MSB_LAST`).
Les axes y sont aussi stockés dans des **AXIS_PTS séparés** (COM_AXIS), sans en-tête `(nx, ny)` inline avant la MAP — ce qui rend l'algorithme de scan par fingerprint inapplicable directement.

### Extension proposée

**a) Schéma JSON** (`ressources/open_damos.schema.json`) :

Ajouter dans l'enum `dataType` :
```json
"enum": ["SBYTE","UBYTE","SWORD_BE","UWORD_BE","SLONG_BE","ULONG_BE",
         "SWORD_LE","UWORD_LE","SLONG_LE","ULONG_LE"]
```

Ajouter un champ optionnel `byteOrder` au niveau recette et/ou `recordLayout` :
```json
"byteOrder": { "type": "string", "enum": ["BE", "LE"], "default": "BE" }
```

**b) Code C++** (`OpenDamos.hpp` / `OpenDamos.cpp`) :

- Étendre `DamosDataType` avec `SWordLE`, `UWordLE`, `SLongLE`, `ULongLE`
- Étendre `parseDamosDataType` pour les reconnaître
- Étendre `damosTypeSize` (identique)
- Modifier `readInt` dans le scan pour utiliser little-endian quand nécessaire

**c) Algorithme de scan pour COM_AXIS (SID807)** :

Le SID807 n'a pas d'en-tête `(nx, ny)` inline. Pour scanner ces ECUs, il faut :
1. Scanner les **AXIS_PTS** (blocs d'axes indépendants, précédés d'un UWORD count)
2. Matcher les fingerprints sur les AXIS_PTS
3. Retrouver les MAPs qui y font référence (en parsant l'A2L ou en utilisant les adresses des AXIS_PTS comme ancres)

Ce mode est un **scan COM_AXIS** distinct du mode actuel (en-tête inline). À documenter dans `OPENDAMOS_STANDARD.md` §11 (Limites).

---

## Tâche 3 — Générer open_damos.json pour EDC16CP33

Une fois la ROM extraite du OLS :

1. Vérifier que l'EDC16CP33 est big-endian (probable, c'est un ECU Bosch)
2. L'A2L n'est pas disponible séparément → utiliser les définitions de cartes extraites du OLS
3. Extraire les fingerprints en lisant la ROM aux offsets des cartes connues
4. Produire `ressources/edc16c34cp33/open_damos.json` (ou `ressources/edc16cp33/`)

Les noms de cartes Bosch EDC16CP33 sont similaires à l'EDC16C34 (même famille DAMOS Bosch) — un mapping partiel peut se faire depuis l'EDC16C34 existant.

---

## Critères de réussite

1. `olsExtractRom("RENAULT_2.0_DCI_EDC16CP33.ols")` retourne un `QByteArray` non vide.
2. `open_damos.schema.json` valide avec un JSON utilisant `SWORD_LE`.
3. L'algorithme de relocalisation supporte `byteOrder: "LE"` dans une recette SID807.
4. Un `open_damos.json` minimal pour l'EDC16CP33 (au moins 5 cartes avec fingerprints) est généré et valide contre le schéma.

---

## Pour démarrer

```bash
# Inspecter l'OLS
xxd RENAULT_2.0_DCI_EDC16CP33.ols | head -20

# Tester le parseur SID807 (Python, déjà fonctionnel pour les axes)
# voir /tmp/parse_sid807_v3.py dans la session précédente

# Valider un JSON contre le schéma
cd /home/leo/ecu_studio_suite
python3 -c "
import json, jsonschema
schema = json.load(open('ressources/open_damos.schema.json'))
recipe = json.load(open('ressources/edc16c34/open_damos.json'))
jsonschema.validate(recipe, schema)
print('Schema OK')
"
```
