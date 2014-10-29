/*
 * QEMU AMD PC-Net II (Am79C970A) PCI emulation
 *
 * Copyright (c) 2004 Antony T Curtis
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

/* This software was written to be compatible with the specification:
 * AMD Am79C970A PCnet-PCI II Ethernet Controller Data-Sheet
 * AMD Publication# 19436  Rev:E  Amendment/0  Issue Date: June 2000
 */

#include "hw/pci/pci.h"
#include "net/net.h"
#include "hw/loader.h"
#include "qemu/timer.h"
#include "sysemu/dma.h"
#include "sysemu/sysemu.h"

#include "pcnet.h"

//#define PCNET_DEBUG
//#define PCNET_DEBUG_IO
//#define PCNET_DEBUG_BCR
//#define PCNET_DEBUG_CSR
//#define PCNET_DEBUG_RMD
//#define PCNET_DEBUG_TMD
//#define PCNET_DEBUG_MATCH

#define TYPE_PCI_PCNET "pcnet"

#define PCI_PCNET(obj) \
     OBJECT_CHECK(PCIPCNetState, (obj), TYPE_PCI_PCNET)

typedef struct {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    PCNetState state;
    MemoryRegion io_bar;
} PCIPCNetState;

static void pcnet_aprom_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    PCNetState *s = opaque;
#ifdef PCNET_DEBUG
    printf("pcnet_aprom_writeb addr=0x%08x val=0x%02x\n", addr, val);
#endif
    if (BCR_APROMWE(s)) {
        s->prom[addr & 15] = val;
    }
}

static uint32_t pcnet_aprom_readb(void *opaque, uint32_t addr)
{
    PCNetState *s = opaque;
    uint32_t val = s->prom[addr & 15];
#ifdef PCNET_DEBUG
    printf("pcnet_aprom_readb addr=0x%08x val=0x%02x\n", addr, val);
#endif
    return val;
}

static uint64_t pcnet_ioport_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    PCNetState *d = opaque;

    if (addr < 0x10) {
        if (!BCR_DWIO(d) && size == 1) {
            return pcnet_aprom_readb(d, addr);
        } else if (!BCR_DWIO(d) && (addr & 1) == 0 && size == 2) {
            return pcnet_aprom_readb(d, addr) |
                   (pcnet_aprom_readb(d, addr + 1) << 8);
        } else if (BCR_DWIO(d) && (addr & 3) == 0 && size == 4) {
            return pcnet_aprom_readb(d, addr) |
                   (pcnet_aprom_readb(d, addr + 1) << 8) |
                   (pcnet_aprom_readb(d, addr + 2) << 16) |
                   (pcnet_aprom_readb(d, addr + 3) << 24);
        }
    } else {
        if (size == 2) {
            return pcnet_ioport_readw(d, addr);
        } else if (size == 4) {
            return pcnet_ioport_readl(d, addr);
        }
    }
    return ((uint64_t)1 << (size * 8)) - 1;
}

static void pcnet_ioport_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    PCNetState *d = opaque;

    if (addr < 0x10) {
        if (!BCR_DWIO(d) && size == 1) {
            pcnet_aprom_writeb(d, addr, data);
        } else if (!BCR_DWIO(d) && (addr & 1) == 0 && size == 2) {
            pcnet_aprom_writeb(d, addr, data & 0xff);
            pcnet_aprom_writeb(d, addr + 1, data >> 8);
        } else if (BCR_DWIO(d) && (addr & 3) == 0 && size == 4) {
            pcnet_aprom_writeb(d, addr, data & 0xff);
            pcnet_aprom_writeb(d, addr + 1, (data >> 8) & 0xff);
            pcnet_aprom_writeb(d, addr + 2, (data >> 16) & 0xff);
            pcnet_aprom_writeb(d, addr + 3, data >> 24);
        }
    } else {
        if (size == 2) {
            pcnet_ioport_writew(d, addr, data);
        } else if (size == 4) {
            pcnet_ioport_writel(d, addr, data);
        }
    }
}

static const MemoryRegionOps pcnet_io_ops = {
    .read = pcnet_ioport_read,
    .write = pcnet_ioport_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pcnet_mmio_writeb(void *opaque, hwaddr addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writeb addr=0x" TARGET_FMT_plx" val=0x%02x\n", addr,
           val);
#endif
    if (!(addr & 0x10))
        pcnet_aprom_writeb(d, addr & 0x0f, val);
}

static uint32_t pcnet_mmio_readb(void *opaque, hwaddr addr)
{
    PCNetState *d = opaque;
    uint32_t val = -1;
    if (!(addr & 0x10))
        val = pcnet_aprom_readb(d, addr & 0x0f);
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readb addr=0x" TARGET_FMT_plx " val=0x%02x\n", addr,
           val & 0xff);
#endif
    return val;
}

static void pcnet_mmio_writew(void *opaque, hwaddr addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writew addr=0x" TARGET_FMT_plx " val=0x%04x\n", addr,
           val);
#endif
    if (addr & 0x10)
        pcnet_ioport_writew(d, addr & 0x0f, val);
    else {
        addr &= 0x0f;
        pcnet_aprom_writeb(d, addr, val & 0xff);
        pcnet_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
    }
}

static uint32_t pcnet_mmio_readw(void *opaque, hwaddr addr)
{
    PCNetState *d = opaque;
    uint32_t val = -1;
    if (addr & 0x10)
        val = pcnet_ioport_readw(d, addr & 0x0f);
    else {
        addr &= 0x0f;
        val = pcnet_aprom_readb(d, addr+1);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr);
    }
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readw addr=0x" TARGET_FMT_plx" val = 0x%04x\n", addr,
           val & 0xffff);
#endif
    return val;
}

static void pcnet_mmio_writel(void *opaque, hwaddr addr, uint32_t val)
{
    PCNetState *d = opaque;
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_writel addr=0x" TARGET_FMT_plx" val=0x%08x\n", addr,
           val);
#endif
    if (addr & 0x10)
        pcnet_ioport_writel(d, addr & 0x0f, val);
    else {
        addr &= 0x0f;
        pcnet_aprom_writeb(d, addr, val & 0xff);
        pcnet_aprom_writeb(d, addr+1, (val & 0xff00) >> 8);
        pcnet_aprom_writeb(d, addr+2, (val & 0xff0000) >> 16);
        pcnet_aprom_writeb(d, addr+3, (val & 0xff000000) >> 24);
    }
}

static uint32_t pcnet_mmio_readl(void *opaque, hwaddr addr)
{
    PCNetState *d = opaque;
    uint32_t val;
    if (addr & 0x10)
        val = pcnet_ioport_readl(d, addr & 0x0f);
    else {
        addr &= 0x0f;
        val = pcnet_aprom_readb(d, addr+3);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr+2);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr+1);
        val <<= 8;
        val |= pcnet_aprom_readb(d, addr);
    }
#ifdef PCNET_DEBUG_IO
    printf("pcnet_mmio_readl addr=0x" TARGET_FMT_plx " val=0x%08x\n", addr,
           val);
#endif
    return val;
}

static const VMStateDescription vmstate_pci_pcnet = {
    .name = "pcnet",
    .version_id = 3,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIPCNetState),
        VMSTATE_STRUCT(state, PCIPCNetState, 0, vmstate_pcnet, PCNetState),
        VMSTATE_END_OF_LIST()
    }
};

/* PCI interface */

static const MemoryRegionOps pcnet_mmio_ops = {
    .old_mmio = {
        .read = { pcnet_mmio_readb, pcnet_mmio_readw, pcnet_mmio_readl },
        .write = { pcnet_mmio_writeb, pcnet_mmio_writew, pcnet_mmio_writel },
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void pci_physical_memory_write(void *dma_opaque, hwaddr addr,
                                      uint8_t *buf, int len, int do_bswap)
{
    pci_dma_write(dma_opaque, addr, buf, len);
}

static void pci_physical_memory_read(void *dma_opaque, hwaddr addr,
                                     uint8_t *buf, int len, int do_bswap)
{
    pci_dma_read(dma_opaque, addr, buf, len);
}

static void pci_pcnet_cleanup(NetClientState *nc)
{
    PCNetState *d = qemu_get_nic_opaque(nc);

    pcnet_common_cleanup(d);
}

static void pci_pcnet_uninit(PCIDevice *dev)
{
    PCIPCNetState *d = PCI_PCNET(dev);

    qemu_free_irq(d->state.irq);
    timer_del(d->state.poll_timer);
    timer_free(d->state.poll_timer);
    qemu_del_nic(d->state.nic);
}

static NetClientInfo net_pci_pcnet_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = pcnet_can_receive,
    .receive = pcnet_receive,
    .link_status_changed = pcnet_set_link_status,
    .cleanup = pci_pcnet_cleanup,
};

static int pci_pcnet_init(PCIDevice *pci_dev)
{
    PCIPCNetState *d = PCI_PCNET(pci_dev);
    PCNetState *s = &d->state;
    uint8_t *pci_conf;

#if 0
    printf("sizeof(RMD)=%d, sizeof(TMD)=%d\n",
        sizeof(struct pcnet_RMD), sizeof(struct pcnet_TMD));
#endif

    pci_conf = pci_dev->config;

    pci_set_word(pci_conf + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0);

    pci_conf[PCI_INTERRUPT_PIN] = 1; /* interrupt pin A */
    pci_conf[PCI_MIN_GNT] = 0x06;
    pci_conf[PCI_MAX_LAT] = 0xff;

    /* Handler for memory-mapped I/O */
    memory_region_init_io(&d->state.mmio, OBJECT(d), &pcnet_mmio_ops, s,
                          "pcnet-mmio", PCNET_PNPMMIO_SIZE);

    memory_region_init_io(&d->io_bar, OBJECT(d), &pcnet_io_ops, s, "pcnet-io",
                          PCNET_IOPORT_SIZE);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->io_bar);

    pci_register_bar(pci_dev, 1, 0, &s->mmio);

    s->irq = pci_allocate_irq(pci_dev);
    s->phys_mem_read = pci_physical_memory_read;
    s->phys_mem_write = pci_physical_memory_write;
    s->dma_opaque = pci_dev;

    return pcnet_common_init(DEVICE(pci_dev), s, &net_pci_pcnet_info);
}

static void pci_reset(DeviceState *dev)
{
    PCIPCNetState *d = PCI_PCNET(dev);

    pcnet_h_reset(&d->state);
}

static void pcnet_instance_init(Object *obj)
{
    PCIPCNetState *d = PCI_PCNET(obj);
    PCNetState *s = &d->state;

    device_add_bootindex_property(obj, &s->conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(obj), NULL);
}

static Property pcnet_properties[] = {
    DEFINE_NIC_PROPERTIES(PCIPCNetState, state.conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcnet_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = pci_pcnet_init;
    k->exit = pci_pcnet_uninit;
    k->romfile = "efi-pcnet.rom",
    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = PCI_DEVICE_ID_AMD_LANCE;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = pci_reset;
    dc->vmsd = &vmstate_pci_pcnet;
    dc->props = pcnet_properties;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pcnet_info = {
    .name          = TYPE_PCI_PCNET,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIPCNetState),
    .class_init    = pcnet_class_init,
    .instance_init = pcnet_instance_init,
};

static void pci_pcnet_register_types(void)
{
    type_register_static(&pcnet_info);
}

type_init(pci_pcnet_register_types)
