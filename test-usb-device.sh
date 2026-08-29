#!/bin/bash
# GEMIOS RTOS - Automated QEMU Test with Attached USB Device (2109:0813) & Hub Storage
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "=========================================================="
echo " GEMIOS RTOS - QEMU Test with USB Hub & Storage Attached"
echo " Attached Host USB: 2109:0813 (VIA Labs Hub)"
echo " Attached Storage:  USB Mass Storage on Hub (FAT16/FAT32)"
echo "=========================================================="

echo "[1/3] Building GEMIOS RTOS and Disk Images..."
make

OUTPUT_LOG="build/test_usb_host_output.log"
SERIAL_LOG="build/serial_usb_host_output.log"
rm -f "$OUTPUT_LOG" "$SERIAL_LOG"

echo "[2/3] Running QEMU VM with USB Hub & Storage Device attached..."

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
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-device', 'usb-host,vendorid=0x2109,productid=0x0813,bus=xhci.0,port=2'
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
        time.sleep(0.2)
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
            time.sleep(0.03)
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

    # Query QEMU monitor USB status
    proc.stdin.write('info usb\n')
    proc.stdin.write('info usbhost\n')
    proc.stdin.flush()
    time.sleep(0.3)

    # Run test commands inside GEMIOS RTOS
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
        'cat usb0 STATUS.TXT',
        'uptime'
    ]

    for cmd_text in commands:
        type_cmd(cmd_text)

    time.sleep(0.5)
    proc.terminate()
    proc.wait()
"

echo "[3/3] Verifying VM Serial Console Output & Test Assertions..."
echo "=========================================================="
if [ -f "$SERIAL_LOG" ]; then
    cat "$SERIAL_LOG"
fi
echo "=========================================================="

echo ""
echo "=== QEMU Monitor (USB Status) ==="
grep -E "Device|Port|Speed|Product|Bus|Class" "$OUTPUT_LOG" || true
echo "================================="

# Assertions
grep -q "USB xHCI Controller" "$SERIAL_LOG" && echo "[PASS] xHCI Controller detected & initialized" || { echo "[FAIL] xHCI Controller"; exit 1; }
grep -q "Initialized USB Hub" "$SERIAL_LOG" && echo "[PASS] USB Hub driver detected & initialized" || { echo "[FAIL] USB Hub"; exit 1; }
grep -q "device on Hub Slot" "$SERIAL_LOG" && echo "[PASS] Storage device on USB Hub enumerated" || { echo "[FAIL] USB Hub Downstream storage device"; exit 1; }
grep -q "USB Mass Storage initialized" "$SERIAL_LOG" && echo "[PASS] USB Mass Storage driver initialized" || { echo "[FAIL] USB Mass Storage"; exit 1; }
grep -q "Registered 'usb0'" "$SERIAL_LOG" && echo "[PASS] Block device 'usb0' registered" || { echo "[FAIL] Block device registration"; exit 1; }
grep -q "GEMIOS RTOS Interactive Console Ready" "$SERIAL_LOG" && echo "[PASS] Interactive Shell task running" || { echo "[FAIL] Shell task"; exit 1; }
grep -q "Reading sector 0 from usb0" "$SERIAL_LOG" && echo "[PASS] Sector read verified on attached storage device" || { echo "[FAIL] Sector read"; exit 1; }
grep -q "Filesystem on usb0" "$SERIAL_LOG" && echo "[PASS] Filesystem detected & listed on attached storage device" || { echo "[FAIL] Filesystem listing"; exit 1; }
grep -q "Welcome to GEMIOS RTOS on USB Mass Storage" "$SERIAL_LOG" && echo "[PASS] File content read verified from storage device on hub" || { echo "[FAIL] File content read"; exit 1; }

echo ""
echo ">>> ALL GEMIOS RTOS USB HUB & STORAGE DEVICE TESTS PASSED SUCCESSFULLY! <<<"
