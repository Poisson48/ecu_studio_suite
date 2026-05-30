#!/bin/bash
# build.sh — Build rapide ECU Studio sur Linux
set -e
cd "$(dirname "$0")"

BUILD_DIR="${1:-build}"
BUILD_TYPE="${2:-Release}"

echo "=== ECU Studio Suite — Build Linux ==="
echo "Build dir : $BUILD_DIR"
echo "Type      : $BUILD_TYPE"
echo ""

# Vérifier dépendances
check_dep() {
    if ! pkg-config --exists "$1" 2>/dev/null; then
        echo "MANQUANT: $1 — installer avec: sudo apt install ${2:-$1}"
        MISSING=1
    fi
}

MISSING=0
check_dep "Qt6Core"     "qt6-base-dev"
check_dep "libusb-1.0"  "libusb-1.0-0-dev"
check_dep "libgit2"     "libgit2-dev"
check_dep "lua5.4"      "liblua5.4-dev"

if [ "$MISSING" = "1" ]; then
    echo ""
    echo "Installer toutes les dépendances :"
    echo "  sudo apt install qt6-base-dev qt6-charts-dev qt6-serialbus-dev qt6-serialport-dev \\"
    echo "                   libusb-1.0-0-dev libgit2-dev liblua5.4-dev \\"
    echo "                   nlohmann-json3-dev libgtest-dev cmake ninja-build"
    exit 1
fi

cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DECU_MPPS_SIMULATION=ON \
    -DECU_BUILD_TESTS=OFF \
    -G Ninja

cmake --build "$BUILD_DIR" --target ecu_studio -j"$(nproc)"

echo ""
echo "=== Build terminé ==="
echo "Lancer : ./$BUILD_DIR/apps/ecu-studio/ecu_studio"
