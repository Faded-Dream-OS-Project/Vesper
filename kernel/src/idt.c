/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <isr.h>
#include <lib.h>

/* IDT installation. Gates point at the generated stubs via the
   explicit isr_stub_table in isr_stubs.S (no pointer arithmetic). */

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr   idtp;

extern void (*const isr_stub_table[256])(void);

void idt_set_gate(u8 vector, void (*handler)(void), u8 ist, u8 type)
{
    u64 addr = (u64)handler;

    idt[vector].offset_lo = (u16)(addr & 0xFFFF);
    idt[vector].selector  = SEL_KCODE;
    idt[vector].ist       = ist;
    idt[vector].type_attr = type;
    idt[vector].offset_mid = (u16)((addr >> 16) & 0xFFFF);
    idt[vector].offset_hi = (u32)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved = 0;
}

/* Stub dispatch targets (isr_stubs.S). */
void isr_dispatcher(struct int_frame *frame)
{
    exception_handler(frame);
}

void irq_dispatcher(struct int_frame *frame)
{
    irq_generic(frame);
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));

    for (int vector = 0; vector < 256; vector++) {
        u8 ist = 0;
        if (vector == 8)
            ist = IST_DF;
        else if (vector == 2)
            ist = IST_NMI;
        idt_set_gate((u8)vector, isr_stub_table[vector], ist,
                     IDT_GATE_INTERRUPT);
    }

    idtp.limit = sizeof(idt) - 1;
    idtp.base = (u64)idt;

    __asm__ volatile ("lidt (%0)" : : "r"(&idtp) : "memory");
}
