/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <lib.h>
#include <limine.h>

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

    kprintf("M1: serial and framebuffer console online.\n");
    kprintf("Halting.\n");

    for (;;)
        halt();
}
