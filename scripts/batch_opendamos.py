#!/usr/bin/env python3
"""Batch DAMOS -> OpenDAMOS over a .rar pack, ONE ECU at a time.

For each convertible ECU dir (A2L + ROM), deduped by family:
  unrar-extract just that dir -> auto-select distinctive maps -> build recipe ->
  relocate on its own ROM (via /tmp/reloc_bin) -> if it relocates, keep the recipe
  under ressources/<ecu>/ -> DELETE the extracted raw files -> next.

Peak disk stays at one ECU (a few hundred MB), never the full 40 GB.
Resumable: an ECU whose recipe already exists is skipped.
"""
import importlib.util, re, json, os, time, subprocess, glob, shutil, sys

ROOT = '/home/valou/leo/ecu_studio_suite'
spec = importlib.util.spec_from_file_location("conv", ROOT + "/scripts/damos_a2l_to_opendamos.py")
conv = importlib.util.module_from_spec(spec); spec.loader.exec_module(conv)

ARCHIVE = '/home/valou/leo/damos_pack_2020.rar'
WORK = '/tmp/ecu_work'
RELOC = '/tmp/reloc_bin'
RES = ROOT + '/ressources'
LOG = '/tmp/batch_opendamos.log'
WANT = 14          # target maps per recipe
MIN_OK = 4         # keep a recipe only if >= this many maps relocate


def log(m):
    line = f"[{time.strftime('%H:%M:%S')}] {m}"
    print(line, flush=True)
    with open(LOG, 'a') as f:
        f.write(line + "\n")


def slug(s):
    return re.sub(r'[^a-z0-9]+', '_', s.lower()).strip('_')


def ecu_id(dirname):
    base = dirname.split('/')[-1]
    s = base.upper()
    fam = None
    for pat in [r'SIMOS\s*([\d.]+)', r'MEDC?17[._]?([\d.]+)', r'\bMG1[A-Z0-9]+', r'\bMD1[A-Z0-9]+',
                r'EDC17[A-Z0-9]*', r'EDC16[A-Z0-9]*', r'EDC15[A-Z0-9]*', r'\bME7[._]?[\d.]*',
                r'\bMED9[._]?[\d.]*', r'\bDQ\d+', r'8HP\d*', r'\bSID\d+', r'\bDELPHI\w*', r'\bTEMIC\w*']:
        m = re.search(pat, s)
        if m:
            fam = m.group(0); break
    me = re.search(r'(\d)[,.](\d)\s*(TFSI|TDI|TSI|FSI|HDI|DCI|CDTI|CRD|BITDI)', s)
    eng = f"{me.group(1)}{me.group(2)}{me.group(3)}" if me else ''
    if fam:
        return slug(fam + ('_' + eng if eng else ''))
    return slug(base)[:40]


def distinctive(c, lo, img):
    """True if the map has monotonic distinctive axes and non-flat data."""
    try:
        if c.get('comAxis'):
            axv = [conv.read_axis(img, lo, conv._phys(a['address']), a['count'], a['dataType'])
                   for a in c['axes']]
        else:
            x, y = conv.read_fp(img, lo, conv._phys(c['address']), 'MAP', c['nx'], c['ny'], c['dataType'])
            axv = [x, y]
        sz = 1 if 'BYTE' in c['dataType'] else (4 if 'LONG' in c['dataType'] else 2)
        hdr = 0 if c.get('comAxis') else (2 * sz)
        off = conv._phys(c['address']) - lo + hdr
        n = c['nx'] * c['ny']
        if off < 0 or off + n * sz > len(img):
            return False
        blk = img[off:off + n * sz]
    except Exception:
        return False
    def mono(v):
        return len(v) >= 6 and all(v[k] < v[k + 1] for k in range(len(v) - 1)) and (v[-1] - v[0]) > 30
    return all(mono(v) for v in axv) and len(set(blk)) >= 8 and c['nx'] * c['ny'] >= 36


def collect_layouts(chars, order):
    rl = {}
    for c in chars:
        n = c.get('recordLayout')
        if not n or n in rl:
            continue
        e = {"type": c['type'], "byteOrder": order,
             "headerBytes": 0 if c.get('comAxis') else (4 if c['type'] == 'MAP' else 2),
             "dataType": c['data']['dataType']}
        if c.get('comAxis'):
            e["axisMode"] = "COM_AXIS"
        rl[n] = e
    return rl


def relocate_count(recipe_path, img):
    """Write the flat ROM + run the C++ relocation harness; return (matched, total)."""
    binp = f'/tmp/_batch_rom_{os.getpid()}.bin'
    open(binp, 'wb').write(img)
    try:
        out = subprocess.run([RELOC, binp, recipe_path], capture_output=True, text=True, timeout=240).stdout
    except Exception:
        return 0, 0
    m = re.search(r'=>\s*(\d+)/(\d+)\s+by fingerprint', out)
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def process(item):
    WORK = f'/tmp/ecu_work_{os.getpid()}'      # per-worker dir (3 run in parallel)
    eid = ecu_id(item['dir'])
    outdir = f"{RES}/{eid}"
    if os.path.exists(f"{outdir}/open_damos.json"):
        return ('skip-exists', eid, 0, 0)
    # extract just this ECU dir
    shutil.rmtree(WORK, ignore_errors=True); os.makedirs(WORK, exist_ok=True)
    r = subprocess.run(['unrar', 'x', '-y', '-idq', ARCHIVE, item['dir'] + '/*', WORK + '/'],
                       capture_output=True, text=True, timeout=600)
    a2ls = glob.glob(WORK + '/**/*.a2l', recursive=True) + glob.glob(WORK + '/**/*.A2L', recursive=True)
    roms = sum((glob.glob(WORK + '/**/*.' + e, recursive=True)
                for e in ('s19', 'S19', 'hex', 'HEX', 'bin', 'BIN', 'ori')), [])
    if not a2ls or not roms:
        shutil.rmtree(WORK, ignore_errors=True)
        return ('no-pair', eid, 0, 0)
    A2L = a2ls[0]; ROM = sorted(roms, key=lambda f: ({'s19': 0, 'hex': 1, 'bin': 2}.get(f.rsplit('.', 1)[-1].lower(), 3)))[0]
    try:
        a2l = conv.load_a2l(A2L); order = conv.detect_byteorder(a2l)
        compu = conv.parse_compu(a2l); layouts = conv.parse_record_layouts(a2l); axpts = conv.parse_axis_pts(a2l)
        lo, img = conv.load_rom(ROM)
        idx = conv.index_chars(a2l)
        sel = []
        for name, blk in idx.items():
            c = conv.parse_char(a2l, name, compu, layouts, order, axpts, block=blk)
            if ('[' not in name and isinstance(c, dict) and c['type'] == 'MAP'
                    and len(c.get('axes', [])) >= 2 and distinctive(c, lo, img)):
                sel.append(name)
                if len(sel) >= WANT:
                    break
        if len(sel) < MIN_OK:
            shutil.rmtree(WORK, ignore_errors=True)
            return ('few-maps', eid, len(sel), 0)
        order, lo2, chars = conv.build(A2L, ROM, sel)
        recipe = {"$schema": "../open_damos.schema.json", "ecu": eid,
                  "name": f"open_damos — {item['dir'].split('/')[-1]}",
                  "version": "0.1.0", "license": "CC0-1.0", "byteOrder": order,
                  "baseline": {"source": os.path.basename(ROM), "damos": os.path.basename(A2L),
                               "note": f"Auto-generated from the 2020 DAMOS pack. Family dir: {item['dir']}"},
                  "recordLayouts": collect_layouts(chars, order),
                  "characteristics": [dict(c, category="auto") for c in chars]}
        os.makedirs(outdir, exist_ok=True)
        rp = f"{outdir}/open_damos.json"
        json.dump(recipe, open(rp, 'w'), indent=2, ensure_ascii=False)
        matched, total = relocate_count(rp, img)
        if matched < MIN_OK:
            os.remove(rp); os.rmdir(outdir) if not os.listdir(outdir) else None
            shutil.rmtree(WORK, ignore_errors=True)
            return ('no-reloc', eid, total, matched)
        shutil.rmtree(WORK, ignore_errors=True)
        return ('OK', eid, total, matched)
    except Exception as e:
        shutil.rmtree(WORK, ignore_errors=True)
        return ('error:' + str(e)[:60], eid, 0, 0)


def main():
    import collections
    from multiprocessing import Pool
    workers = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    work = json.load(open('/tmp/worklist.json'))
    seen, todo = set(), []
    for w in work:
        eid = ecu_id(w['dir'])
        if eid in seen:
            continue
        seen.add(eid); todo.append(w)
    log(f"=== batch start: {len(work)} dirs -> {len(todo)} unique families, {workers} parallel workers ===")
    stats = collections.Counter()
    done = 0
    with Pool(workers) as pool:
        for status, eid, total, matched in pool.imap_unordered(process, todo):
            done += 1
            key = 'error' if status.startswith('error') else status
            stats[key] += 1
            if status == 'OK':
                log(f"[{done}/{len(todo)}] OK    {eid:34s} {matched}/{total} maps")
            elif key not in ('skip-exists',):
                log(f"[{done}/{len(todo)}] {status:11s} {eid:34s}")
    log(f"=== DONE: {dict(stats)} ===")


if __name__ == '__main__':
    main()
