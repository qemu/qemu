/*
 *  TriCore emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2013-2014 Bastian Koppelmann C-Lab/University Paderborn
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */


#include "cpu.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "tricore-opcodes.h"

/*
 * TCG registers
 */
static TCGv cpu_PC;
static TCGv cpu_PCXI;
static TCGv cpu_PSW;
static TCGv cpu_ICR;
/* GPR registers */
static TCGv cpu_gpr_a[16];
static TCGv cpu_gpr_d[16];
/* PSW Flag cache */
static TCGv cpu_PSW_C;
static TCGv cpu_PSW_V;
static TCGv cpu_PSW_SV;
static TCGv cpu_PSW_AV;
static TCGv cpu_PSW_SAV;
/* CPU env */
static TCGv_ptr cpu_env;

#include "exec/gen-icount.h"

static const char *regnames_a[] = {
      "a0"  , "a1"  , "a2"  , "a3" , "a4"  , "a5" ,
      "a6"  , "a7"  , "a8"  , "a9" , "sp" , "a11" ,
      "a12" , "a13" , "a14" , "a15",
    };

static const char *regnames_d[] = {
      "d0"  , "d1"  , "d2"  , "d3" , "d4"  , "d5"  ,
      "d6"  , "d7"  , "d8"  , "d9" , "d10" , "d11" ,
      "d12" , "d13" , "d14" , "d15",
    };

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc, next_pc;
    uint32_t opcode;
    int singlestep_enabled;
    /* Routine used to access memory */
    int mem_idx;
    uint32_t hflags, saved_hflags;
    int bstate;
} DisasContext;

enum {

    BS_NONE   = 0,
    BS_STOP   = 1,
    BS_BRANCH = 2,
    BS_EXCP   = 3,
};

void tricore_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "PC=%08x\n", env->PC);
    for (i = 0; i < 16; ++i) {
        if ((i & 3) == 0) {
            cpu_fprintf(f, "GPR A%02d:", i);
        }
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames_a[i], env->gpr_a[i]);
    }
    for (i = 0; i < 16; ++i) {
        if ((i & 3) == 0) {
            cpu_fprintf(f, "GPR D%02d:", i);
        }
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames_d[i], env->gpr_d[i]);
    }

}

/*
 * Functions to generate micro-ops
 */

/* Makros for generating helpers */

#define gen_helper_1arg(name, arg) do {                           \
    TCGv_i32 helper_tmp = tcg_const_i32(arg);                     \
    gen_helper_##name(cpu_env, helper_tmp);                       \
    tcg_temp_free_i32(helper_tmp);                                \
    } while (0)

/* Functions for load/save to/from memory */

static inline void gen_offset_ld(DisasContext *ctx, TCGv r1, TCGv r2,
                                 int16_t con, TCGMemOp mop)
{
    TCGv temp = tcg_temp_new();
    tcg_gen_addi_tl(temp, r2, con);
    tcg_gen_qemu_ld_tl(r1, temp, ctx->mem_idx, mop);
    tcg_temp_free(temp);
}

static inline void gen_offset_st(DisasContext *ctx, TCGv r1, TCGv r2,
                                 int16_t con, TCGMemOp mop)
{
    TCGv temp = tcg_temp_new();
    tcg_gen_addi_tl(temp, r2, con);
    tcg_gen_qemu_st_tl(r1, temp, ctx->mem_idx, mop);
    tcg_temp_free(temp);
}

/* Functions for arithmetic instructions  */

static inline void gen_add_d(TCGv ret, TCGv r1, TCGv r2)
{
    TCGv t0 = tcg_temp_new_i32();
    TCGv result = tcg_temp_new_i32();
    /* Addition and set V/SV bits */
    tcg_gen_add_tl(result, r1, r2);
    /* calc V bit */
    tcg_gen_xor_tl(cpu_PSW_V, result, r1);
    tcg_gen_xor_tl(t0, r1, r2);
    tcg_gen_andc_tl(cpu_PSW_V, cpu_PSW_V, t0);
    /* Calc SV bit */
    tcg_gen_or_tl(cpu_PSW_SV, cpu_PSW_SV, cpu_PSW_V);
    /* Calc AV/SAV bits */
    tcg_gen_add_tl(cpu_PSW_AV, result, result);
    tcg_gen_xor_tl(cpu_PSW_AV, result, cpu_PSW_AV);
    /* calc SAV */
    tcg_gen_or_tl(cpu_PSW_SAV, cpu_PSW_SAV, cpu_PSW_AV);
    /* write back result */
    tcg_gen_mov_tl(ret, result);

    tcg_temp_free(result);
    tcg_temp_free(t0);
}

static inline void gen_addi_d(TCGv ret, TCGv r1, target_ulong r2)
{
    TCGv temp = tcg_const_i32(r2);
    gen_add_d(ret, r1, temp);
    tcg_temp_free(temp);
}

static inline void gen_cond_add(TCGCond cond, TCGv r1, TCGv r2, TCGv r3,
                                TCGv r4)
{
    TCGv temp = tcg_temp_new();
    TCGv temp2 = tcg_temp_new();
    TCGv result = tcg_temp_new();
    TCGv mask = tcg_temp_new();
    TCGv t0 = tcg_const_i32(0);

    /* create mask for sticky bits */
    tcg_gen_setcond_tl(cond, mask, r4, t0);
    tcg_gen_shli_tl(mask, mask, 31);

    tcg_gen_add_tl(result, r1, r2);
    /* Calc PSW_V */
    tcg_gen_xor_tl(temp, result, r1);
    tcg_gen_xor_tl(temp2, r1, r2);
    tcg_gen_andc_tl(temp, temp, temp2);
    tcg_gen_movcond_tl(cond, cpu_PSW_V, r4, t0, temp, cpu_PSW_V);
    /* Set PSW_SV */
    tcg_gen_and_tl(temp, temp, mask);
    tcg_gen_or_tl(cpu_PSW_SV, temp, cpu_PSW_SV);
    /* calc AV bit */
    tcg_gen_add_tl(temp, result, result);
    tcg_gen_xor_tl(temp, temp, result);
    tcg_gen_movcond_tl(cond, cpu_PSW_AV, r4, t0, temp, cpu_PSW_AV);
    /* calc SAV bit */
    tcg_gen_and_tl(temp, temp, mask);
    tcg_gen_or_tl(cpu_PSW_SAV, temp, cpu_PSW_SAV);
    /* write back result */
    tcg_gen_movcond_tl(cond, r3, r4, t0, result, r3);

    tcg_temp_free(t0);
    tcg_temp_free(temp);
    tcg_temp_free(temp2);
    tcg_temp_free(result);
    tcg_temp_free(mask);
}

static inline void gen_condi_add(TCGCond cond, TCGv r1, int32_t r2,
                                 TCGv r3, TCGv r4)
{
    TCGv temp = tcg_const_i32(r2);
    gen_cond_add(cond, r1, temp, r3, r4);
    tcg_temp_free(temp);
}

static inline void gen_sub_d(TCGv ret, TCGv r1, TCGv r2)
{
    TCGv temp = tcg_temp_new_i32();
    TCGv result = tcg_temp_new_i32();

    tcg_gen_sub_tl(result, r1, r2);
    /* calc V bit */
    tcg_gen_xor_tl(cpu_PSW_V, result, r1);
    tcg_gen_xor_tl(temp, r1, r2);
    tcg_gen_and_tl(cpu_PSW_V, cpu_PSW_V, temp);
    /* calc SV bit */
    tcg_gen_or_tl(cpu_PSW_SV, cpu_PSW_SV, cpu_PSW_V);
    /* Calc AV bit */
    tcg_gen_add_tl(cpu_PSW_AV, result, result);
    tcg_gen_xor_tl(cpu_PSW_AV, result, cpu_PSW_AV);
    /* calc SAV bit */
    tcg_gen_or_tl(cpu_PSW_SAV, cpu_PSW_SAV, cpu_PSW_AV);
    /* write back result */
    tcg_gen_mov_tl(ret, result);

    tcg_temp_free(temp);
    tcg_temp_free(result);
}

static inline void gen_mul_i32s(TCGv ret, TCGv r1, TCGv r2)
{
    TCGv high = tcg_temp_new();
    TCGv low = tcg_temp_new();

    tcg_gen_muls2_tl(low, high, r1, r2);
    tcg_gen_mov_tl(ret, low);
    /* calc V bit */
    tcg_gen_sari_tl(low, low, 31);
    tcg_gen_setcond_tl(TCG_COND_NE, cpu_PSW_V, high, low);
    tcg_gen_shli_tl(cpu_PSW_V, cpu_PSW_V, 31);
    /* calc SV bit */
    tcg_gen_or_tl(cpu_PSW_SV, cpu_PSW_SV, cpu_PSW_V);
    /* Calc AV bit */
    tcg_gen_add_tl(cpu_PSW_AV, ret, ret);
    tcg_gen_xor_tl(cpu_PSW_AV, ret, cpu_PSW_AV);
    /* calc SAV bit */
    tcg_gen_or_tl(cpu_PSW_SAV, cpu_PSW_SAV, cpu_PSW_AV);

    tcg_temp_free(high);
    tcg_temp_free(low);
}

static void gen_saturate(TCGv ret, TCGv arg, int32_t up, int32_t low)
{
    TCGv sat_neg = tcg_const_i32(low);
    TCGv temp = tcg_const_i32(up);

    /* sat_neg = (arg < low ) ? low : arg; */
    tcg_gen_movcond_tl(TCG_COND_LT, sat_neg, arg, sat_neg, sat_neg, arg);

    /* ret = (sat_neg > up ) ? up  : sat_neg; */
    tcg_gen_movcond_tl(TCG_COND_GT, ret, sat_neg, temp, temp, sat_neg);

    tcg_temp_free(sat_neg);
    tcg_temp_free(temp);
}

static void gen_saturate_u(TCGv ret, TCGv arg, int32_t up)
{
    TCGv temp = tcg_const_i32(up);
    /* sat_neg = (arg > up ) ? up : arg; */
    tcg_gen_movcond_tl(TCG_COND_GTU, ret, arg, temp, temp, arg);
    tcg_temp_free(temp);
}

static void gen_shi(TCGv ret, TCGv r1, int32_t shift_count)
{
    if (shift_count == -32) {
        tcg_gen_movi_tl(ret, 0);
    } else if (shift_count >= 0) {
        tcg_gen_shli_tl(ret, r1, shift_count);
    } else {
        tcg_gen_shri_tl(ret, r1, -shift_count);
    }
}

static void gen_shaci(TCGv ret, TCGv r1, int32_t shift_count)
{
    uint32_t msk, msk_start;
    TCGv temp = tcg_temp_new();
    TCGv temp2 = tcg_temp_new();
    TCGv t_0 = tcg_const_i32(0);

    if (shift_count == 0) {
        /* Clear PSW.C and PSW.V */
        tcg_gen_movi_tl(cpu_PSW_C, 0);
        tcg_gen_mov_tl(cpu_PSW_V, cpu_PSW_C);
        tcg_gen_mov_tl(ret, r1);
    } else if (shift_count == -32) {
        /* set PSW.C */
        tcg_gen_mov_tl(cpu_PSW_C, r1);
        /* fill ret completly with sign bit */
        tcg_gen_sari_tl(ret, r1, 31);
        /* clear PSW.V */
        tcg_gen_movi_tl(cpu_PSW_V, 0);
    } else if (shift_count > 0) {
        TCGv t_max = tcg_const_i32(0x7FFFFFFF >> shift_count);
        TCGv t_min = tcg_const_i32(((int32_t) -0x80000000) >> shift_count);

        /* calc carry */
        msk_start = 32 - shift_count;
        msk = ((1 << shift_count) - 1) << msk_start;
        tcg_gen_andi_tl(cpu_PSW_C, r1, msk);
        /* calc v/sv bits */
        tcg_gen_setcond_tl(TCG_COND_GT, temp, r1, t_max);
        tcg_gen_setcond_tl(TCG_COND_LT, temp2, r1, t_min);
        tcg_gen_or_tl(cpu_PSW_V, temp, temp2);
        tcg_gen_shli_tl(cpu_PSW_V, cpu_PSW_V, 31);
        /* calc sv */
        tcg_gen_or_tl(cpu_PSW_SV, cpu_PSW_V, cpu_PSW_SV);
        /* do shift */
        tcg_gen_shli_tl(ret, r1, shift_count);

        tcg_temp_free(t_max);
        tcg_temp_free(t_min);
    } else {
        /* clear PSW.V */
        tcg_gen_movi_tl(cpu_PSW_V, 0);
        /* calc carry */
        msk = (1 << -shift_count) - 1;
        tcg_gen_andi_tl(cpu_PSW_C, r1, msk);
        /* do shift */
        tcg_gen_sari_tl(ret, r1, -shift_count);
    }
    /* calc av overflow bit */
    tcg_gen_add_tl(cpu_PSW_AV, ret, ret);
    tcg_gen_xor_tl(cpu_PSW_AV, ret, cpu_PSW_AV);
    /* calc sav overflow bit */
    tcg_gen_or_tl(cpu_PSW_SAV, cpu_PSW_SAV, cpu_PSW_AV);

    tcg_temp_free(temp);
    tcg_temp_free(temp2);
    tcg_temp_free(t_0);
}

static inline void gen_adds(TCGv ret, TCGv r1, TCGv r2)
{
    gen_helper_add_ssov(ret, cpu_env, r1, r2);
}

static inline void gen_subs(TCGv ret, TCGv r1, TCGv r2)
{
    gen_helper_sub_ssov(ret, cpu_env, r1, r2);
}

/* helpers for generating program flow micro-ops */

static inline void gen_save_pc(target_ulong pc)
{
    tcg_gen_movi_tl(cpu_PC, pc);
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) &&
        likely(!ctx->singlestep_enabled)) {
        tcg_gen_goto_tb(n);
        gen_save_pc(dest);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        gen_save_pc(dest);
        if (ctx->singlestep_enabled) {
            /* raise exception debug */
        }
        tcg_gen_exit_tb(0);
    }
}

static inline void gen_branch_cond(DisasContext *ctx, TCGCond cond, TCGv r1,
                                   TCGv r2, int16_t address)
{
    int jumpLabel;
    jumpLabel = gen_new_label();
    tcg_gen_brcond_tl(cond, r1, r2, jumpLabel);

    gen_goto_tb(ctx, 1, ctx->next_pc);

    gen_set_label(jumpLabel);
    gen_goto_tb(ctx, 0, ctx->pc + address * 2);
}

static inline void gen_branch_condi(DisasContext *ctx, TCGCond cond, TCGv r1,
                                    int r2, int16_t address)
{
    TCGv temp = tcg_const_i32(r2);
    gen_branch_cond(ctx, cond, r1, temp, address);
    tcg_temp_free(temp);
}

static void gen_loop(DisasContext *ctx, int r1, int32_t offset)
{
    int l1;
    l1 = gen_new_label();

    tcg_gen_subi_tl(cpu_gpr_a[r1], cpu_gpr_a[r1], 1);
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr_a[r1], -1, l1);
    gen_goto_tb(ctx, 1, ctx->pc + offset);
    gen_set_label(l1);
    gen_goto_tb(ctx, 0, ctx->next_pc);
}

static void gen_compute_branch(DisasContext *ctx, uint32_t opc, int r1,
                               int r2 , int32_t constant , int32_t offset)
{
    TCGv temp;

    switch (opc) {
/* SB-format jumps */
    case OPC1_16_SB_J:
    case OPC1_32_B_J:
        gen_goto_tb(ctx, 0, ctx->pc + offset * 2);
        break;
    case OPC1_16_SB_CALL:
        gen_helper_1arg(call, ctx->next_pc);
        gen_goto_tb(ctx, 0, ctx->pc + offset * 2);
        break;
    case OPC1_16_SB_JZ:
        gen_branch_condi(ctx, TCG_COND_EQ, cpu_gpr_d[15], 0, offset);
        break;
    case OPC1_16_SB_JNZ:
        gen_branch_condi(ctx, TCG_COND_NE, cpu_gpr_d[15], 0, offset);
        break;
/* SBC-format jumps */
    case OPC1_16_SBC_JEQ:
        gen_branch_condi(ctx, TCG_COND_EQ, cpu_gpr_d[15], constant, offset);
        break;
    case OPC1_16_SBC_JNE:
        gen_branch_condi(ctx, TCG_COND_NE, cpu_gpr_d[15], constant, offset);
        break;
/* SBRN-format jumps */
    case OPC1_16_SBRN_JZ_T:
        temp = tcg_temp_new();
        tcg_gen_andi_tl(temp, cpu_gpr_d[15], 0x1u << constant);
        gen_branch_condi(ctx, TCG_COND_EQ, temp, 0, offset);
        tcg_temp_free(temp);
        break;
    case OPC1_16_SBRN_JNZ_T:
        temp = tcg_temp_new();
        tcg_gen_andi_tl(temp, cpu_gpr_d[15], 0x1u << constant);
        gen_branch_condi(ctx, TCG_COND_NE, temp, 0, offset);
        tcg_temp_free(temp);
        break;
/* SBR-format jumps */
    case OPC1_16_SBR_JEQ:
        gen_branch_cond(ctx, TCG_COND_EQ, cpu_gpr_d[r1], cpu_gpr_d[15],
                        offset);
        break;
    case OPC1_16_SBR_JNE:
        gen_branch_cond(ctx, TCG_COND_NE, cpu_gpr_d[r1], cpu_gpr_d[15],
                        offset);
        break;
    case OPC1_16_SBR_JNZ:
        gen_branch_condi(ctx, TCG_COND_NE, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JNZ_A:
        gen_branch_condi(ctx, TCG_COND_NE, cpu_gpr_a[r1], 0, offset);
        break;
    case OPC1_16_SBR_JGEZ:
        gen_branch_condi(ctx, TCG_COND_GE, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JGTZ:
        gen_branch_condi(ctx, TCG_COND_GT, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JLEZ:
        gen_branch_condi(ctx, TCG_COND_LE, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JLTZ:
        gen_branch_condi(ctx, TCG_COND_LT, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JZ:
        gen_branch_condi(ctx, TCG_COND_EQ, cpu_gpr_d[r1], 0, offset);
        break;
    case OPC1_16_SBR_JZ_A:
        gen_branch_condi(ctx, TCG_COND_EQ, cpu_gpr_a[r1], 0, offset);
        break;
    case OPC1_16_SBR_LOOP:
        gen_loop(ctx, r1, offset * 2 - 32);
        break;
/* SR-format jumps */
    case OPC1_16_SR_JI:
        tcg_gen_andi_tl(cpu_PC, cpu_gpr_a[r1], 0xfffffffe);
        tcg_gen_exit_tb(0);
        break;
    case OPC2_16_SR_RET:
        gen_helper_ret(cpu_env);
        tcg_gen_exit_tb(0);
        break;
    default:
        printf("Branch Error at %x\n", ctx->pc);
    }
    ctx->bstate = BS_BRANCH;
}


/*
 * Functions for decoding instructions
 */

static void decode_src_opc(DisasContext *ctx, int op1)
{
    int r1;
    int32_t const4;
    TCGv temp, temp2;

    r1 = MASK_OP_SRC_S1D(ctx->opcode);
    const4 = MASK_OP_SRC_CONST4_SEXT(ctx->opcode);

    switch (op1) {
    case OPC1_16_SRC_ADD:
        gen_addi_d(cpu_gpr_d[r1], cpu_gpr_d[r1], const4);
        break;
    case OPC1_16_SRC_ADD_A15:
        gen_addi_d(cpu_gpr_d[r1], cpu_gpr_d[15], const4);
        break;
    case OPC1_16_SRC_ADD_15A:
        gen_addi_d(cpu_gpr_d[15], cpu_gpr_d[r1], const4);
        break;
    case OPC1_16_SRC_ADD_A:
        tcg_gen_addi_tl(cpu_gpr_a[r1], cpu_gpr_a[r1], const4);
        break;
    case OPC1_16_SRC_CADD:
        gen_condi_add(TCG_COND_NE, cpu_gpr_d[r1], const4, cpu_gpr_d[r1],
                      cpu_gpr_d[15]);
        break;
    case OPC1_16_SRC_CADDN:
        gen_condi_add(TCG_COND_EQ, cpu_gpr_d[r1], const4, cpu_gpr_d[r1],
                      cpu_gpr_d[15]);
        break;
    case OPC1_16_SRC_CMOV:
        temp = tcg_const_tl(0);
        temp2 = tcg_const_tl(const4);
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr_d[r1], cpu_gpr_d[15], temp,
                           temp2, cpu_gpr_d[r1]);
        tcg_temp_free(temp);
        tcg_temp_free(temp2);
        break;
    case OPC1_16_SRC_CMOVN:
        temp = tcg_const_tl(0);
        temp2 = tcg_const_tl(const4);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_gpr_d[r1], cpu_gpr_d[15], temp,
                           temp2, cpu_gpr_d[r1]);
        tcg_temp_free(temp);
        tcg_temp_free(temp2);
        break;
    case OPC1_16_SRC_EQ:
        tcg_gen_setcondi_tl(TCG_COND_EQ, cpu_gpr_d[15], cpu_gpr_d[r1],
                            const4);
        break;
    case OPC1_16_SRC_LT:
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_gpr_d[15], cpu_gpr_d[r1],
                            const4);
        break;
    case OPC1_16_SRC_MOV:
        tcg_gen_movi_tl(cpu_gpr_d[r1], const4);
        break;
    case OPC1_16_SRC_MOV_A:
        const4 = MASK_OP_SRC_CONST4(ctx->opcode);
        tcg_gen_movi_tl(cpu_gpr_a[r1], const4);
        break;
    case OPC1_16_SRC_SH:
        gen_shi(cpu_gpr_d[r1], cpu_gpr_d[r1], const4);
        break;
    case OPC1_16_SRC_SHA:
        gen_shaci(cpu_gpr_d[r1], cpu_gpr_d[r1], const4);
        break;
    }
}

static void decode_srr_opc(DisasContext *ctx, int op1)
{
    int r1, r2;
    TCGv temp;

    r1 = MASK_OP_SRR_S1D(ctx->opcode);
    r2 = MASK_OP_SRR_S2(ctx->opcode);

    switch (op1) {
    case OPC1_16_SRR_ADD:
        gen_add_d(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_ADD_A15:
        gen_add_d(cpu_gpr_d[r1], cpu_gpr_d[15], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_ADD_15A:
        gen_add_d(cpu_gpr_d[15], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_ADD_A:
        tcg_gen_add_tl(cpu_gpr_a[r1], cpu_gpr_a[r1], cpu_gpr_a[r2]);
        break;
    case OPC1_16_SRR_ADDS:
        gen_adds(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_AND:
        tcg_gen_and_tl(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_CMOV:
        temp = tcg_const_tl(0);
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr_d[r1], cpu_gpr_d[15], temp,
                           cpu_gpr_d[r2], cpu_gpr_d[r1]);
        tcg_temp_free(temp);
        break;
    case OPC1_16_SRR_CMOVN:
        temp = tcg_const_tl(0);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_gpr_d[r1], cpu_gpr_d[15], temp,
                           cpu_gpr_d[r2], cpu_gpr_d[r1]);
        tcg_temp_free(temp);
        break;
    case OPC1_16_SRR_EQ:
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_gpr_d[15], cpu_gpr_d[r1],
                           cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_LT:
        tcg_gen_setcond_tl(TCG_COND_LT, cpu_gpr_d[15], cpu_gpr_d[r1],
                           cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_MOV:
        tcg_gen_mov_tl(cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_MOV_A:
        tcg_gen_mov_tl(cpu_gpr_a[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_MOV_AA:
        tcg_gen_mov_tl(cpu_gpr_a[r1], cpu_gpr_a[r2]);
        break;
    case OPC1_16_SRR_MOV_D:
        tcg_gen_mov_tl(cpu_gpr_d[r1], cpu_gpr_a[r2]);
        break;
    case OPC1_16_SRR_MUL:
        gen_mul_i32s(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_OR:
        tcg_gen_or_tl(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_SUB:
        gen_sub_d(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_SUB_A15B:
        gen_sub_d(cpu_gpr_d[r1], cpu_gpr_d[15], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_SUB_15AB:
        gen_sub_d(cpu_gpr_d[15], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_SUBS:
        gen_subs(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    case OPC1_16_SRR_XOR:
        tcg_gen_xor_tl(cpu_gpr_d[r1], cpu_gpr_d[r1], cpu_gpr_d[r2]);
        break;
    }
}

static void decode_ssr_opc(DisasContext *ctx, int op1)
{
    int r1, r2;

    r1 = MASK_OP_SSR_S1(ctx->opcode);
    r2 = MASK_OP_SSR_S2(ctx->opcode);

    switch (op1) {
    case OPC1_16_SSR_ST_A:
        tcg_gen_qemu_st_tl(cpu_gpr_a[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUL);
        break;
    case OPC1_16_SSR_ST_A_POSTINC:
        tcg_gen_qemu_st_tl(cpu_gpr_a[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUL);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 4);
        break;
    case OPC1_16_SSR_ST_B:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_UB);
        break;
    case OPC1_16_SSR_ST_B_POSTINC:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_UB);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 1);
        break;
    case OPC1_16_SSR_ST_H:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUW);
        break;
    case OPC1_16_SSR_ST_H_POSTINC:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUW);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 2);
        break;
    case OPC1_16_SSR_ST_W:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUL);
        break;
    case OPC1_16_SSR_ST_W_POSTINC:
        tcg_gen_qemu_st_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LEUL);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 4);
        break;
    }
}

static void decode_sc_opc(DisasContext *ctx, int op1)
{
    int32_t const16;

    const16 = MASK_OP_SC_CONST8(ctx->opcode);

    switch (op1) {
    case OPC1_16_SC_AND:
        tcg_gen_andi_tl(cpu_gpr_d[15], cpu_gpr_d[15], const16);
        break;
    case OPC1_16_SC_BISR:
        gen_helper_1arg(bisr, const16 & 0xff);
        break;
    case OPC1_16_SC_LD_A:
        gen_offset_ld(ctx, cpu_gpr_a[15], cpu_gpr_a[10], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SC_LD_W:
        gen_offset_ld(ctx, cpu_gpr_d[15], cpu_gpr_a[10], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SC_MOV:
        tcg_gen_movi_tl(cpu_gpr_d[15], const16);
        break;
    case OPC1_16_SC_OR:
        tcg_gen_ori_tl(cpu_gpr_d[15], cpu_gpr_d[15], const16);
        break;
    case OPC1_16_SC_ST_A:
        gen_offset_st(ctx, cpu_gpr_a[15], cpu_gpr_a[10], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SC_ST_W:
        gen_offset_st(ctx, cpu_gpr_d[15], cpu_gpr_a[10], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SC_SUB_A:
        tcg_gen_subi_tl(cpu_gpr_a[10], cpu_gpr_a[10], const16);
        break;
    }
}

static void decode_slr_opc(DisasContext *ctx, int op1)
{
    int r1, r2;

    r1 = MASK_OP_SLR_D(ctx->opcode);
    r2 = MASK_OP_SLR_S2(ctx->opcode);

    switch (op1) {
/* SLR-format */
    case OPC1_16_SLR_LD_A:
        tcg_gen_qemu_ld_tl(cpu_gpr_a[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESL);
        break;
    case OPC1_16_SLR_LD_A_POSTINC:
        tcg_gen_qemu_ld_tl(cpu_gpr_a[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESL);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 4);
        break;
    case OPC1_16_SLR_LD_BU:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_UB);
        break;
    case OPC1_16_SLR_LD_BU_POSTINC:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_UB);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 1);
        break;
    case OPC1_16_SLR_LD_H:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESW);
        break;
    case OPC1_16_SLR_LD_H_POSTINC:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESW);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 2);
        break;
    case OPC1_16_SLR_LD_W:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESW);
        break;
    case OPC1_16_SLR_LD_W_POSTINC:
        tcg_gen_qemu_ld_tl(cpu_gpr_d[r1], cpu_gpr_a[r2], ctx->mem_idx, MO_LESW);
        tcg_gen_addi_tl(cpu_gpr_a[r2], cpu_gpr_a[r2], 4);
        break;
    }
}

static void decode_sro_opc(DisasContext *ctx, int op1)
{
    int r2;
    int32_t address;

    r2 = MASK_OP_SRO_S2(ctx->opcode);
    address = MASK_OP_SRO_OFF4(ctx->opcode);

/* SRO-format */
    switch (op1) {
    case OPC1_16_SRO_LD_A:
        gen_offset_ld(ctx, cpu_gpr_a[15], cpu_gpr_a[r2], address * 4, MO_LESL);
        break;
    case OPC1_16_SRO_LD_BU:
        gen_offset_ld(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address, MO_UB);
        break;
    case OPC1_16_SRO_LD_H:
        gen_offset_ld(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address, MO_LESW);
        break;
    case OPC1_16_SRO_LD_W:
        gen_offset_ld(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address * 4, MO_LESL);
        break;
    case OPC1_16_SRO_ST_A:
        gen_offset_st(ctx, cpu_gpr_a[15], cpu_gpr_a[r2], address * 4, MO_LESL);
        break;
    case OPC1_16_SRO_ST_B:
        gen_offset_st(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address, MO_UB);
        break;
    case OPC1_16_SRO_ST_H:
        gen_offset_st(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address * 2, MO_LESW);
        break;
    case OPC1_16_SRO_ST_W:
        gen_offset_st(ctx, cpu_gpr_d[15], cpu_gpr_a[r2], address * 4, MO_LESL);
        break;
    }
}

static void decode_sr_system(CPUTriCoreState *env, DisasContext *ctx)
{
    uint32_t op2;
    op2 = MASK_OP_SR_OP2(ctx->opcode);

    switch (op2) {
    case OPC2_16_SR_NOP:
        break;
    case OPC2_16_SR_RET:
        gen_compute_branch(ctx, op2, 0, 0, 0, 0);
        break;
    case OPC2_16_SR_RFE:
        gen_helper_rfe(cpu_env);
        tcg_gen_exit_tb(0);
        ctx->bstate = BS_BRANCH;
        break;
    case OPC2_16_SR_DEBUG:
        /* raise EXCP_DEBUG */
        break;
    }
}

static void decode_sr_accu(CPUTriCoreState *env, DisasContext *ctx)
{
    uint32_t op2;
    uint32_t r1;
    TCGv temp;

    r1 = MASK_OP_SR_S1D(ctx->opcode);
    op2 = MASK_OP_SR_OP2(ctx->opcode);

    switch (op2) {
    case OPC2_16_SR_RSUB:
        /* overflow only if r1 = -0x80000000 */
        temp = tcg_const_i32(-0x80000000);
        /* calc V bit */
        tcg_gen_setcond_tl(TCG_COND_EQ, cpu_PSW_V, cpu_gpr_d[r1], temp);
        tcg_gen_shli_tl(cpu_PSW_V, cpu_PSW_V, 31);
        /* calc SV bit */
        tcg_gen_or_tl(cpu_PSW_SV, cpu_PSW_SV, cpu_PSW_V);
        /* sub */
        tcg_gen_neg_tl(cpu_gpr_d[r1], cpu_gpr_d[r1]);
        /* calc av */
        tcg_gen_add_tl(cpu_PSW_AV, cpu_gpr_d[r1], cpu_gpr_d[r1]);
        tcg_gen_xor_tl(cpu_PSW_AV, cpu_gpr_d[r1], cpu_PSW_AV);
        /* calc sav */
        tcg_gen_or_tl(cpu_PSW_SAV, cpu_PSW_SAV, cpu_PSW_AV);
        tcg_temp_free(temp);
        break;
    case OPC2_16_SR_SAT_B:
        gen_saturate(cpu_gpr_d[r1], cpu_gpr_d[r1], 0x7f, -0x80);
        break;
    case OPC2_16_SR_SAT_BU:
        gen_saturate_u(cpu_gpr_d[r1], cpu_gpr_d[r1], 0xff);
        break;
    case OPC2_16_SR_SAT_H:
        gen_saturate(cpu_gpr_d[r1], cpu_gpr_d[r1], 0x7fff, -0x8000);
        break;
    case OPC2_16_SR_SAT_HU:
        gen_saturate_u(cpu_gpr_d[r1], cpu_gpr_d[r1], 0xffff);
        break;
    }
}

static void decode_16Bit_opc(CPUTriCoreState *env, DisasContext *ctx)
{
    int op1;
    int r1, r2;
    int32_t const16;
    int32_t address;
    TCGv temp;

    op1 = MASK_OP_MAJOR(ctx->opcode);

    /* handle ADDSC.A opcode only being 6 bit long */
    if (unlikely((op1 & 0x3f) == OPC1_16_SRRS_ADDSC_A)) {
        op1 = OPC1_16_SRRS_ADDSC_A;
    }

    switch (op1) {
    case OPC1_16_SRC_ADD:
    case OPC1_16_SRC_ADD_A15:
    case OPC1_16_SRC_ADD_15A:
    case OPC1_16_SRC_ADD_A:
    case OPC1_16_SRC_CADD:
    case OPC1_16_SRC_CADDN:
    case OPC1_16_SRC_CMOV:
    case OPC1_16_SRC_CMOVN:
    case OPC1_16_SRC_EQ:
    case OPC1_16_SRC_LT:
    case OPC1_16_SRC_MOV:
    case OPC1_16_SRC_MOV_A:
    case OPC1_16_SRC_SH:
    case OPC1_16_SRC_SHA:
        decode_src_opc(ctx, op1);
        break;
/* SRR-format */
    case OPC1_16_SRR_ADD:
    case OPC1_16_SRR_ADD_A15:
    case OPC1_16_SRR_ADD_15A:
    case OPC1_16_SRR_ADD_A:
    case OPC1_16_SRR_ADDS:
    case OPC1_16_SRR_AND:
    case OPC1_16_SRR_CMOV:
    case OPC1_16_SRR_CMOVN:
    case OPC1_16_SRR_EQ:
    case OPC1_16_SRR_LT:
    case OPC1_16_SRR_MOV:
    case OPC1_16_SRR_MOV_A:
    case OPC1_16_SRR_MOV_AA:
    case OPC1_16_SRR_MOV_D:
    case OPC1_16_SRR_MUL:
    case OPC1_16_SRR_OR:
    case OPC1_16_SRR_SUB:
    case OPC1_16_SRR_SUB_A15B:
    case OPC1_16_SRR_SUB_15AB:
    case OPC1_16_SRR_SUBS:
    case OPC1_16_SRR_XOR:
        decode_srr_opc(ctx, op1);
        break;
/* SSR-format */
    case OPC1_16_SSR_ST_A:
    case OPC1_16_SSR_ST_A_POSTINC:
    case OPC1_16_SSR_ST_B:
    case OPC1_16_SSR_ST_B_POSTINC:
    case OPC1_16_SSR_ST_H:
    case OPC1_16_SSR_ST_H_POSTINC:
    case OPC1_16_SSR_ST_W:
    case OPC1_16_SSR_ST_W_POSTINC:
        decode_ssr_opc(ctx, op1);
        break;
/* SRRS-format */
    case OPC1_16_SRRS_ADDSC_A:
        r2 = MASK_OP_SRRS_S2(ctx->opcode);
        r1 = MASK_OP_SRRS_S1D(ctx->opcode);
        const16 = MASK_OP_SRRS_N(ctx->opcode);
        temp = tcg_temp_new();
        tcg_gen_shli_tl(temp, cpu_gpr_d[15], const16);
        tcg_gen_add_tl(cpu_gpr_a[r1], cpu_gpr_a[r2], temp);
        tcg_temp_free(temp);
        break;
/* SLRO-format */
    case OPC1_16_SLRO_LD_A:
        r1 = MASK_OP_SLRO_D(ctx->opcode);
        const16 = MASK_OP_SLRO_OFF4(ctx->opcode);
        gen_offset_ld(ctx, cpu_gpr_a[r1], cpu_gpr_a[15], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SLRO_LD_BU:
        r1 = MASK_OP_SLRO_D(ctx->opcode);
        const16 = MASK_OP_SLRO_OFF4(ctx->opcode);
        gen_offset_ld(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16, MO_UB);
        break;
    case OPC1_16_SLRO_LD_H:
        r1 = MASK_OP_SLRO_D(ctx->opcode);
        const16 = MASK_OP_SLRO_OFF4(ctx->opcode);
        gen_offset_ld(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16 * 2, MO_LESW);
        break;
    case OPC1_16_SLRO_LD_W:
        r1 = MASK_OP_SLRO_D(ctx->opcode);
        const16 = MASK_OP_SLRO_OFF4(ctx->opcode);
        gen_offset_ld(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16 * 4, MO_LESL);
        break;
/* SB-format */
    case OPC1_16_SB_CALL:
    case OPC1_16_SB_J:
    case OPC1_16_SB_JNZ:
    case OPC1_16_SB_JZ:
        address = MASK_OP_SB_DISP8_SEXT(ctx->opcode);
        gen_compute_branch(ctx, op1, 0, 0, 0, address);
        break;
/* SBC-format */
    case OPC1_16_SBC_JEQ:
    case OPC1_16_SBC_JNE:
        address = MASK_OP_SBC_DISP4(ctx->opcode);
        const16 = MASK_OP_SBC_CONST4_SEXT(ctx->opcode);
        gen_compute_branch(ctx, op1, 0, 0, const16, address);
        break;
/* SBRN-format */
    case OPC1_16_SBRN_JNZ_T:
    case OPC1_16_SBRN_JZ_T:
        address = MASK_OP_SBRN_DISP4(ctx->opcode);
        const16 = MASK_OP_SBRN_N(ctx->opcode);
        gen_compute_branch(ctx, op1, 0, 0, const16, address);
        break;
/* SBR-format */
    case OPC1_16_SBR_JEQ:
    case OPC1_16_SBR_JGEZ:
    case OPC1_16_SBR_JGTZ:
    case OPC1_16_SBR_JLEZ:
    case OPC1_16_SBR_JLTZ:
    case OPC1_16_SBR_JNE:
    case OPC1_16_SBR_JNZ:
    case OPC1_16_SBR_JNZ_A:
    case OPC1_16_SBR_JZ:
    case OPC1_16_SBR_JZ_A:
    case OPC1_16_SBR_LOOP:
        r1 = MASK_OP_SBR_S2(ctx->opcode);
        address = MASK_OP_SBR_DISP4(ctx->opcode);
        gen_compute_branch(ctx, op1, r1, 0, 0, address);
        break;
/* SC-format */
    case OPC1_16_SC_AND:
    case OPC1_16_SC_BISR:
    case OPC1_16_SC_LD_A:
    case OPC1_16_SC_LD_W:
    case OPC1_16_SC_MOV:
    case OPC1_16_SC_OR:
    case OPC1_16_SC_ST_A:
    case OPC1_16_SC_ST_W:
    case OPC1_16_SC_SUB_A:
        decode_sc_opc(ctx, op1);
        break;
/* SLR-format */
    case OPC1_16_SLR_LD_A:
    case OPC1_16_SLR_LD_A_POSTINC:
    case OPC1_16_SLR_LD_BU:
    case OPC1_16_SLR_LD_BU_POSTINC:
    case OPC1_16_SLR_LD_H:
    case OPC1_16_SLR_LD_H_POSTINC:
    case OPC1_16_SLR_LD_W:
    case OPC1_16_SLR_LD_W_POSTINC:
        decode_slr_opc(ctx, op1);
        break;
/* SRO-format */
    case OPC1_16_SRO_LD_A:
    case OPC1_16_SRO_LD_BU:
    case OPC1_16_SRO_LD_H:
    case OPC1_16_SRO_LD_W:
    case OPC1_16_SRO_ST_A:
    case OPC1_16_SRO_ST_B:
    case OPC1_16_SRO_ST_H:
    case OPC1_16_SRO_ST_W:
        decode_sro_opc(ctx, op1);
        break;
/* SSRO-format */
    case OPC1_16_SSRO_ST_A:
        r1 = MASK_OP_SSRO_S1(ctx->opcode);
        const16 = MASK_OP_SSRO_OFF4(ctx->opcode);
        gen_offset_st(ctx, cpu_gpr_a[r1], cpu_gpr_a[15], const16 * 4, MO_LESL);
        break;
    case OPC1_16_SSRO_ST_B:
        r1 = MASK_OP_SSRO_S1(ctx->opcode);
        const16 = MASK_OP_SSRO_OFF4(ctx->opcode);
        gen_offset_st(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16, MO_UB);
        break;
    case OPC1_16_SSRO_ST_H:
        r1 = MASK_OP_SSRO_S1(ctx->opcode);
        const16 = MASK_OP_SSRO_OFF4(ctx->opcode);
        gen_offset_st(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16 * 2, MO_LESW);
        break;
    case OPC1_16_SSRO_ST_W:
        r1 = MASK_OP_SSRO_S1(ctx->opcode);
        const16 = MASK_OP_SSRO_OFF4(ctx->opcode);
        gen_offset_st(ctx, cpu_gpr_d[r1], cpu_gpr_a[15], const16 * 4, MO_LESL);
        break;
/* SR-format */
    case OPCM_16_SR_SYSTEM:
        decode_sr_system(env, ctx);
        break;
    case OPCM_16_SR_ACCU:
        decode_sr_accu(env, ctx);
        break;
    case OPC1_16_SR_JI:
        r1 = MASK_OP_SR_S1D(ctx->opcode);
        gen_compute_branch(ctx, op1, r1, 0, 0, 0);
        break;
    case OPC1_16_SR_NOT:
        r1 = MASK_OP_SR_S1D(ctx->opcode);
        tcg_gen_not_tl(cpu_gpr_d[r1], cpu_gpr_d[r1]);
        break;
    }
}

static void decode_32Bit_opc(CPUTriCoreState *env, DisasContext *ctx)
{
}

static void decode_opc(CPUTriCoreState *env, DisasContext *ctx, int *is_branch)
{
    /* 16-Bit Instruction */
    if ((ctx->opcode & 0x1) == 0) {
        ctx->next_pc = ctx->pc + 2;
        decode_16Bit_opc(env, ctx);
    /* 32-Bit Instruction */
    } else {
        ctx->next_pc = ctx->pc + 4;
        decode_32Bit_opc(env, ctx);
    }
}

static inline void
gen_intermediate_code_internal(TriCoreCPU *cpu, struct TranslationBlock *tb,
                              int search_pc)
{
    CPUState *cs = CPU(cpu);
    CPUTriCoreState *env = &cpu->env;
    DisasContext ctx;
    target_ulong pc_start;
    int num_insns;
    uint16_t *gen_opc_end;

    if (search_pc) {
        qemu_log("search pc %d\n", search_pc);
    }

    num_insns = 0;
    pc_start = tb->pc;
    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.tb = tb;
    ctx.singlestep_enabled = cs->singlestep_enabled;
    ctx.bstate = BS_NONE;
    ctx.mem_idx = cpu_mmu_index(env);

    tcg_clear_temp_count();
    gen_tb_start();
    while (ctx.bstate == BS_NONE) {
        ctx.opcode = cpu_ldl_code(env, ctx.pc);
        decode_opc(env, &ctx, 0);

        num_insns++;

        if (tcg_ctx.gen_opc_ptr >= gen_opc_end) {
            gen_save_pc(ctx.next_pc);
            tcg_gen_exit_tb(0);
            break;
        }
        if (singlestep) {
            gen_save_pc(ctx.next_pc);
            tcg_gen_exit_tb(0);
            break;
        }
        ctx.pc = ctx.next_pc;
    }

    gen_tb_end(tb, num_insns);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        printf("done_generating search pc\n");
    } else {
        tb->size = ctx.pc - pc_start;
        tb->icount = num_insns;
    }
    if (tcg_check_temp_count()) {
        printf("LEAK at %08x\n", env->PC);
    }

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(env, pc_start, ctx.pc - pc_start, 0);
        qemu_log("\n");
    }
#endif
}

void
gen_intermediate_code(CPUTriCoreState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(tricore_env_get_cpu(env), tb, false);
}

void
gen_intermediate_code_pc(CPUTriCoreState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(tricore_env_get_cpu(env), tb, true);
}

void
restore_state_to_opc(CPUTriCoreState *env, TranslationBlock *tb, int pc_pos)
{
    env->PC = tcg_ctx.gen_opc_pc[pc_pos];
}
/*
 *
 * Initialization
 *
 */

void cpu_state_reset(CPUTriCoreState *env)
{
    /* Reset Regs to Default Value */
    env->PSW = 0xb80;
}

static void tricore_tcg_init_csfr(void)
{
    cpu_PCXI = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUTriCoreState, PCXI), "PCXI");
    cpu_PSW = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUTriCoreState, PSW), "PSW");
    cpu_PC = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUTriCoreState, PC), "PC");
    cpu_ICR = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUTriCoreState, ICR), "ICR");
}

void tricore_tcg_init(void)
{
    int i;
    static int inited;
    if (inited) {
        return;
    }
    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    /* reg init */
    for (i = 0 ; i < 16 ; i++) {
        cpu_gpr_a[i] = tcg_global_mem_new(TCG_AREG0,
                                          offsetof(CPUTriCoreState, gpr_a[i]),
                                          regnames_a[i]);
    }
    for (i = 0 ; i < 16 ; i++) {
        cpu_gpr_d[i] = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUTriCoreState, gpr_d[i]),
                                           regnames_d[i]);
    }
    tricore_tcg_init_csfr();
    /* init PSW flag cache */
    cpu_PSW_C = tcg_global_mem_new(TCG_AREG0,
                                   offsetof(CPUTriCoreState, PSW_USB_C),
                                   "PSW_C");
    cpu_PSW_V = tcg_global_mem_new(TCG_AREG0,
                                   offsetof(CPUTriCoreState, PSW_USB_V),
                                   "PSW_V");
    cpu_PSW_SV = tcg_global_mem_new(TCG_AREG0,
                                    offsetof(CPUTriCoreState, PSW_USB_SV),
                                    "PSW_SV");
    cpu_PSW_AV = tcg_global_mem_new(TCG_AREG0,
                                    offsetof(CPUTriCoreState, PSW_USB_AV),
                                    "PSW_AV");
    cpu_PSW_SAV = tcg_global_mem_new(TCG_AREG0,
                                     offsetof(CPUTriCoreState, PSW_USB_SAV),
                                     "PSW_SAV");
}
