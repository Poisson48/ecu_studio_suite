# IMMO OFF — convention et sécurité

L'IMMO OFF désactive l'antidémarrage (immobilizer) d'un ECU. C'est une opération
**spécifique à chaque ECU/firmware** et **à haut risque** : un patch qui ne
correspond pas exactement au firmware peut rendre l'ECU non démarrable.

> ⚠️ Usage légitime uniquement : véhicule/ECU dont on est propriétaire ou pour
> lequel on est explicitement autorisé (swap moteur, remplacement/réparation
> d'ECU, banc d'essai). Toujours sauvegarder la ROM d'origine avant.

## Pourquoi pas de patterns « génériques » livrés

ECU Studio **ne livre aucun pattern IMMO OFF deviné**. Un pattern faux brick
l'ECU — incompatible avec l'exigence de fiabilité. Un IMMO OFF doit reposer sur
une définition **vérifiée** pour la famille d'ECU concernée.

## Déclarer un IMMO OFF vérifié

L'IMMO OFF s'exprime comme un **auto-mod** dans le recipe OpenDAMOS de l'ECU
(`ressources/<ecu>/open_damos.json`, tableau `autoMods`). Deux formes :

```jsonc
"autoMods": [
  {
    "id": "immo_off",
    "type": "pattern",                 // recherche/remplacement d'octets
    "description": "IMMO OFF — désactive le contrôle antidémarrage",
    "search":  "AA BB CC DD …",         // séquence vérifiée présente dans la ROM
    "replace": "EE FF 00 11 …",         // séquence de remplacement (même taille)
    "restore": "AA BB CC DD …"          // optionnel : pour annuler
  },
  {
    "id": "immo_off_flag",
    "type": "address",                 // écriture à une adresse fixe
    "description": "IMMO OFF — flag immo à 0",
    "address": "0x12345",
    "replace": "00",
    "restore": "01"
  }
]
```

Tout auto-mod dont l'`id` (ou la description) contient « immo » est traité par
l'app comme une opération IMMO OFF : il apparaît dans le panneau **AutoMods** et
déclenche une **confirmation de sécurité renforcée** avant application
(`AutoModsPanel::applySelection`).

## État actuel

- Le framework applique/restaure les auto-mods IMMO OFF (pattern/adresse) et les
  garde derrière l'avertissement renforcé.
- Aucune définition IMMO OFF vérifiée n'est encore embarquée par famille d'ECU :
  il faut intégrer des `search`/`replace` (ou `address`) **confirmés** pour
  chaque ECU ciblé. Le pack DAMOS 2020 contient des dossiers Immo (ex.
  « Immo EA890 ») à exploiter comme source.
