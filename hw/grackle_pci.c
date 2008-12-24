/*
 * QEMU Grackle PCI host (heathrow OldWorld PowerMac)
 *
 * Copyright (c) 2006-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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

/* debug Grackle */
//#define DEBUG_GRACKLE

#ifdef DEBUG_GRACKLE
#define GRACKLE_DPRINTF(fmt, args...) \
do { printf("GRACKLE: " fmt , ##args); } while (0)
#else
#define GRACKLE_DPRINTF(fmt, args...)
#endif

typedef target_phys_addr_t pci_addr_t;
#include "pci_host.h"

typedef PCIHostState GrackleState;

static void pci_grackle_config_writel (void *opaque, target_phys_addr_t addr,
                                       uint32_t val)
{
    GrackleState *s = opaque;

    GRACKLE_DPRINTF("config_writel addr " TARGET_FMT_plx " val %x\n", addr,
                    val);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    s->config_reg = val;
}

static uint32_t pci_grackle_config_readl (void *opaque, target_phys_addr_t addr)
{
    GrackleState *s = opaque;
    uint32_t val;

    val = s->config_reg;
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    GRACKLE_DPRINTF("config_readl addr " TARGET_FMT_plx " val %x\n", addr,
                    val);
    return val;
}

static CPUWriteMemoryFunc *pci_grackle_config_write[] = {
    &pci_grackle_config_writel,
    &pci_grackle_config_writel,
    &pci_grackle_config_writel,
};

static CPUReadMemoryFunc *pci_grackle_config_read[] = {
    &pci_grackle_config_readl,
    &pci_grackle_config_readl,
    &pci_grackle_config_readl,
};

static CPUWriteMemoryFunc *pci_grackle_write[] = {
    &pci_host_data_writeb,
    &pci_host_data_writew,
    &pci_host_data_writel,
};

static CPUReadMemoryFunc *pci_grackle_read[] = {
    &pci_host_data_readb,
    &pci_host_data_readw,
    &pci_host_data_readl,
};

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_grackle_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_grackle_set_irq(qemu_irq *pic, int irq_num, int level)
{
    GRACKLE_DPRINTF("set_irq num %d level %d\n", irq_num, level);
    qemu_set_irq(pic[irq_num + 0x15], level);
}

PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic)
{
    GrackleState *s;
    PCIDevice *d;
    int pci_mem_config, pci_mem_data;

    s = qemu_mallocz(sizeof(GrackleState));
    s->bus = pci_register_bus(pci_grackle_set_irq, pci_grackle_map_irq,
                              pic, 0, 4);

    pci_mem_config = cpu_register_io_memory(0, pci_grackle_config_read,
                                            pci_grackle_config_write, s);
    pci_mem_data = cpu_register_io_memory(0, pci_grackle_read,
                                          pci_grackle_write, s);
    cpu_register_physical_memory(base, 0x1000, pci_mem_config);
    cpu_register_physical_memory(base + 0x00200000, 0x1000, pci_mem_data);
    d = pci_register_device(s->bus, "Grackle host bridge", sizeof(PCIDevice),
                            0, NULL, NULL);
    d->config[0x00] = 0x57; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x02; // device_id
    d->config[0x03] = 0x00;
    d->config[0x08] = 0x00; // revision
    d->config[0x09] = 0x01;
    d->config[0x0a] = 0x00; // class_sub = host
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x00; // header_type

#if 0
    /* PCI2PCI bridge same values as PearPC - check this */
    d->config[0x00] = 0x11; // vendor_id
    d->config[0x01] = 0x10;
    d->config[0x02] = 0x26; // device_id
    d->config[0x03] = 0x00;
    d->config[0x08] = 0x02; // revision
    d->config[0x0a] = 0x04; // class_sub = pci2pci
    d->config[0x0b] = 0x06; // class_base = PCI_bridge
    d->config[0x0e] = 0x01; // header_type

    d->config[0x18] = 0x0;  // primary_bus
    d->config[0x19] = 0x1;  // secondary_bus
    d->config[0x1a] = 0x1;  // subordinate_bus
    d->config[0x1c] = 0x10; // io_base
    d->config[0x1d] = 0x20; // io_limit

    d->config[0x20] = 0x80; // memory_base
    d->config[0x21] = 0x80;
    d->config[0x22] = 0x90; // memory_limit
    d->config[0x23] = 0x80;

    d->config[0x24] = 0x00; // prefetchable_memory_base
    d->config[0x25] = 0x84;
    d->config[0x26] = 0x00; // prefetchable_memory_limit
    d->config[0x27] = 0x85;
#endif
    return s->bus;
}
