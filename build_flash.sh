#!/bin/bash
# Build PX4 for PIC32CZ CA70, generate HEX+ELF, copy to Windows
set -e

export PATH=$HOME/.local/bin:$PATH

BUILD_TARGET="microchip_samv71-xult-clickboards_default"
BUILD_DIR="build/${BUILD_TARGET}"
WIN_DIR="/mnt/c/Users/I73780"

# Clean build if --clean flag passed
if [ "$1" = "--clean" ]; then
    echo "=== Clean build ==="
    rm -rf ${BUILD_DIR}
fi

echo "=== Building PX4 for PIC32CZ CA70 ==="
make ${BUILD_TARGET}

echo "=== Generating HEX ==="
arm-none-eabi-objcopy -O ihex ${BUILD_DIR}/${BUILD_TARGET}.elf ${BUILD_DIR}/${BUILD_TARGET}.hex

echo "=== Copying to Windows ==="
cp ${BUILD_DIR}/${BUILD_TARGET}.elf ${WIN_DIR}/
cp ${BUILD_DIR}/${BUILD_TARGET}.hex ${WIN_DIR}/

echo ""
echo "=== BUILD SUCCESS ==="
echo "ELF: ${WIN_DIR}/${BUILD_TARGET}.elf"
echo "HEX: ${WIN_DIR}/${BUILD_TARGET}.hex"
echo "Flash via MPLAB IPE: Device=PIC32CZ2051CA70144 or ATSAMV71Q21B"
