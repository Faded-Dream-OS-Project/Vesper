/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_LIB_H
#define VESPER_LIB_H

#include <common.h>

/* ---- memory ---- */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

/* ---- string ---- */
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);

/* ---- number formatting ---- */
static inline char *utoa(u64 v, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[72];
    int i = 0;

    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (v > 0 && i < 70) {
        tmp[i++] = digits[v % (u64)base];
        v /= (u64)base;
    }
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - 1 - j];
    buf[i] = '\0';
    return buf;
}

static inline char *itoa(i64 v, char *buf, int base)
{
    if (v < 0) {
        buf[0] = '-';
        utoa((u64)(-(v + 1)) + 1, buf + 1, base);
        return buf;
    }
    return utoa((u64)v, buf, base);
}

#endif /* VESPER_LIB_H */
