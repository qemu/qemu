/*
 * USB xHCI controller for system-bus interface
 * Based on hcd-echi-sysbus.c

 * SPDX-FileCopyrightText: 2020 Xilinx
 * SPDX-FileContributor: Author: Sai Pavan Boddu <sai.pavan.boddu@xilinx.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "hcd-xhci-sysbus.h"
#include "hw/acpi/aml-build.h"
#include "hw/irq.h"

static bool xhci_sysbus_intr_raise(XHCIState *xhci, int n, bool level)
{
    XHCISysbusState *s = container_of(xhci, XHCISysbusState, xhci);

    qemu_set_irq(s->irq[n], level);

    return false;
}

void xhci_sysbus_reset(DeviceState *dev)
{
    XHCISysbusState *s = XHCI_SYSBUS(dev);

    device_cold_reset(DEVICE(&s->xhci));
}

static void xhci_sysbus_realize(DeviceState *dev, Error **errp)
{
    XHCISysbusState *s = XHCI_SYSBUS(dev);

    object_property_set_link(OBJECT(&s->xhci), "host", OBJECT(s), NULL);
    if (!qdev_realize(DEVICE(&s->xhci), NULL, errp)) {
        return;
    }
    s->irq = g_new0(qemu_irq, s->xhci.numintrs);
    qdev_init_gpio_out_named(dev, s->irq, SYSBUS_DEVICE_GPIO_IRQ,
                             s->xhci.numintrs);
    if (s->xhci.dma_mr) {
        s->xhci.as =  g_malloc0(sizeof(AddressSpace));
        address_space_init(s->xhci.as, s->xhci.dma_mr, NULL);
    } else {
        s->xhci.as = &address_space_memory;
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->xhci.mem);
}

static void xhci_sysbus_instance_init(Object *obj)
{
    XHCISysbusState *s = XHCI_SYSBUS(obj);

    object_initialize_child(obj, "xhci-core", &s->xhci, TYPE_XHCI);
    qdev_alias_all_properties(DEVICE(&s->xhci), obj);

    object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
                             (Object **)&s->xhci.dma_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_STRONG);
    s->xhci.intr_update = NULL;
    s->xhci.intr_raise = xhci_sysbus_intr_raise;
}

void xhci_sysbus_build_aml(Aml *scope, uint32_t mmio, unsigned int irq)
{
    Aml *dev = aml_device("XHCI");
    Aml *crs = aml_resource_template();

    aml_append(crs, aml_memory32_fixed(mmio, XHCI_LEN_REGS, AML_READ_WRITE));
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_LEVEL, AML_ACTIVE_HIGH,
                                  AML_EXCLUSIVE, &irq, 1));

    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0D10")));
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(scope, dev);
}

static Property xhci_sysbus_props[] = {
    DEFINE_PROP_UINT32("intrs", XHCISysbusState, xhci.numintrs, XHCI_MAXINTRS),
    DEFINE_PROP_UINT32("slots", XHCISysbusState, xhci.numslots, XHCI_MAXSLOTS),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_xhci_sysbus = {
    .name = "xhci-sysbus",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(xhci, XHCISysbusState, 1, vmstate_xhci, XHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static void xhci_sysbus_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = xhci_sysbus_reset;
    dc->realize = xhci_sysbus_realize;
    dc->vmsd = &vmstate_xhci_sysbus;
    device_class_set_props(dc, xhci_sysbus_props);
}

static const TypeInfo xhci_sysbus_info = {
    .name          = TYPE_XHCI_SYSBUS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XHCISysbusState),
    .class_init    = xhci_sysbus_class_init,
    .instance_init = xhci_sysbus_instance_init
};

static void xhci_sysbus_register_types(void)
{
    type_register_static(&xhci_sysbus_info);
}

type_init(xhci_sysbus_register_types);
