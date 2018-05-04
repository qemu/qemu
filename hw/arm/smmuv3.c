/*
 * Copyright (C) 2014-2016 Broadcom Corporation
 * Copyright (c) 2017 Red Hat, Inc.
 * Written by Prem Mallappa, Eric Auger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include "hw/boards.h"
#include "sysemu/sysemu.h"
#include "hw/sysbus.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

#include "hw/arm/smmuv3.h"
#include "smmuv3-internal.h"

static void smmuv3_init_regs(SMMUv3State *s)
{
    /**
     * IDR0: stage1 only, AArch64 only, coherent access, 16b ASID,
     *       multi-level stream table
     */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, S1P, 1); /* stage 1 supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTF, 2); /* AArch64 PTW only */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, COHACC, 1); /* IO coherent */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, ASID16, 1); /* 16-bit ASID */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TTENDIAN, 2); /* little endian */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STALL_MODEL, 1); /* No stall */
    /* terminated transaction will always be aborted/error returned */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, TERM_MODEL, 1);
    /* 2-level stream table supported */
    s->idr[0] = FIELD_DP32(s->idr[0], IDR0, STLEVEL, 1);

    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, SIDSIZE, SMMU_IDR1_SIDSIZE);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, EVENTQS, SMMU_EVENTQS);
    s->idr[1] = FIELD_DP32(s->idr[1], IDR1, CMDQS,   SMMU_CMDQS);

   /* 4K and 64K granule support */
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN4K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, GRAN64K, 1);
    s->idr[5] = FIELD_DP32(s->idr[5], IDR5, OAS, SMMU_IDR5_OAS); /* 44 bits */

    s->cmdq.base = deposit64(s->cmdq.base, 0, 5, SMMU_CMDQS);
    s->cmdq.prod = 0;
    s->cmdq.cons = 0;
    s->cmdq.entry_size = sizeof(struct Cmd);
    s->eventq.base = deposit64(s->eventq.base, 0, 5, SMMU_EVENTQS);
    s->eventq.prod = 0;
    s->eventq.cons = 0;
    s->eventq.entry_size = sizeof(struct Evt);

    s->features = 0;
    s->sid_split = 0;
}

static MemTxResult smmu_write_mmio(void *opaque, hwaddr offset, uint64_t data,
                                   unsigned size, MemTxAttrs attrs)
{
    /* not yet implemented */
    return MEMTX_ERROR;
}

static MemTxResult smmu_readll(SMMUv3State *s, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_GERROR_IRQ_CFG0:
        *data = s->gerror_irq_cfg0;
        return MEMTX_OK;
    case A_STRTAB_BASE:
        *data = s->strtab_base;
        return MEMTX_OK;
    case A_CMDQ_BASE:
        *data = s->cmdq.base;
        return MEMTX_OK;
    case A_EVENTQ_BASE:
        *data = s->eventq.base;
        return MEMTX_OK;
    default:
        *data = 0;
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 64-bit access to 0x%"PRIx64" (RAZ)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_readl(SMMUv3State *s, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_IDREGS ... A_IDREGS + 0x1f:
        *data = smmuv3_idreg(offset - A_IDREGS);
        return MEMTX_OK;
    case A_IDR0 ... A_IDR5:
        *data = s->idr[(offset - A_IDR0) / 4];
        return MEMTX_OK;
    case A_IIDR:
        *data = s->iidr;
        return MEMTX_OK;
    case A_CR0:
        *data = s->cr[0];
        return MEMTX_OK;
    case A_CR0ACK:
        *data = s->cr0ack;
        return MEMTX_OK;
    case A_CR1:
        *data = s->cr[1];
        return MEMTX_OK;
    case A_CR2:
        *data = s->cr[2];
        return MEMTX_OK;
    case A_STATUSR:
        *data = s->statusr;
        return MEMTX_OK;
    case A_IRQ_CTRL:
    case A_IRQ_CTRL_ACK:
        *data = s->irq_ctrl;
        return MEMTX_OK;
    case A_GERROR:
        *data = s->gerror;
        return MEMTX_OK;
    case A_GERRORN:
        *data = s->gerrorn;
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0: /* 64b */
        *data = extract64(s->gerror_irq_cfg0, 0, 32);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG0 + 4:
        *data = extract64(s->gerror_irq_cfg0, 32, 32);
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG1:
        *data = s->gerror_irq_cfg1;
        return MEMTX_OK;
    case A_GERROR_IRQ_CFG2:
        *data = s->gerror_irq_cfg2;
        return MEMTX_OK;
    case A_STRTAB_BASE: /* 64b */
        *data = extract64(s->strtab_base, 0, 32);
        return MEMTX_OK;
    case A_STRTAB_BASE + 4: /* 64b */
        *data = extract64(s->strtab_base, 32, 32);
        return MEMTX_OK;
    case A_STRTAB_BASE_CFG:
        *data = s->strtab_base_cfg;
        return MEMTX_OK;
    case A_CMDQ_BASE: /* 64b */
        *data = extract64(s->cmdq.base, 0, 32);
        return MEMTX_OK;
    case A_CMDQ_BASE + 4:
        *data = extract64(s->cmdq.base, 32, 32);
        return MEMTX_OK;
    case A_CMDQ_PROD:
        *data = s->cmdq.prod;
        return MEMTX_OK;
    case A_CMDQ_CONS:
        *data = s->cmdq.cons;
        return MEMTX_OK;
    case A_EVENTQ_BASE: /* 64b */
        *data = extract64(s->eventq.base, 0, 32);
        return MEMTX_OK;
    case A_EVENTQ_BASE + 4: /* 64b */
        *data = extract64(s->eventq.base, 32, 32);
        return MEMTX_OK;
    case A_EVENTQ_PROD:
        *data = s->eventq.prod;
        return MEMTX_OK;
    case A_EVENTQ_CONS:
        *data = s->eventq.cons;
        return MEMTX_OK;
    default:
        *data = 0;
        qemu_log_mask(LOG_UNIMP,
                      "%s unhandled 32-bit access at 0x%"PRIx64" (RAZ)\n",
                      __func__, offset);
        return MEMTX_OK;
    }
}

static MemTxResult smmu_read_mmio(void *opaque, hwaddr offset, uint64_t *data,
                                  unsigned size, MemTxAttrs attrs)
{
    SMMUState *sys = opaque;
    SMMUv3State *s = ARM_SMMUV3(sys);
    MemTxResult r;

    /* CONSTRAINED UNPREDICTABLE choice to have page0/1 be exact aliases */
    offset &= ~0x10000;

    switch (size) {
    case 8:
        r = smmu_readll(s, offset, data, attrs);
        break;
    case 4:
        r = smmu_readl(s, offset, data, attrs);
        break;
    default:
        r = MEMTX_ERROR;
        break;
    }

    trace_smmuv3_read_mmio(offset, *data, size, r);
    return r;
}

static const MemoryRegionOps smmu_mem_ops = {
    .read_with_attrs = smmu_read_mmio,
    .write_with_attrs = smmu_write_mmio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void smmu_init_irq(SMMUv3State *s, SysBusDevice *dev)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irq); i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
}

static void smmu_reset(DeviceState *dev)
{
    SMMUv3State *s = ARM_SMMUV3(dev);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);

    c->parent_reset(dev);

    smmuv3_init_regs(s);
}

static void smmu_realize(DeviceState *d, Error **errp)
{
    SMMUState *sys = ARM_SMMU(d);
    SMMUv3State *s = ARM_SMMUV3(sys);
    SMMUv3Class *c = ARM_SMMUV3_GET_CLASS(s);
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    Error *local_err = NULL;

    c->parent_realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_io(&sys->iomem, OBJECT(s),
                          &smmu_mem_ops, sys, TYPE_ARM_SMMUV3, 0x20000);

    sys->mrtypename = TYPE_SMMUV3_IOMMU_MEMORY_REGION;

    sysbus_init_mmio(dev, &sys->iomem);

    smmu_init_irq(s, dev);
}

static const VMStateDescription vmstate_smmuv3_queue = {
    .name = "smmuv3_queue",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(base, SMMUQueue),
        VMSTATE_UINT32(prod, SMMUQueue),
        VMSTATE_UINT32(cons, SMMUQueue),
        VMSTATE_UINT8(log2size, SMMUQueue),
    },
};

static const VMStateDescription vmstate_smmuv3 = {
    .name = "smmuv3",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(features, SMMUv3State),
        VMSTATE_UINT8(sid_size, SMMUv3State),
        VMSTATE_UINT8(sid_split, SMMUv3State),

        VMSTATE_UINT32_ARRAY(cr, SMMUv3State, 3),
        VMSTATE_UINT32(cr0ack, SMMUv3State),
        VMSTATE_UINT32(statusr, SMMUv3State),
        VMSTATE_UINT32(irq_ctrl, SMMUv3State),
        VMSTATE_UINT32(gerror, SMMUv3State),
        VMSTATE_UINT32(gerrorn, SMMUv3State),
        VMSTATE_UINT64(gerror_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(gerror_irq_cfg2, SMMUv3State),
        VMSTATE_UINT64(strtab_base, SMMUv3State),
        VMSTATE_UINT32(strtab_base_cfg, SMMUv3State),
        VMSTATE_UINT64(eventq_irq_cfg0, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg1, SMMUv3State),
        VMSTATE_UINT32(eventq_irq_cfg2, SMMUv3State),

        VMSTATE_STRUCT(cmdq, SMMUv3State, 0, vmstate_smmuv3_queue, SMMUQueue),
        VMSTATE_STRUCT(eventq, SMMUv3State, 0, vmstate_smmuv3_queue, SMMUQueue),

        VMSTATE_END_OF_LIST(),
    },
};

static void smmuv3_instance_init(Object *obj)
{
    /* Nothing much to do here as of now */
}

static void smmuv3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMMUv3Class *c = ARM_SMMUV3_CLASS(klass);

    dc->vmsd = &vmstate_smmuv3;
    device_class_set_parent_reset(dc, smmu_reset, &c->parent_reset);
    c->parent_realize = dc->realize;
    dc->realize = smmu_realize;
}

static void smmuv3_iommu_memory_region_class_init(ObjectClass *klass,
                                                  void *data)
{
}

static const TypeInfo smmuv3_type_info = {
    .name          = TYPE_ARM_SMMUV3,
    .parent        = TYPE_ARM_SMMU,
    .instance_size = sizeof(SMMUv3State),
    .instance_init = smmuv3_instance_init,
    .class_size    = sizeof(SMMUv3Class),
    .class_init    = smmuv3_class_init,
};

static const TypeInfo smmuv3_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_SMMUV3_IOMMU_MEMORY_REGION,
    .class_init = smmuv3_iommu_memory_region_class_init,
};

static void smmuv3_register_types(void)
{
    type_register(&smmuv3_type_info);
    type_register(&smmuv3_iommu_memory_region_info);
}

type_init(smmuv3_register_types)

