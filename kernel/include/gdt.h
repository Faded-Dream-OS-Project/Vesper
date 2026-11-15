/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_GDT_H
#define VESPER_GDT_H

#include <common.h>

/* Kernel GDT selector layout. Kept identical to Limine's so the
   selectors the bootloader already loaded (CS=0x28, DS/SS=0x30 in
   Limine's table) remain valid after our lgdt — with the TSS moved to
   index 7 (0x38) to avoid colliding with the running code selector. */

#define SEL_NULL      0x00
#define SEL_KCODE     0x08
#define SEL_KDATA     0x10
#define SEL_UCODE     0x18
#define SEL_UDATA     0x20
#define SEL_LCODE     0x28   /* Limine's CS value while booting us */
#define SEL_LDATA     0x30   /* Limine's DS/SS value while booting us */
#define SEL_TSS       0x38

void gdt_init(void);

#endif /* VESPER_GDT_H */
