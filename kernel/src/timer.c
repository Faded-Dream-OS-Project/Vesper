/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <acpi.h>
#include <apic.h>
#include <console.h>
#include <cpu.h>
#include <io.h>
#include <timer.h>
#include <vmm.h>

/* LAPIC timer with a calibration ladder and a degraded mode.

   Preferred order (session 5, real hardware):
     1. PIT one-shot window (classic, works nearly everywhere) with a
        sanity check on the result
     2. HPET main counter as the reference window (fixed 14.3181808 MHz
        on every HPET)
     3. CPUID leaf 0x16 base TSC frequency (the LAPIC bus often runs
        at a simple fraction of it; we accept the approximation)
     4. degraded mode: fixed assumed bus frequency + amber warning —
        the kernel keeps running with possibly-drifting time rather
        than hanging at boot. */

#define PIT_CH0_DATA 0x40
#define PIT_MODE_CMD 0x43
#define PIT_HZ        1193182

static volatile u64 ticks;
static u64 lapic_bus_hz;
static u64 period_count;
static int timer_degraded;

int timer_is_degraded(void)
{
    return timer_degraded;
}

void timer_tick(void)
{
    ticks++;
    /* QEMU TCG LAPIC quirk: rewrite the initial count so the periodic
       reload continues (harmless on real hardware). */
    if (period_count)
        lapic_timer_rearm(period_count);
}

u64 timer_ticks(void)
{
    return ticks;
}

u64 timer_uptime_ms(void)
{
    return ticks * 1000 / TIMER_HZ;
}

u64 timer_lapic_freq(void)
{
    return lapic_bus_hz;
}

/* PIT one-shot on channel 0. We arm the counter and then poll the
   channel's OUT pin via the read-back status command (0xE2): bit 7 of
   the returned status is the OUT pin, which goes high when the one-
   shot has expired. Port 0x61 bit 6 does not behave as an OUT
   indicator on QEMU (learned the hard way). */
static void pit_one_shot(u16 count)
{
    outb(PIT_MODE_CMD, 0x30);   /* ch0, lo/hi byte, mode 0 (one-shot) */
    outb(PIT_CH0_DATA, count & 0xFF);
    outb(PIT_CH0_DATA, (count >> 8) & 0xFF);
}

static int pit_out_pin(void)
{
    /* Read-back: LATCH status for counter 0. */
    outb(PIT_MODE_CMD, 0xE2);   /* 11 100 0 10: COUNT=0, STATUS=1, ch0 */
    u8 status = inb(PIT_CH0_DATA);
    return (status & 0x80) != 0;
}

/* Compute the LAPIC frequency by measuring elapsed LAPIC ticks during
   a fixed PIT one-shot window. Returns Hz. */
static u64 calibrate(void)
{
    const u64 lapic_initial = 0xFFFFFFFF;

    /* LAPIC timer one-shot, masked, divider 16. */
    lapic_set_timer(0, 1, lapic_initial);   /* vector 0, masked */

    /* 10 ms window: 1193182 / 100 = 11932 PIT ticks. */
    pit_one_shot(11932);

    /* Wait for the one-shot OUT pin to go high. */
    u64 spins = 0;
    while (!pit_out_pin()) {
        if (++spins > 400000000ull)
            return 0;   /* PIT not behaving; bail out */
    }

    u32 ccr = (u32)lapic_read_ccr();
    u64 elapsed = lapic_initial - ccr;   /* ticks in 10 ms (x16 div) */
    u64 hz16 = elapsed * 100;            /* per second, still /16 */
    return hz16 * 16;
}

/* Sanity range for the LAPIC bus: anything outside 1 MHz .. 10 GHz is
   a broken measurement. */
#define LAPIC_HZ_MIN 1000000ull
#define LAPIC_HZ_MAX 10000000000ull

/* --- HPET fallback: count LAPIC ticks over an HPET main-counter
   window. The HPET main counter always increments at 14.3181808 MHz
   (fixed by the specification). --- */
#define HPET_HZ   14318181ull   /* rounded; ~0.05 ppm error */
/* HPET register offsets (within the MMIO block). */
#define HPET_GCAP 0x00u   /* capabilities (read-only) */
#define HPET_GCFG 0x10u   /* general config: bit 0 = ENABLE_CNF */
#define HPET_MC   0xF0u   /* main counter (64-bit) */

static volatile u64 *hpet_reg(u64 base, u32 off)
{
    return (volatile u64 *)vmm_phys_to_hhdm(base + off);
}

static u64 hpet_read_mc(u64 base)
{
    return *hpet_reg(base, HPET_MC);
}

/* Make sure the main counter is running (firmware may have disabled
   it; the counter only advances when ENABLE_CNF is set). Legacy
   replacement bits are left as firmware set them. */
static void hpet_ensure_enabled(u64 base)
{
    volatile u64 *gcfg = hpet_reg(base, HPET_GCFG);
    if (!(*gcfg & 1))
        *gcfg = *gcfg | 1;
}

/* Calibrate against the HPET main counter: 10 ms window. */
static u64 calibrate_hpet(u64 hpet_base)
{
    const u64 lapic_initial = 0xFFFFFFFF;
    u64 window = HPET_HZ / 100;   /* 10 ms in HPET ticks */

    lapic_set_timer(0, 1, lapic_initial);
    hpet_ensure_enabled(hpet_base);
    u64 start = hpet_read_mc(hpet_base);
    u64 now = start;
    /* wait for the window with a tightly bounded spin (an HPET that
       never counts must not stall boot) */
    u64 spins = 0;
    while (now - start < window) {
        now = hpet_read_mc(hpet_base);
        if (++spins > 400000000ull)
            return 0;   /* HPET not counting: bail out */
    }
    u32 ccr = (u32)lapic_read_ccr();
    u64 elapsed = lapic_initial - ccr;
    return elapsed * 100 * 16;   /* per second, x16 divider */
}

/* CPUID 0x15/0x16 based estimate. The LAPIC bus on modern Intel is
   derived from the base clock; we use the reported base MHz as the
   bus estimate (an approximation, flagged as such). */
static u64 calibrate_cpuid(void)
{
    struct cpu_info c;
    cpu_identify(&c);
    if (c.tsc_ratio_denom && c.tsc_ratio_numer) {
        /* leaf 0x15: TSC = ratio * core crystal; the LAPIC timer on
           such parts typically runs at the crystal or base clock. Use
           the leaf 0x16 base if present, else the crystal. */
        if (c.tsc_hz)
            return c.tsc_hz;
    }
    if (c.tsc_hz)
        return c.tsc_hz;
    return 0;
}

void timer_init(void)
{
    lapic_bus_hz = 0;
    timer_degraded = 0;

    /* 1) PIT, sanity-checked (skipped when the cmdline flag
       timer_force_fallbacks is set — used to exercise the ladder in
       QEMU without breaking real boots) */
    {
        if (cmdline_has("timer_force_fallbacks"))
            lapic_bus_hz = 0;
        else
            lapic_bus_hz = calibrate();
    }
    if (lapic_bus_hz < LAPIC_HZ_MIN || lapic_bus_hz > LAPIC_HZ_MAX) {
        kprintf("$Wtimer: PIT calibration out of range (%lu Hz)$T\n",
                (unsigned long)lapic_bus_hz);
        lapic_bus_hz = 0;
    }

    /* 2) HPET */
    if (!lapic_bus_hz) {
        const struct acpi_info *ai = acpi_get_info();
        if (cmdline_has("timer_test_degraded"))
            ai = 0;   /* simulate no HPET: exercise the last rungs */
        if (ai && ai->has_hpet && ai->hpet_base) {
            lapic_bus_hz = calibrate_hpet(ai->hpet_base);
            if (lapic_bus_hz >= LAPIC_HZ_MIN && lapic_bus_hz <= LAPIC_HZ_MAX)
                kprintf("timer: calibrated against HPET\n");
            else
                lapic_bus_hz = 0;
        }
    }

    /* 3) CPUID TSC frequency */
    if (!lapic_bus_hz) {
        lapic_bus_hz = calibrate_cpuid();
        if (lapic_bus_hz >= LAPIC_HZ_MIN && lapic_bus_hz <= LAPIC_HZ_MAX)
            kprintf("timer: using CPUID-reported base frequency\n");
        else
            lapic_bus_hz = 0;
    }

    /* 4) degraded mode: keep going with an assumed bus clock */
    if (!lapic_bus_hz) {
        timer_degraded = 1;
        lapic_bus_hz = 100000000ull;   /* assume 100 MHz LAPIC bus */
        kprintf("$Wtimer: all calibrations failed — degraded mode "
                "(assumed 100 MHz bus); timekeeping will drift$T\n");
    }

    /* Periodic at TIMER_HZ: initial count = lapic frequency (with the
       timer divider set to 16 inside calibrate) / TIMER_HZ. */
    u64 cnt16 = lapic_bus_hz / 16 / TIMER_HZ;   /* ticks per period */
    period_count = cnt16;
    lapic_set_timer_periodic(TIMER_VECTOR, 0, cnt16);
}

void timer_sleep_ms(u64 ms)
{
    u64 target = timer_ticks() + ms * TIMER_HZ / 1000;
    while (timer_ticks() < target)
        halt();   /* wait for interrupts; each tick wakes us */
}
