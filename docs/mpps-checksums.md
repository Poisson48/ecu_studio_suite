# MPPS V21 — Check/*.dll Checksum Modules — Reverse Engineering

> Reverses the 61 `Check/Check0NN.dll` modules: the per-ECU **flash checksum /
> validation** plugins that MPPS loads to verify (and auto-correct) a ROM image
> before/after flashing.
>
> **FACT** = read directly from the (UPX-unpacked) binaries. **HYPOTHESIS** = inferred.

Tooling: UPX 4.2.4 (`upx -d`) to unpack, then `pefile` + `capstone` (x86-32).
The originals in `tools/reverse/mpps_extracted/Check/` are left UPX-packed and
untouched; analysis was done on copies unpacked under `/tmp`.

---

## 1. Common ABI of every Check DLL (FACT)

All 61 DLLs are native **C++Builder** Win32 DLLs (RogueWave STL markers
`_RWSTDMutex`, `std::bad_alloc`). Every one exports the same 5–6 symbols:

| Export | Ordinal | Signature (reconstructed) | Purpose |
|--------|---------|---------------------------|---------|
| `GetPerMission` | 1 | `int ()` | Sets a global "licensed" gate, returns 1. |
| `Checksum` | 2 | `int __stdcall (uint8_t* buf, int size, void* report, int flag)` — `ret 0x10` (4 args) | **Main entry.** Verifies/corrects the image. Returns number of checksums fixed (0 = already valid / nothing to do). |
| `GetFile` | 3 | `int __stdcall (uint8_t* buf, int size)` — `ret 8` | **ECU match test.** Returns 1 if this DLL handles the given file (matched by size + signature bytes). |
| `GetMemoryAddress` | 5 | `int (x)` | Returns its argument (address passthrough / stub). |
| `DriveDisc` | 4 | `void* ()` | Returns a pointer to an internal buffer/string. |
| `___CPPdebugHook` | 6 | — | C++Builder artifact. |

`Check008` additionally exports `@@Md5@Initialize/Finalize` and
`@@Rsa@Initialize/Finalize` (RSA-signed ECU, see §4).
`Check056` lacks `GetMemoryAddress`.

### `Checksum` wrapper logic (FACT — Check001 @ VA 0x401311)
```
if (global_gate != 0) return 0;        // permission gate (set by GetPerMission)
if (flag == 0) return 0;
return worker(buf, size, report);      // tail-call to the real algorithm
```
### `GetFile` match logic (FACT — Check001 @ VA 0x401348)
Branches on `size`; for each size class it checks 2 signature bytes at a fixed
offset. Check001 example: a 0x8000 file must have `'4'`(0x34)/`'M'`(0x4D) at
offset 0x38/0x39 (either order); a >0x10000 file the same at 0x8048/0x8049.
This is how MPPS picks the right Check DLL for a loaded ROM.

---

## 2. Algorithm family map (FACT)

`GetFile` reveals each module's supported image size(s); the worker body reveals
the algorithm. Classification of all 61 modules:

| Module(s) | Image size(s) | Algorithm (FACT) |
|-----------|---------------|------------------|
| **Check001, Check045** | 0x8000 / 0x10000 (32–64 KB) | **CRC-16/ARC** (poly 0x8005 refl = 0xA001, init 0). Table-driven. **Fully reversed → implemented in ecu-core.** |
| Check003–007, 011–017, 020, 023, 027, 033, 036, 041, 048, 055 … | 0x80000 (512 KB) | **EDC16 region checksums.** Locate an ST10/C166 descriptor by opcode signature, then sum/CRC pointed regions. Per-ECU constants. |
| Check002, 009, 010, 018, 026, 039, 047, 054 | 0x40000 (256 KB) | EDC16 256 KB variants / signature validation. |
| Check008 | 0x80000 | **MD5 + RSA signature** verification (EDC17/MED17 class). |
| Check029–032, 037, 049, 058 | 0x10000–0x20000 | Smaller ECUs / EEPROM-class. |
| Check043, 046, 068 | 0x100000–0xD0000 (1 MB+) | Tricore/large-flash ECUs. |
| Check056, 060 | 0x4000 + others | Multi-size (EEPROM + flash). |

> **Key finding:** the checksum logic is **NOT one shared algorithm.** It is
> per-ECU. Only Check001/045 use a clean, self-contained, standard CRC. The
> majority are EDC16-family "find descriptor → checksum regions" routines whose
> region tables and seeds differ per ECU and would each need individual reversing
> (tractable but 50+ separate small efforts). Per the task brief, only the
> confidently-extracted CRC-16/ARC is promoted to a real engine; the rest are
> documented here with TODO_REVERSE rather than guessed.

---

## 3. CRC-16/ARC — fully reversed (FACT) — Check001 / Check045

### 3.1 The table (FACT)
At VA `0x40A118` sits a 256-entry, 16-bit little-endian lookup table (512 bytes;
copied to the stack with `mov ecx,0x80 ; rep movsd`). Verified byte-exact against
a generated table: it **is** the standard reflected CRC-16 table for
**polynomial 0xA001 (= reversed 0x8005)**:
```
table[0]=0x0000  table[1]=0xC0C1  table[2]=0xC181  table[128]=0xA001  table[255]=0x4040
```
This is CRC-16/ARC (a.k.a. CRC-16/IBM, the unreflected MODBUS family), **init =
0x0000, no final XOR, input & output reflected.**

### 3.2 The worker (FACT — Check001 @ VA 0x40143C)
Two code paths selected by `size`:

**32 KB path (`size == 0x8000`):**
* Stored checksum is read **big-endian** from offset `0x7FEA` (hi) / `0x7FEB`
  (lo): `stored = buf[0x7FEA]<<8 | buf[0x7FEB]`.
* CRC is computed over **`buf[0x30 .. 0x7FEA)`** — start offset 0x30, exactly
  `0x3FDD` loop iterations × 2 bytes = `0x7FBA` bytes ending at 0x7FEA.
* Per byte: `idx=(crc ^ byte)&0xFF ; crc=(crc>>8) ^ table[idx]` (classic
  table-driven reflected CRC-16). Two bytes processed per loop iteration.
* If `computed != stored`: write `computed` back **big-endian** to
  0x7FEA/0x7FEB, set report flag `[+0x4B0]=1`, increment the return counter.
  (So `Checksum()` **patches the image in place** and returns the fix count.)
* The report struct (`arg report`) is also filled: `[+0]=0x30` (start),
  `[+0x190]=0x7FE9` (last data offset), `[+0x320]=0x7FEA` (checksum offset),
  `[+0x640]=1` (done flag).

**64 KB path (`size == 0x10000`):**
* Same CRC; stored checksum at `0xFFFA`/`0xFFFB` (BE); region starts at
  `buf[0x8041]` (base 1 + 0x8040), report `[+0]=0x8040`, `[+0x320]=0xFFFA`.

### 3.3 Reference reimplementation (validated against the extracted table)
```c
uint16_t crc16_arc(const uint8_t* p, size_t n) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < n; i++) {
        crc = (crc >> 8) ^ TABLE[(crc ^ p[i]) & 0xFF];   // TABLE = poly 0xA001
    }
    return crc;                                          // compare BE vs stored
}
// 32KB MPPS layout:  region = [0x0030, 0x7FEA),  stored @ 0x7FEA (big-endian)
// 64KB MPPS layout:  region = [0x8041, 0xFFFA),  stored @ 0xFFFA (big-endian)
```
This is implemented for real as `ecu::ChecksumEngine` in
`libs/ecu-core/ecu/ChecksumEngine.{hpp,cpp}` with a unit test in
`tests/unit/test_checksum_engine.cpp`. The CRC core is table-generated at compile
time from poly 0xA001 and asserted to match the table[1]/[128]/[255] signature
values extracted from the binary.

---

## 4. RSA/MD5 modules (FACT) — Check008

Check008 exports `@@Md5@Initialize/Finalize`, `@@Rsa@Initialize/Finalize`. Its
worker (VA 0x40167E) loads an embedded constant blob, searches the image for a
25-byte (`0x19`) marker pattern (`call 0x402b46` = a byte-pattern search), then
MD5-hashes a region and RSA-verifies a signature. This is the EDC17/MEDC17 family
where Bosch added RSA-signed flash. **Not implemented** — it needs the embedded
RSA public key + the exact hashed region extracted, and (for *writing* a modified
ROM) it cannot be forged anyway without the private key. Documented as
`TODO_REVERSE` only.

---

## 5. EDC16 "descriptor" modules (FACT, partial) — Check003 representative

Check003 (512 KB) worker (VA 0x401400): scans from offset `0x21100` for the
Infineon C166/ST10 opcode signature `E6 F0 B0 00` / `E6 F4 …` / `E6 F5 …`
(`E6 Rw,#data16` = `MOV` — confirms the EDC16 ST10 core), reads region
start/length words from that descriptor (e.g. fields at +0x16/+0x17 and +0xA/+0xB,
plus a `+0x10000` base), copies/relocates bytes, then checksums the region. The
exact summation (additive vs CRC) and the per-ECU offsets differ across the
~50 "custom" modules. These are **individually reversible** with the same
capstone method shown here, but each is a separate small task; they are left as
documented TODO_REVERSE rather than implemented speculatively.

---

## 6. DLL → ECU mapping (HYPOTHESIS / blocked)

`Database/DataBase.Ini` and `Drives/A0NN.Drv` are **encrypted** (high entropy,
keyed by the `*.Key` files whose header is `KEY\0`), so the human-readable
"Check007 = Bosch EDC16U34" mapping is not directly recoverable here. What IS
known per module (FACT) is the **image size class** and **signature bytes** (§2,
table). `Summary.Dat` confirms the suite handles e.g. `EDC16C31`. Full mapping
needs either the decrypted DB (key reversing) or correlating each DLL's `GetFile`
signature against known ROM dumps — handoff item.

---

## 8. EDC16C34 2 MB (PSA 1.6 HDi) — Check025 reverse + dump validation (FACT)

Investigated specifically because `Mpps.Cfg` has `[Last Car] Ecu=25` and we hold two
genuine 2 MB EDC16C34 dumps (`ori.BIN`, `9663944680.Bin`) to use as an oracle.

### 8.1 Is Check025 the EDC16C34 module? — **NOT for a 2 MB image (FACT).**
`Check025.dll` (28160 B, UPX) unpacks to a C++Builder DLL exporting the standard
`Checksum/GetFile/GetMemoryAddress/...` ABI (§1). Its `GetFile` size-matcher
(VA 0x401395) accepts **only** these image sizes and sets an algorithm-mode global
`[0x40c108]`:

| size | bytes | mode | signature check |
|------|-------|------|-----------------|
| `0x23020` / `0x23021` | 143392/3 | 0 / 1 | 2 bytes @ 0x9000 |
| `0x80000` | 512 KB | 2 | 2 bytes @ 0x10010 |
| `0x100000` | 1 MB | 3 | 2 bytes @ 0x10010 |
| `0xB0010`-class | ~720 KB | 4 | 2 bytes @ 0xB0010 |

**`0x200000` (2 MB) is not an accepted size.** The `Checksum` dispatcher
(VA 0x401311) switches on the same mode global; the mode-2 worker (VA 0x401572)
additionally **requires `size >= 0x80000`** and **masks every region address with
`0x7FFFF`** (i.e. a ≤512 KB address space), and reads its region descriptor tables
from **fixed file offsets 0x3EF4 and 0x30180**. In both 2 MB dumps those offsets are
empty (0x00/0xFF) and the mode-2/3 descriptor scan (VA 0x402cb1, step 0x7000 from
0xFFD0 looking for `[+2]==1,[+3]==0`) finds **zero** hits. So Check025 operates on the
**512 KB / 1 MB program image MPPS reads in boot mode**, not on a 2 MB full-flash dump.
`Ecu=25` in `Mpps.Cfg` is a UI menu index, and `DataBase.Ini` is encrypted (§6), so
"Check025 == EDC16C34 2 MB" is **not** established — and is contradicted by the sizes.

### 8.2 Check025 algorithm (FACT, for its 512 KB/1 MB images)
The per-region primitive (VA 0x402e46) is **CRC-16 with reflected polynomial
`0xA001`** (CRC-16/ARC / Modbus family), processed byte-by-byte over inclusive
`[start..end]` ranges taken from an in-ROM descriptor table; the result is compared
to a stored big-endian word and patched back if different. Same CRC core as
Check001/045 (§3) but driven by ROM-resident region tables instead of fixed offsets.

### 8.3 The two 2 MB dumps (FACT)
* `ori.BIN`: full read — program/code `0x10000-0xD0000` (PowerPC-looking opcodes)
  **plus** calibration `0x1C0000-0x200000`.
* `9663944680.Bin`: **calibration-only** — bytes `0..0x1C0000` are entirely FF/00;
  only `0x1C0000-0x200000` is populated. Different vehicle (calib bytes differ).
  → The two dumps do **not** share a checksummed program region, so they cannot both
  validate a program-region algorithm; only the calibration region overlaps.

### 8.4 The EDC16C34 checksum-descriptor table (FACT — empirically found in both)
Both dumps embed a self-describing descriptor table. Each block ends with the 8-byte
magic **`FA DE CA FE CA FE AF FE`**. A `len==4` descriptor lays out as (big-endian):
```
… [CK:4] [subid:2] [flags:2] [00 00 00 04] [start:4] [end:4]  FA DE CA FE CA FE AF FE
```
where `[start..end]` is a flash region and `CK` is its stored 32-bit checksum.
Confirmed entries:

| region | CK (ori.BIN) | CK (9663944680.Bin) |
|--------|--------------|---------------------|
| `0x010000..0x03F97F` | `0x164600C5` | (program absent) |
| `0x040000..0x0FFFFF` | `0xBC444A75` | (program absent) |
| `0x160000..0x1BFFFF` | `0xBC674B2D` | (program absent) |
| **`0x1C0000..0x1FDFFF` (calibration)** | **`0xBC5269AC`** | **`0xBE536AA7`** |

The calibration entry is present and distinct in **both** files → it is the only
usable cross-dump oracle. (A by-byte diff confirms the volatile bytes between the two
dumps are exactly the ASCII part-number @0x1C0010, the CK dword @0x1C0028, and the
sub-block addresses — consistent with `CK` being the calibration checksum.)

### 8.5 Algorithm search — **NOT recovered (FACT, honest negative).**
Computed over `[0x1C0000..0x1FDFFF]` against the stored `CK` for both dumps, **none**
of the following matched (with/without the 4 self-bytes zeroed, several end bounds,
descriptor area excluded):
* 8/16/32-bit additive sums (BE & LE), their negations, word/dword XOR;
* `zlib` CRC-32 and its bit-inverse;
* a table-driven sweep of CRC-32 polynomials {0x04C11DB7, 0x1EDC6F41, 0xA833982B,
  0xF4ACFB13/AUTOSAR, 0x000000AF/XFER, 0x32583499} × init{0,~0} × xorout{0,~0} ×
  {MSB,LSB-first}.

The three program CKs share a high byte `0xBC`, hinting at an additive component, but
no tested closed form reproduces the stored values. Bosch EDC16 uses a proprietary
region checksum here; recovering it needs the 512 KB-image worker of the correct
boot-mode module disassembled to its summation core (its descriptor tables live at the
fixed offsets 0x3EF4/0x30180 of the *program* image, absent from a 2 MB dump) **or** a
known-good corrected ROM to diff. **It is NOT guessed.**

### 8.6 Engine decision — **FAIL-SAFE (no behaviour change to correction).**
`mppsLayoutForSize(0x200000)` already returns `Unknown`, so `verifyMpps`/`correctMpps`
return `nullopt` and the engine **never writes a checksum into a 2 MB EDC16C34 image.**
Added only a non-mutating detector `ecu::isEdc16c34TwoMb()` (size 0x200000 + presence
of `kEdc16c34ChecksumMagic`) so the UI can show a clear "checksum algorithm
unsupported for this ECU — image left untouched" message instead of silently doing
nothing. Unit tests assert detection works **and** that correction is still refused.

**What is needed to finish (handoff):** the exact summation core of the EDC16C34
512 KB/1 MB boot-mode checksum module (disassemble its mode-2 worker around the
0x3EF4/0x30180 descriptor reads down to the per-region accumulate), or one
genuine + one re-checksummed 2 MB pair to diff the calibration region against.

---

## 9. Summary

* **Confident & implemented:** CRC-16/ARC (Check001/045), 32 KB & 64 KB layouts →
  `ecu::ChecksumEngine`.
* **EDC16C34 2 MB:** descriptor table + magic `FA DE CA FE CA FE AF FE` and the
  calibration region `0x1C0000-0x1FDFFF` with its stored 32-bit `CK` identified and
  validated in both genuine dumps, **but the checksum primitive was NOT recovered**
  (no sum/CRC variant matches). Engine kept **fail-safe** (detect-only, never
  corrects) — see §8.
* **Identified, not implemented (per brief — no guessing):** ~50 per-ECU EDC16
  region checksums (Check003-family), the RSA/MD5 module (Check008).
* **Blocked:** DLL→ECU friendly-name map (encrypted DB).
