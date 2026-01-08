/*
 * Cluster Power Controller emulation
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * Copyright (c) 2025 MIPS
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: MIPS P8700 documentation
 *            (https://mips.com/products/hardware/p8700/)
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/bitops.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"

#include "hw/misc/riscv_cpc.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/riscv_aclint.h"
#include "hw/core/resettable.h"

static inline uint64_t cpc_vp_run_mask(RISCVCPCState *cpc)
{
    return MAKE_64BIT_MASK(0, cpc->num_vp);
}

static void riscv_cpu_reset_async_work(CPUState *cs, run_on_cpu_data data)
{
    RISCVCPCState *cpc = (RISCVCPCState *) data.host_ptr;
    int i;

    cpu_reset(cs);
    cs->halted = 0;

    /* Find this CPU's index in the CPC's CPU array */
    for (i = 0; i < cpc->num_vp; i++) {
        if (cpc->cpus[i] == cs) {
            cpc->vps_running_mask |= BIT_ULL(i);
            break;
        }
    }
}

static void cpc_run_vp(RISCVCPCState *cpc, uint64_t vps_run_mask)
{
    int vp;

    for (vp = 0; vp < cpc->num_vp; vp++) {
        CPUState *cs = cpc->cpus[vp];

        if (!extract64(vps_run_mask, vp, 1)) {
            continue;
        }

        if (extract64(cpc->vps_running_mask, vp, 1)) {
            continue;
        }

        /*
         * To avoid racing with a CPU we are just kicking off.
         * We do the final bit of preparation for the work in
         * the target CPUs context.
         */
        async_safe_run_on_cpu(cs, riscv_cpu_reset_async_work,
                              RUN_ON_CPU_HOST_PTR(cpc));
    }
}

static void cpc_stop_vp(RISCVCPCState *cpc, uint64_t vps_stop_mask)
{
    int vp;

    for (vp = 0; vp < cpc->num_vp; vp++) {
        CPUState *cs = cpc->cpus[vp];

        if (!extract64(vps_stop_mask, vp, 1)) {
            continue;
        }

        if (!extract64(cpc->vps_running_mask, vp, 1)) {
            continue;
        }

        cpu_interrupt(cs, CPU_INTERRUPT_HALT);
        cpc->vps_running_mask &= ~BIT_ULL(vp);
    }
}

static void cpc_write(void *opaque, hwaddr offset, uint64_t data,
                      unsigned size)
{
    RISCVCPCState *s = opaque;
    int cpu_index, c;

    for (c = 0; c < s->num_core; c++) {
        cpu_index = c * s->num_hart +
                    s->cluster_id * s->num_core * s->num_hart;
        if (offset ==
            CPC_CL_BASE_OFS + CPC_VP_RUN_OFS + c * CPC_CORE_REG_STRIDE) {
            cpc_run_vp(s, (data << cpu_index) & cpc_vp_run_mask(s));
            return;
        }
        if (offset ==
            CPC_CL_BASE_OFS + CPC_VP_STOP_OFS + c * CPC_CORE_REG_STRIDE) {
            cpc_stop_vp(s, (data << cpu_index) & cpc_vp_run_mask(s));
            return;
        }
    }

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return;
}

static uint64_t cpc_read(void *opaque, hwaddr offset, unsigned size)
{
    RISCVCPCState *s = opaque;
    int c;

    for (c = 0; c < s->num_core; c++) {
        if (offset ==
            CPC_CL_BASE_OFS + CPC_STAT_CONF_OFS + c * CPC_CORE_REG_STRIDE) {
            /* Return the state as U6. */
            return CPC_Cx_STAT_CONF_SEQ_STATE_U6;
        }
    }

    switch (offset) {
    case CPC_CM_STAT_CONF_OFS:
        return CPC_Cx_STAT_CONF_SEQ_STATE_U5;
    case CPC_MTIME_REG_OFS:
        return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ,
                        NANOSECONDS_PER_SECOND);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        return 0;
    }
}

static const MemoryRegionOps cpc_ops = {
    .read = cpc_read,
    .write = cpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 8,
    },
};

static void riscv_cpc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    RISCVCPCState *s = RISCV_CPC(obj);
    int i;

    memory_region_init_io(&s->mr, OBJECT(s), &cpc_ops, s, "xmips-cpc",
                          CPC_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->mr);

    /* Allocate CPU array */
    s->cpus = g_new0(CPUState *, CPC_MAX_VPS);

    /* Create link properties for each possible CPU slot */
    for (i = 0; i < CPC_MAX_VPS; i++) {
        char *propname = g_strdup_printf("cpu[%d]", i);
        object_property_add_link(obj, propname, TYPE_CPU,
                                 (Object **)&s->cpus[i],
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
        g_free(propname);
    }
}

static void riscv_cpc_realize(DeviceState *dev, Error **errp)
{
    RISCVCPCState *s = RISCV_CPC(dev);
    int i;

    if (s->vps_start_running_mask & ~cpc_vp_run_mask(s)) {
        error_setg(errp,
                   "incorrect vps-start-running-mask 0x%" PRIx64
                   " for num_vp = %d",
                   s->vps_start_running_mask, s->num_vp);
        return;
    }

    /* Verify that required CPUs have been linked */
    for (i = 0; i < s->num_vp; i++) {
        if (!s->cpus[i]) {
            error_setg(errp, "CPU %d has not been linked", i);
            return;
        }
    }
}

static void riscv_cpc_reset_hold(Object *obj, ResetType type)
{
    RISCVCPCState *s = RISCV_CPC(obj);

    /* Reflect the fact that all VPs are halted on reset */
    s->vps_running_mask = 0;

    /* Put selected VPs into run state */
    cpc_run_vp(s, s->vps_start_running_mask);
}

static const VMStateDescription vmstate_riscv_cpc = {
    .name = "xmips-cpc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(vps_running_mask, RISCVCPCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property riscv_cpc_properties[] = {
    DEFINE_PROP_UINT32("cluster-id", RISCVCPCState, cluster_id, 0x0),
    DEFINE_PROP_UINT32("num-vp", RISCVCPCState, num_vp, 0x1),
    DEFINE_PROP_UINT32("num-hart", RISCVCPCState, num_hart, 0x1),
    DEFINE_PROP_UINT32("num-core", RISCVCPCState, num_core, 0x1),
    DEFINE_PROP_UINT64("vps-start-running-mask", RISCVCPCState,
                       vps_start_running_mask, 0x1),
};

static void riscv_cpc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = riscv_cpc_realize;
    rc->phases.hold = riscv_cpc_reset_hold;
    dc->vmsd = &vmstate_riscv_cpc;
    device_class_set_props(dc, riscv_cpc_properties);
    dc->user_creatable = false;
}

static const TypeInfo riscv_cpc_info = {
    .name          = TYPE_RISCV_CPC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVCPCState),
    .instance_init = riscv_cpc_init,
    .class_init    = riscv_cpc_class_init,
};

static void riscv_cpc_register_types(void)
{
    type_register_static(&riscv_cpc_info);
}

type_init(riscv_cpc_register_types)
