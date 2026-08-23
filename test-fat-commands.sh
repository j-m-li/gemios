#!/bin/bash
# GEMIOS RTOS - Comprehensive FATLS & FATCAT Test Suite
# Tests both FAT16 and FAT32 filesystems
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Starting GEMIOS FATLS & FATCAT Verification Test Suite"
echo "=========================================================="

echo "[1/3] Building kernel and test disk images..."
make clean
make

FAT16_LOG="build/test_fat16_commands.log"
FAT32_LOG="build/test_fat32_commands.log"

echo "[2/3] Running FAT16 & FAT32 tests in QEMU VM..."

# Test FAT16
python3 -c "
import subprocess, time, select, os

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_disk.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-mouse,bus=xhci.0,port=4.1'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

all_output = bytearray()
def read_more(timeout=0.1):
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([proc.stdout], [], [], 0.05)
        if r:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            all_output.extend(chunk)

def wait_prompt(timeout=8.0):
    start = time.time()
    while time.time() - start < timeout:
        read_more(0.05)
        if b'gemios> ' in all_output[-30:]:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.5)
    prompt_idx = len(all_output)
    proc.stdin.write(cmd_str.encode('utf-8'))
    proc.stdin.flush()
    start = time.time()
    while time.time() - start < 8.0:
        read_more(0.05)
        if b'gemios> ' in all_output[prompt_idx:]:
            return True
    return False

wait_prompt(timeout=6.0)

for cmd_text in ['fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls usb0\n', 'fatcat README.TXT\n', 'fatcat STATUS.TXT\n', 'fatcat NONEXIST.TXT\n', 'fatls baddev\n']:
    send_cmd(cmd_text)

read_more(1.0)
proc.terminate()
try:
    proc.wait(timeout=2.0)
except Exception:
    proc.kill()
read_more(0.5)

out = all_output.decode('utf-8', errors='replace')
with open('$FAT16_LOG', 'w') as f:
    f.write(out)
"

# Test FAT32
python3 -c "
import subprocess, time, select, os

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
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

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

all_output = bytearray()
def read_more(timeout=0.1):
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([proc.stdout], [], [], 0.05)
        if r:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            all_output.extend(chunk)

def wait_prompt(timeout=8.0):
    start = time.time()
    while time.time() - start < timeout:
        read_more(0.05)
        if b'gemios> ' in all_output[-30:]:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.5)
    prompt_idx = len(all_output)
    proc.stdin.write(cmd_str.encode('utf-8'))
    proc.stdin.flush()
    start = time.time()
    while time.time() - start < 8.0:
        read_more(0.05)
        if b'gemios> ' in all_output[prompt_idx:]:
            return True
    return False

wait_prompt(timeout=6.0)

for cmd_text in ['fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls\n', 'fatls usb0\n', 'fatcat README.TXT\n', 'fatcat FAT32DOC.TXT\n', 'fatcat STATUS.TXT\n', 'fatcat NOFILE.TXT\n']:
    send_cmd(cmd_text)

read_more(1.0)
proc.terminate()
try:
    proc.wait(timeout=2.0)
except Exception:
    proc.kill()
read_more(0.5)

out = all_output.decode('utf-8', errors='replace')
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
grep -q "Welcome to GEMIOS RTOS on USB Mass Storage (FAT16)" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat read README.TXT" || { echo "[FAIL] FAT16 fatcat README"; exit 1; }
grep -q "All systems operational" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat read STATUS.TXT" || { echo "[FAIL] FAT16 fatcat STATUS"; exit 1; }
grep -q "File 'NONEXIST.TXT' not found" "$FAT16_LOG" && echo "[PASS] FAT16: fatcat handled missing file error" || { echo "[FAIL] FAT16 missing file"; exit 1; }
grep -q "Block device 'baddev' not found" "$FAT16_LOG" && echo "[PASS] FAT16: fatls handled invalid device error" || { echo "[FAIL] FAT16 invalid device"; exit 1; }

# FAT32 Assertions
grep -q "Filesystem on usb0 (FAT32)" "$FAT32_LOG" && echo "[PASS] FAT32: fatls detected FAT32" || { echo "[FAIL] FAT32 fatls"; exit 1; }
grep -q "Welcome to GEMIOS RTOS on USB Mass Storage (FAT32)" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read README.TXT" || { echo "[FAIL] FAT32 fatcat README"; exit 1; }
grep -q "Supports cluster traversal and 32-bit FAT table lookups" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read FAT32DOC.TXT" || { echo "[FAIL] FAT32 fatcat FAT32DOC"; exit 1; }
grep -q "FAT32 verification passed" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat read STATUS.TXT" || { echo "[FAIL] FAT32 fatcat STATUS"; exit 1; }
grep -q "File 'NOFILE.TXT' not found" "$FAT32_LOG" && echo "[PASS] FAT32: fatcat handled missing file error" || { echo "[FAIL] FAT32 missing file"; exit 1; }

echo ""
echo ">>> ALL FATLS & FATCAT TESTS PASSED SUCCESSFULLY ON BOTH FAT16 AND FAT32! <<<"
