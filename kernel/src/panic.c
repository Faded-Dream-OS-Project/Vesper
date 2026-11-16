/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <lib.h>
#include <selftest.h>

/* panic: render the banner and register dump ON SCREEN in a distinct
   colour (plus serial when present), unwind the stack, then halt with
   cli;hlt forever. The screen keeps the panic visible for
   photographing (docs/REAL_HARDWARE.md). */

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

    kprintf("\n$R*** KERNEL PANIC ***$T\n");
    if (open_suite_idx() >= 0)
        kprintf("$Rpanic inside suite '%s' -> that suite FAIL$T\n",
                open_suite_name());
    test_panic_close_open();   /* suite running when the panic hit = FAIL */
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
    kprintf("\n");

    kprintf("RIP=%#lx\n", rip);
    unwind_stack();
    kprintf("$Rsystem halted.$T\n");
    fb_flush();   /* never lose the panic to a pending dirty batch */
    cli();
    for (;;)
        halt();
}
