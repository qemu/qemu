/*
 * Emulation of the ibm,plb-pcix PCI controller
 * This is found in some 440 SoCs e.g. the 460EX.
 *
 * Copyright (c) 2016-2018 BALATON Zoltan
 *
 * Derived from ppc4xx_pci.c and pci-host/ppce500.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/ppc4xx.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "trace.h"
#include "qom/object.h"

struct PLBOutMap {
    uint64_t la;
    uint64_t pcia;
    uint32_t sa;
    MemoryRegion mr;
};

struct PLBInMap {
    uint64_t sa;
    uint64_t la;
    MemoryRegion mr;
};

#define TYPE_PPC440_PCIX_HOST_BRIDGE "ppc440-pcix-host"
OBJECT_DECLARE_SIMPLE_TYPE(PPC440PCIXState, PPC440_PCIX_HOST_BRIDGE)

#define PPC440_PCIX_NR_POMS 3
#define PPC440_PCIX_NR_PIMS 3

struct PPC440PCIXState {
    PCIHostState parent_obj;

    PCIDevice *dev;
    struct PLBOutMap pom[PPC440_PCIX_NR_POMS];
    struct PLBInMap pim[PPC440_PCIX_NR_PIMS];
    uint32_t sts;
    qemu_irq irq;
    AddressSpace bm_as;
    MemoryRegion bm;

    MemoryRegion container;
    MemoryRegion iomem;
    MemoryRegion busmem;
};

#define PPC440_REG_BASE     0x80000
#define PPC440_REG_SIZE     0xff

#define PCIC0_CFGADDR       0x0
#define PCIC0_CFGDATA       0x4

#define PCIX0_POM0LAL       0x68
#define PCIX0_POM0LAH       0x6c
#define PCIX0_POM0SA        0x70
#define PCIX0_POM0PCIAL     0x74
#define PCIX0_POM0PCIAH     0x78
#define PCIX0_POM1LAL       0x7c
#define PCIX0_POM1LAH       0x80
#define PCIX0_POM1SA        0x84
#define PCIX0_POM1PCIAL     0x88
#define PCIX0_POM1PCIAH     0x8c
#define PCIX0_POM2SA        0x90

#define PCIX0_PIM0SAL       0x98
#define PCIX0_PIM0LAL       0x9c
#define PCIX0_PIM0LAH       0xa0
#define PCIX0_PIM1SA        0xa4
#define PCIX0_PIM1LAL       0xa8
#define PCIX0_PIM1LAH       0xac
#define PCIX0_PIM2SAL       0xb0
#define PCIX0_PIM2LAL       0xb4
#define PCIX0_PIM2LAH       0xb8
#define PCIX0_PIM0SAH       0xf8
#define PCIX0_PIM2SAH       0xfc

#define PCIX0_STS           0xe0

#define PCI_ALL_SIZE        (PPC440_REG_BASE + PPC440_REG_SIZE)

static void ppc440_pcix_clear_region(MemoryRegion *parent,
                                     MemoryRegion *mem)
{
    if (memory_region_is_mapped(mem)) {
        memory_region_del_subregion(parent, mem);
        object_unparent(OBJECT(mem));
    }
}

/* DMA mapping */
static void ppc440_pcix_update_pim(PPC440PCIXState *s, int idx)
{
    MemoryRegion *mem = &s->pim[idx].mr;
    char *name;
    uint64_t size;

    /* Before we modify anything, unmap and destroy the region */
    ppc440_pcix_clear_region(&s->bm, mem);

    if (!(s->pim[idx].sa & 1)) {
        /* Not enabled, nothing to do */
        return;
    }

    name = g_strdup_printf("PCI Inbound Window %d", idx);
    size = ~(s->pim[idx].sa & ~7ULL) + 1;
    memory_region_init_alias(mem, OBJECT(s), name, get_system_memory(),
                             s->pim[idx].la, size);
    memory_region_add_subregion_overlap(&s->bm, 0, mem, -1);
    g_free(name);

    trace_ppc440_pcix_update_pim(idx, size, s->pim[idx].la);
}

/* BAR mapping */
static void ppc440_pcix_update_pom(PPC440PCIXState *s, int idx)
{
    MemoryRegion *mem = &s->pom[idx].mr;
    MemoryRegion *address_space_mem = get_system_memory();
    char *name;
    uint32_t size;

    /* Before we modify anything, unmap and destroy the region */
    ppc440_pcix_clear_region(address_space_mem, mem);

    if (!(s->pom[idx].sa & 1)) {
        /* Not enabled, nothing to do */
        return;
    }

    name = g_strdup_printf("PCI Outbound Window %d", idx);
    size = ~(s->pom[idx].sa & 0xfffffffe) + 1;
    if (!size) {
        size = 0xffffffff;
    }
    memory_region_init_alias(mem, OBJECT(s), name, &s->busmem,
                             s->pom[idx].pcia, size);
    memory_region_add_subregion(address_space_mem, s->pom[idx].la, mem);
    g_free(name);

    trace_ppc440_pcix_update_pom(idx, size, s->pom[idx].la, s->pom[idx].pcia);
}

static void ppc440_pcix_reg_write4(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    struct PPC440PCIXState *s = opaque;

    trace_ppc440_pcix_reg_write(addr, val, size);
    switch (addr) {
    case PCI_VENDOR_ID ... PCI_MAX_LAT:
        stl_le_p(s->dev->config + addr, val);
        break;

    case PCIX0_POM0LAL:
        s->pom[0].la &= 0xffffffff00000000ULL;
        s->pom[0].la |= val;
        ppc440_pcix_update_pom(s, 0);
        break;
    case PCIX0_POM0LAH:
        s->pom[0].la &= 0xffffffffULL;
        s->pom[0].la |= val << 32;
        ppc440_pcix_update_pom(s, 0);
        break;
    case PCIX0_POM0SA:
        s->pom[0].sa = val;
        ppc440_pcix_update_pom(s, 0);
        break;
    case PCIX0_POM0PCIAL:
        s->pom[0].pcia &= 0xffffffff00000000ULL;
        s->pom[0].pcia |= val;
        ppc440_pcix_update_pom(s, 0);
        break;
    case PCIX0_POM0PCIAH:
        s->pom[0].pcia &= 0xffffffffULL;
        s->pom[0].pcia |= val << 32;
        ppc440_pcix_update_pom(s, 0);
        break;
    case PCIX0_POM1LAL:
        s->pom[1].la &= 0xffffffff00000000ULL;
        s->pom[1].la |= val;
        ppc440_pcix_update_pom(s, 1);
        break;
    case PCIX0_POM1LAH:
        s->pom[1].la &= 0xffffffffULL;
        s->pom[1].la |= val << 32;
        ppc440_pcix_update_pom(s, 1);
        break;
    case PCIX0_POM1SA:
        s->pom[1].sa = val;
        ppc440_pcix_update_pom(s, 1);
        break;
    case PCIX0_POM1PCIAL:
        s->pom[1].pcia &= 0xffffffff00000000ULL;
        s->pom[1].pcia |= val;
        ppc440_pcix_update_pom(s, 1);
        break;
    case PCIX0_POM1PCIAH:
        s->pom[1].pcia &= 0xffffffffULL;
        s->pom[1].pcia |= val << 32;
        ppc440_pcix_update_pom(s, 1);
        break;
    case PCIX0_POM2SA:
        s->pom[2].sa = val;
        break;

    case PCIX0_PIM0SAL:
        s->pim[0].sa &= 0xffffffff00000000ULL;
        s->pim[0].sa |= val;
        ppc440_pcix_update_pim(s, 0);
        break;
    case PCIX0_PIM0LAL:
        s->pim[0].la &= 0xffffffff00000000ULL;
        s->pim[0].la |= val;
        ppc440_pcix_update_pim(s, 0);
        break;
    case PCIX0_PIM0LAH:
        s->pim[0].la &= 0xffffffffULL;
        s->pim[0].la |= val << 32;
        ppc440_pcix_update_pim(s, 0);
        break;
    case PCIX0_PIM1SA:
        s->pim[1].sa = val;
        ppc440_pcix_update_pim(s, 1);
        break;
    case PCIX0_PIM1LAL:
        s->pim[1].la &= 0xffffffff00000000ULL;
        s->pim[1].la |= val;
        ppc440_pcix_update_pim(s, 1);
        break;
    case PCIX0_PIM1LAH:
        s->pim[1].la &= 0xffffffffULL;
        s->pim[1].la |= val << 32;
        ppc440_pcix_update_pim(s, 1);
        break;
    case PCIX0_PIM2SAL:
        s->pim[2].sa &= 0xffffffff00000000ULL;
        s->pim[2].sa |= val;
        ppc440_pcix_update_pim(s, 2);
        break;
    case PCIX0_PIM2LAL:
        s->pim[2].la &= 0xffffffff00000000ULL;
        s->pim[2].la |= val;
        ppc440_pcix_update_pim(s, 2);
        break;
    case PCIX0_PIM2LAH:
        s->pim[2].la &= 0xffffffffULL;
        s->pim[2].la |= val << 32;
        ppc440_pcix_update_pim(s, 2);
        break;

    case PCIX0_STS:
        s->sts = val;
        break;

    case PCIX0_PIM0SAH:
        s->pim[0].sa &= 0xffffffffULL;
        s->pim[0].sa |= val << 32;
        ppc440_pcix_update_pim(s, 0);
        break;
    case PCIX0_PIM2SAH:
        s->pim[2].sa &= 0xffffffffULL;
        s->pim[2].sa |= val << 32;
        ppc440_pcix_update_pim(s, 2);
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unhandled PCI internal register 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static uint64_t ppc440_pcix_reg_read4(void *opaque, hwaddr addr,
                                     unsigned size)
{
    struct PPC440PCIXState *s = opaque;
    uint32_t val;

    switch (addr) {
    case PCI_VENDOR_ID ... PCI_MAX_LAT:
        val = ldl_le_p(s->dev->config + addr);
        break;

    case PCIX0_POM0LAL:
        val = s->pom[0].la;
        break;
    case PCIX0_POM0LAH:
        val = s->pom[0].la >> 32;
        break;
    case PCIX0_POM0SA:
        val = s->pom[0].sa;
        break;
    case PCIX0_POM0PCIAL:
        val = s->pom[0].pcia;
        break;
    case PCIX0_POM0PCIAH:
        val = s->pom[0].pcia >> 32;
        break;
    case PCIX0_POM1LAL:
        val = s->pom[1].la;
        break;
    case PCIX0_POM1LAH:
        val = s->pom[1].la >> 32;
        break;
    case PCIX0_POM1SA:
        val = s->pom[1].sa;
        break;
    case PCIX0_POM1PCIAL:
        val = s->pom[1].pcia;
        break;
    case PCIX0_POM1PCIAH:
        val = s->pom[1].pcia >> 32;
        break;
    case PCIX0_POM2SA:
        val = s->pom[2].sa;
        break;

    case PCIX0_PIM0SAL:
        val = s->pim[0].sa;
        break;
    case PCIX0_PIM0LAL:
        val = s->pim[0].la;
        break;
    case PCIX0_PIM0LAH:
        val = s->pim[0].la >> 32;
        break;
    case PCIX0_PIM1SA:
        val = s->pim[1].sa;
        break;
    case PCIX0_PIM1LAL:
        val = s->pim[1].la;
        break;
    case PCIX0_PIM1LAH:
        val = s->pim[1].la >> 32;
        break;
    case PCIX0_PIM2SAL:
        val = s->pim[2].sa;
        break;
    case PCIX0_PIM2LAL:
        val = s->pim[2].la;
        break;
    case PCIX0_PIM2LAH:
        val = s->pim[2].la >> 32;
        break;

    case PCIX0_STS:
        val = s->sts;
        break;

    case PCIX0_PIM0SAH:
        val = s->pim[0].sa  >> 32;
        break;
    case PCIX0_PIM2SAH:
        val = s->pim[2].sa  >> 32;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: invalid PCI internal register 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0;
    }

    trace_ppc440_pcix_reg_read(addr, val);
    return val;
}

static const MemoryRegionOps pci_reg_ops = {
    .read = ppc440_pcix_reg_read4,
    .write = ppc440_pcix_reg_write4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ppc440_pcix_reset(DeviceState *dev)
{
    struct PPC440PCIXState *s = PPC440_PCIX_HOST_BRIDGE(dev);
    int i;

    for (i = 0; i < PPC440_PCIX_NR_POMS; i++) {
        ppc440_pcix_clear_region(get_system_memory(), &s->pom[i].mr);
    }
    for (i = 0; i < PPC440_PCIX_NR_PIMS; i++) {
        ppc440_pcix_clear_region(&s->bm, &s->pim[i].mr);
    }
    memset(s->pom, 0, sizeof(s->pom));
    memset(s->pim, 0, sizeof(s->pim));
    for (i = 0; i < PPC440_PCIX_NR_PIMS; i++) {
        s->pim[i].sa = 0xffffffff00000000ULL;
    }
    s->sts = 0;
}

/*
 * All four IRQ[ABCD] pins from all slots are tied to a single board
 * IRQ, so our mapping function here maps everything to IRQ 0.
 * The code in pci_change_irq_level() tracks the number of times
 * the mapped IRQ is asserted and deasserted, so if multiple devices
 * assert an IRQ at the same time the behaviour is correct.
 *
 * This may need further refactoring for boards that use multiple IRQ lines.
 */
static int ppc440_pcix_map_irq(PCIDevice *pci_dev, int irq_num)
{
    trace_ppc440_pcix_map_irq(pci_dev->devfn, irq_num, 0);
    return 0;
}

static void ppc440_pcix_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pci_irq = opaque;

    trace_ppc440_pcix_set_irq(irq_num);
    if (irq_num < 0) {
        error_report("%s: PCI irq %d", __func__, irq_num);
        return;
    }
    qemu_set_irq(*pci_irq, level);
}

static AddressSpace *ppc440_pcix_set_iommu(PCIBus *b, void *opaque, int devfn)
{
    PPC440PCIXState *s = opaque;

    return &s->bm_as;
}

/*
 * Some guests on sam460ex write all kinds of garbage here such as
 * missing enable bit and low bits set and still expect this to work
 * (apparently it does on real hardware because these boot there) so
 * we have to override these ops here and fix it up
 */
static void pci_host_config_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned len)
{
    PCIHostState *s = opaque;

    if (addr != 0 || len != 4) {
        return;
    }
    s->config_reg = (val & 0xfffffffcULL) | (1UL << 31);
}

static uint64_t pci_host_config_read(void *opaque, hwaddr addr,
                                     unsigned len)
{
    PCIHostState *s = opaque;
    uint32_t val = s->config_reg;

    return val;
}

const MemoryRegionOps ppc440_pcix_host_conf_ops = {
    .read = pci_host_config_read,
    .write = pci_host_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void ppc440_pcix_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    PPC440PCIXState *s;
    PCIHostState *h;

    h = PCI_HOST_BRIDGE(dev);
    s = PPC440_PCIX_HOST_BRIDGE(dev);

    sysbus_init_irq(sbd, &s->irq);
    memory_region_init(&s->busmem, OBJECT(dev), "pci bus memory", UINT64_MAX);
    h->bus = pci_register_root_bus(dev, NULL, ppc440_pcix_set_irq,
                         ppc440_pcix_map_irq, &s->irq, &s->busmem,
                         get_system_io(), PCI_DEVFN(0, 0), 1, TYPE_PCI_BUS);

    s->dev = pci_create_simple(h->bus, PCI_DEVFN(0, 0), "ppc4xx-host-bridge");

    memory_region_init(&s->bm, OBJECT(s), "bm-ppc440-pcix", UINT64_MAX);
    memory_region_add_subregion(&s->bm, 0x0, &s->busmem);
    address_space_init(&s->bm_as, &s->bm, "pci-bm");
    pci_setup_iommu(h->bus, ppc440_pcix_set_iommu, s);

    memory_region_init(&s->container, OBJECT(s), "pci-container", PCI_ALL_SIZE);
    memory_region_init_io(&h->conf_mem, OBJECT(s), &ppc440_pcix_host_conf_ops,
                          h, "pci-conf-idx", 4);
    memory_region_init_io(&h->data_mem, OBJECT(s), &pci_host_data_le_ops,
                          h, "pci-conf-data", 4);
    memory_region_init_io(&s->iomem, OBJECT(s), &pci_reg_ops, s,
                          "pci.reg", PPC440_REG_SIZE);
    memory_region_add_subregion(&s->container, PCIC0_CFGADDR, &h->conf_mem);
    memory_region_add_subregion(&s->container, PCIC0_CFGDATA, &h->data_mem);
    memory_region_add_subregion(&s->container, PPC440_REG_BASE, &s->iomem);
    sysbus_init_mmio(sbd, &s->container);
}

static void ppc440_pcix_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ppc440_pcix_realize;
    dc->reset = ppc440_pcix_reset;
}

static const TypeInfo ppc440_pcix_info = {
    .name          = TYPE_PPC440_PCIX_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PPC440PCIXState),
    .class_init    = ppc440_pcix_class_init,
};

static void ppc440_pcix_register_types(void)
{
    type_register_static(&ppc440_pcix_info);
}

type_init(ppc440_pcix_register_types)
