#!/usr/bin/env python3
"""DAMOS (ASAP2 A2L) + ROM (Intel HEX) -> open_damos recipe.

Reference implementation of docs/opendamos/CONVENTION_DAMOS_VERS_OPENDAMOS.md.
For each requested CHARACTERISTIC it pulls type / address / record layout /
conversions / axes from the A2L, and reads the raw axis values (the fingerprint)
from the ROM at that address.

Handles both families that store an inline (nx, ny) dimension header
(NO_AXIS_PTS in the RECORD_LAYOUT):
  * Bosch EDC16  — big-endian (MSB_FIRST / DAMOS++ exports without RECORD_LAYOUT)
  * Bosch EDC17  — little-endian (MSB_LAST), TriCore 0x80000000 addressing

COM_AXIS characteristics (separate AXIS_PTS blocks, no inline header — PSA Valeo,
Continental SID) are skipped: they need the COM_AXIS relocation mode (see
docs/opendamos/OPENDAMOS_STANDARD.md §13), not the inline-header scan.

Usage:
    damos_a2l_to_opendamos.py <damos.a2l> <rom.hex> <out.json> <Label1,Label2,...>
"""
import re, json, sys, struct


def load_a2l(path):
    # DAMOS++ SunOS exports interleave NUL bytes; strip them.
    return open(path, 'rb').read().replace(b'\x00', b'').decode('latin1')


# TriCore/PowerPC map the same physical flash at several segment addresses
# (e.g. 0x80xxxxxx cached and 0xa0xxxxxx uncached). Normalising to the low 29
# bits collapses those mirrors so A2L addresses and ROM data line up.
ADDR_MASK = 0x1FFFFFFF


def _phys(a):
    return a & ADDR_MASK


def _image_from_mem(mem, gap=0x40000):
    """Flatten {addr: byte} into (lo, image) spanning every non-trivial flash
    cluster. Runs are split on gaps > `gap`; tiny outlier runs (junk header
    records like a dummy S1 at 0x0) are dropped, and the image covers from the
    first to the last kept run (internal gaps are 0xFF-filled)."""
    addrs = sorted(mem)
    runs = []                                   # (start, end_inclusive, bytes)
    run_start, prev, run_bytes = addrs[0], addrs[0], 1
    for a in addrs[1:]:
        if a - prev <= gap:
            run_bytes += 1
        else:
            runs.append((run_start, prev, run_bytes))
            run_start, run_bytes = a, 1
        prev = a
    runs.append((run_start, prev, run_bytes))
    total = sum(r[2] for r in runs)
    keep = [r for r in runs if r[2] >= max(4096, total * 0.005)] or runs
    lo, hi = min(r[0] for r in keep), max(r[1] for r in keep)
    img = bytearray(b'\xff' * (hi - lo + 1))
    for a, v in mem.items():
        if lo <= a <= hi:
            img[a - lo] = v
    return lo, bytes(img)


def load_ihex(path):
    """Intel HEX -> (base_addr, flat_image). Honors type 0x02/0x04 segment records."""
    mem = {}
    base = 0
    for line in open(path, 'rb'):
        line = line.strip()
        if not line or line[:1] != b':':
            continue
        b = bytes.fromhex(line[1:].decode())
        n = b[0]
        # Need length byte + addr(2) + type(1) + n data + checksum(1).
        if len(b) < 4 + n + 1:
            print(f"  ! ihex: short record (skipped)", file=sys.stderr)
            continue
        # Intel HEX checksum: two's-complement of the sum of all preceding bytes.
        if (sum(b[:4 + n]) + b[4 + n]) & 0xFF != 0:
            print(f"  ! ihex: bad checksum (skipped)", file=sys.stderr)
            continue
        addr, typ, dat = (b[1] << 8) | b[2], b[3], b[4:4 + n]
        if typ == 0:
            for k, v in enumerate(dat):
                mem[_phys(base + addr + k)] = v
        elif typ == 4:
            base = ((dat[0] << 8) | dat[1]) << 16
        elif typ == 2:
            base = ((dat[0] << 8) | dat[1]) << 4
        elif typ == 1:
            break
    return _image_from_mem(mem)


def load_srec(path):
    """Motorola S-record -> (base_addr, flat_image). Handles S1/S2/S3 data."""
    mem = {}
    alen = {'1': 2, '2': 3, '3': 4}
    for line in open(path, 'rb'):
        line = line.strip()
        if line[:1] != b'S' or len(line) < 4:
            continue
        t = chr(line[1])
        if t not in alen:
            continue
        b = bytes.fromhex(line[2:].decode())
        cnt, n = b[0], alen[t]
        # Need count byte + cnt payload bytes (cnt counts addr+data+checksum).
        if len(b) < 1 + cnt:
            print(f"  ! srec: short record (skipped)", file=sys.stderr)
            continue
        # S-record checksum: ones-complement of the sum of count + addr + data.
        if (sum(b[:cnt]) + b[cnt]) & 0xFF != 0xFF:
            print(f"  ! srec: bad checksum (skipped)", file=sys.stderr)
            continue
        addr = int.from_bytes(b[1:1 + n], 'big')
        for k, v in enumerate(b[1 + n:cnt]):          # b[cnt] is the checksum
            mem[_phys(addr + k)] = v
    return _image_from_mem(mem)


def load_rom(path):
    """Auto-detect Intel HEX (':'), Motorola S-record ('S') or a raw .bin dump.
    A raw binary is taken as a full-flash image based at 0 (A2L addresses are
    mirror-normalised to physical, so they index straight in for 0-based dumps)."""
    with open(path, 'rb') as f:
        head = f.read(1)
    if head == b'S':
        return load_srec(path)
    if head == b':':
        return load_ihex(path)
    return 0, open(path, 'rb').read()   # raw binary


def detect_byteorder(a2l):
    return 'LE' if 'MSB_LAST' in a2l else 'BE'


def parse_compu(a2l):
    """COMPU_METHOD name -> (factor, offset, unit), phys = raw*factor + offset."""
    out = {}
    for m in re.finditer(r'/begin COMPU_METHOD\s+(\S+).*?/end COMPU_METHOD', a2l, re.S):
        blk, name = m.group(0), m.group(1)
        cm = re.search(r'COEFFS\s+' + r'\s+'.join([r'([-\d.eE]+)'] * 6), blk)
        unit = re.search(r'RAT_FUNC\s+"[^"]*"\s+"([^"]*)"', blk)
        if not cm:
            out[name] = (1.0, 0.0, "")   # non-linear (TAB_INTP/FORM): identity
            continue
        a, b, c, d, e, f = (float(x) for x in cm.groups())
        if not b:
            print(f"  ! {name}: non-linear COMPU_METHOD (b==0), using identity",
                  file=sys.stderr)
        # ASAP2 RAT_FUNC stores the inverse map INT = (b*P + c) / f, so the
        # forward physical conversion is P = INT * f/b - c/b (verified empirically).
        factor = f / b if b else 1.0
        offset = -c / b if b else 0.0
        out[name] = (factor, 0.0 if offset == 0 else offset, unit.group(1) if unit else "")
    return out


_TYPE = {'SBYTE': 'SBYTE', 'UBYTE': 'UBYTE', 'SWORD': 'SWORD', 'UWORD': 'UWORD',
         'SLONG': 'SLONG', 'ULONG': 'ULONG'}


def parse_record_layouts(a2l):
    """name -> dict(inline=bool, axis=<TYPE>, fnc=<TYPE>)."""
    out = {}
    for m in re.finditer(r'/begin RECORD_LAYOUT\s+(\S+).*?/end RECORD_LAYOUT', a2l, re.S):
        blk, name = m.group(0), m.group(1)
        inline = 'NO_AXIS_PTS_X' in blk
        ax = re.search(r'AXIS_PTS_X\s+\d+\s+(\w+)', blk)
        fn = re.search(r'FNC_VALUES\s+\d+\s+(\w+)', blk)
        out[name] = dict(inline=inline,
                         axis=_TYPE.get(ax.group(1)) if ax else None,
                         fnc=_TYPE.get(fn.group(1)) if fn else None)
    return out


def dtype(base, order):
    """'SWORD' + 'LE' -> 'SWORD_LE'; bytes have no endianness suffix."""
    if base in ('SBYTE', 'UBYTE'):
        return base
    return f'{base}_{order}'


_CONT = {'u1': 'UBYTE', 's1': 'SBYTE', 'u2': 'UWORD', 's2': 'SWORD',
         'u4': 'ULONG', 's4': 'SLONG'}


def dtype_from_name(rl, order):
    if 'Ws8' in rl or 'Xs8' in rl:
        return 'SBYTE'
    if 'Wu16' in rl or 'Xu16' in rl:
        return dtype('UWORD', order)
    # Continental: type encoded as trailing tokens, e.g. _REC_A1MAP_20_u1_u1_u1
    # (axisX, axisY, value). The last token is the value/axis element type.
    toks = re.findall(r'_([usUS][124])(?=_|$)', rl)
    if toks:
        return dtype(_CONT[toks[-1].lower()], order)
    return dtype('SWORD', order)


def parse_axis_pts(a2l):
    """AXIS_PTS name -> dict(address, rl). The point count comes from the map's
    AXIS_DESCR, not from here (the AXIS_PTS field is a max-points allocation)."""
    out = {}
    for m in re.finditer(r'/begin AXIS_PTS\s+(\S+)\s+"[^"]*"\s+(0x[0-9A-Fa-f]+)\s+\S+\s+(\S+)', a2l):
        name, addr, rl = m.groups()
        out[name] = dict(address=int(addr, 16), rl=rl)
    return out


def index_chars(a2l):
    """One pass -> {name: block}. Lets a batch avoid re-scanning a huge A2L per
    characteristic (O(n) instead of O(n^2))."""
    idx = {}
    for m in re.finditer(r'/begin CHARACTERISTIC\s+(\S+).*?/end CHARACTERISTIC', a2l, re.S):
        idx.setdefault(m.group(1), m.group(0))
    return idx


def parse_char(a2l, label, compu, layouts, order, axis_pts=None, block=None):
    if block is not None:
        blk = block
    else:
        # `\s` after the name (not `\b`) so array-style names like foo[4] match
        # (a `]` has no word boundary before whitespace).
        m = re.search(r'/begin CHARACTERISTIC\s+' + re.escape(label) + r'\s.*?/end CHARACTERISTIC',
                      a2l, re.S)
        if not m:
            return None
        blk = m.group(0)
    if 'COM_AXIS' in blk:
        return parse_comaxis(blk, label, compu, layouts, order, axis_pts or {})
    h = re.search(r'/begin CHARACTERISTIC\s+' + re.escape(label) +
                  r'\s+"([^"]*)"\s+(\w+)\s+(0x[0-9A-Fa-f]+)\s+(\S+)\s+([-\d.eE]+)\s+(\S+)', blk)
    if not h:
        return None
    desc, ctype, addr, rl, _maxdiff, dcompu = h.groups()
    lay = layouts.get(rl)
    if lay and not lay['inline'] and ctype in ('MAP', 'CURVE'):
        return ('skip', 'fixed-dim (no inline header)')
    adt = dtype(lay['axis'], order) if lay and lay['axis'] else dtype_from_name(rl, order)
    fdt = dtype(lay['fnc'], order) if lay and lay['fnc'] else dtype_from_name(rl, order)
    df, doff, dunit = compu.get(dcompu, (1.0, 0.0, ""))
    axes = []
    for am in re.finditer(r'/begin AXIS_DESCR\s+\w+\s+(\S+)\s+(\S+)\s+(\d+)', blk):
        q, ac, cnt = am.groups()
        af, aoff, aunit = compu.get(ac, (1.0, 0.0, ""))
        axes.append(dict(inputQuantity=q, unit=aunit, factor=af, offset=aoff,
                         count=int(cnt), dataType=adt))
    return dict(name=label, type=ctype, description=desc, address=int(addr, 16),
                recordLayout=rl, dataType=fdt,
                data=dict(factor=df, offset=doff, unit=dunit),
                nx=axes[0]['count'] if axes else 0,
                ny=axes[1]['count'] if len(axes) > 1 else 0, axes=axes)


def parse_comaxis(blk, label, compu, layouts, order, axis_pts):
    """COM_AXIS map: data block (no header) + separate AXIS_PTS axis blocks."""
    h = re.search(r'\b(MAP|CURVE)\s+(0x[0-9A-Fa-f]+)\s+(\S+)', blk)
    if not h:
        return ('skip', 'unparsable COM_AXIS header')
    ctype, addr, rl = h.group(1), int(h.group(2), 16), h.group(3)
    lay = layouts.get(rl)
    fdt = dtype(lay['fnc'], order) if lay and lay['fnc'] else dtype_from_name(rl, order)
    desc = re.search(r'/begin CHARACTERISTIC\s+\S+\s+"([^"]*)"', blk)
    axes = []
    for am in re.finditer(r'COM_AXIS\s+(\S+)\s+(\S+)\s+(\d+).*?AXIS_PTS_REF\s+(\S+)', blk, re.S):
        inq, ac, cnt, ref = am.groups()
        ap = axis_pts.get(ref)
        if not ap:
            return ('skip', f'missing AXIS_PTS {ref}')
        alay = layouts.get(ap['rl'])
        adt = dtype(alay['axis'], order) if alay and alay['axis'] else dtype_from_name(ap['rl'], order)
        af, aoff, aunit = compu.get(ac, (1.0, 0.0, ""))
        axes.append(dict(inputQuantity=(ref if inq == 'NO_INPUT_QUANTITY' else inq),
                         unit=aunit, factor=af, offset=aoff, count=int(cnt),
                         dataType=adt, address=ap['address']))
    if not axes:
        return ('skip', 'no COM_AXIS axes')
    return dict(name=label, type=ctype, description=desc.group(1) if desc else "",
                address=addr, recordLayout=rl, dataType=fdt, comAxis=True,
                data=dict(factor=1.0, offset=0.0, unit=""),
                nx=axes[0]['count'], ny=axes[1]['count'] if len(axes) > 1 else 0, axes=axes)


class OutOfBounds(Exception):
    """Raised when a ROM read would run past the loaded image; build() catches
    this and skips the characteristic with a warning instead of crashing."""


def read_axis(img, lo, addr, count, dt):
    sz = 1 if 'BYTE' in dt else (4 if 'LONG' in dt else 2)
    le = dt.endswith('_LE')
    fmt = {'SBYTE': 'b', 'UBYTE': 'B', 'SWORD': '<h' if le else '>h',
           'UWORD': '<H' if le else '>H', 'SLONG': '<i' if le else '>i',
           'ULONG': '<I' if le else '>I'}[dt.replace('_LE', '').replace('_BE', '')]
    o = addr - lo
    if o < 0 or o + sz * count > len(img):
        raise OutOfBounds(f"axis read at 0x{addr:X} ({count}x{sz}B) out of bounds")
    return [struct.unpack_from(fmt, img, o + sz * k)[0] for k in range(count)]


def read_fp(img, lo, addr, ctype, nx, ny, dt):
    o = addr - lo
    sz = 1 if 'BYTE' in dt else (4 if 'LONG' in dt else 2)
    # Inline (nx, ny) header is stored as element-sized fields: one field for a
    # CURVE, two for a MAP. So its byte size scales with the element size.
    hdr = (2 * sz if ctype == 'MAP' else (sz if ctype == 'CURVE' else 0))
    le = dt.endswith('_LE')
    fmt = {'SBYTE': 'b', 'UBYTE': 'B', 'SWORD': '<h' if le else '>h',
           'UWORD': '<H' if le else '>H', 'SLONG': '<i' if le else '>i',
           'ULONG': '<I' if le else '>I'}[dt.replace('_LE', '').replace('_BE', '')]
    xo = o + hdr
    need = xo + sz * nx + sz * ny
    if o < 0 or need > len(img):
        raise OutOfBounds(f"fingerprint read at 0x{addr:X} out of bounds")

    def rd(off):
        return struct.unpack_from(fmt, img, off)[0]
    x = [rd(xo + sz * k) for k in range(nx)] if nx else []
    y = [rd(xo + sz * nx + sz * k) for k in range(ny)] if ny else []
    return x, y


def build(a2l_path, hex_path, labels):
    a2l = load_a2l(a2l_path)
    order = detect_byteorder(a2l)
    compu = parse_compu(a2l)
    layouts = parse_record_layouts(a2l)
    axis_pts = parse_axis_pts(a2l)
    lo, img = load_rom(hex_path)
    chars = []
    for lab in labels:
        c = parse_char(a2l, lab, compu, layouts, order, axis_pts)
        if not c:
            print(f"  ! {lab}: not found in A2L", file=sys.stderr)
            continue
        if isinstance(c, tuple):
            print(f"  - {lab}: skipped ({c[1]})", file=sys.stderr)
            continue
        # Normalise A2L addresses to physical (mirror-collapse), same as the ROM.
        c['address'] = _phys(c['address'])
        for ax in c['axes']:
            if ax.get('address') is not None:
                ax['address'] = _phys(ax['address'])
        axes = []
        try:
            if c.get('comAxis'):
                for ax in c['axes']:                   # axes live in their own blocks
                    fp = read_axis(img, lo, ax['address'], ax['count'], ax['dataType'])
                    axes.append(dict(inputQuantity=ax['inputQuantity'], unit=ax['unit'],
                                     factor=ax['factor'], offset=ax['offset'],
                                     dataType=ax['dataType'], address=f"0x{ax['address'] - lo:X}",
                                     fingerprint=fp))
            else:
                x, y = read_fp(img, lo, c['address'], c['type'], c['nx'], c['ny'], c['dataType'])
                for i, ax in enumerate(c['axes']):
                    axes.append(dict(inputQuantity=ax['inputQuantity'], unit=ax['unit'],
                                     factor=ax['factor'], offset=ax['offset'],
                                     dataType=ax['dataType'], fingerprint=(x if i == 0 else y)))
        except OutOfBounds as e:
            print(f"  - {lab}: skipped ({e})", file=sys.stderr)
            continue
        dims = ({'nx': c['nx'], 'ny': c['ny']} if c['type'] == 'MAP'
                else {'nx': c['nx']} if c['type'] == 'CURVE' else {})
        entry = dict(name=c['name'], type=c['type'], description=c['description'],
                     recordLayout=c['recordLayout'],
                     defaultAddress=f"0x{c['address'] - lo:X}", dims=dims, axes=axes,
                     data=dict(dataType=c['dataType'], factor=c['data']['factor'],
                               offset=c['data']['offset'], unit=c['data']['unit']))
        if c.get('comAxis'):
            entry['comAxis'] = True
        chars.append(entry)
    return order, lo, chars


if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(2)
    a2l, hexf, out, labels = sys.argv[1:5]
    order, lo, chars = build(a2l, hexf, labels.split(','))
    json.dump({"byteOrder": order, "characteristics": chars}, open(out, 'w'),
              indent=2, ensure_ascii=False)
    print(f"wrote {len(chars)} characteristics (byteOrder={order}) -> {out}")
