/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <console.h>

/* console.c: fan-out of kernel output to every available channel.
   Serial and framebuffer are independent fallbacks: if either is
   missing, the other still receives the full output. */

void console_init(void)
{
    serial_init();
}

void kputchar(char c)
{
    serial_putc(c);
    fb_putc(c);
}
