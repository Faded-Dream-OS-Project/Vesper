/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_APIC_H
#define VESPER_APIC_H

#include <common.h>

/* Local APIC (xAPIC, MMIO) and IOAPIC drivers.

   The LAPIC base comes from the MADT (validated 0xFEE00000 on QEMU).
   All register access is MMIO through the HHDM. */

#define APIC_REG_APICID      0x020
#define APIC_REG_EOI         0x0B0
#define APIC_REG_SPURIOUS    0x0F0
#define APIC_REG_ICR_LO      0x300
#define APIC_REG_ICR_HI      0x310
#define APIC_REG_TIMER_LVT   0x320
#define APIC_REG_TIMER_ICR   0x380   /* initial count */
#define APIC_REG_TIMER_CCR   0x390   /* current count */
#define APIC_REG_TIMER_DIV   0x3E0

#define APIC_LVT_MASKED   (1u << 16)
#define APIC_LVT_PERIODIC (1u << 15)
#define APIC_LVT_VECTOR   0xFFu

#define APIC_SPIV_ENABLE  (1u << 8)
#define APIC_SPIV_EOI_BROADCAST (1u << 12)

/* IOAPIC registers (indirect via the IOREGSEL window). */
#define IOAPIC_REG_IOREGSEL 0x00
#define IOAPIC_REG_IOWIN    0x10

#define IOAPIC_RED_TBL(n)   (0x10 + 2 * (n))
#define IOAPIC_RED_MASKED   (1ull << 16)
#define IOAPIC_RED_LEVEL    (1ull << 15)
#define IOAPIC_RED_TRIGGER_LEVEL (1ull << 14)
#define IOAPIC_RED_DEST_ALL (0xFFull << 56)

void apic_init(u64 lapic_base, u64 ioapic_base, u32 ioapic_gsi_base);
void apic_eoi(void);
u64  apic_ticks(void);       /* LAPIC timer current count (for tests) */
u32  apic_timer_vector(void);

/* Timer helpers used by timer.c. */
void lapic_set_timer(u8 vector, int masked, u64 initial_count);
void lapic_set_timer_periodic(u8 vector, u8 divider_code, u64 initial_count);
u32  lapic_read_ccr(void);
void lapic_timer_dump(void);   /* debug helper */
void lapic_timer_rearm(u64 initial_count);
u64  lapic_timer_icr(void);

void ioapic_route_gsi(u32 gsi, u8 vector, int masked);
void ioapic_route_isa(u8 isa_irq, u8 vector, int masked);
u32  isa_irq_to_gsi(u8 isa_irq);
u32  ioapic_pins(void);

#endif /* VESPER_APIC_H */
