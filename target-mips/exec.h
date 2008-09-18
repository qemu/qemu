#if !defined(__QEMU_MIPS_EXEC_H__)
#define __QEMU_MIPS_EXEC_H__

//#define DEBUG_OP

#include "config.h"
#include "mips-defs.h"
#include "dyngen-exec.h"
#include "cpu-defs.h"

register struct CPUMIPSState *env asm(AREG0);

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

void do_mtc0_status_debug(uint32_t old, uint32_t val);
void do_mtc0_status_irqraise_debug(void);
void dump_fpu(CPUState *env);
void fpu_dump_state(CPUState *env, FILE *f,
                    int (*fpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags);

int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu);
void do_interrupt (CPUState *env);
void r4k_invalidate_tlb (CPUState *env, int idx, int use_extra);

void cpu_loop_exit(void);
void do_raise_exception_err (uint32_t exception, int error_code);
void do_raise_exception (uint32_t exception);

uint32_t cpu_mips_get_random (CPUState *env);
uint32_t cpu_mips_get_count (CPUState *env);
void cpu_mips_store_count (CPUState *env, uint32_t value);
void cpu_mips_store_compare (CPUState *env, uint32_t value);
void cpu_mips_start_count(CPUState *env);
void cpu_mips_stop_count(CPUState *env);
void cpu_mips_update_irq (CPUState *env);
void cpu_mips_clock_init (CPUState *env);
void cpu_mips_tlb_flush (CPUState *env, int flush_global);

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

static inline int cpu_halted(CPUState *env)
{
    if (!env->halted)
        return 0;
    if (env->interrupt_request &
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_TIMER)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

static inline void compute_hflags(CPUState *env)
{
    env->hflags &= ~(MIPS_HFLAG_COP1X | MIPS_HFLAG_64 | MIPS_HFLAG_CP0 |
                     MIPS_HFLAG_F64 | MIPS_HFLAG_FPU | MIPS_HFLAG_KSU);
    if (!(env->CP0_Status & (1 << CP0St_EXL)) &&
        !(env->CP0_Status & (1 << CP0St_ERL)) &&
        !(env->hflags & MIPS_HFLAG_DM)) {
        env->hflags |= (env->CP0_Status >> CP0St_KSU) & MIPS_HFLAG_KSU;
    }
#if defined(TARGET_MIPS64)
    if (((env->hflags & MIPS_HFLAG_KSU) != MIPS_HFLAG_UM) ||
        (env->CP0_Status & (1 << CP0St_PX)) ||
        (env->CP0_Status & (1 << CP0St_UX)))
        env->hflags |= MIPS_HFLAG_64;
#endif
    if ((env->CP0_Status & (1 << CP0St_CU0)) ||
        !(env->hflags & MIPS_HFLAG_KSU))
        env->hflags |= MIPS_HFLAG_CP0;
    if (env->CP0_Status & (1 << CP0St_CU1))
        env->hflags |= MIPS_HFLAG_FPU;
    if (env->CP0_Status & (1 << CP0St_FR))
        env->hflags |= MIPS_HFLAG_F64;
    if (env->insn_flags & ISA_MIPS32R2) {
        if (env->active_fpu.fcr0 & (1 << FCR0_F64))
            env->hflags |= MIPS_HFLAG_COP1X;
    } else if (env->insn_flags & ISA_MIPS32) {
        if (env->hflags & MIPS_HFLAG_64)
            env->hflags |= MIPS_HFLAG_COP1X;
    } else if (env->insn_flags & ISA_MIPS4) {
        /* All supported MIPS IV CPUs use the XX (CU3) to enable
           and disable the MIPS IV extensions to the MIPS III ISA.
           Some other MIPS IV CPUs ignore the bit, so the check here
           would be too restrictive for them.  */
        if (env->CP0_Status & (1 << CP0St_CU3))
            env->hflags |= MIPS_HFLAG_COP1X;
    }
}

#endif /* !defined(__QEMU_MIPS_EXEC_H__) */
