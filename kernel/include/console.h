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
void fb_wc_init(void);   /* post-vmm_init: point fb_hw at the WC window */
void fb_console_disable(void);
void fb_console_enable(void);
bool fb_present(void);
void fb_clear(void);
void serial_putc(char c);
void fb_putc(char c);
void kputchar(char c);        /* writes to serial AND framebuffer */

/* Console colours (fb.c + console.c + panic.c). */
enum fb_color {
    FB_COLOR_TEXT = 0,   /* default light grey */
    FB_COLOR_PASS,       /* green  - self-test PASS lines */
    FB_COLOR_FAIL,       /* red    - self-test FAIL lines + panics */
    FB_COLOR_INFO,       /* cyan   - diagnostics section headers */
    FB_COLOR_WARN,       /* amber  - degraded / unusual conditions */
    FB_COLOR_PANIC,      /* bright red - panic banner */
};

void fb_set_color(u32 color);
u32  fb_get_color(void);

/* Shadow-buffer console control + stats (session 6). */
void fb_flush(void);          /* push all dirty rows to video RAM */
void fb_tick(void);           /* ~30 Hz auto-flush cadence */
u64  fb_flush_ms(void);       /* total ms spent flushing (TSC) */
u32  fb_scale(void);          /* text pixel scale in use */
int  fb_pat_wc(void);         /* 1 = framebuffer mapped write-combining */
u32  fb_width(void);
u32  fb_height(void);

/* Boot diagnostics screen (kernel/src/bootinfo.c). */
extern int boot_mode_uefi;   /* 1 = UEFI, 0 = BIOS */
void bootinfo_print(void);

/* Kernel cmdline token check (kernel/src/kmain.c). */
int cmdline_has(const char *flag);

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
