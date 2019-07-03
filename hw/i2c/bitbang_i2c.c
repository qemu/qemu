/*
 * Bit-Bang i2c emulation extracted from
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/i2c/bitbang_i2c.h"
#include "hw/sysbus.h"
#include "qemu/module.h"

//#define DEBUG_BITBANG_I2C

#ifdef DEBUG_BITBANG_I2C
#define DPRINTF(fmt, ...) \
do { printf("bitbang_i2c: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

static void bitbang_i2c_enter_stop(bitbang_i2c_interface *i2c)
{
    DPRINTF("STOP\n");
    if (i2c->current_addr >= 0)
        i2c_end_transfer(i2c->bus);
    i2c->current_addr = -1;
    i2c->state = STOPPED;
}

/* Set device data pin.  */
static int bitbang_i2c_ret(bitbang_i2c_interface *i2c, int level)
{
    i2c->device_out = level;
    //DPRINTF("%d %d %d\n", i2c->last_clock, i2c->last_data, i2c->device_out);
    return level & i2c->last_data;
}

/* Leave device data pin unodified.  */
static int bitbang_i2c_nop(bitbang_i2c_interface *i2c)
{
    return bitbang_i2c_ret(i2c, i2c->device_out);
}

/* Returns data line level.  */
int bitbang_i2c_set(bitbang_i2c_interface *i2c, int line, int level)
{
    int data;

    if (level != 0 && level != 1) {
        abort();
    }

    if (line == BITBANG_I2C_SDA) {
        if (level == i2c->last_data) {
            return bitbang_i2c_nop(i2c);
        }
        i2c->last_data = level;
        if (i2c->last_clock == 0) {
            return bitbang_i2c_nop(i2c);
        }
        if (level == 0) {
            DPRINTF("START\n");
            /* START condition.  */
            i2c->state = SENDING_BIT7;
            i2c->current_addr = -1;
        } else {
            /* STOP condition.  */
            bitbang_i2c_enter_stop(i2c);
        }
        return bitbang_i2c_ret(i2c, 1);
    }

    data = i2c->last_data;
    if (i2c->last_clock == level) {
        return bitbang_i2c_nop(i2c);
    }
    i2c->last_clock = level;
    if (level == 0) {
        /* State is set/read at the start of the clock pulse.
           release the data line at the end.  */
        return bitbang_i2c_ret(i2c, 1);
    }
    switch (i2c->state) {
    case STOPPED:
    case SENT_NACK:
        return bitbang_i2c_ret(i2c, 1);

    case SENDING_BIT7 ... SENDING_BIT0:
        i2c->buffer = (i2c->buffer << 1) | data;
        /* will end up in WAITING_FOR_ACK */
        i2c->state++; 
        return bitbang_i2c_ret(i2c, 1);

    case WAITING_FOR_ACK:
    {
        int ret;

        if (i2c->current_addr < 0) {
            i2c->current_addr = i2c->buffer;
            DPRINTF("Address 0x%02x\n", i2c->current_addr);
            ret = i2c_start_transfer(i2c->bus, i2c->current_addr >> 1,
                                     i2c->current_addr & 1);
        } else {
            DPRINTF("Sent 0x%02x\n", i2c->buffer);
            ret = i2c_send(i2c->bus, i2c->buffer);
        }
        if (ret) {
            /* NACK (either addressing a nonexistent device, or the
             * device we were sending to decided to NACK us).
             */
            DPRINTF("Got NACK\n");
            bitbang_i2c_enter_stop(i2c);
            return bitbang_i2c_ret(i2c, 1);
        }
        if (i2c->current_addr & 1) {
            i2c->state = RECEIVING_BIT7;
        } else {
            i2c->state = SENDING_BIT7;
        }
        return bitbang_i2c_ret(i2c, 0);
    }
    case RECEIVING_BIT7:
        i2c->buffer = i2c_recv(i2c->bus);
        DPRINTF("RX byte 0x%02x\n", i2c->buffer);
        /* Fall through... */
    case RECEIVING_BIT6 ... RECEIVING_BIT0:
        data = i2c->buffer >> 7;
        /* will end up in SENDING_ACK */
        i2c->state++;
        i2c->buffer <<= 1;
        return bitbang_i2c_ret(i2c, data);

    case SENDING_ACK:
        i2c->state = RECEIVING_BIT7;
        if (data != 0) {
            DPRINTF("NACKED\n");
            i2c->state = SENT_NACK;
            i2c_nack(i2c->bus);
        } else {
            DPRINTF("ACKED\n");
        }
        return bitbang_i2c_ret(i2c, 1);
    }
    abort();
}

void bitbang_i2c_init(bitbang_i2c_interface *s, I2CBus *bus)
{
    s->bus = bus;
    s->last_data = 1;
    s->last_clock = 1;
    s->device_out = 1;
}

/* GPIO interface.  */

#define TYPE_GPIO_I2C "gpio_i2c"
#define GPIO_I2C(obj) OBJECT_CHECK(GPIOI2CState, (obj), TYPE_GPIO_I2C)

typedef struct GPIOI2CState {
    SysBusDevice parent_obj;

    MemoryRegion dummy_iomem;
    bitbang_i2c_interface bitbang;
    int last_level;
    qemu_irq out;
} GPIOI2CState;

static void bitbang_i2c_gpio_set(void *opaque, int irq, int level)
{
    GPIOI2CState *s = opaque;

    level = bitbang_i2c_set(&s->bitbang, irq, level);
    if (level != s->last_level) {
        s->last_level = level;
        qemu_set_irq(s->out, level);
    }
}

static void gpio_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    GPIOI2CState *s = GPIO_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    I2CBus *bus;

    memory_region_init(&s->dummy_iomem, obj, "gpio_i2c", 0);
    sysbus_init_mmio(sbd, &s->dummy_iomem);

    bus = i2c_init_bus(dev, "i2c");
    bitbang_i2c_init(&s->bitbang, bus);

    qdev_init_gpio_in(dev, bitbang_i2c_gpio_set, 2);
    qdev_init_gpio_out(dev, &s->out, 1);
}

static void gpio_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "Virtual GPIO to I2C bridge";
}

static const TypeInfo gpio_i2c_info = {
    .name          = TYPE_GPIO_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GPIOI2CState),
    .instance_init = gpio_i2c_init,
    .class_init    = gpio_i2c_class_init,
};

static void bitbang_i2c_register_types(void)
{
    type_register_static(&gpio_i2c_info);
}

type_init(bitbang_i2c_register_types)
