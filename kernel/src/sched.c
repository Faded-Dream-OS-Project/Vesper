/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <heap.h>
#include <lib.h>
#include <sched.h>

/* Round-robin scheduler.

   A new task's stack is crafted so the FIRST sched_switch_to onto it
   pops into sched_trampoline -> fn(arg) -> kthread_exit. The idle
   task (task 0) is the kernel's main path: everything else runs by
   switching away from it on each tick. */

void sched_switch_to(struct task *prev, struct task *next);

#define MAX_TASKS 16

static struct task tasks[MAX_TASKS];
static struct task *current;
static u64 ready_count;   /* tasks alive (incl. idle) */

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

int kthread_create(const char *name, kthread_fn_t fn, void *arg)
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
       kernel heap. */
    u8 *stack = kmalloc(KTHREAD_STACK_SIZE);
    if (!stack)
        return -1;

    struct task *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->fn = fn;
    t->arg = arg;
    t->state = TASK_READY;
    t->counter = 0;
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

    ready_count++;
    return slot;
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
    sched_switch_to(prev, next);
}

void sched_yield(void)
{
    struct task *next = pick_next(current);
    switch_to(next);
}

/* Called from the timer IRQ: record that a tick arrived so yielding
   tasks (in thread context, not IRQ context) can round-robin. */
static volatile u64 tick_seq;

void sched_note_tick(void)
{
    tick_seq++;
}

u64 sched_tick_seq(void)
{
    return tick_seq;
}


