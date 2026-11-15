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

    /* Software enable + spurious vector 0xFF. (Do NOT touch reserved
       bits in the spurious register — a stray EOI-broadcast bit made
       QEMU's LAPIC swallow timer interrupts.) */
    *lapic_reg(APIC_REG_SPURIOUS) = APIC_SPIV_ENABLE | 0xFF;

    /* Map the LAPIC timer to vector 0x20+... we use 0x30 (48) so it
       stays clear of the legacy PIC remap range and exceptions. */
    *lapic_reg(APIC_REG_TIMER_DIV) = 0x3;   /* divide by 16 */
    *lapic_reg(APIC_REG_TIMER_LVT) = APIC_LVT_MASKED;   /* masked for now */
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

/* ---- IOAPIC ---- */

static u32 ioapic_read(u32 reg)
{
    *(volatile u32 *)((u8 *)ioapic + IOAPIC_REG_IOREGSEL) = reg;
    return *(volatile u32 *)((u8 *)ioapic + IOAPIC_REG_IOWIN);
}

static void ioapic_write(u32 reg, u32 val)
{
    *(volatile u32 *)((u8 *)ioapic + IOAPIC_REG_IOREGSEL) = reg;
    *(volatile u32 *)((u8 *)ioapic + IOAPIC_REG_IOWIN) = val;
}

u32 ioapic_pins(void)
{
    if (!ioapic)
        return 0;
    return ((ioapic_read(1) >> 16) & 0xFF) + 1;
}

void ioapic_route_gsi(u32 gsi, u8 vector, int masked)
{
    if (!ioapic)
        return;
    u32 idx = IOAPIC_RED_TBL(gsi - ioapic_gsi0);
    u64 red = (u64)vector | IOAPIC_RED_LEVEL | IOAPIC_RED_TRIGGER_LEVEL |
              IOAPIC_RED_DEST_ALL;
    if (masked)
        red |= IOAPIC_RED_MASKED;
    ioapic_write(idx + 1, (u32)(red >> 32));
    ioapic_write(idx, (u32)red);
}
