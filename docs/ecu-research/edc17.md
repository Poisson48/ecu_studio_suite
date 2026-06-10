# EDC17 diesel — research notes

> Addresses below come from specific dumps/guides and are **box-specific**. Use as
> labels/leads, not as family-wide constants.

## 0. Structure de la table de blocs — VALIDÉE sur dumps réels (2026-06-10)

Prototype validé contre `.damos_raw/edc17_c50.bin`, `edc17_c56.bin`,
`edc17cp44_audi.bin` : **CRC32 = 100 % valides** sur les blocs programme. Le port C++
doit utiliser EXACTEMENT ceci.

**Découverte des blocs** (scan dword-aligné) : un bloc commence là où
`(u32@+0 & 0xFF) ∈ {0x10,0x30,0x40,0x60,0xC0}`, `size=u32@+4` avec `0x40≤size≤reste`,
et `u32@(start+size-4)==0xDEADBEEF`.

**En-tête de bloc** : `+0x2C` = nombre de structures checksum ; `+0x30` = checksum-
adjust (dword) ; `+0x34` = première structure (32 octets chacune).

**Structure checksum (32 o, little-endian)** : `+4`=adresse début, `+8`=adresse fin
(inclusive), `+12`=valeur init (= 0xFADECAFE), `+16`=valeur attendue (ADD), `+28`=algo
(word & 0xFF) : `0x00`=CRC32, `0x01`=ADD32, `0x10`=ADD16.

**Adresse → offset fichier** : `addr & 0x1FFFFFFF` (miroirs TriCore).

**Algorithmes** : CRC32 bitwise poly `0xEDB88320`, init = champ init, sur **dwords LE**,
attendu **`0x35015001`**. ADD32 = somme de dwords LE (init), attendu = champ attendu
(`0xCAFEAFFE`). ADD16 = somme de mots LE (idem). Région **[start..end] inclusive**.

> Note port : le bloc **dataset (id 0x60)** d'un fichier tuné a un ADD invalide tant
> qu'on n'a pas recalculé l'adjust → c'est ce que la **correction** doit faire
> (ADD : ajuster les 4 derniers octets de la région ; CRC32 : résoudre le patch par
> élimination de Gauss GF(2) — cf. medc17-checksum-tool). Round-trip à tester.

## 1. Checksum / security (CONFIRMED family)

Bosch EDC17 (Tricore) firmware uses **three checksum algorithms** over Bosch blocks:

- **CRC32** — IEEE 802.3 bit-reversed polynomial `0xEDB88320`, init `0xFADECAFE`,
  expected result `0x35015001`.
- **ADD32** — 32-bit dword sum (overflow ignored), init `0xFADECAFE`, expected `0xCAFEAFFE`.
- **ADD16** — sum of 16-bit words from 32-bit dwords, same init/expected as ADD32.

Block types (Bosch header IDs): Absolute constants `0xC0`, Customer block `0x30`,
Application software `0x40`, Dataset `0x60`, Startup block `0x10`. Additionally a **CVN**
(Calibration Verification Number) = CRC32 over calibration, reported via OBD-II.

- CONFIDENCE: **CONFIRMED** — source-grade (open repo) + a second forum/repo corroborating
  CRC32/ADD32/ADD16 for MED17/EDC17.
- SOURCE:
  - https://github.com/ConnorHowell/medc17-checksum-tool (algorithms, polynomials, init/expected, block IDs)
  - https://github.com/ConnorHowell/medc17-checksum-tool ("Two-Pass Correction": last 4 bytes of region, then GF(2) matrix solve)
  - https://tuninghost.com/med17-edc17-crc-algo/ (MED17/EDC17 CRC algo)
  - https://reverseengineer.net/bosch-med17-edc17-ecu-reverse-engineering/ (Tricore RE guide)

PSA seed-keys (diagnostic access): EDC17C10 `1905`, EDC17CP11 `1812`, EDC17C60 `3102`
— SOURCE: https://github.com/ludwig-v/psa-seedkey-algorithm/blob/main/ECU_KEYS.md (REPORTED).

## 2. Stage-1 map addresses — from "EDC17 Tuning Guide" (VAG 2.0 TDI 143hp)

**Exact dump:** Audi A4 2008 2.0 TDI CR, 143 hp / 320 Nm, ECU part **`03L 906 022 JN`**,
SW checksum 396472 (an EDC17 VAG / EA189-era unit). REPORTED, **box-specific**.

SOURCE: https://pdfcoffee.com/edc17-tuning-guide-pdf-free.html
(also https://www.scribd.com/document/324638230/EDC17-Tuning-Guide ,
https://idoc.pub/documents/edc17-tuning-guide-pnxkpgvpdy4v)

| Map | Addr(es) | Dim | Factor | What it does |
|-----|----------|-----|--------|--------------|
| Driver's Wish | `0x1B26CC, 0x1B2800, 0x1B2934, 0x1B2A68, 0x1B2B9C, 0x1B2CD0` | 8×16 | 0.1 | requested torque (Nm) vs pedal × RPM |
| Driver's Wish Limiter | `0x1A13AA` | 8×8 | 0.39063 / 39.552 | max torque (Nm) |
| Torque Limiter | `0x1C1FCC` | 24×4 | 0.1 | torque cap |
| Torque→IQ conversion | `0x1BD364, 0x1BD5A8` | 16×16 | 0.01 | torque → IQ (mg/stroke) |
| Boost Pressure | `0x1E6BBE … 0x1E7FBE` (8 maps) | 16×15 / 13×16 | 1 mBar | requested boost |
| Boost Limiter | `0x1E86C0` | 10×16 | 1 mBar | boost cap |
| N75 (boost ctrl) | `0x1E453C, 0x1E4990, 0x1E4B6E, 0x1E4D4D, 0x1E52E6` | 13–16 | 0.012207 | wastegate valve % |
| Smoke map (by MAF) | `0x1EE33A, 0x1EE610` | 16×11, 16×12 | 0.01 | IQ limit vs airmass |
| Smoke map (by MAP) | `0x1F55BE` | 14×16 | 0.01 | IQ limit vs boost |
| Rail Pressure Limiter Offset | `0x1F2C6E` | 10×14 | 0.1 | rail pressure (bar) |
| Rail Pressure Limiter | `0x1F2DC6` | 10×14 | 0.1 | rail pressure control |

CONFIDENCE: **REPORTED** for all (single guide, single dump).
Typical Stage-1 deltas (REPORTED, EDC17 practice from same guide + ecuedit basic guide):
smoke +15–25%, torque-limiter +15–25%, driver's-wish/limiter raised, boost-limiter +5–10%.
SOURCE for method: https://www.ecuedit.com/basic-stage-1-tuning-edc17-how-to-do-it-right-t18891

## 3. EDC17CP14 map addresses (different dump — REPORTED)

SOURCE: https://ecuedit.com/edc17cp14-map-adress-t21751 ,
https://mhhauto.com/Thread-edc17cp14-drivers-wish-request

| Map | Addr | Dim | Note |
|-----|------|-----|------|
| Driver's Wish | `0x1B421C, 0x1B4350, 0x1B4484` | 8×14 | REPORTED, box-specific |
| Torque Limiter | `0x1C34C0` | — | REPORTED |
| Torque→IQ | `0x1BE49A` | — | REPORTED |

(One responder noted a given CP14 file "has only Lambda maps" — i.e. addresses vary by build.)

## 4. DPF / FAP off — EDC17 (from the VAG 2.0 TDI guide)

- DPF/FAP switches at **`0x1EC667, 0x1EC668`** (1×1 each). Method: search hex `A0 0F 01`
  (8-bit), then 6 bytes later you find `01 01`; flip to disable filtering.
  - CONFIDENCE: **REPORTED** (single guide, box-specific). Do not flash blind.
  - SOURCE: https://pdfcoffee.com/edc17-tuning-guide-pdf-free.html
- EDC17C46 (VAG/Renault) EGR/DPF/DTC-off — widely available as ready files, method-level only:
  - https://ecutools.eu/chip-tuning/probyte/vag-group-diesel-bosch-edc17c46/
  - https://www.ect-download.com/en/dpf-off-egr-off-vw-edc17c46/
  - https://cartechnology.co.uk/showthread.php?tid=116535
  - Warning thread: EGR-off can cause DPF over-regeneration if regen maps not also closed —
    https://mhhauto.com/Thread-Audi-Q3-2-0-TDI-edc17c46-egr-off-problem-regeneration-dpf

**No EDC17 EGR/DPF/DTC hex byte-pattern was published with confirmed bytes → omitted from JSON
except the DPF switch search pattern above, kept as REPORTED note.**

## 5. Open A2L/DAMOS availability

- EDC16+EDC17 DAMOS pack (paid): https://autodtc.net/ecu-item/damos-and-map-pack/damos-for-edc16-and-edc17-database/
- EDC17 VAG DAMOS aide (scribd): https://www.scribd.com/document/464267702/aide-damos-pour-edc17-vag-search
- No open, downloadable EDC17 A2L found in the clear.
