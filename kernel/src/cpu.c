/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <cpu.h>
#include <lib.h>

/* CPUID discovery for the boot diagnostics screen (session 5) plus
   boot-time control-register snapshots (session 7).

   Session 7 policy change: cpu_sanitize() no longer WRITES CR0/CR4.
   Writing control registers before the IDT is installed is the
   riskiest code in the kernel: any #GP (reserved bit, unsupported
   feature, wrong enable order) is an unrecoverable triple fault, and
   on real firmware the boot path dies exactly there — black screen,
   nothing printed. The registers are READ and recorded instead, and
   the diagnostics screen shows what firmware left behind.

   Consequences handled elsewhere:
   - SMAP: if firmware left SMAP set, ring-0 user-buffer accesses in
     sys_write() are wrapped with STAC/CLAC (usermode.c), so no CR4
     write is needed for correctness.
   - SMEP: the kernel never executes user pages, so SMEP is harmless.
   - CR0.EM: left as firmware set it. If EM=1 the FPU is absent or
     firmware-disabled; Vesper executes no x87/SSE instructions in
     ring 0 (built with -mno-sse -mno-mmx), so emulation mode cannot
     fault. The snapshot tells us if this ever changes. */

u64 boot_cr0, boot_cr4, boot_efer;
int cr4_changed;   /* always 0 now: no CR4 writes at boot */

void cpu_sanitize(void)
{
    /* Read + record only. Nothing here can fault, and the IDT is not
       installed yet — by design it never needs to be. */
    boot_cr0 = read_cr0();
    boot_cr4 = read_cr4();
    boot_efer = read_msr(0xC0000080u);
    cr4_changed = 0;
}

void cpu_identify(struct cpu_info *c)
{
    u32 a, b, d, cc;
    char *p;

    memset(c, 0, sizeof(*c));

    /* Leaf 0: vendor + largest standard leaf */
    __asm__ volatile ("cpuid"
                      : "=a"(a), "=b"(b), "=d"(d), "=c"(cc)
                      : "a"(0));
    c->max_leaf = a;
    memcpy(c->vendor, &b, 4);
    memcpy(c->vendor + 4, &d, 4);
    memcpy(c->vendor + 8, &cc, 4);
    c->vendor[12] = 0;

    if (c->max_leaf >= 1) {
        __asm__ volatile ("cpuid"
                          : "=a"(a), "=b"(b), "=d"(d), "=c"(cc)
                          : "a"(1));
        c->apic = (d & (1u << 9)) != 0;
        c->tsc = (d & (1u << 4)) != 0;
        c->msr = (d & (1u << 5)) != 0;
        c->pat = (d & (1u << 16)) != 0;
        c->sse2 = (d & (1u << 26)) != 0;
        c->apic_id = (b >> 24) & 0xFF;
        /* brand leaf available? */
        if (c->max_leaf >= 0x16) {
            u32 ta, tb, td, tc;
            __asm__ volatile ("cpuid"
                              : "=a"(ta), "=b"(tb), "=d"(td), "=c"(tc)
                              : "a"(0x16));
            if (ta)
                c->tsc_hz = (u64)ta * 1000000ull; /* eax = base MHz */
        }
    }

    /* Extended leaves: brand string (0x80000002..04) */
    __asm__ volatile ("cpuid"
                      : "=a"(a), "=b"(b), "=d"(d), "=c"(cc)
                      : "a"(0x80000000));
    if (a >= 0x80000004) {
        p = c->brand;
        for (u32 leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
            __asm__ volatile ("cpuid"
                              : "=a"(a), "=b"(b), "=d"(d), "=c"(cc)
                              : "a"(leaf));
            memcpy(p, &a, 4); memcpy(p + 4, &b, 4);
            memcpy(p + 8, &cc, 4); memcpy(p + 12, &d, 4);
            p += 16;
        }
        c->brand[48] = 0;
    }

    /* Leaf 0x80000001: x2APIC (ECX bit 21), invariant TSC (EDX bit 8) */
    if (a >= 0x80000001) {
        __asm__ volatile ("cpuid"
                          : "=a"(a), "=b"(b), "=d"(d), "=c"(cc)
                          : "a"(0x80000001));
        c->x2apic = (cc & (1u << 21)) != 0;
        c->invariant_tsc = (d & (1u << 8)) != 0;
        c->nx = (d & (1u << 20)) != 0;
        c->lm = (d & (1u << 29)) != 0;
    }

    /* Leaf 0x15: actual core crystal TSC ratio (Intel) */
    if (c->max_leaf >= 0x15) {
        u32 denom, numer, reserved;
        __asm__ volatile ("cpuid"
                          : "=a"(denom), "=b"(numer),
                            "=c"(reserved), "=d"(d)
                          : "a"(0x15));
        if (denom != 0 && numer != 0) {
            c->tsc_ratio_denom = denom;
            c->tsc_ratio_numer = numer;
        }
    }
}
