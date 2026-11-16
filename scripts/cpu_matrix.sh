#!/usr/bin/env bash
# cpu_matrix.sh — boot the ISO under several QEMU CPU models with the
# exception trace on, to hunt for the real-hardware #GP(0x28) shape.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Usage: cpu_matrix.sh <iso> [qemu] [ovmf-code] [ovmf-vars] [outdir]
# For each -cpu model: UEFI boot (OVMF), serial log checked for the
# boot-complete banner / panic; -d int,guest_errors,cpu_reset events
# are counted and any #GP (v=0d) outside the deliberate fault test is
# reported with its error code.

set -u
export LC_ALL=C

ISO="${1:?usage: cpu_matrix.sh <iso> [qemu] [ovmf-code] [ovmf-vars] [outdir]}"
QEMU="${2:-$(command -v qemu-system-x86_64)}"
OVMF_CODE="${3:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
OVMF_VARS="${4:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
OUT="${5:-build/cpu_matrix}"

CPUS="qemu64 Skylake-Client Skylake-Server Icelake-Server Cascadelake-Server EPYC"

# QEMU rom dir (efi roms etc.): distro layout <qemu>/../share/qemu
ROM_DIR="${QEMU_ROM_DIR:-$(dirname "$(command -v "$QEMU")")/../share/qemu}"
ROM_DIR=$(cd "$ROM_DIR" 2>/dev/null && pwd)

mkdir -p "$OUT"

for cpu in $CPUS; do
    log="$OUT/cpu_${cpu}.log"
    intlog="$OUT/cpu_${cpu}.int.log"
    rm -f "$log" "$intlog" "$OUT/vars.fd"
    cp "$OVMF_VARS" "$OUT/vars.fd"
    timeout 90 "$QEMU" -L "$ROM_DIR" -machine q35 -m 512M -cpu "$cpu" \
        -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
        -drive if=pflash,format=raw,file="$OUT/vars.fd" \
        -cdrom "$ISO" -serial file:"$log" -display none -no-reboot \
        -d int,guest_errors,cpu_reset -D "$intlog" >/dev/null 2>&1
    banner=$(grep -ac "boot complete" "$log" 2>/dev/null || echo 0)
    panics=$(grep -ac "KERNEL PANIC" "$log" 2>/dev/null || echo 0)
    # #GP events with error codes, excluding the deliberate fault test one
    gps=$(grep -a "v=0d" "$intlog" 2>/dev/null | wc -l)
    echo "$cpu: banner=$banner panics=$panics gp_events=$gps"
    if [ "$panics" != "0" ]; then
        grep -a -m3 "EXCEPTION\|PANIC" "$log" | head -3
    fi
done
