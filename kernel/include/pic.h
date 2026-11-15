/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_PIC_H
#define VESPER_PIC_H

#include <common.h>

/* Legacy 8259 PIC (remapped + masked; APIC owns interrupt delivery). */

void pic_init(void);     /* remap to 0x20..0x2F, mask all lines */
void pic_mask_all(void);
void pic_eoi(u8 irq);    /* legacy EOI (unused when masked) */
u8   pic_isr(void);      /* ISR register read (spurious-IRQ check) */

#endif /* VESPER_PIC_H */
