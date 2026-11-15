/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_IRQ_H
#define VESPER_IRQ_H

#include <common.h>
#include <isr.h>

/* IRQ routing: vector 0x20..0xFF dispatch to installed handlers; the
   dispatcher sends LAPIC EOI after the handler runs. */

typedef void (*irq_handler_t)(struct int_frame *);

/* Install a handler for an interrupt vector (0x20..0xFF).
   Returns 0 on success, -1 on bad args. */
int irq_install(u8 vector, irq_handler_t fn);

/* LAPIC timer tick entry (installed for TIMER_VECTOR). */
void timer_irq(struct int_frame *frame);

/* Number of interrupts dispatched since boot (self-test evidence). */
u64 irq_count(void);

/* Called by the generated stubs for vectors 32..255. */
void irq_dispatch(struct int_frame *frame);

#endif /* VESPER_IRQ_H */
