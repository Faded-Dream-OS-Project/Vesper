/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <gdt.h>
#include <isr.h>
#include <lib.h>
#include <pmm.h>
#include <sched.h>
#include <timer.h>
#include <usermode.h>
#include <vmm.h>

/* Ring-3 syscall layer.

   ABI (docs/SYSCALLS.md): syscall number in RAX, args RDI RSI RDX,
   return in RAX (0+ success, negative -errno). Entry: syscall_entry.S
   (EFER.LSTAR); return: sysretq. The kernel stack of the current user
   task lives in kstack_cur because SYSCALL does not switch stacks. */

/* asm helpers */
void user_enter(u64 rip, u64 rsp);
extern void syscall_entry(void);

u64 kstack_cur;

static u64 efer_read(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080u));
    return ((u64)hi << 32) | lo;
}

static void msr_write(u32 msr, u64 val)
{
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((u32)val),
                      "d"((u32)(val >> 32)));
}

void usermode_init(void)
{
    /* EFER.SCE */
    msr_write(0xC0000080u, efer_read() | 1ull);

    /* STAR (0xC0000081): bits 63:48 = CS for SYSRET (hardware ORs 3),
       bits 47:32 = SS for SYSRET; bits 31:16 = CS for SYSCALL, bits
       15:0 = SS for SYSCALL (hardware uses CS+8). Selectors: KC 0x08,
       KD 0x10, UC 0x18, UD 0x20. */
    u64 star = (((u64)SEL_UCODE & 0xFFFF) << 48) |   /* SYSRET CS  */
               (((u64)SEL_KCODE & 0xFFFF) << 32);      /* SYSCALL CS */
    msr_write(0xC0000081u, star);

    /* LSTAR: entry RIP. SFMASK: clear IF + direction on entry. */
    msr_write(0xC0000082u, (u64)syscall_entry);
    /* SFMASK: RFLAGS bits to CLEAR on syscall entry. IF (0x200) must
       be cleared so a syscall runs as a critical section on the kernel
       stack (a stray tick mid-syscall used to corrupt the frame that
       iretq/sysret later popped — #GP with the garbage CS, e=0x20).
       TF/DF too. */
    msr_write(0xC0000083u, 0x310);     /* IF, TF, DF cleared on entry */

    kstack_cur = 0;
}

/* ---- pointer validation ---- */

/* A user buffer must live in the user half and be present. */
static int user_range_ok(u64 addr, u64 len)
{
    if (len == 0)
        return 1;
    if (addr >= 0x0000800000000000ull)   /* above user canonical half */
        return 0;
    if (len > 0x10000000ull)             /* sanity cap: 256 MiB */
        return 0;
    if (addr + len > 0x0000800000000000ull)
        return 0;
    /* page tables of the CURRENT task (we run in its CR3 here) */
    u64 first = addr & ~0xFFFull;
    u64 last = (addr + len - 1) & ~0xFFFull;
    for (u64 v = first; ; v += 0x1000) {
        if (vmm_mapped(v, NULL) != 0)
            return 0;                    /* page not present */
        if (v == last)
            break;
    }
    return 1;
}

/* ---- syscall implementations ---- */

/* Capture ring for the LAST user output (honest tests: the init test
   requires its exact output lines, session 6). */
#define USER_CAP 2048
static char user_cap[USER_CAP];
static u32 user_cap_len;

void user_cap_reset(void)
{
    user_cap_len = 0;
}

int user_cap_contains(const char *needle)
{
    u32 nlen = 0;
    while (needle[nlen])
        nlen++;
    if (nlen == 0 || user_cap_len == 0)
        return 0;
    for (u32 i = 0; i + nlen <= user_cap_len; i++) {
        u32 j = 0;
        while (j < nlen && user_cap[i + j] == needle[j])
            j++;
        if (j == nlen)
            return 1;
    }
    return 0;
}

static u64 sys_write(struct syscall_frame *f)
{
    u64 fd = f->rdi, buf = f->rsi, len = f->rdx;
    if (fd != 1 && fd != 2)
        return -(u64)ERANGE_ARG;
    if (!user_range_ok(buf, len))
        return -(u64)ERANGE_INV;
    const char *p = (const char *)buf;
    for (u64 i = 0; i < len; i++)
        kputchar(p[i]);
    /* capture (drop overflow silently; tests use short lines) */
    for (u64 i = 0; i < len && user_cap_len < USER_CAP - 1; i++)
        user_cap[user_cap_len++] = p[i];
    return len;
}

static u64 sys_exit(struct syscall_frame *f)
{
    sched_current()->exit_code = f->rdi;
    kprintf("init: exit(%lu)\n", (unsigned long)f->rdi);
    return SYSCALL_TERMINATE;
}

static u64 sys_yield(void)
{
    sched_yield();
    return 0;
}

static u64 sys_getpid(void)
{
    return sched_current()->pid;
}

static u64 sys_uptime(void)
{
    return timer_uptime_ms();
}

u64 syscall_c_entry(struct syscall_frame *f)
{
    switch (f->nr) {
    case SYS_WRITE:  f->nr = sys_write(f);  break;
    case SYS_EXIT:   f->nr = sys_exit(f);   break;   /* nr = sentinel */
    case SYS_YIELD:  f->nr = sys_yield();   break;
    case SYS_GETPID: f->nr = sys_getpid();  break;
    case SYS_UPTIME: f->nr = sys_uptime();  break;
    default:
        f->nr = -(u64)ERANGE_ARG;
        break;
    }
    return f->nr;
}

u64 syscall_kernel_rsp(void)
{
    return kstack_cur;
}

/* ---- ring-3 launcher ---- */

void task_enter_user(void *arg)
{
    (void)arg;
    struct task *t = sched_current();

    /* CR3: this task's user PML4 (kernel half shared). */
    vmm_load_pml4(t->pml4);

    /* TSS.rsp0: where interrupts/exceptions switch to when this task
       is in ring 3; also the SYSCALL kernel stack (kstack_cur). */
    gdt_set_rsp0(t->user_rsp_top);
    kstack_cur = t->user_rsp_top;

    user_enter(t->user_rip, t->user_rsp);
    /* user_enter never returns into this frame; sysret left. If the
       task returns to ring 0 via an exit syscall, syscall_entry.S
       jumps to kthread_exit instead. */
    kthread_exit();
}

/* ---- user-mode fault kill ---- */

void user_fault_kill(const char *kind)
{
    struct task *t = sched_current();
    if (t->pml4 == 0)
        return;                        /* kernel task: not ours */
    kprintf("user: task '%s' (pid %lu) killed: %s\n",
            t->name, (unsigned long)t->pid, kind);
    t->state = TASK_FINISHED;
    sched_task_release();              /* ready_count-- */
    /* switch away forever */
    for (;;)
        sched_yield();
}

/* ---- init blob ---- */

/* init_blob[] and init_blob_size[1] are generated into
   kernel/src/init_blob.S by scripts/build_init.sh. */

/* Launch the embedded init program as a user task. */
static int launch_user_blob(const char *name, const u8 *blob,
                            const u64 *size, const u64 *entry,
                            const u64 *span)
{
    u64 bytes = size[0];
    u64 total_pages = (span[0] - 0x400000ull + VMM_PAGE_SIZE - 1) /
                      VMM_PAGE_SIZE;

    /* COPY the blob into fresh physical pages: the blob is not
       page-aligned inside the kernel image (mapping its pages in
       place would splice unrelated kernel rodata into user space),
       and the frames come back SCATTERED from the PMM, so the user
       mapping is built per frame. */
    u64 frames[8];
    if (total_pages > 8)
        return -1;
    for (u64 i = 0; i < total_pages; i++) {
        u64 p = pmm_alloc();
        if (p == 0)
            return -1;
        frames[i] = p;
        memset((void *)vmm_phys_to_hhdm(p), 0, VMM_PAGE_SIZE);
    }
    for (u64 off = 0; off < bytes; off += VMM_PAGE_SIZE) {
        u64 page = off / VMM_PAGE_SIZE;
        u8 *dst = (u8 *)vmm_phys_to_hhdm(frames[page]);
        u64 chunk = bytes - off;
        if (chunk > VMM_PAGE_SIZE)
            chunk = VMM_PAGE_SIZE;
        memcpy(dst, blob + off, chunk);
    }

    return task_new_user(name, entry[0], 0, frames, total_pages);
}

void usermode_launch_init(void)
{
    extern const u8 init_blob[];
    extern const u64 init_blob_size[];
    extern const u64 init_blob_entry[];
    extern const u64 init_blob_span[];

    if (launch_user_blob("init", init_blob, init_blob_size,
                         init_blob_entry, init_blob_span) < 0)
        panic("user: cannot create init task");
}

void usermode_launch_fault_init(void)
{
    extern const u8 fault_init_blob[];
    extern const u64 fault_init_blob_size[];
    extern const u64 fault_init_blob_entry[];
    extern const u64 fault_init_blob_span[];

    if (launch_user_blob("fault", fault_init_blob, fault_init_blob_size,
                         fault_init_blob_entry, fault_init_blob_span) < 0)
        panic("user: cannot create fault_init task");
}
