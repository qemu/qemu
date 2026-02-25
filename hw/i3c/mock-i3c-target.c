/*
 * Mock I3C Device
 *
 * Copyright (c) 2025 Google LLC
 *
 * The mock I3C device can be thought of as a simple EEPROM. It has a buffer,
 * and the pointer in the buffer is reset to 0 on an I3C STOP.
 * To write to the buffer, issue a private write and send data.
 * To read from the buffer, issue a private read.
 *
 * The mock target also supports sending target interrupt IBIs.
 * To issue an IBI, set the 'ibi-magic-num' property to a non-zero number, and
 * send that number in a private transaction. The mock target will issue an IBI
 * after 1 second.
 *
 * It also supports a handful of CCCs that are typically used when probing I3C
 * devices.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/mock-i3c-target.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"

#define IBI_DELAY_NS (1 * 1000 * 1000)

static uint32_t mock_i3c_target_rx(I3CTarget *i3c, uint8_t *data,
                                   uint32_t num_to_read)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);
    uint32_t i;

    /* Bounds check. */
    if (s->p_buf == s->cfg.buf_size) {
        return 0;
    }

    for (i = 0; i < num_to_read; i++) {
        data[i] = s->buf[s->p_buf];
        trace_mock_i3c_target_rx(data[i]);
        s->p_buf++;
        if (s->p_buf == s->cfg.buf_size) {
            break;
        }
    }

    /* Return the number of bytes we're sending to the controller. */
    return i;
}

static void mock_i3c_target_ibi_timer_start(MockI3cTargetState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    timer_mod(&s->qtimer, now + IBI_DELAY_NS);
}

static int mock_i3c_target_tx(I3CTarget *i3c, const uint8_t *data,
                              uint32_t num_to_send, uint32_t *num_sent)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);
    int ret;
    uint32_t to_write;

    if (s->cfg.ibi_magic && num_to_send == 1 && s->cfg.ibi_magic == *data) {
        mock_i3c_target_ibi_timer_start(s);
        return 0;
    }

    /* Bounds check. */
    if (num_to_send + s->p_buf > s->cfg.buf_size) {
        to_write = s->cfg.buf_size - s->p_buf;
        ret = -1;
    } else {
        to_write = num_to_send;
        ret = 0;
    }
    for (uint32_t i = 0; i < to_write; i++) {
        trace_mock_i3c_target_tx(data[i]);
        s->buf[s->p_buf] = data[i];
        s->p_buf++;
    }
    return ret;
}

static int mock_i3c_target_event(I3CTarget *i3c, enum I3CEvent event)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);

    trace_mock_i3c_target_event(event);
    if (event == I3C_STOP) {
        s->in_ccc = false;
        s->curr_ccc = 0;
        s->ccc_byte_offset = 0;
        s->p_buf = 0;
    }

    return 0;
}

static int mock_i3c_target_handle_ccc_read(I3CTarget *i3c, uint8_t *data,
                                           uint32_t num_to_read,
                                           uint32_t *num_read)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);

    switch (s->curr_ccc) {
    case I3C_CCCD_GETMXDS:
        /* Default data rate for I3C. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            data[s->ccc_byte_offset] = 0;
            *num_read = s->ccc_byte_offset;
            s->ccc_byte_offset++;
        }
        break;
    case I3C_CCCD_GETCAPS:
        /* Support I3C version 1.1.x, no other features. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            if (s->ccc_byte_offset == 0) {
                data[s->ccc_byte_offset] = 0;
            } else {
                data[s->ccc_byte_offset] = 0x01;
            }
            *num_read = s->ccc_byte_offset;
            s->ccc_byte_offset++;
        }
        break;
    case I3C_CCCD_GETMWL:
    case I3C_CCCD_GETMRL:
        /* MWL/MRL is MSB first. */
        while (s->ccc_byte_offset < num_to_read) {
            if (s->ccc_byte_offset >= 2) {
                break;
            }
            data[s->ccc_byte_offset] = (s->cfg.buf_size &
                                        (0xff00 >> (s->ccc_byte_offset * 8))) >>
                                        (8 - (s->ccc_byte_offset * 8));
            s->ccc_byte_offset++;
            *num_read = num_to_read;
        }
        break;
    case I3C_CCC_ENTDAA:
    case I3C_CCCD_GETPID:
    case I3C_CCCD_GETBCR:
    case I3C_CCCD_GETDCR:
        /* Nothing to do. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Unhandled CCC 0x%.2x\n", s->curr_ccc);
        return -1;
    }

    trace_mock_i3c_target_handle_ccc_read(*num_read, num_to_read);
    return 0;
}

static int mock_i3c_target_handle_ccc_write(I3CTarget *i3c, const uint8_t *data,
                                            uint32_t num_to_send,
                                            uint32_t *num_sent)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);

    if (!s->curr_ccc) {
        s->in_ccc = true;
        s->curr_ccc = *data;
        trace_mock_i3c_target_new_ccc(s->curr_ccc);
    }

    *num_sent = 1;
    switch (s->curr_ccc) {
    case I3C_CCC_ENEC:
    case I3C_CCCD_ENEC:
        s->can_ibi = true;
        break;
    case I3C_CCC_DISEC:
    case I3C_CCCD_DISEC:
        s->can_ibi = false;
        break;
    case I3C_CCC_ENTDAA:
    case I3C_CCC_SETAASA:
    case I3C_CCC_RSTDAA:
    case I3C_CCCD_SETDASA:
    case I3C_CCCD_GETPID:
    case I3C_CCCD_GETBCR:
    case I3C_CCCD_GETDCR:
    case I3C_CCCD_GETMWL:
    case I3C_CCCD_GETMRL:
    case I3C_CCCD_GETMXDS:
    case I3C_CCCD_GETCAPS:
        /* Nothing to do. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "Unhandled CCC 0x%.2x\n", s->curr_ccc);
        return -1;
    }

    trace_mock_i3c_target_handle_ccc_write(*num_sent, num_to_send);
    return 0;
}

static void mock_i3c_target_do_ibi(MockI3cTargetState *s)
{
    if (!s->can_ibi) {
        return;
    }

    trace_mock_i3c_target_do_ibi(s->parent_obj.address, true);
    int nack = i3c_target_send_ibi(&s->parent_obj, s->parent_obj.address,
                                   /*is_recv=*/true);
    /* Getting NACKed isn't necessarily an error, just print it out. */
    if (nack) {
        trace_mock_i3c_target_do_ibi_nack("sending");
    }
    nack = i3c_target_ibi_finish(&s->parent_obj, 0x00);
    if (nack) {
        trace_mock_i3c_target_do_ibi_nack("finishing");
    }
}

static void mock_i3c_target_timer_elapsed(void *opaque)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(opaque);
    timer_del(&s->qtimer);
    mock_i3c_target_do_ibi(s);
}

static void mock_i3c_target_reset(I3CTarget *i3c)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(i3c);
    s->can_ibi = false;
}

static void mock_i3c_target_realize(DeviceState *dev, Error **errp)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(dev);
    s->buf = g_new0(uint8_t, s->cfg.buf_size);
    mock_i3c_target_reset(&s->parent_obj);
}

static void mock_i3c_target_init(Object *obj)
{
    MockI3cTargetState *s = MOCK_I3C_TARGET(obj);
    s->can_ibi = false;

    /* For IBIs. */
    timer_init_ns(&s->qtimer, QEMU_CLOCK_VIRTUAL, mock_i3c_target_timer_elapsed,
                  s);
}

static const Property remote_i3c_props[] = {
    /* The size of the internal buffer. */
    DEFINE_PROP_UINT32("buf-size", MockI3cTargetState, cfg.buf_size, 0x100),
    /*
     * If the mock target receives this number, it will issue an IBI after
     * 1 second. Disabled if the IBI magic number is 0.
     */
    DEFINE_PROP_UINT8("ibi-magic-num", MockI3cTargetState, cfg.ibi_magic, 0x00),
};

static void mock_i3c_target_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I3CTargetClass *k = I3C_TARGET_CLASS(klass);

    dc->realize = mock_i3c_target_realize;
    k->event = mock_i3c_target_event;
    k->recv = mock_i3c_target_rx;
    k->send = mock_i3c_target_tx;
    k->handle_ccc_read = mock_i3c_target_handle_ccc_read;
    k->handle_ccc_write = mock_i3c_target_handle_ccc_write;

    device_class_set_props(dc, remote_i3c_props);
}

static const TypeInfo mock_i3c_target_types[] = {
    {
        .name          = TYPE_MOCK_I3C_TARGET,
        .parent        = TYPE_I3C_TARGET,
        .instance_size = sizeof(MockI3cTargetState),
        .instance_init = mock_i3c_target_init,
        .class_init    = mock_i3c_target_class_init,
    },
};

DEFINE_TYPES(mock_i3c_target_types)

