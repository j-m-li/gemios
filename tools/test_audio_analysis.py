#!/usr/bin/env python3
"""
GEMIOS Automated Audio Stream Continuity & Quality Analyzer
Public Domain Dedication

Analyzes individual audio streams in captured WAV file for internal micro-pauses / dropouts.
"""

import sys
import wave
import struct

def analyze_audio(wav_path):
    print(f"[Audio Analyzer] Loading '{wav_path}'...")
    try:
        wf = wave.open(wav_path, 'rb')
    except Exception as e:
        print(f"[FAIL] Could not open WAV file: {e}")
        return False

    n_channels = wf.getnchannels()
    sampwidth = wf.getsampwidth()
    framerate = wf.getframerate()
    n_frames = wf.getnframes()
    duration = n_frames / framerate

    print(f"[Audio Analyzer] Format: {framerate} Hz, {n_channels} ch, {sampwidth*8}-bit, {n_frames} frames ({duration:.3f} sec)")

    raw_data = wf.readframes(n_frames)
    wf.close()

    if sampwidth != 2:
        print(f"[FAIL] Only 16-bit PCM supported (found {sampwidth*8}-bit)")
        return False

    total_samples = n_frames * n_channels
    fmt = f"<{total_samples}h"
    samples = struct.unpack(fmt, raw_data)

    if n_channels == 2:
        left_samples = [samples[i * 2] for i in range(n_frames)]
    else:
        left_samples = list(samples)

    samples_per_ms = framerate // 1000
    num_ms = n_frames // samples_per_ms

    # Identify audio bursts (separated by >= 5ms of silence)
    bursts = []
    current_burst_start = None
    last_active_ms = None
    silence_run = 0

    for m in range(num_ms):
        chunk = left_samples[m * samples_per_ms : (m + 1) * samples_per_ms]
        peak = max(abs(s) for s in chunk)

        if peak > 600:
            if current_burst_start is None:
                current_burst_start = m
            last_active_ms = m
            silence_run = 0
        else:
            silence_run += 1
            if current_burst_start is not None and silence_run >= 5:
                # Burst ended
                bursts.append((current_burst_start, last_active_ms))
                current_burst_start = None
                last_active_ms = None

    if current_burst_start is not None:
        bursts.append((current_burst_start, last_active_ms))

    print(f"[Audio Analyzer] Detected {len(bursts)} distinct audio playback streams:")
    
    total_dropouts = 0
    for idx, (b_start, b_end) in enumerate(bursts):
        b_dur = b_end - b_start + 1
        if b_dur < 50:
            continue # Ignore transient clicks

        # Check for internal micro-pauses inside this burst (silence >= 1ms)
        burst_gaps = []
        for m in range(b_start, b_end + 1):
            chunk = left_samples[m * samples_per_ms : (m + 1) * samples_per_ms]
            peak = max(abs(s) for s in chunk)
            if peak < 300: # Silence threshold within active playback
                burst_gaps.append(m - b_start)

        if burst_gaps:
            print(f"  [FAIL] Stream #{idx+1} ({b_dur} ms): {len(burst_gaps)} ms micro-pauses at offset: {burst_gaps[:10]} ms")
            total_dropouts += len(burst_gaps)
        else:
            print(f"  [PASS] Stream #{idx+1} ({b_dur} ms): 100% continuous, zero micro-pauses")

    if total_dropouts > 0:
        print(f"[FAIL] >>> Total {total_dropouts} ms of micro-pauses detected inside active audio streams! <<<")
        return False

    print("[SUCCESS] >>> ALL AUDIO STREAMS ARE 100% CONTINUOUS WITH ZERO MICRO-PAUSES! <<<")
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: test_audio_analysis.py <path_to_wav>")
        sys.exit(1)
    
    ok = analyze_audio(sys.argv[1])
    sys.exit(0 if ok else 1)
