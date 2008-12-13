/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

#include "hw.h"
#include "pci.h"
#include "primecell.h"

static inline uint32_t vpb_pci_config_addr(target_phys_addr_t addr)
{
    return addr & 0xffffff;
}

static void pci_vpb_config_writeb (void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
    pci_data_write(opaque, vpb_pci_config_addr (addr), val, 1);
}

static void pci_vpb_config_writew (void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    pci_data_write(opaque, vpb_pci_config_addr (addr), val, 2);
}

static void pci_vpb_config_writel (void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    pci_data_write(opaque, vpb_pci_config_addr (addr), val, 4);
}

static uint32_t pci_vpb_config_readb (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    val = pci_data_read(opaque, vpb_pci_config_addr (addr), 1);
    return val;
}

static uint32_t pci_vpb_config_readw (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    val = pci_data_read(opaque, vpb_pci_config_addr (addr), 2);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap16(val);
#endif
    return val;
}

static uint32_t pci_vpb_config_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    val = pci_data_read(opaque, vpb_pci_config_addr (addr), 4);
#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif
    return val;
}

static CPUWriteMemoryFunc *pci_vpb_config_write[] = {
    &pci_vpb_config_writeb,
    &pci_vpb_config_writew,
    &pci_vpb_config_writel,
};

static CPUReadMemoryFunc *pci_vpb_config_read[] = {
    &pci_vpb_config_readb,
    &pci_vpb_config_readw,
    &pci_vpb_config_readl,
};

static int pci_vpb_irq;

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    return irq_num;
}

static void pci_vpb_set_irq(qemu_irq *pic, int irq_num, int level)
{
    qemu_set_irq(pic[pci_vpb_irq + irq_num], level);
}

PCIBus *pci_vpb_init(qemu_irq *pic, int irq, int realview)
{
    PCIBus *s;
    PCIDevice *d;
    int mem_config;
    uint32_t base;
    const char * name;

    pci_vpb_irq = irq;
    if (realview) {
        base = 0x60000000;
        name = "RealView EB PCI Controller";
    } else {
        base = 0x40000000;
        name = "Versatile/PB PCI Controller";
    }
    s = pci_register_bus(pci_vpb_set_irq, pci_vpb_map_irq, pic, 11 << 3, 4);
    /* ??? Register memory space.  */

    mem_config = cpu_register_io_memory(0, pci_vpb_config_read,
                                        pci_vpb_config_write, s);
    /* Selfconfig area.  */
    cpu_register_physical_memory(base + 0x01000000, 0x1000000, mem_config);
    /* Normal config area.  */
    cpu_register_physical_memory(base + 0x02000000, 0x1000000, mem_config);

    d = pci_register_device(s, name, sizeof(PCIDevice), -1, NULL, NULL);

    if (realview) {
        /* IO memory area.  */
        isa_mmio_init(base + 0x03000000, 0x00100000);
    }

    d->config[0x00] = 0xee; // vendor_id
    d->config[0x01] = 0x10;
    /* Both boards have the same device ID.  Oh well.  */
    d->config[0x02] = 0x00; // device_id
    d->config[0x03] = 0x03;
    d->config[0x04] = 0x00;
    d->config[0x05] = 0x00;
    d->config[0x06] = 0x20;
    d->config[0x07] = 0x02;
    d->config[0x08] = 0x00; // revision
    d->config[0x09] = 0x00; // programming i/f
    d->config[0x0A] = 0x40; // class_sub = pci host
    d->config[0x0B] = 0x0b; // class_base = PCI_bridge
    d->config[0x0D] = 0x10; // latency_timer

    return s;
}
