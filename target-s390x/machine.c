/*
 * S390x machine definitions and functions
 *
 * Copyright IBM Corp. 2014
 *
 * Authors:
 *   Thomas Huth <thuth@linux.vnet.ibm.com>
 *   Christian Borntraeger <borntraeger@de.ibm.com>
 *   Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#include "hw/hw.h"
#include "cpu.h"
#include "sysemu/kvm.h"

static int cpu_post_load(void *opaque, int version_id)
{
    S390CPU *cpu = opaque;

    /*
     * As the cpu state is pushed to kvm via kvm_set_mp_state rather
     * than via cpu_synchronize_state, we need update kvm here.
     */
    if (kvm_enabled()) {
        kvm_s390_set_cpu_state(cpu, cpu->env.cpu_state);
        return kvm_s390_vcpu_interrupt_post_load(cpu);
    }

    return 0;
}
static void cpu_pre_save(void *opaque)
{
    S390CPU *cpu = opaque;

    if (kvm_enabled()) {
        kvm_s390_vcpu_interrupt_pre_save(cpu);
    }
}

const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(env.fregs[0].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[1].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[2].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[3].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[4].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[5].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[6].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[7].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[8].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[9].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[10].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[11].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[12].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[13].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[14].ll, S390CPU),
        VMSTATE_UINT64(env.fregs[15].ll, S390CPU),
        VMSTATE_UINT32(env.fpc, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static inline bool fpu_needed(void *opaque)
{
    return true;
}

const VMStateDescription vmstate_s390_cpu = {
    .name = "cpu",
    .post_load = cpu_post_load,
    .pre_save = cpu_pre_save,
    .version_id = 4,
    .minimum_version_id = 3,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.regs, S390CPU, 16),
        VMSTATE_UINT64(env.psw.mask, S390CPU),
        VMSTATE_UINT64(env.psw.addr, S390CPU),
        VMSTATE_UINT64(env.psa, S390CPU),
        VMSTATE_UINT32(env.todpr, S390CPU),
        VMSTATE_UINT64(env.pfault_token, S390CPU),
        VMSTATE_UINT64(env.pfault_compare, S390CPU),
        VMSTATE_UINT64(env.pfault_select, S390CPU),
        VMSTATE_UINT64(env.cputm, S390CPU),
        VMSTATE_UINT64(env.ckc, S390CPU),
        VMSTATE_UINT64(env.gbea, S390CPU),
        VMSTATE_UINT64(env.pp, S390CPU),
        VMSTATE_UINT32_ARRAY(env.aregs, S390CPU, 16),
        VMSTATE_UINT64_ARRAY(env.cregs, S390CPU, 16),
        VMSTATE_UINT8(env.cpu_state, S390CPU),
        VMSTATE_UINT8(env.sigp_order, S390CPU),
        VMSTATE_UINT32_V(irqstate_saved_size, S390CPU, 4),
        VMSTATE_VBUFFER_UINT32(irqstate, S390CPU, 4, NULL, 0,
                               irqstate_saved_size),
        VMSTATE_END_OF_LIST()
     },
    .subsections = (VMStateSubsection[]) {
        {
            .vmsd = &vmstate_fpu,
            .needed = fpu_needed,
        } , {
            /* empty */
        }
    },
};
