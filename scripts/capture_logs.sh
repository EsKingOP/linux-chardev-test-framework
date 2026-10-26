#!/usr/bin/env bash
# capture_logs.sh - Capture dmesg ring-buffer logs and run test harness under strace
set -euo pipefail

OUTPUT_DIR="$(dirname "$0")/../logs"
mkdir -p "$OUTPUT_DIR"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
DMESG_LOG="$OUTPUT_DIR/dmesg_${TIMESTAMP}.log"
STRACE_LOG="$OUTPUT_DIR/strace_${TIMESTAMP}.log"

echo "[*] Clearing kernel ring buffer..."
sudo dmesg -c > /dev/null

echo "[*] Executing test harness under strace..."
sudo strace -ff -tt -T -o "$STRACE_LOG" "$(dirname "$0")/../userspace/build/bin/test_runner" || true

echo "[*] Dumping dmesg log..."
dmesg > "$DMESG_LOG"

echo "[+] Execution logs captured:"
echo "    Kernel messages: $DMESG_LOG"
echo "    Syscall traces:  $STRACE_LOG"
