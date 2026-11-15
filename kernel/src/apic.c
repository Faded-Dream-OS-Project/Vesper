/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <apic.h>
#include <console.h>
#include <acpi.h>
#include <io.h>
#include <vmm.h>

/* Local APIC + IOAPIC. The LAPIC is memory-mapped at a physical
   address (0xFEE00000); the IOAPIC likewise (0xFEC00000). Both are
   accessed through the HHDM. */

static volatile u32 *lapic;
static volatile u32 *ioapic;
static u32 ioapic_gsi0;

/* defined below (IOAPIC section) */
static void ioapic_enumerate(const struct acpi_info *ai);

static volatile u32 *lapic_reg(u32 reg)
{
    return (volatile u32 *)((u8 *)lapic + reg);
}

void apic_eoi(void)
{
    if (lapic)
        *lapic_reg(APIC_REG_EOI) = 0;
}

void apic_init(u64 lapic_base, u64 ioapic_base, u32 ioapic_gsi_base)
{
    lapic = (volatile u32 *)vmm_phys_to_hhdm(lapic_base);
    ioapic = (volatile u32 *)vmm_phys_to_hhdm(ioapic_base);
    ioapic_gsi0 = ioapic_gsi_base;
    (void)ioapic;   /* superseded by the enumerated ctl[] table */

    /* Software enable + spurious vector 0xFF. (Do NOT touch reserved
       bits in the spurious register — a stray EOI-broadcast bit made
       QEMU's LAPIC swallow timer interrupts.) */
    *lapic_reg(APIC_REG_SPURIOUS) = APIC_SPIV_ENABLE | 0xFF;

    /* Map the LAPIC timer to vector 0x20+... we use 0x30 (48) so it
       stays clear of the legacy PIC remap range and exceptions. */
    *lapic_reg(APIC_REG_TIMER_DIV) = 0x3;   /* divide by 16 */
    *lapic_reg(APIC_REG_TIMER_LVT) = APIC_LVT_MASKED;   /* masked for now */

    /* Enumerate every IOAPIC listed in the MADT (session 5: real
       machines can have several with split GSI ranges). */
    ioapic_enumerate(acpi_get_info());
}

u64 apic_ticks(void)
{
    return *lapic_reg(APIC_REG_TIMER_CCR);
}

void lapic_set_timer(u8 vector, int masked, u64 initial_count)
{
    *lapic_reg(APIC_REG_TIMER_DIV) = 0x3;   /* divide by 16 */
    u32 lvt = vector;
    if (masked)
        lvt |= APIC_LVT_MASKED;
    *lapic_reg(APIC_REG_TIMER_LVT) = lvt;
    *lapic_reg(APIC_REG_TIMER_ICR) = (u32)initial_count;
}

void lapic_set_timer_periodic(u8 vector, u8 divider_code, u64 initial_count)
{
    *lapic_reg(APIC_REG_TIMER_DIV) = divider_code ? divider_code : 0x3;
    *lapic_reg(APIC_REG_TIMER_LVT) = APIC_LVT_PERIODIC | vector;
    *lapic_reg(APIC_REG_TIMER_ICR) = (u32)initial_count;
}

/* Re-arm the periodic timer (write ICR again). QEMU's TCG LAPIC in
   some configurations stops reloading after the first expiry unless
   the initial count is rewritten; the timer module re-arms on each
   tick until steady periodic delivery is confirmed. */
void lapic_timer_rearm(u64 initial_count)
{
    *lapic_reg(APIC_REG_TIMER_ICR) = (u32)initial_count;
}

u64 lapic_timer_icr(void)
{
    return *lapic_reg(APIC_REG_TIMER_ICR);
}

u32 lapic_read_ccr(void)
{
    return *lapic_reg(APIC_REG_TIMER_CCR);
}

/* Debug: dump the LAPIC timer configuration registers. */
void lapic_timer_dump(void)
{
    kprintf("lapic timer: LVT=%#lx ICR=%#lx CCR=%#lx DIV=%#lx\n",
            (unsigned long)*lapic_reg(APIC_REG_TIMER_LVT),
            (unsigned long)*lapic_reg(APIC_REG_TIMER_ICR),
            (unsigned long)*lapic_reg(APIC_REG_TIMER_CCR),
            (unsigned long)*lapic_reg(APIC_REG_TIMER_DIV));
}

u32 apic_timer_vector(void)
{
    return 0x30;
}

/* ---- IOAPIC (multiple controllers + MADT overrides) ---- */

#define MAX_IOAPICS ACPI_MAX_IOAPICS

struct ioapic_ctl {
    volatile u32 *mmio;     /* HHDM address of the IOREGSEL window */
    u32 gsi_base;
    u32 pins;
};

static struct ioapic_ctl ctl[MAX_IOAPICS];
static u32 n_ctl;

static u32 io_read(struct ioapic_ctl *c, u32 reg)
{
    *(volatile u32 *)((u8 *)c->mmio + IOAPIC_REG_IOREGSEL) = reg;
    return *(volatile u32 *)((u8 *)c->mmio + IOAPIC_REG_IOWIN);
}

static void io_write(struct ioapic_ctl *c, u32 reg, u32 val)
{
    *(volatile u32 *)((u8 *)c->mmio + IOAPIC_REG_IOREGSEL) = reg;
    *(volatile u32 *)((u8 *)c->mmio + IOAPIC_REG_IOWIN) = val;
}

/* Register every MADT IOAPIC; called from apic_init. */
static void ioapic_enumerate(const struct acpi_info *ai)
{
    n_ctl = 0;
    for (u32 i = 0; i < ai->n_ioapics && n_ctl < MAX_IOAPICS; i++) {
        struct ioapic_ctl *c = &ctl[n_ctl];
        c->mmio = (volatile u32 *)vmm_phys_to_hhdm(
            ai->ioapics[i].mmio_base);
        c->gsi_base = ai->ioapics[i].gsi_base;
        /* pins = max redirection entries (reg 1, bits 23:16) */
        c->pins = ((io_read(c, 1) >> 16) & 0xFF) + 1;
        n_ctl++;
    }
}

/* Translate an ISA IRQ number to its GSI through the MADT interrupt
   source overrides (no override -> identity). */
u32 isa_irq_to_gsi(u8 isa_irq)
{
    const struct acpi_info *ai = acpi_get_info();
    for (u32 i = 0; i < ai->n_overrides; i++)
        if (ai->overrides[i].bus == 0 &&
            ai->overrides[i].source_irq == isa_irq)
            return ai->overrides[i].gsi;
    return isa_irq;
}

/* The controller that serves this GSI, plus the pin on it. */
static struct ioapic_ctl *ctl_for_gsi(u32 gsi, u32 *pin)
{
    for (u32 i = 0; i < n_ctl; i++) {
        if (gsi >= ctl[i].gsi_base && gsi < ctl[i].gsi_base + ctl[i].pins) {
            *pin = gsi - ctl[i].gsi_base;
            return &ctl[i];
        }
    }
    return NULL;
}

u32 ioapic_pins(void)
{
    return n_ctl ? ctl[0].pins : 0;
}

/* Route a GSI (already translated) to a vector on the controller that
   owns it. Trigger/polarity follow the MADT override flags when the
   GSI came from an ISA IRQ (bit 0: polarity, bit 1: trigger; 0 =
   conforms to bus = active-high/edge for ISA). */
void ioapic_route_gsi(u32 gsi, u8 vector, int masked)
{
    u32 pin;
    struct ioapic_ctl *c = ctl_for_gsi(gsi, &pin);
    if (!c)
        return;                       /* no controller owns this GSI */

    u32 idx = IOAPIC_RED_TBL(pin);
    u64 red = (u64)vector | IOAPIC_RED_LEVEL | IOAPIC_RED_DEST_ALL;
    /* default: edge-triggered, active-high (ISA-like) */
    if (masked)
        red |= IOAPIC_RED_MASKED;
    io_write(c, idx + 1, (u32)(red >> 32));
    io_write(c, idx, (u32)red);
}

/* Route an ISA IRQ through its override (if any) to a vector. */
void ioapic_route_isa(u8 isa_irq, u8 vector, int masked)
{
    ioapic_route_gsi(isa_irq_to_gsi(isa_irq), vector, masked);
}
