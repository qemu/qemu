/*
 *  PowerPC CPU initialization for qemu.
 *
 *  Copyright 2016, David Gibson, Red Hat Inc. <dgibson@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "sysemu/kvm.h"
#include "kvm_ppc.h"
#include "sysemu/cpus.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "cpu-models.h"

typedef struct {
    uint32_t pvr;
    uint64_t pcr;
} CompatInfo;

static const CompatInfo compat_table[] = {
    { /* POWER6, ISA2.05 */
        .pvr = CPU_POWERPC_LOGICAL_2_05,
        .pcr = PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_COMPAT_2_05
               | PCR_TM_DIS | PCR_VSX_DIS,
    },
    { /* POWER7, ISA2.06 */
        .pvr = CPU_POWERPC_LOGICAL_2_06,
        .pcr = PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_TM_DIS,
    },
    {
        .pvr = CPU_POWERPC_LOGICAL_2_06_PLUS,
        .pcr = PCR_COMPAT_2_07 | PCR_COMPAT_2_06 | PCR_TM_DIS,
    },
    { /* POWER8, ISA2.07 */
        .pvr = CPU_POWERPC_LOGICAL_2_07,
        .pcr = PCR_COMPAT_2_07,
    },
};

static const CompatInfo *compat_by_pvr(uint32_t pvr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(compat_table); i++) {
        if (compat_table[i].pvr == pvr) {
            return &compat_table[i];
        }
    }
    return NULL;
}

void ppc_set_compat(PowerPCCPU *cpu, uint32_t compat_pvr, Error **errp)
{
    const CompatInfo *compat = compat_by_pvr(compat_pvr);
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    uint64_t pcr;

    if (!compat_pvr) {
        pcr = 0;
    } else if (!compat) {
        error_setg(errp, "Unknown compatibility PVR 0x%08"PRIx32, compat_pvr);
        return;
    } else {
        pcr = compat->pcr;
    }

    cpu->compat_pvr = compat_pvr;
    env->spr[SPR_PCR] = pcr & pcc->pcr_mask;

    if (kvm_enabled()) {
        int ret = kvmppc_set_compat(cpu, cpu->compat_pvr);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "Unable to set CPU compatibility mode in KVM");
        }
    }
}
