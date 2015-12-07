/*
 * Copyright (C) 2014 - Linaro
 * Author: Rob Herring <rob.herring@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include <cpu.h>
#include <cpu-qom.h>
#include <exec/helper-proto.h>
#include <kvm-consts.h>
#include <sysemu/sysemu.h>
#include "internals.h"

bool arm_is_psci_call(ARMCPU *cpu, int excp_type)
{
    /* Return true if the r0/x0 value indicates a PSCI call and
     * the exception type matches the configured PSCI conduit. This is
     * called before the SMC/HVC instruction is executed, to decide whether
     * we should treat it as a PSCI call or with the architecturally
     * defined behaviour for an SMC or HVC (which might be UNDEF or trap
     * to EL2 or to EL3).
     */
    CPUARMState *env = &cpu->env;
    uint64_t param = is_a64(env) ? env->xregs[0] : env->regs[0];

    switch (excp_type) {
    case EXCP_HVC:
        if (cpu->psci_conduit != QEMU_PSCI_CONDUIT_HVC) {
            return false;
        }
        break;
    case EXCP_SMC:
        if (cpu->psci_conduit != QEMU_PSCI_CONDUIT_SMC) {
            return false;
        }
        break;
    default:
        return false;
    }

    switch (param) {
    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
    case QEMU_PSCI_0_1_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
    case QEMU_PSCI_0_1_FN_CPU_OFF:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
    case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
    case QEMU_PSCI_0_1_FN_MIGRATE:
    case QEMU_PSCI_0_2_FN_MIGRATE:
        return true;
    default:
        return false;
    }
}

static CPUState *get_cpu_by_id(uint64_t id)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        ARMCPU *armcpu = ARM_CPU(cpu);

        if (armcpu->mp_affinity == id) {
            return cpu;
        }
    }

    return NULL;
}

void arm_handle_psci_call(ARMCPU *cpu)
{
    /*
     * This function partially implements the logic for dispatching Power State
     * Coordination Interface (PSCI) calls (as described in ARM DEN 0022B.b),
     * to the extent required for bringing up and taking down secondary cores,
     * and for handling reset and poweroff requests.
     * Additional information about the calling convention used is available in
     * the document 'SMC Calling Convention' (ARM DEN 0028)
     */
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    uint64_t param[4];
    uint64_t context_id, mpidr;
    target_ulong entry;
    int32_t ret = 0;
    int i;

    for (i = 0; i < 4; i++) {
        /*
         * All PSCI functions take explicit 32-bit or native int sized
         * arguments so we can simply zero-extend all arguments regardless
         * of which exact function we are about to call.
         */
        param[i] = is_a64(env) ? env->xregs[i] : env->regs[i];
    }

    if ((param[0] & QEMU_PSCI_0_2_64BIT) && !is_a64(env)) {
        ret = QEMU_PSCI_RET_INVALID_PARAMS;
        goto err;
    }

    switch (param[0]) {
        CPUState *target_cpu_state;
        ARMCPU *target_cpu;
        CPUClass *target_cpu_class;

    case QEMU_PSCI_0_2_FN_PSCI_VERSION:
        ret = QEMU_PSCI_0_2_RET_VERSION_0_2;
        break;
    case QEMU_PSCI_0_2_FN_MIGRATE_INFO_TYPE:
        ret = QEMU_PSCI_0_2_RET_TOS_MIGRATION_NOT_REQUIRED; /* No trusted OS */
        break;
    case QEMU_PSCI_0_2_FN_AFFINITY_INFO:
    case QEMU_PSCI_0_2_FN64_AFFINITY_INFO:
        mpidr = param[1];

        switch (param[2]) {
        case 0:
            target_cpu_state = get_cpu_by_id(mpidr);
            if (!target_cpu_state) {
                ret = QEMU_PSCI_RET_INVALID_PARAMS;
                break;
            }
            target_cpu = ARM_CPU(target_cpu_state);
            ret = target_cpu->powered_off ? 1 : 0;
            break;
        default:
            /* Everything above affinity level 0 is always on. */
            ret = 0;
        }
        break;
    case QEMU_PSCI_0_2_FN_SYSTEM_RESET:
        qemu_system_reset_request();
        /* QEMU reset and shutdown are async requests, but PSCI
         * mandates that we never return from the reset/shutdown
         * call, so power the CPU off now so it doesn't execute
         * anything further.
         */
        goto cpu_off;
    case QEMU_PSCI_0_2_FN_SYSTEM_OFF:
        qemu_system_shutdown_request();
        goto cpu_off;
    case QEMU_PSCI_0_1_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN_CPU_ON:
    case QEMU_PSCI_0_2_FN64_CPU_ON:
        mpidr = param[1];
        entry = param[2];
        context_id = param[3];

        /* change to the cpu we are powering up */
        target_cpu_state = get_cpu_by_id(mpidr);
        if (!target_cpu_state) {
            ret = QEMU_PSCI_RET_INVALID_PARAMS;
            break;
        }
        target_cpu = ARM_CPU(target_cpu_state);
        if (!target_cpu->powered_off) {
            ret = QEMU_PSCI_RET_ALREADY_ON;
            break;
        }
        target_cpu_class = CPU_GET_CLASS(target_cpu);

        /* Initialize the cpu we are turning on */
        cpu_reset(target_cpu_state);
        target_cpu->powered_off = false;
        target_cpu_state->halted = 0;

        /*
         * The PSCI spec mandates that newly brought up CPUs enter the
         * exception level of the caller in the same execution mode as
         * the caller, with context_id in x0/r0, respectively.
         *
         * For now, it is sufficient to assert() that CPUs come out of
         * reset in the same mode as the calling CPU, since we only
         * implement EL1, which means that
         * (a) there is no EL2 for the calling CPU to trap into to change
         *     its state
         * (b) the newly brought up CPU enters EL1 immediately after coming
         *     out of reset in the default state
         */
        assert(is_a64(env) == is_a64(&target_cpu->env));
        if (is_a64(env)) {
            if (entry & 1) {
                ret = QEMU_PSCI_RET_INVALID_PARAMS;
                break;
            }
            target_cpu->env.xregs[0] = context_id;
        } else {
            target_cpu->env.regs[0] = context_id;
            target_cpu->env.thumb = entry & 1;
        }
        target_cpu_class->set_pc(target_cpu_state, entry);

        ret = 0;
        break;
    case QEMU_PSCI_0_1_FN_CPU_OFF:
    case QEMU_PSCI_0_2_FN_CPU_OFF:
        goto cpu_off;
    case QEMU_PSCI_0_1_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN_CPU_SUSPEND:
    case QEMU_PSCI_0_2_FN64_CPU_SUSPEND:
        /* Affinity levels are not supported in QEMU */
        if (param[1] & 0xfffe0000) {
            ret = QEMU_PSCI_RET_INVALID_PARAMS;
            break;
        }
        /* Powerdown is not supported, we always go into WFI */
        if (is_a64(env)) {
            env->xregs[0] = 0;
        } else {
            env->regs[0] = 0;
        }
        helper_wfi(env);
        break;
    case QEMU_PSCI_0_1_FN_MIGRATE:
    case QEMU_PSCI_0_2_FN_MIGRATE:
        ret = QEMU_PSCI_RET_NOT_SUPPORTED;
        break;
    default:
        g_assert_not_reached();
    }

err:
    if (is_a64(env)) {
        env->xregs[0] = ret;
    } else {
        env->regs[0] = ret;
    }
    return;

cpu_off:
    cpu->powered_off = true;
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
    /* notreached */
}
