/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_USERMODE_H
#define VESPER_USERMODE_H

#include <common.h>

/* Ring-3 support: syscall/sysret gate, user task launcher, fault
   handling, and the embedded init program. */

/* Syscall numbers (ABI documented in docs/SYSCALLS.md). */
#define SYS_WRITE     1
#define SYS_EXIT      2
#define SYS_YIELD     3
#define SYS_GETPID    4
#define SYS_UPTIME    5

/* Exit sentinel returned to the asm gate: do not sysret; the task is
   finished and the kernel stack gets abandoned. */
#define SYSCALL_TERMINATE 0xDEAD0000ull

/* Result convention: 0+ = success value; negative = -errno. */
#define ERANGE_INV 1   /* pointer not in user space / page absent */
#define ERANGE_ARG 2   /* invalid length / fd */

/* kernel stack of the CURRENT user task (syscall does not switch
   stacks; syscall_entry.S loads RSP from here). */
extern u64 kstack_cur;

/* Frame pushed by syscall_entry.S. */
struct syscall_frame {
    u64 rdi, rsi, rdx;
    u64 r8, r9;
    u64 nr;
    u64 r11, rcx;
    u64 user_rsp;
};

void usermode_init(void);          /* MSR STAR/LSTAR/SFMASK, EFER.SCE */
u64  syscall_c_entry(struct syscall_frame *f);  /* called from asm */
u64  syscall_kernel_rsp(void);     /* (kept for completeness) */

/* Launch the current task into ring 3 (called by the kernel runner
   thread; user_enter does the actual sysret). */
void task_enter_user(void *arg);

/* Launch the embedded init program as a user task. */
void usermode_launch_init(void);

/* Launch the deliberate-fault test program (privileged instruction). */
void usermode_launch_fault_init(void);
void user_cap_reset(void);       /* clear the user-output capture ring */
int  user_cap_contains(const char *needle);   /* substring search */

/* User-mode exception (any #PF/#GP from CPL=3): kill the process with
   a readable message; the kernel keeps running. */
void user_fault_kill(const char *kind);

/* Init blob (flat binary, built by scripts/build_init.sh). */
extern const u8 init_blob[];
extern const u64 init_blob_size[];

#endif /* VESPER_USERMODE_H */
