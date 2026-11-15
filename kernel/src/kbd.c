/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <io.h>
#include <kbd.h>

/* PS/2 keyboard driver, scancode set 1.

   The controller (port 0x64) is initialised defensively: flush the
   output buffer, enable the first port, and only then unmask GSI 1 in
   the IOAPIC. Decoding keeps per-key shift state and a sticky caps
   lock; characters land in a ring buffer. */

#define KBD_DATA 0x60
#define KBD_STAT 0x64
#define KBD_CMD  0x64

#define RING_SIZE 128

static volatile int ring[RING_SIZE];
static volatile u32 ring_head;   /* writer index */
static volatile u32 ring_tail;   /* reader index */

static int shift_down;
static int caps_lock;
static volatile u32 events;

/* Scancode set 1: index = make scancode, value = ASCII (0 = none).
   Unshifted table; shifted variants handled below. */
static const u8 map_lo[128] = {
    0,   0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    '-', '=', 0x08, '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o', 'p', '[', ']', '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j',
    'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' ', 0,
};

static const u8 map_hi[128] = {
    0,   0x1B, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',
    '_', '+', 0x08, '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O', 'P', '{', '}', '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J',
    'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0, '*', 0, ' ', 0,
};

static void ring_push(int c)
{
    u32 next = (ring_head + 1) % RING_SIZE;
    if (next == ring_tail)
        return;   /* full: drop */
    ring[ring_head] = c;
    ring_head = next;
}

static void handle_scancode(u8 sc)
{
    events++;

    if (sc & 0x80) {
        /* Break code. */
        u8 make = sc & 0x7F;
        if (make == 0x2A || make == 0x36)
            shift_down = 0;
        return;
    }

    switch (sc) {
    case 0x2A: case 0x36:
        shift_down = 1;
        return;
    case 0x3A:
        caps_lock = !caps_lock;
        return;
    default:
        break;
    }

    u8 c = shift_down ? map_hi[sc] : map_lo[sc];
    if (!c)
        return;

    /* Caps lock flips letters (shift and caps xor). */
    if (caps_lock && c >= 'a' && c <= 'z')
        c = (u8)(c - 'a' + 'A');
    else if (caps_lock && c >= 'A' && c <= 'Z')
        c = (u8)(c - 'A' + 'a');

    ring_push((int)c);
}

void kbd_irq(void)
{
    /* Drain the controller output buffer (usually one byte). */
    while (inb(KBD_STAT) & 0x01) {
        u8 sc = inb(KBD_DATA);
        handle_scancode(sc);
    }
}

int kbd_getchar(void)
{
    if (ring_tail == ring_head)
        return -1;
    int c = ring[ring_tail];
    ring_tail = (ring_tail + 1) % RING_SIZE;
    return c;
}

u32 kbd_event_count(void)
{
    return events;
}

void kbd_init(void)
{
    shift_down = 0;
    caps_lock = 0;
    ring_head = ring_tail = 0;
    events = 0;

    /* Flush anything the controller already holds. */
    while (inb(KBD_STAT) & 0x01)
        (void)inb(KBD_DATA);

    /* Enable the first PS/2 port (bit 0 of the controller config). */
    outb(KBD_CMD, 0xAE);
    io_wait();
    outb(KBD_CMD, 0x20);              /* read config byte */
    u8 cfg = inb(KBD_DATA);
    cfg |= 0x01;                      /* port 1 interrupt enabled */
    cfg &= (u8)~0x10;                 /* must be 0 anyway */
    outb(KBD_CMD, 0x60);              /* write config byte */
    outb(KBD_DATA, cfg);

    /* Route ISA IRQ 1 to vector 0x21 through its MADT override (if
       any — real machines may map it to a different GSI). */
    ioapic_route_isa(1, KBD_IRQ_VECTOR, 0);
}
