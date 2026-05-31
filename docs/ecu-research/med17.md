# Bosch MED17 petrol — research notes

> Same family as EDC17 (Tricore). Map names follow the ME7 torque-structure naming
> (KFMIOP/KFMIRL/LDRXN…). Addresses are **per-box**.

## 1. Checksum / security (CONFIRMED family)

Identical algorithm family to EDC17 (see [edc17.md](edc17.md) §1): **CRC32**
(poly `0xEDB88320`, init `0xFADECAFE`, expect `0x35015001`), **ADD32** (init `0xFADECAFE`,
expect `0xCAFEAFFE`), **ADD16**; Bosch block IDs Absolute-const `0xC0`, Customer `0x30`,
AppSW `0x40`, Dataset `0x60`, Startup `0x10`; plus OBD **CVN** (CRC32 over calibration).

- CONFIDENCE: **CONFIRMED** (open repo handles MED17 + EDC17 together; second source corroborates).
- SOURCE:
  - https://github.com/ConnorHowell/medc17-checksum-tool (named "MED17/EDC17" tool)
  - https://tuninghost.com/med17-edc17-crc-algo/
  - https://reverseengineer.net/bosch-med17-edc17-ecu-reverse-engineering/

## 2. Stage-1 maps (names CONFIRMED, semantics CONFIRMED; addresses REPORTED/box-specific)

MED17 boost/torque structure mirrors ME7:

- **LDRXN** — max specified load (boost request limiter).
- **KFMIRL** — requested torque → requested load (the map you raise for more boost).
- **KFMIOP** — load → torque / rlmax → torque cap (inverse of KFMIRL; stock ≤ ~89/94%).
- **KFLDHBN** — max requested pressure ratio.
- Common Stage-1 recipe (REPORTED): multiply LDRXN and KFMIRL by ~1.05 in the target
  region, divide KFMIOP by ~1.05 to keep the inverse consistent and avoid torque cuts.
- CONFIDENCE: names/semantics **CONFIRMED** (≥2 sources); recipe **REPORTED**.
- SOURCE:
  - https://mhhauto.com/Thread-KFMIOP-KFMIRL
  - https://www.ecuedit.com/med17-boost-taper-at-high-rpm-t22200
  - https://www.hpacademy.com/forum/winols-mastery-map-identification-and-editing/show/boost-control-bosch-med17/
  - https://s4wiki.com/wiki/Tuning (map semantics shared with ME7)

### Concrete addresses — MED17.5.5 (one box, REPORTED)

| Map | Addr | CONFIDENCE | SOURCE |
|-----|------|-----------|--------|
| KFMIOP | `0x58366` | REPORTED (box-specific) | https://www.ecuedit.com/map-location-and-name-for-med17-5-5-t17686 |
| KFMIRL | `0x585EA` | REPORTED (box-specific) | same |

> These are for a **MED17.5.5** dump. They are the closest published analogue for
> **MED17.5.25** (EA888) but are **NOT** confirmed valid for 17.5.25 — DO NOT flash by
> analogy. Resolve per-file via DAMOS/A2L.

MED17.4 (Peugeot 207 RC GTI) map-location thread (different box, REPORTED leads):
https://www.ecuedit.com/med17-4-207-rc-gti-t16218

General MED17 map list (scribd, REPORTED): https://www.scribd.com/document/680584316/Lista-de-mapas-med17

## 3. Auto-mods

MED17 petrol — no diesel DPF/EGR. Diagnostic disables (cat/O2/SAI) follow codeword logic
analogous to ME7 but with EDC17-style storage; **no confirmed addresses found** in research.
CONFIDENCE: UNVERIFIED → omitted from JSON.

## 4. Open A2L/DAMOS / tooling

- medc17-checksum-tool (open): https://github.com/ConnorHowell/medc17-checksum-tool
- No open MED17.5.25 A2L found in the clear; commercial DAMOS exists via the usual vendors.
