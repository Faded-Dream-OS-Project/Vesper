/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_SCHED_H
#define VESPER_SCHED_H

#include <common.h>

/* Kernel threads and a round-robin scheduler driven by the timer
   tick. Context switching happens in sched_switch_to.S (plain C
   cannot reload RSP/RIP atomically). */

typedef void (*kthread_fn_t)(void *arg);

#define KTHREAD_STACK_SIZE (16 * 1024)   /* 16 KiB */
#define KTHREAD_NAME_LEN   16

struct task {
    u64 rsp;                 /* saved kernel stack pointer */
    kthread_fn_t fn;
    void *arg;
    char name[KTHREAD_NAME_LEN];
    int state;
    u64 counter;             /* test-visible progress counter */
    struct task *next;
};

enum { TASK_UNUSED = 0, TASK_READY, TASK_RUNNING, TASK_FINISHED };

void sched_init(void);
int  kthread_create(const char *name, kthread_fn_t fn, void *arg);
void kthread_exit(void);     /* marks current task finished, yields */
void sched_yield(void);      /* cooperative switch */
void sched_note_tick(void);  /* timer IRQ records a tick */
u64  sched_tick_seq(void);
struct task *sched_current(void);
u64  sched_task_count(void);

#endif /* VESPER_SCHED_H */
