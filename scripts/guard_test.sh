#!/usr/bin/env bash
# guard_test.sh — kernel-thread stack-overflow guard-page self-test.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Builds the ISO with limine.conf cmdline 'guard_test' and boots it.
# The kernel creates a thread that recurses until its stack hits the
# unmapped guard page below it: the #PF dump (CR2, task name, RSP)
# must mention task 'ovf' and fault at an address BELOW the 16 KiB
# stack's guard base. A readable panic instead of silent corruption
# is the pass condition.

set -u
export LC_ALL=C
# QEMU from PATH (env override QEMU_BIN); roms dir derived from it.
QEMU="${QEMU_BIN:-$(command -v qemu-system-x86_64 2>/dev/null)}"
SHARE=$(dirname "$(command -v "$QEMU")")/../share/qemu
SHARE=$(cd "$SHARE" 2>/dev/null && pwd)
cd "$(dirname "$0")/.."

sed 's/^    cmdline:$/    cmdline: guard_test/' limine.conf > build/limine.conf.guard
make iso CONF=build/limine.conf.guard >/dev/null 2>&1 || \
    { echo "make iso failed"; exit 1; }

rm -f build/serial.log
timeout 90 "$QEMU" -L "$SHARE" -machine q35 -m 512M \
    -cdrom vesper.iso -serial file:build/serial.log -display none -no-reboot \
    > /dev/null 2>&1 &
QPID=$!

for i in $(seq 1 18); do
    sleep 5
    grep -aq "KERNEL PANIC\|system halted" build/serial.log 2>/dev/null && break
done
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null

# restore default ISO
make iso >/dev/null 2>&1

grep -a "guard\|EXCEPTION #14\|CR2\|PANIC\|task 'ovf'\|system halted" \
    build/serial.log | tr -c '[:print:]\n' ' '
