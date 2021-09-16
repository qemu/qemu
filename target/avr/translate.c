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

#define DISAS_EXIT   DISAS_TARGET_0  /* We want return to the cpu main loop.  */
#define DISAS_LOOKUP DISAS_TARGET_1  /* We have a variable condition exit.  */
#define DISAS_CHAIN  DISAS_TARGET_2  /* We have a single condition exit.  */

typedef struct DisasContext DisasContext;

/* This is the state at translation time. */
struct DisasContext {
    DisasContextBase base;

    CPUAVRState *env;
    CPUState *cs;

    target_long npc;
    uint32_t opcode;

    /* Routine used to access memory */
    int memidx;

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
     * if (ctx->skip_cond != TCG_COND_NEVER) {
     *     skip_label = gen_new_label();
     *     tcg_gen_brcond_tl(skip_cond, skip_var0, skip_var1, skip_label);
     * }
     *
     * if (free_skip_var0) {
     *     tcg_temp_free(skip_var0);
     *     free_skip_var0 = false;
     * }
     *
     * translate(ctx);
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

void avr_cpu_tcg_init(void)
{
    int i;

#define AVR_REG_OFFS(x) offsetof(CPUAVRState, x)
    cpu_pc = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(pc_w), "pc");
    cpu_Cf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregC), "Cf");
    cpu_Zf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregZ), "Zf");
    cpu_Nf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregN), "Nf");
    cpu_Vf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregV), "Vf");
    cpu_Sf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregS), "Sf");
    cpu_Hf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregH), "Hf");
    cpu_Tf = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregT), "Tf");
    cpu_If = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sregI), "If");
    cpu_rampD = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampD), "rampD");
    cpu_rampX = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampX), "rampX");
    cpu_rampY = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampY), "rampY");
    cpu_rampZ = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(rampZ), "rampZ");
    cpu_eind = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(eind), "eind");
    cpu_sp = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(sp), "sp");
    cpu_skip = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(skip), "skip");

    for (i = 0; i < NUMBER_OF_CPU_REGISTERS; i++) {
        cpu_r[i] = tcg_global_mem_new_i32(cpu_env, AVR_REG_OFFS(r[i]),
                                          reg_names[i]);
    }
#undef AVR_REG_OFFS
}

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

static int to_regs_00_30_by_two(DisasContext *ctx, int indx)
{
    return (indx % 16) * 2;
}

static uint16_t next_word(DisasContext *ctx)
{
    return cpu_lduw_code(ctx->env, ctx->npc++ * 2);
}

static int append_16(DisasContext *ctx, int x)
{
    return x << 16 | next_word(ctx);
}

static bool avr_have_feature(DisasContext *ctx, int feature)
{
    if (!avr_feature(ctx->env, feature)) {
        gen_helper_unsupported(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
        return false;
    }
    return true;
}

static bool decode_insn(DisasContext *ctx, uint16_t insn);
#include "decode-insn.c.inc"

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

/*
 * Branch Instructions
 */
static void gen_jmp_ez(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    tcg_gen_or_tl(cpu_pc, cpu_pc, cpu_eind);
    ctx->base.is_jmp = DISAS_LOOKUP;
}

static void gen_jmp_z(DisasContext *ctx)
{
    tcg_gen_deposit_tl(cpu_pc, cpu_r[30], cpu_r[31], 8, 8);
    ctx->base.is_jmp = DISAS_LOOKUP;
}

static void gen_push_ret(DisasContext *ctx, int ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x0000ff));

        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {

        TCGv t0 = tcg_const_i32((ret & 0x00ffff));

        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_st_tl(t0, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(t0);

    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {

        TCGv lo = tcg_const_i32((ret & 0x0000ff));
        TCGv hi = tcg_const_i32((ret & 0xffff00) >> 8);

        tcg_gen_qemu_st_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_st_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

static void gen_pop_ret(DisasContext *ctx, TCGv ret)
{
    if (avr_feature(ctx->env, AVR_FEATURE_1_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_UB);
    } else if (avr_feature(ctx->env, AVR_FEATURE_2_BYTE_PC)) {
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(ret, cpu_sp, MMU_DATA_IDX, MO_BEUW);
        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
    } else if (avr_feature(ctx->env, AVR_FEATURE_3_BYTE_PC)) {
        TCGv lo = tcg_temp_new_i32();
        TCGv hi = tcg_temp_new_i32();

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
        tcg_gen_qemu_ld_tl(hi, cpu_sp, MMU_DATA_IDX, MO_BEUW);

        tcg_gen_addi_tl(cpu_sp, cpu_sp, 2);
        tcg_gen_qemu_ld_tl(lo, cpu_sp, MMU_DATA_IDX, MO_UB);

        tcg_gen_deposit_tl(ret, lo, hi, 8, 16);

        tcg_temp_free_i32(lo);
        tcg_temp_free_i32(hi);
    }
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    const TranslationBlock *tb = ctx->base.tb;

    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(tb, n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

/*
 *  Relative jump to an address within PC - 2K +1 and PC + 2K (words). For
 *  AVR microcontrollers with Program memory not exceeding 4K words (8KB) this
 *  instruction can address the entire memory from every address location. See
 *  also JMP.
 */
static bool trans_RJMP(DisasContext *ctx, arg_RJMP *a)
{
    int dst = ctx->npc + a->imm;

    gen_goto_tb(ctx, 0, dst);

    return true;
}

/*
 *  Indirect jump to the address pointed to by the Z (16 bits) Pointer
 *  Register in the Register File. The Z-pointer Register is 16 bits wide and
 *  allows jump within the lowest 64K words (128KB) section of Program memory.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_IJMP(DisasContext *ctx, arg_IJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    gen_jmp_z(ctx);

    return true;
}

/*
 *  Indirect jump to the address pointed to by the Z (16 bits) Pointer
 *  Register in the Register File and the EIND Register in the I/O space. This
 *  instruction allows for indirect jumps to the entire 4M (words) Program
 *  memory space. See also IJMP.  This instruction is not available in all
 *  devices. Refer to the device specific instruction set summary.
 */
static bool trans_EIJMP(DisasContext *ctx, arg_EIJMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    gen_jmp_ez(ctx);
    return true;
}

/*
 *  Jump to an address within the entire 4M (words) Program memory. See also
 *  RJMP.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.0
 */
static bool trans_JMP(DisasContext *ctx, arg_JMP *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    gen_goto_tb(ctx, 0, a->imm);

    return true;
}

/*
 *  Relative call to an address within PC - 2K + 1 and PC + 2K (words). The
 *  return address (the instruction after the RCALL) is stored onto the Stack.
 *  See also CALL. For AVR microcontrollers with Program memory not exceeding 4K
 *  words (8KB) this instruction can address the entire memory from every
 *  address location. The Stack Pointer uses a post-decrement scheme during
 *  RCALL.
 */
static bool trans_RCALL(DisasContext *ctx, arg_RCALL *a)
{
    int ret = ctx->npc;
    int dst = ctx->npc + a->imm;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, dst);

    return true;
}

/*
 *  Calls to a subroutine within the entire 4M (words) Program memory. The
 *  return address (to the instruction after the CALL) will be stored onto the
 *  Stack. See also RCALL. The Stack Pointer uses a post-decrement scheme during
 *  CALL.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_ICALL(DisasContext *ctx, arg_ICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_IJMP_ICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_z(ctx);

    return true;
}

/*
 *  Indirect call of a subroutine pointed to by the Z (16 bits) Pointer
 *  Register in the Register File and the EIND Register in the I/O space. This
 *  instruction allows for indirect calls to the entire 4M (words) Program
 *  memory space. See also ICALL. The Stack Pointer uses a post-decrement scheme
 *  during EICALL.  This instruction is not available in all devices. Refer to
 *  the device specific instruction set summary.
 */
static bool trans_EICALL(DisasContext *ctx, arg_EICALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_EIJMP_EICALL)) {
        return true;
    }

    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_jmp_ez(ctx);
    return true;
}

/*
 *  Calls to a subroutine within the entire Program memory. The return
 *  address (to the instruction after the CALL) will be stored onto the Stack.
 *  (See also RCALL). The Stack Pointer uses a post-decrement scheme during
 *  CALL.  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_CALL(DisasContext *ctx, arg_CALL *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_JMP_CALL)) {
        return true;
    }

    int Imm = a->imm;
    int ret = ctx->npc;

    gen_push_ret(ctx, ret);
    gen_goto_tb(ctx, 0, Imm);

    return true;
}

/*
 *  Returns from subroutine. The return address is loaded from the STACK.
 *  The Stack Pointer uses a preincrement scheme during RET.
 */
static bool trans_RET(DisasContext *ctx, arg_RET *a)
{
    gen_pop_ret(ctx, cpu_pc);

    ctx->base.is_jmp = DISAS_LOOKUP;
    return true;
}

/*
 *  Returns from interrupt. The return address is loaded from the STACK and
 *  the Global Interrupt Flag is set.  Note that the Status Register is not
 *  automatically stored when entering an interrupt routine, and it is not
 *  restored when returning from an interrupt routine. This must be handled by
 *  the application program. The Stack Pointer uses a pre-increment scheme
 *  during RETI.
 */
static bool trans_RETI(DisasContext *ctx, arg_RETI *a)
{
    gen_pop_ret(ctx, cpu_pc);
    tcg_gen_movi_tl(cpu_If, 1);

    /* Need to return to main loop to re-evaluate interrupts.  */
    ctx->base.is_jmp = DISAS_EXIT;
    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr, and
 *  skips the next instruction if Rd = Rr.
 */
static bool trans_CPSE(DisasContext *ctx, arg_CPSE *a)
{
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = cpu_r[a->rd];
    ctx->skip_var1 = cpu_r[a->rr];
    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr.
 *  None of the registers are changed. All conditional branches can be used
 *  after this instruction.
 */
static bool trans_CP(DisasContext *ctx, arg_CP *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs a compare between two registers Rd and Rr and
 *  also takes into account the previous carry. None of the registers are
 *  changed. All conditional branches can be used after this instruction.
 */
static bool trans_CPC(DisasContext *ctx, arg_CPC *a)
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

    tcg_temp_free_i32(zero);
    tcg_temp_free_i32(R);

    return true;
}

/*
 *  This instruction performs a compare between register Rd and a constant.
 *  The register is not changed. All conditional branches can be used after this
 *  instruction.
 */
static bool trans_CPI(DisasContext *ctx, arg_CPI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int Imm = a->imm;
    TCGv Rr = tcg_const_i32(Imm);
    TCGv R = tcg_temp_new_i32();

    tcg_gen_sub_tl(R, Rd, Rr); /* R = Rd - Rr */
    tcg_gen_andi_tl(R, R, 0xff); /* make it 8 bits */

    /* update status register */
    gen_sub_CHf(R, Rd, Rr);
    gen_sub_Vf(R, Rd, Rr);
    gen_ZNSf(R);

    tcg_temp_free_i32(R);
    tcg_temp_free_i32(Rr);

    return true;
}

/*
 *  This instruction tests a single bit in a register and skips the next
 *  instruction if the bit is cleared.
 */
static bool trans_SBRC(DisasContext *ctx, arg_SBRC *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = tcg_temp_new();
    ctx->free_skip_var0 = true;

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

/*
 *  This instruction tests a single bit in a register and skips the next
 *  instruction if the bit is set.
 */
static bool trans_SBRS(DisasContext *ctx, arg_SBRS *a)
{
    TCGv Rr = cpu_r[a->rr];

    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = tcg_temp_new();
    ctx->free_skip_var0 = true;

    tcg_gen_andi_tl(ctx->skip_var0, Rr, 1 << a->bit);
    return true;
}

/*
 *  This instruction tests a single bit in an I/O Register and skips the
 *  next instruction if the bit is cleared. This instruction operates on the
 *  lower 32 I/O Registers -- addresses 0-31.
 */
static bool trans_SBIC(DisasContext *ctx, arg_SBIC *a)
{
    TCGv temp = tcg_const_i32(a->reg);

    gen_helper_inb(temp, cpu_env, temp);
    tcg_gen_andi_tl(temp, temp, 1 << a->bit);
    ctx->skip_cond = TCG_COND_EQ;
    ctx->skip_var0 = temp;
    ctx->free_skip_var0 = true;

    return true;
}

/*
 *  This instruction tests a single bit in an I/O Register and skips the
 *  next instruction if the bit is set. This instruction operates on the lower
 *  32 I/O Registers -- addresses 0-31.
 */
static bool trans_SBIS(DisasContext *ctx, arg_SBIS *a)
{
    TCGv temp = tcg_const_i32(a->reg);

    gen_helper_inb(temp, cpu_env, temp);
    tcg_gen_andi_tl(temp, temp, 1 << a->bit);
    ctx->skip_cond = TCG_COND_NE;
    ctx->skip_var0 = temp;
    ctx->free_skip_var0 = true;

    return true;
}

/*
 *  Conditional relative branch. Tests a single bit in SREG and branches
 *  relatively to PC if the bit is cleared. This instruction branches relatively
 *  to PC in either direction (PC - 63 < = destination <= PC + 64). The
 *  parameter k is the offset from PC and is represented in two's complement
 *  form.
 */
static bool trans_BRBC(DisasContext *ctx, arg_BRBC *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_NE, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->base.is_jmp = DISAS_CHAIN;
    return true;
}

/*
 *  Conditional relative branch. Tests a single bit in SREG and branches
 *  relatively to PC if the bit is set. This instruction branches relatively to
 *  PC in either direction (PC - 63 < = destination <= PC + 64). The parameter k
 *  is the offset from PC and is represented in two's complement form.
 */
static bool trans_BRBS(DisasContext *ctx, arg_BRBS *a)
{
    TCGLabel *not_taken = gen_new_label();

    TCGv var;

    switch (a->bit) {
    case 0x00:
        var = cpu_Cf;
        break;
    case 0x01:
        var = cpu_Zf;
        break;
    case 0x02:
        var = cpu_Nf;
        break;
    case 0x03:
        var = cpu_Vf;
        break;
    case 0x04:
        var = cpu_Sf;
        break;
    case 0x05:
        var = cpu_Hf;
        break;
    case 0x06:
        var = cpu_Tf;
        break;
    case 0x07:
        var = cpu_If;
        break;
    default:
        g_assert_not_reached();
    }

    tcg_gen_brcondi_i32(TCG_COND_EQ, var, 0, not_taken);
    gen_goto_tb(ctx, 0, ctx->npc + a->imm);
    gen_set_label(not_taken);

    ctx->base.is_jmp = DISAS_CHAIN;
    return true;
}

/*
 * Data Transfer Instructions
 */

/*
 *  in the gen_set_addr & gen_get_addr functions
 *  H assumed to be in 0x00ff0000 format
 *  M assumed to be in 0x000000ff format
 *  L assumed to be in 0x000000ff format
 */
static void gen_set_addr(TCGv addr, TCGv H, TCGv M, TCGv L)
{

    tcg_gen_andi_tl(L, addr, 0x000000ff);

    tcg_gen_andi_tl(M, addr, 0x0000ff00);
    tcg_gen_shri_tl(M, M, 8);

    tcg_gen_andi_tl(H, addr, 0x00ff0000);
}

static void gen_set_xaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampX, cpu_r[27], cpu_r[26]);
}

static void gen_set_yaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampY, cpu_r[29], cpu_r[28]);
}

static void gen_set_zaddr(TCGv addr)
{
    gen_set_addr(addr, cpu_rampZ, cpu_r[31], cpu_r[30]);
}

static TCGv gen_get_addr(TCGv H, TCGv M, TCGv L)
{
    TCGv addr = tcg_temp_new_i32();

    tcg_gen_deposit_tl(addr, M, H, 8, 8);
    tcg_gen_deposit_tl(addr, L, addr, 8, 16);

    return addr;
}

static TCGv gen_get_xaddr(void)
{
    return gen_get_addr(cpu_rampX, cpu_r[27], cpu_r[26]);
}

static TCGv gen_get_yaddr(void)
{
    return gen_get_addr(cpu_rampY, cpu_r[29], cpu_r[28]);
}

static TCGv gen_get_zaddr(void)
{
    return gen_get_addr(cpu_rampZ, cpu_r[31], cpu_r[30]);
}

/*
 *  Load one byte indirect from data space to register and stores an clear
 *  the bits in data space specified by the register. The instruction can only
 *  be used towards internal SRAM.  The data location is pointed to by the Z (16
 *  bits) Pointer Register in the Register File. Memory access is limited to the
 *  current data segment of 64KB. To access another data segment in devices with
 *  more than 64KB data space, the RAMPZ in register in the I/O area has to be
 *  changed.  The Z-pointer Register is left unchanged by the operation. This
 *  instruction is especially suited for clearing status bits stored in SRAM.
 */
static void gen_data_store(DisasContext *ctx, TCGv data, TCGv addr)
{
    if (ctx->base.tb->flags & TB_FLAGS_FULL_ACCESS) {
        gen_helper_fullwr(cpu_env, data, addr);
    } else {
        tcg_gen_qemu_st8(data, addr, MMU_DATA_IDX); /* mem[addr] = data */
    }
}

static void gen_data_load(DisasContext *ctx, TCGv data, TCGv addr)
{
    if (ctx->base.tb->flags & TB_FLAGS_FULL_ACCESS) {
        gen_helper_fullrd(data, cpu_env, addr);
    } else {
        tcg_gen_qemu_ld8u(data, addr, MMU_DATA_IDX); /* data = mem[addr] */
    }
}

/*
 *  This instruction makes a copy of one register into another. The source
 *  register Rr is left unchanged, while the destination register Rd is loaded
 *  with a copy of Rr.
 */
static bool trans_MOV(DisasContext *ctx, arg_MOV *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv Rr = cpu_r[a->rr];

    tcg_gen_mov_tl(Rd, Rr);

    return true;
}

/*
 *  This instruction makes a copy of one register pair into another register
 *  pair. The source register pair Rr+1:Rr is left unchanged, while the
 *  destination register pair Rd+1:Rd is loaded with a copy of Rr + 1:Rr.  This
 *  instruction is not available in all devices. Refer to the device specific
 *  instruction set summary.
 */
static bool trans_MOVW(DisasContext *ctx, arg_MOVW *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_MOVW)) {
        return true;
    }

    TCGv RdL = cpu_r[a->rd];
    TCGv RdH = cpu_r[a->rd + 1];
    TCGv RrL = cpu_r[a->rr];
    TCGv RrH = cpu_r[a->rr + 1];

    tcg_gen_mov_tl(RdH, RrH);
    tcg_gen_mov_tl(RdL, RrL);

    return true;
}

/*
 * Loads an 8 bit constant directly to register 16 to 31.
 */
static bool trans_LDI(DisasContext *ctx, arg_LDI *a)
{
    TCGv Rd = cpu_r[a->rd];
    int imm = a->imm;

    tcg_gen_movi_tl(Rd, imm);

    return true;
}

/*
 *  Loads one byte from the data space to a register. For parts with SRAM,
 *  the data space consists of the Register File, I/O memory and internal SRAM
 *  (and external SRAM if applicable). For parts without SRAM, the data space
 *  consists of the register file only. The EEPROM has a separate address space.
 *  A 16-bit address must be supplied. Memory access is limited to the current
 *  data segment of 64KB. The LDS instruction uses the RAMPD Register to access
 *  memory above 64KB. To access another data segment in devices with more than
 *  64KB data space, the RAMPD in register in the I/O area has to be changed.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_LDS(DisasContext *ctx, arg_LDS *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;
    a->imm = next_word(ctx);

    tcg_gen_mov_tl(addr, H); /* addr = H:M:L */
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, a->imm);

    gen_data_load(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Loads one byte indirect from the data space to a register. For parts
 *  with SRAM, the data space consists of the Register File, I/O memory and
 *  internal SRAM (and external SRAM if applicable). For parts without SRAM, the
 *  data space consists of the Register File only. In some parts the Flash
 *  Memory has been mapped to the data space and can be read using this command.
 *  The EEPROM has a separate address space.  The data location is pointed to by
 *  the X (16 bits) Pointer Register in the Register File. Memory access is
 *  limited to the current data segment of 64KB. To access another data segment
 *  in devices with more than 64KB data space, the RAMPX in register in the I/O
 *  area has to be changed.  The X-pointer Register can either be left unchanged
 *  by the operation, or it can be post-incremented or predecremented.  These
 *  features are especially suited for accessing arrays, tables, and Stack
 *  Pointer usage of the X-pointer Register. Note that only the low byte of the
 *  X-pointer is updated in devices with no more than 256 bytes data space. For
 *  such devices, the high byte of the pointer is not used by this instruction
 *  and can be used for other purposes. The RAMPX Register in the I/O area is
 *  updated in parts with more than 64KB data space or more than 64KB Program
 *  memory, and the increment/decrement is added to the entire 24-bit address on
 *  such devices.  Not all variants of this instruction is available in all
 *  devices. Refer to the device specific instruction set summary.  In the
 *  Reduced Core tinyAVR the LD instruction can be used to achieve the same
 *  operation as LPM since the program memory is mapped to the data memory
 *  space.
 */
static bool trans_LDX1(DisasContext *ctx, arg_LDX1 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    gen_data_load(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDX2(DisasContext *ctx, arg_LDX2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */

    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDX3(DisasContext *ctx, arg_LDX3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_load(ctx, Rd, addr);
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Loads one byte indirect with or without displacement from the data space
 *  to a register. For parts with SRAM, the data space consists of the Register
 *  File, I/O memory and internal SRAM (and external SRAM if applicable). For
 *  parts without SRAM, the data space consists of the Register File only. In
 *  some parts the Flash Memory has been mapped to the data space and can be
 *  read using this command. The EEPROM has a separate address space.  The data
 *  location is pointed to by the Y (16 bits) Pointer Register in the Register
 *  File. Memory access is limited to the current data segment of 64KB. To
 *  access another data segment in devices with more than 64KB data space, the
 *  RAMPY in register in the I/O area has to be changed.  The Y-pointer Register
 *  can either be left unchanged by the operation, or it can be post-incremented
 *  or predecremented.  These features are especially suited for accessing
 *  arrays, tables, and Stack Pointer usage of the Y-pointer Register. Note that
 *  only the low byte of the Y-pointer is updated in devices with no more than
 *  256 bytes data space. For such devices, the high byte of the pointer is not
 *  used by this instruction and can be used for other purposes. The RAMPY
 *  Register in the I/O area is updated in parts with more than 64KB data space
 *  or more than 64KB Program memory, and the increment/decrement/displacement
 *  is added to the entire 24-bit address on such devices.  Not all variants of
 *  this instruction is available in all devices. Refer to the device specific
 *  instruction set summary.  In the Reduced Core tinyAVR the LD instruction can
 *  be used to achieve the same operation as LPM since the program memory is
 *  mapped to the data memory space.
 */
static bool trans_LDY2(DisasContext *ctx, arg_LDY2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */

    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDY3(DisasContext *ctx, arg_LDY3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_load(ctx, Rd, addr);
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDDY(DisasContext *ctx, arg_LDDY *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, a->imm); /* addr = addr + q */
    gen_data_load(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Loads one byte indirect with or without displacement from the data space
 *  to a register. For parts with SRAM, the data space consists of the Register
 *  File, I/O memory and internal SRAM (and external SRAM if applicable). For
 *  parts without SRAM, the data space consists of the Register File only. In
 *  some parts the Flash Memory has been mapped to the data space and can be
 *  read using this command. The EEPROM has a separate address space.  The data
 *  location is pointed to by the Z (16 bits) Pointer Register in the Register
 *  File. Memory access is limited to the current data segment of 64KB. To
 *  access another data segment in devices with more than 64KB data space, the
 *  RAMPZ in register in the I/O area has to be changed.  The Z-pointer Register
 *  can either be left unchanged by the operation, or it can be post-incremented
 *  or predecremented.  These features are especially suited for Stack Pointer
 *  usage of the Z-pointer Register, however because the Z-pointer Register can
 *  be used for indirect subroutine calls, indirect jumps and table lookup, it
 *  is often more convenient to use the X or Y-pointer as a dedicated Stack
 *  Pointer. Note that only the low byte of the Z-pointer is updated in devices
 *  with no more than 256 bytes data space. For such devices, the high byte of
 *  the pointer is not used by this instruction and can be used for other
 *  purposes. The RAMPZ Register in the I/O area is updated in parts with more
 *  than 64KB data space or more than 64KB Program memory, and the
 *  increment/decrement/displacement is added to the entire 24-bit address on
 *  such devices.  Not all variants of this instruction is available in all
 *  devices. Refer to the device specific instruction set summary.  In the
 *  Reduced Core tinyAVR the LD instruction can be used to achieve the same
 *  operation as LPM since the program memory is mapped to the data memory
 *  space.  For using the Z-pointer for table lookup in Program memory see the
 *  LPM and ELPM instructions.
 */
static bool trans_LDZ2(DisasContext *ctx, arg_LDZ2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    gen_data_load(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDZ3(DisasContext *ctx, arg_LDZ3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_load(ctx, Rd, addr);

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LDDZ(DisasContext *ctx, arg_LDDZ *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, a->imm); /* addr = addr + q */
    gen_data_load(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Stores one byte from a Register to the data space. For parts with SRAM,
 *  the data space consists of the Register File, I/O memory and internal SRAM
 *  (and external SRAM if applicable). For parts without SRAM, the data space
 *  consists of the Register File only. The EEPROM has a separate address space.
 *  A 16-bit address must be supplied. Memory access is limited to the current
 *  data segment of 64KB. The STS instruction uses the RAMPD Register to access
 *  memory above 64KB. To access another data segment in devices with more than
 *  64KB data space, the RAMPD in register in the I/O area has to be changed.
 *  This instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 */
static bool trans_STS(DisasContext *ctx, arg_STS *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_rampD;
    a->imm = next_word(ctx);

    tcg_gen_mov_tl(addr, H); /* addr = H:M:L */
    tcg_gen_shli_tl(addr, addr, 16);
    tcg_gen_ori_tl(addr, addr, a->imm);
    gen_data_store(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 * Stores one byte indirect from a register to data space. For parts with SRAM,
 * the data space consists of the Register File, I/O memory, and internal SRAM
 * (and external SRAM if applicable). For parts without SRAM, the data space
 * consists of the Register File only. The EEPROM has a separate address space.
 *
 * The data location is pointed to by the X (16 bits) Pointer Register in the
 * Register File. Memory access is limited to the current data segment of 64KB.
 * To access another data segment in devices with more than 64KB data space, the
 * RAMPX in register in the I/O area has to be changed.
 *
 * The X-pointer Register can either be left unchanged by the operation, or it
 * can be post-incremented or pre-decremented. These features are especially
 * suited for accessing arrays, tables, and Stack Pointer usage of the
 * X-pointer Register. Note that only the low byte of the X-pointer is updated
 * in devices with no more than 256 bytes data space. For such devices, the high
 * byte of the pointer is not used by this instruction and can be used for other
 * purposes. The RAMPX Register in the I/O area is updated in parts with more
 * than 64KB data space or more than 64KB Program memory, and the increment /
 * decrement is added to the entire 24-bit address on such devices.
 */
static bool trans_STX1(DisasContext *ctx, arg_STX1 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    gen_data_store(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STX2(DisasContext *ctx, arg_STX2 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STX3(DisasContext *ctx, arg_STX3 *a)
{
    TCGv Rd = cpu_r[a->rr];
    TCGv addr = gen_get_xaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_store(ctx, Rd, addr);
    gen_set_xaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 * Stores one byte indirect with or without displacement from a register to data
 * space. For parts with SRAM, the data space consists of the Register File, I/O
 * memory, and internal SRAM (and external SRAM if applicable). For parts
 * without SRAM, the data space consists of the Register File only. The EEPROM
 * has a separate address space.
 *
 * The data location is pointed to by the Y (16 bits) Pointer Register in the
 * Register File. Memory access is limited to the current data segment of 64KB.
 * To access another data segment in devices with more than 64KB data space, the
 * RAMPY in register in the I/O area has to be changed.
 *
 * The Y-pointer Register can either be left unchanged by the operation, or it
 * can be post-incremented or pre-decremented. These features are especially
 * suited for accessing arrays, tables, and Stack Pointer usage of the Y-pointer
 * Register. Note that only the low byte of the Y-pointer is updated in devices
 * with no more than 256 bytes data space. For such devices, the high byte of
 * the pointer is not used by this instruction and can be used for other
 * purposes. The RAMPY Register in the I/O area is updated in parts with more
 * than 64KB data space or more than 64KB Program memory, and the increment /
 * decrement / displacement is added to the entire 24-bit address on such
 * devices.
 */
static bool trans_STY2(DisasContext *ctx, arg_STY2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STY3(DisasContext *ctx, arg_STY3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_store(ctx, Rd, addr);
    gen_set_yaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STDY(DisasContext *ctx, arg_STDY *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_yaddr();

    tcg_gen_addi_tl(addr, addr, a->imm); /* addr = addr + q */
    gen_data_store(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 * Stores one byte indirect with or without displacement from a register to data
 * space. For parts with SRAM, the data space consists of the Register File, I/O
 * memory, and internal SRAM (and external SRAM if applicable). For parts
 * without SRAM, the data space consists of the Register File only. The EEPROM
 * has a separate address space.
 *
 * The data location is pointed to by the Y (16 bits) Pointer Register in the
 * Register File. Memory access is limited to the current data segment of 64KB.
 * To access another data segment in devices with more than 64KB data space, the
 * RAMPY in register in the I/O area has to be changed.
 *
 * The Y-pointer Register can either be left unchanged by the operation, or it
 * can be post-incremented or pre-decremented. These features are especially
 * suited for accessing arrays, tables, and Stack Pointer usage of the Y-pointer
 * Register. Note that only the low byte of the Y-pointer is updated in devices
 * with no more than 256 bytes data space. For such devices, the high byte of
 * the pointer is not used by this instruction and can be used for other
 * purposes. The RAMPY Register in the I/O area is updated in parts with more
 * than 64KB data space or more than 64KB Program memory, and the increment /
 * decrement / displacement is added to the entire 24-bit address on such
 * devices.
 */
static bool trans_STZ2(DisasContext *ctx, arg_STZ2 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    gen_data_store(ctx, Rd, addr);
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STZ3(DisasContext *ctx, arg_STZ3 *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_subi_tl(addr, addr, 1); /* addr = addr - 1 */
    gen_data_store(ctx, Rd, addr);

    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_STDZ(DisasContext *ctx, arg_STDZ *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_addi_tl(addr, addr, a->imm); /* addr = addr + q */
    gen_data_store(ctx, Rd, addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Loads one byte pointed to by the Z-register into the destination
 *  register Rd. This instruction features a 100% space effective constant
 *  initialization or constant data fetch. The Program memory is organized in
 *  16-bit words while the Z-pointer is a byte address. Thus, the least
 *  significant bit of the Z-pointer selects either low byte (ZLSB = 0) or high
 *  byte (ZLSB = 1). This instruction can address the first 64KB (32K words) of
 *  Program memory. The Zpointer Register can either be left unchanged by the
 *  operation, or it can be incremented. The incrementation does not apply to
 *  the RAMPZ Register.
 *
 *  Devices with Self-Programming capability can use the LPM instruction to read
 *  the Fuse and Lock bit values.
 */
static bool trans_LPM1(DisasContext *ctx, arg_LPM1 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPM)) {
        return true;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8); /* addr = H:L */
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LPM2(DisasContext *ctx, arg_LPM2 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPM)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8); /* addr = H:L */
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_LPMX(DisasContext *ctx, arg_LPMX *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_LPMX)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = tcg_temp_new_i32();
    TCGv H = cpu_r[31];
    TCGv L = cpu_r[30];

    tcg_gen_shli_tl(addr, H, 8); /* addr = H:L */
    tcg_gen_or_tl(addr, addr, L);
    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */
    tcg_gen_andi_tl(L, addr, 0xff);
    tcg_gen_shri_tl(addr, addr, 8);
    tcg_gen_andi_tl(H, addr, 0xff);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Loads one byte pointed to by the Z-register and the RAMPZ Register in
 *  the I/O space, and places this byte in the destination register Rd. This
 *  instruction features a 100% space effective constant initialization or
 *  constant data fetch. The Program memory is organized in 16-bit words while
 *  the Z-pointer is a byte address. Thus, the least significant bit of the
 *  Z-pointer selects either low byte (ZLSB = 0) or high byte (ZLSB = 1). This
 *  instruction can address the entire Program memory space. The Z-pointer
 *  Register can either be left unchanged by the operation, or it can be
 *  incremented. The incrementation applies to the entire 24-bit concatenation
 *  of the RAMPZ and Z-pointer Registers.
 *
 *  Devices with Self-Programming capability can use the ELPM instruction to
 *  read the Fuse and Lock bit value.
 */
static bool trans_ELPM1(DisasContext *ctx, arg_ELPM1 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPM)) {
        return true;
    }

    TCGv Rd = cpu_r[0];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_ELPM2(DisasContext *ctx, arg_ELPM2 *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPM)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */

    tcg_temp_free_i32(addr);

    return true;
}

static bool trans_ELPMX(DisasContext *ctx, arg_ELPMX *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_ELPMX)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();

    tcg_gen_qemu_ld8u(Rd, addr, MMU_CODE_IDX); /* Rd = mem[addr] */
    tcg_gen_addi_tl(addr, addr, 1); /* addr = addr + 1 */
    gen_set_zaddr(addr);

    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  SPM can be used to erase a page in the Program memory, to write a page
 *  in the Program memory (that is already erased), and to set Boot Loader Lock
 *  bits. In some devices, the Program memory can be written one word at a time,
 *  in other devices an entire page can be programmed simultaneously after first
 *  filling a temporary page buffer. In all cases, the Program memory must be
 *  erased one page at a time. When erasing the Program memory, the RAMPZ and
 *  Z-register are used as page address. When writing the Program memory, the
 *  RAMPZ and Z-register are used as page or word address, and the R1:R0
 *  register pair is used as data(1). When setting the Boot Loader Lock bits,
 *  the R1:R0 register pair is used as data. Refer to the device documentation
 *  for detailed description of SPM usage. This instruction can address the
 *  entire Program memory.
 *
 *  The SPM instruction is not available in all devices. Refer to the device
 *  specific instruction set summary.
 *
 *  Note: 1. R1 determines the instruction high byte, and R0 determines the
 *  instruction low byte.
 */
static bool trans_SPM(DisasContext *ctx, arg_SPM *a)
{
    /* TODO */
    if (!avr_have_feature(ctx, AVR_FEATURE_SPM)) {
        return true;
    }

    return true;
}

static bool trans_SPMX(DisasContext *ctx, arg_SPMX *a)
{
    /* TODO */
    if (!avr_have_feature(ctx, AVR_FEATURE_SPMX)) {
        return true;
    }

    return true;
}

/*
 *  Loads data from the I/O Space (Ports, Timers, Configuration Registers,
 *  etc.) into register Rd in the Register File.
 */
static bool trans_IN(DisasContext *ctx, arg_IN *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv port = tcg_const_i32(a->imm);

    gen_helper_inb(Rd, cpu_env, port);

    tcg_temp_free_i32(port);

    return true;
}

/*
 *  Stores data from register Rr in the Register File to I/O Space (Ports,
 *  Timers, Configuration Registers, etc.).
 */
static bool trans_OUT(DisasContext *ctx, arg_OUT *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv port = tcg_const_i32(a->imm);

    gen_helper_outb(cpu_env, port, Rd);

    tcg_temp_free_i32(port);

    return true;
}

/*
 *  This instruction stores the contents of register Rr on the STACK. The
 *  Stack Pointer is post-decremented by 1 after the PUSH.  This instruction is
 *  not available in all devices. Refer to the device specific instruction set
 *  summary.
 */
static bool trans_PUSH(DisasContext *ctx, arg_PUSH *a)
{
    TCGv Rd = cpu_r[a->rd];

    gen_data_store(ctx, Rd, cpu_sp);
    tcg_gen_subi_tl(cpu_sp, cpu_sp, 1);

    return true;
}

/*
 *  This instruction loads register Rd with a byte from the STACK. The Stack
 *  Pointer is pre-incremented by 1 before the POP.  This instruction is not
 *  available in all devices. Refer to the device specific instruction set
 *  summary.
 */
static bool trans_POP(DisasContext *ctx, arg_POP *a)
{
    /*
     * Using a temp to work around some strange behaviour:
     * tcg_gen_addi_tl(cpu_sp, cpu_sp, 1);
     * gen_data_load(ctx, Rd, cpu_sp);
     * seems to cause the add to happen twice.
     * This doesn't happen if either the add or the load is removed.
     */
    TCGv t1 = tcg_temp_new_i32();
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_addi_tl(t1, cpu_sp, 1);
    gen_data_load(ctx, Rd, t1);
    tcg_gen_mov_tl(cpu_sp, t1);

    return true;
}

/*
 *  Exchanges one byte indirect between register and data space.  The data
 *  location is pointed to by the Z (16 bits) Pointer Register in the Register
 *  File. Memory access is limited to the current data segment of 64KB. To
 *  access another data segment in devices with more than 64KB data space, the
 *  RAMPZ in register in the I/O area has to be changed.
 *
 *  The Z-pointer Register is left unchanged by the operation. This instruction
 *  is especially suited for writing/reading status bits stored in SRAM.
 */
static bool trans_XCH(DisasContext *ctx, arg_XCH *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();
    TCGv addr = gen_get_zaddr();

    gen_data_load(ctx, t0, addr);
    gen_data_store(ctx, Rd, addr);
    tcg_gen_mov_tl(Rd, t0);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Load one byte indirect from data space to register and set bits in data
 *  space specified by the register. The instruction can only be used towards
 *  internal SRAM.  The data location is pointed to by the Z (16 bits) Pointer
 *  Register in the Register File. Memory access is limited to the current data
 *  segment of 64KB. To access another data segment in devices with more than
 *  64KB data space, the RAMPZ in register in the I/O area has to be changed.
 *
 *  The Z-pointer Register is left unchanged by the operation. This instruction
 *  is especially suited for setting status bits stored in SRAM.
 */
static bool trans_LAS(DisasContext *ctx, arg_LAS *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rr = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr); /* t0 = mem[addr] */
    tcg_gen_or_tl(t1, t0, Rr);
    tcg_gen_mov_tl(Rr, t0); /* Rr = t0 */
    gen_data_store(ctx, t1, addr); /* mem[addr] = t1 */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return true;
}

/*
 *  Load one byte indirect from data space to register and stores and clear
 *  the bits in data space specified by the register. The instruction can
 *  only be used towards internal SRAM.  The data location is pointed to by
 *  the Z (16 bits) Pointer Register in the Register File. Memory access is
 *  limited to the current data segment of 64KB. To access another data
 *  segment in devices with more than 64KB data space, the RAMPZ in register
 *  in the I/O area has to be changed.
 *
 *  The Z-pointer Register is left unchanged by the operation. This instruction
 *  is especially suited for clearing status bits stored in SRAM.
 */
static bool trans_LAC(DisasContext *ctx, arg_LAC *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rr = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr); /* t0 = mem[addr] */
    tcg_gen_andc_tl(t1, t0, Rr); /* t1 = t0 & (0xff - Rr) = t0 & ~Rr */
    tcg_gen_mov_tl(Rr, t0); /* Rr = t0 */
    gen_data_store(ctx, t1, addr); /* mem[addr] = t1 */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return true;
}


/*
 *  Load one byte indirect from data space to register and toggles bits in
 *  the data space specified by the register.  The instruction can only be used
 *  towards SRAM.  The data location is pointed to by the Z (16 bits) Pointer
 *  Register in the Register File. Memory access is limited to the current data
 *  segment of 64KB. To access another data segment in devices with more than
 *  64KB data space, the RAMPZ in register in the I/O area has to be changed.
 *
 *  The Z-pointer Register is left unchanged by the operation. This instruction
 *  is especially suited for changing status bits stored in SRAM.
 */
static bool trans_LAT(DisasContext *ctx, arg_LAT *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_RMW)) {
        return true;
    }

    TCGv Rd = cpu_r[a->rd];
    TCGv addr = gen_get_zaddr();
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    gen_data_load(ctx, t0, addr); /* t0 = mem[addr] */
    tcg_gen_xor_tl(t1, t0, Rd);
    tcg_gen_mov_tl(Rd, t0); /* Rd = t0 */
    gen_data_store(ctx, t1, addr); /* mem[addr] = t1 */

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(addr);

    return true;
}

/*
 * Bit and Bit-test Instructions
 */
static void gen_rshift_ZNVSf(TCGv R)
{
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, R, 0); /* Zf = R == 0 */
    tcg_gen_shri_tl(cpu_Nf, R, 7); /* Nf = R(7) */
    tcg_gen_xor_tl(cpu_Vf, cpu_Nf, cpu_Cf);
    tcg_gen_xor_tl(cpu_Sf, cpu_Nf, cpu_Vf); /* Sf = Nf ^ Vf */
}

/*
 *  Shifts all bits in Rd one place to the right. Bit 7 is cleared. Bit 0 is
 *  loaded into the C Flag of the SREG. This operation effectively divides an
 *  unsigned value by two. The C Flag can be used to round the result.
 */
static bool trans_LSR(DisasContext *ctx, arg_LSR *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_andi_tl(cpu_Cf, Rd, 1);
    tcg_gen_shri_tl(Rd, Rd, 1);

    /* update status register */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_Zf, Rd, 0); /* Zf = Rd == 0 */
    tcg_gen_movi_tl(cpu_Nf, 0);
    tcg_gen_mov_tl(cpu_Vf, cpu_Cf);
    tcg_gen_mov_tl(cpu_Sf, cpu_Vf);

    return true;
}

/*
 *  Shifts all bits in Rd one place to the right. The C Flag is shifted into
 *  bit 7 of Rd. Bit 0 is shifted into the C Flag.  This operation, combined
 *  with ASR, effectively divides multi-byte signed values by two. Combined with
 *  LSR it effectively divides multi-byte unsigned values by two. The Carry Flag
 *  can be used to round the result.
 */
static bool trans_ROR(DisasContext *ctx, arg_ROR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();

    tcg_gen_shli_tl(t0, cpu_Cf, 7);

    /* update status register */
    tcg_gen_andi_tl(cpu_Cf, Rd, 1);

    /* update output register */
    tcg_gen_shri_tl(Rd, Rd, 1);
    tcg_gen_or_tl(Rd, Rd, t0);

    /* update status register */
    gen_rshift_ZNVSf(Rd);

    tcg_temp_free_i32(t0);

    return true;
}

/*
 *  Shifts all bits in Rd one place to the right. Bit 7 is held constant. Bit 0
 *  is loaded into the C Flag of the SREG. This operation effectively divides a
 *  signed value by two without changing its sign. The Carry Flag can be used to
 *  round the result.
 */
static bool trans_ASR(DisasContext *ctx, arg_ASR *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();

    /* update status register */
    tcg_gen_andi_tl(cpu_Cf, Rd, 1); /* Cf = Rd(0) */

    /* update output register */
    tcg_gen_andi_tl(t0, Rd, 0x80); /* Rd = (Rd & 0x80) | (Rd >> 1) */
    tcg_gen_shri_tl(Rd, Rd, 1);
    tcg_gen_or_tl(Rd, Rd, t0);

    /* update status register */
    gen_rshift_ZNVSf(Rd);

    tcg_temp_free_i32(t0);

    return true;
}

/*
 *  Swaps high and low nibbles in a register.
 */
static bool trans_SWAP(DisasContext *ctx, arg_SWAP *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(t0, Rd, 0x0f);
    tcg_gen_shli_tl(t0, t0, 4);
    tcg_gen_andi_tl(t1, Rd, 0xf0);
    tcg_gen_shri_tl(t1, t1, 4);
    tcg_gen_or_tl(Rd, t0, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);

    return true;
}

/*
 *  Sets a specified bit in an I/O Register. This instruction operates on
 *  the lower 32 I/O Registers -- addresses 0-31.
 */
static bool trans_SBI(DisasContext *ctx, arg_SBI *a)
{
    TCGv data = tcg_temp_new_i32();
    TCGv port = tcg_const_i32(a->reg);

    gen_helper_inb(data, cpu_env, port);
    tcg_gen_ori_tl(data, data, 1 << a->bit);
    gen_helper_outb(cpu_env, port, data);

    tcg_temp_free_i32(port);
    tcg_temp_free_i32(data);

    return true;
}

/*
 *  Clears a specified bit in an I/O Register. This instruction operates on
 *  the lower 32 I/O Registers -- addresses 0-31.
 */
static bool trans_CBI(DisasContext *ctx, arg_CBI *a)
{
    TCGv data = tcg_temp_new_i32();
    TCGv port = tcg_const_i32(a->reg);

    gen_helper_inb(data, cpu_env, port);
    tcg_gen_andi_tl(data, data, ~(1 << a->bit));
    gen_helper_outb(cpu_env, port, data);

    tcg_temp_free_i32(data);
    tcg_temp_free_i32(port);

    return true;
}

/*
 *  Stores bit b from Rd to the T Flag in SREG (Status Register).
 */
static bool trans_BST(DisasContext *ctx, arg_BST *a)
{
    TCGv Rd = cpu_r[a->rd];

    tcg_gen_andi_tl(cpu_Tf, Rd, 1 << a->bit);
    tcg_gen_shri_tl(cpu_Tf, cpu_Tf, a->bit);

    return true;
}

/*
 *  Copies the T Flag in the SREG (Status Register) to bit b in register Rd.
 */
static bool trans_BLD(DisasContext *ctx, arg_BLD *a)
{
    TCGv Rd = cpu_r[a->rd];
    TCGv t1 = tcg_temp_new_i32();

    tcg_gen_andi_tl(Rd, Rd, ~(1u << a->bit)); /* clear bit */
    tcg_gen_shli_tl(t1, cpu_Tf, a->bit); /* create mask */
    tcg_gen_or_tl(Rd, Rd, t1);

    tcg_temp_free_i32(t1);

    return true;
}

/*
 *  Sets a single Flag or bit in SREG.
 */
static bool trans_BSET(DisasContext *ctx, arg_BSET *a)
{
    switch (a->bit) {
    case 0x00:
        tcg_gen_movi_tl(cpu_Cf, 0x01);
        break;
    case 0x01:
        tcg_gen_movi_tl(cpu_Zf, 0x01);
        break;
    case 0x02:
        tcg_gen_movi_tl(cpu_Nf, 0x01);
        break;
    case 0x03:
        tcg_gen_movi_tl(cpu_Vf, 0x01);
        break;
    case 0x04:
        tcg_gen_movi_tl(cpu_Sf, 0x01);
        break;
    case 0x05:
        tcg_gen_movi_tl(cpu_Hf, 0x01);
        break;
    case 0x06:
        tcg_gen_movi_tl(cpu_Tf, 0x01);
        break;
    case 0x07:
        tcg_gen_movi_tl(cpu_If, 0x01);
        break;
    }

    return true;
}

/*
 *  Clears a single Flag in SREG.
 */
static bool trans_BCLR(DisasContext *ctx, arg_BCLR *a)
{
    switch (a->bit) {
    case 0x00:
        tcg_gen_movi_tl(cpu_Cf, 0x00);
        break;
    case 0x01:
        tcg_gen_movi_tl(cpu_Zf, 0x00);
        break;
    case 0x02:
        tcg_gen_movi_tl(cpu_Nf, 0x00);
        break;
    case 0x03:
        tcg_gen_movi_tl(cpu_Vf, 0x00);
        break;
    case 0x04:
        tcg_gen_movi_tl(cpu_Sf, 0x00);
        break;
    case 0x05:
        tcg_gen_movi_tl(cpu_Hf, 0x00);
        break;
    case 0x06:
        tcg_gen_movi_tl(cpu_Tf, 0x00);
        break;
    case 0x07:
        tcg_gen_movi_tl(cpu_If, 0x00);
        break;
    }

    return true;
}

/*
 * MCU Control Instructions
 */

/*
 *  The BREAK instruction is used by the On-chip Debug system, and is
 *  normally not used in the application software. When the BREAK instruction is
 *  executed, the AVR CPU is set in the Stopped Mode. This gives the On-chip
 *  Debugger access to internal resources.  If any Lock bits are set, or either
 *  the JTAGEN or OCDEN Fuses are unprogrammed, the CPU will treat the BREAK
 *  instruction as a NOP and will not enter the Stopped mode.  This instruction
 *  is not available in all devices. Refer to the device specific instruction
 *  set summary.
 */
static bool trans_BREAK(DisasContext *ctx, arg_BREAK *a)
{
    if (!avr_have_feature(ctx, AVR_FEATURE_BREAK)) {
        return true;
    }

#ifdef BREAKPOINT_ON_BREAK
    tcg_gen_movi_tl(cpu_pc, ctx->npc - 1);
    gen_helper_debug(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#else
    /* NOP */
#endif

    return true;
}

/*
 *  This instruction performs a single cycle No Operation.
 */
static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{

    /* NOP */

    return true;
}

/*
 *  This instruction sets the circuit in sleep mode defined by the MCU
 *  Control Register.
 */
static bool trans_SLEEP(DisasContext *ctx, arg_SLEEP *a)
{
    gen_helper_sleep(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

/*
 *  This instruction resets the Watchdog Timer. This instruction must be
 *  executed within a limited time given by the WD prescaler. See the Watchdog
 *  Timer hardware specification.
 */
static bool trans_WDR(DisasContext *ctx, arg_WDR *a)
{
    gen_helper_wdr(cpu_env);

    return true;
}

/*
 *  Core translation mechanism functions:
 *
 *    - translate()
 *    - canonicalize_skip()
 *    - gen_intermediate_code()
 *    - restore_state_to_opc()
 *
 */
static void translate(DisasContext *ctx)
{
    uint32_t opcode = next_word(ctx);

    if (!decode_insn(ctx, opcode)) {
        gen_helper_unsupported(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

/* Standardize the cpu_skip condition to NE.  */
static bool canonicalize_skip(DisasContext *ctx)
{
    switch (ctx->skip_cond) {
    case TCG_COND_NEVER:
        /* Normal case: cpu_skip is known to be false.  */
        return false;

    case TCG_COND_ALWAYS:
        /*
         * Breakpoint case: cpu_skip is known to be true, via TB_FLAGS_SKIP.
         * The breakpoint is on the instruction being skipped, at the start
         * of the TranslationBlock.  No need to update.
         */
        return false;

    case TCG_COND_NE:
        if (ctx->skip_var1 == NULL) {
            tcg_gen_mov_tl(cpu_skip, ctx->skip_var0);
        } else {
            tcg_gen_xor_tl(cpu_skip, ctx->skip_var0, ctx->skip_var1);
            ctx->skip_var1 = NULL;
        }
        break;

    default:
        /* Convert to a NE condition vs 0. */
        if (ctx->skip_var1 == NULL) {
            tcg_gen_setcondi_tl(ctx->skip_cond, cpu_skip, ctx->skip_var0, 0);
        } else {
            tcg_gen_setcond_tl(ctx->skip_cond, cpu_skip,
                               ctx->skip_var0, ctx->skip_var1);
            ctx->skip_var1 = NULL;
        }
        ctx->skip_cond = TCG_COND_NE;
        break;
    }
    if (ctx->free_skip_var0) {
        tcg_temp_free(ctx->skip_var0);
        ctx->free_skip_var0 = false;
    }
    ctx->skip_var0 = cpu_skip;
    return true;
}

static void avr_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUAVRState *env = cs->env_ptr;
    uint32_t tb_flags = ctx->base.tb->flags;

    ctx->cs = cs;
    ctx->env = env;
    ctx->npc = ctx->base.pc_first / 2;

    ctx->skip_cond = TCG_COND_NEVER;
    if (tb_flags & TB_FLAGS_SKIP) {
        ctx->skip_cond = TCG_COND_ALWAYS;
        ctx->skip_var0 = cpu_skip;
    }

    if (tb_flags & TB_FLAGS_FULL_ACCESS) {
        /*
         * This flag is set by ST/LD instruction we will regenerate it ONLY
         * with mem/cpu memory access instead of mem access
         */
        ctx->base.max_insns = 1;
    }
}

static void avr_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void avr_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->npc);
}

static void avr_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    TCGLabel *skip_label = NULL;

    /* Conditionally skip the next instruction, if indicated.  */
    if (ctx->skip_cond != TCG_COND_NEVER) {
        skip_label = gen_new_label();
        if (ctx->skip_var0 == cpu_skip) {
            /*
             * Copy cpu_skip so that we may zero it before the branch.
             * This ensures that cpu_skip is non-zero after the label
             * if and only if the skipped insn itself sets a skip.
             */
            ctx->free_skip_var0 = true;
            ctx->skip_var0 = tcg_temp_new();
            tcg_gen_mov_tl(ctx->skip_var0, cpu_skip);
            tcg_gen_movi_tl(cpu_skip, 0);
        }
        if (ctx->skip_var1 == NULL) {
            tcg_gen_brcondi_tl(ctx->skip_cond, ctx->skip_var0, 0, skip_label);
        } else {
            tcg_gen_brcond_tl(ctx->skip_cond, ctx->skip_var0,
                              ctx->skip_var1, skip_label);
            ctx->skip_var1 = NULL;
        }
        if (ctx->free_skip_var0) {
            tcg_temp_free(ctx->skip_var0);
            ctx->free_skip_var0 = false;
        }
        ctx->skip_cond = TCG_COND_NEVER;
        ctx->skip_var0 = NULL;
    }

    translate(ctx);

    ctx->base.pc_next = ctx->npc * 2;

    if (skip_label) {
        canonicalize_skip(ctx);
        gen_set_label(skip_label);
        if (ctx->base.is_jmp == DISAS_NORETURN) {
            ctx->base.is_jmp = DISAS_CHAIN;
        }
    }

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_first = ctx->base.pc_first & TARGET_PAGE_MASK;

        if ((ctx->base.pc_next - page_first) >= TARGET_PAGE_SIZE - 4) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void avr_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    bool nonconst_skip = canonicalize_skip(ctx);

    switch (ctx->base.is_jmp) {
    case DISAS_NORETURN:
        assert(!nonconst_skip);
        break;
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
    case DISAS_CHAIN:
        if (!nonconst_skip) {
            /* Note gen_goto_tb checks singlestep.  */
            gen_goto_tb(ctx, 1, ctx->npc);
            break;
        }
        tcg_gen_movi_tl(cpu_pc, ctx->npc);
        /* fall through */
    case DISAS_LOOKUP:
        if (!ctx->base.singlestep_enabled) {
            tcg_gen_lookup_and_goto_ptr();
            break;
        }
        /* fall through */
    case DISAS_EXIT:
        if (ctx->base.singlestep_enabled) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void avr_tr_disas_log(const DisasContextBase *dcbase, CPUState *cs)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps avr_tr_ops = {
    .init_disas_context = avr_tr_init_disas_context,
    .tb_start           = avr_tr_tb_start,
    .insn_start         = avr_tr_insn_start,
    .translate_insn     = avr_tr_translate_insn,
    .tb_stop            = avr_tr_tb_stop,
    .disas_log          = avr_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext dc = { };
    translator_loop(&avr_tr_ops, &dc.base, cs, tb, max_insns);
}

void restore_state_to_opc(CPUAVRState *env, TranslationBlock *tb,
                            target_ulong *data)
{
    env->pc_w = data[0];
}
