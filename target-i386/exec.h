/*
 *  i386 execution defines
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"
#include "dyngen-exec.h"

/* XXX: factorize this mess */
#ifdef TARGET_X86_64
#define TARGET_LONG_BITS 64
#else
#define TARGET_LONG_BITS 32
#endif

#include "cpu-defs.h"

/* at least 4 register variables are defined */
register struct CPUX86State *env asm(AREG0);

#if TARGET_LONG_BITS > HOST_LONG_BITS

/* no registers can be used */
#define T0 (env->t0)
#define T1 (env->t1)
#define T2 (env->t2)

#else

/* XXX: use unsigned long instead of target_ulong - better code will
   be generated for 64 bit CPUs */
register target_ulong T0 asm(AREG1);
register target_ulong T1 asm(AREG2);
register target_ulong T2 asm(AREG3);

/* if more registers are available, we define some registers too */
#ifdef AREG4
register target_ulong EAX asm(AREG4);
#define reg_EAX
#endif

#ifdef AREG5
register target_ulong ESP asm(AREG5);
#define reg_ESP
#endif

#ifdef AREG6
register target_ulong EBP asm(AREG6);
#define reg_EBP
#endif

#ifdef AREG7
register target_ulong ECX asm(AREG7);
#define reg_ECX
#endif

#ifdef AREG8
register target_ulong EDX asm(AREG8);
#define reg_EDX
#endif

#ifdef AREG9
register target_ulong EBX asm(AREG9);
#define reg_EBX
#endif

#ifdef AREG10
register target_ulong ESI asm(AREG10);
#define reg_ESI
#endif

#ifdef AREG11
register target_ulong EDI asm(AREG11);
#define reg_EDI
#endif

#endif /* ! (TARGET_LONG_BITS > HOST_LONG_BITS) */

#define A0 T2

extern FILE *logfile;
extern int loglevel;

#ifndef reg_EAX
#define EAX (env->regs[R_EAX])
#endif
#ifndef reg_ECX
#define ECX (env->regs[R_ECX])
#endif
#ifndef reg_EDX
#define EDX (env->regs[R_EDX])
#endif
#ifndef reg_EBX
#define EBX (env->regs[R_EBX])
#endif
#ifndef reg_ESP
#define ESP (env->regs[R_ESP])
#endif
#ifndef reg_EBP
#define EBP (env->regs[R_EBP])
#endif
#ifndef reg_ESI
#define ESI (env->regs[R_ESI])
#endif
#ifndef reg_EDI
#define EDI (env->regs[R_EDI])
#endif
#define EIP  (env->eip)
#define DF  (env->df)

#define CC_SRC (env->cc_src)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

/* float macros */
#define FT0    (env->ft0)
#define ST0    (env->fpregs[env->fpstt].d)
#define ST(n)  (env->fpregs[(env->fpstt + (n)) & 7].d)
#define ST1    ST(1)

#ifdef USE_FP_CONVERT
#define FP_CONVERT  (env->fp_convert)
#endif

#include "cpu.h"
#include "exec-all.h"

typedef struct CCTable {
    int (*compute_all)(void); /* return all the flags */
    int (*compute_c)(void);  /* return the C flag */
} CCTable;

extern CCTable cc_table[];

void load_seg(int seg_reg, int selector);
void helper_ljmp_protected_T0_T1(int next_eip);
void helper_lcall_real_T0_T1(int shift, int next_eip);
void helper_lcall_protected_T0_T1(int shift, int next_eip);
void helper_iret_real(int shift);
void helper_iret_protected(int shift, int next_eip);
void helper_lret_protected(int shift, int addend);
void helper_lldt_T0(void);
void helper_ltr_T0(void);
void helper_movl_crN_T0(int reg);
void helper_movl_drN_T0(int reg);
void helper_invlpg(target_ulong addr);
void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0);
void cpu_x86_update_cr3(CPUX86State *env, target_ulong new_cr3);
void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4);
void cpu_x86_flush_tlb(CPUX86State *env, target_ulong addr);
int cpu_x86_handle_mmu_fault(CPUX86State *env, target_ulong addr,
                             int is_write, int mmu_idx, int is_softmmu);
void tlb_fill(target_ulong addr, int is_write, int mmu_idx,
              void *retaddr);
void __hidden cpu_lock(void);
void __hidden cpu_unlock(void);
void do_interrupt(int intno, int is_int, int error_code,
                  target_ulong next_eip, int is_hw);
void do_interrupt_user(int intno, int is_int, int error_code,
                       target_ulong next_eip);
void raise_interrupt(int intno, int is_int, int error_code,
                     int next_eip_addend);
void raise_exception_err(int exception_index, int error_code);
void raise_exception(int exception_index);
void do_smm_enter(void);
void __hidden cpu_loop_exit(void);

void OPPROTO op_movl_eflags_T0(void);
void OPPROTO op_movl_T0_eflags(void);
void helper_divl_EAX_T0(void);
void helper_idivl_EAX_T0(void);
void helper_mulq_EAX_T0(void);
void helper_imulq_EAX_T0(void);
void helper_imulq_T0_T1(void);
void helper_divq_EAX_T0(void);
void helper_idivq_EAX_T0(void);
void helper_bswapq_T0(void);
void helper_cmpxchg8b(void);
void helper_single_step(void);
void helper_cpuid(void);
void helper_enter_level(int level, int data32);
void helper_enter64_level(int level, int data64);
void helper_sysenter(void);
void helper_sysexit(void);
void helper_syscall(int next_eip_addend);
void helper_sysret(int dflag);
void helper_rdtsc(void);
void helper_rdpmc(void);
void helper_rdmsr(void);
void helper_wrmsr(void);
void helper_lsl(void);
void helper_lar(void);
void helper_verr(void);
void helper_verw(void);
void helper_rsm(void);

void check_iob_T0(void);
void check_iow_T0(void);
void check_iol_T0(void);
void check_iob_DX(void);
void check_iow_DX(void);
void check_iol_DX(void);

#if !defined(CONFIG_USER_ONLY)

#include "softmmu_exec.h"

static inline double ldfq(target_ulong ptr)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.i = ldq(ptr);
    return u.d;
}

static inline void stfq(target_ulong ptr, double v)
{
    union {
        double d;
        uint64_t i;
    } u;
    u.d = v;
    stq(ptr, u.i);
}

static inline float ldfl(target_ulong ptr)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.i = ldl(ptr);
    return u.f;
}

static inline void stfl(target_ulong ptr, float v)
{
    union {
        float f;
        uint32_t i;
    } u;
    u.f = v;
    stl(ptr, u.i);
}

#endif /* !defined(CONFIG_USER_ONLY) */

#ifdef USE_X86LDOUBLE
/* use long double functions */
#define floatx_to_int32 floatx80_to_int32
#define floatx_to_int64 floatx80_to_int64
#define floatx_to_int32_round_to_zero floatx80_to_int32_round_to_zero
#define floatx_to_int64_round_to_zero floatx80_to_int64_round_to_zero
#define floatx_abs floatx80_abs
#define floatx_chs floatx80_chs
#define floatx_round_to_int floatx80_round_to_int
#define floatx_compare floatx80_compare
#define floatx_compare_quiet floatx80_compare_quiet
#define sin sinl
#define cos cosl
#define sqrt sqrtl
#define pow powl
#define log logl
#define tan tanl
#define atan2 atan2l
#define floor floorl
#define ceil ceill
#define ldexp ldexpl
#else
#define floatx_to_int32 float64_to_int32
#define floatx_to_int64 float64_to_int64
#define floatx_to_int32_round_to_zero float64_to_int32_round_to_zero
#define floatx_to_int64_round_to_zero float64_to_int64_round_to_zero
#define floatx_abs float64_abs
#define floatx_chs float64_chs
#define floatx_round_to_int float64_round_to_int
#define floatx_compare float64_compare
#define floatx_compare_quiet float64_compare_quiet
#endif

extern CPU86_LDouble sin(CPU86_LDouble x);
extern CPU86_LDouble cos(CPU86_LDouble x);
extern CPU86_LDouble sqrt(CPU86_LDouble x);
extern CPU86_LDouble pow(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble log(CPU86_LDouble x);
extern CPU86_LDouble tan(CPU86_LDouble x);
extern CPU86_LDouble atan2(CPU86_LDouble, CPU86_LDouble);
extern CPU86_LDouble floor(CPU86_LDouble x);
extern CPU86_LDouble ceil(CPU86_LDouble x);

#define RC_MASK         0xc00
#define RC_NEAR		0x000
#define RC_DOWN		0x400
#define RC_UP		0x800
#define RC_CHOP		0xc00

#define MAXTAN 9223372036854775808.0

#ifdef USE_X86LDOUBLE

/* only for x86 */
typedef union {
    long double d;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
} CPU86_LDoubleU;

/* the following deal with x86 long double-precision numbers */
#define MAXEXPD 0x7fff
#define EXPBIAS 16383
#define EXPD(fp)	(fp.l.upper & 0x7fff)
#define SIGND(fp)	((fp.l.upper) & 0x8000)
#define MANTD(fp)       (fp.l.lower)
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7fff)) | EXPBIAS

#else

/* NOTE: arm is horrible as double 32 bit words are stored in big endian ! */
typedef union {
    double d;
#if !defined(WORDS_BIGENDIAN) && !defined(__arm__)
    struct {
        uint32_t lower;
        int32_t upper;
    } l;
#else
    struct {
        int32_t upper;
        uint32_t lower;
    } l;
#endif
#ifndef __arm__
    int64_t ll;
#endif
} CPU86_LDoubleU;

/* the following deal with IEEE double-precision numbers */
#define MAXEXPD 0x7ff
#define EXPBIAS 1023
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & 0x80000000)
#ifdef __arm__
#define MANTD(fp)	(fp.l.lower | ((uint64_t)(fp.l.upper & ((1 << 20) - 1)) << 32))
#else
#define MANTD(fp)	(fp.ll & ((1LL << 52) - 1))
#endif
#define BIASEXPONENT(fp) fp.l.upper = (fp.l.upper & ~(0x7ff << 20)) | (EXPBIAS << 20)
#endif

static inline void fpush(void)
{
    env->fpstt = (env->fpstt - 1) & 7;
    env->fptags[env->fpstt] = 0; /* validate stack entry */
}

static inline void fpop(void)
{
    env->fptags[env->fpstt] = 1; /* invvalidate stack entry */
    env->fpstt = (env->fpstt + 1) & 7;
}

#ifndef USE_X86LDOUBLE
static inline CPU86_LDouble helper_fldt(target_ulong ptr)
{
    CPU86_LDoubleU temp;
    int upper, e;
    uint64_t ll;

    /* mantissa */
    upper = lduw(ptr + 8);
    /* XXX: handle overflow ? */
    e = (upper & 0x7fff) - 16383 + EXPBIAS; /* exponent */
    e |= (upper >> 4) & 0x800; /* sign */
    ll = (ldq(ptr) >> 11) & ((1LL << 52) - 1);
#ifdef __arm__
    temp.l.upper = (e << 20) | (ll >> 32);
    temp.l.lower = ll;
#else
    temp.ll = ll | ((uint64_t)e << 52);
#endif
    return temp.d;
}

static inline void helper_fstt(CPU86_LDouble f, target_ulong ptr)
{
    CPU86_LDoubleU temp;
    int e;

    temp.d = f;
    /* mantissa */
    stq(ptr, (MANTD(temp) << 11) | (1LL << 63));
    /* exponent + sign */
    e = EXPD(temp) - EXPBIAS + 16383;
    e |= SIGND(temp) >> 16;
    stw(ptr + 8, e);
}
#else

/* XXX: same endianness assumed */

#ifdef CONFIG_USER_ONLY

static inline CPU86_LDouble helper_fldt(target_ulong ptr)
{
    return *(CPU86_LDouble *)(unsigned long)ptr;
}

static inline void helper_fstt(CPU86_LDouble f, target_ulong ptr)
{
    *(CPU86_LDouble *)(unsigned long)ptr = f;
}

#else

/* we use memory access macros */

static inline CPU86_LDouble helper_fldt(target_ulong ptr)
{
    CPU86_LDoubleU temp;

    temp.l.lower = ldq(ptr);
    temp.l.upper = lduw(ptr + 8);
    return temp.d;
}

static inline void helper_fstt(CPU86_LDouble f, target_ulong ptr)
{
    CPU86_LDoubleU temp;

    temp.d = f;
    stq(ptr, temp.l.lower);
    stw(ptr + 8, temp.l.upper);
}

#endif /* !CONFIG_USER_ONLY */

#endif /* USE_X86LDOUBLE */

#define FPUS_IE (1 << 0)
#define FPUS_DE (1 << 1)
#define FPUS_ZE (1 << 2)
#define FPUS_OE (1 << 3)
#define FPUS_UE (1 << 4)
#define FPUS_PE (1 << 5)
#define FPUS_SF (1 << 6)
#define FPUS_SE (1 << 7)
#define FPUS_B  (1 << 15)

#define FPUC_EM 0x3f

extern const CPU86_LDouble f15rk[7];

void helper_fldt_ST0_A0(void);
void helper_fstt_ST0_A0(void);
void fpu_raise_exception(void);
CPU86_LDouble helper_fdiv(CPU86_LDouble a, CPU86_LDouble b);
void helper_fbld_ST0_A0(void);
void helper_fbst_ST0_A0(void);
void helper_f2xm1(void);
void helper_fyl2x(void);
void helper_fptan(void);
void helper_fpatan(void);
void helper_fxtract(void);
void helper_fprem1(void);
void helper_fprem(void);
void helper_fyl2xp1(void);
void helper_fsqrt(void);
void helper_fsincos(void);
void helper_frndint(void);
void helper_fscale(void);
void helper_fsin(void);
void helper_fcos(void);
void helper_fxam_ST0(void);
void helper_fstenv(target_ulong ptr, int data32);
void helper_fldenv(target_ulong ptr, int data32);
void helper_fsave(target_ulong ptr, int data32);
void helper_frstor(target_ulong ptr, int data32);
void helper_fxsave(target_ulong ptr, int data64);
void helper_fxrstor(target_ulong ptr, int data64);
void restore_native_fp_state(CPUState *env);
void save_native_fp_state(CPUState *env);
float approx_rsqrt(float a);
float approx_rcp(float a);
void update_fp_status(void);
void helper_hlt(void);
void helper_monitor(void);
void helper_mwait(void);
void helper_vmrun(target_ulong addr);
void helper_vmmcall(void);
void helper_vmload(target_ulong addr);
void helper_vmsave(target_ulong addr);
void helper_stgi(void);
void helper_clgi(void);
void helper_skinit(void);
void helper_invlpga(void);
void vmexit(uint64_t exit_code, uint64_t exit_info_1);

extern const uint8_t parity_table[256];
extern const uint8_t rclw_table[32];
extern const uint8_t rclb_table[32];

static inline uint32_t compute_eflags(void)
{
    return env->eflags | cc_table[CC_OP].compute_all() | (DF & DF_MASK);
}

/* NOTE: CC_OP must be modified manually to CC_OP_EFLAGS */
static inline void load_eflags(int eflags, int update_mask)
{
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    env->eflags = (env->eflags & ~update_mask) |
        (eflags & update_mask);
}

static inline void env_to_regs(void)
{
#ifdef reg_EAX
    EAX = env->regs[R_EAX];
#endif
#ifdef reg_ECX
    ECX = env->regs[R_ECX];
#endif
#ifdef reg_EDX
    EDX = env->regs[R_EDX];
#endif
#ifdef reg_EBX
    EBX = env->regs[R_EBX];
#endif
#ifdef reg_ESP
    ESP = env->regs[R_ESP];
#endif
#ifdef reg_EBP
    EBP = env->regs[R_EBP];
#endif
#ifdef reg_ESI
    ESI = env->regs[R_ESI];
#endif
#ifdef reg_EDI
    EDI = env->regs[R_EDI];
#endif
}

static inline void regs_to_env(void)
{
#ifdef reg_EAX
    env->regs[R_EAX] = EAX;
#endif
#ifdef reg_ECX
    env->regs[R_ECX] = ECX;
#endif
#ifdef reg_EDX
    env->regs[R_EDX] = EDX;
#endif
#ifdef reg_EBX
    env->regs[R_EBX] = EBX;
#endif
#ifdef reg_ESP
    env->regs[R_ESP] = ESP;
#endif
#ifdef reg_EBP
    env->regs[R_EBP] = EBP;
#endif
#ifdef reg_ESI
    env->regs[R_ESI] = ESI;
#endif
#ifdef reg_EDI
    env->regs[R_EDI] = EDI;
#endif
}

static inline int cpu_halted(CPUState *env) {
    /* handle exit of HALTED state */
    if (!(env->hflags & HF_HALTED_MASK))
        return 0;
    /* disable halt condition */
    if ((env->interrupt_request & CPU_INTERRUPT_HARD) &&
        (env->eflags & IF_MASK)) {
        env->hflags &= ~HF_HALTED_MASK;
        return 0;
    }
    return EXCP_HALTED;
}

