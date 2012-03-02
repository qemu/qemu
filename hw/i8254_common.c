/*
 * QEMU 8253/8254 - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2012      Jan Kiszka, Siemens AG
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "hw.h"
#include "pc.h"
#include "isa.h"
#include "qemu-timer.h"
#include "i8254.h"
#include "i8254_internal.h"

/* val must be 0 or 1 */
void pit_set_gate(ISADevice *dev, int channel, int val)
{
    PITCommonState *pit = PIT_COMMON(dev);
    PITChannelState *s = &pit->channels[channel];
    PITCommonClass *c = PIT_COMMON_GET_CLASS(pit);

    c->set_channel_gate(pit, s, val);
}

/* get pit output bit */
int pit_get_out(PITChannelState *s, int64_t current_time)
{
    uint64_t d;
    int out;

    d = muldiv64(current_time - s->count_load_time, PIT_FREQ,
                 get_ticks_per_sec());
    switch (s->mode) {
    default:
    case 0:
        out = (d >= s->count);
        break;
    case 1:
        out = (d < s->count);
        break;
    case 2:
        if ((d % s->count) == 0 && d != 0) {
            out = 1;
        } else {
            out = 0;
        }
        break;
    case 3:
        out = (d % s->count) < ((s->count + 1) >> 1);
        break;
    case 4:
    case 5:
        out = (d == s->count);
        break;
    }
    return out;
}

/* return -1 if no transition will occur.  */
int64_t pit_get_next_transition_time(PITChannelState *s, int64_t current_time)
{
    uint64_t d, next_time, base;
    int period2;

    d = muldiv64(current_time - s->count_load_time, PIT_FREQ,
                 get_ticks_per_sec());
    switch (s->mode) {
    default:
    case 0:
    case 1:
        if (d < s->count) {
            next_time = s->count;
        } else {
            return -1;
        }
        break;
    case 2:
        base = (d / s->count) * s->count;
        if ((d - base) == 0 && d != 0) {
            next_time = base + s->count;
        } else {
            next_time = base + s->count + 1;
        }
        break;
    case 3:
        base = (d / s->count) * s->count;
        period2 = ((s->count + 1) >> 1);
        if ((d - base) < period2) {
            next_time = base + period2;
        } else {
            next_time = base + s->count;
        }
        break;
    case 4:
    case 5:
        if (d < s->count) {
            next_time = s->count;
        } else if (d == s->count) {
            next_time = s->count + 1;
        } else {
            return -1;
        }
        break;
    }
    /* convert to timer units */
    next_time = s->count_load_time + muldiv64(next_time, get_ticks_per_sec(),
                                              PIT_FREQ);
    /* fix potential rounding problems */
    /* XXX: better solution: use a clock at PIT_FREQ Hz */
    if (next_time <= current_time) {
        next_time = current_time + 1;
    }
    return next_time;
}

void pit_get_channel_info_common(PITCommonState *s, PITChannelState *sc,
                                 PITChannelInfo *info)
{
    info->gate = sc->gate;
    info->mode = sc->mode;
    info->initial_count = sc->count;
    info->out = pit_get_out(sc, qemu_get_clock_ns(vm_clock));
}

void pit_get_channel_info(ISADevice *dev, int channel, PITChannelInfo *info)
{
    PITCommonState *pit = PIT_COMMON(dev);
    PITChannelState *s = &pit->channels[channel];
    PITCommonClass *c = PIT_COMMON_GET_CLASS(pit);

    c->get_channel_info(pit, s, info);
}

void pit_reset_common(PITCommonState *pit)
{
    PITChannelState *s;
    int i;

    for (i = 0; i < 3; i++) {
        s = &pit->channels[i];
        s->mode = 3;
        s->gate = (i != 2);
        s->count_load_time = qemu_get_clock_ns(vm_clock);
        s->count = 0x10000;
        if (i == 0 && !s->irq_disabled) {
            s->next_transition_time =
                pit_get_next_transition_time(s, s->count_load_time);
        }
    }
}

static int pit_init_common(ISADevice *dev)
{
    PITCommonState *pit = PIT_COMMON(dev);
    PITCommonClass *c = PIT_COMMON_GET_CLASS(pit);
    int ret;

    ret = c->init(pit);
    if (ret < 0) {
        return ret;
    }

    isa_register_ioport(dev, &pit->ioports, pit->iobase);

    qdev_set_legacy_instance_id(&dev->qdev, pit->iobase, 2);

    return 0;
}

static const VMStateDescription vmstate_pit_channel = {
    .name = "pit channel",
    .version_id = 2,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(count, PITChannelState),
        VMSTATE_UINT16(latched_count, PITChannelState),
        VMSTATE_UINT8(count_latched, PITChannelState),
        VMSTATE_UINT8(status_latched, PITChannelState),
        VMSTATE_UINT8(status, PITChannelState),
        VMSTATE_UINT8(read_state, PITChannelState),
        VMSTATE_UINT8(write_state, PITChannelState),
        VMSTATE_UINT8(write_latch, PITChannelState),
        VMSTATE_UINT8(rw_mode, PITChannelState),
        VMSTATE_UINT8(mode, PITChannelState),
        VMSTATE_UINT8(bcd, PITChannelState),
        VMSTATE_UINT8(gate, PITChannelState),
        VMSTATE_INT64(count_load_time, PITChannelState),
        VMSTATE_INT64(next_transition_time, PITChannelState),
        VMSTATE_END_OF_LIST()
    }
};

static int pit_load_old(QEMUFile *f, void *opaque, int version_id)
{
    PITCommonState *pit = opaque;
    PITCommonClass *c = PIT_COMMON_GET_CLASS(pit);
    PITChannelState *s;
    int i;

    if (version_id != 1) {
        return -EINVAL;
    }

    for (i = 0; i < 3; i++) {
        s = &pit->channels[i];
        s->count = qemu_get_be32(f);
        qemu_get_be16s(f, &s->latched_count);
        qemu_get_8s(f, &s->count_latched);
        qemu_get_8s(f, &s->status_latched);
        qemu_get_8s(f, &s->status);
        qemu_get_8s(f, &s->read_state);
        qemu_get_8s(f, &s->write_state);
        qemu_get_8s(f, &s->write_latch);
        qemu_get_8s(f, &s->rw_mode);
        qemu_get_8s(f, &s->mode);
        qemu_get_8s(f, &s->bcd);
        qemu_get_8s(f, &s->gate);
        s->count_load_time = qemu_get_be64(f);
        s->irq_disabled = 0;
        if (i == 0) {
            s->next_transition_time = qemu_get_be64(f);
        }
    }
    if (c->post_load) {
        c->post_load(pit);
    }
    return 0;
}

static void pit_dispatch_pre_save(void *opaque)
{
    PITCommonState *s = opaque;
    PITCommonClass *c = PIT_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }
}

static int pit_dispatch_post_load(void *opaque, int version_id)
{
    PITCommonState *s = opaque;
    PITCommonClass *c = PIT_COMMON_GET_CLASS(s);

    if (c->post_load) {
        c->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_pit_common = {
    .name = "i8254",
    .version_id = 3,
    .minimum_version_id = 2,
    .minimum_version_id_old = 1,
    .load_state_old = pit_load_old,
    .pre_save = pit_dispatch_pre_save,
    .post_load = pit_dispatch_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_V(channels[0].irq_disabled, PITCommonState, 3),
        VMSTATE_STRUCT_ARRAY(channels, PITCommonState, 3, 2,
                             vmstate_pit_channel, PITChannelState),
        VMSTATE_INT64(channels[0].next_transition_time,
                      PITCommonState), /* formerly irq_timer */
        VMSTATE_END_OF_LIST()
    }
};

static void pit_common_class_init(ObjectClass *klass, void *data)
{
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    ic->init = pit_init_common;
    dc->vmsd = &vmstate_pit_common;
    dc->no_user = 1;
}

static TypeInfo pit_common_type = {
    .name          = TYPE_PIT_COMMON,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PITCommonState),
    .class_size    = sizeof(PITCommonClass),
    .class_init    = pit_common_class_init,
    .abstract      = true,
};

static void register_devices(void)
{
    type_register_static(&pit_common_type);
}

type_init(register_devices);
