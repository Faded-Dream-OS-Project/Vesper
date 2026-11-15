/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <console.h>
#include <lib.h>
#include <stdbool.h>

/* Minimal kprintf: %s %c %d %i %u %x %p %% with 'l'/'z' length
   modifiers, '#' (0x prefix), zero padding and field width
   (e.g. %08x, %#lx). Output goes to BOTH the serial line and the
   framebuffer (whichever exists).

   Colour tags (framebuffer only, stripped from serial output):
     $P = PASS green    $F = FAIL red
     $I = INFO cyan     $W = WARN amber
     $R = PANIC red     $T = back to default text colour
   They may appear anywhere in the format string. */

static void apply_tag(char t)
{
    switch (t) {
    case 'P': fb_set_color(FB_COLOR_PASS); break;
    case 'F': fb_set_color(FB_COLOR_FAIL); break;
    case 'I': fb_set_color(FB_COLOR_INFO); break;
    case 'W': fb_set_color(FB_COLOR_WARN); break;
    case 'R': fb_set_color(FB_COLOR_PANIC); break;
    case 'T': fb_set_color(FB_COLOR_TEXT); break;
    default: break;
    }
}

static void pad_zeroes(int count)
{
    while (count-- > 0)
        kputchar('0');
}

static void emit_str(const char *s)
{
    if (!s)
        s = "(null)";
    while (*s)
        kputchar(*s++);
}

void kvprintf(const char *fmt, va_list args)
{
    char buf[72];

    for (; *fmt; fmt++) {
        if (*fmt == '$' && fmt[1] != '\0') {
            /* colour tag: framebuffer only */
            apply_tag(fmt[1]);
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            kputchar(*fmt);
            continue;
        }
        fmt++;

        /* flags */
        bool alt = false;
        bool zero_pad = false;
        for (;;) {
            if (*fmt == '#') {
                alt = true;
                fmt++;
            } else if (*fmt == '0') {
                zero_pad = true;
                fmt++;
            } else {
                break;
            }
        }

        /* field width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* length modifiers (consumed; ints are int-sized on this ABI) */
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') {
            if (*fmt == 'h') {
                /* 'h' narrows; harmless to ignore given varargs
                   promotion, but consume both hh and h */
                fmt++;
                if (*fmt == 'h')
                    fmt++;
            } else {
                fmt++;
            }
        }

        switch (*fmt) {
        case '%':
            kputchar('%');
            break;
        case 'c':
            kputchar((char)va_arg(args, int));
            break;
        case 's':
            emit_str(va_arg(args, const char *));
            break;
        case 'd':
        case 'i':
            itoa(va_arg(args, i64), buf, 10);
            goto number;
        case 'u':
            utoa(va_arg(args, u64), buf, 10);
            goto number;
        case 'x':
            utoa(va_arg(args, u64), buf, 16);
            if (alt) {
                kputchar('0');
                kputchar('x');
            }
            goto number;
        case 'p': {
            void *ptr = va_arg(args, void *);
            kputchar('0');
            kputchar('x');
            utoa((u64)(uintptr_t)ptr, buf, 16);
            int len = (int)strlen(buf);
            if (zero_pad && width > len)
                pad_zeroes(width - len);
            emit_str(buf);
            break;
        }
        default:
            /* unknown conversion: print it raw */
            kputchar('%');
            kputchar(*fmt);
            break;
        number:
            {
                int len = (int)strlen(buf);
                if (zero_pad && width > len)
                    pad_zeroes(width - len);
                emit_str(buf);
            }
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
