/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <isr.h>
#include <lib.h>
#include <sched.h>
#include <usermode.h>

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

/* Was this exception one that pushes an error code? (0x8, 0xA-0xE,
   0x11, 0x15+ architecturally do; the ISR stubs know, but this table
   is accurate for the exceptions the CPU can actually raise here.) */
static int vector_has_error(u64 n)
{
    return n == 8 || (n >= 10 && n <= 14) || n == 17 || n == 21 ||
           n == 29 || n == 30;
}

void exception_handler(struct int_frame *frame)
{
    /* Ring-3 fault: record the outcome on the task, dump everything a
       hardware debugger needs, kill that process, keep the kernel
       running. */
    if ((frame->cs & 3) == 3 && frame->int_no != 3) {
        struct task *t = sched_current();
        t->fault_vector = (u8)frame->int_no;
        t->fault_had_error = (u8)vector_has_error(frame->int_no);
        t->fault_rip = frame->rip;
        t->fault_cr2 = frame->int_no == 14 ? read_cr2() : 0;
        t->fault_errcode = frame->errcode;

        kprintf("$Wuser: fault #%lu (%s) at RIP %#lx, errcode %#lx$T\n",
                frame->int_no, exception_name(frame->int_no),
                frame->rip,
                vector_has_error(frame->int_no) ? frame->errcode : 0ul);
        if (frame->int_no == 14) {
            u64 cr2 = read_cr2();
            kprintf("  #PF errcode bits: P=%lu W=%lu U=%lu R=%lu I=%lu\n",
                    (frame->errcode >> 0) & 1, (frame->errcode >> 1) & 1,
                    (frame->errcode >> 2) & 1, (frame->errcode >> 3) & 1,
                    (frame->errcode >> 4) & 1);
            kprintf("  fault address CR2=%#lx, task '%s' pid %lu\n",
                    cr2, t->name, (unsigned long)t->pid);
        }
        kprintf("  RAX=%#016lx RBX=%#016lx RCX=%#016lx RDX=%#016lx\n",
                frame->rax, frame->rbx, frame->rcx, frame->rdx);
        kprintf("  RSI=%#016lx RDI=%#016lx RBP=%#016lx RSP=%#016lx\n",
                frame->rsi, frame->rdi, frame->rbp, frame->rsp);
        kprintf("  R8 =%#016lx R9 =%#016lx R10=%#016lx R11=%#016lx\n",
                frame->r8, frame->r9, frame->r10, frame->r11);
        kprintf("  R12=%#016lx R13=%#016lx R14=%#016lx R15=%#016lx\n",
                frame->r12, frame->r13, frame->r14, frame->r15);
        kprintf("  RFLAGS=%#lx CS=%#lx SS=%#lx\n",
                frame->rflags, frame->cs, frame->ss);
        /* 16 bytes at RIP (user memory, mapped in CR3 right now). */
        {
            const u8 *rip8 = (const u8 *)frame->rip;
            u8 bytes[16];
            int ok = 1;
            for (int i = 0; i < 16; i++) {
                /* faulting page may be short; per-byte read would be
                   safe via copy, but the code page is readable — a
                   straight read with a bounds guard below. */
                if (frame->rip + (u64)i < 0x0000800000000000ull)
                    bytes[i] = rip8[i];
                else {
                    ok = 0;
                    break;
                }
            }
            if (ok)
                kprintf("  code@RIP: %02x %02x %02x %02x %02x %02x %02x "
                        "%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        bytes[0], bytes[1], bytes[2], bytes[3],
                        bytes[4], bytes[5], bytes[6], bytes[7],
                        bytes[8], bytes[9], bytes[10], bytes[11],
                        bytes[12], bytes[13], bytes[14], bytes[15]);
        }
        user_fault_kill("user-mode exception");
    }

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
    /* raw iretq frame at RSP: [rip][cs][rflags][rsp][ss] */
    {
        volatile u64 *f = (volatile u64 *)frame->rsp;
        kprintf("iret frame: rip=%#lx cs=%#lx rfl=%#lx rsp=%#lx ss=%#lx\n",
                (unsigned long)f[0], (unsigned long)f[1],
                (unsigned long)f[2], (unsigned long)f[3],
                (unsigned long)f[4]);
    }

    panic("exception %lu at RIP %#lx", frame->int_no, frame->rip);
}

void irq_generic(struct int_frame *frame)
{
    /* Spurious or unhandled IRQ; no EOI here because the IRQ module
       owns the LAPIC. */
    (void)frame;
}
