# Vesper

Vesper is the kernel of Faded Dream OS, an independent operating system for
PCs. It is written from scratch in plain C (C17, freestanding) and does not
use or derive from Linux, Windows NT, BSD, or any other kernel. There is no
libc: the kernel ships its own string/memory helpers and a minimal kprintf.

Vesper targets x86_64 first. It is booted by the Limine bootloader
(BSD-2-Clause, vendored under `thirdparty/limine/` with its license intact),
which hands over:

- a higher-half kernel at `0xffffffff80000000`,
- a framebuffer (when a linear framebuffer exists),
- the bootloader memory map,
- a higher-half direct map offset,
- a loaded 64 KiB stack.

## Building

Requirements: `gcc` (or `x86_64-elf-gcc`), `ld`, `make`, `xorriso`, and the
Limine host tool (built automatically from `thirdparty/limine/limine.c`).

```sh
make            # build vesper.elf
make iso        # build vesper.iso (BIOS + UEFI hybrid)
make clean
```

The build is warning-free with `-Wall -Wextra`.

## Running in QEMU (UEFI via OVMF)

With OVMF firmware and QEMU installed:

```sh
make run
# or manually:
qemu-system-x86_64 -machine q35 -m 512M \
    -drive if=pflash,format=raw,file=OVMF_CODE.fd,readonly=on \
    -drive if=pflash,format=raw,file=OVMF_VARS.fd \
    -cdrom vesper.iso \
    -serial file:serial.log -display none
```

Serial output (COM1, 115200 8N1) is the primary console; the framebuffer is
the second channel. Each is a fallback for the other: if either is missing,
the other still receives full kernel output.

## Milestone status

See `docs/ROADMAP.md`.

## License

Copyright (c) Faded Dream OS Project. All rights reserved. Source is viewable
but may not be copied, modified, or redistributed without permission. See
`LICENSE`. The Limine bootloader under `thirdparty/limine/` is distributed
under its own BSD-2-Clause license (see `thirdparty/limine/LICENSE`), which
remains intact.
