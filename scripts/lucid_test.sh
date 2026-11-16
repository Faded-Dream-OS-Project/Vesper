#!/usr/bin/env bash
# lucid_test.sh — boot a Lucid test-mode binary as Vesper's init in
# QEMU and require the banner, two heartbeats and a clean exit.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Usage: lucid_test.sh <path-to-lucid_test.bin> [qemu] [ovmf-code] [ovmf-vars]
# Builds the ISO with INIT_BIN=<binary>, boots it under OVMF (UEFI),
# then checks the serial log for:
#   - "Lucid 0.1 (init of Faded Dream OS), pid" banner
#   - heartbeat #1 and #2
#   - "lucid: test complete, exiting(0)"
#   - init task exit recorded by the kernel ("init: exit(0)")
#   - and the rest of the boot suites still ALL PASS.
# Exits 0 only if every requirement holds.

set -u
export LC_ALL=C

BIN="${1:?usage: lucid_test.sh <lucid_test.bin> [qemu] [ovmf-code] [ovmf-vars]}"
QEMU="${2:-$(command -v qemu-system-x86_64)}"
OVMF_CODE="${3:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${4:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
[ -f "$BIN" ] || { echo "FAIL: binary not found: $BIN"; exit 1; }

cd "$(dirname "$0")/.."

ROM_DIR=$(dirname "$(command -v "$QEMU")")/../share/qemu
ROM_DIR=$(cd "$ROM_DIR" 2>/dev/null && pwd)

LOG=build/lucid_serial.log
rm -f "$LOG" build/OVMF_VARS.fd
cp "$OVMF_VARS" build/OVMF_VARS.fd

# Rebuild kernel+ISO with the external init. Clean the blob first so
# init_blob.S regenerates from the new binary.
rm -f build/init.bin kernel/src/init_blob.S build/init_blob.o
if ! make iso INIT_BIN="$BIN" >/dev/null 2>&1; then
    echo "FAIL: make iso INIT_BIN=$BIN"
    exit 1
fi

timeout 120 "$QEMU" -L "$ROM_DIR" -machine q35 -m 512M \
    -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
    -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
    -cdrom vesper.iso -serial file:"$LOG" -display none -no-reboot \
    >/dev/null 2>&1

FAILS=0
check() {
    if grep -aq "$2" "$LOG"; then
        echo "PASS  $1"
    else
        echo "FAIL  $1 (missing: $2)"
        FAILS=$((FAILS + 1))
    fi
}

check "Lucid banner with pid"      "Lucid 0.1 (init of Faded Dream OS), pid"
check "heartbeat 1"                "lucid: heartbeat #1 at"
check "heartbeat 2"                "lucid: heartbeat #2 at"
check "lucid exit line"            "lucid: test complete, exiting(0)"
check "kernel recorded exit(0)"    "init: exit(0)"
check "boot suites green"          "suites: 6 pass, 0 fail, 0 not run"
check "boot complete banner"       "=== Vesper boot complete ==="

echo
if [ "$FAILS" -eq 0 ]; then
    echo "RESULT: LUCID BOOT TEST PASS"
    exit 0
fi
echo "RESULT: $FAILS CHECK(S) FAILED"
exit 1
