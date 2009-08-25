/*
 * Bit-Bang i2c emulation extracted from
 * Marvell MV88W8618 / Freecom MusicPal emulation.
 *
 * Copyright (c) 2008 Jan Kiszka
 *
 * This code is licenced under the GNU GPL v2.
 */
#include "hw.h"
#include "i2c.h"
#include "sysbus.h"

typedef enum bitbang_i2c_state {
    STOPPED = 0,
    INITIALIZING,
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
    SENDING_ACK
} bitbang_i2c_state;

typedef struct bitbang_i2c_interface {
    SysBusDevice busdev;
    i2c_bus *bus;
    bitbang_i2c_state state;
    int last_data;
    int last_clock;
    uint8_t buffer;
    int current_addr;
    qemu_irq out;
} bitbang_i2c_interface;

static void bitbang_i2c_enter_stop(bitbang_i2c_interface *i2c)
{
    if (i2c->current_addr >= 0)
        i2c_end_transfer(i2c->bus);
    i2c->current_addr = -1;
    i2c->state = STOPPED;
}

static void bitbang_i2c_gpio_set(void *opaque, int irq, int level)
{
    bitbang_i2c_interface *i2c = opaque;
    int data;
    int clock;
    int data_goes_up;
    int data_goes_down;
    int clock_goes_up;
    int clock_goes_down;

    /* get pins states */
    data    = i2c->last_data;
    clock   = i2c->last_clock;

    if (irq == 0)
        data = level;
    if (irq == 1)
        clock = level;

    /* compute pins changes */
    data_goes_up    = data == 1 && i2c->last_data == 0;
    data_goes_down  = data == 0 && i2c->last_data == 1;
    clock_goes_up   = clock == 1 && i2c->last_clock == 0;
    clock_goes_down = clock == 0 && i2c->last_clock == 1;

    if (data_goes_up == 0 && data_goes_down == 0 &&
        clock_goes_up == 0 && clock_goes_down == 0)
        return;

    if (!i2c)
        return;

    if ((RECEIVING_BIT7 > i2c->state && i2c->state > RECEIVING_BIT0)
            || i2c->state == WAITING_FOR_ACK)
        qemu_set_irq(i2c->out, 0);

    switch (i2c->state) {
    case STOPPED:
        if (data_goes_down && clock == 1)
            i2c->state = INITIALIZING;
        break;

    case INITIALIZING:
        if (clock_goes_down && data == 0)
            i2c->state = SENDING_BIT7;
        else
            bitbang_i2c_enter_stop(i2c);
        break;

    case SENDING_BIT7 ... SENDING_BIT0:
        if (clock_goes_down) {
            i2c->buffer = (i2c->buffer << 1) | data;
            /* will end up in WAITING_FOR_ACK */
            i2c->state++; 
        } else if (data_goes_up && clock == 1)
            bitbang_i2c_enter_stop(i2c);
        break;

    case WAITING_FOR_ACK:
        if (clock_goes_down) {
            if (i2c->current_addr < 0) {
                i2c->current_addr = i2c->buffer;
                i2c_start_transfer(i2c->bus, (i2c->current_addr & 0xfe) / 2,
                                   i2c->buffer & 1);
            } else
                i2c_send(i2c->bus, i2c->buffer);
            if (i2c->current_addr & 1) {
                i2c->state = RECEIVING_BIT7;
                i2c->buffer = i2c_recv(i2c->bus);
            } else
                i2c->state = SENDING_BIT7;
        } else if (data_goes_up && clock == 1)
            bitbang_i2c_enter_stop(i2c);
        break;

    case RECEIVING_BIT7 ... RECEIVING_BIT0:
        qemu_set_irq(i2c->out, i2c->buffer >> 7);
        if (clock_goes_down) {
            /* will end up in SENDING_ACK */
            i2c->state++;
            i2c->buffer <<= 1;
        } else if (data_goes_up && clock == 1)
            bitbang_i2c_enter_stop(i2c);
        break;

    case SENDING_ACK:
        if (clock_goes_down) {
            i2c->state = RECEIVING_BIT7;
            if (data == 0)
                i2c->buffer = i2c_recv(i2c->bus);
            else
                i2c_nack(i2c->bus);
        } else if (data_goes_up && clock == 1)
            bitbang_i2c_enter_stop(i2c);
        break;
    }

    i2c->last_data = data;
    i2c->last_clock = clock;
}

static void bitbang_i2c_init(SysBusDevice *dev)
{
    bitbang_i2c_interface *s = FROM_SYSBUS(bitbang_i2c_interface, dev);
    i2c_bus *bus;

    sysbus_init_mmio(dev, 0x0, 0);

    bus = i2c_init_bus(&dev->qdev, "i2c");
    s->bus = bus;

    s->last_data = 1;
    s->last_clock = 1;

    qdev_init_gpio_in(&dev->qdev, bitbang_i2c_gpio_set, 2);
    qdev_init_gpio_out(&dev->qdev, &s->out, 1);
}

static void bitbang_i2c_register(void)
{
    sysbus_register_dev("bitbang_i2c",
        sizeof(bitbang_i2c_interface), bitbang_i2c_init);
}

device_init(bitbang_i2c_register)
