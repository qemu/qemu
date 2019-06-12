/*
 * SSD0323 OLED controller with OSRAM Pictiva 128x64 display.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/* The controller can support a variety of different displays, but we only
   implement one.  Most of the commends relating to brightness and geometry
   setup are ignored. */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "qemu/module.h"
#include "ui/console.h"

//#define DEBUG_SSD0323 1

#ifdef DEBUG_SSD0323
#define DPRINTF(fmt, ...) \
do { printf("ssd0323: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { \
    fprintf(stderr, "ssd0323: error: " fmt , ## __VA_ARGS__); abort(); \
} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "ssd0323: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

/* Scaling factor for pixels.  */
#define MAGNIFY 4

#define REMAP_SWAP_COLUMN 0x01
#define REMAP_SWAP_NYBBLE 0x02
#define REMAP_VERTICAL    0x04
#define REMAP_SWAP_COM    0x10
#define REMAP_SPLIT_COM   0x40

enum ssd0323_mode
{
    SSD0323_CMD,
    SSD0323_DATA
};

typedef struct {
    SSISlave ssidev;
    QemuConsole *con;

    uint32_t cmd_len;
    int32_t cmd;
    int32_t cmd_data[8];
    int32_t row;
    int32_t row_start;
    int32_t row_end;
    int32_t col;
    int32_t col_start;
    int32_t col_end;
    int32_t redraw;
    int32_t remap;
    uint32_t mode;
    uint8_t framebuffer[128 * 80 / 2];
} ssd0323_state;

static uint32_t ssd0323_transfer(SSISlave *dev, uint32_t data)
{
    ssd0323_state *s = FROM_SSI_SLAVE(ssd0323_state, dev);

    switch (s->mode) {
    case SSD0323_DATA:
        DPRINTF("data 0x%02x\n", data);
        s->framebuffer[s->col + s->row * 64] = data;
        if (s->remap & REMAP_VERTICAL) {
            s->row++;
            if (s->row > s->row_end) {
                s->row = s->row_start;
                s->col++;
            }
            if (s->col > s->col_end) {
                s->col = s->col_start;
            }
        } else {
            s->col++;
            if (s->col > s->col_end) {
                s->row++;
                s->col = s->col_start;
            }
            if (s->row > s->row_end) {
                s->row = s->row_start;
            }
        }
        s->redraw = 1;
        break;
    case SSD0323_CMD:
        DPRINTF("cmd 0x%02x\n", data);
        if (s->cmd_len == 0) {
            s->cmd = data;
        } else {
            s->cmd_data[s->cmd_len - 1] = data;
        }
        s->cmd_len++;
        switch (s->cmd) {
#define DATA(x) if (s->cmd_len <= (x)) return 0
        case 0x15: /* Set column.  */
            DATA(2);
            s->col = s->col_start = s->cmd_data[0] % 64;
            s->col_end = s->cmd_data[1] % 64;
            break;
        case 0x75: /* Set row.  */
            DATA(2);
            s->row = s->row_start = s->cmd_data[0] % 80;
            s->row_end = s->cmd_data[1] % 80;
            break;
        case 0x81: /* Set contrast */
            DATA(1);
            break;
        case 0x84: case 0x85: case 0x86: /* Max current.  */
            DATA(0);
            break;
        case 0xa0: /* Set remapping.  */
            /* FIXME: Implement this.  */
            DATA(1);
            s->remap = s->cmd_data[0];
            break;
        case 0xa1: /* Set display start line.  */
        case 0xa2: /* Set display offset.  */
            /* FIXME: Implement these.  */
            DATA(1);
            break;
        case 0xa4: /* Normal mode.  */
        case 0xa5: /* All on.  */
        case 0xa6: /* All off.  */
        case 0xa7: /* Inverse.  */
            /* FIXME: Implement these.  */
            DATA(0);
            break;
        case 0xa8: /* Set multiplex ratio.  */
        case 0xad: /* Set DC-DC converter.  */
            DATA(1);
            /* Ignored.  Don't care.  */
            break;
        case 0xae: /* Display off.  */
        case 0xaf: /* Display on.  */
            DATA(0);
            /* TODO: Implement power control.  */
            break;
        case 0xb1: /* Set phase length.  */
        case 0xb2: /* Set row period.  */
        case 0xb3: /* Set clock rate.  */
        case 0xbc: /* Set precharge.  */
        case 0xbe: /* Set VCOMH.  */
        case 0xbf: /* Set segment low.  */
            DATA(1);
            /* Ignored.  Don't care.  */
            break;
        case 0xb8: /* Set grey scale table.  */
            /* FIXME: Implement this.  */
            DATA(8);
            break;
        case 0xe3: /* NOP.  */
            DATA(0);
            break;
        case 0xff: /* Nasty hack because we don't handle chip selects
                      properly.  */
            break;
        default:
            BADF("Unknown command: 0x%x\n", data);
        }
        s->cmd_len = 0;
        return 0;
    }
    return 0;
}

static void ssd0323_update_display(void *opaque)
{
    ssd0323_state *s = (ssd0323_state *)opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint8_t *dest;
    uint8_t *src;
    int x;
    int y;
    int i;
    int line;
    char *colors[16];
    char colortab[MAGNIFY * 64];
    char *p;
    int dest_width;

    if (!s->redraw)
        return;

    switch (surface_bits_per_pixel(surface)) {
    case 0:
        return;
    case 15:
        dest_width = 2;
        break;
    case 16:
        dest_width = 2;
        break;
    case 24:
        dest_width = 3;
        break;
    case 32:
        dest_width = 4;
        break;
    default:
        BADF("Bad color depth\n");
        return;
    }
    p = colortab;
    for (i = 0; i < 16; i++) {
        int n;
        colors[i] = p;
        switch (surface_bits_per_pixel(surface)) {
        case 15:
            n = i * 2 + (i >> 3);
            p[0] = n | (n << 5);
            p[1] = (n << 2) | (n >> 3);
            break;
        case 16:
            n = i * 2 + (i >> 3);
            p[0] = n | (n << 6) | ((n << 1) & 0x20);
            p[1] = (n << 3) | (n >> 2);
            break;
        case 24:
        case 32:
            n = (i << 4) | i;
            p[0] = p[1] = p[2] = n;
            break;
        default:
            BADF("Bad color depth\n");
            return;
        }
        p += dest_width;
    }
    /* TODO: Implement row/column remapping.  */
    dest = surface_data(surface);
    for (y = 0; y < 64; y++) {
        line = y;
        src = s->framebuffer + 64 * line;
        for (x = 0; x < 64; x++) {
            int val;
            val = *src >> 4;
            for (i = 0; i < MAGNIFY; i++) {
                memcpy(dest, colors[val], dest_width);
                dest += dest_width;
            }
            val = *src & 0xf;
            for (i = 0; i < MAGNIFY; i++) {
                memcpy(dest, colors[val], dest_width);
                dest += dest_width;
            }
            src++;
        }
        for (i = 1; i < MAGNIFY; i++) {
            memcpy(dest, dest - dest_width * MAGNIFY * 128,
                   dest_width * 128 * MAGNIFY);
            dest += dest_width * 128 * MAGNIFY;
        }
    }
    s->redraw = 0;
    dpy_gfx_update(s->con, 0, 0, 128 * MAGNIFY, 64 * MAGNIFY);
}

static void ssd0323_invalidate_display(void * opaque)
{
    ssd0323_state *s = (ssd0323_state *)opaque;
    s->redraw = 1;
}

/* Command/data input.  */
static void ssd0323_cd(void *opaque, int n, int level)
{
    ssd0323_state *s = (ssd0323_state *)opaque;
    DPRINTF("%s mode\n", level ? "Data" : "Command");
    s->mode = level ? SSD0323_DATA : SSD0323_CMD;
}

static int ssd0323_post_load(void *opaque, int version_id)
{
    ssd0323_state *s = (ssd0323_state *)opaque;

    if (s->cmd_len > ARRAY_SIZE(s->cmd_data)) {
        return -EINVAL;
    }
    if (s->row < 0 || s->row >= 80) {
        return -EINVAL;
    }
    if (s->row_start < 0 || s->row_start >= 80) {
        return -EINVAL;
    }
    if (s->row_end < 0 || s->row_end >= 80) {
        return -EINVAL;
    }
    if (s->col < 0 || s->col >= 64) {
        return -EINVAL;
    }
    if (s->col_start < 0 || s->col_start >= 64) {
        return -EINVAL;
    }
    if (s->col_end < 0 || s->col_end >= 64) {
        return -EINVAL;
    }
    if (s->mode != SSD0323_CMD && s->mode != SSD0323_DATA) {
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_ssd0323 = {
    .name = "ssd0323_oled",
    .version_id = 2,
    .minimum_version_id = 2,
    .post_load = ssd0323_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT32(cmd_len, ssd0323_state),
        VMSTATE_INT32(cmd, ssd0323_state),
        VMSTATE_INT32_ARRAY(cmd_data, ssd0323_state, 8),
        VMSTATE_INT32(row, ssd0323_state),
        VMSTATE_INT32(row_start, ssd0323_state),
        VMSTATE_INT32(row_end, ssd0323_state),
        VMSTATE_INT32(col, ssd0323_state),
        VMSTATE_INT32(col_start, ssd0323_state),
        VMSTATE_INT32(col_end, ssd0323_state),
        VMSTATE_INT32(redraw, ssd0323_state),
        VMSTATE_INT32(remap, ssd0323_state),
        VMSTATE_UINT32(mode, ssd0323_state),
        VMSTATE_BUFFER(framebuffer, ssd0323_state),
        VMSTATE_SSI_SLAVE(ssidev, ssd0323_state),
        VMSTATE_END_OF_LIST()
    }
};

static const GraphicHwOps ssd0323_ops = {
    .invalidate  = ssd0323_invalidate_display,
    .gfx_update  = ssd0323_update_display,
};

static void ssd0323_realize(SSISlave *d, Error **errp)
{
    DeviceState *dev = DEVICE(d);
    ssd0323_state *s = FROM_SSI_SLAVE(ssd0323_state, d);

    s->col_end = 63;
    s->row_end = 79;
    s->con = graphic_console_init(dev, 0, &ssd0323_ops, s);
    qemu_console_resize(s->con, 128 * MAGNIFY, 64 * MAGNIFY);

    qdev_init_gpio_in(dev, ssd0323_cd, 1);
}

static void ssd0323_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->realize = ssd0323_realize;
    k->transfer = ssd0323_transfer;
    k->cs_polarity = SSI_CS_HIGH;
    dc->vmsd = &vmstate_ssd0323;
}

static const TypeInfo ssd0323_info = {
    .name          = "ssd0323",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(ssd0323_state),
    .class_init    = ssd0323_class_init,
};

static void ssd03232_register_types(void)
{
    type_register_static(&ssd0323_info);
}

type_init(ssd03232_register_types)
