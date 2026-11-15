/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <isr.h>
#include <lib.h>
#include <sched.h>

/* Exception dispatch and register dump. */

static const char *const exc_names[32] = {
    "Divide-by-zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND range exceeded", "Invalid opcode", "No FPU",
    "Double fault", "Coproc segment overrun", "Invalid TSS",
    "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 exception", "Alignment check",
    "Machine check", "SIMD exception", "Virtualization exception",
    "Control protection", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Hypervisor injection",
    "VMM communication", "Security exception", "Reserved",
};

const char *exception_name(u64 n)
{
    if (n < 32)
        return exc_names[n];
    return "Unknown";
}

void exception_handler(struct int_frame *frame)
{
    kprintf("\n=== EXCEPTION #%lu (%s) ===\n",
            frame->int_no, exception_name(frame->int_no));
    if (frame->int_no == 14)
        kprintf("CR2 (fault address): %#lx (task '%s', RSP %#lx — a\n"
                "  stack pointer below its guard page means a kernel\n"
                "  thread stack overflow hit the guard page)\n",
                read_cr2(), sched_current()->name, frame->rsp);

    kprintf("RAX=%#016lx RBX=%#016lx RCX=%#016lx RDX=%#016lx\n",
            frame->rax, frame->rbx, frame->rcx, frame->rdx);
    kprintf("RSI=%#016lx RDI=%#016lx RBP=%#016lx RSP=%#016lx\n",
            frame->rsi, frame->rdi, frame->rbp, frame->rsp);
    kprintf("R8 =%#016lx R9 =%#016lx R10=%#016lx R11=%#016lx\n",
            frame->r8, frame->r9, frame->r10, frame->r11);
    kprintf("R12=%#016lx R13=%#016lx R14=%#016lx R15=%#016lx\n",
            frame->r12, frame->r13, frame->r14, frame->r15);
    kprintf("RIP=%#016lx CS=%#lx RFLAGS=%#lx SS=%#lx\n",
            frame->rip, frame->cs, frame->rflags, frame->ss);
    kprintf("ERCODE=%#lx\n", frame->errcode);

    panic("exception %lu at RIP %#lx", frame->int_no, frame->rip);
}

void irq_generic(struct int_frame *frame)
{
    /* Spurious or unhandled IRQ; no EOI here because the IRQ module
       owns the LAPIC. */
    (void)frame;
}
