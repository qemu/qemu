#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "dyngen-exec.h"
#include "config.h"

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
register uint32_t *REGWPTR asm(AREG3);
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
#define reg_T2
#endif
#endif

#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define DT0 (env->dt0)
#define DT1 (env->dt1)

#include "cpu.h"
#include "exec-all.h"

void cpu_lock(void);
void cpu_unlock(void);
void cpu_loop_exit(void);
void helper_flush(target_ulong addr);
void helper_ld_asi(int asi, int size, int sign);
void helper_st_asi(int asi, int size, int sign);
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
#ifdef TARGET_SPARC64
void do_fabsd(void);
void do_fcmps_fcc1(void);
void do_fcmpd_fcc1(void);
void do_fcmps_fcc2(void);
void do_fcmpd_fcc2(void);
void do_fcmps_fcc3(void);
void do_fcmpd_fcc3(void);
void do_popc();
#endif
void do_ldd_kernel(target_ulong addr);
void do_ldd_user(target_ulong addr);
void do_ldd_raw(target_ulong addr);
void do_interrupt(int intno);
void raise_exception(int tt);
void memcpy32(target_ulong *dst, const target_ulong *src);
target_ulong mmu_probe(target_ulong address, int mmulev);
void dump_mmu(void);
void helper_debug();
void do_wrpsr();
void do_rdpsr();

/* XXX: move that to a generic header */
#if !defined(CONFIG_USER_ONLY)

#define ldul_user ldl_user
#define ldul_kernel ldl_kernel

#define ACCESS_TYPE 0
#define MEMSUFFIX _kernel
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ACCESS_TYPE 1
#define MEMSUFFIX _user
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

/* these access are slower, they must be as rare as possible */
#define ACCESS_TYPE 2
#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "softmmu_header.h"

#define DATA_SIZE 2
#include "softmmu_header.h"

#define DATA_SIZE 4
#include "softmmu_header.h"

#define DATA_SIZE 8
#include "softmmu_header.h"
#undef ACCESS_TYPE
#undef MEMSUFFIX

#define ldub(p) ldub_data(p)
#define ldsb(p) ldsb_data(p)
#define lduw(p) lduw_data(p)
#define ldsw(p) ldsw_data(p)
#define ldl(p) ldl_data(p)
#define ldq(p) ldq_data(p)

#define stb(p, v) stb_data(p, v)
#define stw(p, v) stw_data(p, v)
#define stl(p, v) stl_data(p, v)
#define stq(p, v) stq_data(p, v)

#endif /* !defined(CONFIG_USER_ONLY) */

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

int cpu_sparc_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                               int is_user, int is_softmmu);

#endif
