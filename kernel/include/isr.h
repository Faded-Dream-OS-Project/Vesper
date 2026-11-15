/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_ISR_H
#define VESPER_ISR_H

#include <common.h>

/* CPU-pushed part of the interrupt frame (after the selector error
   push, before the stub's stack alignment). */
struct int_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
    u64 int_no, errcode;
    u64 rip, cs, rflags, rsp, ss;
};

struct full_frame {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rdi, rsi, rbp, rbx, rdx, rcx, rax;
    u64 int_no, errcode;
    u64 rip, cs, rflags, rsp, ss;
};

/* Implemented in isr.c */
void exception_handler(struct int_frame *frame);
void irq_generic(struct int_frame *frame);

/* Exception mnemonics for the panic dump. */
const char *exception_name(u64 n);

#endif /* VESPER_ISR_H */
