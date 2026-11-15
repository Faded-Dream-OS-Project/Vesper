/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_KBD_H
#define VESPER_KBD_H

#include <common.h>

/* PS/2 keyboard: IRQ1 (GSI 1) routed through the IOAPIC, scancode
   set 1 decoding with shift and caps lock, a ring buffer of decoded
   characters, kbd_getchar() for consumers. */

#define KBD_IRQ_VECTOR 0x21   /* timer is 0x30; IRQ1 mapped to 0x21 */

void kbd_init(void);          /* PS/2 controller + IOAPIC routing */
void kbd_irq(void);           /* called from irq_dispatch for 0x21 */
int  kbd_getchar(void);       /* -1 if empty; else decoded ASCII */
u32  kbd_event_count(void);   /* scancodes handled (self-test) */

#endif /* VESPER_KBD_H */
