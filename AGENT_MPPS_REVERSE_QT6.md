# AGENT_MPPS_REVERSE_QT6.md
# Reverse engineering complet de l'exe MPPS V21
# + Migration open_car_reprog → Qt6 (ECU Studio)
# + Intégration MPPS dans SocketSpy
#
# ══════════════════════════════════════════════════════════════
# UTILISATION DES MODÈLES
# ══════════════════════════════════════════════════════════════
#
# Ce prompt utilise 3 modèles selon la difficulté des tâches :
#
#   claude-haiku-4-5-20251001   → tâches mécaniques et répétitives
#     - parsing de strings/sections PE
#     - génération de fichiers CMakeLists boilerplate
#     - portage 1-to-1 de fonctions JS simples vers C++
#     - création de stubs/headers vides
#     - écriture de scripts shell utilitaires
#     - reformatage / conversion de données
#
#   claude-sonnet-4-6           → tâches d'ingénierie standard
#     - architecture des modules C++
#     - portage des modules Node.js complexes (A2lParser, MapFinder)
#     - implémentation de l'UI Qt6 (panels, widgets)
#     - intégration libusb / libgit2
#     - tests GTest
#     - CMakeLists cross-platform
#
#   claude-opus-4-6             → tâches de reverse engineering pur
#     - analyse du binaire MPPS (Ghidra/IDR/dnspy selon techno)
#     - reconstruction du protocole depuis le désassemblage
#     - analyse des cas limites : boot mode, recovery, TPROT
#     - validation logique du protocole reconstitué
#     - toute décision d'architecture critique non évidente
#
# Syntaxe dans ce prompt : [HAIKU] [SONNET] [OPUS] précède chaque bloc
# indiquant quel modèle doit exécuter cette étape.
#
# Lancer avec :
#   claude --dangerously-skip-permissions -p "$(cat AGENT_MPPS_REVERSE_QT6.md)"
# ou session interactive :
#   claude --dangerously-skip-permissions
#   puis coller le contenu
#
# ══════════════════════════════════════════════════════════════
# NOTE IMPORTANTE — EXE MPPS
# ══════════════════════════════════════════════════════════════
#
# L'exe MPPS V21 sera fourni prochainement par l'utilisateur.
# En attendant, exécuter TOUTES les étapes qui ne nécessitent
# pas l'exe (architecture, portage Node.js→C++, UI Qt6, CMake).
# Les étapes [REVERSE] sont marquées — les mettre en attente
# avec des stubs annotés TODO_REVERSE.
#
# Quand l'exe arrive :
#   cp /chemin/vers/MPPS_V21.exe ./tools/reverse/MPPS_V21.exe
# puis relancer la PHASE 1 uniquement.
#
# ══════════════════════════════════════════════════════════════

---

## PHASE 0 — INITIALISATION DU MONO-REPO [HAIKU]

```bash
# Créer la structure complète du mono-repo cb4tech-suite
mkdir -p cb4tech-suite/{libs/{shared,mpps,can-core,ecu-core},apps,tools/reverse,tests,docs,cmake}
cd cb4tech-suite

# Submodule SocketSpy
git init
git submodule add https://github.com/Poisson48/SocketSpy apps/socketspy

# Créer TOUS les sous-dossiers manquants
mkdir -p libs/shared/{include/cb4,src}
mkdir -p libs/mpps/{include/mpps,src}
mkdir -p libs/ecu-core/{include/ecu,src}
mkdir -p libs/can-core/{include/can,src}
mkdir -p apps/ecu-studio/{src/{panels,widgets,dialogs},resources,i18n}
mkdir -p tools/reverse
mkdir -p tests/{unit,integration,fixtures}
mkdir -p docs
```

### [HAIKU] Générer le CMakeLists.txt racine

```cmake
# cb4tech-suite/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(cb4tech-suite VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──────────────────────────────────────────────────────────────────
option(CB4_BUILD_SOCKETSPY    "Build SocketSpy"                      ON)
option(CB4_BUILD_ECU_STUDIO   "Build ECU Studio"                     ON)
option(CB4_BUILD_TESTS        "Build tests"                          ON)
option(CB4_MPPS_SIMULATION    "Simulate MPPS (pas de vrai USB)"      OFF)
option(CB4_MPPS_PROTOCOL_LOG  "Logger toutes les trames MPPS"        OFF)

# ── Qt6 ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS
    Core Widgets Charts SerialBus SerialPort
    Concurrent Sql Network OpenGL
)
qt_standard_project_setup()

# ── Autres dépendances ───────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LUA     REQUIRED lua5.4)
pkg_check_modules(LIBUSB  REQUIRED libusb-1.0)
pkg_check_modules(LIBGIT2 REQUIRED libgit2)

find_package(nlohmann_json REQUIRED)
find_package(GTest         REQUIRED)

# ── Platform ─────────────────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(CB4_PLATFORM_WINDOWS)
else()
    add_compile_definitions(CB4_PLATFORM_LINUX CB4_SOCKETCAN_AVAILABLE)
endif()

if(CB4_MPPS_PROTOCOL_LOG)
    add_compile_definitions(CB4_MPPS_PROTOCOL_LOG)
endif()

# ── Compile flags ────────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Sous-répertoires ─────────────────────────────────────────────────────────
add_subdirectory(libs/shared)
add_subdirectory(libs/can-core)
add_subdirectory(libs/mpps)
add_subdirectory(libs/ecu-core)

if(CB4_BUILD_SOCKETSPY)
    add_subdirectory(apps/socketspy)
endif()
if(CB4_BUILD_ECU_STUDIO)
    add_subdirectory(apps/ecu-studio)
endif()
if(CB4_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### [HAIKU] Générer vcpkg.json

```json
{
  "name": "cb4tech-suite",
  "version": "1.0.0",
  "dependencies": [
    "nlohmann-json",
    "libusb",
    "libgit2",
    "gtest",
    "lua",
    {
      "name": "qt6",
      "features": ["core","widgets","charts","serialbus","serialport",
                   "concurrent","sql","network","opengl"]
    }
  ]
}
```

### [HAIKU] Créer le fichier udev Linux pour MPPS

```
# tools/60-mpps.rules
# Installer : sudo cp tools/60-mpps.rules /etc/udev/rules.d/
# Puis      : sudo udevadm control --reload-rules && sudo udevadm trigger

# FTDI FT232BM/RL — clones MPPS haut de gamme
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
# FTDI FT2232H — dual channel
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6010", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
# FTDI FTX series
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6015", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
# WCH CH340 — clones bas coût chinois
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
# WCH CH341
SUBSYSTEM=="usb", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="5523", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
# SiLabs CP2102
SUBSYSTEM=="usb", ATTRS{idVendor}=="10c4", ATTRS{idProduct}=="ea60", MODE="0666", GROUP="plugdev", SYMLINK+="mpps_%k"
```

---

## PHASE 1 — REVERSE ENGINEERING DE L'EXE MPPS [ATTEND L'EXE]

> ⚠️ L'exe MPPS V21 sera fourni prochainement.
> Préparer les outils maintenant, exécuter le reverse quand l'exe arrive.

### [HAIKU] Préparer l'environnement de reverse

```bash
# Installer les outils de reverse
pip install pefile capstone --break-system-packages

# Sur Linux — outils natifs
sudo apt-get install -y \
    wine64 \
    wireshark \
    tshark \
    upx \
    strings \
    binwalk \
    xxd

# Télécharger Ghidra si pas présent
if [ ! -d "/opt/ghidra" ]; then
    echo "Ghidra non trouvé — télécharger depuis https://ghidra-sre.org"
    echo "Extraire dans /opt/ghidra"
fi

# dnspy pour .NET (si nécessaire)
# https://github.com/dnSpy/dnSpy/releases — binaire Windows, lancer sous Wine

# IDR pour Delphi (si nécessaire)
# https://github.com/crypto2011/IDR — lancer sous Wine

echo "Outils prêts — attente de MPPS_V21.exe"
```

### [HAIKU] Script d'identification automatique `tools/reverse/identify_exe.py`

```python
#!/usr/bin/env python3
"""
identify_exe.py — Analyse rapide d'un exe pour guider le reverse engineering
Usage : python3 identify_exe.py MPPS_V21.exe
"""

import sys
import subprocess
import struct
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

    # ── DLLs importées (section imports PE) ─────────────────────────────────
    try:
        import pefile
        pe = pefile.PE(exe_path)
        print(f"\n[SECTIONS] {[s.Name.decode(errors='replace').strip() for s in pe.sections]}")
        if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
            dlls = [e.dll.decode(errors='replace') for e in pe.DIRECTORY_ENTRY_IMPORT]
            print(f"[IMPORTS DLLs] {dlls}")
            # Point critique : quelle API USB ?
            if any('ftd2xx' in d.lower() for d in dlls):
                print("\n⭐ FTDI D2XX API détectée (ftd2xx.dll)")
                print("   → Stratégie : DLL proxy ftd2xx.dll pour intercepter FT_Write/FT_Read")
                print("   → Toutes les trames MPPS passent par ces deux fonctions")
            if any('libusb' in d.lower() for d in dlls):
                print("\n⭐ libusb détectée")
                print("   → Stratégie : hook libusb_bulk_transfer")
            if any('hidapi' in d.lower() or 'hid.dll' in d.lower() for d in dlls):
                print("\n⭐ HID API détectée")
                print("   → Stratégie : hook HidD_GetFeature / HidD_SetFeature")
    except ImportError:
        print("[pefile non installé] pip install pefile")
    except Exception as e:
        print(f"[pefile erreur] {e}")

    # ── Strings intéressantes ───────────────────────────────────────────────
    print("\n[STRINGS PROTOCOLE]")
    keywords = [b'kline', b'k-line', b'K-Line', b'KWP', b'CAN', b'baud',
                b'timeout', b'INIT', b'READ', b'WRITE', b'FLASH', b'ERASE',
                b'EDC16', b'EDC17', b'ME7', b'checksum', b'0x68', b'0x81']
    found = set()
    for i in range(0, len(data) - 4):
        for kw in keywords:
            if data[i:i+len(kw)] == kw:
                # Extraire la string complète autour
                start = max(0, i - 20)
                end   = min(len(data), i + 60)
                chunk = data[start:end]
                printable = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                if printable not in found:
                    found.add(printable)
                    print(f"  0x{i:08X}: ...{printable}...")
                break

    # ── Résumé et recommandation ────────────────────────────────────────────
    print("\n[RECOMMANDATION REVERSE]")
    if 'Delphi' in str(detected_lang) or 'TForm' in str(detected_lang):
        print("→ Delphi détecté : lancer IDR (Interactive Delphi Reconstructor)")
        print("  IDR reconstruit les noms de classes, méthodes, formulaires")
        print("  + Chercher la classe qui gère ftd2xx/COM port → protocole direct")
    elif '.NET' in str(detected_lang):
        print("→ .NET détecté : lancer dnspy ou ILSpy")
        print("  Décompile directement en C# lisible — protocole visible en 30 min")
    else:
        print("→ C++ natif probable : utiliser Ghidra")
        print("  Si UPX : d'abord upx -d MPPS_V21.exe")
        print("  Puis charger dans Ghidra, analyser les imports ftd2xx/libusb")

if __name__ == '__main__':
    exe = sys.argv[1] if len(sys.argv) > 1 else 'tools/reverse/MPPS_V21.exe'
    identify(exe)
```

### [OPUS] Analyse du binaire et reconstruction du protocole [ATTEND L'EXE]

```
⚠️ Cette étape requiert claude-opus-4-6 et l'exe MPPS_V21.exe

Quand l'exe est disponible dans tools/reverse/MPPS_V21.exe :

1. Lancer identify_exe.py pour déterminer la stratégie

2a. Si Delphi → IDR :
    - Ouvrir MPPS_V21.exe dans IDR sous Wine
    - IDR reconstruit : TForm*, classes de communication, méthodes
    - Chercher : TComPort / TSerial / toute classe avec Write/Read
    - Chercher : les constantes 0x68 (STX) si présentes en clair
    - Exporter le rapport IDR → tools/reverse/idr_report/
    - Documenter chaque méthode pertinente dans docs/mpps-protocol.md

2b. Si .NET → dnspy :
    - wine dnspy/dnSpy.exe tools/reverse/MPPS_V21.exe
    - Chercher namespace contenant "Flash", "Protocol", "FTDI", "USB"
    - Copier les méthodes Send/Receive verbatim → docs/mpps-protocol.md

2c. Si C++ + UPX :
    - upx -d tools/reverse/MPPS_V21.exe -o tools/reverse/MPPS_V21_unpacked.exe
    - Charger dans Ghidra, auto-analyse
    - Chercher les imports : FT_Write, FT_Read, FT_Open
    - Tracer les xrefs sur FT_Write → trouver la fonction qui construit les trames
    - Décompiler cette fonction → reconstituer la structure des trames

3. Dans tous les cas — proxy DLL ftd2xx :
    Créer tools/reverse/ftd2xx_proxy/ (voir ci-dessous)
    Lancer l'exe MPPS avec le proxy → capturer TOUTES les trames réelles

4. Résultat attendu dans docs/mpps-protocol.md :
    - Structure exacte des trames (header, longueur, cmd, payload, checksum)
    - Table de tous les codes de commandes avec paramètres
    - Séquences d'initialisation K-line et CAN
    - Séquences read/write par famille ECU (EDC16, ME7, MED17...)
    - Gestion des erreurs et retries
    - Timeouts par type d'opération

Documenter TOUT dans docs/mpps-protocol.md avec exemples hex réels.
```

### [SONNET] Créer le proxy DLL ftd2xx `tools/reverse/ftd2xx_proxy/`

```c
/* ftd2xx_proxy.c
 * DLL proxy pour intercepter toutes les communications MPPS ↔ USB
 * Compilé en DLL Windows 32/64-bit, placé dans le dossier de l'exe MPPS
 * Forwarde vers ftd2xx_real.dll (renommer l'original)
 *
 * Compiler : x86_64-w64-mingw32-gcc -shared -o ftd2xx.dll ftd2xx_proxy.c
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <time.h>

// Types FTDI D2XX (subset nécessaire)
typedef void* FT_HANDLE;
typedef unsigned long FT_STATUS;
typedef unsigned long DWORD;

// Charger la vraie DLL au démarrage
static HMODULE real_dll = NULL;
static FILE*   log_file = NULL;

typedef FT_STATUS (__stdcall *FT_Write_t)(FT_HANDLE, void*, DWORD, DWORD*);
typedef FT_STATUS (__stdcall *FT_Read_t)(FT_HANDLE, void*, DWORD, DWORD*);
typedef FT_STATUS (__stdcall *FT_Open_t)(int, FT_HANDLE*);
typedef FT_STATUS (__stdcall *FT_Close_t)(FT_HANDLE);
typedef FT_STATUS (__stdcall *FT_SetBaudRate_t)(FT_HANDLE, DWORD);
typedef FT_STATUS (__stdcall *FT_SetTimeouts_t)(FT_HANDLE, DWORD, DWORD);

static FT_Write_t       real_FT_Write       = NULL;
static FT_Read_t        real_FT_Read        = NULL;
static FT_Open_t        real_FT_Open        = NULL;
static FT_Close_t       real_FT_Close       = NULL;
static FT_SetBaudRate_t real_FT_SetBaudRate = NULL;
static FT_SetTimeouts_t real_FT_SetTimeouts = NULL;

static void log_hex(const char* direction, const void* buf, DWORD len) {
    if (!log_file) return;

    // Timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(log_file, "[%02d:%02d:%02d.%03d] %s (%lu bytes): ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            direction, len);

    // Hex dump
    const unsigned char* p = (const unsigned char*)buf;
    for (DWORD i = 0; i < len; i++) {
        fprintf(log_file, "%02X ", p[i]);
        if ((i + 1) % 16 == 0) fprintf(log_file, "\n    ");
    }
    fprintf(log_file, "\n");

    // Analyse basique de la trame
    if (len >= 3 && p[0] == 0x68) {
        fprintf(log_file, "    → STX=0x68 LEN=%02X CMD=%02X\n", p[1], p[2]);
    } else if (len >= 1 && (p[0] & 0x80)) {
        fprintf(log_file, "    → RESPONSE ACK=0x%02X\n", p[0]);
    }

    fflush(log_file);
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        // Ouvrir le log
        log_file = fopen("mpps_protocol_capture.log", "a");
        if (log_file) fprintf(log_file, "\n=== Session démarrée ===\n");

        // Charger la vraie DLL
        real_dll = LoadLibraryA("ftd2xx_real.dll");
        if (!real_dll) {
            if (log_file) fprintf(log_file, "[ERREUR] ftd2xx_real.dll introuvable\n");
            return FALSE;
        }

        // Résoudre les fonctions
        real_FT_Write       = (FT_Write_t)      GetProcAddress(real_dll, "FT_Write");
        real_FT_Read        = (FT_Read_t)       GetProcAddress(real_dll, "FT_Read");
        real_FT_Open        = (FT_Open_t)       GetProcAddress(real_dll, "FT_Open");
        real_FT_Close       = (FT_Close_t)      GetProcAddress(real_dll, "FT_Close");
        real_FT_SetBaudRate = (FT_SetBaudRate_t)GetProcAddress(real_dll, "FT_SetBaudRate");
        real_FT_SetTimeouts = (FT_SetTimeouts_t)GetProcAddress(real_dll, "FT_SetTimeouts");

        if (log_file) fprintf(log_file, "[OK] Proxy DLL initialisé\n");
    }
    else if (fdwReason == DLL_PROCESS_DETACH) {
        if (log_file) { fprintf(log_file, "=== Session terminée ===\n"); fclose(log_file); }
        if (real_dll) FreeLibrary(real_dll);
    }
    return TRUE;
}

// ── Fonctions interceptées ───────────────────────────────────────────────────

__declspec(dllexport) FT_STATUS __stdcall FT_Write(
    FT_HANDLE ftHandle, void* lpBuffer, DWORD dwBytesToWrite, DWORD* lpdwBytesWritten)
{
    log_hex("TX →", lpBuffer, dwBytesToWrite);
    return real_FT_Write(ftHandle, lpBuffer, dwBytesToWrite, lpdwBytesWritten);
}

__declspec(dllexport) FT_STATUS __stdcall FT_Read(
    FT_HANDLE ftHandle, void* lpBuffer, DWORD dwBytesToRead, DWORD* lpdwBytesRead)
{
    FT_STATUS ret = real_FT_Read(ftHandle, lpBuffer, dwBytesToRead, lpdwBytesRead);
    if (ret == 0 && *lpdwBytesRead > 0) {
        log_hex("← RX", lpBuffer, *lpdwBytesRead);
    }
    return ret;
}

__declspec(dllexport) FT_STATUS __stdcall FT_Open(int iDevice, FT_HANDLE* pHandle) {
    if (log_file) fprintf(log_file, "[FT_Open] device=%d\n", iDevice);
    return real_FT_Open(iDevice, pHandle);
}

__declspec(dllexport) FT_STATUS __stdcall FT_Close(FT_HANDLE ftHandle) {
    if (log_file) fprintf(log_file, "[FT_Close]\n");
    return real_FT_Close(ftHandle);
}

__declspec(dllexport) FT_STATUS __stdcall FT_SetBaudRate(FT_HANDLE ftHandle, DWORD dwBaudRate) {
    if (log_file) fprintf(log_file, "[FT_SetBaudRate] %lu baud\n", dwBaudRate);
    return real_FT_SetBaudRate(ftHandle, dwBaudRate);
}

__declspec(dllexport) FT_STATUS __stdcall FT_SetTimeouts(
    FT_HANDLE ftHandle, DWORD dwReadTimeout, DWORD dwWriteTimeout)
{
    if (log_file) fprintf(log_file, "[FT_SetTimeouts] read=%lu write=%lu ms\n",
                          dwReadTimeout, dwWriteTimeout);
    return real_FT_SetTimeouts(ftHandle, dwReadTimeout, dwWriteTimeout);
}
```

### [HAIKU] Script de build du proxy DLL

```bash
#!/bin/bash
# tools/reverse/build_proxy.sh
# Compile le proxy DLL pour interception FTDI

set -e
cd "$(dirname "$0")"

echo "Compilation ftd2xx proxy DLL..."

# Pour Windows 64-bit
x86_64-w64-mingw32-gcc \
    -shared \
    -o ftd2xx.dll \
    ftd2xx_proxy.c \
    -Wl,--kill-at \
    -static-libgcc

# Pour Windows 32-bit (si l'exe est 32-bit)
i686-w64-mingw32-gcc \
    -shared \
    -o ftd2xx_32.dll \
    ftd2xx_proxy.c \
    -Wl,--kill-at \
    -static-libgcc

echo "Instructions :"
echo "1. Renommer ftd2xx.dll original → ftd2xx_real.dll dans le dossier MPPS"
echo "2. Copier notre ftd2xx.dll dans ce même dossier"
echo "3. Lancer MPPS_V21.exe"
echo "4. Faire : init → connecter ECU → lire ROM → écrire ROM"
echo "5. Récupérer mpps_protocol_capture.log"
echo "6. Analyser le log → compléter libs/mpps/src/MppsProtocol.cpp"
```

### [OPUS] Parser le log de capture `tools/reverse/parse_capture.py`

```python
#!/usr/bin/env python3
"""
parse_capture.py — Analyse mpps_protocol_capture.log
Reconstruit le protocole MPPS depuis les trames capturées
Usage : python3 parse_capture.py mpps_protocol_capture.log
"""
import sys
import re
from dataclasses import dataclass, field
from typing import Optional

@dataclass
class Frame:
    timestamp: str
    direction: str   # 'TX' ou 'RX'
    raw: bytes
    # Champs décodés
    stx: Optional[int] = None
    length: Optional[int] = None
    cmd: Optional[int] = None
    payload: Optional[bytes] = None
    checksum: Optional[int] = None
    valid_checksum: bool = False

def parse_log(log_path: str) -> list[Frame]:
    frames = []
    with open(log_path) as f:
        content = f.read()

    # Pattern : [HH:MM:SS.mmm] TX/RX (N bytes): HH HH HH...
    pattern = r'\[(\d{2}:\d{2}:\d{2}\.\d{3})\] (TX →|← RX) \((\d+) bytes\): ([0-9A-F ]+)'
    for m in re.finditer(pattern, content):
        ts, direction, length, hex_str = m.groups()
        raw = bytes(int(h, 16) for h in hex_str.strip().split() if h)
        frame = Frame(timestamp=ts, direction='TX' if '→' in direction else 'RX', raw=raw)

        # Décoder la structure MPPS (hypothèse initiale à valider)
        if len(raw) >= 3:
            frame.stx = raw[0]
            if raw[0] == 0x68:  # STX attendu pour TX
                frame.length = raw[1]
                frame.cmd    = raw[2]
                frame.payload = raw[3:-1] if len(raw) > 3 else b''
                frame.checksum = raw[-1]
                # Vérifier XOR
                xor = 0
                for b in raw[:-1]: xor ^= b
                frame.valid_checksum = (xor == frame.checksum)

        frames.append(frame)
    return frames

def analyze(frames: list[Frame]):
    print(f"Total trames : {len(frames)}")
    print(f"TX : {sum(1 for f in frames if f.direction == 'TX')}")
    print(f"RX : {sum(1 for f in frames if f.direction == 'RX')}")
    print()

    # Identifier les commandes uniques
    cmds = {}
    for f in frames:
        if f.direction == 'TX' and f.cmd is not None:
            if f.cmd not in cmds:
                cmds[f.cmd] = {'count': 0, 'payloads': [], 'responses': []}
            cmds[f.cmd]['count'] += 1
            if f.payload:
                cmds[f.cmd]['payloads'].append(f.payload.hex())

    print("=== COMMANDES DÉTECTÉES ===")
    for cmd, info in sorted(cmds.items()):
        print(f"\nCMD 0x{cmd:02X} — {info['count']} occurrences")
        unique_payloads = list(set(info['payloads'][:5]))
        for p in unique_payloads[:3]:
            print(f"  payload exemple : {p}")

    # Vérifier l'hypothèse STX=0x68
    stx_values = set(f.raw[0] for f in frames if f.direction == 'TX' and f.raw)
    print(f"\n=== STX observés (TX) : {[hex(v) for v in stx_values]} ===")

    # Checksum validation
    valid = sum(1 for f in frames if f.valid_checksum)
    print(f"Checksums valides : {valid}/{len([f for f in frames if f.direction == 'TX'])}")
    if valid == 0:
        print("→ L'hypothèse XOR checksum est INCORRECTE")
        print("→ Tester : ADD, CRC8, CRC16, checksum sur sous-ensemble")
    else:
        print("→ Checksum XOR confirmé ✓")

    # Patterns de séquences (init, read, write)
    print("\n=== SÉQUENCES DÉTECTÉES ===")
    tx_cmds = [f.cmd for f in frames if f.direction == 'TX' and f.cmd]
    print(f"Séquence complète : {[hex(c) for c in tx_cmds[:20]]}...")
    # Chercher la séquence d'init (début de session)
    print(f"Première commande (init) : CMD 0x{tx_cmds[0]:02X}" if tx_cmds else "Aucune TX")

if __name__ == '__main__':
    log = sys.argv[1] if len(sys.argv) > 1 else 'mpps_protocol_capture.log'
    try:
        frames = parse_log(log)
        analyze(frames)
    except FileNotFoundError:
        print(f"[ATTENTE] {log} pas encore disponible")
        print("Lancer d'abord l'exe MPPS avec le proxy DLL ftd2xx")
```

---

## PHASE 2 — LIB `libs/mpps/` [SONNET]

Implémenter le driver MPPS en C++23 avec les informations disponibles.
Les valeurs marquées `TODO_REVERSE` seront remplies après l'analyse du log.

### `libs/mpps/include/mpps/MppsDevice.hpp`

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include <string>
#include <span>

namespace mpps {

// ── Erreurs ──────────────────────────────────────────────────────────────────
enum class MppsError {
    DeviceNotFound,
    DriverNotReady,     // ftdi_sio non détaché sur Linux / WinUSB pas installé
    UsbError,
    Timeout,
    ProtocolError,
    ChecksumMismatch,
    EcuNotResponding,
    WriteProtected,
    NotImplemented,
    SimulationMode,
};

std::string to_string(MppsError e);

template<typename T>
using MppsResult = std::expected<T, MppsError>;

// ── Infos device ─────────────────────────────────────────────────────────────
struct MppsDeviceInfo {
    std::string path;            // "/dev/ttyUSB0" sur Linux
    std::string serial;
    std::string chipType;        // "FTDI FT232R", "CH340", "CP2102"
    uint16_t    vid = 0;
    uint16_t    pid = 0;
    std::string firmwareVersion; // rempli après connect()
    bool        isMpps = false;
};

// Callback progression : (done, total, message)
using ProgressCb = std::function<void(uint32_t, uint32_t, const std::string&)>;

// ── Interface abstraite ───────────────────────────────────────────────────────
class MppsDevice {
public:
    virtual ~MppsDevice() = default;

    // Factory
    static std::vector<MppsDeviceInfo> enumerate();
    static std::unique_ptr<MppsDevice> open(const MppsDeviceInfo& info);
    static std::unique_ptr<MppsDevice> openSimulation(); // CB4_MPPS_SIMULATION

    // Connexion
    virtual MppsResult<MppsDeviceInfo> connect()    = 0;
    virtual void                       disconnect()  = 0;
    virtual bool                       isConnected() const = 0;

    // ECU
    virtual MppsResult<std::string>  identifyEcu()   = 0;
    virtual MppsResult<void>         enterProgMode()  = 0;
    virtual MppsResult<void>         exitProgMode()   = 0;

    // Mémoire
    virtual MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb = nullptr) = 0;

    virtual MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data,
        ProgressCb cb = nullptr) = 0;

    virtual MppsResult<void> eraseSector(uint32_t address, uint32_t size) = 0;

    // ROM complète (multi-blocs automatique)
    MppsResult<std::vector<uint8_t>> readFullRom(uint32_t romSize, ProgressCb cb = nullptr);
    MppsResult<void> writeFullRom(std::span<const uint8_t> rom, ProgressCb cb = nullptr);

    // Protocole physique
    enum class PhysicalProtocol { KLine, Can, Auto };
    virtual MppsResult<void> setProtocol(
        PhysicalProtocol p, uint32_t bitrate = 500000) = 0;

    // Checksum hardware MPPS (optionnel selon ECU)
    virtual MppsResult<uint32_t> hardwareChecksum(
        uint32_t start, uint32_t end) = 0;
};

// ── Constantes protocole (à valider/compléter après reverse) ─────────────────
namespace protocol {

// TODO_REVERSE : valider ces valeurs depuis mpps_protocol_capture.log
constexpr uint8_t STX             = 0x68;  // début de trame TX
constexpr uint8_t CMD_HANDSHAKE   = 0x01;  // init / version firmware
constexpr uint8_t CMD_ECU_IDENT   = 0x02;  // identifier l'ECU
constexpr uint8_t CMD_READ_BLOCK  = 0x10;  // lire N bytes depuis address
constexpr uint8_t CMD_WRITE_BLOCK = 0x11;  // écrire N bytes à address
constexpr uint8_t CMD_ERASE       = 0x20;  // effacer un secteur
constexpr uint8_t CMD_CHECKSUM    = 0x30;  // checksum hardware d'une plage
constexpr uint8_t CMD_ENTER_PROG  = 0x40;  // mode programmation
constexpr uint8_t CMD_EXIT_PROG   = 0x41;  // redémarrer ECU
constexpr uint8_t CMD_KLINE_INIT  = 0x50;  // init K-line
constexpr uint8_t CMD_CAN_INIT    = 0x51;  // init CAN
constexpr uint8_t CMD_ABORT       = 0xFF;  // annuler opération

constexpr uint32_t MAX_BLOCK_SIZE = 256;   // bytes max par trame read/write
constexpr uint32_t DEFAULT_TIMEOUT_MS = 1000;
constexpr uint32_t FLASH_TIMEOUT_MS   = 5000; // plus long pour l'écriture flash

} // namespace protocol
} // namespace mpps
```

### `libs/mpps/src/MppsDevice_libusb.cpp` [SONNET]

```cpp
#include "mpps/MppsDevice.hpp"
#include <libusb-1.0/libusb.h>
#include <array>
#include <thread>
#include <chrono>
#include <format>

#ifdef CB4_MPPS_PROTOCOL_LOG
#include <fstream>
static std::ofstream proto_log("mpps_protocol.log", std::ios::app);
#define PROTO_LOG(msg) if(proto_log.is_open()) proto_log << msg << "\n"
#else
#define PROTO_LOG(msg)
#endif

namespace mpps {

// VID/PID connus
static constexpr std::array<std::pair<uint16_t,uint16_t>, 6> KNOWN_VID_PID {{
    {0x0403, 0x6001},  // FTDI FT232BM/RL
    {0x0403, 0x6010},  // FTDI FT2232H
    {0x0403, 0x6015},  // FTDI FTX
    {0x1A86, 0x7523},  // WCH CH340
    {0x1A86, 0x5523},  // WCH CH341
    {0x10C4, 0xEA60},  // SiLabs CP2102
}};

static std::string chipType(uint16_t vid, uint16_t pid) {
    if (vid == 0x0403) {
        if (pid == 0x6010) return "FTDI FT2232H";
        if (pid == 0x6015) return "FTDI FTX";
        return "FTDI FT232R";
    }
    if (vid == 0x1A86) return pid == 0x7523 ? "WCH CH340" : "WCH CH341";
    if (vid == 0x10C4) return "SiLabs CP2102";
    return "Unknown";
}

class MppsDeviceLibUsb : public MppsDevice {
public:
    ~MppsDeviceLibUsb() override { disconnect(); }

    MppsResult<MppsDeviceInfo> connect() override {
        if (m_connected) return m_info;

        int r = libusb_init(&m_ctx);
        if (r < 0) return std::unexpected(MppsError::UsbError);

        // Trouver le device
        libusb_device** list = nullptr;
        ssize_t cnt = libusb_get_device_list(m_ctx, &list);
        libusb_device* found = nullptr;

        for (ssize_t i = 0; i < cnt; i++) {
            libusb_device_descriptor desc;
            libusb_get_device_descriptor(list[i], &desc);
            if (desc.idVendor == m_info.vid && desc.idProduct == m_info.pid) {
                found = libusb_ref_device(list[i]);
                break;
            }
        }
        libusb_free_device_list(list, 1);

        if (!found) return std::unexpected(MppsError::DeviceNotFound);

        r = libusb_open(found, &m_handle);
        libusb_unref_device(found);
        if (r < 0) return std::unexpected(MppsError::UsbError);

        // Détacher le driver kernel (ftdi_sio) si nécessaire
#ifdef CB4_PLATFORM_LINUX
        if (libusb_kernel_driver_active(m_handle, 0) == 1) {
            r = libusb_detach_kernel_driver(m_handle, 0);
            if (r < 0) {
                libusb_close(m_handle);
                return std::unexpected(MppsError::DriverNotReady);
            }
            m_driver_detached = true;
        }
#endif

        r = libusb_claim_interface(m_handle, 0);
        if (r < 0) {
            libusb_close(m_handle);
            return std::unexpected(MppsError::UsbError);
        }

        // Configurer le baud rate FTDI via control transfer
        // TODO_REVERSE : valider baud rate initial depuis le log (38400? 57600?)
        configureFTDI(38400);

        // Handshake MPPS
        auto result = sendCommand(protocol::CMD_HANDSHAKE, {});
        if (!result) return std::unexpected(MppsError::EcuNotResponding);

        // Parser la réponse pour extraire la version firmware
        if (!result->empty()) {
            m_info.firmwareVersion = std::string(result->begin(), result->end());
        }
        m_info.isMpps = true;
        m_connected = true;

        PROTO_LOG("[connect] OK, fw=" + m_info.firmwareVersion);
        return m_info;
    }

    void disconnect() override {
        if (!m_connected) return;
        sendCommand(protocol::CMD_ABORT, {});  // annuler toute opération
        libusb_release_interface(m_handle, 0);
#ifdef CB4_PLATFORM_LINUX
        if (m_driver_detached)
            libusb_attach_kernel_driver(m_handle, 0);
#endif
        libusb_close(m_handle);
        libusb_exit(m_ctx);
        m_handle = nullptr;
        m_ctx = nullptr;
        m_connected = false;
    }

    bool isConnected() const override { return m_connected; }

    MppsResult<std::string> identifyEcu() override {
        auto r = sendCommand(protocol::CMD_ECU_IDENT, {});
        if (!r) return std::unexpected(r.error());
        return std::string(r->begin(), r->end());
    }

    MppsResult<void> enterProgMode() override {
        auto r = sendCommand(protocol::CMD_ENTER_PROG, {});
        if (!r) return std::unexpected(r.error());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return {};
    }

    MppsResult<void> exitProgMode() override {
        auto r = sendCommand(protocol::CMD_EXIT_PROG, {});
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb) override
    {
        std::vector<uint8_t> result;
        result.reserve(length);

        for (uint32_t offset = 0; offset < length;
             offset += protocol::MAX_BLOCK_SIZE)
        {
            uint32_t chunk = std::min(protocol::MAX_BLOCK_SIZE, length - offset);
            uint32_t addr  = address + offset;

            std::vector<uint8_t> payload = {
                uint8_t((addr >> 16) & 0xFF),
                uint8_t((addr >>  8) & 0xFF),
                uint8_t( addr        & 0xFF),
                uint8_t((chunk >> 8) & 0xFF),
                uint8_t( chunk       & 0xFF),
            };

            auto r = sendCommand(protocol::CMD_READ_BLOCK, payload,
                                 protocol::DEFAULT_TIMEOUT_MS);
            if (!r) return std::unexpected(r.error());

            result.insert(result.end(), r->begin(), r->end());
            if (cb) cb(offset + chunk, length,
                       std::format("Lecture 0x{:06X}", addr));
        }
        return result;
    }

    MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data, ProgressCb cb) override
    {
        for (uint32_t offset = 0; offset < data.size();
             offset += protocol::MAX_BLOCK_SIZE)
        {
            uint32_t chunk = std::min((uint32_t)protocol::MAX_BLOCK_SIZE,
                                      (uint32_t)data.size() - offset);
            uint32_t addr  = address + offset;

            std::vector<uint8_t> payload = {
                uint8_t((addr >> 16) & 0xFF),
                uint8_t((addr >>  8) & 0xFF),
                uint8_t( addr        & 0xFF),
                uint8_t((chunk >> 8) & 0xFF),
                uint8_t( chunk       & 0xFF),
            };
            payload.insert(payload.end(),
                           data.begin() + offset,
                           data.begin() + offset + chunk);

            auto r = sendCommand(protocol::CMD_WRITE_BLOCK, payload,
                                 protocol::FLASH_TIMEOUT_MS);
            if (!r) return std::unexpected(r.error());
            if (cb) cb(offset + chunk, data.size(),
                       std::format("Écriture 0x{:06X}", addr));
        }
        return {};
    }

    MppsResult<void> eraseSector(uint32_t address, uint32_t size) override {
        std::vector<uint8_t> payload = {
            uint8_t((address >> 16) & 0xFF),
            uint8_t((address >>  8) & 0xFF),
            uint8_t( address        & 0xFF),
            uint8_t((size    >>  8) & 0xFF),
            uint8_t( size           & 0xFF),
        };
        auto r = sendCommand(protocol::CMD_ERASE, payload,
                             protocol::FLASH_TIMEOUT_MS);
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<void> setProtocol(PhysicalProtocol p, uint32_t bitrate) override {
        uint8_t cmd = (p == PhysicalProtocol::KLine)
                    ? protocol::CMD_KLINE_INIT
                    : protocol::CMD_CAN_INIT;
        std::vector<uint8_t> payload = {
            uint8_t((bitrate >> 8) & 0xFF),
            uint8_t( bitrate       & 0xFF),
        };
        auto r = sendCommand(cmd, payload);
        if (!r) return std::unexpected(r.error());
        return {};
    }

    MppsResult<uint32_t> hardwareChecksum(uint32_t start, uint32_t end) override {
        std::vector<uint8_t> payload = {
            uint8_t((start >> 16) & 0xFF), uint8_t((start >> 8) & 0xFF), uint8_t(start & 0xFF),
            uint8_t((end   >> 16) & 0xFF), uint8_t((end   >> 8) & 0xFF), uint8_t(end   & 0xFF),
        };
        auto r = sendCommand(protocol::CMD_CHECKSUM, payload);
        if (!r) return std::unexpected(r.error());
        if (r->size() < 4) return std::unexpected(MppsError::ProtocolError);
        uint32_t csum = ((*r)[0] << 24) | ((*r)[1] << 16) | ((*r)[2] << 8) | (*r)[3];
        return csum;
    }

    // ── Factory statics ────────────────────────────────────────────────────
    static std::vector<MppsDeviceInfo> enumerate_impl() {
        std::vector<MppsDeviceInfo> results;
        libusb_context* ctx = nullptr;
        libusb_init(&ctx);

        libusb_device** list = nullptr;
        ssize_t cnt = libusb_get_device_list(ctx, &list);

        for (ssize_t i = 0; i < cnt; i++) {
            libusb_device_descriptor desc;
            libusb_get_device_descriptor(list[i], &desc);
            for (auto [vid, pid] : KNOWN_VID_PID) {
                if (desc.idVendor == vid && desc.idProduct == pid) {
                    MppsDeviceInfo info;
                    info.vid      = vid;
                    info.pid      = pid;
                    info.chipType = chipType(vid, pid);
                    info.path     = std::format("usb:{:04x}:{:04x}:{}", vid, pid, i);
                    results.push_back(info);
                    break;
                }
            }
        }
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return results;
    }

    MppsDeviceInfo m_info;

private:
    libusb_context*      m_ctx    = nullptr;
    libusb_device_handle* m_handle = nullptr;
    bool m_connected       = false;
    bool m_driver_detached = false;

    // Endpoints FTDI standard (TODO_REVERSE : confirmer depuis le log)
    static constexpr uint8_t EP_OUT = 0x02;  // Bulk OUT host→device
    static constexpr uint8_t EP_IN  = 0x81;  // Bulk IN  device→host

    void configureFTDI(uint32_t baudrate) {
        // TODO_REVERSE : confirmer les valeurs depuis le log FT_SetBaudRate
        // Diviseur FTDI pour baud rate : 3000000 / (baudrate * 16) approximatif
        (void)baudrate;
        // libusb_control_transfer(m_handle, 0x40, 3, divisor, 0, nullptr, 0, 1000);
    }

    MppsResult<std::vector<uint8_t>> sendCommand(
        uint8_t cmd,
        const std::vector<uint8_t>& payload,
        int timeoutMs = protocol::DEFAULT_TIMEOUT_MS)
    {
        // Construire trame : STX + LEN + CMD + PAYLOAD + XOR_CHECKSUM
        // TODO_REVERSE : valider cette structure depuis parse_capture.py
        std::vector<uint8_t> frame;
        frame.push_back(protocol::STX);
        frame.push_back(static_cast<uint8_t>(payload.size() + 1));
        frame.push_back(cmd);
        frame.insert(frame.end(), payload.begin(), payload.end());
        uint8_t chk = 0;
        for (auto b : frame) chk ^= b;
        frame.push_back(chk);

        PROTO_LOG("TX: " + hex_dump(frame));

        // Envoyer
        int transferred = 0;
        int r = libusb_bulk_transfer(m_handle, EP_OUT,
                    frame.data(), (int)frame.size(),
                    &transferred, timeoutMs);
        if (r != LIBUSB_SUCCESS) return std::unexpected(MppsError::UsbError);

        // Recevoir
        std::vector<uint8_t> resp(4096);
        r = libusb_bulk_transfer(m_handle, EP_IN,
                resp.data(), (int)resp.size(),
                &transferred, timeoutMs);
        if (r != LIBUSB_SUCCESS) return std::unexpected(MppsError::Timeout);
        resp.resize(transferred);

        PROTO_LOG("RX: " + hex_dump(resp));

        // Valider la réponse
        // TODO_REVERSE : valider la structure de la réponse depuis le log
        if (resp.empty()) return std::unexpected(MppsError::ProtocolError);

        // Retourner le payload de la réponse (sans header)
        if (resp.size() <= 3) return std::vector<uint8_t>{};
        return std::vector<uint8_t>(resp.begin() + 3, resp.end() - 1);
    }

    static std::string hex_dump(const std::vector<uint8_t>& v) {
        std::string s;
        for (auto b : v) s += std::format("{:02X} ", b);
        return s;
    }
};

// ── Implémentation Simulation ────────────────────────────────────────────────
class MppsDeviceSim : public MppsDevice {
public:
    MppsResult<MppsDeviceInfo> connect() override {
        m_connected = true;
        m_rom.assign(0x200000, 0xFF);   // 2Mo de FF — simuler ROM vierge
        // Remplir avec des données EDC16C34 bidon
        for (size_t i = 0x1C0000; i < 0x200000; i += 4)
            m_rom[i] = (uint8_t)(i & 0xFF);

        MppsDeviceInfo info;
        info.chipType = "SIMULATION";
        info.firmwareVersion = "SIM-1.0";
        info.isMpps = true;
        return info;
    }
    void disconnect() override { m_connected = false; }
    bool isConnected() const override { return m_connected; }
    MppsResult<std::string> identifyEcu() override { return "EDC16C34_SIM"; }
    MppsResult<void> enterProgMode() override { return {}; }
    MppsResult<void> exitProgMode()  override { return {}; }
    MppsResult<void> setProtocol(PhysicalProtocol, uint32_t) override { return {}; }
    MppsResult<uint32_t> hardwareChecksum(uint32_t, uint32_t) override { return 0xD110DD00u; }
    MppsResult<void> eraseSector(uint32_t addr, uint32_t size) override {
        if (addr + size > m_rom.size()) return std::unexpected(MppsError::UsbError);
        std::fill(m_rom.begin() + addr, m_rom.begin() + addr + size, 0xFF);
        return {};
    }
    MppsResult<std::vector<uint8_t>> readBlock(
        uint32_t address, uint32_t length, ProgressCb cb) override
    {
        if (address + length > m_rom.size()) return std::unexpected(MppsError::UsbError);
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // simuler latence
        if (cb) cb(length, length, "Simulation lecture");
        return std::vector<uint8_t>(m_rom.begin()+address, m_rom.begin()+address+length);
    }
    MppsResult<void> writeBlock(
        uint32_t address, std::span<const uint8_t> data, ProgressCb cb) override
    {
        if (address + data.size() > m_rom.size()) return std::unexpected(MppsError::UsbError);
        std::copy(data.begin(), data.end(), m_rom.begin() + address);
        if (cb) cb(data.size(), data.size(), "Simulation écriture");
        return {};
    }

private:
    bool m_connected = false;
    std::vector<uint8_t> m_rom;
};

// ── Factory globale ──────────────────────────────────────────────────────────
std::vector<MppsDeviceInfo> MppsDevice::enumerate() {
#ifdef CB4_MPPS_SIMULATION
    return {{ .path="sim://", .chipType="SIMULATION", .isMpps=true }};
#else
    return MppsDeviceLibUsb::enumerate_impl();
#endif
}

std::unique_ptr<MppsDevice> MppsDevice::open(const MppsDeviceInfo& info) {
#ifdef CB4_MPPS_SIMULATION
    return std::make_unique<MppsDeviceSim>();
#else
    auto dev = std::make_unique<MppsDeviceLibUsb>();
    dev->m_info = info;
    return dev;
#endif
}

std::unique_ptr<MppsDevice> MppsDevice::openSimulation() {
    return std::make_unique<MppsDeviceSim>();
}

} // namespace mpps
```

---

## PHASE 3 — PORT `libs/ecu-core/` DEPUIS NODE.JS [SONNET]

Lire les fichiers source de open_car_reprog :
```bash
git clone https://github.com/Poisson48/open_car_reprog /tmp/open_car_reprog
ls /tmp/open_car_reprog/src/
```

Porter chaque module dans cet ordre (du plus simple au plus complexe) :

### Ordre de portage [SONNET]

```
1. ecu-catalog.js       → EcuCatalog.cpp      [HAIKU — portage mécanique]
2. vehicle-templates.js → VehicleTemplates.cpp [HAIKU — portage mécanique]
3. checksum-engine.js   → ChecksumEngine.cpp   [SONNET — algos CRC/ADD32]
4. rom-patcher.js       → RomPatcher.cpp       [SONNET — lecture/écriture maps]
5. map-finder.js        → MapFinder.cpp        [SONNET — scan heuristique]
6. open-damos.js        → OpenDamos.cpp        [SONNET — fingerprint + reloc]
7. map-differ.js        → MapDiffer.cpp        [SONNET — diff ROM]
8. project-manager.js   → ProjectManager.cpp   [SONNET — QFile/QDir]
9. git-manager.js       → GitManager.cpp       [SONNET — libgit2]
10. a2l-parser.js       → A2lParser.cpp        [OPUS — parser récursif ASAP2]
```

Pour chaque module :
```bash
# Lire le source JS
cat /tmp/open_car_reprog/src/MODULE.js

# Créer le header C++
# Créer l'implémentation C++
# Écrire le test GTest correspondant dans tests/unit/test_MODULE.cpp
```

### [OPUS] `A2lParser.cpp` — le plus critique

```cpp
// A2lParser doit :
// 1. Parser le format ASAP2 (DAMOS) récursivement
//    /begin TAG ... /end TAG avec imbrication
// 2. Extraire : CHARACTERISTIC, AXIS_PTS, RECORD_LAYOUT, COMPU_METHOD
// 3. Résoudre les références croisées (AXIS_PTS → RECORD_LAYOUT → scaling)
// 4. Sérialiser en cache QDataStream pour rechargement rapide (3s → 50ms)
// 5. Gérer les fichiers A2L de 50-200 Mo sans exploser la mémoire

// Structure des données en sortie :
struct Characteristic {
    QString          name;
    QString          longIdentifier;
    uint32_t         address;
    QString          type;           // VALUE, CURVE, MAP, VAL_BLK
    int              nx = 1, ny = 1;
    float            factor = 1.0f, offset = 0.0f;
    QString          unit;
    QString          axisXName, axisYName;
    // Axes résolus (après résolution des références)
    QList<float>     axisX, axisY;
};

// Le parser doit gérer les dialects A2L courants :
// - Bosch DAMOS (EDC16C34, ME7, MED17)
// - PSA DAMOS (variante légèrement différente)
// - Vector A2L standard
// Tester avec le fichier A2L EDC16C34 existant dans le projet
```

---

## PHASE 4 — APP `apps/ecu-studio/` [SONNET]

### MainWindow — sidebar inspirée SocketSpy

```cpp
// apps/ecu-studio/src/MainWindow.hpp
// Architecture identique à SocketSpy :
// - QMainWindow avec QDockWidget pour chaque panel
// - Sidebar icônes à gauche (QToolBar vertical)
// - Panels détachables (drag, right-click → Detach)
// - Theme dark cohérent avec SocketSpy (#1e2327)

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    // Panels — même pattern que SocketSpy
    void createPanels();
    void createSidebar();
    void createMenus();
    void applyDarkTheme();

    // Panels
    QDockWidget* m_projectDock;
    QDockWidget* m_hexDock;
    QDockWidget* m_mapEditorDock;
    QDockWidget* m_paramDock;
    QDockWidget* m_gitDock;
    QDockWidget* m_mppsDock;
    QDockWidget* m_autoModsDock;
    QDockWidget* m_checksumDock;
    QDockWidget* m_compareDock;
    QDockWidget* m_aliasDock;

    // Core
    std::shared_ptr<ecu::ProjectManager> m_projectMgr;
    std::unique_ptr<mpps::MppsDevice>    m_mppsDevice;
};
```

### [SONNET] MppsPanel — UI complète

```cpp
// apps/ecu-studio/src/panels/MppsPanel.cpp
// Panel de connexion et d'opérations MPPS
// Layout :
// ┌─ Connexion ────────────────────────────────┐
// │ [🔍 Scanner] [Device: FTDI FT232R ▼] [▶ Connecter] │
// │ Status: ● Connecté — FTDI FT232R — fw 2.1.3        │
// │ ECU identifié: EDC16C34 (1.6 HDi 110ch)            │
// ├─ Opérations ───────────────────────────────┤
// │ Protocole: [K-Line ▼]  Baud: [38400 ▼]            │
// │ [📥 Lire ROM]  [📤 Écrire ROM]  [🗑 Effacer]       │
// │ ████████████████░░░░░░  67% — Lecture 0x1AC000     │
// ├─ Journal ──────────────────────────────────┤
// │ 14:32:01 [OK] Connexion FTDI FT232R        │
// │ 14:32:02 [OK] ECU identifié: EDC16C34      │
// │ 14:32:03 [>>] Lecture ROM démarrée (2Mo)   │
// └────────────────────────────────────────────┘

void MppsPanel::onReadRom() {
    // Lancer dans un QThread pour ne pas bloquer l'UI
    auto* worker = new QThread(this);
    connect(worker, &QThread::started, [this, worker]() {
        auto cb = [this](uint32_t done, uint32_t total, const std::string& msg) {
            // Signal vers le thread UI via Qt::QueuedConnection
            emit progressUpdated((int)(done * 100 / total), QString::fromStdString(msg));
        };

        auto result = m_device->readFullRom(0x200000, cb);

        if (result) {
            // ROM lue avec succès → injecter dans le projet courant
            QByteArray romData(reinterpret_cast<const char*>(result->data()),
                               result->size());
            emit romReadComplete(romData);
        } else {
            emit operationFailed(QString::fromStdString(
                mpps::to_string(result.error())));
        }
        worker->quit();
    });
    worker->start();
}
```

### [HAIKU] HexView — QAbstractScrollArea virtuel

```cpp
// apps/ecu-studio/src/widgets/HexView.hpp
// Virtual scrolling hex editor pour ROMs 2-6 Mo
// QPainter direct, pas de QTextEdit, pas de QTableWidget
// Performance cible : 60 fps sur 6Mo (Simos18)

class HexView : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr int COLS       = 16;
    static constexpr int ROW_H      = 17;   // px
    static constexpr int ADDR_W     = 80;   // px colonne adresse
    static constexpr int HEX_CW     = 25;   // px par octet hex
    static constexpr int ASCII_CW   = 9;    // px par char ASCII

    void setData(std::shared_ptr<QByteArray> data);
    void setMapRanges(const QList<MapRange>& ranges);  // overlay coloré
    void gotoOffset(uint32_t offset);
    uint32_t cursorOffset() const { return m_cursor; }

signals:
    void cursorMoved(uint32_t offset, uint8_t value);
    void selectionChanged(uint32_t start, uint32_t length);
    void bytesEdited(uint32_t offset, const QByteArray& newData);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    std::shared_ptr<QByteArray> m_data;
    uint32_t m_cursor  = 0;
    uint32_t m_selStart = 0, m_selEnd = 0;
    QList<MapRange> m_mapRanges;
    QFont m_font{"Monospace", 10};

    int  totalRows() const;
    int  visibleRows() const;
    int  firstRow() const;
    uint32_t offsetFromPos(const QPoint& pos) const;
    QColor colorForOffset(uint32_t offset) const;  // heatmap + map overlay
    void updateScrollBar();
    void renderRow(QPainter& p, int row, int y);
};
```

---

## PHASE 5 — INTÉGRATION SOCKETSPY [SONNET]

Dans `apps/socketspy/`, ajouter un panel ECU Flash minimal.

```bash
# Modifier CMakeLists de SocketSpy pour linker mpps et ecu-core
# Ajouter EcuFlashPanel dans la sidebar SocketSpy
# Le panel partage le même MppsDevice que ECU Studio via un singleton Qt

# Avantage unique de l'intégration SocketSpy :
# Pendant un flash MPPS, le monitor CAN de SocketSpy reste actif
# → on voit les trames K-line/CAN OBD pendant l'opération
# → utile pour débugger des ECUs récalcitrants
```

---

## PHASE 6 — TESTS [HAIKU + SONNET]

### [HAIKU] Structure des tests

```bash
mkdir -p tests/unit tests/integration tests/fixtures

# Fixtures : créer des ROMs synthétiques pour les tests
# tests/fixtures/edc16c34_synthetic.bin → 2Mo de 0x00 avec checksum correct
# tests/fixtures/me7_vag_synthetic.bin → 512Ko
```

### [SONNET] Tests critiques à implémenter

```cpp
// tests/unit/test_checksum.cpp
// tests/unit/test_rom_patcher.cpp
// tests/unit/test_a2l_parser.cpp
// tests/unit/test_map_finder.cpp
// tests/unit/test_open_damos.cpp
// tests/unit/test_mpps_sim.cpp — teste toute l'API MPPS via simulation
// tests/integration/test_mpps_real.cpp — nécessite hardware, optionnel
```

---

## PHASE 7 — PACKAGING [HAIKU]

```bash
# Linux AppImage (même que SocketSpy)
# scripts/build_appimage.sh

# Windows (cross-compile depuis Linux)
# cmake -DCMAKE_TOOLCHAIN_FILE=cmake/windows-cross.cmake
# cpack -G NSIS

# Installer les règles udev sur Linux :
sudo cp tools/60-mpps.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

---

## ORDRE D'EXÉCUTION GLOBAL

```
MAINTENANT (sans l'exe MPPS) :
  [HAIKU]  Phase 0 — mono-repo, CMake, udev, vcpkg.json
  [HAIKU]  Phase 1 partielle — outils de reverse, proxy DLL compilé
  [HAIKU]  Phase 3 — portage mécanique : EcuCatalog, VehicleTemplates
  [SONNET] Phase 3 — portage complexe : ChecksumEngine, RomPatcher, MapFinder, OpenDamos, MapDiffer, ProjectManager, GitManager
  [OPUS]   Phase 3 — A2lParser (tâche la plus complexe du portage)
  [SONNET] Phase 2 — MppsDevice squelette + simulation
  [SONNET] Phase 4 — UI ECU Studio complète (MainWindow + tous panels)
  [HAIKU]  Phase 6 — structure tests + fixtures
  [SONNET] Phase 6 — implémentation tests
  [HAIKU]  Phase 7 — scripts packaging

QUAND L'EXE MPPS ARRIVE :
  [HAIKU]  identify_exe.py → identifier compilateur + API USB
  [OPUS]   Reverse engineering selon la techno identifiée
  [OPUS]   parse_capture.py sur le log proxy DLL
  [SONNET] Compléter tous les TODO_REVERSE dans MppsDevice_libusb.cpp
  [SONNET] Tests intégration avec hardware réel
  [HAIKU]  Packaging final

VALIDATION FINALE :
  Tester sur un vrai Berlingo EDC16C34 :
  1. Scanner → détecter le MPPS
  2. Connecter → identifier EDC16C34
  3. Lire ROM 2Mo → vérifier intégrité vs sauvegarde connue
  4. Patch 1 byte → écrire → vérifier checksum auto-corrigé
  5. Restaurer ROM originale
```

---

## NOTES DE DÉVELOPPEMENT

### Sur le modèle à utiliser
Haiku pour tout ce qui est mécanique et répétitif — il est plus rapide et moins cher.
Sonnet pour l'ingénierie standard — bon équilibre.
Opus uniquement pour le reverse (analyse binaire, reconstruction protocole, A2lParser)
car ces tâches nécessitent un raisonnement profond sur du code non documenté.

### Sur les TODO_REVERSE
Chaque `TODO_REVERSE` dans le code est un point bloquant validé uniquement
par l'analyse du log proxy DLL. Ne pas inventer les valeurs — les laisser
comme constantes nommées avec une valeur placeholder et un commentaire clair.
L'exe MPPS révélera les valeurs exactes.

### Sur la compatibilité K-line vs CAN
Le MPPS V21 auto-détecte K-line ou CAN selon l'ECU.
Pour l'EDC16C34 PSA : K-line 10400 baud (initialisation 5-baud) puis lecture.
Pour MED17/EDC17 VAG : CAN 500 kbps, protocole UDS ISO 14229.
Les deux modes passent par le même protocole MPPS niveau câble —
seul le CMD_KLINE_INIT vs CMD_CAN_INIT change, puis les trames ECU
encapsulées dans les payloads MPPS.

### Sur la sécurité des écritures ROM
Implémenter une double vérification obligatoire avant tout write :
1. Backup automatique de la ROM originale si pas déjà fait
2. Checksum de la ROM à écrire validé avant envoi
3. Vérification post-écriture : relire et comparer
4. Timeout watchdog : si write interrompu → tenter recovery
Un ECU bricqué par un write raté est difficilement récupérable sans
programmer JTAG/BDM — la prudence est absolue ici.
