/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "target/arm/cpu.h"
#include "target/arm/internals.h"

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

int aarch64_cpu_gdb_read_register(CPUState *cpu, GByteArray *buf, int reg)
{
    g_assert_not_reached();
}

int aarch64_cpu_gdb_write_register(CPUState *cpu, uint8_t *buf, int reg)
{
    g_assert_not_reached();
}

void aarch64_cpu_register_gdb_commands(ARMCPU *cpu, GString *qsupported,
                                       GPtrArray *qtable, GPtrArray *stable)
{
    g_assert_not_reached();
}

void aarch64_cpu_register_gdb_regs_for_features(ARMCPU *cpu)
{
    g_assert_not_reached();
}
