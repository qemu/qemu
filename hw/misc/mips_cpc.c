/*
 * Cluster Power Controller emulation
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"

#include "hw/misc/mips_cpc.h"
#include "hw/qdev-properties.h"

static inline uint64_t cpc_vp_run_mask(MIPSCPCState *cpc)
{
    return (1ULL << cpc->num_vp) - 1;
}

static void mips_cpu_reset_async_work(CPUState *cs, run_on_cpu_data data)
{
    MIPSCPCState *cpc = (MIPSCPCState *) data.host_ptr;

    cpu_reset(cs);
    cs->halted = 0;
    cpc->vp_running |= 1ULL << cs->cpu_index;
}

static void cpc_run_vp(MIPSCPCState *cpc, uint64_t vp_run)
{
    CPUState *cs = first_cpu;

    CPU_FOREACH(cs) {
        uint64_t i = 1ULL << cs->cpu_index;
        if (i & vp_run & ~cpc->vp_running) {
            /*
             * To avoid racing with a CPU we are just kicking off.
             * We do the final bit of preparation for the work in
             * the target CPUs context.
             */
            async_safe_run_on_cpu(cs, mips_cpu_reset_async_work,
                                  RUN_ON_CPU_HOST_PTR(cpc));
        }
    }
}

static void cpc_stop_vp(MIPSCPCState *cpc, uint64_t vp_stop)
{
    CPUState *cs = first_cpu;

    CPU_FOREACH(cs) {
        uint64_t i = 1ULL << cs->cpu_index;
        if (i & vp_stop & cpc->vp_running) {
            cpu_interrupt(cs, CPU_INTERRUPT_HALT);
            cpc->vp_running &= ~i;
        }
    }
}

static void cpc_write(void *opaque, hwaddr offset, uint64_t data,
                      unsigned size)
{
    MIPSCPCState *s = opaque;

    switch (offset) {
    case CPC_CL_BASE_OFS + CPC_VP_RUN_OFS:
    case CPC_CO_BASE_OFS + CPC_VP_RUN_OFS:
        cpc_run_vp(s, data & cpc_vp_run_mask(s));
        break;
    case CPC_CL_BASE_OFS + CPC_VP_STOP_OFS:
    case CPC_CO_BASE_OFS + CPC_VP_STOP_OFS:
        cpc_stop_vp(s, data & cpc_vp_run_mask(s));
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        break;
    }

    return;
}

static uint64_t cpc_read(void *opaque, hwaddr offset, unsigned size)
{
    MIPSCPCState *s = opaque;

    switch (offset) {
    case CPC_CL_BASE_OFS + CPC_VP_RUNNING_OFS:
    case CPC_CO_BASE_OFS + CPC_VP_RUNNING_OFS:
        return s->vp_running;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: Bad offset 0x%x\n",  __func__, (int)offset);
        return 0;
    }
}

static const MemoryRegionOps cpc_ops = {
    .read = cpc_read,
    .write = cpc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void mips_cpc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MIPSCPCState *s = MIPS_CPC(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &cpc_ops, s, "mips-cpc",
                          CPC_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->mr);
}

static void mips_cpc_realize(DeviceState *dev, Error **errp)
{
    MIPSCPCState *s = MIPS_CPC(dev);

    if (s->vp_start_running > cpc_vp_run_mask(s)) {
        error_setg(errp,
                   "incorrect vp_start_running 0x%" PRIx64 " for num_vp = %d",
                   s->vp_running, s->num_vp);
        return;
    }
}

static void mips_cpc_reset(DeviceState *dev)
{
    MIPSCPCState *s = MIPS_CPC(dev);

    /* Reflect the fact that all VPs are halted on reset */
    s->vp_running = 0;

    /* Put selected VPs into run state */
    cpc_run_vp(s, s->vp_start_running);
}

static const VMStateDescription vmstate_mips_cpc = {
    .name = "mips-cpc",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(vp_running, MIPSCPCState),
        VMSTATE_END_OF_LIST()
    },
};

static Property mips_cpc_properties[] = {
    DEFINE_PROP_UINT32("num-vp", MIPSCPCState, num_vp, 0x1),
    DEFINE_PROP_UINT64("vp-start-running", MIPSCPCState, vp_start_running, 0x1),
    DEFINE_PROP_END_OF_LIST(),
};

static void mips_cpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mips_cpc_realize;
    dc->reset = mips_cpc_reset;
    dc->vmsd = &vmstate_mips_cpc;
    device_class_set_props(dc, mips_cpc_properties);
}

static const TypeInfo mips_cpc_info = {
    .name          = TYPE_MIPS_CPC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MIPSCPCState),
    .instance_init = mips_cpc_init,
    .class_init    = mips_cpc_class_init,
};

static void mips_cpc_register_types(void)
{
    type_register_static(&mips_cpc_info);
}

type_init(mips_cpc_register_types)
