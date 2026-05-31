#!/usr/bin/env python3
"""EDC16C34 2 MB checksum validation harness.

Validates candidate checksum algorithms for the Bosch EDC16C34 (PSA 1.6 HDi)
2 MB flash against two GENUINE factory dumps. SAFETY-CRITICAL: a wrong checksum
bricks the ECU, so an algorithm is only accepted if computed == stored for BOTH
dumps. This script currently reports a NEGATIVE result (no tested algorithm
matches) — see docs/mpps-checksums.md section 8.

The dumps are NOT committed; point DUMP_DIR at a local copy.

Findings encoded here (FACT):
  * Each EDC16C34 checksum descriptor block ends with the 8-byte magic
        FA DE CA FE CA FE AF FE
  * A len==4 descriptor is laid out big-endian as:
        [CK:4][subid:2][flags:2][00 00 00 04][start:4][end:4] MAGIC
    where [start..end] is a flash region and CK is its stored 32-bit checksum.
  * The calibration region 0x1C0000..0x1FDFFF is present in BOTH dumps and is the
    only usable cross-dump oracle (one dump is calibration-only).
"""
import os
import re
import struct
import zlib

DUMP_DIR = os.environ.get(
    "EDC16C34_DUMP_DIR",
    "/home/valou/leo/open_car_reprog/ressources/edc16c34",
)
MAGIC = b"\xfa\xde\xca\xfe\xca\xfe\xaf\xfe"


def parse_descriptors(buf):
    """Yield (ck, start, end) for every len==4 checksum descriptor."""
    for m in re.finditer(re.escape(MAGIC), buf):
        e = m.start()
        if e < 20:
            continue
        length = struct.unpack(">I", buf[e - 12:e - 8])[0]
        if length != 4:
            continue
        start = struct.unpack(">I", buf[e - 8:e - 4])[0]
        end = struct.unpack(">I", buf[e - 4:e])[0]
        ck = struct.unpack(">I", buf[e - 20:e - 16])[0]
        if start < end < len(buf):
            yield ck, start, end


# ---- candidate algorithms over a byte range [start, end] inclusive ----------

def _rev(x, n):
    r = 0
    for _ in range(n):
        r = (r << 1) | (x & 1)
        x >>= 1
    return r


def _crc32_table(poly, reflect):
    if reflect:
        rp = _rev(poly, 32)
        t = []
        for i in range(256):
            c = i
            for _ in range(8):
                c = (c >> 1) ^ rp if c & 1 else c >> 1
            t.append(c & 0xFFFFFFFF)
        return t
    t = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
        t.append(c)
    return t


def _crc32(data, poly, init, reflect, xorout):
    t = _crc32_table(poly, reflect)
    crc = init
    if reflect:
        for b in data:
            crc = (crc >> 8) ^ t[(crc ^ b) & 0xFF]
    else:
        for b in data:
            crc = ((crc << 8) & 0xFFFFFFFF) ^ t[((crc >> 24) ^ b) & 0xFF]
    return crc ^ xorout


def candidates(seg):
    out = {}
    out["sum8"] = sum(seg) & 0xFFFFFFFF
    for tag, fmt in (("BE", ">H"), ("LE", "<H")):
        s = 0
        for i in range(0, len(seg) & ~1, 2):
            s += struct.unpack(fmt, seg[i:i + 2])[0]
        out["sumw" + tag] = s & 0xFFFFFFFF
    for tag, fmt in (("BE", ">I"), ("LE", "<I")):
        s = 0
        for i in range(0, len(seg) & ~3, 4):
            s += struct.unpack(fmt, seg[i:i + 4])[0]
        out["sumd" + tag] = s & 0xFFFFFFFF
    out["crc32"] = zlib.crc32(bytes(seg)) & 0xFFFFFFFF
    out["crc32inv"] = (zlib.crc32(bytes(seg)) ^ 0xFFFFFFFF) & 0xFFFFFFFF
    for name, poly in (("0x04C11DB7", 0x04C11DB7), ("0x1EDC6F41", 0x1EDC6F41),
                       ("0xA833982B", 0xA833982B), ("AUTOSAR", 0xF4ACFB13)):
        for init in (0x00000000, 0xFFFFFFFF):
            for refl in (False, True):
                for xo in (0x00000000, 0xFFFFFFFF):
                    out["crc[%s,i%08x,%s,x%08x]" % (name, init, refl, xo)] = \
                        _crc32(seg, poly, init, refl, xo)
    return out


def main():
    dumps = []
    for fn in ("ori.BIN", "9663944680.Bin"):
        p = os.path.join(DUMP_DIR, fn)
        if os.path.exists(p):
            dumps.append((fn, open(p, "rb").read()))
    if not dumps:
        print("No dumps found in", DUMP_DIR, "- set EDC16C34_DUMP_DIR.")
        return

    # The cross-dump oracle is the calibration region.
    CALIB = (0x1C0000, 0x1FDFFF)
    any_match = False
    for fn, buf in dumps:
        print("==== %s (size=%d) ====" % (fn, len(buf)))
        descs = list(parse_descriptors(buf))
        for ck, s, e in descs:
            print("  descriptor region [0x%06x..0x%06x] CK=0x%08x" % (s, e, ck))
        # validate against the calibration descriptor
        cand_ck = next((ck for ck, s, e in descs if (s, e) == CALIB), None)
        if cand_ck is None:
            print("  (no calibration descriptor found)")
            continue
        seg = buf[CALIB[0]:CALIB[1] + 1]
        results = candidates(seg)
        hits = [k for k, v in results.items() if v == cand_ck]
        print("  calib CK=0x%08x  matching algorithms: %s"
              % (cand_ck, hits or "NONE"))
        any_match = any_match or bool(hits)

    print()
    print("RESULT:", "an algorithm matched" if any_match
          else "NO tested algorithm reproduces the stored checksum -> "
               "engine stays FAIL-SAFE (detect-only, never corrects).")


if __name__ == "__main__":
    main()
