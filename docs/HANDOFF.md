# Vesper — development handoff (2026-11-15, session 4)

State: milestones 1-6 complete and verified; all session-3 shortcuts
paid down (preemption root-caused, kbd test gated, RWX gone, guard
pages + overflow self-test). Branch `main` (sole branch). All commits
attributed to FemBoyGamerTechGuy
<242450780+FemBoyGamerTechGuy@users.noreply.github.com>.

## Identity rule
Every future commit MUST use:
  git config user.name  "FemBoyGamerTechGuy"
  git config user.email "242450780+FemBoyGamerTechGuy@users.noreply.github.com"
No Co-authored-by trailers.

## Environment (unchanged)
- Tools in /home/z/my-project/.tools/root (QEMU 10.0.13, OVMF 4M,
  mtools, xorriso); QEMU needs `-L .../usr/share/qemu`.
- `make` / `make iso` / `make img` / `make run RUN_SECS=N` /
  `make clean`; `make iso CONF=<file>` for a variant limine.conf.
- Auth: credential helper reads /home/z/my-project/.git-token
  (gitignored; never print or commit it).

## Verified this session (commands + observed output)
- Regression: `make clean && make && make iso && make run RUN_SECS=60`
  — all suites green on BIOS and UEFI.
- Preemption: busy threads (no yields) -> 'counters: a=2500000
  b=2500000 c=2500000 (ticks=451)' BIOS, ticks=450 UEFI, sched 5/5.
- Guard: `bash scripts/guard_test.sh` -> "EXCEPTION #14 ... CR2
  0xffff888000125e70 (task 'ovf', RSP ... below its guard page...)",
  readable panic, halt.
- M6: normal boot serial shows init's full ring-3 session (hello,
  getpid()=4, uptime_ms()=670, bad-ptr errno 1, yield loop counter=5,
  exit(0)) then fault_init killed by #GP (hlt in ring 3), kernel keeps
  ticking, selftest 3/3 ALL PASS — BIOS and UEFI.
- `bash scripts/kbd_test.sh` still 5/5 with the flag; normal boot
  prints 'kbd: skipped'.

## Gotchas learned (do not re-learn these)
1-14. See git history of this file for the session-2/3 list (Limine
request ordering, page-aligned PHDRs, ACPI pointer semantics, PIT
read-back, LAPIC EOI/re-arm, the two-arg context switch).
15. Preemption livelock root cause (session 3 gotcha 12, now FIXED):
    (a) interrupt entry clears IF and a task switched-in via `ret`
    never got IF back — it ran unpreemptible forever; (b) the EOI ran
    at the END of irq_dispatch on the interrupted task's own return
    path, so the LAPIC held the timer in-service and blocked further
    ticks. Fix: `sti` between RSP store/load in sched_switch_to; timer
    EOI before preempting; preempt at the OUTERMOST interrupt level
    only (irq_depth gate).
16. SFMASK must clear IF (0x310: IF|TF|DF) or a syscall can be
    interrupted mid-frame on its kernel stack and iretq later pops a
    garbage CS (#GP e=0x20 at the timer stub).
17. STAR layout: bits 63:48 = CS for SYSRET, bits 47:32 = CS for
    SYSCALL (SS = CS+8 each, hardware ORs RPL). Swapping the fields
    makes every `syscall` #GP with error code 0x20 (the selector
    value) — deceptively like a stack corruption story.
18. SYSCALL does NOT switch stacks (unlike interrupts): the entry stub
    must load the kernel RSP itself (kstack_cur, kept equal to
    TSS.rsp0). TSS.rsp0 doubles as the interrupt stack for ring-3
    preemption.
19. User PML4: intermediate table entries created during the page walk
    MUST carry the USER bit or ring 3 cannot traverse (faults at the
    very first user instruction). Copy the kernel half ATOMICALLY (cli)
    — a preempted copy once left a half-populated PML4.
20. PMM frames are SCATTERED: mapping "code_phys + i*4096" splices
    unrelated physical memory into user space. Map per frame. Also do
    not map an in-image blob by its own pages (not page-aligned) —
    copy to fresh frames.
21. User tasks must become schedulable only AFTER their address space
    is fully built (a premature switch loaded CR3=0 -> fetch fault at
    vmm_load_pml4's ret -> #DF).
22. SysV alignment: _start must see RSP % 16 == 8 (as if called);
    sysret with a 16-aligned RSP misaligns callees and execution can
    jump mid-instruction (#GP at a bogus RIP).
23. Stack exhaustion needs an IST for #PF (IST_PF) or the guard-page
    fault escalates to #DF while pushing the exception frame.
24. Guard pages under kmalloc'd stacks must be page-aligned INSIDE the
    allocation (heap blocks are only 8-byte aligned); unmapping the
    raw block start kills the previous heap block's tail.
25. The generated init_blob.S cannot contain bare C-style comments
    with apostrophes (assembler errors); use '#' lines. gcc turns
    obvious self-recursion into a tail-call jmp (229 "restarts", zero
    stack growth, guard never hit) — use a volatile fn pointer and
    keep a statement after the call.

## M7 plan (next milestone: bootable USB image + docs)
1. `make img` already produces a GPT disk (BIOS-boot partition +
   FAT32 ESP with BOOTX64.EFI, limine.conf, kernel) that boots OVMF
   (AHCI) and SeaBIOS — re-verify with the CURRENT kernel (user mode
   changes the boot requirements none, but the RWX segment split
   changed the image layout).
2. Add the init blobs to the image build (kernel image already embeds
   them — verify img boots and reaches M6 self-tests).
3. Write flashing/boot docs (dd command, safe target selection, what
   to expect on real hardware; USB keyboard NOT supported until USB —
   serial console is the only interactive channel on metal).
4. Optional hardening if time: per-task kernel stacks for interrupts
   (currently TSS.rsp0 == the runner's syscall stack), LAPIC timer
   one-shot deadline mode, and a proper idle task (hlt loop with
   preemption) instead of the kmain spin loop.

## Verification commands
```
export PATH=/home/z/my-project/.tools/root/usr/bin:/home/z/my-project/.tools/root/usr/sbin:$PATH
export LD_LIBRARY_PATH=/home/z/my-project/.tools/root/usr/lib/x86_64-linux-gnu:/home/z/my-project/.tools/root/usr/lib
make clean && make && make iso
make run RUN_SECS=60                                    # UEFI/OVMF
SHARE=/home/z/my-project/.tools/root/usr/share/qemu
qemu-system-x86_64 -L $SHARE -machine q35 -m 512M \
  -cdrom vesper.iso -serial file:build/serial.log -display none -no-reboot   # BIOS
bash scripts/kbd_test.sh                                # keyboard self-test
bash scripts/guard_test.sh                              # guard page self-test
```
