#!/bin/bash
# Build, generate HEX, and copy to Windows
set -e

BUILD_DIR="build/microchip_samv71-xult-clickboards_default"
FW_NAME="microchip_samv71-xult-clickboards_default"
WIN_DIR="/mnt/c/Users/I73780"

echo "=== Building PX4 for SAMV71 ==="
make microchip_samv71-xult-clickboards_default

echo "=== Generating HEX ==="
arm-none-eabi-objcopy -O ihex ${BUILD_DIR}/${FW_NAME}.elf ${BUILD_DIR}/${FW_NAME}.hex

echo "=== Copying to Windows ==="
cp ${BUILD_DIR}/${FW_NAME}.elf ${WIN_DIR}/
cp ${BUILD_DIR}/${FW_NAME}.hex ${WIN_DIR}/

echo "=== Done ==="
echo "ELF: ${WIN_DIR}/${FW_NAME}.elf"
echo "HEX: ${WIN_DIR}/${FW_NAME}.hex"
