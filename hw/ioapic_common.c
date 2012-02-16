/*
 *  IOAPIC emulation logic - common bits of emulated and KVM kernel model
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *  Copyright (c) 2009      Xiantao Zhang, Intel
 *  Copyright (c) 2011      Jan Kiszka, Siemens AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "ioapic.h"
#include "ioapic_internal.h"
#include "sysbus.h"

void ioapic_reset_common(DeviceState *dev)
{
    IOAPICCommonState *s = IOAPIC_COMMON(dev);
    int i;

    s->id = 0;
    s->ioregsel = 0;
    s->irr = 0;
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        s->ioredtbl[i] = 1 << IOAPIC_LVT_MASKED_SHIFT;
    }
}

static void ioapic_dispatch_pre_save(void *opaque)
{
    IOAPICCommonState *s = IOAPIC_COMMON(opaque);
    IOAPICCommonClass *info = IOAPIC_COMMON_GET_CLASS(s);

    if (info->pre_save) {
        info->pre_save(s);
    }
}

static int ioapic_dispatch_post_load(void *opaque, int version_id)
{
    IOAPICCommonState *s = IOAPIC_COMMON(opaque);
    IOAPICCommonClass *info = IOAPIC_COMMON_GET_CLASS(s);

    if (info->post_load) {
        info->post_load(s);
    }
    return 0;
}

static int ioapic_init_common(SysBusDevice *dev)
{
    IOAPICCommonState *s = FROM_SYSBUS(IOAPICCommonState, dev);
    IOAPICCommonClass *info;
    static int ioapic_no;

    if (ioapic_no >= MAX_IOAPICS) {
        return -1;
    }

    info = IOAPIC_COMMON_GET_CLASS(s);
    info->init(s, ioapic_no);

    sysbus_init_mmio(&s->busdev, &s->io_memory);
    ioapic_no++;

    return 0;
}

static const VMStateDescription vmstate_ioapic_common = {
    .name = "ioapic",
    .version_id = 3,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .pre_save = ioapic_dispatch_pre_save,
    .post_load = ioapic_dispatch_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(id, IOAPICCommonState),
        VMSTATE_UINT8(ioregsel, IOAPICCommonState),
        VMSTATE_UNUSED_V(2, 8), /* to account for qemu-kvm's v2 format */
        VMSTATE_UINT32_V(irr, IOAPICCommonState, 2),
        VMSTATE_UINT64_ARRAY(ioredtbl, IOAPICCommonState, IOAPIC_NUM_PINS),
        VMSTATE_END_OF_LIST()
    }
};

static void ioapic_common_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sc->init = ioapic_init_common;
    dc->vmsd = &vmstate_ioapic_common;
    dc->no_user = 1;
}

static TypeInfo ioapic_common_type = {
    .name = TYPE_IOAPIC_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IOAPICCommonState),
    .class_size = sizeof(IOAPICCommonClass),
    .class_init = ioapic_common_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&ioapic_common_type);
}

type_init(register_types)
