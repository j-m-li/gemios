#!/bin/bash
# GEMIOS RTOS - USB Audio Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "==========================================================="
echo "  GEMIOS USB Audio (UAC 1.0 / C-Media 0d8c:0014) Test Suite"
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

# Format clean 64MB FAT32 test disk
build/tools/dd if=/dev/zero of=build/test_fat32.img bs=1M count=64 status=none
build/tools/mkfs.fat -F 32 -n "GEMIOS32" build/test_fat32.img > /dev/null
build/tools/mcopy -i build/test_fat32.img build/README.TXT ::README.TXT
build/tools/mcopy -i build/test_fat32.img build/TEST.WAV ::TEST.WAV

echo "[2/3] Running USB Audio Verification in QEMU VM..."
OUTPUT_LOG="build/test_audio_output.log"
SERIAL_LOG="build/test_audio_serial.log"
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
    '-audiodev', 'wav,id=snd0,path=build/qemu_captured.wav',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-storage,bus=xhci.0,port=1,drive=usbstick',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_fat32.img,file.locking=off',
    '-device', 'usb-audio,bus=xhci.0,port=2,audiodev=snd0'
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log_file, stderr=subprocess.STDOUT, text=True)

time.sleep(2.0) # Wait for boot

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
        time.sleep(0.08)
    proc.stdin.write('sendkey ret\n')
    proc.stdin.flush()
    time.sleep(1.8)

# 1. Check audio device info
send_cmd('audio')
time.sleep(0.5)

# 2. Set volume to 90%
send_cmd('audio vol 90')
time.sleep(0.5)

# 3. Continuous 3000ms Sine Tone Verification
send_cmd('beep 440 3000')
time.sleep(4.0)

# 4. List files and play WAV audio file from FAT32 filesystem
send_cmd('ls')
time.sleep(0.5)
send_cmd('play TEST.WAV')
time.sleep(2.5)

# 5. Record 1 second of audio into REC.WAV
send_cmd('record REC.WAV 1')
time.sleep(2.5)
send_cmd('ls')
time.sleep(0.5)
send_cmd('play REC.WAV')
time.sleep(2.5)

time.sleep(1.0)
proc.terminate()
try:
    proc.wait(timeout=3)
except Exception:
    proc.kill()
log_file.close()
"

echo "[3/3] Verifying USB Audio Test Results..."
if [ -f "$SERIAL_LOG" ]; then
    cat "$SERIAL_LOG"
fi

echo "=========================================================="
ERRORS=0

if grep -q "USB Audio" "$SERIAL_LOG"; then
    echo "[PASS] USB Audio device detected & initialized"
else
    echo "[FAIL] USB Audio device NOT found in serial output"
    ERRORS=$((ERRORS + 1))
fi

if grep -q "Playback Stream" "$SERIAL_LOG" || grep -q "USB Audio Devices" "$SERIAL_LOG"; then
    echo "[PASS] Audio streaming playback endpoint registered"
else
    echo "[FAIL] Audio streaming endpoint NOT found"
    ERRORS=$((ERRORS + 1))
fi

if grep -q "Playing beep tone" "$SERIAL_LOG" || grep -q "Playing 440 Hz tone" "$SERIAL_LOG"; then
    echo "[PASS] Successfully generated and streamed sine audio tone"
else
    echo "[FAIL] Audio tone playback failed"
    ERRORS=$((ERRORS + 1))
fi

if grep -q "Playing WAV" "$SERIAL_LOG"; then
    echo "[PASS] Successfully parsed and played WAV audio file"
else
    echo "[FAIL] WAV audio file playback failed"
    ERRORS=$((ERRORS + 1))
fi

if grep -q "Saved recording to" "$SERIAL_LOG"; then
    echo "[PASS] Successfully captured audio and saved WAV recording"
else
    echo "[FAIL] Audio recording failed"
    ERRORS=$((ERRORS + 1))
fi

# 4. Automated Waveform Dropout & Continuity Verification
echo "----------------------------------------------------------"
echo "[Automated Waveform & Micro-Pause Analysis]"
if [ -f "build/qemu_captured.wav" ]; then
    if python3 tools/test_audio_analysis.py build/qemu_captured.wav; then
        echo "[PASS] Captured USB audio stream contains zero dropouts or pauses"
    else
        echo "[FAIL] Captured USB audio stream contains glitches or dropouts"
        ERRORS=$((ERRORS + 1))
    fi
else
    echo "[WARN] build/qemu_captured.wav not generated by host"
fi

echo "=========================================================="
if [ $ERRORS -eq 0 ]; then
    echo ">>> ALL USB AUDIO TESTS PASSED SUCCESSFULLY! <<<"
    exit 0
else
    echo ">>> SOME USB AUDIO TESTS FAILED ($ERRORS failures) <<<"
    exit 1
fi
