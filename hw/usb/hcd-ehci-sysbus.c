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
    s->portscbase = sec->portscbase;
    s->portnr = sec->portnr;
    s->as = &address_space_memory;

    usb_ehci_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void ehci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(klass);

    sec->portscbase = 0x44;
    sec->portnr = NB_PORTS;

    dc->realize = usb_ehci_sysbus_realize;
    dc->vmsd = &vmstate_ehci_sysbus;
    dc->props = ehci_sysbus_properties;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
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
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_USB, dc->categories);
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
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_exynos4210_type_info = {
    .name          = TYPE_EXYNOS4210_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_exynos4210_class_init,
};

static void ehci_tegra2_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_tegra2_type_info = {
    .name          = TYPE_TEGRA2_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .class_init    = ehci_tegra2_class_init,
};

/*
 * Faraday FUSBH200 USB 2.0 EHCI
 */

/**
 * FUSBH200EHCIRegs:
 * @FUSBH200_REG_EOF_ASTR: EOF/Async. Sleep Timer Register
 * @FUSBH200_REG_BMCSR: Bus Monitor Control/Status Register
 */
enum FUSBH200EHCIRegs {
    FUSBH200_REG_EOF_ASTR = 0x34,
    FUSBH200_REG_BMCSR    = 0x40,
};

static uint64_t fusbh200_ehci_read(void *opaque, hwaddr addr, unsigned size)
{
    EHCIState *s = opaque;
    hwaddr off = s->opregbase + s->portscbase + 4 * s->portnr + addr;

    switch (off) {
    case FUSBH200_REG_EOF_ASTR:
        return 0x00000041;
    case FUSBH200_REG_BMCSR:
        /* High-Speed, VBUS valid, interrupt level-high active */
        return (2 << 9) | (1 << 8) | (1 << 3);
    }

    return 0;
}

static void fusbh200_ehci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
}

static const MemoryRegionOps fusbh200_ehci_mmio_ops = {
    .read = fusbh200_ehci_read,
    .write = fusbh200_ehci_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fusbh200_ehci_init(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    FUSBH200EHCIState *f = FUSBH200_EHCI(obj);
    EHCIState *s = &i->ehci;

    memory_region_init_io(&f->mem_vendor, OBJECT(f), &fusbh200_ehci_mmio_ops, s,
                          "fusbh200", 0x4c);
    memory_region_add_subregion(&s->mem,
                                s->opregbase + s->portscbase + 4 * s->portnr,
                                &f->mem_vendor);
}

static void fusbh200_ehci_class_init(ObjectClass *oc, void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x20;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_fusbh200_type_info = {
    .name          = TYPE_FUSBH200_EHCI,
    .parent        = TYPE_SYS_BUS_EHCI,
    .instance_size = sizeof(FUSBH200EHCIState),
    .instance_init = fusbh200_ehci_init,
    .class_init    = fusbh200_ehci_class_init,
};

static void ehci_sysbus_register_types(void)
{
    type_register_static(&ehci_type_info);
    type_register_static(&ehci_xlnx_type_info);
    type_register_static(&ehci_exynos4210_type_info);
    type_register_static(&ehci_tegra2_type_info);
    type_register_static(&ehci_fusbh200_type_info);
}

type_init(ehci_sysbus_register_types)
