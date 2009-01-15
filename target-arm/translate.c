/*
 *  ARM translation
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Copyright (c) 2007 OpenedHand, Ltd.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-log.h"

#include "helpers.h"
#define GEN_HELPER 1
#include "helpers.h"

#define ENABLE_ARCH_5J    0
#define ENABLE_ARCH_6     arm_feature(env, ARM_FEATURE_V6)
#define ENABLE_ARCH_6K   arm_feature(env, ARM_FEATURE_V6K)
#define ENABLE_ARCH_6T2   arm_feature(env, ARM_FEATURE_THUMB2)
#define ENABLE_ARCH_7     arm_feature(env, ARM_FEATURE_V7)

#define ARCH(x) do { if (!ENABLE_ARCH_##x) goto illegal_op; } while(0)

/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    int condlabel;
    /* Thumb-2 condtional execution bits.  */
    int condexec_mask;
    int condexec_cond;
    struct TranslationBlock *tb;
    int singlestep_enabled;
    int thumb;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
} DisasContext;

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s) (s->user)
#endif

/* These instructions trap after executing, so defer them until after the
   conditional executions state has been updated.  */
#define DISAS_WFI 4
#define DISAS_SWI 5

static TCGv_ptr cpu_env;
/* We reuse the same 64-bit temporaries for efficiency.  */
static TCGv_i64 cpu_V0, cpu_V1, cpu_M0;

/* FIXME:  These should be removed.  */
static TCGv cpu_T[2];
static TCGv cpu_F0s, cpu_F1s;
static TCGv_i64 cpu_F0d, cpu_F1d;

#define ICOUNT_TEMP cpu_T[0]
#include "gen-icount.h"

/* initialize TCG globals.  */
void arm_translate_init(void)
{
    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    cpu_T[0] = tcg_global_reg_new_i32(TCG_AREG1, "T0");
    cpu_T[1] = tcg_global_reg_new_i32(TCG_AREG2, "T1");

#define GEN_HELPER 2
#include "helpers.h"
}

/* The code generator doesn't like lots of temporaries, so maintain our own
   cache for reuse within a function.  */
#define MAX_TEMPS 8
static int num_temps;
static TCGv temps[MAX_TEMPS];

/* Allocate a temporary variable.  */
static TCGv_i32 new_tmp(void)
{
    TCGv tmp;
    if (num_temps == MAX_TEMPS)
        abort();

    if (GET_TCGV_I32(temps[num_temps]))
      return temps[num_temps++];

    tmp = tcg_temp_new_i32();
    temps[num_temps++] = tmp;
    return tmp;
}

/* Release a temporary variable.  */
static void dead_tmp(TCGv tmp)
{
    int i;
    num_temps--;
    i = num_temps;
    if (TCGV_EQUAL(temps[i], tmp))
        return;

    /* Shuffle this temp to the last slot.  */
    while (!TCGV_EQUAL(temps[i], tmp))
        i--;
    while (i < num_temps) {
        temps[i] = temps[i + 1];
        i++;
    }
    temps[i] = tmp;
}

static inline TCGv load_cpu_offset(int offset)
{
    TCGv tmp = new_tmp();
    tcg_gen_ld_i32(tmp, cpu_env, offset);
    return tmp;
}

#define load_cpu_field(name) load_cpu_offset(offsetof(CPUState, name))

static inline void store_cpu_offset(TCGv var, int offset)
{
    tcg_gen_st_i32(var, cpu_env, offset);
    dead_tmp(var);
}

#define store_cpu_field(var, name) \
    store_cpu_offset(var, offsetof(CPUState, name))

/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv var, int reg)
{
    if (reg == 15) {
        uint32_t addr;
        /* normaly, since we updated PC, we need only to add one insn */
        if (s->thumb)
            addr = (long)s->pc + 2;
        else
            addr = (long)s->pc + 4;
        tcg_gen_movi_i32(var, addr);
    } else {
        tcg_gen_ld_i32(var, cpu_env, offsetof(CPUState, regs[reg]));
    }
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv load_reg(DisasContext *s, int reg)
{
    TCGv tmp = new_tmp();
    load_reg_var(s, tmp, reg);
    return tmp;
}

/* Set a CPU register.  The source must be a temporary and will be
   marked as dead.  */
static void store_reg(DisasContext *s, int reg, TCGv var)
{
    if (reg == 15) {
        tcg_gen_andi_i32(var, var, ~1);
        s->is_jmp = DISAS_JUMP;
    }
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUState, regs[reg]));
    dead_tmp(var);
}


/* Basic operations.  */
#define gen_op_movl_T0_T1() tcg_gen_mov_i32(cpu_T[0], cpu_T[1])
#define gen_op_movl_T0_im(im) tcg_gen_movi_i32(cpu_T[0], im)
#define gen_op_movl_T1_im(im) tcg_gen_movi_i32(cpu_T[1], im)

#define gen_op_addl_T1_im(im) tcg_gen_addi_i32(cpu_T[1], cpu_T[1], im)
#define gen_op_addl_T0_T1() tcg_gen_add_i32(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_subl_T0_T1() tcg_gen_sub_i32(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_rsbl_T0_T1() tcg_gen_sub_i32(cpu_T[0], cpu_T[1], cpu_T[0])

#define gen_op_addl_T0_T1_cc() gen_helper_add_cc(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_adcl_T0_T1_cc() gen_helper_adc_cc(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_subl_T0_T1_cc() gen_helper_sub_cc(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_sbcl_T0_T1_cc() gen_helper_sbc_cc(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_rsbl_T0_T1_cc() gen_helper_sub_cc(cpu_T[0], cpu_T[1], cpu_T[0])
#define gen_op_rscl_T0_T1_cc() gen_helper_sbc_cc(cpu_T[0], cpu_T[1], cpu_T[0])

#define gen_op_andl_T0_T1() tcg_gen_and_i32(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_xorl_T0_T1() tcg_gen_xor_i32(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_orl_T0_T1() tcg_gen_or_i32(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_op_notl_T0() tcg_gen_not_i32(cpu_T[0], cpu_T[0])
#define gen_op_notl_T1() tcg_gen_not_i32(cpu_T[1], cpu_T[1])
#define gen_op_logic_T0_cc() gen_logic_CC(cpu_T[0]);
#define gen_op_logic_T1_cc() gen_logic_CC(cpu_T[1]);

#define gen_op_shll_T1_im(im) tcg_gen_shli_i32(cpu_T[1], cpu_T[1], im)
#define gen_op_shrl_T1_im(im) tcg_gen_shri_i32(cpu_T[1], cpu_T[1], im)

/* Value extensions.  */
#define gen_uxtb(var) tcg_gen_ext8u_i32(var, var)
#define gen_uxth(var) tcg_gen_ext16u_i32(var, var)
#define gen_sxtb(var) tcg_gen_ext8s_i32(var, var)
#define gen_sxth(var) tcg_gen_ext16s_i32(var, var)

#define gen_sxtb16(var) gen_helper_sxtb16(var, var)
#define gen_uxtb16(var) gen_helper_uxtb16(var, var)

#define gen_op_mul_T0_T1() tcg_gen_mul_i32(cpu_T[0], cpu_T[0], cpu_T[1])

#define gen_set_cpsr(var, mask) gen_helper_cpsr_write(var, tcg_const_i32(mask))
/* Set NZCV flags from the high 4 bits of var.  */
#define gen_set_nzcv(var) gen_set_cpsr(var, CPSR_NZCV)

static void gen_exception(int excp)
{
    TCGv tmp = new_tmp();
    tcg_gen_movi_i32(tmp, excp);
    gen_helper_exception(tmp);
    dead_tmp(tmp);
}

static void gen_smul_dual(TCGv a, TCGv b)
{
    TCGv tmp1 = new_tmp();
    TCGv tmp2 = new_tmp();
    tcg_gen_ext16s_i32(tmp1, a);
    tcg_gen_ext16s_i32(tmp2, b);
    tcg_gen_mul_i32(tmp1, tmp1, tmp2);
    dead_tmp(tmp2);
    tcg_gen_sari_i32(a, a, 16);
    tcg_gen_sari_i32(b, b, 16);
    tcg_gen_mul_i32(b, b, a);
    tcg_gen_mov_i32(a, tmp1);
    dead_tmp(tmp1);
}

/* Byteswap each halfword.  */
static void gen_rev16(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_shri_i32(tmp, var, 8);
    tcg_gen_andi_i32(tmp, tmp, 0x00ff00ff);
    tcg_gen_shli_i32(var, var, 8);
    tcg_gen_andi_i32(var, var, 0xff00ff00);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

/* Byteswap low halfword and sign extend.  */
static void gen_revsh(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_shri_i32(tmp, var, 8);
    tcg_gen_andi_i32(tmp, tmp, 0x00ff);
    tcg_gen_shli_i32(var, var, 8);
    tcg_gen_ext8s_i32(var, var);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

/* Unsigned bitfield extract.  */
static void gen_ubfx(TCGv var, int shift, uint32_t mask)
{
    if (shift)
        tcg_gen_shri_i32(var, var, shift);
    tcg_gen_andi_i32(var, var, mask);
}

/* Signed bitfield extract.  */
static void gen_sbfx(TCGv var, int shift, int width)
{
    uint32_t signbit;

    if (shift)
        tcg_gen_sari_i32(var, var, shift);
    if (shift + width < 32) {
        signbit = 1u << (width - 1);
        tcg_gen_andi_i32(var, var, (1u << width) - 1);
        tcg_gen_xori_i32(var, var, signbit);
        tcg_gen_subi_i32(var, var, signbit);
    }
}

/* Bitfield insertion.  Insert val into base.  Clobbers base and val.  */
static void gen_bfi(TCGv dest, TCGv base, TCGv val, int shift, uint32_t mask)
{
    tcg_gen_andi_i32(val, val, mask);
    tcg_gen_shli_i32(val, val, shift);
    tcg_gen_andi_i32(base, base, ~(mask << shift));
    tcg_gen_or_i32(dest, base, val);
}

/* Round the top 32 bits of a 64-bit value.  */
static void gen_roundqd(TCGv a, TCGv b)
{
    tcg_gen_shri_i32(a, a, 31);
    tcg_gen_add_i32(a, a, b);
}

/* FIXME: Most targets have native widening multiplication.
   It would be good to use that instead of a full wide multiply.  */
/* 32x32->64 multiply.  Marks inputs as dead.  */
static TCGv_i64 gen_mulu_i64_i32(TCGv a, TCGv b)
{
    TCGv_i64 tmp1 = tcg_temp_new_i64();
    TCGv_i64 tmp2 = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(tmp1, a);
    dead_tmp(a);
    tcg_gen_extu_i32_i64(tmp2, b);
    dead_tmp(b);
    tcg_gen_mul_i64(tmp1, tmp1, tmp2);
    return tmp1;
}

static TCGv_i64 gen_muls_i64_i32(TCGv a, TCGv b)
{
    TCGv_i64 tmp1 = tcg_temp_new_i64();
    TCGv_i64 tmp2 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(tmp1, a);
    dead_tmp(a);
    tcg_gen_ext_i32_i64(tmp2, b);
    dead_tmp(b);
    tcg_gen_mul_i64(tmp1, tmp1, tmp2);
    return tmp1;
}

/* Unsigned 32x32->64 multiply.  */
static void gen_op_mull_T0_T1(void)
{
    TCGv_i64 tmp1 = tcg_temp_new_i64();
    TCGv_i64 tmp2 = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(tmp1, cpu_T[0]);
    tcg_gen_extu_i32_i64(tmp2, cpu_T[1]);
    tcg_gen_mul_i64(tmp1, tmp1, tmp2);
    tcg_gen_trunc_i64_i32(cpu_T[0], tmp1);
    tcg_gen_shri_i64(tmp1, tmp1, 32);
    tcg_gen_trunc_i64_i32(cpu_T[1], tmp1);
}

/* Signed 32x32->64 multiply.  */
static void gen_imull(TCGv a, TCGv b)
{
    TCGv_i64 tmp1 = tcg_temp_new_i64();
    TCGv_i64 tmp2 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(tmp1, a);
    tcg_gen_ext_i32_i64(tmp2, b);
    tcg_gen_mul_i64(tmp1, tmp1, tmp2);
    tcg_gen_trunc_i64_i32(a, tmp1);
    tcg_gen_shri_i64(tmp1, tmp1, 32);
    tcg_gen_trunc_i64_i32(b, tmp1);
}

/* Swap low and high halfwords.  */
static void gen_swap_half(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_shri_i32(tmp, var, 16);
    tcg_gen_shli_i32(var, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

/* Dual 16-bit add.  Result placed in t0 and t1 is marked as dead.
    tmp = (t0 ^ t1) & 0x8000;
    t0 &= ~0x8000;
    t1 &= ~0x8000;
    t0 = (t0 + t1) ^ tmp;
 */

static void gen_add16(TCGv t0, TCGv t1)
{
    TCGv tmp = new_tmp();
    tcg_gen_xor_i32(tmp, t0, t1);
    tcg_gen_andi_i32(tmp, tmp, 0x8000);
    tcg_gen_andi_i32(t0, t0, ~0x8000);
    tcg_gen_andi_i32(t1, t1, ~0x8000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_xor_i32(t0, t0, tmp);
    dead_tmp(tmp);
    dead_tmp(t1);
}

#define gen_set_CF(var) tcg_gen_st_i32(var, cpu_env, offsetof(CPUState, CF))

/* Set CF to the top bit of var.  */
static void gen_set_CF_bit31(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_shri_i32(tmp, var, 31);
    gen_set_CF(tmp);
    dead_tmp(tmp);
}

/* Set N and Z flags from var.  */
static inline void gen_logic_CC(TCGv var)
{
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUState, NF));
    tcg_gen_st_i32(var, cpu_env, offsetof(CPUState, ZF));
}

/* T0 += T1 + CF.  */
static void gen_adc_T0_T1(void)
{
    TCGv tmp;
    gen_op_addl_T0_T1();
    tmp = load_cpu_field(CF);
    tcg_gen_add_i32(cpu_T[0], cpu_T[0], tmp);
    dead_tmp(tmp);
}

/* dest = T0 - T1 + CF - 1.  */
static void gen_sub_carry(TCGv dest, TCGv t0, TCGv t1)
{
    TCGv tmp;
    tcg_gen_sub_i32(dest, t0, t1);
    tmp = load_cpu_field(CF);
    tcg_gen_add_i32(dest, dest, tmp);
    tcg_gen_subi_i32(dest, dest, 1);
    dead_tmp(tmp);
}

#define gen_sbc_T0_T1() gen_sub_carry(cpu_T[0], cpu_T[0], cpu_T[1])
#define gen_rsc_T0_T1() gen_sub_carry(cpu_T[0], cpu_T[1], cpu_T[0])

/* T0 &= ~T1.  Clobbers T1.  */
/* FIXME: Implement bic natively.  */
static inline void tcg_gen_bic_i32(TCGv dest, TCGv t0, TCGv t1)
{
    TCGv tmp = new_tmp();
    tcg_gen_not_i32(tmp, t1);
    tcg_gen_and_i32(dest, t0, tmp);
    dead_tmp(tmp);
}
static inline void gen_op_bicl_T0_T1(void)
{
    gen_op_notl_T1();
    gen_op_andl_T0_T1();
}

/* FIXME:  Implement this natively.  */
#define tcg_gen_abs_i32(t0, t1) gen_helper_abs(t0, t1)

/* FIXME:  Implement this natively.  */
static void tcg_gen_rori_i32(TCGv t0, TCGv t1, int i)
{
    TCGv tmp;

    if (i == 0)
        return;

    tmp = new_tmp();
    tcg_gen_shri_i32(tmp, t1, i);
    tcg_gen_shli_i32(t1, t1, 32 - i);
    tcg_gen_or_i32(t0, t1, tmp);
    dead_tmp(tmp);
}

static void shifter_out_im(TCGv var, int shift)
{
    TCGv tmp = new_tmp();
    if (shift == 0) {
        tcg_gen_andi_i32(tmp, var, 1);
    } else {
        tcg_gen_shri_i32(tmp, var, shift);
        if (shift != 31)
            tcg_gen_andi_i32(tmp, tmp, 1);
    }
    gen_set_CF(tmp);
    dead_tmp(tmp);
}

/* Shift by immediate.  Includes special handling for shift == 0.  */
static inline void gen_arm_shift_im(TCGv var, int shiftop, int shift, int flags)
{
    switch (shiftop) {
    case 0: /* LSL */
        if (shift != 0) {
            if (flags)
                shifter_out_im(var, 32 - shift);
            tcg_gen_shli_i32(var, var, shift);
        }
        break;
    case 1: /* LSR */
        if (shift == 0) {
            if (flags) {
                tcg_gen_shri_i32(var, var, 31);
                gen_set_CF(var);
            }
            tcg_gen_movi_i32(var, 0);
        } else {
            if (flags)
                shifter_out_im(var, shift - 1);
            tcg_gen_shri_i32(var, var, shift);
        }
        break;
    case 2: /* ASR */
        if (shift == 0)
            shift = 32;
        if (flags)
            shifter_out_im(var, shift - 1);
        if (shift == 32)
          shift = 31;
        tcg_gen_sari_i32(var, var, shift);
        break;
    case 3: /* ROR/RRX */
        if (shift != 0) {
            if (flags)
                shifter_out_im(var, shift - 1);
            tcg_gen_rori_i32(var, var, shift); break;
        } else {
            TCGv tmp = load_cpu_field(CF);
            if (flags)
                shifter_out_im(var, 0);
            tcg_gen_shri_i32(var, var, 1);
            tcg_gen_shli_i32(tmp, tmp, 31);
            tcg_gen_or_i32(var, var, tmp);
            dead_tmp(tmp);
        }
    }
};

static inline void gen_arm_shift_reg(TCGv var, int shiftop,
                                     TCGv shift, int flags)
{
    if (flags) {
        switch (shiftop) {
        case 0: gen_helper_shl_cc(var, var, shift); break;
        case 1: gen_helper_shr_cc(var, var, shift); break;
        case 2: gen_helper_sar_cc(var, var, shift); break;
        case 3: gen_helper_ror_cc(var, var, shift); break;
        }
    } else {
        switch (shiftop) {
        case 0: gen_helper_shl(var, var, shift); break;
        case 1: gen_helper_shr(var, var, shift); break;
        case 2: gen_helper_sar(var, var, shift); break;
        case 3: gen_helper_ror(var, var, shift); break;
        }
    }
    dead_tmp(shift);
}

#define PAS_OP(pfx) \
    switch (op2) {  \
    case 0: gen_pas_helper(glue(pfx,add16)); break; \
    case 1: gen_pas_helper(glue(pfx,addsubx)); break; \
    case 2: gen_pas_helper(glue(pfx,subaddx)); break; \
    case 3: gen_pas_helper(glue(pfx,sub16)); break; \
    case 4: gen_pas_helper(glue(pfx,add8)); break; \
    case 7: gen_pas_helper(glue(pfx,sub8)); break; \
    }
static void gen_arm_parallel_addsub(int op1, int op2, TCGv a, TCGv b)
{
    TCGv_ptr tmp;

    switch (op1) {
#define gen_pas_helper(name) glue(gen_helper_,name)(a, a, b, tmp)
    case 1:
        tmp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tmp, cpu_env, offsetof(CPUState, GE));
        PAS_OP(s)
        break;
    case 5:
        tmp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tmp, cpu_env, offsetof(CPUState, GE));
        PAS_OP(u)
        break;
#undef gen_pas_helper
#define gen_pas_helper(name) glue(gen_helper_,name)(a, a, b)
    case 2:
        PAS_OP(q);
        break;
    case 3:
        PAS_OP(sh);
        break;
    case 6:
        PAS_OP(uq);
        break;
    case 7:
        PAS_OP(uh);
        break;
#undef gen_pas_helper
    }
}
#undef PAS_OP

/* For unknown reasons Arm and Thumb-2 use arbitrarily different encodings.  */
#define PAS_OP(pfx) \
    switch (op2) {  \
    case 0: gen_pas_helper(glue(pfx,add8)); break; \
    case 1: gen_pas_helper(glue(pfx,add16)); break; \
    case 2: gen_pas_helper(glue(pfx,addsubx)); break; \
    case 4: gen_pas_helper(glue(pfx,sub8)); break; \
    case 5: gen_pas_helper(glue(pfx,sub16)); break; \
    case 6: gen_pas_helper(glue(pfx,subaddx)); break; \
    }
static void gen_thumb2_parallel_addsub(int op1, int op2, TCGv a, TCGv b)
{
    TCGv_ptr tmp;

    switch (op1) {
#define gen_pas_helper(name) glue(gen_helper_,name)(a, a, b, tmp)
    case 0:
        tmp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tmp, cpu_env, offsetof(CPUState, GE));
        PAS_OP(s)
        break;
    case 4:
        tmp = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(tmp, cpu_env, offsetof(CPUState, GE));
        PAS_OP(u)
        break;
#undef gen_pas_helper
#define gen_pas_helper(name) glue(gen_helper_,name)(a, a, b)
    case 1:
        PAS_OP(q);
        break;
    case 2:
        PAS_OP(sh);
        break;
    case 5:
        PAS_OP(uq);
        break;
    case 6:
        PAS_OP(uh);
        break;
#undef gen_pas_helper
    }
}
#undef PAS_OP

static void gen_test_cc(int cc, int label)
{
    TCGv tmp;
    TCGv tmp2;
    int inv;

    switch (cc) {
    case 0: /* eq: Z */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 1: /* ne: !Z */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        break;
    case 2: /* cs: C */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        break;
    case 3: /* cc: !C */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 4: /* mi: N */
        tmp = load_cpu_field(NF);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 5: /* pl: !N */
        tmp = load_cpu_field(NF);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 6: /* vs: V */
        tmp = load_cpu_field(VF);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 7: /* vc: !V */
        tmp = load_cpu_field(VF);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 8: /* hi: C && !Z */
        inv = gen_new_label();
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, inv);
        dead_tmp(tmp);
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, label);
        gen_set_label(inv);
        break;
    case 9: /* ls: !C || Z */
        tmp = load_cpu_field(CF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        dead_tmp(tmp);
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        break;
    case 10: /* ge: N == V -> N ^ V == 0 */
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        break;
    case 11: /* lt: N != V -> N ^ V != 0 */
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    case 12: /* gt: !Z && N == V */
        inv = gen_new_label();
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, inv);
        dead_tmp(tmp);
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, label);
        gen_set_label(inv);
        break;
    case 13: /* le: Z || N != V */
        tmp = load_cpu_field(ZF);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        dead_tmp(tmp);
        tmp = load_cpu_field(VF);
        tmp2 = load_cpu_field(NF);
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        dead_tmp(tmp2);
        tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, label);
        break;
    default:
        fprintf(stderr, "Bad condition code 0x%x\n", cc);
        abort();
    }
    dead_tmp(tmp);
}

static const uint8_t table_logic_cc[16] = {
    1, /* and */
    1, /* xor */
    0, /* sub */
    0, /* rsb */
    0, /* add */
    0, /* adc */
    0, /* sbc */
    0, /* rsc */
    1, /* andl */
    1, /* xorl */
    0, /* cmp */
    0, /* cmn */
    1, /* orr */
    1, /* mov */
    1, /* bic */
    1, /* mvn */
};

/* Set PC and Thumb state from an immediate address.  */
static inline void gen_bx_im(DisasContext *s, uint32_t addr)
{
    TCGv tmp;

    s->is_jmp = DISAS_UPDATE;
    tmp = new_tmp();
    if (s->thumb != (addr & 1)) {
        tcg_gen_movi_i32(tmp, addr & 1);
        tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUState, thumb));
    }
    tcg_gen_movi_i32(tmp, addr & ~1);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUState, regs[15]));
    dead_tmp(tmp);
}

/* Set PC and Thumb state from var.  var is marked as dead.  */
static inline void gen_bx(DisasContext *s, TCGv var)
{
    TCGv tmp;

    s->is_jmp = DISAS_UPDATE;
    tmp = new_tmp();
    tcg_gen_andi_i32(tmp, var, 1);
    store_cpu_field(tmp, thumb);
    tcg_gen_andi_i32(var, var, ~1);
    store_cpu_field(var, regs[15]);
}

/* TODO: This should be removed.  Use gen_bx instead.  */
static inline void gen_bx_T0(DisasContext *s)
{
    TCGv tmp = new_tmp();
    tcg_gen_mov_i32(tmp, cpu_T[0]);
    gen_bx(s, tmp);
}

static inline TCGv gen_ld8s(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld8s(tmp, addr, index);
    return tmp;
}
static inline TCGv gen_ld8u(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld8u(tmp, addr, index);
    return tmp;
}
static inline TCGv gen_ld16s(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld16s(tmp, addr, index);
    return tmp;
}
static inline TCGv gen_ld16u(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld16u(tmp, addr, index);
    return tmp;
}
static inline TCGv gen_ld32(TCGv addr, int index)
{
    TCGv tmp = new_tmp();
    tcg_gen_qemu_ld32u(tmp, addr, index);
    return tmp;
}
static inline void gen_st8(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st8(val, addr, index);
    dead_tmp(val);
}
static inline void gen_st16(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st16(val, addr, index);
    dead_tmp(val);
}
static inline void gen_st32(TCGv val, TCGv addr, int index)
{
    tcg_gen_qemu_st32(val, addr, index);
    dead_tmp(val);
}

static inline void gen_movl_T0_reg(DisasContext *s, int reg)
{
    load_reg_var(s, cpu_T[0], reg);
}

static inline void gen_movl_T1_reg(DisasContext *s, int reg)
{
    load_reg_var(s, cpu_T[1], reg);
}

static inline void gen_movl_T2_reg(DisasContext *s, int reg)
{
    load_reg_var(s, cpu_T[2], reg);
}

static inline void gen_set_pc_im(uint32_t val)
{
    TCGv tmp = new_tmp();
    tcg_gen_movi_i32(tmp, val);
    store_cpu_field(tmp, regs[15]);
}

static inline void gen_movl_reg_TN(DisasContext *s, int reg, int t)
{
    TCGv tmp;
    if (reg == 15) {
        tmp = new_tmp();
        tcg_gen_andi_i32(tmp, cpu_T[t], ~1);
    } else {
        tmp = cpu_T[t];
    }
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUState, regs[reg]));
    if (reg == 15) {
        dead_tmp(tmp);
        s->is_jmp = DISAS_JUMP;
    }
}

static inline void gen_movl_reg_T0(DisasContext *s, int reg)
{
    gen_movl_reg_TN(s, reg, 0);
}

static inline void gen_movl_reg_T1(DisasContext *s, int reg)
{
    gen_movl_reg_TN(s, reg, 1);
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static inline void gen_lookup_tb(DisasContext *s)
{
    gen_op_movl_T0_im(s->pc);
    gen_movl_reg_T0(s, 15);
    s->is_jmp = DISAS_UPDATE;
}

static inline void gen_add_data_offset(DisasContext *s, unsigned int insn,
                                       TCGv var)
{
    int val, rm, shift, shiftop;
    TCGv offset;

    if (!(insn & (1 << 25))) {
        /* immediate */
        val = insn & 0xfff;
        if (!(insn & (1 << 23)))
            val = -val;
        if (val != 0)
            tcg_gen_addi_i32(var, var, val);
    } else {
        /* shift/register */
        rm = (insn) & 0xf;
        shift = (insn >> 7) & 0x1f;
        shiftop = (insn >> 5) & 3;
        offset = load_reg(s, rm);
        gen_arm_shift_im(offset, shiftop, shift, 0);
        if (!(insn & (1 << 23)))
            tcg_gen_sub_i32(var, var, offset);
        else
            tcg_gen_add_i32(var, var, offset);
        dead_tmp(offset);
    }
}

static inline void gen_add_datah_offset(DisasContext *s, unsigned int insn,
                                        int extra, TCGv var)
{
    int val, rm;
    TCGv offset;

    if (insn & (1 << 22)) {
        /* immediate */
        val = (insn & 0xf) | ((insn >> 4) & 0xf0);
        if (!(insn & (1 << 23)))
            val = -val;
        val += extra;
        if (val != 0)
            tcg_gen_addi_i32(var, var, val);
    } else {
        /* register */
        if (extra)
            tcg_gen_addi_i32(var, var, extra);
        rm = (insn) & 0xf;
        offset = load_reg(s, rm);
        if (!(insn & (1 << 23)))
            tcg_gen_sub_i32(var, var, offset);
        else
            tcg_gen_add_i32(var, var, offset);
        dead_tmp(offset);
    }
}

#define VFP_OP2(name)                                                 \
static inline void gen_vfp_##name(int dp)                             \
{                                                                     \
    if (dp)                                                           \
        gen_helper_vfp_##name##d(cpu_F0d, cpu_F0d, cpu_F1d, cpu_env); \
    else                                                              \
        gen_helper_vfp_##name##s(cpu_F0s, cpu_F0s, cpu_F1s, cpu_env); \
}

VFP_OP2(add)
VFP_OP2(sub)
VFP_OP2(mul)
VFP_OP2(div)

#undef VFP_OP2

static inline void gen_vfp_abs(int dp)
{
    if (dp)
        gen_helper_vfp_absd(cpu_F0d, cpu_F0d);
    else
        gen_helper_vfp_abss(cpu_F0s, cpu_F0s);
}

static inline void gen_vfp_neg(int dp)
{
    if (dp)
        gen_helper_vfp_negd(cpu_F0d, cpu_F0d);
    else
        gen_helper_vfp_negs(cpu_F0s, cpu_F0s);
}

static inline void gen_vfp_sqrt(int dp)
{
    if (dp)
        gen_helper_vfp_sqrtd(cpu_F0d, cpu_F0d, cpu_env);
    else
        gen_helper_vfp_sqrts(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_cmp(int dp)
{
    if (dp)
        gen_helper_vfp_cmpd(cpu_F0d, cpu_F1d, cpu_env);
    else
        gen_helper_vfp_cmps(cpu_F0s, cpu_F1s, cpu_env);
}

static inline void gen_vfp_cmpe(int dp)
{
    if (dp)
        gen_helper_vfp_cmped(cpu_F0d, cpu_F1d, cpu_env);
    else
        gen_helper_vfp_cmpes(cpu_F0s, cpu_F1s, cpu_env);
}

static inline void gen_vfp_F1_ld0(int dp)
{
    if (dp)
        tcg_gen_movi_i64(cpu_F1d, 0);
    else
        tcg_gen_movi_i32(cpu_F1s, 0);
}

static inline void gen_vfp_uito(int dp)
{
    if (dp)
        gen_helper_vfp_uitod(cpu_F0d, cpu_F0s, cpu_env);
    else
        gen_helper_vfp_uitos(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_sito(int dp)
{
    if (dp)
        gen_helper_vfp_sitod(cpu_F0d, cpu_F0s, cpu_env);
    else
        gen_helper_vfp_sitos(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_toui(int dp)
{
    if (dp)
        gen_helper_vfp_touid(cpu_F0s, cpu_F0d, cpu_env);
    else
        gen_helper_vfp_touis(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_touiz(int dp)
{
    if (dp)
        gen_helper_vfp_touizd(cpu_F0s, cpu_F0d, cpu_env);
    else
        gen_helper_vfp_touizs(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_tosi(int dp)
{
    if (dp)
        gen_helper_vfp_tosid(cpu_F0s, cpu_F0d, cpu_env);
    else
        gen_helper_vfp_tosis(cpu_F0s, cpu_F0s, cpu_env);
}

static inline void gen_vfp_tosiz(int dp)
{
    if (dp)
        gen_helper_vfp_tosizd(cpu_F0s, cpu_F0d, cpu_env);
    else
        gen_helper_vfp_tosizs(cpu_F0s, cpu_F0s, cpu_env);
}

#define VFP_GEN_FIX(name) \
static inline void gen_vfp_##name(int dp, int shift) \
{ \
    if (dp) \
        gen_helper_vfp_##name##d(cpu_F0d, cpu_F0d, tcg_const_i32(shift), cpu_env);\
    else \
        gen_helper_vfp_##name##s(cpu_F0s, cpu_F0s, tcg_const_i32(shift), cpu_env);\
}
VFP_GEN_FIX(tosh)
VFP_GEN_FIX(tosl)
VFP_GEN_FIX(touh)
VFP_GEN_FIX(toul)
VFP_GEN_FIX(shto)
VFP_GEN_FIX(slto)
VFP_GEN_FIX(uhto)
VFP_GEN_FIX(ulto)
#undef VFP_GEN_FIX

static inline void gen_vfp_ld(DisasContext *s, int dp)
{
    if (dp)
        tcg_gen_qemu_ld64(cpu_F0d, cpu_T[1], IS_USER(s));
    else
        tcg_gen_qemu_ld32u(cpu_F0s, cpu_T[1], IS_USER(s));
}

static inline void gen_vfp_st(DisasContext *s, int dp)
{
    if (dp)
        tcg_gen_qemu_st64(cpu_F0d, cpu_T[1], IS_USER(s));
    else
        tcg_gen_qemu_st32(cpu_F0s, cpu_T[1], IS_USER(s));
}

static inline long
vfp_reg_offset (int dp, int reg)
{
    if (dp)
        return offsetof(CPUARMState, vfp.regs[reg]);
    else if (reg & 1) {
        return offsetof(CPUARMState, vfp.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.upper);
    } else {
        return offsetof(CPUARMState, vfp.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.lower);
    }
}

/* Return the offset of a 32-bit piece of a NEON register.
   zero is the least significant end of the register.  */
static inline long
neon_reg_offset (int reg, int n)
{
    int sreg;
    sreg = reg * 2 + n;
    return vfp_reg_offset(0, sreg);
}

/* FIXME: Remove these.  */
#define neon_T0 cpu_T[0]
#define neon_T1 cpu_T[1]
#define NEON_GET_REG(T, reg, n) \
  tcg_gen_ld_i32(neon_##T, cpu_env, neon_reg_offset(reg, n))
#define NEON_SET_REG(T, reg, n) \
  tcg_gen_st_i32(neon_##T, cpu_env, neon_reg_offset(reg, n))

static TCGv neon_load_reg(int reg, int pass)
{
    TCGv tmp = new_tmp();
    tcg_gen_ld_i32(tmp, cpu_env, neon_reg_offset(reg, pass));
    return tmp;
}

static void neon_store_reg(int reg, int pass, TCGv var)
{
    tcg_gen_st_i32(var, cpu_env, neon_reg_offset(reg, pass));
    dead_tmp(var);
}

static inline void neon_load_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_ld_i64(var, cpu_env, vfp_reg_offset(1, reg));
}

static inline void neon_store_reg64(TCGv_i64 var, int reg)
{
    tcg_gen_st_i64(var, cpu_env, vfp_reg_offset(1, reg));
}

#define tcg_gen_ld_f32 tcg_gen_ld_i32
#define tcg_gen_ld_f64 tcg_gen_ld_i64
#define tcg_gen_st_f32 tcg_gen_st_i32
#define tcg_gen_st_f64 tcg_gen_st_i64

static inline void gen_mov_F0_vreg(int dp, int reg)
{
    if (dp)
        tcg_gen_ld_f64(cpu_F0d, cpu_env, vfp_reg_offset(dp, reg));
    else
        tcg_gen_ld_f32(cpu_F0s, cpu_env, vfp_reg_offset(dp, reg));
}

static inline void gen_mov_F1_vreg(int dp, int reg)
{
    if (dp)
        tcg_gen_ld_f64(cpu_F1d, cpu_env, vfp_reg_offset(dp, reg));
    else
        tcg_gen_ld_f32(cpu_F1s, cpu_env, vfp_reg_offset(dp, reg));
}

static inline void gen_mov_vreg_F0(int dp, int reg)
{
    if (dp)
        tcg_gen_st_f64(cpu_F0d, cpu_env, vfp_reg_offset(dp, reg));
    else
        tcg_gen_st_f32(cpu_F0s, cpu_env, vfp_reg_offset(dp, reg));
}

#define ARM_CP_RW_BIT	(1 << 20)

static inline void iwmmxt_load_reg(TCGv_i64 var, int reg)
{
    tcg_gen_ld_i64(var, cpu_env, offsetof(CPUState, iwmmxt.regs[reg]));
}

static inline void iwmmxt_store_reg(TCGv_i64 var, int reg)
{
    tcg_gen_st_i64(var, cpu_env, offsetof(CPUState, iwmmxt.regs[reg]));
}

static inline void gen_op_iwmmxt_movl_wCx_T0(int reg)
{
    tcg_gen_st_i32(cpu_T[0], cpu_env, offsetof(CPUState, iwmmxt.cregs[reg]));
}

static inline void gen_op_iwmmxt_movl_T0_wCx(int reg)
{
    tcg_gen_ld_i32(cpu_T[0], cpu_env, offsetof(CPUState, iwmmxt.cregs[reg]));
}

static inline void gen_op_iwmmxt_movl_T1_wCx(int reg)
{
    tcg_gen_ld_i32(cpu_T[1], cpu_env, offsetof(CPUState, iwmmxt.cregs[reg]));
}

static inline void gen_op_iwmmxt_movq_wRn_M0(int rn)
{
    iwmmxt_store_reg(cpu_M0, rn);
}

static inline void gen_op_iwmmxt_movq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_M0, rn);
}

static inline void gen_op_iwmmxt_orq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_or_i64(cpu_M0, cpu_M0, cpu_V1);
}

static inline void gen_op_iwmmxt_andq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_and_i64(cpu_M0, cpu_M0, cpu_V1);
}

static inline void gen_op_iwmmxt_xorq_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_xor_i64(cpu_M0, cpu_M0, cpu_V1);
}

#define IWMMXT_OP(name) \
static inline void gen_op_iwmmxt_##name##_M0_wRn(int rn) \
{ \
    iwmmxt_load_reg(cpu_V1, rn); \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_M0, cpu_V1); \
}

#define IWMMXT_OP_ENV(name) \
static inline void gen_op_iwmmxt_##name##_M0_wRn(int rn) \
{ \
    iwmmxt_load_reg(cpu_V1, rn); \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_env, cpu_M0, cpu_V1); \
}

#define IWMMXT_OP_ENV_SIZE(name) \
IWMMXT_OP_ENV(name##b) \
IWMMXT_OP_ENV(name##w) \
IWMMXT_OP_ENV(name##l)

#define IWMMXT_OP_ENV1(name) \
static inline void gen_op_iwmmxt_##name##_M0(void) \
{ \
    gen_helper_iwmmxt_##name(cpu_M0, cpu_env, cpu_M0); \
}

IWMMXT_OP(maddsq)
IWMMXT_OP(madduq)
IWMMXT_OP(sadb)
IWMMXT_OP(sadw)
IWMMXT_OP(mulslw)
IWMMXT_OP(mulshw)
IWMMXT_OP(mululw)
IWMMXT_OP(muluhw)
IWMMXT_OP(macsw)
IWMMXT_OP(macuw)

IWMMXT_OP_ENV_SIZE(unpackl)
IWMMXT_OP_ENV_SIZE(unpackh)

IWMMXT_OP_ENV1(unpacklub)
IWMMXT_OP_ENV1(unpackluw)
IWMMXT_OP_ENV1(unpacklul)
IWMMXT_OP_ENV1(unpackhub)
IWMMXT_OP_ENV1(unpackhuw)
IWMMXT_OP_ENV1(unpackhul)
IWMMXT_OP_ENV1(unpacklsb)
IWMMXT_OP_ENV1(unpacklsw)
IWMMXT_OP_ENV1(unpacklsl)
IWMMXT_OP_ENV1(unpackhsb)
IWMMXT_OP_ENV1(unpackhsw)
IWMMXT_OP_ENV1(unpackhsl)

IWMMXT_OP_ENV_SIZE(cmpeq)
IWMMXT_OP_ENV_SIZE(cmpgtu)
IWMMXT_OP_ENV_SIZE(cmpgts)

IWMMXT_OP_ENV_SIZE(mins)
IWMMXT_OP_ENV_SIZE(minu)
IWMMXT_OP_ENV_SIZE(maxs)
IWMMXT_OP_ENV_SIZE(maxu)

IWMMXT_OP_ENV_SIZE(subn)
IWMMXT_OP_ENV_SIZE(addn)
IWMMXT_OP_ENV_SIZE(subu)
IWMMXT_OP_ENV_SIZE(addu)
IWMMXT_OP_ENV_SIZE(subs)
IWMMXT_OP_ENV_SIZE(adds)

IWMMXT_OP_ENV(avgb0)
IWMMXT_OP_ENV(avgb1)
IWMMXT_OP_ENV(avgw0)
IWMMXT_OP_ENV(avgw1)

IWMMXT_OP(msadb)

IWMMXT_OP_ENV(packuw)
IWMMXT_OP_ENV(packul)
IWMMXT_OP_ENV(packuq)
IWMMXT_OP_ENV(packsw)
IWMMXT_OP_ENV(packsl)
IWMMXT_OP_ENV(packsq)

static inline void gen_op_iwmmxt_muladdsl_M0_T0_T1(void)
{
    gen_helper_iwmmxt_muladdsl(cpu_M0, cpu_M0, cpu_T[0], cpu_T[1]);
}

static inline void gen_op_iwmmxt_muladdsw_M0_T0_T1(void)
{
    gen_helper_iwmmxt_muladdsw(cpu_M0, cpu_M0, cpu_T[0], cpu_T[1]);
}

static inline void gen_op_iwmmxt_muladdswl_M0_T0_T1(void)
{
    gen_helper_iwmmxt_muladdswl(cpu_M0, cpu_M0, cpu_T[0], cpu_T[1]);
}

static inline void gen_op_iwmmxt_align_M0_T0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    gen_helper_iwmmxt_align(cpu_M0, cpu_M0, cpu_V1, cpu_T[0]);
}

static inline void gen_op_iwmmxt_insr_M0_T0_T1(int shift)
{
    TCGv tmp = tcg_const_i32(shift);
    gen_helper_iwmmxt_insr(cpu_M0, cpu_M0, cpu_T[0], cpu_T[1], tmp);
}

static inline void gen_op_iwmmxt_extrsb_T0_M0(int shift)
{
    tcg_gen_shri_i64(cpu_M0, cpu_M0, shift);
    tcg_gen_trunc_i64_i32(cpu_T[0], cpu_M0);
    tcg_gen_ext8s_i32(cpu_T[0], cpu_T[0]);
}

static inline void gen_op_iwmmxt_extrsw_T0_M0(int shift)
{
    tcg_gen_shri_i64(cpu_M0, cpu_M0, shift);
    tcg_gen_trunc_i64_i32(cpu_T[0], cpu_M0);
    tcg_gen_ext16s_i32(cpu_T[0], cpu_T[0]);
}

static inline void gen_op_iwmmxt_extru_T0_M0(int shift, uint32_t mask)
{
    tcg_gen_shri_i64(cpu_M0, cpu_M0, shift);
    tcg_gen_trunc_i64_i32(cpu_T[0], cpu_M0);
    if (mask != ~0u)
        tcg_gen_andi_i32(cpu_T[0], cpu_T[0], mask);
}

static void gen_op_iwmmxt_set_mup(void)
{
    TCGv tmp;
    tmp = load_cpu_field(iwmmxt.cregs[ARM_IWMMXT_wCon]);
    tcg_gen_ori_i32(tmp, tmp, 2);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCon]);
}

static void gen_op_iwmmxt_set_cup(void)
{
    TCGv tmp;
    tmp = load_cpu_field(iwmmxt.cregs[ARM_IWMMXT_wCon]);
    tcg_gen_ori_i32(tmp, tmp, 1);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCon]);
}

static void gen_op_iwmmxt_setpsr_nz(void)
{
    TCGv tmp = new_tmp();
    gen_helper_iwmmxt_setpsr_nz(tmp, cpu_M0);
    store_cpu_field(tmp, iwmmxt.cregs[ARM_IWMMXT_wCASF]);
}

static inline void gen_op_iwmmxt_addl_M0_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V1, rn);
    tcg_gen_ext32u_i64(cpu_V1, cpu_V1);
    tcg_gen_add_i64(cpu_M0, cpu_M0, cpu_V1);
}


static void gen_iwmmxt_movl_T0_T1_wRn(int rn)
{
    iwmmxt_load_reg(cpu_V0, rn);
    tcg_gen_trunc_i64_i32(cpu_T[0], cpu_V0);
    tcg_gen_shri_i64(cpu_V0, cpu_V0, 32);
    tcg_gen_trunc_i64_i32(cpu_T[1], cpu_V0);
}

static void gen_iwmmxt_movl_wRn_T0_T1(int rn)
{
    tcg_gen_concat_i32_i64(cpu_V0, cpu_T[0], cpu_T[1]);
    iwmmxt_store_reg(cpu_V0, rn);
}

static inline int gen_iwmmxt_address(DisasContext *s, uint32_t insn)
{
    int rd;
    uint32_t offset;

    rd = (insn >> 16) & 0xf;
    gen_movl_T1_reg(s, rd);

    offset = (insn & 0xff) << ((insn >> 7) & 2);
    if (insn & (1 << 24)) {
        /* Pre indexed */
        if (insn & (1 << 23))
            gen_op_addl_T1_im(offset);
        else
            gen_op_addl_T1_im(-offset);

        if (insn & (1 << 21))
            gen_movl_reg_T1(s, rd);
    } else if (insn & (1 << 21)) {
        /* Post indexed */
        if (insn & (1 << 23))
            gen_op_movl_T0_im(offset);
        else
            gen_op_movl_T0_im(- offset);
        gen_op_addl_T0_T1();
        gen_movl_reg_T0(s, rd);
    } else if (!(insn & (1 << 23)))
        return 1;
    return 0;
}

static inline int gen_iwmmxt_shift(uint32_t insn, uint32_t mask)
{
    int rd = (insn >> 0) & 0xf;

    if (insn & (1 << 8))
        if (rd < ARM_IWMMXT_wCGR0 || rd > ARM_IWMMXT_wCGR3)
            return 1;
        else
            gen_op_iwmmxt_movl_T0_wCx(rd);
    else
        gen_iwmmxt_movl_T0_T1_wRn(rd);

    gen_op_movl_T1_im(mask);
    gen_op_andl_T0_T1();
    return 0;
}

/* Disassemble an iwMMXt instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_iwmmxt_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    int rd, wrd;
    int rdhi, rdlo, rd0, rd1, i;
    TCGv tmp;

    if ((insn & 0x0e000e00) == 0x0c000000) {
        if ((insn & 0x0fe00ff0) == 0x0c400000) {
            wrd = insn & 0xf;
            rdlo = (insn >> 12) & 0xf;
            rdhi = (insn >> 16) & 0xf;
            if (insn & ARM_CP_RW_BIT) {			/* TMRRC */
                gen_iwmmxt_movl_T0_T1_wRn(wrd);
                gen_movl_reg_T0(s, rdlo);
                gen_movl_reg_T1(s, rdhi);
            } else {					/* TMCRR */
                gen_movl_T0_reg(s, rdlo);
                gen_movl_T1_reg(s, rdhi);
                gen_iwmmxt_movl_wRn_T0_T1(wrd);
                gen_op_iwmmxt_set_mup();
            }
            return 0;
        }

        wrd = (insn >> 12) & 0xf;
        if (gen_iwmmxt_address(s, insn))
            return 1;
        if (insn & ARM_CP_RW_BIT) {
            if ((insn >> 28) == 0xf) {			/* WLDRW wCx */
                tmp = gen_ld32(cpu_T[1], IS_USER(s));
                tcg_gen_mov_i32(cpu_T[0], tmp);
                dead_tmp(tmp);
                gen_op_iwmmxt_movl_wCx_T0(wrd);
            } else {
                i = 1;
                if (insn & (1 << 8)) {
                    if (insn & (1 << 22)) {		/* WLDRD */
                        tcg_gen_qemu_ld64(cpu_M0, cpu_T[1], IS_USER(s));
                        i = 0;
                    } else {				/* WLDRW wRd */
                        tmp = gen_ld32(cpu_T[1], IS_USER(s));
                    }
                } else {
                    if (insn & (1 << 22)) {		/* WLDRH */
                        tmp = gen_ld16u(cpu_T[1], IS_USER(s));
                    } else {				/* WLDRB */
                        tmp = gen_ld8u(cpu_T[1], IS_USER(s));
                    }
                }
                if (i) {
                    tcg_gen_extu_i32_i64(cpu_M0, tmp);
                    dead_tmp(tmp);
                }
                gen_op_iwmmxt_movq_wRn_M0(wrd);
            }
        } else {
            if ((insn >> 28) == 0xf) {			/* WSTRW wCx */
                gen_op_iwmmxt_movl_T0_wCx(wrd);
                tmp = new_tmp();
                tcg_gen_mov_i32(tmp, cpu_T[0]);
                gen_st32(tmp, cpu_T[1], IS_USER(s));
            } else {
                gen_op_iwmmxt_movq_M0_wRn(wrd);
                tmp = new_tmp();
                if (insn & (1 << 8)) {
                    if (insn & (1 << 22)) {		/* WSTRD */
                        dead_tmp(tmp);
                        tcg_gen_qemu_st64(cpu_M0, cpu_T[1], IS_USER(s));
                    } else {				/* WSTRW wRd */
                        tcg_gen_trunc_i64_i32(tmp, cpu_M0);
                        gen_st32(tmp, cpu_T[1], IS_USER(s));
                    }
                } else {
                    if (insn & (1 << 22)) {		/* WSTRH */
                        tcg_gen_trunc_i64_i32(tmp, cpu_M0);
                        gen_st16(tmp, cpu_T[1], IS_USER(s));
                    } else {				/* WSTRB */
                        tcg_gen_trunc_i64_i32(tmp, cpu_M0);
                        gen_st8(tmp, cpu_T[1], IS_USER(s));
                    }
                }
            }
        }
        return 0;
    }

    if ((insn & 0x0f000000) != 0x0e000000)
        return 1;

    switch (((insn >> 12) & 0xf00) | ((insn >> 4) & 0xff)) {
    case 0x000:						/* WOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_orq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x011:						/* TMCR */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        switch (wrd) {
        case ARM_IWMMXT_wCID:
        case ARM_IWMMXT_wCASF:
            break;
        case ARM_IWMMXT_wCon:
            gen_op_iwmmxt_set_cup();
            /* Fall through.  */
        case ARM_IWMMXT_wCSSF:
            gen_op_iwmmxt_movl_T0_wCx(wrd);
            gen_movl_T1_reg(s, rd);
            gen_op_bicl_T0_T1();
            gen_op_iwmmxt_movl_wCx_T0(wrd);
            break;
        case ARM_IWMMXT_wCGR0:
        case ARM_IWMMXT_wCGR1:
        case ARM_IWMMXT_wCGR2:
        case ARM_IWMMXT_wCGR3:
            gen_op_iwmmxt_set_cup();
            gen_movl_reg_T0(s, rd);
            gen_op_iwmmxt_movl_wCx_T0(wrd);
            break;
        default:
            return 1;
        }
        break;
    case 0x100:						/* WXOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_xorq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x111:						/* TMRC */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movl_T0_wCx(wrd);
        gen_movl_reg_T0(s, rd);
        break;
    case 0x300:						/* WANDN */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        tcg_gen_neg_i64(cpu_M0, cpu_M0);
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x200:						/* WAND */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x810: case 0xa10:				/* WMADD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_maddsq_M0_wRn(rd1);
        else
            gen_op_iwmmxt_madduq_M0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x10e: case 0x50e: case 0x90e: case 0xd0e:	/* WUNPCKIL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpacklb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpacklw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackll_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x10c: case 0x50c: case 0x90c: case 0xd0c:	/* WUNPCKIH */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpackhb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpackhw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackhl_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x012: case 0x112: case 0x412: case 0x512:	/* WSAD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22))
            gen_op_iwmmxt_sadw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_sadb_M0_wRn(rd1);
        if (!(insn & (1 << 20)))
            gen_op_iwmmxt_addl_M0_wRn(wrd);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x010: case 0x110: case 0x210: case 0x310:	/* WMUL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21)) {
            if (insn & (1 << 20))
                gen_op_iwmmxt_mulshw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_mulslw_M0_wRn(rd1);
        } else {
            if (insn & (1 << 20))
                gen_op_iwmmxt_muluhw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_mululw_M0_wRn(rd1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x410: case 0x510: case 0x610: case 0x710:	/* WMAC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_macsw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_macuw_M0_wRn(rd1);
        if (!(insn & (1 << 20))) {
            iwmmxt_load_reg(cpu_V1, wrd);
            tcg_gen_add_i64(cpu_M0, cpu_M0, cpu_V1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x006: case 0x406: case 0x806: case 0xc06:	/* WCMPEQ */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_cmpeqb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_cmpeqw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_cmpeql_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x800: case 0x900: case 0xc00: case 0xd00:	/* WAVG2 */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22)) {
            if (insn & (1 << 20))
                gen_op_iwmmxt_avgw1_M0_wRn(rd1);
            else
                gen_op_iwmmxt_avgw0_M0_wRn(rd1);
        } else {
            if (insn & (1 << 20))
                gen_op_iwmmxt_avgb1_M0_wRn(rd1);
            else
                gen_op_iwmmxt_avgb0_M0_wRn(rd1);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x802: case 0x902: case 0xa02: case 0xb02:	/* WALIGNR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_movl_T0_wCx(ARM_IWMMXT_wCGR0 + ((insn >> 20) & 3));
        gen_op_movl_T1_im(7);
        gen_op_andl_T0_T1();
        gen_op_iwmmxt_align_M0_T0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x601: case 0x605: case 0x609: case 0x60d:	/* TINSR */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_movl_T0_reg(s, rd);
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 6) & 3) {
        case 0:
            gen_op_movl_T1_im(0xff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 7) << 3);
            break;
        case 1:
            gen_op_movl_T1_im(0xffff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 3) << 4);
            break;
        case 2:
            gen_op_movl_T1_im(0xffffffff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 1) << 5);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x107: case 0x507: case 0x907: case 0xd07:	/* TEXTRM */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        if (rd == 15)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & 8)
                gen_op_iwmmxt_extrsb_T0_M0((insn & 7) << 3);
            else {
                gen_op_iwmmxt_extru_T0_M0((insn & 7) << 3, 0xff);
            }
            break;
        case 1:
            if (insn & 8)
                gen_op_iwmmxt_extrsw_T0_M0((insn & 3) << 4);
            else {
                gen_op_iwmmxt_extru_T0_M0((insn & 3) << 4, 0xffff);
            }
            break;
        case 2:
            gen_op_iwmmxt_extru_T0_M0((insn & 1) << 5, ~0u);
            break;
        case 3:
            return 1;
        }
        gen_movl_reg_T0(s, rd);
        break;
    case 0x117: case 0x517: case 0x917: case 0xd17:	/* TEXTRC */
        if ((insn & 0x000ff008) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_shrl_T1_im(((insn & 7) << 2) + 0);
            break;
        case 1:
            gen_op_shrl_T1_im(((insn & 3) << 3) + 4);
            break;
        case 2:
            gen_op_shrl_T1_im(((insn & 1) << 4) + 12);
            break;
        case 3:
            return 1;
        }
        gen_op_shll_T1_im(28);
        gen_set_nzcv(cpu_T[1]);
        break;
    case 0x401: case 0x405: case 0x409: case 0x40d:	/* TBCST */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_movl_T0_reg(s, rd);
        switch ((insn >> 6) & 3) {
        case 0:
            gen_helper_iwmmxt_bcstb(cpu_M0, cpu_T[0]);
            break;
        case 1:
            gen_helper_iwmmxt_bcstw(cpu_M0, cpu_T[0]);
            break;
        case 2:
            gen_helper_iwmmxt_bcstl(cpu_M0, cpu_T[0]);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x113: case 0x513: case 0x913: case 0xd13:	/* TANDC */
        if ((insn & 0x000ff00f) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                gen_op_shll_T1_im(4);
                gen_op_andl_T0_T1();
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                gen_op_shll_T1_im(8);
                gen_op_andl_T0_T1();
            }
            break;
        case 2:
            gen_op_shll_T1_im(16);
            gen_op_andl_T0_T1();
            break;
        case 3:
            return 1;
        }
        gen_set_nzcv(cpu_T[0]);
        break;
    case 0x01c: case 0x41c: case 0x81c: case 0xc1c:	/* WACC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_helper_iwmmxt_addcb(cpu_M0, cpu_M0);
            break;
        case 1:
            gen_helper_iwmmxt_addcw(cpu_M0, cpu_M0);
            break;
        case 2:
            gen_helper_iwmmxt_addcl(cpu_M0, cpu_M0);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x115: case 0x515: case 0x915: case 0xd15:	/* TORC */
        if ((insn & 0x000ff00f) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                gen_op_shll_T1_im(4);
                gen_op_orl_T0_T1();
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                gen_op_shll_T1_im(8);
                gen_op_orl_T0_T1();
            }
            break;
        case 2:
            gen_op_shll_T1_im(16);
            gen_op_orl_T0_T1();
            break;
        case 3:
            return 1;
        }
        gen_set_nzcv(cpu_T[0]);
        break;
    case 0x103: case 0x503: case 0x903: case 0xd03:	/* TMOVMSK */
        rd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        if ((insn & 0xf) != 0)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_helper_iwmmxt_msbb(cpu_T[0], cpu_M0);
            break;
        case 1:
            gen_helper_iwmmxt_msbw(cpu_T[0], cpu_M0);
            break;
        case 2:
            gen_helper_iwmmxt_msbl(cpu_T[0], cpu_M0);
            break;
        case 3:
            return 1;
        }
        gen_movl_reg_T0(s, rd);
        break;
    case 0x106: case 0x306: case 0x506: case 0x706:	/* WCMPGT */
    case 0x906: case 0xb06: case 0xd06: case 0xf06:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00e: case 0x20e: case 0x40e: case 0x60e:	/* WUNPCKEL */
    case 0x80e: case 0xa0e: case 0xc0e: case 0xe0e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsb_M0();
            else
                gen_op_iwmmxt_unpacklub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsw_M0();
            else
                gen_op_iwmmxt_unpackluw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsl_M0();
            else
                gen_op_iwmmxt_unpacklul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00c: case 0x20c: case 0x40c: case 0x60c:	/* WUNPCKEH */
    case 0x80c: case 0xa0c: case 0xc0c: case 0xe0c:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsb_M0();
            else
                gen_op_iwmmxt_unpackhub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsw_M0();
            else
                gen_op_iwmmxt_unpackhuw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsl_M0();
            else
                gen_op_iwmmxt_unpackhul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x204: case 0x604: case 0xa04: case 0xe04:	/* WSRL */
    case 0x214: case 0x614: case 0xa14: case 0xe14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_helper_iwmmxt_srlw(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 2:
            gen_helper_iwmmxt_srll(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 3:
            gen_helper_iwmmxt_srlq(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x004: case 0x404: case 0x804: case 0xc04:	/* WSRA */
    case 0x014: case 0x414: case 0x814: case 0xc14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_helper_iwmmxt_sraw(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 2:
            gen_helper_iwmmxt_sral(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 3:
            gen_helper_iwmmxt_sraq(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x104: case 0x504: case 0x904: case 0xd04:	/* WSLL */
    case 0x114: case 0x514: case 0x914: case 0xd14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_helper_iwmmxt_sllw(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 2:
            gen_helper_iwmmxt_slll(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 3:
            gen_helper_iwmmxt_sllq(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x304: case 0x704: case 0xb04: case 0xf04:	/* WROR */
    case 0x314: case 0x714: case 0xb14: case 0xf14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            if (gen_iwmmxt_shift(insn, 0xf))
                return 1;
            gen_helper_iwmmxt_rorw(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 2:
            if (gen_iwmmxt_shift(insn, 0x1f))
                return 1;
            gen_helper_iwmmxt_rorl(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        case 3:
            if (gen_iwmmxt_shift(insn, 0x3f))
                return 1;
            gen_helper_iwmmxt_rorq(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x116: case 0x316: case 0x516: case 0x716:	/* WMIN */
    case 0x916: case 0xb16: case 0xd16: case 0xf16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x016: case 0x216: case 0x416: case 0x616:	/* WMAX */
    case 0x816: case 0xa16: case 0xc16: case 0xe16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x002: case 0x102: case 0x202: case 0x302:	/* WALIGNI */
    case 0x402: case 0x502: case 0x602: case 0x702:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_movl_T0_im((insn >> 20) & 3);
        gen_op_iwmmxt_align_M0_T0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x01a: case 0x11a: case 0x21a: case 0x31a:	/* WSUB */
    case 0x41a: case 0x51a: case 0x61a: case 0x71a:
    case 0x81a: case 0x91a: case 0xa1a: case 0xb1a:
    case 0xc1a: case 0xd1a: case 0xe1a: case 0xf1a:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_subnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_subub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_subsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_subnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_subuw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_subsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_subnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_subul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_subsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x01e: case 0x11e: case 0x21e: case 0x31e:	/* WSHUFH */
    case 0x41e: case 0x51e: case 0x61e: case 0x71e:
    case 0x81e: case 0x91e: case 0xa1e: case 0xb1e:
    case 0xc1e: case 0xd1e: case 0xe1e: case 0xf1e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_movl_T0_im(((insn >> 16) & 0xf0) | (insn & 0x0f));
        gen_helper_iwmmxt_shufh(cpu_M0, cpu_env, cpu_M0, cpu_T[0]);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x018: case 0x118: case 0x218: case 0x318:	/* WADD */
    case 0x418: case 0x518: case 0x618: case 0x718:
    case 0x818: case 0x918: case 0xa18: case 0xb18:
    case 0xc18: case 0xd18: case 0xe18: case 0xf18:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_addnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_addub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_addsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_addnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_adduw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_addsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_addnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_addul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_addsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x008: case 0x108: case 0x208: case 0x308:	/* WPACK */
    case 0x408: case 0x508: case 0x608: case 0x708:
    case 0x808: case 0x908: case 0xa08: case 0xb08:
    case 0xc08: case 0xd08: case 0xe08: case 0xf08:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (!(insn & (1 << 20)))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packul_M0_wRn(rd1);
            break;
        case 3:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsq_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuq_M0_wRn(rd1);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x201: case 0x203: case 0x205: case 0x207:
    case 0x209: case 0x20b: case 0x20d: case 0x20f:
    case 0x211: case 0x213: case 0x215: case 0x217:
    case 0x219: case 0x21b: case 0x21d: case 0x21f:
        wrd = (insn >> 5) & 0xf;
        rd0 = (insn >> 12) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        if (rd0 == 0xf || rd1 == 0xf)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 16) & 0xf) {
        case 0x0:					/* TMIA */
            gen_movl_T0_reg(s, rd0);
            gen_movl_T1_reg(s, rd1);
            gen_op_iwmmxt_muladdsl_M0_T0_T1();
            break;
        case 0x8:					/* TMIAPH */
            gen_movl_T0_reg(s, rd0);
            gen_movl_T1_reg(s, rd1);
            gen_op_iwmmxt_muladdsw_M0_T0_T1();
            break;
        case 0xc: case 0xd: case 0xe: case 0xf:		/* TMIAxy */
            gen_movl_T1_reg(s, rd0);
            if (insn & (1 << 16))
                gen_op_shrl_T1_im(16);
            gen_op_movl_T0_T1();
            gen_movl_T1_reg(s, rd1);
            if (insn & (1 << 17))
                gen_op_shrl_T1_im(16);
            gen_op_iwmmxt_muladdswl_M0_T0_T1();
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    default:
        return 1;
    }

    return 0;
}

/* Disassemble an XScale DSP instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_dsp_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    int acc, rd0, rd1, rdhi, rdlo;

    if ((insn & 0x0ff00f10) == 0x0e200010) {
        /* Multiply with Internal Accumulate Format */
        rd0 = (insn >> 12) & 0xf;
        rd1 = insn & 0xf;
        acc = (insn >> 5) & 7;

        if (acc != 0)
            return 1;

        switch ((insn >> 16) & 0xf) {
        case 0x0:					/* MIA */
            gen_movl_T0_reg(s, rd0);
            gen_movl_T1_reg(s, rd1);
            gen_op_iwmmxt_muladdsl_M0_T0_T1();
            break;
        case 0x8:					/* MIAPH */
            gen_movl_T0_reg(s, rd0);
            gen_movl_T1_reg(s, rd1);
            gen_op_iwmmxt_muladdsw_M0_T0_T1();
            break;
        case 0xc:					/* MIABB */
        case 0xd:					/* MIABT */
        case 0xe:					/* MIATB */
        case 0xf:					/* MIATT */
            gen_movl_T1_reg(s, rd0);
            if (insn & (1 << 16))
                gen_op_shrl_T1_im(16);
            gen_op_movl_T0_T1();
            gen_movl_T1_reg(s, rd1);
            if (insn & (1 << 17))
                gen_op_shrl_T1_im(16);
            gen_op_iwmmxt_muladdswl_M0_T0_T1();
            break;
        default:
            return 1;
        }

        gen_op_iwmmxt_movq_wRn_M0(acc);
        return 0;
    }

    if ((insn & 0x0fe00ff8) == 0x0c400000) {
        /* Internal Accumulator Access Format */
        rdhi = (insn >> 16) & 0xf;
        rdlo = (insn >> 12) & 0xf;
        acc = insn & 7;

        if (acc != 0)
            return 1;

        if (insn & ARM_CP_RW_BIT) {			/* MRA */
            gen_iwmmxt_movl_T0_T1_wRn(acc);
            gen_movl_reg_T0(s, rdlo);
            gen_op_movl_T0_im((1 << (40 - 32)) - 1);
            gen_op_andl_T0_T1();
            gen_movl_reg_T0(s, rdhi);
        } else {					/* MAR */
            gen_movl_T0_reg(s, rdlo);
            gen_movl_T1_reg(s, rdhi);
            gen_iwmmxt_movl_wRn_T0_T1(acc);
        }
        return 0;
    }

    return 1;
}

/* Disassemble system coprocessor instruction.  Return nonzero if
   instruction is not defined.  */
static int disas_cp_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    TCGv tmp;
    uint32_t rd = (insn >> 12) & 0xf;
    uint32_t cp = (insn >> 8) & 0xf;
    if (IS_USER(s)) {
        return 1;
    }

    if (insn & ARM_CP_RW_BIT) {
        if (!env->cp[cp].cp_read)
            return 1;
        gen_set_pc_im(s->pc);
        tmp = new_tmp();
        gen_helper_get_cp(tmp, cpu_env, tcg_const_i32(insn));
        store_reg(s, rd, tmp);
    } else {
        if (!env->cp[cp].cp_write)
            return 1;
        gen_set_pc_im(s->pc);
        tmp = load_reg(s, rd);
        gen_helper_set_cp(cpu_env, tcg_const_i32(insn), tmp);
        dead_tmp(tmp);
    }
    return 0;
}

static int cp15_user_ok(uint32_t insn)
{
    int cpn = (insn >> 16) & 0xf;
    int cpm = insn & 0xf;
    int op = ((insn >> 5) & 7) | ((insn >> 18) & 0x38);

    if (cpn == 13 && cpm == 0) {
        /* TLS register.  */
        if (op == 2 || (op == 3 && (insn & ARM_CP_RW_BIT)))
            return 1;
    }
    if (cpn == 7) {
        /* ISB, DSB, DMB.  */
        if ((cpm == 5 && op == 4)
                || (cpm == 10 && (op == 4 || op == 5)))
            return 1;
    }
    return 0;
}

/* Disassemble system coprocessor (cp15) instruction.  Return nonzero if
   instruction is not defined.  */
static int disas_cp15_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    uint32_t rd;
    TCGv tmp;

    /* M profile cores use memory mapped registers instead of cp15.  */
    if (arm_feature(env, ARM_FEATURE_M))
	return 1;

    if ((insn & (1 << 25)) == 0) {
        if (insn & (1 << 20)) {
            /* mrrc */
            return 1;
        }
        /* mcrr.  Used for block cache operations, so implement as no-op.  */
        return 0;
    }
    if ((insn & (1 << 4)) == 0) {
        /* cdp */
        return 1;
    }
    if (IS_USER(s) && !cp15_user_ok(insn)) {
        return 1;
    }
    if ((insn & 0x0fff0fff) == 0x0e070f90
        || (insn & 0x0fff0fff) == 0x0e070f58) {
        /* Wait for interrupt.  */
        gen_set_pc_im(s->pc);
        s->is_jmp = DISAS_WFI;
        return 0;
    }
    rd = (insn >> 12) & 0xf;
    if (insn & ARM_CP_RW_BIT) {
        tmp = new_tmp();
        gen_helper_get_cp15(tmp, cpu_env, tcg_const_i32(insn));
        /* If the destination register is r15 then sets condition codes.  */
        if (rd != 15)
            store_reg(s, rd, tmp);
        else
            dead_tmp(tmp);
    } else {
        tmp = load_reg(s, rd);
        gen_helper_set_cp15(cpu_env, tcg_const_i32(insn), tmp);
        dead_tmp(tmp);
        /* Normally we would always end the TB here, but Linux
         * arch/arm/mach-pxa/sleep.S expects two instructions following
         * an MMU enable to execute from cache.  Imitate this behaviour.  */
        if (!arm_feature(env, ARM_FEATURE_XSCALE) ||
                (insn & 0x0fff0fff) != 0x0e010f10)
            gen_lookup_tb(s);
    }
    return 0;
}

#define VFP_REG_SHR(x, n) (((n) > 0) ? (x) >> (n) : (x) << -(n))
#define VFP_SREG(insn, bigbit, smallbit) \
  ((VFP_REG_SHR(insn, bigbit - 1) & 0x1e) | (((insn) >> (smallbit)) & 1))
#define VFP_DREG(reg, insn, bigbit, smallbit) do { \
    if (arm_feature(env, ARM_FEATURE_VFP3)) { \
        reg = (((insn) >> (bigbit)) & 0x0f) \
              | (((insn) >> ((smallbit) - 4)) & 0x10); \
    } else { \
        if (insn & (1 << (smallbit))) \
            return 1; \
        reg = ((insn) >> (bigbit)) & 0x0f; \
    }} while (0)

#define VFP_SREG_D(insn) VFP_SREG(insn, 12, 22)
#define VFP_DREG_D(reg, insn) VFP_DREG(reg, insn, 12, 22)
#define VFP_SREG_N(insn) VFP_SREG(insn, 16,  7)
#define VFP_DREG_N(reg, insn) VFP_DREG(reg, insn, 16,  7)
#define VFP_SREG_M(insn) VFP_SREG(insn,  0,  5)
#define VFP_DREG_M(reg, insn) VFP_DREG(reg, insn,  0,  5)

/* Move between integer and VFP cores.  */
static TCGv gen_vfp_mrs(void)
{
    TCGv tmp = new_tmp();
    tcg_gen_mov_i32(tmp, cpu_F0s);
    return tmp;
}

static void gen_vfp_msr(TCGv tmp)
{
    tcg_gen_mov_i32(cpu_F0s, tmp);
    dead_tmp(tmp);
}

static inline int
vfp_enabled(CPUState * env)
{
    return ((env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) != 0);
}

static void gen_neon_dup_u8(TCGv var, int shift)
{
    TCGv tmp = new_tmp();
    if (shift)
        tcg_gen_shri_i32(var, var, shift);
    tcg_gen_ext8u_i32(var, var);
    tcg_gen_shli_i32(tmp, var, 8);
    tcg_gen_or_i32(var, var, tmp);
    tcg_gen_shli_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

static void gen_neon_dup_low16(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_ext16u_i32(var, var);
    tcg_gen_shli_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

static void gen_neon_dup_high16(TCGv var)
{
    TCGv tmp = new_tmp();
    tcg_gen_andi_i32(var, var, 0xffff0000);
    tcg_gen_shri_i32(tmp, var, 16);
    tcg_gen_or_i32(var, var, tmp);
    dead_tmp(tmp);
}

/* Disassemble a VFP instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_vfp_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    uint32_t rd, rn, rm, op, i, n, offset, delta_d, delta_m, bank_mask;
    int dp, veclen;
    TCGv tmp;
    TCGv tmp2;

    if (!arm_feature(env, ARM_FEATURE_VFP))
        return 1;

    if (!vfp_enabled(env)) {
        /* VFP disabled.  Only allow fmxr/fmrx to/from some control regs.  */
        if ((insn & 0x0fe00fff) != 0x0ee00a10)
            return 1;
        rn = (insn >> 16) & 0xf;
        if (rn != ARM_VFP_FPSID && rn != ARM_VFP_FPEXC
            && rn != ARM_VFP_MVFR1 && rn != ARM_VFP_MVFR0)
            return 1;
    }
    dp = ((insn & 0xf00) == 0xb00);
    switch ((insn >> 24) & 0xf) {
    case 0xe:
        if (insn & (1 << 4)) {
            /* single register transfer */
            rd = (insn >> 12) & 0xf;
            if (dp) {
                int size;
                int pass;

                VFP_DREG_N(rn, insn);
                if (insn & 0xf)
                    return 1;
                if (insn & 0x00c00060
                    && !arm_feature(env, ARM_FEATURE_NEON))
                    return 1;

                pass = (insn >> 21) & 1;
                if (insn & (1 << 22)) {
                    size = 0;
                    offset = ((insn >> 5) & 3) * 8;
                } else if (insn & (1 << 5)) {
                    size = 1;
                    offset = (insn & (1 << 6)) ? 16 : 0;
                } else {
                    size = 2;
                    offset = 0;
                }
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    tmp = neon_load_reg(rn, pass);
                    switch (size) {
                    case 0:
                        if (offset)
                            tcg_gen_shri_i32(tmp, tmp, offset);
                        if (insn & (1 << 23))
                            gen_uxtb(tmp);
                        else
                            gen_sxtb(tmp);
                        break;
                    case 1:
                        if (insn & (1 << 23)) {
                            if (offset) {
                                tcg_gen_shri_i32(tmp, tmp, 16);
                            } else {
                                gen_uxth(tmp);
                            }
                        } else {
                            if (offset) {
                                tcg_gen_sari_i32(tmp, tmp, 16);
                            } else {
                                gen_sxth(tmp);
                            }
                        }
                        break;
                    case 2:
                        break;
                    }
                    store_reg(s, rd, tmp);
                } else {
                    /* arm->vfp */
                    tmp = load_reg(s, rd);
                    if (insn & (1 << 23)) {
                        /* VDUP */
                        if (size == 0) {
                            gen_neon_dup_u8(tmp, 0);
                        } else if (size == 1) {
                            gen_neon_dup_low16(tmp);
                        }
                        tmp2 = new_tmp();
                        tcg_gen_mov_i32(tmp2, tmp);
                        neon_store_reg(rn, 0, tmp2);
                        neon_store_reg(rn, 1, tmp);
                    } else {
                        /* VMOV */
                        switch (size) {
                        case 0:
                            tmp2 = neon_load_reg(rn, pass);
                            gen_bfi(tmp, tmp2, tmp, offset, 0xff);
                            dead_tmp(tmp2);
                            break;
                        case 1:
                            tmp2 = neon_load_reg(rn, pass);
                            gen_bfi(tmp, tmp2, tmp, offset, 0xffff);
                            dead_tmp(tmp2);
                            break;
                        case 2:
                            break;
                        }
                        neon_store_reg(rn, pass, tmp);
                    }
                }
            } else { /* !dp */
                if ((insn & 0x6f) != 0x00)
                    return 1;
                rn = VFP_SREG_N(insn);
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    if (insn & (1 << 21)) {
                        /* system register */
                        rn >>= 1;

                        switch (rn) {
                        case ARM_VFP_FPSID:
                            /* VFP2 allows access to FSID from userspace.
                               VFP3 restricts all id registers to privileged
                               accesses.  */
                            if (IS_USER(s)
                                && arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            tmp = load_cpu_field(vfp.xregs[rn]);
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s))
                                return 1;
                            tmp = load_cpu_field(vfp.xregs[rn]);
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            /* Not present in VFP3.  */
                            if (IS_USER(s)
                                || arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            tmp = load_cpu_field(vfp.xregs[rn]);
                            break;
                        case ARM_VFP_FPSCR:
                            if (rd == 15) {
                                tmp = load_cpu_field(vfp.xregs[ARM_VFP_FPSCR]);
                                tcg_gen_andi_i32(tmp, tmp, 0xf0000000);
                            } else {
                                tmp = new_tmp();
                                gen_helper_vfp_get_fpscr(tmp, cpu_env);
                            }
                            break;
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            if (IS_USER(s)
                                || !arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            tmp = load_cpu_field(vfp.xregs[rn]);
                            break;
                        default:
                            return 1;
                        }
                    } else {
                        gen_mov_F0_vreg(0, rn);
                        tmp = gen_vfp_mrs();
                    }
                    if (rd == 15) {
                        /* Set the 4 flag bits in the CPSR.  */
                        gen_set_nzcv(tmp);
                        dead_tmp(tmp);
                    } else {
                        store_reg(s, rd, tmp);
                    }
                } else {
                    /* arm->vfp */
                    tmp = load_reg(s, rd);
                    if (insn & (1 << 21)) {
                        rn >>= 1;
                        /* system register */
                        switch (rn) {
                        case ARM_VFP_FPSID:
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            /* Writes are ignored.  */
                            break;
                        case ARM_VFP_FPSCR:
                            gen_helper_vfp_set_fpscr(cpu_env, tmp);
                            dead_tmp(tmp);
                            gen_lookup_tb(s);
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s))
                                return 1;
                            store_cpu_field(tmp, vfp.xregs[rn]);
                            gen_lookup_tb(s);
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            store_cpu_field(tmp, vfp.xregs[rn]);
                            break;
                        default:
                            return 1;
                        }
                    } else {
                        gen_vfp_msr(tmp);
                        gen_mov_vreg_F0(0, rn);
                    }
                }
            }
        } else {
            /* data processing */
            /* The opcode is in bits 23, 21, 20 and 6.  */
            op = ((insn >> 20) & 8) | ((insn >> 19) & 6) | ((insn >> 6) & 1);
            if (dp) {
                if (op == 15) {
                    /* rn is opcode */
                    rn = ((insn >> 15) & 0x1e) | ((insn >> 7) & 1);
                } else {
                    /* rn is register number */
                    VFP_DREG_N(rn, insn);
                }

                if (op == 15 && (rn == 15 || rn > 17)) {
                    /* Integer or single precision destination.  */
                    rd = VFP_SREG_D(insn);
                } else {
                    VFP_DREG_D(rd, insn);
                }

                if (op == 15 && (rn == 16 || rn == 17)) {
                    /* Integer source.  */
                    rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);
                } else {
                    VFP_DREG_M(rm, insn);
                }
            } else {
                rn = VFP_SREG_N(insn);
                if (op == 15 && rn == 15) {
                    /* Double precision destination.  */
                    VFP_DREG_D(rd, insn);
                } else {
                    rd = VFP_SREG_D(insn);
                }
                rm = VFP_SREG_M(insn);
            }

            veclen = env->vfp.vec_len;
            if (op == 15 && rn > 3)
                veclen = 0;

            /* Shut up compiler warnings.  */
            delta_m = 0;
            delta_d = 0;
            bank_mask = 0;

            if (veclen > 0) {
                if (dp)
                    bank_mask = 0xc;
                else
                    bank_mask = 0x18;

                /* Figure out what type of vector operation this is.  */
                if ((rd & bank_mask) == 0) {
                    /* scalar */
                    veclen = 0;
                } else {
                    if (dp)
                        delta_d = (env->vfp.vec_stride >> 1) + 1;
                    else
                        delta_d = env->vfp.vec_stride + 1;

                    if ((rm & bank_mask) == 0) {
                        /* mixed scalar/vector */
                        delta_m = 0;
                    } else {
                        /* vector */
                        delta_m = delta_d;
                    }
                }
            }

            /* Load the initial operands.  */
            if (op == 15) {
                switch (rn) {
                case 16:
                case 17:
                    /* Integer source */
                    gen_mov_F0_vreg(0, rm);
                    break;
                case 8:
                case 9:
                    /* Compare */
                    gen_mov_F0_vreg(dp, rd);
                    gen_mov_F1_vreg(dp, rm);
                    break;
                case 10:
                case 11:
                    /* Compare with zero */
                    gen_mov_F0_vreg(dp, rd);
                    gen_vfp_F1_ld0(dp);
                    break;
                case 20:
                case 21:
                case 22:
                case 23:
                case 28:
                case 29:
                case 30:
                case 31:
                    /* Source and destination the same.  */
                    gen_mov_F0_vreg(dp, rd);
                    break;
                default:
                    /* One source operand.  */
                    gen_mov_F0_vreg(dp, rm);
                    break;
                }
            } else {
                /* Two source operands.  */
                gen_mov_F0_vreg(dp, rn);
                gen_mov_F1_vreg(dp, rm);
            }

            for (;;) {
                /* Perform the calculation.  */
                switch (op) {
                case 0: /* mac: fd + (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_add(dp);
                    break;
                case 1: /* nmac: fd - (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_vfp_neg(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_add(dp);
                    break;
                case 2: /* msc: -fd + (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_sub(dp);
                    break;
                case 3: /* nmsc: -fd - (fn * fm)  */
                    gen_vfp_mul(dp);
                    gen_vfp_neg(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_sub(dp);
                    break;
                case 4: /* mul: fn * fm */
                    gen_vfp_mul(dp);
                    break;
                case 5: /* nmul: -(fn * fm) */
                    gen_vfp_mul(dp);
                    gen_vfp_neg(dp);
                    break;
                case 6: /* add: fn + fm */
                    gen_vfp_add(dp);
                    break;
                case 7: /* sub: fn - fm */
                    gen_vfp_sub(dp);
                    break;
                case 8: /* div: fn / fm */
                    gen_vfp_div(dp);
                    break;
                case 14: /* fconst */
                    if (!arm_feature(env, ARM_FEATURE_VFP3))
                      return 1;

                    n = (insn << 12) & 0x80000000;
                    i = ((insn >> 12) & 0x70) | (insn & 0xf);
                    if (dp) {
                        if (i & 0x40)
                            i |= 0x3f80;
                        else
                            i |= 0x4000;
                        n |= i << 16;
                        tcg_gen_movi_i64(cpu_F0d, ((uint64_t)n) << 32);
                    } else {
                        if (i & 0x40)
                            i |= 0x780;
                        else
                            i |= 0x800;
                        n |= i << 19;
                        tcg_gen_movi_i32(cpu_F0s, n);
                    }
                    break;
                case 15: /* extension space */
                    switch (rn) {
                    case 0: /* cpy */
                        /* no-op */
                        break;
                    case 1: /* abs */
                        gen_vfp_abs(dp);
                        break;
                    case 2: /* neg */
                        gen_vfp_neg(dp);
                        break;
                    case 3: /* sqrt */
                        gen_vfp_sqrt(dp);
                        break;
                    case 8: /* cmp */
                        gen_vfp_cmp(dp);
                        break;
                    case 9: /* cmpe */
                        gen_vfp_cmpe(dp);
                        break;
                    case 10: /* cmpz */
                        gen_vfp_cmp(dp);
                        break;
                    case 11: /* cmpez */
                        gen_vfp_F1_ld0(dp);
                        gen_vfp_cmpe(dp);
                        break;
                    case 15: /* single<->double conversion */
                        if (dp)
                            gen_helper_vfp_fcvtsd(cpu_F0s, cpu_F0d, cpu_env);
                        else
                            gen_helper_vfp_fcvtds(cpu_F0d, cpu_F0s, cpu_env);
                        break;
                    case 16: /* fuito */
                        gen_vfp_uito(dp);
                        break;
                    case 17: /* fsito */
                        gen_vfp_sito(dp);
                        break;
                    case 20: /* fshto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_shto(dp, 16 - rm);
                        break;
                    case 21: /* fslto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_slto(dp, 32 - rm);
                        break;
                    case 22: /* fuhto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_uhto(dp, 16 - rm);
                        break;
                    case 23: /* fulto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_ulto(dp, 32 - rm);
                        break;
                    case 24: /* ftoui */
                        gen_vfp_toui(dp);
                        break;
                    case 25: /* ftouiz */
                        gen_vfp_touiz(dp);
                        break;
                    case 26: /* ftosi */
                        gen_vfp_tosi(dp);
                        break;
                    case 27: /* ftosiz */
                        gen_vfp_tosiz(dp);
                        break;
                    case 28: /* ftosh */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_tosh(dp, 16 - rm);
                        break;
                    case 29: /* ftosl */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_tosl(dp, 32 - rm);
                        break;
                    case 30: /* ftouh */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_touh(dp, 16 - rm);
                        break;
                    case 31: /* ftoul */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_toul(dp, 32 - rm);
                        break;
                    default: /* undefined */
                        printf ("rn:%d\n", rn);
                        return 1;
                    }
                    break;
                default: /* undefined */
                    printf ("op:%d\n", op);
                    return 1;
                }

                /* Write back the result.  */
                if (op == 15 && (rn >= 8 && rn <= 11))
                    ; /* Comparison, do nothing.  */
                else if (op == 15 && rn > 17)
                    /* Integer result.  */
                    gen_mov_vreg_F0(0, rd);
                else if (op == 15 && rn == 15)
                    /* conversion */
                    gen_mov_vreg_F0(!dp, rd);
                else
                    gen_mov_vreg_F0(dp, rd);

                /* break out of the loop if we have finished  */
                if (veclen == 0)
                    break;

                if (op == 15 && delta_m == 0) {
                    /* single source one-many */
                    while (veclen--) {
                        rd = ((rd + delta_d) & (bank_mask - 1))
                             | (rd & bank_mask);
                        gen_mov_vreg_F0(dp, rd);
                    }
                    break;
                }
                /* Setup the next operands.  */
                veclen--;
                rd = ((rd + delta_d) & (bank_mask - 1))
                     | (rd & bank_mask);

                if (op == 15) {
                    /* One source operand.  */
                    rm = ((rm + delta_m) & (bank_mask - 1))
                         | (rm & bank_mask);
                    gen_mov_F0_vreg(dp, rm);
                } else {
                    /* Two source operands.  */
                    rn = ((rn + delta_d) & (bank_mask - 1))
                         | (rn & bank_mask);
                    gen_mov_F0_vreg(dp, rn);
                    if (delta_m) {
                        rm = ((rm + delta_m) & (bank_mask - 1))
                             | (rm & bank_mask);
                        gen_mov_F1_vreg(dp, rm);
                    }
                }
            }
        }
        break;
    case 0xc:
    case 0xd:
        if (dp && (insn & 0x03e00000) == 0x00400000) {
            /* two-register transfer */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            if (dp) {
                VFP_DREG_M(rm, insn);
            } else {
                rm = VFP_SREG_M(insn);
            }

            if (insn & ARM_CP_RW_BIT) {
                /* vfp->arm */
                if (dp) {
                    gen_mov_F0_vreg(0, rm * 2);
                    tmp = gen_vfp_mrs();
                    store_reg(s, rd, tmp);
                    gen_mov_F0_vreg(0, rm * 2 + 1);
                    tmp = gen_vfp_mrs();
                    store_reg(s, rn, tmp);
                } else {
                    gen_mov_F0_vreg(0, rm);
                    tmp = gen_vfp_mrs();
                    store_reg(s, rn, tmp);
                    gen_mov_F0_vreg(0, rm + 1);
                    tmp = gen_vfp_mrs();
                    store_reg(s, rd, tmp);
                }
            } else {
                /* arm->vfp */
                if (dp) {
                    tmp = load_reg(s, rd);
                    gen_vfp_msr(tmp);
                    gen_mov_vreg_F0(0, rm * 2);
                    tmp = load_reg(s, rn);
                    gen_vfp_msr(tmp);
                    gen_mov_vreg_F0(0, rm * 2 + 1);
                } else {
                    tmp = load_reg(s, rn);
                    gen_vfp_msr(tmp);
                    gen_mov_vreg_F0(0, rm);
                    tmp = load_reg(s, rd);
                    gen_vfp_msr(tmp);
                    gen_mov_vreg_F0(0, rm + 1);
                }
            }
        } else {
            /* Load/store */
            rn = (insn >> 16) & 0xf;
            if (dp)
                VFP_DREG_D(rd, insn);
            else
                rd = VFP_SREG_D(insn);
            if (s->thumb && rn == 15) {
                gen_op_movl_T1_im(s->pc & ~2);
            } else {
                gen_movl_T1_reg(s, rn);
            }
            if ((insn & 0x01200000) == 0x01000000) {
                /* Single load/store */
                offset = (insn & 0xff) << 2;
                if ((insn & (1 << 23)) == 0)
                    offset = -offset;
                gen_op_addl_T1_im(offset);
                if (insn & (1 << 20)) {
                    gen_vfp_ld(s, dp);
                    gen_mov_vreg_F0(dp, rd);
                } else {
                    gen_mov_F0_vreg(dp, rd);
                    gen_vfp_st(s, dp);
                }
            } else {
                /* load/store multiple */
                if (dp)
                    n = (insn >> 1) & 0x7f;
                else
                    n = insn & 0xff;

                if (insn & (1 << 24)) /* pre-decrement */
                    gen_op_addl_T1_im(-((insn & 0xff) << 2));

                if (dp)
                    offset = 8;
                else
                    offset = 4;
                for (i = 0; i < n; i++) {
                    if (insn & ARM_CP_RW_BIT) {
                        /* load */
                        gen_vfp_ld(s, dp);
                        gen_mov_vreg_F0(dp, rd + i);
                    } else {
                        /* store */
                        gen_mov_F0_vreg(dp, rd + i);
                        gen_vfp_st(s, dp);
                    }
                    gen_op_addl_T1_im(offset);
                }
                if (insn & (1 << 21)) {
                    /* writeback */
                    if (insn & (1 << 24))
                        offset = -offset * n;
                    else if (dp && (insn & 1))
                        offset = 4;
                    else
                        offset = 0;

                    if (offset != 0)
                        gen_op_addl_T1_im(offset);
                    gen_movl_reg_T1(s, rn);
                }
            }
        }
        break;
    default:
        /* Should never happen.  */
        return 1;
    }
    return 0;
}

static inline void gen_goto_tb(DisasContext *s, int n, uint32_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        tcg_gen_goto_tb(n);
        gen_set_pc_im(dest);
        tcg_gen_exit_tb((long)tb + n);
    } else {
        gen_set_pc_im(dest);
        tcg_gen_exit_tb(0);
    }
}

static inline void gen_jmp (DisasContext *s, uint32_t dest)
{
    if (unlikely(s->singlestep_enabled)) {
        /* An indirect jump so that we still trigger the debug exception.  */
        if (s->thumb)
            dest |= 1;
        gen_bx_im(s, dest);
    } else {
        gen_goto_tb(s, 0, dest);
        s->is_jmp = DISAS_TB_JUMP;
    }
}

static inline void gen_mulxy(TCGv t0, TCGv t1, int x, int y)
{
    if (x)
        tcg_gen_sari_i32(t0, t0, 16);
    else
        gen_sxth(t0);
    if (y)
        tcg_gen_sari_i32(t1, t1, 16);
    else
        gen_sxth(t1);
    tcg_gen_mul_i32(t0, t0, t1);
}

/* Return the mask of PSR bits set by a MSR instruction.  */
static uint32_t msr_mask(CPUState *env, DisasContext *s, int flags, int spsr) {
    uint32_t mask;

    mask = 0;
    if (flags & (1 << 0))
        mask |= 0xff;
    if (flags & (1 << 1))
        mask |= 0xff00;
    if (flags & (1 << 2))
        mask |= 0xff0000;
    if (flags & (1 << 3))
        mask |= 0xff000000;

    /* Mask out undefined bits.  */
    mask &= ~CPSR_RESERVED;
    if (!arm_feature(env, ARM_FEATURE_V6))
        mask &= ~(CPSR_E | CPSR_GE);
    if (!arm_feature(env, ARM_FEATURE_THUMB2))
        mask &= ~CPSR_IT;
    /* Mask out execution state bits.  */
    if (!spsr)
        mask &= ~CPSR_EXEC;
    /* Mask out privileged bits.  */
    if (IS_USER(s))
        mask &= CPSR_USER;
    return mask;
}

/* Returns nonzero if access to the PSR is not permitted.  */
static int gen_set_psr_T0(DisasContext *s, uint32_t mask, int spsr)
{
    TCGv tmp;
    if (spsr) {
        /* ??? This is also undefined in system mode.  */
        if (IS_USER(s))
            return 1;

        tmp = load_cpu_field(spsr);
        tcg_gen_andi_i32(tmp, tmp, ~mask);
        tcg_gen_andi_i32(cpu_T[0], cpu_T[0], mask);
        tcg_gen_or_i32(tmp, tmp, cpu_T[0]);
        store_cpu_field(tmp, spsr);
    } else {
        gen_set_cpsr(cpu_T[0], mask);
    }
    gen_lookup_tb(s);
    return 0;
}

/* Generate an old-style exception return.  */
static void gen_exception_return(DisasContext *s)
{
    TCGv tmp;
    gen_movl_reg_T0(s, 15);
    tmp = load_cpu_field(spsr);
    gen_set_cpsr(tmp, 0xffffffff);
    dead_tmp(tmp);
    s->is_jmp = DISAS_UPDATE;
}

/* Generate a v6 exception return.  Marks both values as dead.  */
static void gen_rfe(DisasContext *s, TCGv pc, TCGv cpsr)
{
    gen_set_cpsr(cpsr, 0xffffffff);
    dead_tmp(cpsr);
    store_reg(s, 15, pc);
    s->is_jmp = DISAS_UPDATE;
}

static inline void
gen_set_condexec (DisasContext *s)
{
    if (s->condexec_mask) {
        uint32_t val = (s->condexec_cond << 4) | (s->condexec_mask >> 1);
        TCGv tmp = new_tmp();
        tcg_gen_movi_i32(tmp, val);
        store_cpu_field(tmp, condexec_bits);
    }
}

static void gen_nop_hint(DisasContext *s, int val)
{
    switch (val) {
    case 3: /* wfi */
        gen_set_pc_im(s->pc);
        s->is_jmp = DISAS_WFI;
        break;
    case 2: /* wfe */
    case 4: /* sev */
        /* TODO: Implement SEV and WFE.  May help SMP performance.  */
    default: /* nop */
        break;
    }
}

/* These macros help make the code more readable when migrating from the
   old dyngen helpers.  They should probably be removed when
   T0/T1 are removed.  */
#define CPU_T001 cpu_T[0], cpu_T[0], cpu_T[1]
#define CPU_T0E01 cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]

#define CPU_V001 cpu_V0, cpu_V0, cpu_V1

static inline int gen_neon_add(int size)
{
    switch (size) {
    case 0: gen_helper_neon_add_u8(CPU_T001); break;
    case 1: gen_helper_neon_add_u16(CPU_T001); break;
    case 2: gen_op_addl_T0_T1(); break;
    default: return 1;
    }
    return 0;
}

static inline void gen_neon_rsb(int size)
{
    switch (size) {
    case 0: gen_helper_neon_sub_u8(cpu_T[0], cpu_T[1], cpu_T[0]); break;
    case 1: gen_helper_neon_sub_u16(cpu_T[0], cpu_T[1], cpu_T[0]); break;
    case 2: gen_op_rsbl_T0_T1(); break;
    default: return;
    }
}

/* 32-bit pairwise ops end up the same as the elementwise versions.  */
#define gen_helper_neon_pmax_s32  gen_helper_neon_max_s32
#define gen_helper_neon_pmax_u32  gen_helper_neon_max_u32
#define gen_helper_neon_pmin_s32  gen_helper_neon_min_s32
#define gen_helper_neon_pmin_u32  gen_helper_neon_min_u32

/* FIXME: This is wrong.  They set the wrong overflow bit.  */
#define gen_helper_neon_qadd_s32(a, e, b, c) gen_helper_add_saturate(a, b, c)
#define gen_helper_neon_qadd_u32(a, e, b, c) gen_helper_add_usaturate(a, b, c)
#define gen_helper_neon_qsub_s32(a, e, b, c) gen_helper_sub_saturate(a, b, c)
#define gen_helper_neon_qsub_u32(a, e, b, c) gen_helper_sub_usaturate(a, b, c)

#define GEN_NEON_INTEGER_OP_ENV(name) do { \
    switch ((size << 1) | u) { \
    case 0: \
        gen_helper_neon_##name##_s8(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    case 1: \
        gen_helper_neon_##name##_u8(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    case 2: \
        gen_helper_neon_##name##_s16(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    case 3: \
        gen_helper_neon_##name##_u16(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    case 4: \
        gen_helper_neon_##name##_s32(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    case 5: \
        gen_helper_neon_##name##_u32(cpu_T[0], cpu_env, cpu_T[0], cpu_T[1]); \
        break; \
    default: return 1; \
    }} while (0)

#define GEN_NEON_INTEGER_OP(name) do { \
    switch ((size << 1) | u) { \
    case 0: \
        gen_helper_neon_##name##_s8(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    case 1: \
        gen_helper_neon_##name##_u8(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    case 2: \
        gen_helper_neon_##name##_s16(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    case 3: \
        gen_helper_neon_##name##_u16(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    case 4: \
        gen_helper_neon_##name##_s32(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    case 5: \
        gen_helper_neon_##name##_u32(cpu_T[0], cpu_T[0], cpu_T[1]); \
        break; \
    default: return 1; \
    }} while (0)

static inline void
gen_neon_movl_scratch_T0(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  tcg_gen_st_i32(cpu_T[0], cpu_env, offset);
}

static inline void
gen_neon_movl_scratch_T1(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  tcg_gen_st_i32(cpu_T[1], cpu_env, offset);
}

static inline void
gen_neon_movl_T0_scratch(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  tcg_gen_ld_i32(cpu_T[0], cpu_env, offset);
}

static inline void
gen_neon_movl_T1_scratch(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  tcg_gen_ld_i32(cpu_T[1], cpu_env, offset);
}

static inline void gen_neon_get_scalar(int size, int reg)
{
    if (size == 1) {
        NEON_GET_REG(T0, reg >> 1, reg & 1);
    } else {
        NEON_GET_REG(T0, reg >> 2, (reg >> 1) & 1);
        if (reg & 1)
            gen_neon_dup_low16(cpu_T[0]);
        else
            gen_neon_dup_high16(cpu_T[0]);
    }
}

static void gen_neon_unzip(int reg, int q, int tmp, int size)
{
    int n;

    for (n = 0; n < q + 1; n += 2) {
        NEON_GET_REG(T0, reg, n);
        NEON_GET_REG(T0, reg, n + n);
        switch (size) {
        case 0: gen_helper_neon_unzip_u8(); break;
        case 1: gen_helper_neon_zip_u16(); break; /* zip and unzip are the same.  */
        case 2: /* no-op */; break;
        default: abort();
        }
        gen_neon_movl_scratch_T0(tmp + n);
        gen_neon_movl_scratch_T1(tmp + n + 1);
    }
}

static struct {
    int nregs;
    int interleave;
    int spacing;
} neon_ls_element_type[11] = {
    {4, 4, 1},
    {4, 4, 2},
    {4, 1, 1},
    {4, 2, 1},
    {3, 3, 1},
    {3, 3, 2},
    {3, 1, 1},
    {1, 1, 1},
    {2, 2, 1},
    {2, 2, 2},
    {2, 1, 1}
};

/* Translate a NEON load/store element instruction.  Return nonzero if the
   instruction is invalid.  */
static int disas_neon_ls_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int rd, rn, rm;
    int op;
    int nregs;
    int interleave;
    int stride;
    int size;
    int reg;
    int pass;
    int load;
    int shift;
    int n;
    TCGv tmp;
    TCGv tmp2;

    if (!vfp_enabled(env))
      return 1;
    VFP_DREG_D(rd, insn);
    rn = (insn >> 16) & 0xf;
    rm = insn & 0xf;
    load = (insn & (1 << 21)) != 0;
    if ((insn & (1 << 23)) == 0) {
        /* Load store all elements.  */
        op = (insn >> 8) & 0xf;
        size = (insn >> 6) & 3;
        if (op > 10 || size == 3)
            return 1;
        nregs = neon_ls_element_type[op].nregs;
        interleave = neon_ls_element_type[op].interleave;
        gen_movl_T1_reg(s, rn);
        stride = (1 << size) * interleave;
        for (reg = 0; reg < nregs; reg++) {
            if (interleave > 2 || (interleave == 2 && nregs == 2)) {
                gen_movl_T1_reg(s, rn);
                gen_op_addl_T1_im((1 << size) * reg);
            } else if (interleave == 2 && nregs == 4 && reg == 2) {
                gen_movl_T1_reg(s, rn);
                gen_op_addl_T1_im(1 << size);
            }
            for (pass = 0; pass < 2; pass++) {
                if (size == 2) {
                    if (load) {
                        tmp = gen_ld32(cpu_T[1], IS_USER(s));
                        neon_store_reg(rd, pass, tmp);
                    } else {
                        tmp = neon_load_reg(rd, pass);
                        gen_st32(tmp, cpu_T[1], IS_USER(s));
                    }
                    gen_op_addl_T1_im(stride);
                } else if (size == 1) {
                    if (load) {
                        tmp = gen_ld16u(cpu_T[1], IS_USER(s));
                        gen_op_addl_T1_im(stride);
                        tmp2 = gen_ld16u(cpu_T[1], IS_USER(s));
                        gen_op_addl_T1_im(stride);
                        gen_bfi(tmp, tmp, tmp2, 16, 0xffff);
                        dead_tmp(tmp2);
                        neon_store_reg(rd, pass, tmp);
                    } else {
                        tmp = neon_load_reg(rd, pass);
                        tmp2 = new_tmp();
                        tcg_gen_shri_i32(tmp2, tmp, 16);
                        gen_st16(tmp, cpu_T[1], IS_USER(s));
                        gen_op_addl_T1_im(stride);
                        gen_st16(tmp2, cpu_T[1], IS_USER(s));
                        gen_op_addl_T1_im(stride);
                    }
                } else /* size == 0 */ {
                    if (load) {
                        TCGV_UNUSED(tmp2);
                        for (n = 0; n < 4; n++) {
                            tmp = gen_ld8u(cpu_T[1], IS_USER(s));
                            gen_op_addl_T1_im(stride);
                            if (n == 0) {
                                tmp2 = tmp;
                            } else {
                                gen_bfi(tmp2, tmp2, tmp, n * 8, 0xff);
                                dead_tmp(tmp);
                            }
                        }
                        neon_store_reg(rd, pass, tmp2);
                    } else {
                        tmp2 = neon_load_reg(rd, pass);
                        for (n = 0; n < 4; n++) {
                            tmp = new_tmp();
                            if (n == 0) {
                                tcg_gen_mov_i32(tmp, tmp2);
                            } else {
                                tcg_gen_shri_i32(tmp, tmp2, n * 8);
                            }
                            gen_st8(tmp, cpu_T[1], IS_USER(s));
                            gen_op_addl_T1_im(stride);
                        }
                        dead_tmp(tmp2);
                    }
                }
            }
            rd += neon_ls_element_type[op].spacing;
        }
        stride = nregs * 8;
    } else {
        size = (insn >> 10) & 3;
        if (size == 3) {
            /* Load single element to all lanes.  */
            if (!load)
                return 1;
            size = (insn >> 6) & 3;
            nregs = ((insn >> 8) & 3) + 1;
            stride = (insn & (1 << 5)) ? 2 : 1;
            gen_movl_T1_reg(s, rn);
            for (reg = 0; reg < nregs; reg++) {
                switch (size) {
                case 0:
                    tmp = gen_ld8u(cpu_T[1], IS_USER(s));
                    gen_neon_dup_u8(tmp, 0);
                    break;
                case 1:
                    tmp = gen_ld16u(cpu_T[1], IS_USER(s));
                    gen_neon_dup_low16(tmp);
                    break;
                case 2:
                    tmp = gen_ld32(cpu_T[0], IS_USER(s));
                    break;
                case 3:
                    return 1;
                default: /* Avoid compiler warnings.  */
                    abort();
                }
                gen_op_addl_T1_im(1 << size);
                tmp2 = new_tmp();
                tcg_gen_mov_i32(tmp2, tmp);
                neon_store_reg(rd, 0, tmp2);
                neon_store_reg(rd, 1, tmp);
                rd += stride;
            }
            stride = (1 << size) * nregs;
        } else {
            /* Single element.  */
            pass = (insn >> 7) & 1;
            switch (size) {
            case 0:
                shift = ((insn >> 5) & 3) * 8;
                stride = 1;
                break;
            case 1:
                shift = ((insn >> 6) & 1) * 16;
                stride = (insn & (1 << 5)) ? 2 : 1;
                break;
            case 2:
                shift = 0;
                stride = (insn & (1 << 6)) ? 2 : 1;
                break;
            default:
                abort();
            }
            nregs = ((insn >> 8) & 3) + 1;
            gen_movl_T1_reg(s, rn);
            for (reg = 0; reg < nregs; reg++) {
                if (load) {
                    switch (size) {
                    case 0:
                        tmp = gen_ld8u(cpu_T[1], IS_USER(s));
                        break;
                    case 1:
                        tmp = gen_ld16u(cpu_T[1], IS_USER(s));
                        break;
                    case 2:
                        tmp = gen_ld32(cpu_T[1], IS_USER(s));
                        break;
                    default: /* Avoid compiler warnings.  */
                        abort();
                    }
                    if (size != 2) {
                        tmp2 = neon_load_reg(rd, pass);
                        gen_bfi(tmp, tmp2, tmp, shift, size ? 0xffff : 0xff);
                        dead_tmp(tmp2);
                    }
                    neon_store_reg(rd, pass, tmp);
                } else { /* Store */
                    tmp = neon_load_reg(rd, pass);
                    if (shift)
                        tcg_gen_shri_i32(tmp, tmp, shift);
                    switch (size) {
                    case 0:
                        gen_st8(tmp, cpu_T[1], IS_USER(s));
                        break;
                    case 1:
                        gen_st16(tmp, cpu_T[1], IS_USER(s));
                        break;
                    case 2:
                        gen_st32(tmp, cpu_T[1], IS_USER(s));
                        break;
                    }
                }
                rd += stride;
                gen_op_addl_T1_im(1 << size);
            }
            stride = nregs * (1 << size);
        }
    }
    if (rm != 15) {
        TCGv base;

        base = load_reg(s, rn);
        if (rm == 13) {
            tcg_gen_addi_i32(base, base, stride);
        } else {
            TCGv index;
            index = load_reg(s, rm);
            tcg_gen_add_i32(base, base, index);
            dead_tmp(index);
        }
        store_reg(s, rn, base);
    }
    return 0;
}

/* Bitwise select.  dest = c ? t : f.  Clobbers T and F.  */
static void gen_neon_bsl(TCGv dest, TCGv t, TCGv f, TCGv c)
{
    tcg_gen_and_i32(t, t, c);
    tcg_gen_bic_i32(f, f, c);
    tcg_gen_or_i32(dest, t, f);
}

static inline void gen_neon_narrow(int size, TCGv dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_u8(dest, src); break;
    case 1: gen_helper_neon_narrow_u16(dest, src); break;
    case 2: tcg_gen_trunc_i64_i32(dest, src); break;
    default: abort();
    }
}

static inline void gen_neon_narrow_sats(int size, TCGv dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_sat_s8(dest, cpu_env, src); break;
    case 1: gen_helper_neon_narrow_sat_s16(dest, cpu_env, src); break;
    case 2: gen_helper_neon_narrow_sat_s32(dest, cpu_env, src); break;
    default: abort();
    }
}

static inline void gen_neon_narrow_satu(int size, TCGv dest, TCGv_i64 src)
{
    switch (size) {
    case 0: gen_helper_neon_narrow_sat_u8(dest, cpu_env, src); break;
    case 1: gen_helper_neon_narrow_sat_u16(dest, cpu_env, src); break;
    case 2: gen_helper_neon_narrow_sat_u32(dest, cpu_env, src); break;
    default: abort();
    }
}

static inline void gen_neon_shift_narrow(int size, TCGv var, TCGv shift,
                                         int q, int u)
{
    if (q) {
        if (u) {
            switch (size) {
            case 1: gen_helper_neon_rshl_u16(var, var, shift); break;
            case 2: gen_helper_neon_rshl_u32(var, var, shift); break;
            default: abort();
            }
        } else {
            switch (size) {
            case 1: gen_helper_neon_rshl_s16(var, var, shift); break;
            case 2: gen_helper_neon_rshl_s32(var, var, shift); break;
            default: abort();
            }
        }
    } else {
        if (u) {
            switch (size) {
            case 1: gen_helper_neon_rshl_u16(var, var, shift); break;
            case 2: gen_helper_neon_rshl_u32(var, var, shift); break;
            default: abort();
            }
        } else {
            switch (size) {
            case 1: gen_helper_neon_shl_s16(var, var, shift); break;
            case 2: gen_helper_neon_shl_s32(var, var, shift); break;
            default: abort();
            }
        }
    }
}

static inline void gen_neon_widen(TCGv_i64 dest, TCGv src, int size, int u)
{
    if (u) {
        switch (size) {
        case 0: gen_helper_neon_widen_u8(dest, src); break;
        case 1: gen_helper_neon_widen_u16(dest, src); break;
        case 2: tcg_gen_extu_i32_i64(dest, src); break;
        default: abort();
        }
    } else {
        switch (size) {
        case 0: gen_helper_neon_widen_s8(dest, src); break;
        case 1: gen_helper_neon_widen_s16(dest, src); break;
        case 2: tcg_gen_ext_i32_i64(dest, src); break;
        default: abort();
        }
    }
    dead_tmp(src);
}

static inline void gen_neon_addl(int size)
{
    switch (size) {
    case 0: gen_helper_neon_addl_u16(CPU_V001); break;
    case 1: gen_helper_neon_addl_u32(CPU_V001); break;
    case 2: tcg_gen_add_i64(CPU_V001); break;
    default: abort();
    }
}

static inline void gen_neon_subl(int size)
{
    switch (size) {
    case 0: gen_helper_neon_subl_u16(CPU_V001); break;
    case 1: gen_helper_neon_subl_u32(CPU_V001); break;
    case 2: tcg_gen_sub_i64(CPU_V001); break;
    default: abort();
    }
}

static inline void gen_neon_negl(TCGv_i64 var, int size)
{
    switch (size) {
    case 0: gen_helper_neon_negl_u16(var, var); break;
    case 1: gen_helper_neon_negl_u32(var, var); break;
    case 2: gen_helper_neon_negl_u64(var, var); break;
    default: abort();
    }
}

static inline void gen_neon_addl_saturate(TCGv_i64 op0, TCGv_i64 op1, int size)
{
    switch (size) {
    case 1: gen_helper_neon_addl_saturate_s32(op0, cpu_env, op0, op1); break;
    case 2: gen_helper_neon_addl_saturate_s64(op0, cpu_env, op0, op1); break;
    default: abort();
    }
}

static inline void gen_neon_mull(TCGv_i64 dest, TCGv a, TCGv b, int size, int u)
{
    TCGv_i64 tmp;

    switch ((size << 1) | u) {
    case 0: gen_helper_neon_mull_s8(dest, a, b); break;
    case 1: gen_helper_neon_mull_u8(dest, a, b); break;
    case 2: gen_helper_neon_mull_s16(dest, a, b); break;
    case 3: gen_helper_neon_mull_u16(dest, a, b); break;
    case 4:
        tmp = gen_muls_i64_i32(a, b);
        tcg_gen_mov_i64(dest, tmp);
        break;
    case 5:
        tmp = gen_mulu_i64_i32(a, b);
        tcg_gen_mov_i64(dest, tmp);
        break;
    default: abort();
    }
    if (size < 2) {
        dead_tmp(b);
        dead_tmp(a);
    }
}

/* Translate a NEON data processing instruction.  Return nonzero if the
   instruction is invalid.
   We process data in a mixture of 32-bit and 64-bit chunks.
   Mostly we use 32-bit chunks so we can use normal scalar instructions.  */

static int disas_neon_data_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int op;
    int q;
    int rd, rn, rm;
    int size;
    int shift;
    int pass;
    int count;
    int pairwise;
    int u;
    int n;
    uint32_t imm;
    TCGv tmp;
    TCGv tmp2;
    TCGv tmp3;
    TCGv_i64 tmp64;

    if (!vfp_enabled(env))
      return 1;
    q = (insn & (1 << 6)) != 0;
    u = (insn >> 24) & 1;
    VFP_DREG_D(rd, insn);
    VFP_DREG_N(rn, insn);
    VFP_DREG_M(rm, insn);
    size = (insn >> 20) & 3;
    if ((insn & (1 << 23)) == 0) {
        /* Three register same length.  */
        op = ((insn >> 7) & 0x1e) | ((insn >> 4) & 1);
        if (size == 3 && (op == 1 || op == 5 || op == 8 || op == 9
                          || op == 10 || op  == 11 || op == 16)) {
            /* 64-bit element instructions.  */
            for (pass = 0; pass < (q ? 2 : 1); pass++) {
                neon_load_reg64(cpu_V0, rn + pass);
                neon_load_reg64(cpu_V1, rm + pass);
                switch (op) {
                case 1: /* VQADD */
                    if (u) {
                        gen_helper_neon_add_saturate_u64(CPU_V001);
                    } else {
                        gen_helper_neon_add_saturate_s64(CPU_V001);
                    }
                    break;
                case 5: /* VQSUB */
                    if (u) {
                        gen_helper_neon_sub_saturate_u64(CPU_V001);
                    } else {
                        gen_helper_neon_sub_saturate_s64(CPU_V001);
                    }
                    break;
                case 8: /* VSHL */
                    if (u) {
                        gen_helper_neon_shl_u64(cpu_V0, cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_shl_s64(cpu_V0, cpu_V1, cpu_V0);
                    }
                    break;
                case 9: /* VQSHL */
                    if (u) {
                        gen_helper_neon_qshl_u64(cpu_V0, cpu_env,
                                                 cpu_V0, cpu_V0);
                    } else {
                        gen_helper_neon_qshl_s64(cpu_V1, cpu_env,
                                                 cpu_V1, cpu_V0);
                    }
                    break;
                case 10: /* VRSHL */
                    if (u) {
                        gen_helper_neon_rshl_u64(cpu_V0, cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_rshl_s64(cpu_V0, cpu_V1, cpu_V0);
                    }
                    break;
                case 11: /* VQRSHL */
                    if (u) {
                        gen_helper_neon_qrshl_u64(cpu_V0, cpu_env,
                                                  cpu_V1, cpu_V0);
                    } else {
                        gen_helper_neon_qrshl_s64(cpu_V0, cpu_env,
                                                  cpu_V1, cpu_V0);
                    }
                    break;
                case 16:
                    if (u) {
                        tcg_gen_sub_i64(CPU_V001);
                    } else {
                        tcg_gen_add_i64(CPU_V001);
                    }
                    break;
                default:
                    abort();
                }
                neon_store_reg64(cpu_V0, rd + pass);
            }
            return 0;
        }
        switch (op) {
        case 8: /* VSHL */
        case 9: /* VQSHL */
        case 10: /* VRSHL */
        case 11: /* VQRSHL */
            {
                int rtmp;
                /* Shift instruction operands are reversed.  */
                rtmp = rn;
                rn = rm;
                rm = rtmp;
                pairwise = 0;
            }
            break;
        case 20: /* VPMAX */
        case 21: /* VPMIN */
        case 23: /* VPADD */
            pairwise = 1;
            break;
        case 26: /* VPADD (float) */
            pairwise = (u && size < 2);
            break;
        case 30: /* VPMIN/VPMAX (float) */
            pairwise = u;
            break;
        default:
            pairwise = 0;
            break;
        }
        for (pass = 0; pass < (q ? 4 : 2); pass++) {

        if (pairwise) {
            /* Pairwise.  */
            if (q)
                n = (pass & 1) * 2;
            else
                n = 0;
            if (pass < q + 1) {
                NEON_GET_REG(T0, rn, n);
                NEON_GET_REG(T1, rn, n + 1);
            } else {
                NEON_GET_REG(T0, rm, n);
                NEON_GET_REG(T1, rm, n + 1);
            }
        } else {
            /* Elementwise.  */
            NEON_GET_REG(T0, rn, pass);
            NEON_GET_REG(T1, rm, pass);
        }
        switch (op) {
        case 0: /* VHADD */
            GEN_NEON_INTEGER_OP(hadd);
            break;
        case 1: /* VQADD */
            GEN_NEON_INTEGER_OP_ENV(qadd);
            break;
        case 2: /* VRHADD */
            GEN_NEON_INTEGER_OP(rhadd);
            break;
        case 3: /* Logic ops.  */
            switch ((u << 2) | size) {
            case 0: /* VAND */
                gen_op_andl_T0_T1();
                break;
            case 1: /* BIC */
                gen_op_bicl_T0_T1();
                break;
            case 2: /* VORR */
                gen_op_orl_T0_T1();
                break;
            case 3: /* VORN */
                gen_op_notl_T1();
                gen_op_orl_T0_T1();
                break;
            case 4: /* VEOR */
                gen_op_xorl_T0_T1();
                break;
            case 5: /* VBSL */
                tmp = neon_load_reg(rd, pass);
                gen_neon_bsl(cpu_T[0], cpu_T[0], cpu_T[1], tmp);
                dead_tmp(tmp);
                break;
            case 6: /* VBIT */
                tmp = neon_load_reg(rd, pass);
                gen_neon_bsl(cpu_T[0], cpu_T[0], tmp, cpu_T[1]);
                dead_tmp(tmp);
                break;
            case 7: /* VBIF */
                tmp = neon_load_reg(rd, pass);
                gen_neon_bsl(cpu_T[0], tmp, cpu_T[0], cpu_T[1]);
                dead_tmp(tmp);
                break;
            }
            break;
        case 4: /* VHSUB */
            GEN_NEON_INTEGER_OP(hsub);
            break;
        case 5: /* VQSUB */
            GEN_NEON_INTEGER_OP_ENV(qsub);
            break;
        case 6: /* VCGT */
            GEN_NEON_INTEGER_OP(cgt);
            break;
        case 7: /* VCGE */
            GEN_NEON_INTEGER_OP(cge);
            break;
        case 8: /* VSHL */
            GEN_NEON_INTEGER_OP(shl);
            break;
        case 9: /* VQSHL */
            GEN_NEON_INTEGER_OP_ENV(qshl);
            break;
        case 10: /* VRSHL */
            GEN_NEON_INTEGER_OP(rshl);
            break;
        case 11: /* VQRSHL */
            GEN_NEON_INTEGER_OP_ENV(qrshl);
            break;
        case 12: /* VMAX */
            GEN_NEON_INTEGER_OP(max);
            break;
        case 13: /* VMIN */
            GEN_NEON_INTEGER_OP(min);
            break;
        case 14: /* VABD */
            GEN_NEON_INTEGER_OP(abd);
            break;
        case 15: /* VABA */
            GEN_NEON_INTEGER_OP(abd);
            NEON_GET_REG(T1, rd, pass);
            gen_neon_add(size);
            break;
        case 16:
            if (!u) { /* VADD */
                if (gen_neon_add(size))
                    return 1;
            } else { /* VSUB */
                switch (size) {
                case 0: gen_helper_neon_sub_u8(CPU_T001); break;
                case 1: gen_helper_neon_sub_u16(CPU_T001); break;
                case 2: gen_op_subl_T0_T1(); break;
                default: return 1;
                }
            }
            break;
        case 17:
            if (!u) { /* VTST */
                switch (size) {
                case 0: gen_helper_neon_tst_u8(CPU_T001); break;
                case 1: gen_helper_neon_tst_u16(CPU_T001); break;
                case 2: gen_helper_neon_tst_u32(CPU_T001); break;
                default: return 1;
                }
            } else { /* VCEQ */
                switch (size) {
                case 0: gen_helper_neon_ceq_u8(CPU_T001); break;
                case 1: gen_helper_neon_ceq_u16(CPU_T001); break;
                case 2: gen_helper_neon_ceq_u32(CPU_T001); break;
                default: return 1;
                }
            }
            break;
        case 18: /* Multiply.  */
            switch (size) {
            case 0: gen_helper_neon_mul_u8(CPU_T001); break;
            case 1: gen_helper_neon_mul_u16(CPU_T001); break;
            case 2: gen_op_mul_T0_T1(); break;
            default: return 1;
            }
            NEON_GET_REG(T1, rd, pass);
            if (u) { /* VMLS */
                gen_neon_rsb(size);
            } else { /* VMLA */
                gen_neon_add(size);
            }
            break;
        case 19: /* VMUL */
            if (u) { /* polynomial */
                gen_helper_neon_mul_p8(CPU_T001);
            } else { /* Integer */
                switch (size) {
                case 0: gen_helper_neon_mul_u8(CPU_T001); break;
                case 1: gen_helper_neon_mul_u16(CPU_T001); break;
                case 2: gen_op_mul_T0_T1(); break;
                default: return 1;
                }
            }
            break;
        case 20: /* VPMAX */
            GEN_NEON_INTEGER_OP(pmax);
            break;
        case 21: /* VPMIN */
            GEN_NEON_INTEGER_OP(pmin);
            break;
        case 22: /* Hultiply high.  */
            if (!u) { /* VQDMULH */
                switch (size) {
                case 1: gen_helper_neon_qdmulh_s16(CPU_T0E01); break;
                case 2: gen_helper_neon_qdmulh_s32(CPU_T0E01); break;
                default: return 1;
                }
            } else { /* VQRDHMUL */
                switch (size) {
                case 1: gen_helper_neon_qrdmulh_s16(CPU_T0E01); break;
                case 2: gen_helper_neon_qrdmulh_s32(CPU_T0E01); break;
                default: return 1;
                }
            }
            break;
        case 23: /* VPADD */
            if (u)
                return 1;
            switch (size) {
            case 0: gen_helper_neon_padd_u8(CPU_T001); break;
            case 1: gen_helper_neon_padd_u16(CPU_T001); break;
            case 2: gen_op_addl_T0_T1(); break;
            default: return 1;
            }
            break;
        case 26: /* Floating point arithnetic.  */
            switch ((u << 2) | size) {
            case 0: /* VADD */
                gen_helper_neon_add_f32(CPU_T001);
                break;
            case 2: /* VSUB */
                gen_helper_neon_sub_f32(CPU_T001);
                break;
            case 4: /* VPADD */
                gen_helper_neon_add_f32(CPU_T001);
                break;
            case 6: /* VABD */
                gen_helper_neon_abd_f32(CPU_T001);
                break;
            default:
                return 1;
            }
            break;
        case 27: /* Float multiply.  */
            gen_helper_neon_mul_f32(CPU_T001);
            if (!u) {
                NEON_GET_REG(T1, rd, pass);
                if (size == 0) {
                    gen_helper_neon_add_f32(CPU_T001);
                } else {
                    gen_helper_neon_sub_f32(cpu_T[0], cpu_T[1], cpu_T[0]);
                }
            }
            break;
        case 28: /* Float compare.  */
            if (!u) {
                gen_helper_neon_ceq_f32(CPU_T001);
            } else {
                if (size == 0)
                    gen_helper_neon_cge_f32(CPU_T001);
                else
                    gen_helper_neon_cgt_f32(CPU_T001);
            }
            break;
        case 29: /* Float compare absolute.  */
            if (!u)
                return 1;
            if (size == 0)
                gen_helper_neon_acge_f32(CPU_T001);
            else
                gen_helper_neon_acgt_f32(CPU_T001);
            break;
        case 30: /* Float min/max.  */
            if (size == 0)
                gen_helper_neon_max_f32(CPU_T001);
            else
                gen_helper_neon_min_f32(CPU_T001);
            break;
        case 31:
            if (size == 0)
                gen_helper_recps_f32(cpu_T[0], cpu_T[0], cpu_T[1], cpu_env);
            else
                gen_helper_rsqrts_f32(cpu_T[0], cpu_T[0], cpu_T[1], cpu_env);
            break;
        default:
            abort();
        }
        /* Save the result.  For elementwise operations we can put it
           straight into the destination register.  For pairwise operations
           we have to be careful to avoid clobbering the source operands.  */
        if (pairwise && rd == rm) {
            gen_neon_movl_scratch_T0(pass);
        } else {
            NEON_SET_REG(T0, rd, pass);
        }

        } /* for pass */
        if (pairwise && rd == rm) {
            for (pass = 0; pass < (q ? 4 : 2); pass++) {
                gen_neon_movl_T0_scratch(pass);
                NEON_SET_REG(T0, rd, pass);
            }
        }
        /* End of 3 register same size operations.  */
    } else if (insn & (1 << 4)) {
        if ((insn & 0x00380080) != 0) {
            /* Two registers and shift.  */
            op = (insn >> 8) & 0xf;
            if (insn & (1 << 7)) {
                /* 64-bit shift.   */
                size = 3;
            } else {
                size = 2;
                while ((insn & (1 << (size + 19))) == 0)
                    size--;
            }
            shift = (insn >> 16) & ((1 << (3 + size)) - 1);
            /* To avoid excessive dumplication of ops we implement shift
               by immediate using the variable shift operations.  */
            if (op < 8) {
                /* Shift by immediate:
                   VSHR, VSRA, VRSHR, VRSRA, VSRI, VSHL, VQSHL, VQSHLU.  */
                /* Right shifts are encoded as N - shift, where N is the
                   element size in bits.  */
                if (op <= 4)
                    shift = shift - (1 << (size + 3));
                if (size == 3) {
                    count = q + 1;
                } else {
                    count = q ? 4: 2;
                }
                switch (size) {
                case 0:
                    imm = (uint8_t) shift;
                    imm |= imm << 8;
                    imm |= imm << 16;
                    break;
                case 1:
                    imm = (uint16_t) shift;
                    imm |= imm << 16;
                    break;
                case 2:
                case 3:
                    imm = shift;
                    break;
                default:
                    abort();
                }

                for (pass = 0; pass < count; pass++) {
                    if (size == 3) {
                        neon_load_reg64(cpu_V0, rm + pass);
                        tcg_gen_movi_i64(cpu_V1, imm);
                        switch (op) {
                        case 0:  /* VSHR */
                        case 1:  /* VSRA */
                            if (u)
                                gen_helper_neon_shl_u64(cpu_V0, cpu_V0, cpu_V1);
                            else
                                gen_helper_neon_shl_s64(cpu_V0, cpu_V0, cpu_V1);
                            break;
                        case 2: /* VRSHR */
                        case 3: /* VRSRA */
                            if (u)
                                gen_helper_neon_rshl_u64(cpu_V0, cpu_V0, cpu_V1);
                            else
                                gen_helper_neon_rshl_s64(cpu_V0, cpu_V0, cpu_V1);
                            break;
                        case 4: /* VSRI */
                            if (!u)
                                return 1;
                            gen_helper_neon_shl_u64(cpu_V0, cpu_V0, cpu_V1);
                            break;
                        case 5: /* VSHL, VSLI */
                            gen_helper_neon_shl_u64(cpu_V0, cpu_V0, cpu_V1);
                            break;
                        case 6: /* VQSHL */
                            if (u)
                                gen_helper_neon_qshl_u64(cpu_V0, cpu_env, cpu_V0, cpu_V1);
                            else
                                gen_helper_neon_qshl_s64(cpu_V0, cpu_env, cpu_V0, cpu_V1);
                            break;
                        case 7: /* VQSHLU */
                            gen_helper_neon_qshl_u64(cpu_V0, cpu_env, cpu_V0, cpu_V1);
                            break;
                        }
                        if (op == 1 || op == 3) {
                            /* Accumulate.  */
                            neon_load_reg64(cpu_V0, rd + pass);
                            tcg_gen_add_i64(cpu_V0, cpu_V0, cpu_V1);
                        } else if (op == 4 || (op == 5 && u)) {
                            /* Insert */
                            cpu_abort(env, "VS[LR]I.64 not implemented");
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    } else { /* size < 3 */
                        /* Operands in T0 and T1.  */
                        gen_op_movl_T1_im(imm);
                        NEON_GET_REG(T0, rm, pass);
                        switch (op) {
                        case 0:  /* VSHR */
                        case 1:  /* VSRA */
                            GEN_NEON_INTEGER_OP(shl);
                            break;
                        case 2: /* VRSHR */
                        case 3: /* VRSRA */
                            GEN_NEON_INTEGER_OP(rshl);
                            break;
                        case 4: /* VSRI */
                            if (!u)
                                return 1;
                            GEN_NEON_INTEGER_OP(shl);
                            break;
                        case 5: /* VSHL, VSLI */
                            switch (size) {
                            case 0: gen_helper_neon_shl_u8(CPU_T001); break;
                            case 1: gen_helper_neon_shl_u16(CPU_T001); break;
                            case 2: gen_helper_neon_shl_u32(CPU_T001); break;
                            default: return 1;
                            }
                            break;
                        case 6: /* VQSHL */
                            GEN_NEON_INTEGER_OP_ENV(qshl);
                            break;
                        case 7: /* VQSHLU */
                            switch (size) {
                            case 0: gen_helper_neon_qshl_u8(CPU_T0E01); break;
                            case 1: gen_helper_neon_qshl_u16(CPU_T0E01); break;
                            case 2: gen_helper_neon_qshl_u32(CPU_T0E01); break;
                            default: return 1;
                            }
                            break;
                        }

                        if (op == 1 || op == 3) {
                            /* Accumulate.  */
                            NEON_GET_REG(T1, rd, pass);
                            gen_neon_add(size);
                        } else if (op == 4 || (op == 5 && u)) {
                            /* Insert */
                            switch (size) {
                            case 0:
                                if (op == 4)
                                    imm = 0xff >> -shift;
                                else
                                    imm = (uint8_t)(0xff << shift);
                                imm |= imm << 8;
                                imm |= imm << 16;
                                break;
                            case 1:
                                if (op == 4)
                                    imm = 0xffff >> -shift;
                                else
                                    imm = (uint16_t)(0xffff << shift);
                                imm |= imm << 16;
                                break;
                            case 2:
                                if (op == 4)
                                    imm = 0xffffffffu >> -shift;
                                else
                                    imm = 0xffffffffu << shift;
                                break;
                            default:
                                abort();
                            }
                            tmp = neon_load_reg(rd, pass);
                            tcg_gen_andi_i32(cpu_T[0], cpu_T[0], imm);
                            tcg_gen_andi_i32(tmp, tmp, ~imm);
                            tcg_gen_or_i32(cpu_T[0], cpu_T[0], tmp);
                        }
                        NEON_SET_REG(T0, rd, pass);
                    }
                } /* for pass */
            } else if (op < 10) {
                /* Shift by immediate and narrow:
                   VSHRN, VRSHRN, VQSHRN, VQRSHRN.  */
                shift = shift - (1 << (size + 3));
                size++;
                switch (size) {
                case 1:
                    imm = (uint16_t)shift;
                    imm |= imm << 16;
                    tmp2 = tcg_const_i32(imm);
                    TCGV_UNUSED_I64(tmp64);
                    break;
                case 2:
                    imm = (uint32_t)shift;
                    tmp2 = tcg_const_i32(imm);
                    TCGV_UNUSED_I64(tmp64);
                    break;
                case 3:
                    tmp64 = tcg_const_i64(shift);
                    TCGV_UNUSED(tmp2);
                    break;
                default:
                    abort();
                }

                for (pass = 0; pass < 2; pass++) {
                    if (size == 3) {
                        neon_load_reg64(cpu_V0, rm + pass);
                        if (q) {
                          if (u)
                            gen_helper_neon_rshl_u64(cpu_V0, cpu_V0, tmp64);
                          else
                            gen_helper_neon_rshl_s64(cpu_V0, cpu_V0, tmp64);
                        } else {
                          if (u)
                            gen_helper_neon_shl_u64(cpu_V0, cpu_V0, tmp64);
                          else
                            gen_helper_neon_shl_s64(cpu_V0, cpu_V0, tmp64);
                        }
                    } else {
                        tmp = neon_load_reg(rm + pass, 0);
                        gen_neon_shift_narrow(size, tmp, tmp2, q, u);
                        tmp3 = neon_load_reg(rm + pass, 1);
                        gen_neon_shift_narrow(size, tmp3, tmp2, q, u);
                        tcg_gen_concat_i32_i64(cpu_V0, tmp, tmp3);
                        dead_tmp(tmp);
                        dead_tmp(tmp3);
                    }
                    tmp = new_tmp();
                    if (op == 8 && !u) {
                        gen_neon_narrow(size - 1, tmp, cpu_V0);
                    } else {
                        if (op == 8)
                            gen_neon_narrow_sats(size - 1, tmp, cpu_V0);
                        else
                            gen_neon_narrow_satu(size - 1, tmp, cpu_V0);
                    }
                    if (pass == 0) {
                        tmp2 = tmp;
                    } else {
                        neon_store_reg(rd, 0, tmp2);
                        neon_store_reg(rd, 1, tmp);
                    }
                } /* for pass */
            } else if (op == 10) {
                /* VSHLL */
                if (q || size == 3)
                    return 1;
                tmp = neon_load_reg(rm, 0);
                tmp2 = neon_load_reg(rm, 1);
                for (pass = 0; pass < 2; pass++) {
                    if (pass == 1)
                        tmp = tmp2;

                    gen_neon_widen(cpu_V0, tmp, size, u);

                    if (shift != 0) {
                        /* The shift is less than the width of the source
                           type, so we can just shift the whole register.  */
                        tcg_gen_shli_i64(cpu_V0, cpu_V0, shift);
                        if (size < 2 || !u) {
                            uint64_t imm64;
                            if (size == 0) {
                                imm = (0xffu >> (8 - shift));
                                imm |= imm << 16;
                            } else {
                                imm = 0xffff >> (16 - shift);
                            }
                            imm64 = imm | (((uint64_t)imm) << 32);
                            tcg_gen_andi_i64(cpu_V0, cpu_V0, imm64);
                        }
                    }
                    neon_store_reg64(cpu_V0, rd + pass);
                }
            } else if (op == 15 || op == 16) {
                /* VCVT fixed-point.  */
                for (pass = 0; pass < (q ? 4 : 2); pass++) {
                    tcg_gen_ld_f32(cpu_F0s, cpu_env, neon_reg_offset(rm, pass));
                    if (op & 1) {
                        if (u)
                            gen_vfp_ulto(0, shift);
                        else
                            gen_vfp_slto(0, shift);
                    } else {
                        if (u)
                            gen_vfp_toul(0, shift);
                        else
                            gen_vfp_tosl(0, shift);
                    }
                    tcg_gen_st_f32(cpu_F0s, cpu_env, neon_reg_offset(rd, pass));
                }
            } else {
                return 1;
            }
        } else { /* (insn & 0x00380080) == 0 */
            int invert;

            op = (insn >> 8) & 0xf;
            /* One register and immediate.  */
            imm = (u << 7) | ((insn >> 12) & 0x70) | (insn & 0xf);
            invert = (insn & (1 << 5)) != 0;
            switch (op) {
            case 0: case 1:
                /* no-op */
                break;
            case 2: case 3:
                imm <<= 8;
                break;
            case 4: case 5:
                imm <<= 16;
                break;
            case 6: case 7:
                imm <<= 24;
                break;
            case 8: case 9:
                imm |= imm << 16;
                break;
            case 10: case 11:
                imm = (imm << 8) | (imm << 24);
                break;
            case 12:
                imm = (imm < 8) | 0xff;
                break;
            case 13:
                imm = (imm << 16) | 0xffff;
                break;
            case 14:
                imm |= (imm << 8) | (imm << 16) | (imm << 24);
                if (invert)
                    imm = ~imm;
                break;
            case 15:
                imm = ((imm & 0x80) << 24) | ((imm & 0x3f) << 19)
                      | ((imm & 0x40) ? (0x1f << 25) : (1 << 30));
                break;
            }
            if (invert)
                imm = ~imm;

            if (op != 14 || !invert)
                gen_op_movl_T1_im(imm);

            for (pass = 0; pass < (q ? 4 : 2); pass++) {
                if (op & 1 && op < 12) {
                    tmp = neon_load_reg(rd, pass);
                    if (invert) {
                        /* The immediate value has already been inverted, so
                           BIC becomes AND.  */
                        tcg_gen_andi_i32(tmp, tmp, imm);
                    } else {
                        tcg_gen_ori_i32(tmp, tmp, imm);
                    }
                } else {
                    /* VMOV, VMVN.  */
                    tmp = new_tmp();
                    if (op == 14 && invert) {
                        uint32_t val;
                        val = 0;
                        for (n = 0; n < 4; n++) {
                            if (imm & (1 << (n + (pass & 1) * 4)))
                                val |= 0xff << (n * 8);
                        }
                        tcg_gen_movi_i32(tmp, val);
                    } else {
                        tcg_gen_movi_i32(tmp, imm);
                    }
                }
                neon_store_reg(rd, pass, tmp);
            }
        }
    } else { /* (insn & 0x00800010 == 0x00800000) */
        if (size != 3) {
            op = (insn >> 8) & 0xf;
            if ((insn & (1 << 6)) == 0) {
                /* Three registers of different lengths.  */
                int src1_wide;
                int src2_wide;
                int prewiden;
                /* prewiden, src1_wide, src2_wide */
                static const int neon_3reg_wide[16][3] = {
                    {1, 0, 0}, /* VADDL */
                    {1, 1, 0}, /* VADDW */
                    {1, 0, 0}, /* VSUBL */
                    {1, 1, 0}, /* VSUBW */
                    {0, 1, 1}, /* VADDHN */
                    {0, 0, 0}, /* VABAL */
                    {0, 1, 1}, /* VSUBHN */
                    {0, 0, 0}, /* VABDL */
                    {0, 0, 0}, /* VMLAL */
                    {0, 0, 0}, /* VQDMLAL */
                    {0, 0, 0}, /* VMLSL */
                    {0, 0, 0}, /* VQDMLSL */
                    {0, 0, 0}, /* Integer VMULL */
                    {0, 0, 0}, /* VQDMULL */
                    {0, 0, 0}  /* Polynomial VMULL */
                };

                prewiden = neon_3reg_wide[op][0];
                src1_wide = neon_3reg_wide[op][1];
                src2_wide = neon_3reg_wide[op][2];

                if (size == 0 && (op == 9 || op == 11 || op == 13))
                    return 1;

                /* Avoid overlapping operands.  Wide source operands are
                   always aligned so will never overlap with wide
                   destinations in problematic ways.  */
                if (rd == rm && !src2_wide) {
                    NEON_GET_REG(T0, rm, 1);
                    gen_neon_movl_scratch_T0(2);
                } else if (rd == rn && !src1_wide) {
                    NEON_GET_REG(T0, rn, 1);
                    gen_neon_movl_scratch_T0(2);
                }
                TCGV_UNUSED(tmp3);
                for (pass = 0; pass < 2; pass++) {
                    if (src1_wide) {
                        neon_load_reg64(cpu_V0, rn + pass);
                        TCGV_UNUSED(tmp);
                    } else {
                        if (pass == 1 && rd == rn) {
                            gen_neon_movl_T0_scratch(2);
                            tmp = new_tmp();
                            tcg_gen_mov_i32(tmp, cpu_T[0]);
                        } else {
                            tmp = neon_load_reg(rn, pass);
                        }
                        if (prewiden) {
                            gen_neon_widen(cpu_V0, tmp, size, u);
                        }
                    }
                    if (src2_wide) {
                        neon_load_reg64(cpu_V1, rm + pass);
                        TCGV_UNUSED(tmp2);
                    } else {
                        if (pass == 1 && rd == rm) {
                            gen_neon_movl_T0_scratch(2);
                            tmp2 = new_tmp();
                            tcg_gen_mov_i32(tmp2, cpu_T[0]);
                        } else {
                            tmp2 = neon_load_reg(rm, pass);
                        }
                        if (prewiden) {
                            gen_neon_widen(cpu_V1, tmp2, size, u);
                        }
                    }
                    switch (op) {
                    case 0: case 1: case 4: /* VADDL, VADDW, VADDHN, VRADDHN */
                        gen_neon_addl(size);
                        break;
                    case 2: case 3: case 6: /* VSUBL, VSUBW, VSUBHL, VRSUBHL */
                        gen_neon_subl(size);
                        break;
                    case 5: case 7: /* VABAL, VABDL */
                        switch ((size << 1) | u) {
                        case 0:
                            gen_helper_neon_abdl_s16(cpu_V0, tmp, tmp2);
                            break;
                        case 1:
                            gen_helper_neon_abdl_u16(cpu_V0, tmp, tmp2);
                            break;
                        case 2:
                            gen_helper_neon_abdl_s32(cpu_V0, tmp, tmp2);
                            break;
                        case 3:
                            gen_helper_neon_abdl_u32(cpu_V0, tmp, tmp2);
                            break;
                        case 4:
                            gen_helper_neon_abdl_s64(cpu_V0, tmp, tmp2);
                            break;
                        case 5:
                            gen_helper_neon_abdl_u64(cpu_V0, tmp, tmp2);
                            break;
                        default: abort();
                        }
                        dead_tmp(tmp2);
                        dead_tmp(tmp);
                        break;
                    case 8: case 9: case 10: case 11: case 12: case 13:
                        /* VMLAL, VQDMLAL, VMLSL, VQDMLSL, VMULL, VQDMULL */
                        gen_neon_mull(cpu_V0, tmp, tmp2, size, u);
                        break;
                    case 14: /* Polynomial VMULL */
                        cpu_abort(env, "Polynomial VMULL not implemented");

                    default: /* 15 is RESERVED.  */
                        return 1;
                    }
                    if (op == 5 || op == 13 || (op >= 8 && op <= 11)) {
                        /* Accumulate.  */
                        if (op == 10 || op == 11) {
                            gen_neon_negl(cpu_V0, size);
                        }

                        if (op != 13) {
                            neon_load_reg64(cpu_V1, rd + pass);
                        }

                        switch (op) {
                        case 5: case 8: case 10: /* VABAL, VMLAL, VMLSL */
                            gen_neon_addl(size);
                            break;
                        case 9: case 11: /* VQDMLAL, VQDMLSL */
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            gen_neon_addl_saturate(cpu_V0, cpu_V1, size);
                            break;
                            /* Fall through.  */
                        case 13: /* VQDMULL */
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            break;
                        default:
                            abort();
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    } else if (op == 4 || op == 6) {
                        /* Narrowing operation.  */
                        tmp = new_tmp();
                        if (u) {
                            switch (size) {
                            case 0:
                                gen_helper_neon_narrow_high_u8(tmp, cpu_V0);
                                break;
                            case 1:
                                gen_helper_neon_narrow_high_u16(tmp, cpu_V0);
                                break;
                            case 2:
                                tcg_gen_shri_i64(cpu_V0, cpu_V0, 32);
                                tcg_gen_trunc_i64_i32(tmp, cpu_V0);
                                break;
                            default: abort();
                            }
                        } else {
                            switch (size) {
                            case 0:
                                gen_helper_neon_narrow_round_high_u8(tmp, cpu_V0);
                                break;
                            case 1:
                                gen_helper_neon_narrow_round_high_u16(tmp, cpu_V0);
                                break;
                            case 2:
                                tcg_gen_addi_i64(cpu_V0, cpu_V0, 1u << 31);
                                tcg_gen_shri_i64(cpu_V0, cpu_V0, 32);
                                tcg_gen_trunc_i64_i32(tmp, cpu_V0);
                                break;
                            default: abort();
                            }
                        }
                        if (pass == 0) {
                            tmp3 = tmp;
                        } else {
                            neon_store_reg(rd, 0, tmp3);
                            neon_store_reg(rd, 1, tmp);
                        }
                    } else {
                        /* Write back the result.  */
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                }
            } else {
                /* Two registers and a scalar.  */
                switch (op) {
                case 0: /* Integer VMLA scalar */
                case 1: /* Float VMLA scalar */
                case 4: /* Integer VMLS scalar */
                case 5: /* Floating point VMLS scalar */
                case 8: /* Integer VMUL scalar */
                case 9: /* Floating point VMUL scalar */
                case 12: /* VQDMULH scalar */
                case 13: /* VQRDMULH scalar */
                    gen_neon_get_scalar(size, rm);
                    gen_neon_movl_scratch_T0(0);
                    for (pass = 0; pass < (u ? 4 : 2); pass++) {
                        if (pass != 0)
                            gen_neon_movl_T0_scratch(0);
                        NEON_GET_REG(T1, rn, pass);
                        if (op == 12) {
                            if (size == 1) {
                                gen_helper_neon_qdmulh_s16(CPU_T0E01);
                            } else {
                                gen_helper_neon_qdmulh_s32(CPU_T0E01);
                            }
                        } else if (op == 13) {
                            if (size == 1) {
                                gen_helper_neon_qrdmulh_s16(CPU_T0E01);
                            } else {
                                gen_helper_neon_qrdmulh_s32(CPU_T0E01);
                            }
                        } else if (op & 1) {
                            gen_helper_neon_mul_f32(CPU_T001);
                        } else {
                            switch (size) {
                            case 0: gen_helper_neon_mul_u8(CPU_T001); break;
                            case 1: gen_helper_neon_mul_u16(CPU_T001); break;
                            case 2: gen_op_mul_T0_T1(); break;
                            default: return 1;
                            }
                        }
                        if (op < 8) {
                            /* Accumulate.  */
                            NEON_GET_REG(T1, rd, pass);
                            switch (op) {
                            case 0:
                                gen_neon_add(size);
                                break;
                            case 1:
                                gen_helper_neon_add_f32(CPU_T001);
                                break;
                            case 4:
                                gen_neon_rsb(size);
                                break;
                            case 5:
                                gen_helper_neon_sub_f32(cpu_T[0], cpu_T[1], cpu_T[0]);
                                break;
                            default:
                                abort();
                            }
                        }
                        NEON_SET_REG(T0, rd, pass);
                    }
                    break;
                case 2: /* VMLAL sclar */
                case 3: /* VQDMLAL scalar */
                case 6: /* VMLSL scalar */
                case 7: /* VQDMLSL scalar */
                case 10: /* VMULL scalar */
                case 11: /* VQDMULL scalar */
                    if (size == 0 && (op == 3 || op == 7 || op == 11))
                        return 1;

                    gen_neon_get_scalar(size, rm);
                    NEON_GET_REG(T1, rn, 1);

                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 0) {
                            tmp = neon_load_reg(rn, 0);
                        } else {
                            tmp = new_tmp();
                            tcg_gen_mov_i32(tmp, cpu_T[1]);
                        }
                        tmp2 = new_tmp();
                        tcg_gen_mov_i32(tmp2, cpu_T[0]);
                        gen_neon_mull(cpu_V0, tmp, tmp2, size, u);
                        if (op == 6 || op == 7) {
                            gen_neon_negl(cpu_V0, size);
                        }
                        if (op != 11) {
                            neon_load_reg64(cpu_V1, rd + pass);
                        }
                        switch (op) {
                        case 2: case 6:
                            gen_neon_addl(size);
                            break;
                        case 3: case 7:
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            gen_neon_addl_saturate(cpu_V0, cpu_V1, size);
                            break;
                        case 10:
                            /* no-op */
                            break;
                        case 11:
                            gen_neon_addl_saturate(cpu_V0, cpu_V0, size);
                            break;
                        default:
                            abort();
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                default: /* 14 and 15 are RESERVED */
                    return 1;
                }
            }
        } else { /* size == 3 */
            if (!u) {
                /* Extract.  */
                imm = (insn >> 8) & 0xf;
                count = q + 1;

                if (imm > 7 && !q)
                    return 1;

                if (imm == 0) {
                    neon_load_reg64(cpu_V0, rn);
                    if (q) {
                        neon_load_reg64(cpu_V1, rn + 1);
                    }
                } else if (imm == 8) {
                    neon_load_reg64(cpu_V0, rn + 1);
                    if (q) {
                        neon_load_reg64(cpu_V1, rm);
                    }
                } else if (q) {
                    tmp64 = tcg_temp_new_i64();
                    if (imm < 8) {
                        neon_load_reg64(cpu_V0, rn);
                        neon_load_reg64(tmp64, rn + 1);
                    } else {
                        neon_load_reg64(cpu_V0, rn + 1);
                        neon_load_reg64(tmp64, rm);
                    }
                    tcg_gen_shri_i64(cpu_V0, cpu_V0, (imm & 7) * 8);
                    tcg_gen_shli_i64(cpu_V1, tmp64, 64 - ((imm & 7) * 8));
                    tcg_gen_or_i64(cpu_V0, cpu_V0, cpu_V1);
                    if (imm < 8) {
                        neon_load_reg64(cpu_V1, rm);
                    } else {
                        neon_load_reg64(cpu_V1, rm + 1);
                        imm -= 8;
                    }
                    tcg_gen_shli_i64(cpu_V1, cpu_V1, 64 - (imm * 8));
                    tcg_gen_shri_i64(tmp64, tmp64, imm * 8);
                    tcg_gen_or_i64(cpu_V1, cpu_V1, tmp64);
                } else {
                    /* BUGFIX */
                    neon_load_reg64(cpu_V0, rn);
                    tcg_gen_shri_i64(cpu_V0, cpu_V0, imm * 8);
                    neon_load_reg64(cpu_V1, rm);
                    tcg_gen_shli_i64(cpu_V1, cpu_V1, 64 - (imm * 8));
                    tcg_gen_or_i64(cpu_V0, cpu_V0, cpu_V1);
                }
                neon_store_reg64(cpu_V0, rd);
                if (q) {
                    neon_store_reg64(cpu_V1, rd + 1);
                }
            } else if ((insn & (1 << 11)) == 0) {
                /* Two register misc.  */
                op = ((insn >> 12) & 0x30) | ((insn >> 7) & 0xf);
                size = (insn >> 18) & 3;
                switch (op) {
                case 0: /* VREV64 */
                    if (size == 3)
                        return 1;
                    for (pass = 0; pass < (q ? 2 : 1); pass++) {
                        NEON_GET_REG(T0, rm, pass * 2);
                        NEON_GET_REG(T1, rm, pass * 2 + 1);
                        switch (size) {
                        case 0: tcg_gen_bswap_i32(cpu_T[0], cpu_T[0]); break;
                        case 1: gen_swap_half(cpu_T[0]); break;
                        case 2: /* no-op */ break;
                        default: abort();
                        }
                        NEON_SET_REG(T0, rd, pass * 2 + 1);
                        if (size == 2) {
                            NEON_SET_REG(T1, rd, pass * 2);
                        } else {
                            gen_op_movl_T0_T1();
                            switch (size) {
                            case 0: tcg_gen_bswap_i32(cpu_T[0], cpu_T[0]); break;
                            case 1: gen_swap_half(cpu_T[0]); break;
                            default: abort();
                            }
                            NEON_SET_REG(T0, rd, pass * 2);
                        }
                    }
                    break;
                case 4: case 5: /* VPADDL */
                case 12: case 13: /* VPADAL */
                    if (size == 3)
                        return 1;
                    for (pass = 0; pass < q + 1; pass++) {
                        tmp = neon_load_reg(rm, pass * 2);
                        gen_neon_widen(cpu_V0, tmp, size, op & 1);
                        tmp = neon_load_reg(rm, pass * 2 + 1);
                        gen_neon_widen(cpu_V1, tmp, size, op & 1);
                        switch (size) {
                        case 0: gen_helper_neon_paddl_u16(CPU_V001); break;
                        case 1: gen_helper_neon_paddl_u32(CPU_V001); break;
                        case 2: tcg_gen_add_i64(CPU_V001); break;
                        default: abort();
                        }
                        if (op >= 12) {
                            /* Accumulate.  */
                            neon_load_reg64(cpu_V1, rd + pass);
                            gen_neon_addl(size);
                        }
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                case 33: /* VTRN */
                    if (size == 2) {
                        for (n = 0; n < (q ? 4 : 2); n += 2) {
                            NEON_GET_REG(T0, rm, n);
                            NEON_GET_REG(T1, rd, n + 1);
                            NEON_SET_REG(T1, rm, n);
                            NEON_SET_REG(T0, rd, n + 1);
                        }
                    } else {
                        goto elementwise;
                    }
                    break;
                case 34: /* VUZP */
                    /* Reg  Before       After
                       Rd   A3 A2 A1 A0  B2 B0 A2 A0
                       Rm   B3 B2 B1 B0  B3 B1 A3 A1
                     */
                    if (size == 3)
                        return 1;
                    gen_neon_unzip(rd, q, 0, size);
                    gen_neon_unzip(rm, q, 4, size);
                    if (q) {
                        static int unzip_order_q[8] =
                            {0, 2, 4, 6, 1, 3, 5, 7};
                        for (n = 0; n < 8; n++) {
                            int reg = (n < 4) ? rd : rm;
                            gen_neon_movl_T0_scratch(unzip_order_q[n]);
                            NEON_SET_REG(T0, reg, n % 4);
                        }
                    } else {
                        static int unzip_order[4] =
                            {0, 4, 1, 5};
                        for (n = 0; n < 4; n++) {
                            int reg = (n < 2) ? rd : rm;
                            gen_neon_movl_T0_scratch(unzip_order[n]);
                            NEON_SET_REG(T0, reg, n % 2);
                        }
                    }
                    break;
                case 35: /* VZIP */
                    /* Reg  Before       After
                       Rd   A3 A2 A1 A0  B1 A1 B0 A0
                       Rm   B3 B2 B1 B0  B3 A3 B2 A2
                     */
                    if (size == 3)
                        return 1;
                    count = (q ? 4 : 2);
                    for (n = 0; n < count; n++) {
                        NEON_GET_REG(T0, rd, n);
                        NEON_GET_REG(T1, rd, n);
                        switch (size) {
                        case 0: gen_helper_neon_zip_u8(); break;
                        case 1: gen_helper_neon_zip_u16(); break;
                        case 2: /* no-op */; break;
                        default: abort();
                        }
                        gen_neon_movl_scratch_T0(n * 2);
                        gen_neon_movl_scratch_T1(n * 2 + 1);
                    }
                    for (n = 0; n < count * 2; n++) {
                        int reg = (n < count) ? rd : rm;
                        gen_neon_movl_T0_scratch(n);
                        NEON_SET_REG(T0, reg, n % count);
                    }
                    break;
                case 36: case 37: /* VMOVN, VQMOVUN, VQMOVN */
                    if (size == 3)
                        return 1;
                    TCGV_UNUSED(tmp2);
                    for (pass = 0; pass < 2; pass++) {
                        neon_load_reg64(cpu_V0, rm + pass);
                        tmp = new_tmp();
                        if (op == 36 && q == 0) {
                            gen_neon_narrow(size, tmp, cpu_V0);
                        } else if (q) {
                            gen_neon_narrow_satu(size, tmp, cpu_V0);
                        } else {
                            gen_neon_narrow_sats(size, tmp, cpu_V0);
                        }
                        if (pass == 0) {
                            tmp2 = tmp;
                        } else {
                            neon_store_reg(rd, 0, tmp2);
                            neon_store_reg(rd, 1, tmp);
                        }
                    }
                    break;
                case 38: /* VSHLL */
                    if (q || size == 3)
                        return 1;
                    tmp = neon_load_reg(rm, 0);
                    tmp2 = neon_load_reg(rm, 1);
                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 1)
                            tmp = tmp2;
                        gen_neon_widen(cpu_V0, tmp, size, 1);
                        neon_store_reg64(cpu_V0, rd + pass);
                    }
                    break;
                default:
                elementwise:
                    for (pass = 0; pass < (q ? 4 : 2); pass++) {
                        if (op == 30 || op == 31 || op >= 58) {
                            tcg_gen_ld_f32(cpu_F0s, cpu_env,
                                           neon_reg_offset(rm, pass));
                        } else {
                            NEON_GET_REG(T0, rm, pass);
                        }
                        switch (op) {
                        case 1: /* VREV32 */
                            switch (size) {
                            case 0: tcg_gen_bswap_i32(cpu_T[0], cpu_T[0]); break;
                            case 1: gen_swap_half(cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 2: /* VREV16 */
                            if (size != 0)
                                return 1;
                            gen_rev16(cpu_T[0]);
                            break;
                        case 8: /* CLS */
                            switch (size) {
                            case 0: gen_helper_neon_cls_s8(cpu_T[0], cpu_T[0]); break;
                            case 1: gen_helper_neon_cls_s16(cpu_T[0], cpu_T[0]); break;
                            case 2: gen_helper_neon_cls_s32(cpu_T[0], cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 9: /* CLZ */
                            switch (size) {
                            case 0: gen_helper_neon_clz_u8(cpu_T[0], cpu_T[0]); break;
                            case 1: gen_helper_neon_clz_u16(cpu_T[0], cpu_T[0]); break;
                            case 2: gen_helper_clz(cpu_T[0], cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 10: /* CNT */
                            if (size != 0)
                                return 1;
                            gen_helper_neon_cnt_u8(cpu_T[0], cpu_T[0]);
                            break;
                        case 11: /* VNOT */
                            if (size != 0)
                                return 1;
                            gen_op_notl_T0();
                            break;
                        case 14: /* VQABS */
                            switch (size) {
                            case 0: gen_helper_neon_qabs_s8(cpu_T[0], cpu_env, cpu_T[0]); break;
                            case 1: gen_helper_neon_qabs_s16(cpu_T[0], cpu_env, cpu_T[0]); break;
                            case 2: gen_helper_neon_qabs_s32(cpu_T[0], cpu_env, cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 15: /* VQNEG */
                            switch (size) {
                            case 0: gen_helper_neon_qneg_s8(cpu_T[0], cpu_env, cpu_T[0]); break;
                            case 1: gen_helper_neon_qneg_s16(cpu_T[0], cpu_env, cpu_T[0]); break;
                            case 2: gen_helper_neon_qneg_s32(cpu_T[0], cpu_env, cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 16: case 19: /* VCGT #0, VCLE #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_helper_neon_cgt_s8(CPU_T001); break;
                            case 1: gen_helper_neon_cgt_s16(CPU_T001); break;
                            case 2: gen_helper_neon_cgt_s32(CPU_T001); break;
                            default: return 1;
                            }
                            if (op == 19)
                                gen_op_notl_T0();
                            break;
                        case 17: case 20: /* VCGE #0, VCLT #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_helper_neon_cge_s8(CPU_T001); break;
                            case 1: gen_helper_neon_cge_s16(CPU_T001); break;
                            case 2: gen_helper_neon_cge_s32(CPU_T001); break;
                            default: return 1;
                            }
                            if (op == 20)
                                gen_op_notl_T0();
                            break;
                        case 18: /* VCEQ #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_helper_neon_ceq_u8(CPU_T001); break;
                            case 1: gen_helper_neon_ceq_u16(CPU_T001); break;
                            case 2: gen_helper_neon_ceq_u32(CPU_T001); break;
                            default: return 1;
                            }
                            break;
                        case 22: /* VABS */
                            switch(size) {
                            case 0: gen_helper_neon_abs_s8(cpu_T[0], cpu_T[0]); break;
                            case 1: gen_helper_neon_abs_s16(cpu_T[0], cpu_T[0]); break;
                            case 2: tcg_gen_abs_i32(cpu_T[0], cpu_T[0]); break;
                            default: return 1;
                            }
                            break;
                        case 23: /* VNEG */
                            gen_op_movl_T1_im(0);
                            if (size == 3)
                                return 1;
                            gen_neon_rsb(size);
                            break;
                        case 24: case 27: /* Float VCGT #0, Float VCLE #0 */
                            gen_op_movl_T1_im(0);
                            gen_helper_neon_cgt_f32(CPU_T001);
                            if (op == 27)
                                gen_op_notl_T0();
                            break;
                        case 25: case 28: /* Float VCGE #0, Float VCLT #0 */
                            gen_op_movl_T1_im(0);
                            gen_helper_neon_cge_f32(CPU_T001);
                            if (op == 28)
                                gen_op_notl_T0();
                            break;
                        case 26: /* Float VCEQ #0 */
                            gen_op_movl_T1_im(0);
                            gen_helper_neon_ceq_f32(CPU_T001);
                            break;
                        case 30: /* Float VABS */
                            gen_vfp_abs(0);
                            break;
                        case 31: /* Float VNEG */
                            gen_vfp_neg(0);
                            break;
                        case 32: /* VSWP */
                            NEON_GET_REG(T1, rd, pass);
                            NEON_SET_REG(T1, rm, pass);
                            break;
                        case 33: /* VTRN */
                            NEON_GET_REG(T1, rd, pass);
                            switch (size) {
                            case 0: gen_helper_neon_trn_u8(); break;
                            case 1: gen_helper_neon_trn_u16(); break;
                            case 2: abort();
                            default: return 1;
                            }
                            NEON_SET_REG(T1, rm, pass);
                            break;
                        case 56: /* Integer VRECPE */
                            gen_helper_recpe_u32(cpu_T[0], cpu_T[0], cpu_env);
                            break;
                        case 57: /* Integer VRSQRTE */
                            gen_helper_rsqrte_u32(cpu_T[0], cpu_T[0], cpu_env);
                            break;
                        case 58: /* Float VRECPE */
                            gen_helper_recpe_f32(cpu_F0s, cpu_F0s, cpu_env);
                            break;
                        case 59: /* Float VRSQRTE */
                            gen_helper_rsqrte_f32(cpu_F0s, cpu_F0s, cpu_env);
                            break;
                        case 60: /* VCVT.F32.S32 */
                            gen_vfp_tosiz(0);
                            break;
                        case 61: /* VCVT.F32.U32 */
                            gen_vfp_touiz(0);
                            break;
                        case 62: /* VCVT.S32.F32 */
                            gen_vfp_sito(0);
                            break;
                        case 63: /* VCVT.U32.F32 */
                            gen_vfp_uito(0);
                            break;
                        default:
                            /* Reserved: 21, 29, 39-56 */
                            return 1;
                        }
                        if (op == 30 || op == 31 || op >= 58) {
                            tcg_gen_st_f32(cpu_F0s, cpu_env,
                                           neon_reg_offset(rd, pass));
                        } else {
                            NEON_SET_REG(T0, rd, pass);
                        }
                    }
                    break;
                }
            } else if ((insn & (1 << 10)) == 0) {
                /* VTBL, VTBX.  */
                n = ((insn >> 5) & 0x18) + 8;
                if (insn & (1 << 6)) {
                    tmp = neon_load_reg(rd, 0);
                } else {
                    tmp = new_tmp();
                    tcg_gen_movi_i32(tmp, 0);
                }
                tmp2 = neon_load_reg(rm, 0);
                gen_helper_neon_tbl(tmp2, tmp2, tmp, tcg_const_i32(rn),
                                    tcg_const_i32(n));
                dead_tmp(tmp);
                if (insn & (1 << 6)) {
                    tmp = neon_load_reg(rd, 1);
                } else {
                    tmp = new_tmp();
                    tcg_gen_movi_i32(tmp, 0);
                }
                tmp3 = neon_load_reg(rm, 1);
                gen_helper_neon_tbl(tmp3, tmp3, tmp, tcg_const_i32(rn),
                                    tcg_const_i32(n));
                neon_store_reg(rd, 0, tmp2);
                neon_store_reg(rd, 1, tmp3);
                dead_tmp(tmp);
            } else if ((insn & 0x380) == 0) {
                /* VDUP */
                if (insn & (1 << 19)) {
                    NEON_SET_REG(T0, rm, 1);
                } else {
                    NEON_SET_REG(T0, rm, 0);
                }
                if (insn & (1 << 16)) {
                    gen_neon_dup_u8(cpu_T[0], ((insn >> 17) & 3) * 8);
                } else if (insn & (1 << 17)) {
                    if ((insn >> 18) & 1)
                        gen_neon_dup_high16(cpu_T[0]);
                    else
                        gen_neon_dup_low16(cpu_T[0]);
                }
                for (pass = 0; pass < (q ? 4 : 2); pass++) {
                    NEON_SET_REG(T0, rd, pass);
                }
            } else {
                return 1;
            }
        }
    }
    return 0;
}

static int disas_cp14_read(CPUState * env, DisasContext *s, uint32_t insn)
{
    int crn = (insn >> 16) & 0xf;
    int crm = insn & 0xf;
    int op1 = (insn >> 21) & 7;
    int op2 = (insn >> 5) & 7;
    int rt = (insn >> 12) & 0xf;
    TCGv tmp;

    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        if (op1 == 6 && crn == 0 && crm == 0 && op2 == 0) {
            /* TEECR */
            if (IS_USER(s))
                return 1;
            tmp = load_cpu_field(teecr);
            store_reg(s, rt, tmp);
            return 0;
        }
        if (op1 == 6 && crn == 1 && crm == 0 && op2 == 0) {
            /* TEEHBR */
            if (IS_USER(s) && (env->teecr & 1))
                return 1;
            tmp = load_cpu_field(teehbr);
            store_reg(s, rt, tmp);
            return 0;
        }
    }
    fprintf(stderr, "Unknown cp14 read op1:%d crn:%d crm:%d op2:%d\n",
            op1, crn, crm, op2);
    return 1;
}

static int disas_cp14_write(CPUState * env, DisasContext *s, uint32_t insn)
{
    int crn = (insn >> 16) & 0xf;
    int crm = insn & 0xf;
    int op1 = (insn >> 21) & 7;
    int op2 = (insn >> 5) & 7;
    int rt = (insn >> 12) & 0xf;
    TCGv tmp;

    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        if (op1 == 6 && crn == 0 && crm == 0 && op2 == 0) {
            /* TEECR */
            if (IS_USER(s))
                return 1;
            tmp = load_reg(s, rt);
            gen_helper_set_teecr(cpu_env, tmp);
            dead_tmp(tmp);
            return 0;
        }
        if (op1 == 6 && crn == 1 && crm == 0 && op2 == 0) {
            /* TEEHBR */
            if (IS_USER(s) && (env->teecr & 1))
                return 1;
            tmp = load_reg(s, rt);
            store_cpu_field(tmp, teehbr);
            return 0;
        }
    }
    fprintf(stderr, "Unknown cp14 write op1:%d crn:%d crm:%d op2:%d\n",
            op1, crn, crm, op2);
    return 1;
}

static int disas_coproc_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int cpnum;

    cpnum = (insn >> 8) & 0xf;
    if (arm_feature(env, ARM_FEATURE_XSCALE)
	    && ((env->cp15.c15_cpar ^ 0x3fff) & (1 << cpnum)))
	return 1;

    switch (cpnum) {
      case 0:
      case 1:
	if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
	    return disas_iwmmxt_insn(env, s, insn);
	} else if (arm_feature(env, ARM_FEATURE_XSCALE)) {
	    return disas_dsp_insn(env, s, insn);
	}
	return 1;
    case 10:
    case 11:
	return disas_vfp_insn (env, s, insn);
    case 14:
        /* Coprocessors 7-15 are architecturally reserved by ARM.
           Unfortunately Intel decided to ignore this.  */
        if (arm_feature(env, ARM_FEATURE_XSCALE))
            goto board;
        if (insn & (1 << 20))
            return disas_cp14_read(env, s, insn);
        else
            return disas_cp14_write(env, s, insn);
    case 15:
	return disas_cp15_insn (env, s, insn);
    default:
    board:
	/* Unknown coprocessor.  See if the board has hooked it.  */
	return disas_cp_insn (env, s, insn);
    }
}


/* Store a 64-bit value to a register pair.  Clobbers val.  */
static void gen_storeq_reg(DisasContext *s, int rlow, int rhigh, TCGv_i64 val)
{
    TCGv tmp;
    tmp = new_tmp();
    tcg_gen_trunc_i64_i32(tmp, val);
    store_reg(s, rlow, tmp);
    tmp = new_tmp();
    tcg_gen_shri_i64(val, val, 32);
    tcg_gen_trunc_i64_i32(tmp, val);
    store_reg(s, rhigh, tmp);
}

/* load a 32-bit value from a register and perform a 64-bit accumulate.  */
static void gen_addq_lo(DisasContext *s, TCGv_i64 val, int rlow)
{
    TCGv_i64 tmp;
    TCGv tmp2;

    /* Load value and extend to 64 bits.  */
    tmp = tcg_temp_new_i64();
    tmp2 = load_reg(s, rlow);
    tcg_gen_extu_i32_i64(tmp, tmp2);
    dead_tmp(tmp2);
    tcg_gen_add_i64(val, val, tmp);
}

/* load and add a 64-bit value from a register pair.  */
static void gen_addq(DisasContext *s, TCGv_i64 val, int rlow, int rhigh)
{
    TCGv_i64 tmp;
    TCGv tmpl;
    TCGv tmph;

    /* Load 64-bit value rd:rn.  */
    tmpl = load_reg(s, rlow);
    tmph = load_reg(s, rhigh);
    tmp = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(tmp, tmpl, tmph);
    dead_tmp(tmpl);
    dead_tmp(tmph);
    tcg_gen_add_i64(val, val, tmp);
}

/* Set N and Z flags from a 64-bit value.  */
static void gen_logicq_cc(TCGv_i64 val)
{
    TCGv tmp = new_tmp();
    gen_helper_logicq_cc(tmp, val);
    gen_logic_CC(tmp);
    dead_tmp(tmp);
}

static void disas_arm_insn(CPUState * env, DisasContext *s)
{
    unsigned int cond, insn, val, op1, i, shift, rm, rs, rn, rd, sh;
    TCGv tmp;
    TCGv tmp2;
    TCGv tmp3;
    TCGv addr;
    TCGv_i64 tmp64;

    insn = ldl_code(s->pc);
    s->pc += 4;

    /* M variants do not implement ARM mode.  */
    if (IS_M(env))
        goto illegal_op;
    cond = insn >> 28;
    if (cond == 0xf){
        /* Unconditional instructions.  */
        if (((insn >> 25) & 7) == 1) {
            /* NEON Data processing.  */
            if (!arm_feature(env, ARM_FEATURE_NEON))
                goto illegal_op;

            if (disas_neon_data_insn(env, s, insn))
                goto illegal_op;
            return;
        }
        if ((insn & 0x0f100000) == 0x04000000) {
            /* NEON load/store.  */
            if (!arm_feature(env, ARM_FEATURE_NEON))
                goto illegal_op;

            if (disas_neon_ls_insn(env, s, insn))
                goto illegal_op;
            return;
        }
        if ((insn & 0x0d70f000) == 0x0550f000)
            return; /* PLD */
        else if ((insn & 0x0ffffdff) == 0x01010000) {
            ARCH(6);
            /* setend */
            if (insn & (1 << 9)) {
                /* BE8 mode not implemented.  */
                goto illegal_op;
            }
            return;
        } else if ((insn & 0x0fffff00) == 0x057ff000) {
            switch ((insn >> 4) & 0xf) {
            case 1: /* clrex */
                ARCH(6K);
                gen_helper_clrex(cpu_env);
                return;
            case 4: /* dsb */
            case 5: /* dmb */
            case 6: /* isb */
                ARCH(7);
                /* We don't emulate caches so these are a no-op.  */
                return;
            default:
                goto illegal_op;
            }
        } else if ((insn & 0x0e5fffe0) == 0x084d0500) {
            /* srs */
            uint32_t offset;
            if (IS_USER(s))
                goto illegal_op;
            ARCH(6);
            op1 = (insn & 0x1f);
            if (op1 == (env->uncached_cpsr & CPSR_M)) {
                addr = load_reg(s, 13);
            } else {
                addr = new_tmp();
                gen_helper_get_r13_banked(addr, cpu_env, tcg_const_i32(op1));
            }
            i = (insn >> 23) & 3;
            switch (i) {
            case 0: offset = -4; break; /* DA */
            case 1: offset = -8; break; /* DB */
            case 2: offset = 0; break; /* IA */
            case 3: offset = 4; break; /* IB */
            default: abort();
            }
            if (offset)
                tcg_gen_addi_i32(addr, addr, offset);
            tmp = load_reg(s, 14);
            gen_st32(tmp, addr, 0);
            tmp = new_tmp();
            gen_helper_cpsr_read(tmp);
            tcg_gen_addi_i32(addr, addr, 4);
            gen_st32(tmp, addr, 0);
            if (insn & (1 << 21)) {
                /* Base writeback.  */
                switch (i) {
                case 0: offset = -8; break;
                case 1: offset = -4; break;
                case 2: offset = 4; break;
                case 3: offset = 0; break;
                default: abort();
                }
                if (offset)
                    tcg_gen_addi_i32(addr, tmp, offset);
                if (op1 == (env->uncached_cpsr & CPSR_M)) {
                    gen_movl_reg_T1(s, 13);
                } else {
                    gen_helper_set_r13_banked(cpu_env, tcg_const_i32(op1), cpu_T[1]);
                }
            } else {
                dead_tmp(addr);
            }
        } else if ((insn & 0x0e5fffe0) == 0x081d0a00) {
            /* rfe */
            uint32_t offset;
            if (IS_USER(s))
                goto illegal_op;
            ARCH(6);
            rn = (insn >> 16) & 0xf;
            addr = load_reg(s, rn);
            i = (insn >> 23) & 3;
            switch (i) {
            case 0: offset = -4; break; /* DA */
            case 1: offset = -8; break; /* DB */
            case 2: offset = 0; break; /* IA */
            case 3: offset = 4; break; /* IB */
            default: abort();
            }
            if (offset)
                tcg_gen_addi_i32(addr, addr, offset);
            /* Load PC into tmp and CPSR into tmp2.  */
            tmp = gen_ld32(addr, 0);
            tcg_gen_addi_i32(addr, addr, 4);
            tmp2 = gen_ld32(addr, 0);
            if (insn & (1 << 21)) {
                /* Base writeback.  */
                switch (i) {
                case 0: offset = -8; break;
                case 1: offset = -4; break;
                case 2: offset = 4; break;
                case 3: offset = 0; break;
                default: abort();
                }
                if (offset)
                    tcg_gen_addi_i32(addr, addr, offset);
                store_reg(s, rn, addr);
            } else {
                dead_tmp(addr);
            }
            gen_rfe(s, tmp, tmp2);
        } else if ((insn & 0x0e000000) == 0x0a000000) {
            /* branch link and change to thumb (blx <offset>) */
            int32_t offset;

            val = (uint32_t)s->pc;
            tmp = new_tmp();
            tcg_gen_movi_i32(tmp, val);
            store_reg(s, 14, tmp);
            /* Sign-extend the 24-bit offset */
            offset = (((int32_t)insn) << 8) >> 8;
            /* offset * 4 + bit24 * 2 + (thumb bit) */
            val += (offset << 2) | ((insn >> 23) & 2) | 1;
            /* pipeline offset */
            val += 4;
            gen_bx_im(s, val);
            return;
        } else if ((insn & 0x0e000f00) == 0x0c000100) {
            if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
                /* iWMMXt register transfer.  */
                if (env->cp15.c15_cpar & (1 << 1))
                    if (!disas_iwmmxt_insn(env, s, insn))
                        return;
            }
        } else if ((insn & 0x0fe00000) == 0x0c400000) {
            /* Coprocessor double register transfer.  */
        } else if ((insn & 0x0f000010) == 0x0e000010) {
            /* Additional coprocessor register transfer.  */
        } else if ((insn & 0x0ff10020) == 0x01000000) {
            uint32_t mask;
            uint32_t val;
            /* cps (privileged) */
            if (IS_USER(s))
                return;
            mask = val = 0;
            if (insn & (1 << 19)) {
                if (insn & (1 << 8))
                    mask |= CPSR_A;
                if (insn & (1 << 7))
                    mask |= CPSR_I;
                if (insn & (1 << 6))
                    mask |= CPSR_F;
                if (insn & (1 << 18))
                    val |= mask;
            }
            if (insn & (1 << 17)) {
                mask |= CPSR_M;
                val |= (insn & 0x1f);
            }
            if (mask) {
                gen_op_movl_T0_im(val);
                gen_set_psr_T0(s, mask, 0);
            }
            return;
        }
        goto illegal_op;
    }
    if (cond != 0xe) {
        /* if not always execute, we generate a conditional jump to
           next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc(cond ^ 1, s->condlabel);
        s->condjmp = 1;
    }
    if ((insn & 0x0f900000) == 0x03000000) {
        if ((insn & (1 << 21)) == 0) {
            ARCH(6T2);
            rd = (insn >> 12) & 0xf;
            val = ((insn >> 4) & 0xf000) | (insn & 0xfff);
            if ((insn & (1 << 22)) == 0) {
                /* MOVW */
                tmp = new_tmp();
                tcg_gen_movi_i32(tmp, val);
            } else {
                /* MOVT */
                tmp = load_reg(s, rd);
                tcg_gen_ext16u_i32(tmp, tmp);
                tcg_gen_ori_i32(tmp, tmp, val << 16);
            }
            store_reg(s, rd, tmp);
        } else {
            if (((insn >> 12) & 0xf) != 0xf)
                goto illegal_op;
            if (((insn >> 16) & 0xf) == 0) {
                gen_nop_hint(s, insn & 0xff);
            } else {
                /* CPSR = immediate */
                val = insn & 0xff;
                shift = ((insn >> 8) & 0xf) * 2;
                if (shift)
                    val = (val >> shift) | (val << (32 - shift));
                gen_op_movl_T0_im(val);
                i = ((insn & (1 << 22)) != 0);
                if (gen_set_psr_T0(s, msr_mask(env, s, (insn >> 16) & 0xf, i), i))
                    goto illegal_op;
            }
        }
    } else if ((insn & 0x0f900000) == 0x01000000
               && (insn & 0x00000090) != 0x00000090) {
        /* miscellaneous instructions */
        op1 = (insn >> 21) & 3;
        sh = (insn >> 4) & 0xf;
        rm = insn & 0xf;
        switch (sh) {
        case 0x0: /* move program status register */
            if (op1 & 1) {
                /* PSR = reg */
                gen_movl_T0_reg(s, rm);
                i = ((op1 & 2) != 0);
                if (gen_set_psr_T0(s, msr_mask(env, s, (insn >> 16) & 0xf, i), i))
                    goto illegal_op;
            } else {
                /* reg = PSR */
                rd = (insn >> 12) & 0xf;
                if (op1 & 2) {
                    if (IS_USER(s))
                        goto illegal_op;
                    tmp = load_cpu_field(spsr);
                } else {
                    tmp = new_tmp();
                    gen_helper_cpsr_read(tmp);
                }
                store_reg(s, rd, tmp);
            }
            break;
        case 0x1:
            if (op1 == 1) {
                /* branch/exchange thumb (bx).  */
                tmp = load_reg(s, rm);
                gen_bx(s, tmp);
            } else if (op1 == 3) {
                /* clz */
                rd = (insn >> 12) & 0xf;
                tmp = load_reg(s, rm);
                gen_helper_clz(tmp, tmp);
                store_reg(s, rd, tmp);
            } else {
                goto illegal_op;
            }
            break;
        case 0x2:
            if (op1 == 1) {
                ARCH(5J); /* bxj */
                /* Trivial implementation equivalent to bx.  */
                tmp = load_reg(s, rm);
                gen_bx(s, tmp);
            } else {
                goto illegal_op;
            }
            break;
        case 0x3:
            if (op1 != 1)
              goto illegal_op;

            /* branch link/exchange thumb (blx) */
            tmp = load_reg(s, rm);
            tmp2 = new_tmp();
            tcg_gen_movi_i32(tmp2, s->pc);
            store_reg(s, 14, tmp2);
            gen_bx(s, tmp);
            break;
        case 0x5: /* saturating add/subtract */
            rd = (insn >> 12) & 0xf;
            rn = (insn >> 16) & 0xf;
            tmp = load_reg(s, rm);
            tmp2 = load_reg(s, rn);
            if (op1 & 2)
                gen_helper_double_saturate(tmp2, tmp2);
            if (op1 & 1)
                gen_helper_sub_saturate(tmp, tmp, tmp2);
            else
                gen_helper_add_saturate(tmp, tmp, tmp2);
            dead_tmp(tmp2);
            store_reg(s, rd, tmp);
            break;
        case 7: /* bkpt */
            gen_set_condexec(s);
            gen_set_pc_im(s->pc - 4);
            gen_exception(EXCP_BKPT);
            s->is_jmp = DISAS_JUMP;
            break;
        case 0x8: /* signed multiply */
        case 0xa:
        case 0xc:
        case 0xe:
            rs = (insn >> 8) & 0xf;
            rn = (insn >> 12) & 0xf;
            rd = (insn >> 16) & 0xf;
            if (op1 == 1) {
                /* (32 * 16) >> 16 */
                tmp = load_reg(s, rm);
                tmp2 = load_reg(s, rs);
                if (sh & 4)
                    tcg_gen_sari_i32(tmp2, tmp2, 16);
                else
                    gen_sxth(tmp2);
                tmp64 = gen_muls_i64_i32(tmp, tmp2);
                tcg_gen_shri_i64(tmp64, tmp64, 16);
                tmp = new_tmp();
                tcg_gen_trunc_i64_i32(tmp, tmp64);
                if ((sh & 2) == 0) {
                    tmp2 = load_reg(s, rn);
                    gen_helper_add_setq(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                }
                store_reg(s, rd, tmp);
            } else {
                /* 16 * 16 */
                tmp = load_reg(s, rm);
                tmp2 = load_reg(s, rs);
                gen_mulxy(tmp, tmp2, sh & 2, sh & 4);
                dead_tmp(tmp2);
                if (op1 == 2) {
                    tmp64 = tcg_temp_new_i64();
                    tcg_gen_ext_i32_i64(tmp64, tmp);
                    dead_tmp(tmp);
                    gen_addq(s, tmp64, rn, rd);
                    gen_storeq_reg(s, rn, rd, tmp64);
                } else {
                    if (op1 == 0) {
                        tmp2 = load_reg(s, rn);
                        gen_helper_add_setq(tmp, tmp, tmp2);
                        dead_tmp(tmp2);
                    }
                    store_reg(s, rd, tmp);
                }
            }
            break;
        default:
            goto illegal_op;
        }
    } else if (((insn & 0x0e000000) == 0 &&
                (insn & 0x00000090) != 0x90) ||
               ((insn & 0x0e000000) == (1 << 25))) {
        int set_cc, logic_cc, shiftop;

        op1 = (insn >> 21) & 0xf;
        set_cc = (insn >> 20) & 1;
        logic_cc = table_logic_cc[op1] & set_cc;

        /* data processing instruction */
        if (insn & (1 << 25)) {
            /* immediate operand */
            val = insn & 0xff;
            shift = ((insn >> 8) & 0xf) * 2;
            if (shift)
                val = (val >> shift) | (val << (32 - shift));
            gen_op_movl_T1_im(val);
            if (logic_cc && shift)
                gen_set_CF_bit31(cpu_T[1]);
        } else {
            /* register */
            rm = (insn) & 0xf;
            gen_movl_T1_reg(s, rm);
            shiftop = (insn >> 5) & 3;
            if (!(insn & (1 << 4))) {
                shift = (insn >> 7) & 0x1f;
                gen_arm_shift_im(cpu_T[1], shiftop, shift, logic_cc);
            } else {
                rs = (insn >> 8) & 0xf;
                tmp = load_reg(s, rs);
                gen_arm_shift_reg(cpu_T[1], shiftop, tmp, logic_cc);
            }
        }
        if (op1 != 0x0f && op1 != 0x0d) {
            rn = (insn >> 16) & 0xf;
            gen_movl_T0_reg(s, rn);
        }
        rd = (insn >> 12) & 0xf;
        switch(op1) {
        case 0x00:
            gen_op_andl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x01:
            gen_op_xorl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x02:
            if (set_cc && rd == 15) {
                /* SUBS r15, ... is used for exception return.  */
                if (IS_USER(s))
                    goto illegal_op;
                gen_op_subl_T0_T1_cc();
                gen_exception_return(s);
            } else {
                if (set_cc)
                    gen_op_subl_T0_T1_cc();
                else
                    gen_op_subl_T0_T1();
                gen_movl_reg_T0(s, rd);
            }
            break;
        case 0x03:
            if (set_cc)
                gen_op_rsbl_T0_T1_cc();
            else
                gen_op_rsbl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x04:
            if (set_cc)
                gen_op_addl_T0_T1_cc();
            else
                gen_op_addl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x05:
            if (set_cc)
                gen_op_adcl_T0_T1_cc();
            else
                gen_adc_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x06:
            if (set_cc)
                gen_op_sbcl_T0_T1_cc();
            else
                gen_sbc_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x07:
            if (set_cc)
                gen_op_rscl_T0_T1_cc();
            else
                gen_rsc_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x08:
            if (set_cc) {
                gen_op_andl_T0_T1();
                gen_op_logic_T0_cc();
            }
            break;
        case 0x09:
            if (set_cc) {
                gen_op_xorl_T0_T1();
                gen_op_logic_T0_cc();
            }
            break;
        case 0x0a:
            if (set_cc) {
                gen_op_subl_T0_T1_cc();
            }
            break;
        case 0x0b:
            if (set_cc) {
                gen_op_addl_T0_T1_cc();
            }
            break;
        case 0x0c:
            gen_op_orl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x0d:
            if (logic_cc && rd == 15) {
                /* MOVS r15, ... is used for exception return.  */
                if (IS_USER(s))
                    goto illegal_op;
                gen_op_movl_T0_T1();
                gen_exception_return(s);
            } else {
                gen_movl_reg_T1(s, rd);
                if (logic_cc)
                    gen_op_logic_T1_cc();
            }
            break;
        case 0x0e:
            gen_op_bicl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        default:
        case 0x0f:
            gen_op_notl_T1();
            gen_movl_reg_T1(s, rd);
            if (logic_cc)
                gen_op_logic_T1_cc();
            break;
        }
    } else {
        /* other instructions */
        op1 = (insn >> 24) & 0xf;
        switch(op1) {
        case 0x0:
        case 0x1:
            /* multiplies, extra load/stores */
            sh = (insn >> 5) & 3;
            if (sh == 0) {
                if (op1 == 0x0) {
                    rd = (insn >> 16) & 0xf;
                    rn = (insn >> 12) & 0xf;
                    rs = (insn >> 8) & 0xf;
                    rm = (insn) & 0xf;
                    op1 = (insn >> 20) & 0xf;
                    switch (op1) {
                    case 0: case 1: case 2: case 3: case 6:
                        /* 32 bit mul */
                        tmp = load_reg(s, rs);
                        tmp2 = load_reg(s, rm);
                        tcg_gen_mul_i32(tmp, tmp, tmp2);
                        dead_tmp(tmp2);
                        if (insn & (1 << 22)) {
                            /* Subtract (mls) */
                            ARCH(6T2);
                            tmp2 = load_reg(s, rn);
                            tcg_gen_sub_i32(tmp, tmp2, tmp);
                            dead_tmp(tmp2);
                        } else if (insn & (1 << 21)) {
                            /* Add */
                            tmp2 = load_reg(s, rn);
                            tcg_gen_add_i32(tmp, tmp, tmp2);
                            dead_tmp(tmp2);
                        }
                        if (insn & (1 << 20))
                            gen_logic_CC(tmp);
                        store_reg(s, rd, tmp);
                        break;
                    default:
                        /* 64 bit mul */
                        tmp = load_reg(s, rs);
                        tmp2 = load_reg(s, rm);
                        if (insn & (1 << 22))
                            tmp64 = gen_muls_i64_i32(tmp, tmp2);
                        else
                            tmp64 = gen_mulu_i64_i32(tmp, tmp2);
                        if (insn & (1 << 21)) /* mult accumulate */
                            gen_addq(s, tmp64, rn, rd);
                        if (!(insn & (1 << 23))) { /* double accumulate */
                            ARCH(6);
                            gen_addq_lo(s, tmp64, rn);
                            gen_addq_lo(s, tmp64, rd);
                        }
                        if (insn & (1 << 20))
                            gen_logicq_cc(tmp64);
                        gen_storeq_reg(s, rn, rd, tmp64);
                        break;
                    }
                } else {
                    rn = (insn >> 16) & 0xf;
                    rd = (insn >> 12) & 0xf;
                    if (insn & (1 << 23)) {
                        /* load/store exclusive */
                        op1 = (insn >> 21) & 0x3;
                        if (op1)
                            ARCH(6K);
                        else
                            ARCH(6);
                        gen_movl_T1_reg(s, rn);
                        addr = cpu_T[1];
                        if (insn & (1 << 20)) {
                            gen_helper_mark_exclusive(cpu_env, cpu_T[1]);
                            switch (op1) {
                            case 0: /* ldrex */
                                tmp = gen_ld32(addr, IS_USER(s));
                                break;
                            case 1: /* ldrexd */
                                tmp = gen_ld32(addr, IS_USER(s));
                                store_reg(s, rd, tmp);
                                tcg_gen_addi_i32(addr, addr, 4);
                                tmp = gen_ld32(addr, IS_USER(s));
                                rd++;
                                break;
                            case 2: /* ldrexb */
                                tmp = gen_ld8u(addr, IS_USER(s));
                                break;
                            case 3: /* ldrexh */
                                tmp = gen_ld16u(addr, IS_USER(s));
                                break;
                            default:
                                abort();
                            }
                            store_reg(s, rd, tmp);
                        } else {
                            int label = gen_new_label();
                            rm = insn & 0xf;
                            gen_helper_test_exclusive(cpu_T[0], cpu_env, addr);
                            tcg_gen_brcondi_i32(TCG_COND_NE, cpu_T[0],
                                                0, label);
                            tmp = load_reg(s,rm);
                            switch (op1) {
                            case 0:  /*  strex */
                                gen_st32(tmp, addr, IS_USER(s));
                                break;
                            case 1: /*  strexd */
                                gen_st32(tmp, addr, IS_USER(s));
                                tcg_gen_addi_i32(addr, addr, 4);
                                tmp = load_reg(s, rm + 1);
                                gen_st32(tmp, addr, IS_USER(s));
                                break;
                            case 2: /*  strexb */
                                gen_st8(tmp, addr, IS_USER(s));
                                break;
                            case 3: /* strexh */
                                gen_st16(tmp, addr, IS_USER(s));
                                break;
                            default:
                                abort();
                            }
                            gen_set_label(label);
                            gen_movl_reg_T0(s, rd);
                        }
                    } else {
                        /* SWP instruction */
                        rm = (insn) & 0xf;

                        /* ??? This is not really atomic.  However we know
                           we never have multiple CPUs running in parallel,
                           so it is good enough.  */
                        addr = load_reg(s, rn);
                        tmp = load_reg(s, rm);
                        if (insn & (1 << 22)) {
                            tmp2 = gen_ld8u(addr, IS_USER(s));
                            gen_st8(tmp, addr, IS_USER(s));
                        } else {
                            tmp2 = gen_ld32(addr, IS_USER(s));
                            gen_st32(tmp, addr, IS_USER(s));
                        }
                        dead_tmp(addr);
                        store_reg(s, rd, tmp2);
                    }
                }
            } else {
                int address_offset;
                int load;
                /* Misc load/store */
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                addr = load_reg(s, rn);
                if (insn & (1 << 24))
                    gen_add_datah_offset(s, insn, 0, addr);
                address_offset = 0;
                if (insn & (1 << 20)) {
                    /* load */
                    switch(sh) {
                    case 1:
                        tmp = gen_ld16u(addr, IS_USER(s));
                        break;
                    case 2:
                        tmp = gen_ld8s(addr, IS_USER(s));
                        break;
                    default:
                    case 3:
                        tmp = gen_ld16s(addr, IS_USER(s));
                        break;
                    }
                    load = 1;
                } else if (sh & 2) {
                    /* doubleword */
                    if (sh & 1) {
                        /* store */
                        tmp = load_reg(s, rd);
                        gen_st32(tmp, addr, IS_USER(s));
                        tcg_gen_addi_i32(addr, addr, 4);
                        tmp = load_reg(s, rd + 1);
                        gen_st32(tmp, addr, IS_USER(s));
                        load = 0;
                    } else {
                        /* load */
                        tmp = gen_ld32(addr, IS_USER(s));
                        store_reg(s, rd, tmp);
                        tcg_gen_addi_i32(addr, addr, 4);
                        tmp = gen_ld32(addr, IS_USER(s));
                        rd++;
                        load = 1;
                    }
                    address_offset = -4;
                } else {
                    /* store */
                    tmp = load_reg(s, rd);
                    gen_st16(tmp, addr, IS_USER(s));
                    load = 0;
                }
                /* Perform base writeback before the loaded value to
                   ensure correct behavior with overlapping index registers.
                   ldrd with base writeback is is undefined if the
                   destination and index registers overlap.  */
                if (!(insn & (1 << 24))) {
                    gen_add_datah_offset(s, insn, address_offset, addr);
                    store_reg(s, rn, addr);
                } else if (insn & (1 << 21)) {
                    if (address_offset)
                        tcg_gen_addi_i32(addr, addr, address_offset);
                    store_reg(s, rn, addr);
                } else {
                    dead_tmp(addr);
                }
                if (load) {
                    /* Complete the load.  */
                    store_reg(s, rd, tmp);
                }
            }
            break;
        case 0x4:
        case 0x5:
            goto do_ldst;
        case 0x6:
        case 0x7:
            if (insn & (1 << 4)) {
                ARCH(6);
                /* Armv6 Media instructions.  */
                rm = insn & 0xf;
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                rs = (insn >> 8) & 0xf;
                switch ((insn >> 23) & 3) {
                case 0: /* Parallel add/subtract.  */
                    op1 = (insn >> 20) & 7;
                    tmp = load_reg(s, rn);
                    tmp2 = load_reg(s, rm);
                    sh = (insn >> 5) & 7;
                    if ((op1 & 3) == 0 || sh == 5 || sh == 6)
                        goto illegal_op;
                    gen_arm_parallel_addsub(op1, sh, tmp, tmp2);
                    dead_tmp(tmp2);
                    store_reg(s, rd, tmp);
                    break;
                case 1:
                    if ((insn & 0x00700020) == 0) {
                        /* Halfword pack.  */
                        tmp = load_reg(s, rn);
                        tmp2 = load_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        if (insn & (1 << 6)) {
                            /* pkhtb */
                            if (shift == 0)
                                shift = 31;
                            tcg_gen_sari_i32(tmp2, tmp2, shift);
                            tcg_gen_andi_i32(tmp, tmp, 0xffff0000);
                            tcg_gen_ext16u_i32(tmp2, tmp2);
                        } else {
                            /* pkhbt */
                            if (shift)
                                tcg_gen_shli_i32(tmp2, tmp2, shift);
                            tcg_gen_ext16u_i32(tmp, tmp);
                            tcg_gen_andi_i32(tmp2, tmp2, 0xffff0000);
                        }
                        tcg_gen_or_i32(tmp, tmp, tmp2);
                        dead_tmp(tmp2);
                        store_reg(s, rd, tmp);
                    } else if ((insn & 0x00200020) == 0x00200000) {
                        /* [us]sat */
                        tmp = load_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        if (insn & (1 << 6)) {
                            if (shift == 0)
                                shift = 31;
                            tcg_gen_sari_i32(tmp, tmp, shift);
                        } else {
                            tcg_gen_shli_i32(tmp, tmp, shift);
                        }
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22))
                                gen_helper_usat(tmp, tmp, tcg_const_i32(sh));
                            else
                                gen_helper_ssat(tmp, tmp, tcg_const_i32(sh));
                        }
                        store_reg(s, rd, tmp);
                    } else if ((insn & 0x00300fe0) == 0x00200f20) {
                        /* [us]sat16 */
                        tmp = load_reg(s, rm);
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22))
                                gen_helper_usat16(tmp, tmp, tcg_const_i32(sh));
                            else
                                gen_helper_ssat16(tmp, tmp, tcg_const_i32(sh));
                        }
                        store_reg(s, rd, tmp);
                    } else if ((insn & 0x00700fe0) == 0x00000fa0) {
                        /* Select bytes.  */
                        tmp = load_reg(s, rn);
                        tmp2 = load_reg(s, rm);
                        tmp3 = new_tmp();
                        tcg_gen_ld_i32(tmp3, cpu_env, offsetof(CPUState, GE));
                        gen_helper_sel_flags(tmp, tmp3, tmp, tmp2);
                        dead_tmp(tmp3);
                        dead_tmp(tmp2);
                        store_reg(s, rd, tmp);
                    } else if ((insn & 0x000003e0) == 0x00000060) {
                        tmp = load_reg(s, rm);
                        shift = (insn >> 10) & 3;
                        /* ??? In many cases it's not neccessary to do a
                           rotate, a shift is sufficient.  */
                        if (shift != 0)
                            tcg_gen_rori_i32(tmp, tmp, shift * 8);
                        op1 = (insn >> 20) & 7;
                        switch (op1) {
                        case 0: gen_sxtb16(tmp);  break;
                        case 2: gen_sxtb(tmp);    break;
                        case 3: gen_sxth(tmp);    break;
                        case 4: gen_uxtb16(tmp);  break;
                        case 6: gen_uxtb(tmp);    break;
                        case 7: gen_uxth(tmp);    break;
                        default: goto illegal_op;
                        }
                        if (rn != 15) {
                            tmp2 = load_reg(s, rn);
                            if ((op1 & 3) == 0) {
                                gen_add16(tmp, tmp2);
                            } else {
                                tcg_gen_add_i32(tmp, tmp, tmp2);
                                dead_tmp(tmp2);
                            }
                        }
                        store_reg(s, rd, tmp);
                    } else if ((insn & 0x003f0f60) == 0x003f0f20) {
                        /* rev */
                        tmp = load_reg(s, rm);
                        if (insn & (1 << 22)) {
                            if (insn & (1 << 7)) {
                                gen_revsh(tmp);
                            } else {
                                ARCH(6T2);
                                gen_helper_rbit(tmp, tmp);
                            }
                        } else {
                            if (insn & (1 << 7))
                                gen_rev16(tmp);
                            else
                                tcg_gen_bswap_i32(tmp, tmp);
                        }
                        store_reg(s, rd, tmp);
                    } else {
                        goto illegal_op;
                    }
                    break;
                case 2: /* Multiplies (Type 3).  */
                    tmp = load_reg(s, rm);
                    tmp2 = load_reg(s, rs);
                    if (insn & (1 << 20)) {
                        /* Signed multiply most significant [accumulate].  */
                        tmp64 = gen_muls_i64_i32(tmp, tmp2);
                        if (insn & (1 << 5))
                            tcg_gen_addi_i64(tmp64, tmp64, 0x80000000u);
                        tcg_gen_shri_i64(tmp64, tmp64, 32);
                        tmp = new_tmp();
                        tcg_gen_trunc_i64_i32(tmp, tmp64);
                        if (rd != 15) {
                            tmp2 = load_reg(s, rd);
                            if (insn & (1 << 6)) {
                                tcg_gen_sub_i32(tmp, tmp, tmp2);
                            } else {
                                tcg_gen_add_i32(tmp, tmp, tmp2);
                            }
                            dead_tmp(tmp2);
                        }
                        store_reg(s, rn, tmp);
                    } else {
                        if (insn & (1 << 5))
                            gen_swap_half(tmp2);
                        gen_smul_dual(tmp, tmp2);
                        /* This addition cannot overflow.  */
                        if (insn & (1 << 6)) {
                            tcg_gen_sub_i32(tmp, tmp, tmp2);
                        } else {
                            tcg_gen_add_i32(tmp, tmp, tmp2);
                        }
                        dead_tmp(tmp2);
                        if (insn & (1 << 22)) {
                            /* smlald, smlsld */
                            tmp64 = tcg_temp_new_i64();
                            tcg_gen_ext_i32_i64(tmp64, tmp);
                            dead_tmp(tmp);
                            gen_addq(s, tmp64, rd, rn);
                            gen_storeq_reg(s, rd, rn, tmp64);
                        } else {
                            /* smuad, smusd, smlad, smlsd */
                            if (rd != 15)
                              {
                                tmp2 = load_reg(s, rd);
                                gen_helper_add_setq(tmp, tmp, tmp2);
                                dead_tmp(tmp2);
                              }
                            store_reg(s, rn, tmp);
                        }
                    }
                    break;
                case 3:
                    op1 = ((insn >> 17) & 0x38) | ((insn >> 5) & 7);
                    switch (op1) {
                    case 0: /* Unsigned sum of absolute differences.  */
                        ARCH(6);
                        tmp = load_reg(s, rm);
                        tmp2 = load_reg(s, rs);
                        gen_helper_usad8(tmp, tmp, tmp2);
                        dead_tmp(tmp2);
                        if (rd != 15) {
                            tmp2 = load_reg(s, rd);
                            tcg_gen_add_i32(tmp, tmp, tmp2);
                            dead_tmp(tmp2);
                        }
                        store_reg(s, rn, tmp);
                        break;
                    case 0x20: case 0x24: case 0x28: case 0x2c:
                        /* Bitfield insert/clear.  */
                        ARCH(6T2);
                        shift = (insn >> 7) & 0x1f;
                        i = (insn >> 16) & 0x1f;
                        i = i + 1 - shift;
                        if (rm == 15) {
                            tmp = new_tmp();
                            tcg_gen_movi_i32(tmp, 0);
                        } else {
                            tmp = load_reg(s, rm);
                        }
                        if (i != 32) {
                            tmp2 = load_reg(s, rd);
                            gen_bfi(tmp, tmp2, tmp, shift, (1u << i) - 1);
                            dead_tmp(tmp2);
                        }
                        store_reg(s, rd, tmp);
                        break;
                    case 0x12: case 0x16: case 0x1a: case 0x1e: /* sbfx */
                    case 0x32: case 0x36: case 0x3a: case 0x3e: /* ubfx */
                        ARCH(6T2);
                        tmp = load_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        i = ((insn >> 16) & 0x1f) + 1;
                        if (shift + i > 32)
                            goto illegal_op;
                        if (i < 32) {
                            if (op1 & 0x20) {
                                gen_ubfx(tmp, shift, (1u << i) - 1);
                            } else {
                                gen_sbfx(tmp, shift, i);
                            }
                        }
                        store_reg(s, rd, tmp);
                        break;
                    default:
                        goto illegal_op;
                    }
                    break;
                }
                break;
            }
        do_ldst:
            /* Check for undefined extension instructions
             * per the ARM Bible IE:
             * xxxx 0111 1111 xxxx  xxxx xxxx 1111 xxxx
             */
            sh = (0xf << 20) | (0xf << 4);
            if (op1 == 0x7 && ((insn & sh) == sh))
            {
                goto illegal_op;
            }
            /* load/store byte/word */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            tmp2 = load_reg(s, rn);
            i = (IS_USER(s) || (insn & 0x01200000) == 0x00200000);
            if (insn & (1 << 24))
                gen_add_data_offset(s, insn, tmp2);
            if (insn & (1 << 20)) {
                /* load */
                if (insn & (1 << 22)) {
                    tmp = gen_ld8u(tmp2, i);
                } else {
                    tmp = gen_ld32(tmp2, i);
                }
            } else {
                /* store */
                tmp = load_reg(s, rd);
                if (insn & (1 << 22))
                    gen_st8(tmp, tmp2, i);
                else
                    gen_st32(tmp, tmp2, i);
            }
            if (!(insn & (1 << 24))) {
                gen_add_data_offset(s, insn, tmp2);
                store_reg(s, rn, tmp2);
            } else if (insn & (1 << 21)) {
                store_reg(s, rn, tmp2);
            } else {
                dead_tmp(tmp2);
            }
            if (insn & (1 << 20)) {
                /* Complete the load.  */
                if (rd == 15)
                    gen_bx(s, tmp);
                else
                    store_reg(s, rd, tmp);
            }
            break;
        case 0x08:
        case 0x09:
            {
                int j, n, user, loaded_base;
                TCGv loaded_var;
                /* load/store multiple words */
                /* XXX: store correct base if write back */
                user = 0;
                if (insn & (1 << 22)) {
                    if (IS_USER(s))
                        goto illegal_op; /* only usable in supervisor mode */

                    if ((insn & (1 << 15)) == 0)
                        user = 1;
                }
                rn = (insn >> 16) & 0xf;
                addr = load_reg(s, rn);

                /* compute total size */
                loaded_base = 0;
                TCGV_UNUSED(loaded_var);
                n = 0;
                for(i=0;i<16;i++) {
                    if (insn & (1 << i))
                        n++;
                }
                /* XXX: test invalid n == 0 case ? */
                if (insn & (1 << 23)) {
                    if (insn & (1 << 24)) {
                        /* pre increment */
                        tcg_gen_addi_i32(addr, addr, 4);
                    } else {
                        /* post increment */
                    }
                } else {
                    if (insn & (1 << 24)) {
                        /* pre decrement */
                        tcg_gen_addi_i32(addr, addr, -(n * 4));
                    } else {
                        /* post decrement */
                        if (n != 1)
                        tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                    }
                }
                j = 0;
                for(i=0;i<16;i++) {
                    if (insn & (1 << i)) {
                        if (insn & (1 << 20)) {
                            /* load */
                            tmp = gen_ld32(addr, IS_USER(s));
                            if (i == 15) {
                                gen_bx(s, tmp);
                            } else if (user) {
                                gen_helper_set_user_reg(tcg_const_i32(i), tmp);
                                dead_tmp(tmp);
                            } else if (i == rn) {
                                loaded_var = tmp;
                                loaded_base = 1;
                            } else {
                                store_reg(s, i, tmp);
                            }
                        } else {
                            /* store */
                            if (i == 15) {
                                /* special case: r15 = PC + 8 */
                                val = (long)s->pc + 4;
                                tmp = new_tmp();
                                tcg_gen_movi_i32(tmp, val);
                            } else if (user) {
                                tmp = new_tmp();
                                gen_helper_get_user_reg(tmp, tcg_const_i32(i));
                            } else {
                                tmp = load_reg(s, i);
                            }
                            gen_st32(tmp, addr, IS_USER(s));
                        }
                        j++;
                        /* no need to add after the last transfer */
                        if (j != n)
                            tcg_gen_addi_i32(addr, addr, 4);
                    }
                }
                if (insn & (1 << 21)) {
                    /* write back */
                    if (insn & (1 << 23)) {
                        if (insn & (1 << 24)) {
                            /* pre increment */
                        } else {
                            /* post increment */
                            tcg_gen_addi_i32(addr, addr, 4);
                        }
                    } else {
                        if (insn & (1 << 24)) {
                            /* pre decrement */
                            if (n != 1)
                                tcg_gen_addi_i32(addr, addr, -((n - 1) * 4));
                        } else {
                            /* post decrement */
                            tcg_gen_addi_i32(addr, addr, -(n * 4));
                        }
                    }
                    store_reg(s, rn, addr);
                } else {
                    dead_tmp(addr);
                }
                if (loaded_base) {
                    store_reg(s, rn, loaded_var);
                }
                if ((insn & (1 << 22)) && !user) {
                    /* Restore CPSR from SPSR.  */
                    tmp = load_cpu_field(spsr);
                    gen_set_cpsr(tmp, 0xffffffff);
                    dead_tmp(tmp);
                    s->is_jmp = DISAS_UPDATE;
                }
            }
            break;
        case 0xa:
        case 0xb:
            {
                int32_t offset;

                /* branch (and link) */
                val = (int32_t)s->pc;
                if (insn & (1 << 24)) {
                    tmp = new_tmp();
                    tcg_gen_movi_i32(tmp, val);
                    store_reg(s, 14, tmp);
                }
                offset = (((int32_t)insn << 8) >> 8);
                val += (offset << 2) + 4;
                gen_jmp(s, val);
            }
            break;
        case 0xc:
        case 0xd:
        case 0xe:
            /* Coprocessor.  */
            if (disas_coproc_insn(env, s, insn))
                goto illegal_op;
            break;
        case 0xf:
            /* swi */
            gen_set_pc_im(s->pc);
            s->is_jmp = DISAS_SWI;
            break;
        default:
        illegal_op:
            gen_set_condexec(s);
            gen_set_pc_im(s->pc - 4);
            gen_exception(EXCP_UDEF);
            s->is_jmp = DISAS_JUMP;
            break;
        }
    }
}

/* Return true if this is a Thumb-2 logical op.  */
static int
thumb2_logic_op(int op)
{
    return (op < 8);
}

/* Generate code for a Thumb-2 data processing operation.  If CONDS is nonzero
   then set condition code flags based on the result of the operation.
   If SHIFTER_OUT is nonzero then set the carry flag for logical operations
   to the high bit of T1.
   Returns zero if the opcode is valid.  */

static int
gen_thumb2_data_op(DisasContext *s, int op, int conds, uint32_t shifter_out)
{
    int logic_cc;

    logic_cc = 0;
    switch (op) {
    case 0: /* and */
        gen_op_andl_T0_T1();
        logic_cc = conds;
        break;
    case 1: /* bic */
        gen_op_bicl_T0_T1();
        logic_cc = conds;
        break;
    case 2: /* orr */
        gen_op_orl_T0_T1();
        logic_cc = conds;
        break;
    case 3: /* orn */
        gen_op_notl_T1();
        gen_op_orl_T0_T1();
        logic_cc = conds;
        break;
    case 4: /* eor */
        gen_op_xorl_T0_T1();
        logic_cc = conds;
        break;
    case 8: /* add */
        if (conds)
            gen_op_addl_T0_T1_cc();
        else
            gen_op_addl_T0_T1();
        break;
    case 10: /* adc */
        if (conds)
            gen_op_adcl_T0_T1_cc();
        else
            gen_adc_T0_T1();
        break;
    case 11: /* sbc */
        if (conds)
            gen_op_sbcl_T0_T1_cc();
        else
            gen_sbc_T0_T1();
        break;
    case 13: /* sub */
        if (conds)
            gen_op_subl_T0_T1_cc();
        else
            gen_op_subl_T0_T1();
        break;
    case 14: /* rsb */
        if (conds)
            gen_op_rsbl_T0_T1_cc();
        else
            gen_op_rsbl_T0_T1();
        break;
    default: /* 5, 6, 7, 9, 12, 15. */
        return 1;
    }
    if (logic_cc) {
        gen_op_logic_T0_cc();
        if (shifter_out)
            gen_set_CF_bit31(cpu_T[1]);
    }
    return 0;
}

/* Translate a 32-bit thumb instruction.  Returns nonzero if the instruction
   is not legal.  */
static int disas_thumb2_insn(CPUState *env, DisasContext *s, uint16_t insn_hw1)
{
    uint32_t insn, imm, shift, offset;
    uint32_t rd, rn, rm, rs;
    TCGv tmp;
    TCGv tmp2;
    TCGv tmp3;
    TCGv addr;
    TCGv_i64 tmp64;
    int op;
    int shiftop;
    int conds;
    int logic_cc;

    if (!(arm_feature(env, ARM_FEATURE_THUMB2)
          || arm_feature (env, ARM_FEATURE_M))) {
        /* Thumb-1 cores may need to treat bl and blx as a pair of
           16-bit instructions to get correct prefetch abort behavior.  */
        insn = insn_hw1;
        if ((insn & (1 << 12)) == 0) {
            /* Second half of blx.  */
            offset = ((insn & 0x7ff) << 1);
            tmp = load_reg(s, 14);
            tcg_gen_addi_i32(tmp, tmp, offset);
            tcg_gen_andi_i32(tmp, tmp, 0xfffffffc);

            tmp2 = new_tmp();
            tcg_gen_movi_i32(tmp2, s->pc | 1);
            store_reg(s, 14, tmp2);
            gen_bx(s, tmp);
            return 0;
        }
        if (insn & (1 << 11)) {
            /* Second half of bl.  */
            offset = ((insn & 0x7ff) << 1) | 1;
            tmp = load_reg(s, 14);
            tcg_gen_addi_i32(tmp, tmp, offset);

            tmp2 = new_tmp();
            tcg_gen_movi_i32(tmp2, s->pc | 1);
            store_reg(s, 14, tmp2);
            gen_bx(s, tmp);
            return 0;
        }
        if ((s->pc & ~TARGET_PAGE_MASK) == 0) {
            /* Instruction spans a page boundary.  Implement it as two
               16-bit instructions in case the second half causes an
               prefetch abort.  */
            offset = ((int32_t)insn << 21) >> 9;
            gen_op_movl_T0_im(s->pc + 2 + offset);
            gen_movl_reg_T0(s, 14);
            return 0;
        }
        /* Fall through to 32-bit decode.  */
    }

    insn = lduw_code(s->pc);
    s->pc += 2;
    insn |= (uint32_t)insn_hw1 << 16;

    if ((insn & 0xf800e800) != 0xf000e800) {
        ARCH(6T2);
    }

    rn = (insn >> 16) & 0xf;
    rs = (insn >> 12) & 0xf;
    rd = (insn >> 8) & 0xf;
    rm = insn & 0xf;
    switch ((insn >> 25) & 0xf) {
    case 0: case 1: case 2: case 3:
        /* 16-bit instructions.  Should never happen.  */
        abort();
    case 4:
        if (insn & (1 << 22)) {
            /* Other load/store, table branch.  */
            if (insn & 0x01200000) {
                /* Load/store doubleword.  */
                if (rn == 15) {
                    addr = new_tmp();
                    tcg_gen_movi_i32(addr, s->pc & ~3);
                } else {
                    addr = load_reg(s, rn);
                }
                offset = (insn & 0xff) * 4;
                if ((insn & (1 << 23)) == 0)
                    offset = -offset;
                if (insn & (1 << 24)) {
                    tcg_gen_addi_i32(addr, addr, offset);
                    offset = 0;
                }
                if (insn & (1 << 20)) {
                    /* ldrd */
                    tmp = gen_ld32(addr, IS_USER(s));
                    store_reg(s, rs, tmp);
                    tcg_gen_addi_i32(addr, addr, 4);
                    tmp = gen_ld32(addr, IS_USER(s));
                    store_reg(s, rd, tmp);
                } else {
                    /* strd */
                    tmp = load_reg(s, rs);
                    gen_st32(tmp, addr, IS_USER(s));
                    tcg_gen_addi_i32(addr, addr, 4);
                    tmp = load_reg(s, rd);
                    gen_st32(tmp, addr, IS_USER(s));
                }
                if (insn & (1 << 21)) {
                    /* Base writeback.  */
                    if (rn == 15)
                        goto illegal_op;
                    tcg_gen_addi_i32(addr, addr, offset - 4);
                    store_reg(s, rn, addr);
                } else {
                    dead_tmp(addr);
                }
            } else if ((insn & (1 << 23)) == 0) {
                /* Load/store exclusive word.  */
                gen_movl_T1_reg(s, rn);
                addr = cpu_T[1];
                if (insn & (1 << 20)) {
                    gen_helper_mark_exclusive(cpu_env, cpu_T[1]);
                    tmp = gen_ld32(addr, IS_USER(s));
                    store_reg(s, rd, tmp);
                } else {
                    int label = gen_new_label();
                    gen_helper_test_exclusive(cpu_T[0], cpu_env, addr);
                    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_T[0],
                                        0, label);
                    tmp = load_reg(s, rs);
                    gen_st32(tmp, cpu_T[1], IS_USER(s));
                    gen_set_label(label);
                    gen_movl_reg_T0(s, rd);
                }
            } else if ((insn & (1 << 6)) == 0) {
                /* Table Branch.  */
                if (rn == 15) {
                    addr = new_tmp();
                    tcg_gen_movi_i32(addr, s->pc);
                } else {
                    addr = load_reg(s, rn);
                }
                tmp = load_reg(s, rm);
                tcg_gen_add_i32(addr, addr, tmp);
                if (insn & (1 << 4)) {
                    /* tbh */
                    tcg_gen_add_i32(addr, addr, tmp);
                    dead_tmp(tmp);
                    tmp = gen_ld16u(addr, IS_USER(s));
                } else { /* tbb */
                    dead_tmp(tmp);
                    tmp = gen_ld8u(addr, IS_USER(s));
                }
                dead_tmp(addr);
                tcg_gen_shli_i32(tmp, tmp, 1);
                tcg_gen_addi_i32(tmp, tmp, s->pc);
                store_reg(s, 15, tmp);
            } else {
                /* Load/store exclusive byte/halfword/doubleword.  */
                /* ??? These are not really atomic.  However we know
                   we never have multiple CPUs running in parallel,
                   so it is good enough.  */
                op = (insn >> 4) & 0x3;
                /* Must use a global reg for the address because we have
                   a conditional branch in the store instruction.  */
                gen_movl_T1_reg(s, rn);
                addr = cpu_T[1];
                if (insn & (1 << 20)) {
                    gen_helper_mark_exclusive(cpu_env, addr);
                    switch (op) {
                    case 0:
                        tmp = gen_ld8u(addr, IS_USER(s));
                        break;
                    case 1:
                        tmp = gen_ld16u(addr, IS_USER(s));
                        break;
                    case 3:
                        tmp = gen_ld32(addr, IS_USER(s));
                        tcg_gen_addi_i32(addr, addr, 4);
                        tmp2 = gen_ld32(addr, IS_USER(s));
                        store_reg(s, rd, tmp2);
                        break;
                    default:
                        goto illegal_op;
                    }
                    store_reg(s, rs, tmp);
                } else {
                    int label = gen_new_label();
                    /* Must use a global that is not killed by the branch.  */
                    gen_helper_test_exclusive(cpu_T[0], cpu_env, addr);
                    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_T[0], 0, label);
                    tmp = load_reg(s, rs);
                    switch (op) {
                    case 0:
                        gen_st8(tmp, addr, IS_USER(s));
                        break;
                    case 1:
                        gen_st16(tmp, addr, IS_USER(s));
                        break;
                    case 3:
                        gen_st32(tmp, addr, IS_USER(s));
                        tcg_gen_addi_i32(addr, addr, 4);
                        tmp = load_reg(s, rd);
                        gen_st32(tmp, addr, IS_USER(s));
                        break;
                    default:
                        goto illegal_op;
                    }
                    gen_set_label(label);
                    gen_movl_reg_T0(s, rm);
                }
            }
        } else {
            /* Load/store multiple, RFE, SRS.  */
            if (((insn >> 23) & 1) == ((insn >> 24) & 1)) {
                /* Not available in user mode.  */
                if (IS_USER(s))
                    goto illegal_op;
                if (insn & (1 << 20)) {
                    /* rfe */
                    addr = load_reg(s, rn);
                    if ((insn & (1 << 24)) == 0)
                        tcg_gen_addi_i32(addr, addr, -8);
                    /* Load PC into tmp and CPSR into tmp2.  */
                    tmp = gen_ld32(addr, 0);
                    tcg_gen_addi_i32(addr, addr, 4);
                    tmp2 = gen_ld32(addr, 0);
                    if (insn & (1 << 21)) {
                        /* Base writeback.  */
                        if (insn & (1 << 24)) {
                            tcg_gen_addi_i32(addr, addr, 4);
                        } else {
                            tcg_gen_addi_i32(addr, addr, -4);
                        }
                        store_reg(s, rn, addr);
                    } else {
                        dead_tmp(addr);
                    }
                    gen_rfe(s, tmp, tmp2);
                } else {
                    /* srs */
                    op = (insn & 0x1f);
                    if (op == (env->uncached_cpsr & CPSR_M)) {
                        addr = load_reg(s, 13);
                    } else {
                        addr = new_tmp();
                        gen_helper_get_r13_banked(addr, cpu_env, tcg_const_i32(op));
                    }
                    if ((insn & (1 << 24)) == 0) {
                        tcg_gen_addi_i32(addr, addr, -8);
                    }
                    tmp = load_reg(s, 14);
                    gen_st32(tmp, addr, 0);
                    tcg_gen_addi_i32(addr, addr, 4);
                    tmp = new_tmp();
                    gen_helper_cpsr_read(tmp);
                    gen_st32(tmp, addr, 0);
                    if (insn & (1 << 21)) {
                        if ((insn & (1 << 24)) == 0) {
                            tcg_gen_addi_i32(addr, addr, -4);
                        } else {
                            tcg_gen_addi_i32(addr, addr, 4);
                        }
                        if (op == (env->uncached_cpsr & CPSR_M)) {
                            store_reg(s, 13, addr);
                        } else {
                            gen_helper_set_r13_banked(cpu_env,
                                tcg_const_i32(op), addr);
                        }
                    } else {
                        dead_tmp(addr);
                    }
                }
            } else {
                int i;
                /* Load/store multiple.  */
                addr = load_reg(s, rn);
                offset = 0;
                for (i = 0; i < 16; i++) {
                    if (insn & (1 << i))
                        offset += 4;
                }
                if (insn & (1 << 24)) {
                    tcg_gen_addi_i32(addr, addr, -offset);
                }

                for (i = 0; i < 16; i++) {
                    if ((insn & (1 << i)) == 0)
                        continue;
                    if (insn & (1 << 20)) {
                        /* Load.  */
                        tmp = gen_ld32(addr, IS_USER(s));
                        if (i == 15) {
                            gen_bx(s, tmp);
                        } else {
                            store_reg(s, i, tmp);
                        }
                    } else {
                        /* Store.  */
                        tmp = load_reg(s, i);
                        gen_st32(tmp, addr, IS_USER(s));
                    }
                    tcg_gen_addi_i32(addr, addr, 4);
                }
                if (insn & (1 << 21)) {
                    /* Base register writeback.  */
                    if (insn & (1 << 24)) {
                        tcg_gen_addi_i32(addr, addr, -offset);
                    }
                    /* Fault if writeback register is in register list.  */
                    if (insn & (1 << rn))
                        goto illegal_op;
                    store_reg(s, rn, addr);
                } else {
                    dead_tmp(addr);
                }
            }
        }
        break;
    case 5: /* Data processing register constant shift.  */
        if (rn == 15)
            gen_op_movl_T0_im(0);
        else
            gen_movl_T0_reg(s, rn);
        gen_movl_T1_reg(s, rm);
        op = (insn >> 21) & 0xf;
        shiftop = (insn >> 4) & 3;
        shift = ((insn >> 6) & 3) | ((insn >> 10) & 0x1c);
        conds = (insn & (1 << 20)) != 0;
        logic_cc = (conds && thumb2_logic_op(op));
        gen_arm_shift_im(cpu_T[1], shiftop, shift, logic_cc);
        if (gen_thumb2_data_op(s, op, conds, 0))
            goto illegal_op;
        if (rd != 15)
            gen_movl_reg_T0(s, rd);
        break;
    case 13: /* Misc data processing.  */
        op = ((insn >> 22) & 6) | ((insn >> 7) & 1);
        if (op < 4 && (insn & 0xf000) != 0xf000)
            goto illegal_op;
        switch (op) {
        case 0: /* Register controlled shift.  */
            tmp = load_reg(s, rn);
            tmp2 = load_reg(s, rm);
            if ((insn & 0x70) != 0)
                goto illegal_op;
            op = (insn >> 21) & 3;
            logic_cc = (insn & (1 << 20)) != 0;
            gen_arm_shift_reg(tmp, op, tmp2, logic_cc);
            if (logic_cc)
                gen_logic_CC(tmp);
            store_reg(s, rd, tmp);
            break;
        case 1: /* Sign/zero extend.  */
            tmp = load_reg(s, rm);
            shift = (insn >> 4) & 3;
            /* ??? In many cases it's not neccessary to do a
               rotate, a shift is sufficient.  */
            if (shift != 0)
                tcg_gen_rori_i32(tmp, tmp, shift * 8);
            op = (insn >> 20) & 7;
            switch (op) {
            case 0: gen_sxth(tmp);   break;
            case 1: gen_uxth(tmp);   break;
            case 2: gen_sxtb16(tmp); break;
            case 3: gen_uxtb16(tmp); break;
            case 4: gen_sxtb(tmp);   break;
            case 5: gen_uxtb(tmp);   break;
            default: goto illegal_op;
            }
            if (rn != 15) {
                tmp2 = load_reg(s, rn);
                if ((op >> 1) == 1) {
                    gen_add16(tmp, tmp2);
                } else {
                    tcg_gen_add_i32(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                }
            }
            store_reg(s, rd, tmp);
            break;
        case 2: /* SIMD add/subtract.  */
            op = (insn >> 20) & 7;
            shift = (insn >> 4) & 7;
            if ((op & 3) == 3 || (shift & 3) == 3)
                goto illegal_op;
            tmp = load_reg(s, rn);
            tmp2 = load_reg(s, rm);
            gen_thumb2_parallel_addsub(op, shift, tmp, tmp2);
            dead_tmp(tmp2);
            store_reg(s, rd, tmp);
            break;
        case 3: /* Other data processing.  */
            op = ((insn >> 17) & 0x38) | ((insn >> 4) & 7);
            if (op < 4) {
                /* Saturating add/subtract.  */
                tmp = load_reg(s, rn);
                tmp2 = load_reg(s, rm);
                if (op & 2)
                    gen_helper_double_saturate(tmp, tmp);
                if (op & 1)
                    gen_helper_sub_saturate(tmp, tmp2, tmp);
                else
                    gen_helper_add_saturate(tmp, tmp, tmp2);
                dead_tmp(tmp2);
            } else {
                tmp = load_reg(s, rn);
                switch (op) {
                case 0x0a: /* rbit */
                    gen_helper_rbit(tmp, tmp);
                    break;
                case 0x08: /* rev */
                    tcg_gen_bswap_i32(tmp, tmp);
                    break;
                case 0x09: /* rev16 */
                    gen_rev16(tmp);
                    break;
                case 0x0b: /* revsh */
                    gen_revsh(tmp);
                    break;
                case 0x10: /* sel */
                    tmp2 = load_reg(s, rm);
                    tmp3 = new_tmp();
                    tcg_gen_ld_i32(tmp3, cpu_env, offsetof(CPUState, GE));
                    gen_helper_sel_flags(tmp, tmp3, tmp, tmp2);
                    dead_tmp(tmp3);
                    dead_tmp(tmp2);
                    break;
                case 0x18: /* clz */
                    gen_helper_clz(tmp, tmp);
                    break;
                default:
                    goto illegal_op;
                }
            }
            store_reg(s, rd, tmp);
            break;
        case 4: case 5: /* 32-bit multiply.  Sum of absolute differences.  */
            op = (insn >> 4) & 0xf;
            tmp = load_reg(s, rn);
            tmp2 = load_reg(s, rm);
            switch ((insn >> 20) & 7) {
            case 0: /* 32 x 32 -> 32 */
                tcg_gen_mul_i32(tmp, tmp, tmp2);
                dead_tmp(tmp2);
                if (rs != 15) {
                    tmp2 = load_reg(s, rs);
                    if (op)
                        tcg_gen_sub_i32(tmp, tmp2, tmp);
                    else
                        tcg_gen_add_i32(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                }
                break;
            case 1: /* 16 x 16 -> 32 */
                gen_mulxy(tmp, tmp2, op & 2, op & 1);
                dead_tmp(tmp2);
                if (rs != 15) {
                    tmp2 = load_reg(s, rs);
                    gen_helper_add_setq(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                }
                break;
            case 2: /* Dual multiply add.  */
            case 4: /* Dual multiply subtract.  */
                if (op)
                    gen_swap_half(tmp2);
                gen_smul_dual(tmp, tmp2);
                /* This addition cannot overflow.  */
                if (insn & (1 << 22)) {
                    tcg_gen_sub_i32(tmp, tmp, tmp2);
                } else {
                    tcg_gen_add_i32(tmp, tmp, tmp2);
                }
                dead_tmp(tmp2);
                if (rs != 15)
                  {
                    tmp2 = load_reg(s, rs);
                    gen_helper_add_setq(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                  }
                break;
            case 3: /* 32 * 16 -> 32msb */
                if (op)
                    tcg_gen_sari_i32(tmp2, tmp2, 16);
                else
                    gen_sxth(tmp2);
                tmp64 = gen_muls_i64_i32(tmp, tmp2);
                tcg_gen_shri_i64(tmp64, tmp64, 16);
                tmp = new_tmp();
                tcg_gen_trunc_i64_i32(tmp, tmp64);
                if (rs != 15)
                  {
                    tmp2 = load_reg(s, rs);
                    gen_helper_add_setq(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                  }
                break;
            case 5: case 6: /* 32 * 32 -> 32msb */
                gen_imull(tmp, tmp2);
                if (insn & (1 << 5)) {
                    gen_roundqd(tmp, tmp2);
                    dead_tmp(tmp2);
                } else {
                    dead_tmp(tmp);
                    tmp = tmp2;
                }
                if (rs != 15) {
                    tmp2 = load_reg(s, rs);
                    if (insn & (1 << 21)) {
                        tcg_gen_add_i32(tmp, tmp, tmp2);
                    } else {
                        tcg_gen_sub_i32(tmp, tmp2, tmp);
                    }
                    dead_tmp(tmp2);
                }
                break;
            case 7: /* Unsigned sum of absolute differences.  */
                gen_helper_usad8(tmp, tmp, tmp2);
                dead_tmp(tmp2);
                if (rs != 15) {
                    tmp2 = load_reg(s, rs);
                    tcg_gen_add_i32(tmp, tmp, tmp2);
                    dead_tmp(tmp2);
                }
                break;
            }
            store_reg(s, rd, tmp);
            break;
        case 6: case 7: /* 64-bit multiply, Divide.  */
            op = ((insn >> 4) & 0xf) | ((insn >> 16) & 0x70);
            tmp = load_reg(s, rn);
            tmp2 = load_reg(s, rm);
            if ((op & 0x50) == 0x10) {
                /* sdiv, udiv */
                if (!arm_feature(env, ARM_FEATURE_DIV))
                    goto illegal_op;
                if (op & 0x20)
                    gen_helper_udiv(tmp, tmp, tmp2);
                else
                    gen_helper_sdiv(tmp, tmp, tmp2);
                dead_tmp(tmp2);
                store_reg(s, rd, tmp);
            } else if ((op & 0xe) == 0xc) {
                /* Dual multiply accumulate long.  */
                if (op & 1)
                    gen_swap_half(tmp2);
                gen_smul_dual(tmp, tmp2);
                if (op & 0x10) {
                    tcg_gen_sub_i32(tmp, tmp, tmp2);
                } else {
                    tcg_gen_add_i32(tmp, tmp, tmp2);
                }
                dead_tmp(tmp2);
                /* BUGFIX */
                tmp64 = tcg_temp_new_i64();
                tcg_gen_ext_i32_i64(tmp64, tmp);
                dead_tmp(tmp);
                gen_addq(s, tmp64, rs, rd);
                gen_storeq_reg(s, rs, rd, tmp64);
            } else {
                if (op & 0x20) {
                    /* Unsigned 64-bit multiply  */
                    tmp64 = gen_mulu_i64_i32(tmp, tmp2);
                } else {
                    if (op & 8) {
                        /* smlalxy */
                        gen_mulxy(tmp, tmp2, op & 2, op & 1);
                        dead_tmp(tmp2);
                        tmp64 = tcg_temp_new_i64();
                        tcg_gen_ext_i32_i64(tmp64, tmp);
                        dead_tmp(tmp);
                    } else {
                        /* Signed 64-bit multiply  */
                        tmp64 = gen_muls_i64_i32(tmp, tmp2);
                    }
                }
                if (op & 4) {
                    /* umaal */
                    gen_addq_lo(s, tmp64, rs);
                    gen_addq_lo(s, tmp64, rd);
                } else if (op & 0x40) {
                    /* 64-bit accumulate.  */
                    gen_addq(s, tmp64, rs, rd);
                }
                gen_storeq_reg(s, rs, rd, tmp64);
            }
            break;
        }
        break;
    case 6: case 7: case 14: case 15:
        /* Coprocessor.  */
        if (((insn >> 24) & 3) == 3) {
            /* Translate into the equivalent ARM encoding.  */
            insn = (insn & 0xe2ffffff) | ((insn & (1 << 28)) >> 4);
            if (disas_neon_data_insn(env, s, insn))
                goto illegal_op;
        } else {
            if (insn & (1 << 28))
                goto illegal_op;
            if (disas_coproc_insn (env, s, insn))
                goto illegal_op;
        }
        break;
    case 8: case 9: case 10: case 11:
        if (insn & (1 << 15)) {
            /* Branches, misc control.  */
            if (insn & 0x5000) {
                /* Unconditional branch.  */
                /* signextend(hw1[10:0]) -> offset[:12].  */
                offset = ((int32_t)insn << 5) >> 9 & ~(int32_t)0xfff;
                /* hw1[10:0] -> offset[11:1].  */
                offset |= (insn & 0x7ff) << 1;
                /* (~hw2[13, 11] ^ offset[24]) -> offset[23,22]
                   offset[24:22] already have the same value because of the
                   sign extension above.  */
                offset ^= ((~insn) & (1 << 13)) << 10;
                offset ^= ((~insn) & (1 << 11)) << 11;

                if (insn & (1 << 14)) {
                    /* Branch and link.  */
                    gen_op_movl_T1_im(s->pc | 1);
                    gen_movl_reg_T1(s, 14);
                }

                offset += s->pc;
                if (insn & (1 << 12)) {
                    /* b/bl */
                    gen_jmp(s, offset);
                } else {
                    /* blx */
                    offset &= ~(uint32_t)2;
                    gen_bx_im(s, offset);
                }
            } else if (((insn >> 23) & 7) == 7) {
                /* Misc control */
                if (insn & (1 << 13))
                    goto illegal_op;

                if (insn & (1 << 26)) {
                    /* Secure monitor call (v6Z) */
                    goto illegal_op; /* not implemented.  */
                } else {
                    op = (insn >> 20) & 7;
                    switch (op) {
                    case 0: /* msr cpsr.  */
                        if (IS_M(env)) {
                            tmp = load_reg(s, rn);
                            addr = tcg_const_i32(insn & 0xff);
                            gen_helper_v7m_msr(cpu_env, addr, tmp);
                            gen_lookup_tb(s);
                            break;
                        }
                        /* fall through */
                    case 1: /* msr spsr.  */
                        if (IS_M(env))
                            goto illegal_op;
                        gen_movl_T0_reg(s, rn);
                        if (gen_set_psr_T0(s,
                              msr_mask(env, s, (insn >> 8) & 0xf, op == 1),
                              op == 1))
                            goto illegal_op;
                        break;
                    case 2: /* cps, nop-hint.  */
                        if (((insn >> 8) & 7) == 0) {
                            gen_nop_hint(s, insn & 0xff);
                        }
                        /* Implemented as NOP in user mode.  */
                        if (IS_USER(s))
                            break;
                        offset = 0;
                        imm = 0;
                        if (insn & (1 << 10)) {
                            if (insn & (1 << 7))
                                offset |= CPSR_A;
                            if (insn & (1 << 6))
                                offset |= CPSR_I;
                            if (insn & (1 << 5))
                                offset |= CPSR_F;
                            if (insn & (1 << 9))
                                imm = CPSR_A | CPSR_I | CPSR_F;
                        }
                        if (insn & (1 << 8)) {
                            offset |= 0x1f;
                            imm |= (insn & 0x1f);
                        }
                        if (offset) {
                            gen_op_movl_T0_im(imm);
                            gen_set_psr_T0(s, offset, 0);
                        }
                        break;
                    case 3: /* Special control operations.  */
                        op = (insn >> 4) & 0xf;
                        switch (op) {
                        case 2: /* clrex */
                            gen_helper_clrex(cpu_env);
                            break;
                        case 4: /* dsb */
                        case 5: /* dmb */
                        case 6: /* isb */
                            /* These execute as NOPs.  */
                            ARCH(7);
                            break;
                        default:
                            goto illegal_op;
                        }
                        break;
                    case 4: /* bxj */
                        /* Trivial implementation equivalent to bx.  */
                        tmp = load_reg(s, rn);
                        gen_bx(s, tmp);
                        break;
                    case 5: /* Exception return.  */
                        /* Unpredictable in user mode.  */
                        goto illegal_op;
                    case 6: /* mrs cpsr.  */
                        tmp = new_tmp();
                        if (IS_M(env)) {
                            addr = tcg_const_i32(insn & 0xff);
                            gen_helper_v7m_mrs(tmp, cpu_env, addr);
                        } else {
                            gen_helper_cpsr_read(tmp);
                        }
                        store_reg(s, rd, tmp);
                        break;
                    case 7: /* mrs spsr.  */
                        /* Not accessible in user mode.  */
                        if (IS_USER(s) || IS_M(env))
                            goto illegal_op;
                        tmp = load_cpu_field(spsr);
                        store_reg(s, rd, tmp);
                        break;
                    }
                }
            } else {
                /* Conditional branch.  */
                op = (insn >> 22) & 0xf;
                /* Generate a conditional jump to next instruction.  */
                s->condlabel = gen_new_label();
                gen_test_cc(op ^ 1, s->condlabel);
                s->condjmp = 1;

                /* offset[11:1] = insn[10:0] */
                offset = (insn & 0x7ff) << 1;
                /* offset[17:12] = insn[21:16].  */
                offset |= (insn & 0x003f0000) >> 4;
                /* offset[31:20] = insn[26].  */
                offset |= ((int32_t)((insn << 5) & 0x80000000)) >> 11;
                /* offset[18] = insn[13].  */
                offset |= (insn & (1 << 13)) << 5;
                /* offset[19] = insn[11].  */
                offset |= (insn & (1 << 11)) << 8;

                /* jump to the offset */
                gen_jmp(s, s->pc + offset);
            }
        } else {
            /* Data processing immediate.  */
            if (insn & (1 << 25)) {
                if (insn & (1 << 24)) {
                    if (insn & (1 << 20))
                        goto illegal_op;
                    /* Bitfield/Saturate.  */
                    op = (insn >> 21) & 7;
                    imm = insn & 0x1f;
                    shift = ((insn >> 6) & 3) | ((insn >> 10) & 0x1c);
                    if (rn == 15) {
                        tmp = new_tmp();
                        tcg_gen_movi_i32(tmp, 0);
                    } else {
                        tmp = load_reg(s, rn);
                    }
                    switch (op) {
                    case 2: /* Signed bitfield extract.  */
                        imm++;
                        if (shift + imm > 32)
                            goto illegal_op;
                        if (imm < 32)
                            gen_sbfx(tmp, shift, imm);
                        break;
                    case 6: /* Unsigned bitfield extract.  */
                        imm++;
                        if (shift + imm > 32)
                            goto illegal_op;
                        if (imm < 32)
                            gen_ubfx(tmp, shift, (1u << imm) - 1);
                        break;
                    case 3: /* Bitfield insert/clear.  */
                        if (imm < shift)
                            goto illegal_op;
                        imm = imm + 1 - shift;
                        if (imm != 32) {
                            tmp2 = load_reg(s, rd);
                            gen_bfi(tmp, tmp2, tmp, shift, (1u << imm) - 1);
                            dead_tmp(tmp2);
                        }
                        break;
                    case 7:
                        goto illegal_op;
                    default: /* Saturate.  */
                        if (shift) {
                            if (op & 1)
                                tcg_gen_sari_i32(tmp, tmp, shift);
                            else
                                tcg_gen_shli_i32(tmp, tmp, shift);
                        }
                        tmp2 = tcg_const_i32(imm);
                        if (op & 4) {
                            /* Unsigned.  */
                            if ((op & 1) && shift == 0)
                                gen_helper_usat16(tmp, tmp, tmp2);
                            else
                                gen_helper_usat(tmp, tmp, tmp2);
                        } else {
                            /* Signed.  */
                            if ((op & 1) && shift == 0)
                                gen_helper_ssat16(tmp, tmp, tmp2);
                            else
                                gen_helper_ssat(tmp, tmp, tmp2);
                        }
                        break;
                    }
                    store_reg(s, rd, tmp);
                } else {
                    imm = ((insn & 0x04000000) >> 15)
                          | ((insn & 0x7000) >> 4) | (insn & 0xff);
                    if (insn & (1 << 22)) {
                        /* 16-bit immediate.  */
                        imm |= (insn >> 4) & 0xf000;
                        if (insn & (1 << 23)) {
                            /* movt */
                            tmp = load_reg(s, rd);
                            tcg_gen_ext16u_i32(tmp, tmp);
                            tcg_gen_ori_i32(tmp, tmp, imm << 16);
                        } else {
                            /* movw */
                            tmp = new_tmp();
                            tcg_gen_movi_i32(tmp, imm);
                        }
                    } else {
                        /* Add/sub 12-bit immediate.  */
                        if (rn == 15) {
                            offset = s->pc & ~(uint32_t)3;
                            if (insn & (1 << 23))
                                offset -= imm;
                            else
                                offset += imm;
                            tmp = new_tmp();
                            tcg_gen_movi_i32(tmp, offset);
                        } else {
                            tmp = load_reg(s, rn);
                            if (insn & (1 << 23))
                                tcg_gen_subi_i32(tmp, tmp, imm);
                            else
                                tcg_gen_addi_i32(tmp, tmp, imm);
                        }
                    }
                    store_reg(s, rd, tmp);
                }
            } else {
                int shifter_out = 0;
                /* modified 12-bit immediate.  */
                shift = ((insn & 0x04000000) >> 23) | ((insn & 0x7000) >> 12);
                imm = (insn & 0xff);
                switch (shift) {
                case 0: /* XY */
                    /* Nothing to do.  */
                    break;
                case 1: /* 00XY00XY */
                    imm |= imm << 16;
                    break;
                case 2: /* XY00XY00 */
                    imm |= imm << 16;
                    imm <<= 8;
                    break;
                case 3: /* XYXYXYXY */
                    imm |= imm << 16;
                    imm |= imm << 8;
                    break;
                default: /* Rotated constant.  */
                    shift = (shift << 1) | (imm >> 7);
                    imm |= 0x80;
                    imm = imm << (32 - shift);
                    shifter_out = 1;
                    break;
                }
                gen_op_movl_T1_im(imm);
                rn = (insn >> 16) & 0xf;
                if (rn == 15)
                    gen_op_movl_T0_im(0);
                else
                    gen_movl_T0_reg(s, rn);
                op = (insn >> 21) & 0xf;
                if (gen_thumb2_data_op(s, op, (insn & (1 << 20)) != 0,
                                       shifter_out))
                    goto illegal_op;
                rd = (insn >> 8) & 0xf;
                if (rd != 15) {
                    gen_movl_reg_T0(s, rd);
                }
            }
        }
        break;
    case 12: /* Load/store single data item.  */
        {
        int postinc = 0;
        int writeback = 0;
        int user;
        if ((insn & 0x01100000) == 0x01000000) {
            if (disas_neon_ls_insn(env, s, insn))
                goto illegal_op;
            break;
        }
        user = IS_USER(s);
        if (rn == 15) {
            addr = new_tmp();
            /* PC relative.  */
            /* s->pc has already been incremented by 4.  */
            imm = s->pc & 0xfffffffc;
            if (insn & (1 << 23))
                imm += insn & 0xfff;
            else
                imm -= insn & 0xfff;
            tcg_gen_movi_i32(addr, imm);
        } else {
            addr = load_reg(s, rn);
            if (insn & (1 << 23)) {
                /* Positive offset.  */
                imm = insn & 0xfff;
                tcg_gen_addi_i32(addr, addr, imm);
            } else {
                op = (insn >> 8) & 7;
                imm = insn & 0xff;
                switch (op) {
                case 0: case 8: /* Shifted Register.  */
                    shift = (insn >> 4) & 0xf;
                    if (shift > 3)
                        goto illegal_op;
                    tmp = load_reg(s, rm);
                    if (shift)
                        tcg_gen_shli_i32(tmp, tmp, shift);
                    tcg_gen_add_i32(addr, addr, tmp);
                    dead_tmp(tmp);
                    break;
                case 4: /* Negative offset.  */
                    tcg_gen_addi_i32(addr, addr, -imm);
                    break;
                case 6: /* User privilege.  */
                    tcg_gen_addi_i32(addr, addr, imm);
                    user = 1;
                    break;
                case 1: /* Post-decrement.  */
                    imm = -imm;
                    /* Fall through.  */
                case 3: /* Post-increment.  */
                    postinc = 1;
                    writeback = 1;
                    break;
                case 5: /* Pre-decrement.  */
                    imm = -imm;
                    /* Fall through.  */
                case 7: /* Pre-increment.  */
                    tcg_gen_addi_i32(addr, addr, imm);
                    writeback = 1;
                    break;
                default:
                    goto illegal_op;
                }
            }
        }
        op = ((insn >> 21) & 3) | ((insn >> 22) & 4);
        if (insn & (1 << 20)) {
            /* Load.  */
            if (rs == 15 && op != 2) {
                if (op & 2)
                    goto illegal_op;
                /* Memory hint.  Implemented as NOP.  */
            } else {
                switch (op) {
                case 0: tmp = gen_ld8u(addr, user); break;
                case 4: tmp = gen_ld8s(addr, user); break;
                case 1: tmp = gen_ld16u(addr, user); break;
                case 5: tmp = gen_ld16s(addr, user); break;
                case 2: tmp = gen_ld32(addr, user); break;
                default: goto illegal_op;
                }
                if (rs == 15) {
                    gen_bx(s, tmp);
                } else {
                    store_reg(s, rs, tmp);
                }
            }
        } else {
            /* Store.  */
            if (rs == 15)
                goto illegal_op;
            tmp = load_reg(s, rs);
            switch (op) {
            case 0: gen_st8(tmp, addr, user); break;
            case 1: gen_st16(tmp, addr, user); break;
            case 2: gen_st32(tmp, addr, user); break;
            default: goto illegal_op;
            }
        }
        if (postinc)
            tcg_gen_addi_i32(addr, addr, imm);
        if (writeback) {
            store_reg(s, rn, addr);
        } else {
            dead_tmp(addr);
        }
        }
        break;
    default:
        goto illegal_op;
    }
    return 0;
illegal_op:
    return 1;
}

static void disas_thumb_insn(CPUState *env, DisasContext *s)
{
    uint32_t val, insn, op, rm, rn, rd, shift, cond;
    int32_t offset;
    int i;
    TCGv tmp;
    TCGv tmp2;
    TCGv addr;

    if (s->condexec_mask) {
        cond = s->condexec_cond;
        s->condlabel = gen_new_label();
        gen_test_cc(cond ^ 1, s->condlabel);
        s->condjmp = 1;
    }

    insn = lduw_code(s->pc);
    s->pc += 2;

    switch (insn >> 12) {
    case 0: case 1:
        rd = insn & 7;
        op = (insn >> 11) & 3;
        if (op == 3) {
            /* add/subtract */
            rn = (insn >> 3) & 7;
            gen_movl_T0_reg(s, rn);
            if (insn & (1 << 10)) {
                /* immediate */
                gen_op_movl_T1_im((insn >> 6) & 7);
            } else {
                /* reg */
                rm = (insn >> 6) & 7;
                gen_movl_T1_reg(s, rm);
            }
            if (insn & (1 << 9)) {
                if (s->condexec_mask)
                    gen_op_subl_T0_T1();
                else
                    gen_op_subl_T0_T1_cc();
            } else {
                if (s->condexec_mask)
                    gen_op_addl_T0_T1();
                else
                    gen_op_addl_T0_T1_cc();
            }
            gen_movl_reg_T0(s, rd);
        } else {
            /* shift immediate */
            rm = (insn >> 3) & 7;
            shift = (insn >> 6) & 0x1f;
            tmp = load_reg(s, rm);
            gen_arm_shift_im(tmp, op, shift, s->condexec_mask == 0);
            if (!s->condexec_mask)
                gen_logic_CC(tmp);
            store_reg(s, rd, tmp);
        }
        break;
    case 2: case 3:
        /* arithmetic large immediate */
        op = (insn >> 11) & 3;
        rd = (insn >> 8) & 0x7;
        if (op == 0) {
            gen_op_movl_T0_im(insn & 0xff);
        } else {
            gen_movl_T0_reg(s, rd);
            gen_op_movl_T1_im(insn & 0xff);
        }
        switch (op) {
        case 0: /* mov */
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 1: /* cmp */
            gen_op_subl_T0_T1_cc();
            break;
        case 2: /* add */
            if (s->condexec_mask)
                gen_op_addl_T0_T1();
            else
                gen_op_addl_T0_T1_cc();
            break;
        case 3: /* sub */
            if (s->condexec_mask)
                gen_op_subl_T0_T1();
            else
                gen_op_subl_T0_T1_cc();
            break;
        }
        if (op != 1)
            gen_movl_reg_T0(s, rd);
        break;
    case 4:
        if (insn & (1 << 11)) {
            rd = (insn >> 8) & 7;
            /* load pc-relative.  Bit 1 of PC is ignored.  */
            val = s->pc + 2 + ((insn & 0xff) * 4);
            val &= ~(uint32_t)2;
            addr = new_tmp();
            tcg_gen_movi_i32(addr, val);
            tmp = gen_ld32(addr, IS_USER(s));
            dead_tmp(addr);
            store_reg(s, rd, tmp);
            break;
        }
        if (insn & (1 << 10)) {
            /* data processing extended or blx */
            rd = (insn & 7) | ((insn >> 4) & 8);
            rm = (insn >> 3) & 0xf;
            op = (insn >> 8) & 3;
            switch (op) {
            case 0: /* add */
                gen_movl_T0_reg(s, rd);
                gen_movl_T1_reg(s, rm);
                gen_op_addl_T0_T1();
                gen_movl_reg_T0(s, rd);
                break;
            case 1: /* cmp */
                gen_movl_T0_reg(s, rd);
                gen_movl_T1_reg(s, rm);
                gen_op_subl_T0_T1_cc();
                break;
            case 2: /* mov/cpy */
                gen_movl_T0_reg(s, rm);
                gen_movl_reg_T0(s, rd);
                break;
            case 3:/* branch [and link] exchange thumb register */
                tmp = load_reg(s, rm);
                if (insn & (1 << 7)) {
                    val = (uint32_t)s->pc | 1;
                    tmp2 = new_tmp();
                    tcg_gen_movi_i32(tmp2, val);
                    store_reg(s, 14, tmp2);
                }
                gen_bx(s, tmp);
                break;
            }
            break;
        }

        /* data processing register */
        rd = insn & 7;
        rm = (insn >> 3) & 7;
        op = (insn >> 6) & 0xf;
        if (op == 2 || op == 3 || op == 4 || op == 7) {
            /* the shift/rotate ops want the operands backwards */
            val = rm;
            rm = rd;
            rd = val;
            val = 1;
        } else {
            val = 0;
        }

        if (op == 9) /* neg */
            gen_op_movl_T0_im(0);
        else if (op != 0xf) /* mvn doesn't read its first operand */
            gen_movl_T0_reg(s, rd);

        gen_movl_T1_reg(s, rm);
        switch (op) {
        case 0x0: /* and */
            gen_op_andl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0x1: /* eor */
            gen_op_xorl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0x2: /* lsl */
            if (s->condexec_mask) {
                gen_helper_shl(cpu_T[1], cpu_T[1], cpu_T[0]);
            } else {
                gen_helper_shl_cc(cpu_T[1], cpu_T[1], cpu_T[0]);
                gen_op_logic_T1_cc();
            }
            break;
        case 0x3: /* lsr */
            if (s->condexec_mask) {
                gen_helper_shr(cpu_T[1], cpu_T[1], cpu_T[0]);
            } else {
                gen_helper_shr_cc(cpu_T[1], cpu_T[1], cpu_T[0]);
                gen_op_logic_T1_cc();
            }
            break;
        case 0x4: /* asr */
            if (s->condexec_mask) {
                gen_helper_sar(cpu_T[1], cpu_T[1], cpu_T[0]);
            } else {
                gen_helper_sar_cc(cpu_T[1], cpu_T[1], cpu_T[0]);
                gen_op_logic_T1_cc();
            }
            break;
        case 0x5: /* adc */
            if (s->condexec_mask)
                gen_adc_T0_T1();
            else
                gen_op_adcl_T0_T1_cc();
            break;
        case 0x6: /* sbc */
            if (s->condexec_mask)
                gen_sbc_T0_T1();
            else
                gen_op_sbcl_T0_T1_cc();
            break;
        case 0x7: /* ror */
            if (s->condexec_mask) {
                gen_helper_ror(cpu_T[1], cpu_T[1], cpu_T[0]);
            } else {
                gen_helper_ror_cc(cpu_T[1], cpu_T[1], cpu_T[0]);
                gen_op_logic_T1_cc();
            }
            break;
        case 0x8: /* tst */
            gen_op_andl_T0_T1();
            gen_op_logic_T0_cc();
            rd = 16;
            break;
        case 0x9: /* neg */
            if (s->condexec_mask)
                tcg_gen_neg_i32(cpu_T[0], cpu_T[1]);
            else
                gen_op_subl_T0_T1_cc();
            break;
        case 0xa: /* cmp */
            gen_op_subl_T0_T1_cc();
            rd = 16;
            break;
        case 0xb: /* cmn */
            gen_op_addl_T0_T1_cc();
            rd = 16;
            break;
        case 0xc: /* orr */
            gen_op_orl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xd: /* mul */
            gen_op_mull_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xe: /* bic */
            gen_op_bicl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xf: /* mvn */
            gen_op_notl_T1();
            if (!s->condexec_mask)
                gen_op_logic_T1_cc();
            val = 1;
            rm = rd;
            break;
        }
        if (rd != 16) {
            if (val)
                gen_movl_reg_T1(s, rm);
            else
                gen_movl_reg_T0(s, rd);
        }
        break;

    case 5:
        /* load/store register offset.  */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        rm = (insn >> 6) & 7;
        op = (insn >> 9) & 7;
        addr = load_reg(s, rn);
        tmp = load_reg(s, rm);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);

        if (op < 3) /* store */
            tmp = load_reg(s, rd);

        switch (op) {
        case 0: /* str */
            gen_st32(tmp, addr, IS_USER(s));
            break;
        case 1: /* strh */
            gen_st16(tmp, addr, IS_USER(s));
            break;
        case 2: /* strb */
            gen_st8(tmp, addr, IS_USER(s));
            break;
        case 3: /* ldrsb */
            tmp = gen_ld8s(addr, IS_USER(s));
            break;
        case 4: /* ldr */
            tmp = gen_ld32(addr, IS_USER(s));
            break;
        case 5: /* ldrh */
            tmp = gen_ld16u(addr, IS_USER(s));
            break;
        case 6: /* ldrb */
            tmp = gen_ld8u(addr, IS_USER(s));
            break;
        case 7: /* ldrsh */
            tmp = gen_ld16s(addr, IS_USER(s));
            break;
        }
        if (op >= 3) /* load */
            store_reg(s, rd, tmp);
        dead_tmp(addr);
        break;

    case 6:
        /* load/store word immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        addr = load_reg(s, rn);
        val = (insn >> 4) & 0x7c;
        tcg_gen_addi_i32(addr, addr, val);

        if (insn & (1 << 11)) {
            /* load */
            tmp = gen_ld32(addr, IS_USER(s));
            store_reg(s, rd, tmp);
        } else {
            /* store */
            tmp = load_reg(s, rd);
            gen_st32(tmp, addr, IS_USER(s));
        }
        dead_tmp(addr);
        break;

    case 7:
        /* load/store byte immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        addr = load_reg(s, rn);
        val = (insn >> 6) & 0x1f;
        tcg_gen_addi_i32(addr, addr, val);

        if (insn & (1 << 11)) {
            /* load */
            tmp = gen_ld8u(addr, IS_USER(s));
            store_reg(s, rd, tmp);
        } else {
            /* store */
            tmp = load_reg(s, rd);
            gen_st8(tmp, addr, IS_USER(s));
        }
        dead_tmp(addr);
        break;

    case 8:
        /* load/store halfword immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        addr = load_reg(s, rn);
        val = (insn >> 5) & 0x3e;
        tcg_gen_addi_i32(addr, addr, val);

        if (insn & (1 << 11)) {
            /* load */
            tmp = gen_ld16u(addr, IS_USER(s));
            store_reg(s, rd, tmp);
        } else {
            /* store */
            tmp = load_reg(s, rd);
            gen_st16(tmp, addr, IS_USER(s));
        }
        dead_tmp(addr);
        break;

    case 9:
        /* load/store from stack */
        rd = (insn >> 8) & 7;
        addr = load_reg(s, 13);
        val = (insn & 0xff) * 4;
        tcg_gen_addi_i32(addr, addr, val);

        if (insn & (1 << 11)) {
            /* load */
            tmp = gen_ld32(addr, IS_USER(s));
            store_reg(s, rd, tmp);
        } else {
            /* store */
            tmp = load_reg(s, rd);
            gen_st32(tmp, addr, IS_USER(s));
        }
        dead_tmp(addr);
        break;

    case 10:
        /* add to high reg */
        rd = (insn >> 8) & 7;
        if (insn & (1 << 11)) {
            /* SP */
            tmp = load_reg(s, 13);
        } else {
            /* PC. bit 1 is ignored.  */
            tmp = new_tmp();
            tcg_gen_movi_i32(tmp, (s->pc + 2) & ~(uint32_t)2);
        }
        val = (insn & 0xff) * 4;
        tcg_gen_addi_i32(tmp, tmp, val);
        store_reg(s, rd, tmp);
        break;

    case 11:
        /* misc */
        op = (insn >> 8) & 0xf;
        switch (op) {
        case 0:
            /* adjust stack pointer */
            tmp = load_reg(s, 13);
            val = (insn & 0x7f) * 4;
            if (insn & (1 << 7))
                val = -(int32_t)val;
            tcg_gen_addi_i32(tmp, tmp, val);
            store_reg(s, 13, tmp);
            break;

        case 2: /* sign/zero extend.  */
            ARCH(6);
            rd = insn & 7;
            rm = (insn >> 3) & 7;
            tmp = load_reg(s, rm);
            switch ((insn >> 6) & 3) {
            case 0: gen_sxth(tmp); break;
            case 1: gen_sxtb(tmp); break;
            case 2: gen_uxth(tmp); break;
            case 3: gen_uxtb(tmp); break;
            }
            store_reg(s, rd, tmp);
            break;
        case 4: case 5: case 0xc: case 0xd:
            /* push/pop */
            addr = load_reg(s, 13);
            if (insn & (1 << 8))
                offset = 4;
            else
                offset = 0;
            for (i = 0; i < 8; i++) {
                if (insn & (1 << i))
                    offset += 4;
            }
            if ((insn & (1 << 11)) == 0) {
                tcg_gen_addi_i32(addr, addr, -offset);
            }
            for (i = 0; i < 8; i++) {
                if (insn & (1 << i)) {
                    if (insn & (1 << 11)) {
                        /* pop */
                        tmp = gen_ld32(addr, IS_USER(s));
                        store_reg(s, i, tmp);
                    } else {
                        /* push */
                        tmp = load_reg(s, i);
                        gen_st32(tmp, addr, IS_USER(s));
                    }
                    /* advance to the next address.  */
                    tcg_gen_addi_i32(addr, addr, 4);
                }
            }
            TCGV_UNUSED(tmp);
            if (insn & (1 << 8)) {
                if (insn & (1 << 11)) {
                    /* pop pc */
                    tmp = gen_ld32(addr, IS_USER(s));
                    /* don't set the pc until the rest of the instruction
                       has completed */
                } else {
                    /* push lr */
                    tmp = load_reg(s, 14);
                    gen_st32(tmp, addr, IS_USER(s));
                }
                tcg_gen_addi_i32(addr, addr, 4);
            }
            if ((insn & (1 << 11)) == 0) {
                tcg_gen_addi_i32(addr, addr, -offset);
            }
            /* write back the new stack pointer */
            store_reg(s, 13, addr);
            /* set the new PC value */
            if ((insn & 0x0900) == 0x0900)
                gen_bx(s, tmp);
            break;

        case 1: case 3: case 9: case 11: /* czb */
            rm = insn & 7;
            tmp = load_reg(s, rm);
            s->condlabel = gen_new_label();
            s->condjmp = 1;
            if (insn & (1 << 11))
                tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, s->condlabel);
            else
                tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, s->condlabel);
            dead_tmp(tmp);
            offset = ((insn & 0xf8) >> 2) | (insn & 0x200) >> 3;
            val = (uint32_t)s->pc + 2;
            val += offset;
            gen_jmp(s, val);
            break;

        case 15: /* IT, nop-hint.  */
            if ((insn & 0xf) == 0) {
                gen_nop_hint(s, (insn >> 4) & 0xf);
                break;
            }
            /* If Then.  */
            s->condexec_cond = (insn >> 4) & 0xe;
            s->condexec_mask = insn & 0x1f;
            /* No actual code generated for this insn, just setup state.  */
            break;

        case 0xe: /* bkpt */
            gen_set_condexec(s);
            gen_set_pc_im(s->pc - 2);
            gen_exception(EXCP_BKPT);
            s->is_jmp = DISAS_JUMP;
            break;

        case 0xa: /* rev */
            ARCH(6);
            rn = (insn >> 3) & 0x7;
            rd = insn & 0x7;
            tmp = load_reg(s, rn);
            switch ((insn >> 6) & 3) {
            case 0: tcg_gen_bswap_i32(tmp, tmp); break;
            case 1: gen_rev16(tmp); break;
            case 3: gen_revsh(tmp); break;
            default: goto illegal_op;
            }
            store_reg(s, rd, tmp);
            break;

        case 6: /* cps */
            ARCH(6);
            if (IS_USER(s))
                break;
            if (IS_M(env)) {
                tmp = tcg_const_i32((insn & (1 << 4)) != 0);
                /* PRIMASK */
                if (insn & 1) {
                    addr = tcg_const_i32(16);
                    gen_helper_v7m_msr(cpu_env, addr, tmp);
                }
                /* FAULTMASK */
                if (insn & 2) {
                    addr = tcg_const_i32(17);
                    gen_helper_v7m_msr(cpu_env, addr, tmp);
                }
                gen_lookup_tb(s);
            } else {
                if (insn & (1 << 4))
                    shift = CPSR_A | CPSR_I | CPSR_F;
                else
                    shift = 0;

                val = ((insn & 7) << 6) & shift;
                gen_op_movl_T0_im(val);
                gen_set_psr_T0(s, shift, 0);
            }
            break;

        default:
            goto undef;
        }
        break;

    case 12:
        /* load/store multiple */
        rn = (insn >> 8) & 0x7;
        addr = load_reg(s, rn);
        for (i = 0; i < 8; i++) {
            if (insn & (1 << i)) {
                if (insn & (1 << 11)) {
                    /* load */
                    tmp = gen_ld32(addr, IS_USER(s));
                    store_reg(s, i, tmp);
                } else {
                    /* store */
                    tmp = load_reg(s, i);
                    gen_st32(tmp, addr, IS_USER(s));
                }
                /* advance to the next address */
                tcg_gen_addi_i32(addr, addr, 4);
            }
        }
        /* Base register writeback.  */
        if ((insn & (1 << rn)) == 0) {
            store_reg(s, rn, addr);
        } else {
            dead_tmp(addr);
        }
        break;

    case 13:
        /* conditional branch or swi */
        cond = (insn >> 8) & 0xf;
        if (cond == 0xe)
            goto undef;

        if (cond == 0xf) {
            /* swi */
            gen_set_condexec(s);
            gen_set_pc_im(s->pc);
            s->is_jmp = DISAS_SWI;
            break;
        }
        /* generate a conditional jump to next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc(cond ^ 1, s->condlabel);
        s->condjmp = 1;
        gen_movl_T1_reg(s, 15);

        /* jump to the offset */
        val = (uint32_t)s->pc + 2;
        offset = ((int32_t)insn << 24) >> 24;
        val += offset << 1;
        gen_jmp(s, val);
        break;

    case 14:
        if (insn & (1 << 11)) {
            if (disas_thumb2_insn(env, s, insn))
              goto undef32;
            break;
        }
        /* unconditional branch */
        val = (uint32_t)s->pc;
        offset = ((int32_t)insn << 21) >> 21;
        val += (offset << 1) + 2;
        gen_jmp(s, val);
        break;

    case 15:
        if (disas_thumb2_insn(env, s, insn))
            goto undef32;
        break;
    }
    return;
undef32:
    gen_set_condexec(s);
    gen_set_pc_im(s->pc - 4);
    gen_exception(EXCP_UDEF);
    s->is_jmp = DISAS_JUMP;
    return;
illegal_op:
undef:
    gen_set_condexec(s);
    gen_set_pc_im(s->pc - 2);
    gen_exception(EXCP_UDEF);
    s->is_jmp = DISAS_JUMP;
}

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
static inline void gen_intermediate_code_internal(CPUState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    CPUBreakpoint *bp;
    uint16_t *gen_opc_end;
    int j, lj;
    target_ulong pc_start;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    /* generate intermediate code */
    num_temps = 0;
    memset(temps, 0, sizeof(temps));

    pc_start = tb->pc;

    dc->tb = tb;

    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->condjmp = 0;
    dc->thumb = env->thumb;
    dc->condexec_mask = (env->condexec_bits & 0xf) << 1;
    dc->condexec_cond = env->condexec_bits >> 4;
#if !defined(CONFIG_USER_ONLY)
    if (IS_M(env)) {
        dc->user = ((env->v7m.exception == 0) && (env->v7m.control & 1));
    } else {
        dc->user = (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_USR;
    }
#endif
    cpu_F0s = tcg_temp_new_i32();
    cpu_F1s = tcg_temp_new_i32();
    cpu_F0d = tcg_temp_new_i64();
    cpu_F1d = tcg_temp_new_i64();
    cpu_V0 = cpu_F0d;
    cpu_V1 = cpu_F1d;
    /* FIXME: cpu_M0 can probably be the same as cpu_V0.  */
    cpu_M0 = tcg_temp_new_i64();
    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_icount_start();
    /* Reset the conditional execution bits immediately. This avoids
       complications trying to do it at the end of the block.  */
    if (env->condexec_bits)
      {
        TCGv tmp = new_tmp();
        tcg_gen_movi_i32(tmp, 0);
        store_cpu_field(tmp, condexec_bits);
      }
    do {
#ifdef CONFIG_USER_ONLY
        /* Intercept jump to the magic kernel page.  */
        if (dc->pc >= 0xffff0000) {
            /* We always get here via a jump, so know we are not in a
               conditional execution block.  */
            gen_exception(EXCP_KERNEL_TRAP);
            dc->is_jmp = DISAS_UPDATE;
            break;
        }
#else
        if (dc->pc >= 0xfffffff0 && IS_M(env)) {
            /* We always get here via a jump, so know we are not in a
               conditional execution block.  */
            gen_exception(EXCP_EXCEPTION_EXIT);
            dc->is_jmp = DISAS_UPDATE;
            break;
        }
#endif

        if (unlikely(!TAILQ_EMPTY(&env->breakpoints))) {
            TAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == dc->pc) {
                    gen_set_condexec(dc);
                    gen_set_pc_im(dc->pc);
                    gen_exception(EXCP_DEBUG);
                    dc->is_jmp = DISAS_JUMP;
                    /* Advance PC so that clearing the breakpoint will
                       invalidate this TB.  */
                    dc->pc += 2;
                    goto done_generating;
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = dc->pc;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();

        if (env->thumb) {
            disas_thumb_insn(env, dc);
            if (dc->condexec_mask) {
                dc->condexec_cond = (dc->condexec_cond & 0xe)
                                   | ((dc->condexec_mask >> 4) & 1);
                dc->condexec_mask = (dc->condexec_mask << 1) & 0x1f;
                if (dc->condexec_mask == 0) {
                    dc->condexec_cond = 0;
                }
            }
        } else {
            disas_arm_insn(env, dc);
        }
        if (num_temps) {
            fprintf(stderr, "Internal resource leak before %08x\n", dc->pc);
            num_temps = 0;
        }

        if (dc->condjmp && !dc->is_jmp) {
            gen_set_label(dc->condlabel);
            dc->condjmp = 0;
        }
        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.  */
        num_insns ++;
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end &&
             !env->singlestep_enabled &&
             dc->pc < next_page_start &&
             num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        if (dc->condjmp) {
            /* FIXME:  This can theoretically happen with self-modifying
               code.  */
            cpu_abort(env, "IO on conditional branch instruction");
        }
        gen_io_end();
    }

    /* At this stage dc->condjmp will only be set when the skipped
       instruction was a conditional branch or trap, and the PC has
       already been written.  */
    if (unlikely(env->singlestep_enabled)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (dc->condjmp) {
            gen_set_condexec(dc);
            if (dc->is_jmp == DISAS_SWI) {
                gen_exception(EXCP_SWI);
            } else {
                gen_exception(EXCP_DEBUG);
            }
            gen_set_label(dc->condlabel);
        }
        if (dc->condjmp || !dc->is_jmp) {
            gen_set_pc_im(dc->pc);
            dc->condjmp = 0;
        }
        gen_set_condexec(dc);
        if (dc->is_jmp == DISAS_SWI && !dc->condjmp) {
            gen_exception(EXCP_SWI);
        } else {
            /* FIXME: Single stepping a WFI insn will not halt
               the CPU.  */
            gen_exception(EXCP_DEBUG);
        }
    } else {
        /* While branches must always occur at the end of an IT block,
           there are a few other things that can cause us to terminate
           the TB in the middel of an IT block:
            - Exception generating instructions (bkpt, swi, undefined).
            - Page boundaries.
            - Hardware watchpoints.
           Hardware breakpoints have already been handled and skip this code.
         */
        gen_set_condexec(dc);
        switch(dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        case DISAS_WFI:
            gen_helper_wfi();
            break;
        case DISAS_SWI:
            gen_exception(EXCP_SWI);
            break;
        }
        if (dc->condjmp) {
            gen_set_label(dc->condlabel);
            gen_set_condexec(dc);
            gen_goto_tb(dc, 1, dc->pc);
            dc->condjmp = 0;
        }
    }

done_generating:
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(pc_start, dc->pc - pc_start, env->thumb);
        qemu_log("\n");
    }
#endif
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }
}

void gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

static const char *cpu_mode_names[16] = {
  "usr", "fiq", "irq", "svc", "???", "???", "???", "abt",
  "???", "???", "???", "und", "???", "???", "???", "sys"
};

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i;
#if 0
    union {
        uint32_t i;
        float s;
    } s0, s1;
    CPU_DoubleU d;
    /* ??? This assumes float64 and double have the same layout.
       Oh well, it's only debug dumps.  */
    union {
        float64 f64;
        double d;
    } d0;
#endif
    uint32_t psr;

    for(i=0;i<16;i++) {
        cpu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3)
            cpu_fprintf(f, "\n");
        else
            cpu_fprintf(f, " ");
    }
    psr = cpsr_read(env);
    cpu_fprintf(f, "PSR=%08x %c%c%c%c %c %s%d\n",
                psr,
                psr & (1 << 31) ? 'N' : '-',
                psr & (1 << 30) ? 'Z' : '-',
                psr & (1 << 29) ? 'C' : '-',
                psr & (1 << 28) ? 'V' : '-',
                psr & CPSR_T ? 'T' : 'A',
                cpu_mode_names[psr & 0xf], (psr & 0x10) ? 32 : 26);

#if 0
    for (i = 0; i < 16; i++) {
        d.d = env->vfp.regs[i];
        s0.i = d.l.lower;
        s1.i = d.l.upper;
        d0.f64 = d.d;
        cpu_fprintf(f, "s%02d=%08x(%8g) s%02d=%08x(%8g) d%02d=%08x%08x(%8g)\n",
                    i * 2, (int)s0.i, s0.s,
                    i * 2 + 1, (int)s1.i, s1.s,
                    i, (int)(uint32_t)d.l.upper, (int)(uint32_t)d.l.lower,
                    d0.d);
    }
    cpu_fprintf(f, "FPSCR: %08x\n", (int)env->vfp.xregs[ARM_VFP_FPSCR]);
#endif
}

void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->regs[15] = gen_opc_pc[pc_pos];
}
