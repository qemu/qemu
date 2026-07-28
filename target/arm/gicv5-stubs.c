/*
 * QEMU ARM stubs for GICv5 TCG helper functions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "internals.h"

void define_gicv5_cpuif_regs(ARMCPU *cpu)
{
    g_assert_not_reached();
}

void gicv5_update_ppi_state(CPUARMState *env, int ppi, bool level)
{
}
