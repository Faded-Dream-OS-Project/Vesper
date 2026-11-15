# Vesper — development handoff (2026-11-15, session 3)

State: milestones 1-5 complete and verified. Branch `main` (sole
branch; `dev` was force-merged into `main` and deleted this session,
default branch confirmed `main` via API). All commits attributed to
FemBoyGamerTechGuy
<242450780+FemBoyGamerTechGuy@users.noreply.github.com> (history
rewritten in session 2; mirror backups at
/home/z/my-project/vesper-backup-mirror{,-s3}).

## Identity rule
Every future commit MUST use:
  git config user.name  "FemBoyGamerTechGuy"
  git config user.email "242450780+FemBoyGamerTechGuy@users.noreply.github.com"
(or the same as GIT_AUTHOR_*/GIT_COMMITTER_* env vars). Never commit
under any other identity. No Co-authored-by trailers.

## Environment (recreated on demand)
- Tools live in /home/z/my-project/.tools/root (QEMU 10.0.13, OVMF 4M,
  mtools, xorriso, qemu-img, mkfs.vfat) — installed WITHOUT root via
  `bash /home/z/my-project/scripts/setup_tools.sh`. QEMU needs `-L
  /home/z/my-project/.tools/root/usr/share/qemu`.
- Build: host gcc 14.2, freestanding; `make` / `make iso` / `make img`
  / `make run RUN_SECS=30` / `make clean`. Warning-free (-Wall
  -Wextra; only the benign ld RWX-segment note remains).
- Auth: credential helper in .git/config reads
  /home/z/my-project/.git-token (gitignored; never print or commit it).

## Verified this session (commands + observed output)
- Regression after session recycle: `make clean && make && make iso &&
  make run RUN_SECS=25` — banner, GDT/IDT, pmm 6/6 vmm 7/7 heap 10/10,
  div-zero dump, system halted.
- M4: `bash scripts/kbd_test.sh` (automated QEMU monitor sendkey h/i/
  shift-h/caps_lock/a) -> "key: h", "key: i", "key: H", selftest 5/5.
- M4/M5 full serial log per boot (BIOS and UEFI paths):
  "PIC remapped (0x20-0x2F) and fully masked."
  "ACPI: MADT ok — LAPIC 0xfee00000, IOAPIC 0xfec00000 (GSI 0 base),
  1 LAPICs"
  "timer: LAPIC timer calibrated (1003872000 Hz bus), 100 Hz tick"
  "interrupts enabled (sti)."
  "sched: idle task online" -> threads a/b/c interleave -> "counters:
  a=5 b=5 c=5", selftest 5/5 -> M2 div-zero -> halt.

## Gotchas learned (do not re-learn these)
1. Limine request structs must be in program order: ordered
   subsections .limine_requests.0..N (kmain.c + linker.ld KEEP list —
   extend BOTH when adding a request).
2. Limine rejects ELFs whose PHDRs with different permissions share a
   page: page-align every segment in kernel/linker.ld.
3. limine.conf v12 syntax: "option: value"; `serial: yes` is BIOS-only.
4. Fastest debug channel: BIOS boot `qemu ... -cdrom vesper.iso
   -serial file:build/serial.log`.
5. GDT descriptors: bytes 0-1 limit lo, 2-4 base[0:24], 5 access,
   6 flags<<4|limit hi, 7 base[24:32]. Keep selectors 0x28/0x30 valid;
   TSS at 0x38. No far CS reload.
6. ISR stubs: explicit .rodata pointer table (isr_stub_table).
7. GPT images: protective MBR 0xEE entry MUST start at LBA 1 (OVMF
   rejects otherwise); huge pages only at PDPT/PD level; kernel image
   needs 4 KiB pages (virt/phys not 2 MiB-congruent); HHDM must cover
   the full physical space; limine bios-install appends ~532 bytes
   (truncate back); mformat `-c 1 -H 2048 -T <secs>` for a real FAT32
   ESP. No `uefi-install` command exists in this Limine build.
8. ACPI: Limine's rsdp response->address is ALREADY VIRTUAL (HHDM) —
   use directly; XSDT/RSDT entry pointers are PHYSICAL — translate via
   vmm_phys_to_hhdm. RSDP layout is packed: rev @15, rsdt @16, len @20,
   xsdt @24 (a natural C struct is off by one). SeaBIOS boots provide
   ACPI 1.0 (RSDT, 4-byte entries); OVMF provides 2.0 (XSDT) — parse
   both.
9. PIT: the one-shot gate is polled via the READ-BACK command
   (outb 0x43, 0xE2; status bit 7 = OUT pin). Port 0x61 bit 6 does NOT
   work on QEMU.
10. LAPIC: do not set reserved bits in the spurious-interrupt
    register; EOI is a plain write of 0 to 0xB0. QEMU TCG's LAPIC
    needs the initial count REWRITTEN on each timer interrupt or the
    periodic reload stalls after the first expiry (timer_tick re-arms
    — harmless on hardware).
11. Context switching: sched_switch_to MUST take (prev, next) as
    explicit arguments. Reading the global `current` inside the
    switch happens AFTER the caller updated it — the first version
    saved the new task's rsp over the old task's and "switched to
    itself" (livelock with zero crashes — extremely confusing).
12. Switching tasks from INSIDE an interrupt handler livelocks under
    QEMU TCG (threads get exactly one activation each). The timer IRQ
    only records the tick; threads round-robin cooperatively
    (sched_yield while waiting for the tick counter to advance).
13. kprintf from thread context is safe; fb console scrolling is a
    memmove storm under TCG — fb_console_disable()/enable() gate it
    (used around the scheduler self-test only).
14. kprintf is homegrown (%s %c %d %u %x %p %% with '#', '0', width,
    l/z/h modifiers); u64 args print fine with %lx — but volatiles
    promoted to int need casts.

## M6 plan (next milestone: syscalls + user mode)
1. Syscall layer (MSR STAR/LSTAR/MASK; syscall/sysret in a small .S
   stub; vector table in C — plain C only elsewhere).
2. User mode: page tables per process (own PML4 from the PMM/VMM
   helpers; user=1 pages; kernel entry region shared read-only).
3. Ring-3 trampoline + a tiny static "init" program embedded in the
   kernel image (no userland tools yet; a raw binary blob in .rodata
   is fine).
4. Self-tests: syscall echo (MSR syscall path), user-mode segfault
   caught by the #PF handler with user RIP reported, then a clean
   return to kernel threads.
5. Then M7 (USB image + docs; `make img` GPT/ESP groundwork exists).

## Verification commands
```
export PATH=/home/z/my-project/.tools/root/usr/bin:/home/z/my-project/.tools/root/usr/sbin:$PATH
export LD_LIBRARY_PATH=/home/z/my-project/.tools/root/usr/lib/x86_64-linux-gnu:/home/z/my-project/.tools/root/usr/lib
make clean && make && make iso
make run RUN_SECS=25                       # ISO under OVMF (UEFI), serial tail
bash scripts/kbd_test.sh                   # automated PS/2 keyboard self-test
SHARE=/home/z/my-project/.tools/root/usr/share/qemu
qemu-system-x86_64 -L $SHARE -machine q35 -m 512M \
  -cdrom vesper.iso -serial file:build/serial.log -display none -no-reboot   # BIOS path
```
