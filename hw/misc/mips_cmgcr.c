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
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "hw/misc/mips_cmgcr.h"
#include "hw/misc/mips_cpc.h"

static inline bool is_cpc_connected(MIPSGCRState *s)
{
    return s->cpc_mr != NULL;
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

/* Read GCR registers */
static uint64_t gcr_read(void *opaque, hwaddr addr, unsigned size)
{
    MIPSGCRState *gcr = (MIPSGCRState *) opaque;

    switch (addr) {
    /* Global Control Block Register */
    case GCR_CONFIG_OFS:
        /* Set PCORES to 0 */
        return 0;
    case GCR_BASE_OFS:
        return gcr->gcr_base;
    case GCR_REV_OFS:
        return gcr->gcr_rev;
    case GCR_CPC_BASE_OFS:
        return gcr->cpc_base;
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
    case MIPS_CLCB_OFS + GCR_CL_OTHER_OFS:
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "Read %d bytes at GCR offset 0x%" HWADDR_PRIx
                      "\n", size, addr);
        return 0;
    }
    return 0;
}

/* Write GCR registers */
static void gcr_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    MIPSGCRState *gcr = (MIPSGCRState *)opaque;

    switch (addr) {
    case GCR_CPC_BASE_OFS:
        update_cpc_base(gcr, data);
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

    object_property_add_link(obj, "cpc", TYPE_MEMORY_REGION,
                             (Object **)&s->cpc_mr,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);

    memory_region_init_io(&s->iomem, OBJECT(s), &gcr_ops, s,
                          "mips-gcr", GCR_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void mips_gcr_reset(DeviceState *dev)
{
    MIPSGCRState *s = MIPS_GCR(dev);

    update_cpc_base(s, 0);
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
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_gcr_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->props = mips_gcr_properties;
    dc->vmsd = &vmstate_mips_gcr;
    dc->reset = mips_gcr_reset;
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
