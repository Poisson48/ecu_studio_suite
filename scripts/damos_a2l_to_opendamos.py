#!/usr/bin/env python3
"""DAMOS (ASAP2 A2L) + ROM (Intel HEX) -> open_damos recipe.

Reference implementation of docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md.
For each requested CHARACTERISTIC it pulls type / address / record layout /
conversions / axes from the A2L, and reads the raw axis values (the fingerprint)
from the ROM at that address. The emitted recipe validates against
ressources/open_damos.schema.json and relocates by fingerprint onto any firmware
of the same family.

Usage:
    damos_a2l_to_opendamos.py <damos.a2l> <rom.hex> <out.json> <Label1,Label2,...>

Notes:
  * COMPU_METHOD RAT_FUNC "0 b 0 0 0 f" (linear) -> phys = raw*(f/b) - c/b.
  * Only inline-header Bosch layouts (Kf/Kl, big-endian) are handled here; group
    maps (_GMAP) and separate-axis (COM_AXIS) ECUs need a different reader.
"""
import re, json, sys, struct


def load_a2l(path):
    # DAMOS++ SunOS exports interleave NUL bytes; strip them.
    return open(path, 'rb').read().replace(b'\x00', b'').decode('latin1')


def load_ihex(path):
    """Intel HEX -> (base_addr, flat_image). Honors type 0x02/0x04 segment records."""
    mem = {}
    base = 0
    for line in open(path, 'rb'):
        line = line.strip()
        if not line or line[:1] != b':':
            continue
        b = bytes.fromhex(line[1:].decode())
        n, addr, typ, dat = b[0], (b[1] << 8) | b[2], b[3], b[4:4 + b[0]]
        if typ == 0:
            for k, v in enumerate(dat):
                mem[base + addr + k] = v
        elif typ == 4:
            base = ((dat[0] << 8) | dat[1]) << 16
        elif typ == 2:
            base = ((dat[0] << 8) | dat[1]) << 4
        elif typ == 1:
            break
    lo, hi = min(mem), max(mem)
    img = bytearray(b'\xff' * (hi - lo + 1))
    for a, v in mem.items():
        img[a - lo] = v
    return lo, bytes(img)


def parse_compu(a2l):
    """COMPU_METHOD name -> (factor, offset, unit), phys = raw*factor + offset."""
    out = {}
    for m in re.finditer(r'/begin COMPU_METHOD\s+(\S+).*?/end COMPU_METHOD', a2l, re.S):
        blk, name = m.group(0), m.group(1)
        cm = re.search(r'COEFFS\s+' + r'\s+'.join([r'([-\d.eE]+)'] * 6), blk)
        unit = re.search(r'RAT_FUNC\s+"[^"]*"\s+"([^"]*)"', blk)
        if not cm:
            continue
        a, b, c, d, e, f = (float(x) for x in cm.groups())
        factor = f / b if b else 1.0
        offset = -c / b if b else 0.0
        if offset == 0:
            offset = 0.0  # normalise -0.0
        out[name] = (factor, offset, unit.group(1) if unit else "")
    return out


def dtype_of(rl):
    if 'Ws8' in rl or 'Xs8' in rl:
        return 'SBYTE'
    if 'Wu16' in rl:
        return 'UWORD_BE'
    return 'SWORD_BE'


def parse_char(a2l, label, compu):
    m = re.search(r'/begin CHARACTERISTIC\s+' + re.escape(label) + r'\b.*?/end CHARACTERISTIC',
                  a2l, re.S)
    if not m:
        return None
    blk = m.group(0)
    h = re.search(r'/begin CHARACTERISTIC\s+' + re.escape(label) +
                  r'\s+"([^"]*)"\s+(\w+)\s+(0x[0-9A-Fa-f]+)\s+(\S+)\s+([-\d.eE]+)\s+(\S+)', blk)
    if not h:
        return None
    desc, ctype, addr, rl, _maxdiff, dcompu = h.groups()
    df, doff, dunit = compu.get(dcompu, (1.0, 0.0, ""))
    axes = []
    for am in re.finditer(r'/begin AXIS_DESCR\s+\w+\s+(\S+)\s+(\S+)\s+(\d+)', blk):
        q, ac, cnt = am.groups()
        af, aoff, aunit = compu.get(ac, (1.0, 0.0, ""))
        axes.append(dict(inputQuantity=q, unit=aunit, factor=af, offset=aoff, count=int(cnt)))
    return dict(name=label, type=ctype, description=desc, address=int(addr, 16),
                recordLayout=rl, dataType=dtype_of(rl),
                data=dict(factor=df, offset=doff, unit=dunit),
                nx=axes[0]['count'] if axes else 0,
                ny=axes[1]['count'] if len(axes) > 1 else 0, axes=axes)


def read_fp(img, lo, addr, ctype, nx, ny, dt):
    o = addr - lo
    sz = 1 if 'BYTE' in dt else 2
    hdr = 4 if ctype == 'MAP' else (2 if ctype == 'CURVE' else 0)

    def rd(off):
        if dt == 'SBYTE':
            v = img[off]
            return v - 256 if v >= 128 else v
        if dt == 'UBYTE':
            return img[off]
        return struct.unpack_from('>h' if dt == 'SWORD_BE' else '>H', img, off)[0]
    xo = o + hdr
    x = [rd(xo + sz * k) for k in range(nx)] if nx else []
    y = [rd(xo + sz * nx + sz * k) for k in range(ny)] if ny else []
    return x, y


def build(a2l_path, hex_path, labels):
    a2l = load_a2l(a2l_path)
    compu = parse_compu(a2l)
    lo, img = load_ihex(hex_path)
    chars = []
    for lab in labels:
        c = parse_char(a2l, lab, compu)
        if not c:
            print(f"  ! {lab}: not found in A2L", file=sys.stderr)
            continue
        x, y = read_fp(img, lo, c['address'], c['type'], c['nx'], c['ny'], c['dataType'])
        axes = []
        for i, ax in enumerate(c['axes']):
            axes.append(dict(inputQuantity=ax['inputQuantity'], unit=ax['unit'],
                             factor=ax['factor'], offset=ax['offset'],
                             dataType=c['dataType'], fingerprint=(x if i == 0 else y)))
        dims = ({'nx': c['nx'], 'ny': c['ny']} if c['type'] == 'MAP'
                else {'nx': c['nx']} if c['type'] == 'CURVE' else {})
        chars.append(dict(name=c['name'], type=c['type'], description=c['description'],
                          recordLayout=c['recordLayout'],
                          defaultAddress=f"0x{c['address']:X}", dims=dims, axes=axes,
                          data=dict(dataType=c['dataType'], factor=c['data']['factor'],
                                    offset=c['data']['offset'], unit=c['data']['unit'])))
    return chars


if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(2)
    a2l, hexf, out, labels = sys.argv[1:5]
    chars = build(a2l, hexf, labels.split(','))
    json.dump({"characteristics": chars}, open(out, 'w'), indent=2, ensure_ascii=False)
    print(f"wrote {len(chars)} characteristics -> {out}")
