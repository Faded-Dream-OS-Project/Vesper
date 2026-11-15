# Vesper — development handoff (2026-11-15)

State: milestones 1 and 2 complete, verified, committed and pushed to
`origin/dev` (branch `dev`). Milestones 3-7 remain.

## Environment (recreated on demand)
- Tools live in /home/z/my-project/.tools/root (QEMU 10.0.13, OVMF 4M,
  mtools, xorriso, qemu-img, mkfs.vfat) — installed WITHOUT root via
  `bash /home/z/my-project/scripts/setup_tools.sh` (apt-get download +
  dpkg-deb -x). QEMU needs `-L /home/z/my-project/.tools/root/usr/share/qemu`
  (seabios ROMs are symlinked there).
- Build: host gcc 14.2 with -ffreestanding -nostdlib -mcmodel=kernel;
  Makefile auto-uses x86_64-elf-gcc if present.
- Auth: git credential helper in the repo config reads
  /home/z/my-project/.git-token (gitignored, never printed/committed).

## Verified so far
- M1: boots via Limine 12.10.0 on QEMU q35 under BOTH OVMF (UEFI) and
  SeaBIOS; banner on serial AND framebuffer (screendump decoded to
  "Vesper 0.1.0 (kernel of Faded Dream OS)" glyph-by-glyph).
- M2: GDT (canonical descriptor layout, mirrors Limine selectors so
  running CS/SS stay valid; TSS at 0x38 with IST stacks), IDT (256
  gates via generated stubs + explicit isr_stub_table), exception
  handlers with full register dump, panic with stack unwind. Verified:
  deliberate divide-by-zero → "EXCEPTION #0 (Divide-by-zero)" + full
  dump + "KERNEL PANIC" + stack trace + halt, on serial.

## Gotchas learned (do not re-learn these)
1. Limine request structs must be in program order: use ordered
   subsections .limine_requests.0..8 in linker script (kmain.c).
2. Limine rejects ELFs whose PHDRs with different perms share a page:
   page-align every segment in kernel/linker.ld.
3. limine.conf (v12 syntax, "option: value"), serial: yes is BIOS-only.
4. Limine's serial console on BIOS boot is the fastest debug channel:
   `qemu-system-x86_64 -cdrom vesper.iso -serial file:build/serial.log`.
5. GDT descriptors: bytes 0-1 limit lo, 2-4 base[0:24], 5 access,
   6 flags<<4|limit hi, 7 base[24:32]. (Got this wrong twice.)
6. Keep selectors 0x28/0x30 (Limine's CS/DS) valid in our GDT; TSS at
   0x38. A far CS reload (lretq) kept #GP-ing under QEMU TCG — avoided
   entirely by mirroring descriptors.
7. ISR stubs: fixed 64-byte slots were error-prone via pointer
   arithmetic; now an explicit .rodata pointer table (isr_stub_table).
8. `make run` target is UNFINISHED: use the manual QEMU command from
   README (needs -L and a fresh copy of OVMF_VARS each run:
   cp .../OVMF_VARS_4M.fd build/OVMF_VARS.fd).

## Next: milestone 3 (PMM/VMM/heap)
- Consume memmap_request (already requested in kmain.c) + hhdm offset.
- Page-frame allocator (bitmap or buddy) over USABLE regions only.
- Higher-half VMM (reuse Limine's HHDM; own page tables for kernel
  heap region), kmalloc/kfree with self-tests run from kmain before
  the M2 self-test.
- Then M4 (PIT + PS/2 kbd, LAPIC EOI), M5 (kthreads + ctx switch),
   M6 (syscall/sysret + init), M7 (USB image + docs; `make img` needs
   a GPT/ESP — current raw-FAT img boot was attempted but Limine
   uefi-install refused: needs partition table first; use sgdisk or
   manual GPT + mkfs.vfat ESP).

## Verification commands
```
export PATH=/home/z/my-project/.tools/root/usr/bin:/home/z/my-project/.tools/root/usr/sbin:$PATH
export LD_LIBRARY_PATH=/home/z/my-project/.tools/root/usr/lib/x86_64-linux-gnu:/home/z/my-project/.tools/root/usr/lib
make iso
SHARE=/home/z/my-project/.tools/root/usr/share/qemu
cp /home/z/my-project/.tools/root/usr/share/OVMF/OVMF_VARS_4M.fd build/OVMF_VARS.fd
qemu-system-x86_64 -L $SHARE -machine q35 -m 512M \
  -drive if=pflash,format=raw,file=/home/z/my-project/.tools/root/usr/share/OVMF/OVMF_CODE_4M.fd,readonly=on \
  -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
  -cdrom vesper.iso -serial file:build/serial.log -display none -no-reboot
```
