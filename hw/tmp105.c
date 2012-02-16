/*
 * Texas Instruments TMP105 temperature sensor.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw.h"
#include "i2c.h"

typedef struct {
    I2CSlave i2c;
    uint8_t len;
    uint8_t buf[2];
    qemu_irq pin;

    uint8_t pointer;
    uint8_t config;
    int16_t temperature;
    int16_t limit[2];
    int faults;
    uint8_t alarm;
} TMP105State;

static void tmp105_interrupt_update(TMP105State *s)
{
    qemu_set_irq(s->pin, s->alarm ^ ((~s->config >> 2) & 1));	/* POL */
}

static void tmp105_alarm_update(TMP105State *s)
{
    if ((s->config >> 0) & 1) {					/* SD */
        if ((s->config >> 7) & 1)				/* OS */
            s->config &= ~(1 << 7);				/* OS */
        else
            return;
    }

    if ((s->config >> 1) & 1) {					/* TM */
        if (s->temperature >= s->limit[1])
            s->alarm = 1;
        else if (s->temperature < s->limit[0])
            s->alarm = 1;
    } else {
        if (s->temperature >= s->limit[1])
            s->alarm = 1;
        else if (s->temperature < s->limit[0])
            s->alarm = 0;
    }

    tmp105_interrupt_update(s);
}

/* Units are 0.001 centigrades relative to 0 C.  */
void tmp105_set(I2CSlave *i2c, int temp)
{
    TMP105State *s = (TMP105State *) i2c;

    if (temp >= 128000 || temp < -128000) {
        fprintf(stderr, "%s: values is out of range (%i.%03i C)\n",
                        __FUNCTION__, temp / 1000, temp % 1000);
        exit(-1);
    }

    s->temperature = ((int16_t) (temp * 0x800 / 128000)) << 4;

    tmp105_alarm_update(s);
}

static const int tmp105_faultq[4] = { 1, 2, 4, 6 };

static void tmp105_read(TMP105State *s)
{
    s->len = 0;

    if ((s->config >> 1) & 1) {					/* TM */
        s->alarm = 0;
        tmp105_interrupt_update(s);
    }

    switch (s->pointer & 3) {
    case 0:	/* Temperature */
        s->buf[s->len ++] = (((uint16_t) s->temperature) >> 8);
        s->buf[s->len ++] = (((uint16_t) s->temperature) >> 0) &
                (0xf0 << ((~s->config >> 5) & 3));		/* R */
        break;

    case 1:	/* Configuration */
        s->buf[s->len ++] = s->config;
        break;

    case 2:	/* T_LOW */
        s->buf[s->len ++] = ((uint16_t) s->limit[0]) >> 8;
        s->buf[s->len ++] = ((uint16_t) s->limit[0]) >> 0;
        break;

    case 3:	/* T_HIGH */
        s->buf[s->len ++] = ((uint16_t) s->limit[1]) >> 8;
        s->buf[s->len ++] = ((uint16_t) s->limit[1]) >> 0;
        break;
    }
}

static void tmp105_write(TMP105State *s)
{
    switch (s->pointer & 3) {
    case 0:	/* Temperature */
        break;

    case 1:	/* Configuration */
        if (s->buf[0] & ~s->config & (1 << 0))			/* SD */
            printf("%s: TMP105 shutdown\n", __FUNCTION__);
        s->config = s->buf[0];
        s->faults = tmp105_faultq[(s->config >> 3) & 3];	/* F */
        tmp105_alarm_update(s);
        break;

    case 2:	/* T_LOW */
    case 3:	/* T_HIGH */
        if (s->len >= 3)
            s->limit[s->pointer & 1] = (int16_t)
                    ((((uint16_t) s->buf[0]) << 8) | s->buf[1]);
        tmp105_alarm_update(s);
        break;
    }
}

static int tmp105_rx(I2CSlave *i2c)
{
    TMP105State *s = (TMP105State *) i2c;

    if (s->len < 2)
        return s->buf[s->len ++];
    else
        return 0xff;
}

static int tmp105_tx(I2CSlave *i2c, uint8_t data)
{
    TMP105State *s = (TMP105State *) i2c;

    if (!s->len ++)
        s->pointer = data;
    else {
        if (s->len <= 2)
            s->buf[s->len - 1] = data;
        tmp105_write(s);
    }

    return 0;
}

static void tmp105_event(I2CSlave *i2c, enum i2c_event event)
{
    TMP105State *s = (TMP105State *) i2c;

    if (event == I2C_START_RECV)
        tmp105_read(s);

    s->len = 0;
}

static int tmp105_post_load(void *opaque, int version_id)
{
    TMP105State *s = opaque;

    s->faults = tmp105_faultq[(s->config >> 3) & 3];		/* F */

    tmp105_interrupt_update(s);
    return 0;
}

static const VMStateDescription vmstate_tmp105 = {
    .name = "TMP105",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .post_load = tmp105_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_UINT8(len, TMP105State),
        VMSTATE_UINT8_ARRAY(buf, TMP105State, 2),
        VMSTATE_UINT8(pointer, TMP105State),
        VMSTATE_UINT8(config, TMP105State),
        VMSTATE_INT16(temperature, TMP105State),
        VMSTATE_INT16_ARRAY(limit, TMP105State, 2),
        VMSTATE_UINT8(alarm, TMP105State),
        VMSTATE_I2C_SLAVE(i2c, TMP105State),
        VMSTATE_END_OF_LIST()
    }
};

static void tmp105_reset(I2CSlave *i2c)
{
    TMP105State *s = (TMP105State *) i2c;

    s->temperature = 0;
    s->pointer = 0;
    s->config = 0;
    s->faults = tmp105_faultq[(s->config >> 3) & 3];
    s->alarm = 0;

    tmp105_interrupt_update(s);
}

static int tmp105_init(I2CSlave *i2c)
{
    TMP105State *s = FROM_I2C_SLAVE(TMP105State, i2c);

    qdev_init_gpio_out(&i2c->qdev, &s->pin, 1);

    tmp105_reset(&s->i2c);

    return 0;
}

static void tmp105_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->init = tmp105_init;
    k->event = tmp105_event;
    k->recv = tmp105_rx;
    k->send = tmp105_tx;
    dc->vmsd = &vmstate_tmp105;
}

static TypeInfo tmp105_info = {
    .name          = "tmp105",
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(TMP105State),
    .class_init    = tmp105_class_init,
};

static void tmp105_register_types(void)
{
    type_register_static(&tmp105_info);
}

type_init(tmp105_register_types)
