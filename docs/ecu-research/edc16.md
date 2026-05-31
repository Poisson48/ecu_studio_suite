# EDC16 diesel — research notes

> Every address is **box-specific** unless stated otherwise. The "EDC16 Tuning Guide"
> addresses below come from one specific dump and must NOT be treated as family-wide.

## 1. Checksum / security

### EDC16C34 (PSA — Peugeot/Citroën/Ford 1.6 HDi/TDCi)

**This is the important correction to our catalog's "CRC-16/ARC" comment.**
PSA EDC16C34 is protected by a **1024-bit RSA signature plus 32-bit additive checksums**,
not a plain CRC-16/ARC over the map zone.

- RSA modulus + public exponent (3) live in the **code zone**.
- The **1024-bit signature is at `0x1FDF7C`** (MAP zone), readable over OBD.
- The last 32-bit control word **`@ 0x1FDFFC`** = `-0xD01FE500 - sum32[0x1FDF7C..0x1FDFFB]`,
  i.e. a negated 32-bit sum of the signature region, with the constant `0xD01FE500`
  observed fixed across original-dump collections.
- CONFIDENCE: **REPORTED**, but corroborated across two sources (digital-kaos thread +
  multiple summarised forum posts) and consistent with the DTCController repo note that
  the ECU checks "two 32-bit checksums" plus "both MD5 and RSA signature".
- SOURCE:
  - https://www.digital-kaos.co.uk/forums/archive/index.php/t-965883.html ("EDC16C34 checksum algorithm")
  - https://www.digital-kaos.co.uk/forums/showthread.php/965883-EDC16C34-checksum-algorithm
  - https://reflashecu.com/en/docs/edc16c34-psa/ (BSM bench pinout; confirms PSA platform + factory files)
  - https://github.com/JeanLucPons/DTCController (repo states ECU checks two 32-bit checksums + MD5 + RSA)

> ACTION for ECU Studio: verify what region/algorithm `ChecksumEngine.cpp` currently
> applies to EDC16C34. If it applies CRC-16/ARC it is almost certainly wrong for PSA dumps.

### VAG / Siemens EDC16 (EDC16U31, EDC16U34, EDC16C3…)

- General community statements describe additive 16-bit / multi-region checksums for VAG
  EDC16; some discussion of RSA shortcuts. **No single algorithm confirmed** in research.
- CONFIDENCE: **UNVERIFIED** (do not encode an algorithm without reversing a real dump).
- SOURCE: https://mhhauto.com/Thread-EDC16-Checksum ,
  https://www.ecuedit.com/checksum-edc15-edc16-t17128 ,
  https://www.evc.de/en/service/q1304.asp (FAQ: programmers use RSA shortcuts to compute EDC16 checksum)

## 2. Seed-key (security access) — PSA

From the open `ludwig-v/psa-seedkey-algorithm` repo (`ECU_KEYS.md`):

| ECU | KEY | CONFIDENCE | SOURCE |
|-----|-----|-----------|--------|
| EDC16C34 | `475A` | REPORTED | https://github.com/ludwig-v/psa-seedkey-algorithm/blob/main/ECU_KEYS.md |
| EDC17C10 | `1905` | REPORTED | same |
| EDC17CP11 | `1812` | REPORTED | same |
| EDC17C60 | `3102` | REPORTED | same |
| ME745 (ME7.4.5 PSA petrol) | `F8F3` | REPORTED | same |

(Seed-keys are diagnostic-access constants, not map data; useful for the `application` field
and for read/write tooling, not for flashing maps.)

## 3. DTC table — EDC16C34 (CONFIRMED region)

The open-source `DTCController` (Java, JeanLucPons) operates on the EDC16C34 internal DTC
code table. This corroborates the kind of region our existing catalog edits.

- DTC code table runs from **`0x1C65AE`** (first code, P0530 — A/C) to **`0x1C6732`**
  (last code, P1621 — WdCom), **2 bytes per entry**.
- Example entries: P0530 @ `0x1C65AE`; misfire P0301–P0306 @ `0x1C661E`–`0x1C662A`.
- CONFIDENCE: **CONFIRMED** (source-grade: an open repo; consistent with our existing
  `dpf_dtc_off` @ `0x1E9DD4` being a *separate* DTC-status byte, not the code table).
- SOURCE: https://github.com/JeanLucPons/DTCController

> Note: our existing catalog `egr_off @ 0x1C41B8` and `dpf_dtc_off @ 0x1E9DD4` for
> EDC16C34 are NOT corroborated by any public source found here — keep them as-is /
> UNVERIFIED until re-checked against a real dump.

## 4. EGR-off — EDC16 (method only; addresses NOT family-stable)

**Key honest finding:** the old EDC15 trick of zeroing EGR maps does **not** reliably work
on EDC16. The ecuedit administrator states plainly: "For MSA15 or EDC15 Yes. For EDC16 No."

- Working approach reported (EDC16U31, Audi A4 1.9 TDI BKE): set MAF target tables to ~1500 mg
  and drop EGR control duty cycle to ~15% at upper RPM; result MAF read ~1200 mg, no DTCs.
  Better practice is to operate on EGR **control bits**, not duty-cycle maps.
  - CONFIDENCE: **REPORTED** (single thread, no hex addresses given).
  - SOURCE: https://www.ecuedit.com/disabling-egr-edc16u31-t232
- EDC16C34 (Ford Focus 1.6 TDCi 109hp): enable diagnostic mode, find two **1×12 EGR
  hysteresis maps**, fill with zeros, correct checksum, save.
  - CONFIDENCE: **REPORTED** (single doc, no hex addresses in the public excerpt).
  - SOURCE: https://www.scribd.com/document/384997388/EDC16c34
- General VAG 1.9 TDI EDC16 EGR-delete walkthrough (WinOLS): zero EGR maps + DTC off.
  - SOURCE: https://www.youtube.com/watch?v=pFE3O0iXj10
- DPF-off / EGR-off VAG EDC16U31/U34 free file references:
  - https://www.ect-download.com/en/dpf-off-egr-off-vw-edc16u31-edc16u34/
  - https://obdtotal.com/product/vw-passat-b6-1-9-tdi-bosch-edc16u34-03g906021lr-egr-dtc-p0401-off/

**No EDC16 EGR/DPF hex address was published in any source found → omitted from JSON.**

## 5. Stage-1 map addresses — from "EDC16 Tuning Guide v1.1" (one specific dump)

These are concrete and useful as *labels/semantics*, but the addresses belong to the
guide's specific bin (a VAG-style EDC16). **REPORTED, box-specific. Do not apply blindly.**

SOURCE: https://pdfcoffee.com/edc16-tuning-guide-version-11-pdf-free.html
(also https://pdfcoffee.com/edc16-tuning-guide-pdf-free.html)

| Map | Addr | Dim | Fmt | Factor | What it does |
|-----|------|-----|-----|--------|--------------|
| Driver's Wish (req torque) | `0x1C2F96` | 8×16 | 16-bit HiLo | 0.1 | required torque vs RPM × pedal |
| Torque Limiter | `0x1D43C2` | 21×3 | 16-bit HiLo | 0.1 | caps output per RPM × atmos pressure |
| Nm→IQ conversion | `0x1D6F9A` | 16×15 | 16-bit HiLo | 0.1 | requested Nm → injection qty |
| Boost map | `0x1EAD92` | 10×16 | 16-bit HiLo | 1 | turbo pressure vs torque × RPM |
| Boost limiter | `0x1EAF00` | 10×11 | 16-bit HiLo | 1 | limits boost vs atmos pressure |
| Smoke limiter | `0x1D6188` | 12×16 | 16-bit HiLo | 0.01 | limits IQ vs airmass × RPM |
| Single-value boost limiter | `0x1EB04A` | 1×1 | — | — | absolute turbo pressure cap (2350 mBar stock) |
| Injection duration | `0x1E4F90` | 15×19 | 16-bit HiLo | 0.023437 | crank degrees for injection |

Typical Stage-1 (REPORTED, generic diesel practice): smoke limiter +15–25%, Nm→IQ +10–15%,
torque limiter +15–25%, boost / boost-limiter +5–15%, single-value boost cap raised.

## 6. Open A2L/DAMOS availability

- EDC16 + EDC17 DAMOS/map pack (846 projects incl. EDC16C34, EDC16U34) — **paid/commercial**:
  https://autodtc.net/ecu-item/damos-and-map-pack/damos-for-edc16-and-edc17-database/
  and https://autodtc.net/damos-id-edc16-vag-ecu/
- PSA EDC16C34 DAMOS thread (forum): https://www.ecuedit.com/psa-edc16c34-damos-t1153
- XDF for EDC16U1 (TunerPro) discussion: https://www.ecuedit.com/xdf-for-edc16u1-help-t2475
- No fully open, downloadable EDC16C34 A2L/DAMOS was found in the clear.
