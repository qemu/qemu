#if !defined(__QEMU_MIPS_EXEC_H__)
#define __QEMU_MIPS_EXEC_H__

//#define DEBUG_OP

#include "mips-defs.h"
#include "dyngen-exec.h"

register struct CPUMIPSState *env asm(AREG0);

#if defined (USE_64BITS_REGS)
typedef int64_t host_int_t;
typedef uint64_t host_uint_t;
#else
typedef int32_t host_int_t;
typedef uint32_t host_uint_t;
#endif

register host_uint_t T0 asm(AREG1);
register host_uint_t T1 asm(AREG2);
register host_uint_t T2 asm(AREG3);

#if defined (USE_HOST_FLOAT_REGS)
#error "implement me."
#else
#define FDT0 (env->ft0.fd)
#define FDT1 (env->ft1.fd)
#define FDT2 (env->ft2.fd)
#define FST0 (env->ft0.fs[FP_ENDIAN_IDX])
#define FST1 (env->ft1.fs[FP_ENDIAN_IDX])
#define FST2 (env->ft2.fs[FP_ENDIAN_IDX])
#define DT0 (env->ft0.d)
#define DT1 (env->ft1.d)
#define DT2 (env->ft2.d)
#define WT0 (env->ft0.w[FP_ENDIAN_IDX])
#define WT1 (env->ft1.w[FP_ENDIAN_IDX])
#define WT2 (env->ft2.w[FP_ENDIAN_IDX])
#endif

#if defined (DEBUG_OP)
#define RETURN() __asm__ __volatile__("nop");
#else
#define RETURN() __asm__ __volatile__("");
#endif

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

static inline void env_to_regs(void)
{
}

static inline void regs_to_env(void)
{
}

#if (HOST_LONG_BITS == 32)
void do_mult (void);
void do_multu (void);
void do_madd (void);
void do_maddu (void);
void do_msub (void);
void do_msubu (void);
#endif
void do_mfc0(int reg, int sel);
void do_mtc0(int reg, int sel);
void do_tlbwi (void);
void do_tlbwr (void);
void do_tlbp (void);
void do_tlbr (void);
#ifdef MIPS_USES_FPU
void dump_fpu(CPUState *env);
void fpu_dump_state(CPUState *env, FILE *f, 
                    int (*fpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags);
#endif
void dump_sc (void);
void do_lwl_raw (uint32_t);
void do_lwr_raw (uint32_t);
uint32_t do_swl_raw (uint32_t);
uint32_t do_swr_raw (uint32_t);
#if !defined(CONFIG_USER_ONLY)
void do_lwl_user (uint32_t);
void do_lwl_kernel (uint32_t);
void do_lwr_user (uint32_t);
void do_lwr_kernel (uint32_t);
uint32_t do_swl_user (uint32_t);
uint32_t do_swl_kernel (uint32_t);
uint32_t do_swr_user (uint32_t);
uint32_t do_swr_kernel (uint32_t);
#endif
void do_pmon (int function);

void dump_sc (void);

int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int is_user, int is_softmmu);
void do_interrupt (CPUState *env);

void cpu_loop_exit(void);
void do_raise_exception_err (uint32_t exception, int error_code);
void do_raise_exception (uint32_t exception);
void do_raise_exception_direct (uint32_t exception);

void cpu_dump_state(CPUState *env, FILE *f, 
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags);
void cpu_mips_irqctrl_init (void);
uint32_t cpu_mips_get_random (CPUState *env);
uint32_t cpu_mips_get_count (CPUState *env);
void cpu_mips_store_count (CPUState *env, uint32_t value);
void cpu_mips_store_compare (CPUState *env, uint32_t value);
void cpu_mips_clock_init (CPUState *env);

#endif /* !defined(__QEMU_MIPS_EXEC_H__) */
