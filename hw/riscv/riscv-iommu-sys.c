/*
 * QEMU emulation of an RISC-V IOMMU Platform Device
 *
 * Copyright (C) 2022-2023 Rivos Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "trace.h"

#include "riscv-iommu.h"

#define RISCV_IOMMU_SYSDEV_ICVEC_VECTORS 0x3333

#define RISCV_IOMMU_PCI_MSIX_VECTORS 5

/* RISC-V IOMMU System Platform Device Emulation */

struct RISCVIOMMUStateSys {
    SysBusDevice     parent;
    uint64_t         addr;
    uint32_t         base_irq;
    DeviceState      *irqchip;
    RISCVIOMMUState  iommu;

    /* Wired int support */
    qemu_irq         irqs[RISCV_IOMMU_INTR_COUNT];

    /* Memory Regions for MSIX table and pending bit entries. */
    MemoryRegion msix_table_mmio;
    MemoryRegion msix_pba_mmio;
    uint8_t *msix_table;
    uint8_t *msix_pba;
};

static uint64_t msix_table_mmio_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    RISCVIOMMUStateSys *s = opaque;

    g_assert(addr + size <= RISCV_IOMMU_PCI_MSIX_VECTORS * PCI_MSIX_ENTRY_SIZE);
    return pci_get_long(s->msix_table + addr);
}

static void msix_table_mmio_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    RISCVIOMMUStateSys *s = opaque;

    g_assert(addr + size <= RISCV_IOMMU_PCI_MSIX_VECTORS * PCI_MSIX_ENTRY_SIZE);
    pci_set_long(s->msix_table + addr, val);
}

static const MemoryRegionOps msix_table_mmio_ops = {
    .read = msix_table_mmio_read,
    .write = msix_table_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .max_access_size = 4,
    },
};

static uint64_t msix_pba_mmio_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    RISCVIOMMUStateSys *s = opaque;

    return pci_get_long(s->msix_pba + addr);
}

static void msix_pba_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
}

static const MemoryRegionOps msix_pba_mmio_ops = {
    .read = msix_pba_mmio_read,
    .write = msix_pba_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .max_access_size = 4,
    },
};

static void riscv_iommu_sysdev_init_msi(RISCVIOMMUStateSys *s,
                                        uint32_t n_vectors)
{
    RISCVIOMMUState *iommu = &s->iommu;
    uint32_t table_size = n_vectors * PCI_MSIX_ENTRY_SIZE;
    uint32_t table_offset = RISCV_IOMMU_REG_MSI_CONFIG;
    uint32_t pba_size = QEMU_ALIGN_UP(n_vectors, 64) / 8;
    uint32_t pba_offset = RISCV_IOMMU_REG_MSI_CONFIG + 256;

    s->msix_table = g_malloc0(table_size);
    s->msix_pba = g_malloc0(pba_size);

    memory_region_init_io(&s->msix_table_mmio, OBJECT(s), &msix_table_mmio_ops,
                          s, "msix-table", table_size);
    memory_region_add_subregion(&iommu->regs_mr, table_offset,
                                &s->msix_table_mmio);

    memory_region_init_io(&s->msix_pba_mmio, OBJECT(s), &msix_pba_mmio_ops, s,
                          "msix-pba", pba_size);
    memory_region_add_subregion(&iommu->regs_mr, pba_offset,
                                &s->msix_pba_mmio);
}

static void riscv_iommu_sysdev_send_MSI(RISCVIOMMUStateSys *s,
                                        uint32_t vector)
{
    uint8_t *table_entry = s->msix_table + vector * PCI_MSIX_ENTRY_SIZE;
    uint64_t msi_addr = pci_get_quad(table_entry + PCI_MSIX_ENTRY_LOWER_ADDR);
    uint32_t msi_data = pci_get_long(table_entry + PCI_MSIX_ENTRY_DATA);
    MemTxResult result;

    address_space_stl_le(&address_space_memory, msi_addr,
                         msi_data, MEMTXATTRS_UNSPECIFIED, &result);
    trace_riscv_iommu_sys_msi_sent(vector, msi_addr, msi_data, result);
}

static void riscv_iommu_sysdev_notify(RISCVIOMMUState *iommu,
                                      unsigned vector)
{
    RISCVIOMMUStateSys *s = container_of(iommu, RISCVIOMMUStateSys, iommu);
    uint32_t fctl =  riscv_iommu_reg_get32(iommu, RISCV_IOMMU_REG_FCTL);

    if (fctl & RISCV_IOMMU_FCTL_WSI) {
        qemu_irq_pulse(s->irqs[vector]);
        trace_riscv_iommu_sys_irq_sent(vector);
        return;
    }

    riscv_iommu_sysdev_send_MSI(s, vector);
}

static void riscv_iommu_sys_realize(DeviceState *dev, Error **errp)
{
    RISCVIOMMUStateSys *s = RISCV_IOMMU_SYS(dev);
    SysBusDevice *sysdev = SYS_BUS_DEVICE(s);
    PCIBus *pci_bus;
    qemu_irq irq;

    qdev_realize(DEVICE(&s->iommu), NULL, errp);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iommu.regs_mr);
    if (s->addr) {
        sysbus_mmio_map(SYS_BUS_DEVICE(s), 0, s->addr);
    }

    pci_bus = (PCIBus *) object_resolve_path_type("", TYPE_PCI_BUS, NULL);
    if (pci_bus) {
        riscv_iommu_pci_setup_iommu(&s->iommu, pci_bus, errp);
    }

    s->iommu.notify = riscv_iommu_sysdev_notify;

    /* 4 IRQs are defined starting from s->base_irq */
    for (int i = 0; i < RISCV_IOMMU_INTR_COUNT; i++) {
        sysbus_init_irq(sysdev, &s->irqs[i]);
        irq = qdev_get_gpio_in(s->irqchip, s->base_irq + i);
        sysbus_connect_irq(sysdev, i, irq);
    }

    riscv_iommu_sysdev_init_msi(s, RISCV_IOMMU_PCI_MSIX_VECTORS);
}

static void riscv_iommu_sys_init(Object *obj)
{
    RISCVIOMMUStateSys *s = RISCV_IOMMU_SYS(obj);
    RISCVIOMMUState *iommu = &s->iommu;

    object_initialize_child(obj, "iommu", iommu, TYPE_RISCV_IOMMU);
    qdev_alias_all_properties(DEVICE(iommu), obj);

    iommu->icvec_avail_vectors = RISCV_IOMMU_SYSDEV_ICVEC_VECTORS;
    riscv_iommu_set_cap_igs(iommu, RISCV_IOMMU_CAP_IGS_BOTH);
}

static const Property riscv_iommu_sys_properties[] = {
    DEFINE_PROP_UINT64("addr", RISCVIOMMUStateSys, addr, 0),
    DEFINE_PROP_UINT32("base-irq", RISCVIOMMUStateSys, base_irq, 0),
    DEFINE_PROP_LINK("irqchip", RISCVIOMMUStateSys, irqchip,
                     TYPE_DEVICE, DeviceState *),
};

static void riscv_iommu_sys_reset_hold(Object *obj, ResetType type)
{
    RISCVIOMMUStateSys *sys = RISCV_IOMMU_SYS(obj);
    RISCVIOMMUState *iommu = &sys->iommu;

    riscv_iommu_reset(iommu);

    trace_riscv_iommu_sys_reset_hold(type);
}

static void riscv_iommu_sys_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = riscv_iommu_sys_reset_hold;

    dc->realize = riscv_iommu_sys_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, riscv_iommu_sys_properties);
}

static const TypeInfo riscv_iommu_sys = {
    .name          = TYPE_RISCV_IOMMU_SYS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = riscv_iommu_sys_class_init,
    .instance_init = riscv_iommu_sys_init,
    .instance_size = sizeof(RISCVIOMMUStateSys),
};

static void riscv_iommu_register_sys(void)
{
    type_register_static(&riscv_iommu_sys);
}

type_init(riscv_iommu_register_sys)
