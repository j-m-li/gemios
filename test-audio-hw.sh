#!/bin/bash
# GEMIOS RTOS - Automated Test with Physical USB Audio Device (0d8c:0014)
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

if [ "$EUID" -ne 0 ]; then
    echo "[*] Requesting sudo for raw USB device node access..."
    exec sudo "$0" "$@"
fi

echo "==========================================================="
echo "  GEMIOS Physical USB Audio Device (0d8c:0014) Test"
echo "==========================================================="

echo "[1/3] Building GEMIOS RTOS and Test Assets..."
make

# Generate test WAV file (48kHz 16-bit Stereo PCM, 440Hz tone)
python3 -c "
import wave, struct, math
with wave.open('build/TEST.WAV', 'w') as wav:
    wav.setnchannels(2)
    wav.setsampwidth(2)
    wav.setframerate(48000)
    frames = []
    sr = 48000
    notes = [523.25, 659.25, 783.99, 1046.50]
    note_dur = sr // 4 # 250ms per note = 1.0s total
    for n_idx, freq in enumerate(notes):
        for i in range(note_dur):
            t = i / sr
            att = min(1.0, i / (0.015 * sr))
            rel = min(1.0, (note_dur - 1 - i) / (0.015 * sr))
            val = int(20000 * att * rel * math.sin(2 * math.pi * freq * t))
            frames.append(struct.pack('<hh', val, val))
    wav.writeframes(b''.join(frames))
"

# Clean 64MB FAT32 test disk
build/tools/dd if=/dev/zero of=build/test_fat32.img bs=1M count=64 status=none
build/tools/mkfs.fat -F 32 -n "GEMIOS32" build/test_fat32.img > /dev/null
build/tools/mcopy -i build/test_fat32.img build/README.TXT ::README.TXT
build/tools/mcopy -i build/test_fat32.img build/TEST.WAV ::TEST.WAV

OUTPUT_LOG="build/test_audio_hw_output.log"
SERIAL_LOG="build/test_audio_hw_serial.log"
rm -f "$OUTPUT_LOG" "$SERIAL_LOG"

echo "[2/3] Running QEMU with Physical USB Audio Passthrough (0d8c:0014)..."

python3 -c "
import subprocess, time, sys, os

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
    '-device', 'usb-storage,bus=xhci.0,port=1,drive=usbstick',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_fat32.img',
    '-device', 'usb-host,vendorid=0x0d8c,productid=0x0014,bus=xhci.0,port=2'
]

def wait_for_pattern(pattern, timeout=15.0):
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
    
    if not wait_for_pattern('=== GEMIOS RTOS Interactive Console Ready ===', 15.0):
        print('Timeout waiting for GEMIOS console to be ready.')
        proc.terminate()
        sys.exit(1)
    
    time.sleep(1.0)
    
    def send_cmd(command):
        for ch in command:
            if ch == ' ':
                proc.stdin.write('sendkey spc\n')
            elif ch == '.':
                proc.stdin.write('sendkey dot\n')
            elif ch == '-':
                proc.stdin.write('sendkey minus\n')
            elif ch == '_':
                proc.stdin.write('sendkey shift-minus\n')
            elif ch == '/':
                proc.stdin.write('sendkey slash\n')
            elif ch == '\"':
                proc.stdin.write('sendkey shift-2\n')
            elif ch >= '0' and ch <= '9':
                proc.stdin.write(f'sendkey {ch}\n')
            elif ch >= 'a' and ch <= 'z':
                proc.stdin.write(f'sendkey {ch}\n')
            elif ch >= 'A' and ch <= 'Z':
                proc.stdin.write(f'sendkey shift-{ch.lower()}\n')
            proc.stdin.flush()
            time.sleep(0.06)
        proc.stdin.write('sendkey ret\n')
        proc.stdin.flush()
        time.sleep(0.8)

    # Shell commands
    send_cmd('audio')
    send_cmd('audio vol 85')
    send_cmd('audio tone 440 250')
    send_cmd('beep 880 200')
    send_cmd('play TEST.WAV')

    time.sleep(2.0)
    proc.terminate()
"

echo "[3/3] Physical USB Audio Test Output Log:"
echo "----------------------------------------------------------"
cat "$SERIAL_LOG"
echo "----------------------------------------------------------"
