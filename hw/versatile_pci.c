/*
 * ARM Versatile/PB PCI host controller
 *
 * Copyright (c) 2006-2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the LGPL.
 */

#include "sysbus.h"
#include "pci.h"
#include "pci_host.h"

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
    pci_data_write(opaque, vpb_pci_config_addr (addr), val, 2);
}

static void pci_vpb_config_writel (void *opaque, target_phys_addr_t addr,
                                   uint32_t val)
{
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
    return val;
}

static uint32_t pci_vpb_config_readl (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;
    val = pci_data_read(opaque, vpb_pci_config_addr (addr), 4);
    return val;
}

static CPUWriteMemoryFunc * const pci_vpb_config_write[] = {
    &pci_vpb_config_writeb,
    &pci_vpb_config_writew,
    &pci_vpb_config_writel,
};

static CPUReadMemoryFunc * const pci_vpb_config_read[] = {
    &pci_vpb_config_readb,
    &pci_vpb_config_readw,
    &pci_vpb_config_readl,
};

static int pci_vpb_map_irq(PCIDevice *d, int irq_num)
{
    return irq_num;
}

static void pci_vpb_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

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

static int pci_vpb_init(SysBusDevice *dev)
{
    PCIVPBState *s = FROM_SYSBUS(PCIVPBState, dev);
    PCIBus *bus;
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
    bus = pci_register_bus(&dev->qdev, "pci",
                           pci_vpb_set_irq, pci_vpb_map_irq, s->irq,
                           PCI_DEVFN(11, 0), 4);

    /* ??? Register memory space.  */

    s->mem_config = cpu_register_io_memory(pci_vpb_config_read,
                                           pci_vpb_config_write, bus,
                                           DEVICE_LITTLE_ENDIAN);
    sysbus_init_mmio_cb(dev, 0x04000000, pci_vpb_map);

    pci_create_simple(bus, -1, "versatile_pci_host");
    return 0;
}

static int pci_realview_init(SysBusDevice *dev)
{
    PCIVPBState *s = FROM_SYSBUS(PCIVPBState, dev);
    s->realview = 1;
    return pci_vpb_init(dev);
}

static int versatile_pci_host_init(PCIDevice *d)
{
    pci_set_word(d->config + PCI_STATUS,
		 PCI_STATUS_66MHZ | PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_byte(d->config + PCI_LATENCY_TIMER, 0x10);
    return 0;
}

static PCIDeviceInfo versatile_pci_host_info = {
    .qdev.name = "versatile_pci_host",
    .qdev.size = sizeof(PCIDevice),
    .init      = versatile_pci_host_init,
    .vendor_id = PCI_VENDOR_ID_XILINX,
    /* Both boards have the same device ID.  Oh well.  */
    .device_id = PCI_DEVICE_ID_XILINX_XC2VP30,
    .class_id  = PCI_CLASS_PROCESSOR_CO,
};

static void versatile_pci_register_devices(void)
{
    sysbus_register_dev("versatile_pci", sizeof(PCIVPBState), pci_vpb_init);
    sysbus_register_dev("realview_pci", sizeof(PCIVPBState),
                        pci_realview_init);
    pci_qdev_register(&versatile_pci_host_info);
}

device_init(versatile_pci_register_devices)
