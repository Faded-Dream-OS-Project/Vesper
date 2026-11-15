/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_GDT_H
#define VESPER_GDT_H

#include <common.h>

/* Kernel GDT layout (session 8 rework). Vesper OWNS the whole table:
   nothing of Limine runs after gdt_init, so the old "compat
   descriptors at 0x28/0x30 for Limine's live selectors" are gone —
   they were the WRONG descriptors for the one selector pair that
   actually runs user code:

     SYSRET loads CS = STAR[63:48]+16 and SS = STAR[63:48]+8 (RPL 3).
     usermode.c programs STAR[63:48] = SEL_UCODE_BASE = 0x18, so every
     sysret enters ring 3 through CS=0x2b / SS=0x23 — i.e. through the
     entries at selectors 0x28/0x20, NOT through 0x18/0x20 the names
     suggested. On the human's i5-8400 that descriptor load faulted
     with #GP err=0x28 (selector 0x28, index 5, TI=0, EXT=0) — the
     err code names the exact descriptor the CPU rejected.

   The table is now laid out to match what the CPU actually loads:

     0x00 null
     0x08 kernel code64 (SYSCALL CS; DPL 0)
     0x10 kernel data   (SYSCALL SS = CS+8; DPL 0)
     0x18 user code64 base (STAR[63:48]; also usable directly at DPL 3)
     0x20 user data   (SYSRET SS; DPL 3)
     0x28 user code64 (SYSRET CS = STAR[63:48]+16; DPL 3)
     0x30 user data   (SYSCALL SS alias / spare; DPL 3)
     0x38 TSS

   All code/data descriptors have the Accessed bit PRE-SET so no
   segment register load ever needs to write the GDT (a descriptor
   load sets A; if it ever did write, a read-only GDT page would turn
   that load into a fault). */

#define SEL_NULL      0x00
#define SEL_KCODE     0x08
#define SEL_KDATA     0x10
#define SEL_UCODE     0x18   /* STAR[63:48] base + direct user code64 */
#define SEL_UDATA     0x20   /* SYSRET SS (STAR+8) */
#define SEL_USYSCODE  0x28   /* SYSRET CS (STAR[63:48]+16) */
#define SEL_USYSDATA  0x30   /* spare user data */
#define SEL_TSS       0x38

void gdt_init(void);
void gdt_set_rsp0(u64 rsp);   /* TSS.rsp0 for the current user task */

/* GDT diagnostics (session 8): raw + decoded entries for the boot
   screen and the #GP/#SS/#NP handlers. */
void gdt_dump(const char *tag);
int gdt_verify_selectors(void);   /* lar/lsl check, returns #bad */

#endif /* VESPER_GDT_H */
