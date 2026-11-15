/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_VMM_H
#define VESPER_VMM_H

#include <common.h>

struct limine_executable_address_response;

/* Virtual memory manager: 4-level x86_64 paging.

   - Limine's higher-half direct map (HHDM) stays as the bootloader
     built it; we use it to read/write physical memory.
   - vmm_init builds OUR OWN kernel PML4 and switches CR3 to it:
     the kernel image + HHDM are mapped identically to what Limine
     provided (higher-half kernel at 0xffffffff80000000, HHDM at the
     same offset the bootloader chose).
   - vmm_map/vmm_unmap manage the kernel virtual region
     0xffff888000000000.. (the "kernel scratch" area we allocate
     page tables from). */

#define VMM_PAGE_SIZE   0x1000
#define VMM_PRESENT     (1ull << 0)
#define VMM_WRITABLE    (1ull << 1)
#define VMM_USER        (1ull << 2)
#define VMM_NOEXEC      (1ull << 63)

/* Kernel scratch area for mappings (below HHDM, above user space). */
#define VMM_KSCRATCH_BASE 0xffff888000000000ull

/* Dedicated write-combining window for the framebuffer (session 7):
   4 KiB PTEs created by vmm_map_wc() with PWT set (=> PAT entry 1 =
   WC after fb.c programs the PAT MSR). Sized 16 MiB, above the
   kernel scratch area. */
#define VMM_FB_WC_BASE   0xffff889000000000ull

void vmm_init(u64 hhdm_offset, struct limine_executable_address_response *ea);
u64  vmm_hhdm_offset(void);
int  vmm_set_leaf_flags(u64 virt, u64 flags);   /* OR flags into leaf PTE */
int  vmm_map_wc(u64 phys);                      /* map one WC page into the FB window; 0 = ok */
int  vmm_entry_is_big(const volatile u64 *e);   /* PS-set leaf (1 GiB/2 MiB) */

/* Physical address -> HHDM virtual address (bootloader-provided map). */
u64 vmm_phys_to_hhdm(u64 paddr);
int  vmm_map(u64 virt, u64 phys, u64 flags);   /* 0 = ok, -1 = error */
int  vmm_map_table(u64 pml4_phys, u64 virt, u64 phys, u64 flags);
u64  vmm_kernel_pml4(void);
void vmm_load_pml4(u64 pml4);
u64  vmm_create_user_pml4(void);
int  vmm_unmap(u64 virt);                      /* 0 = ok, -1 = not mapped */
int  vmm_mapped(u64 virt, u64 *phys_out);      /* 0 = mapped */

#endif /* VESPER_VMM_H */
