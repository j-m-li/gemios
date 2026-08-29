#!/bin/bash
# GEMIOS RTOS - Launch QEMU with Physical Thunderbolt USB Controller (37:00.0)
# Must be run as root or with sudo for VFIO memory locking (memlock)

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if [ "$EUID" -ne 0 ]; then
  echo "Notice: VFIO memory locking typically requires root privileges."
  echo "Running with sudo..."
  exec sudo "$0" "$@"
fi

if [ ! -f "build/gemios.elf" ]; then
    echo "[*] Building GEMIOS RTOS..."
    make
fi

# Ensure device is bound to vfio-pci
if ! lspci -k -s 0000:37:00.0 | grep -q "vfio-pci"; then
    echo "[*] Binding 37:00.0 to vfio-pci..."
    ./bind-vfio.sh
fi

echo "=========================================================="
echo " Starting GEMIOS RTOS with Physical Thunderbolt Controller"
echo " Controller:  PCI 37:00.0 (Intel JHL6540 xHCI)"
echo " Attached:    External USB Hub (2109:0813) & Physical Disk"
echo "=========================================================="

exec qemu-system-i386 \
    -kernel build/gemios.elf \
    -m 256M \
    -enable-kvm \
    -device vfio-pci,host=37:00.0 \
    -serial stdio \
    -vga std \
    "$@"
