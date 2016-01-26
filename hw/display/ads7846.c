/*
 * TI ADS7846 / TSC2046 chip emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "ui/console.h"

typedef struct {
    SSISlave ssidev;
    qemu_irq interrupt;

    int input[8];
    int pressure;
    int noise;

    int cycle;
    int output;
} ADS7846State;

/* Control-byte bitfields */
#define CB_PD0		(1 << 0)
#define CB_PD1		(1 << 1)
#define CB_SER		(1 << 2)
#define CB_MODE		(1 << 3)
#define CB_A0		(1 << 4)
#define CB_A1		(1 << 5)
#define CB_A2		(1 << 6)
#define CB_START	(1 << 7)

#define X_AXIS_DMAX	3470
#define X_AXIS_MIN	290
#define Y_AXIS_DMAX	3450
#define Y_AXIS_MIN	200

#define ADS_VBAT	2000
#define ADS_VAUX	2000
#define ADS_TEMP0	2000
#define ADS_TEMP1	3000
#define ADS_XPOS(x, y)	(X_AXIS_MIN + ((X_AXIS_DMAX * (x)) >> 15))
#define ADS_YPOS(x, y)	(Y_AXIS_MIN + ((Y_AXIS_DMAX * (y)) >> 15))
#define ADS_Z1POS(x, y)	600
#define ADS_Z2POS(x, y)	(600 + 6000 / ADS_XPOS(x, y))

static void ads7846_int_update(ADS7846State *s)
{
    if (s->interrupt)
        qemu_set_irq(s->interrupt, s->pressure == 0);
}

static uint32_t ads7846_transfer(SSISlave *dev, uint32_t value)
{
    ADS7846State *s = FROM_SSI_SLAVE(ADS7846State, dev);

    switch (s->cycle ++) {
    case 0:
        if (!(value & CB_START)) {
            s->cycle = 0;
            break;
        }

        s->output = s->input[(value >> 4) & 7];

        /* Imitate the ADC noise, some drivers expect this.  */
        s->noise = (s->noise + 3) & 7;
        switch ((value >> 4) & 7) {
        case 1: s->output += s->noise ^ 2; break;
        case 3: s->output += s->noise ^ 0; break;
        case 4: s->output += s->noise ^ 7; break;
        case 5: s->output += s->noise ^ 5; break;
        }

        if (value & CB_MODE)
            s->output >>= 4;	/* 8 bits instead of 12 */

        break;
    case 1:
        s->cycle = 0;
        break;
    }
    return s->output;
}

static void ads7846_ts_event(void *opaque,
                int x, int y, int z, int buttons_state)
{
    ADS7846State *s = opaque;

    if (buttons_state) {
        x = 0x7fff - x;
        s->input[1] = ADS_XPOS(x, y);
        s->input[3] = ADS_Z1POS(x, y);
        s->input[4] = ADS_Z2POS(x, y);
        s->input[5] = ADS_YPOS(x, y);
    }

    if (s->pressure == !buttons_state) {
        s->pressure = !!buttons_state;

        ads7846_int_update(s);
    }
}

static int ads7856_post_load(void *opaque, int version_id)
{
    ADS7846State *s = opaque;

    s->pressure = 0;
    ads7846_int_update(s);
    return 0;
}

static const VMStateDescription vmstate_ads7846 = {
    .name = "ads7846",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ads7856_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_SSI_SLAVE(ssidev, ADS7846State),
        VMSTATE_INT32_ARRAY(input, ADS7846State, 8),
        VMSTATE_INT32(noise, ADS7846State),
        VMSTATE_INT32(cycle, ADS7846State),
        VMSTATE_INT32(output, ADS7846State),
        VMSTATE_END_OF_LIST()
    }
};

static int ads7846_init(SSISlave *d)
{
    DeviceState *dev = DEVICE(d);
    ADS7846State *s = FROM_SSI_SLAVE(ADS7846State, d);

    qdev_init_gpio_out(dev, &s->interrupt, 1);

    s->input[0] = ADS_TEMP0;	/* TEMP0 */
    s->input[2] = ADS_VBAT;	/* VBAT */
    s->input[6] = ADS_VAUX;	/* VAUX */
    s->input[7] = ADS_TEMP1;	/* TEMP1 */

    /* We want absolute coordinates */
    qemu_add_mouse_event_handler(ads7846_ts_event, s, 1,
                    "QEMU ADS7846-driven Touchscreen");

    ads7846_int_update(s);

    vmstate_register(NULL, -1, &vmstate_ads7846, s);
    return 0;
}

static void ads7846_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = ads7846_init;
    k->transfer = ads7846_transfer;
}

static const TypeInfo ads7846_info = {
    .name          = "ads7846",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(ADS7846State),
    .class_init    = ads7846_class_init,
};

static void ads7846_register_types(void)
{
    type_register_static(&ads7846_info);
}

type_init(ads7846_register_types)
