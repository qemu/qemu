/*
 * QEMU Ultrasparc APB PCI host
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

/* XXX This file and most of its contests are somewhat misnamed.  The
   Ultrasparc PCI host is called the PCI Bus Module (PBM).  The APB is
   the secondary PCI bridge.  */

#include "hw.h"
#include "pci.h"
typedef target_phys_addr_t pci_addr_t;
#include "pci_host.h"

typedef PCIHostState APBState;

static void pci_apb_config_writel (void *opaque, target_phys_addr_t addr,
                                         uint32_t val)
{
    APBState *s = opaque;
    int i;

    for (i = 11; i < 32; i++) {
        if ((val & (1 << i)) != 0)
            break;
    }
    s->config_reg = (1 << 16) | (val & 0x7FC) | (i << 11);
}

static uint32_t pci_apb_config_readl (void *opaque,
                                            target_phys_addr_t addr)
{
    APBState *s = opaque;
    uint32_t val;
    int devfn;

    devfn = (s->config_reg >> 8) & 0xFF;
    val = (1 << (devfn >> 3)) | ((devfn & 0x07) << 8) | (s->config_reg & 0xFC);
    return val;
}

static CPUWriteMemoryFunc *pci_apb_config_write[] = {
    &pci_apb_config_writel,
    &pci_apb_config_writel,
    &pci_apb_config_writel,
};

static CPUReadMemoryFunc *pci_apb_config_read[] = {
    &pci_apb_config_readl,
    &pci_apb_config_readl,
    &pci_apb_config_readl,
};

static void apb_config_writel (void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    //PCIBus *s = opaque;

    switch (addr & 0x3f) {
    case 0x00: // Control/Status
    case 0x10: // AFSR
    case 0x18: // AFAR
    case 0x20: // Diagnostic
    case 0x28: // Target address space
        // XXX
    default:
        break;
    }
}

static uint32_t apb_config_readl (void *opaque,
                                  target_phys_addr_t addr)
{
    //PCIBus *s = opaque;
    uint32_t val;

    switch (addr & 0x3f) {
    case 0x00: // Control/Status
    case 0x10: // AFSR
    case 0x18: // AFAR
    case 0x20: // Diagnostic
    case 0x28: // Target address space
        // XXX
    default:
        val = 0;
        break;
    }
    return val;
}

static CPUWriteMemoryFunc *apb_config_write[] = {
    &apb_config_writel,
    &apb_config_writel,
    &apb_config_writel,
};

static CPUReadMemoryFunc *apb_config_read[] = {
    &apb_config_readl,
    &apb_config_readl,
    &apb_config_readl,
};

static CPUWriteMemoryFunc *pci_apb_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static CPUReadMemoryFunc *pci_apb_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

static void pci_apb_iowriteb (void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    cpu_outb(NULL, addr & 0xffff, val);
}

static void pci_apb_iowritew (void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    cpu_outw(NULL, addr & 0xffff, val);
}

static void pci_apb_iowritel (void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    cpu_outl(NULL, addr & 0xffff, val);
}

static uint32_t pci_apb_ioreadb (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inb(NULL, addr & 0xffff);
    return val;
}

static uint32_t pci_apb_ioreadw (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inw(NULL, addr & 0xffff);
    return val;
}

static uint32_t pci_apb_ioreadl (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inl(NULL, addr & 0xffff);
    return val;
}

static CPUWriteMemoryFunc *pci_apb_iowrite[] = {
    &pci_apb_iowriteb,
    &pci_apb_iowritew,
    &pci_apb_iowritel,
};

static CPUReadMemoryFunc *pci_apb_ioread[] = {
    &pci_apb_ioreadb,
    &pci_apb_ioreadw,
    &pci_apb_ioreadl,
};

/* The APB host has an IRQ line for each IRQ line of each slot.  */
static int pci_apb_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return ((pci_dev->devfn & 0x18) >> 1) + irq_num;
}

static int pci_pbm_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int bus_offset;
    if (pci_dev->devfn & 1)
        bus_offset = 16;
    else
        bus_offset = 0;
    return bus_offset + irq_num;
}

static void pci_apb_set_irq(qemu_irq *pic, int irq_num, int level)
{
    /* PCI IRQ map onto the first 32 INO.  */
    qemu_set_irq(pic[irq_num], level);
}

PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *pic)
{
    APBState *s;
    PCIDevice *d;
    int pci_mem_config, pci_mem_data, apb_config, pci_ioport;
    PCIBus *secondary;

    s = qemu_mallocz(sizeof(APBState));
    /* Ultrasparc PBM main bus */
    s->bus = pci_register_bus(pci_apb_set_irq, pci_pbm_map_irq, pic, 0, 32);

    pci_mem_config = cpu_register_io_memory(0, pci_apb_config_read,
                                            pci_apb_config_write, s);
    apb_config = cpu_register_io_memory(0, apb_config_read,
                                        apb_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_apb_read,
                                          pci_apb_write, s);
    pci_ioport = cpu_register_io_memory(0, pci_apb_ioread,
                                          pci_apb_iowrite, s);

    cpu_register_physical_memory(special_base + 0x2000ULL, 0x40, apb_config);
    cpu_register_physical_memory(special_base + 0x1000000ULL, 0x10,
                                 pci_mem_config);
    cpu_register_physical_memory(special_base + 0x2000000ULL, 0x10000,
                                 pci_ioport);
    cpu_register_physical_memory(mem_base, 0x10000000,
                                 pci_mem_data); // XXX size should be 4G-prom

    d = pci_register_device(s->bus, "Advanced PCI Bus", sizeof(PCIDevice),
                            0, NULL, NULL);
    d->config[0x00] = 0x8e; // vendor_id : Sun
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x00; // device_id
    d->config[0x03] = 0xa0;
    d->config[0x04] = 0x06; // command = bus master, pci mem
    d->config[0x05] = 0x00;
    d->config[0x06] = 0xa0; // status = fast back-to-back, 66MHz, no error
    d->config[0x07] = 0x03; // status = medium devsel
    d->config[0x08] = 0x00; // revision
    d->config[0x09] = 0x00; // programming i/f
    d->config[0x0A] = 0x00; // class_sub = pci host
    d->config[0x0B] = 0x06; // class_base = PCI_bridge
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x0E] = 0x00; // header_type

    /* APB secondary busses */
    secondary = pci_bridge_init(s->bus, 8, 0x108e5000, pci_apb_map_irq,
                                "Advanced PCI Bus secondary bridge 1");
    pci_bridge_init(s->bus, 9, 0x108e5000, pci_apb_map_irq,
                    "Advanced PCI Bus secondary bridge 2");
    return secondary;
}
