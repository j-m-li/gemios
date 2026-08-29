#!/bin/bash
# GEMIOS RTOS - PS/2 Keyboard and Mouse Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "[1/3] Building GEMIOS RTOS..."
make clean
make

echo "[2/3] Running PS/2 Keyboard and Mouse Automated Tests in QEMU VM..."
OUTPUT_LOG="build/test_ps2_output.log"
SERIAL_LOG="build/test_ps2_serial.log"
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
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_disk.img',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log_file, stderr=subprocess.STDOUT, text=True)

time.sleep(2.0) # Wait for boot

# 1. Test initial mouse state
proc.stdin.write('sendkey m\nsendkey o\nsendkey u\nsendkey s\nsendkey e\nsendkey ret\n')
proc.stdin.flush()
time.sleep(0.5)

# 2. Test PS/2 Mouse Movement and Buttons via QEMU monitor
proc.stdin.write('mouse_move 20 -10\n')
proc.stdin.flush()
time.sleep(0.3)
proc.stdin.write('mouse_button 1\n')
proc.stdin.flush()
time.sleep(0.3)

# 3. Read mouse state while button is pressed
proc.stdin.write('sendkey m\nsendkey o\nsendkey u\nsendkey s\nsendkey e\nsendkey ret\n')
proc.stdin.flush()
time.sleep(0.5)

# 4. Release mouse button and move mouse again
proc.stdin.write('mouse_button 0\n')
proc.stdin.flush()
time.sleep(0.3)
proc.stdin.write('mouse_move -5 5\n')
proc.stdin.flush()
time.sleep(0.3)

# 5. Type commands with PS/2 Keyboard: help, ps, mem, uptime
commands = [
    'mouse',
    'help',
    'ps',
    'mem',
    'uptime'
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
    time.sleep(0.3)

time.sleep(1.5)
proc.terminate()
proc.wait()
log_file.close()
"

echo "[3/3] Verifying Test Results..."
echo "=========================================================="
cat "$SERIAL_LOG"
echo "=========================================================="

# Assertions
grep -q "i8042 Controller Initialized" "$SERIAL_LOG" && echo "[PASS] PS/2 i8042 Controller detected and initialized" || { echo "[FAIL] PS/2 Controller"; exit 1; }
grep -q "Initialized PS/2 Keyboard Driver" "$SERIAL_LOG" && echo "[PASS] PS/2 Keyboard driver registered (IRQ1)" || { echo "[FAIL] PS/2 Keyboard Driver"; exit 1; }
grep -q "Initialized PS/2 Mouse Driver" "$SERIAL_LOG" && echo "[PASS] PS/2 Mouse driver registered (IRQ12)" || { echo "[FAIL] PS/2 Mouse Driver"; exit 1; }
grep -q "PS/2 Mouse Position:" "$SERIAL_LOG" && echo "[PASS] PS/2 Mouse status reporting verified" || { echo "[FAIL] PS/2 Mouse status"; exit 1; }
grep -q "Buttons: Left=Pressed" "$SERIAL_LOG" && echo "[PASS] PS/2 Mouse button press detection verified" || { echo "[FAIL] PS/2 Mouse button click"; exit 1; }
grep -q "GEMIOS RTOS Commands" "$SERIAL_LOG" && echo "[PASS] PS/2 Keyboard input execution verified" || { echo "[FAIL] PS/2 Keyboard execution"; exit 1; }

echo ""
echo ">>> ALL PS/2 KEYBOARD AND MOUSE TESTS PASSED SUCCESSFULLY! <<<"
