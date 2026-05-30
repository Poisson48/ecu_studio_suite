#!/usr/bin/env python3
"""
mpps_decrypt.py - MPPS V21 encrypted-asset analysis / decryption toolkit.

Targets:
  - tools/reverse/mpps_extracted/Database/DataBase.Ini   (encrypted ECU DB, ~16.7 KB)
  - tools/reverse/mpps_extracted/Drives/A0NN.Drv          (183 encrypted ECU "drivers")
  - tools/reverse/mpps_extracted/*.Key                    (KEY\0 blobs, 312 B)

STATUS (2026-05): cipher NOT cracked statically. See docs/mpps-decryption.md.
This script collects the *confirmed* structural evidence (entropy, keystream-reuse,
header map) and provides ready-to-run attack harnesses (RC4 / XOR / known-plaintext)
so the work can resume the moment the real key/keystream is recovered (e.g. via a
dynamic dump of Mpps.exe's crypto routine under Windows).

Run:  python3 tools/reverse/mpps_decrypt.py analyze
      python3 tools/reverse/mpps_decrypt.py rc4 <keyfile-or-asciistring>
      python3 tools/reverse/mpps_decrypt.py xor <keyfile-or-asciistring>
      python3 tools/reverse/mpps_decrypt.py keystream    # dump reused header keystream evidence
"""
import sys, os, glob, math, struct, collections

HERE = os.path.dirname(os.path.abspath(__file__))
EXTRACT = os.path.join(HERE, "mpps_extracted")
DB = os.path.join(EXTRACT, "Database", "DataBase.Ini")
DRV_GLOB = os.path.join(EXTRACT, "Drives", "*.Drv")


# ---------------------------------------------------------------- primitives
def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b)
    n = len(b)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def printable_ratio(b):
    if not b:
        return 0.0
    return sum(1 for x in b if x in (9, 10, 13) or 32 <= x < 127) / len(b)


def rc4(key, data, drop=0):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 255
        S[i], S[j] = S[j], S[i]
    out = bytearray()
    i = j = 0
    for _ in range(drop):
        i = (i + 1) & 255
        j = (j + S[i]) & 255
        S[i], S[j] = S[j], S[i]
    for b in data:
        i = (i + 1) & 255
        j = (j + S[i]) & 255
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 255])
    return bytes(out)


def xor_repeat(key, data):
    return bytes(data[i] ^ key[i % len(key)] for i in range(len(data)))


def load_key(arg):
    """arg is a path to a .Key file or a literal ascii string."""
    if os.path.isfile(arg):
        raw = open(arg, "rb").read()
        # strip 8-byte "KEY\0" + length header if present
        return raw[8:] if raw[:4] == b"KEY\x00" else raw
    return arg.encode()


# ---------------------------------------------------------------- commands
def cmd_analyze():
    print("== Entropy / distribution ==")
    targets = [DB] + sorted(glob.glob(DRV_GLOB))[:3] + \
              glob.glob(os.path.join(EXTRACT, "*.Key")) + \
              glob.glob(os.path.join(EXTRACT, "*.key"))
    for f in targets:
        b = open(f, "rb").read()
        body = b[8:] if b[:4] == b"KEY\x00" else b
        c = collections.Counter(body)
        exp = len(body) / 256
        chi = sum((c.get(i, 0) - exp) ** 2 / exp for i in range(256))
        print(f"  {os.path.basename(f):20s} len={len(b):6d} "
              f"entropy={entropy(body):.3f} chi2={chi:7.0f} distinct={len(c)}/256")
    print("\n  -> entropy ~7.99 + chi2~256 = uniform random = stream cipher / block cipher.")
    print("     (rules out repeating-XOR, single-byte XOR, ROT, add/sub).")

    cmd_keystream()


def cmd_keystream():
    """Document the reused-keystream evidence in the .Drv header."""
    files = sorted(glob.glob(DRV_GLOB))
    datas = [open(f, "rb").read() for f in files]
    d0 = datas[0]
    HDR = 120
    const = [i for i in range(HDR) if all(d[i] == d0[i] for d in datas)]
    var = [i for i in range(HDR) if i not in const]
    ml = min(len(d) for d in datas)
    deeper = [i for i in range(HDR, min(ml, 4000)) if all(d[i] == d0[i] for d in datas)]
    print("\n== .Drv structure (183 files) ==")
    print(f"  Fixed 120-byte header. CONSTANT offsets (same cipher byte in ALL files):")
    print(f"    {const}")
    print(f"  VARIABLE offsets (per-driver metadata): {var}")
    print(f"  Constant offsets in body 120..4000: {len(deeper)} (0 = body fully diverges)")
    print("  => Same keystream reused for every .Drv (fixed key, fixed/no IV).")
    print("     C0 XOR C1 cancels in the header (proof of reuse) but is high-entropy")
    print("     in the body because the per-driver plaintext genuinely differs.")
    # show the constant encrypted header bytes (known ciphertext, unknown plaintext)
    print("  Constant encrypted header bytes (hex):")
    hexs = "".join(f"{d0[i]:02x}" for i in const)
    print("   ", hexs)


def cmd_rc4(arg):
    key = load_key(arg)
    print(f"RC4 key = {key[:16]!r}... (len {len(key)})")
    for name, path in [("DataBase.Ini", DB), ("A001.Drv", sorted(glob.glob(DRV_GLOB))[0])]:
        ct = open(path, "rb").read()
        for drop in (0, 256, 768, 1024):
            pt = rc4(key, ct[:256], drop)
            print(f"  {name} drop={drop:4d} printable={printable_ratio(pt):.2f} {pt[:40]!r}")


def cmd_xor(arg):
    key = load_key(arg)
    print(f"XOR key = {key[:16]!r}... (len {len(key)})")
    for name, path in [("DataBase.Ini", DB), ("A001.Drv", sorted(glob.glob(DRV_GLOB))[0])]:
        ct = open(path, "rb").read()
        pt = xor_repeat(key, ct[:256])
        print(f"  {name} printable={printable_ratio(pt):.2f} {pt[:40]!r}")


USAGE = __doc__


def main():
    if len(sys.argv) < 2:
        print(USAGE)
        return
    cmd = sys.argv[1]
    if cmd == "analyze":
        cmd_analyze()
    elif cmd == "keystream":
        cmd_keystream()
    elif cmd == "rc4" and len(sys.argv) > 2:
        cmd_rc4(sys.argv[2])
    elif cmd == "xor" and len(sys.argv) > 2:
        cmd_xor(sys.argv[2])
    else:
        print(USAGE)


if __name__ == "__main__":
    main()
