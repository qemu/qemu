#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "config.h"
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);

#ifdef TARGET_SPARC64
#define T0 (env->t0)
#define T1 (env->t1)
#define T2 (env->t2)
#define REGWPTR env->regwptr
#else
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);

#undef REG_REGWPTR // Broken
#ifdef REG_REGWPTR
#if defined(__sparc__)
register uint32_t *REGWPTR asm(AREG4);
#else
register uint32_t *REGWPTR asm(AREG3);
#endif
#define reg_REGWPTR

#ifdef AREG4
register uint32_t T2 asm(AREG4);
#define reg_T2
#else
#define T2 (env->t2)
#endif

#else
#define REGWPTR env->regwptr
register uint32_t T2 asm(AREG3);
#endif
#define reg_T2
#endif

#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define DT0 (env->dt0)
#define DT1 (env->dt1)
#if defined(CONFIG_USER_ONLY)
#define QT0 (env->qt0)
#define QT1 (env->qt1)
#endif

#include "cpu.h"
#include "exec-all.h"

void cpu_lock(void);
void cpu_unlock(void);
void cpu_loop_exit(void);
void helper_flush(target_ulong addr);
void helper_ld_asi(int asi, int size, int sign);
void helper_st_asi(int asi, int size);
void helper_ldf_asi(int asi, int size, int rd);
void helper_stf_asi(int asi, int size, int rd);
void helper_rett(void);
void helper_ldfsr(void);
void set_cwp(int new_cwp);
void do_fitos(void);
void do_fitod(void);
void do_fabss(void);
void do_fsqrts(void);
void do_fsqrtd(void);
void do_fcmps(void);
void do_fcmpd(void);
void do_fcmpes(void);
void do_fcmped(void);
#if defined(CONFIG_USER_ONLY)
void do_fitoq(void);
void do_fsqrtq(void);
void do_fcmpq(void);
void do_fcmpeq(void);
#endif
#ifdef TARGET_SPARC64
void do_fabsd(void);
void do_fxtos(void);
void do_fxtod(void);
void do_fcmps_fcc1(void);
void do_fcmpd_fcc1(void);
void do_fcmps_fcc2(void);
void do_fcmpd_fcc2(void);
void do_fcmps_fcc3(void);
void do_fcmpd_fcc3(void);
void do_fcmpes_fcc1(void);
void do_fcmped_fcc1(void);
void do_fcmpes_fcc2(void);
void do_fcmped_fcc2(void);
void do_fcmpes_fcc3(void);
void do_fcmped_fcc3(void);
#if defined(CONFIG_USER_ONLY)
void do_fabsq(void);
void do_fxtoq(void);
void do_fcmpq_fcc1(void);
void do_fcmpq_fcc2(void);
void do_fcmpq_fcc3(void);
void do_fcmpeq_fcc1(void);
void do_fcmpeq_fcc2(void);
void do_fcmpeq_fcc3(void);
#endif
void do_popc();
void do_wrpstate();
void do_done();
void do_retry();
#endif
void do_ldd_kernel(target_ulong addr);
void do_ldd_user(target_ulong addr);
void do_ldd_raw(target_ulong addr);
void do_interrupt(int intno);
void raise_exception(int tt);
void check_ieee_exceptions();
void memcpy32(target_ulong *dst, const target_ulong *src);
target_ulong mmu_probe(CPUState *env, target_ulong address, int mmulev);
void dump_mmu(CPUState *env);
void helper_debug();
void do_wrpsr();
void do_rdpsr();

/* XXX: move that to a generic header */
#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static inline void env_to_regs(void)
{
#if defined(reg_REGWPTR)
    REGWPTR = env->regbase + (env->cwp * 16);
    env->regwptr = REGWPTR;
#endif
}

static inline void regs_to_env(void)
{
}

int cpu_sparc_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu);

static inline int cpu_halted(CPUState *env) {
    if (!env->halted)
        return 0;
    if ((env->interrupt_request & CPU_INTERRUPT_HARD) && (env->psret != 0)) {
        env->halted = 0;
        return 0;
    }
    return EXCP_HALTED;
}

#endif
