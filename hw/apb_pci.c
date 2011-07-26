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
#include "pci_bridge.h"
#include "pci_internals.h"
#include "rwhandler.h"
#include "apb_pci.h"
#include "sysemu.h"
#include "exec-memory.h"

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

#define PBM_PCI_IMR_MASK    0x7fffffff
#define PBM_PCI_IMR_ENABLED 0x80000000

#define POR          (1 << 31)
#define SOFT_POR     (1 << 30)
#define SOFT_XIR     (1 << 29)
#define BTN_POR      (1 << 28)
#define BTN_XIR      (1 << 27)
#define RESET_MASK   0xf8000000
#define RESET_WCMASK 0x98000000
#define RESET_WMASK  0x60000000

typedef struct APBState {
    SysBusDevice busdev;
    PCIBus      *bus;
    ReadWriteHandler pci_config_handler;
    uint32_t iommu[4];
    uint32_t pci_control[16];
    uint32_t pci_irq_map[8];
    uint32_t obio_irq_map[32];
    qemu_irq pci_irqs[32];
    uint32_t reset_control;
    unsigned int nr_resets;
} APBState;

static void apb_config_writel (void *opaque, target_phys_addr_t addr,
                               uint32_t val)
{
    APBState *s = opaque;

    APB_DPRINTF("%s: addr " TARGET_FMT_lx " val %x\n", __func__, addr, val);

    switch (addr & 0xffff) {
    case 0x30 ... 0x4f: /* DMA error registers */
        /* XXX: not implemented yet */
        break;
    case 0x200 ... 0x20b: /* IOMMU */
        s->iommu[(addr & 0xf) >> 2] = val;
        break;
    case 0x20c ... 0x3ff: /* IOMMU flush */
        break;
    case 0xc00 ... 0xc3f: /* PCI interrupt control */
        if (addr & 4) {
            s->pci_irq_map[(addr & 0x3f) >> 3] &= PBM_PCI_IMR_MASK;
            s->pci_irq_map[(addr & 0x3f) >> 3] |= val & ~PBM_PCI_IMR_MASK;
        }
        break;
    case 0x2000 ... 0x202f: /* PCI control */
        s->pci_control[(addr & 0x3f) >> 2] = val;
        break;
    case 0xf020 ... 0xf027: /* Reset control */
        if (addr & 4) {
            val &= RESET_MASK;
            s->reset_control &= ~(val & RESET_WCMASK);
            s->reset_control |= val & RESET_WMASK;
            if (val & SOFT_POR) {
                s->nr_resets = 0;
                qemu_system_reset_request();
            } else if (val & SOFT_XIR) {
                qemu_system_reset_request();
            }
        }
        break;
    case 0x5000 ... 0x51cf: /* PIO/DMA diagnostics */
    case 0xa400 ... 0xa67f: /* IOMMU diagnostics */
    case 0xa800 ... 0xa80f: /* Interrupt diagnostics */
    case 0xf000 ... 0xf01f: /* FFB config, memory control */
        /* we don't care */
    default:
        break;
    }
}

static uint32_t apb_config_readl (void *opaque,
                                  target_phys_addr_t addr)
{
    APBState *s = opaque;
    uint32_t val;

    switch (addr & 0xffff) {
    case 0x30 ... 0x4f: /* DMA error registers */
        val = 0;
        /* XXX: not implemented yet */
        break;
    case 0x200 ... 0x20b: /* IOMMU */
        val = s->iommu[(addr & 0xf) >> 2];
        break;
    case 0x20c ... 0x3ff: /* IOMMU flush */
        val = 0;
        break;
    case 0xc00 ... 0xc3f: /* PCI interrupt control */
        if (addr & 4) {
            val = s->pci_irq_map[(addr & 0x3f) >> 3];
        } else {
            val = 0;
        }
        break;
    case 0x2000 ... 0x202f: /* PCI control */
        val = s->pci_control[(addr & 0x3f) >> 2];
        break;
    case 0xf020 ... 0xf027: /* Reset control */
        if (addr & 4) {
            val = s->reset_control;
        } else {
            val = 0;
        }
        break;
    case 0x5000 ... 0x51cf: /* PIO/DMA diagnostics */
    case 0xa400 ... 0xa67f: /* IOMMU diagnostics */
    case 0xa800 ... 0xa80f: /* Interrupt diagnostics */
    case 0xf000 ... 0xf01f: /* FFB config, memory control */
        /* we don't care */
    default:
        val = 0;
        break;
    }
    APB_DPRINTF("%s: addr " TARGET_FMT_lx " -> %x\n", __func__, addr, val);

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

static void apb_pci_config_write(ReadWriteHandler *h, pcibus_t addr,
                                 uint32_t val, int size)
{
    APBState *s = container_of(h, APBState, pci_config_handler);

    val = qemu_bswap_len(val, size);
    APB_DPRINTF("%s: addr " TARGET_FMT_lx " val %x\n", __func__, addr, val);
    pci_data_write(s->bus, addr, val, size);
}

static uint32_t apb_pci_config_read(ReadWriteHandler *h, pcibus_t addr,
                                    int size)
{
    uint32_t ret;
    APBState *s = container_of(h, APBState, pci_config_handler);

    ret = pci_data_read(s->bus, addr, size);
    ret = qemu_bswap_len(ret, size);
    APB_DPRINTF("%s: addr " TARGET_FMT_lx " -> %x\n", __func__, addr, ret);
    return ret;
}

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
    APBState *s = opaque;

    /* PCI IRQ map onto the first 32 INO.  */
    if (irq_num < 32) {
        if (s->pci_irq_map[irq_num >> 2] & PBM_PCI_IMR_ENABLED) {
            APB_DPRINTF("%s: set irq %d level %d\n", __func__, irq_num, level);
            qemu_set_irq(s->pci_irqs[irq_num], level);
        } else {
            APB_DPRINTF("%s: not enabled: lower irq %d\n", __func__, irq_num);
            qemu_irq_lower(s->pci_irqs[irq_num]);
        }
    }
}

static int apb_pci_bridge_initfn(PCIDevice *dev)
{
    int rc;

    rc = pci_bridge_initfn(dev);
    if (rc < 0) {
        return rc;
    }

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
                 PCI_COMMAND_MEMORY);
    pci_set_word(dev->config + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_66MHZ |
                 PCI_STATUS_DEVSEL_MEDIUM);
    return 0;
}

PCIBus *pci_apb_init(target_phys_addr_t special_base,
                     target_phys_addr_t mem_base,
                     qemu_irq *pic, PCIBus **bus2, PCIBus **bus3)
{
    DeviceState *dev;
    SysBusDevice *s;
    APBState *d;
    unsigned int i;
    PCIDevice *pci_dev;
    PCIBridge *br;

    /* Ultrasparc PBM main bus */
    dev = qdev_create(NULL, "pbm");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    /* apb_config */
    sysbus_mmio_map(s, 0, special_base);
    /* PCI configuration space */
    sysbus_mmio_map(s, 1, special_base + 0x1000000ULL);
    /* pci_ioport */
    sysbus_mmio_map(s, 2, special_base + 0x2000000ULL);
    d = FROM_SYSBUS(APBState, s);

    d->bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_apb_set_irq, pci_pbm_map_irq, d,
                                         get_system_memory(),
                                         0, 32);
    pci_bus_set_mem_base(d->bus, mem_base);

    for (i = 0; i < 32; i++) {
        sysbus_connect_irq(s, i, pic[i]);
    }

    pci_create_simple(d->bus, 0, "pbm");

    /* APB secondary busses */
    pci_dev = pci_create_multifunction(d->bus, PCI_DEVFN(1, 0), true,
                                   "pbm-bridge");
    br = DO_UPCAST(PCIBridge, dev, pci_dev);
    pci_bridge_map_irq(br, "Advanced PCI Bus secondary bridge 1",
                       pci_apb_map_irq);
    qdev_init_nofail(&pci_dev->qdev);
    *bus2 = pci_bridge_get_sec_bus(br);

    pci_dev = pci_create_multifunction(d->bus, PCI_DEVFN(1, 1), true,
                                   "pbm-bridge");
    br = DO_UPCAST(PCIBridge, dev, pci_dev);
    pci_bridge_map_irq(br, "Advanced PCI Bus secondary bridge 2",
                       pci_apb_map_irq);
    qdev_init_nofail(&pci_dev->qdev);
    *bus3 = pci_bridge_get_sec_bus(br);

    return d->bus;
}

static void pci_pbm_reset(DeviceState *d)
{
    unsigned int i;
    APBState *s = container_of(d, APBState, busdev.qdev);

    for (i = 0; i < 8; i++) {
        s->pci_irq_map[i] &= PBM_PCI_IMR_MASK;
    }

    if (s->nr_resets++ == 0) {
        /* Power on reset */
        s->reset_control = POR;
    }
}

static int pci_pbm_init_device(SysBusDevice *dev)
{
    APBState *s;
    int pci_config, apb_config, pci_ioport;
    unsigned int i;

    s = FROM_SYSBUS(APBState, dev);
    for (i = 0; i < 8; i++) {
        s->pci_irq_map[i] = (0x1f << 6) | (i << 2);
    }
    for (i = 0; i < 32; i++) {
        sysbus_init_irq(dev, &s->pci_irqs[i]);
    }

    /* apb_config */
    apb_config = cpu_register_io_memory(apb_config_read,
                                        apb_config_write, s,
                                        DEVICE_NATIVE_ENDIAN);
    /* at region 0 */
    sysbus_init_mmio(dev, 0x10000ULL, apb_config);

    /* PCI configuration space */
    s->pci_config_handler.read = apb_pci_config_read;
    s->pci_config_handler.write = apb_pci_config_write;
    pci_config = cpu_register_io_memory_simple(&s->pci_config_handler,
                                               DEVICE_NATIVE_ENDIAN);
    assert(pci_config >= 0);
    /* at region 1 */
    sysbus_init_mmio(dev, 0x1000000ULL, pci_config);

    /* pci_ioport */
    pci_ioport = cpu_register_io_memory(pci_apb_ioread,
                                        pci_apb_iowrite, s,
                                        DEVICE_NATIVE_ENDIAN);
    /* at region 2 */
    sysbus_init_mmio(dev, 0x10000ULL, pci_ioport);

    return 0;
}

static int pbm_pci_host_init(PCIDevice *d)
{
    pci_set_word(d->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_66MHZ |
                 PCI_STATUS_DEVSEL_MEDIUM);
    return 0;
}

static PCIDeviceInfo pbm_pci_host_info = {
    .qdev.name = "pbm",
    .qdev.size = sizeof(PCIDevice),
    .init      = pbm_pci_host_init,
    .vendor_id = PCI_VENDOR_ID_SUN,
    .device_id = PCI_DEVICE_ID_SUN_SABRE,
    .class_id  = PCI_CLASS_BRIDGE_HOST,
    .is_bridge = 1,
};

static SysBusDeviceInfo pbm_host_info = {
    .qdev.name = "pbm",
    .qdev.size = sizeof(APBState),
    .qdev.reset = pci_pbm_reset,
    .init = pci_pbm_init_device,
};

static PCIDeviceInfo pbm_pci_bridge_info = {
    .qdev.name = "pbm-bridge",
    .qdev.size = sizeof(PCIBridge),
    .qdev.vmsd = &vmstate_pci_device,
    .qdev.reset = pci_bridge_reset,
    .init = apb_pci_bridge_initfn,
    .exit = pci_bridge_exitfn,
    .vendor_id = PCI_VENDOR_ID_SUN,
    .device_id = PCI_DEVICE_ID_SUN_SIMBA,
    .revision = 0x11,
    .config_write = pci_bridge_write_config,
    .is_bridge = 1,
};

static void pbm_register_devices(void)
{
    sysbus_register_withprop(&pbm_host_info);
    pci_qdev_register(&pbm_pci_host_info);
    pci_qdev_register(&pbm_pci_bridge_info);
}

device_init(pbm_register_devices)
