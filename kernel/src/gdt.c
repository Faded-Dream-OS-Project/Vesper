/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <lib.h>
#include <limine.h>

/* GDT + TSS. The TSS provides IST stacks for #DF and NMI so a corrupt
   kernel stack can still be diagnosed.

   Session 8: the two descriptors that USED to be "Limine compat"
   (0x28/0x30) are now real USER descriptors, because SYSRET enters
   ring 3 through them (CS = STAR[63:48]+16 = 0x28|3, SS = +8 =
   0x20|3). Every descriptor's Accessed bit is pre-set, so segment
   loads never write the GDT. gdt_dump() prints the table for the
   diagnostics screen and the selector-fault handlers. */

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

/* Access-byte decode for the diagnostics dump. */
static const char *seg_type_str(u8 access)
{
    u8 type = access & 0xF;   /* high nibble of access byte */
    if (access & 0x10) {      /* S=1: code or data */
        if (type & 0x8) {     /* code */
            if (type & 0x4) return "code32/16 (A=1, R, C?)";
            return (type & 0x2) ? "code64-readable" : "code64-execute-only";
        }
        return (type & 0x2) ? "data-rw" : "data-ro";
    }
    switch (type) {
    case 0x9:  return "TSS-avail-64";
    case 0xB: return "TSS-busy-64";
    default:   return "system";
    }
}

void gdt_dump(const char *tag)
{
    for (int i = 0; i < 8; i++) {
        u64 raw = gdt[i];
        if (!raw && i != 0)
            continue;
        u8 access = (u8)((raw >> 40) & 0xFF);
        u8 flags  = (u8)((raw >> 52) & 0xF);
        int p = (access >> 7) & 1, dpl = (access >> 5) & 3,
            s = (access >> 4) & 1, l = (flags >> 1) & 1,
            d = (flags >> 0) & 1, db = d;
        kprintf("GDT[%d] sel=%#04x raw=%016lx %s P=%d DPL=%d "
                "S=%d L=%d D=%d A=%d\n",
                i, i * 8, (unsigned long)raw, seg_type_str(access),
                p, dpl, s, l, db, access & 0x1);
    }
    (void)tag;
}

/* lar/lsl verification (session 8, task 1.5): after the table is live,
   every selector we use must load access rights and a sane limit.
   ZF=1 from lar/lsl means the CPU accepted the selector; the printed
   rights/limit make selector faults diagnosable from a photo. */
static int verify_selector(const char *name, u16 sel)
{
    u32 rights = 0, limit = 0, zf_r = 0, zf_l = 0;
    __asm__ volatile ("lar %w[s], %k[r]\n"
                      "jnz 1f\n"
                      "movl $1, %[z]\n"
                      "1:\n"
                      : [r] "=r"(rights), [z] "+m"(zf_r)
                      : [s] "r"((u64)sel)
                      : "cc");
    __asm__ volatile ("lsl %w[s], %k[r]\n"
                      "jnz 1f\n"
                      "movl $1, %[z]\n"
                      "1:\n"
                      : [r] "=r"(limit), [z] "+m"(zf_l)
                      : [s] "r"((u64)sel)
                      : "cc");
    int ok = zf_r && zf_l && rights != 0;
    kprintf("%s sel=%#04x %s rights=%#lx limit=%#lx\n",
            name, sel, ok ? "OK " : "BAD", (unsigned long)rights,
            (unsigned long)limit);
    return ok;
}

/* Verify all selectors the kernel and ring-3 paths load. Returns the
   number of failures (0 = all good). */
int gdt_verify_selectors(void)
{
    int bad = 0;
    kprintf("$I--- selector verification (lar/lsl) ---$T\n");
    bad += !verify_selector("KCODE ", SEL_KCODE);
    bad += !verify_selector("KDATA ", SEL_KDATA);
    bad += !verify_selector("UCODE ", SEL_UCODE);
    bad += !verify_selector("UDATA ", SEL_UDATA);
    bad += !verify_selector("USYSCS", SEL_USYSCODE);   /* sysret CS+3 */
    bad += !verify_selector("USYSDS", SEL_USYSDATA);   /* spare data  */
    bad += !verify_selector("TSS   ", SEL_TSS);
    kprintf("selector verification: %d bad\n", bad);
    return bad;
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

    /* Accessed bit (0x02 in the access byte) pre-set everywhere: a
       segment register load normally WRITES A into the descriptor;
       pre-setting means no load ever writes the GDT (see header). */
    encode_segment(&gdt[1], 0, 0xFFFFF, 0x9B, 0xA); /* 0x08 kcode64 A=1 */
    encode_segment(&gdt[2], 0, 0xFFFFF, 0x93, 0xC); /* 0x10 kdata   A=1 */
    encode_segment(&gdt[3], 0, 0xFFFFF, 0xFB, 0xA); /* 0x18 ucode64 A=1 DPL3 */
    encode_segment(&gdt[4], 0, 0xFFFFF, 0xF3, 0xC); /* 0x20 udata   A=1 DPL3 */
    /* SYSRET CS = STAR[63:48]+16 = 0x2b: a REAL user code64 here. */
    encode_segment(&gdt[5], 0, 0xFFFFF, 0xFB, 0xA); /* 0x28 ucode64 DPL3 A=1 */
    encode_segment(&gdt[6], 0, 0xFFFFF, 0xF3, 0xC); /* 0x30 udata   A=1 DPL3 */
    encode_tss(&gdt[7], (u64)&tss);                 /* 0x38 TSS */

    static struct gdtr gdtr;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u64)gdt;

    gdt_flush((u64)&gdtr, SEL_TSS);
}
