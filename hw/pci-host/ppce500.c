/*
 * QEMU PowerPC E500 embedded processors pci controller emulation
 *
 * Copyright (C) 2009 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Yu Liu,     <yu.liu@freescale.com>
 *
 * This file is derived from hw/ppc4xx_pci.c,
 * the copyright for that material belongs to the original owners.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of  the GNU General  Public License as published by
 * the Free Software Foundation;  either version 2 of the  License, or
 * (at your option) any later version.
 */

#include "hw/hw.h"
#include "hw/ppc/e500-ccsr.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "qemu/bswap.h"
#include "hw/pci-host/ppce500.h"

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

struct  pci_outbound {
    uint32_t potar;
    uint32_t potear;
    uint32_t powbar;
    uint32_t powar;
};

struct pci_inbound {
    uint32_t pitar;
    uint32_t piwbar;
    uint32_t piwbear;
    uint32_t piwar;
};

#define TYPE_PPC_E500_PCI_HOST_BRIDGE "e500-pcihost"

#define PPC_E500_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(PPCE500PCIState, (obj), TYPE_PPC_E500_PCI_HOST_BRIDGE)

struct PPCE500PCIState {
    PCIHostState parent_obj;

    struct pci_outbound pob[PPCE500_PCI_NR_POBS];
    struct pci_inbound pib[PPCE500_PCI_NR_PIBS];
    uint32_t gasket_time;
    qemu_irq irq[4];
    uint32_t first_slot;
    /* mmio maps */
    MemoryRegion container;
    MemoryRegion iomem;
    MemoryRegion pio;
};

#define TYPE_PPC_E500_PCI_BRIDGE "e500-host-bridge"
#define PPC_E500_PCI_BRIDGE(obj) \
    OBJECT_CHECK(PPCE500PCIBridgeState, (obj), TYPE_PPC_E500_PCI_BRIDGE)

struct PPCE500PCIBridgeState {
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

    MemoryRegion bar0;
};

typedef struct PPCE500PCIBridgeState PPCE500PCIBridgeState;
typedef struct PPCE500PCIState PPCE500PCIState;

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
        switch (addr & 0xC) {
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
        switch (addr & 0xC) {
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

    pci_debug("%s: win:%lx(addr:" TARGET_FMT_plx ") -> value:%x\n", __func__,
              win, addr, value);
    return value;
}

static void pci_reg_write4(void *opaque, hwaddr addr,
                           uint64_t value, unsigned size)
{
    PPCE500PCIState *pci = opaque;
    unsigned long win;
    int idx;

    win = addr & 0xfe0;

    pci_debug("%s: value:%x -> win:%lx(addr:" TARGET_FMT_plx ")\n",
              __func__, (unsigned)value, win, addr);

    switch (win) {
    case PPCE500_PCI_OW1:
    case PPCE500_PCI_OW2:
    case PPCE500_PCI_OW3:
    case PPCE500_PCI_OW4:
        idx = (addr >> 5) & 0x7;
        switch (addr & 0xC) {
        case PCI_POTAR:
            pci->pob[idx].potar = value;
            break;
        case PCI_POTEAR:
            pci->pob[idx].potear = value;
            break;
        case PCI_POWBAR:
            pci->pob[idx].powbar = value;
            break;
        case PCI_POWAR:
            pci->pob[idx].powar = value;
            break;
        default:
            break;
        };
        break;

    case PPCE500_PCI_IW3:
    case PPCE500_PCI_IW2:
    case PPCE500_PCI_IW1:
        idx = ((addr >> 5) & 0x3) - 1;
        switch (addr & 0xC) {
        case PCI_PITAR:
            pci->pib[idx].pitar = value;
            break;
        case PCI_PIWBAR:
            pci->pib[idx].piwbar = value;
            break;
        case PCI_PIWBEAR:
            pci->pib[idx].piwbear = value;
            break;
        case PCI_PIWAR:
            pci->pib[idx].piwar = value;
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

static int mpc85xx_pci_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int devno = pci_dev->devfn >> 3;
    int ret;

    ret = ppce500_pci_map_irq_slot(devno, irq_num);

    pci_debug("%s: devfn %x irq %d -> %d  devno:%x\n", __func__,
           pci_dev->devfn, irq_num, ret, devno);

    return ret;
}

static void mpc85xx_pci_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    pci_debug("%s: PCI irq %d, level:%d\n", __func__, irq_num, level);

    qemu_set_irq(pic[irq_num], level);
}

static const VMStateDescription vmstate_pci_outbound = {
    .name = "pci_outbound",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
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
    .minimum_version_id_old = 0,
    .fields      = (VMStateField[]) {
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
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(pob, PPCE500PCIState, PPCE500_PCI_NR_POBS, 1,
                             vmstate_pci_outbound, struct pci_outbound),
        VMSTATE_STRUCT_ARRAY(pib, PPCE500PCIState, PPCE500_PCI_NR_PIBS, 1,
                             vmstate_pci_outbound, struct pci_inbound),
        VMSTATE_UINT32(gasket_time, PPCE500PCIState),
        VMSTATE_END_OF_LIST()
    }
};

#include "exec/address-spaces.h"

static int e500_pcihost_bridge_initfn(PCIDevice *d)
{
    PPCE500PCIBridgeState *b = PPC_E500_PCI_BRIDGE(d);
    PPCE500CCSRState *ccsr = CCSR(container_get(qdev_get_machine(),
                                  "/e500-ccsr"));

    pci_config_set_class(d->config, PCI_CLASS_BRIDGE_PCI);
    d->config[PCI_HEADER_TYPE] =
        (d->config[PCI_HEADER_TYPE] & PCI_HEADER_TYPE_MULTI_FUNCTION) |
        PCI_HEADER_TYPE_BRIDGE;

    memory_region_init_alias(&b->bar0, OBJECT(ccsr), "e500-pci-bar0", &ccsr->ccsr_space,
                             0, int128_get64(ccsr->ccsr_space.size));
    pci_register_bar(d, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &b->bar0);

    return 0;
}

static int e500_pcihost_initfn(SysBusDevice *dev)
{
    PCIHostState *h;
    PPCE500PCIState *s;
    PCIBus *b;
    int i;
    MemoryRegion *address_space_mem = get_system_memory();

    h = PCI_HOST_BRIDGE(dev);
    s = PPC_E500_PCI_HOST_BRIDGE(dev);

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    memory_region_init(&s->pio, OBJECT(s), "pci-pio", PCIE500_PCI_IOLEN);

    b = pci_register_bus(DEVICE(dev), NULL, mpc85xx_pci_set_irq,
                         mpc85xx_pci_map_irq, s->irq, address_space_mem,
                         &s->pio, PCI_DEVFN(s->first_slot, 0), 4, TYPE_PCI_BUS);
    h->bus = b;

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
    sysbus_init_mmio(dev, &s->container);
    sysbus_init_mmio(dev, &s->pio);

    return 0;
}

static void e500_host_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = e500_pcihost_bridge_initfn;
    k->vendor_id = PCI_VENDOR_ID_FREESCALE;
    k->device_id = PCI_DEVICE_ID_MPC8533E;
    k->class_id = PCI_CLASS_PROCESSOR_POWERPC;
    dc->desc = "Host bridge";
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo e500_host_bridge_info = {
    .name          = "e500-host-bridge",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PPCE500PCIBridgeState),
    .class_init    = e500_host_bridge_class_init,
};

static Property pcihost_properties[] = {
    DEFINE_PROP_UINT32("first_slot", PPCE500PCIState, first_slot, 0x11),
    DEFINE_PROP_END_OF_LIST(),
};

static void e500_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = e500_pcihost_initfn;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->props = pcihost_properties;
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
