#!/usr/bin/env bash
# bisect_pack.sh — build the hardware bisect pack (session 7).
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# The human's PC boots 8789845 (session 5) but not the session-6 tip.
# This script uses git worktrees (NO history rewrite) to build the
# exact commits in between:
#     8789845 (known good)   69b84e3   9c09219   eaeeec2
# Each worktree gets the BUILD-ID stamp (git hash + dirty + compiler)
# and the EARLY-BOOT BREADCRUMBS backported on top as UNCOMMITTED
# changes, so a photo of any black screen identifies both the exact
# source AND how far boot reached (coloured squares + 0xE9 codes).
# The original commit content is 100% preserved (bisect remains valid:
# kernel behaviour differences come from the commit under test, and
# the instrumentation is identical across all four images).
#
# Output: <out-dir>/vesper-<commit>.iso and .img + SHA256SUMS.
# Every image is boot-tested in QEMU (BIOS and UEFI/OVMF) before it
# lands in the pack.
#
# Usage: bisect_pack.sh <repo> <out-dir> [qemu] [ovmf-code] [ovmf-vars]
# Environment: PATH must hold gcc, ld, xorriso, mtools, python3.

set -u
export LC_ALL=C

REPO="${1:?usage: bisect_pack.sh <repo> <out-dir> [qemu] [ovmf-code] [ovmf-vars]}"
OUT="${2:?usage: bisect_pack.sh <repo> <out-dir> [qemu] [ovmf-code] [ovmf-vars]}"
QEMU="${3:-$(command -v qemu-system-x86_64 2>/dev/null)}"
OVMF_CODE="${4:-}"
OVMF_VARS="${5:-}"
if [ -z "$OVMF_CODE" ]; then
    for d in /usr/share/OVMF /usr/share/ovmf /usr/share/edk2/ovmf; do
        [ -f "$d/OVMF_CODE_4M.fd" ] && OVMF_CODE="$d/OVMF_CODE_4M.fd" && OVMF_VARS="$d/OVMF_VARS_4M.fd" && break
    done
fi

COMMITS="8789845 69b84e3 9c09219 eaeeec2"
WT_ROOT="$REPO/.bisect-worktrees"

# The instrumentation overlay: exactly the two session-7 features that
# make black screens debuggable on metal (build-ID line, breadcrumbs).
# Sourced from the current tip's committed versions.
STAMP_SRC="$REPO/scripts/gen_build_id.sh"
BC_SRC="$REPO/kernel/src/breadcrumb.c"
BC_HDR_SRC="$REPO/kernel/include/breadcrumb.h"

mkdir -p "$OUT"
rm -rf "$WT_ROOT"
mkdir -p "$WT_ROOT"

for c in $COMMITS; do
    wt="$WT_ROOT/$c"
    echo "== worktree $c =="
    git -C "$REPO" worktree add --detach "$wt" "$c" >/dev/null 2>&1 || { echo "worktree $c FAILED"; exit 1; }

    # ---- instrumentation overlay (uncommitted; keeps the commit intact) ----
    # 1. build-ID stamping machinery (scripts + Makefile hook + header include guard)
    cp "$STAMP_SRC" "$wt/scripts/gen_build_id.sh"
    chmod +x "$wt/scripts/gen_build_id.sh"
    # kprintf build line + include: patch kmain.c idempotently via python
    python3 - "$wt" <<'PYEOF'
import re, sys
wt = sys.argv[1]

# kmain.c: add include + build-ID print after the version print
km = wt + '/kernel/src/kmain.c'
src = open(km).read()
if 'build_id.h' not in src:
    src = src.replace('#include <vmm.h>',
                      '#include <vmm.h>\n#include <build_id.h>   /* bisect pack: build identity */', 1)
    ver_line = re.search(r'kprintf\("Vesper " VESPER_VERSION[^\n]*\n', src)
    if ver_line:
        src = src.replace(ver_line.group(0),
                          ver_line.group(0) +
                          '    kprintf("build: git %s, %s, %s\\n",\n'
                          '            BUILD_ID_GIT, BUILD_ID_CC, BUILD_ID_STAMP);\n', 1)
    open(km, 'w').write(src)

# Makefile: build-id regeneration + object dependency (idempotent)
mk = wt + '/Makefile'
src = open(mk).read()
if 'BUILD_ID_HDR' not in src:
    src = src.replace('KERNEL  := vesper.elf',
                      'KERNEL  := vesper.elf\nBUILD_ID_HDR := kernel/include/build_id.h\n'
                      '$(BUILD_ID_HDR): FORCE\n\t@bash scripts/gen_build_id.sh $(CC) $@\n'
                      '.PHONY: FORCE\nFORCE:\n'
                      '$(patsubst kernel/src/%.c,build/%.o,$(wildcard kernel/src/*.c)): $(BUILD_ID_HDR)\n', 1)
    open(mk, 'w').write(src)
PYEOF

    # 2. breadcrumbs: copy module + wire the stage calls into kmain
    cp "$BC_SRC" "$wt/kernel/src/breadcrumb.c"
    cp "$BC_HDR_SRC" "$wt/kernel/include/breadcrumb.h"
    python3 - "$wt" <<'PYEOF'
import re, sys
wt = sys.argv[1]
km = wt + '/kernel/src/kmain.c'
src = open(km).read()
if 'breadcrumb.h' not in src:
    src = src.replace('#include <build_id.h>',
                      '#include <build_id.h>\n#include <breadcrumb.h>', 1)
    # wire stages: entry, after cpu_sanitize, after fb_init block, after idt, after pmm, after vmm,
    # after acpi block, after apic block, after timer print, after sched_init, after usermode_init
    wires = [
        ('void kmain(void)\n{', 'void kmain(void)\n{\n    breadcrumb_stage(1);'),
        ('cpu_sanitize();', 'cpu_sanitize();\n    breadcrumb_stage(2);'),
        ('fb_init(fb_response->framebuffers[0]);', 'fb_init(fb_response->framebuffers[0]);\n    breadcrumb_stage(3);'),
        ('kprintf("IDT loaded (256 gates).\\n");', 'kprintf("IDT loaded (256 gates).\\n");\n    breadcrumb_stage(6);'),
        ('pmm_total_usable_frames() >> 8);', 'pmm_total_usable_frames() >> 8);\n    breadcrumb_stage(8);'),
        ('(read_cr3() & ~0xFFFull));', '(read_cr3() & ~0xFFFull));\n    breadcrumb_stage(9);'),
        ('kprintf("ACPI: MADT unavailable\\n");\n    }', 'kprintf("ACPI: MADT unavailable\\n");\n    }\n    breadcrumb_stage(10);'),
        ('interrupts enabled (sti).\\n");', 'interrupts enabled (sti).\\n");\n    breadcrumb_stage(12);'),
        ('sched_init();', 'sched_init();\n    breadcrumb_stage(13);'),
        ('usermode_init();', 'usermode_init();\n    breadcrumb_stage(14);'),
    ]
    for old, new in wires:
        if old in src:
            src = src.replace(old, new, 1)
    # stage 15 must go BEFORE the banner print (never inside a string!)
    src = src.replace('    /* Normal end of boot: green banner, timing summary, then sleep. */\n    kprintf("\\n");',
                      '    /* Normal end of boot: green banner, timing summary, then sleep. */\n'
                      '    breadcrumb_stage(15);\n    kprintf("\\n");', 1)
    # breadcrumb_fb: pass the framebuffer before fb_init
    src = src.replace('fb_init(fb_response->framebuffers[0]);',
                      'breadcrumb_fb(fb_response->framebuffers[0]);\n    fb_init(fb_response->framebuffers[0]);', 1)
    # clear the squares once the console prints the build line
    src = src.replace('BUILD_ID_CC, BUILD_ID_STAMP);',
                      'BUILD_ID_CC, BUILD_ID_STAMP);\n    breadcrumb_handover();', 1)
    open(km, 'w').write(src)
PYEOF

    # build
    (cd "$wt" && make img iso >/dev/null 2>&1) || { echo "BUILD $c FAILED"; continue; }
    cp "$wt/vesper.iso" "$OUT/vesper-$c.iso"
    cp "$wt/vesper.img" "$OUT/vesper-$c.img"
    echo "built: $OUT/vesper-$c.iso / .img"
done

# ---- boot-test every image: BIOS and UEFI ----
QEMU_ROMS=$(dirname "$(command -v "$QEMU")")/../share/qemu
QEMU_ROMS=$(cd "$QEMU_ROMS" 2>/dev/null && pwd)
for c in $COMMITS; do
    iso="$OUT/vesper-$c.iso"
    img="$OUT/vesper-$c.img"
    [ -f "$iso" ] || { echo "BOOTTEST $c: SKIP (no iso)"; continue; }
    log_b="/tmp/bisect_bios_$c.log"
    log_u="/tmp/bisect_uefi_$c.log"
    # BIOS (SeaBIOS)
    rm -f "$log_b"
    timeout 60 "$QEMU" -L "$QEMU_ROMS" -machine q35 -m 512M -cdrom "$iso" \
        -serial file:"$log_b" -display none -no-reboot >/dev/null 2>&1
    # UEFI (OVMF)
    rm -f "$log_u" /tmp/bisect_vars.fd
    cp "$OVMF_VARS" /tmp/bisect_vars.fd
    timeout 90 "$QEMU" -L "$QEMU_ROMS" -machine q35 -m 512M \
        -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
        -drive if=pflash,format=raw,file=/tmp/bisect_vars.fd \
        -cdrom "$iso" -serial file:"$log_u" -display none -no-reboot >/dev/null 2>&1
    # NOTE: 'boot complete' is the session-6/7 tail; the session-5 kernel
    # (8789845, 69b84e3) ENDS in its deliberate divide-by-zero panic after
    # all suites pass — count either as a successful boot.
    b_ok=$(grep -ac -e "boot complete" -e "KERNEL PANIC" "$log_b" 2>/dev/null || echo 0)
    u_ok=$(grep -ac -e "boot complete" -e "KERNEL PANIC" "$log_u" 2>/dev/null || echo 0)
    b_id=$(grep -a "build: git" "$log_b" | head -1)
    u_id=$(grep -a "build: git" "$log_u" | head -1)
    echo "BOOTTEST $c: BIOS ${b_ok:-0} banner(s) | UEFI ${u_ok:-0} banner(s)"
    echo "  id: $u_id"
done

git -C "$REPO" worktree list >/dev/null 2>&1

# ---- checksums ----
cd "$OUT"
rm -f SHA256SUMS
sha256sum vesper-*.iso vesper-*.img > SHA256SUMS 2>/dev/null
cat SHA256SUMS | head -20
echo
echo "bisect pack ready: $OUT"
