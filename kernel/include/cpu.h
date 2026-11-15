/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_CPU_H
#define VESPER_CPU_H

#include <common.h>

/* CPUID-derived identity for the boot diagnostics screen. Fields stay
   zero when a leaf is absent; callers must tolerate that. */
struct cpu_info {
    char vendor[13];       /* "GenuineIntel", "AuthenticAMD", ... */
    char brand[49];        /* extended brand string (0x80000002..4) */
    u32 max_leaf;
    u32 apic_id;

    /* EDX/ECX feature bits Vesper cares about */
    int apic, x2apic;
    int tsc, invariant_tsc;
    int msr, pat, nx, lm, sse2;

    u64 tsc_hz;            /* leaf 0x16 base frequency (0 = absent) */
    u32 tsc_ratio_denom, tsc_ratio_numer;   /* leaf 0x15 (0 = absent) */
};

void cpu_identify(struct cpu_info *c);

#endif /* VESPER_CPU_H */
