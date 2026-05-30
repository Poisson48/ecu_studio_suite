# MPPS V21 — Decryption of `DataBase.Ini` and `*.Drv`

**Goal:** decrypt MPPS V21's encrypted per-ECU flash definitions
(`Database/DataBase.Ini` = ECU catalogue, `Drives/A0NN.Drv` = 183 flash drivers)
to recover MPPS's full ECU coverage.

**Result (2026-05): NOT cracked statically.** The cipher is conclusively identified
as a **stream cipher with a fixed, reused keystream** (same key, no per-file IV), but
the key is not derivable from the on-disk material without dumping the crypto routine
from the packed `Mpps.exe`. Everything tried is documented below with honest results.
Confidence statements are explicit; no fabricated decryptions.

Tooling: `tools/reverse/mpps_decrypt.py` (entropy/keystream analysis + RC4/XOR harnesses).

---

## 1. Material

| File | Size | State |
|---|---|---|
| `Database/DataBase.Ini` | 16 697 B | **encrypted** (ECU catalogue, expected INI text) |
| `Drives/A0NN.Drv` × 183 | 29–64 KB | **encrypted** (per-ECU flash drivers, binary) |
| `41363130.Key`, `Mpps.key`, `MultiBoot.Key`, `TPBoot.Key` | 312 B | `KEY\0` blobs |
| `DtcCodes/*.Ini`, `Language/*.lang`, `Mpps.Cfg`, `Tricore.Cfg` | — | **plaintext** INI |
| `Mpps.exe` | 3.1 MB | Delphi/BCB, **custom-packed** (EP in high-entropy `.data`) |
| `Check/Check0NN.dll` × 68 | ~26 KB | UPX-packed; unpack OK; checksum validators |

Only the *valuable IP* (ECU DB + flash drivers) is encrypted; DTC text and language
files are plain. So the encryption is a deliberate content-protection layer.

### `.Key` file format
```
00: 4B 45 59 00            "KEY\0" magic
04: 20 01 00 00            length = 0x120 = 288 (body bytes that follow)
08: <288 bytes, entropy ~7.30 bits/byte>   ← itself encrypted/obfuscated
```
The 288-byte body is **not** a plain RC4 key and **not** a 256-byte S-box permutation
(only ~178/256 distinct values). It is almost certainly an RSA-/proprietary-wrapped
blob that `Mpps.exe` unwraps at runtime to produce the real symmetric key. The
`41363130` filename is the dongle hardware serial (confirmed by `Summary.Dat`:
`[Device] 0=41363130`).

---

## 2. Entropy & distribution analysis

`python3 tools/reverse/mpps_decrypt.py analyze`

| File | entropy (bits/byte) | χ² vs uniform | distinct bytes |
|---|---|---|---|
| DataBase.Ini | 7.989 | 247 | 256/256 |
| A001.Drv | 7.995 | 264 | 256/256 |
| A002.Drv | 7.996 | 256 | 256/256 |
| *.Key body | 7.29–7.34 | ~250 | ~180/256 |

The encrypted assets are **maximal-entropy and uniformly distributed** (χ² ≈ 256 with
256 bins is exactly the expectation for uniform random data). This **rules out**:

- repeating-key XOR / Vigenère (would leave histogram structure + periodicity),
- single-byte XOR, byte add/sub mod 256, ROT/nibble-swap (all preserve the very
  non-uniform histogram of INI text),
- any transform that preserves plaintext byte statistics.

**Autocorrelation** of `DataBase.Ini` at all shifts 1..256: peaks ≈ 91 vs random
expectation ≈ 65 — i.e. **no periodicity** → not a repeating key.

**ECB test:** 0 duplicated 8-byte or 16-byte blocks in `DataBase.Ini`.

**Block-alignment test:** the 183 `.Drv` sizes cover **all residues mod 8 and mod 16**
→ files are **not** block-multiples → **not a raw block cipher** (ECB/CBC without
ciphertext stealing). ⇒ The cipher is a **stream cipher** (RC4 / PRNG-XOR / custom
byte cipher), or a block cipher in a streaming mode (CFB/OFB/CTR).

---

## 3. KEY FINDING — fixed reused keystream across all `.Drv`

`python3 tools/reverse/mpps_decrypt.py keystream`

All 183 `.Drv` share an **identical 120-byte encrypted header** in the constant
offsets below; only a handful of header bytes vary per file:

```
CONSTANT offsets : 0-7, 10-11, 16, 19, 24-118   (same cipher byte in ALL 183 files)
VARIABLE offsets : 8, 9, 12-15, 17-18, 20-23, 119   (per-driver metadata)
Body 120..EOF    : fully diverges between files (no shared constant offset)
```

`A001.Drv XOR A002.Drv` is **zero across the constant header** (first 8 bytes + a
96-byte run) and high-entropy elsewhere. For a stream cipher `C = P ⊕ KS`,
`C0 ⊕ C1 = P0 ⊕ P1`; the header cancels to zero ⇒ **same keystream AND same header
plaintext** in those positions. This proves:

> **The same keystream is reused for every `.Drv` (fixed key, fixed or no IV).**

The body XOR is high-entropy not because the keystream differs but because the
per-driver binary plaintext genuinely differs. The variable header offsets are
per-driver metadata (size/ID/CRC) encrypted with that same fixed keystream.

This is the strongest lever: **any single known-plaintext `.Drv` (or its 120-byte
header plaintext) instantly yields the keystream for that whole region, decrypting the
headers of all 183 drivers.** A *full* known-plaintext file would expose the keystream
for the entire file length.

What I could NOT do: recover the header plaintext. I tested whether the variable
header bytes encode the file size (LE/BE, 16/32-bit, at every offset 0..40) — **no
match**, so the header is not a trivially-guessable size field. Without a guessable
header structure, the keystream stays unknown.

---

## 4. Every attack tried (and its result)

| # | Attack | Key material | Result |
|---|---|---|---|
| 1 | Repeating-key XOR | each `.Key` body; filename | ✗ (ruled out by entropy + no autocorrelation) |
| 2 | Single-byte XOR / add / sub | all 256 | ✗ (entropy) |
| 3 | ROT / nibble-swap | — | ✗ (histogram preserved) |
| 4 | RC4, key = `.Key` body / full / first-256 | 4 key files | ✗ printable ≈ 0.40 (random) |
| 5 | RC4, key = ascii `41363130`, `MPPS`, `Mpps`, … | serials | ✗ |
| 6 | RC4, key = MD5/SHA1/SHA256(serial) | hashed serials | ✗ |
| 7 | RC4 with KSA drop 256/768/1024/3072 | all above | ✗ |
| 8 | RC4 brute, all printable keys len 1–3 + wordlist | exhaustive | ✗ (best printable 0.69 = noise) |
| 9 | `.Key` body as RC4 initial S-box | 4 key files | ✗ (body is not a permutation) |
| 10 | Delphi LCG XOR (`seed·0x08088405+1`), seeds 0–2000 + serials, 3 byte-taps | — | ✗ (best 0.57/128B) |
| 11 | Known-plaintext: header = filesize (LE/BE,16/32-bit) every offset | — | ✗ no match |
| 12 | Two-time-pad: `C0⊕C1` zero-run search across 183 files | — | only the header cancels (§3) |
| 13 | DataBase.Ini ⊕ A001.Drv (shared keystream?) | — | high-entropy ⇒ DB header plaintext ≠ Drv header plaintext (or different key) |

(A "hit" would be a decrypted block that is clearly INI text — `[Make]`, `key=value`,
CRLF. The best printable ratio achieved was ~0.69, indistinguishable from random; a
real INI decrypt would be ≥0.97. No attack produced valid plaintext.)

### Static binary analysis
- `Mpps.exe`: Delphi/BCB (`.adata`/`.itext`/`.didata` sections), **custom-packed** —
  entry point at `0xab3000` in the high-entropy `.data` section, a huge zero-raw
  `.didata` (0x8b2000 vbytes) is the unpack target. Import table stubbed to 1 function
  per DLL (`GetModuleHandleA`, `DrawIconEx`, `RegSetValueExW`, `ImageList_Write`) —
  real imports resolved at runtime. No AES S-box / Te-table / Rcon / TEA-delta
  (0x9e3779b9) / Blowfish-π / CRC32 poly / `KEY\0` constant found in the packed image.
- `MultiBoot.exe`, `Tricore Boot.exe`: same custom packer.
- `Check/*.dll`: UPX — unpacked cleanly with `upx -d`. Searched all unpacked DLLs for
  AES/MD5/SHA/TEA/Blowfish/CRC32 constants → **none**. These are per-ECU checksum
  validators (`GetFileCheckSum`-style), not the asset decryptor.

⇒ The crypto routine lives only inside the packed `Mpps.exe` and is not recoverable
without unpacking it.

---

## 5. Conclusion & most-promising lead

**Cipher identified:** stream cipher (RC4 or a custom PRNG-XOR), **fixed key, reused
keystream** across all `.Drv`. **Key: not recovered.** The `.Key` blobs are themselves
wrapped and are unwrapped at runtime by `Mpps.exe`.

**Blocking lead / what's needed (in priority order):**

1. **Dynamic dump of `Mpps.exe` under Windows.** Run MPPS under x64dbg/Ghidra-debugger
   (or Frida), break after the custom packer's OEP, then either
   (a) hook `advapi32!CryptDecrypt` / `CryptDeriveKey` (if it uses CryptoAPI), or
   (b) set a memory-read breakpoint on a `.Drv` buffer right after `ReadFile` and trace
   the routine that transforms it — dump the key/keystream from registers/stack.
   This is the highest-value, most reliable path and would unlock everything.
2. **Recover the reused keystream via better-guessed `.Drv` header plaintext.** The
   keystream is *fixed* (§3): one known plaintext driver, or a correct guess of the
   120-byte header layout (magic + version + offsets), yields the keystream for that
   region for all 183 files. Useful corpora: a leaked decrypted MPPS driver, or a
   newer/older MPPS build whose drivers are plaintext for diffing.
3. **Unwrap the `.Key` blob.** If the 288-byte body is RSA-wrapped, the public modulus
   would be embedded in `Mpps.exe` (recoverable after step 1); the unwrapped result is
   the symmetric key for steps above.

Until step 1 is done, the MPPS ECU catalogue inside `DataBase.Ini` cannot be read, and
the `EcuCatalog` integration is blocked on it (no fabricated catalogue is provided).

---

## 6. Repro

```bash
pip install pycryptodome pefile --break-system-packages
python3 tools/reverse/mpps_decrypt.py analyze      # entropy + keystream evidence
python3 tools/reverse/mpps_decrypt.py keystream     # .Drv header constant/variable map
python3 tools/reverse/mpps_decrypt.py rc4 tools/reverse/mpps_extracted/41363130.Key
python3 tools/reverse/mpps_decrypt.py xor 41363130
```

(Binaries are gitignored; only this doc + the script are tracked.)
