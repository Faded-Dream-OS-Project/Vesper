/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <console.h>
#include <irq.h>
#include <kbd.h>
#include <sched.h>
#include <timer.h>

/* IRQ routing table. Vectors 0x20..0xFF map to handlers; the LAPIC
   EOI is sent by this layer after the handler returns (or by the
   timer path for the LAPIC-internal timer). */

static irq_handler_t handlers[256];
static u64 dispatched;
static u8 irq_depth;   /* interrupt nesting level (IF stays on inside
                          handlers, so nesting is possible) */

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
    /* EOI FIRST: the timer vector leaves service so the next tick can
       reach whichever task runs next. Preemption itself moved to the
       outermost-dispatch tail in irq_dispatch (nested-safe). */
    apic_eoi();
}

/* PS/2 keyboard entry point (vector 0x21). */
void kbd_irq_entry(struct int_frame *frame)
{
    (void)frame;
    kbd_irq();
}

u64 irq_count(void)
{
    return dispatched;
}

void irq_dispatch(struct int_frame *frame)
{
    u64 n = frame->int_no;
    dispatched++;
    irq_depth++;

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
    irq_depth--;

    /* Preempt ONLY at the outermost level: switching tasks inside a
       NESTED interrupt abandons inner frames across tasks and has
       corrupted iretq state (observed as #GP e=0x20 popping a garbage
       CS from the timer stub's iretq). At depth 1 the switch happens
       just before the stub returns, which is exactly the preemption
       point we want. */
    if (irq_depth == 0 && n == 0x30)
        sched_tick();
}
