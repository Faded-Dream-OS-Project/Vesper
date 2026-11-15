/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <io.h>
#include <pic.h>

/* Legacy 8259 PIC control.

   Vesper routes interrupts through the (IO)APIC only, so the PIC is
   remapped out of the way (0x20..0x2F, below the exception range)
   and fully masked. A masked PIC can still raise spurious IRQ7/IRQ15
   on edge conditions; those arrive as vectors 0x27/0x2F and the IRQ
   path ignores them (isr.c checks the PIC ISR register before EOI
   when the PIC is in use — with the IMR fully masked nothing gets
   through, and the APIC does not cascade from the PIC).

   Init sequence per the 8259 datasheet: ICW1 -> ICW2 -> ICW3 ->
   OCW1 (mask). */

#define PIC1_CMD 0x20
#define PIC1_DAT 0x21
#define PIC2_CMD 0xA0
#define PIC2_DAT 0xA1

#define PIC_ICW1_INIT  0x10   /* initialise */
#define PIC_ICW1_IC4    0x01  /* ICW4 needed */
#define PIC_ICW4_8086   0x01  /* 8086 mode */

void pic_init(void)
{
    /* Save masks. */
    u8 m1 = inb(PIC1_DAT);
    u8 m2 = inb(PIC2_DAT);

    outb(PIC1_CMD, PIC_ICW1_INIT | PIC_ICW1_IC4);  io_wait();
    outb(PIC1_DAT, 0x20);                          io_wait(); /* base 0x20 */
    outb(PIC1_DAT, 1 << 2);                         io_wait(); /* slave on IRQ2 */
    outb(PIC1_DAT, PIC_ICW4_8086);                  io_wait();
    outb(PIC2_CMD, PIC_ICW1_INIT | PIC_ICW1_IC4);   io_wait();
    outb(PIC2_DAT, 0x28);                           io_wait(); /* base 0x28 */
    outb(PIC2_DAT, 2);                               io_wait(); /* slave id 2 */
    outb(PIC2_DAT, PIC_ICW4_8086);                  io_wait();

    /* Mask everything (restore nothing: APIC owns delivery). */
    outb(PIC1_DAT, 0xFF);
    outb(PIC2_DAT, 0xFF);
    (void)m1; (void)m2;
}

void pic_mask_all(void)
{
    outb(PIC1_DAT, 0xFF);
    outb(PIC2_DAT, 0xFF);
}

void pic_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

u8 pic_isr(void)
{
    return inb(PIC1_CMD);
}
