/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <heap.h>
#include <lib.h>
#include <sched.h>
#include <vmm.h>
#include <pmm.h>

/* Round-robin scheduler.

   A new task's stack is crafted so the FIRST sched_switch_to onto it
   pops into sched_trampoline -> fn(arg) -> kthread_exit. The idle
   task (task 0) is the kernel's main path: everything else runs by
   switching away from it on each tick.

   Every switch also swaps CR3: user runners own their address space,
   kernel threads return to the kernel PML4. */

void sched_switch_to(struct task *prev, struct task *next);

#define MAX_TASKS 16

static struct task tasks[MAX_TASKS];
static int last_user_slot = -1;   /* most recent user task (any state) */
static struct task *current;
static u64 ready_count;   /* tasks alive (incl. idle) */
static u64 next_pid = 1;

/* Trampoline for new threads: runs fn(arg), then exits. */
static void sched_trampoline(void)
{
    current->fn(current->arg);
    kthread_exit();
    /* not reached */
}

void sched_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    current = &tasks[0];       /* idle = kmain's own context */
    current->state = TASK_RUNNING;
    strcpy(current->name, "idle");
    current->counter = 0;
    ready_count = 1;
}

/* Allocate and fully initialise a task WITHOUT making it schedulable
   (used by task_new_user, which must finish mapping the address space
   before the runner can ever run). */
static int task_alloc(const char *name, kthread_fn_t fn, void *arg)
{
    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    /* Task struct stays in the static table; the stack comes from the
       kernel heap. One guard page is reserved BELOW the stack and left
       unmapped: a downward overflow faults on it (readable #PF panic)
       instead of silently corrupting the neighbouring heap blocks.

       Heap blocks are only 8-byte aligned, so the guard page must be
       page-ALIGNED WITHIN the allocation (unmapping an unaligned block
       start once took out the tail of the PREVIOUS heap block and
       corrupted a live thread's stack — found by the guard test
       itself). Slack: up to one page of lead-in + one guard page. */
    u8 *raw = kmalloc(KTHREAD_STACK_SIZE + 8 * VMM_PAGE_SIZE);
    if (!raw)
        return -1;
    u64 guard = ((u64)raw + VMM_PAGE_SIZE - 1) & ~(u64)(VMM_PAGE_SIZE - 1);
    vmm_unmap(guard);   /* guard page (page-aligned, inside raw) */
    u8 *stack = (u8 *)(guard + VMM_PAGE_SIZE);

    struct task *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->fault_vector = 0xFF;   /* no fault (yet) */
    t->fn = fn;
    t->arg = arg;
    t->state = TASK_READY;
    t->counter = 0;
    t->pml4 = 0;
    t->user_rip = 0;
    t->user_rsp = 0;
    t->user_rsp_top = 0;
    t->exit_code = 0;
    t->pid = sched_assign_pid();
    if (name) {
        strncpy(t->name, name, KTHREAD_NAME_LEN - 1);
        t->name[KTHREAD_NAME_LEN - 1] = 0;
    }

    /* Build the initial frame that sched_switch_to pops:
       r15 r14 r13 r12 rbx rbp, then the RETURN address. */
    u64 *top = (u64 *)(stack + KTHREAD_STACK_SIZE);
    *(--top) = (u64)sched_trampoline;   /* ret target */
    *(--top) = 0;                       /* rbp */
    *(--top) = 0;                       /* rbx */
    *(--top) = 0;                       /* r12 */
    *(--top) = 0;                       /* r13 */
    *(--top) = 0;                       /* r14 */
    *(--top) = 0;                       /* r15 */
    t->rsp = (u64)top;
    return slot;
}

int kthread_create(const char *name, kthread_fn_t fn, void *arg)
{
    int slot = task_alloc(name, fn, arg);
    if (slot < 0)
        return -1;
    tasks[slot].state = TASK_READY;
    ready_count++;
    return slot;
}

/* Map extra zero-filled user pages into an existing user task
   (used for .bss beyond the file-backed blob). */
int task_add_user_pages(int slot, u64 vaddr, u64 pages)
{
    if (slot < 0 || slot >= MAX_TASKS)
        return -1;
    struct task *t = &tasks[slot];
    if (!t->pml4)
        return -1;
    for (u64 i = 0; i < pages; i++) {
        u64 p = pmm_alloc();
        if (p == 0)
            return -1;
        memset((void *)vmm_phys_to_hhdm(p), 0, VMM_PAGE_SIZE);
        if (vmm_map_table(t->pml4, vaddr + i * VMM_PAGE_SIZE,
                          p, VMM_WRITABLE | VMM_USER) != 0)
            return -1;
    }
    return 0;
}

/* Create a USER (ring 3) task.
   - code: physical pages mapped at USER_CODE_BASE, R+X, U bit
   - data: physical pages mapped at USER_DATA_BASE, R+W, U bit
   - stack: fresh pages at USER_STACK_TOP - n, R+W, U bit
   The kernel-side runner thread enters ring 3 via syscall_return. */
#define USER_CODE_BASE  0x400000ull          /* 4 MiB */
#define USER_STACK_TOP  0x800000000000ull     /* user half top-16K */
#define USER_STACK_SIZE (64 * 1024)
#define USER_STACK_BASE (USER_STACK_TOP - USER_STACK_SIZE)

int task_new_user(const char *name, u64 entry, u64 user_rsp,
                  const u64 *code_frames, u64 code_pages)
{
    /* 1) a kernel runner thread (it owns a kernel stack + guard page
       and starts the user code via a dedicated entry function). The
       task stays unschedulable until its address space is complete —
       a premature switch once loaded CR3=0 and double-faulted. */
    int slot = task_alloc(name, task_enter_user, 0);
    if (slot < 0)
        return -1;
    last_user_slot = slot;
    struct task *t = &tasks[slot];

    t->user_rip = entry;
    t->user_rsp = user_rsp ? user_rsp : USER_STACK_TOP - 136;
    t->user_rsp_top = t->rsp;   /* TSS.rsp0 switch target */

    /* 2) its own address space */
    t->pml4 = vmm_create_user_pml4();
    if (t->pml4 == 0)
        return -1;

    /* 3) code pages (r+x, user) — frames are SCATTERED: map each
       one individually (mapping code_phys+i*4096 once spliced
       unrelated physical memory into user space and execution
       degenerated into zero-op garbage -> non-canonical #GP). */
    for (u64 i = 0; i < code_pages; i++) {
        if (vmm_map_table(t->pml4, USER_CODE_BASE + i * VMM_PAGE_SIZE,
                          code_frames[i],
                          VMM_WRITABLE | VMM_USER) != 0)
            return -1;
    }

    /* 4) stack pages (rw, user) — explicitly zeroed: QEMU hands out
       pre-zeroed frames so a missing memset hid for months, but real
       RAM keeps whatever the firmware left there (session 6: the
       hardware boot faulted in user mode). */
    u64 spages = USER_STACK_SIZE / VMM_PAGE_SIZE;
    for (u64 i = 0; i < spages; i++) {
        u64 p = pmm_alloc();
        if (p == 0)
            return -1;
        memset((void *)vmm_phys_to_hhdm(p), 0, VMM_PAGE_SIZE);
        if (vmm_map_table(t->pml4, USER_STACK_BASE + i * VMM_PAGE_SIZE,
                          p, VMM_WRITABLE | VMM_USER) != 0)
            return -1;
    }

    /* fully initialised: make it schedulable */
    t->state = TASK_READY;
    ready_count++;
    return slot;
}

u64 sched_assign_pid(void)
{
    return next_pid++;
}

/* Mark the current task finished and drop it from the ready count
   (used by user_fault_kill; kthread_exit does the same inline). */
void sched_task_release(void)
{
    ready_count--;
}

void kthread_exit(void)
{
    current->state = TASK_FINISHED;
    ready_count--;
    /* Never returns: switch away forever. */
    for (;;)
        sched_yield();
}

struct task *sched_current(void)
{
    return current;
}

u64 sched_task_count(void)
{
    return ready_count;
}

/* Pick the next READY task after `from` (round robin, wrapping). */
static struct task *pick_next(struct task *from)
{
    int start = (int)(from - tasks);
    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (start + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_READY || tasks[idx].state == TASK_RUNNING) {
            if (&tasks[idx] != from)
                return &tasks[idx];
        }
    }
    return from;   /* only ourselves: idle loop continues */
}

static void switch_to(struct task *next)
{
    if (next == current)
        return;
    struct task *prev = current;
    current = next;
    prev->state = (prev->state == TASK_RUNNING) ? TASK_READY : prev->state;
    next->state = TASK_RUNNING;

    /* Address spaces are per task: kernel threads use the kernel
       PML4, user runners their own. This must happen BEFORE the stack
       switch so the next task's first instruction fetch uses the
       right tables (a missing switch here faulted init at its very
       first instruction). */
    if (next->pml4)
        vmm_load_pml4(next->pml4);
    else
        vmm_load_pml4(vmm_kernel_pml4());

    sched_switch_to(prev, next);
}

void sched_yield(void)
{
    struct task *next = pick_next(current);
    switch_to(next);
}

/* Called from the timer IRQ after the EOI: preempt the current task
   in favour of the next one (round robin). The context switch
   re-enables interrupts on the way in (sched_switch_to sti), so the
   switched-in task is preemptible even though it enters via `ret`
   rather than iretq — and a preempted task later resumes its own
   interrupt chain, whose iretq restores its saved RFLAGS. */
void sched_tick(void)
{
    if (ready_count > 1) {
        struct task *next = pick_next(current);
        switch_to(next);
    }
}



const struct task *sched_last_user_task(void)
{
    if (last_user_slot < 0)
        return NULL;
    return &tasks[last_user_slot];
}
