#!/bin/bash
# GEMIOS RTOS - Automated fatmkdir Verification Test Suite
# Tests directory creation on FAT16 and FAT32 filesystems
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Starting GEMIOS fatmkdir Verification Test Suite"
echo "=========================================================="

echo "[1/3] Building kernel and fresh test disk images..."
make clean
make

FAT16_LOG="build/test_fat16_mkdir.log"
FAT32_LOG="build/test_fat32_mkdir.log"

echo "[2/3] Running fatmkdir tests in QEMU VM..."

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
    '-device', 'usb-mouse,bus=xhci.0,port=2',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_disk.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
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
        if b'gemios> ' in all_output:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.3)
    prompt_idx = len(all_output)
    if isinstance(cmd_str, str):
        cmd_str = cmd_str.encode('utf-8')
    for i in range(0, len(cmd_str), 4):
        try:
            proc.stdin.write(cmd_str[i:i+4])
            proc.stdin.flush()
        except Exception:
            return False
        time.sleep(0.03)
    start = time.time()
    while time.time() - start < 8.0:
        read_more(0.05)
        if b'gemios> ' in all_output[prompt_idx:]:
            return True
    return False

wait_prompt(timeout=6.0)

send_cmd('fatls usb0\n')
send_cmd('fatmkdir usb0 TESTDIR\n')
send_cmd('fatls usb0\n')
send_cmd('fatmkdir usb0 TESTDIR\n')
send_cmd('fatmkdir usb0 DOCS\n')
send_cmd('fatls usb0\n')

read_more(1.5)
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
        if b'gemios> ' in all_output:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.3)
    prompt_idx = len(all_output)
    if isinstance(cmd_str, str):
        cmd_str = cmd_str.encode('utf-8')
    for i in range(0, len(cmd_str), 4):
        try:
            proc.stdin.write(cmd_str[i:i+4])
            proc.stdin.flush()
        except Exception:
            return False
        time.sleep(0.03)
    start = time.time()
    while time.time() - start < 8.0:
        read_more(0.05)
        if b'gemios> ' in all_output[prompt_idx:]:
            return True
    return False

wait_prompt(timeout=6.0)

send_cmd('fatls usb0\n')
send_cmd('fatmkdir usb0 FAT32DIR\n')
send_cmd('fatls usb0\n')
send_cmd('fatmkdir usb0 FAT32DIR\n')

read_more(1.5)
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
echo "=== FAT16 mkdir Log ==="
cat "$FAT16_LOG"
echo "----------------------------------------------------------"
echo "=== FAT32 mkdir Log ==="
cat "$FAT32_LOG"
echo "----------------------------------------------------------"

# Assertions for FAT16
grep -q "Directory 'TESTDIR' created successfully" "$FAT16_LOG" && echo "[PASS] FAT16: Created TESTDIR successfully" || { echo "[FAIL] FAT16 TESTDIR creation"; exit 1; }
grep -q "TESTDIR          <DIR>" "$FAT16_LOG" && echo "[PASS] FAT16: TESTDIR listed with <DIR> attribute" || { echo "[FAIL] FAT16 TESTDIR listing"; exit 1; }
grep -q "Directory or file 'TESTDIR' already exists" "$FAT16_LOG" && echo "[PASS] FAT16: Duplicate directory detection" || { echo "[FAIL] FAT16 duplicate detection"; exit 1; }
grep -q "Directory 'DOCS' created successfully" "$FAT16_LOG" && echo "[PASS] FAT16: Created DOCS successfully" || { echo "[FAIL] FAT16 DOCS creation"; exit 1; }
grep -q "DOCS             <DIR>" "$FAT16_LOG" && echo "[PASS] FAT16: DOCS listed with <DIR> attribute" || { echo "[FAIL] FAT16 DOCS listing"; exit 1; }

# Assertions for FAT32
grep -q "Directory 'FAT32DIR' created successfully" "$FAT32_LOG" && echo "[PASS] FAT32: Created FAT32DIR successfully" || { echo "[FAIL] FAT32 FAT32DIR creation"; exit 1; }
grep -q "FAT32DIR         <DIR>" "$FAT32_LOG" && echo "[PASS] FAT32: FAT32DIR listed with <DIR> attribute" || { echo "[FAIL] FAT32 FAT32DIR listing"; exit 1; }
grep -q "Directory or file 'FAT32DIR' already exists" "$FAT32_LOG" && echo "[PASS] FAT32: Duplicate directory detection" || { echo "[FAIL] FAT32 duplicate detection"; exit 1; }

echo ""
echo ">>> ALL FATMKDIR TESTS PASSED SUCCESSFULLY ON FAT16 & FAT32! <<<"
