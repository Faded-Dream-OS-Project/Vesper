#!/usr/bin/env bash
# gop_matrix.sh — boot the ISO under OVMF at several GOP resolutions.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# QEMU lets OVMF pick the GOP mode from -device ramfb / std VGA with
# xres/yres properties on the display device. This script boots the
# freshly built ISO at a matrix of resolutions and checks the serial
# log for the green completion banner, reporting PASS/FAIL per mode.
#
# Usage: gop_matrix.sh <iso> [qemu] [ovmf-dir]   (from repo root)

set -u
export LC_ALL=C

ISO="${1:-vesper.iso}"
QEMU="${2:-qemu-system-x86_64}"
OVMF_DIR="${3:-/usr/share/OVMF}"
OUT_DIR="${4:-build/gop}"

CODE="$OVMF_DIR/OVMF_CODE_4M.fd"
VARS="$OVMF_DIR/OVMF_VARS_4M.fd"

# QEMU rom dir (efi roms etc.): distro layout /usr/share/qemu
ROM_DIR="${QEMU_ROM_DIR:-$(dirname "$(dirname "$(command -v "$QEMU")")")/share/qemu}"

RESOLUTIONS="640x480 800x600 1024x768 1280x800 1920x1080 2560x1600"

mkdir -p "$OUT_DIR"
cp "$VARS" "$OUT_DIR/vars.fd"

for res in $RESOLUTIONS; do
    x="${res%x*}"; y="${res#*x}"
    log="$OUT_DIR/gop_${res}.log"
    rm -f "$log"
    # -device VGA with xres/yres steers OVMF GOP mode selection
    timeout 45 "$QEMU" -L "$ROM_DIR" -machine q35 -m 512M \
        -drive if=pflash,format=raw,file="$CODE",readonly=on \
        -drive if=pflash,format=raw,file="$OUT_DIR/vars.fd" \
        -cdrom "$ISO" \
        -device VGA,xres=$x,yres=$y \
        -serial file:"$log" -display none -no-reboot >/dev/null 2>&1
    rc=$?
    if [ $rc -eq 124 ] || [ $rc -eq 0 ]; then
        if grep -aq "boot complete" "$log" 2>/dev/null; then
            echo "PASS  $res  ($(grep -ac 'ALL PASS' "$log") suites green)"
        elif grep -aq "Vesper 0" "$log" 2>/dev/null; then
            echo "PARTIAL $res (kernel started, no banner)"
        else
            echo "FAIL  $res (no kernel output)"
        fi
    else
        echo "FAIL  $res (qemu rc=$rc)"
    fi
done
