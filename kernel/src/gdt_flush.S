/* gdt_flush.S — GDT reload trampoline.
 * Copyright (c) Faded Dream OS Project. All rights reserved.
 * Source is viewable but may not be copied, modified, or redistributed
 * without permission.
 *
 * rdi = address of struct gdtr {u16 limit; u64 base}
 * si  = TSS selector
 *
 * Session 8: the GDT is ours alone; the caller may still be executing
 * on whatever CS the bootloader left (Limine: 0x28). After lgdt we do
 * a far return onto SEL_KCODE (0x08, DPL 0) so the running CS points
 * at THIS table's kernel code64 descriptor, reload the data selectors,
 * and ltr. (A plain `ret` never reloads CS; executing on a stale
 * selector is architecturally permitted, but a clean reload removes
 * any dependence on the previous table.)
 */
.section .text
.global gdt_flush
gdt_flush:
    lgdt (%rdi)

    /* far return: push kernel code selector + return address */
    leaq 1f(%rip), %rax
    pushq $0x08               /* SEL_KCODE */
    pushq %rax
    lretq
1:
    mov $0x10, %ax            /* SEL_KDATA */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss              /* reload SS too: Limine may leave SS=0x30,
                                 and gdt[6] (0x30) is a USER descriptor —
                                 a kernel push through a DPL3 SS is #GP */
    ltr %si
    ret
