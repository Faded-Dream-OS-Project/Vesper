/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_CONSOLE_H
#define VESPER_CONSOLE_H

#include <common.h>
#include <stdarg.h>
#include <stdbool.h>
#include <limine.h>   /* thirdparty/limine/limine.h, on the include path */

/* Both output channels are initialised as early as possible and are
   independent fallbacks for each other: if one is absent the other
   still works. */
void console_init(void);
void serial_init(void);
bool serial_present(void);

/* Framebuffer console (kernel/src/fb.c). fb_init is called from kmain
   with the Limine framebuffer response. */
void fb_init(struct limine_framebuffer *framebuffer);
bool fb_present(void);
void fb_clear(void);
void serial_putc(char c);
void fb_putc(char c);
void kputchar(char c);        /* writes to serial AND framebuffer */

/* Minimal printf-style formatting (subset: %s %c %d %u %x %p %% and
   width/zero padding like %08x). */
void kvprintf(const char *fmt, va_list args)
    __attribute__((format(printf, 1, 0)));
void kprintf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

/* Panics (declared here so early code can call it; implemented in
   panic.c). */
void panic(const char *fmt, ...)
    __attribute__((format(printf, 1, 2), noreturn));

#endif /* VESPER_CONSOLE_H */
