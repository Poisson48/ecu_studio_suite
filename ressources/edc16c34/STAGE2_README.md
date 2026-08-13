# ⚠️ UNSTABLE — Calibrations Stage 2 EDC16C34 / DV6B 9HW

> **Ces fichiers n'ont jamais tourné sur un moteur.** Ils sont issus d'un calcul,
> pas d'une mesure. Aucun banc, aucun log, aucun kilomètre. Ils sont publiés pour
> être tracés et reproductibles, **pas pour être flashés en l'état par un tiers**.

## Périmètre

Véhicule cible unique : Citroën Berlingo B9 fourgon 1.6 HDi 75, moteur DV6BTED4
(code 9HW), Bosch EDC16C34-4.11, calibration **C353**, SW Bosch **1037383736**,
lecture MPPS calibration seule.

**Ne conviennent à aucun autre véhicule**, y compris un autre 1.6 HDi 75 : le
limiteur de fumée et la suralimentation sont calibrés différemment d'une
référence à l'autre. La ROM d'origine attendue a le md5
`f733c7bc5d4b1fc0092976874c21d08e`.

## Fichiers

| Fichier | md5 | Contenu |
|---|---|---|
| `9663944680.bin` | `f733c7bc5d4b1fc0092976874c21d08e` | origine, lecture MPPS, **ne jamais écraser** |
| `9663944680_stage2.bin` | `155130df43a84eb211d842882b6d1f02` | version A, 104 octets modifiés |
| `9663944680_stage2B.bin` | `b4edc7c191d6fb654a35f7b845c0aa42` | version B, 111 octets modifiés |

Les deux binaires sont **entièrement reproductibles** depuis
`stage2_berlingo_9hw.json` et `stage2B_berlingo_9hw.json` via `apply_recipe`,
qui relocalise par empreinte d'axes. Les recettes sont la source de vérité ;
les `.bin` ne sont là que pour éviter d'avoir à les régénérer.

Depuis `open_damos.json` 1.5.0, les mêmes modifications sont aussi disponibles
dans le **panneau Auto-mods de l'application** sous forme de trois patterns
search/replace (avec restore intégré) :

| Auto-mod | Contenu |
|---|---|
| `stage2_smoke_9hw` | limiteur de fumée, commun A et B — **toujours requis** |
| `stage2A_boost_9hw` | suralimentation version A |
| `stage2B_boost_9hw` | suralimentation version B |

Un Stage 2 complet = `stage2_smoke_9hw` + **un seul** des deux boost. Les deux
boost sont mutuellement exclusifs : restaurer l'un avant d'appliquer l'autre
(le motif de recherche ne matche que les octets d'origine). Appliqués sur la
lecture d'origine, ils reproduisent les md5 ci-dessus à l'octet près (vérifié
via `damos_apply_automod`). Les motifs sont uniques dans la ROM, mais restent
liés à cette calibration : sur toute autre ROM, le pattern ne matchera
simplement pas.

## EGR OFF (9HW)

Sur ce firmware, **ne pas** utiliser l’ancien auto-mod catalogue `egr_off`
(adresse `0x1C41B8` fausse). Appliquer plutôt :

- `egr_off_rEGR_9hw`
- `egr_off_preCtl_9hw`

Détail : [`EGR_OFF_9HW.md`](EGR_OFF_9HW.md). Compatible avec Stage 2 A/B
(md5 stage2+egr : `53a296670b74db43ae21faec83c1abd0`).

## Ce qui est modifié

Deux cartos, rien d'autre. Vérifié octet par octet : zone programme,
identification, descripteurs de checksum et sous-bloc checksummé intacts.

**`AirSys_pAirBasRgn1_MAP`** (suralimentation, `0x1C4D1C`) — consigne relevée
dans le médium, retour à la valeur d'origine dès 3250 tr/min, jamais de
dépassement au-delà. Pic 1810 mbar absolus en version A, 1870 en version B.

**`FlMng_rLmbdSmk_MAP`** (limiteur de fumée, `0x1CC33C`) — plancher de lambda
abaissé de 3000 à 4500 tr/min sur les colonnes de masse d'air ≥ 550 mg/Hub,
de 1,37-1,46 d'origine vers 1,23-1,38. Lambda mini modifié **1,230, soit
AFR 17,8:1**. Identique dans les deux versions.

Version A et version B ne diffèrent **que** par la suralimentation sous
2500 tr/min : +120 mbar en B, pour le couple à bas régime.

## Prévisions, et leur statut

Estimations issues du modèle suralimentation → masse d'air → limiteur de fumée
→ FMTC → couple. Le modèle reproduit **74 ch pour la calibration d'origine
contre 75 ch annoncés par PSA**, ce qui le crédibilise sans le valider.

| | origine | version A | version B |
|---|---|---|---|
| couple à 1600 tr/min | 146 Nm | 161 Nm | **182 Nm** |
| couple maxi | 175 Nm | 187 Nm | **194 Nm** |
| puissance maxi | 74 ch | 87 ch | 87 ch |

**Aucun de ces chiffres n'est mesuré.** La cible initiale de 105-115 ch n'est
pas atteignable : elle demanderait ~1936 mbar à 4000 tr/min et un rapport
air/carburant sous le plancher de 17:1, hors de portée du TD025 à géométrie
fixe. Il faudrait le turbo du 110 ch.

## Prérequis absolu : sonde EGT

**Ne pas flasher sans sonde EGT avant turbine, posée et fonctionnelle.**

L'ouverture du limiteur de fumée ajoute du gazole à 3500-4000 tr/min, au point
de débit maximal, donc de température d'échappement maximale. C'est le
paramètre le plus destructeur de cette calibration et **aucun capteur d'origine
ne le voit**.

| EGT avant turbine | Conduite |
|---|---|
| jusqu'à 700 °C | normal |
| 700 à 750 °C | acceptable en montée longue |
| 750 à 780 °C | lever le pied |
| > 780 °C | **couper la charge immédiatement** |

Une sonde en aval de turbine lit 80 à 120 °C de moins avec du retard : elle ne
protège pas.

## Checksum

Le CK 32 bits de la région calibration (`0x1C0028`) est **périmé** dans les deux
fichiers : des octets de la région qu'il couvre ont changé sans qu'il soit
recalculé. L'algorithme n'est pas recouvré (cf. `docs/mpps-checksums.md` §8), et
`verify_checksum` / `correct_checksum` refusent délibérément les images 2 Mo
EDC16C34.

**Le MPPS doit recalculer le checksum à l'écriture.** À vérifier avant de lancer
le flash : c'est le premier risque d'échec.

Attention aussi au sous-bloc `0x1C122C`-`0x1C1557`, qui porte son propre CK. Il
n'est pas touché ici, mais toute modification future des cartos de couple pédale
tomberait dedans.

## Validation, dans cet ordre

1. **Log de référence AVANT flash**, EGT incluse. Sans baseline thermique, un
   relevé à 720 °C ne veut rien dire.
2. **Version A d'abord.** Montée pleine charge en 3e. Deux points à regarder :
   l'écart consigne/réel de suralimentation à 1600-2000 tr/min, et l'EGT à
   3500-4000.
3. **Version B ensuite**, seulement si A tient sa consigne sans écart et sans
   dépasser 750 °C. Si A montre déjà un écart à bas régime, B demandera
   davantage et n'apportera que de la chaleur.
4. **Arrêt immédiat** si fumée noire en charge, écart de suralimentation
   > 200 mbar, défaut de pression rail, patinage d'embrayage, ou mise en
   sécurité.

## Retour arrière

`9663944680.bin` est la lecture d'origine du calculateur, intacte et reflashable
telle quelle. **C'est le chemin de retour.** En garder une copie sur un support
externe au dépôt.

## Risques connus

- **Fumée visible en reprise sous 1800 tr/min**, surtout en version B. Le
  plancher de lambda usine descend à 1,03 dans cette zone (AFR 14,9:1) : ce
  n'est pas un enrichissement ajouté, mais il y circule plus de gazole, donc
  plus de suie.
- **Pression cylindre à bas régime** en version B (1544 mbar à 1600 tr/min
  contre 1374 d'origine). Point faible connu du DV6 côté joint de culasse, et
  invisible sur les capteurs d'origine. Surveiller température d'eau et traces
  de gaz au vase d'expansion.
- **Le TD025 doit réellement tenir la consigne à bas régime.** Non démontré. Si
  l'écart consigne/réel dépasse 150 mbar à 1600 tr/min, le fichier demande plus
  que ce que le turbo sait donner.
- **Correction IAT et limiteur de suralimentation non identifiés** dans ce
  firmware. Un défaut de surpression reste possible et n'a pas pu être anticipé.
