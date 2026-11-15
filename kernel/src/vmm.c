/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <limine.h>
#include <pmm.h>
#include <vmm.h>

/* 4-level paging.
   Entry indices: PML4[47:39] PDPT[38:30] PD[29:21] PT[20:12]. */

static u64 hhdm;
static u64 kernel_virt_base;
static u64 kernel_phys_base;
static u64 kernel_pml4;   /* the kernel's own PML4 (physical) */

/* Physical -> HHDM virtual address translation. */
static inline u64 phys_to_virt(u64 p)
{
    return p + hhdm;
}

/* Read/write a physical address through the HHDM. */
static inline u64 *pml_entry(u64 table_phys, u64 idx)
{
    return (u64 *)phys_to_virt(table_phys) + idx;
}

/* Allocate a zeroed page table page; returns physical address, 0 on
   failure. The frame comes back zeroed from the PMM bitmap because
   usable-region frames are zero-filled by the platform on x86 (and
   Limine reports them as usable). Still zero it defensively. */
static u64 alloc_table(void)
{
    u64 p = pmm_alloc();
    if (p == 0)
        return 0;
    u64 *v = (u64 *)phys_to_virt(p);
    for (int i = 0; i < 512; i++)
        v[i] = 0;
    return p;
}

/* Walk the hierarchy, creating intermediate levels as requested.
   user=1: created intermediate entries carry VMM_USER. */
static u64 *walk_gen(u64 pml4_phys, u64 virt, int create, int user)
{
    u64 idx = (virt >> 39) & 0x1FF;
    u64 *e = pml_entry(pml4_phys, idx);

    u64 uflag = user ? VMM_USER : 0;
    u64 pdpt;
    if (*e & VMM_PRESENT) {
        pdpt = *e & 0x000FFFFFFFFFF000ull;
    } else {
        if (!create)
            return NULL;
        pdpt = alloc_table();
        if (pdpt == 0)
            return NULL;
        *e = pdpt | VMM_PRESENT | VMM_WRITABLE | uflag;
    }

    idx = (virt >> 30) & 0x1FF;
    e = pml_entry(pdpt, idx);
    u64 pd;
    if (*e & VMM_PRESENT) {
        pd = *e & 0x000FFFFFFFFFF000ull;
    } else {
        if (!create)
            return NULL;
        pd = alloc_table();
        if (pd == 0)
            return NULL;
        *e = pd | VMM_PRESENT | VMM_WRITABLE | uflag;
    }

    idx = (virt >> 21) & 0x1FF;
    e = pml_entry(pd, idx);
    u64 pt;
    if (*e & VMM_PRESENT) {
        pt = *e & 0x000FFFFFFFFFF000ull;
    } else {
        if (!create)
            return NULL;
        pt = alloc_table();
        if (pt == 0)
            return NULL;
        *e = pt | VMM_PRESENT | VMM_WRITABLE | uflag;
    }

    return pml_entry(pt, (virt >> 12) & 0x1FF);
}

static u64 *walk(u64 pml4_phys, u64 virt, int create)
{
    return walk_gen(pml4_phys, virt, create, 0);
}

static u64 *walk_user(u64 pml4_phys, u64 virt, int create)
{
    return walk_gen(pml4_phys, virt, create, 1);
}

void vmm_init(u64 hhdm_offset, struct limine_executable_address_response *ea)
{
    hhdm = hhdm_offset;
    kernel_virt_base = ea->virtual_base;
    kernel_phys_base = ea->physical_base;

    /* Build our own PML4 with the two mappings the running kernel
       needs: the HHDM (identity) and the higher-half kernel image
       (virt <-> phys NOT congruent modulo 2 MiB, so plain 4 KiB
       pages are used for the image). */
    u64 pml4 = alloc_table();
    if (pml4 == 0)
        panic("vmm: cannot allocate PML4");

    /* HHDM: entry 256 (0xffff800000000000 >> 39 == 256) points at a
       PDPT of 512 x 1 GiB huge pages covering ALL of physical memory
       (framebuffer and other high regions included).
       (Huge pages are only legal at PDPT/PD level, never in the PML4.) */
    u64 pdpt_hhdm = alloc_table();
    if (pdpt_hhdm == 0)
        panic("vmm: cannot allocate HHDM PDPT");
    u64 *e = pml_entry(pml4, 256);
    *e = pdpt_hhdm | VMM_PRESENT | VMM_WRITABLE;
    for (u64 i = 0; i < 512; i++) {
        e = pml_entry(pdpt_hhdm, i);
        *e = (i << 30) | VMM_PRESENT | VMM_WRITABLE | VMM_NOEXEC | (1ull << 7);
    }

    /* Kernel image: 16 MiB of 4 KiB pages from kernel_virt_base to
       kernel_phys_base (covers text, rodata, data/bss, TSS, IDT and
       the PMM bitmap with room to grow). */
    for (u64 off = 0; off < 16ull * 1024 * 1024; off += VMM_PAGE_SIZE) {
        u64 *pt = walk(pml4, kernel_virt_base + off, 1);
        if (!pt)
            panic("vmm: kernel image mapping failed");
        *pt = (kernel_phys_base + off) | VMM_PRESENT | VMM_WRITABLE;
    }

    /* Everything below uses our own walker while CR3 still points at
       Limine's tables, so all HHDM accesses remain valid. */
    (void)e;

    /* Switch to our tables. */
    kernel_pml4 = pml4;
    write_cr3(pml4);
}

u64 vmm_kernel_pml4(void)
{
    return kernel_pml4;
}

void vmm_load_pml4(u64 pml4)
{
    write_cr3(pml4);
}

/* Map a page into an EXPLICIT PML4 (not CR3): used for per-process
   user address spaces. Flags as vmm_map.

   Intermediate page tables created during the walk MUST carry the
   USER bit too — ring 3 cannot traverse supervisor-only entries, and
   a missing USER bit on the PDPT/PD/PT silently faults the user task
   at its first instruction. */
int vmm_map_table(u64 pml4_phys, u64 virt, u64 phys, u64 flags)
{
    u64 *e = walk_user(pml4_phys, virt, 1);
    if (!e)
        return -1;
    if (*e & VMM_PRESENT)
        return -1;                       /* already mapped */
    *e = (phys & 0x000FFFFFFFFFF000ull) | VMM_PRESENT | flags;
    return 0;
}

/* Build a fresh PML4 sharing the kernel half (entries 256..511
   copied from the kernel PML4) with an empty user half: the base of
   every user process address space. Returns the PML4 physical
   address, 0 on failure. */
u64 vmm_create_user_pml4(void)
{
    u64 pml4 = alloc_table();
    if (pml4 == 0)
        return 0;
    /* The kernel-half copy must be atomic: a timer preempting the
       loop mid-copy once left a half-populated PML4 that faulted on
       the very next kernel instruction fetch after CR3 loaded it. */
    cli();
    u64 *src = (u64 *)phys_to_virt(kernel_pml4);
    u64 *dst = (u64 *)phys_to_virt(pml4);
    for (int i = 256; i < 512; i++)
        dst[i] = src[i];
    sti();
    return pml4;
}

u64 vmm_phys_to_hhdm(u64 paddr)
{
    return paddr + hhdm;
}

int vmm_map(u64 virt, u64 phys, u64 flags)
{
    u64 pml4 = read_cr3() & 0x000FFFFFFFFFF000ull;
    u64 *e = walk(pml4, virt, 1);
    if (!e)
        return -1;
    if (*e & VMM_PRESENT)
        return -1;                       /* already mapped */
    *e = (phys & 0x000FFFFFFFFFF000ull) | VMM_PRESENT | flags;
    return 0;
}

int vmm_unmap(u64 virt)
{
    u64 pml4 = read_cr3() & 0x000FFFFFFFFFF000ull;
    u64 *e = walk(pml4, virt, 0);
    if (!e || !(*e & VMM_PRESENT))
        return -1;
    *e = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

u64 vmm_hhdm_offset(void)
{
    return hhdm;
}

/* OR extra flags into the LEAF (PTE) entry of an existing mapping.
   Returns 0 if the leaf exists (and was patched), -1 otherwise. Used
   by the framebuffer console to select a PAT entry via the PWT bit. */
int vmm_set_leaf_flags(u64 virt, u64 flags)
{
    u64 pml4 = read_cr3() & 0x000FFFFFFFFFF000ull;
    u64 *e = walk(pml4, virt, 0);
    if (!e || !(*e & VMM_PRESENT))
        return -1;
    *e |= flags;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int vmm_mapped(u64 virt, u64 *phys_out)
{
    u64 pml4 = read_cr3() & 0x000FFFFFFFFFF000ull;
    u64 *e = walk(pml4, virt, 0);
    if (!e || !(*e & VMM_PRESENT))
        return -1;
    if (phys_out)
        *phys_out = *e & 0x000FFFFFFFFFF000ull;
    return 0;
}
