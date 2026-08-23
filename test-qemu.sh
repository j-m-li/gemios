#!/bin/bash
# GEMIOS RTOS - Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "[1/3] Building GEMIOS RTOS..."
make clean
make

echo "[2/3] Running Interactive & Automated Tests in QEMU VM..."
OUTPUT_LOG="build/test_output.log"
SERIAL_LOG="build/serial_output.log"
rm -f "$OUTPUT_LOG" "$SERIAL_LOG"

python3 -c "
import subprocess, time, os, sys

log_file = open('$OUTPUT_LOG', 'w')
cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', 'file:$SERIAL_LOG',
    '-monitor', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-device', 'usb-mouse,bus=xhci.0,port=2',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_disk.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-mouse,bus=xhci.0,port=4.1'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log_file, stderr=subprocess.STDOUT, text=True)

time.sleep(2.0) # Wait for boot & USB device enumeration

# Send test commands via keyboard
commands = [
    'help',
    'ps',
    'mem',
    'pci',
    'lsusb',
    'storage',
    'readsec usb0 0',
    'ls usb0',
    'cat usb0 README.TXT',
    'mouse',
    'uptime'
]

for cmd_text in commands:
    for ch in cmd_text:
        if ch == ' ':
            proc.stdin.write('sendkey spc\n')
        elif ch == '.':
            proc.stdin.write('sendkey dot\n')
        elif ch == '/':
            proc.stdin.write('sendkey slash\n')
        elif ch == '-':
            proc.stdin.write('sendkey minus\n')
        elif ch >= 'A' and ch <= 'Z':
            proc.stdin.write(f'sendkey shift-{ch.lower()}\n')
        else:
            proc.stdin.write(f'sendkey {ch}\n')
        proc.stdin.flush()
        time.sleep(0.04)
    proc.stdin.write('sendkey ret\n')
    proc.stdin.flush()
    time.sleep(0.3)

time.sleep(2.0)
proc.terminate()
proc.wait()
log_file.close()
"

echo "[3/3] Verifying Test Results..."
echo "=========================================================="
cat "$SERIAL_LOG"
echo "=========================================================="

# Assertions
grep -q "USB xHCI Controller" "$SERIAL_LOG" && echo "[PASS] xHCI Controller detected & initialized" || { echo "[FAIL] xHCI Controller"; exit 1; }
grep -q "Bound USB Keyboard" "$SERIAL_LOG" && echo "[PASS] USB HID Keyboard driver bound" || { echo "[FAIL] USB HID Keyboard"; exit 1; }
grep -q "Bound USB Mouse" "$SERIAL_LOG" && echo "[PASS] USB HID Mouse driver bound" || { echo "[FAIL] USB HID Mouse"; exit 1; }
grep -q "USB Mass Storage initialized" "$SERIAL_LOG" && echo "[PASS] USB Mass Storage driver initialized" || { echo "[FAIL] USB Mass Storage"; exit 1; }
grep -q "Initialized USB Hub" "$SERIAL_LOG" && echo "[PASS] USB Hub driver initialized" || { echo "[FAIL] USB Hub"; exit 1; }
grep -q "device on Hub Slot" "$SERIAL_LOG" && echo "[PASS] Downstream device on USB Hub enumerated" || { echo "[FAIL] USB Hub Downstream device"; exit 1; }
grep -q "Registered 'usb0'" "$SERIAL_LOG" && echo "[PASS] Block device 'usb0' registered" || { echo "[FAIL] Block device registration"; exit 1; }
grep -q "GEMIOS RTOS Interactive Console Ready" "$SERIAL_LOG" && echo "[PASS] Interactive Shell task running" || { echo "[FAIL] Shell task"; exit 1; }
grep -q "README.TXT" "$SERIAL_LOG" && echo "[PASS] Keyboard typing & FAT command execution verified" || { echo "[FAIL] FAT command execution"; exit 1; }

echo ""
echo ">>> ALL GEMIOS RTOS TESTS PASSED SUCCESSFULLY! KEYBOARD FULLY OPERATIONAL! <<<"
