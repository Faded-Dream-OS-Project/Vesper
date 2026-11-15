/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <console.h>
#include <io.h>
#include <kbd.h>

/* PS/2 keyboard driver, scancode set 1.

   The i8042 controller is PROBED with bounded waits before anything
   else: on machines without a PS/2 controller (most modern laptops)
   the status port reads 0xFF forever and an unbounded wait would hang
   boot. If the controller does not pass its self-test we print
   'kbd: not available' and continue without a keyboard. Decoding
   keeps per-key shift state and a sticky caps lock; characters land
   in a ring buffer. */

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
static int kbd_ok;   /* controller passed the probe and was init'd */

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

/* Bounded wait for a status-bit condition. Real hardware answers in
   microseconds; 2M spins covers slow firmware without ever hanging. */
static int wait_stat(u8 mask, u8 want)
{
    for (u32 i = 0; i < 2000000u; i++) {
        if ((inb(KBD_STAT) & mask) == want)
            return 1;
    }
    return 0;
}

/* Probe the i8042: input-buffer empty + self-test 0x55. Returns 0 when
   the controller is usable. */
static int i8042_probe(void)
{
    u8 stat = inb(KBD_STAT);

    /* Absent controllers float the bus: status 0xFF or 0x00. */
    if (stat == 0xFF || stat == 0x00)
        return -1;

    /* Controller self-test. */
    if (!wait_stat(0x02, 0))          /* input buffer must be empty */
        return -1;
    outb(KBD_CMD, 0xAA);
    if (!wait_stat(0x01, 0x01))       /* wait for a response byte */
        return -1;
    if (inb(KBD_DATA) != 0x55)
        return -1;

    return 0;
}

int kbd_available(void)
{
    return kbd_ok;
}

void kbd_init(void)
{
    shift_down = 0;
    caps_lock = 0;
    ring_head = ring_tail = 0;
    events = 0;

    /* test hook: simulate an absent controller (QEMU always emulates
       the i8042 ports, so the probe cannot fail there) */
    {
        extern int cmdline_has(const char *flag);
        if (cmdline_has("kbd_test_absent")) {
            kprintf("$Wkbd: not available (no PS/2 controller response)$T\n");
            return;
        }
    }

    if (i8042_probe() != 0) {
        kprintf("$Wkbd: not available (no PS/2 controller response)$T\n");
        kbd_ok = 0;
        return;
    }
    kbd_ok = 1;

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
