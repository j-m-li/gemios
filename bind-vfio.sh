#!/bin/bash
# GEMIOS RTOS - Bind Thunderbolt USB Controller (37:00.0) to vfio-pci
# Must be run as root (e.g. sudo ./bind-vfio.sh)

set -e

if [ "$EUID" -ne 0 ]; then
  echo "Error: Please run as root: sudo ./bind-vfio.sh"
  exit 1
fi

echo "[*] Loading vfio-pci module..."
modprobe vfio-pci

DEV="0000:37:00.0"
VENDOR_DEVICE="8086 15d4"

echo "[*] Unbinding $DEV from current driver (xhci_hcd)..."
if [ -e "/sys/bus/pci/devices/$DEV/driver/unbind" ]; then
    echo "$DEV" > "/sys/bus/pci/devices/$DEV/driver/unbind" || true
fi

echo "[*] Adding PCI ID ($VENDOR_DEVICE) to vfio-pci..."
echo "$VENDOR_DEVICE" > /sys/bus/pci/drivers/vfio-pci/new_id 2>/dev/null || true

echo "[*] Binding $DEV to vfio-pci..."
if [ ! -e "/sys/bus/pci/drivers/vfio-pci/$DEV" ]; then
    echo "$DEV" > /sys/bus/pci/drivers/vfio-pci/bind || true
fi

echo "[*] Adjusting /dev/vfio permissions for user..."
chmod 666 /dev/vfio/vfio
if [ -d "/dev/vfio" ]; then
    chmod -R 666 /dev/vfio/* 2>/dev/null || true
fi

echo "[*] Setting memlock limits for VFIO DMA in /etc/security/limits.d/99-vfio.conf..."
cat << 'LIMITS' > /etc/security/limits.d/99-vfio.conf
* soft memlock unlimited
* hard memlock unlimited
root soft memlock unlimited
root hard memlock unlimited
LIMITS

echo "=========================================================="
echo " Device $DEV successfully bound to vfio-pci!"
echo " Driver: $(lspci -k -s $DEV | grep 'Kernel driver')"
echo "=========================================================="
