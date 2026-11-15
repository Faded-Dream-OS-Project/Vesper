/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_TIMER_H
#define VESPER_TIMER_H

#include <common.h>

/* Timer: LAPIC periodic timer (vector 0x30) calibrated against the
   legacy PIT. Provides a global tick counter and timer_sleep_ms. */

#define TIMER_HZ        100
#define TIMER_VECTOR    0x30

void timer_init(void);       /* calibrates + starts the LAPIC timer */
void timer_tick(void);       /* called from the IRQ path on each tick */
u64  timer_ticks(void);      /* ticks since timer start */
u64  timer_uptime_ms(void);
void timer_sleep_ms(u64 ms); /* busy-sleep; needs interrupts ON */
u64  timer_lapic_freq(void);
int  timer_is_degraded(void);   /* 1 = fixed assumed bus clock */ /* calibrated LAPIC bus frequency (Hz) */

#endif /* VESPER_TIMER_H */
