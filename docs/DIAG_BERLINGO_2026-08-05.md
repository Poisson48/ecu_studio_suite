# Diagnostic Berlingo — 5 août 2026

Lecture OBD-II réalisée en direct sur l'ELM327, plus un bug de décodage trouvé
au passage dans `libs/ecu-core`. Ce document contient le relevé, son
interprétation, la preuve du bug, et ce qu'il reste à coder.

---

## 1. Le relevé (données réelles)

**Matériel / liaison**

| Élément | Valeur |
|---|---|
| Interface | ELM327 v1.5 USB, `/dev/ttyACM0` (`ID 0918:7104 QBD Virtual COM Port`) |
| Baud | indifférent — c'est un port CDC virtuel, il répond à 9600/38400/115200/500000 |
| Protocole détecté | `AUTO, ISO 14230-4 (KWP FAST)` |
| Tension `ATRV` | **12,4 V** → contact mis, moteur à l'arrêt |

Le dongle Bluetooth « OBDII » (`00:1D:A5:68:98:8C`) est appairé mais n'a rien à
voir : il n'a jamais répondu (`br-connection-page-timeout`). Tout est passé par
l'USB.

**Statut MIL — `0101`**

```
41 01 85 06 80 00
      ^^
```

`0x85` = `1000 0101` → bit 7 = **voyant moteur allumé**, 7 bits bas = `0x05` =
**5 défauts mémorisés**.

**Défauts mémorisés — mode `03`**

```
43 21 43 04 05 04 02
43 13 51 02 99 00 00      ← 00 00 = padding, pas un code
```

**Défauts en attente — mode `07`**

```
47 21 43 04 05 04 02
47 13 51 02 99 00 00
```

**Défauts permanents — mode `0A`** → `7F 0A 11` = service non supporté. Normal
sur cette génération d'ECU, ce n'est pas une anomalie.

---

## 2. Les 5 codes

| Code | Signification | Famille |
|---|---|---|
| **P0402** | Débit EGR excessif détecté | EGR |
| **P0405** | Circuit capteur EGR « A » — signal bas | EGR |
| **P2143** | Circuit commande vanne EGR — signal haut | EGR |
| **P0299** | Sous-suralimentation turbo (underboost) | Turbo |
| **P1351** | **Code constructeur PSA** — hors table générique | à confirmer |

**Ces défauts sont actifs, pas historiques.** Le mode `07` (en attente) renvoie
exactement les mêmes 5 codes : ils se re-déclenchent au cycle en cours, ce n'est
pas un résidu d'une panne déjà réparée.

### Lecture

Trois codes EGR groupés + un underboost, c'est cohérent sur un HDi : vanne EGR
encrassée ou grippée (P0402 + P2143 vont ensemble — la vanne ne suit pas la
consigne), P0405 pointant le retour de position. Le P0299 en parallèle est
classique quand la géométrie variable du turbo colle, souvent liée au même
encrassement calamine.

### Deux réserves

- **P1351 n'est pas interprétable ici.** C'est un code propriétaire PSA, sa
  table n'est pas générique. À lire avec Diagbox/Lexia. Ne pas se fier à une
  signification trouvée sur un forum générique.
- **L'ordre de cause à effet EGR ↔ turbo n'est pas décidable depuis les seuls
  codes.** La trame figée (régime / charge / pression collecteur au moment du
  défaut) trancherait — elle n'a pas pu être relevée, la sonde a été interrompue
  avant d'avoir vidé son tampon. À refaire quand la voiture est rebranchée :

  ```
  mode 02 : 0202 (DTC déclencheur), 0204 charge, 0205 temp. liq., 020B pression
            collecteur, 020C régime, 020D vitesse, 020F temp. air admission
  ```

**Rien n'a été effacé** — aucune commande `04` n'a été envoyée, les codes sont
intacts.

---

## 3. Bug trouvé : `decodeDtcs` fabrique des codes

**Ne pas se fier au bouton « Lire DTC » de l'app tant que ce n'est pas corrigé.**

`libs/ecu-core/src/Obd2.cpp` → `decodeDtcs()` concatène toutes les lignes de la
réponse avant de décoder, puis `break` après la première trame. Sur une réponse
KWP multi-trames — exactement le cas du Berlingo — l'octet de réponse `43` de la
**deuxième** trame est consommé comme un demi-DTC.

Sur les données réelles ci-dessus, l'app affiche :

```
obtenu  : P2143, P0405, P0402, C0313, C1102, B1900
attendu : P2143, P0405, P0402, P1351, P0299
```

- **3 codes inventés** : `C0313`, `C1102`, `B1900` — des codes ABS/carrosserie
  qui n'existent pas sur la voiture.
- **2 codes manqués** : `P1351` et surtout **`P0299` (turbo)**.

Le commentaire du header (`Obd2.hpp:56`) annonce pourtant « gère le
multi-lignes ». Le test unitaire existant (`tests/unit/test_obd2.cpp:43`) ne
teste qu'**une seule** trame, d'où le passage inaperçu.

Chemin du bug : `elm327.cpp:177` prend tout le buffer jusqu'au prompt `>`
(donc les deux lignes), `elm327.cpp:202` le passe à `decodeDtcs`.

Note annexe : l'init envoie `ATS1` (`elm327.cpp:14`), donc espaces activés. Avec
`ATS0` le bug est encore pire — `hexBytes` ne garde que les tokens de 2
caractères, donc `43214304050402` (un seul token de 14) est **entièrement jeté**
et la liste ressort vide.

Script de reproduction (réplique Python de l'algo) :
`/home/leo/.claude/jobs/abd539bb/tmp/repro.py`

---

## 4. État actuel de la branche

Branche **`feat/mcp-read-dtc`**, créée depuis `origin/main` (`e36ac6a`, 27 juil.)
— c'est bien la source de la release `~/Downloads/ECU_Studio-x86_64.AppImage`.
Ton checkout précédent (`debug/run-app`) était deux mois en retard et ne
contenait pas du tout le code OBD.

**Déjà modifié (non commité, non compilé, non testé) :**

```
 M libs/ecu-core/include/ecu/Obd2.hpp     (+11 -2)
 M libs/ecu-core/src/Obd2.cpp             (+69 -22)
```

Le correctif de `decodeDtcs` : décodage ligne par ligne, en distinguant les deux
formes de multi-trames (KWP — chaque ligne rouvre par l'octet de réponse ;
ISO-TP/CAN — lignes de continuation sans en-tête), plus un paramètre
`mode = 0x03` pour pouvoir décoder aussi le mode `07`.

Pour repartir de zéro si tu préfères tout réécrire :

```bash
git checkout -- libs/ecu-core/include/ecu/Obd2.hpp libs/ecu-core/src/Obd2.cpp
```

---

## 5. Ce qu'il reste à faire

### a. Valider le correctif `decodeDtcs`

Ajouter un test avec les données réelles dans `tests/unit/test_obd2.cpp` :

```cpp
TEST(Obd2, DecodeDtcsMultiFrameKwp) {
    // Réponse réelle Berlingo (ISO 14230-4, deux trames)
    auto codes = decodeDtcs(QStringLiteral("43 21 43 04 05 04 02\r"
                                           "43 13 51 02 99 00 00"));
    ASSERT_EQ(codes.size(), 5);
    EXPECT_EQ(codes[0], QStringLiteral("P2143"));
    EXPECT_EQ(codes[3], QStringLiteral("P1351"));
    EXPECT_EQ(codes[4], QStringLiteral("P0299"));
}

TEST(Obd2, DecodeDtcsPendingMode07) {
    auto codes = decodeDtcs(QStringLiteral("47 21 43 00 00"), 0x07);
    ASSERT_EQ(codes.size(), 1);
    EXPECT_EQ(codes[0], QStringLiteral("P2143"));
}
```

Rien n'a été compilé — le correctif est relu mais **pas** vérifié par un build.

### b. Outil MCP `read_dtc`

**Point de blocage architectural à trancher.** `Elm327` vit dans
`apps/ecu-studio/src/obd/elm327.{h,cpp}` (293 lignes, Qt + `QSerialPort`,
asynchrone à signaux), alors que `libs/ecu-mcp` est une lib qui ne peut pas
remonter dans `apps/`. Deux options :

1. **Déplacer** `elm327.{h,cpp}` vers une nouvelle `libs/ecu-obd` (link
   `Qt6::SerialPort` + `ecu_core`), puis `libs/ecu-mcp` la lie et ajoute
   `src/tools_obd.cpp`. Propre, conforme au découpage `libs/` vs `apps/`, mais
   touche les `CMakeLists` et les `#include` de `obd_panel.cpp` /
   `main_window.cpp`.
2. **Enregistrer l'outil depuis la couche app** : `makeReadDtcTool()` dans
   `apps/ecu-studio/src/obd/`, appelé dans `runMcp()` (`main.cpp:46`) juste après
   `registerAllTools(server)`. Zéro churn CMake, mais l'outil n'est plus dans
   `libs/ecu-mcp`.

Dans les deux cas le handler MCP est synchrone alors qu'`Elm327` est
asynchrone : il faut brancher `dtcsReady` / `errorOccurred` sur un `QEventLoop`
avec timeout. Le serveur MCP tourne déjà sous `QCoreApplication`
(`main.cpp:47`), donc la boucle d'événements est disponible.

Schéma d'entrée suggéré : `{ port?: string, baud?: int, pending?: bool }` —
`port` optionnel car `Elm327::listPorts()` sait déjà repérer les ponts USB-série
typiques (`likelyElm`).

### c. `clear_dtc` : à ne PAS exposer en MCP

`libs/ecu-mcp/include/ecu/mcp/Tools.hpp` déclare son contrat de sûreté en
en-tête :

> les outils d'écriture écrivent TOUJOURS dans un fichier de sortie distinct,
> jamais sur la ROM source **ni sur un périphérique connecté**

Effacer des défauts est une écriture sur la voiture. L'exposer en MCP casserait
cet invariant — autant garder l'effacement dans le GUI, où c'est un geste
explicite.

### d. Sortie facile des codes depuis l'interface

`ObdPanel` (`apps/ecu-studio/src/panels/obd_panel.h`) n'affiche aujourd'hui les
codes que dans un `QLabel* m_dtcLabel` — pas copiable proprement, pas
exportable. Piste : remplacer par un `QTableWidget` (code / famille / statut
mémorisé‑vs‑en‑attente) + un bouton « Copier » et un export `.txt`/`.csv`, en
réutilisant le `m_csvBtn` déjà présent pour le datalog. Ajouter aussi un appel
mode `07` en parallèle du `03` : c'est ce qui distingue un défaut actif d'un
résidu, et c'est l'info qui a été décisive ici.

---

## 6. Rappel commandes ELM327

```bash
# init
ATZ ; ATE0 ; ATL0 ; ATS1 ; ATH0 ; ATSP0
0100          # établit la liaison + PID supportés
ATDP          # protocole retenu
ATRV          # tension batterie

# défauts
0101          # statut MIL + nombre de codes
03            # mémorisés
07            # en attente
0A            # permanents (non supporté ici)
04            # EFFACEMENT — ne pas envoyer par erreur
```

Après remontage de la vanne EGR : relire `03` **et** `07` avant d'effacer, puis
effacer, puis refaire un cycle de conduite et relire `07` — c'est le mode `07`
qui dira si le défaut revient.
