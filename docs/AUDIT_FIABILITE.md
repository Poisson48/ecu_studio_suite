# Audit de fiabilité — verdict honnête (juin 2026)

> Correctif d'honnêteté au `COMPARATIF_WINOLS.md` : plusieurs « avantages ECU Studio »
> y étaient **survendus**. Cet audit (adversarial, basé sur les tests réels et le code)
> dit ce qui est vraiment **solide** vs **fragile** vs **vernis**.

## Verdict global

Le **socle** est correct (checksum EDC16 32/64K, parsers de base, RomPatcher — testés).
Mais les **différenciateurs** vantés (catalogue « 127 ECU », relocalisation
firmware-agnostique) sont **prometteurs mais immatures**, et plusieurs panneaux sont
du **vernis** (non testés). Le **retard réel** vs WinOLS/ECM/Swiftec est large :
checksum moderne (EDC17+) absent, couverture validée ~0,03 % de la leur.

## Classement par fiabilité

### ✅ SOLIDE (prod, testé)
- **ChecksumEngine EDC16 32K/64K** — CRC-16/ARC, table validée à la compilation, 8 tests, fail-safe EDC16C34 2 Mo (refus de corriger). **Le vrai point fort.**
- **WinolsParser** (HEX/ZIP/.ols/raw) — durci (checksum HEX validé, anti-OOM, memcpy borné), 7 tests dont 2 d'entrées hostiles.
- **OlsImport** (extraction ROM) — testé sur un vrai `.ols` 9 Mo.
- **RomPatcher** (lectures/écritures bornées, ±%) — 6 tests.
- **DamosImport** (conversion DAMOS→OpenDAMOS) — 4 tests… mais **synthétiques seulement** (voir Fragile).

### 🟡 FRAGILE (marche sur cas heureux, non prouvé en vrai)
- **A2lParser** — streaming + décodage robuste, mais **0 test dédié** : blocs `/begin//end` imbriqués, `IF_DATA`, `COMPU_VTAB`, vrais DAMOS complexes **jamais testés**.
- **DamosImport** — validé seulement sur ROM synthétiques 0x200 o ; **jamais de bout en bout sur un vrai DAMOS A2L** ; FIX_AXIS / float / record-layouts variés non couverts.
- **MapFinder** — heuristique correcte, 2 tests synthétiques ; **taux de faux positifs vs WinOLS/Swiftec inconnu**, jamais testé sur vraie ROM.
- **MCP / OpenDamos JSON** — code défensif, mais pas de validation de schéma, peu de tests d'intégration.

### 🔴 VERNIS (à NE PAS vendre tel quel)
- **OpenDamosRelocate (722 lignes, 0 test)** — **le cœur de notre argument**, et le moins prouvé :
  - taux de relocalisation réel **~28-43 %** (`batch_opendamos.py` : MIN_OK=4 sur WANT=14) ;
  - matcher d'axes flou (bag-of-values, seuil 0,70) → **faux positifs possibles** (relocaliser sur la mauvaise adresse) ;
  - **aucune validation** que l'adresse trouvée est la BONNE (juste « une » adresse) ;
  - COM_AXIS « beta, no cross-firmware test » (README ligne ~112).
- **Map editor ops** (±%, lisser, interpoler, copier/coller) — **0 test**, risque d'overflow d'index, conversions phys/raw non vérifiées sur vrai ROM.
- **3D** — surtout **fallback QPainter** (pas GPU), modes delta/split absents, 0 test.
- **GitManager** — solide via libgit2 mais **0 test**, dépendance optionnelle (dégradé silencieux sans libgit2).
- **Checksum hors EDC16** — EDC17/MED17/ME7/SIMOS : **0 %**.

## Chiffres qui recadrent (vs ce que j'avais laissé entendre)

| Ce que j'ai dit | Réalité |
|---|---|
| « 127 ECU » | **123 « beta » auto-générés**, **4 « proven »** (EDC16C34/C39, EDC17C50/CP44) |
| « OpenDAMOS firmware-agnostique » | vrai **pour ces 4 ECU** ; ~30 % de relocalisation ailleurs, non validé |
| « recettes guidées » | **6 recettes**, ciblant surtout **EDC16C34** ; DPF/IMMO OFF absents |
| « MapFinder + empreinte » | heuristique non mesurée vs concurrents |
| « 3D » | projection QPainter, modes incomplets |
| Couverture validée | **~0,03 %** de WinOLS/ECM/Swiftec |

## Bugs/risques réels (extrait)
- ✅ **Corrigé** : `WinolsParser` parseIntelHex — `memcpy` non borné + image ~2 Gio possible + checksum HEX non validé. (Durci + 2 tests.)
- ⏳ À durcir : ZIP `uncompressedSize` non plafonné (zip-bomb) ; OlsImport bornes du bloc ROM ; OpenDamos JSON accès typés sans `is_number_integer`.

## Reprioritisation — « solidifier avant d'élargir »

Avant d'ajouter des features (et avant de revendiquer la parité) :
1. **Tester le cœur** : suite de tests **OpenDamosRelocate** sur de vrais ROM (mesurer le taux réel + les faux positifs) ; tests **A2lParser**, **MapEditor ops**.
2. **Valider la relocalisation** : comparer l'adresse trouvée à une vérité-terrain (DAMOS officiel) → publier un **taux de succès honnête**.
3. **Étiqueter la maturité** dans l'UI (proven/beta) au lieu de « 127 ECU ».
4. **Durcir** les parsers restants (ZIP, OlsImport, JSON).
5. **Puis** seulement : checksum EDC17/MED17 (le vrai retard P0) et le reste du plan.

**En une phrase :** fondations honnêtes + un *concept* OpenDAMOS réellement bon, mais
**la maturité, la couverture validée et le checksum moderne manquent** ; il faut
**solidifier et mesurer** avant de se comparer frontalement aux outils commerciaux.
</content>
