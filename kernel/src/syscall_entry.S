/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

# syscall_entry.S — syscall/sysret ring-3 gate.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# user_enter(rip, rsp): drop into ring 3 via SYSRETQ. Flags are fixed
# (IF=1); RBP=0. Called from task_enter_user after CR3/TSS/kstack were
# set up in C.
#
# syscall_entry: ring-3 -> ring-0 gate (EFER.LSTAR). syscall does NOT
# switch stacks, so the user RSP is moved to r10 (not part of our
# 3-arg ABI), the kernel stack is taken from the per-task global
# kstack_cur (maintained by the scheduler on every switch into a user
# task), the full register set is pushed there, and the C dispatcher
# runs. Result: rax = syscall return (negative = -errno); the sentinel
# SYSCALL_TERMINATE means "task exited — never return to ring 3".
#
# SysV ABI inside: syscall_c_entry(struct syscall_frame *) in usermode.c.

.section .text
.global user_enter
.global syscall_entry
.type user_enter, @function

/* void user_enter(u64 rip in rdi, u64 rsp in rsi); */
user_enter:
    movq %rsi, %rsp         /* user RSP */
    movq %rdi, %rcx         /* user RIP (sysret loads RIP=RCX) */
    movq $0x202, %r11       /* user RFLAGS: IF=1, bit1 fixed */
    xorq %rbp, %rbp
    xorq %rax, %rax
    sysretq
.size user_enter, . - user_enter

.type syscall_entry, @function
syscall_entry:
    /* rax = nr; args: rdi, rsi, rdx; rcx/r11 = return rip/rflags. */
    movq %rsp, %r10                 /* user RSP */
    movq kstack_cur(%rip), %rsp     /* kernel stack of THIS task */

    /* frame: [rdi][rsi][rdx][r8][r9][nr][r11][rcx][r10-user-rsp] */
    pushq %r10
    pushq %rcx
    pushq %r11
    pushq %rax
    pushq %r9
    pushq %r8
    pushq %rdx
    pushq %rsi
    pushq %rdi

    movq %rsp, %rdi                 /* frame pointer */
    call syscall_c_entry

    /* rax = result. Terminate sentinel -> task exited: switch away
       forever (kthread_exit never returns; this stack is abandoned). */
    movq %rax, %r12
    movq $0xDEAD0000, %rax
    cmpq %rax, %r12
    je .L_terminate

    popq %rdi
    popq %rsi
    popq %rdx
    popq %r8
    popq %r9
    popq %rax                       /* nr slot — overwritten below */
    popq %r11
    popq %rcx
    popq %r10                        /* user RSP */
    movq %r12, %rax                 /* result */
    movq %r10, %rsp
    sysretq

.L_terminate:
    call kthread_exit                /* never returns */
.size syscall_entry, . - syscall_entry
