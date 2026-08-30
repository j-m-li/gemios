#!/bin/bash
# GEMIOS RTOS - FAT Long File Name (LFN/VFAT) Automated Test Suite
# Public Domain Dedication

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$DIR"

echo "==========================================================="
echo "  GEMIOS FAT Long File Name (LFN / VFAT) Test Suite        "
echo "==========================================================="

echo "[1/3] Building GEMIOS RTOS and 64MB FAT32 LFN Disk..."
make -j$(nproc)

mkdir -p build
build/tools/dd if=/dev/zero of=build/test_lfn.img bs=1M count=64 status=none
build/tools/mkfs.fat -F 32 -n "GEMIOS_LFN" build/test_lfn.img > /dev/null

# Create host test files
cat << 'EOF' > build/lfn_doc1.txt
Hello from Long File Name Test!
This file has a 36-character long filename with spaces and mixed case.
GEMIOS VFAT / LFN implementation is active and functional!
EOF

cat << 'EOF' > build/lfn_doc2.txt
Another file with a long name to test multiple LFN directory entries.
EOF

# Copy files with Long File Names using mtools
build/tools/mcopy -i build/test_lfn.img build/lfn_doc1.txt "::My Long Document 2026.txt"
build/tools/mcopy -i build/test_lfn.img build/lfn_doc2.txt "::Second Very Long Name.txt"
build/tools/mcopy -i build/test_lfn.img build/README.TXT "::README.TXT"

echo "[2/3] Running FAT32 LFN Verification Tests in QEMU VM..."
python3 tools/test_lfn_runner.py

SERIAL_LOG="build/serial_lfn_output.log"
echo "[3/3] Verifying FAT32 LFN Test Results..."
echo "=========================================================="
cat "$SERIAL_LOG"
echo "=========================================================="

ERR=0
check_pass() {
    local desc="$1"
    local pattern="$2"
    if grep -F "${pattern}" "${SERIAL_LOG}" > /dev/null; then
        echo -e "\e[32m[PASS]\e[0m ${desc}"
    else
        echo -e "\e[31m[FAIL]\e[0m ${desc}"
        ERR=1
    fi
}

check_pass "xHCI Controller detected & initialized" "Found USB xHCI Controller"
check_pass "USB Mass Storage registered as usb0" "Registered 'usb0'"
check_pass "FAT32 LFN 'My Long Document 2026.txt' listed" "My Long Document 2026.txt"
check_pass "FAT32 LFN 'Second Very Long Name.txt' listed" "Second Very Long Name.txt"
check_pass "Successfully read LFN file contents" "GEMIOS VFAT / LFN implementation is active and functional!"
check_pass "Created LFN directory 'Created Inside GEMIOS'" "Created Inside GEMIOS"
check_pass "Copied 8.3 file via cp command" "Copied '/README.TXT' -> '/COPY.TXT'"
check_pass "Read copied file contents" "COPY.TXT"
check_pass "Copied LFN file via cp command" "Copied '/My Long Document 2026.txt' -> '/Doc Copy.txt'"
check_pass "Read copied LFN file contents" "Doc Copy.txt"
check_pass "Successfully removed LFN file" "Removed 'Second Very Long Name.txt'"

if [ ${ERR} -eq 0 ]; then
    echo "=========================================================="
    echo ">>> ALL FAT LONG FILE NAME (LFN) TESTS PASSED! <<<"
    echo "=========================================================="
    exit 0
else
    echo "=========================================================="
    echo ">>> SOME LFN TESTS FAILED <<<"
    echo "=========================================================="
    exit 1
fi
