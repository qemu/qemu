/* SPDX-License-Identifier: GPL-2.0-or-later */

/* QEMU ARM CPU - user-mode emulation stubs for EL2 interrupts
 *
 * These should not really be needed, but CP registers for EL2
 * are not elided by user-mode emulation and they call these
 * functions.  Leave them as stubs until it's cleaned up.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"

void arm_cpu_update_virq(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void arm_cpu_update_vfiq(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void arm_cpu_update_vinmi(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void arm_cpu_update_vfnmi(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void arm_cpu_update_vserr(ARMCPU *cpu)
{
    g_assert_not_reached();
}
