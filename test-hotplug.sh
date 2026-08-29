#!/bin/bash
# GEMIOS RTOS - USB Drive Hot-Plug Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo "   GEMIOS RTOS - USB Drive Hot-Plug Test Suite            "
echo "=========================================================="

echo "[1/3] Building GEMIOS RTOS and Test Disk Images..."
make clean
make

OUTPUT_LOG="build/test_hotplug_output.log"
SERIAL_LOG="build/serial_hotplug_output.log"
rm -f "$OUTPUT_LOG" "$SERIAL_LOG"

echo "[2/3] Running USB Drive Hot-Plug & Hot-Unplug Tests in QEMU VM..."

python3 -c "
import subprocess, time, os, sys

serial_log = '$SERIAL_LOG'
monitor_log = '$OUTPUT_LOG'

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', f'file:{serial_log}',
    '-monitor', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-hub,bus=xhci.0,port=4',
    '-device', 'usb-kbd,bus=xhci.0,port=4.1',
    '-device', 'usb-mouse,bus=xhci.0,port=4.2',
]

def wait_for_pattern(pattern, timeout=8.0):
    start = time.time()
    while time.time() - start < timeout:
        if os.path.exists(serial_log):
            with open(serial_log, 'r', errors='replace') as f:
                if pattern in f.read():
                    return True
        time.sleep(0.05)
    return False

with open(monitor_log, 'w') as mon_out:
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=mon_out, stderr=subprocess.STDOUT, text=True)
    
    if not wait_for_pattern('=== GEMIOS RTOS Interactive Console Ready ===', 8.0):
        print('Timeout waiting for GEMIOS console to be ready.')
        proc.terminate()
        sys.exit(1)
    
    time.sleep(0.5)

    def type_cmd(s):
        time.sleep(0.3)
        prompt_pos = os.path.getsize(serial_log) if os.path.exists(serial_log) else 0
        for ch in s:
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
        start = time.time()
        while time.time() - start < 4.0:
            if os.path.exists(serial_log):
                with open(serial_log, 'r', errors='replace') as f:
                    f.seek(prompt_pos)
                    if 'gemios> ' in f.read():
                        break
            time.sleep(0.05)
        time.sleep(0.3)

    # 1. Initial state (no storage device attached)
    type_cmd('storage')
    type_cmd('ls usb0')

    # 2. Hot-plug FAT16 USB drive into USB hub port 3 (port=4.3)
    proc.stdin.write('drive_add 0 if=none,id=usbstick16,format=raw,file=build/test_disk.img\n')
    proc.stdin.flush()
    time.sleep(0.2)
    proc.stdin.write('device_add usb-storage,bus=xhci.0,port=4.3,drive=usbstick16,id=usbdisk3\n')
    proc.stdin.flush()
    wait_for_pattern(\"Registered 'usb0'\", 6.0)
    time.sleep(1.0)

    # 3. Test commands on hotplugged FAT16 USB drive
    type_cmd('storage')
    type_cmd('ls usb0')
    type_cmd('cat usb0 README.TXT')
    type_cmd('cat usb0 STATUS.TXT')

    # 4. Hot-unplug FAT16 USB drive from USB hub
    proc.stdin.write('device_del usbdisk3\n')
    proc.stdin.flush()
    wait_for_pattern('disconnected', 6.0)
    time.sleep(0.5)

    # 5. Verify storage device removal
    type_cmd('storage')
    type_cmd('ls usb0')

    # 6. Hot-plug FAT32 USB drive into USB hub port 3 (port=4.3)
    proc.stdin.write('drive_add 0 if=none,id=usbstick32,format=raw,file=build/test_fat32.img\n')
    proc.stdin.flush()
    time.sleep(0.2)
    proc.stdin.write('device_add usb-storage,bus=xhci.0,port=4.3,drive=usbstick32,id=usbdisk3\n')
    proc.stdin.flush()
    wait_for_pattern('131072 blocks', 6.0)
    time.sleep(0.5)

    # 7. Test commands on hotplugged FAT32 USB drive
    type_cmd('storage')
    type_cmd('ls usb0')
    type_cmd('cat usb0 FAT32DOC.TXT')

    # 8. Hot-unplug FAT32 USB drive from USB hub
    proc.stdin.write('device_del usbdisk3\n')
    proc.stdin.flush()
    wait_for_pattern('disconnected', 6.0)
    time.sleep(0.5)

    # 9. Verify storage device removed again
    type_cmd('storage')

    time.sleep(0.5)
    proc.terminate()
    proc.wait()
"

echo "[3/3] Verifying Hot-Plug Test Results & Assertions..."
echo "=========================================================="
cat "$SERIAL_LOG"
echo "=========================================================="

# Assertions
grep -q "USB xHCI Controller" "$SERIAL_LOG" && echo "[PASS] xHCI Controller detected & initialized" || { echo "[FAIL] xHCI Controller"; exit 1; }
grep -q "Initialized USB Hub" "$SERIAL_LOG" && echo "[PASS] USB Hub detected & initialized" || { echo "[FAIL] USB Hub"; exit 1; }
grep -q "No storage block devices found." "$SERIAL_LOG" && echo "[PASS] Initial state confirmed: no storage devices attached" || { echo "[FAIL] Initial storage state"; exit 1; }
grep -q "Block device 'usb0' not found." "$SERIAL_LOG" && echo "[PASS] Initial state error handling: access to absent device handled" || { echo "[FAIL] Initial device error handling"; exit 1; }

grep -q "Registered 'usb0' (65536 blocks, 512 bytes/block, 32 MB)" "$SERIAL_LOG" && echo "[PASS] FAT16 USB drive registered as block device 'usb0' on hub port" || { echo "[FAIL] FAT16 block device registration"; exit 1; }
grep -q "Filesystem on usb0 (FAT16)" "$SERIAL_LOG" && echo "[PASS] FAT16 filesystem mounted on hotplugged drive" || { echo "[FAIL] FAT16 filesystem mount"; exit 1; }
grep -q "README.TXT" "$SERIAL_LOG" && echo "[PASS] FAT16 directory contents listed" || { echo "[FAIL] FAT16 directory list"; exit 1; }
grep -q "Welcome to GEMIOS RTOS on USB Mass Storage (FAT16)!" "$SERIAL_LOG" && echo "[PASS] FAT16 file read from hotplugged drive" || { echo "[FAIL] FAT16 file read"; exit 1; }

grep -q "disconnected" "$SERIAL_LOG" && echo "[PASS] Hub port disconnect event detected and handled" || { echo "[FAIL] Hot-unplug disconnect event"; exit 1; }

grep -q "Registered 'usb0' (131072 blocks, 512 bytes/block, 64 MB)" "$SERIAL_LOG" && echo "[PASS] FAT32 USB drive registered as block device 'usb0' on hub re-plug" || { echo "[FAIL] FAT32 block device registration"; exit 1; }
grep -q "Filesystem on usb0 (FAT32)" "$SERIAL_LOG" && echo "[PASS] FAT32 filesystem mounted on hotplugged drive" || { echo "[FAIL] FAT32 filesystem mount"; exit 1; }
grep -q "FAT32DOC.TXT" "$SERIAL_LOG" && echo "[PASS] FAT32 directory contents listed" || { echo "[FAIL] FAT32 directory list"; exit 1; }
grep -q "Supports cluster traversal and 32-bit FAT table lookups." "$SERIAL_LOG" && echo "[PASS] FAT32 file read from hotplugged drive" || { echo "[FAIL] FAT32 file read"; exit 1; }

echo ""
echo ">>> ALL USB DRIVE HOT-PLUG TESTS PASSED SUCCESSFULLY! <<<"
