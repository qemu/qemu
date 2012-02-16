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
#include "hw.h"
#include "ppc_mac.h"
#include "pci.h"
#include "pci_host.h"

/* debug UniNorth */
//#define DEBUG_UNIN

#ifdef DEBUG_UNIN
#define UNIN_DPRINTF(fmt, ...)                                  \
    do { printf("UNIN: " fmt , ## __VA_ARGS__); } while (0)
#else
#define UNIN_DPRINTF(fmt, ...)
#endif

static const int unin_irq_line[] = { 0x1b, 0x1c, 0x1d, 0x1e };

typedef struct UNINState {
    PCIHostState host_state;
    MemoryRegion pci_mmio;
    MemoryRegion pci_hole;
} UNINState;

static int pci_unin_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int retval;
    int devfn = pci_dev->devfn & 0x00FFFFFF;

    retval = (((devfn >> 11) & 0x1F) + irq_num) & 3;

    return retval;
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
        slot = ffs(reg & 0xfffff800) - 1;
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

static void unin_data_write(void *opaque, target_phys_addr_t addr,
                            uint64_t val, unsigned len)
{
    UNINState *s = opaque;
    UNIN_DPRINTF("write addr %" TARGET_FMT_plx " len %d val %"PRIx64"\n",
                 addr, len, val);
    pci_data_write(s->host_state.bus,
                   unin_get_config_reg(s->host_state.config_reg, addr),
                   val, len);
}

static uint64_t unin_data_read(void *opaque, target_phys_addr_t addr,
                               unsigned len)
{
    UNINState *s = opaque;
    uint32_t val;

    val = pci_data_read(s->host_state.bus,
                        unin_get_config_reg(s->host_state.config_reg, addr),
                        len);
    UNIN_DPRINTF("read addr %" TARGET_FMT_plx " len %d val %x\n",
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
    UNINState *s;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    h = FROM_SYSBUS(PCIHostState, dev);
    s = DO_UPCAST(UNINState, host_state, h);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &unin_data_ops, s,
                          "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &s->host_state.conf_mem);
    sysbus_init_mmio(dev, &s->host_state.data_mem);

    return 0;
}


static int pci_u3_agp_init_device(SysBusDevice *dev)
{
    PCIHostState *h;
    UNINState *s;

    /* Uninorth U3 AGP bus */
    h = FROM_SYSBUS(PCIHostState, dev);
    s = DO_UPCAST(UNINState, host_state, h);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &unin_data_ops, s,
                          "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &s->host_state.conf_mem);
    sysbus_init_mmio(dev, &s->host_state.data_mem);

    return 0;
}

static int pci_unin_agp_init_device(SysBusDevice *dev)
{
    PCIHostState *h;
    UNINState *s;

    /* Uninorth AGP bus */
    h = FROM_SYSBUS(PCIHostState, dev);
    s = DO_UPCAST(UNINState, host_state, h);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &pci_host_data_le_ops,
                          &s->host_state, "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &s->host_state.conf_mem);
    sysbus_init_mmio(dev, &s->host_state.data_mem);
    return 0;
}

static int pci_unin_internal_init_device(SysBusDevice *dev)
{
    PCIHostState *h;
    UNINState *s;

    /* Uninorth internal bus */
    h = FROM_SYSBUS(PCIHostState, dev);
    s = DO_UPCAST(UNINState, host_state, h);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &pci_host_data_le_ops,
                          &s->host_state, "pci-conf-data", 0x1000);
    sysbus_init_mmio(dev, &s->host_state.conf_mem);
    sysbus_init_mmio(dev, &s->host_state.data_mem);
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
    dev = qdev_create(NULL, "uni-north-pci-pcihost");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    h = FROM_SYSBUS(PCIHostState, s);
    d = DO_UPCAST(UNINState, host_state, h);
    memory_region_init(&d->pci_mmio, "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x70000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    d->host_state.bus = pci_register_bus(dev, "pci",
                                         pci_unin_set_irq, pci_unin_map_irq,
                                         pic,
                                         &d->pci_mmio,
                                         address_space_io,
                                         PCI_DEVFN(11, 0), 4);

#if 0
    pci_create_simple(d->host_state.bus, PCI_DEVFN(11, 0), "uni-north");
#endif

    sysbus_mmio_map(s, 0, 0xf2800000);
    sysbus_mmio_map(s, 1, 0xf2c00000);

    /* DEC 21154 bridge */
#if 0
    /* XXX: not activated as PPC BIOS doesn't handle multiple buses properly */
    pci_create_simple(d->host_state.bus, PCI_DEVFN(12, 0), "dec-21154");
#endif

    /* Uninorth AGP bus */
    pci_create_simple(d->host_state.bus, PCI_DEVFN(11, 0), "uni-north-agp");
    dev = qdev_create(NULL, "uni-north-agp-pcihost");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    /* Uninorth internal bus */
#if 0
    /* XXX: not needed for now */
    pci_create_simple(d->host_state.bus, PCI_DEVFN(14, 0),
                      "uni-north-internal-pci");
    dev = qdev_create(NULL, "uni-north-internal-pci-pcihost");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0xf4800000);
    sysbus_mmio_map(s, 1, 0xf4c00000);
#endif

    return d->host_state.bus;
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

    dev = qdev_create(NULL, "u3-agp-pcihost");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    h = FROM_SYSBUS(PCIHostState, s);
    d = DO_UPCAST(UNINState, host_state, h);

    memory_region_init(&d->pci_mmio, "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x70000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    d->host_state.bus = pci_register_bus(dev, "pci",
                                         pci_unin_set_irq, pci_unin_map_irq,
                                         pic,
                                         &d->pci_mmio,
                                         address_space_io,
                                         PCI_DEVFN(11, 0), 4);

    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    pci_create_simple(d->host_state.bus, 11 << 3, "u3-agp");

    return d->host_state.bus;
}

static int unin_main_pci_host_init(PCIDevice *d)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static int unin_agp_pci_host_init(PCIDevice *d)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    //    d->config[0x34] = 0x80; // capabilities_pointer
    return 0;
}

static int u3_agp_pci_host_init(PCIDevice *d)
{
    /* cache line size */
    d->config[0x0C] = 0x08;
    /* latency timer */
    d->config[0x0D] = 0x10;
    return 0;
}

static int unin_internal_pci_host_init(PCIDevice *d)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static void unin_main_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init      = unin_main_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_PCI;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
}

static TypeInfo unin_main_pci_host_info = {
    .name = "uni-north-pci",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_main_pci_host_class_init,
};

static void u3_agp_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init      = u3_agp_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_U3_AGP;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
}

static TypeInfo u3_agp_pci_host_info = {
    .name = "u3-agp",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = u3_agp_pci_host_class_init,
};

static void unin_agp_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init      = unin_agp_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_AGP;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
}

static TypeInfo unin_agp_pci_host_info = {
    .name = "uni-north-agp",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_agp_pci_host_class_init,
};

static void unin_internal_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init      = unin_internal_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_APPLE;
    k->device_id = PCI_DEVICE_ID_APPLE_UNI_N_I_PCI;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
}

static TypeInfo unin_internal_pci_host_info = {
    .name = "uni-north-internal-pci",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = unin_internal_pci_host_class_init,
};

static void pci_unin_main_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = pci_unin_main_init_device;
}

static TypeInfo pci_unin_main_info = {
    .name          = "uni-north-pci-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_unin_main_class_init,
};

static void pci_u3_agp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = pci_u3_agp_init_device;
}

static TypeInfo pci_u3_agp_info = {
    .name          = "u3-agp-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_u3_agp_class_init,
};

static void pci_unin_agp_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = pci_unin_agp_init_device;
}

static TypeInfo pci_unin_agp_info = {
    .name          = "uni-north-agp-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(UNINState),
    .class_init    = pci_unin_agp_class_init,
};

static void pci_unin_internal_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    sbc->init = pci_unin_internal_init_device;
}

static TypeInfo pci_unin_internal_info = {
    .name          = "uni-north-internal-pci-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
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
