#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "config.h"
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

/* op_helper.c */
void do_interrupt(CPUState *env);

static inline int cpu_has_work(CPUState *env1)
{
    return (env1->interrupt_request & CPU_INTERRUPT_HARD) &&
           cpu_interrupts_enabled(env1);
}


static inline int cpu_halted(CPUState *env1) {
    if (!env1->halted)
        return 0;
    if (cpu_has_work(env1)) {
        env1->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
    env->npc = tb->cs_base;
}

#endif
