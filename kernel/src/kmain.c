/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <lib.h>
#include <limine.h>
#include <pmm.h>
#include <selftest.h>
#include <vmm.h>

/* ---- Limine requests ----
   Marked sections bracket the request block; the bootloader scans it
   and fills in the response pointers. */

__attribute__((used, section(".limine_requests.0")))
static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests.1")))
static volatile uint64_t limine_requests_start[4] =
    LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests.2")))
static struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.3")))
static struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.4")))
static struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.5")))
static struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .stack_size = 0x10000,   /* 64 KiB */
};

__attribute__((used, section(".limine_requests.6")))
static struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.7")))
static struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.8")))
static volatile uint64_t limine_requests_end[2] =
    LIMINE_REQUESTS_END_MARKER;

/* ---- kernel entry ---- */

void kmain(void)
{
    serial_init();

    struct limine_framebuffer_response *fb_response =
        framebuffer_request.response;
    if (fb_response && fb_response->framebuffer_count > 0)
        fb_init(fb_response->framebuffers[0]);

    kprintf("\n");
    kprintf("Vesper " VESPER_VERSION " (kernel of Faded Dream OS)\n");

    struct limine_bootloader_info_response *bl =
        bootloader_info_request.response;
    if (bl)
        kprintf("bootloader: %s %s\n", bl->name, bl->version);

    struct limine_hhdm_response *hhdm = hhdm_request.response;
    if (hhdm)
        kprintf("hhdm offset: %#lx\n", hhdm->offset);

    struct limine_executable_address_response *ea =
        executable_address_request.response;
    if (ea)
        kprintf("kernel: phys %#lx, virt %#lx\n",
                ea->physical_base, ea->virtual_base);

    struct limine_memmap_response *mm = memmap_request.response;
    if (mm) {
        u64 usable = 0;
        for (u64 i = 0; i < mm->entry_count; i++) {
            if (mm->entries[i]->type == LIMINE_MEMMAP_USABLE)
                usable += mm->entries[i]->length;
        }
        kprintf("memory map: %lu entries, %lu MiB usable\n",
                mm->entry_count, usable >> 20);
    }

    gdt_init();
    kprintf("GDT/TSS loaded.\n");
    idt_init();
    kprintf("IDT loaded (256 gates).\n");

    /* ---- Milestone 3 ---- */
    pmm_init(mm, hhdm_request.response->offset);
    kprintf("PMM: %lu/%lu frames free (%lu MiB RAM tracked)\n",
            pmm_free_frames(), pmm_total_usable_frames(),
            pmm_total_usable_frames() >> 8);

    vmm_init(hhdm_request.response->offset,
             executable_address_request.response);
    kprintf("VMM: own PML4 active at %#llx\n",
            (unsigned long long)(read_cr3() & ~0xFFFull));

    /* M3 VMM self-test. */

    {
        struct test_ctx t;
        u64 f1, f2;
        u64 before = pmm_free_frames();

        test_begin(&t, "pmm");
        f1 = pmm_alloc();
        test_check(&t, "alloc returns nonzero", f1 != 0);
        test_check(&t, "alloc is page aligned", (f1 & 0xFFF) == 0);
        test_check(&t, "free count drops by 1",
                   pmm_free_frames() == before - 1);
        f2 = pmm_alloc();
        test_check(&t, "second alloc differs", f2 != f1 && f2 != 0);
        pmm_free(f1);
        test_check(&t, "free restores count",
                   pmm_free_frames() == before - 1);
        pmm_free(f2);
        test_check(&t, "second free restores count",
                   pmm_free_frames() == before);
        test_summary(&t);
    }

    {
        struct test_ctx t;
        u64 va = VMM_KSCRATCH_BASE;
        u64 pa = pmm_alloc();
        u64 got = 0;

        test_begin(&t, "vmm");
        test_check(&t, "map new page", vmm_map(va, pa, VMM_WRITABLE) == 0);
        test_check(&t, "mapped reports phys", vmm_mapped(va, &got) == 0
                   && got == pa);
        *(volatile u64 *)va = 0x5154504552565350ull;
        test_check(&t, "write via mapping works",
                   *(volatile u64 *)vmm_phys_to_hhdm(pa) ==
                   0x5154504552565350ull);
        test_check(&t, "unmap page", vmm_unmap(va) == 0);
        test_check(&t, "unmapped reports unmapped", vmm_mapped(va, NULL) != 0);
        test_check(&t, "double unmap fails", vmm_unmap(va) != 0);
        {
            u64 after_free = pmm_free_frames();
            pmm_free(pa);
            /* freeing the data frame returns exactly +1; the page-table
               frames walk() created stay allocated by design */
            test_check(&t, "pmm count +1 after data-frame free",
                       pmm_free_frames() == after_free + 1);
        }
        test_summary(&t);
    }

    kprintf("M2 self-test: raising divide-by-zero...\n");
    {
        volatile u64 num = 1, denom = 0;
        __asm__ volatile ("divq %2" : "+a"(num) : "d"(num), "m"(denom));
    }

    kprintf("M1: serial and framebuffer console online.\n");
    kprintf("Halting.\n");

    for (;;)
        halt();
}
