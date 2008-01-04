#if !defined(__QEMU_MIPS_EXEC_H__)
#define __QEMU_MIPS_EXEC_H__

//#define DEBUG_OP

#include "config.h"
#include "mips-defs.h"
#include "dyngen-exec.h"
#include "cpu-defs.h"

register struct CPUMIPSState *env asm(AREG0);

#if TARGET_LONG_BITS > HOST_LONG_BITS
#define T0 (env->t0)
#define T1 (env->t1)
#define T2 (env->t2)
#else
register target_ulong T0 asm(AREG1);
register target_ulong T1 asm(AREG2);
register target_ulong T2 asm(AREG3);
#endif

#if defined (USE_HOST_FLOAT_REGS)
#error "implement me."
#else
#define FDT0 (env->fpu->ft0.fd)
#define FDT1 (env->fpu->ft1.fd)
#define FDT2 (env->fpu->ft2.fd)
#define FST0 (env->fpu->ft0.fs[FP_ENDIAN_IDX])
#define FST1 (env->fpu->ft1.fs[FP_ENDIAN_IDX])
#define FST2 (env->fpu->ft2.fs[FP_ENDIAN_IDX])
#define FSTH0 (env->fpu->ft0.fs[!FP_ENDIAN_IDX])
#define FSTH1 (env->fpu->ft1.fs[!FP_ENDIAN_IDX])
#define FSTH2 (env->fpu->ft2.fs[!FP_ENDIAN_IDX])
#define DT0 (env->fpu->ft0.d)
#define DT1 (env->fpu->ft1.d)
#define DT2 (env->fpu->ft2.d)
#define WT0 (env->fpu->ft0.w[FP_ENDIAN_IDX])
#define WT1 (env->fpu->ft1.w[FP_ENDIAN_IDX])
#define WT2 (env->fpu->ft2.w[FP_ENDIAN_IDX])
#define WTH0 (env->fpu->ft0.w[!FP_ENDIAN_IDX])
#define WTH1 (env->fpu->ft1.w[!FP_ENDIAN_IDX])
#define WTH2 (env->fpu->ft2.w[!FP_ENDIAN_IDX])
#endif

#include "cpu.h"
#include "exec-all.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif /* !defined(CONFIG_USER_ONLY) */

#if defined(TARGET_MIPS64)
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
void do_dclo (void);
void do_dclz (void);
#endif
#endif

#if HOST_LONG_BITS < 64
void do_div (void);
#endif
#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_mult (void);
void do_multu (void);
void do_madd (void);
void do_maddu (void);
void do_msub (void);
void do_msubu (void);
void do_muls (void);
void do_mulsu (void);
void do_macc (void);
void do_macchi (void);
void do_maccu (void);
void do_macchiu (void);
void do_msac (void);
void do_msachi (void);
void do_msacu (void);
void do_msachiu (void);
void do_mulhi (void);
void do_mulhiu (void);
void do_mulshi (void);
void do_mulshiu (void);
#endif
#if defined(TARGET_MIPS64)
void do_ddiv (void);
#if TARGET_LONG_BITS > HOST_LONG_BITS
void do_ddivu (void);
#endif
#endif
void do_mfc0_random(void);
void do_mfc0_count(void);
void do_mtc0_entryhi(uint32_t in);
void do_mtc0_status_debug(uint32_t old, uint32_t val);
void do_mtc0_status_irqraise_debug(void);
void dump_fpu(CPUState *env);
void fpu_dump_state(CPUState *env, FILE *f,
                    int (*fpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags);
void dump_sc (void);
void do_pmon (int function);

void dump_sc (void);

int cpu_mips_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu);
void do_interrupt (CPUState *env);
void r4k_invalidate_tlb (CPUState *env, int idx, int use_extra);

void cpu_loop_exit(void);
void do_raise_exception_err (uint32_t exception, int error_code);
void do_raise_exception (uint32_t exception);
void do_raise_exception_direct_err (uint32_t exception, int error_code);
void do_raise_exception_direct (uint32_t exception);

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags);
void cpu_mips_irqctrl_init (void);
uint32_t cpu_mips_get_random (CPUState *env);
uint32_t cpu_mips_get_count (CPUState *env);
void cpu_mips_store_count (CPUState *env, uint32_t value);
void cpu_mips_store_compare (CPUState *env, uint32_t value);
void cpu_mips_start_count(CPUState *env);
void cpu_mips_stop_count(CPUState *env);
void cpu_mips_update_irq (CPUState *env);
void cpu_mips_clock_init (CPUState *env);
void cpu_mips_tlb_flush (CPUState *env, int flush_global);

void do_cfc1 (int reg);
void do_ctc1 (int reg);

#define FOP_PROTO(op)              \
void do_float_ ## op ## _s(void);  \
void do_float_ ## op ## _d(void);
FOP_PROTO(roundl)
FOP_PROTO(roundw)
FOP_PROTO(truncl)
FOP_PROTO(truncw)
FOP_PROTO(ceill)
FOP_PROTO(ceilw)
FOP_PROTO(floorl)
FOP_PROTO(floorw)
FOP_PROTO(rsqrt)
FOP_PROTO(recip)
#undef FOP_PROTO

#define FOP_PROTO(op)              \
void do_float_ ## op ## _s(void);  \
void do_float_ ## op ## _d(void);  \
void do_float_ ## op ## _ps(void);
FOP_PROTO(add)
FOP_PROTO(sub)
FOP_PROTO(mul)
FOP_PROTO(div)
FOP_PROTO(recip1)
FOP_PROTO(recip2)
FOP_PROTO(rsqrt1)
FOP_PROTO(rsqrt2)
#undef FOP_PROTO

void do_float_cvtd_s(void);
void do_float_cvtd_w(void);
void do_float_cvtd_l(void);
void do_float_cvtl_d(void);
void do_float_cvtl_s(void);
void do_float_cvtps_pw(void);
void do_float_cvtpw_ps(void);
void do_float_cvts_d(void);
void do_float_cvts_w(void);
void do_float_cvts_l(void);
void do_float_cvts_pl(void);
void do_float_cvts_pu(void);
void do_float_cvtw_s(void);
void do_float_cvtw_d(void);

void do_float_addr_ps(void);
void do_float_mulr_ps(void);

#define FOP_PROTO(op)                      \
void do_cmp_d_ ## op(long cc);             \
void do_cmpabs_d_ ## op(long cc);          \
void do_cmp_s_ ## op(long cc);             \
void do_cmpabs_s_ ## op(long cc);          \
void do_cmp_ps_ ## op(long cc);            \
void do_cmpabs_ps_ ## op(long cc);

FOP_PROTO(f)
FOP_PROTO(un)
FOP_PROTO(eq)
FOP_PROTO(ueq)
FOP_PROTO(olt)
FOP_PROTO(ult)
FOP_PROTO(ole)
FOP_PROTO(ule)
FOP_PROTO(sf)
FOP_PROTO(ngle)
FOP_PROTO(seq)
FOP_PROTO(ngl)
FOP_PROTO(lt)
FOP_PROTO(nge)
FOP_PROTO(le)
FOP_PROTO(ngt)
#undef FOP_PROTO

static always_inline void env_to_regs(void)
{
}

static always_inline void regs_to_env(void)
{
}

static always_inline int cpu_halted(CPUState *env)
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

static always_inline void compute_hflags(CPUState *env)
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
        if (env->fpu->fcr0 & FCR0_F64)
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
