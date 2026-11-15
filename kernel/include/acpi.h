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

#define ACPI_MAX_IOAPICS  4
#define ACPI_MAX_OVERRIDES 16

struct acpi_iso {                 /* MADT interrupt source override */
    u8 bus, source_irq;
    u32 gsi;
    u16 flags;
};

struct acpi_ioapic {
    u64 mmio_base;
    u32 gsi_base;
    u32 gsi_count;                /* from the IOAPIC's own redir table */
};

struct acpi_info {
    u64 lapic_base;     /* LAPIC MMIO base (0xFEE00000 typical) */
    u64 ioapic_base;    /* IOAPIC MMIO base (0xFEC00000 typical) */
    u32 ioapic_gsi_base;/* first GSI the FIRST IOAPIC serves */
    u32 lapic_count;    /* LAPIC entries seen in MADT */
    u32 uart_intid;      /* GSI that ISA IRQ 4 (serial) maps to */
    int  valid;

    /* diagnostics (session 5): */
    u8  rsdp_revision;   /* 0 = 1.0 (RSDT), 2 = 2.0+ (XSDT) */
    char oem_id[7];
    u64 rsdt_phys;        /* root table physical address */
    u64 xsdt_phys;
    int  xsdt_used;       /* 1 = XSDT, 0 = RSDT */
    u32 root_entries;     /* tables listed by the root */
    int  has_hpet, has_fadt, has_mcfg;

    struct acpi_ioapic ioapics[ACPI_MAX_IOAPICS];
    u32 n_ioapics;

    struct acpi_iso overrides[ACPI_MAX_OVERRIDES];
    u32 n_overrides;
};

int acpi_init(void *rsdp_virt); /* pass Limine's VIRTUAL RSDP pointer */
const struct acpi_info *acpi_get_info(void);

#endif /* VESPER_ACPI_H */
