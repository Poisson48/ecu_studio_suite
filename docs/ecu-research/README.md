# ECU Tuning Research Database — ECU Studio

> Structured, source-cited research to extend `libs/ecu-core/src/EcuCatalog.cpp`.
> **PROPOSAL ONLY.** Nothing here has been written into `EcuCatalog.cpp`.
> Wrong addresses brick ECUs. Every datum below carries a SOURCE and a CONFIDENCE.
> If an address could not be sourced, it is **omitted**, not invented.

Last updated: 2026-05-31

---

## 1. Methodology

Research was done entirely via public web search + fetch. For every datum we record:

- **SOURCE** — the URL (and, where relevant, the specific forum post / repo file).
- **CONFIDENCE** — one of:
  - **CONFIRMED** — corroborated by ≥2 independent sources, *or* taken from source code
    / an open A2L/DAMOS, *or* already present and validated in our own `EcuCatalog.cpp`.
  - **REPORTED** — stated by one credible source (a known tuning forum, a tuning guide
    PDF, a public repo) but not independently cross-checked, **or** cross-checked but
    known to be firmware/box-specific (the address is real for *that* dump only).
  - **UNVERIFIED** — mentioned in passing, ambiguous, or from a single low-trust source.
    Treated as a research lead, never as a flashable value.

### Critical caveats discovered during research

1. **Map addresses are firmware/box-specific.** A "KFMIRL" address valid for one
   `.bin` is *not* valid for another build of the same ECU family. Every concrete
   address below is tagged with the exact dump/box/part-number it came from where known.
   These belong in the catalog only as *per-variant* entries keyed to a specific
   software version, or as DAMOS/A2L-driven label lookups — **not** as family-wide constants.
2. **EDC16 checksum is NOT one algorithm.** Our existing catalog comment says
   "EDC16 = CRC-16/ARC". Research did **not** corroborate CRC-16/ARC for PSA EDC16C34.
   PSA EDC16C34 uses a **1024-bit RSA signature + 32-bit additive (sum32) checksums**
   (see [edc16.md](edc16.md)). VAG/Siemens EDC16 variants differ again. The CRC-16/ARC
   claim should be re-examined against the actual region our `ChecksumEngine` validates.
3. **"EGR off by zeroing maps" largely does NOT work on EDC16** (unlike EDC15). The
   ecuedit admin states this directly. EDC16 EGR-off needs control-bit / hysteresis-map
   tricks, and is variant-specific. Treat any EDC16 EGR address as REPORTED at best.

---

## 2. Master ECU table

Coverage legend: ✓ = sourced data captured in this research; — = stub only (no public data found yet);
(existing) = already populated in `EcuCatalog.cpp` before this research.

| id | Name | Family | Fuel | Stage1 maps | Checksum | Auto-mods (DPF/EGR/DTC) | A2L/DAMOS link |
|----|------|--------|------|:-----------:|----------|:-----------------------:|----------------|
| edc16c34   | EDC16C34   | EDC16 | diesel | ✓ (existing) | RSA1024 + sum32 (REPORTED) | DTC ✓, EGR/DPF REPORTED | DTCController repo (tool) |
| edc16c39   | EDC16C39   | EDC16 | diesel | — | (unknown) | — | — |
| edc16c3    | EDC16C3    | EDC16 | diesel | — | (unknown) | — | — |
| edc16u31   | EDC16U31   | EDC16 | diesel | — | additive/Siemens (UNVERIFIED) | EGR REPORTED (method, no addr) | autodtc damos pack (paid) |
| edc16u34   | EDC16U34   | EDC16 | diesel | — | additive/Siemens (UNVERIFIED) | EGR/DTC REPORTED | autodtc damos pack (paid) |
| edc16cp31  | EDC16CP31  | EDC16 | diesel | — | (unknown) | — | — |
| edc16cp35  | EDC16CP35  | EDC16 | diesel | — | (unknown) | — | — |
| edc16c2    | EDC16C2    | EDC16 | diesel | — | (unknown) | — | — |
| edc16c35   | EDC16C35   | EDC16 | diesel | — | (unknown) | — | — |
| *generic EDC16 (guide)* | EDC16 guide bin | EDC16 | diesel | ✓ REPORTED (box-specific) | — | — | EDC16 Tuning Guide v1.1 |
| edc17c10   | EDC17C10   | EDC17 | diesel | — | CRC32/ADD32/ADD16 (CONFIRMED) | — | psa-seedkey repo (key) |
| edc17c46   | EDC17C46   | EDC17 | diesel | — | CRC32/ADD32/ADD16 (CONFIRMED) | EGR/DPF/DTC REPORTED (files) | — |
| edc17c54   | EDC17C54   | EDC17 | diesel | — | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |
| edc17c60   | EDC17C60   | EDC17 | diesel | — | CRC32/ADD32/ADD16 (CONFIRMED) | — | psa-seedkey repo (key) |
| edc17cp14  | EDC17CP14  | EDC17 | diesel | ◐ REPORTED (box-specific) | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |
| edc17cp20  | EDC17CP20  | EDC17 | diesel | — | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |
| *EDC17 VAG 2.0 TDI (guide)* | 03L906022xx | EDC17 | diesel | ✓ REPORTED (box-specific) | — | DPF switch REPORTED | EDC17 Tuning Guide |
| me7.4.4    | ME7.4.4    | ME7   | petrol | ✓ REPORTED (map names) | multipoint + main + RSA (CONFIRMED) | DTC/lambda via codewords REPORTED | s4wiki / nefmoto wiki |
| me7.5      | ME7.5      | ME7   | petrol | ✓ REPORTED (map names + 1 box addr) | multipoint + main + RSA (CONFIRMED) | codewords REPORTED | s4wiki / nefmoto wiki |
| *me7.1.1*  | ME7.1.1    | ME7   | petrol | ✓ REPORTED (map names) | multipoint 0x0–0x3fff etc (CONFIRMED) | codewords REPORTED | nefmoto / ME7Sum repo |
| med17.5.25 | MED17.5.25 | MED17 | petrol | ◐ REPORTED (analogous to 17.5.5) | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |
| med17.5.5  | MED17.5.5  | MED17 | petrol | ✓ REPORTED (2 box addrs) | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |
| med17.1    | MED17.1    | MED17 | petrol | — | CRC32/ADD32/ADD16 (CONFIRMED) | — | — |

◐ = partial / by-analogy only.

### Confidence summary (this research)

- **CONFIRMED** data points: checksum *families* (EDC17/MED17 = CRC32+ADD32+ADD16,
  corroborated by 2 repos + forum; ME7 = multipoint+main+RSA, corroborated by ME7Sum repo
  + nefmoto; EDC16C34 PSA = RSA1024+sum32, corroborated by digital-kaos + reflashecu + repo);
  PSA seed-keys; EDC16C34 DTC table region (corroborated by DTCController repo + our catalog).
- **REPORTED** data points: all concrete *map addresses* (every one is box-specific),
  EGR/DPF/DTC-off methods, ME7 map semantics and tuning percentages.
- **UNVERIFIED**: VAG/Siemens EDC16 checksum type (additive assumed, not confirmed);
  Marelli/Delphi/Siemens-SID checksum specifics (tool support confirmed, algorithm not).

---

## 3. Per-family detail files

- [edc16.md](edc16.md) — EDC16 diesel (incl. EDC16C34 checksum/RSA, VAG EGR notes, guide map list)
- [edc17.md](edc17.md) — EDC17 diesel (checksum, map addresses from guide, EGR/DPF/DTC)
- [me7.md](me7.md)   — ME7 petrol (KFMIOP/KFMIRL/LDRXN etc, checksum, codeword auto-mods)
- [med17.md](med17.md) — MED17 petrol (checksum, KFMIOP/KFMIRL addresses)
- [ecu_catalog_proposed.json](ecu_catalog_proposed.json) — structured proposal mirroring the C++ schema

---

## 4. Richest sources (ranked)

1. **nefariousmotorsports.com ME7 wiki / forum** + **s4wiki.com/wiki/Tuning** — by far the
   deepest ME7 map semantics (every map's function + how to tune it). s4wiki mirror is
   fetchable; the nefmoto wiki itself currently has an expired TLS cert.
2. **EDC16 Tuning Guide v1.1** and **EDC17 Tuning Guide** (PDF, on pdfcoffee/scribd) —
   concrete map addresses *with dimensions and factors*, tied to specific dumps.
3. **GitHub: ConnorHowell/medc17-checksum-tool**, **nyetwurk/ME7Sum**,
   **JeanLucPons/DTCController**, **ludwig-v/psa-seedkey-algorithm** — source-grade
   checksum/security/DTC detail.
4. **digital-kaos.co.uk** + **reflashecu.com** — EDC16C34 PSA RSA/sum32 checksum specifics.
5. **ecuedit.com** + **mhhauto.com** forums — map addresses and EGR/DPF-off threads
   (many require login; treat as REPORTED, often box-specific).

Full URLs are cited inline in each family file.
