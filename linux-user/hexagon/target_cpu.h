/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */

#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUHexagonState *env, target_ulong newsp)
{
    if (newsp) {
        env->gpr[HEX_REG_SP] = newsp;
    }
    env->gpr[0] = 0;
}

static inline void cpu_set_tls(CPUHexagonState *env, target_ulong newtls)
{
    env->gpr[HEX_REG_UGP] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPUHexagonState *state)
{
    return state->gpr[HEX_REG_SP];
}

#endif
