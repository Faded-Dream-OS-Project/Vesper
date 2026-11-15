/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_IO_H
#define VESPER_IO_H

#include <common.h>

/* x86 port I/O: outb/inb/outw/inw live in common.h; this header adds
   the 32-bit forms and a port-drain wait. */

static inline void outl(u16 port, u32 val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u32 inl(u16 port)
{
    u32 v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}

/* Short port delay: an outb to port 0x80 (POST) drains prior port
   writes on slow devices (PIC, PIT). */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif /* VESPER_IO_H */
