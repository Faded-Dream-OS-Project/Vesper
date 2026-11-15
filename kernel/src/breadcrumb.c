/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <breadcrumb.h>

/* Early-boot breadcrumbs (session 7): before the console exists the
   kernel is blind — a real-hardware black screen gave no clue how far
   boot reached. Each init stage now pokes BOTH:
     - a 16x16 coloured square in a row at the top-left of the
       framebuffer, written with plain stores (no shadow buffer, no
       console, works the instant Limine's framebuffer response is
       valid), and
     - one stage code byte to port 0xE9 (QEMU -debugcon; real hardware
       with a POST card shows the last stage reached).
   The squares are cleared once fb_init() takes the screen over; the
   final code stays on 0xE9 and the stage count is printed on the
   diagnostics screen, so a photo still tells how far boot got.

   Safe by construction: nothing here runs before the bootloader gave
   us a framebuffer pointer, every store is bounds-checked against
   width/height/pitch, and bpp<32 writes 3 bytes per pixel. */

#define BC_SQUARE 16   /* 16x16 px per stage */
#define BC_GAP    4   /* gap between squares */

/* stage codes: also the value written to port 0xE9 */
enum {
    BC_ENTRY    = 1,   /* kmain first instruction           */
    BC_CPU      = 2,   /* cpu_sanitize                      */
    BC_FB      = 3,   /* framebuffer response accepted     */
    BC_CONSOLE = 4,   /* console init / first kprintf      */
    BC_GDT      = 5,
    BC_IDT      = 6,
    BC_PIC      = 7,
    BC_PMM      = 8,
    BC_VMM      = 9,
    BC_ACPI     = 10,
    BC_APIC     = 11,
    BC_TIMER    = 12,
    BC_SCHED    = 13,
    BC_USER     = 14,
    BC_DONE     = 15,   /* end of kmain init sequence       */
};

static struct limine_framebuffer *bc_fb;
static int bc_squares;   /* squares drawn so far (only while fb is ours) */
static int bc_count;    /* ALL stages recorded, framebuffer or not */
static u8  bc_last;     /* last code written to 0xE9 */

/* RGB for each stage: distinct colours so a photo tells WHICH stage
   was reached last, not just a count. */
static const u32 bc_rgb[] = {
    0x00FFFFFF,   /* 1 entry     white  */
    0x00E0A030,   /* 2 cpu       amber  */
    0x005AC8FA,   /* 3 fb        blue   */
    0x0000C060,   /* 4 console   teal   */
    0x00C060C0,   /* 5 gdt       purple */
    0x00E03A3A,   /* 6 idt       red    */
    0x00A05050,   /* 7 pic       dull red*/
    0x0022C222,   /* 8 pmm       green  */
    0x0000E090,   /* 9 vmm       cyan   */
    0x00E0E030,   /* 10 acpi     yellow */
    0x3070FF30,   /* 11 apic     grass  */
    0x00B0B0FF,   /* 12 timer    sky    */
    0xFF8000A0,   /* 13 sched    pink   */
    0xA0A0A0A0,   /* 14 user     grey   */
    0x00FF8000,   /* 15 done     orange */
};

/* ---- tiny plain-store pixel blitter (no libc, no shadow) ---- */

static void bc_putpixel(u32 x, u32 y, u32 rgb)
{
    u8 *base = (u8 *)bc_fb->address;
    u64 pitch = bc_fb->pitch;
    u16 bpp = bc_fb->bpp;
    u8 *p = base + (u64)y * pitch + (u64)x * (bpp / 8);

    switch (bpp) {
    case 32:
        *(u32 *)p = rgb;
        break;
    case 24:
        p[0] = (u8)(rgb & 0xFF);
        p[1] = (u8)((rgb >> 8) & 0xFF);
        p[2] = (u8)((rgb >> 16) & 0xFF);
        break;
    case 16:
        *(u16 *)p = (u16)(((rgb >> 16 & 0xFF) >> 3) << 11 |
                          ((rgb >> 8 & 0xFF) >> 2) << 5 |
                          ((rgb & 0xFF) >> 3));
        break;
    default:
        /* 15bpp and anything odd: best-effort 565 write */
        *(u16 *)p = (u16)(((rgb >> 16 & 0xFF) >> 3) << 10 |
                          ((rgb >> 8 & 0xFF) >> 3) << 5 |
                          ((rgb & 0xFF) >> 3));
        break;
    }
}

/* Draw one square for stage index (0-based stage-1). */
static void bc_draw_square(int stage)
{
    u32 x0 = (u32)stage * (BC_SQUARE + BC_GAP);
    const u32 rgb = bc_rgb[stage];

    for (u32 y = 0; y < BC_SQUARE; y++) {
        for (u32 x = 0; x < BC_SQUARE; x++) {
            u32 px = x0 + x, py = 2 + y;   /* 2px top margin */
            if (px < bc_fb->width && py < bc_fb->height)
                bc_putpixel(px, py, rgb);
        }
    }
}

/* Clear the breadcrumb strip (called from fb_init before it claims
   the screen: the shadow buffer covers the whole screen anyway, but
   clearing keeps the handover explicit and tidy). */
static void bc_clear_squares(void)
{
    int n = bc_squares;
    u32 x0_end = (u32)n * (BC_SQUARE + BC_GAP);

    for (u32 y = 0; y < BC_SQUARE + 4; y++) {
        for (u32 x = 0; x < x0_end; x++) {
            u32 px = x, py = y;
            if (px < bc_fb->width && py < bc_fb->height)
                bc_putpixel(px, py, 0);
        }
    }
}

/* Post-console persistence: after the handover the CONSOLE owns the
   whole screen (50 text rows at 1280x800/1x, more on bigger panels,
   and scrolling rewrites every row), so any VRAM mark drawn here would
   be overwritten by the next flush. Persistence therefore lives on
   port 0xE9 (last code readable with a POST card) and in the
   'boot stages' line printed on the diagnostics screen; both are
   verified behaviors. */
void breadcrumb_fb(struct limine_framebuffer *fb)
{
    bc_fb = fb;
}

void breadcrumb_stage(u8 code)
{
    bc_last = code;
    bc_count++;                    /* every stage counted, fb or not */
    outb(0xE9, code);   /* QEMU -debugcon char device / POST card */

    if (bc_fb && bc_squares < (int)(sizeof(bc_rgb) / sizeof(bc_rgb[0]))) {
        bc_draw_square(bc_squares);
        bc_squares++;
    }
}

void breadcrumb_handover(void)
{
    if (bc_fb)
        bc_clear_squares();
    bc_fb = NULL;   /* console owns the screen from here on */
}

u8 breadcrumb_last(void)
{
    return bc_last;
}

int breadcrumb_count(void)
{
    return bc_count;
}
