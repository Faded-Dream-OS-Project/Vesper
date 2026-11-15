/* init.c — the Vesper user init program (ring 3).
 * Copyright (c) Faded Dream OS Project. All rights reserved.
 * Source is viewable but may not be copied, modified, or redistributed
 * without permission.
 *
 * Runs as the first user task: prints a banner, exercises the syscall
 * ABI (write/getpid/uptime_ms/yield), deliberately passes a bad
 * pointer to write() (must return an error, not crash the kernel),
 * then exits. The privileged-instruction fault test is driven from
 * kmain with a second, deliberate-faulting program (fault_init).
 */

typedef unsigned long u64_;
typedef long i64_;
typedef unsigned char u8_;

#define SYS_WRITE  1
#define SYS_EXIT   2
#define SYS_YIELD  3
#define SYS_GETPID 4
#define SYS_UPTIME 5

static inline i64_ syscall3(u64_ nr, u64_ a, u64_ b, u64_ c)
{
    i64_ ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return ret;
}

static u64_ strlen_(const char *s)
{
    u64_ n = 0;
    while (s[n])
        n++;
    return n;
}

static void puts_(const char *s)
{
    syscall3(SYS_WRITE, 1, (u64_)s, strlen_(s));
}

static u64_ uput(u64_ v, char *buf, u64_ cap)
{
    char tmp[24];
    u64_ n = 0;
    if (v == 0)
        tmp[n++] = '0';
    while (v && n < 24) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    u64_ out = 0;
    while (n && out + 1 < cap)
        buf[out++] = tmp[--n];
    buf[out] = 0;
    return out;
}

/* Data lives at the fixed USER_DATA vaddr (0x500000): our .data/.bss
   was placed there by the linker script (init.ld). */
static u64_ counter;

void _start(void)
{
    char line[80];
    u64_ n;

    puts_("init: hello from ring 3\n");

    n = uput((u64_)syscall3(SYS_GETPID, 0, 0, 0), line, sizeof(line));
    puts_("init: getpid() = ");
    syscall3(SYS_WRITE, 1, (u64_)line, n);
    puts_("\n");

    n = uput((u64_)syscall3(SYS_UPTIME, 0, 0, 0), line, sizeof(line));
    puts_("init: uptime_ms() = ");
    syscall3(SYS_WRITE, 1, (u64_)line, n);
    puts_("\n");

    /* Bad pointer: must return an error, NOT crash the kernel. */
    i64_ bad = syscall3(SYS_WRITE, 1, 0x8000000000000000ull, 16);
    n = uput((u64_)(bad < 0 ? -bad : bad), line, sizeof(line));
    puts_("init: write(bad ptr) -> errno ");
    syscall3(SYS_WRITE, 1, (u64_)line, n);
    puts_("\n");

    /* Spin a few rounds with yields — proves ring-3 yield works and
       the task is preemptible. */
    for (u64_ i = 0; i < 5; i++) {
        counter++;
        syscall3(SYS_YIELD, 0, 0, 0);
    }
    n = uput(counter, line, sizeof(line));
    puts_("init: yield loop done, counter=");
    syscall3(SYS_WRITE, 1, (u64_)line, n);
    puts_("\n");

    syscall3(SYS_EXIT, 0, 0, 0);
    for (;;)
        ;
}
