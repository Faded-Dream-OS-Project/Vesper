/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <acpi.h>
#include <console.h>
#include <cpu.h>
#include <limine.h>
#include <lib.h>

/* Boot diagnostics screen (session 5): one readable block, printed
   after the framebuffer is up, so a human with a camera can capture
   what the machine actually has before anything else happens. Section
   headers cyan, abnormal findings amber. */

extern struct limine_framebuffer_request framebuffer_request;
extern struct limine_memmap_request memmap_request;
extern struct limine_firmware_type_request firmware_request;

int boot_mode_uefi;   /* set once at boot from the firmware request */

static void cpu_flags_str(const struct cpu_info *c, char *out, u64 cap)
{
    u64 n = 0;

    out[0] = 0;
#define APPEND(s)  do { \
        const char *_s = (s); \
        while (*_s && n + 1 < cap) out[n++] = *_s++; \
        if (n + 1 < cap && *out) out[n++] = ' '; \
    } while (0)
    if (c->apic)         APPEND("APIC");
    if (c->x2apic)       APPEND("x2APIC");
    if (c->tsc)          APPEND("TSC");
    if (c->invariant_tsc) APPEND("invTSC");
    if (c->msr)          APPEND("MSR");
    if (c->nx)           APPEND("NX");
    if (c->lm)           APPEND("LM");
#undef APPEND
    out[n] = 0;
}

void bootinfo_print(void)
{
    struct cpu_info cpu;
    char flags[80];
    const struct acpi_info *ai = acpi_get_info();

    kprintf("$I=== Vesper boot diagnostics ===$T\n");

    /* CPU */
    cpu_identify(&cpu);
    kprintf("CPU: %s\n", cpu.brand[0] ? cpu.brand : cpu.vendor);
    kprintf("CPUID: vendor %s, max leaf %#x, apic id %lu\n",
            cpu.vendor, cpu.max_leaf, (unsigned long)cpu.apic_id);
    cpu_flags_str(&cpu, flags, sizeof(flags));
    kprintf("CPUID flags: %s\n", flags[0] ? flags : "(none reported)");

    /* Memory map */
    {
        struct limine_memmap_response *mm = memmap_request.response;
        u64 usable = 0;
        u64 count = 0;
        if (mm) {
            count = mm->entry_count;
            for (u64 i = 0; i < mm->entry_count; i++)
                if (mm->entries[i]->type == LIMINE_MEMMAP_USABLE)
                    usable += mm->entries[i]->length;
        }
        kprintf("Memory: %lu map entries, %lu MiB usable\n",
                (unsigned long)count, usable >> 20);
    }

    /* Framebuffer mode */
    {
        struct limine_framebuffer_response *r = framebuffer_request.response;
        if (r && r->framebuffer_count > 0 && r->framebuffers[0]) {
            struct limine_framebuffer *f = r->framebuffers[0];
            kprintf("Framebuffer: %lux%lu, %ubpp, pitch %lu, rgb "
                    "%u/%u:%u/%u:%u/%u\n",
                    (unsigned long)f->width, (unsigned long)f->height,
                    (unsigned)f->bpp, (unsigned long)f->pitch,
                    (unsigned)f->red_mask_size,
                    (unsigned)f->red_mask_shift,
                    (unsigned)f->green_mask_size,
                    (unsigned)f->green_mask_shift,
                    (unsigned)f->blue_mask_size,
                    (unsigned)f->blue_mask_shift);
        } else {
            kprintf("$WFramebuffer: none (serial-only console)$T\n");
        }
    }

    /* ACPI */
    if (ai->valid) {
        kprintf("ACPI %lu.0: OEM %s, %s at %#lx (%lu tables)%s\n",
                (unsigned long)(ai->rsdp_revision >= 2 ? 2 : 1),
                ai->oem_id,
                ai->xsdt_used ? "XSDT" : "RSDT",
                ai->xsdt_used ? ai->xsdt_phys : ai->rsdt_phys,
                (unsigned long)ai->root_entries,
                ai->rsdp_revision >= 2 ? "" : " (ACPI 1.0)");
        kprintf("ACPI tables: MADT%s%s%s\n",
                ai->has_hpet ? ", HPET" : "",
                ai->has_fadt ? ", FADT" : "",
                ai->has_mcfg ? ", MCFG" : "");
        kprintf("LAPIC base %#lx, %lu CPU entries listed (parked: all "
                "but BSP)\n",
                ai->lapic_base, (unsigned long)ai->lapic_count);
        for (u32 i = 0; i < ai->n_ioapics; i++)
            kprintf("IOAPIC[%lu] base %#lx (GSI %lu..)\n",
                    (unsigned long)i,
                    (unsigned long)ai->ioapics[i].mmio_base,
                    (unsigned long)ai->ioapics[i].gsi_base);
        if (ai->n_overrides == 0)
            kprintf("MADT overrides: none (ISA IRQs identity-mapped)\n");
        for (u32 i = 0; i < ai->n_overrides; i++) {
            const struct acpi_iso *o = &ai->overrides[i];
            kprintf("MADT override: ISA IRQ %lu -> GSI %lu, flags %#x\n",
                    (unsigned long)o->source_irq,
                    (unsigned long)o->gsi, (unsigned)o->flags);
        }
    } else {
        kprintf("$WACPI: no valid tables found$T\n");
    }

    /* Boot mode from the bootloader's firmware-type response. */
    boot_mode_uefi = 0;
    if (firmware_request.response) {
        u64 fw = firmware_request.response->firmware_type;
        if (fw == LIMINE_FIRMWARE_TYPE_EFI32 ||
            fw == LIMINE_FIRMWARE_TYPE_EFI64)
            boot_mode_uefi = 1;
    }
    kprintf("Boot mode: %s\n", boot_mode_uefi ? "UEFI" : "BIOS");
}
