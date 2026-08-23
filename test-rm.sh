#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo "   GEMIOS RTOS - FAT rm and rm -r * Test Suite            "
echo "=========================================================="

echo "[1/3] Building GEMIOS Kernel and Disk Images..."
make

FAT16_LOG="build/test_rm_fat16.log"
FAT32_LOG="build/test_rm_fat32.log"
rm -f "$FAT16_LOG" "$FAT32_LOG"

echo "[2/3] Running rm and rm -r * Tests in QEMU VM..."

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
    'ls\n',
    'mkdir TESTDIR\n',
    'ls\n',
    'rm TESTDIR\n',
    'rm -r TESTDIR\n',
    'ls\n',
    'mkdir NEST1\n',
    'cd NEST1\n',
    'mkdir NEST2\n',
    'cd /\n',
    'ls\n',
    'rm -r *\n',
    'ls\n',
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
    'ls\n',
    'mkdir /SUBDIR\n',
    'cd /SUBDIR\n',
    'mkdir A\n',
    'mkdir B\n',
    'ls\n',
    'rm -r *\n',
    'ls\n',
    'cd /\n',
    'ls\n',
    'rm -r *\n',
    'ls\n',
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

echo "[3/3] Verifying Test Results & Assertions..."
echo "----------------------------------------------------------"
echo "=== FAT16 Log ==="
cat "$FAT16_LOG"
echo "----------------------------------------------------------"
echo "=== FAT32 Log ==="
cat "$FAT32_LOG"
echo "----------------------------------------------------------"

# FAT16 Assertions
if grep -q "Is a directory (use -r)" "$FAT16_LOG"; then
    echo "[PASS] FAT16: rm directory without -r reported error"
else
    echo "[FAIL] FAT16: rm directory without -r"
    exit 1
fi

if grep -q "Removed 'TESTDIR' on 'usb0'" "$FAT16_LOG"; then
    echo "[PASS] FAT16: rm -r TESTDIR removed directory"
else
    echo "[FAIL] FAT16: rm -r TESTDIR"
    exit 1
fi

if grep -q "Total: 0 item(s)" "$FAT16_LOG"; then
    echo "[PASS] FAT16: rm -r * emptied the directory"
else
    echo "[FAIL] FAT16: rm -r *"
    exit 1
fi

# FAT32 Assertions
if grep -q "Total: 0 item(s)" "$FAT32_LOG"; then
    echo "[PASS] FAT32: rm -r * emptied the directory"
else
    echo "[FAIL] FAT32: rm -r *"
    exit 1
fi

echo ""
echo ">>> ALL RM AND RM -R * TESTS PASSED SUCCESSFULLY! <<<"
