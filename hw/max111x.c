/*
 * Maxim MAX1110/1111 ADC chip emulation.
 *
 * Copyright (c) 2006 Openedhand Ltd.
 * Written by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPLv2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "hw/ssi.h"

typedef struct {
    SSISlave ssidev;
    qemu_irq interrupt;
    uint8_t tb1, rb2, rb3;
    int cycle;

    uint8_t input[8];
    int inputs, com;
} MAX111xState;

/* Control-byte bitfields */
#define CB_PD0		(1 << 0)
#define CB_PD1		(1 << 1)
#define CB_SGL		(1 << 2)
#define CB_UNI		(1 << 3)
#define CB_SEL0		(1 << 4)
#define CB_SEL1		(1 << 5)
#define CB_SEL2		(1 << 6)
#define CB_START	(1 << 7)

#define CHANNEL_NUM(v, b0, b1, b2)	\
			((((v) >> (2 + (b0))) & 4) |	\
			 (((v) >> (3 + (b1))) & 2) |	\
			 (((v) >> (4 + (b2))) & 1))

static uint32_t max111x_read(MAX111xState *s)
{
    if (!s->tb1)
        return 0;

    switch (s->cycle ++) {
    case 1:
        return s->rb2;
    case 2:
        return s->rb3;
    }

    return 0;
}

/* Interpret a control-byte */
static void max111x_write(MAX111xState *s, uint32_t value)
{
    int measure, chan;

    /* Ignore the value if START bit is zero */
    if (!(value & CB_START))
        return;

    s->cycle = 0;

    if (!(value & CB_PD1)) {
        s->tb1 = 0;
        return;
    }

    s->tb1 = value;

    if (s->inputs == 8)
        chan = CHANNEL_NUM(value, 1, 0, 2);
    else
        chan = CHANNEL_NUM(value & ~CB_SEL0, 0, 1, 2);

    if (value & CB_SGL)
        measure = s->input[chan] - s->com;
    else
        measure = s->input[chan] - s->input[chan ^ 1];

    if (!(value & CB_UNI))
        measure ^= 0x80;

    s->rb2 = (measure >> 2) & 0x3f;
    s->rb3 = (measure << 6) & 0xc0;

    /* FIXME: When should the IRQ be lowered?  */
    qemu_irq_raise(s->interrupt);
}

static uint32_t max111x_transfer(SSISlave *dev, uint32_t value)
{
    MAX111xState *s = FROM_SSI_SLAVE(MAX111xState, dev);
    max111x_write(s, value);
    return max111x_read(s);
}

static const VMStateDescription vmstate_max111x = {
    .name = "max111x",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_SSI_SLAVE(ssidev, MAX111xState),
        VMSTATE_UINT8(tb1, MAX111xState),
        VMSTATE_UINT8(rb2, MAX111xState),
        VMSTATE_UINT8(rb3, MAX111xState),
        VMSTATE_INT32_EQUAL(inputs, MAX111xState),
        VMSTATE_INT32(com, MAX111xState),
        VMSTATE_ARRAY_INT32_UNSAFE(input, MAX111xState, inputs,
                                   vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static int max111x_init(SSISlave *dev, int inputs)
{
    MAX111xState *s = FROM_SSI_SLAVE(MAX111xState, dev);

    qdev_init_gpio_out(&dev->qdev, &s->interrupt, 1);

    s->inputs = inputs;
    /* TODO: add a user interface for setting these */
    s->input[0] = 0xf0;
    s->input[1] = 0xe0;
    s->input[2] = 0xd0;
    s->input[3] = 0xc0;
    s->input[4] = 0xb0;
    s->input[5] = 0xa0;
    s->input[6] = 0x90;
    s->input[7] = 0x80;
    s->com = 0;

    vmstate_register(&dev->qdev, -1, &vmstate_max111x, s);
    return 0;
}

static int max1110_init(SSISlave *dev)
{
    return max111x_init(dev, 8);
}

static int max1111_init(SSISlave *dev)
{
    return max111x_init(dev, 4);
}

void max111x_set_input(DeviceState *dev, int line, uint8_t value)
{
    MAX111xState *s = FROM_SSI_SLAVE(MAX111xState, SSI_SLAVE_FROM_QDEV(dev));
    assert(line >= 0 && line < s->inputs);
    s->input[line] = value;
}

static void max1110_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = max1110_init;
    k->transfer = max111x_transfer;
}

static const TypeInfo max1110_info = {
    .name          = "max1110",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(MAX111xState),
    .class_init    = max1110_class_init,
};

static void max1111_class_init(ObjectClass *klass, void *data)
{
    SSISlaveClass *k = SSI_SLAVE_CLASS(klass);

    k->init = max1111_init;
    k->transfer = max111x_transfer;
}

static const TypeInfo max1111_info = {
    .name          = "max1111",
    .parent        = TYPE_SSI_SLAVE,
    .instance_size = sizeof(MAX111xState),
    .class_init    = max1111_class_init,
};

static void max111x_register_types(void)
{
    type_register_static(&max1110_info);
    type_register_static(&max1111_info);
}

type_init(max111x_register_types)
