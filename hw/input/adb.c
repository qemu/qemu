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
#include "adb-internal.h"

/* error codes */
#define ADB_RET_NOTPRESENT (-2)

static void adb_device_reset(ADBDevice *d)
{
    qdev_reset_all(DEVICE(d));
}

int adb_request(ADBBusState *s, uint8_t *obuf, const uint8_t *buf, int len)
{
    ADBDevice *d;
    int devaddr, cmd, i;

    cmd = buf[0] & 0xf;
    if (cmd == ADB_BUSRESET) {
        for(i = 0; i < s->nb_devices; i++) {
            d = s->devices[i];
            adb_device_reset(d);
        }
        return 0;
    }
    devaddr = buf[0] >> 4;
    for(i = 0; i < s->nb_devices; i++) {
        d = s->devices[i];
        if (d->devaddr == devaddr) {
            ADBDeviceClass *adc = ADB_DEVICE_GET_CLASS(d);
            return adc->devreq(d, obuf, buf, len);
        }
    }
    return ADB_RET_NOTPRESENT;
}

/* XXX: move that to cuda ? */
int adb_poll(ADBBusState *s, uint8_t *obuf, uint16_t poll_mask)
{
    ADBDevice *d;
    int olen, i;
    uint8_t buf[1];

    olen = 0;
    for(i = 0; i < s->nb_devices; i++) {
        if (s->poll_index >= s->nb_devices)
            s->poll_index = 0;
        d = s->devices[s->poll_index];
        if ((1 << d->devaddr) & poll_mask) {
            buf[0] = ADB_READREG | (d->devaddr << 4);
            olen = adb_request(s, obuf + 1, buf, 1);
            /* if there is data, we poll again the same device */
            if (olen > 0) {
                obuf[0] = buf[0];
                olen++;
                break;
            }
        }
        s->poll_index++;
    }
    return olen;
}

static const TypeInfo adb_bus_type_info = {
    .name = TYPE_ADB_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(ADBBusState),
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

static Property adb_device_properties[] = {
    DEFINE_PROP_BOOL("disable-direct-reg3-writes", ADBDevice,
                     disable_direct_reg3_writes, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void adb_device_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = adb_device_realizefn;
    device_class_set_props(dc, adb_device_properties);
    dc->bus_type = TYPE_ADB_BUS;
}

static const TypeInfo adb_device_type_info = {
    .name = TYPE_ADB_DEVICE,
    .parent = TYPE_DEVICE,
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
