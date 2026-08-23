#!/bin/bash
# GEMOS RTOS - Comprehensive FATLS & FATCAT Test Suite
# Tests both FAT16 and FAT32 filesystems
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Starting GEMOS FATLS & FATCAT Verification Test Suite"
echo "=========================================================="

echo "[1/3] Building kernel and test disk images..."
make clean
make

FAT16_LOG="build/test_fat16_commands.log"
FAT32_LOG="build/test_fat32_commands.log"

echo "[2/3] Running FAT16 & FAT32 tests in QEMU VM..."

# Test FAT16
python3 -c "
import subprocess, time

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemos.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-device', 'usb-mouse,bus=xhci.0,port=2',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_disk.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-mouse,bus=xhci.0,port=4.1'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
time.sleep(1.8)

for cmd_text in ['fatls\n', 'fatls usb0\n', 'fatcat README.TXT\n', 'fatcat STATUS.TXT\n', 'fatcat NONEXIST.TXT\n', 'fatls baddev\n']:
    proc.stdin.write(cmd_text)
    proc.stdin.flush()
    time.sleep(0.7)

time.sleep(3.0)
proc.terminate()
out, _ = proc.communicate()
with open('$FAT16_LOG', 'w') as f:
    f.write(out)
"

# Test FAT32
python3 -c "
import subprocess, time

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemos.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-device', 'usb-mouse,bus=xhci.0,port=2',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_fat32.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-mouse,bus=xhci.0,port=4.1'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
time.sleep(1.8)

for cmd_text in ['fatls\n', 'fatls usb0\n', 'fatcat README.TXT\n', 'fatcat FAT32DOC.TXT\n', 'fatcat STATUS.TXT\n', 'fatcat NOFILE.TXT\n']:
    proc.stdin.write(cmd_text)
    proc.stdin.flush()
    time.sleep(0.7)

time.sleep(5.0)
proc.terminate()
out, _ = proc.communicate()
with open('$FAT32_LOG', 'w') as f:
    f.write(out)
"

echo "[3/3] Verifying Test Results & Assertions..."
echo "----------------------------------------------------------"
echo "=== FAT16 Test Log ==="
cat "$FAT16_LOG"
echo "----------------------------------------------------------"
echo "=== FAT32 Test Log ==="
cat "$FAT32_LOG"
echo "----------------------------------------------------------"

# FAT16 Assertions
grep -q "Filesystem on usb0 (FAT16)" "$FAT16_LOG" && echo "[PASS] FAT16: fatls detected FAT16" || { echo "[FAIL] FAT16 fatls"; exit 1; }
grep -q "Welcome to GEMOS RTOS on USB Mass Storage (FAT16)" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat read README.TXT" || { echo "[FAIL] FAT16 fatcat README"; exit 1; }
grep -q "All systems operational" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat read STATUS.TXT" || { echo "[FAIL] FAT16 fatcat STATUS"; exit 1; }
grep -q "File 'NONEXIST.TXT' not found" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat handled missing file error" || { echo "[FAIL] FAT16 missing file"; exit 1; }
grep -q "Block device 'baddev' not found" "$FAT16_LOG" && echo "[PASS] FAT16: fatls handled invalid device error" || { echo "[FAIL] FAT16 invalid device"; exit 1; }

# FAT32 Assertions
grep -q "Filesystem on usb0 (FAT32)" "$FAT32_LOG" && echo "[PASS] FAT32: fatls detected FAT32" || { echo "[FAIL] FAT32 fatls"; exit 1; }
grep -q "Welcome to GEMOS RTOS on USB Mass Storage (FAT32)" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read README.TXT" || { echo "[FAIL] FAT32 fatcat README"; exit 1; }
grep -q "Supports cluster traversal and 32-bit FAT table lookups" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read FAT32DOC.TXT" || { echo "[FAIL] FAT32 fatcat FAT32DOC"; exit 1; }
grep -q "FAT32 verification passed" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read STATUS.TXT" || { echo "[FAIL] FAT32 fatcat STATUS"; exit 1; }
grep -q "File 'NOFILE.TXT' not found" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat handled missing file error" || { echo "[FAIL] FAT32 missing file"; exit 1; }

echo ""
echo ">>> ALL FATLS & FATCAT TESTS PASSED SUCCESSFULLY ON BOTH FAT16 AND FAT32! <<<"
