#!/bin/bash
# GEMOS RTOS - MS-DOS Edit & UTF-8 Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo " Starting GEMOS MS-DOS Edit & UTF-8 Text Editor Test"
echo "=========================================================="

echo "[1/3] Building kernel and test disk images..."
make clean
make

TEST_LOG="build/test_editor.log"

echo "[2/3] Running MS-DOS Edit UTF-8 test in QEMU VM..."

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

def wait_for(pattern, timeout=8.0):
    start = time.time()
    while time.time() - start < timeout:
        read_more(0.05)
        if pattern in all_output:
            return True
    return False

def send(data, wait=0.5):
    if isinstance(data, str):
        data = data.encode('utf-8')
    for i in range(0, len(data), 4):
        proc.stdin.write(data[i:i+4])
        proc.stdin.flush()
        time.sleep(0.03)
    read_more(wait)

# 1. Wait for prompt
wait_for(b'gemios> ', timeout=6.0)

# 2. Open editor for NEWFILE.TXT
send('edit usb0 NEWFILE.TXT\n', 0.5)
wait_for(b'[EDITOR] Ready', timeout=5.0)

# 3. Type text with UTF-8 characters
send('GEMOS RTOS UTF-8: ä ö ü é ─ │\n', 0.8)
send('Second line in MS-DOS Edit!\n', 0.8)

# 4. Save file using Ctrl+S (\x13)
send(b'\x13', 0.5)
wait_for(b'[EDITOR] Saved successfully!', timeout=5.0)

# 5. Exit editor using Ctrl+Q (\x11)
send(b'\x11', 0.5)
wait_for(b'[EDITOR] Exited editor', timeout=5.0)

# 6. Verify file was created in FAT directory
prompt_idx = len(all_output)
proc.stdin.write(b'fatls usb0\n')
proc.stdin.flush()
start = time.time()
while time.time() - start < 5.0:
    read_more(0.05)
    if b'gemios> ' in all_output[prompt_idx:]:
        break

# 7. Read back file contents using fatcat
prompt_idx = len(all_output)
proc.stdin.write(b'fatcat usb0 NEWFILE.TXT\n')
proc.stdin.flush()
start = time.time()
while time.time() - start < 5.0:
    read_more(0.05)
    if b'gemios> ' in all_output[prompt_idx:]:
        break

read_more(1.0)
proc.terminate()
try:
    proc.wait(timeout=2.0)
except Exception:
    proc.kill()
read_more(0.5)

out = all_output.decode('utf-8', errors='replace')
with open('$TEST_LOG', 'w') as f:
    f.write(out)
"

echo "[3/3] Verifying Test Results & Assertions..."
echo "----------------------------------------------------------"
cat "$TEST_LOG"
echo "----------------------------------------------------------"

# Assertions
grep -q "NEWFILE.TXT" "$TEST_LOG" && echo "[PASS] File NEWFILE.TXT created in FAT directory" || { echo "[FAIL] NEWFILE.TXT not listed in FAT"; exit 1; }
grep -q "GEMOS RTOS UTF-8: ä ö ü é ─ │" "$TEST_LOG" && echo "[PASS] UTF-8 text saved and read back successfully" || { echo "[FAIL] UTF-8 content mismatch"; exit 1; }
grep -q "Second line in MS-DOS Edit!" "$TEST_LOG" && echo "[PASS] Multiline text saved and read back successfully" || { echo "[FAIL] Multiline content mismatch"; exit 1; }

echo ""
echo ">>> ALL MS-DOS EDIT & UTF-8 TESTS PASSED SUCCESSFULLY! <<<"
