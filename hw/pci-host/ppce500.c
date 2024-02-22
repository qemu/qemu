/*
 * QEMU PowerPC E500 embedded processors pci controller emulation
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This file is derived from ppc4xx_pci.c,
 * the copyright for that material belongs to the original owners.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/ppc/e500-ccsr.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "hw/pci-host/ppce500.h"
#include "qom/object.h"

#ifdef DEBUG_PCI
#define pci_debug(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define pci_debug(fmt, ...)
#endif

#define PCIE500_CFGADDR       0x0
#define PCIE500_CFGDATA       0x4
#define PCIE500_REG_BASE      0xC00
#define PCIE500_ALL_SIZE      0x1000
#define PCIE500_REG_SIZE      (PCIE500_ALL_SIZE - PCIE500_REG_BASE)

#define PCIE500_PCI_IOLEN     0x10000ULL

#define PPCE500_PCI_CONFIG_ADDR         0x0
#define PPCE500_PCI_CONFIG_DATA         0x4
#define PPCE500_PCI_INTACK              0x8

#define PPCE500_PCI_OW1                 (0xC20 - PCIE500_REG_BASE)
#define PPCE500_PCI_OW2                 (0xC40 - PCIE500_REG_BASE)
#define PPCE500_PCI_OW3                 (0xC60 - PCIE500_REG_BASE)
#define PPCE500_PCI_OW4                 (0xC80 - PCIE500_REG_BASE)
#define PPCE500_PCI_IW3                 (0xDA0 - PCIE500_REG_BASE)
#define PPCE500_PCI_IW2                 (0xDC0 - PCIE500_REG_BASE)
#define PPCE500_PCI_IW1                 (0xDE0 - PCIE500_REG_BASE)

#define PPCE500_PCI_GASKET_TIMR         (0xE20 - PCIE500_REG_BASE)

#define PCI_POTAR               0x0
#define PCI_POTEAR              0x4
#define PCI_POWBAR              0x8
#define PCI_POWAR               0x10

#define PCI_PITAR               0x0
#define PCI_PIWBAR              0x8
#define PCI_PIWBEAR             0xC
#define PCI_PIWAR               0x10

#define PPCE500_PCI_NR_POBS     5
#define PPCE500_PCI_NR_PIBS     3

#define PIWAR_EN                0x80000000      /* Enable */
#define PIWAR_PF                0x20000000      /* prefetch */
#define PIWAR_TGI_LOCAL         0x00f00000      /* target - local memory */
#define PIWAR_READ_SNOOP        0x00050000
#define PIWAR_WRITE_SNOOP       0x00005000
#define PIWAR_SZ_MASK           0x0000003f

struct  pci_outbound {
    uint32_t potar;
    uint32_t potear;
    uint32_t powbar;
    uint32_t powar;
    MemoryRegion mem;
};

struct pci_inbound {
    uint32_t pitar;
    uint32_t piwbar;
    uint32_t piwbear;
    uint32_t piwar;
    MemoryRegion mem;
};

#define TYPE_PPC_E500_PCI_HOST_BRIDGE "e500-pcihost"

OBJECT_DECLARE_SIMPLE_TYPE(PPCE500PCIState, PPC_E500_PCI_HOST_BRIDGE)

struct PPCE500PCIState {
    PCIHostState parent_obj;

    struct pci_outbound pob[PPCE500_PCI_NR_POBS];
    struct pci_inbound pib[PPCE500_PCI_NR_PIBS];
    uint32_t gasket_time;
    qemu_irq irq[PCI_NUM_PINS];
    uint32_t irq_num[PCI_NUM_PINS];
    uint32_t first_slot;
    uint32_t first_pin_irq;
    AddressSpace bm_as;
    MemoryRegion bm;
    /* mmio maps */
    MemoryRegion container;
    MemoryRegion iomem;
    MemoryRegion pio;
    MemoryRegion busmem;
};

#define TYPE_PPC_E500_PCI_BRIDGE "e500-host-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(PPCE500PCIBridgeState, PPC_E500_PCI_BRIDGE)

struct PPCE500PCIBridgeState {
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

    MemoryRegion bar0;
};


static uint64_t pci_reg_read4(void *opaque, hwaddr addr,
                              unsigned size)
{
    PPCE500PCIState *pci = opaque;
    unsigned long win;
    uint32_t value = 0;
    int idx;

    win = addr & 0xfe0;

    switch (win) {
    case PPCE500_PCI_OW1:
    case PPCE500_PCI_OW2:
    case PPCE500_PCI_OW3:
    case PPCE500_PCI_OW4:
        idx = (addr >> 5) & 0x7;
        switch (addr & 0x1F) {
        case PCI_POTAR:
            value = pci->pob[idx].potar;
            break;
        case PCI_POTEAR:
            value = pci->pob[idx].potear;
            break;
        case PCI_POWBAR:
            value = pci->pob[idx].powbar;
            break;
        case PCI_POWAR:
            value = pci->pob[idx].powar;
            break;
        default:
            break;
        }
        break;

    case PPCE500_PCI_IW3:
    case PPCE500_PCI_IW2:
    case PPCE500_PCI_IW1:
        idx = ((addr >> 5) & 0x3) - 1;
        switch (addr & 0x1F) {
        case PCI_PITAR:
            value = pci->pib[idx].pitar;
            break;
        case PCI_PIWBAR:
            value = pci->pib[idx].piwbar;
            break;
        case PCI_PIWBEAR:
            value = pci->pib[idx].piwbear;
            break;
        case PCI_PIWAR:
            value = pci->pib[idx].piwar;
            break;
        default:
            break;
        };
        break;

    case PPCE500_PCI_GASKET_TIMR:
        value = pci->gasket_time;
        break;

    default:
        break;
    }

    pci_debug("%s: win:%lx(addr:" HWADDR_FMT_plx ") -> value:%x\n", __func__,
              win, addr, value);
    return value;
}

/* DMA mapping */
static void e500_update_piw(PPCE500PCIState *pci, int idx)
{
    uint64_t tar = ((uint64_t)pci->pib[idx].pitar) << 12;
    uint64_t wbar = ((uint64_t)pci->pib[idx].piwbar) << 12;
    uint64_t war = pci->pib[idx].piwar;
    uint64_t size = 2ULL << (war & PIWAR_SZ_MASK);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *mem = &pci->pib[idx].mem;
    MemoryRegion *bm = &pci->bm;
    char *name;

    if (memory_region_is_mapped(mem)) {
        /* Before we modify anything, unmap and destroy the region */
        memory_region_del_subregion(bm, mem);
        object_unparent(OBJECT(mem));
    }

    if (!(war & PIWAR_EN)) {
        /* Not enabled, nothing to do */
        return;
    }

    name = g_strdup_printf("PCI Inbound Window %d", idx);
    memory_region_init_alias(mem, OBJECT(pci), name, address_space_mem, tar,
                             size);
    memory_region_add_subregion_overlap(bm, wbar, mem, -1);
    g_free(name);

    pci_debug("%s: Added window of size=%#lx from PCI=%#lx to CPU=%#lx\n",
              __func__, size, wbar, tar);
}

/* BAR mapping */
static void e500_update_pow(PPCE500PCIState *pci, int idx)
{
    uint64_t tar = ((uint64_t)pci->pob[idx].potar) << 12;
    uint64_t wbar = ((uint64_t)pci->pob[idx].powbar) << 12;
    uint64_t war = pci->pob[idx].powar;
    uint64_t size = 2ULL << (war & PIWAR_SZ_MASK);
    MemoryRegion *mem = &pci->pob[idx].mem;
    MemoryRegion *address_space_mem = get_system_memory();
    char *name;

    if (memory_region_is_mapped(mem)) {
        /* Before we modify anything, unmap and destroy the region */
        memory_region_del_subregion(address_space_mem, mem);
        object_unparent(OBJECT(mem));
    }

    if (!(war & PIWAR_EN)) {
        /* Not enabled, nothing to do */
        return;
    }

    name = g_strdup_printf("PCI Outbound Window %d", idx);
    memory_region_init_alias(mem, OBJECT(pci), name, &pci->busmem, tar,
                             size);
    memory_region_add_subregion(address_space_mem, wbar, mem);
    g_free(name);

    pci_debug("%s: Added window of size=%#lx from CPU=%#lx to PCI=%#lx\n",
              __func__, size, wbar, tar);
}

static void pci_reg_write4(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    PPCE500PCIState *pci = opaque;
    unsigned long win;
    int idx;

    win = addr & 0xfe0;

    pci_debug("%s: value:%x -> win:%lx(addr:" HWADDR_FMT_plx ")\n",
              __func__, (unsigned)value, win, addr);

    switch (win) {
    case PPCE500_PCI_OW1:
    case PPCE500_PCI_OW2:
    case PPCE500_PCI_OW3:
    case PPCE500_PCI_OW4:
        idx = (addr >> 5) & 0x7;
        switch (addr & 0x1F) {
        case PCI_POTAR:
            pci->pob[idx].potar = value;
            e500_update_pow(pci, idx);
            break;
        case PCI_POTEAR:
            pci->pob[idx].potear = value;
            e500_update_pow(pci, idx);
            break;
        case PCI_POWBAR:
            pci->pob[idx].powbar = value;
            e500_update_pow(pci, idx);
            break;
        case PCI_POWAR:
            pci->pob[idx].powar = value;
            e500_update_pow(pci, idx);
            break;
        default:
            break;
        };
        break;

    case PPCE500_PCI_IW3:
    case PPCE500_PCI_IW2:
    case PPCE500_PCI_IW1:
        idx = ((addr >> 5) & 0x3) - 1;
        switch (addr & 0x1F) {
        case PCI_PITAR:
            pci->pib[idx].pitar = value;
            e500_update_piw(pci, idx);
            break;
        case PCI_PIWBAR:
            pci->pib[idx].piwbar = value;
            e500_update_piw(pci, idx);
            break;
        case PCI_PIWBEAR:
            pci->pib[idx].piwbear = value;
            e500_update_piw(pci, idx);
            break;
        case PCI_PIWAR:
            pci->pib[idx].piwar = value;
            e500_update_piw(pci, idx);
            break;
        default:
            break;
        };
        break;

    case PPCE500_PCI_GASKET_TIMR:
        pci->gasket_time = value;
        break;

    default:
        break;
    };
}

static const MemoryRegionOps e500_pci_reg_ops = {
    .read = pci_reg_read4,
    .write = pci_reg_write4,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int mpc85xx_pci_map_irq(PCIDevice *pci_dev, int pin)
{
    int devno = PCI_SLOT(pci_dev->devfn);
    int ret;

    ret = ppce500_pci_map_irq_slot(devno, pin);

    pci_debug("%s: devfn %x irq %d -> %d  devno:%x\n", __func__,
           pci_dev->devfn, pin, ret, devno);

    return ret;
}

static void mpc85xx_pci_set_irq(void *opaque, int pin, int level)
{
    PPCE500PCIState *s = opaque;
    qemu_irq *pic = s->irq;

    pci_debug("%s: PCI irq %d, level:%d\n", __func__, pin , level);

    qemu_set_irq(pic[pin], level);
}

static PCIINTxRoute e500_route_intx_pin_to_irq(void *opaque, int pin)
{
    PCIINTxRoute route;
    PPCE500PCIState *s = opaque;

    route.mode = PCI_INTX_ENABLED;
    route.irq = s->irq_num[pin];

    pci_debug("%s: PCI irq-pin = %d, irq_num= %d\n", __func__, pin, route.irq);
    return route;
}

static const VMStateDescription vmstate_pci_outbound = {
    .name = "pci_outbound",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(potar, struct pci_outbound),
        VMSTATE_UINT32(potear, struct pci_outbound),
        VMSTATE_UINT32(powbar, struct pci_outbound),
        VMSTATE_UINT32(powar, struct pci_outbound),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pci_inbound = {
    .name = "pci_inbound",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(pitar, struct pci_inbound),
        VMSTATE_UINT32(piwbar, struct pci_inbound),
        VMSTATE_UINT32(piwbear, struct pci_inbound),
        VMSTATE_UINT32(piwar, struct pci_inbound),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ppce500_pci = {
    .name = "ppce500_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(pob, PPCE500PCIState, PPCE500_PCI_NR_POBS, 1,
                             vmstate_pci_outbound, struct pci_outbound),
        VMSTATE_STRUCT_ARRAY(pib, PPCE500PCIState, PPCE500_PCI_NR_PIBS, 1,
                             vmstate_pci_inbound, struct pci_inbound),
        VMSTATE_UINT32(gasket_time, PPCE500PCIState),
        VMSTATE_END_OF_LIST()
    }
};


static void e500_pcihost_bridge_realize(PCIDevice *d, Error **errp)
{
    PPCE500PCIBridgeState *b = PPC_E500_PCI_BRIDGE(d);
    PPCE500CCSRState *ccsr = CCSR(container_get(qdev_get_machine(),
                                  "/e500-ccsr"));

    memory_region_init_alias(&b->bar0, OBJECT(ccsr), "e500-pci-bar0", &ccsr->ccsr_space,
                             0, int128_get64(ccsr->ccsr_space.size));
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &b->bar0);
}

static AddressSpace *e500_pcihost_set_iommu(PCIBus *bus, void *opaque,
                                            int devfn)
{
    PPCE500PCIState *s = opaque;

    return &s->bm_as;
}

static const PCIIOMMUOps ppce500_iommu_ops = {
    .get_address_space = e500_pcihost_set_iommu,
};

static void e500_pcihost_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PCIHostState *h;
    PPCE500PCIState *s;
    PCIBus *b;
    int i;

    h = PCI_HOST_BRIDGE(dev);
    s = PPC_E500_PCI_HOST_BRIDGE(dev);

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }

    for (i = 0; i < PCI_NUM_PINS; i++) {
        s->irq_num[i] = s->first_pin_irq + i;
    }

    memory_region_init(&s->pio, OBJECT(s), "pci-pio", PCIE500_PCI_IOLEN);
    memory_region_init(&s->busmem, OBJECT(s), "pci bus memory", UINT64_MAX);

    /* PIO lives at the bottom of our bus space */
    memory_region_add_subregion_overlap(&s->busmem, 0, &s->pio, -2);

    b = pci_register_root_bus(dev, NULL, mpc85xx_pci_set_irq,
                              mpc85xx_pci_map_irq, s, &s->busmem, &s->pio,
                              PCI_DEVFN(s->first_slot, 0), 4, TYPE_PCI_BUS);
    h->bus = b;

    /* Set up PCI view of memory */
    memory_region_init(&s->bm, OBJECT(s), "bm-e500", UINT64_MAX);
    memory_region_add_subregion(&s->bm, 0x0, &s->busmem);
    address_space_init(&s->bm_as, &s->bm, "pci-bm");
    pci_setup_iommu(b, &ppce500_iommu_ops, s);

    pci_create_simple(b, 0, "e500-host-bridge");

    memory_region_init(&s->container, OBJECT(h), "pci-container", PCIE500_ALL_SIZE);
    memory_region_init_io(&h->conf_mem, OBJECT(h), &pci_host_conf_be_ops, h,
                          "pci-conf-idx", 4);
    memory_region_init_io(&h->data_mem, OBJECT(h), &pci_host_data_le_ops, h,
                          "pci-conf-data", 4);
    memory_region_init_io(&s->iomem, OBJECT(s), &e500_pci_reg_ops, s,
                          "pci.reg", PCIE500_REG_SIZE);
    memory_region_add_subregion(&s->container, PCIE500_CFGADDR, &h->conf_mem);
    memory_region_add_subregion(&s->container, PCIE500_CFGDATA, &h->data_mem);
    memory_region_add_subregion(&s->container, PCIE500_REG_BASE, &s->iomem);
    sysbus_init_mmio(sbd, &s->container);
    pci_bus_set_route_irq_fn(b, e500_route_intx_pin_to_irq);
}

static void e500_host_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = e500_pcihost_bridge_realize;
    k->vendor_id = PCI_VENDOR_ID_FREESCALE;
    k->device_id = PCI_DEVICE_ID_MPC8533E;
    k->class_id = PCI_CLASS_PROCESSOR_POWERPC;
    dc->desc = "Host bridge";
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo e500_host_bridge_info = {
    .name          = TYPE_PPC_E500_PCI_BRIDGE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PPCE500PCIBridgeState),
    .class_init    = e500_host_bridge_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static Property pcihost_properties[] = {
    DEFINE_PROP_UINT32("first_slot", PPCE500PCIState, first_slot, 0x11),
    DEFINE_PROP_UINT32("first_pin_irq", PPCE500PCIState, first_pin_irq, 0x1),
    DEFINE_PROP_END_OF_LIST(),
};

static void e500_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = e500_pcihost_realize;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    device_class_set_props(dc, pcihost_properties);
    dc->vmsd = &vmstate_ppce500_pci;
}

static const TypeInfo e500_pcihost_info = {
    .name          = TYPE_PPC_E500_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PPCE500PCIState),
    .class_init    = e500_pcihost_class_init,
};

static void e500_pci_register_types(void)
{
    type_register_static(&e500_pcihost_info);
    type_register_static(&e500_host_bridge_info);
}

type_init(e500_pci_register_types)
