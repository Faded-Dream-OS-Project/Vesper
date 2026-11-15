# Vesper on real hardware (milestone 7)

This guide is for the human who flashes Vesper onto a USB stick and
boots it on a real PC. Everything the kernel needs to tell you is on
screen — no serial port, no keyboard required.

The kernel image is **untested on real hardware** so far. Everything
below was verified in QEMU (BIOS + UEFI, USB mass-storage emulation,
no serial, no PS/2 device). If the real machine misbehaves, the
"What to report back" section tells you exactly what to capture.

## What you need

- A **spare USB stick**, 64 MB or larger. **Everything on it will be
  erased.** Use a stick you are willing to lose.
- A PC with UEFI boot (2012 or newer). Secure Boot **must be off**
  (Vesper is unsigned). CSM/legacy mode off is preferred but the BIOS
  path also works.
- Built artifact: `vesper.img` (64 MB GPT image) from `make img`.
  Verify it first: `bash scripts/verify_image.sh` must print
  `RESULT: ALL CHECKS PASS`.

## Warnings — read before flashing

- **Flashing erases the target completely.** No recovery from the
  stick itself; re-flash to reuse it.
- **Triple-check the target device.** Flashing the wrong target
  destroys the internal disk — usually the whole operating system.
  On Windows, check Disk Management for the disk NUMBER; on Linux,
  `lsblk` for the device; on macOS, `diskutil list`.
- **Never flash while the target is mounted.** Unmount/eject it first.
- Prefer a USB stick you can dedicate to this; label it.

## Flashing

### Windows (Rufus in DD mode, or balenaEtcher)

Rufus:

1. Insert the stick. Note its size; confirm in Disk Management which
   disk number it is.
2. Open Rufus. Device: select your stick (verify the size).
3. Boot selection: SELECT → pick `vesper.img`.
4. Partition scheme: leave whatever the image dictates (DD mode will
   be offered).
5. Click START. Rufus detects an ISOHybrid/DD image and asks
   *ISO mode vs DD mode* — **choose DD mode**.
6. Wait for "READY". Done.

balenaEtcher:

1. Flash from file → `vesper.img`.
2. Select target → your stick (verify the size!).
3. Flash! → wait for validation to finish.

### Linux (dd)

1. Insert the stick. Run `lsblk` and identify it by SIZE and the
   `usb`/`usb_disk` transport column — e.g. `/dev/sdb` (the whole
   disk, **no partition number**). If in doubt, unplug, run `lsblk`,
   plug back in, run `lsblk` again: the new entry is your stick.
2. Unmount any mounted partitions:
   `sudo umount /dev/sdX?*` (replace X).
3. Flash and verify:
   ```
   sudo dd if=vesper.img of=/dev/sdX bs=4M conv=fsync status=progress
   sudo sync
   ```
4. Verification (recommended): re-read the start and compare:
   ```
   sudo head -c 1048576 /dev/sdX | sha256sum
   head -c 1048576 vesper.img | sha256sum
   ```
   The two hashes must match (first MiB; the GPT + ESP lives there).

### macOS (dd)

1. Insert the stick. `diskutil list external` — identify it by SIZE
   (e.g. `/dev/disk4`, the whole disk). **Confirm it is `external`.**
2. Unmount it (do not eject): `diskutil unmountDisk /dev/diskN`.
3. Flash (notice the `/dev/rdiskN` raw device, much faster):
   ```
   sudo dd if=vesper.img of=/dev/rdiskN bs=4m
   sync
   ```
4. Eject: `diskutil eject /dev/diskN`.

## Booting the machine

### Ventoy (tested, works)

The first hardware run booted the image through **Ventoy**: copy
`vesper.img` into the Ventoy USB stick's data partition, power on,
open the Ventoy menu, and pick the image. Ventoy maps it as a virtual
USB drive and the Limine menu below appears. This path is CONFIRMED
working on real hardware (diagnostics, self-tests and panics all
rendered); direct flash of `vesper.img` (below) also works.

### The Vesper boot menu

After the bootloader loads you get three entries (the first is the
default; the menu appears when Limine has multiple entries):

| Entry | What it is for |
| --- | --- |
| `Vesper (normal)` | Full boot: diagnostics, all self-tests, ring-3 init, ends on a green `=== Vesper boot complete ===` banner with a timing line, then idles. Use this by default. |
| `Vesper (diagnostics hold)` | Stops right after the diagnostics block (`holding: diagnostics on screen, hlt.`) so the machine inventory can be photographed before anything else scrolls it away. |
| `Vesper (quiet)` | No framebuffer rendering at all (serial only) - measures what the screen costs. The boot-time line still prints `console flushes took 0 ms`. |

Hidden flags for debugging (add to the cmdline in limine.conf):
`poison` (fill all free RAM with 0xA5 - uninitialized-memory
detector), `syscall_trace` (one line per syscall - catches ring-3
derailing), `panic_test` (end in the deliberate divide-by-zero panic
to test the panic path), `guard_test` (stack-overflow panic test),
`kbd_test_absent` / `timer_test_degraded` / `timer_force_fallbacks`
(component-absence simulations), `kbd_test` (automated keyboard
self-test).

1. Insert the stick, power on.
2. Open the firmware boot menu — usually a key at power-on: **F12,
   F11, F9, F8, Esc, Enter** (varies by vendor; some machines show
   "Press F12 for boot menu" on their logo screen). Choose the entry
   that mentions **"USB"** and **"UEFI"** (e.g. "UEFI: SanDisk ...").
   If you only see "USB ..." without UEFI, the firmware is in legacy
   mode — it should still boot via the BIOS path, but UEFI is better
   supported.
3. If no USB entry appears: reboot into firmware setup (F2/Del),
   disable **Secure Boot**, try again; also try a different USB port
   (rear ports on desktops are often more reliable).

### What a successful boot looks like

Within a few seconds you should see, on a black screen in light-grey
text (cyan headers, green results):

```
=== Vesper boot diagnostics ===
CPU: <your CPU brand string>
CPUID: vendor GenuineIntel/AuthenticAMD, max leaf ..., apic id 0
CPUID flags: APIC TSC MSR ...
Memory: N map entries, M MiB usable
Framebuffer: WxH, 32bpp, pitch ..., rgb 8/16:8/8:8/0
ACPI 2.0: OEM ..., XSDT at ... (N tables)
ACPI tables: MADT, HPET, FADT, MCFG
LAPIC base 0xfee00000, N CPU entries listed (parked: all but BSP)
IOAPIC[0] base 0xfec00000 (GSI 0..)
MADT override: ISA IRQ 0 -> GSI 2, flags 0x0
...
Boot mode: UEFI
selftest: pmm
  PASS ...
selftest: 6 passed, 0 failed -> ALL PASS
...
selftest: user
init: hello from ring 3
init: getpid() = 4
init: uptime_ms() = ...
init: write(bad ptr) -> errno 1
init: yield loop done, counter=5
init: exit(0)
user: fault_init finished (killed), tasks=1
selftest: 3 passed, 0 failed -> ALL PASS

=== Vesper boot complete ===
boot: NNN ms total, console flushes took M ms (scale Nx, fb WxH, fb ok, PAT-WC yes)
``

The screen **stays** on the final state (the green boot-complete
banner with the timing line) — it does not clear or reboot. That is
the end of the demo: the kernel runs its self-tests, prints the
results, and parks. Power off when done.

Timing to report: the `boot:` line's total ms and the flush ms (the
first hardware run took minutes with visible line-sweeping; the
shadow-buffer console should bring the flush cost to tens of ms).

Amber-colored lines (e.g. `kbd: not available`, timer degraded-mode
warnings) are informational: the kernel continues without that
device. A bright-red `*** KERNEL PANIC ***` block means the kernel
hit a bug; photograph it and report.

## Troubleshooting

| Symptom | Likely cause | Next step |
| --- | --- | --- |
| Black screen, no text at all | stick not booted; firmware fell back to internal disk; or framebuffer mode the console cannot render | Check boot menu again; verify `verify_image.sh` passes; try CSM on/off; try another port; report machine model |
| "no bootable device" | Secure Boot on; USB not selected; stick flashed in ISO mode (Rufus) | Disable Secure Boot; choose the UEFI USB entry in the boot menu; re-flash with DD mode |
| Stuck at a Limine menu or text mentioning `limine` | bootloader started but did not find the kernel | The ESP is present (verified) — likely a firmware quirk; photograph the exact screen and report |
| Garbled / shifted / off-center text | framebuffer format differs from anything seen in QEMU | Photograph the screen; include the `Framebuffer:` line if any text is readable at all |
| Reboots immediately in a loop | kernel triple-faulted before the console was up | Try CSM on/off; report machine model + firmware version |
| Kernel panic (red text) on screen | a real kernel bug | Photograph the whole screen (register dump + stack trace included) |
| Boots but timer warning (amber) | PIT/HPET/CPUID calibration all failed | Photograph; note whether time-based tests still pass |
| Boots but `kbd: not available` (amber) | no PS/2 controller (normal on modern laptops) | Expected on many machines — USB keyboard support is future work |

## What to report back

For any failed or weird boot, capture:

1. **A photo of the screen** — as much of it as possible, in focus.
   Include the top (diagnostics) and the bottom (last lines) — pan if
   needed and send two photos.
2. **Make and model of the machine** (e.g. "Lenovo ThinkPad T480").
3. **CPU** (from the diagnostics `CPU:` line if readable, or from
   your OS).
4. **Firmware version and vendor** (shown in firmware setup, e.g.
   "AMI 1.30 / Insyde 2.17").
5. Whether **Secure Boot / CSM** were on or off.
6. How the stick was flashed (tool + mode).

With that, the next session can fix hardware-specific behavior.
