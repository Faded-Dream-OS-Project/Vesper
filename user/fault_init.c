/* fault_init.c — deliberate ring-3 fault test program.
 * Copyright (c) Faded Dream OS Project. All rights reserved.
 * Source is viewable but may not be copied, modified, or redistributed
 * without permission.
 *
 * Prints one line, then executes a privileged instruction (hlt): the
 * kernel must kill THIS process and keep running. */

typedef unsigned long u64_;
typedef long i64_;

#define SYS_WRITE 1

static inline i64_ syscall3(u64_ nr, u64_ a, u64_ b, u64_ c)
{
    i64_ ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return ret;
}

void _start(void)
{
    syscall3(SYS_WRITE, 1, (u64_)"fault: executing hlt in ring 3\n", 30);
    __asm__ volatile ("hlt");
    for (;;)
        ;
}
