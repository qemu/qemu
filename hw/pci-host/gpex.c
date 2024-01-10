/*
 * QEMU Generic PCI Express Bridge Emulation
 *
 * Copyright (C) 2015 Alexander Graf <agraf@suse.de>
 *
 * Code loosely based on q35.c.
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
 *
 * Check out these documents for more information on the device:
 *
 * http://www.kernel.org/doc/Documentation/devicetree/bindings/pci/host-generic-pci.txt
 * http://www.firmware.org/1275/practice/imap/imap0_9d.pdf
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/pci-host/gpex.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

/****************************************************************************
 * GPEX host
 */

static void gpex_set_irq(void *opaque, int irq_num, int level)
{
    GPEXHost *s = opaque;

    qemu_set_irq(s->irq[irq_num], level);
}

int gpex_set_irq_num(GPEXHost *s, int index, int gsi)
{
    if (index >= GPEX_NUM_IRQS) {
        return -EINVAL;
    }

    s->irq_num[index] = gsi;
    return 0;
}

static PCIINTxRoute gpex_route_intx_pin_to_irq(void *opaque, int pin)
{
    PCIINTxRoute route;
    GPEXHost *s = opaque;
    int gsi = s->irq_num[pin];

    route.irq = gsi;
    if (gsi < 0) {
        route.mode = PCI_INTX_DISABLED;
    } else {
        route.mode = PCI_INTX_ENABLED;
    }

    return route;
}

static void gpex_host_realize(DeviceState *dev, Error **errp)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    GPEXHost *s = GPEX_HOST(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PCIExpressHost *pex = PCIE_HOST_BRIDGE(dev);
    int i;

    pcie_host_mmcfg_init(pex, PCIE_MMCFG_SIZE_MAX);
    sysbus_init_mmio(sbd, &pex->mmio);

    /*
     * Note that the MemoryRegions io_mmio and io_ioport that we pass
     * to pci_register_root_bus() are not the same as the
     * MemoryRegions io_mmio_window and io_ioport_window that we
     * expose as SysBus MRs. The difference is in the behaviour of
     * accesses to addresses where no PCI device has been mapped.
     *
     * io_mmio and io_ioport are the underlying PCI view of the PCI
     * address space, and when a PCI device does a bus master access
     * to a bad address this is reported back to it as a transaction
     * failure.
     *
     * io_mmio_window and io_ioport_window implement "unmapped
     * addresses read as -1 and ignore writes"; this is traditional
     * x86 PC behaviour, which is not mandated by the PCI spec proper
     * but expected by much PCI-using guest software, including Linux.
     *
     * In the interests of not being unnecessarily surprising, we
     * implement it in the gpex PCI host controller, by providing the
     * _window MRs, which are containers with io ops that implement
     * the 'background' behaviour and which hold the real PCI MRs as
     * subregions.
     */
    memory_region_init(&s->io_mmio, OBJECT(s), "gpex_mmio", UINT64_MAX);
    memory_region_init(&s->io_ioport, OBJECT(s), "gpex_ioport", 64 * 1024);

    if (s->allow_unmapped_accesses) {
        memory_region_init_io(&s->io_mmio_window, OBJECT(s),
                              &unassigned_io_ops, OBJECT(s),
                              "gpex_mmio_window", UINT64_MAX);
        memory_region_init_io(&s->io_ioport_window, OBJECT(s),
                              &unassigned_io_ops, OBJECT(s),
                              "gpex_ioport_window", 64 * 1024);

        memory_region_add_subregion(&s->io_mmio_window, 0, &s->io_mmio);
        memory_region_add_subregion(&s->io_ioport_window, 0, &s->io_ioport);
        sysbus_init_mmio(sbd, &s->io_mmio_window);
        sysbus_init_mmio(sbd, &s->io_ioport_window);
    } else {
        sysbus_init_mmio(sbd, &s->io_mmio);
        sysbus_init_mmio(sbd, &s->io_ioport);
    }

    for (i = 0; i < GPEX_NUM_IRQS; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
        s->irq_num[i] = -1;
    }

    pci->bus = pci_register_root_bus(dev, "pcie.0", gpex_set_irq,
                                     pci_swizzle_map_irq_fn, s, &s->io_mmio,
                                     &s->io_ioport, 0, 4, TYPE_PCIE_BUS);

    pci_bus_set_route_irq_fn(pci->bus, gpex_route_intx_pin_to_irq);
    qdev_realize(DEVICE(&s->gpex_root), BUS(pci->bus), &error_fatal);
}

static const char *gpex_host_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    return "0000:00";
}

static Property gpex_host_properties[] = {
    /*
     * Permit CPU accesses to unmapped areas of the PIO and MMIO windows
     * (discarding writes and returning -1 for reads) rather than aborting.
     */
    DEFINE_PROP_BOOL("allow-unmapped-accesses", GPEXHost,
                     allow_unmapped_accesses, true),
    DEFINE_PROP_UINT64(PCI_HOST_ECAM_BASE, GPEXHost, gpex_cfg.ecam.base, 0),
    DEFINE_PROP_SIZE(PCI_HOST_ECAM_SIZE, GPEXHost, gpex_cfg.ecam.size, 0),
    DEFINE_PROP_UINT64(PCI_HOST_PIO_BASE, GPEXHost, gpex_cfg.pio.base, 0),
    DEFINE_PROP_SIZE(PCI_HOST_PIO_SIZE, GPEXHost, gpex_cfg.pio.size, 0),
    DEFINE_PROP_UINT64(PCI_HOST_BELOW_4G_MMIO_BASE, GPEXHost,
                       gpex_cfg.mmio32.base, 0),
    DEFINE_PROP_SIZE(PCI_HOST_BELOW_4G_MMIO_SIZE, GPEXHost,
                     gpex_cfg.mmio32.size, 0),
    DEFINE_PROP_UINT64(PCI_HOST_ABOVE_4G_MMIO_BASE, GPEXHost,
                       gpex_cfg.mmio64.base, 0),
    DEFINE_PROP_SIZE(PCI_HOST_ABOVE_4G_MMIO_SIZE, GPEXHost,
                     gpex_cfg.mmio64.size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void gpex_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);

    hc->root_bus_path = gpex_host_root_bus_path;
    dc->realize = gpex_host_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    device_class_set_props(dc, gpex_host_properties);
}

static void gpex_host_initfn(Object *obj)
{
    GPEXHost *s = GPEX_HOST(obj);
    GPEXRootState *root = &s->gpex_root;

    object_initialize_child(obj, "gpex_root", root, TYPE_GPEX_ROOT_DEVICE);
    qdev_prop_set_int32(DEVICE(root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(root), "multifunction", false);
}

static const TypeInfo gpex_host_info = {
    .name       = TYPE_GPEX_HOST,
    .parent     = TYPE_PCIE_HOST_BRIDGE,
    .instance_size = sizeof(GPEXHost),
    .instance_init = gpex_host_initfn,
    .class_init = gpex_host_class_init,
};

/****************************************************************************
 * GPEX Root D0:F0
 */

static const VMStateDescription vmstate_gpex_root = {
    .name = "gpex_root",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, GPEXRootState),
        VMSTATE_END_OF_LIST()
    }
};

static void gpex_root_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->desc = "QEMU generic PCIe host bridge";
    dc->vmsd = &vmstate_gpex_root;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_PCIE_HOST;
    k->revision = 0;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo gpex_root_info = {
    .name = TYPE_GPEX_ROOT_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(GPEXRootState),
    .class_init = gpex_root_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void gpex_register(void)
{
    type_register_static(&gpex_root_info);
    type_register_static(&gpex_host_info);
}

type_init(gpex_register)
