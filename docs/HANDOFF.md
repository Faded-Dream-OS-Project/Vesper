# Vesper — development handoff (2026-11-15, session 2)

State: milestones 1-3 complete and verified; `make run` and `make img`
both work. Branch `dev`, all commits attributed to FemBoyGamerTechGuy
<242450780+FemBoyGamerTechGuy@users.noreply.github.com> (history was
rewritten with git-filter-repo this session; mirror backup at
/home/z/my-project/vesper-backup-mirror, pre-rewrite dev was fc1e531).

## Identity rule
Every future commit MUST use:
  git config user.name  "FemBoyGamerTechGuy"
  git config user.email "242450780+FemBoyGamerTechGuy@users.noreply.github.com"
(or the same as GIT_AUTHOR_*/GIT_COMMITTER_* env vars). Never commit
under any other identity.

## Environment (recreated on demand)
- Tools live in /home/z/my-project/.tools/root (QEMU 10.0.13, OVMF 4M,
  mtools, xorriso, qemu-img, mkfs.vfat) — installed WITHOUT root via
  `bash /home/z/my-project/scripts/setup_tools.sh` (apt-get download +
  dpkg-deb -x). QEMU needs `-L /home/z/my-project/.tools/root/usr/share/qemu`.
- Build: host gcc 14.2, freestanding; Makefile auto-uses x86_64-elf-gcc
  if present. `make` / `make iso` / `make img` / `make run RUN_SECS=30`
  / `make clean`.
- Auth: credential helper in .git/config reads
  /home/z/my-project/.git-token (gitignored; never print or commit it).

## Verified this session (commands + observed output)
- Regression: clean rebuild; `make iso` + OVMF boot → banner + M2
  divide-by-zero dump + "system halted" on serial.
- `make run RUN_SECS=25` → same, in one command.
- `make img` → 64 MiB GPT image; boots under OVMF (AHCI) AND SeaBIOS
  with full banner + M2 dump.
- M3 self-tests on serial (both BIOS and UEFI paths):
  "selftest: pmm  6 passed, 0 failed -> ALL PASS"
  "selftest: vmm  7 passed, 0 failed -> ALL PASS"
  "selftest: heap 10 passed, 0 failed -> ALL PASS"

## Gotchas learned (do not re-learn these)
1. Limine request structs must be in program order: ordered
   subsections .limine_requests.0..8 in the linker script (kmain.c).
2. Limine rejects ELFs whose PHDRs with different permissions share a
   page: page-align every segment in kernel/linker.ld.
3. limine.conf v12 syntax: "option: value"; `serial: yes` is BIOS-only.
4. Fastest debug channel: BIOS boot `qemu ... -cdrom vesper.iso
   -serial file:build/serial.log` (Limine prints its own errors there,
   e.g. "PHDRs with different permissions sharing the same memory page").
5. GDT descriptors: bytes 0-1 limit lo, 2-4 base[0:24], 5 access,
   6 flags<<4|limit hi, 7 base[24:32].
6. Keep selectors 0x28/0x30 (Limine's CS/DS) valid in our GDT; TSS at
   0x38. Avoid a far CS reload (lretq) — mirror descriptors instead.
7. ISR stubs: explicit .rodata pointer table (isr_stub_table), never
   runtime pointer arithmetic over the stub region.
8. GPT images (the OVMF boot saga):
   - The protective MBR entry (type 0xEE) MUST span from LBA 1; OVMF
     rejects GPT disks otherwise (root cause of the whole "no ESP
     mount" chain — not the FAT, not CRCs, not attributes).
   - Huge pages are only legal at PDPT (1 GiB) / PD (2 MiB) level,
     never in the PML4 itself (a PS bit in a PML4 entry is ignored →
     the entry is read as a table pointer → #PF).
   - The kernel image needs plain 4 KiB pages: virt 0xffffffff80000000
     and phys (Limine-chosen) are NOT congruent mod 2 MiB, so huge
     pages would translate code to garbage physical addresses.
   - The HHDM must cover the full physical space (512 x 1 GiB PDPT
     entries): QEMU's framebuffer sits near 4 GiB and fb.c writes
     through the HHDM.
   - `limine bios-install <img> <N>` needs a BIOS boot partition
     (type GUID 21686148-...) as partition N; it also APPENDS ~532
     bytes of uninstall data to the image — truncate back afterward
     (`truncate -s 64M vesper.img`).
   - mformat for the ESP: `-c 1 -H 2048 -T <sectors>` → real FAT32
     with hidden sectors set.
   - This Limine build has NO `uefi-install` command; UEFI boots
     straight from BOOTX64.EFI in the ESP.
9. PMM/VMM interplay: walk() creates page-table frames from the PMM;
   they intentionally persist after unmap (self-tests account for
   that: free of the data frame must give exactly +1).
10. kprintf is homegrown: supports %s %c %d %u %x %p %% with '#',
    '0', width, and l/z/h length modifiers. u64 args print fine with
    %lx after the modifiers exist; when in doubt cast to (unsigned
    long long) + %llx.

## M4 plan (next milestone: timer + keyboard)
1. LAPIC (parse ACPI MADT via Limine's ACPI RSDP request — add
   `limine_rsdp_request` to kmain; Local APIC at MMIO, register
   LAPIC_TIMER with a calibration against the PIT).
   Simpler first cut: PIT on IRQ0 (0x40 ports, 100 Hz, mode 3),
   remap PIC (0x20/0xA0, ICW sequence), unmask IRQ0 only, EOI.
2. Timer interrupt path: irq_dispatcher must dispatch IRQ0 → handler
   with EOI; a jiffies counter + uptime print over serial proves it.
   Our stubs already route vectors 32..255 to irq_dispatcher; wire a
   small irq_install(n, handler) table in idt.c/isr.c.
3. PS/2 keyboard: IRQ1, ports 0x60/0x64, scancode set 1 → ASCII map;
   echo keys to serial. Verify with QEMU monitor `sendkey a` over the
   serial log.
4. Keep everything in C; asm only where C can't (iretq stubs exist).
5. Self-tests/verification: boot → "PIT: 100 Hz, 10 ticks in 100 ms"
   style output; `sendkey` → "key: a" on serial.

## Verification commands
```
export PATH=/home/z/my-project/.tools/root/usr/bin:/home/z/my-project/.tools/root/usr/sbin:$PATH
export LD_LIBRARY_PATH=/home/z/my-project/.tools/root/usr/lib/x86_64-linux-gnu:/home/z/my-project/.tools/root/usr/lib
make iso && make run RUN_SECS=25        # ISO under OVMF, serial tail printed
make img                                # GPT/FAT32 ESP disk image
SHARE=/home/z/my-project/.tools/root/usr/share/qemu
qemu-system-x86_64 -L $SHARE -machine q35 -m 512M \
  -device ahci,id=ahci0 -drive file=vesper.img,format=raw,if=none,id=hd0 \
  -device ide-hd,drive=hd0,bus=ahci0.0 \
  -serial file:build/serial.log -display none -no-reboot            # BIOS path
qemu-system-x86_64 -L $SHARE -machine q35 -m 512M \
  -drive if=pflash,format=raw,file=.../OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
  -device ahci,id=ahci0 -drive file=vesper.img,format=raw,if=none,id=hd0 \
  -device ide-hd,drive=hd0,bus=ahci0.0 \
  -serial file:build/serial.log -display none -no-reboot            # UEFI path
```
