/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R + X */
    rodata PT_LOAD FLAGS(4);   /* R     */
    data   PT_LOAD FLAGS(6);   /* R + W */
}

SECTIONS
{
    /* Limine convention: kernel is loaded at the higher half,
       base virtual address 0xFFFFFFFF80000000. */
    . = 0xFFFFFFFF80000000;

    .text : ALIGN(0x1000) {
        KEEP(*(.text .text.*))
    } :text

    /* Limine scans the loaded image for the request block. The input
       sections must keep their program order: base revision, start
       marker, requests, end marker. */
    .limine_requests : ALIGN(0x1000) {
        KEEP(*(.limine_requests.0))
        KEEP(*(.limine_requests.1))
        KEEP(*(.limine_requests.2))
        KEEP(*(.limine_requests.3))
        KEEP(*(.limine_requests.4))
        KEEP(*(.limine_requests.5))
        KEEP(*(.limine_requests.6))
        KEEP(*(.limine_requests.7))
        KEEP(*(.limine_requests.8))
        KEEP(*(.limine_requests.9))
        KEEP(*(.limine_requests.10))
        KEEP(*(.limine_requests.11))
        KEEP(*(.limine_requests.12))
        KEEP(*(.limine_requests.13))
        KEEP(*(.limine_requests))
    } :text

    .rodata : ALIGN(0x1000) {
        *(.rodata .rodata.*)
    } :rodata

    .data : ALIGN(0x1000) {
        *(.data .data.*)
    } :data

    /* .bss is its own NOBITS output section: zero-filled by the
       bootloader, NOT stored in the ELF file. Inlining .bss into .data
       (the old layout) turned the 4 MiB console shadow buffer into 4 MiB
       of FILE data — the kernel ELF ballooned from ~700 KiB (session 5)
       to 4.5 MiB (session 6), which real UEFI/Ventoy paths must copy
       byte by byte. Largest .bss consumers: the framebuffer shadow
       (4 MiB) and per-CPU structures. */
    .bss : ALIGN(0x1000) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}

