/* gdt_flush.S — GDT reload trampoline.
 * Copyright (c) Faded Dream OS Project. All rights reserved.
 * Source is viewable but may not be copied, modified, or redistributed
 * without permission.
 *
 * rdi = address of struct gdtr {u16 limit; u64 base}
 * si  = TSS selector
 *
 * The kernel keeps the selectors Limine booted us with (CS=0x28,
 * DS/SS=0x30); our GDT provides identical descriptors at those indices,
 * so no far jump is needed — only lgdt, data segment reload, and ltr.
 */
.section .text
.global gdt_flush
gdt_flush:
    lgdt (%rdi)
    mov $0x10, %ax         /* SEL_KDATA */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    ltr %si
    ret
