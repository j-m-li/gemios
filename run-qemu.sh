#!/bin/bash
# GEMOS RTOS - QEMU Virtual Machine Launch Script
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

DISK_FILE="build/test_disk.img"
GRAPHICS_FLAG="-serial stdio"

for arg in "$@"; do
    if [ "$arg" == "--fat32" ]; then
        DISK_FILE="build/test_fat32.img"
    elif [ "$arg" == "--nographic" ] || [ "$arg" == "-nographic" ]; then
        GRAPHICS_FLAG="-nographic"
    elif [ "$arg" == "--headless" ]; then
        GRAPHICS_FLAG="-display none -serial stdio"
    fi
done

# Build kernel and disk image if needed
if [ ! -f "build/gemios.elf" ] || [ ! -f "$DISK_FILE" ]; then
    echo "[*] Building GEMOS RTOS and disk images..."
    make
fi

QEMU="qemu-system-i386"
if ! command -v $QEMU &> /dev/null; then
    QEMU="qemu-system-x86_64"
fi

echo "=========================================================="
echo " Starting GEMOS x86-32 Preemptive RTOS in QEMU VM"
echo " Configuration:"
echo "   - Host Controller: USB 3.0 xHCI"
echo "   - Disk Image:      $DISK_FILE"
echo "   - Devices:         USB Keyboard, USB Mouse, USB Storage, USB Hub"
echo "   - Downstream:      Secondary USB Mouse on USB Hub"
echo "=========================================================="

exec $QEMU -kernel build/gemios.elf -m 256M \
    $GRAPHICS_FLAG \
    -device qemu-xhci,id=xhci,p2=8,p3=8 \
    -device usb-kbd,bus=xhci.0,port=1 \
    -device usb-mouse,bus=xhci.0,port=2 \
    -drive if=none,id=usbstick,format=raw,file=$DISK_FILE \
    -device usb-storage,bus=xhci.0,port=3,drive=usbstick \
    -device usb-hub,bus=xhci.0,port=4 \
    -device usb-mouse,bus=xhci.0,port=4.1
