#if !defined(__QEMU_MIPS_EXEC_H__)
#define __QEMU_MIPS_EXEC_H__

//#define DEBUG_OP

#include "config.h"
#include "mips-defs.h"
#include "dyngen-exec.h"

#if defined(__sparc__)
struct CPUMIPSState *env;
#else
register struct CPUMIPSState *env asm(AREG0);
#endif

#if defined (USE_64BITS_REGS)
typedef int64_t host_int_t;
typedef uint64_t host_uint_t;
#else
typedef int32_t host_int_t;
typedef uint32_t host_uint_t;
#endif

#if defined(__sparc__)
host_uint_t T0;
host_uint_t T1;
host_uint_t T2;
#else
#if TARGET_LONG_BITS > HOST_LONG_BITS
#define T0 (env->t0)
#define T1 (env->t1)
#define T2 (env->t2)
#else
register host_uint_t T0 asm(AREG1);
register host_uint_t T1 asm(AREG2);
register host_uint_t T2 asm(AREG3);
#endif
#endif

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
# define RETURN() __asm__ __volatile__("nop" : : : "memory");
#else
# define RETURN() __asm__ __volatile__("" : : : "memory");
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

#ifdef MIPS_HAS_MIPS64
#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_dsll (void);
void do_dsll32 (void);
void do_dsra (void);
void do_dsra32 (void);
void do_dsrl (void);
void do_dsrl32 (void);
void do_drotr (void);
void do_drotr32 (void);
void do_dsllv (void);
void do_dsrav (void);
void do_dsrlv (void);
void do_drotrv (void);
#endif
#endif

#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_mult (void);
void do_multu (void);
void do_madd (void);
void do_maddu (void);
void do_msub (void);
void do_msubu (void);
void do_ddiv (void);
void do_ddivu (void);
#endif
#ifdef MIPS_HAS_MIPS64
void do_dmult (void);
void do_dmultu (void);
#endif
void do_mfc0_random(void);
void do_mfc0_count(void);
void do_mtc0_entryhi(uint32_t in);
void do_mtc0_status_debug(uint32_t old, uint32_t val);
void do_mtc0_status_irqraise_debug(void);
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
#ifdef MIPS_HAS_MIPS64
void do_ldl_raw (uint64_t);
void do_ldr_raw (uint64_t);
uint64_t do_sdl_raw (uint64_t);
uint64_t do_sdr_raw (uint64_t);
#endif
#if !defined(CONFIG_USER_ONLY)
void do_lwl_user (uint32_t);
void do_lwl_kernel (uint32_t);
void do_lwr_user (uint32_t);
void do_lwr_kernel (uint32_t);
uint32_t do_swl_user (uint32_t);
uint32_t do_swl_kernel (uint32_t);
uint32_t do_swr_user (uint32_t);
uint32_t do_swr_kernel (uint32_t);
#ifdef MIPS_HAS_MIPS64
void do_ldl_user (uint64_t);
void do_ldl_kernel (uint64_t);
void do_ldr_user (uint64_t);
void do_ldr_kernel (uint64_t);
uint64_t do_sdl_user (uint64_t);
uint64_t do_sdl_kernel (uint64_t);
uint64_t do_sdr_user (uint64_t);
uint64_t do_sdr_kernel (uint64_t);
#endif
#endif
void do_pmon (int function);

void dump_sc (void);

int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int is_user, int is_softmmu);
void do_interrupt (CPUState *env);
void invalidate_tlb (CPUState *env, int idx, int use_extra);

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
void cpu_mips_update_irq(CPUState *env);
void cpu_mips_clock_init (CPUState *env);
void cpu_mips_tlb_flush (CPUState *env, int flush_global);

#endif /* !defined(__QEMU_MIPS_EXEC_H__) */
