/*
 *  UniCore32 execution defines
 *
 * Copyright (C) 2010-2011 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UC32_EXEC_H__
#define __UC32_EXEC_H__

#include "config.h"
#include "dyngen-exec.h"

register struct CPUState_UniCore32 *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

static inline int cpu_has_work(CPUState *env)
{
    return env->interrupt_request &
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB);
}

static inline int cpu_halted(CPUState *env)
{
    if (!env->halted) {
        return 0;
    }
    /* An interrupt wakes the CPU even if the I and R ASR bits are
       set.  We use EXITTB to silently wake CPU without causing an
       actual interrupt.  */
    if (cpu_has_work(env)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif /* __UC32_EXEC_H__ */
