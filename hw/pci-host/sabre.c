/*
 * QEMU Ultrasparc Sabre PCI host (PBM)
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2012,2013 Artyom Tarasenko
 * Copyright (c) 2018 Mark Cave-Ayland
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

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_bus.h"
#include "hw/irq.h"
#include "hw/pci-bridge/simba.h"
#include "hw/pci-host/sabre.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "trace.h"

/*
 * Chipset docs:
 * PBM: "UltraSPARC IIi User's Manual",
 * https://web.archive.org/web/20030403110020/http://www.sun.com/processors/manuals/805-0087.pdf
 */

#define PBM_PCI_IMR_MASK    0x7fffffff
#define PBM_PCI_IMR_ENABLED 0x80000000

#define POR          (1U << 31)
#define SOFT_POR     (1U << 30)
#define SOFT_XIR     (1U << 29)
#define BTN_POR      (1U << 28)
#define BTN_XIR      (1U << 27)
#define RESET_MASK   0xf8000000
#define RESET_WCMASK 0x98000000
#define RESET_WMASK  0x60000000

#define NO_IRQ_REQUEST (MAX_IVEC + 1)

static inline void sabre_set_request(SabreState *s, unsigned int irq_num)
{
    trace_sabre_set_request(irq_num);
    s->irq_request = irq_num;
    qemu_set_irq(s->ivec_irqs[irq_num], 1);
}

static inline void sabre_check_irqs(SabreState *s)
{
    unsigned int i;

    /* Previous request is not acknowledged, resubmit */
    if (s->irq_request != NO_IRQ_REQUEST) {
        sabre_set_request(s, s->irq_request);
        return;
    }
    /* no request pending */
    if (s->pci_irq_in == 0ULL) {
        return;
    }
    for (i = 0; i < 32; i++) {
        if (s->pci_irq_in & (1ULL << i)) {
            if (s->pci_irq_map[i >> 2] & PBM_PCI_IMR_ENABLED) {
                sabre_set_request(s, i);
                return;
            }
        }
    }
    for (i = 32; i < 64; i++) {
        if (s->pci_irq_in & (1ULL << i)) {
            if (s->obio_irq_map[i - 32] & PBM_PCI_IMR_ENABLED) {
                sabre_set_request(s, i);
                break;
            }
        }
    }
}

static inline void sabre_clear_request(SabreState *s, unsigned int irq_num)
{
    trace_sabre_clear_request(irq_num);
    qemu_set_irq(s->ivec_irqs[irq_num], 0);
    s->irq_request = NO_IRQ_REQUEST;
}

static AddressSpace *sabre_pci_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    IOMMUState *is = opaque;

    return &is->iommu_as;
}

static const PCIIOMMUOps sabre_iommu_ops = {
    .get_address_space = sabre_pci_dma_iommu,
};

static void sabre_config_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    SabreState *s = opaque;

    trace_sabre_config_write(addr, val);

    switch (addr) {
    case 0x30 ... 0x4f: /* DMA error registers */
        /* XXX: not implemented yet */
        break;
    case 0xc00 ... 0xc3f: /* PCI interrupt control */
        if (addr & 4) {
            unsigned int ino = (addr & 0x3f) >> 3;
            s->pci_irq_map[ino] &= PBM_PCI_IMR_MASK;
            s->pci_irq_map[ino] |= val & ~PBM_PCI_IMR_MASK;
            if ((s->irq_request == ino) && !(val & ~PBM_PCI_IMR_MASK)) {
                sabre_clear_request(s, ino);
            }
            sabre_check_irqs(s);
        }
        break;
    case 0x1000 ... 0x107f: /* OBIO interrupt control */
        if (addr & 4) {
            unsigned int ino = ((addr & 0xff) >> 3);
            s->obio_irq_map[ino] &= PBM_PCI_IMR_MASK;
            s->obio_irq_map[ino] |= val & ~PBM_PCI_IMR_MASK;
            if ((s->irq_request == (ino | 0x20))
                 && !(val & ~PBM_PCI_IMR_MASK)) {
                sabre_clear_request(s, ino | 0x20);
            }
            sabre_check_irqs(s);
        }
        break;
    case 0x1400 ... 0x14ff: /* PCI interrupt clear */
        if (addr & 4) {
            unsigned int ino = (addr & 0xff) >> 5;
            if ((s->irq_request / 4)  == ino) {
                sabre_clear_request(s, s->irq_request);
                sabre_check_irqs(s);
            }
        }
        break;
    case 0x1800 ... 0x1860: /* OBIO interrupt clear */
        if (addr & 4) {
            unsigned int ino = ((addr & 0xff) >> 3) | 0x20;
            if (s->irq_request == ino) {
                sabre_clear_request(s, ino);
                sabre_check_irqs(s);
            }
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
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            } else if (val & SOFT_XIR) {
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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

static uint64_t sabre_config_read(void *opaque,
                                  hwaddr addr, unsigned size)
{
    SabreState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case 0x30 ... 0x4f: /* DMA error registers */
        /* XXX: not implemented yet */
        break;
    case 0xc00 ... 0xc3f: /* PCI interrupt control */
        if (addr & 4) {
            val = s->pci_irq_map[(addr & 0x3f) >> 3];
        }
        break;
    case 0x1000 ... 0x107f: /* OBIO interrupt control */
        if (addr & 4) {
            val = s->obio_irq_map[(addr & 0xff) >> 3];
        }
        break;
    case 0x1080 ... 0x108f: /* PCI bus error */
        if (addr & 4) {
            val = s->pci_err_irq_map[(addr & 0xf) >> 3];
        }
        break;
    case 0x2000 ... 0x202f: /* PCI control */
        val = s->pci_control[(addr & 0x3f) >> 2];
        break;
    case 0xf020 ... 0xf027: /* Reset control */
        if (addr & 4) {
            val = s->reset_control;
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
    trace_sabre_config_read(addr, val);

    return val;
}

static const MemoryRegionOps sabre_config_ops = {
    .read = sabre_config_read,
    .write = sabre_config_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void sabre_pci_config_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    SabreState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);

    trace_sabre_pci_config_write(addr, val);
    pci_data_write(phb->bus, addr, val, size);
}

static uint64_t sabre_pci_config_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
    uint32_t ret;
    SabreState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);

    ret = pci_data_read(phb->bus, addr, size);
    trace_sabre_pci_config_read(addr, ret);
    return ret;
}

/* The sabre host has an IRQ line for each IRQ line of each slot.  */
static int pci_sabre_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* Return the irq as swizzled by the PBM */
    return irq_num;
}

static int pci_simbaA_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* The on-board devices have fixed (legacy) OBIO intnos */
    switch (PCI_SLOT(pci_dev->devfn)) {
    case 1:
        /* Onboard NIC */
        return OBIO_NIC_IRQ;
    case 3:
        /* Onboard IDE */
        return OBIO_HDD_IRQ;
    default:
        /* Normal intno, fall through */
        break;
    }

    return ((PCI_SLOT(pci_dev->devfn) << 2) + irq_num) & 0x1f;
}

static int pci_simbaB_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (0x10 + (PCI_SLOT(pci_dev->devfn) << 2) + irq_num) & 0x1f;
}

static void pci_sabre_set_irq(void *opaque, int irq_num, int level)
{
    SabreState *s = opaque;

    trace_sabre_pci_set_irq(irq_num, level);

    /* PCI IRQ map onto the first 32 INO.  */
    if (irq_num < 32) {
        if (level) {
            s->pci_irq_in |= 1ULL << irq_num;
            if (s->pci_irq_map[irq_num >> 2] & PBM_PCI_IMR_ENABLED) {
                sabre_set_request(s, irq_num);
            }
        } else {
            s->pci_irq_in &= ~(1ULL << irq_num);
        }
    } else {
        /* OBIO IRQ map onto the next 32 INO.  */
        if (level) {
            trace_sabre_pci_set_obio_irq(irq_num, level);
            s->pci_irq_in |= 1ULL << irq_num;
            if ((s->irq_request == NO_IRQ_REQUEST)
                && (s->obio_irq_map[irq_num - 32] & PBM_PCI_IMR_ENABLED)) {
                sabre_set_request(s, irq_num);
            }
        } else {
            s->pci_irq_in &= ~(1ULL << irq_num);
        }
    }
}

static void sabre_reset(DeviceState *d)
{
    SabreState *s = SABRE(d);
    PCIDevice *pci_dev;
    unsigned int i;
    uint16_t cmd;

    for (i = 0; i < 8; i++) {
        s->pci_irq_map[i] &= PBM_PCI_IMR_MASK;
    }
    for (i = 0; i < 32; i++) {
        s->obio_irq_map[i] &= PBM_PCI_IMR_MASK;
    }

    s->irq_request = NO_IRQ_REQUEST;
    s->pci_irq_in = 0ULL;

    if (s->nr_resets++ == 0) {
        /* Power on reset */
        s->reset_control = POR;
    }

    /* As this is the busA PCI bridge which contains the on-board devices
     * attached to the ebus, ensure that we initially allow IO transactions
     * so that we get the early serial console until OpenBIOS can properly
     * configure the PCI bridge itself */
    pci_dev = PCI_DEVICE(s->bridgeA);
    cmd = pci_get_word(pci_dev->config + PCI_COMMAND);
    pci_set_word(pci_dev->config + PCI_COMMAND, cmd | PCI_COMMAND_IO);
    pci_bridge_update_mappings(PCI_BRIDGE(pci_dev));
}

static const MemoryRegionOps pci_config_ops = {
    .read = sabre_pci_config_read,
    .write = sabre_pci_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void sabre_realize(DeviceState *dev, Error **errp)
{
    SabreState *s = SABRE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    PCIDevice *pci_dev;

    memory_region_init(&s->pci_mmio, OBJECT(s), "pci-mmio", 0x100000000ULL);
    memory_region_add_subregion(get_system_memory(), s->mem_base,
                                &s->pci_mmio);

    phb->bus = pci_register_root_bus(dev, "pci",
                                     pci_sabre_set_irq, pci_sabre_map_irq, s,
                                     &s->pci_mmio,
                                     &s->pci_ioport,
                                     0, 0x40, TYPE_PCI_BUS);

    pci_create_simple(phb->bus, 0, TYPE_SABRE_PCI_DEVICE);

    /* IOMMU */
    memory_region_add_subregion_overlap(&s->sabre_config, 0x200,
                    sysbus_mmio_get_region(SYS_BUS_DEVICE(s->iommu), 0), 1);
    pci_setup_iommu(phb->bus, &sabre_iommu_ops, s->iommu);

    /* APB secondary busses */
    pci_dev = pci_new_multifunction(PCI_DEVFN(1, 0), TYPE_SIMBA_PCI_BRIDGE);
    s->bridgeB = PCI_BRIDGE(pci_dev);
    pci_bridge_map_irq(s->bridgeB, "pciB", pci_simbaB_map_irq);
    pci_realize_and_unref(pci_dev, phb->bus, &error_fatal);

    pci_dev = pci_new_multifunction(PCI_DEVFN(1, 1), TYPE_SIMBA_PCI_BRIDGE);
    s->bridgeA = PCI_BRIDGE(pci_dev);
    pci_bridge_map_irq(s->bridgeA, "pciA", pci_simbaA_map_irq);
    pci_realize_and_unref(pci_dev, phb->bus, &error_fatal);
}

static void sabre_init(Object *obj)
{
    SabreState *s = SABRE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    for (i = 0; i < 8; i++) {
        s->pci_irq_map[i] = (0x1f << 6) | (i << 2);
    }
    for (i = 0; i < 2; i++) {
        s->pci_err_irq_map[i] = (0x1f << 6) | 0x30;
    }
    for (i = 0; i < 32; i++) {
        s->obio_irq_map[i] = ((0x1f << 6) | 0x20) + i;
    }
    qdev_init_gpio_in_named(DEVICE(s), pci_sabre_set_irq, "pbm-irq", MAX_IVEC);
    qdev_init_gpio_out_named(DEVICE(s), s->ivec_irqs, "ivec-irq", MAX_IVEC);
    s->irq_request = NO_IRQ_REQUEST;
    s->pci_irq_in = 0ULL;

    /* IOMMU */
    object_property_add_link(obj, "iommu", TYPE_SUN4U_IOMMU,
                             (Object **) &s->iommu,
                             qdev_prop_allow_set_link_before_realize,
                             0);

    /* sabre_config */
    memory_region_init_io(&s->sabre_config, OBJECT(s), &sabre_config_ops, s,
                          "sabre-config", 0x10000);
    /* at region 0 */
    sysbus_init_mmio(sbd, &s->sabre_config);

    memory_region_init_io(&s->pci_config, OBJECT(s), &pci_config_ops, s,
                          "sabre-pci-config", 0x1000000);
    /* at region 1 */
    sysbus_init_mmio(sbd, &s->pci_config);

    /* pci_ioport */
    memory_region_init(&s->pci_ioport, OBJECT(s), "sabre-pci-ioport",
                       0x1000000);

    /* at region 2 */
    sysbus_init_mmio(sbd, &s->pci_ioport);
}

static void sabre_pci_realize(PCIDevice *d, Error **errp)
{
    pci_set_word(d->config + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_66MHZ |
                 PCI_STATUS_DEVSEL_MEDIUM);
}

static void sabre_pci_class_init(ObjectClass *klass, const void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = sabre_pci_realize;
    k->vendor_id = PCI_VENDOR_ID_SUN;
    k->device_id = PCI_DEVICE_ID_SUN_SABRE;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo sabre_pci_info = {
    .name          = TYPE_SABRE_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SabrePCIState),
    .class_init    = sabre_pci_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static char *sabre_ofw_unit_address(const SysBusDevice *dev)
{
    SabreState *s = SABRE(dev);

    return g_strdup_printf("%x,%x",
               (uint32_t)((s->special_base >> 32) & 0xffffffff),
               (uint32_t)(s->special_base & 0xffffffff));
}

static const Property sabre_properties[] = {
    DEFINE_PROP_UINT64("special-base", SabreState, special_base, 0),
    DEFINE_PROP_UINT64("mem-base", SabreState, mem_base, 0),
};

static void sabre_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    dc->realize = sabre_realize;
    device_class_set_legacy_reset(dc, sabre_reset);
    device_class_set_props(dc, sabre_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    sbc->explicit_ofw_unit_address = sabre_ofw_unit_address;
}

static const TypeInfo sabre_info = {
    .name          = TYPE_SABRE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(SabreState),
    .instance_init = sabre_init,
    .class_init    = sabre_class_init,
};

static void sabre_register_types(void)
{
    type_register_static(&sabre_info);
    type_register_static(&sabre_pci_info);
}

type_init(sabre_register_types)
