/*
 * QEMU PREP PCI host
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
#include "pci.h"
#include "pci_host.h"
#include "prep_pci.h"

typedef PCIHostState PREPPCIState;

typedef struct RavenPCIState {
    PCIDevice dev;
} RavenPCIState;

static inline uint32_t PPC_PCIIO_config(target_phys_addr_t addr)
{
    int i;

    for(i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0)
            break;
    }
    return (addr & 0x7ff) |  (i << 11);
}

static void PPC_PCIIO_writeb (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PREPPCIState *s = opaque;
    pci_data_write(s->bus, PPC_PCIIO_config(addr), val, 1);
}

static void PPC_PCIIO_writew (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PREPPCIState *s = opaque;
    val = bswap16(val);
    pci_data_write(s->bus, PPC_PCIIO_config(addr), val, 2);
}

static void PPC_PCIIO_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PREPPCIState *s = opaque;
    val = bswap32(val);
    pci_data_write(s->bus, PPC_PCIIO_config(addr), val, 4);
}

static uint32_t PPC_PCIIO_readb (void *opaque, target_phys_addr_t addr)
{
    PREPPCIState *s = opaque;
    uint32_t val;
    val = pci_data_read(s->bus, PPC_PCIIO_config(addr), 1);
    return val;
}

static uint32_t PPC_PCIIO_readw (void *opaque, target_phys_addr_t addr)
{
    PREPPCIState *s = opaque;
    uint32_t val;
    val = pci_data_read(s->bus, PPC_PCIIO_config(addr), 2);
    val = bswap16(val);
    return val;
}

static uint32_t PPC_PCIIO_readl (void *opaque, target_phys_addr_t addr)
{
    PREPPCIState *s = opaque;
    uint32_t val;
    val = pci_data_read(s->bus, PPC_PCIIO_config(addr), 4);
    val = bswap32(val);
    return val;
}

static const MemoryRegionOps PPC_PCIIO_ops = {
    .old_mmio = {
        .read = { PPC_PCIIO_readb, PPC_PCIIO_readw, PPC_PCIIO_readl, },
        .write = { PPC_PCIIO_writeb, PPC_PCIIO_writew, PPC_PCIIO_writel, },
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int prep_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 1;
}

static void prep_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[(irq_num & 1) ? 11 : 9] , level);
}

PCIBus *pci_prep_init(qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io)
{
    PREPPCIState *s;

    s = g_malloc0(sizeof(PREPPCIState));
    s->bus = pci_register_bus(NULL, "pci",
                              prep_set_irq, prep_map_irq, pic,
                              address_space_mem,
                              address_space_io,
                              0, 4);

    memory_region_init_io(&s->conf_mem, &pci_host_conf_be_ops, s,
                          "pci-conf-idx", 1);
    memory_region_add_subregion(address_space_io, 0xcf8, &s->conf_mem);
    sysbus_init_ioports(&s->busdev, 0xcf8, 1);

    memory_region_init_io(&s->data_mem, &pci_host_data_be_ops, s,
                          "pci-conf-data", 1);
    memory_region_add_subregion(address_space_io, 0xcfc, &s->data_mem);
    sysbus_init_ioports(&s->busdev, 0xcfc, 1);

    memory_region_init_io(&s->mmcfg, &PPC_PCIIO_ops, s, "pciio", 0x00400000);
    memory_region_add_subregion(address_space_mem, 0x80800000, &s->mmcfg);

    pci_create_simple(s->bus, 0, "raven");

    return s->bus;
}

static int raven_init(PCIDevice *d)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer

    return 0;
}

static const VMStateDescription vmstate_raven = {
    .name = "raven",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, RavenPCIState),
        VMSTATE_END_OF_LIST()
    },
};

static PCIDeviceInfo raven_info = {
    .qdev.name = "raven",
    .qdev.desc = "PReP Host Bridge - Motorola Raven",
    .qdev.size = sizeof(RavenPCIState),
    .qdev.vmsd = &vmstate_raven,
    .qdev.no_user = 1,
    .no_hotplug = 1,
    .init = raven_init,
    .vendor_id = PCI_VENDOR_ID_MOTOROLA,
    .device_id = PCI_DEVICE_ID_MOTOROLA_RAVEN,
    .revision = 0x00,
    .class_id = PCI_CLASS_BRIDGE_HOST,
    .qdev.props = (Property[]) {
        DEFINE_PROP_END_OF_LIST()
    },
};

static void raven_register_devices(void)
{
    pci_qdev_register(&raven_info);
}

device_init(raven_register_devices)
