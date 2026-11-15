/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_PMM_H
#define VESPER_PMM_H

#include <common.h>

/* Physical memory manager.
   A single bitmap over the whole physical address space. Frames that
   fall inside USABLE memory-map regions are free unless allocated;
   everything else is permanently busy. */

/* One frame == one 4 KiB page (matches the VMM's page size). */
#define PMM_FRAME_SIZE 0x1000
#define PMM_FRAME_SHIFT 12

#define PMM_MAX_BITMAP_BYTES (64 * 1024)   /* 512 MiB tracked max */

void pmm_init(struct limine_memmap_response *memmap, u64 hhdm_offset);
u64  pmm_alloc(void);          /* returns physical address, 0 on failure */
void pmm_free(u64 paddr);
void pmm_mark_busy(u64 paddr);  /* remove one frame from the free pool */
u64  pmm_free_frames(void);
u64  pmm_total_usable_frames(void);

#endif /* VESPER_PMM_H */
