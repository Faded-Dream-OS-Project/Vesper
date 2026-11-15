# Vesper roadmap

Milestone status is updated as work is completed and verified.

| # | Milestone | Status | Verified how |
|---|-----------|--------|--------------|
| 1 | Boot in QEMU UEFI (OVMF), "Vesper" to serial + framebuffer | done | QEMU q35 + OVMF: serial log shows full banner; framebuffer screendump decoded to the same text |
| 2 | GDT, IDT, exception handlers, panic register dump | done | QEMU q35 (BIOS+UEFI): deliberate divide-by-zero caught; full register dump + panic + stack trace on serial, then halt |
| 3 | Physical memory manager, paging, kmalloc/kfree | pending | |
| 4 | Timer interrupts + keyboard driver | pending | |
| 5 | Scheduler with kernel threads + context switching | pending | |
| 6 | Syscalls, user mode, minimal init | pending | |
| 7 | Bootable USB image, safe fallbacks, flash/boot docs | pending | |

Notes:
- The kernel keeps Limine's boot selectors (CS=0x28, DS/SS=0x30) valid
  by placing matching descriptors in its own GDT at the same indices;
  TSS lives at 0x38.
- ISR stubs are generated (scripts/gen_isr_stubs.py) with an explicit
  pointer table (isr_stub_table) — no runtime pointer arithmetic.
