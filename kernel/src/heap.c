/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <heap.h>
#include <lib.h>
#include <pmm.h>
#include <vmm.h>

/* Kernel heap: free-list allocator with splitting and coalescing.
   Pages are allocated from the PMM and mapped at KHEAP_START on
   demand; blocks carry an 16-byte header (magic, size-and-state). */

#define HDR_SIZE     16
#define MIN_BLOCK    (HDR_SIZE + 16)   /* header + room for a next ptr */

struct block {
    u64 magic;    /* HEAP_MAGIC_FREE / HEAP_MAGIC_ALLOC */
    u64 size;     /* total block size including header */
    struct block *next_free;   /* valid when free */
};

static struct block *free_head;
static u64 heap_brk;       /* next virtual address to grow into */
static u64 heap_used_bytes;

/* Map one more page of heap address space. Returns the new page's
   virtual start, or 0 on failure. */
static u64 grow(u64 need)
{
    u64 bytes = (need + VMM_PAGE_SIZE - 1) & ~(u64)(VMM_PAGE_SIZE - 1);
    u64 start = heap_brk;

    if (start + bytes - KHEAP_START > KHEAP_MAX)
        return 0;
    for (u64 v = start; v < start + bytes; v += VMM_PAGE_SIZE) {
        u64 p = pmm_alloc();
        if (p == 0)
            return 0;
        if (vmm_map(v, p, VMM_WRITABLE) != 0) {
            pmm_free(p);
            return 0;
        }
        memset((void *)v, 0, VMM_PAGE_SIZE);
    }
    heap_brk = start + bytes;
    return start;
}

void kheap_init(void)
{
    heap_brk = KHEAP_START;
    heap_used_bytes = 0;

    /* Seed with one page. */
    u64 page = grow(VMM_PAGE_SIZE);
    if (page == 0)
        panic("kheap: initial page allocation failed");

    free_head = (struct block *)page;
    free_head->magic = HEAP_MAGIC_FREE;
    free_head->size = VMM_PAGE_SIZE;
    free_head->next_free = NULL;
}

/* Split a free block if the remainder is large enough. */
static void split(struct block *b, u64 need)
{
    if (b->size >= need + MIN_BLOCK) {
        struct block *rest = (struct block *)((u8 *)b + need);
        rest->magic = HEAP_MAGIC_FREE;
        rest->size = b->size - need;
        rest->next_free = b->next_free;
        b->size = need;
        b->next_free = rest;
    }
}

void *kmalloc(u64 size)
{
    if (size == 0)
        return NULL;

    u64 need = (size + HDR_SIZE + 7) & ~(u64)7;
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    /* First fit. */
    struct block **pp = &free_head;
    for (struct block *b = free_head; b; b = b->next_free) {
        if (b->magic != HEAP_MAGIC_FREE)
            panic("kheap: corrupt free list at %p", b);
        if (b->size >= need) {
            split(b, need);
            *pp = b->next_free;     /* unlink allocated block */
            b->magic = HEAP_MAGIC_ALLOC;
            b->next_free = NULL;
            heap_used_bytes += b->size;
            return (u8 *)b + HDR_SIZE;
        }
        pp = &b->next_free;
    }

    /* No fit: grow the heap and add a new free block, then retry
       through the free list (coalescing with the tail if adjacent). */
    u64 page = grow(need);
    if (page == 0)
        return NULL;

    struct block *nb = (struct block *)page;
    nb->magic = HEAP_MAGIC_FREE;
    nb->size = heap_brk - page;
    nb->next_free = NULL;

    /* Coalesce with an adjacent free tail. */
    if (free_head) {
        struct block *tail = free_head;
        while (tail->next_free)
            tail = tail->next_free;
        if ((u8 *)tail + tail->size == (u8 *)nb) {
            tail->size += nb->size;
        } else {
            tail->next_free = nb;
        }
    } else {
        free_head = nb;
    }
    return kmalloc(size);
}

void kfree(void *ptr)
{
    if (ptr == NULL)
        return;

    struct block *b = (struct block *)((u8 *)ptr - HDR_SIZE);
    if (b->magic != HEAP_MAGIC_ALLOC)
        panic("kheap: kfree on bad pointer %p (magic %#llx)", ptr,
              (unsigned long long)b->magic);

    heap_used_bytes -= b->size;
    b->magic = HEAP_MAGIC_FREE;

    /* Insert address-ordered and coalesce both neighbours. */
    struct block **pp = &free_head;
    while (*pp && (u8 *)*pp < (u8 *)b)
        pp = &(*pp)->next_free;
    b->next_free = *pp;
    *pp = b;

    if (b->next_free && (u8 *)b + b->size == (u8 *)b->next_free) {
        b->size += b->next_free->size;
        b->next_free = b->next_free->next_free;
    }
    struct block *prev = free_head;
    if (prev != b) {
        while (prev->next_free != b)
            prev = prev->next_free;
        if ((u8 *)prev + prev->size == (u8 *)b) {
            prev->size += b->size;
            prev->next_free = b->next_free;
        }
    }
}

u64 kheap_used(void)
{
    return heap_used_bytes;
}
