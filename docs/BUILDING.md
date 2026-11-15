# Building Vesper

Copyright (c) Faded Dream OS Project. All rights reserved.
Source is viewable but may not be copied, modified, or redistributed
without permission.

This is the full build/test reference for a normal Linux machine
(Debian or Ubuntu; other distros work with the equivalent packages).
No sandbox-specific paths exist anywhere in the build: every tool is
found via `PATH` and standard distro locations, with environment
overrides.

## Dependencies

One-time install on Debian or Ubuntu:

```sh
sudo apt-get install -y build-essential gcc python3 xorriso mtools qemu-system-x86 ovmf
```

| Tool | Debian/Ubuntu package | Needed for |
| --- | --- | --- |
| gcc (or x86_64-elf-gcc) | build-essential | kernel + user blob |
| ld, objcopy | binutils (pulled by build-essential) | linking |
| python3 | python3 | ISR stub generation, GPT builder, build scripts |
| git | git | build-ID stamps (optional — falls back to `unknown`) |
| xorriso | xorriso | `make iso` |
| mformat/mmd/mcopy (mtools) | mtools | `make img` (the FAT32 ESP) |
| qemu-system-x86_64 | qemu-system-x86 | `make run`, all boot tests |
| OVMF firmware | ovmf | UEFI boot tests (`/usr/share/OVMF`) |
| python3 PIL | python3-pil | pixel checks in `verify_image.sh` (optional) |

Run `make doctor` (or `bash scripts/doctor.sh`) at any time to check
all of these; it prints the exact install line for what is missing
and never writes anything.

Toolchain choice, in order:

1. `x86_64-elf-gcc` cross toolchain if installed (preferred);
2. host `gcc`, **but only if it passes `scripts/check_cc.sh`**: the
   compiler is given the exact freestanding flag set and its output is
   disassembled — a compiler that silently ignores
   `-fcf-protection=none` (leaves `endbr64` in the code) or
   `-mno-sse` (emits xmm instructions) fails the build loudly instead
   of producing a kernel that faults before the console exists on
   real firmware.

The forced freestanding flag set (`CFLAGS` in the Makefile):
`-ffreestanding -nostdlib -fno-stack-protector -fno-pie -fno-pic
-fcf-protection=none -mno-red-zone -mno-sse -mno-mmx -mno-avx
-fno-asynchronous-unwind-tables -mcmodel=kernel`.

## Commands

All commands run from the repository root. `make` prints the
compiler and version on every build; every kernel prints its exact
build identity (`git hash`, dirty flag, compiler, build stamp) as the
first line on screen — a photo of any boot identifies the build.

| Command | Produces |
| --- | --- |
| `make` | `vesper.elf` — the kernel ELF (~250 KiB on disk; `.bss` is NOBITS) |
| `make iso` | `vesper.iso` — UEFI+BIOS hybrid ISO (cdrom, Ventoy) |
| `make img` | `vesper.img` — 64 MiB GPT+FAT32 ESP disk image (USB stick) |
| `make run` | boots the ISO under QEMU/OVMF (UEFI), serial to `build/serial.log` |
| `make doctor` | environment check, prints what is missing + install line |
| `make clean` | removes `build/`, kernel, ISO, IMG |

Environment overrides (all optional): `QEMU_BIN`, `OVMF_DIR`,
`OVMF_CODE`, `OVMF_VARS`, `CONF` (alternative limine.conf, used by the
self-test scripts), `RUN_SECS` (make run timeout).

All scripts run under `LC_ALL=C` and never depend on the locale of
tool output.

## Test scripts

| Script | What it does |
| --- | --- |
| `scripts/verify_image.sh` | validates `vesper.img` (GPT CRCs, ESP files, USB boot under OVMF with pixel-verified screen). Builds the image itself if missing. |
| `scripts/gop_matrix.sh <iso>` | boots the ISO at 6 GOP resolutions (640x480..2560x1600) under OVMF; PASS when the green completion banner appears. |
| `scripts/guard_test.sh` | boots with `guard_test` cmdline: stack-overflow guard page must catch the overflow with a readable panic. |
| `scripts/kbd_test.sh` | boots with `kbd_test` cmdline: QEMU monitor `sendkey` drives the PS/2 keyboard test. |
| `scripts/bisect_pack.sh <repo> <outdir> <qemu> <ovmf-code> <ovmf-vars>` | builds vesper-\<commit\>.iso/.img for 8789845/69b84e3/9c09219/eaeeec2 with build-ID + breadcrumbs instrumentation (uncommitted), QEMU-tests each, writes SHA256SUMS. |
| `scripts/doctor.sh` | same as `make doctor`. |

## Boot entries

`limine.conf` provides three entries (first is the default):
**Vesper (normal)**, **Vesper (diagnostics hold)** (freezes on the
diagnostics screen for photographing) and **Vesper (quiet)** (serial
only, no rendering). Extra kernel cmdline flags understood by the
kernel: `poison`, `syscall_trace`, `kbd_test`, `guard_test`,
`panic_test`, `diagnostics_hold`, `quiet`.

## From a clean clone, end to end

```sh
git clone <repo> vesper && cd vesper
make doctor          # check the environment
make img iso         # build the kernel, ISO and USB image
bash scripts/verify_image.sh   # full verification incl. UEFI USB boot
```

That exact flow was verified from a clean clone in a scrubbed
environment (no sandbox variables, different directory): the image
self-built and all checks passed, including the pixel-verified USB
boot screen.
