#ifndef EXEC_SPARC_H
#define EXEC_SPARC_H 1
#include "dyngen-exec.h"

register struct CPUSPARCState *env asm(AREG0);
register uint32_t T0 asm(AREG1);
register uint32_t T1 asm(AREG2);
register uint32_t T2 asm(AREG3);
#define FT0 (env->ft0)
#define FT1 (env->ft1)
#define FT2 (env->ft2)
#define DT0 (env->dt0)
#define DT1 (env->dt1)
#define DT2 (env->dt2)

#include "cpu.h"
#include "exec-all.h"

void cpu_lock(void);
void cpu_unlock(void);
void cpu_loop_exit(void);
void helper_flush(target_ulong addr);
void helper_ld_asi(int asi, int size, int sign);
void helper_st_asi(int asi, int size, int sign);
void helper_rett(void);
void helper_stfsr(void);
void set_cwp(int new_cwp);
void do_fabss(void);
void do_fsqrts(void);
void do_fsqrtd(void);
void do_fcmps(void);
void do_fcmpd(void);
void do_interrupt(int intno, int is_int, int error_code, 
                  unsigned int next_eip, int is_hw);
void raise_exception_err(int exception_index, int error_code);
void raise_exception(int tt);
void memcpy32(uint32_t *dst, const uint32_t *src);

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
#endif
