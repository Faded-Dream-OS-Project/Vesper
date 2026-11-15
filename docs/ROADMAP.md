# Vesper roadmap

Milestone status is updated as work is completed and verified.

| # | Milestone | Status | Verified how |
|---|-----------|--------|--------------|
| 1 | Boot in QEMU UEFI (OVMF), "Vesper" to serial + framebuffer | done | QEMU q35 + OVMF: serial log shows full banner; framebuffer screendump decoded to the same text |
| 2 | GDT, IDT, exception handlers, panic register dump | pending | |
| 3 | Physical memory manager, paging, kmalloc/kfree | pending | |
| 4 | Timer interrupts + keyboard driver | pending | |
| 5 | Scheduler with kernel threads + context switching | pending | |
| 6 | Syscalls, user mode, minimal init | pending | |
| 7 | Bootable USB image, safe fallbacks, flash/boot docs | pending | |
