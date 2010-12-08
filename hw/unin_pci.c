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
    SysBusDevice busdev;
    PCIHostState host_state;
    ReadWriteHandler data_handler;
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

static void pci_unin_save(QEMUFile* f, void *opaque)
{
    PCIDevice *d = opaque;

    pci_device_save(d, f);
}

static int pci_unin_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIDevice *d = opaque;

    if (version_id != 1)
        return -EINVAL;

    return pci_device_load(d, f);
}

static void pci_unin_reset(void *opaque)
{
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

static void unin_data_write(ReadWriteHandler *handler,
                            pcibus_t addr, uint32_t val, int len)
{
    UNINState *s = container_of(handler, UNINState, data_handler);
    UNIN_DPRINTF("write addr %" FMT_PCIBUS " len %d val %x\n", addr, len, val);
    pci_data_write(s->host_state.bus,
                   unin_get_config_reg(s->host_state.config_reg, addr),
                   val, len);
}

static uint32_t unin_data_read(ReadWriteHandler *handler,
                               pcibus_t addr, int len)
{
    UNINState *s = container_of(handler, UNINState, data_handler);
    uint32_t val;

    val = pci_data_read(s->host_state.bus,
                        unin_get_config_reg(s->host_state.config_reg, addr),
                        len);
    UNIN_DPRINTF("read addr %" FMT_PCIBUS " len %d val %x\n", addr, len, val);
    return val;
}

static int pci_unin_main_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = pci_host_conf_register_mmio(&s->host_state,
                                                 DEVICE_LITTLE_ENDIAN);
    s->data_handler.read = unin_data_read;
    s->data_handler.write = unin_data_write;
    pci_mem_data = cpu_register_io_memory_simple(&s->data_handler,
                                                 DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);

    register_savevm(&dev->qdev, "uninorth", 0, 1,
                    pci_unin_save, pci_unin_load, &s->host_state);
    qemu_register_reset(pci_unin_reset, &s->host_state);
    return 0;
}

static int pci_u3_agp_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth U3 AGP bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = pci_host_conf_register_mmio(&s->host_state,
                                                 DEVICE_LITTLE_ENDIAN);
    s->data_handler.read = unin_data_read;
    s->data_handler.write = unin_data_write;
    pci_mem_data = cpu_register_io_memory_simple(&s->data_handler,
                                                 DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);

    register_savevm(&dev->qdev, "uninorth", 0, 1,
                    pci_unin_save, pci_unin_load, &s->host_state);
    qemu_register_reset(pci_unin_reset, &s->host_state);

    return 0;
}

static int pci_unin_agp_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth AGP bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = pci_host_conf_register_mmio(&s->host_state,
                                                 DEVICE_LITTLE_ENDIAN);
    pci_mem_data = pci_host_data_register_mmio(&s->host_state,
                                               DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);
    return 0;
}

static int pci_unin_internal_init_device(SysBusDevice *dev)
{
    UNINState *s;
    int pci_mem_config, pci_mem_data;

    /* Uninorth internal bus */
    s = FROM_SYSBUS(UNINState, dev);

    pci_mem_config = pci_host_conf_register_mmio(&s->host_state,
                                                 DEVICE_LITTLE_ENDIAN);
    pci_mem_data = pci_host_data_register_mmio(&s->host_state,
                                               DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, pci_mem_config);
    sysbus_init_mmio(dev, 0x1000, pci_mem_data);
    return 0;
}

PCIBus *pci_pmac_init(qemu_irq *pic)
{
    DeviceState *dev;
    SysBusDevice *s;
    UNINState *d;

    /* Use values found on a real PowerMac */
    /* Uninorth main bus */
    dev = qdev_create(NULL, "uni-north");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(UNINState, s);
    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_unin_set_irq, pci_unin_map_irq,
                                         pic, PCI_DEVFN(11, 0), 4);

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
    dev = qdev_create(NULL, "uni-north-agp");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    /* Uninorth internal bus */
#if 0
    /* XXX: not needed for now */
    pci_create_simple(d->host_state.bus, PCI_DEVFN(14, 0), "uni-north-pci");
    dev = qdev_create(NULL, "uni-north-pci");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0xf4800000);
    sysbus_mmio_map(s, 1, 0xf4c00000);
#endif

    return d->host_state.bus;
}

PCIBus *pci_pmac_u3_init(qemu_irq *pic)
{
    DeviceState *dev;
    SysBusDevice *s;
    UNINState *d;

    /* Uninorth AGP bus */

    dev = qdev_create(NULL, "u3-agp");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(UNINState, s);

    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_unin_set_irq, pci_unin_map_irq,
                                         pic, PCI_DEVFN(11, 0), 4);

    sysbus_mmio_map(s, 0, 0xf0800000);
    sysbus_mmio_map(s, 1, 0xf0c00000);

    pci_create_simple(d->host_state.bus, 11 << 3, "u3-agp");

    return d->host_state.bus;
}

static int unin_main_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static int unin_agp_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_AGP);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    //    d->config[0x34] = 0x80; // capabilities_pointer
    return 0;
}

static int u3_agp_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_U3_AGP);
    /* revision */
    d->config[0x08] = 0x00;
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    /* cache line size */
    d->config[0x0C] = 0x08;
    /* latency timer */
    d->config[0x0D] = 0x10;
    return 0;
}

static int unin_internal_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_APPLE);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_APPLE_UNI_N_I_PCI);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer
    return 0;
}

static PCIDeviceInfo unin_main_pci_host_info = {
    .qdev.name = "uni-north",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_main_pci_host_init,
};

static PCIDeviceInfo u3_agp_pci_host_info = {
    .qdev.name = "u3-agp",
    .qdev.size = sizeof(PCIDevice),
    .init      = u3_agp_pci_host_init,
};

static PCIDeviceInfo unin_agp_pci_host_info = {
    .qdev.name = "uni-north-agp",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_agp_pci_host_init,
};

static PCIDeviceInfo unin_internal_pci_host_info = {
    .qdev.name = "uni-north-pci",
    .qdev.size = sizeof(PCIDevice),
    .init      = unin_internal_pci_host_init,
};

static void unin_register_devices(void)
{
    sysbus_register_dev("uni-north", sizeof(UNINState),
                        pci_unin_main_init_device);
    pci_qdev_register(&unin_main_pci_host_info);
    sysbus_register_dev("u3-agp", sizeof(UNINState),
                        pci_u3_agp_init_device);
    pci_qdev_register(&u3_agp_pci_host_info);
    sysbus_register_dev("uni-north-agp", sizeof(UNINState),
                        pci_unin_agp_init_device);
    pci_qdev_register(&unin_agp_pci_host_info);
    sysbus_register_dev("uni-north-pci", sizeof(UNINState),
                        pci_unin_internal_init_device);
    pci_qdev_register(&unin_internal_pci_host_info);
}

device_init(unin_register_devices)
