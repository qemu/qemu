/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"
#include <glib.h>

void arm_cpu_sme_finalize(ARMCPU *cpu, Error **errp)
{
    g_assert_not_reached();
}

void arm_cpu_sve_finalize(ARMCPU *cpu, Error **errp)
{
    g_assert_not_reached();
}

void arm_cpu_pauth_finalize(ARMCPU *cpu, Error **errp)
{
    g_assert_not_reached();
}

void arm_cpu_lpa2_finalize(ARMCPU *cpu, Error **errp)
{
    g_assert_not_reached();
}
