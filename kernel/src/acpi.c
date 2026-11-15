/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <acpi.h>
#include <lib.h>
#include <console.h>
#include <vmm.h>

/* ACPI table walk: RSDP -> XSDT (ACPI 2.0) -> MADT.
   Everything is read at virtual addresses provided by the caller
   (Limine maps ACPI tables into its higher-half window). */

/* RSDP layout per the ACPI 2.0 spec (all offsets validated against
   QEMU's table): sig[8] @0, checksum @8, oem_id[6] @9, revision @15,
   rsdt_addr(u32) @16, length(u32) @20, xsdt_addr(u64) @24,
   ext_checksum @32, reserved[3] @33. */
struct __attribute__((packed)) rsdp_desc {
    char signature[8];
    u8  checksum;
    char oem_id[6];
    u8  revision;
    u32 rsdt_addr;
    /* ACPI 1.0 ends here; 2.0 continues: */
    u32 length;
    u64 xsdt_addr;
    u8  ext_checksum;
    u8  reserved[3];
};

struct sdt_hdr {
    char signature[4];    /* "APIC" for the MADT etc. */
    u32 length;
    u8  rev;
    u8  checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_rev;
    u32 creator_id;
    u32 creator_rev;
};

struct madt_hdr {
    u32 lapic_addr;      /* default local APIC address */
    u32 flags;
};

struct madt_entry_hdr {
    u8 type;
    u8 length;
};

static struct acpi_info info;

static u32 checksum8(const u8 *p, u64 len)
{
    u32 s = 0;
    for (u64 i = 0; i < len; i++)
        s += p[i];
    return s & 0xFF;
}

static int table_ok(const struct sdt_hdr *h)
{
    if (!h)
        return 0;
    return checksum8((const u8 *)h, h->length) == 0;
}

/* Limine's RSDP response->address is already a VIRTUAL pointer into
   its higher-half window (observed: 0xffff8000000f52c0 on QEMU).
   The XSDT entries, however, hold PHYSICAL addresses, so translate
   those through the HHDM (vmm_phys_to_hhdm) before dereferencing. */
#define PHYS_PTR(p) ((void *)vmm_phys_to_hhdm((u64)(p)))

int acpi_init(void *rsdp_virt)
{
    struct rsdp_desc *rsdp = rsdp_virt;
    if (!rsdp)
        return -1;
    if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0)
        return -1;
    /* RSDP signature checksum is over the first 20 bytes. */
    if (checksum8((const u8 *)rsdp, 20) != 0)
        return -1;

    /* ACPI 2.0+ -> XSDT (8-byte physical pointers); ACPI 1.0 -> RSDT
       (4-byte). Both are translated through the HHDM. */
    struct sdt_hdr *root = NULL;
    u64 entry_size = 8;
    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        root = (struct sdt_hdr *)PHYS_PTR(rsdp->xsdt_addr);
        entry_size = 8;
        if (!table_ok(root) || memcmp(root->signature, "XSDT", 4) != 0)
            return -1;
    } else if (rsdp->rsdt_addr) {
        root = (struct sdt_hdr *)PHYS_PTR(rsdp->rsdt_addr);
        entry_size = 4;
        if (!table_ok(root) || memcmp(root->signature, "RSDT", 4) != 0)
            return -1;
    } else {
        return -1;
    }

    u64 n_entries = (root->length - sizeof(struct sdt_hdr)) / entry_size;
    struct sdt_hdr *madt = NULL;
    for (u64 i = 0; i < n_entries; i++) {
        u64 tbl_phys;
        if (entry_size == 8)
            tbl_phys = *(u64 *)((u8 *)root + sizeof(struct sdt_hdr) + 8 * i);
        else
            tbl_phys = *(u32 *)((u8 *)root + sizeof(struct sdt_hdr) + 4 * i);
        struct sdt_hdr *tbl = (struct sdt_hdr *)PHYS_PTR(tbl_phys);
        if (table_ok(tbl) && memcmp(tbl->signature, "APIC", 4) == 0) {
            madt = tbl;
            break;
        }
    }
    if (!madt)
        return -1;

    struct madt_hdr *m = (struct madt_hdr *)((u8 *)madt + sizeof(struct sdt_hdr));
    info.lapic_base = m->lapic_addr;
    info.lapic_count = 0;
    info.ioapic_base = 0;
    info.ioapic_gsi_base = 0;
    info.uart_intid = 4;

    u64 off = sizeof(struct madt_hdr);
    while (off + sizeof(struct madt_entry_hdr) <= madt->length -
             sizeof(struct sdt_hdr)) {
        struct madt_entry_hdr *e =
            (struct madt_entry_hdr *)((u8 *)m + off);
        if (e->length < 2)
            break;
        switch (e->type) {
        case 0:   /* Processor Local APIC */
            info.lapic_count++;
            break;
        case 1: {  /* I/O APIC */
            u8 *p = (u8 *)e;
            info.ioapic_base   = *(u32 *)(p + 4);
            info.ioapic_gsi_base = *(u32 *)(p + 8);
            break;
        }
        case 2: {  /* Interrupt Source Override */
            u8 *p = (u8 *)e;
            u8 bus = p[4];
            u8 irq = p[5];
            u32 gsi = *(u32 *)(p + 8);
            /* ISA bus 0 overrides: remember which GSI serial IRQ 4
               lands on (QEMU maps ISA IRQs 1:1, but record it). */
            if (bus == 0 && irq == 4)
                info.uart_intid = gsi;
            break;
        }
        default:
            break;
        }
        off += e->length;
    }

    info.valid = (info.lapic_base != 0);
    return info.valid ? 0 : -1;
}

const struct acpi_info *acpi_get_info(void)
{
    return &info;
}
