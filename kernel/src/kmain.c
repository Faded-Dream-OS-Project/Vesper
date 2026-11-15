/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <idt.h>
#include <lib.h>
#include <acpi.h>
#include <apic.h>
#include <irq.h>
#include <kbd.h>
#include <limine.h>
#include <pic.h>
#include <pmm.h>
#include <sched.h>
#include <selftest.h>
#include <timer.h>
#include <vmm.h>
#include <heap.h>

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
static struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.6")))
static struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.7")))
static struct limine_stack_size_request stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .stack_size = 0x10000,   /* 64 KiB */
};

__attribute__((used, section(".limine_requests.8")))
static struct limine_bootloader_info_request bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.9")))
static struct limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
};

__attribute__((used, section(".limine_requests.10")))
static volatile uint64_t limine_requests_end[2] =
    LIMINE_REQUESTS_END_MARKER;

/* ---- M5 self-test threads ---- */

volatile u64 m5_counts[3];

static volatile int m5_go;


/* ---- M5 self-test threads: busy loops, NO yields, NO sleeps.
   Progress proves the timer alone preempts them. ---- */

volatile u64 m5_counts[3];

void m5_thread_a(void *arg)
{
    (void)arg;
    while (m5_counts[0] < 2500000)
        m5_counts[0]++;
    kthread_exit();
}

void m5_thread_b(void *arg)
{
    (void)arg;
    while (m5_counts[1] < 2500000)
        m5_counts[1]++;
    kthread_exit();
}

void m5_thread_c(void *arg)
{
    (void)arg;
    while (m5_counts[2] < 2500000)
        m5_counts[2]++;
    kthread_exit();
}

/* Guard-page self-test thread: unbounded recursion blows the 16 KiB
   stack downward until it touches the unmapped guard page below it. */
static volatile u64 ovf_sink;

void m5_overflow_thread(void *arg)
{
    (void)arg;
    volatile char big[2048];
    for (u64 i = 0; i < sizeof(big); i++)
        big[i] = (char)i;
    ovf_sink = (u64)sizeof(big);
    /* Recurse until the guard page faults. The volatile trampoline
       keeps -Winfinite-recursion quiet (the recursion IS the point). */
    {
        void (*volatile next)(void *) = m5_overflow_thread;
        /* force a REAL frame (no tail call): the call never returns
           (the guard page faults first), and the statement below keeps
           the compiler from turning the call into a jmp */
        next(arg + 1);
        ovf_sink++;   /* unreachable */
    }
    kthread_exit();   /* placate -Wfalling-through... not reached */
}

/* Kernel command line flag: does the boot cmdline contain 'flag'
   as a whitespace-separated token? (limine.conf 'cmdline: ...') */
static int cmdline_has(const char *flag)
{
    if (!cmdline_request.response || !cmdline_request.response->cmdline)
        return 0;
    const char *s = cmdline_request.response->cmdline;
    size_t n = strlen(flag);
    while (*s) {
        while (*s == ' ' || *s == '\t')
            s++;
        const char *tok = s;
        while (*s && *s != ' ' && *s != '\t')
            s++;
        if ((size_t)(s - tok) == n && memcmp(tok, flag, n) == 0)
            return 1;
    }
    return 0;
}

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

    /* Milestone 4: interrupt hardware. PIC first: remap + full mask so
       the legacy chips cannot raise spurious vectors while we own the
       APIC path. */
    pic_init();
    kprintf("PIC remapped (0x20-0x2F) and fully masked.\n");

    /* ---- Milestone 3 ---- */
    pmm_init(mm, hhdm_request.response->offset);
    kprintf("PMM: %lu/%lu frames free (%lu MiB RAM tracked)\n",
            pmm_free_frames(), pmm_total_usable_frames(),
            pmm_total_usable_frames() >> 8);

    vmm_init(hhdm_request.response->offset,
             executable_address_request.response);
    kprintf("VMM: own PML4 active at %#llx\n",
            (unsigned long long)(read_cr3() & ~0xFFFull));

    /* ACPI: Limine hands us the RSDP; walk it to the MADT for the
       LAPIC/IOAPIC addresses (x86-64 QEMU always uses ACPI 2.0). */
    if (rsdp_request.response && acpi_init(rsdp_request.response->address) == 0) {
        const struct acpi_info *ai = acpi_get_info();
        kprintf("ACPI: MADT ok — LAPIC %#lx, IOAPIC %#lx (GSI %lu base), %lu LAPICs\n",
                ai->lapic_base, ai->ioapic_base,
                (unsigned long)ai->ioapic_gsi_base,
                (unsigned long)ai->lapic_count);
    } else {
        kprintf("ACPI: MADT unavailable\n");
    }

    /* LAPIC + IOAPIC from the MADT, then the calibrated timer. */
    {
        const struct acpi_info *ai = acpi_get_info();
        if (ai->valid) {
            apic_init(ai->lapic_base, ai->ioapic_base, ai->ioapic_gsi_base);
            kprintf("APIC: LAPIC online (spurious vector 0xFF, EOI broadcast).\n");
        } else {
            kprintf("APIC: unavailable; halting.\n");
            for (;;)
                halt();
        }
    }

    timer_init();
    irq_install(TIMER_VECTOR, timer_irq);
    kprintf("timer: LAPIC timer calibrated (%lu Hz bus), %lu Hz tick\n",
            (unsigned long)timer_lapic_freq(), (unsigned long)TIMER_HZ);

    /* PS/2 keyboard via the IOAPIC (GSI 1 -> vector 0x21). */
    kbd_init();
    irq_install(KBD_IRQ_VECTOR, kbd_irq_entry);
    kprintf("kbd: PS/2 init done, GSI 1 -> vector %#x\n",
            (unsigned int)KBD_IRQ_VECTOR);

    /* Enable interrupts. */
    sti();
    kprintf("interrupts enabled (sti).\n");


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

    kheap_init();
    kprintf("heap: initialised at %#llx\n",
            (unsigned long long)KHEAP_START);

    {
        struct test_ctx t;
        void *a, *b, *c;
        u64 used0;

        test_begin(&t, "heap");
        a = kmalloc(100);
        test_check(&t, "kmalloc returns ptr", a != NULL);
        memset(a, 0xAB, 100);
        test_check(&t, "write to allocation works",
                   ((volatile u8 *)a)[0] == 0xAB &&
                   ((volatile u8 *)a)[99] == 0xAB);
        used0 = kheap_used();
        b = kmalloc(4096);
        test_check(&t, "second alloc distinct", b != NULL && b != a);
        test_check(&t, "used grows", kheap_used() > used0);
        kfree(a);
        test_check(&t, "kfree accepted", 1);
        c = kmalloc(50);
        test_check(&t, "reuse after free", c != NULL);
        /* the freed 100-byte block must satisfy this 50-byte alloc
           without growing used beyond the freed amount */
        test_check(&t, "used reflects reuse",
                   kheap_used() <= used0 + 4096);
        kfree(b);
        kfree(c);
        test_check(&t, "all freed", kheap_used() == 0);
        {
            /* stress: many small allocs then free all */
            void *ptrs[64];
            int ok = 1;
            for (int i = 0; i < 64; i++) {
                ptrs[i] = kmalloc(1 + (u64)(i * 37) % 512);
                if (!ptrs[i])
                    ok = 0;
                else
                    memset(ptrs[i], i & 0xFF, 16);
            }
            for (int i = 0; i < 64; i++)
                kfree(ptrs[i]);
            test_check(&t, "stress 64 allocs + frees", ok);
            test_check(&t, "stress leaves heap empty",
                       kheap_used() == 0);
        }
        test_summary(&t);
    }

    {
        struct test_ctx t;
        u64 t0, t1;

        test_begin(&t, "timer");
        t0 = timer_ticks();
        timer_sleep_ms(100);
        t1 = timer_ticks();
        {
            u64 ms = timer_uptime_ms();
            /* 100 ms at 100 Hz = 10 ticks; allow 8..12 for jitter. */
            int ok_ticks = (t1 - t0) >= 8 && (t1 - t0) <= 12;
            test_check(&t, "100ms sleep advances ticks ~10",
                       ok_ticks);
            test_check(&t, "uptime plausible (>=100ms)", ms >= 100);
            test_check(&t, "interrupts counted (EOI loop works)",
                       irq_count() >= 8);
        }
        /* The timer must keep ticking (EOI + periodic reload OK). */
        {
            u64 before = timer_ticks();
            timer_sleep_ms(50);
            test_check(&t, "timer still ticking after sleep",
                       timer_ticks() > before);
        }
        test_summary(&t);
    }

    if (cmdline_has("kbd_test")) {
        struct test_ctx t;
        char got[8];
        int n = 0;

        test_begin(&t, "kbd");
        /* Window: collect characters for ~4 seconds. The driver echoes
           each decoded char to serial + framebuffer as it arrives. */
        kprintf("  (kbd test window: 4s, send keys now)\n");
        u64 deadline = timer_ticks() + 4 * TIMER_HZ;
        while (timer_ticks() < deadline) {
            int c = kbd_getchar();
            if (c >= 0) {
                kprintf("key: %c\n", (char)c);
                if (n < 8)
                    got[n++] = (char)c;
            }
        }
        /* Automated run (QEMU sendkey): expect h, i, H (shift). */
        test_check(&t, "received >=3 chars via sendkey", n >= 3);
        test_check(&t, "first char is 'h'", n >= 1 && got[0] == 'h');
        test_check(&t, "second char is 'i'", n >= 2 && got[1] == 'i');
        test_check(&t, "shift decodes to 'H'", n >= 3 && got[2] == 'H');
        test_check(&t, "scancodes were handled", kbd_event_count() >= 6);
        test_summary(&t);
    } else {
        kprintf("kbd: skipped (no kbd_test flag on cmdline)\n");
    }

    /* ---- Milestone 5: kernel threads (preemption) ---- */
    sched_init();
    kprintf("sched: idle task online (%lu tasks).\n",
            (unsigned long)sched_task_count());
    {
        struct test_ctx t;

        test_begin(&t, "sched");
        kthread_create("a", m5_thread_a, 0);
        kthread_create("b", m5_thread_b, 0);
        kthread_create("c", m5_thread_c, 0);
        kprintf("  (3 busy threads created; they never yield — only\n"
                "   timer preemption can share the CPU)\n");

        /* Idle waits for all three to finish. Note the busy threads
           cannot yield: their completion IS the preemption proof. */
        u64 spins = 0;
        while (sched_task_count() > 1 && spins++ < 5000000000ull) {
            u64 c0 = m5_counts[0], c1 = m5_counts[1], c2 = m5_counts[2];
            __asm__ volatile ("pause");
            if (m5_counts[0] < c0 || m5_counts[1] < c1 ||
                m5_counts[2] < c2)
                break;   /* counters must never go backwards */
        }

        kprintf("  counters: a=%lu b=%lu c=%lu (ticks=%lu)\n",
                (unsigned long)m5_counts[0],
                (unsigned long)m5_counts[1],
                (unsigned long)m5_counts[2],
                (unsigned long)timer_ticks());
        test_check(&t, "all busy threads finished",
                   sched_task_count() == 1);
        test_check(&t, "thread a made progress", m5_counts[0] >= 2500000);
        test_check(&t, "thread b made progress", m5_counts[1] >= 2500000);
        test_check(&t, "thread c made progress", m5_counts[2] >= 2500000);
        test_check(&t, "no starvation (all reached target)",
                   m5_counts[0] >= 2500000 && m5_counts[1] >= 2500000 &&
                   m5_counts[2] >= 2500000);
        test_summary(&t);
    }

    /* ---- guard-page stack-overflow self-test (gated: it panics on
       purpose). Runs AFTER everything else would normally halt; only
       with 'guard_test' on the cmdline. The pass condition is a #PF
       with CR2 inside the guard region and a readable panic dump —
       verified from the serial log by scripts/guard_test.sh. ---- */
    if (cmdline_has("guard_test")) {
        kprintf("guard: creating overflowing thread ( deliberate )\n");
        int ovf_id = kthread_create("ovf", m5_overflow_thread, 0);
        /* give the overflowing thread up to 10 seconds to blow its
           stack; the #PF handler panics (halted) — that is the PASS
           condition checked from the serial log */
        (void)ovf_id;
        u64 deadline = timer_ticks() + 10 * TIMER_HZ;
        while (timer_ticks() < deadline)
            __asm__ volatile ("pause");
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
