/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <lib.h>

/* panic: print the message, dump the caller's RIP, halt forever. */

struct link_reg {
    struct link_reg *rbp;
    u64 rip;
};

static void unwind_stack(void)
{
    struct link_reg *frame;

    __asm__ volatile ("mov %%rbp, %0" : "=r"(frame));
    kprintf("stack trace:\n");
    for (int i = 0; i < 16 && frame; i++) {
        kprintf("  [%d] %#lx\n", i, frame->rip);
        frame = frame->rbp;
    }
}

void panic(const char *fmt, ...)
{
    va_list args;
    u64 rip;

    __asm__ volatile ("mov 8(%%rbp), %0" : "=r"(rip));

    kprintf("\nKERNEL PANIC: ");
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
    kprintf("\n");

    kprintf("RIP=%#lx\n", rip);
    unwind_stack();
    kprintf("system halted.\n");

    cli();
    for (;;)
        halt();
}
