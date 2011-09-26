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

static CPUWriteMemoryFunc * const PPC_PCIIO_write[] = {
    &PPC_PCIIO_writeb,
    &PPC_PCIIO_writew,
    &PPC_PCIIO_writel,
};

static CPUReadMemoryFunc * const PPC_PCIIO_read[] = {
    &PPC_PCIIO_readb,
    &PPC_PCIIO_readw,
    &PPC_PCIIO_readl,
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
    PCIDevice *d;
    int PPC_io_memory;

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

    PPC_io_memory = cpu_register_io_memory(PPC_PCIIO_read,
                                           PPC_PCIIO_write, s,
                                           DEVICE_NATIVE_ENDIAN);
    cpu_register_physical_memory(0x80800000, 0x00400000, PPC_io_memory);

    /* PCI host bridge */
    d = pci_register_device(s->bus, "PREP Host Bridge - Motorola Raven",
                            sizeof(PCIDevice), 0, NULL, NULL);
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_MOTOROLA);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_MOTOROLA_RAVEN);
    d->config[0x08] = 0x00; // revision
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer

    return s->bus;
}
