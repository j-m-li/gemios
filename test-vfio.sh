#!/bin/bash
# GEMIOS RTOS - Automated QEMU Test with Physical Controller Passthrough (37:00.0)
# Run with: sudo ./test-vfio.sh

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if [ "$EUID" -ne 0 ]; then
  echo "Notice: VFIO passthrough requires root privileges for DMA memory locking."
  echo "Running with sudo..."
  exec sudo "$0" "$@"
fi

echo "[1/3] Building GEMIOS RTOS..."
make

OUTPUT_LOG="build/test_vfio_output.log"
SERIAL_LOG="build/serial_vfio_output.log"
rm -f "$OUTPUT_LOG" "$SERIAL_LOG"

echo "[2/3] Running QEMU VM with Physical Thunderbolt xHCI Controller Passthrough..."

python3 -c "
import subprocess, time, os, sys

serial_log = '$SERIAL_LOG'
monitor_log = '$OUTPUT_LOG'

cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
    '-m', '256M',
    '-enable-kvm',
    '-display', 'none',
    '-serial', f'file:{serial_log}',
    '-monitor', 'stdio',
    '-device', 'vfio-pci,host=37:00.0'
]

def wait_for_pattern(pattern, timeout=12.0):
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
    
    if not wait_for_pattern('=== GEMIOS RTOS Interactive Console Ready ===', 12.0):
        print('Timeout waiting for GEMIOS console to be ready.')
        proc.terminate()
        sys.exit(1)
    
    time.sleep(1.0)

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
            time.sleep(0.03)
        proc.stdin.write('sendkey ret\n')
        proc.stdin.flush()
        start = time.time()
        while time.time() - start < 5.0:
            if os.path.exists(serial_log):
                with open(serial_log, 'r', errors='replace') as f:
                    f.seek(prompt_pos)
                    if 'gemios> ' in f.read():
                        break
            time.sleep(0.05)
        time.sleep(0.3)

    commands = [
        'help',
        'ps',
        'mem',
        'pci',
        'lsusb',
        'storage',
        'readsec usb0 0',
        'ls usb0',
        'uptime'
    ]

    for cmd_text in commands:
        type_cmd(cmd_text)

    time.sleep(1.0)
    proc.terminate()
    proc.wait()
"

echo "[3/3] VM Serial Console Output from Physical Hardware:"
echo "=========================================================="
if [ -f "$SERIAL_LOG" ]; then
    cat "$SERIAL_LOG"
fi
echo "=========================================================="
