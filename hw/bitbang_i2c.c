/*
 * Bit-Bang i2c emulation extracted from
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licenced under the GNU GPL v2.
 */
#include "hw.h"
#include "bitbang_i2c.h"
#include "sysbus.h"

//#define DEBUG_BITBANG_I2C

#ifdef DEBUG_BITBANG_I2C
#define DPRINTF(fmt, ...) \
do { printf("bitbang_i2c: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while(0)
#endif

typedef enum bitbang_i2c_state {
    STOPPED = 0,
    SENDING_BIT7,
    SENDING_BIT6,
    SENDING_BIT5,
    SENDING_BIT4,
    SENDING_BIT3,
    SENDING_BIT2,
    SENDING_BIT1,
    SENDING_BIT0,
    WAITING_FOR_ACK,
    RECEIVING_BIT7,
    RECEIVING_BIT6,
    RECEIVING_BIT5,
    RECEIVING_BIT4,
    RECEIVING_BIT3,
    RECEIVING_BIT2,
    RECEIVING_BIT1,
    RECEIVING_BIT0,
    SENDING_ACK,
    SENT_NACK
} bitbang_i2c_state;

struct bitbang_i2c_interface {
    i2c_bus *bus;
    bitbang_i2c_state state;
    int last_data;
    int last_clock;
    int device_out;
    uint8_t buffer;
    int current_addr;
};

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
        if (i2c->current_addr < 0) {
            i2c->current_addr = i2c->buffer;
            DPRINTF("Address 0x%02x\n", i2c->current_addr);
            i2c_start_transfer(i2c->bus, i2c->current_addr >> 1,
                               i2c->current_addr & 1);
        } else {
            DPRINTF("Sent 0x%02x\n", i2c->buffer);
            i2c_send(i2c->bus, i2c->buffer);
        }
        if (i2c->current_addr & 1) {
            i2c->state = RECEIVING_BIT7;
        } else {
            i2c->state = SENDING_BIT7;
        }
        return bitbang_i2c_ret(i2c, 0);

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

bitbang_i2c_interface *bitbang_i2c_init(i2c_bus *bus)
{
    bitbang_i2c_interface *s;

    s = qemu_mallocz(sizeof(bitbang_i2c_interface));

    s->bus = bus;
    s->last_data = 1;
    s->last_clock = 1;
    s->device_out = 1;

    return s;
}

/* GPIO interface.  */
typedef struct {
    SysBusDevice busdev;
    bitbang_i2c_interface *bitbang;
    int last_level;
    qemu_irq out;
} GPIOI2CState;

static void bitbang_i2c_gpio_set(void *opaque, int irq, int level)
{
    GPIOI2CState *s = opaque;

    level = bitbang_i2c_set(s->bitbang, irq, level);
    if (level != s->last_level) {
        s->last_level = level;
        qemu_set_irq(s->out, level);
    }
}

static int gpio_i2c_init(SysBusDevice *dev)
{
    GPIOI2CState *s = FROM_SYSBUS(GPIOI2CState, dev);
    i2c_bus *bus;

    sysbus_init_mmio(dev, 0x0, 0);

    bus = i2c_init_bus(&dev->qdev, "i2c");
    s->bitbang = bitbang_i2c_init(bus);

    qdev_init_gpio_in(&dev->qdev, bitbang_i2c_gpio_set, 2);
    qdev_init_gpio_out(&dev->qdev, &s->out, 1);

    return 0;
}

static SysBusDeviceInfo gpio_i2c_info = {
    .init = gpio_i2c_init,
    .qdev.name  = "gpio_i2c",
    .qdev.desc  = "Virtual GPIO to I2C bridge",
    .qdev.size  = sizeof(GPIOI2CState),
};

static void bitbang_i2c_register(void)
{
    sysbus_register_withprop(&gpio_i2c_info);
}

device_init(bitbang_i2c_register)
