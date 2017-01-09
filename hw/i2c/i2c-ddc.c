/* A simple I2C slave for returning monitor EDID data via DDC.
 *
 * Copyright (c) 2011 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/i2c-ddc.h"

#ifndef DEBUG_I2CDDC
#define DEBUG_I2CDDC 0
#endif

#define DPRINTF(fmt, ...) do {                                                 \
    if (DEBUG_I2CDDC) {                                                        \
        qemu_log("i2c-ddc: " fmt , ## __VA_ARGS__);                            \
    }                                                                          \
} while (0);

/* Structure defining a monitor's characteristics in a
 * readable format: this should be passed to build_edid_blob()
 * to convert it into the 128 byte binary EDID blob.
 * Not all bits of the EDID are customisable here.
 */
struct EDIDData {
    char manuf_id[3]; /* three upper case letters */
    uint16_t product_id;
    uint32_t serial_no;
    uint8_t manuf_week;
    int manuf_year;
    uint8_t h_cm;
    uint8_t v_cm;
    uint8_t gamma;
    char monitor_name[14];
    char serial_no_string[14];
    /* Range limits */
    uint8_t vmin; /* Hz */
    uint8_t vmax; /* Hz */
    uint8_t hmin; /* kHz */
    uint8_t hmax; /* kHz */
    uint8_t pixclock; /* MHz / 10 */
    uint8_t timing_data[18];
};

typedef struct EDIDData EDIDData;

/* EDID data for a simple LCD monitor */
static const EDIDData lcd_edid = {
    /* The manuf_id ought really to be an assigned EISA ID */
    .manuf_id = "QMU",
    .product_id = 0,
    .serial_no = 1,
    .manuf_week = 1,
    .manuf_year = 2011,
    .h_cm = 40,
    .v_cm = 30,
    .gamma = 0x78,
    .monitor_name = "QEMU monitor",
    .serial_no_string = "1",
    .vmin = 40,
    .vmax = 120,
    .hmin = 30,
    .hmax = 100,
    .pixclock = 18,
    .timing_data = {
        /* Borrowed from a 21" LCD */
        0x48, 0x3f, 0x40, 0x30, 0x62, 0xb0, 0x32, 0x40, 0x40,
        0xc0, 0x13, 0x00, 0x98, 0x32, 0x11, 0x00, 0x00, 0x1e
    }
};

static uint8_t manuf_char_to_int(char c)
{
    return (c - 'A') & 0x1f;
}

static void write_ascii_descriptor_block(uint8_t *descblob, uint8_t blocktype,
                                         const char *string)
{
    /* Write an EDID Descriptor Block of the "ascii string" type */
    int i;
    descblob[0] = descblob[1] = descblob[2] = descblob[4] = 0;
    descblob[3] = blocktype;
    /* The rest is 13 bytes of ASCII; if less then the rest must
     * be filled with newline then spaces
     */
    for (i = 5; i < 19; i++) {
        descblob[i] = string[i - 5];
        if (!descblob[i]) {
            break;
        }
    }
    if (i < 19) {
        descblob[i++] = '\n';
    }
    for ( ; i < 19; i++) {
        descblob[i] = ' ';
    }
}

static void write_range_limits_descriptor(const EDIDData *edid,
                                          uint8_t *descblob)
{
    int i;
    descblob[0] = descblob[1] = descblob[2] = descblob[4] = 0;
    descblob[3] = 0xfd;
    descblob[5] = edid->vmin;
    descblob[6] = edid->vmax;
    descblob[7] = edid->hmin;
    descblob[8] = edid->hmax;
    descblob[9] = edid->pixclock;
    descblob[10] = 0;
    descblob[11] = 0xa;
    for (i = 12; i < 19; i++) {
        descblob[i] = 0x20;
    }
}

static void build_edid_blob(const EDIDData *edid, uint8_t *blob)
{
    /* Write an EDID 1.3 format blob (128 bytes) based
     * on the EDIDData structure.
     */
    int i;
    uint8_t cksum;

    /* 00-07 : header */
    blob[0] = blob[7] = 0;
    for (i = 1 ; i < 7; i++) {
        blob[i] = 0xff;
    }
    /* 08-09 : manufacturer ID */
    blob[8] = (manuf_char_to_int(edid->manuf_id[0]) << 2)
        | (manuf_char_to_int(edid->manuf_id[1]) >> 3);
    blob[9] = (manuf_char_to_int(edid->manuf_id[1]) << 5)
        | manuf_char_to_int(edid->manuf_id[2]);
    /* 10-11 : product ID code */
    blob[10] = edid->product_id;
    blob[11] = edid->product_id >> 8;
    blob[12] = edid->serial_no;
    blob[13] = edid->serial_no >> 8;
    blob[14] = edid->serial_no >> 16;
    blob[15] = edid->serial_no >> 24;
    /* 16 : week of manufacture */
    blob[16] = edid->manuf_week;
    /* 17 : year of manufacture - 1990 */
    blob[17] = edid->manuf_year - 1990;
    /* 18, 19 : EDID version and revision */
    blob[18] = 1;
    blob[19] = 3;
    /* 20 - 24 : basic display parameters */
    /* We are always a digital display */
    blob[20] = 0x80;
    /* 21, 22 : max h/v size in cm */
    blob[21] = edid->h_cm;
    blob[22] = edid->v_cm;
    /* 23 : gamma (divide by 100 then add 1 for actual value) */
    blob[23] = edid->gamma;
    /* 24 feature support: no power management, RGB, preferred timing mode,
     * standard colour space
     */
    blob[24] = 0x0e;
    /* 25 - 34 : chromaticity coordinates. These are the
     * standard sRGB chromaticity values
     */
    blob[25] = 0xee;
    blob[26] = 0x91;
    blob[27] = 0xa3;
    blob[28] = 0x54;
    blob[29] = 0x4c;
    blob[30] = 0x99;
    blob[31] = 0x26;
    blob[32] = 0x0f;
    blob[33] = 0x50;
    blob[34] = 0x54;
    /* 35, 36 : Established timings: claim to support everything */
    blob[35] = blob[36] = 0xff;
    /* 37 : manufacturer's reserved timing: none */
    blob[37] = 0;
    /* 38 - 53 : standard timing identification
     * don't claim anything beyond what the 'established timings'
     * already provide. Unused slots must be (0x1, 0x1)
     */
    for (i = 38; i < 54; i++) {
        blob[i] = 0x1;
    }
    /* 54 - 71 : descriptor block 1 : must be preferred timing data */
    memcpy(blob + 54, edid->timing_data, 18);
    /* 72 - 89, 90 - 107, 108 - 125 : descriptor block 2, 3, 4
     * Order not important, but we must have a monitor name and a
     * range limits descriptor.
     */
    write_range_limits_descriptor(edid, blob + 72);
    write_ascii_descriptor_block(blob + 90, 0xfc, edid->monitor_name);
    write_ascii_descriptor_block(blob + 108, 0xff, edid->serial_no_string);

    /* 126 : extension flag */
    blob[126] = 0;

    cksum = 0;
    for (i = 0; i < 127; i++) {
        cksum += blob[i];
    }
    /* 127 : checksum */
    blob[127] = -cksum;
    if (DEBUG_I2CDDC) {
        qemu_hexdump((char *)blob, stdout, "", 128);
    }
}

static void i2c_ddc_reset(DeviceState *ds)
{
    I2CDDCState *s = I2CDDC(ds);

    s->firstbyte = false;
    s->reg = 0;
}

static int i2c_ddc_event(I2CSlave *i2c, enum i2c_event event)
{
    I2CDDCState *s = I2CDDC(i2c);

    if (event == I2C_START_SEND) {
        s->firstbyte = true;
    }

    return 0;
}

static int i2c_ddc_rx(I2CSlave *i2c)
{
    I2CDDCState *s = I2CDDC(i2c);

    int value;
    value = s->edid_blob[s->reg];
    s->reg++;
    return value;
}

static int i2c_ddc_tx(I2CSlave *i2c, uint8_t data)
{
    I2CDDCState *s = I2CDDC(i2c);
    if (s->firstbyte) {
        s->reg = data;
        s->firstbyte = false;
        DPRINTF("[EDID] Written new pointer: %u\n", data);
        return 1;
    }

    /* Ignore all writes */
    s->reg++;
    return 1;
}

static void i2c_ddc_init(Object *obj)
{
    I2CDDCState *s = I2CDDC(obj);
    build_edid_blob(&lcd_edid, s->edid_blob);
}

static const VMStateDescription vmstate_i2c_ddc = {
    .name = TYPE_I2CDDC,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(firstbyte, I2CDDCState),
        VMSTATE_UINT8(reg, I2CDDCState),
        VMSTATE_END_OF_LIST()
    }
};

static void i2c_ddc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);

    dc->reset = i2c_ddc_reset;
    dc->vmsd = &vmstate_i2c_ddc;
    isc->event = i2c_ddc_event;
    isc->recv = i2c_ddc_rx;
    isc->send = i2c_ddc_tx;
}

static TypeInfo i2c_ddc_info = {
    .name = TYPE_I2CDDC,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(I2CDDCState),
    .instance_init = i2c_ddc_init,
    .class_init = i2c_ddc_class_init
};

static void ddc_register_devices(void)
{
    type_register_static(&i2c_ddc_info);
}

type_init(ddc_register_devices);
