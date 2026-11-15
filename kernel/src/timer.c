/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <io.h>
#include <timer.h>

/* LAPIC timer calibrated against the PIT.

   Calibration: program the PIT channel 0 for a known period
   (10 ms at 1193182 Hz -> 11932 ticks), set the LAPIC timer to
   one-shot with a huge initial count, wait for the PIT gate to
   expire, then read how many LAPIC ticks elapsed. freq = laps *
   (initial - current) / 0.01. */

#define PIT_CH0_DATA 0x40
#define PIT_MODE_CMD 0x43
#define PIT_HZ        1193182

static volatile u64 ticks;
static u64 lapic_bus_hz;
static u64 period_count;

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

void timer_init(void)
{
    lapic_bus_hz = calibrate();

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
