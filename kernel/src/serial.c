/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <stdbool.h>

/* COM1 UART (16550). Safe fallbacks: if no UART is present the port
   reads return junk; the line-status check keeps that from wedging us. */

#define COM1 0x3F8

static bool serial_ok;
bool serial_present(void);

void serial_init(void)
{
    outb(COM1 + 1, 0x00);    /* disable interrupts */
    outb(COM1 + 3, 0x80);    /* DLAB on */
    outb(COM1 + 0, 0x01);    /* 115200 baud divisor lo (QEMU default-safe) */
    outb(COM1 + 1, 0x00);    /* hi */
    outb(COM1 + 3, 0x03);    /* 8N1, DLAB off */
    outb(COM1 + 2, 0xC7);    /* FIFO, clear, 14-byte threshold */
    outb(COM1 + 4, 0x0B);    /* DTR+RTS+OUT2 */

    /* Scratch register probe: what we write should come back. */
    outb(COM1 + 7, 0x5A);
    serial_ok = (inb(COM1 + 7) == 0x5A);
}

bool serial_present(void)
{
    return serial_ok;
}

void serial_putc(char c)
{
    if (!serial_ok)
        return;
    if (c == '\n') {
        /* prepend CR so dumb terminals get CRLF */
        for (int i = 0; i < 100000; i++)
            if (inb(COM1 + 5) & 0x20)
                break;
        outb(COM1, '\r');
    }
    for (int i = 0; i < 100000; i++) {
        if (inb(COM1 + 5) & 0x20)
            break;
    }
    outb(COM1, (u8)c);
}
