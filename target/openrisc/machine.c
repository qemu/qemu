/*
 * OpenRISC Machine
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
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
#include "cpu.h"
#include "migration/cpu.h"

static const VMStateDescription vmstate_tlb_entry = {
    .name = "tlb_entry",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL(mr, OpenRISCTLBEntry),
        VMSTATE_UINTTL(tr, OpenRISCTLBEntry),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_cpu_tlb = {
    .name = "cpu_tlb",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(itlb, CPUOpenRISCTLBContext, TLB_SIZE, 0,
                             vmstate_tlb_entry, OpenRISCTLBEntry),
        VMSTATE_STRUCT_ARRAY(dtlb, CPUOpenRISCTLBContext, TLB_SIZE, 0,
                             vmstate_tlb_entry, OpenRISCTLBEntry),
        VMSTATE_END_OF_LIST()
    }
};

static int get_sr(QEMUFile *f, void *opaque, size_t size,
                  const VMStateField *field)
{
    CPUOpenRISCState *env = opaque;
    cpu_set_sr(env, qemu_get_be32(f));
    return 0;
}

static int put_sr(QEMUFile *f, void *opaque, size_t size,
                  const VMStateField *field, JSONWriter *vmdesc)
{
    CPUOpenRISCState *env = opaque;
    qemu_put_be32(f, cpu_get_sr(env));
    return 0;
}

static const VMStateInfo vmstate_sr = {
    .name = "sr",
    .get = get_sr,
    .put = put_sr,
};

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 6,
    .minimum_version_id = 6,
    .fields = (VMStateField[]) {
        VMSTATE_UINTTL_2DARRAY(shadow_gpr, CPUOpenRISCState, 16, 32),
        VMSTATE_UINTTL(pc, CPUOpenRISCState),
        VMSTATE_UINTTL(ppc, CPUOpenRISCState),
        VMSTATE_UINTTL(jmp_pc, CPUOpenRISCState),
        VMSTATE_UINTTL(lock_addr, CPUOpenRISCState),
        VMSTATE_UINTTL(lock_value, CPUOpenRISCState),
        VMSTATE_UINTTL(epcr, CPUOpenRISCState),
        VMSTATE_UINTTL(eear, CPUOpenRISCState),

        /* Save the architecture value of the SR, not the internally
           expanded version.  Since this architecture value does not
           exist in memory to be stored, this requires a but of hoop
           jumping.  We want OFFSET=0 so that we effectively pass ENV
           to the helper functions, and we need to fill in the name by
           hand since there's no field of that name.  */
        {
            .name = "sr",
            .version_id = 0,
            .size = sizeof(uint32_t),
            .info = &vmstate_sr,
            .flags = VMS_SINGLE,
            .offset = 0
        },

        VMSTATE_UINT32(vr, CPUOpenRISCState),
        VMSTATE_UINT32(upr, CPUOpenRISCState),
        VMSTATE_UINT32(cpucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(dmmucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(immucfgr, CPUOpenRISCState),
        VMSTATE_UINT32(evbar, CPUOpenRISCState),
        VMSTATE_UINT32(pmr, CPUOpenRISCState),
        VMSTATE_UINT32(esr, CPUOpenRISCState),
        VMSTATE_UINT32(fpcsr, CPUOpenRISCState),
        VMSTATE_UINT64(mac, CPUOpenRISCState),

        VMSTATE_STRUCT(tlb, CPUOpenRISCState, 1,
                       vmstate_cpu_tlb, CPUOpenRISCTLBContext),

        VMSTATE_TIMER_PTR(timer, CPUOpenRISCState),
        VMSTATE_UINT32(ttmr, CPUOpenRISCState),

        VMSTATE_UINT32(picmr, CPUOpenRISCState),
        VMSTATE_UINT32(picsr, CPUOpenRISCState),

        VMSTATE_END_OF_LIST()
    }
};

static int cpu_post_load(void *opaque, int version_id)
{
    OpenRISCCPU *cpu = opaque;
    CPUOpenRISCState *env = &cpu->env;

    /* Update env->fp_status to match env->fpcsr.  */
    cpu_set_fpcsr(env, env->fpcsr);
    return 0;
}

const VMStateDescription vmstate_openrisc_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = cpu_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_CPU(),
        VMSTATE_STRUCT(env, OpenRISCCPU, 1, vmstate_env, CPUOpenRISCState),
        VMSTATE_END_OF_LIST()
    }
};
