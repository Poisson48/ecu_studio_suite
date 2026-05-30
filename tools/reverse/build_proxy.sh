#!/bin/bash
# build_proxy.sh — Compile the ftd2xx capture proxy DLL for MPPS interception.
#
# Mpps.exe is 32-bit (i386) -> the proxy MUST be 32-bit. We use the .def file so
# that the loader forwards all 71 non-hooked ftd2xx exports to ftd2xx_real.dll,
# while the 14 hooked functions in ftd2xx_proxy.c do the logging.
set -e
cd "$(dirname "$0")"

CC=i686-w64-mingw32-gcc
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "ERROR: $CC not found."
    echo "  Linux:   sudo apt install gcc-mingw-w64-i686"
    echo "  Windows: cl /LD ftd2xx_proxy.c /Fe:ftd2xx.dll ftd2xx_proxy.def  (MSVC, x86 prompt)"
    exit 1
fi

echo "Compiling 32-bit ftd2xx proxy DLL with $CC ..."
"$CC" \
    -shared \
    -m32 \
    -o ftd2xx.dll \
    ftd2xx_proxy.c \
    ftd2xx_proxy.def \
    -Wl,--enable-stdcall-fixup \
    -static-libgcc

echo "Built ftd2xx.dll ($(file ftd2xx.dll | sed 's/.*: //'))"
echo ""
echo "Usage (on the Windows machine with the MPPS dongle):"
echo "  1. In the MPPS folder, rename the ORIGINAL ftd2xx.dll -> ftd2xx_real.dll"
echo "  2. Copy THIS ftd2xx.dll into the MPPS folder (next to Mpps.exe)"
echo "  3. Run Mpps.exe and perform: init -> identify ECU -> read ROM ->"
echo "     write 1 byte -> restore"
echo "  4. Send back mpps_protocol_capture.log"
echo "  5. Analyse: python3 parse_capture.py mpps_protocol_capture.log"
echo ""
echo "See tools/reverse/CAPTURE_SOP.md for the full procedure."
