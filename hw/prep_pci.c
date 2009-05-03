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

typedef uint32_t pci_addr_t;
#include "pci_host.h"

typedef PCIHostState PREPPCIState;

static void pci_prep_addr_writel(void* opaque, uint32_t addr, uint32_t val)
{
    PREPPCIState *s = opaque;
    s->config_reg = val;
}

static uint32_t pci_prep_addr_readl(void* opaque, uint32_t addr)
{
    PREPPCIState *s = opaque;
    return s->config_reg;
}

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
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    pci_data_write(s->bus, PPC_PCIIO_config(addr), val, 2);
}

static void PPC_PCIIO_writel (void *opaque, target_phys_addr_t addr, uint32_t val)
{
    PREPPCIState *s = opaque;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
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
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t PPC_PCIIO_readl (void *opaque, target_phys_addr_t addr)
{
    PREPPCIState *s = opaque;
    uint32_t val;
    val = pci_data_read(s->bus, PPC_PCIIO_config(addr), 4);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static CPUWriteMemoryFunc *PPC_PCIIO_write[] = {
    &PPC_PCIIO_writeb,
    &PPC_PCIIO_writew,
    &PPC_PCIIO_writel,
};

static CPUReadMemoryFunc *PPC_PCIIO_read[] = {
    &PPC_PCIIO_readb,
    &PPC_PCIIO_readw,
    &PPC_PCIIO_readl,
};

static int prep_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 1;
}

static void prep_set_irq(qemu_irq *pic, int irq_num, int level)
{
    qemu_set_irq(pic[(irq_num & 1) ? 11 : 9] , level);
}

PCIBus *pci_prep_init(qemu_irq *pic)
{
    PREPPCIState *s;
    PCIDevice *d;
    int PPC_io_memory;

    s = qemu_mallocz(sizeof(PREPPCIState));
    s->bus = pci_register_bus(prep_set_irq, prep_map_irq, pic, 0, 4);

    register_ioport_write(0xcf8, 4, 4, pci_prep_addr_writel, s);
    register_ioport_read(0xcf8, 4, 4, pci_prep_addr_readl, s);

    register_ioport_write(0xcfc, 4, 1, pci_host_data_writeb, s);
    register_ioport_write(0xcfc, 4, 2, pci_host_data_writew, s);
    register_ioport_write(0xcfc, 4, 4, pci_host_data_writel, s);
    register_ioport_read(0xcfc, 4, 1, pci_host_data_readb, s);
    register_ioport_read(0xcfc, 4, 2, pci_host_data_readw, s);
    register_ioport_read(0xcfc, 4, 4, pci_host_data_readl, s);

    PPC_io_memory = cpu_register_io_memory(0, PPC_PCIIO_read,
                                           PPC_PCIIO_write, s);
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
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    d->config[0x34] = 0x00; // capabilities_pointer

    return s->bus;
}
