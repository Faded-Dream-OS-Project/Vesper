/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_BREADCRUMB_H
#define VESPER_BREADCRUMB_H

#include <common.h>
#include <limine.h>

/* Early-boot breadcrumbs (kernel/src/breadcrumb.c): one 16x16 coloured
   square per init stage at the top-left of the framebuffer (plain
   stores, valid the moment Limine's framebuffer response is usable)
   plus one code byte to port 0xE9 per stage. A photo of a black screen
   then shows how far boot got; QEMU -debugcon sees the codes too.

   Codes (also written to 0xE9):
     1 entry   2 cpu   3 fb   4 console   5 gdt   6 idt   7 pic
     8 pmm     9 vmm  10 acpi  11 apic  12 timer  13 sched
     14 user  15 done                                  */

/* Give the breadcrumbs the framebuffer (kmain, as soon as the Limine
   response is checked). Drawing before this call is a no-op. */
void breadcrumb_fb(struct limine_framebuffer *fb);

/* Record reaching a stage: square + port 0xE9 code. */
void breadcrumb_stage(u8 code);

/* Console takes the screen over: clear the squares, stop drawing. */
void breadcrumb_handover(void);

/* Diagnostics access: last code written, squares drawn. */
u8 breadcrumb_last(void);
int breadcrumb_count(void);

#endif /* VESPER_BREADCRUMB_H */
