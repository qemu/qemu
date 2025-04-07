/*
 * Example I2C device using asynchronous I2C send.
 *
 * Copyright (C) 2023 Samsung Electronics Co., Ltd. All Rights Reserved.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h"
#include "block/aio.h"
#include "hw/i2c/i2c.h"
#include "trace.h"

#define TYPE_I2C_ECHO "i2c-echo"
OBJECT_DECLARE_SIMPLE_TYPE(I2CEchoState, I2C_ECHO)

enum i2c_echo_state {
    I2C_ECHO_STATE_IDLE,
    I2C_ECHO_STATE_START_SEND,
    I2C_ECHO_STATE_ACK,
};

typedef struct I2CEchoState {
    I2CSlave parent_obj;

    I2CBus *bus;

    enum i2c_echo_state state;
    QEMUBH *bh;

    unsigned int pos;
    uint8_t data[3];
} I2CEchoState;

static void i2c_echo_bh(void *opaque)
{
    I2CEchoState *state = opaque;

    switch (state->state) {
    case I2C_ECHO_STATE_IDLE:
        return;

    case I2C_ECHO_STATE_START_SEND:
        if (i2c_start_send_async(state->bus, state->data[0])) {
            goto release_bus;
        }

        state->pos++;
        state->state = I2C_ECHO_STATE_ACK;
        return;

    case I2C_ECHO_STATE_ACK:
        if (state->pos > 2) {
            break;
        }

        if (i2c_send_async(state->bus, state->data[state->pos++])) {
            break;
        }

        return;
    }


    i2c_end_transfer(state->bus);
release_bus:
    i2c_bus_release(state->bus);

    state->state = I2C_ECHO_STATE_IDLE;
}

static int i2c_echo_event(I2CSlave *s, enum i2c_event event)
{
    I2CEchoState *state = I2C_ECHO(s);

    switch (event) {
    case I2C_START_RECV:
        state->pos = 0;

        trace_i2c_echo_event(DEVICE(s)->canonical_path, "I2C_START_RECV");
        break;

    case I2C_START_SEND:
        state->pos = 0;

        trace_i2c_echo_event(DEVICE(s)->canonical_path, "I2C_START_SEND");
        break;

    case I2C_FINISH:
        state->pos = 0;
        state->state = I2C_ECHO_STATE_START_SEND;
        i2c_bus_master(state->bus, state->bh);

        trace_i2c_echo_event(DEVICE(s)->canonical_path, "I2C_FINISH");
        break;

    case I2C_NACK:
        trace_i2c_echo_event(DEVICE(s)->canonical_path, "I2C_NACK");
        break;

    default:
        trace_i2c_echo_event(DEVICE(s)->canonical_path, "UNHANDLED");
        return -1;
    }

    return 0;
}

static uint8_t i2c_echo_recv(I2CSlave *s)
{
    I2CEchoState *state = I2C_ECHO(s);

    if (state->pos > 2) {
        return 0xff;
    }

    trace_i2c_echo_recv(DEVICE(s)->canonical_path, state->data[state->pos]);
    return state->data[state->pos++];
}

static int i2c_echo_send(I2CSlave *s, uint8_t data)
{
    I2CEchoState *state = I2C_ECHO(s);

    trace_i2c_echo_send(DEVICE(s)->canonical_path, data);
    if (state->pos > 2) {
        return -1;
    }

    state->data[state->pos++] = data;

    return 0;
}

static void i2c_echo_realize(DeviceState *dev, Error **errp)
{
    I2CEchoState *state = I2C_ECHO(dev);
    BusState *bus = qdev_get_parent_bus(dev);

    state->bus = I2C_BUS(bus);
    state->bh = qemu_bh_new(i2c_echo_bh, state);
}

static void i2c_echo_class_init(ObjectClass *oc, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = i2c_echo_realize;

    sc->event = i2c_echo_event;
    sc->recv = i2c_echo_recv;
    sc->send = i2c_echo_send;
}

static const TypeInfo i2c_echo = {
    .name = TYPE_I2C_ECHO,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(I2CEchoState),
    .class_init = i2c_echo_class_init,
};

static void register_types(void)
{
    type_register_static(&i2c_echo);
}

type_init(register_types);
