/*
 * Coherent Manager Global Control Register
 *
 * Copyright (C) 2015 Imagination Technologies
 *
 * Copyright (C) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: MIPS P8700 documentation
 *            (https://mips.com/products/hardware/p8700/)
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "hw/misc/riscv_cmgcr.h"
#include "hw/core/qdev-properties.h"

#include "cpu.h"

#define CM_RESET_VEC 0x1FC00000
#define GCR_ADDRSPACE_SZ        0x8000

/* Offsets to register blocks */
#define RISCV_GCB_OFS        0x0000 /* Global Control Block */
#define RISCV_CLCB_OFS       0x2000 /* Core Control Block */
#define RISCV_CORE_REG_STRIDE 0x100 /* Stride between core-specific registers */

/* Global Control Block Register Map */
#define GCR_CONFIG_OFS      0x0000
#define GCR_BASE_OFS        0x0008
#define GCR_REV_OFS         0x0030
#define GCR_CPC_STATUS_OFS  0x00F0
#define GCR_L2_CONFIG_OFS   0x0130

/* GCR_L2_CONFIG register fields */
#define GCR_L2_CONFIG_BYPASS_SHF    20
#define GCR_L2_CONFIG_BYPASS_MSK    ((0x1ULL) << GCR_L2_CONFIG_BYPASS_SHF)

/* GCR_BASE register fields */
#define GCR_BASE_GCRBASE_MSK     0xffffffff8000ULL

/* GCR_CPC_BASE register fields */
#define GCR_CPC_BASE_CPCEN_MSK   1
#define GCR_CPC_BASE_CPCBASE_MSK 0xFFFFFFFF8000ULL
#define GCR_CPC_BASE_MSK (GCR_CPC_BASE_CPCEN_MSK | GCR_CPC_BASE_CPCBASE_MSK)

/* GCR_CL_RESETBASE_OFS register fields */
#define GCR_CL_RESET_BASE_RESETBASE_MSK 0xFFFFFFFFFFFFF000U
#define GCR_CL_RESET_BASE_MSK GCR_CL_RESET_BASE_RESETBASE_MSK

static inline bool is_cpc_connected(RISCVGCRState *s)
{
    return s->cpc_mr != NULL;
}

static inline void update_cpc_base(RISCVGCRState *gcr, uint64_t val)
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

static inline void update_gcr_base(RISCVGCRState *gcr, uint64_t val)
{
    gcr->gcr_base = val & GCR_BASE_GCRBASE_MSK;
    memory_region_set_address(&gcr->iomem, gcr->gcr_base);

    /*
     * For boston-aia, cpc_base is set to gcr_base + 0x8001 to enable
     * cpc automatically.
     */
    update_cpc_base(gcr, val + 0x8001);
}

/* Read GCR registers */
static uint64_t gcr_read(void *opaque, hwaddr addr, unsigned size)
{
    RISCVGCRState *gcr = (RISCVGCRState *) opaque;

    switch (addr) {
    /* Global Control Block Register */
    case GCR_CONFIG_OFS:
        /* Set PCORES to 0 */
        return 0;
    case GCR_BASE_OFS:
        return gcr->gcr_base;
    case GCR_REV_OFS:
        return gcr->gcr_rev;
    case GCR_CPC_STATUS_OFS:
        return is_cpc_connected(gcr);
    case GCR_L2_CONFIG_OFS:
        /* L2 BYPASS */
        return GCR_L2_CONFIG_BYPASS_MSK;
    default:
        qemu_log_mask(LOG_UNIMP, "Read %d bytes at GCR offset 0x%" HWADDR_PRIx
                      "\n", size, addr);
    }
    return 0;
}

static inline target_ulong get_exception_base(RISCVGCRVPState *vps)
{
    return vps->reset_base & GCR_CL_RESET_BASE_RESETBASE_MSK;
}

/* Write GCR registers */
static void gcr_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    RISCVGCRState *gcr = (RISCVGCRState *)opaque;
    RISCVGCRVPState *current_vps;
    int cpu_index, c, h;

    for (c = 0; c < gcr->num_core; c++) {
        for (h = 0; h < gcr->num_hart; h++) {
            if (addr == RISCV_CLCB_OFS + c * RISCV_CORE_REG_STRIDE + h * 8) {
                cpu_index = c * gcr->num_hart + h;
                current_vps = &gcr->vps[cpu_index];
                current_vps->reset_base = data & GCR_CL_RESET_BASE_MSK;
                cpu_set_exception_base(cpu_index + gcr->cluster_id *
                                       gcr->num_core * gcr->num_hart,
                                       get_exception_base(current_vps));
                return;
            }
        }
    }

    switch (addr) {
    case GCR_BASE_OFS:
        update_gcr_base(gcr, data);
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
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void riscv_gcr_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RISCVGCRState *s = RISCV_GCR(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &gcr_ops, s,
                          "riscv-gcr", GCR_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void riscv_gcr_reset(DeviceState *dev)
{
    RISCVGCRState *s = RISCV_GCR(dev);
    int i;

    /* Update cpc_base to gcr_base + 0x8001 to enable cpc automatically. */
    update_cpc_base(s, s->gcr_base + 0x8001);

    for (i = 0; i < s->num_vps; i++) {
        s->vps[i].reset_base = CM_RESET_VEC & GCR_CL_RESET_BASE_MSK;
        cpu_set_exception_base(i, get_exception_base(&s->vps[i]));
    }
}

static const VMStateDescription vmstate_riscv_gcr = {
    .name = "riscv-gcr",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(cpc_base, RISCVGCRState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property riscv_gcr_properties[] = {
    DEFINE_PROP_UINT32("cluster-id", RISCVGCRState, cluster_id, 0),
    DEFINE_PROP_UINT32("num-vp", RISCVGCRState, num_vps, 1),
    DEFINE_PROP_UINT32("num-hart", RISCVGCRState, num_hart, 1),
    DEFINE_PROP_UINT32("num-core", RISCVGCRState, num_core, 1),
    DEFINE_PROP_INT32("gcr-rev", RISCVGCRState, gcr_rev, 0xa00),
    DEFINE_PROP_UINT64("gcr-base", RISCVGCRState, gcr_base, GCR_BASE_ADDR),
    DEFINE_PROP_LINK("cpc", RISCVGCRState, cpc_mr, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void riscv_gcr_realize(DeviceState *dev, Error **errp)
{
    RISCVGCRState *s = RISCV_GCR(dev);

    /* Validate num_vps */
    if (s->num_vps == 0) {
        error_setg(errp, "num-vp must be at least 1");
        return;
    }
    if (s->num_vps > GCR_MAX_VPS) {
        error_setg(errp, "num-vp cannot exceed %d", GCR_MAX_VPS);
        return;
    }

    /* Create local set of registers for each VP */
    s->vps = g_new(RISCVGCRVPState, s->num_vps);
}

static void riscv_gcr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_props(dc, riscv_gcr_properties);
    dc->vmsd = &vmstate_riscv_gcr;
    device_class_set_legacy_reset(dc, riscv_gcr_reset);
    dc->realize = riscv_gcr_realize;
}

static const TypeInfo riscv_gcr_info = {
    .name          = TYPE_RISCV_GCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVGCRState),
    .instance_init = riscv_gcr_init,
    .class_init    = riscv_gcr_class_init,
};

static void riscv_gcr_register_types(void)
{
    type_register_static(&riscv_gcr_info);
}

type_init(riscv_gcr_register_types)
