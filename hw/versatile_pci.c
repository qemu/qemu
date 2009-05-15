/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the LGPL.
 */

#include "sysbus.h"
#include "pci.h"

typedef struct {
    SysBusDevice busdev;
    qemu_irq irq[4];
    int realview;
    int mem_config;
} PCIVPBState;

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

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    return irq_num;
}

static void pci_vpb_set_irq(qemu_irq *pic, int irq_num, int level)
{
    qemu_set_irq(pic[irq_num], level);
}

static void pci_vpb_map(SysBusDevice *dev, target_phys_addr_t base)
{
    PCIVPBState *s = (PCIVPBState *)dev;
    /* Selfconfig area.  */
    cpu_register_physical_memory(base + 0x01000000, 0x1000000, s->mem_config);
    /* Normal config area.  */
    cpu_register_physical_memory(base + 0x02000000, 0x1000000, s->mem_config);

    if (s->realview) {
        /* IO memory area.  */
        isa_mmio_init(base + 0x03000000, 0x00100000);
    }
}

static void pci_vpb_init(SysBusDevice *dev)
{
    PCIVPBState *s = FROM_SYSBUS(PCIVPBState, dev);
    PCIBus *bus;
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
    bus = pci_register_bus(pci_vpb_set_irq, pci_vpb_map_irq, s->irq,
                           11 << 3, 4);
    qdev_attach_child_bus(&dev->qdev, "pci", bus);

    /* ??? Register memory space.  */

    s->mem_config = cpu_register_io_memory(0, pci_vpb_config_read,
                                           pci_vpb_config_write, bus);
    sysbus_init_mmio_cb(dev, 0x04000000, pci_vpb_map);

    pci_create_simple(bus, -1, "versatile_pci_host");
}

static void pci_realview_init(SysBusDevice *dev)
{
    PCIVPBState *s = FROM_SYSBUS(PCIVPBState, dev);
    s->realview = 1;
    pci_vpb_init(dev);
}

static void versatile_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_XILINX);
    /* Both boards have the same device ID.  Oh well.  */
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_XILINX_XC2VP30);
    d->config[0x04] = 0x00;
    d->config[0x05] = 0x00;
    d->config[0x06] = 0x20;
    d->config[0x07] = 0x02;
    d->config[0x08] = 0x00; // revision
    d->config[0x09] = 0x00; // programming i/f
    pci_config_set_class(d->config, PCI_CLASS_PROCESSOR_CO);
    d->config[0x0D] = 0x10; // latency_timer
}

static void versatile_pci_register_devices(void)
{
    sysbus_register_dev("versatile_pci", sizeof(PCIVPBState), pci_vpb_init);
    sysbus_register_dev("realview_pci", sizeof(PCIVPBState),
                        pci_realview_init);
    pci_qdev_register("versatile_pci_host", sizeof(PCIDevice),
                      versatile_pci_host_init);
}

device_init(versatile_pci_register_devices)
