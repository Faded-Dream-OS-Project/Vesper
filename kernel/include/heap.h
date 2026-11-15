/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_HEAP_H
#define VESPER_HEAP_H

#include <common.h>

/* Kernel heap: a free-list allocator over virtually contiguous pages
   mapped on demand through the VMM.

   Block layout: [ header (16 bytes: magic, size) | payload ... ].
   Free blocks keep an inline forward pointer in their payload area. */

#define HEAP_MAGIC_FREE  0x4652454550455248ull   /* "HEERPF" */
#define HEAP_MAGIC_ALLOC 0x4C4C4F4345455248ull   /* "HEERCOLL" hmm */
#define HEAP_MAGIC_A     0x4147414C4C4F4341ull   /* "ALLOCA" marker */

#define KHEAP_START   0xffff888000100000ull
#define KHEAP_MAX     (64u << 20)   /* 64 MiB address span */

void kheap_init(void);
void *kmalloc(u64 size);
void kfree(void *ptr);
u64  kheap_used(void);

#endif /* VESPER_HEAP_H */
