/*
 * QEMU USB EHCI Emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or(at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/usb/hcd-ehci.h"

static const VMStateDescription vmstate_ehci_sysbus = {
    .name        = "ehci-sysbus",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT(ehci, EHCISysBusState, 2, vmstate_ehci, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ehci_sysbus_properties[] = {
    DEFINE_PROP_UINT32("maxframes", EHCISysBusState, ehci.maxframes, 128),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_ehci_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(dev);
    EHCIState *s = &i->ehci;

    usb_ehci_realize(s, dev, errp);
    sysbus_init_irq(d, &s->irq);
}

static void ehci_sysbus_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_GET_CLASS(obj);
    EHCIState *s = &i->ehci;

    s->capsbase = sec->capsbase;
    s->opregbase = sec->opregbase;
    s->as = &address_space_memory;

    usb_ehci_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void ehci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = usb_ehci_sysbus_realize;
    dc->vmsd = &vmstate_ehci_sysbus;
    dc->props = ehci_sysbus_properties;
}

static const TypeInfo ehci_type_info = {
    .name          = TYPE_SYS_BUS_EHCI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EHCISysBusState),
    .instance_init = ehci_sysbus_init,
    .abstract      = true,
    .class_init    = ehci_sysbus_class_init,
    .class_size    = sizeof(SysBusEHCIClass),
};

static void ehci_xlnx_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
}

static const TypeInfo ehci_xlnx_type_info = {
    .name          = "xlnx,ps7-usb",
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_xlnx_class_init,
};

static void ehci_exynos4210_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
}

static const TypeInfo ehci_exynos4210_type_info = {
    .name          = TYPE_EXYNOS4210_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_exynos4210_class_init,
};

static void ehci_tegra2_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
}

static const TypeInfo ehci_tegra2_type_info = {
    .name          = TYPE_TEGRA2_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_tegra2_class_init,
};

static void ehci_sysbus_register_types(void)
{
    type_register_static(&ehci_type_info);
    type_register_static(&ehci_xlnx_type_info);
    type_register_static(&ehci_exynos4210_type_info);
    type_register_static(&ehci_tegra2_type_info);
}

type_init(ehci_sysbus_register_types)
