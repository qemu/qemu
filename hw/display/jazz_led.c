/*
 * QEMU JAZZ LED emulator.
 *
 * Copyright (c) 2007-2012 Herve Poussineau
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

#include "qemu-common.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "trace.h"
#include "hw/sysbus.h"

typedef enum {
    REDRAW_NONE = 0, REDRAW_SEGMENTS = 1, REDRAW_BACKGROUND = 2,
} screen_state_t;

#define TYPE_JAZZ_LED "jazz-led"
#define JAZZ_LED(obj) OBJECT_CHECK(LedState, (obj), TYPE_JAZZ_LED)

typedef struct LedState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t segments;
    QemuConsole *con;
    screen_state_t state;
} LedState;

static uint64_t jazz_led_read(void *opaque, hwaddr addr,
                              unsigned int size)
{
    LedState *s = opaque;
    uint8_t val;

    val = s->segments;
    trace_jazz_led_read(addr, val);

    return val;
}

static void jazz_led_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned int size)
{
    LedState *s = opaque;
    uint8_t new_val = val & 0xff;

    trace_jazz_led_write(addr, new_val);

    s->segments = new_val;
    s->state |= REDRAW_SEGMENTS;
}

static const MemoryRegionOps led_ops = {
    .read = jazz_led_read,
    .write = jazz_led_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
};

/***********************************************************/
/* jazz_led display */

static void draw_horizontal_line(DisplaySurface *ds,
                                 int posy, int posx1, int posx2,
                                 uint32_t color)
{
    uint8_t *d;
    int x, bpp;

    bpp = (surface_bits_per_pixel(ds) + 7) >> 3;
    d = surface_data(ds) + surface_stride(ds) * posy + bpp * posx1;
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

static void draw_vertical_line(DisplaySurface *ds,
                               int posx, int posy1, int posy2,
                               uint32_t color)
{
    uint8_t *d;
    int y, bpp;

    bpp = (surface_bits_per_pixel(ds) + 7) >> 3;
    d = surface_data(ds) + surface_stride(ds) * posy1 + bpp * posx;
    switch(bpp) {
        case 1:
            for (y = posy1; y <= posy2; y++) {
                *((uint8_t *)d) = color;
                d += surface_stride(ds);
            }
            break;
        case 2:
            for (y = posy1; y <= posy2; y++) {
                *((uint16_t *)d) = color;
                d += surface_stride(ds);
            }
            break;
        case 4:
            for (y = posy1; y <= posy2; y++) {
                *((uint32_t *)d) = color;
                d += surface_stride(ds);
            }
            break;
    }
}

static void jazz_led_update_display(void *opaque)
{
    LedState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t *d1;
    uint32_t color_segment, color_led;
    int y, bpp;

    if (s->state & REDRAW_BACKGROUND) {
        /* clear screen */
        bpp = (surface_bits_per_pixel(surface) + 7) >> 3;
        d1 = surface_data(surface);
        for (y = 0; y < surface_height(surface); y++) {
            memset(d1, 0x00, surface_width(surface) * bpp);
            d1 += surface_stride(surface);
        }
    }

    if (s->state & REDRAW_SEGMENTS) {
        /* set colors according to bpp */
        switch (surface_bits_per_pixel(surface)) {
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
                break;
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
        draw_horizontal_line(surface, 40, 10, 40,
                             (s->segments & 0x02) ? color_segment : 0);
        draw_vertical_line(surface, 10, 10, 40,
                           (s->segments & 0x04) ? color_segment : 0);
        draw_vertical_line(surface, 10, 40, 70,
                           (s->segments & 0x08) ? color_segment : 0);
        draw_horizontal_line(surface, 70, 10, 40,
                             (s->segments & 0x10) ? color_segment : 0);
        draw_vertical_line(surface, 40, 40, 70,
                           (s->segments & 0x20) ? color_segment : 0);
        draw_vertical_line(surface, 40, 10, 40,
                           (s->segments & 0x40) ? color_segment : 0);
        draw_horizontal_line(surface, 10, 10, 40,
                             (s->segments & 0x80) ? color_segment : 0);

        /* display led */
        if (!(s->segments & 0x01))
            color_led = 0; /* black */
        draw_horizontal_line(surface, 68, 50, 50, color_led);
        draw_horizontal_line(surface, 69, 49, 51, color_led);
        draw_horizontal_line(surface, 70, 48, 52, color_led);
        draw_horizontal_line(surface, 71, 49, 51, color_led);
        draw_horizontal_line(surface, 72, 50, 50, color_led);
    }

    s->state = REDRAW_NONE;
    dpy_gfx_update(s->con, 0, 0,
                   surface_width(surface), surface_height(surface));
}

static void jazz_led_invalidate_display(void *opaque)
{
    LedState *s = opaque;
    s->state |= REDRAW_SEGMENTS | REDRAW_BACKGROUND;
}

static void jazz_led_text_update(void *opaque, console_ch_t *chardata)
{
    LedState *s = opaque;
    char buf[2];

    dpy_text_cursor(s->con, -1, -1);
    qemu_console_resize(s->con, 2, 1);

    /* TODO: draw the segments */
    snprintf(buf, 2, "%02hhx\n", s->segments);
    console_write_ch(chardata++, 0x00200100 | buf[0]);
    console_write_ch(chardata++, 0x00200100 | buf[1]);

    dpy_text_update(s->con, 0, 0, 2, 1);
}

static int jazz_led_post_load(void *opaque, int version_id)
{
    /* force refresh */
    jazz_led_invalidate_display(opaque);

    return 0;
}

static const VMStateDescription vmstate_jazz_led = {
    .name = "jazz-led",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = jazz_led_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(segments, LedState),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps jazz_led_ops = {
    .invalidate  = jazz_led_invalidate_display,
    .gfx_update  = jazz_led_update_display,
    .text_update = jazz_led_text_update,
};

static int jazz_led_init(SysBusDevice *dev)
{
    LedState *s = JAZZ_LED(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &led_ops, s, "led", 1);
    sysbus_init_mmio(dev, &s->iomem);

    s->con = graphic_console_init(DEVICE(dev), 0, &jazz_led_ops, s);

    return 0;
}

static void jazz_led_reset(DeviceState *d)
{
    LedState *s = JAZZ_LED(d);

    s->segments = 0;
    s->state = REDRAW_SEGMENTS | REDRAW_BACKGROUND;
    qemu_console_resize(s->con, 60, 80);
}

static void jazz_led_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = jazz_led_init;
    dc->desc = "Jazz LED display",
    dc->vmsd = &vmstate_jazz_led;
    dc->reset = jazz_led_reset;
}

static const TypeInfo jazz_led_info = {
    .name          = TYPE_JAZZ_LED,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LedState),
    .class_init    = jazz_led_class_init,
};

static void jazz_led_register(void)
{
    type_register_static(&jazz_led_info);
}

type_init(jazz_led_register);
