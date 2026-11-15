/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <lib.h>
#include <limine.h>
#include <pmm.h>

/* Bitmap PMM. Bit i == frame i (physical address i * 4096).
   bit set  = frame busy / not usable
   bit clear = frame free (usable and unallocated) */

static u8 bitmap[PMM_MAX_BITMAP_BYTES] __attribute__((aligned(8)));
static u64 bitmap_frames;      /* frames covered by the bitmap */
static u64 hhdm;               /* offset to convert phys -> virt */
static u64 free_count;
static u64 total_usable;

/* ---- bitmap helpers ---- */

static inline void bm_set(u64 frame)
{
    bitmap[frame >> 3] |= (u8)(1u << (frame & 7));
}

static inline void bm_clear(u64 frame)
{
    bitmap[frame >> 3] &= (u8)~(1u << (frame & 7));
}

static inline int bm_test(u64 frame)
{
    return (bitmap[frame >> 3] >> (frame & 7)) & 1;
}

void pmm_init(struct limine_memmap_response *memmap, u64 hhdm_offset)
{
    hhdm = hhdm_offset;

    if (!memmap) {
        /* No memory map: mark everything busy. */
        memset(bitmap, 0xFF, sizeof(bitmap));
        bitmap_frames = PMM_MAX_BITMAP_BYTES * 8;
        return;
    }

    /* Find the top of physical memory to size the bitmap. */
    u64 top = 0;
    for (u64 i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->base + e->length > top)
            top = e->base + e->length;
    }

    bitmap_frames = (top + PMM_FRAME_SIZE - 1) >> PMM_FRAME_SHIFT;
    if (bitmap_frames > PMM_MAX_BITMAP_BYTES * 8)
        bitmap_frames = PMM_MAX_BITMAP_BYTES * 8;   /* track first 512 MiB */

    /* Start with everything busy, then carve out usable frames. */
    memset(bitmap, 0xFF, (size_t)((bitmap_frames + 7) >> 3));

    free_count = 0;
    total_usable = 0;
    for (u64 i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        /* Clip the region to the tracked range, page-aligned. */
        u64 start = (e->base + PMM_FRAME_SIZE - 1) & ~(u64)(PMM_FRAME_SIZE - 1);
        u64 end = (e->base + e->length) & ~(u64)(PMM_FRAME_SIZE - 1);
        if (start >= end)
            continue;
        if (end > bitmap_frames << PMM_FRAME_SHIFT)
            end = bitmap_frames << PMM_FRAME_SHIFT;
        if (start >= end)
            continue;

        for (u64 p = start; p < end; p += PMM_FRAME_SIZE) {
            u64 frame = p >> PMM_FRAME_SHIFT;
            if (bm_test(frame)) {
                bm_clear(frame);
                free_count++;
            }
            total_usable++;
        }
    }
}

u64 pmm_alloc(void)
{
    for (u64 frame = 0; frame < bitmap_frames; frame++) {
        if (!bm_test(frame)) {
            bm_set(frame);
            free_count--;
            return frame << PMM_FRAME_SHIFT;
        }
    }
    return 0;   /* out of memory */
}

void pmm_free(u64 paddr)
{
    if (paddr & (PMM_FRAME_SIZE - 1))
        return;                     /* not frame-aligned: refuse */
    u64 frame = paddr >> PMM_FRAME_SHIFT;
    if (frame >= bitmap_frames)
        return;
    bm_clear(frame);
    free_count++;
}

void pmm_mark_busy(u64 paddr)
{
    u64 frame = paddr >> PMM_FRAME_SHIFT;
    if (frame >= bitmap_frames)
        return;
    if (!bm_test(frame)) {
        bm_set(frame);
        free_count--;
    }
}

u64 pmm_free_frames(void)
{
    return free_count;
}

u64 pmm_total_usable_frames(void)
{
    return total_usable;
}
