# Vesper roadmap

Milestone status is updated as work is completed and verified.

| # | Milestone | Status | Verified how |
|---|-----------|--------|--------------|
| 1 | Boot in QEMU UEFI (OVMF), "Vesper" to serial + framebuffer | done | QEMU q35 + OVMF: serial log shows full banner; framebuffer screendump decoded to the same text |
| 2 | GDT, IDT, exception handlers, panic register dump | done | QEMU q35 (BIOS+UEFI): deliberate divide-by-zero caught; full register dump + panic + stack trace on serial, then halt |
| 3 | Physical memory manager, paging, kmalloc/kfree | done | QEMU q35 (BIOS+UEFI): self-tests on serial — pmm 6/6, vmm 7/7 (own PML4 active, map/unmap/write), heap 10/10 (incl. 64-alloc stress) ALL PASS |
| 4 | Timer interrupts + keyboard driver | done | QEMU q35 (BIOS+UEFI): PIC remapped+masked, MADT parsed (LAPIC 0xFEE00000, IOAPIC 0xFEC00000), LAPIC timer calibrated vs PIT (1003.9 MHz bus, 100 Hz tick), timer self-test 4/4 (100 ms sleep -> ~10 ticks, EOI loop); PS/2 keyboard via IOAPIC GSI 1, automated QEMU-monitor sendkey harness (scripts/kbd_test.sh): 'key: h/i/H' + 'selftest: 5 passed, 0 failed -> ALL PASS' |
| 5 | Scheduler with kernel threads + context switching | done | QEMU q35 (BIOS+UEFI): PREEMPTIVE round robin — 3 threads that never yield, pure busy loops to 2,500,000 each, 'counters: a=2500000 b=2500000 c=2500000', selftest 5/5 ALL PASS; cooperative sched_yield kept; guard page below every thread stack (scripts/guard_test.sh: overflowing thread -> readable #PF with task name, halt, no silent corruption) |
| 6 | Syscalls, user mode, minimal init | done | QEMU q35 (BIOS+UEFI): init runs in ring 3 — 'init: hello from ring 3', getpid()=4, uptime_ms()=670, write(bad ptr) -> errno 1 (no kernel fault), 5x yield loop, exit(0); fault_init executes hlt in ring 3 -> only that process killed, kernel keeps ticking; selftest 3/3 ALL PASS; ABI documented in docs/SYSCALLS.md |
| 7 | Bootable USB image, safe fallbacks, flash/boot docs | done + FIRST HARDWARE RUN (Ventoy/UEFI): M1-M5 pass on metal; ring-3 failure (root cause not reproducible in QEMU — defensive fixes + syscall_trace instrumentation landed); console slow -> shadow-buffer rewrite (flush cost now measurable); boot menu (normal/diagnostics-hold/quiet) | On-screen console (colour PASS/FAIL, panic in red, any bpp/pitch, 1x-4x scaling) verified by QEMU screendump pixel analysis incl. `-serial none` and `-nodefaults`; boot diagnostics block (CPU brand, CPUID flags, memory, fb mode, ACPI rev/tables, IOAPICs, MADT overrides, boot mode) verified BIOS+UEFI; IOAPIC honours MADT overrides + multiple controllers; timer ladder PIT->HPET->CPUID->degraded all exercised; PS/2 probe with timeouts; idle uses hlt; `-smp 4` tolerated; USB mass-storage boot under OVMF (qemu-xhci) passes all suites; `scripts/verify_image.sh` = GPT CRCs + ESP inventory + no-serial USB boot screen check -> ALL CHECKS PASS; docs/REAL_HARDWARE.md flash guide |
| 8 (session 7) | Hardware-regression hunt: 4 boot hazards fixed (PAT walk on Limine big-page tables pre-IDT; 4.5 MiB PROGBITS .bss ELF; serial-only fallback above 1280x800; pre-IDT CR writes) + build-ID line + breadcrumbs + portable build (doctor, check_cc, no sandbox paths) + bisect pack | QEMU BIOS+UEFI all suites ALL PASS with codes 1..15 on 0xE9; GOP matrix 640x480..2560x1600 6/6 'fb ok, PAT-WC yes'; ELF 4573072->247720 bytes; clean-clone scrubbed-env verify_image ALL CHECKS PASS; bisect pack 4 images BIOS+UEFI boot-tested |
| 9 (session 8) | Real-hardware #GP(0x28) root-caused and fixed: SYSRET ran user code through a DPL-0 'Limine compat' descriptor at 0x28; GDT now fully owned (user DPL3 descriptors at 0x28/0x30, A-bit preset, far-return + SS reload); GDT dump + err decode in handlers; lar/lsl boot verification; sysret frame validation with named panics; strict per-suite boot summary (PASS/FAIL/NOT RUN, panic-in-suite = FAIL); scripts/cpu_matrix.sh | QEMU UEFI+BIOS 6/6 suites ALL PASS (7 'ALL PASS' lines incl. strict summary), 0 FAIL; CPU matrix 6 models: only the deliberate fault-test #GP; GDT[5] dump shows DPL=3 A=1; syscall_trace pins photo RIP 0x40012c; hardware fix NOT yet verified (needs the human's i5-8400) |

Session-4 shortcut paydown (all verified):
- Preemptive scheduling root-caused and fixed: the switched-in task
  entered via `ret` (nothing restored IF — it could never be preempted
  again, one activation each) AND the LAPIC timer vector stayed
  in-service because the EOI only ran on the interrupted task's own
  return path. Fix: `sti` in sched_switch_to between the RSP store and
  load + EOI before preemption. Preemption now fires at the outermost
  interrupt level (nesting-safe).
- Keyboard self-test gated behind the `kbd_test` kernel cmdline flag
  (limine.conf; scripts/kbd_test.sh sets it). Normal boots print
  'kbd: skipped' and stay all-green.
- RWX linker warning gone: LOAD segments split R+X / R / R+W,
  page-aligned, warning-free link on BIOS and UEFI.
- Guard page below each kernel thread stack + IST_PF stack for #PF
  (stack exhaustion cannot escalate to #DF); scripts/guard_test.sh.

Infrastructure:
- `main` is the sole branch (dev merged + deleted in session 3).
- `make run` boots the ISO under OVMF in one command (RUN_SECS
  budget, serial tail printed).
- `make img` builds a 64 MiB GPT disk: BIOS-boot partition (Limine
  stage 2), FAT32 ESP with BOOTX64.EFI + limine.conf + kernel; boots
  under OVMF (AHCI) and SeaBIOS.
- `scripts/kbd_test.sh` runs the automated keyboard self-test.
- History rewritten to a single identity (FemBoyGamerTechGuy
  <242450780+FemBoyGamerTechGuy@users.noreply.github.com>); that
  identity is used for every commit.

Notes:
- The kernel keeps Limine's boot selectors (CS=0x28, DS/SS=0x30) valid
  by placing matching descriptors in its own GDT at the same indices;
  TSS lives at 0x38.
- ISR stubs are generated (scripts/gen_isr_stubs.py) with an explicit
  pointer table (isr_stub_table) — no runtime pointer arithmetic.
- A USB keyboard on real hardware will not appear as PS/2; the
  keyboard stack is QEMU-only until USB lands (M7+).
