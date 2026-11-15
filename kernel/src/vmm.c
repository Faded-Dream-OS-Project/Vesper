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
   user=1: created intermediate entries carry VMM_USER.

   Big-page safety (session 7): a PDPT/PD entry with the PS bit (bit 7)
   set IS the leaf — the remaining address bits are a page base, not a
   next-level table pointer. Descending anyway walks into garbage
   (frames, MMIO holes), which is fatal on real firmware whose
   bootloader tables map RAM with 1 GiB leaves. walk_gen therefore
   returns the big-page entry itself, so leaf-flag helpers (PAT
   write-combining) can either patch it deliberately or refuse. */
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
    if (*e & VMM_PRESENT && (*e & (1ull << 7)))   /* 1 GiB leaf */
        return e;
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
    if (*e & VMM_PRESENT && (*e & (1ull << 7)))   /* 2 MiB leaf */
        return e;
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

/* Is the entry returned by a walk a big-page (PDPT/PD) leaf rather
   than a 4 KiB PTE? Kept for call-site clarity; walk_gen returning a
   PS-set entry IS the leaf, and vmm_set_leaf_flags refuses to patch
   it (bit 7 is the size encoding there, not a cache hint). */
int vmm_entry_is_big(const volatile u64 *e)
{
    return e != NULL && (*e & VMM_PRESENT) && (*e & (1ull << 7));
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
       (Huge pages are only legal at PDPT/PD level, never in the PML4.)
       NOTE (session 7): the framebuffer lives here, so if the console
       wants to PAT-write-combine the framebuffer it must be able to
       patch HHDM leaves — big leaves cannot be PAT-patched safely
       (bit 7 is the size encoding), so the HHDM is created as a
       2-level structure: 1 GiB big leaves everywhere EXCEPT the one
       GiB slot containing the framebuffer, which is a full PD of 2 MiB
       leaves (patchable: PAT works on 2 MiB leaves through the
       PAT/PWT/PWT bits 12..7? no — cleanest is 4 KiB, but 2 MiB
       leaves accept PWT and reserve the encoding safely, see SDM
       4.5: PAT index comes from bits 12,7,4,3 (PAT, PWT, PCD... )).
       Simpler and proven: the console patch path (vmm_set_leaf_flags)
       refuses big leaves; fb.c keeps a PERMANENT direct 4 KiB mapping
       of the framebuffer in KSCRATCH (its own page tables, patched
       at creation time). Done in fb.c — see map_fb_write_combining. */
    u64 pdpt_hhdm = alloc_table();
    if (pdpt_hhdm == 0)
        panic("vmm: cannot allocate HHDM PDPT");
    u64 *e = pml_entry(pml4, 256);
    *e = pdpt_hhdm | VMM_PRESENT | VMM_WRITABLE;
    for (u64 i = 0; i < 512; i++) {
        e = pml_entry(pdpt_hhdm, i);
        *e = (i << 30) | VMM_PRESENT | VMM_WRITABLE | VMM_NOEXEC | (1ull << 7);
    }

    /* Kernel image: 32 MiB of 4 KiB pages from kernel_virt_base to
       kernel_phys_base (covers text, rodata, data, the 16 MiB .bss
       shadow buffer, TSS, IDT and the PMM bitmap with room to grow;
       session 7: was 16 MiB, extended when the console shadow grew to
       cover 2560x1600 panels). */
    for (u64 off = 0; off < 32ull * 1024 * 1024; off += VMM_PAGE_SIZE) {
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

/* Map physical page `phys` into the dedicated framebuffer WC window
   at VMM_FB_WC_BASE+next (sequential allocator, up to 16 MiB). PTEs
   carry PWT (0x08): with the PAT MSR programmed so entry 1 = WC, a
   PTE with PWT and no PAT bit selects entry 1 => write-combining.
   Called by fb.c AFTER vmm_init (our own PML4 is active), so the walk
   only ever sees tables we created — no bootloader-table assumptions
   (the session-6 code patched Limine's tables and dereferenced
   big-page leaves as table pointers: unrecoverable on real firmware). */
int vmm_map_wc(u64 phys)
{
    static u64 next;   /* 0 = start of the window */
    static int inited;
    if (!inited) {
        next = VMM_FB_WC_BASE;
        inited = 1;
    }
    if (next >= VMM_FB_WC_BASE + (16ull << 20))
        return -1;   /* window exhausted: framebuffer > 16 MiB */
    u64 *e = walk(read_cr3() & 0x000FFFFFFFFFF000ull, next, 1);
    if (!e)
        return -1;
    if (*e & VMM_PRESENT)
        return -1;
    *e = (phys & 0x000FFFFFFFFFF000ull) | VMM_PRESENT | VMM_WRITABLE |
         0x08ull /* PWT */;
    next += VMM_PAGE_SIZE;
    return 0;
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
   Returns 0 if a 4 KiB leaf exists (and was patched), -1 otherwise
   (unmapped, or a big-page leaf — patching the PWT bit on 1 GiB/2 MiB
   leaves is unsafe: bit 7 is the size encoding there, not a cache
   hint). Used by the framebuffer console to select a PAT entry via
   the PWT bit; big leaves are deliberately refused. */
int vmm_set_leaf_flags(u64 virt, u64 flags)
{
    u64 pml4 = read_cr3() & 0x000FFFFFFFFFF000ull;
    u64 *e = walk(pml4, virt, 0);
    if (!e || !(*e & VMM_PRESENT))
        return -1;
    if (*e & (1ull << 7))   /* big-page leaf: refuse, not a PTE */
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
