/*
 * QEMU JAZZ LED emulator.
 *
 * Copyright (c) 2007 Hervé Poussineau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw.h"
#include "mips.h"
#include "console.h"
#include "pixel_ops.h"

//#define DEBUG_LED

typedef enum {
    REDRAW_NONE = 0, REDRAW_SEGMENTS = 1, REDRAW_BACKGROUND = 2,
} screen_state_t;

typedef struct LedState {
    uint8_t segments;
    DisplayState *ds;
    screen_state_t state;
} LedState;

static uint32_t led_readb(void *opaque, target_phys_addr_t addr)
{
    LedState *s = opaque;
    uint32_t val;

    switch (addr) {
        case 0:
            val = s->segments;
            break;
        default:
#ifdef DEBUG_LED
            printf("jazz led: invalid read [0x%x]\n", relative_addr);
#endif
            val = 0;
    }

    return val;
}

static uint32_t led_readw(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = led_readb(opaque, addr) << 8;
    v |= led_readb(opaque, addr + 1);
#else
    v = led_readb(opaque, addr);
    v |= led_readb(opaque, addr + 1) << 8;
#endif
    return v;
}

static uint32_t led_readl(void *opaque, target_phys_addr_t addr)
{
    uint32_t v;
#ifdef TARGET_WORDS_BIGENDIAN
    v = led_readb(opaque, addr) << 24;
    v |= led_readb(opaque, addr + 1) << 16;
    v |= led_readb(opaque, addr + 2) << 8;
    v |= led_readb(opaque, addr + 3);
#else
    v = led_readb(opaque, addr);
    v |= led_readb(opaque, addr + 1) << 8;
    v |= led_readb(opaque, addr + 2) << 16;
    v |= led_readb(opaque, addr + 3) << 24;
#endif
    return v;
}

static void led_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
    LedState *s = opaque;

    switch (addr) {
        case 0:
            s->segments = val;
            s->state |= REDRAW_SEGMENTS;
            break;
        default:
#ifdef DEBUG_LED
            printf("jazz led: invalid write of 0x%02x at [0x%x]\n", val, relative_addr);
#endif
            break;
    }
}

static void led_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    led_writeb(opaque, addr, (val >> 8) & 0xff);
    led_writeb(opaque, addr + 1, val & 0xff);
#else
    led_writeb(opaque, addr, val & 0xff);
    led_writeb(opaque, addr + 1, (val >> 8) & 0xff);
#endif
}

static void led_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    led_writeb(opaque, addr, (val >> 24) & 0xff);
    led_writeb(opaque, addr + 1, (val >> 16) & 0xff);
    led_writeb(opaque, addr + 2, (val >> 8) & 0xff);
    led_writeb(opaque, addr + 3, val & 0xff);
#else
    led_writeb(opaque, addr, val & 0xff);
    led_writeb(opaque, addr + 1, (val >> 8) & 0xff);
    led_writeb(opaque, addr + 2, (val >> 16) & 0xff);
    led_writeb(opaque, addr + 3, (val >> 24) & 0xff);
#endif
}

static CPUReadMemoryFunc *led_read[3] = {
    led_readb,
    led_readw,
    led_readl,
};

static CPUWriteMemoryFunc *led_write[3] = {
    led_writeb,
    led_writew,
    led_writel,
};

/***********************************************************/
/* jazz_led display */

static void draw_horizontal_line(DisplayState *ds, int posy, int posx1, int posx2, uint32_t color)
{
    uint8_t *d;
    int x, bpp;

    bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
    d = ds_get_data(ds) + ds_get_linesize(ds) * posy + bpp * posx1;
    switch(bpp) {
        case 1:
            for (x = posx1; x <= posx2; x++) {
                *((uint8_t *)d) = color;
                d++;
            }
            break;
        case 2:
            for (x = posx1; x <= posx2; x++) {
                *((uint16_t *)d) = color;
                d += 2;
            }
            break;
        case 4:
            for (x = posx1; x <= posx2; x++) {
                *((uint32_t *)d) = color;
                d += 4;
            }
            break;
    }
}

static void draw_vertical_line(DisplayState *ds, int posx, int posy1, int posy2, uint32_t color)
{
    uint8_t *d;
    int y, bpp;

    bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
    d = ds_get_data(ds) + ds_get_linesize(ds) * posy1 + bpp * posx;
    switch(bpp) {
        case 1:
            for (y = posy1; y <= posy2; y++) {
                *((uint8_t *)d) = color;
                d += ds_get_linesize(ds);
            }
            break;
        case 2:
            for (y = posy1; y <= posy2; y++) {
                *((uint16_t *)d) = color;
                d += ds_get_linesize(ds);
            }
            break;
        case 4:
            for (y = posy1; y <= posy2; y++) {
                *((uint32_t *)d) = color;
                d += ds_get_linesize(ds);
            }
            break;
    }
}

static void jazz_led_update_display(void *opaque)
{
    LedState *s = opaque;
    DisplayState *ds = s->ds;
    uint8_t *d1;
    uint32_t color_segment, color_led;
    int y, bpp;

    if (s->state & REDRAW_BACKGROUND) {
        /* clear screen */
        bpp = (ds_get_bits_per_pixel(ds) + 7) >> 3;
        d1 = ds_get_data(ds);
        for (y = 0; y < ds_get_height(ds); y++) {
            memset(d1, 0x00, ds_get_width(ds) * bpp);
            d1 += ds_get_linesize(ds);
        }
    }

    if (s->state & REDRAW_SEGMENTS) {
        /* set colors according to bpp */
        switch (ds_get_bits_per_pixel(ds)) {
            case 8:
                color_segment = rgb_to_pixel8(0xaa, 0xaa, 0xaa);
                color_led = rgb_to_pixel8(0x00, 0xff, 0x00);
                break;
            case 15:
                color_segment = rgb_to_pixel15(0xaa, 0xaa, 0xaa);
                color_led = rgb_to_pixel15(0x00, 0xff, 0x00);
                break;
            case 16:
                color_segment = rgb_to_pixel16(0xaa, 0xaa, 0xaa);
                color_led = rgb_to_pixel16(0x00, 0xff, 0x00);
            case 24:
                color_segment = rgb_to_pixel24(0xaa, 0xaa, 0xaa);
                color_led = rgb_to_pixel24(0x00, 0xff, 0x00);
                break;
            case 32:
                color_segment = rgb_to_pixel32(0xaa, 0xaa, 0xaa);
                color_led = rgb_to_pixel32(0x00, 0xff, 0x00);
                break;
            default:
                return;
        }

        /* display segments */
        draw_horizontal_line(ds, 40, 10, 40, (s->segments & 0x02) ? color_segment : 0);
        draw_vertical_line(ds, 10, 10, 40, (s->segments & 0x04) ? color_segment : 0);
        draw_vertical_line(ds, 10, 40, 70, (s->segments & 0x08) ? color_segment : 0);
        draw_horizontal_line(ds, 70, 10, 40, (s->segments & 0x10) ? color_segment : 0);
        draw_vertical_line(ds, 40, 40, 70, (s->segments & 0x20) ? color_segment : 0);
        draw_vertical_line(ds, 40, 10, 40, (s->segments & 0x40) ? color_segment : 0);
        draw_horizontal_line(ds, 10, 10, 40, (s->segments & 0x80) ? color_segment : 0);

        /* display led */
        if (!(s->segments & 0x01))
            color_led = 0; /* black */
        draw_horizontal_line(ds, 68, 50, 50, color_led);
        draw_horizontal_line(ds, 69, 49, 51, color_led);
        draw_horizontal_line(ds, 70, 48, 52, color_led);
        draw_horizontal_line(ds, 71, 49, 51, color_led);
        draw_horizontal_line(ds, 72, 50, 50, color_led);
    }

    s->state = REDRAW_NONE;
    dpy_update(ds, 0, 0, ds_get_width(ds), ds_get_height(ds));
}

static void jazz_led_invalidate_display(void *opaque)
{
    LedState *s = opaque;
    s->state |= REDRAW_SEGMENTS | REDRAW_BACKGROUND;
}

static void jazz_led_screen_dump(void *opaque, const char *filename)
{
    printf("jazz_led_screen_dump() not implemented\n");
}

static void jazz_led_text_update(void *opaque, console_ch_t *chardata)
{
    LedState *s = opaque;
    char buf[2];

    dpy_cursor(s->ds, -1, -1);
    qemu_console_resize(s->ds, 2, 1);

    /* TODO: draw the segments */
    snprintf(buf, 2, "%02hhx\n", s->segments);
    console_write_ch(chardata++, 0x00200100 | buf[0]);
    console_write_ch(chardata++, 0x00200100 | buf[1]);

    dpy_update(s->ds, 0, 0, 2, 1);
}

void jazz_led_init(target_phys_addr_t base)
{
    LedState *s;
    int io;

    s = qemu_mallocz(sizeof(LedState));

    s->state = REDRAW_SEGMENTS | REDRAW_BACKGROUND;

    io = cpu_register_io_memory(0, led_read, led_write, s);
    cpu_register_physical_memory(base, 1, io);

    s->ds = graphic_console_init(jazz_led_update_display,
                                 jazz_led_invalidate_display,
                                 jazz_led_screen_dump,
                                 jazz_led_text_update, s);
    qemu_console_resize(s->ds, 60, 80);
}
