# Bosch ME7 petrol — research notes

> ME7 map **addresses are per-box** (M-box / part-number specific). The map *names and
> semantics* below are stable and well-documented; the few concrete addresses are tagged
> with their box and marked REPORTED/UNVERIFIED. Use a DAMOS/A2L or ME7 auto-detect to
> resolve addresses per file — never hardcode an address family-wide.

Richest sources (ME7 is the best-documented family by far):
- s4wiki: https://s4wiki.com/wiki/Tuning  (fetchable mirror of nefmoto knowledge)
- nefmoto wiki: http://www.nefariousmotorsports.com/wiki/index.php/ME7_Tuning_Information (TLS cert currently expired)
- nefmoto MDFUE page: http://www.nefariousmotorsports.com/wiki/index.php/Setpoint_for_air_mass_from_the_desired_torque_(MDFUE)
- KFMIOP/KFMIRL/LDRXN clarification thread: http://nefariousmotorsports.com/forum/index.php?topic=7106.0
- tuning-database ME7 guide: https://www.tuning-database.co.uk/bosch-me7-tuning-complete-technical-guide/

## 1. Checksum / security (CONFIRMED family)

ME7.1 firmware checksums are validated/corrected by the open **ME7Sum** tool
(nyetwurk). The structure (CONFIRMED via repo + nefmoto threads):

- **Main ROM checksum** + a **multipoint checksum block** (a table of region descriptors,
  each `{start,end,checksum}`), plus **CRC** blocks and an **RSA** signature on later builds.
- One multipoint region noted on ME7.1.1: **`0x0 – 0x3FFF`**.
- ME7Sum "should autodetect checksum/CRC blocks, but is known not to work on non-VAG
  Motronic bins" — i.e. block layout is VAG-specific.
- CONFIDENCE: **CONFIRMED** (open repo + multiple nefmoto threads).
- SOURCE:
  - https://github.com/nyetwurk/ME7Sum
  - http://nefariousmotorsports.com/forum/index.php?topic=447.0 (ME7Check)
  - http://nefariousmotorsports.com/forum/index.php?topic=3347.0 (ME7Sum open-source corrector)
  - https://github.com/chaoschris/ME7Sum (fork)

Tools: ME7Check (validate), ME7Sum (validate+correct, open source), ECUFix/MTX (paid).
SOURCE: http://contiman.free.fr/reprog/reprogrammation%20moteur%201.8t.pdf

## 2. Stage-1 boost/torque maps (names CONFIRMED, semantics CONFIRMED)

The "torque structure" maps — the core of any ME7 Stage-1 — corroborated across s4wiki,
nefmoto and tuning-database:

| Map | What it does | Stage-1 use |
|-----|--------------|-------------|
| **LDRXN** | "Maximum specified load" — primary boost-request limiter | raise in target RPM band; biggest difference between stock states of tune |
| **KFMIRL** | requested torque → requested load | top rows must request enough load for target boost; the map you actually raise (KFMIOP stock never exceeds ~89%) |
| **KFMIOP** | (actual) load → torque; also rlmax→torque cap | inverse of KFMIRL; keep consistent or torque intervention triggers |
| **KFLDHBN** | maximum requested pressure ratio | alt boost limiter; raise with LDRXN |
| **KFLDIMX** | max boost vs altitude / I-regulator limit | follow expected steady-state WG duty |
| **KFLDRL** | linearisation boost = f(TV) (post-PID WG duty) | calibrate for flat actual boost |
| **LDRPID / LDRQ0 / KFLDRQ2** | boost PID P/I/D terms | usually left stock |
| **KFVPDKSD / KFVPDKSE** | target pressure ratio dyn/steady | set base boost, avoid premature WG opening |
| **KFTARX / LDIATA** | IAT correction of max load / PID | taper for hot-day knock protection |

Fuelling / AFR maps: **KRKTE** (airmass→injector on-time), **MLHFM** (MAF transfer),
**KFKHFM** (MAF correction), **LAMFA** (requested lambda at high load — enrich for peak
power; ensure axis spans 50–100% load), **KFLBTS** (component-protection lambda vs EGT),
**KFLAMKR/KFLAMKRL** (knock-based enrichment).

Ignition: **KFZW / KFZW2** (base timing, VVT inactive/active), **KFZWWLNM/KFZWWLRL** (delta
timing). Cam: **KFNW**. Limiters: **NMAX/DMNFALM** (rev), **VMAX/VFMX** (speed),
**KFDLULS** (overboost protection).

CONFIDENCE: **CONFIRMED** for names + semantics (≥2 sources).
Typical Stage-1 (REPORTED, e.g. TT/Golf 1.8T 225 → ~215–290 Nm): LDRXN +,
KFMIRL + to match, KFMIOP adjusted to stay consistent, KFLDHBN + for new boost,
LAMFA enriched, KFZW trimmed for pump fuel.
SOURCE: https://s4wiki.com/wiki/Tuning , https://www.ttforum.co.uk/threads/me7-5-first-tune-parameters.1894463/ ,
https://www.tuning-database.co.uk/bosch-me7-tuning-complete-technical-guide/

### Concrete addresses (box-specific — REPORTED/UNVERIFIED)

A couple of literal addresses appeared in the wiki text, tied to a specific box; kept only
as leads, NOT family-wide:
- `ZKLAMFAW/ZKWLAFWL` ≈ `0x1C3EE`, `FKVA` ≈ `0x1A4D5`, `CWKONABG` `0x181B9`,
  `CWKONLS` `0x181BB`, `CLRHK` `0x11A87`, `CLRKA` `0x11A72`, `CDATR/CDATS` `0x18196–0x18197`,
  `CATR` `0x192CA`, `FKHABMN` `0x1937D`, ESKONF bytes `0x10C77 / 0x10C7A / 0x10C7B`.
- CONFIDENCE: **UNVERIFIED** (single wiki dump's box). SOURCE: s4wiki / nefmoto ME7 wiki (above).

## 3. Auto-mods (DTC / O2 / cat / EGT off) — codeword method (REPORTED)

ME7 emissions/diagnostic disables are done via **codewords (CW…) and ESKONF config bytes**,
not map zeroing. From the ME7 wiki (REPORTED, box-specific addresses above):

- **ESKONF** (13-byte hardware-config array): set unused component bit-pairs to `11` to
  silence false DTCs (ignition, injectors, O2 heaters, N75, SAI, VVT, EVAP).
- **CDKAT=0** disable rear-cat efficiency monitor; **CDLSH=0** disable post-cat O2;
  **CDHSH/CDHSHE=0** disable rear-O2 heater diag.
- **CWKONABG** (`0x181B9`) / **CWKONLS** (`0x181BB`, e.g. `0x33→0x11`) reconfigure O2 banks.
- **CLRHK** (`0x11A87`) disable rear-O2 closed-loop; **CLRKA** (`0x11A72`) catalyst control.
- **CATR=0** (`0x192CA`) / **TABGSS=1229** disable EGT regulation; **CDATR/CDATS=0** EGT diag.
- **CDSLS=0** SAI monitor; **CDTES/CDLDP=0** EVAP/leak; **FKHABMN=0** (`0x1937D`) cat heating.
- CONFIDENCE: **REPORTED** (method CONFIRMED across wiki; concrete addresses UNVERIFIED/box-specific).
- SOURCE: http://www.nefariousmotorsports.com/wiki/index.php/ME7_Tuning_Information , https://s4wiki.com/wiki/Tuning

> ME7 is petrol — there is no DPF/EGR-off in the diesel sense. The analogous "auto-mods"
> are cat/O2/SAI/EGT/EVAP diagnostic disables via the codewords above.

## 4. Open A2L/DAMOS / tooling

- **ME7Sum** (open checksum corrector): https://github.com/nyetwurk/ME7Sum
- **ME7Tuner / MxT** (open MAF/fuel/torque calibration tool, M-box): https://github.com/KalebKE/ME7Tuner
- s4wiki and nefmoto wiki document map *labels*; per-box addresses require a DAMOS/A2L for
  that part number (e.g. 06A906032xx for 1.8T). No single open ME7.5 A2L found in the clear.
- ME7.5 address threads (box-specific, REPORTED): https://www.ecuedit.com/bosch-me7-5-audi-1-8t-ajq-auq-t1516 ,
  https://mhhauto.com/Thread-KFMIOP-KFMIRL
