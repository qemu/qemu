#ifndef RISCV_TARGET_CPU_H
#define RISCV_TARGET_CPU_H

static inline void cpu_clone_regs_child(CPURISCVState *env, target_ulong newsp,
                                        unsigned flags)
{
    if (newsp) {
        env->gpr[xSP] = newsp;
    }

    env->gpr[xA0] = 0;
}

static inline void cpu_clone_regs_parent(CPURISCVState *env, unsigned flags)
{
}

static inline void cpu_set_tls(CPURISCVState *env, target_ulong newtls)
{
    env->gpr[xTP] = newtls;
}

static inline abi_ulong get_sp_from_cpustate(CPURISCVState *state)
{
   return state->gpr[xSP];
}
#endif
