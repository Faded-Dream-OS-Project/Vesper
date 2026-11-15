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
# switch.
#
# Interrupts are re-enabled just before the stack swap. When the
# switch is driven from inside the timer IRQ (preemption), the CPU
# entered the handler with IF=0; without the sti the switched-in
# task would run with interrupts disabled — it could never be
# preempted again (and hlt would never wake), which was the original
# timer-IRQ livelock. A fresh task enters via `ret`, so nothing else
# would restore IF for it. A preempted task instead resumes its own
# interrupt chain and its iretq restores ITS saved RFLAGS.
#
# The window between sti and the rsp load is safe: an interrupt
# arriving there pushes its frame below the current rsp and unwinds
# without disturbing the pushed callee-saved state, and the LAPIC
# timer has just been re-armed (next fire ~10 ms out).

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
    sti
    movq (%rsi), %rsp          /* next->rsp */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
.size sched_switch_to, . - sched_switch_to
