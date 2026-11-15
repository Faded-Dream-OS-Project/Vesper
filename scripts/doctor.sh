#!/usr/bin/env bash
# doctor.sh — check every tool Vesper's build/test flow needs.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Prints PASS/MISSING per tool with the exact Debian/Ubuntu install
# line for anything missing. Exit 0 if everything needed for
# `make && make iso` is present; missing QEMU/OVMF/mtools only warn
# (they are needed for img/run/tests, not the basic build).
#
# All output is fixed English/ASCII and runs under LC_ALL=C so the
# script itself never depends on locale. PATH-only discovery (no
# hard-coded sandbox paths).

set -u
export LC_ALL=C

MISSING_BUILD=0   # blocks make/isolated kernel build
MISSING_TEST=0   # blocks img / run / verification

note()  { printf '%-14s PASS  %s\n' "$1" "$2"; }
miss()  { printf '%-14s MISSING %s\n' "$1" "$2"; }

APT_LINE='sudo apt-get install -y build-essential gcc python3 xorriso mtools qemu-system-x86 ovmf'
echo "Vesper build doctor"
echo "  (if anything is missing, as root or with sudo on Debian/Ubuntu):"
echo "      $APT_LINE"
echo

# ---- compilers / linker (kernel + host tools) ----
if command -v x86_64-elf-gcc >/dev/null 2>&1; then
    note x86_64-elf-gcc "$(x86_64-elf-gcc -dumpversion 2>/dev/null) (preferred cross toolchain)"
elif command -v gcc >/dev/null 2>&1; then
    note gcc "$(gcc -dumpfullversion -dumpversion 2>/dev/null || gcc -dumpversion) (host gcc + freestanding flags)"
else
    miss gcc "no C compiler at all"
    MISSING_BUILD=1
fi

if command -v ld >/dev/null 2>&1; then note ld "$(ld --version 2>/dev/null | head -1 | cut -d' ' -f4)"
else miss ld "linker"; MISSING_BUILD=1; fi

if command -v objcopy >/dev/null 2>&1; then note objcopy "binutils"
else miss objcopy "binutils"; MISSING_BUILD=1; fi

# ---- build scripting ----
command -v python3 >/dev/null 2>&1 && note python3 "$(python3 --version 2>&1 | cut -d' ' -f2)" || { miss python3 "scripts (gen_isr_stubs, make_gpt)"; MISSING_BUILD=1; }
command -v git >/dev/null 2>&1 && note git "$(git --version 2>/dev/null | cut -d' ' -f3)" || miss git "version stamps (build ID falls back to 'unknown')"
command -v truncate >/dev/null 2>&1 && note truncate "coreutils" || miss truncate "img final size"
command -v stat >/dev/null 2>&1 && note stat "coreutils" || miss stat "img build"

# ---- ISO creation ----
if command -v xorriso >/dev/null 2>&1; then note xorriso "$(xorriso --version 2>/dev/null | head -1 | grep -o 'xorriso [0-9.]*' | cut -d' ' -f2)"
else miss xorriso "make iso"; MISSING_TEST=1; fi

# ---- disk image (mtools) ----
for t in mformat mmd mcopy mdir; do
    command -v $t >/dev/null 2>&1 || { miss mtools "$t (make img)"; MISSING_TEST=1; }
done
command -v mformat >/dev/null 2>&1 && note mtools "mformat/mmd/mcopy/mdir"

# ---- QEMU (run/tests) ----
QEMU_BIN=$(command -v qemu-system-x86_64 2>/dev/null || true)
if [ -n "$QEMU_BIN" ]; then
    note qemu "$(command "$QEMU_BIN" --version 2>/dev/null | head -1 | grep -o 'version [0-9.]*' | cut -d' ' -f2)"
else
    miss qemu-system "make run, verify_image.sh, GOP matrix"
    MISSING_TEST=1
fi

# ---- OVMF (UEFI tests): PATH, then distro locations ----
OVMF_DIR=""
for d in "${OVMF_DIR_OVERRIDE:-}" /usr/share/OVMF /usr/share/ovmf \
         /usr/share/edk2/ovmf /usr/share/qemu/OVMF; do
    [ -n "$d" ] && [ -f "$d/OVMF_CODE_4M.fd" ] && { OVMF_DIR="$d"; break; }
    [ -n "$d" ] && [ -f "$d/OVMF_CODE.fd" ] && { OVMF_DIR="$d"; break; }
done
if [ -n "$OVMF_DIR" ]; then
    note OVMF "$OVMF_DIR"
else
    miss OVMF "UEFI boot tests (BIOS-only tests still work)"
    MISSING_TEST=1
fi

# ---- Python PIL (verify_image.sh pixel analysis) ----
if python3 -c "import PIL" >/dev/null 2>&1; then note PIL "screendump pixel checks"
else
    miss PIL "verify_image.sh pixel analysis (python3-pil)"
fi

echo
if [ "$MISSING_BUILD" -ne 0 ]; then
    echo "RESULT: kernel build BLOCKED — install the missing build tools first."
    exit 1
fi
if [ "$MISSING_TEST" -ne 0 ]; then
    echo "RESULT: kernel will BUILD; image/UEFI test tooling incomplete."
    exit 0
fi
echo "RESULT: all tools present — build, image and test flows are good to go."
exit 0
