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
    }

    return 0;
}

const VMStateDescription vmstate_s390_cpu = {
    .name = "cpu",
    .post_load = cpu_post_load,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields      = (VMStateField[]) {
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
        VMSTATE_UINT64_ARRAY(env.regs, S390CPU, 16),
        VMSTATE_UINT64(env.psw.mask, S390CPU),
        VMSTATE_UINT64(env.psw.addr, S390CPU),
        VMSTATE_UINT64(env.psa, S390CPU),
        VMSTATE_UINT32(env.fpc, S390CPU),
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
        VMSTATE_END_OF_LIST()
     },
};
