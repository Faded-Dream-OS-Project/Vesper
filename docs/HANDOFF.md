# Vesper — development handoff (2026-11-15, session 7)

State: all five session-6 commits pushed (remote==local, confirmed
with git ls-remote). Session 7 hunted the real-hardware regression
(session-6 build = black screen after Limine on the human's PC, while
8789845 booted fine). Everything is committed and pushed to `main`
as FemBoyGamerTechGuy.

Session 7 delivered (all committed, pushed):

- build-ID: every kernel prints 'build: git <hash>, <cc>, <stamp>' as
  its first line (scripts/gen_build_id.sh; header gitignored).
- breadcrumbs: 15 init stages draw coloured 16x16 squares at the
  top-left before the console exists + stage codes to port 0xE9; a
  black-screen photo shows the last stage reached. Verified: debugcon
  stream is exactly 1..15.
- FOUR real-firmware boot hazards fixed (commit 18d3370):
  1. PAT/WC remap walked Limine's 1 GiB big-page HHDM entries as if
     4 KiB tables, WRITING garbage pre-IDT (top suspect; latent in
     QEMU). Now: dedicated 16 MiB WC window built from OUR tables
     after vmm_init; big-page leaves refused by vmm_set_leaf_flags;
     PAT MSR write gated on CPUID.
  2. .bss inlined into .data PROGBITS -> kernel ELF 4.5 MiB of file
     data (session 5 was ~700 KiB). Now NOBITS; ELF 247720 bytes.
  3. Panels >1280x800 fell back to serial-only (4 MiB shadow): on a
     desktop with no serial that IS a black screen with a healthy
     kernel. Now 16 MiB shadow; GOP matrix 6/6 renders incl.
     2560x1600; kernel image mapping extended to 32 MiB.
  4. cpu_sanitize wrote CR0/CR4 pre-IDT. Now read+record only;
     SMAP handled via STAC/CLAC in sys_write (CPUID-gated).
- portable build (commit c75b1a3): toolchain discovery + forced
  freestanding flags verified by disassembly (check_cc.sh — catches
  endbr64/xmm); doctor.sh with the exact Debian/Ubuntu apt line; all
  scripts de-hardcoded (PATH + distro OVMF locations + env
  overrides), LC_ALL=C; verify_image.sh builds the image itself.
  Clean-clone scrubbed-env flow verified: ALL CHECKS PASS.
- bisect pack (commit 230aaea + download/bisect/): worktree builds of
  8789845/69b84e3/9c09219/eaeeec2 with instrumentation backported
  UNCOMMITTED (bisect-valid); all 4 QEMU-tested BIOS+UEFI with their
  own build-ID lines.

Next hardware run order (docs/REAL_HARDWARE.md has the details):
1. NEW vesper.img (session 7) — expect green banner; photo the
   'build:' line and the boot: timing line.
2. If black: count breadcrumb squares, photo; if no squares at all,
   try the bisect pack in order — first failing image brackets the
   regression; each photo shows its own commit hash.

Untested on hardware (hypotheses only): all four fixes above;
the bisect pack behaviour on metal; ring-3 still-unresolved failure
from session 6 (syscall_trace is the instrument).

---

# Vesper — development handoff (2026-11-15, session 6)

State: first real-hardware run completed (Ventoy/UEFI on the human's
main PC): M1-M5 self-tests PASS on metal; ring-3 init faulted
(#PF/CR2=0, RIP inside rodata — not reproducible in QEMU; defensive
fixes + instrumentation landed this session); console was unusably
slow -> shadow-buffer console rewrite. Branch `main` (sole branch).
All commits attributed to FemBoyGamerTechGuy
<242450780+FemBoyGamerTechGuy@users.noreply.github.com>.

Session 6 delivered: honest user-mode tests + full user-fault dump,
poison flag, zeroed user stacks/pages, CR0/CR4/EFER hygiene +
diagnostics, -mgeneral-regs-only user builds, syscall_trace,
shadow-buffer console with WC video mapping + boot timing, boot menu
(normal / diagnostics hold / quiet), ASCII-only printed strings with
fallback glyph.

NOTE: pushes are BLOCKED this session — the sandbox recycle wiped
/home/z/my-project/.git-token (and .tools, which was restored via
scripts/setup_tools.sh). Commits 77d879d..HEAD are local; push them
as soon as the token file is back (credential helper reads it).

Session 5 delivered: full on-screen framebuffer console with colours,
the boot diagnostics screen, hardware-robustness fixes (IOAPIC MADT
overrides + multi-controller, timer calibration ladder with degraded
mode, i8042 probe, hlt idle, multi-CPU tolerance), the verified USB
image + scripts/verify_image.sh, and the real-hardware flash guide.

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

## Next plan (session 6: first real-hardware fixes)
The human flashes vesper.img per docs/REAL_HARDWARE.md and reports
back (photo + machine model + CPU + firmware version). Likely fix
areas, in expected order of likelihood:
1. Framebuffer quirks: unusual bpp/pitch/masks (15bpp RGB555,
   BGR-order masks) or text rendered in a bad scale — the console
   already handles all documented formats, so a photo will show what
   the firmware actually set.
2. ACPI layout oddities: the walk skips bad-checksum/absurd-length
   entries non-fatally, but a machine with an RSDT-only (ACPI 1.0)
   or zero-length root table could still print 'no valid tables' —
   then the LAPIC defaults matter.
3. Timer: if all calibrations fail on metal, the degraded-mode
   warning appears; a photo plus the CPU (leaf 0x15/0x16 presence)
   will tell which rung to fix.
4. IOAPIC overrides with level/edge or polarity flags (the flags are
   parsed and printed but the redirection entries are still programmed
   edge/active-high for ISA-style inputs — honour flags if a device
   misbehaves).
5. Interrupt-stack hardening (deferred from session 4): interrupts
   during ring 3 reuse the runner's syscall stack (TSS.rsp0 ==
   kstack_cur); a per-task interrupt stack would remove the last
   known structural risk.
6. USB keyboard support (xHCI) so real machines without PS/2 can
   interact — currently informational only.

## Session 5 gotchas (26-32)
26. mdir splits FAT name/extension into separate columns — verify ESP
    contents with `mdir -b` (bare path format), not by grepping the
    default listing.
27. GPT header CRC is computed over header_size bytes with ONLY the
    CRC field (offset 16) zeroed — the entries CRC at offset 88
    participates in the header CRC; zeroing both yields a mismatch.
28. HPET base address in the HPET table lives inside a Generic
    Address Structure at table offset +44 (after the 4-byte GAS
    prefix at +40); reading a u64 at +40 reads the GAS space-id and
    garbage — a #GP in the HPET fallback.
29. The HPET main counter does not advance unless ENABLE_CNF (GC
    bit 0) is set — firmware may leave it off; set it before using
    the counter as a calibration reference.
30. MADT interrupt-source-override fields sit at entry offsets
    2/3/4/8 (bus/source/gsi/flags) AFTER the 2-byte entry header —
    offsets 4/5/8/12 decode garbage GSI numbers.
31. Limine's framebuffer/memmap request structs live in
    .limine_requests.N sections; exporting them for another file
    requires making them non-static AND the symbol names must match
    exactly (fb_request vs framebuffer_request cost a build loop).
32. QEMU always emulates the i8042 ports even with -nodefaults (they
    are chipset functionality, not a device) — the PS/2-absent path
    can only be exercised with the kbd_test_absent cmdline flag.

## Session 6 gotchas (33-39)
33. The sandbox recycle wipes /home/z/my-project/.tools AND
    /home/z/my-project/.git-token; rerun scripts/setup_tools.sh AND
    re-symlink SeaBIOS: bios-256k.bin etc. live in share/seabios/,
    not share/qemu/ (QEMU -L looks only in share/qemu). Pushes fail
    until the token file is restored.
34. git core.fileMode must stay false — the recycle flips exec bits
    on every tracked file and floods 'mode change' diffs.
35. The hardware ring-3 failure (#PF CR2=0, RIP inside .rodata, both
    programs, init also faulting where fault_init's #GP was expected)
    did NOT reproduce in QEMU under: poison, -cpu
    Skylake/Icelake/EPYC, la57=on, smp4, UEFI+poison. Unresolved;
    syscall_trace + the honest tests will catch it on the next
    hardware boot. Zeroed user stacks, SMEP/SMAP clearing and
    -mgeneral-regs-only are landed as fixes regardless.
36. The console must NOT allocate its shadow from the heap: fb_init
    runs before kheap_init (kmalloc NULL -> silent serial-only
    fallback). Static .bss buffers instead.
37. PAT WC recipe: PAT entry 1 (MSR 0x277 bits 15:8) = 0x01, then
    PWT (0x8) on the leaf PTE selects index 1; entry 0 stays WB.
    vmm_set_leaf_flags does the invlpg.
38. QEMU TCG numbers: normal boot ~2.1 s (163 ms flushing), quiet
    ~0.35 s — the difference is the shadow->video cost, NOT glyph
    drawing (which is RAM now). On hardware compare via the boot:
    line's flush-ms figure.
39. Limine shows its menu when limine.conf has >1 entry; timeout: 0
    picks the first as default — entry order matters for the 'normal'
    default.

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
