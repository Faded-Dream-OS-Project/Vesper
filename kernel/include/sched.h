/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_SCHED_H
#define VESPER_SCHED_H

#include <common.h>

/* Kernel threads and a round-robin PREEMPTIVE scheduler driven by
   the timer tick; sched_yield() remains for cooperative switching.
   Context switching happens in sched_switch_to.S (plain C cannot
   reload RSP/RIP atomically). */

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

    /* ring-3 (user) state */
    u64 pml4;                /* user PML4 (physical), 0 = kernel task */
    u64 user_rip;            /* entry RIP (0 = kernel thread) */
    u64 user_rsp;            /* initial user RSP */
    u64 user_rsp_top;        /* kernel stack switch target (TSS.rsp0) */
    u64 exit_code;
    u64 pid;

    /* outcome tracking for honest user-mode tests (session 6): */
    u8  fault_vector;      /* exception number that killed us, 0xFF = none */
    u8  fault_had_error;   /* exception pushed an error code */
    u64 fault_rip;
    u64 fault_cr2;
    u64 fault_errcode;
};

enum { TASK_UNUSED = 0, TASK_READY, TASK_RUNNING, TASK_FINISHED };

void sched_init(void);
int  kthread_create(const char *name, kthread_fn_t fn, void *arg);
int  task_new_user(const char *name, u64 entry, u64 user_rsp,
                   const u64 *code_frames, u64 code_pages);
void kthread_exit(void);     /* marks current task finished, yields */
u64  sched_assign_pid(void);
void sched_task_release(void);
int  task_add_user_pages(int slot, u64 vaddr, u64 pages);
void task_enter_user(void *arg);   /* ring-3 launcher (usermode.c) */
void sched_yield(void);      /* cooperative switch */
void sched_tick(void);       /* timer IRQ preempts (after EOI) */
struct task *sched_current(void);
u64  sched_task_count(void);
const struct task *sched_last_user_task(void);  /* NULL if none ever */

#endif /* VESPER_SCHED_H */
