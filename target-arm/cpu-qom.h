/*
 * QEMU ARM CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */
#ifndef QEMU_ARM_CPU_QOM_H
#define QEMU_ARM_CPU_QOM_H

#include "qom/cpu.h"

#define TYPE_ARM_CPU "arm-cpu"

#define ARM_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(ARMCPUClass, (klass), TYPE_ARM_CPU)
#define ARM_CPU(obj) \
    OBJECT_CHECK(ARMCPU, (obj), TYPE_ARM_CPU)
#define ARM_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ARMCPUClass, (obj), TYPE_ARM_CPU)

/**
 * ARMCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An ARM CPU model.
 */
typedef struct ARMCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} ARMCPUClass;

/**
 * ARMCPU:
 * @env: #CPUARMState
 *
 * An ARM CPU core.
 */
typedef struct ARMCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUARMState env;

    /* Coprocessor information */
    GHashTable *cp_regs;

    /* The instance init functions for implementation-specific subclasses
     * set these fields to specify the implementation-dependent values of
     * various constant registers and reset values of non-constant
     * registers.
     * Some of these might become QOM properties eventually.
     * Field names match the official register names as defined in the
     * ARMv7AR ARM Architecture Reference Manual. A reset_ prefix
     * is used for reset values of non-constant registers; no reset_
     * prefix means a constant register.
     */
    uint32_t midr;
    uint32_t reset_fpsid;
    uint32_t mvfr0;
    uint32_t mvfr1;
    uint32_t ctr;
    uint32_t reset_sctlr;
    uint32_t id_pfr0;
    uint32_t id_pfr1;
    uint32_t id_dfr0;
    uint32_t id_afr0;
    uint32_t id_mmfr0;
    uint32_t id_mmfr1;
    uint32_t id_mmfr2;
    uint32_t id_mmfr3;
    uint32_t id_isar0;
    uint32_t id_isar1;
    uint32_t id_isar2;
    uint32_t id_isar3;
    uint32_t id_isar4;
    uint32_t id_isar5;
    uint32_t clidr;
    /* The elements of this array are the CCSIDR values for each cache,
     * in the order L1DCache, L1ICache, L2DCache, L2ICache, etc.
     */
    uint32_t ccsidr[16];
    uint32_t reset_cbar;
    uint32_t reset_auxcr;
} ARMCPU;

static inline ARMCPU *arm_env_get_cpu(CPUARMState *env)
{
    return ARM_CPU(container_of(env, ARMCPU, env));
}

#define ENV_GET_CPU(e) CPU(arm_env_get_cpu(e))

#define ENV_OFFSET offsetof(ARMCPU, env)

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_arm_cpu;
#endif

void register_cp_regs_for_features(ARMCPU *cpu);

void arm_cpu_do_interrupt(CPUState *cpu);
void arm_v7m_cpu_do_interrupt(CPUState *cpu);

#endif
