/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <cpu.h>
#include <lib.h>

/* CPUID discovery for the boot diagnostics screen (session 5) plus
   early control-register sanitisation for real hardware (session 6):
   firmware may leave SMEP/SMAP set (we access user memory from ring 0
   in sys_write — SMAP would fault those accesses) or leave legacy
   emulation bits in CR0. Print what we found and what we changed. */

u64 boot_cr0, boot_cr4, boot_efer;
int cr4_changed;

void cpu_sanitize(void)
{
    boot_cr0 = read_cr0();
    boot_cr4 = read_cr4();
    boot_efer = read_msr(0xC0000080u);

    /* CR0: clear emulation (EM) so FPU instructions work if user code
       ever uses them; keep everything else firmware chose (WP, PG,
       PE, NE...). */
    u64 cr0 = boot_cr0 & ~(1ull << 2);           /* EM=0 */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    /* CR4: clear SMEP (bit 20) / SMAP (bit 21) — the kernel reads and
       writes user buffers from ring 0. Keep OSFXSR/OSXMMEXCPT (bit
       9/10) as firmware set them; set them if absent so SSE state is
       usable. Also clear PGE? no — keep. */
    u64 cr4 = boot_cr4 & ~((1ull << 20) | (1ull << 21));
    cr4 |= (1ull << 9) | (1ull << 10);           /* OSFXSR, OSXMMEXCPT */
    cr4_changed = (cr4 != boot_cr4);
    write_cr4(cr4);

    /* EFER.NXE is enabled by usermode_init later; nothing to do here.
       Cache the post state for the diagnostics print. */
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
