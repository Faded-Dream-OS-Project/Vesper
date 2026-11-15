/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <common.h>
#include <console.h>
#include <lib.h>
#include <limine.h>
#include <stdbool.h>

/* Framebuffer text console.

   Design notes (docs/REAL_HARDWARE.md summarizes the user-visible
   behaviour):
   - Works with any Limine pixel format: bpp 15/16/24/32 and
     pitch >= width*bpp/8. Colours are composed through the
     red/green/blue mask size+shift the bootloader reports (defaults:
     32bpp RGB888 positions).
   - Pixel integer scaling (2x/3x/4x) keeps text readable on
     high-resolution panels; scale is chosen from the panel height.
   - Every glyph is drawn with the current foreground colour, so
     PASS/FAIL/info lines can be colourised on the fly.
   - Scrolling keeps the last screenful of results on screen; boot
     never clears the console afterwards. */

#define TAB_WIDTH 8

#define FB_FONT_W 8
#define FB_FONT_H 16

/* 8x16 bitmap font: hand-drawn for Vesper (Faded Dream OS Project),
   stored as one byte per row (MSB = leftmost pixel). Origin:
   designed from scratch for this kernel in session 2 and extended in
   session 5; deliberately NOT derived from IBM VGA fonts or any
   third-party glyph set, so no licence can attach to
   it. Covers ASCII 0x20..0x7E (95 glyphs). */
static const u8 font8x16[95][16] = {
    /* ' ' */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* '!' */ {0,0,0,18,18,18,18,18,18,0,18,18,0,0,0,0},
    /* '"' */ {0,0,54,54,54,0,0,0,0,0,0,0,0,0,0,0},
    /* '#' */ {0,0,0,54,54,127,127,54,54,127,127,54,54,0,0,0},
    /* '$' */ {0,0,8,62,127,107,110,62,11,7,31,62,8,0,0,0},
    /* '%' */ {0,0,0,99,99,99,6,6,12,12,108,108,108,0,0,0},
    /* '&' */ {0,0,0,28,54,54,28,57,119,119,115,111,60,0,0,0},
    /* '\'' */ {0,0,12,12,12,0,0,0,0,0,0,0,0,0,0,0},
    /* '(' */ {0,0,3,6,12,24,24,24,24,24,12,6,3,0,0,0},
    /* ')' */ {0,0,48,24,12,6,6,6,6,6,12,24,48,0,0,0},
    /* '*' */ {0,0,0,0,36,36,126,126,36,36,0,0,0,0,0,0},
    /* '+' */ {0,0,0,0,0,8,8,62,62,8,8,0,0,0,0,0},
    /* ',' */ {0,0,0,0,0,0,0,0,0,24,24,24,48,0,0,0},
    /* '-' */ {0,0,0,0,0,0,0,126,126,0,0,0,0,0,0,0},
    /* '.' */ {0,0,0,0,0,0,0,0,0,0,24,24,0,0,0,0},
    /* '/' */ {0,0,0,0,1,3,7,14,28,56,112,96,0,0,0,0},
    /* '0' */ {0,0,28,54,99,99,107,107,115,115,99,54,28,0,0,0},
    /* '1' */ {0,0,8,24,56,8,8,8,8,8,8,8,62,0,0,0},
    /* '2' */ {0,0,62,99,99,3,6,12,24,48,96,99,127,0,0,0},
    /* '3' */ {0,0,62,99,99,3,6,28,6,3,99,99,62,0,0,0},
    /* '4' */ {0,0,6,14,30,54,102,102,127,127,6,6,6,0,0,0},
    /* '5' */ {0,0,127,96,96,124,110,3,3,3,99,99,62,0,0,0},
    /* '6' */ {0,0,28,54,96,96,124,110,99,99,99,54,28,0,0,0},
    /* '7' */ {0,0,127,127,99,3,6,12,12,24,24,24,24,0,0,0},
    /* '8' */ {0,0,62,99,99,99,62,62,99,99,99,99,62,0,0,0},
    /* '9' */ {0,0,62,99,99,99,62,59,3,3,6,54,28,0,0,0},
    /* ':' */ {0,0,0,0,24,24,0,0,0,0,24,24,0,0,0,0},
    /* ';' */ {0,0,0,0,24,24,0,0,0,24,24,24,48,0,0,0},
    /* '<' */ {0,0,0,1,3,7,14,28,56,28,14,7,3,1,0,0},
    /* '=' */ {0,0,0,0,0,126,126,0,0,126,126,0,0,0,0,0},
    /* '>' */ {0,0,0,64,112,56,28,14,7,14,28,56,112,64,0,0},
    /* '?' */ {0,0,62,99,99,3,6,12,12,12,0,12,12,0,0,0},
    /* '@' */ {0,0,62,99,107,107,107,110,110,110,108,100,62,0,0,0},
    /* 'A' */ {0,0,8,8,28,28,54,54,99,127,127,195,195,0,0,0},
    /* 'B' */ {0,0,124,102,99,99,102,124,110,99,99,99,126,0,0,0},
    /* 'C' */ {0,0,30,54,99,96,96,96,96,96,99,54,30,0,0,0},
    /* 'D' */ {0,0,120,108,102,99,99,99,99,99,102,108,120,0,0,0},
    /* 'E' */ {0,0,127,99,96,96,98,124,98,96,96,99,127,0,0,0},
    /* 'F' */ {0,0,127,99,96,96,98,124,98,96,96,96,96,0,0,0},
    /* 'G' */ {0,0,30,54,99,96,96,96,103,99,99,55,31,0,0,0},
    /* 'H' */ {0,0,99,99,99,99,99,127,127,99,99,99,99,0,0,0},
    /* 'I' */ {0,0,62,24,24,24,24,24,24,24,24,24,62,0,0,0},
    /* 'J' */ {0,0,31,6,6,6,6,6,6,6,6,102,60,0,0,0},
    /* 'K' */ {0,0,99,102,108,120,112,120,108,102,99,99,99,0,0,0},
    /* 'L' */ {0,0,96,96,96,96,96,96,96,96,96,99,127,0,0,0},
    /* 'M' */ {0,0,195,195,231,231,219,219,195,195,195,195,195,0,0,0},
    /* 'N' */ {0,0,195,227,227,243,219,219,207,207,199,199,195,0,0,0},
    /* 'O' */ {0,0,28,54,99,99,99,99,99,99,99,54,28,0,0,0},
    /* 'P' */ {0,0,126,99,99,99,102,126,96,96,96,96,96,0,0,0},
    /* 'Q' */ {0,0,28,54,99,99,99,99,99,99,103,54,28,3,1,0},
    /* 'R' */ {0,0,126,99,99,99,102,126,108,102,99,99,99,0,0,0},
    /* 'S' */ {0,0,62,99,96,96,60,14,3,3,99,99,62,0,0,0},
    /* 'T' */ {0,0,255,255,24,24,24,24,24,24,24,24,24,0,0,0},
    /* 'U' */ {0,0,99,99,99,99,99,99,99,99,99,99,62,0,0,0},
    /* 'V' */ {0,0,195,195,195,195,195,195,195,102,102,60,24,0,0,0},
    /* 'W' */ {0,0,195,195,195,195,195,219,219,231,231,102,102,0,0,0},
    /* 'X' */ {0,0,195,195,102,60,24,24,24,60,102,195,195,0,0,0},
    /* 'Y' */ {0,0,195,195,102,102,60,24,24,24,24,24,24,0,0,0},
    /* 'Z' */ {0,0,127,127,3,6,12,12,24,48,96,127,127,0,0,0},
    /* '[' */ {0,0,30,24,24,24,24,24,24,24,24,24,30,0,0,0},
    /* '\\' */ {0,0,0,0,64,96,112,56,28,14,7,3,0,0,0,0},
    /* ']' */ {0,0,30,6,6,6,6,6,6,6,6,6,30,0,0,0},
    /* '^' */ {0,24,24,36,36,54,54,99,0,0,0,0,0,0,0,0},
    /* '_' */ {0,0,0,0,0,0,0,0,0,0,0,0,255,255,0,0},
    /* '`' */ {0,0,24,24,12,0,0,0,0,0,0,0,0,0,0,0},
    /* 'a' */ {0,0,0,0,0,62,99,3,127,99,99,99,55,0,0,0},
    /* 'b' */ {0,0,96,96,96,124,102,99,99,99,99,102,124,0,0,0},
    /* 'c' */ {0,0,0,0,0,30,54,96,96,96,96,54,30,0,0,0},
    /* 'd' */ {0,0,3,3,3,31,55,99,99,99,99,55,31,0,0,0},
    /* 'e' */ {0,0,0,0,0,62,99,99,127,96,96,99,62,0,0,0},
    /* 'f' */ {0,0,14,28,24,24,126,24,24,24,24,24,24,0,0,0},
    /* 'g' */ {0,0,0,0,0,55,99,99,99,99,55,31,3,99,62,0},
    /* 'h' */ {0,0,96,96,96,124,102,99,99,99,99,99,99,0,0,0},
    /* 'i' */ {0,0,24,0,0,24,24,24,24,24,24,24,24,0,0,0},
    /* 'j' */ {0,0,6,0,0,6,6,6,6,6,6,6,6,102,60,0},
    /* 'k' */ {0,0,96,96,96,102,108,120,112,120,108,102,99,0,0,0},
    /* 'l' */ {0,0,56,24,24,24,24,24,24,24,24,24,56,0,0,0},
    /* 'm' */ {0,0,0,0,0,124,219,219,219,219,219,219,219,0,0,0},
    /* 'n' */ {0,0,0,0,0,124,102,99,99,99,99,99,99,0,0,0},
    /* 'o' */ {0,0,0,0,0,62,99,99,99,99,99,99,62,0,0,0},
    /* 'p' */ {0,0,0,0,0,124,102,99,99,99,102,124,96,96,96,0},
    /* 'q' */ {0,0,0,0,0,55,99,99,99,99,55,31,3,3,3,0},
    /* 'r' */ {0,0,0,0,0,110,119,102,96,96,96,96,96,0,0,0},
    /* 's' */ {0,0,0,0,0,62,99,96,60,14,3,99,62,0,0,0},
    /* 't' */ {0,0,24,24,24,126,24,24,24,24,24,24,14,0,0,0},
    /* 'u' */ {0,0,0,0,0,99,99,99,99,99,99,55,31,0,0,0},
    /* 'v' */ {0,0,0,0,0,99,99,99,99,99,54,28,8,0,0,0},
    /* 'w' */ {0,0,0,0,0,195,219,219,219,219,219,219,102,0,0,0},
    /* 'x' */ {0,0,0,0,0,99,99,54,28,28,54,99,99,0,0,0},
    /* 'y' */ {0,0,0,0,0,99,99,99,99,99,55,31,3,99,62,0},
    /* 'z' */ {0,0,0,0,0,127,127,6,12,24,48,127,127,0,0,0},
    /* '{' */ {0,0,14,24,24,24,24,48,24,24,24,24,14,0,0,0},
    /* '|' */ {0,0,24,24,24,24,24,24,24,24,24,24,24,0,0,0},
    /* '}' */ {0,0,56,12,12,12,12,6,12,12,12,12,56,0,0,0},
    /* '~' */ {0,0,0,0,0,0,110,219,0,0,0,0,0,0,0,0},
};

/* ---- colours (8-bit RGB components) ---- */

enum {
    RGB_TEXT  = 0x00D8D8D8u,   /* light grey */
    RGB_BLACK = 0x00000000u,
    RGB_PASS  = 0x0022C222u,   /* green */
    RGB_FAIL  = 0x00E03A3Au,   /* red */
    RGB_INFO  = 0x005AC8FAu,   /* cyan/blue */
    RGB_WARN  = 0x00E0A030u,   /* amber */
    RGB_PANIC = 0x00FF4040u,   /* bright red */
};

static struct limine_framebuffer *fb;
static u32 fb_w, fb_h, fb_pitch, fb_bpp;
static u32 cur_x, cur_y;      /* in characters */
static u32 cols, rows;
static u32 scale = 1;         /* pixel scale (1..4) */
static u32 fg_rgb = RGB_TEXT; /* current foreground */

/* mask info (defaults = classic 32bpp RGB888) */
static u8 r_size = 8, r_shift = 16;
static u8 g_size = 8, g_shift = 8;
static u8 b_size = 8, b_shift = 0;

static int fb_enabled = 1;

void fb_console_disable(void) { fb_enabled = 0; }
void fb_console_enable(void)  { fb_enabled = 1; }

bool fb_present(void)
{
    return fb != NULL;
}

/* Compose a pixel value in the framebuffer's own format from 8-bit RGB
   components using the mask sizes/shifts the bootloader reported. */
static u32 compose_rgb(u32 rgb)
{
    u32 r8 = (rgb >> 16) & 0xFF, g8 = (rgb >> 8) & 0xFF, b8 = rgb & 0xFF;
    u32 rv, gv, bv;

    /* scale 8-bit component into the mask width (left-aligned) */
    if (r_size == 0) {
        r_size = 8; r_shift = 16;
        g_size = 8; g_shift = 8;
        b_size = 8; b_shift = 0;
    }
    if (r_size >= 8)
        rv = r8 >> (r_size - 8);
    else
        rv = r8 >> (8 - r_size);   /* truncate (approximate) */
    if (g_size >= 8)
        gv = g8 >> (g_size - 8);
    else
        gv = g8 >> (8 - g_size);
    if (b_size >= 8)
        bv = b8 >> (b_size - 8);
    else
        bv = b8 >> (8 - b_size);

    return (rv << r_shift) | (gv << g_shift) | (bv << b_shift);
}

/* Raw pixel writer for the active format. bpp is trusted to be one of
   15/16/24/32 (Limine guarantees a supported mode); anything else
   falls back to 32bpp-like behaviour safely (writes within pitch). */
static void putpixel_rgb(u32 x, u32 y, u32 rgb)
{
    size_t off = (size_t)y * fb_pitch + (size_t)x * (fb_bpp / 8);
    u8 *p = (u8 *)fb->address + off;
    u32 v = compose_rgb(rgb);

    switch (fb_bpp) {
    case 32:
        *(u32 *)p = v;
        break;
    case 24:
        p[0] = (u8)(v & 0xFF);
        p[1] = (u8)((v >> 8) & 0xFF);
        p[2] = (u8)((v >> 16) & 0xFF);
        break;
    case 16:
        *(u16 *)p = (u16)v;
        break;
    case 15:
        /* RGB555: recompose 5-5-5 from the 8-bit components */
        {
            u32 r8 = (rgb >> 16) & 0xFF, g8 = (rgb >> 8) & 0xFF,
                b8 = rgb & 0xFF;
            *(u16 *)p = (u16)(((r8 >> 3) << 10) | ((g8 >> 3) << 5) |
                              (b8 >> 3));
        }
        break;
    default:
        *(u32 *)p = v;
        break;
    }
}

static void putpixel_black(u32 x, u32 y)
{
    size_t off = (size_t)y * fb_pitch + (size_t)x * (fb_bpp / 8);
    u8 *p = (u8 *)fb->address + off;

    switch (fb_bpp) {
    case 32:
        *(u32 *)p = 0;
        break;
    case 24:
        p[0] = 0; p[1] = 0; p[2] = 0;
        break;
    case 16:
    case 15:
        *(u16 *)p = 0;
        break;
    default:
        *(u32 *)p = 0;
        break;
    }
}

/* Draw one glyph cell (cursor position in characters) with the given
   foreground colour, honouring the pixel scale. */
static void fb_draw_char(u32 cx, u32 cy, char ch, u32 rgb)
{
    if (ch < 0x20 || ch > 0x7E)
        ch = '?';

    const u8 *glyph = font8x16[ch - 0x20];
    u32 bx = cx * FB_FONT_W * scale;
    u32 by = cy * FB_FONT_H * scale;

    for (u32 r = 0; r < FB_FONT_H; r++) {
        u8 bits = glyph[r];
        for (u32 c = 0; c < FB_FONT_W; c++) {
            u32 on = (bits & (0x80u >> c)) != 0;
            if (scale == 1) {
                if (on)
                    putpixel_rgb(bx + c, by + r, rgb);
                else
                    putpixel_black(bx + c, by + r);
            } else {
                for (u32 sy = 0; sy < scale; sy++) {
                    for (u32 sx = 0; sx < scale; sx++) {
                        u32 x = bx + c * scale + sx;
                        u32 y = by + r * scale + sy;
                        if (on)
                            putpixel_rgb(x, y, rgb);
                        else
                            putpixel_black(x, y);
                    }
                }
            }
        }
    }
}

/* Hardware cursor: invert a thin bar under the cursor cell. */
static void fb_cursor_on(void)
{
    u32 bx = cur_x * FB_FONT_W * scale;
    u32 by = cur_y * FB_FONT_H * scale;
    u32 bar_h = 2 * scale;
    u32 bar_y = by + FB_FONT_H * scale - bar_h;

    for (u32 r = 0; r < bar_h; r++) {
        for (u32 c = 0; c < FB_FONT_W * scale; c++) {
            u32 x = bx + c, y = bar_y + r;
            if (x < fb_w && y < fb_h)
                putpixel_rgb(x, y, RGB_TEXT);
        }
    }
}

static void fb_scroll_1(void)
{
    if (!fb)
        return;
    u8 *base = (u8 *)fb->address;
    size_t row_bytes = (size_t)fb_pitch * FB_FONT_H * scale;

    if (row_bytes >= (size_t)fb_pitch * fb_h)
        return;   /* single text row: nothing to scroll */
    memmove(base, base + row_bytes,
            (size_t)fb_pitch * fb_h - row_bytes);
    memset(base + (size_t)fb_pitch * fb_h - row_bytes, 0, row_bytes);
}

void fb_init(struct limine_framebuffer *framebuffer)
{
    if (!framebuffer || !framebuffer->address)
        return;
    fb = framebuffer;
    fb_w = framebuffer->width;
    fb_h = framebuffer->height;
    fb_pitch = framebuffer->pitch;
    fb_bpp = framebuffer->bpp;

    if (fb_bpp != 15 && fb_bpp != 16 && fb_bpp != 24 && fb_bpp != 32)
        return;   /* unsupported format: stay serial-only */
    if (fb_pitch < (u32)(fb_w * (fb_bpp / 8)))
        fb_pitch = fb_w * (fb_bpp / 8);   /* defensive */

    /* Pixel scale so text stays readable on big panels: aim for at
       least ~28 text rows; 1080p+ gets 2x, 4K gets 3-4x. */
    scale = 1;
    while (scale < 4 &&
           fb_h / (FB_FONT_H * (scale + 1)) >= 30 &&
           fb_w / (FB_FONT_W * (scale + 1)) >= 60)
        scale++;

    cols = fb_w / (FB_FONT_W * scale);
    rows = fb_h / (FB_FONT_H * scale);
    cur_x = 0;
    cur_y = 0;
    fg_rgb = RGB_TEXT;

    /* Use the bootloader's mask layout when it reports one. */
    if (framebuffer->red_mask_size + framebuffer->green_mask_size +
        framebuffer->blue_mask_size > 0) {
        r_size = framebuffer->red_mask_size;
        r_shift = framebuffer->red_mask_shift;
        g_size = framebuffer->green_mask_size;
        g_shift = framebuffer->green_mask_shift;
        b_size = framebuffer->blue_mask_size;
        b_shift = framebuffer->blue_mask_shift;
    }

    fb_clear();
}

void fb_clear(void)
{
    if (!fb)
        return;
    for (u32 y = 0; y < fb_h; y++) {
        u8 *line = (u8 *)fb->address + (size_t)y * fb_pitch;
        memset(line, 0, (size_t)fb_w * (fb_bpp / 8));
    }
    cur_x = 0;
    cur_y = 0;
}

/* ---- colour escape API ---- */

void fb_set_color(u32 color)
{
    switch (color) {
    case FB_COLOR_PASS:  fg_rgb = RGB_PASS;  break;
    case FB_COLOR_FAIL:  fg_rgb = RGB_FAIL;  break;
    case FB_COLOR_INFO:  fg_rgb = RGB_INFO;  break;
    case FB_COLOR_WARN:  fg_rgb = RGB_WARN;  break;
    case FB_COLOR_PANIC: fg_rgb = RGB_PANIC; break;
    default:             fg_rgb = RGB_TEXT;  break;
    }
}

u32 fb_get_color(void)
{
    if (fg_rgb == RGB_PASS)  return FB_COLOR_PASS;
    if (fg_rgb == RGB_FAIL)  return FB_COLOR_FAIL;
    if (fg_rgb == RGB_INFO)  return FB_COLOR_INFO;
    if (fg_rgb == RGB_WARN)  return FB_COLOR_WARN;
    if (fg_rgb == RGB_PANIC) return FB_COLOR_PANIC;
    return FB_COLOR_TEXT;
}

void fb_putc(char c)
{
    if (!fb_enabled || !fb)
        return;

    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\r') {
        cur_x = 0;
    } else if (c == '\t') {
        cur_x = (cur_x / TAB_WIDTH + 1) * TAB_WIDTH;
        if (cur_x >= cols) {
            cur_x = 0;
            cur_y++;
        }
    } else if (c == '\b') {
        if (cur_x > 0)
            cur_x--;
    } else {
        fb_draw_char(cur_x, cur_y, c, fg_rgb);
        cur_x++;
    }

    if (cur_x >= cols) {
        cur_x = 0;
        cur_y++;
    }
    if (cur_y >= rows) {
        fb_scroll_1();
        cur_y = rows - 1;
    }

    if (cur_x < cols && cur_y < rows)
        fb_cursor_on();   /* solid bar under the next cell */
}
