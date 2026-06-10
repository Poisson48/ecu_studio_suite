# Rapport comparatif : ECU Studio vs WinOLS

**Date : juin 2026 · ECU Studio v1.4.5 (branche feat/windows-build-et-ux)**
Remplace `docs/GAP_ANALYSIS.md` (périmé, daté v1.0).

---

## 0. Cadrage — ce qu'est (et n'est pas) WinOLS

WinOLS (EVC) est un **éditeur/analyseur de fichiers ECU**, **pas un flasher** : la
lecture/écriture de l'ECU se fait avec du matériel séparé (KESS, KTAG, CMD, MPPS,
Autotuner…). ([evc.de](https://www.evc.de/en/product/ols/software/Default.asp),
[hpacademy](https://www.hpacademy.com/technical-articles/what-is-winols-can-it-tune-any-car/))

**Conséquence pour la comparaison :** le périmètre où il faut « battre WinOLS » est
**édition + détection de maps + 3D + compare + DAMOS/A2L + checksum/signature**. Le
flash matériel (notre MPPS, mis en *coming-soon*) est une **catégorie séparée** que
WinOLS n'a pas non plus — ce n'est pas un retard face à WinOLS.

**Faiblesses connues de WinOLS** (forums MHH/Digital-Kaos/HPA) : prix élevé (≈859 €
par poste + 98 €/an + formations), courbe d'apprentissage raide, **attente de
« drivers »/définitions pour les ECU récents ou rares**, lenteur pour un simple
Stage 1 (trop de maps non regroupées vs un ECM Titanium).
([carinterior](https://carinterior.alibaba.com/buyingguides/winols-cost-buying-guide-2024),
[mhhauto](https://mhhauto.com/Thread-Need-some-advice-about-Buying-Winols-please))
→ Ce sont précisément les angles où **OpenDAMOS** (relocalisation par empreinte, sans
DAMOS par firmware) et les **recettes** nous avantagent.

---

## 1. Tableau comparatif par catégorie

Légende : ✅ fait+UI · 🟡 partiel/lib · ❌ absent.

| Domaine | WinOLS | ECU Studio | Écart / priorité |
|---|---|---|---|
| **Import .bin / Intel HEX** | ✅ | ✅ | — |
| **Import .ols natif** | ✅ (format propre) | ✅ ROM + noms + ECU auto ; 🟡 adresses des maps (reverse en cours) | P1 (reverse records OLS) |
| **Import Motorola S19/S-record** | ✅ | ❌ | P2 (facile) |
| **DAMOS / A2L** | ✅ (charge DAMOS) | ✅ charge A2L + **convertit DAMOS→OpenDAMOS** (lecture empreintes ROM) | **avantage ECU Studio** |
| **Mappacks** | ✅ (format propriétaire) | 🟡 bibliothèque OpenDAMOS (127 ECU) | P2 |
| **Détection auto de maps** | ✅ (référence du marché) | ✅ MapFinder heuristique + empreinte d'axes | 🟡 affiner le scoring |
| **Édition 2D / heatmap** | ✅ | ✅ | — |
| **Vue 3D** | ✅ 2D/3D/4D | ✅ surface 3D + fantôme/baseline ; 🟡 modes delta/split | P1 (modes delta/split) |
| **Opérations maps** (±%, lisser, interpoler, copier/coller) | ✅ | ✅ (±%, lisser 3×3, interpoler, copier/coller TSV) | — |
| **Compare / différence** | ✅ | ✅ 2 ROMs + diff commits git + diff map-level | — |
| **Versioning** | ❌ (fichiers) | ✅ **git natif** (branches/variantes, undo-redo, restore) | **avantage ECU Studio** |
| **Checksum EDC16 32/64K** | ✅ | ✅ CRC-16/ARC | — |
| **Checksum EDC16 512K (région)** | ✅ | ❌ | **P1** |
| **Checksum EDC17/MED17 (CRC32/ADD32/ADD16)** | ✅ | ❌ | **P0** |
| **Signature RSA EDC17 (« Change RSA key »)** | ✅ (remplace la clé) | ❌ | **P0** |
| **Recettes guidées** (Stage1, limiters, EGR/DPF…) | 🟡 (manuel, scripts) | ✅ recettes OpenDAMOS + auto-mods + **badge de relocalisation** | **avantage ECU Studio** |
| **DPF OFF / IMMO OFF** | ✅ (manuel par l'expert) | 🟡 framework + garde sécurité IMMO ; ❌ patterns vérifiés | P1 (données vérifiées) |
| **Gestion de projets / base** | ✅ (base WinOLS) | ✅ ProjectManager | — |
| **Batch / multi-fichiers** | ✅ | 🟡 scripts Python, pas d'UI | P2 |
| **Scripting / automation** | ✅ (scripts WinOLS) | ✅ **serveur MCP** (pilotage par IA/Claude) | **avantage ECU Studio** |
| **Rapport** | ✅ (impression) | ✅ rapport HTML | — |
| **Coût / plateforme** | 💸 859 €+/poste, Windows, dongle/VM | **gratuit, Linux + Windows natif, open** | **avantage ECU Studio** |

---

## 2. Le point critique : checksum & signatures (ce qui gate l'écriture)

C'est **le** sujet pour rivaliser WinOLS sur les ECU modernes.

### État actuel ECU Studio
- ✅ **EDC16 32K / 64K** : CRC-16/ARC réel, validé par tests (`ChecksumEngine.cpp`).
- ⚠️ **EDC16C34 2 Mo** : **détection seule**, correction **refusée** (fail-safe anti-brick — algo non confirmé).
- ❌ **EDC16 512K (région), EDC17, MED17, ME7** : non implémentés.
- ✅ **Signature de l'app** (updater) : Ed25519 via OpenSSL, opérationnel (≠ signature ECU).

### Comment WinOLS fait l'EDC17/MED17 (confirmé forums + FAQ EVC)
- **Checksums** : `CRC32`, `ADD32`, `ADD16` sur les blocs Bosch.
  ([ecuconnections](https://www.ecuconnections.com/forum/viewtopic.php?t=29947),
  [github medc17-checksum-tool](https://github.com/ConnorHowell/medc17-checksum-tool))
- **Signature RSA** — option **« Change RSA key »** : au lieu de forger la signature
  Bosch (impossible sans la clé privée Bosch), WinOLS **remplace la clé publique de
  l'ECU par la sienne** (dont il connaît la privée), puis signe en pleine précision.
  ([FAQ EVC q1304](https://www.evc.de/en/service/q1304.asp))
  - Contrainte : la clé publique est dans la **zone OS** de l'ECU → programmable
    **uniquement en boot mode** (l'OBD2 le bloque), et certaines zones TriCore sont
    *one-time-programmable* (alors impossible).

**→ Conclusion clé : l'EDC17 est implémentable chez nous.** Le checksum
(CRC32/ADD32/ADD16) ET le « Change RSA key » (remplacement de paire de clés + signature
avec NOTRE clé) ne nécessitent **pas** la clé privée Bosch. Le seul prérequis est un
**flash boot mode** (matériel, hors WinOLS). Notre note actuelle « impossible sans clé
Bosch » est donc à corriger : c'est faisable comme WinOLS.

### Tableau famille × capacité (cible)
| Famille | Checksum | Signature | ECU Studio aujourd'hui | À faire |
|---|---|---|---|---|
| EDC16 32/64K | CRC-16/ARC | — | ✅ | — |
| EDC16 512K | CRC-16/ARC région | — (VAG) / RSA (PSA) | ❌ | descripteurs par ECU |
| EDC16C34 2M | additif 32b (?) | RSA 1024 | détection seule | reverse algo |
| **EDC17** | CRC32/ADD32/ADD16 | MD5+RSA (Change-key) | ❌ | **moteur EDC17 + change-RSA-key** |
| **MED17** | idem EDC17 | idem | ❌ | idem |
| ME7 | multi-points | — / RSA tardif | ❌ | port ME7Sum |

---

## 3. Ce qui manque vraiment pour « app complète » (priorisé)

**P0 — rivaliser sur les ECU modernes (50 % du marché)**
1. **Moteur checksum EDC17/MED17** : CRC32 + ADD32 + ADD16 sur blocs Bosch (algos
   documentés dans `docs/ecu-research/edc17.md`, `med17.md`). Effort moyen, gros impact.
2. **« Change RSA key » EDC17** : MD5 de la région + remplacement de la clé publique +
   signature RSA avec notre paire. Effort moyen ; nécessite OpenSSL (déjà lié).

**P1 — couverture & parité d'édition**
3. **Checksum EDC16 512K** (région-wise, VAG/BMW) — descripteurs par ECU.
4. **Reverse records OLS 5.x** → adresses des maps (`.ols → OpenDAMOS` parfait) —
   *bloqué sur vérité-terrain WinOLS (2-3 nom→adresse)*.
5. **Modes 3D delta/split** (comparaison visuelle stock↔tune).
6. **DPF/IMMO OFF** : intégrer des patterns **vérifiés** par famille (framework prêt).

**P2 — confort / complétude**
7. Import Motorola **S19/S-record** (facile).
8. **Batch UI** (appliquer une recette à N fichiers).
9. Affiner le **scoring** du MapFinder (smoothness + cross-ref A2L + empreinte).
10. **ME7** checksum (port ME7Sum, marché ancien).

---

## 4. Où ECU Studio **dépasse déjà** WinOLS

- **OpenDAMOS** : relocalisation par **empreinte d'axes** → une recette s'applique à
  *n'importe quel firmware* de la famille **sans DAMOS dédié**. Répond directement à la
  plainte n°1 des forums (« attendre les drivers / reverse manuel par firmware »).
- **Convertisseur DAMOS(A2L) → OpenDAMOS dans l'app** (lecture des empreintes ROM).
- **Git natif** : branches/variantes, undo-redo, restore, diff entre versions.
- **Recettes guidées + badge de qualité** : Stage 1 en quelques clics (vs « trop de
  maps, c'est lent » reproché à WinOLS).
- **Gratuit, open, Linux + Windows natif, MCP/IA** (pilotable par Claude), updater signé.

---

## 5. Roadmap proposée

1. **Moteur checksum multi-familles** (P0) : refactor `ChecksumEngine` en
   table de descripteurs par famille + ajout EDC17/MED17 (CRC32/ADD32/ADD16), tests
   known-answer. Puis **Change-RSA-key** EDC17.
2. **EDC16 512K** région-wise (P1).
3. **Reverse OLS** dès réception de la vérité-terrain WinOLS.
4. **3D delta/split**, **S-record**, **Batch UI**, **DPF/IMMO OFF vérifiés** (P1/P2).

---

## Sources
- [EVC — Software WinOLS](https://www.evc.de/en/product/ols/software/Default.asp)
- [EVC FAQ — RSA / EDC17 « Change RSA key »](https://www.evc.de/en/service/q1304.asp)
- [HPAcademy — What is WinOLS / can it tune any car](https://www.hpacademy.com/technical-articles/what-is-winols-can-it-tune-any-car/)
- [ECUconnections — Checksum EDC17 VAG WinOLS](https://www.ecuconnections.com/forum/viewtopic.php?t=29947)
- [MHH AUTO — WinOLS 2.24 RSA correction ols807](https://mhhauto.com/Thread-WinOLS-2-24-RSA-correction-problem-ols807)
- [GitHub — medc17-checksum-tool (CRC32/ADD32/ADD16)](https://github.com/ConnorHowell/medc17-checksum-tool)
- [CarInterior — WinOLS coût/limites](https://carinterior.alibaba.com/buyingguides/winols-cost-buying-guide-2024)
- [MHH AUTO — Need advice about buying WinOLS](https://mhhauto.com/Thread-Need-some-advice-about-Buying-Winols-please)
</content>
