/*
 * SSD0303 OLED controller with OSRAM Pictiva 96x16 display.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

/* The controller can support a variety of different displays, but we only
   implement one.  Most of the commends relating to brightness and geometry
   setup are ignored. */
#include "i2c.h"
#include "console.h"

//#define DEBUG_SSD0303 1

#ifdef DEBUG_SSD0303
#define DPRINTF(fmt, ...) \
do { printf("ssd0303: " fmt , ## __VA_ARGS__); } while (0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "ssd0303: error: " fmt , ## __VA_ARGS__); exit(1);} while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#define BADF(fmt, ...) \
do { fprintf(stderr, "ssd0303: error: " fmt , ## __VA_ARGS__);} while (0)
#endif

/* Scaling factor for pixels.  */
#define MAGNIFY 4

enum ssd0303_mode
{
    SSD0303_IDLE,
    SSD0303_DATA,
    SSD0303_CMD
};

enum ssd0303_cmd {
    SSD0303_CMD_NONE,
    SSD0303_CMD_SKIP1
};

typedef struct {
    I2CSlave i2c;
    DisplayState *ds;
    int row;
    int col;
    int start_line;
    int mirror;
    int flash;
    int enabled;
    int inverse;
    int redraw;
    enum ssd0303_mode mode;
    enum ssd0303_cmd cmd_state;
    uint8_t framebuffer[132*8];
} ssd0303_state;

static int ssd0303_recv(I2CSlave *i2c)
{
    BADF("Reads not implemented\n");
    return -1;
}

static int ssd0303_send(I2CSlave *i2c, uint8_t data)
{
    ssd0303_state *s = (ssd0303_state *)i2c;
    enum ssd0303_cmd old_cmd_state;
    switch (s->mode) {
    case SSD0303_IDLE:
        DPRINTF("byte 0x%02x\n", data);
        if (data == 0x80)
            s->mode = SSD0303_CMD;
        else if (data == 0x40)
            s->mode = SSD0303_DATA;
        else
            BADF("Unexpected byte 0x%x\n", data);
        break;
    case SSD0303_DATA:
        DPRINTF("data 0x%02x\n", data);
        if (s->col < 132) {
            s->framebuffer[s->col + s->row * 132] = data;
            s->col++;
            s->redraw = 1;
        }
        break;
    case SSD0303_CMD:
        old_cmd_state = s->cmd_state;
        s->cmd_state = SSD0303_CMD_NONE;
        switch (old_cmd_state) {
        case SSD0303_CMD_NONE:
            DPRINTF("cmd 0x%02x\n", data);
            s->mode = SSD0303_IDLE;
            switch (data) {
            case 0x00 ... 0x0f: /* Set lower column address.  */
                s->col = (s->col & 0xf0) | (data & 0xf);
                break;
            case 0x10 ... 0x20: /* Set higher column address.  */
                s->col = (s->col & 0x0f) | ((data & 0xf) << 4);
                break;
            case 0x40 ... 0x7f: /* Set start line.  */
                s->start_line = 0;
                break;
            case 0x81: /* Set contrast (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xa0: /* Mirror off.  */
                s->mirror = 0;
                break;
            case 0xa1: /* Mirror off.  */
                s->mirror = 1;
                break;
            case 0xa4: /* Entire display off.  */
                s->flash = 0;
                break;
            case 0xa5: /* Entire display on.  */
                s->flash = 1;
                break;
            case 0xa6: /* Inverse off.  */
                s->inverse = 0;
                break;
            case 0xa7: /* Inverse on.  */
                s->inverse = 1;
                break;
            case 0xa8: /* Set multiplied ratio (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xad: /* DC-DC power control.  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xae: /* Display off.  */
                s->enabled = 0;
                break;
            case 0xaf: /* Display on.  */
                s->enabled = 1;
                break;
            case 0xb0 ... 0xbf: /* Set Page address.  */
                s->row = data & 7;
                break;
            case 0xc0 ... 0xc8: /* Set COM output direction (Ignored).  */
                break;
            case 0xd3: /* Set display offset (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xd5: /* Set display clock (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xd8: /* Set color and power mode (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xd9: /* Set pre-charge period (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xda: /* Set COM pin configuration (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xdb: /* Set VCOM dselect level (Ignored).  */
                s->cmd_state = SSD0303_CMD_SKIP1;
                break;
            case 0xe3: /* no-op.  */
                break;
            default:
                BADF("Unknown command: 0x%x\n", data);
            }
            break;
        case SSD0303_CMD_SKIP1:
            DPRINTF("skip 0x%02x\n", data);
            break;
        }
        break;
    }
    return 0;
}

static void ssd0303_event(I2CSlave *i2c, enum i2c_event event)
{
    ssd0303_state *s = (ssd0303_state *)i2c;
    switch (event) {
    case I2C_FINISH:
        s->mode = SSD0303_IDLE;
        break;
    case I2C_START_RECV:
    case I2C_START_SEND:
    case I2C_NACK:
        /* Nothing to do.  */
        break;
    }
}

static void ssd0303_update_display(void *opaque)
{
    ssd0303_state *s = (ssd0303_state *)opaque;
    uint8_t *dest;
    uint8_t *src;
    int x;
    int y;
    int line;
    char *colors[2];
    char colortab[MAGNIFY * 8];
    int dest_width;
    uint8_t mask;

    if (!s->redraw)
        return;

    switch (ds_get_bits_per_pixel(s->ds)) {
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
    dest_width *= MAGNIFY;
    memset(colortab, 0xff, dest_width);
    memset(colortab + dest_width, 0, dest_width);
    if (s->flash) {
        colors[0] = colortab;
        colors[1] = colortab;
    } else if (s->inverse) {
        colors[0] = colortab;
        colors[1] = colortab + dest_width;
    } else {
        colors[0] = colortab + dest_width;
        colors[1] = colortab;
    }
    dest = ds_get_data(s->ds);
    for (y = 0; y < 16; y++) {
        line = (y + s->start_line) & 63;
        src = s->framebuffer + 132 * (line >> 3) + 36;
        mask = 1 << (line & 7);
        for (x = 0; x < 96; x++) {
            memcpy(dest, colors[(*src & mask) != 0], dest_width);
            dest += dest_width;
            src++;
        }
        for (x = 1; x < MAGNIFY; x++) {
            memcpy(dest, dest - dest_width * 96, dest_width * 96);
            dest += dest_width * 96;
        }
    }
    s->redraw = 0;
    dpy_update(s->ds, 0, 0, 96 * MAGNIFY, 16 * MAGNIFY);
}

static void ssd0303_invalidate_display(void * opaque)
{
    ssd0303_state *s = (ssd0303_state *)opaque;
    s->redraw = 1;
}

static const VMStateDescription vmstate_ssd0303 = {
    .name = "ssd0303_oled",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_INT32(row, ssd0303_state),
        VMSTATE_INT32(col, ssd0303_state),
        VMSTATE_INT32(start_line, ssd0303_state),
        VMSTATE_INT32(mirror, ssd0303_state),
        VMSTATE_INT32(flash, ssd0303_state),
        VMSTATE_INT32(enabled, ssd0303_state),
        VMSTATE_INT32(inverse, ssd0303_state),
        VMSTATE_INT32(redraw, ssd0303_state),
        VMSTATE_UINT32(mode, ssd0303_state),
        VMSTATE_UINT32(cmd_state, ssd0303_state),
        VMSTATE_BUFFER(framebuffer, ssd0303_state),
        VMSTATE_I2C_SLAVE(i2c, ssd0303_state),
        VMSTATE_END_OF_LIST()
    }
};

static int ssd0303_init(I2CSlave *i2c)
{
    ssd0303_state *s = FROM_I2C_SLAVE(ssd0303_state, i2c);

    s->ds = graphic_console_init(ssd0303_update_display,
                                 ssd0303_invalidate_display,
                                 NULL, NULL, s);
    qemu_console_resize(s->ds, 96 * MAGNIFY, 16 * MAGNIFY);
    return 0;
}

static void ssd0303_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = ssd0303_init;
    k->event = ssd0303_event;
    k->recv = ssd0303_recv;
    k->send = ssd0303_send;
    dc->vmsd = &vmstate_ssd0303;
}

static TypeInfo ssd0303_info = {
    .name          = "ssd0303",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(ssd0303_state),
    .class_init    = ssd0303_class_init,
};

static void ssd0303_register_types(void)
{
    type_register_static(&ssd0303_info);
}

type_init(ssd0303_register_types)
