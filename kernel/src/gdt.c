/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <lib.h>

/* GDT + TSS. The TSS provides IST stacks for #DF and NMI so a corrupt
   kernel stack can still be diagnosed. */

struct tss_entry {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u32 reserved2, reserved3;
    u16 iopb_offset;
} __attribute__((packed));

static struct tss_entry tss __attribute__((aligned(16)));

/* ist stacks: 16 KiB each, static since we have no heap yet. */
static u8 df_stack[16384] __attribute__((aligned(16)));
static u8 nmi_stack[16384] __attribute__((aligned(16)));
static u8 pf_stack[16384] __attribute__((aligned(16)));

static u64 gdt[16] __attribute__((aligned(16)));

/* Build a descriptor using the canonical 8-byte layout:
     byte 0-1: limit[0:16]
     byte 2-4: base[0:24]
     byte 5:   access
     byte 6:   flags << 4 | limit[16:20]
     byte 7:   base[24:32]
*/
static void encode_segment(u64 *desc, u32 base, u32 limit, u8 access, u8 flags)
{
    *desc = 0;
    *desc |= (u64)(limit & 0xFFFF);                        /* bytes 0-1 */
    *desc |= (u64)(base & 0xFFFFFF) << 16;                 /* bytes 2-4 */
    *desc |= (u64)access << 40;                            /* byte 5 */
    *desc |= (u64)((limit >> 16) & 0x0F) << 48;            /* byte 6 low */
    *desc |= (u64)(flags & 0x0F) << 52;                    /* byte 6 high */
    *desc |= (u64)((base >> 24) & 0xFF) << 56;             /* byte 7 */
}

static void encode_tss(u64 *desc, u64 base)
{
    encode_segment(&desc[0], (u32)base, sizeof(struct tss_entry) - 1,
                   0x89, 0x00);
    desc[1] = base >> 32;   /* bytes 8..15: base[32:63] */
}

struct gdtr {
    u16 limit;
    u64 base;
} __attribute__((packed));

/* Implemented in gdt_flush.S: far-return to reload CS, reload data
   segment selectors, then ltr. */
void gdt_flush(u64 gdtr_addr, u16 tr_selector);

/* TSS.rsp0 — the ring-0 stack for interrupts/syscalls while a user
   task is in ring 3. */
void gdt_set_rsp0(u64 rsp)
{
    tss.rsp0 = rsp;
}

void gdt_init(void)
{
    memset(&tss, 0, sizeof(tss));
    memset(df_stack, 0, sizeof(df_stack));
    memset(nmi_stack, 0, sizeof(nmi_stack));
    memset(pf_stack, 0, sizeof(pf_stack));

    tss.ist[IST_DF - 1] = (u64)(df_stack + sizeof(df_stack));
    tss.ist[IST_NMI - 1] = (u64)(nmi_stack + sizeof(nmi_stack));
    tss.ist[IST_PF - 1] = (u64)(pf_stack + sizeof(pf_stack));

    encode_segment(&gdt[1], 0, 0xFFFFF, 0x9A, 0xA);  /* 0x08 kernel code64 */
    encode_segment(&gdt[2], 0, 0xFFFFF, 0x92, 0xC);  /* 0x10 kernel data */
    encode_segment(&gdt[3], 0, 0xFFFFF, 0xFA, 0xA);  /* 0x18 user code64 */
    encode_segment(&gdt[4], 0, 0xFFFFF, 0xF2, 0xC);  /* 0x20 user data */
    /* 0x28/0x30: descriptors matching Limine's running CS/SS values so
       the CPU keeps executing valid segments after the lgdt. */
    encode_segment(&gdt[5], 0, 0xFFFFF, 0x9A, 0xA);  /* 0x28 code64 */
    encode_segment(&gdt[6], 0, 0xFFFFF, 0x92, 0xC);  /* 0x30 data */
    encode_tss(&gdt[7], (u64)&tss);                  /* 0x38 TSS */

    static struct gdtr gdtr;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)gdt;

    gdt_flush((u64)&gdtr, SEL_TSS);
}
