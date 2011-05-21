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
static inline bool cpu_has_work(CPUState *env1)
{
    return (env1->interrupt_request & CPU_INTERRUPT_HARD) &&
           cpu_interrupts_enabled(env1);
}


static inline void cpu_pc_from_tb(CPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
    env->npc = tb->cs_base;
}

#endif
