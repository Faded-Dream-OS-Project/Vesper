/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <console.h>
#include <irq.h>
#include <timer.h>

/* IRQ routing table. Vectors 0x20..0xFF map to handlers; the LAPIC
   EOI is sent by this layer after the handler returns (or by the
   timer path for the LAPIC-internal timer). */

static irq_handler_t handlers[256];
static u64 dispatched;

int irq_install(u8 vector, irq_handler_t fn)
{
    if (vector < 0x20 || !fn)
        return -1;
    handlers[vector] = fn;
    return 0;
}

/* LAPIC timer tick entry point (vector 0x30). */
void timer_irq(struct int_frame *frame)
{
    (void)frame;
    timer_tick();
}

u64 irq_count(void)
{
    return dispatched;
}

void irq_dispatch(struct int_frame *frame)
{
    u64 n = frame->int_no;
    dispatched++;

    if (n < 256 && handlers[n]) {
        handlers[n](frame);
    } else if (n >= 0x20 && n <= 0x2F) {
        /* Legacy PIC range (we keep it fully masked; reaching here
           means a spurious PIC interrupt): ignore. */
    } else if (n == 0xFF) {
        /* LAPIC spurious vector: must NOT send EOI. */
        return;
    } else {
        kprintf("irq: unexpected vector %lu\n", n);
    }

    apic_eoi();
}
