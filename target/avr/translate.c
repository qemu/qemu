/*
 * QEMU AVR CPU
 *
 * Copyright (c) 2019-2020 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "tcg/tcg.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "exec/gen-icount.h"

/*
 *  Define if you want a BREAK instruction translated to a breakpoint
 *  Active debugging connection is assumed
 *  This is for
 *  https://github.com/seharris/qemu-avr-tests/tree/master/instruction-tests
 *  tests
 */
#undef BREAKPOINT_ON_BREAK

static TCGv cpu_pc;

static TCGv cpu_Cf;
static TCGv cpu_Zf;
static TCGv cpu_Nf;
static TCGv cpu_Vf;
static TCGv cpu_Sf;
static TCGv cpu_Hf;
static TCGv cpu_Tf;
static TCGv cpu_If;

static TCGv cpu_rampD;
static TCGv cpu_rampX;
static TCGv cpu_rampY;
static TCGv cpu_rampZ;

static TCGv cpu_r[NUMBER_OF_CPU_REGISTERS];
static TCGv cpu_eind;
static TCGv cpu_sp;

static TCGv cpu_skip;

static const char reg_names[NUMBER_OF_CPU_REGISTERS][8] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};
#define REG(x) (cpu_r[x])

enum {
    DISAS_EXIT   = DISAS_TARGET_0,  /* We want return to the cpu main loop.  */
    DISAS_LOOKUP = DISAS_TARGET_1,  /* We have a variable condition exit.  */
    DISAS_CHAIN  = DISAS_TARGET_2,  /* We have a single condition exit.  */
};

typedef struct DisasContext DisasContext;

/* This is the state at translation time. */
struct DisasContext {
    TranslationBlock *tb;

    CPUAVRState *env;
    CPUState *cs;

    target_long npc;
    uint32_t opcode;

    /* Routine used to access memory */
    int memidx;
    int bstate;
    int singlestep;

    /*
     * some AVR instructions can make the following instruction to be skipped
     * Let's name those instructions
     *     A   - instruction that can skip the next one
     *     B   - instruction that can be skipped. this depends on execution of A
     * there are two scenarios
     * 1. A and B belong to the same translation block
     * 2. A is the last instruction in the translation block and B is the last
     *
     * following variables are used to simplify the skipping logic, they are
     * used in the following manner (sketch)
     *
     * TCGLabel *skip_label = NULL;
     * if (ctx.skip_cond != TCG_COND_NEVER) {
     *     skip_label = gen_new_label();
     *     tcg_gen_brcond_tl(skip_cond, skip_var0, skip_var1, skip_label);
     * }
     *
     * if (free_skip_var0) {
     *     tcg_temp_free(skip_var0);
     *     free_skip_var0 = false;
     * }
     *
     * translate(&ctx);
     *
     * if (skip_label) {
     *     gen_set_label(skip_label);
     * }
     */
    TCGv skip_var0;
    TCGv skip_var1;
    TCGCond skip_cond;
    bool free_skip_var0;
};

static int to_regs_16_31_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 16);
}

static int to_regs_16_23_by_one(DisasContext *ctx, int indx)
{
    return 16 + (indx % 8);
}

static int to_regs_24_30_by_two(DisasContext *ctx, int indx)
{
    return 24 + (indx % 4) * 2;
}


static bool avr_have_feature(DisasContext *ctx, int feature)
{
    if (!avr_feature(ctx->env, feature)) {
        gen_helper_unsupported(cpu_env);
        ctx->bstate = DISAS_NORETURN;
        return false;
    }
    return true;
}

static bool decode_insn(DisasContext *ctx, uint16_t insn);
#include "decode_insn.inc.c"

/*
 * Arithmetic Instructions
 */

/*
 * Utility functions for updating status registers:
 *
 *   - gen_add_CHf()
 *   - gen_add_Vf()
 *   - gen_sub_CHf()
 *   - gen_sub_Vf()
 *   - gen_NSf()
 *   - gen_ZNSf()
 *
 */

static void gen_add_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_and_tl(t1, Rd, Rr); /* t1 = Rd & Rr */
    tcg_gen_andc_tl(t2, Rd, R); /* t2 = Rd & ~R */
    tcg_gen_andc_tl(t3, Rr, R); /* t3 = Rr & ~R */
    tcg_gen_or_tl(t1, t1, t2); /* t1 = t1 | t2 | t3 */
    tcg_gen_or_tl(t1, t1, t3);

    tcg_gen_shri_tl(cpu_Cf, t1, 7); /* Cf = t1(7) */
    tcg_gen_shri_tl(cpu_Hf, t1, 3); /* Hf = t1(3) */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_add_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /* t1 = Rd & Rr & ~R | ~Rd & ~Rr & R */
    /*    = (Rd ^ R) & ~(Rd ^ Rr) */
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_andc_tl(t1, t1, t2);

    tcg_gen_shri_tl(cpu_Vf, t1, 7); /* Vf = t1(7) */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_sub_CHf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();
    TCGv t3 = tcg_temp_new_i32();

    tcg_gen_not_tl(t1, Rd); /* t1 = ~Rd */
    tcg_gen_and_tl(t2, t1, Rr); /* t2 = ~Rd & Rr */
    tcg_gen_or_tl(t3, t1, Rr); /* t3 = (~Rd | Rr) & R */
    tcg_gen_and_tl(t3, t3, R);
    tcg_gen_or_tl(t2, t2, t3); /* t2 = ~Rd & Rr | ~Rd & R | R & Rr */

    tcg_gen_shri_tl(cpu_Cf, t2, 7); /* Cf = t2(7) */
    tcg_gen_shri_tl(cpu_Hf, t2, 3); /* Hf = t2(3) */
    tcg_gen_andi_tl(cpu_Hf, cpu_Hf, 1);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_sub_Vf(TCGv R, TCGv Rd, TCGv Rr)
{
    TCGv t1 = tcg_temp_new_i32();
    TCGv t2 = tcg_temp_new_i32();

    /* t1 = Rd & ~Rr & ~R | ~Rd & Rr & R */
    /*    = (Rd ^ R) & (Rd ^ R) */
    tcg_gen_xor_tl(t1, Rd, R);
    tcg_gen_xor_tl(t2, Rd, Rr);
    tcg_gen_and_tl(t1, t1, t2);

    tcg_gen_shri_tl(cpu_Vf, t1, 7); /* Vf = t1(7) */

    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_NSf(TCGv R)
{
    tcg_gen_shri_tl(cpu_Nf, R, 7); /* Nf = R(7) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
}

static void gen_ZNSf(TCGv R)
{
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    /* update status register */
    tcg_gen_shri_tl(cpu_Nf, R, 7); /* Nf = R(7) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
}

/*
 *  Adds two registers without the C Flag and places the result in the
 *  destination register Rd.
 */
static bool trans_ADD(DisasContext *ctx, arg_ADD *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr); /* Rd = Rd + Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds two registers and the contents of the C Flag and places the result in
 *  the destination register Rd.
 */
static bool trans_ADC(DisasContext *ctx, arg_ADC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_add_tl(R, Rd, Rr); /* R = Rd + Rr + Cf */
    tcg_gen_add_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_add_CHf(R, Rd, Rr);
    gen_add_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds an immediate value (0 - 63) to a register pair and places the result
 *  in the register pair. This instruction operates on the upper four register
 *  pairs, and is well suited for operations on the pointer registers.  This
 *  instruction is not available in all devices. Refer to the device specific
 *  instruction set summary.
 */
static bool trans_ADIW(DisasContext *ctx, arg_ADIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8); /* Rd = RdH:RdL */
    tcg_gen_addi_tl(R, Rd, Imm); /* R = Rd + Imm */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */

    /* update status register */
    tcg_gen_andc_tl(cpu_Cf, Rd, R); /* Cf = Rd & ~R */
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15);
    tcg_gen_andc_tl(cpu_Vf, R, Rd); /* Vf = R & ~Rd */
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 15); /* Nf = R(15) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf);/* Sf = Nf ^ Vf */

    /* update output registers */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Subtracts two registers and places the result in the destination
 *  register Rd.
 */
static bool trans_SUB(DisasContext *ctx, arg_SUB *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    tcg_gen_andc_tl(cpu_Cf, Rd, R); /* Cf = Rd & ~R */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Subtracts a register and a constant and places the result in the
 *  destination register Rd. This instruction is working on Register R16 to R31
 *  and is very well suited for operations on the X, Y, and Z-pointers.
 */
static bool trans_SUBI(DisasContext *ctx, arg_SUBI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_const_i32(a->imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Imm */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  Subtracts two registers and subtracts with the C Flag and places the
 *  result in the destination register Rd.
 */
static bool trans_SBC(DisasContext *ctx, arg_SBC *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_const_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr - Cf */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*
     * Previous value remains unchanged when the result is zero;
     * cleared otherwise.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  SBCI -- Subtract Immediate with Carry
 */
static bool trans_SBCI(DisasContext *ctx, arg_SBCI *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = tcg_const_i32(a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv zero = tcg_const_i32(0);

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr - Cf */
    tcg_gen_sub_tl(R, R, cpu_Cf);
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_NSf(R);

    /*
     * Previous value remains unchanged when the result is zero;
     * cleared otherwise.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_Zf, R, zero, cpu_Zf, zero);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  Subtracts an immediate value (0-63) from a register pair and places the
 *  result in the register pair. This instruction operates on the upper four
 *  register pairs, and is well suited for operations on the Pointer Registers.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_SBIW(DisasContext *ctx, arg_SBIW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ADIW_SBIW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    int Imm = (a->imm);
    TCGv R = tcg_temp_new_i32();
    TCGv Rd = tcg_temp_new_i32();

    tcg_gen_deposit_tl(Rd, RdL, RdH, 8, 8); /* Rd = RdH:RdL */
    tcg_gen_subi_tl(R, Rd, Imm); /* R = Rd - Imm */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */

    /* update status register */
    tcg_gen_andc_tl(cpu_Cf, R, Rd);
    tcg_gen_shri_tl(cpu_Cf, cpu_Cf, 15); /* Cf = R & ~Rd */
    tcg_gen_andc_tl(cpu_Vf, Rd, R);
    tcg_gen_shri_tl(cpu_Vf, cpu_Vf, 15); /* Vf = Rd & ~R */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 15); /* Nf = R(15) */
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */

    /* update output registers */
    tcg_gen_andi_tl(RdL, R, 0xff);
    tcg_gen_shri_tl(RdH, R, 8);

    tcg_temp_free_i32(Rd);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical AND between the contents of register Rd and register
 *  Rr and places the result in the destination register Rd.
 */
static bool trans_AND(DisasContext *ctx, arg_AND *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_and_tl(R, Rd, Rr); /* Rd = Rd and Rr */

    /* update status register */
    tcg_gen_movi_tl(cpu_Vf, 0); /* Vf = 0 */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical AND between the contents of register Rd and a constant
 *  and places the result in the destination register Rd.
 */
static bool trans_ANDI(DisasContext *ctx, arg_ANDI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_andi_tl(Rd, Rd, Imm); /* Rd = Rd & Imm */

    /* update status register */
    tcg_gen_movi_tl(cpu_Vf, 0x00); /* Vf = 0 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Performs the logical OR between the contents of register Rd and register
 *  Rr and places the result in the destination register Rd.
 */
static bool trans_OR(DisasContext *ctx, arg_OR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_or_tl(R, Rd, Rr);

    /* update status register */
    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Performs the logical OR between the contents of register Rd and a
 *  constant and places the result in the destination register Rd.
 */
static bool trans_ORI(DisasContext *ctx, arg_ORI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = (a->imm);

    tcg_gen_ori_tl(Rd, Rd, Imm); /* Rd = Rd | Imm */

    /* update status register */
    tcg_gen_movi_tl(cpu_Vf, 0x00); /* Vf = 0 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Performs the logical EOR between the contents of register Rd and
 *  register Rr and places the result in the destination register Rd.
 */
static bool trans_EOR(DisasContext *ctx, arg_EOR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];

    tcg_gen_xor_tl(Rd, Rd, Rr);

    /* update status register */
    tcg_gen_movi_tl(cpu_Vf, 0);
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Clears the specified bits in register Rd. Performs the logical AND
 *  between the contents of register Rd and the complement of the constant mask
 *  K. The result will be placed in register Rd.
 */
static bool trans_COM(DisasContext *ctx, arg_COM *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_xori_tl(Rd, Rd, 0xff);

    /* update status register */
    tcg_gen_movi_tl(cpu_Cf, 1); /* Cf = 1 */
    tcg_gen_movi_tl(cpu_Vf, 0); /* Vf = 0 */
    gen_ZNSf(Rd);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Replaces the contents of register Rd with its two's complement; the
 *  value $80 is left unchanged.
 */
static bool trans_NEG(DisasContext *ctx, arg_NEG *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_const_i32(0);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, t0, Rd); /* R = 0 - Rd */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, t0, Rd);
    gen_sub_Vf(R, t0, Rd);
    gen_ZNSf(R);

    /* update output registers */
    tcg_gen_mov_tl(Rd, R);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  Adds one -1- to the contents of register Rd and places the result in the
 *  destination register Rd.  The C Flag in SREG is not affected by the
 *  operation, thus allowing the INC instruction to be used on a loop counter in
 *  multiple-precision computations.  When operating on unsigned numbers, only
 *  BREQ and BRNE branches can be expected to perform consistently. When
 *  operating on two's complement values, all signed branches are available.
 */
static bool trans_INC(DisasContext *ctx, arg_INC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_addi_tl(Rd, Rd, 1);
    tcg_gen_andi_tl(Rd, Rd, 0xff);

    /* update status register */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x80); /* Vf = Rd == 0x80 */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  Subtracts one -1- from the contents of register Rd and places the result
 *  in the destination register Rd.  The C Flag in SREG is not affected by the
 *  operation, thus allowing the DEC instruction to be used on a loop counter in
 *  multiple-precision computations.  When operating on unsigned values, only
 *  BREQ and BRNE branches can be expected to perform consistently.  When
 *  operating on two's complement values, all signed branches are available.
 */
static bool trans_DEC(DisasContext *ctx, arg_DEC *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_subi_tl(Rd, Rd, 1); /* Rd = Rd - 1 */
    tcg_gen_andi_tl(Rd, Rd, 0xff); /* make it 8 bits */

    /* update status register */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Vf, Rd, 0x7f); /* Vf = Rd == 0x7f */
    gen_ZNSf(Rd);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit unsigned multiplication.
 */
static bool trans_MUL(DisasContext *ctx, arg_MUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication.
 */
static bool trans_MULS(DisasContext *ctx, arg_MULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_ext8s_tl(t1, Rr); /* make Rr full 32 bit signed */
    tcg_gen_mul_tl(R, t0, t1); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit multiplication of a
 *  signed and an unsigned number.
 */
static bool trans_MULSU(DisasContext *ctx, arg_MULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_mul_tl(R, t0, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make R 16 bits */
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit unsigned
 *  multiplication and shifts the result one bit left.
 */
static bool trans_FMUL(DisasContext *ctx, arg_FMUL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_mul_tl(R, Rd, Rr); /* R = Rd * Rr */

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    /* update output registers */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);


    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
 *  and shifts the result one bit left.
 */
static bool trans_FMULS(DisasContext *ctx, arg_FMULS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_ext8s_tl(t1, Rr); /* make Rr full 32 bit signed */
    tcg_gen_mul_tl(R, t0, t1); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    /* update output registers */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs 8-bit x 8-bit -> 16-bit signed multiplication
 *  and shifts the result one bit left.
 */
static bool trans_FMULSU(DisasContext *ctx, arg_FMULSU *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MUL)) {
        return true;
    }

    TCGv R0 = cpu_r[0];
    TCGv R1 = cpu_r[1];
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_ext8s_tl(t0, Rd); /* make Rd full 32 bit signed */
    tcg_gen_mul_tl(R, t0, Rr); /* R = Rd * Rr */
    tcg_gen_andi_tl(R, R, 0xffff); /* make it 16 bits */

    /* update status register */
    tcg_gen_shri_tl(cpu_Cf, R, 15); /* Cf = R(15) */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */

    /* update output registers */
    tcg_gen_shli_tl(R, R, 1);
    tcg_gen_andi_tl(R0, R, 0xff);
    tcg_gen_shri_tl(R1, R, 8);
    tcg_gen_andi_tl(R1, R1, 0xff);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  The module is an instruction set extension to the AVR CPU, performing
 *  DES iterations. The 64-bit data block (plaintext or ciphertext) is placed in
 *  the CPU register file, registers R0-R7, where LSB of data is placed in LSB
 *  of R0 and MSB of data is placed in MSB of R7. The full 64-bit key (including
 *  parity bits) is placed in registers R8- R15, organized in the register file
 *  with LSB of key in LSB of R8 and MSB of key in MSB of R15. Executing one DES
 *  instruction performs one round in the DES algorithm. Sixteen rounds must be
 *  executed in increasing order to form the correct DES ciphertext or
 *  plaintext. Intermediate results are stored in the register file (R0-R15)
 *  after each DES instruction. The instruction's operand (K) determines which
 *  round is executed, and the half carry flag (H) determines whether encryption
 *  or decryption is performed.  The DES algorithm is described in
 *  "Specifications for the Data Encryption Standard" (Federal Information
 *  Processing Standards Publication 46). Intermediate results in this
 *  implementation differ from the standard because the initial permutation and
 *  the inverse initial permutation are performed each iteration. This does not
 *  affect the result in the final ciphertext or plaintext, but reduces
 *  execution time.
 */
static bool trans_DES(DisasContext *ctx, arg_DES *a)
{
    /* TODO */
    if (!avr_have_feature(ctx, AVR_FEATURE_DES)) {
        return true;
    }

    qemu_log_mask(LOG_UNIMP, "%s: not implemented\n", __func__);

    return true;
}
