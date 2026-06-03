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


def dtype_from_name(rl, order):
    if 'Ws8' in rl or 'Xs8' in rl:
        return 'SBYTE'
    if 'Wu16' in rl or 'Xu16' in rl:
        return dtype('UWORD', order)
    return dtype('SWORD', order)


def parse_axis_pts(a2l):
    """AXIS_PTS name -> dict(address, rl). The point count comes from the map's
    AXIS_DESCR, not from here (the AXIS_PTS field is a max-points allocation)."""
    out = {}
    for m in re.finditer(r'/begin AXIS_PTS\s+(\S+)\s+"[^"]*"\s+(0x[0-9A-Fa-f]+)\s+\S+\s+(\S+)', a2l):
        name, addr, rl = m.groups()
        out[name] = dict(address=int(addr, 16), rl=rl)
    return out


def parse_char(a2l, label, compu, layouts, order, axis_pts=None):
    m = re.search(r'/begin CHARACTERISTIC\s+' + re.escape(label) + r'\b.*?/end CHARACTERISTIC',
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
    fdt = dtype(lay['fnc'], order) if lay and lay['fnc'] else dtype('UWORD', order)
    desc = re.search(r'/begin CHARACTERISTIC\s+\S+\s+"([^"]*)"', blk)
    axes = []
    for am in re.finditer(r'COM_AXIS\s+(\S+)\s+(\S+)\s+(\d+).*?AXIS_PTS_REF\s+(\S+)', blk, re.S):
        inq, ac, cnt, ref = am.groups()
        ap = axis_pts.get(ref)
        if not ap:
            return ('skip', f'missing AXIS_PTS {ref}')
        alay = layouts.get(ap['rl'])
        adt = dtype(alay['axis'], order) if alay and alay['axis'] else dtype('SWORD', order)
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


def read_axis(img, lo, addr, count, dt):
    sz = 1 if 'BYTE' in dt else (4 if 'LONG' in dt else 2)
    le = dt.endswith('_LE')
    fmt = {'SBYTE': 'b', 'UBYTE': 'B', 'SWORD': '<h' if le else '>h',
           'UWORD': '<H' if le else '>H', 'SLONG': '<i' if le else '>i',
           'ULONG': '<I' if le else '>I'}[dt.replace('_LE', '').replace('_BE', '')]
    o = addr - lo
    return [struct.unpack_from(fmt, img, o + sz * k)[0] for k in range(count)]


def read_fp(img, lo, addr, ctype, nx, ny, dt):
    o = addr - lo
    sz = 1 if 'BYTE' in dt else (4 if 'LONG' in dt else 2)
    hdr = 4 if ctype == 'MAP' else (2 if ctype == 'CURVE' else 0)
    le = dt.endswith('_LE')
    fmt = {'SBYTE': 'b', 'UBYTE': 'B', 'SWORD': '<h' if le else '>h',
           'UWORD': '<H' if le else '>H', 'SLONG': '<i' if le else '>i',
           'ULONG': '<I' if le else '>I'}[dt.replace('_LE', '').replace('_BE', '')]

    def rd(off):
        return struct.unpack_from(fmt, img, off)[0]
    xo = o + hdr
    x = [rd(xo + sz * k) for k in range(nx)] if nx else []
    y = [rd(xo + sz * nx + sz * k) for k in range(ny)] if ny else []
    return x, y


def build(a2l_path, hex_path, labels):
    a2l = load_a2l(a2l_path)
    order = detect_byteorder(a2l)
    compu = parse_compu(a2l)
    layouts = parse_record_layouts(a2l)
    axis_pts = parse_axis_pts(a2l)
    lo, img = load_ihex(hex_path)
    chars = []
    for lab in labels:
        c = parse_char(a2l, lab, compu, layouts, order, axis_pts)
        if not c:
            print(f"  ! {lab}: not found in A2L", file=sys.stderr)
            continue
        if isinstance(c, tuple):
            print(f"  - {lab}: skipped ({c[1]})", file=sys.stderr)
            continue
        axes = []
        if c.get('comAxis'):
            for ax in c['axes']:                       # axes live in their own blocks
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
