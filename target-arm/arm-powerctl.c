/*
 * QEMU support -- ARM Power Control specific functions.
 *
 * Copyright (c) 2016 Jean-Christophe Dubois
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <cpu.h>
#include <cpu-qom.h>
#include "internals.h"
#include "arm-powerctl.h"

#ifndef DEBUG_ARM_POWERCTL
#define DEBUG_ARM_POWERCTL 0
#endif

#define DPRINTF(fmt, args...) \
    do { \
        if (DEBUG_ARM_POWERCTL) { \
            fprintf(stderr, "[ARM]%s: " fmt , __func__, ##args); \
        } \
    } while (0)

CPUState *arm_get_cpu_by_id(uint64_t id)
{
    CPUState *cpu;

    DPRINTF("cpu %" PRId64 "\n", id);

    CPU_FOREACH(cpu) {
        ARMCPU *armcpu = ARM_CPU(cpu);

        if (armcpu->mp_affinity == id) {
            return cpu;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "[ARM]%s: Requesting unknown CPU %" PRId64 "\n",
                  __func__, id);

    return NULL;
}

int arm_set_cpu_on(uint64_t cpuid, uint64_t entry, uint64_t context_id,
                   uint32_t target_el, bool target_aa64)
{
    CPUState *target_cpu_state;
    ARMCPU *target_cpu;

    DPRINTF("cpu %" PRId64 " (EL %d, %s) @ 0x%" PRIx64 " with R0 = 0x%" PRIx64
            "\n", cpuid, target_el, target_aa64 ? "aarch64" : "aarch32", entry,
            context_id);

    /* requested EL level need to be in the 1 to 3 range */
    assert((target_el > 0) && (target_el < 4));

    if (target_aa64 && (entry & 3)) {
        /*
         * if we are booting in AArch64 mode then "entry" needs to be 4 bytes
         * aligned.
         */
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }

    /* Retrieve the cpu we are powering up */
    target_cpu_state = arm_get_cpu_by_id(cpuid);
    if (!target_cpu_state) {
        /* The cpu was not found */
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }

    target_cpu = ARM_CPU(target_cpu_state);
    if (!target_cpu->powered_off) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[ARM]%s: CPU %" PRId64 " is already on\n",
                      __func__, cpuid);
        return QEMU_ARM_POWERCTL_ALREADY_ON;
    }

    /*
     * The newly brought CPU is requested to enter the exception level
     * "target_el" and be in the requested mode (AArch64 or AArch32).
     */

    if (((target_el == 3) && !arm_feature(&target_cpu->env, ARM_FEATURE_EL3)) ||
        ((target_el == 2) && !arm_feature(&target_cpu->env, ARM_FEATURE_EL2))) {
        /*
         * The CPU does not support requested level
         */
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }

    if (!target_aa64 && arm_feature(&target_cpu->env, ARM_FEATURE_AARCH64)) {
        /*
         * For now we don't support booting an AArch64 CPU in AArch32 mode
         * TODO: We should add this support later
         */
        qemu_log_mask(LOG_UNIMP,
                      "[ARM]%s: Starting AArch64 CPU %" PRId64
                      " in AArch32 mode is not supported yet\n",
                      __func__, cpuid);
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }

    /* Initialize the cpu we are turning on */
    cpu_reset(target_cpu_state);
    target_cpu->powered_off = false;
    target_cpu_state->halted = 0;

    if (target_aa64) {
        if ((target_el < 3) && arm_feature(&target_cpu->env, ARM_FEATURE_EL3)) {
            /*
             * As target mode is AArch64, we need to set lower
             * exception level (the requested level 2) to AArch64
             */
            target_cpu->env.cp15.scr_el3 |= SCR_RW;
        }

        if ((target_el < 2) && arm_feature(&target_cpu->env, ARM_FEATURE_EL2)) {
            /*
             * As target mode is AArch64, we need to set lower
             * exception level (the requested level 1) to AArch64
             */
            target_cpu->env.cp15.hcr_el2 |= HCR_RW;
        }

        target_cpu->env.pstate = aarch64_pstate_mode(target_el, true);
    } else {
        /* We are requested to boot in AArch32 mode */
        static uint32_t mode_for_el[] = { 0,
                                          ARM_CPU_MODE_SVC,
                                          ARM_CPU_MODE_HYP,
                                          ARM_CPU_MODE_SVC };

        cpsr_write(&target_cpu->env, mode_for_el[target_el], CPSR_M,
                   CPSRWriteRaw);
    }

    if (target_el == 3) {
        /* Processor is in secure mode */
        target_cpu->env.cp15.scr_el3 &= ~SCR_NS;
    } else {
        /* Processor is not in secure mode */
        target_cpu->env.cp15.scr_el3 |= SCR_NS;
    }

    /* We check if the started CPU is now at the correct level */
    assert(target_el == arm_current_el(&target_cpu->env));

    if (target_aa64) {
        target_cpu->env.xregs[0] = context_id;
        target_cpu->env.thumb = false;
    } else {
        target_cpu->env.regs[0] = context_id;
        target_cpu->env.thumb = entry & 1;
        entry &= 0xfffffffe;
    }

    /* Start the new CPU at the requested address */
    cpu_set_pc(target_cpu_state, entry);

    /* We are good to go */
    return QEMU_ARM_POWERCTL_RET_SUCCESS;
}

int arm_set_cpu_off(uint64_t cpuid)
{
    CPUState *target_cpu_state;
    ARMCPU *target_cpu;

    DPRINTF("cpu %" PRId64 "\n", cpuid);

    /* change to the cpu we are powering up */
    target_cpu_state = arm_get_cpu_by_id(cpuid);
    if (!target_cpu_state) {
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }
    target_cpu = ARM_CPU(target_cpu_state);
    if (target_cpu->powered_off) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[ARM]%s: CPU %" PRId64 " is already off\n",
                      __func__, cpuid);
        return QEMU_ARM_POWERCTL_IS_OFF;
    }

    target_cpu->powered_off = true;
    target_cpu_state->halted = 1;
    target_cpu_state->exception_index = EXCP_HLT;
    cpu_loop_exit(target_cpu_state);
    /* notreached */

    return QEMU_ARM_POWERCTL_RET_SUCCESS;
}

int arm_reset_cpu(uint64_t cpuid)
{
    CPUState *target_cpu_state;
    ARMCPU *target_cpu;

    DPRINTF("cpu %" PRId64 "\n", cpuid);

    /* change to the cpu we are resetting */
    target_cpu_state = arm_get_cpu_by_id(cpuid);
    if (!target_cpu_state) {
        return QEMU_ARM_POWERCTL_INVALID_PARAM;
    }
    target_cpu = ARM_CPU(target_cpu_state);
    if (target_cpu->powered_off) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[ARM]%s: CPU %" PRId64 " is off\n",
                      __func__, cpuid);
        return QEMU_ARM_POWERCTL_IS_OFF;
    }

    /* Reset the cpu */
    cpu_reset(target_cpu_state);

    return QEMU_ARM_POWERCTL_RET_SUCCESS;
}
