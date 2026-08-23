#!/bin/bash
# GEMIOS RTOS - FAT32 Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "[1/3] Building GEMIOS RTOS and 64MB FAT32 Disk..."
make clean
make

echo "[2/3] Running FAT32 Verification Tests in QEMU VM..."
OUTPUT_LOG="build/test_fat32_output.log"
SERIAL_LOG="build/serial_fat32_output.log"
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
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_fat32.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-mouse,bus=xhci.0,port=4.1'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log_file, stderr=subprocess.STDOUT, text=True)

time.sleep(1.2) # Wait for boot

commands = [
    'storage',
    'ls usb0',
    'cat usb0 README.TXT',
    'cat usb0 STATUS.TXT',
    'cat usb0 FAT32DOC.TXT'
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
    time.sleep(0.2)

time.sleep(1.5)
proc.terminate()
proc.wait()
log_file.close()
"

echo "[3/3] Verifying FAT32 Test Results..."
echo "=========================================================="
cat "$SERIAL_LOG"
echo "=========================================================="

# Assertions
grep -q "USB xHCI Controller" "$SERIAL_LOG" && echo "[PASS] xHCI Controller detected & initialized" || { echo "[FAIL] xHCI Controller"; exit 1; }
grep -q "Capacity: 131072 blocks" "$SERIAL_LOG" && echo "[PASS] 64MB USB Mass Storage detected" || { echo "[FAIL] Storage Capacity"; exit 1; }
grep -q "Filesystem on usb0 (FAT32)" "$SERIAL_LOG" && echo "[PASS] Filesystem detected as FAT32" || { echo "[FAIL] FAT32 detection"; exit 1; }
grep -q "FAT32DOC.TXT" "$SERIAL_LOG" && echo "[PASS] FAT32 root directory listed files" || { echo "[FAIL] FAT32 directory list"; exit 1; }
grep -q "Supports cluster traversal and 32-bit FAT table lookups" "$SERIAL_LOG" && echo "[PASS] Successfully read FAT32 file contents" || { echo "[FAIL] FAT32 file read"; exit 1; }

echo ""
echo ">>> ALL FAT32 TESTS PASSED SUCCESSFULLY! <<<"
