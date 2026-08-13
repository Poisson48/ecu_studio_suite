# EGR OFF — Berlingo / Partner 1.6 HDi 75 (9HW / C353)

Firmware cible : **SW 1037383736**, cal **C353**, ROM origine md5
`f733c7bc5d4b1fc0092976874c21d08e`. Compatible avec Stage 2 A/B (zone EGR
hors smoke / boost).

## Pourquoi l’ancien `egr_off` était faux

Le catalogue écrivait `0x1F40` (8000 rpm) à **`0x1C41B8`**, présenté comme
`AirCtl_nMin_C`. Sur ce firmware 75 ch, **cette adresse n’est pas le scalaire
nMin** : c’est une **cellule de carto**. Le patch ne coupait pas l’EGR et
pouvait corrompre une map.

Méthode retenue : **mettre à 0 les maps de consigne / précommande EGR**,
cohérent avec une **plaque mécanique**.

## Auto-mods (les deux)

| Id | Carto | Effet |
|---|---|---|
| `egr_off_rEGR_9hw` | `AirCtl_rEGR_MAP` @ data `0x1C45CC` | taux EGR demandé → 0 |
| `egr_off_preCtl_9hw` | `EGRVlv_rPreCtlStat_MAP` @ data `0x1CAA10` | précommande position → 0 |

`EGRVlv_rPreCtlDyn_MAP` est **déjà à 0** d’usine sur ce dump.

Appliqués sur `9663944680_stage2.bin` → md5
`53a296670b74db43ae21faec83c1abd0`.

## Procédure

1. Plaque EGR en place (déjà chez toi).
2. Charger la ROM flashée (Stage 2A ok).
3. Appliquer **les deux** patterns (Auto-mods / template dépollution).
4. MPPS : **recalculer le checksum** à l’écriture.
5. Effacer les DTC, faire un cycle de conduite, relire `P0402` / `P0405` / `P0409`.

## Ce que ça ne fait pas

- **Pas de masquage DTC** : le dump MPPS cal-only n’a pas la table DTC
  exploitable. Un `P0405` / `P0409` (capteur position) peut **persister** tant
  que le circuit est diagnostiqué — même avec plaque + maps à 0.
- **Pas de Stage 2** : le turbo / underboost est un problème séparé.

## Retour arrière

Restore intégré sur chaque pattern (recherche des zéros → octets d’origine),
ou reflash de la lecture d’origine / Stage 2 sans EGR off.
