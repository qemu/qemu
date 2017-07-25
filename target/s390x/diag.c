/*
 * S390x DIAG instruction helper functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "hw/watchdog/wdt_diag288.h"
#include "sysemu/cpus.h"
#include "hw/s390x/ipl.h"

static int modified_clear_reset(S390CPU *cpu)
{
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUState *t;

    pause_all_vcpus();
    cpu_synchronize_all_states();
    CPU_FOREACH(t) {
        run_on_cpu(t, s390_do_cpu_full_reset, RUN_ON_CPU_NULL);
    }
    s390_cmma_reset();
    subsystem_reset();
    s390_crypto_reset();
    scc->load_normal(CPU(cpu));
    cpu_synchronize_all_post_reset();
    resume_all_vcpus();
    return 0;
}

static int load_normal_reset(S390CPU *cpu)
{
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);
    CPUState *t;

    pause_all_vcpus();
    cpu_synchronize_all_states();
    CPU_FOREACH(t) {
        run_on_cpu(t, s390_do_cpu_reset, RUN_ON_CPU_NULL);
    }
    s390_cmma_reset();
    subsystem_reset();
    scc->initial_cpu_reset(CPU(cpu));
    scc->load_normal(CPU(cpu));
    cpu_synchronize_all_post_reset();
    resume_all_vcpus();
    return 0;
}

int handle_diag_288(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    uint64_t func = env->regs[r1];
    uint64_t timeout = env->regs[r1 + 1];
    uint64_t action = env->regs[r3];
    Object *obj;
    DIAG288State *diag288;
    DIAG288Class *diag288_class;

    if (r1 % 2 || action != 0) {
        return -1;
    }

    /* Timeout must be more than 15 seconds except for timer deletion */
    if (func != WDT_DIAG288_CANCEL && timeout < 15) {
        return -1;
    }

    obj = object_resolve_path_type("", TYPE_WDT_DIAG288, NULL);
    if (!obj) {
        return -1;
    }

    diag288 = DIAG288(obj);
    diag288_class = DIAG288_GET_CLASS(diag288);
    return diag288_class->handle_timer(diag288, func, timeout);
}

#define DIAG_308_RC_OK              0x0001
#define DIAG_308_RC_NO_CONF         0x0102
#define DIAG_308_RC_INVALID         0x0402

void handle_diag_308(CPUS390XState *env, uint64_t r1, uint64_t r3)
{
    uint64_t addr =  env->regs[r1];
    uint64_t subcode = env->regs[r3];
    IplParameterBlock *iplb;

    if (env->psw.mask & PSW_MASK_PSTATE) {
        program_interrupt(env, PGM_PRIVILEGED, ILEN_AUTO);
        return;
    }

    if ((subcode & ~0x0ffffULL) || (subcode > 6)) {
        program_interrupt(env, PGM_SPECIFICATION, ILEN_AUTO);
        return;
    }

    switch (subcode) {
    case 0:
        modified_clear_reset(s390_env_get_cpu(env));
        if (tcg_enabled()) {
            cpu_loop_exit(CPU(s390_env_get_cpu(env)));
        }
        break;
    case 1:
        load_normal_reset(s390_env_get_cpu(env));
        if (tcg_enabled()) {
            cpu_loop_exit(CPU(s390_env_get_cpu(env)));
        }
        break;
    case 3:
        s390_reipl_request();
        if (tcg_enabled()) {
            cpu_loop_exit(CPU(s390_env_get_cpu(env)));
        }
        break;
    case 5:
        if ((r1 & 1) || (addr & 0x0fffULL)) {
            program_interrupt(env, PGM_SPECIFICATION, ILEN_AUTO);
            return;
        }
        if (!address_space_access_valid(&address_space_memory, addr,
                                        sizeof(IplParameterBlock), false)) {
            program_interrupt(env, PGM_ADDRESSING, ILEN_AUTO);
            return;
        }
        iplb = g_malloc0(sizeof(IplParameterBlock));
        cpu_physical_memory_read(addr, iplb, sizeof(iplb->len));
        if (!iplb_valid_len(iplb)) {
            env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            goto out;
        }

        cpu_physical_memory_read(addr, iplb, be32_to_cpu(iplb->len));

        if (!iplb_valid_ccw(iplb) && !iplb_valid_fcp(iplb)) {
            env->regs[r1 + 1] = DIAG_308_RC_INVALID;
            goto out;
        }

        s390_ipl_update_diag308(iplb);
        env->regs[r1 + 1] = DIAG_308_RC_OK;
out:
        g_free(iplb);
        return;
    case 6:
        if ((r1 & 1) || (addr & 0x0fffULL)) {
            program_interrupt(env, PGM_SPECIFICATION, ILEN_AUTO);
            return;
        }
        if (!address_space_access_valid(&address_space_memory, addr,
                                        sizeof(IplParameterBlock), true)) {
            program_interrupt(env, PGM_ADDRESSING, ILEN_AUTO);
            return;
        }
        iplb = s390_ipl_get_iplb();
        if (iplb) {
            cpu_physical_memory_write(addr, iplb, be32_to_cpu(iplb->len));
            env->regs[r1 + 1] = DIAG_308_RC_OK;
        } else {
            env->regs[r1 + 1] = DIAG_308_RC_NO_CONF;
        }
        return;
    default:
        hw_error("Unhandled diag308 subcode %" PRIx64, subcode);
        break;
    }
}
