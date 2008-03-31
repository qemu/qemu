/*
 *  ARM micro operations
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery, LLC
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
#include "exec.h"

void OPPROTO op_addl_T0_T1_cc(void)
{
    unsigned int src1;
    src1 = T0;
    T0 += T1;
    env->NZF = T0;
    env->CF = T0 < src1;
    env->VF = (src1 ^ T1 ^ -1) & (src1 ^ T0);
}

void OPPROTO op_adcl_T0_T1_cc(void)
{
    unsigned int src1;
    src1 = T0;
    if (!env->CF) {
        T0 += T1;
        env->CF = T0 < src1;
    } else {
        T0 += T1 + 1;
        env->CF = T0 <= src1;
    }
    env->VF = (src1 ^ T1 ^ -1) & (src1 ^ T0);
    env->NZF = T0;
    FORCE_RET();
}

#define OPSUB(sub, sbc, res, T0, T1)            \
                                                \
void OPPROTO op_ ## sub ## l_T0_T1_cc(void)     \
{                                               \
    unsigned int src1;                          \
    src1 = T0;                                  \
    T0 -= T1;                                   \
    env->NZF = T0;                              \
    env->CF = src1 >= T1;                       \
    env->VF = (src1 ^ T1) & (src1 ^ T0);        \
    res = T0;                                   \
}                                               \
                                                \
void OPPROTO op_ ## sbc ## l_T0_T1_cc(void)     \
{                                               \
    unsigned int src1;                          \
    src1 = T0;                                  \
    if (!env->CF) {                             \
        T0 = T0 - T1 - 1;                       \
        env->CF = src1 > T1;                    \
    } else {                                    \
        T0 = T0 - T1;                           \
        env->CF = src1 >= T1;                   \
    }                                           \
    env->VF = (src1 ^ T1) & (src1 ^ T0);        \
    env->NZF = T0;                              \
    res = T0;                                   \
    FORCE_RET();                                \
}

OPSUB(sub, sbc, T0, T0, T1)

OPSUB(rsb, rsc, T0, T1, T0)

#define EIP (env->regs[15])

void OPPROTO op_test_eq(void)
{
    if (env->NZF == 0)
        GOTO_LABEL_PARAM(1);;
    FORCE_RET();
}

void OPPROTO op_test_ne(void)
{
    if (env->NZF != 0)
        GOTO_LABEL_PARAM(1);;
    FORCE_RET();
}

void OPPROTO op_test_cs(void)
{
    if (env->CF != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_cc(void)
{
    if (env->CF == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_mi(void)
{
    if ((env->NZF & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_pl(void)
{
    if ((env->NZF & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_vs(void)
{
    if ((env->VF & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_vc(void)
{
    if ((env->VF & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_hi(void)
{
    if (env->CF != 0 && env->NZF != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_ls(void)
{
    if (env->CF == 0 || env->NZF == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_ge(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_lt(void)
{
    if (((env->VF ^ env->NZF) & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_gt(void)
{
    if (env->NZF != 0 && ((env->VF ^ env->NZF) & 0x80000000) == 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_le(void)
{
    if (env->NZF == 0 || ((env->VF ^ env->NZF) & 0x80000000) != 0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_test_T0(void)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}
void OPPROTO op_testn_T0(void)
{
    if (!T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

void OPPROTO op_movl_T0_cpsr(void)
{
    /* Execution state bits always read as zero.  */
    T0 = cpsr_read(env) & ~CPSR_EXEC;
    FORCE_RET();
}

void OPPROTO op_movl_T0_spsr(void)
{
    T0 = env->spsr;
}

void OPPROTO op_movl_spsr_T0(void)
{
    uint32_t mask = PARAM1;
    env->spsr = (env->spsr & ~mask) | (T0 & mask);
}

void OPPROTO op_movl_cpsr_T0(void)
{
    cpsr_write(env, T0, PARAM1);
    FORCE_RET();
}

/* 48 bit signed mul, top 32 bits */
void OPPROTO op_imulw_T0_T1(void)
{
  uint64_t res;
  res = (int64_t)((int32_t)T0) * (int64_t)((int32_t)T1);
  T0 = res >> 16;
}

void OPPROTO op_addq_T0_T1(void)
{
    uint64_t res;
    res = ((uint64_t)T1 << 32) | T0;
    res += ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
    T1 = res >> 32;
    T0 = res;
}

void OPPROTO op_addq_lo_T0_T1(void)
{
    uint64_t res;
    res = ((uint64_t)T1 << 32) | T0;
    res += (uint64_t)(env->regs[PARAM1]);
    T1 = res >> 32;
    T0 = res;
}

/* Dual 16-bit accumulate.  */
void OPPROTO op_addq_T0_T1_dual(void)
{
  uint64_t res;
  res = ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
  res += (int32_t)T0;
  res += (int32_t)T1;
  env->regs[PARAM1] = (uint32_t)res;
  env->regs[PARAM2] = res >> 32;
}

/* Dual 16-bit subtract accumulate.  */
void OPPROTO op_subq_T0_T1_dual(void)
{
  uint64_t res;
  res = ((uint64_t)(env->regs[PARAM2]) << 32) | (env->regs[PARAM1]);
  res += (int32_t)T0;
  res -= (int32_t)T1;
  env->regs[PARAM1] = (uint32_t)res;
  env->regs[PARAM2] = res >> 32;
}

void OPPROTO op_logicq_cc(void)
{
    env->NZF = (T1 & 0x80000000) | ((T0 | T1) != 0);
}

/* memory access */

#define MEMSUFFIX _raw
#include "op_mem.h"

#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"
#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

void OPPROTO op_clrex(void)
{
    cpu_lock();
    helper_clrex(env);
    cpu_unlock();
}

/* T1 based, use T0 as shift count */

void OPPROTO op_shll_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        T1 = 0;
    else
        T1 = T1 << shift;
    FORCE_RET();
}

void OPPROTO op_shrl_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        T1 = 0;
    else
        T1 = (uint32_t)T1 >> shift;
    FORCE_RET();
}

void OPPROTO op_sarl_T1_T0(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32)
        shift = 31;
    T1 = (int32_t)T1 >> shift;
}

void OPPROTO op_rorl_T1_T0(void)
{
    int shift;
    shift = T0 & 0x1f;
    if (shift) {
        T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
    }
    FORCE_RET();
}

/* T1 based, use T0 as shift count and compute CF */

void OPPROTO op_shll_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = T1 & 1;
        else
            env->CF = 0;
        T1 = 0;
    } else if (shift != 0) {
        env->CF = (T1 >> (32 - shift)) & 1;
        T1 = T1 << shift;
    }
    FORCE_RET();
}

void OPPROTO op_shrl_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        if (shift == 32)
            env->CF = (T1 >> 31) & 1;
        else
            env->CF = 0;
        T1 = 0;
    } else if (shift != 0) {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = (uint32_t)T1 >> shift;
    }
    FORCE_RET();
}

void OPPROTO op_sarl_T1_T0_cc(void)
{
    int shift;
    shift = T0 & 0xff;
    if (shift >= 32) {
        env->CF = (T1 >> 31) & 1;
        T1 = (int32_t)T1 >> 31;
    } else if (shift != 0) {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = (int32_t)T1 >> shift;
    }
    FORCE_RET();
}

void OPPROTO op_rorl_T1_T0_cc(void)
{
    int shift1, shift;
    shift1 = T0 & 0xff;
    shift = shift1 & 0x1f;
    if (shift == 0) {
        if (shift1 != 0)
            env->CF = (T1 >> 31) & 1;
    } else {
        env->CF = (T1 >> (shift - 1)) & 1;
        T1 = ((uint32_t)T1 >> shift) | (T1 << (32 - shift));
    }
    FORCE_RET();
}

/* exceptions */

void OPPROTO op_swi(void)
{
    env->exception_index = EXCP_SWI;
    cpu_loop_exit();
}

void OPPROTO op_undef_insn(void)
{
    env->exception_index = EXCP_UDEF;
    cpu_loop_exit();
}

void OPPROTO op_debug(void)
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void OPPROTO op_wfi(void)
{
    env->exception_index = EXCP_HLT;
    env->halted = 1;
    cpu_loop_exit();
}

void OPPROTO op_bkpt(void)
{
    env->exception_index = EXCP_BKPT;
    cpu_loop_exit();
}

void OPPROTO op_exception_exit(void)
{
    env->exception_index = EXCP_EXCEPTION_EXIT;
    cpu_loop_exit();
}

/* VFP support.  We follow the convention used for VFP instrunctions:
   Single precition routines have a "s" suffix, double precision a
   "d" suffix.  */

#define VFP_OP(name, p) void OPPROTO op_vfp_##name##p(void)

#define VFP_BINOP(name) \
VFP_OP(name, s)             \
{                           \
    FT0s = float32_ ## name (FT0s, FT1s, &env->vfp.fp_status);    \
}                           \
VFP_OP(name, d)             \
{                           \
    FT0d = float64_ ## name (FT0d, FT1d, &env->vfp.fp_status);    \
}
VFP_BINOP(add)
VFP_BINOP(sub)
VFP_BINOP(mul)
VFP_BINOP(div)
#undef VFP_BINOP

#define VFP_HELPER(name)  \
VFP_OP(name, s)           \
{                         \
    do_vfp_##name##s();    \
}                         \
VFP_OP(name, d)           \
{                         \
    do_vfp_##name##d();    \
}
VFP_HELPER(abs)
VFP_HELPER(sqrt)
VFP_HELPER(cmp)
VFP_HELPER(cmpe)
#undef VFP_HELPER

/* XXX: Will this do the right thing for NANs.  Should invert the signbit
   without looking at the rest of the value.  */
VFP_OP(neg, s)
{
    FT0s = float32_chs(FT0s);
}

VFP_OP(neg, d)
{
    FT0d = float64_chs(FT0d);
}

VFP_OP(F1_ld0, s)
{
    union {
        uint32_t i;
        float32 s;
    } v;
    v.i = 0;
    FT1s = v.s;
}

VFP_OP(F1_ld0, d)
{
    union {
        uint64_t i;
        float64 d;
    } v;
    v.i = 0;
    FT1d = v.d;
}

/* Helper routines to perform bitwise copies between float and int.  */
static inline float32 vfp_itos(uint32_t i)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.i = i;
    return v.s;
}

static inline uint32_t vfp_stoi(float32 s)
{
    union {
        uint32_t i;
        float32 s;
    } v;

    v.s = s;
    return v.i;
}

static inline float64 vfp_itod(uint64_t i)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.i = i;
    return v.d;
}

static inline uint64_t vfp_dtoi(float64 d)
{
    union {
        uint64_t i;
        float64 d;
    } v;

    v.d = d;
    return v.i;
}

/* Integer to float conversion.  */
VFP_OP(uito, s)
{
    FT0s = uint32_to_float32(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(uito, d)
{
    FT0d = uint32_to_float64(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(sito, s)
{
    FT0s = int32_to_float32(vfp_stoi(FT0s), &env->vfp.fp_status);
}

VFP_OP(sito, d)
{
    FT0d = int32_to_float64(vfp_stoi(FT0s), &env->vfp.fp_status);
}

/* Float to integer conversion.  */
VFP_OP(toui, s)
{
    FT0s = vfp_itos(float32_to_uint32(FT0s, &env->vfp.fp_status));
}

VFP_OP(toui, d)
{
    FT0s = vfp_itos(float64_to_uint32(FT0d, &env->vfp.fp_status));
}

VFP_OP(tosi, s)
{
    FT0s = vfp_itos(float32_to_int32(FT0s, &env->vfp.fp_status));
}

VFP_OP(tosi, d)
{
    FT0s = vfp_itos(float64_to_int32(FT0d, &env->vfp.fp_status));
}

/* TODO: Set rounding mode properly.  */
VFP_OP(touiz, s)
{
    FT0s = vfp_itos(float32_to_uint32_round_to_zero(FT0s, &env->vfp.fp_status));
}

VFP_OP(touiz, d)
{
    FT0s = vfp_itos(float64_to_uint32_round_to_zero(FT0d, &env->vfp.fp_status));
}

VFP_OP(tosiz, s)
{
    FT0s = vfp_itos(float32_to_int32_round_to_zero(FT0s, &env->vfp.fp_status));
}

VFP_OP(tosiz, d)
{
    FT0s = vfp_itos(float64_to_int32_round_to_zero(FT0d, &env->vfp.fp_status));
}

/* floating point conversion */
VFP_OP(fcvtd, s)
{
    FT0d = float32_to_float64(FT0s, &env->vfp.fp_status);
}

VFP_OP(fcvts, d)
{
    FT0s = float64_to_float32(FT0d, &env->vfp.fp_status);
}

/* VFP3 fixed point conversion.  */
#define VFP_CONV_FIX(name, p, ftype, itype, sign) \
VFP_OP(name##to, p) \
{ \
    ftype tmp; \
    tmp = sign##int32_to_##ftype ((itype)vfp_##p##toi(FT0##p), \
                                  &env->vfp.fp_status); \
    FT0##p = ftype##_scalbn(tmp, PARAM1, &env->vfp.fp_status); \
} \
VFP_OP(to##name, p) \
{ \
    ftype tmp; \
    tmp = ftype##_scalbn(FT0##p, PARAM1, &env->vfp.fp_status); \
    FT0##p = vfp_ito##p((itype)ftype##_to_##sign##int32_round_to_zero(tmp, \
            &env->vfp.fp_status)); \
}

VFP_CONV_FIX(sh, d, float64, int16, )
VFP_CONV_FIX(sl, d, float64, int32, )
VFP_CONV_FIX(uh, d, float64, uint16, u)
VFP_CONV_FIX(ul, d, float64, uint32, u)
VFP_CONV_FIX(sh, s, float32, int16, )
VFP_CONV_FIX(sl, s, float32, int32, )
VFP_CONV_FIX(uh, s, float32, uint16, u)
VFP_CONV_FIX(ul, s, float32, uint32, u)

/* Get and Put values from registers.  */
VFP_OP(getreg_F0, d)
{
  FT0d = *(float64 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F0, s)
{
  FT0s = *(float32 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F1, d)
{
  FT1d = *(float64 *)((char *) env + PARAM1);
}

VFP_OP(getreg_F1, s)
{
  FT1s = *(float32 *)((char *) env + PARAM1);
}

VFP_OP(setreg_F0, d)
{
  *(float64 *)((char *) env + PARAM1) = FT0d;
}

VFP_OP(setreg_F0, s)
{
  *(float32 *)((char *) env + PARAM1) = FT0s;
}

void OPPROTO op_vfp_movl_T0_fpscr(void)
{
    do_vfp_get_fpscr ();
}

void OPPROTO op_vfp_movl_T0_fpscr_flags(void)
{
    T0 = env->vfp.xregs[ARM_VFP_FPSCR] & (0xf << 28);
}

void OPPROTO op_vfp_movl_fpscr_T0(void)
{
    do_vfp_set_fpscr();
}

void OPPROTO op_vfp_movl_T0_xreg(void)
{
    T0 = env->vfp.xregs[PARAM1];
}

void OPPROTO op_vfp_movl_xreg_T0(void)
{
    env->vfp.xregs[PARAM1] = T0;
}

/* Move between FT0s to T0  */
void OPPROTO op_vfp_mrs(void)
{
    T0 = vfp_stoi(FT0s);
}

void OPPROTO op_vfp_msr(void)
{
    FT0s = vfp_itos(T0);
}

/* Move between FT0d and {T0,T1} */
void OPPROTO op_vfp_mrrd(void)
{
    CPU_DoubleU u;

    u.d = FT0d;
    T0 = u.l.lower;
    T1 = u.l.upper;
}

void OPPROTO op_vfp_mdrr(void)
{
    CPU_DoubleU u;

    u.l.lower = T0;
    u.l.upper = T1;
    FT0d = u.d;
}

/* Load immediate.  PARAM1 is the 32 most significant bits of the value.  */
void OPPROTO op_vfp_fconstd(void)
{
    CPU_DoubleU u;
    u.l.upper = PARAM1;
    u.l.lower = 0;
    FT0d = u.d;
}

void OPPROTO op_vfp_fconsts(void)
{
    FT0s = vfp_itos(PARAM1);
}

void OPPROTO op_movl_cp_T0(void)
{
    helper_set_cp(env, PARAM1, T0);
    FORCE_RET();
}

void OPPROTO op_movl_T0_cp(void)
{
    T0 = helper_get_cp(env, PARAM1);
    FORCE_RET();
}

void OPPROTO op_movl_cp15_T0(void)
{
    helper_set_cp15(env, PARAM1, T0);
    FORCE_RET();
}

void OPPROTO op_movl_T0_cp15(void)
{
    T0 = helper_get_cp15(env, PARAM1);
    FORCE_RET();
}

/* Access to user mode registers from privileged modes.  */
void OPPROTO op_movl_T0_user(void)
{
    int regno = PARAM1;
    if (regno == 13) {
        T0 = env->banked_r13[0];
    } else if (regno == 14) {
        T0 = env->banked_r14[0];
    } else if ((env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        T0 = env->usr_regs[regno - 8];
    } else {
        T0 = env->regs[regno];
    }
    FORCE_RET();
}


void OPPROTO op_movl_user_T0(void)
{
    int regno = PARAM1;
    if (regno == 13) {
        env->banked_r13[0] = T0;
    } else if (regno == 14) {
        env->banked_r14[0] = T0;
    } else if ((env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_FIQ) {
        env->usr_regs[regno - 8] = T0;
    } else {
        env->regs[regno] = T0;
    }
    FORCE_RET();
}

/* ARMv6 Media instructions.  */

/* Note that signed overflow is undefined in C.  The following routines are
   careful to use unsigned types where modulo arithmetic is required.
   Failure to do so _will_ break on newer gcc.  */

/* Signed saturating arithmetic.  */

/* Perform 16-bit signed satruating addition.  */
static inline uint16_t add16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a + b;
    if (((res ^ a) & 0x8000) && !((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed satruating addition.  */
static inline uint8_t add8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a + b;
    if (((res ^ a) & 0x80) && !((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

/* Perform 16-bit signed satruating subtraction.  */
static inline uint16_t sub16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a - b;
    if (((res ^ a) & 0x8000) && ((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed satruating subtraction.  */
static inline uint8_t sub8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a - b;
    if (((res ^ a) & 0x80) && ((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

#define ADD16(a, b, n) RESULT(add16_sat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_sat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_sat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_sat(a, b), n, 8);
#define PFX q

#include "op_addsub.h"

/* Unsigned saturating arithmetic.  */
static inline uint16_t add16_usat(uint16_t a, uint8_t b)
{
    uint16_t res;
    res = a + b;
    if (res < a)
        res = 0xffff;
    return res;
}

static inline uint16_t sub16_usat(uint16_t a, uint8_t b)
{
    if (a < b)
        return a - b;
    else
        return 0;
}

static inline uint8_t add8_usat(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a + b;
    if (res < a)
        res = 0xff;
    return res;
}

static inline uint8_t sub8_usat(uint8_t a, uint8_t b)
{
    if (a < b)
        return a - b;
    else
        return 0;
}

#define ADD16(a, b, n) RESULT(add16_usat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_usat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_usat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_usat(a, b), n, 8);
#define PFX uq

#include "op_addsub.h"

/* Signed modulo arithmetic.  */
#define SARITH16(a, b, n, op) do { \
    int32_t sum; \
    sum = (int16_t)((uint16_t)(a) op (uint16_t)(b)); \
    RESULT(sum, n, 16); \
    if (sum >= 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SARITH8(a, b, n, op) do { \
    int32_t sum; \
    sum = (int8_t)((uint8_t)(a) op (uint8_t)(b)); \
    RESULT(sum, n, 8); \
    if (sum >= 0) \
        ge |= 1 << n; \
    } while(0)


#define ADD16(a, b, n) SARITH16(a, b, n, +)
#define SUB16(a, b, n) SARITH16(a, b, n, -)
#define ADD8(a, b, n)  SARITH8(a, b, n, +)
#define SUB8(a, b, n)  SARITH8(a, b, n, -)
#define PFX s
#define ARITH_GE

#include "op_addsub.h"

/* Unsigned modulo arithmetic.  */
#define ADD16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define ADD8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SUB16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SUB8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define PFX u
#define ARITH_GE

#include "op_addsub.h"

/* Halved signed arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) + (int32_t)(int16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) - (int32_t)(int16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) + (int32_t)(int8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) - (int32_t)(int8_t)(b)) >> 1, n, 8)
#define PFX sh

#include "op_addsub.h"

/* Halved unsigned arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define PFX uh

#include "op_addsub.h"

void OPPROTO op_sel_T0_T1(void)
{
    uint32_t mask;
    uint32_t flags;

    flags = env->GE;
    mask = 0;
    if (flags & 1)
        mask |= 0xff;
    if (flags & 2)
        mask |= 0xff00;
    if (flags & 4)
        mask |= 0xff0000;
    if (flags & 8)
        mask |= 0xff000000;
    T0 = (T0 & mask) | (T1 & ~mask);
    FORCE_RET();
}

/* Signed saturation.  */
static inline uint32_t do_ssat(int32_t val, int shift)
{
    int32_t top;
    uint32_t mask;

    shift = PARAM1;
    top = val >> shift;
    mask = (1u << shift) - 1;
    if (top > 0) {
        env->QF = 1;
        return mask;
    } else if (top < -1) {
        env->QF = 1;
        return ~mask;
    }
    return val;
}

/* Unsigned saturation.  */
static inline uint32_t do_usat(int32_t val, int shift)
{
    uint32_t max;

    shift = PARAM1;
    max = (1u << shift) - 1;
    if (val < 0) {
        env->QF = 1;
        return 0;
    } else if (val > max) {
        env->QF = 1;
        return max;
    }
    return val;
}

/* Signed saturate.  */
void OPPROTO op_ssat_T1(void)
{
    T0 = do_ssat(T0, PARAM1);
    FORCE_RET();
}

/* Dual halfword signed saturate.  */
void OPPROTO op_ssat16_T1(void)
{
    uint32_t res;

    res = (uint16_t)do_ssat((int16_t)T0, PARAM1);
    res |= do_ssat(((int32_t)T0) >> 16, PARAM1) << 16;
    T0 = res;
    FORCE_RET();
}

/* Unsigned saturate.  */
void OPPROTO op_usat_T1(void)
{
    T0 = do_usat(T0, PARAM1);
    FORCE_RET();
}

/* Dual halfword unsigned saturate.  */
void OPPROTO op_usat16_T1(void)
{
    uint32_t res;

    res = (uint16_t)do_usat((int16_t)T0, PARAM1);
    res |= do_usat(((int32_t)T0) >> 16, PARAM1) << 16;
    T0 = res;
    FORCE_RET();
}

/* Dual 16-bit add.  */
static inline uint8_t do_usad(uint8_t a, uint8_t b)
{
    if (a > b)
        return a - b;
    else
        return b - a;
}

/* Unsigned sum of absolute byte differences.  */
void OPPROTO op_usad8_T0_T1(void)
{
    uint32_t sum;
    sum = do_usad(T0, T1);
    sum += do_usad(T0 >> 8, T1 >> 8);
    sum += do_usad(T0 >> 16, T1 >>16);
    sum += do_usad(T0 >> 24, T1 >> 24);
    T0 = sum;
}

void OPPROTO op_movl_T1_r13_banked(void)
{
    T1 = helper_get_r13_banked(env, PARAM1);
}

void OPPROTO op_movl_r13_T1_banked(void)
{
    helper_set_r13_banked(env, PARAM1, T1);
}

void OPPROTO op_v7m_mrs_T0(void)
{
    T0 = helper_v7m_mrs(env, PARAM1);
}

void OPPROTO op_v7m_msr_T0(void)
{
    helper_v7m_msr(env, PARAM1, T0);
}

void OPPROTO op_movl_T0_sp(void)
{
    if (PARAM1 == env->v7m.current_sp)
        T0 = env->regs[13];
    else
        T0 = env->v7m.other_sp;
    FORCE_RET();
}

#include "op_neon.h"

/* iwMMXt support */
#include "op_iwmmxt.c"
