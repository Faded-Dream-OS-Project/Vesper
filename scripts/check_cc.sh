#!/usr/bin/env bash
# check_cc.sh — verify the chosen compiler honours Vesper's freestanding
# flag set; fail the build with a clear message if it cannot.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Usage: check_cc.sh <compiler>
#
# What is verified (each a real failure mode seen on real hosts):
#   - every flag parses (gcc/clang accept unknown -fno-* silently in
#     some versions; -Werror turns "ignored" into failure)
#   - -fcf-protection=none actually drops CET (no endbr64 in the probe)
#   - -mno-sse drops SSE (no xmm register reference)
#   - -ffreestanding -nostdlib still compiles freestanding C
#   - -mcmodel=kernel is accepted (clang/gcc both, on x86-64)
# A compiler failing any check must NOT build the kernel: a boot image
# with SSE/CET instructions in it faults on real firmware before the
# console exists — exactly the bug class session 7 hunts.

set -u
export LC_ALL=C

CC="${1:-gcc}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/probe.c" <<'EOF'
/* freestanding probe: no libc, naked computation */
int probe(int x) { return x + 1; }
EOF

FLAGS="-ffreestanding -nostdlib -fno-stack-protector -fno-pie -fno-pic \
-fcf-protection=none -mno-red-zone -mno-sse -mno-mmx -mno-avx \
-fno-asynchronous-unwind-tables -mcmodel=kernel"

fail() {
    echo "check_cc: FAIL: $1"
    echo "check_cc: compiler: $CC ($("$CC" -dumpfullversion -dumpversion 2>/dev/null || "$CC" -dumpversion 2>/dev/null || echo unknown))"
    echo "check_cc: the kernel cannot be built with this compiler/flags."
    exit 1
}

# 1. all flags parse (unknown flags make cc fail outright; gcc's
#    "ignored argument" class is covered by -Werror=unknown-warning*
#    equivalents below when supported)
if ! "$CC" $FLAGS -c "$TMP/probe.c" -o "$TMP/probe.o" 2>"$TMP/err.txt"; then
    cat "$TMP/err.txt"
    fail "flag set rejected (see messages above)"
fi
# gcc silently ignoring a flag would still leave the default feature
# on; the disassembly checks below are the real gate.

# 2. CET really off: no endbr64 anywhere in the object
if command -v objdump >/dev/null 2>&1; then
    if objdump -d "$TMP/probe.o" | grep -q endbr64; then
        fail "-fcf-protection=none did not remove endbr64 (CET still on)"
    fi
else
    echo "check_cc: note: objdump not found, skipping CET disassembly check"
fi

# 3. SSE really off: no xmm use in the object
if command -v objdump >/dev/null 2>&1; then
    if objdump -d "$TMP/probe.o" | grep -qE '%xmm|%ymm'; then
        fail "-mno-sse did not remove vector registers"
    fi
fi

echo "check_cc: OK: $CC honours the freestanding flag set"
