/*
 * S390x machine definitions and functions
 *
 * Copyright IBM Corp. 2014, 2018
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "kvm/kvm_s390x.h"
#include "migration/vmstate.h"
#include "tcg/tcg_s390x.h"
#include "system/kvm.h"
#include "system/tcg.h"

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

    if (tcg_enabled()) {
        /* Rearm the CKC timer if necessary */
        tcg_s390_tod_updated(CPU(cpu), RUN_ON_CPU_NULL);
    }

    return 0;
}

static int cpu_pre_save(void *opaque)
{
    S390CPU *cpu = opaque;

    if (kvm_enabled()) {
        kvm_s390_vcpu_interrupt_pre_save(cpu);
    }

    return 0;
}

static inline bool fpu_needed(void *opaque)
{
    /* This looks odd, but we might want to NOT transfer fprs in the future */
    return true;
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.vregs[0][0], S390CPU),
        VMSTATE_UINT64(env.vregs[1][0], S390CPU),
        VMSTATE_UINT64(env.vregs[2][0], S390CPU),
        VMSTATE_UINT64(env.vregs[3][0], S390CPU),
        VMSTATE_UINT64(env.vregs[4][0], S390CPU),
        VMSTATE_UINT64(env.vregs[5][0], S390CPU),
        VMSTATE_UINT64(env.vregs[6][0], S390CPU),
        VMSTATE_UINT64(env.vregs[7][0], S390CPU),
        VMSTATE_UINT64(env.vregs[8][0], S390CPU),
        VMSTATE_UINT64(env.vregs[9][0], S390CPU),
        VMSTATE_UINT64(env.vregs[10][0], S390CPU),
        VMSTATE_UINT64(env.vregs[11][0], S390CPU),
        VMSTATE_UINT64(env.vregs[12][0], S390CPU),
        VMSTATE_UINT64(env.vregs[13][0], S390CPU),
        VMSTATE_UINT64(env.vregs[14][0], S390CPU),
        VMSTATE_UINT64(env.vregs[15][0], S390CPU),
        VMSTATE_UINT32(env.fpc, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool vregs_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_VECTOR);
}

static const VMStateDescription vmstate_vregs = {
    .name = "cpu/vregs",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vregs_needed,
    .fields = (const VMStateField[]) {
        /* vregs[0][0] -> vregs[15][0] and fregs are overlays */
        VMSTATE_UINT64(env.vregs[16][0], S390CPU),
        VMSTATE_UINT64(env.vregs[17][0], S390CPU),
        VMSTATE_UINT64(env.vregs[18][0], S390CPU),
        VMSTATE_UINT64(env.vregs[19][0], S390CPU),
        VMSTATE_UINT64(env.vregs[20][0], S390CPU),
        VMSTATE_UINT64(env.vregs[21][0], S390CPU),
        VMSTATE_UINT64(env.vregs[22][0], S390CPU),
        VMSTATE_UINT64(env.vregs[23][0], S390CPU),
        VMSTATE_UINT64(env.vregs[24][0], S390CPU),
        VMSTATE_UINT64(env.vregs[25][0], S390CPU),
        VMSTATE_UINT64(env.vregs[26][0], S390CPU),
        VMSTATE_UINT64(env.vregs[27][0], S390CPU),
        VMSTATE_UINT64(env.vregs[28][0], S390CPU),
        VMSTATE_UINT64(env.vregs[29][0], S390CPU),
        VMSTATE_UINT64(env.vregs[30][0], S390CPU),
        VMSTATE_UINT64(env.vregs[31][0], S390CPU),
        VMSTATE_UINT64(env.vregs[0][1], S390CPU),
        VMSTATE_UINT64(env.vregs[1][1], S390CPU),
        VMSTATE_UINT64(env.vregs[2][1], S390CPU),
        VMSTATE_UINT64(env.vregs[3][1], S390CPU),
        VMSTATE_UINT64(env.vregs[4][1], S390CPU),
        VMSTATE_UINT64(env.vregs[5][1], S390CPU),
        VMSTATE_UINT64(env.vregs[6][1], S390CPU),
        VMSTATE_UINT64(env.vregs[7][1], S390CPU),
        VMSTATE_UINT64(env.vregs[8][1], S390CPU),
        VMSTATE_UINT64(env.vregs[9][1], S390CPU),
        VMSTATE_UINT64(env.vregs[10][1], S390CPU),
        VMSTATE_UINT64(env.vregs[11][1], S390CPU),
        VMSTATE_UINT64(env.vregs[12][1], S390CPU),
        VMSTATE_UINT64(env.vregs[13][1], S390CPU),
        VMSTATE_UINT64(env.vregs[14][1], S390CPU),
        VMSTATE_UINT64(env.vregs[15][1], S390CPU),
        VMSTATE_UINT64(env.vregs[16][1], S390CPU),
        VMSTATE_UINT64(env.vregs[17][1], S390CPU),
        VMSTATE_UINT64(env.vregs[18][1], S390CPU),
        VMSTATE_UINT64(env.vregs[19][1], S390CPU),
        VMSTATE_UINT64(env.vregs[20][1], S390CPU),
        VMSTATE_UINT64(env.vregs[21][1], S390CPU),
        VMSTATE_UINT64(env.vregs[22][1], S390CPU),
        VMSTATE_UINT64(env.vregs[23][1], S390CPU),
        VMSTATE_UINT64(env.vregs[24][1], S390CPU),
        VMSTATE_UINT64(env.vregs[25][1], S390CPU),
        VMSTATE_UINT64(env.vregs[26][1], S390CPU),
        VMSTATE_UINT64(env.vregs[27][1], S390CPU),
        VMSTATE_UINT64(env.vregs[28][1], S390CPU),
        VMSTATE_UINT64(env.vregs[29][1], S390CPU),
        VMSTATE_UINT64(env.vregs[30][1], S390CPU),
        VMSTATE_UINT64(env.vregs[31][1], S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool riccb_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_RUNTIME_INSTRUMENTATION);
}

static const VMStateDescription vmstate_riccb = {
    .name = "cpu/riccb",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = riccb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(env.riccb, S390CPU, 64),
        VMSTATE_END_OF_LIST()
    }
};

static bool exval_needed(void *opaque)
{
    S390CPU *cpu = opaque;
    return cpu->env.ex_value != 0;
}

static const VMStateDescription vmstate_exval = {
    .name = "cpu/exval",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = exval_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.ex_value, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool gscb_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_GUARDED_STORAGE);
}

static const VMStateDescription vmstate_gscb = {
    .name = "cpu/gscb",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = gscb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64_ARRAY(env.gscb, S390CPU, 4),
        VMSTATE_END_OF_LIST()
        }
};

static bool bpbc_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_BPB);
}

static const VMStateDescription vmstate_bpbc = {
    .name = "cpu/bpbc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = bpbc_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(env.bpbc, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool etoken_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_ETOKEN);
}

static const VMStateDescription vmstate_etoken = {
    .name = "cpu/etoken",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = etoken_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.etoken, S390CPU),
        VMSTATE_UINT64(env.etoken_extension, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

static bool diag318_needed(void *opaque)
{
    return s390_has_feat(S390_FEAT_DIAG_318);
}

static const VMStateDescription vmstate_diag318 = {
    .name = "cpu/diag318",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = diag318_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.diag318_info, S390CPU),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_s390_cpu = {
    .name = "cpu",
    .post_load = cpu_post_load,
    .pre_save = cpu_pre_save,
    .version_id = 4,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
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
        VMSTATE_VBUFFER_UINT32(irqstate, S390CPU, 4, NULL,
                               irqstate_saved_size),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_fpu,
        &vmstate_vregs,
        &vmstate_riccb,
        &vmstate_exval,
        &vmstate_gscb,
        &vmstate_bpbc,
        &vmstate_etoken,
        &vmstate_diag318,
        NULL
    },
};
