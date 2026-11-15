/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

# sched_switch_to.S — round-robin context switch.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# void sched_switch_to(struct task *prev, struct task *next);
#
# Saves the CALLER's callee-saved registers + return address frame on
# the PREVIOUS task's stack, stores RSP into prev->rsp, then loads
# next->rsp and pops the same frame. prev is an explicit argument:
# reading the `current` global here would already see the NEW task
# (the caller updates it first), which once silently broke every
# switch. Because this is a plain call, the interrupt frame (if
# switching from an IRQ) stays untouched on the interrupted task's
# stack and iretq later restores it.

.section .text
.global sched_switch_to
.type sched_switch_to, @function
sched_switch_to:
    /* rdi = prev, rsi = next */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, (%rdi)          /* prev->rsp (offset 0) */
    movq (%rsi), %rsp          /* next->rsp */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size sched_switch_to, . - sched_switch_to
