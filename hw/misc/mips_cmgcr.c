/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 *
 * Copyright (C) 2015 Imagination Technologies
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/misc/mips_cmgcr.h"
#include "hw/misc/mips_cpc.h"
#include "hw/intc/mips_gic.h"

static inline bool is_cpc_connected(MIPSGCRState *s)
{
    return s->cpc_mr != NULL;
}

static inline bool is_gic_connected(MIPSGCRState *s)
{
    return s->gic_mr != NULL;
}

static inline void update_gcr_base(MIPSGCRState *gcr, uint64_t val)
{
    CPUState *cpu;
    MIPSCPU *mips_cpu;

    gcr->gcr_base = val & GCR_BASE_GCRBASE_MSK;
    memory_region_set_address(&gcr->iomem, gcr->gcr_base);

    CPU_FOREACH(cpu) {
        mips_cpu = MIPS_CPU(cpu);
        mips_cpu->env.CP0_CMGCRBase = gcr->gcr_base >> 4;
    }
}

static inline void update_cpc_base(MIPSGCRState *gcr, uint64_t val)
{
    if (is_cpc_connected(gcr)) {
        gcr->cpc_base = val & GCR_CPC_BASE_MSK;
        memory_region_transaction_begin();
        memory_region_set_address(gcr->cpc_mr,
                                  gcr->cpc_base & GCR_CPC_BASE_CPCBASE_MSK);
        memory_region_set_enabled(gcr->cpc_mr,
                                  gcr->cpc_base & GCR_CPC_BASE_CPCEN_MSK);
        memory_region_transaction_commit();
    }
}

static inline void update_gic_base(MIPSGCRState *gcr, uint64_t val)
{
    if (is_gic_connected(gcr)) {
        gcr->gic_base = val & GCR_GIC_BASE_MSK;
        memory_region_transaction_begin();
        memory_region_set_address(gcr->gic_mr,
                                  gcr->gic_base & GCR_GIC_BASE_GICBASE_MSK);
        memory_region_set_enabled(gcr->gic_mr,
                                  gcr->gic_base & GCR_GIC_BASE_GICEN_MSK);
        memory_region_transaction_commit();
    }
}

/* Read GCR registers */
static uint64_t gcr_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSGCRState *gcr = (MIPSGCRState *) opaque;
    MIPSGCRVPState *current_vps = &gcr->vps[current_cpu->cpu_index];
    MIPSGCRVPState *other_vps = &gcr->vps[current_vps->other];

    switch (addr) {
    /* Global Control Block Register */
    case GCR_CONFIG_OFS:
        /* Set PCORES to 0 */
        return 0;
    case GCR_BASE_OFS:
        return gcr->gcr_base;
    case GCR_REV_OFS:
        return gcr->gcr_rev;
    case GCR_GIC_BASE_OFS:
        return gcr->gic_base;
    case GCR_CPC_BASE_OFS:
        return gcr->cpc_base;
    case GCR_GIC_STATUS_OFS:
        return is_gic_connected(gcr);
    case GCR_CPC_STATUS_OFS:
        return is_cpc_connected(gcr);
    case GCR_L2_CONFIG_OFS:
        /* L2 BYPASS */
        return GCR_L2_CONFIG_BYPASS_MSK;
        /* Core-Local and Core-Other Control Blocks */
    case MIPS_CLCB_OFS + GCR_CL_CONFIG_OFS:
    case MIPS_COCB_OFS + GCR_CL_CONFIG_OFS:
        /* Set PVP to # of VPs - 1 */
        return gcr->num_vps - 1;
    case MIPS_CLCB_OFS + GCR_CL_RESETBASE_OFS:
        return current_vps->reset_base;
    case MIPS_COCB_OFS + GCR_CL_RESETBASE_OFS:
        return other_vps->reset_base;
    case MIPS_CLCB_OFS + GCR_CL_OTHER_OFS:
        return current_vps->other;
    case MIPS_COCB_OFS + GCR_CL_OTHER_OFS:
        return other_vps->other;
    default:
        qemu_log_mask(LOG_UNIMP, "Read %d bytes at GCR offset 0x%" HWADDR_PRIx
                      "\n", size, addr);
        return 0;
    }
    return 0;
}

static inline target_ulong get_exception_base(MIPSGCRVPState *vps)
{
    /* TODO: BEV_BASE and SELECT_BEV */
    return (int32_t)(vps->reset_base & GCR_CL_RESET_BASE_RESETBASE_MSK);
}

/* Write GCR registers */
static void gcr_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    MIPSGCRState *gcr = (MIPSGCRState *)opaque;
    MIPSGCRVPState *current_vps = &gcr->vps[current_cpu->cpu_index];
    MIPSGCRVPState *other_vps = &gcr->vps[current_vps->other];

    switch (addr) {
    case GCR_BASE_OFS:
        update_gcr_base(gcr, data);
        break;
    case GCR_GIC_BASE_OFS:
        update_gic_base(gcr, data);
        break;
    case GCR_CPC_BASE_OFS:
        update_cpc_base(gcr, data);
        break;
    case MIPS_CLCB_OFS + GCR_CL_RESETBASE_OFS:
        current_vps->reset_base = data & GCR_CL_RESET_BASE_MSK;
        cpu_set_exception_base(current_cpu->cpu_index,
                               get_exception_base(current_vps));
        break;
    case MIPS_COCB_OFS + GCR_CL_RESETBASE_OFS:
        other_vps->reset_base = data & GCR_CL_RESET_BASE_MSK;
        cpu_set_exception_base(current_vps->other,
                               get_exception_base(other_vps));
        break;
    case MIPS_CLCB_OFS + GCR_CL_OTHER_OFS:
        if ((data & GCR_CL_OTHER_MSK) < gcr->num_vps) {
            current_vps->other = data & GCR_CL_OTHER_MSK;
        }
        break;
    case MIPS_COCB_OFS + GCR_CL_OTHER_OFS:
        if ((data & GCR_CL_OTHER_MSK) < gcr->num_vps) {
            other_vps->other = data & GCR_CL_OTHER_MSK;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "Write %d bytes at GCR offset 0x%" HWADDR_PRIx
                      " 0x%" PRIx64 "\n", size, addr, data);
        break;
    }
}

static const MemoryRegionOps gcr_ops = {
    .read = gcr_read,
    .write = gcr_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void mips_gcr_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSGCRState *s = MIPS_GCR(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &gcr_ops, s,
                          "mips-gcr", GCR_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void mips_gcr_reset(DeviceState *dev)
{
    MIPSGCRState *s = MIPS_GCR(dev);
    int i;

    update_gic_base(s, 0);
    update_cpc_base(s, 0);

    for (i = 0; i < s->num_vps; i++) {
        s->vps[i].other = 0;
        s->vps[i].reset_base = 0xBFC00000 & GCR_CL_RESET_BASE_MSK;
        cpu_set_exception_base(i, get_exception_base(&s->vps[i]));
    }
}

static const VMStateDescription vmstate_mips_gcr = {
    .name = "mips-gcr",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(cpc_base, MIPSGCRState),
        VMSTATE_END_OF_LIST()
    },
};

static Property mips_gcr_properties[] = {
    DEFINE_PROP_INT32("num-vp", MIPSGCRState, num_vps, 1),
    DEFINE_PROP_INT32("gcr-rev", MIPSGCRState, gcr_rev, 0x800),
    DEFINE_PROP_UINT64("gcr-base", MIPSGCRState, gcr_base, GCR_BASE_ADDR),
    DEFINE_PROP_LINK("gic", MIPSGCRState, gic_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("cpc", MIPSGCRState, cpc_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_gcr_realize(DeviceState *dev, Error **errp)
{
    MIPSGCRState *s = MIPS_GCR(dev);

    /* Create local set of registers for each VP */
    s->vps = g_new(MIPSGCRVPState, s->num_vps);
}

static void mips_gcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->props = mips_gcr_properties;
    dc->vmsd = &vmstate_mips_gcr;
    dc->reset = mips_gcr_reset;
    dc->realize = mips_gcr_realize;
}

static const TypeInfo mips_gcr_info = {
    .name          = TYPE_MIPS_GCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSGCRState),
    .instance_init = mips_gcr_init,
    .class_init    = mips_gcr_class_init,
};

static void mips_gcr_register_types(void)
{
    type_register_static(&mips_gcr_info);
}

type_init(mips_gcr_register_types)
