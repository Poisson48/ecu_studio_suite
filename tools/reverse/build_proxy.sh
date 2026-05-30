#!/bin/bash
# build_proxy.sh — Compile le proxy DLL ftd2xx pour interception MPPS
set -e
cd "$(dirname "$0")"

echo "Compilation ftd2xx proxy DLL..."

x86_64-w64-mingw32-gcc \
    -shared \
    -o ftd2xx.dll \
    ftd2xx_proxy.c \
    -Wl,--kill-at \
    -static-libgcc

echo ""
echo "Instructions d'utilisation :"
echo "1. Copier ftd2xx.dll dans le dossier de MPPS_V21.exe"
echo "2. Renommer l'original ftd2xx.dll → ftd2xx_real.dll dans ce même dossier"
echo "3. Lancer MPPS_V21.exe"
echo "4. Faire : init → connecter ECU → lire ROM → écrire ROM"
echo "5. Récupérer mpps_protocol_capture.log"
echo "6. Analyser : python3 parse_capture.py mpps_protocol_capture.log"
