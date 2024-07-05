/*
 * ARM MemTag convenience functions.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "mte_user_helper.h"

void arm_set_mte_tcf0(CPUArchState *env, abi_long value)
{
    /*
     * Write PR_MTE_TCF to SCTLR_EL1[TCF0].
     *
     * The kernel has a per-cpu configuration for the sysadmin,
     * /sys/devices/system/cpu/cpu<N>/mte_tcf_preferred,
     * which qemu does not implement.
     *
     * Because there is no performance difference between the modes, and
     * because SYNC is most useful for debugging MTE errors, choose SYNC
     * as the preferred mode.  With this preference, and the way the API
     * uses only two bits, there is no way for the program to select
     * ASYMM mode.
     */
    unsigned tcf = 0;
    if (value & PR_MTE_TCF_SYNC) {
        tcf = 1;
    } else if (value & PR_MTE_TCF_ASYNC) {
        tcf = 2;
    }
    env->cp15.sctlr_el[1] = deposit64(env->cp15.sctlr_el[1], 38, 2, tcf);
}
