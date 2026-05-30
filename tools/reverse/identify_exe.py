#!/usr/bin/env python3
"""
identify_exe.py — Analyse rapide d'un exe pour guider le reverse engineering
Usage : python3 identify_exe.py MPPS_V21.exe
"""

import sys
from pathlib import Path

def identify(exe_path: str):
    path = Path(exe_path)
    if not path.exists():
        print(f"[ATTENTE] {exe_path} pas encore fourni — relancer quand l'exe arrive")
        return

    data = path.read_bytes()
    print(f"Taille : {len(data):,} bytes ({len(data)/1024/1024:.1f} MB)")

    # ── Détection packer ────────────────────────────────────────────────────
    markers = {
        b'UPX0':      'UPX (dépackable avec : upx -d)',
        b'UPX1':      'UPX (dépackable avec : upx -d)',
        b'Themida':   'Themida — protection lourde, nécessite snapshot mémoire',
        b'VMProtect': 'VMProtect — protection très lourde',
        b'ASProtect': 'ASProtect',
        b'PELock':    'PELock',
    }
    for marker, desc in markers.items():
        if marker in data:
            print(f"[PACKER] {desc}")

    # ── Détection compilateur ───────────────────────────────────────────────
    langs = {
        b'Borland': 'Delphi/Borland → utiliser IDR pour reconstruction',
        b'TForm':   'Delphi VCL → IDR reconstruit les formulaires',
        b'TButton': 'Delphi VCL',
        b'mscoree': '.NET → utiliser dnspy pour décompilation directe en C#',
        b'_CorExe': '.NET → utiliser dnspy',
        b'MSVBVM':  'Visual Basic 6',
        b'Qt5Core': 'Qt5 C++',
        b'Qt6Core': 'Qt6 C++',
    }
    detected_lang = None
    for marker, desc in langs.items():
        if marker in data:
            print(f"[LANG] {desc}")
            detected_lang = desc

    # ── DLLs importées ──────────────────────────────────────────────────────
    try:
        import pefile
        pe = pefile.PE(exe_path)
        print(f"\n[SECTIONS] {[s.Name.decode(errors='replace').strip() for s in pe.sections]}")
        if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
            dlls = [e.dll.decode(errors='replace') for e in pe.DIRECTORY_ENTRY_IMPORT]
            print(f"[IMPORTS DLLs] {dlls}")
            if any('ftd2xx' in d.lower() for d in dlls):
                print("\n⭐ FTDI D2XX API détectée (ftd2xx.dll)")
                print("   → Stratégie : DLL proxy ftd2xx.dll pour intercepter FT_Write/FT_Read")
            if any('libusb' in d.lower() for d in dlls):
                print("\n⭐ libusb détectée → hook libusb_bulk_transfer")
            if any('hidapi' in d.lower() or 'hid.dll' in d.lower() for d in dlls):
                print("\n⭐ HID API détectée → hook HidD_GetFeature / HidD_SetFeature")
    except ImportError:
        print("[pefile non installé] pip install pefile")
    except Exception as e:
        print(f"[pefile erreur] {e}")

    # ── Strings protocole ───────────────────────────────────────────────────
    print("\n[STRINGS PROTOCOLE]")
    keywords = [b'kline', b'k-line', b'K-Line', b'KWP', b'CAN', b'baud',
                b'timeout', b'INIT', b'READ', b'WRITE', b'FLASH', b'ERASE',
                b'EDC16', b'EDC17', b'ME7', b'checksum', b'0x68', b'0x81']
    found = set()
    for i in range(0, len(data) - 4):
        for kw in keywords:
            if data[i:i+len(kw)] == kw:
                start = max(0, i - 20)
                end   = min(len(data), i + 60)
                chunk = data[start:end]
                printable = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                if printable not in found:
                    found.add(printable)
                    print(f"  0x{i:08X}: ...{printable}...")
                break

    # ── Recommandation ──────────────────────────────────────────────────────
    print("\n[RECOMMANDATION REVERSE]")
    if detected_lang and ('Delphi' in str(detected_lang) or 'TForm' in str(detected_lang)):
        print("→ Delphi : lancer IDR (Interactive Delphi Reconstructor) sous Wine")
        print("  IDR reconstruit classes, méthodes, formulaires")
    elif detected_lang and '.NET' in str(detected_lang):
        print("→ .NET : lancer dnspy ou ILSpy")
        print("  Décompile directement en C# lisible")
    else:
        print("→ C++ natif probable : utiliser Ghidra")
        print("  Si UPX : d'abord upx -d MPPS_V21.exe")
        print("  Puis charger dans Ghidra, analyser imports ftd2xx/libusb")

if __name__ == '__main__':
    exe = sys.argv[1] if len(sys.argv) > 1 else 'tools/reverse/MPPS_V21.exe'
    identify(exe)
