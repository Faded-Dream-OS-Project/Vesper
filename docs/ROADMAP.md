# Vesper roadmap

Milestone status is updated as work is completed and verified.

| # | Milestone | Status | Verified how |
|---|-----------|--------|--------------|
| 1 | Boot in QEMU UEFI (OVMF), "Vesper" to serial + framebuffer | done | QEMU q35 + OVMF: serial log shows full banner; framebuffer screendump decoded to the same text |
| 2 | GDT, IDT, exception handlers, panic register dump | done | QEMU q35 (BIOS+UEFI): deliberate divide-by-zero caught; full register dump + panic + stack trace on serial, then halt |
| 3 | Physical memory manager, paging, kmalloc/kfree | done | QEMU q35 (BIOS+UEFI): self-tests on serial — pmm 6/6, vmm 7/7 (own PML4 active, map/unmap/write), heap 10/10 (incl. 64-alloc stress) ALL PASS |
| 4 | Timer interrupts + keyboard driver | pending | |
| 5 | Scheduler with kernel threads + context switching | pending | |
| 6 | Syscalls, user mode, minimal init | pending | |
| 7 | Bootable USB image, safe fallbacks, flash/boot docs | pending (image groundwork done: `make img` GPT+FAT32 ESP boots OVMF+SeaBIOS) | |

Infrastructure:
- `make run` boots the ISO under OVMF in one command (RUN_SECS
  budget, serial tail printed).
- `make img` builds a 64 MiB GPT disk: BIOS-boot partition (entry 1,
  Limine stage 2), FAT32 ESP (entry 0) with BOOTX64.EFI +
  limine.conf + kernel; boots under OVMF (AHCI) and SeaBIOS.
- History rewritten to a single identity (FemBoyGamerTechGuy
  <242450780+FemBoyGamerTechGuy@users.noreply.github.com>); that
  identity must be used for every future commit.

Notes:
- The kernel keeps Limine's boot selectors (CS=0x28, DS/SS=0x30) valid
  by placing matching descriptors in its own GDT at the same indices;
  TSS lives at 0x38.
- ISR stubs are generated (scripts/gen_isr_stubs.py) with an explicit
  pointer table (isr_stub_table) — no runtime pointer arithmetic.
