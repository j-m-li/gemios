#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo "   GEMIOS RTOS - FAT Directory Hierarchy Test Suite       "
echo "=========================================================="

echo "[1/3] Building GEMIOS Kernel and Disk Images..."
make

FAT16_LOG="build/test_dir_fat16.log"
FAT32_LOG="build/test_dir_fat32.log"
rm -f "$FAT16_LOG" "$FAT32_LOG"

echo "[2/3] Running Hierarchical Directory Tests in QEMU VM..."

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
            if chunk:
                all_output.extend(chunk)

def wait_prompt(timeout=6.0):
    start = time.time()
    while time.time() - start < timeout:
        read_more(0.1)
        if b'gemios> ' in all_output:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.4)
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

commands = [
    'pwd\n',
    'mkdir /DIRA\n',
    'ls\n',
    'cd DIRA\n',
    'pwd\n',
    'mkdir SUBDIR\n',
    'ls\n',
    'cd SUBDIR\n',
    'pwd\n',
    'ls\n',
    'cd ..\n',
    'pwd\n',
    'cd /\n',
    'pwd\n',
    'ls /DIRA/SUBDIR\n',
]

for cmd_text in commands:
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
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
all_output = bytearray()

def read_more(timeout=0.1):
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([proc.stdout], [], [], 0.05)
        if r:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if chunk:
                all_output.extend(chunk)

def wait_prompt(timeout=6.0):
    start = time.time()
    while time.time() - start < timeout:
        read_more(0.1)
        if b'gemios> ' in all_output:
            return True
    return False

def send_cmd(cmd_str):
    time.sleep(0.4)
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

commands = [
    'pwd\n',
    'mkdir /DOCS\n',
    'cd /DOCS\n',
    'pwd\n',
    'mkdir /DOCS/NESTED\n',
    'ls /DOCS\n',
    'cd NESTED\n',
    'pwd\n',
    'cd ..\n',
    'pwd\n',
    'cd /\n',
    'pwd\n',
]

for cmd_text in commands:
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

echo "[3/3] Verifying Directory Test Results & Assertions..."
echo "----------------------------------------------------------"

# FAT16 Assertions
if grep -q "usb0:/" "$FAT16_LOG"; then
    echo "[PASS] FAT16: pwd in root directory"
else
    echo "[FAIL] FAT16: pwd in root directory"
    exit 1
fi

if grep -q "Directory '/DIRA' created successfully" "$FAT16_LOG"; then
    echo "[PASS] FAT16: Created /DIRA"
else
    echo "[FAIL] FAT16: Created /DIRA"
    exit 1
fi

if grep -q "usb0:/DIRA" "$FAT16_LOG"; then
    echo "[PASS] FAT16: cd /DIRA updated CWD"
else
    echo "[FAIL] FAT16: cd /DIRA updated CWD"
    exit 1
fi

if grep -q "Directory 'SUBDIR' created successfully" "$FAT16_LOG"; then
    echo "[PASS] FAT16: Created nested SUBDIR"
else
    echo "[FAIL] FAT16: Created nested SUBDIR"
    exit 1
fi

if grep -q "usb0:/DIRA/SUBDIR" "$FAT16_LOG"; then
    echo "[PASS] FAT16: cd SUBDIR nested CWD"
else
    echo "[FAIL] FAT16: cd SUBDIR nested CWD"
    exit 1
fi

# FAT32 Assertions
if grep -q "Directory '/DOCS' created successfully" "$FAT32_LOG"; then
    echo "[PASS] FAT32: Created /DOCS"
else
    echo "[FAIL] FAT32: Created /DOCS"
    exit 1
fi

if grep -q "usb0:/DOCS" "$FAT32_LOG"; then
    echo "[PASS] FAT32: cd /DOCS updated CWD"
else
    echo "[FAIL] FAT32: cd /DOCS updated CWD"
    exit 1
fi

if grep -q "Directory '/DOCS/NESTED' created successfully" "$FAT32_LOG"; then
    echo "[PASS] FAT32: Created /DOCS/NESTED"
else
    echo "[FAIL] FAT32: Created /DOCS/NESTED"
    exit 1
fi

if grep -q "usb0:/DOCS/NESTED" "$FAT32_LOG"; then
    echo "[PASS] FAT32: cd NESTED updated CWD"
else
    echo "[FAIL] FAT32: cd NESTED updated CWD"
    exit 1
fi

echo ""
echo ">>> ALL HIERARCHICAL DIRECTORY TESTS PASSED ON FAT16 & FAT32! <<<"
