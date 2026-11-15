/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_ACPI_H
#define VESPER_ACPI_H

#include <common.h>

/* ACPI parsing, kept to the minimum the kernel needs:
   RSDP (from Limine) -> XSDT -> MADT, and out fall:
   - Local APIC (LAPIC) physical MMIO base
   - IOAPIC physical MMIO base (+ its GSI base)
   - number of LAPICs on the platform. */

struct acpi_info {
    u64 lapic_base;     /* LAPIC MMIO base (0xFEE00000 typical) */
    u64 ioapic_base;    /* IOAPIC MMIO base (0xFEC00000 typical) */
    u32 ioapic_gsi_base;/* first GSI this IOAPIC serves */
    u32 lapic_count;    /* LAPIC entries seen in MADT */
    u32 uart_intid;      /* GSI that ISA IRQ 4 (serial) maps to */
    int  valid;
};

int acpi_init(void *rsdp_virt); /* pass Limine's VIRTUAL RSDP pointer */
const struct acpi_info *acpi_get_info(void);

#endif /* VESPER_ACPI_H */
