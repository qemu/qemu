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

/* XXX This file and most of its contents are somewhat misnamed.  The
   Ultrasparc PCI host is called the PCI Bus Module (PBM).  The APB is
   the secondary PCI bridge.  */

#include "sysbus.h"
#include "pci.h"
#include "pci_host.h"
#include "apb_pci.h"

/* debug APB */
//#define DEBUG_APB

#ifdef DEBUG_APB
#define APB_DPRINTF(fmt, ...) \
do { printf("APB: " fmt , ## __VA_ARGS__); } while (0)
#else
#define APB_DPRINTF(fmt, ...)
#endif

/*
 * Chipset docs:
 * PBM: "UltraSPARC IIi User's Manual",
 * http://www.sun.com/processors/manuals/805-0087.pdf
 *
 * APB: "Advanced PCI Bridge (APB) User's Manual",
 * http://www.sun.com/processors/manuals/805-1251.pdf
 */

typedef struct APBState {
    SysBusDevice busdev;
    PCIHostState host_state;
} APBState;

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

static CPUWriteMemoryFunc * const apb_config_write[] = {
    &apb_config_writel,
    &apb_config_writel,
    &apb_config_writel,
};

static CPUReadMemoryFunc * const apb_config_read[] = {
    &apb_config_readl,
    &apb_config_readl,
    &apb_config_readl,
};

static void apb_pci_config_write(APBState *s, target_phys_addr_t addr,
                                 uint32_t val, int size)
{
    APB_DPRINTF("%s: addr " TARGET_FMT_lx " val %x\n", __func__, addr, val);
    pci_data_write(s->host_state.bus, (addr & 0x00ffffff) | (1u << 31), val,
                   size);
}

static uint32_t apb_pci_config_read(APBState *s, target_phys_addr_t addr,
                                    int size)
{
    uint32_t ret;

    ret = pci_data_read(s->host_state.bus, (addr & 0x00ffffff) | (1u << 31),
                        size);
    APB_DPRINTF("%s: addr " TARGET_FMT_lx " -> %x\n", __func__, addr, ret);
    return ret;
}

static void apb_pci_config_writel(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    APBState *s = opaque;

    apb_pci_config_write(s, addr, bswap32(val), 4);
}

static void apb_pci_config_writew(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    APBState *s = opaque;

    apb_pci_config_write(s, addr, bswap16(val), 2);
}

static void apb_pci_config_writeb(void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    APBState *s = opaque;

    apb_pci_config_write(s, addr, val, 1);
}

static uint32_t apb_pci_config_readl(void *opaque, target_phys_addr_t addr)
{
    APBState *s = opaque;

    return bswap32(apb_pci_config_read(s, addr, 4));
}

static uint32_t apb_pci_config_readw(void *opaque, target_phys_addr_t addr)
{
    APBState *s = opaque;

    return bswap16(apb_pci_config_read(s, addr, 2));
}

static uint32_t apb_pci_config_readb(void *opaque, target_phys_addr_t addr)
{
    APBState *s = opaque;

    return apb_pci_config_read(s, addr, 1);
}

static CPUWriteMemoryFunc * const apb_pci_config_writes[] = {
    &apb_pci_config_writeb,
    &apb_pci_config_writew,
    &apb_pci_config_writel,
};

static CPUReadMemoryFunc * const apb_pci_config_reads[] = {
    &apb_pci_config_readb,
    &apb_pci_config_readw,
    &apb_pci_config_readl,
};

static void pci_apb_iowriteb (void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    cpu_outb(addr & IOPORTS_MASK, val);
}

static void pci_apb_iowritew (void *opaque, target_phys_addr_t addr,
                                  uint32_t val)
{
    cpu_outw(addr & IOPORTS_MASK, bswap16(val));
}

static void pci_apb_iowritel (void *opaque, target_phys_addr_t addr,
                                uint32_t val)
{
    cpu_outl(addr & IOPORTS_MASK, bswap32(val));
}

static uint32_t pci_apb_ioreadb (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = cpu_inb(addr & IOPORTS_MASK);
    return val;
}

static uint32_t pci_apb_ioreadw (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = bswap16(cpu_inw(addr & IOPORTS_MASK));
    return val;
}

static uint32_t pci_apb_ioreadl (void *opaque, target_phys_addr_t addr)
{
    uint32_t val;

    val = bswap32(cpu_inl(addr & IOPORTS_MASK));
    return val;
}

static CPUWriteMemoryFunc * const pci_apb_iowrite[] = {
    &pci_apb_iowriteb,
    &pci_apb_iowritew,
    &pci_apb_iowritel,
};

static CPUReadMemoryFunc * const pci_apb_ioread[] = {
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

static void pci_apb_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    /* PCI IRQ map onto the first 32 INO.  */
    qemu_set_irq(pic[irq_num], level);
}

static void apb_pci_bridge_init(PCIBus *b)
{
    PCIDevice *dev = pci_bridge_get_device(b);

    /*
     * command register:
     * According to PCI bridge spec, after reset
     *   bus master bit is off
     *   memory space enable bit is off
     * According to manual (805-1251.pdf).
     *   the reset value should be zero unless the boot pin is tied high
     *   (which is true) and thus it should be PCI_COMMAND_MEMORY.
     */
    pci_set_word(dev->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    dev->config[PCI_LATENCY_TIMER] = 0x10;
    dev->config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
}

PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *pic, PCIBus **bus2, PCIBus **bus3)
{
    DeviceState *dev;
    SysBusDevice *s;
    APBState *d;

    /* Ultrasparc PBM main bus */
    dev = qdev_create(NULL, "pbm");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    /* apb_config */
    sysbus_mmio_map(s, 0, special_base);
    /* pci_ioport */
    sysbus_mmio_map(s, 1, special_base + 0x2000000ULL);
    /* pci_config */
    sysbus_mmio_map(s, 2, special_base + 0x1000000ULL);
    /* mem_data */
    sysbus_mmio_map(s, 3, mem_base);
    d = FROM_SYSBUS(APBState, s);
    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_apb_set_irq, pci_pbm_map_irq, pic,
                                         0, 32);
    pci_bus_set_mem_base(d->host_state.bus, mem_base);

    pci_create_simple(d->host_state.bus, 0, "pbm");
    /* APB secondary busses */
    *bus2 = pci_bridge_init(d->host_state.bus, PCI_DEVFN(1, 0),
                            PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_SIMBA,
                            pci_apb_map_irq,
                            "Advanced PCI Bus secondary bridge 1");
    apb_pci_bridge_init(*bus2);

    *bus3 = pci_bridge_init(d->host_state.bus, PCI_DEVFN(1, 1),
                            PCI_VENDOR_ID_SUN, PCI_DEVICE_ID_SUN_SIMBA,
                            pci_apb_map_irq,
                            "Advanced PCI Bus secondary bridge 2");
    apb_pci_bridge_init(*bus3);

    return d->host_state.bus;
}

static int pci_pbm_init_device(SysBusDevice *dev)
{

    APBState *s;
    int pci_mem_data, apb_config, pci_ioport, pci_config;

    s = FROM_SYSBUS(APBState, dev);
    /* apb_config */
    apb_config = cpu_register_io_memory(apb_config_read,
                                        apb_config_write, s);
    sysbus_init_mmio(dev, 0x10000ULL, apb_config);
    /* pci_ioport */
    pci_ioport = cpu_register_io_memory(pci_apb_ioread,
                                          pci_apb_iowrite, s);
    sysbus_init_mmio(dev, 0x10000ULL, pci_ioport);
    /* pci_config */
    pci_config = cpu_register_io_memory(apb_pci_config_reads,
                                        apb_pci_config_writes, s);
    sysbus_init_mmio(dev, 0x1000000ULL, pci_config);
    /* mem_data */
    pci_mem_data = pci_host_data_register_mmio(&s->host_state);
    sysbus_init_mmio(dev, 0x10000000ULL, pci_mem_data);
    return 0;
}

static int pbm_pci_host_init(PCIDevice *d)
{
    pci_config_set_vendor_id(d->config, PCI_VENDOR_ID_SUN);
    pci_config_set_device_id(d->config, PCI_DEVICE_ID_SUN_SABRE);
    d->config[0x04] = 0x06; // command = bus master, pci mem
    d->config[0x05] = 0x00;
    d->config[0x06] = 0xa0; // status = fast back-to-back, 66MHz, no error
    d->config[0x07] = 0x03; // status = medium devsel
    d->config[0x08] = 0x00; // revision
    d->config[0x09] = 0x00; // programming i/f
    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_HOST);
    d->config[0x0D] = 0x10; // latency_timer
    d->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type
    return 0;
}

static PCIDeviceInfo pbm_pci_host_info = {
    .qdev.name = "pbm",
    .qdev.size = sizeof(PCIDevice),
    .init      = pbm_pci_host_init,
    .header_type  = PCI_HEADER_TYPE_BRIDGE,
};

static void pbm_register_devices(void)
{
    sysbus_register_dev("pbm", sizeof(APBState), pci_pbm_init_device);
    pci_qdev_register(&pbm_pci_host_info);
}

device_init(pbm_register_devices)
