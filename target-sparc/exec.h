#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "config.h"
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);

#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define DT0 (env->dt0)
#define DT1 (env->dt1)
#define QT0 (env->qt0)
#define QT1 (env->qt1)

#include "cpu.h"
#include "exec-all.h"

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

/* helper.c */
void cpu_lock(void);
void cpu_unlock(void);
int cpu_sparc_handle_mmu_fault(CPUState *env1, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu);
target_ulong mmu_probe(CPUState *env, target_ulong address, int mmulev);
void dump_mmu(CPUState *env);
void memcpy32(target_ulong *dst, const target_ulong *src);

/* op_helper.c */
void do_interrupt(CPUState *env);

/* cpu-exec.c */
void cpu_loop_exit(void);
int cpu_sparc_signal_handler(int host_signum, void *pinfo, void *puc);

/* sun4m.c */
void cpu_check_irqs(CPUSPARCState *env);

static inline int cpu_halted(CPUState *env1) {
    if (!env1->halted)
        return 0;
    if ((env1->interrupt_request & CPU_INTERRUPT_HARD) && (env1->psret != 0)) {
        env1->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif
