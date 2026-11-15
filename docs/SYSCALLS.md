# Vesper syscall ABI (milestone 6)

The Vesper kernel provides five syscalls to ring-3 programs. This
document is the ABI contract future userland is built against.

## Invocation

`syscall` instruction (AMD64 ABI; EFER.SCE enabled).

| Register | On entry                        | On return          |
|----------|---------------------------------|--------------------|
| RAX      | syscall number                  | result (see below) |
| RDI      | argument 1                      | preserved          |
| RSI      | argument 2                      | preserved          |
| RDX      | argument 3                      | preserved          |
| RCX      | —                               | clobbered (return RIP) |
| R11      | —                               | clobbered (return RFLAGS) |

C prototype shape: `long syscall(long nr, long a1, long a2, long a3)`
with `"syscall"` inline assembly clobbering `rcx` and `r11`.

The kernel clears IF, TF and DF on entry (SFMASK = 0x310): syscalls
run as short critical sections on the calling task's kernel stack.
RFLAGS on return always has IF=1, bit 1 = 1 (fixed), everything else 0.

## Result convention

- `>= 0`: success; the value is the syscall's natural return.
- `< 0`: `-errno`.

| errno | Name       | Meaning                                        |
|-------|------------|------------------------------------------------|
| 1     | EFAULT_INV | user pointer not canonical-user, or a page in range is absent |
| 2     | EFAULT_ARG | invalid fd / unsupported syscall number       |

## Syscalls

| #  | Name     | Prototype                          | Notes |
|----|----------|------------------------------------|-------|
| 1  | write    | `long write(long fd, const char *buf, long len)` | fd 1 (serial+fb console) or 2; buf must be a valid user range; returns len or -errno |
| 2  | exit     | `long exit(long code)              ` | terminates the calling task (no return); the exit code is recorded in the task and reported on serial |
| 3  | yield    | `long yield(void)                  ` | cooperative switch to the next task; returns 0 |
| 4  | getpid   | `long getpid(void)                 ` | returns the calling task's pid |
| 5  | uptime_ms | `long uptime_ms(void)             ` | milliseconds since timer start (100 Hz tick counter) |

Unassigned numbers return `-EFAULT_ARG` (2).

## Memory layout of a user program

- Entry RIP: `0x400000` region; the embedded programs are linked with
  `user/init.ld` (text/rodata/data/bss contiguous from `0x400000`,
  `_end` exported).
- Stack: top of the user half minus 136 bytes (SysV: RSP%16 == 8 at
  function entry), growing down; 64 KiB mapped.
- The kernel half of every user PML4 is shared with the kernel (same
  entries), but kernel pages are supervisor-only: ring 3 cannot read
  or write kernel memory; any attempt page-faults and the process is
  killed with a diagnostic.

## Faults from ring 3

A user task that raises #PF/#GP (bad pointer, privileged instruction
such as `hlt`, non-canonical access...) is killed:

```
user: fault #13 (General protection fault) at RIP 0x400014
user: task 'fault' (pid 5) killed: user-mode exception
```

The kernel and other tasks keep running. Syscall-side pointer
validation (`write`) returns `-errno` instead of faulting.

## Example

`user/init.c` (embedded in the kernel image) is the reference client:

```c
static inline long syscall3(unsigned long nr, unsigned long a,
                             unsigned long b, unsigned long c)
{
    long ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(nr), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return ret;
}
```

Rebuild the user blobs with `bash scripts/build_init.sh` (invoked by
`make` via `build/init.bin`).
