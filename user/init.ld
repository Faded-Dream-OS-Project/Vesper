/* init.ld — flat user program layout, page aligned at 0x400000.
 * Copyright (c) Faded Dream OS Project. All rights reserved. */
ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.* COMMON) }
    _end = .;
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
