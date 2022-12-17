/*
 * QEMU ADB support
 *
 * Copyright (c) 2004 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "hw/input/adb.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "adb-internal.h"
#include "trace.h"

/* error codes */
#define ADB_RET_NOTPRESENT (-2)

static const char *adb_commands[] = {
    "RESET", "FLUSH", "(Reserved 0x2)", "(Reserved 0x3)",
    "Reserved (0x4)", "(Reserved 0x5)", "(Reserved 0x6)", "(Reserved 0x7)",
    "LISTEN r0", "LISTEN r1", "LISTEN r2", "LISTEN r3",
    "TALK r0", "TALK r1", "TALK r2", "TALK r3",
};

static void adb_device_reset(ADBDevice *d)
{
    device_cold_reset(DEVICE(d));
}

static int do_adb_request(ADBBusState *s, uint8_t *obuf, const uint8_t *buf,
                          int len)
{
    ADBDevice *d;
    ADBDeviceClass *adc;
    int devaddr, cmd, olen, i;

    cmd = buf[0] & 0xf;
    if (cmd == ADB_BUSRESET) {
        for (i = 0; i < s->nb_devices; i++) {
            d = s->devices[i];
            adb_device_reset(d);
        }
        s->status = 0;
        return 0;
    }

    s->pending = 0;
    for (i = 0; i < s->nb_devices; i++) {
        d = s->devices[i];
        adc = ADB_DEVICE_GET_CLASS(d);

        if (adc->devhasdata(d)) {
            s->pending |= (1 << d->devaddr);
        }
    }

    s->status = 0;
    devaddr = buf[0] >> 4;
    for (i = 0; i < s->nb_devices; i++) {
        d = s->devices[i];
        adc = ADB_DEVICE_GET_CLASS(d);

        if (d->devaddr == devaddr) {
            olen = adc->devreq(d, obuf, buf, len);
            if (!olen) {
                s->status |= ADB_STATUS_BUSTIMEOUT;
            }
            return olen;
        }
    }

    s->status |= ADB_STATUS_BUSTIMEOUT;
    return ADB_RET_NOTPRESENT;
}

int adb_request(ADBBusState *s, uint8_t *obuf, const uint8_t *buf, int len)
{
    int ret;

    trace_adb_bus_request(buf[0] >> 4, adb_commands[buf[0] & 0xf], len);

    assert(s->autopoll_blocked);

    ret = do_adb_request(s, obuf, buf, len);

    trace_adb_bus_request_done(buf[0] >> 4, adb_commands[buf[0] & 0xf], ret);
    return ret;
}

int adb_poll(ADBBusState *s, uint8_t *obuf, uint16_t poll_mask)
{
    ADBDevice *d;
    int olen, i;
    uint8_t buf[1];

    olen = 0;
    for (i = 0; i < s->nb_devices; i++) {
        if (s->poll_index >= s->nb_devices) {
            s->poll_index = 0;
        }
        d = s->devices[s->poll_index];
        if ((1 << d->devaddr) & poll_mask) {
            buf[0] = ADB_READREG | (d->devaddr << 4);
            olen = do_adb_request(s, obuf + 1, buf, 1);
            /* if there is data, we poll again the same device */
            if (olen > 0) {
                s->status |= ADB_STATUS_POLLREPLY;
                obuf[0] = buf[0];
                olen++;
                return olen;
            }
        }
        s->poll_index++;
    }
    return olen;
}

void adb_set_autopoll_enabled(ADBBusState *s, bool enabled)
{
    if (s->autopoll_enabled != enabled) {
        s->autopoll_enabled = enabled;
        if (s->autopoll_enabled) {
            timer_mod(s->autopoll_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                      s->autopoll_rate_ms);
        } else {
            timer_del(s->autopoll_timer);
        }
    }
}

void adb_set_autopoll_rate_ms(ADBBusState *s, int rate_ms)
{
    s->autopoll_rate_ms = rate_ms;

    if (s->autopoll_enabled) {
        timer_mod(s->autopoll_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  s->autopoll_rate_ms);
    }
}

void adb_set_autopoll_mask(ADBBusState *s, uint16_t mask)
{
    if (s->autopoll_mask != mask) {
        s->autopoll_mask = mask;
        if (s->autopoll_enabled && s->autopoll_mask) {
            timer_mod(s->autopoll_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                      s->autopoll_rate_ms);
        } else {
            timer_del(s->autopoll_timer);
        }
    }
}

void adb_autopoll_block(ADBBusState *s)
{
    s->autopoll_blocked = true;
    trace_adb_bus_autopoll_block(s->autopoll_blocked);

    if (s->autopoll_enabled) {
        timer_del(s->autopoll_timer);
    }
}

void adb_autopoll_unblock(ADBBusState *s)
{
    s->autopoll_blocked = false;
    trace_adb_bus_autopoll_block(s->autopoll_blocked);

    if (s->autopoll_enabled) {
        timer_mod(s->autopoll_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                  s->autopoll_rate_ms);
    }
}

static void adb_autopoll(void *opaque)
{
    ADBBusState *s = opaque;

    if (!s->autopoll_blocked) {
        trace_adb_bus_autopoll_cb(s->autopoll_mask);
        s->autopoll_cb(s->autopoll_cb_opaque);
        trace_adb_bus_autopoll_cb_done(s->autopoll_mask);
    }

    timer_mod(s->autopoll_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              s->autopoll_rate_ms);
}

void adb_register_autopoll_callback(ADBBusState *s, void (*cb)(void *opaque),
                                    void *opaque)
{
    s->autopoll_cb = cb;
    s->autopoll_cb_opaque = opaque;
}

static const VMStateDescription vmstate_adb_bus = {
    .name = "adb_bus",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_TIMER_PTR(autopoll_timer, ADBBusState),
        VMSTATE_BOOL(autopoll_enabled, ADBBusState),
        VMSTATE_UINT8(autopoll_rate_ms, ADBBusState),
        VMSTATE_UINT16(autopoll_mask, ADBBusState),
        VMSTATE_BOOL(autopoll_blocked, ADBBusState),
        VMSTATE_END_OF_LIST()
    }
};

static void adb_bus_reset(BusState *qbus)
{
    ADBBusState *adb_bus = ADB_BUS(qbus);

    adb_bus->autopoll_enabled = false;
    adb_bus->autopoll_mask = 0xffff;
    adb_bus->autopoll_rate_ms = 20;
}

static void adb_bus_realize(BusState *qbus, Error **errp)
{
    ADBBusState *adb_bus = ADB_BUS(qbus);

    adb_bus->autopoll_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, adb_autopoll,
                                           adb_bus);

    vmstate_register(NULL, -1, &vmstate_adb_bus, adb_bus);
}

static void adb_bus_unrealize(BusState *qbus)
{
    ADBBusState *adb_bus = ADB_BUS(qbus);

    timer_del(adb_bus->autopoll_timer);

    vmstate_unregister(NULL, &vmstate_adb_bus, adb_bus);
}

static void adb_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->realize = adb_bus_realize;
    k->unrealize = adb_bus_unrealize;
    k->reset = adb_bus_reset;
}

static const TypeInfo adb_bus_type_info = {
    .name = TYPE_ADB_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ADBBusState),
    .class_init = adb_bus_class_init,
};

const VMStateDescription vmstate_adb_device = {
    .name = "adb_device",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(devaddr, ADBDevice),
        VMSTATE_INT32(handler, ADBDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void adb_device_realizefn(DeviceState *dev, Error **errp)
{
    ADBDevice *d = ADB_DEVICE(dev);
    ADBBusState *bus = ADB_BUS(qdev_get_parent_bus(dev));

    if (bus->nb_devices >= MAX_ADB_DEVICES) {
        return;
    }

    bus->devices[bus->nb_devices++] = d;
}

static void adb_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = adb_device_realizefn;
    dc->bus_type = TYPE_ADB_BUS;
}

static const TypeInfo adb_device_type_info = {
    .name = TYPE_ADB_DEVICE,
    .parent = TYPE_DEVICE,
    .class_size = sizeof(ADBDeviceClass),
    .instance_size = sizeof(ADBDevice),
    .abstract = true,
    .class_init = adb_device_class_init,
};

static void adb_register_types(void)
{
    type_register_static(&adb_bus_type_info);
    type_register_static(&adb_device_type_info);
}

type_init(adb_register_types)
