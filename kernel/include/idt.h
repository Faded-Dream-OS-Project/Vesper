/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_IDT_H
#define VESPER_IDT_H

#include <common.h>

#define IDT_ENTRIES 256

/* Interrupt stack table entries in the TSS (0 = disabled). We use
   ist1 for double fault (#DF) and ist2 for NMI. */
#define IST_DF   1
#define IST_NMI  2
#define IST_PF   3   /* valid stack for #PF so a guard-page hit during
                       stack exhaustion cannot escalate to #DF */

struct idt_entry {
    u16 offset_lo;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_hi;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(u8 vector, void (*handler)(void), u8 ist, u8 type);

/* Type attributes */
#define IDT_GATE_INTERRUPT 0x8E
#define IDT_GATE_TRAP      0xEF

#endif /* VESPER_IDT_H */
