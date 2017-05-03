/*
 * QEMU Uninorth PCI host (for all Mac99 and newer machines)
 *
 * Copyright (c) 2006 Fabrice Bellard
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
#include "hw/hw.h"
#include "hw/ppc/mac.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"

/* debug UniNorth */
//#define DEBUG_UNIN

#ifdef DEBUG_UNIN
#define UNIN_DPRINTF(fmt, ...)                                  \
    do { printf("UNIN: " fmt , ## __VA_ARGS__); } while (0)
#else
#define UNIN_DPRINTF(fmt, ...)
#endif

static const int unin_irq_line[] = { 0x1b, 0x1c, 0x1d, 0x1e };

#define TYPE_UNI_NORTH_PCI_HOST_BRIDGE "uni-north-pci-pcihost"
#define TYPE_UNI_NORTH_AGP_HOST_BRIDGE "uni-north-agp-pcihost"
#define TYPE_UNI_NORTH_INTERNAL_PCI_HOST_BRIDGE "uni-north-internal-pci-pcihost"
#define TYPE_U3_AGP_HOST_BRIDGE "u3-agp-pcihost"

#define UNI_NORTH_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(UNINState, (obj), TYPE_UNI_NORTH_PCI_HOST_BRIDGE)
#define UNI_NORTH_AGP_HOST_BRIDGE(obj) \
    OBJECT_CHECK(UNINState, (obj), TYPE_UNI_NORTH_AGP_HOST_BRIDGE)
#define UNI_NORTH_INTERNAL_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(UNINState, (obj), TYPE_UNI_NORTH_INTERNAL_PCI_HOST_BRIDGE)
#define U3_AGP_HOST_BRIDGE(obj) \
    OBJECT_CHECK(UNINState, (obj), TYPE_U3_AGP_HOST_BRIDGE)

typedef struct UNINState {
    PCIHostState parent_obj;

    MemoryRegion pci_mmio;
    MemoryRegion pci_hole;
} UNINState;

static int pci_unin_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_unin_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    UNIN_DPRINTF("%s: setting INT %d = %d\n", __func__,
                 unin_irq_line[irq_num], level);
    qemu_set_irq(pic[unin_irq_line[irq_num]], level);
}

static uint32_t unin_get_config_reg(uint32_t reg, uint32_t addr)
{
    uint32_t retval;

    if (reg & (1u << 31)) {
        /* XXX OpenBIOS compatibility hack */
        retval = reg | (addr & 3);
    } else if (reg & 1) {
        /* CFA1 style */
        retval = (reg & ~7u) | (addr & 7);
    } else {
        uint32_t slot, func;

        /* Grab CFA0 style values */
        slot = ctz32(reg & 0xfffff800);
        if (slot == 32) {
            slot = -1; /* XXX: should this be 0? */
        }
        func = (reg >> 8) & 7;

        /* ... and then convert them to x86 format */
        /* config pointer */
        retval = (reg & (0xff - 7)) | (addr & 7);
        /* slot */
        retval |= slot << 11;
        /* fn */
        retval |= func << 8;
    }


    UNIN_DPRINTF("Converted config space accessor %08x/%08x -> %08x\n",
                 reg, addr, retval);

    return retval;
}

static void unin_data_write(void *opaque, hwaddr addr,
                            uint64_t val, unsigned len)
{
    UNINState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    UNIN_DPRINTF("write addr " TARGET_FMT_plx " len %d val %"PRIx64"\n",
                 addr, len, val);
    pci_data_write(phb->bus,
                   unin_get_config_reg(phb->config_reg, addr),
                   val, len);
}

static uint64_t unin_data_read(void *opaque, hwaddr addr,
                               unsigned len)
{
    UNINState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    uint32_t val;

    val = pci_data_read(phb->bus,
                        unin_get_config_reg(phb->config_reg, addr),
                        len);
    UNIN_DPRINTF("read addr " TARGET_FMT_plx " len %d val %x\n",
                 addr, len, val);
    return val;
}

static const MemoryRegionOps unin_data_ops = {
    .read = unin_data_read,
    .write = unin_data_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int pci_unin_main_init_device(SysBusDevice *dev)
{
    PCIHostState *h;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    h = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&h->data_mem, OBJECT(h), &unin_data_ops, dev,
                          "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &h->conf_mem);
    sysbus_init_mmio(dev, &h->data_mem);

    return 0;
}


static int pci_u3_agp_init_device(SysBusDevice *dev)
{
    PCIHostState *h;

    /* Uninorth U3 AGP bus */
    h = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&h->data_mem, OBJECT(h), &unin_data_ops, dev,
                          "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &h->conf_mem);
    sysbus_init_mmio(dev, &h->data_mem);

    return 0;
}

static int pci_unin_agp_init_device(SysBusDevice *dev)
{
    PCIHostState *h;

    /* Uninorth AGP bus */
    h = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&h->data_mem, OBJECT(h), &pci_host_data_le_ops,
                          dev, "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &h->conf_mem);
    sysbus_init_mmio(dev, &h->data_mem);
    return 0;
}

static int pci_unin_internal_init_device(SysBusDevice *dev)
{
    PCIHostState *h;

    /* Uninorth internal bus */
    h = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&h->data_mem, OBJECT(h), &pci_host_data_le_ops,
                          dev, "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &h->conf_mem);
    sysbus_init_mmio(dev, &h->data_mem);
    return 0;
}

PCIBus *pci_pmac_init(qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io)
{
    DeviceState *dev;
    SysBusDevice *s;
    PCIHostState *h;
    UNINState *d;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    dev = qdev_create(NULL, TYPE_UNI_NORTH_PCI_HOST_BRIDGE);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    h = PCI_HOST_BRIDGE(s);
    d = UNI_NORTH_PCI_HOST_BRIDGE(dev);
    memory_region_init(&d->pci_mmio, OBJECT(d), "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, OBJECT(d), "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x10000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    h->bus = pci_register_bus(dev, NULL,
                              pci_unin_set_irq, pci_unin_map_irq,
                              pic,
                              &d->pci_mmio,
                              address_space_io,
                              PCI_DEVFN(11, 0), 4, TYPE_PCI_BUS);

#if 0
    pci_create_simple(h->bus, PCI_DEVFN(11, 0), "uni-north");
#endif

    sysbus_mmio_map(s, 0, 0xf2800000);
    sysbus_mmio_map(s, 1, 0xf2c00000);

    /* DEC 21154 bridge */
#if 0
    /* XXX: not activated as PPC BIOS doesn't handle multiple buses properly */
    pci_create_simple(h->bus, PCI_DEVFN(12, 0), "dec-21154");
#endif

    /* Uninorth AGP bus */
    pci_create_simple(h->bus, PCI_DEVFN(11, 0), "uni-north-agp");
    dev = qdev_create(NULL, TYPE_UNI_NORTH_AGP_HOST_BRIDGE);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    /* Uninorth internal bus */
#if 0
    /* XXX: not needed for now */
    pci_create_simple(h->bus, PCI_DEVFN(14, 0),
                      "uni-north-internal-pci");
    dev = qdev_create(NULL, TYPE_UNI_NORTH_INTERNAL_PCI_HOST_BRIDGE);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, 0xf4800000);
    sysbus_mmio_map(s, 1, 0xf4c00000);
#endif

    return h->bus;
}

PCIBus *pci_pmac_u3_init(qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io)
{
    DeviceState *dev;
    SysBusDevice *s;
    PCIHostState *h;
    UNINState *d;

    /* Uninorth AGP bus */

    dev = qdev_create(NULL, TYPE_U3_AGP_HOST_BRIDGE);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    h = PCI_HOST_BRIDGE(dev);
    d = U3_AGP_HOST_BRIDGE(dev);

    memory_region_init(&d->pci_mmio, OBJECT(d), "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, OBJECT(d), "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x70000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    h->bus = pci_register_bus(dev, NULL,
                              pci_unin_set_irq, pci_unin_map_irq,
                              pic,
                              &d->pci_mmio,
                              address_space_io,
                              PCI_DEVFN(11, 0), 4, TYPE_PCI_BUS);

    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    pci_create_simple(h->bus, 11 << 3, "u3-agp");

    return h->bus;
}

static void unin_main_pci_host_realize(PCIDevice *d, Error **errp)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
}

static void unin_agp_pci_host_realize(PCIDevice *d, Error **errp)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    //    d->config[0x34] = 0x80; // capabilities_pointer
    /*
     * Set kMacRISCPCIAddressSelect (0x48) register to indicate PCI
     * memory space with base 0x80000000, size 0x10000000 for Apple's
     * AppleMacRiscPCI driver
     */
    d->config[0x48] = 0x0;
    d->config[0x49] = 0x0;
    d->config[0x4a] = 0x0;
    d->config[0x4b] = 0x1;
}

static void u3_agp_pci_host_realize(PCIDevice *d, Error **errp)
{
    /* cache line size */
    d->config[0x0C] = 0x08;
    /* latency timer */
    d->config[0x0D] = 0x10;
}

static void unin_internal_pci_host_realize(PCIDevice *d, Error **errp)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
}

static void unin_main_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = unin_main_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_PCI;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo unin_main_pci_host_info = {
    .name = "uni-north-pci",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_main_pci_host_class_init,
};

static void u3_agp_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = u3_agp_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_U3_AGP;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo u3_agp_pci_host_info = {
    .name = "u3-agp",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = u3_agp_pci_host_class_init,
};

static void unin_agp_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = unin_agp_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_AGP;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo unin_agp_pci_host_info = {
    .name = "uni-north-agp",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_agp_pci_host_class_init,
};

static void unin_internal_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = unin_internal_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_I_PCI;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo unin_internal_pci_host_info = {
    .name = "uni-north-internal-pci",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_internal_pci_host_class_init,
};

static void pci_unin_main_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbc->init = pci_unin_main_init_device;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pci_unin_main_info = {
    .name          = TYPE_UNI_NORTH_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_unin_main_class_init,
};

static void pci_u3_agp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbc->init = pci_u3_agp_init_device;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pci_u3_agp_info = {
    .name          = TYPE_U3_AGP_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_u3_agp_class_init,
};

static void pci_unin_agp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbc->init = pci_unin_agp_init_device;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pci_unin_agp_info = {
    .name          = TYPE_UNI_NORTH_AGP_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_unin_agp_class_init,
};

static void pci_unin_internal_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbc->init = pci_unin_internal_init_device;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo pci_unin_internal_info = {
    .name          = TYPE_UNI_NORTH_INTERNAL_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_unin_internal_class_init,
};

static void unin_register_types(void)
{
    type_register_static(&unin_main_pci_host_info);
    type_register_static(&u3_agp_pci_host_info);
    type_register_static(&unin_agp_pci_host_info);
    type_register_static(&unin_internal_pci_host_info);

    type_register_static(&pci_unin_main_info);
    type_register_static(&pci_u3_agp_info);
    type_register_static(&pci_unin_agp_info);
    type_register_static(&pci_unin_internal_info);
}

type_init(unin_register_types)
