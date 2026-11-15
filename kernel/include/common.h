/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_COMMON_H
#define VESPER_COMMON_H

#include <stdint.h>
#include <stddef.h>

#define VESPER_VERSION "0.1.0"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

static inline void outb(u16 port, u8 val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void outw(u16 port, u16 val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

static inline u16 inw(u16 port)
{
    u16 ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void halt(void)
{
    __asm__ volatile ("hlt" ::: "memory");
}

static inline void cli(void)
{
    __asm__ volatile ("cli" ::: "memory");
}

static inline void sti(void)
{
    __asm__ volatile ("sti" ::: "memory");
}

static inline u64 read_cr0(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr2(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline u64 read_cr3(void)
{
    u64 v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(u64 v)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline u64 read_msr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static inline void write_msr(u32 msr, u64 val)
{
    __asm__ volatile ("wrmsr" : :
                      "a"((u32)(val & 0xFFFFFFFFu)),
                      "d"((u32)(val >> 32)),
                      "c"(msr));
}

#endif /* VESPER_COMMON_H */
