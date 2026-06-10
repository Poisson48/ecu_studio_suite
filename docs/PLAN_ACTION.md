# Plan d'action — atteindre/dépasser WinOLS et ses concurrents

**Date : juin 2026 · complète `docs/COMPARATIF_WINOLS.md`.**
Objectif : faire d'ECU Studio une app **équivalente ou meilleure** que le marché.

---

## 1. Paysage concurrentiel (au-delà de WinOLS)

| Outil | Approche maps | Checksum | Signature RSA | Flash | Définitions | Datalog | Prix / OS |
|---|---|---|---|---|---|---|---|
| **WinOLS** (EVC) | manuel + détection ; DAMOS/A2L | CRC16/32, ADD, multi-familles | ✅ « Change RSA key » | ❌ (matériel tiers) | DAMOS/A2L/mappacks | ❌ | 💸 859 €+, Windows |
| **ECM Titanium** (Alientech) | **Driver** = maps auto-groupées, **tables liées**, valeurs réelles | auto (driver) | partiel | tool Alientech | Drivers (par firmware) | ❌ | 💸 abonnement, Windows |
| **Swiftec** | **détection auto multi-constructeurs** (Bosch/Siemens/Delphi/Temic/Lucas/TRW…) | **détection auto du type de checksum** | partiel | OBD/bench/boot | plugins (10 000+ apps) | ❌ | 💸, Windows |
| **Autotuner** | guidé, **UX moderne/rapide** | auto | oui (HW) | ✅ OBD rapide | base intégrée | ❌ | 💸 (achat unique) |
| **Magic FLEX** | guidé | auto | oui (HW) | ✅ OBD/bench/boot | base intégrée | ❌ | 💸 (HW) |
| **RomRaider** (open) | **définitions XML** | (selon ECU) | — | OBD (Subaru…) | **XML ouvert** | ✅ **logging↔tuning** | gratuit, multi-OS |
| **TunerPro** (open) | définitions **XDF** | — | — | émulateur | **XDF ouvert** | ✅ | gratuit, Windows |
| **ECU Studio (nous)** | **OpenDAMOS = empreinte, firmware-agnostique** + MapFinder | CRC16/ARC (EDC16 32/64K) | ❌ | MPPS (coming-soon) | **OpenDAMOS** + A2L + conv. DAMOS | 🟡 (CAN/SocketSpy) | **gratuit, Linux+Win, open** |

Sources : [Alientech ECM vs WinOLS](https://www.alientech-tuning.com/ecm-titanium-3-0-vs-winols-5/),
[Swiftec/Balticdiag](https://www.balticdiag.com/Swiftec/),
[HP best chiptuning 2025](https://hp-chiptuningfiles.com/news/best-chiptuning-software-2025),
[RomRaider](https://www.romraider.com/), [obdexpress ECM/WinOLS/Swiftec](https://www.obdexpress.co.uk/service/ecm-titanium-vs-winols-vs-swiftec-for-ecu-tuning.html).

## 2. Ce que chaque concurrent fait de MIEUX (à capter)

- **ECM Titanium** → *Driver* : maps **groupées, nommées, prêtes à éditer**, en **valeurs réelles** (factor/offset/axes), avec **tables liées** (modifier une map met à jour les maps liées). C'est exactement notre OpenDAMOS… qu'on peut rendre **supérieur** car *firmware-agnostique* (pas un driver par firmware). **À ajouter : le lien entre tables.**
- **Swiftec** → **détection automatique** : (a) du **type d'ECU** depuis le binaire, (b) du **type de checksum**, (c) des maps **multi-constructeurs** (Siemens/Continental/Delphi, pas que Bosch). **À ajouter : modules d'identification auto.**
- **Autotuner** → **UX moderne + rapidité + mises à jour gratuites**. (On est déjà gratuit/cross-platform ; pousser l'UX.)
- **RomRaider / TunerPro** → **formats de définitions OUVERTS** (XML/XDF) et **boucle datalog↔tuning** (logger en live, vérifier l'effet). On a CAN/SocketSpy → **différenciateur** si on ferme la boucle.

## 3. Notre positionnement unique (à marteler)

**OpenDAMOS = le meilleur des deux mondes** : auto-groupé/guidé comme ECM Titanium,
**mais sans driver par firmware** (relocalisation par empreinte) → règle la plainte
n°1 des forums sur WinOLS/ECM (« attendre les drivers, reverse manuel par firmware »).
Plus : **git natif**, **gratuit/open**, **Linux+Windows**, **MCP/IA**, **convertisseur
DAMOS→OpenDAMOS intégré**.

---

## 4. Plan d'action phasé

Effort : ⭐ faible · ⭐⭐ moyen · ⭐⭐⭐ élevé. Impact relatif indiqué.

### Phase 1 — Écriture fiable des ECU modernes (parité WinOLS, P0)
> *Sans ça, on ne peut pas flasher un ROM moderne accepté par l'ECU. C'est LE gate.*

1. **Moteur checksum multi-familles** ⭐⭐ — refactor `ChecksumEngine` en **table de
   descripteurs par famille** (window, type, offset, init/poly), + ajout
   **EDC17/MED17 : CRC32 + ADD32 + ADD16** (algos déjà dans `docs/ecu-research/edc17.md`).
   Tests known-answer. → **égale WinOLS/Swiftec sur le checksum.**
2. **« Change RSA key » EDC17** ⭐⭐ — MD5 de la région signée + **remplacement de la
   clé publique** par la nôtre + signature RSA (OpenSSL déjà lié). N'exige PAS la clé
   Bosch. → **égale WinOLS** (flash boot-mode requis, matériel tiers).
3. **Auto-identification ECU + checksum depuis le binaire** ⭐⭐ — module qui devine
   famille/ECU/checksum à partir de signatures (magies, strings SW, taille, descripteurs).
   → **égale Swiftec**, et alimente 1/2 automatiquement.

### Phase 2 — Détection & définitions (parité Swiftec/ECM + écosystème)
4. **MapFinder multi-constructeurs** ⭐⭐ — étendre au-delà de Bosch (Siemens/Continental,
   Delphi, Marelli) + meilleur scoring (lissage + empreinte + cross-ref A2L).
5. **Import définitions ouvertes** ⭐ — lire **RomRaider XML** et **TunerPro XDF** →
   réutilise des milliers de définitions communautaires (différenciateur open).
6. **Tables liées (linked maps)** ⭐⭐ — propager une édition aux maps liées (axes
   partagés, maps miroir) comme ECM Titanium.
7. **Reverse records OLS 5.x** ⭐⭐⭐ — adresses des maps → `.ols → OpenDAMOS` parfait.
   *Bloqué sur vérité-terrain (2-3 nom→adresse depuis WinOLS).*

### Phase 3 — Différenciation (faire MIEUX que tous)
8. **Boucle datalog ↔ tuning** ⭐⭐⭐ — via CAN/SocketSpy : logger en live (UDS/OBD),
   superposer la donnée live sur la map, vérifier l'effet d'un tune. **Aucun concurrent
   éditeur ne le fait nativement** (RomRaider le fait pour Subaru seulement).
9. **3D delta/split** ⭐⭐ — comparaison visuelle stock↔tune en 3D.
10. **Batch UI** ⭐⭐ — appliquer une recette OpenDAMOS à N fichiers (flotte).
11. **DPF/IMMO OFF vérifiés** ⭐⭐ — patterns confirmés par famille (framework prêt).

### Phase 4 — Complétude & polish
12. Import **Motorola S19/S-record** ⭐.
13. **Mappacks** (import/export) ⭐⭐.
14. **Bibliothèque OpenDAMOS partagée/cloud** ⭐⭐ — communauté contribue/corrige.
15. Polish UX (onboarding, écran « aucune ROM », raccourcis) ⭐.

---

## 5. Ordre d'exécution recommandé

1. **Phase 1 entière** (checksum EDC17/MED17 + Change-RSA-key + auto-ID) — débloque
   l'écriture moderne, c'est le seul vrai retard fonctionnel vs WinOLS.
2. **Phase 2** items 4-5-6 (détection + défs ouvertes + tables liées) en parallèle du
   reverse OLS (dès vérité-terrain).
3. **Phase 3** (datalog loop = le différenciateur fort) + 3D/batch.
4. **Phase 4** au fil de l'eau.

**Résultat visé** : parité d'édition/checksum avec WinOLS/Swiftec/ECM, **supériorité**
sur le firmware-agnostique (OpenDAMOS), le versioning (git), l'ouverture (gratuit,
multi-OS, défs ouvertes, MCP) et la boucle datalog↔tuning.
</content>
