/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"

#include "instmap.h"

/* global register indices */
static TCGv cpu_gpr[32], cpu_pc;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv load_res;
static TCGv load_val;

#include "exec/gen-icount.h"

typedef struct DisasContext {
    DisasContextBase base;
    /* pc_succ_insn points to the instruction following base.pc_next */
    target_ulong pc_succ_insn;
    uint32_t opcode;
    uint32_t flags;
    uint32_t mem_idx;
    /* Remember the rounding mode encoded in the previous fp instruction,
       which we have already installed into env->fp_status.  Or -1 for
       no previous fp instruction.  Note that we exit the TB when writing
       to any system register, which includes CSR_FRM, so we do not have
       to reset this known value.  */
    int frm;
} DisasContext;

/* convert riscv funct3 to qemu memop for load/store */
static const int tcg_memop_lookup[8] = {
    [0 ... 7] = -1,
    [0] = MO_SB,
    [1] = MO_TESW,
    [2] = MO_TESL,
    [4] = MO_UB,
    [5] = MO_TEUW,
#ifdef TARGET_RISCV64
    [3] = MO_TEQ,
    [6] = MO_TEUL,
#endif
};

#ifdef TARGET_RISCV64
#define CASE_OP_32_64(X) case X: case glue(X, W)
#else
#define CASE_OP_32_64(X) case X
#endif

static void generate_exception(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void generate_exception_mbadaddr(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    tcg_gen_st_tl(cpu_pc, cpu_env, offsetof(CPURISCVState, badaddr));
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_debug(void)
{
    TCGv_i32 helper_tmp = tcg_const_i32(EXCP_DEBUG);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
}

static void gen_exception_illegal(DisasContext *ctx)
{
    generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
}

static void gen_exception_inst_addr_mis(DisasContext *ctx)
{
    generate_exception_mbadaddr(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    if (unlikely(ctx->base.singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (ctx->base.tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        /* chaining is only allowed when the jump is to the same page */
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (ctx->base.singlestep_enabled) {
            gen_exception_debug();
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
    }
}

/* Wrapper for getting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated
 */
static inline void gen_get_gpr(TCGv t, int reg_num)
{
    if (reg_num == 0) {
        tcg_gen_movi_tl(t, 0);
    } else {
        tcg_gen_mov_tl(t, cpu_gpr[reg_num]);
    }
}

/* Wrapper for setting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated. this is more for safety purposes,
 * since we usually avoid calling the OP_TYPE_gen function if we see a write to
 * $zero
 */
static inline void gen_set_gpr(int reg_num_dst, TCGv t)
{
    if (reg_num_dst != 0) {
        tcg_gen_mov_tl(cpu_gpr[reg_num_dst], t);
    }
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static void gen_fsgnj(DisasContext *ctx, uint32_t rd, uint32_t rs1,
    uint32_t rs2, int rm, uint64_t min)
{
    switch (rm) {
    case 0: /* fsgnj */
        if (rs1 == rs2) { /* FMOV */
            tcg_gen_mov_i64(cpu_fpr[rd], cpu_fpr[rs1]);
        } else {
            tcg_gen_deposit_i64(cpu_fpr[rd], cpu_fpr[rs2], cpu_fpr[rs1],
                                0, min == INT32_MIN ? 31 : 63);
        }
        break;
    case 1: /* fsgnjn */
        if (rs1 == rs2) { /* FNEG */
            tcg_gen_xori_i64(cpu_fpr[rd], cpu_fpr[rs1], min);
        } else {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_not_i64(t0, cpu_fpr[rs2]);
            tcg_gen_deposit_i64(cpu_fpr[rd], t0, cpu_fpr[rs1],
                                0, min == INT32_MIN ? 31 : 63);
            tcg_temp_free_i64(t0);
        }
        break;
    case 2: /* fsgnjx */
        if (rs1 == rs2) { /* FABS */
            tcg_gen_andi_i64(cpu_fpr[rd], cpu_fpr[rs1], ~min);
        } else {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_andi_i64(t0, cpu_fpr[rs2], min);
            tcg_gen_xor_i64(cpu_fpr[rd], cpu_fpr[rs1], t0);
            tcg_temp_free_i64(t0);
        }
        break;
    default:
        gen_exception_illegal(ctx);
    }
}

static void gen_arith(DisasContext *ctx, uint32_t opc, int rd, int rs1,
        int rs2)
{
    TCGv source1, source2, cond1, cond2, zeroreg, resultopt1;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();
    gen_get_gpr(source1, rs1);
    gen_get_gpr(source2, rs2);

    switch (opc) {
    CASE_OP_32_64(OPC_RISC_ADD):
        tcg_gen_add_tl(source1, source1, source2);
        break;
    CASE_OP_32_64(OPC_RISC_SUB):
        tcg_gen_sub_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SLLW:
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_shl_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SLL:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_shl_tl(source1, source1, source2);
        break;
    case OPC_RISC_SLT:
        tcg_gen_setcond_tl(TCG_COND_LT, source1, source1, source2);
        break;
    case OPC_RISC_SLTU:
        tcg_gen_setcond_tl(TCG_COND_LTU, source1, source1, source2);
        break;
    case OPC_RISC_XOR:
        tcg_gen_xor_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SRLW:
        /* clear upper 32 */
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_shr_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SRL:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_shr_tl(source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SRAW:
        /* first, trick to get it to act like working on 32 bits (get rid of
        upper 32, sign extend to fill space) */
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_andi_tl(source2, source2, 0x1F);
        tcg_gen_sar_tl(source1, source1, source2);
        break;
#endif
    case OPC_RISC_SRA:
        tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
        tcg_gen_sar_tl(source1, source1, source2);
        break;
    case OPC_RISC_OR:
        tcg_gen_or_tl(source1, source1, source2);
        break;
    case OPC_RISC_AND:
        tcg_gen_and_tl(source1, source1, source2);
        break;
    CASE_OP_32_64(OPC_RISC_MUL):
        tcg_gen_mul_tl(source1, source1, source2);
        break;
    case OPC_RISC_MULH:
        tcg_gen_muls2_tl(source2, source1, source1, source2);
        break;
    case OPC_RISC_MULHSU:
        gen_mulhsu(source1, source1, source2);
        break;
    case OPC_RISC_MULHU:
        tcg_gen_mulu2_tl(source2, source1, source1, source2);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_DIVW:
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_ext32s_tl(source2, source2);
        /* fall through to DIV */
#endif
    case OPC_RISC_DIV:
        /* Handle by altering args to tcg_gen_div to produce req'd results:
         * For overflow: want source1 in source1 and 1 in source2
         * For div by zero: want -1 in source1 and 1 in source2 -> -1 result */
        cond1 = tcg_temp_new();
        cond2 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)(~0L));
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                            ((target_ulong)1) << (TARGET_LONG_BITS - 1));
        tcg_gen_and_tl(cond1, cond1, cond2); /* cond1 = overflow */
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, 0); /* cond2 = div 0 */
        /* if div by zero, set source1 to -1, otherwise don't change */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond2, zeroreg, source1,
                resultopt1);
        /* if overflow or div by zero, set source2 to 1, else don't change */
        tcg_gen_or_tl(cond1, cond1, cond2);
        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_div_tl(source1, source1, source2);

        tcg_temp_free(cond1);
        tcg_temp_free(cond2);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_DIVUW:
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_ext32u_tl(source2, source2);
        /* fall through to DIVU */
#endif
    case OPC_RISC_DIVU:
        cond1 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
        tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, source1,
                resultopt1);
        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_divu_tl(source1, source1, source2);

        tcg_temp_free(cond1);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_REMW:
        tcg_gen_ext32s_tl(source1, source1);
        tcg_gen_ext32s_tl(source2, source2);
        /* fall through to REM */
#endif
    case OPC_RISC_REM:
        cond1 = tcg_temp_new();
        cond2 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, 1L);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)-1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                            (target_ulong)1 << (TARGET_LONG_BITS - 1));
        tcg_gen_and_tl(cond2, cond1, cond2); /* cond1 = overflow */
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0); /* cond2 = div 0 */
        /* if overflow or div by zero, set source2 to 1, else don't change */
        tcg_gen_or_tl(cond2, cond1, cond2);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond2, zeroreg, source2,
                resultopt1);
        tcg_gen_rem_tl(resultopt1, source1, source2);
        /* if div by zero, just return the original dividend */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, resultopt1,
                source1);

        tcg_temp_free(cond1);
        tcg_temp_free(cond2);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_REMUW:
        tcg_gen_ext32u_tl(source1, source1);
        tcg_gen_ext32u_tl(source2, source2);
        /* fall through to REMU */
#endif
    case OPC_RISC_REMU:
        cond1 = tcg_temp_new();
        zeroreg = tcg_const_tl(0);
        resultopt1 = tcg_temp_new();

        tcg_gen_movi_tl(resultopt1, (target_ulong)1);
        tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
        tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
                resultopt1);
        tcg_gen_remu_tl(resultopt1, source1, source2);
        /* if div by zero, just return the original dividend */
        tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, resultopt1,
                source1);

        tcg_temp_free(cond1);
        tcg_temp_free(zeroreg);
        tcg_temp_free(resultopt1);
        break;
    default:
        gen_exception_illegal(ctx);
        return;
    }

    if (opc & 0x8) { /* sign extend for W instructions */
        tcg_gen_ext32s_tl(source1, source1);
    }

    gen_set_gpr(rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
}

static void gen_arith_imm(DisasContext *ctx, uint32_t opc, int rd,
        int rs1, target_long imm)
{
    TCGv source1 = tcg_temp_new();
    int shift_len = TARGET_LONG_BITS;
    int shift_a;

    gen_get_gpr(source1, rs1);

    switch (opc) {
    case OPC_RISC_ADDI:
#if defined(TARGET_RISCV64)
    case OPC_RISC_ADDIW:
#endif
        tcg_gen_addi_tl(source1, source1, imm);
        break;
    case OPC_RISC_SLTI:
        tcg_gen_setcondi_tl(TCG_COND_LT, source1, source1, imm);
        break;
    case OPC_RISC_SLTIU:
        tcg_gen_setcondi_tl(TCG_COND_LTU, source1, source1, imm);
        break;
    case OPC_RISC_XORI:
        tcg_gen_xori_tl(source1, source1, imm);
        break;
    case OPC_RISC_ORI:
        tcg_gen_ori_tl(source1, source1, imm);
        break;
    case OPC_RISC_ANDI:
        tcg_gen_andi_tl(source1, source1, imm);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SLLIW:
        shift_len = 32;
        /* FALLTHRU */
#endif
    case OPC_RISC_SLLI:
        if (imm >= shift_len) {
            goto do_illegal;
        }
        tcg_gen_shli_tl(source1, source1, imm);
        break;
#if defined(TARGET_RISCV64)
    case OPC_RISC_SHIFT_RIGHT_IW:
        shift_len = 32;
        /* FALLTHRU */
#endif
    case OPC_RISC_SHIFT_RIGHT_I:
        /* differentiate on IMM */
        shift_a = imm & 0x400;
        imm &= 0x3ff;
        if (imm >= shift_len) {
            goto do_illegal;
        }
        if (imm != 0) {
            if (shift_a) {
                /* SRAI[W] */
                tcg_gen_sextract_tl(source1, source1, imm, shift_len - imm);
            } else {
                /* SRLI[W] */
                tcg_gen_extract_tl(source1, source1, imm, shift_len - imm);
            }
            /* No further sign-extension needed for W instructions.  */
            opc &= ~0x8;
        }
        break;
    default:
    do_illegal:
        gen_exception_illegal(ctx);
        return;
    }

    if (opc & 0x8) { /* sign-extend for W instructions */
        tcg_gen_ext32s_tl(source1, source1);
    }

    gen_set_gpr(rd, source1);
    tcg_temp_free(source1);
}

static void gen_jal(CPURISCVState *env, DisasContext *ctx, int rd,
                    target_ulong imm)
{
    target_ulong next_pc;

    /* check misaligned: */
    next_pc = ctx->base.pc_next + imm;
    if (!riscv_has_ext(env, RVC)) {
        if ((next_pc & 0x3) != 0) {
            gen_exception_inst_addr_mis(ctx);
            return;
        }
    }
    if (rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[rd], ctx->pc_succ_insn);
    }

    gen_goto_tb(ctx, 0, ctx->base.pc_next + imm); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_jalr(CPURISCVState *env, DisasContext *ctx, uint32_t opc,
                     int rd, int rs1, target_long imm)
{
    /* no chaining with JALR */
    TCGLabel *misaligned = NULL;
    TCGv t0 = tcg_temp_new();

    switch (opc) {
    case OPC_RISC_JALR:
        gen_get_gpr(cpu_pc, rs1);
        tcg_gen_addi_tl(cpu_pc, cpu_pc, imm);
        tcg_gen_andi_tl(cpu_pc, cpu_pc, (target_ulong)-2);

        if (!riscv_has_ext(env, RVC)) {
            misaligned = gen_new_label();
            tcg_gen_andi_tl(t0, cpu_pc, 0x2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0x0, misaligned);
        }

        if (rd != 0) {
            tcg_gen_movi_tl(cpu_gpr[rd], ctx->pc_succ_insn);
        }
        tcg_gen_lookup_and_goto_ptr();

        if (misaligned) {
            gen_set_label(misaligned);
            gen_exception_inst_addr_mis(ctx);
        }
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    default:
        gen_exception_illegal(ctx);
        break;
    }
    tcg_temp_free(t0);
}

static void gen_branch(CPURISCVState *env, DisasContext *ctx, uint32_t opc,
                       int rs1, int rs2, target_long bimm)
{
    TCGLabel *l = gen_new_label();
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();
    gen_get_gpr(source1, rs1);
    gen_get_gpr(source2, rs2);

    switch (opc) {
    case OPC_RISC_BEQ:
        tcg_gen_brcond_tl(TCG_COND_EQ, source1, source2, l);
        break;
    case OPC_RISC_BNE:
        tcg_gen_brcond_tl(TCG_COND_NE, source1, source2, l);
        break;
    case OPC_RISC_BLT:
        tcg_gen_brcond_tl(TCG_COND_LT, source1, source2, l);
        break;
    case OPC_RISC_BGE:
        tcg_gen_brcond_tl(TCG_COND_GE, source1, source2, l);
        break;
    case OPC_RISC_BLTU:
        tcg_gen_brcond_tl(TCG_COND_LTU, source1, source2, l);
        break;
    case OPC_RISC_BGEU:
        tcg_gen_brcond_tl(TCG_COND_GEU, source1, source2, l);
        break;
    default:
        gen_exception_illegal(ctx);
        return;
    }
    tcg_temp_free(source1);
    tcg_temp_free(source2);

    gen_goto_tb(ctx, 1, ctx->pc_succ_insn);
    gen_set_label(l); /* branch taken */
    if (!riscv_has_ext(env, RVC) && ((ctx->base.pc_next + bimm) & 0x3)) {
        /* misaligned */
        gen_exception_inst_addr_mis(ctx);
    } else {
        gen_goto_tb(ctx, 0, ctx->base.pc_next + bimm);
    }
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_load(DisasContext *ctx, uint32_t opc, int rd, int rs1,
        target_long imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);
    int memop = tcg_memop_lookup[(opc >> 12) & 0x7];

    if (memop < 0) {
        gen_exception_illegal(ctx);
        return;
    }

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, memop);
    gen_set_gpr(rd, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_store(DisasContext *ctx, uint32_t opc, int rs1, int rs2,
        target_long imm)
{
    TCGv t0 = tcg_temp_new();
    TCGv dat = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);
    gen_get_gpr(dat, rs2);
    int memop = tcg_memop_lookup[(opc >> 12) & 0x7];

    if (memop < 0) {
        gen_exception_illegal(ctx);
        return;
    }

    tcg_gen_qemu_st_tl(dat, t0, ctx->mem_idx, memop);
    tcg_temp_free(t0);
    tcg_temp_free(dat);
}

static void gen_fp_load(DisasContext *ctx, uint32_t opc, int rd,
        int rs1, target_long imm)
{
    TCGv t0;

    if (!(ctx->flags & TB_FLAGS_FP_ENABLE)) {
        gen_exception_illegal(ctx);
        return;
    }

    t0 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);

    switch (opc) {
    case OPC_RISC_FLW:
        tcg_gen_qemu_ld_i64(cpu_fpr[rd], t0, ctx->mem_idx, MO_TEUL);
        /* RISC-V requires NaN-boxing of narrower width floating point values */
        tcg_gen_ori_i64(cpu_fpr[rd], cpu_fpr[rd], 0xffffffff00000000ULL);
        break;
    case OPC_RISC_FLD:
        tcg_gen_qemu_ld_i64(cpu_fpr[rd], t0, ctx->mem_idx, MO_TEQ);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
    tcg_temp_free(t0);
}

static void gen_fp_store(DisasContext *ctx, uint32_t opc, int rs1,
        int rs2, target_long imm)
{
    TCGv t0;

    if (!(ctx->flags & TB_FLAGS_FP_ENABLE)) {
        gen_exception_illegal(ctx);
        return;
    }

    t0 = tcg_temp_new();
    gen_get_gpr(t0, rs1);
    tcg_gen_addi_tl(t0, t0, imm);

    switch (opc) {
    case OPC_RISC_FSW:
        tcg_gen_qemu_st_i64(cpu_fpr[rs2], t0, ctx->mem_idx, MO_TEUL);
        break;
    case OPC_RISC_FSD:
        tcg_gen_qemu_st_i64(cpu_fpr[rs2], t0, ctx->mem_idx, MO_TEQ);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }

    tcg_temp_free(t0);
}

static void gen_atomic(DisasContext *ctx, uint32_t opc,
                      int rd, int rs1, int rs2)
{
    TCGv src1, src2, dat;
    TCGLabel *l1, *l2;
    TCGMemOp mop;
    bool aq, rl;

    /* Extract the size of the atomic operation.  */
    switch (extract32(opc, 12, 3)) {
    case 2: /* 32-bit */
        mop = MO_ALIGN | MO_TESL;
        break;
#if defined(TARGET_RISCV64)
    case 3: /* 64-bit */
        mop = MO_ALIGN | MO_TEQ;
        break;
#endif
    default:
        gen_exception_illegal(ctx);
        return;
    }
    rl = extract32(opc, 25, 1);
    aq = extract32(opc, 26, 1);

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();

    switch (MASK_OP_ATOMIC_NO_AQ_RL_SZ(opc)) {
    case OPC_RISC_LR:
        /* Put addr in load_res, data in load_val.  */
        gen_get_gpr(src1, rs1);
        if (rl) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
        }
        tcg_gen_qemu_ld_tl(load_val, src1, ctx->mem_idx, mop);
        if (aq) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
        }
        tcg_gen_mov_tl(load_res, src1);
        gen_set_gpr(rd, load_val);
        break;

    case OPC_RISC_SC:
        l1 = gen_new_label();
        l2 = gen_new_label();
        dat = tcg_temp_new();

        gen_get_gpr(src1, rs1);
        tcg_gen_brcond_tl(TCG_COND_NE, load_res, src1, l1);

        gen_get_gpr(src2, rs2);
        /* Note that the TCG atomic primitives are SC,
           so we can ignore AQ/RL along this path.  */
        tcg_gen_atomic_cmpxchg_tl(src1, load_res, load_val, src2,
                                  ctx->mem_idx, mop);
        tcg_gen_setcond_tl(TCG_COND_NE, dat, src1, load_val);
        gen_set_gpr(rd, dat);
        tcg_gen_br(l2);

        gen_set_label(l1);
        /* Address comparion failure.  However, we still need to
           provide the memory barrier implied by AQ/RL.  */
        tcg_gen_mb(TCG_MO_ALL + aq * TCG_BAR_LDAQ + rl * TCG_BAR_STRL);
        tcg_gen_movi_tl(dat, 1);
        gen_set_gpr(rd, dat);

        gen_set_label(l2);
        tcg_temp_free(dat);
        break;

    case OPC_RISC_AMOSWAP:
        /* Note that the TCG atomic primitives are SC,
           so we can ignore AQ/RL along this path.  */
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_xchg_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOADD:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_add_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOXOR:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_xor_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOAND:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_and_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOOR:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_or_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOMIN:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_smin_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOMAX:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_smax_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOMINU:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_umin_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;
    case OPC_RISC_AMOMAXU:
        gen_get_gpr(src1, rs1);
        gen_get_gpr(src2, rs2);
        tcg_gen_atomic_fetch_umax_tl(src2, src1, src2, ctx->mem_idx, mop);
        gen_set_gpr(rd, src2);
        break;

    default:
        gen_exception_illegal(ctx);
        break;
    }

    tcg_temp_free(src1);
    tcg_temp_free(src2);
}

static void gen_set_rm(DisasContext *ctx, int rm)
{
    TCGv_i32 t0;

    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;
    t0 = tcg_const_i32(rm);
    gen_helper_set_rounding_mode(cpu_env, t0);
    tcg_temp_free_i32(t0);
}

static void gen_fp_fmadd(DisasContext *ctx, uint32_t opc, int rd,
                         int rs1, int rs2, int rs3, int rm)
{
    switch (opc) {
    case OPC_RISC_FMADD_S:
        gen_set_rm(ctx, rm);
        gen_helper_fmadd_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                           cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    case OPC_RISC_FMADD_D:
        gen_set_rm(ctx, rm);
        gen_helper_fmadd_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                           cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void gen_fp_fmsub(DisasContext *ctx, uint32_t opc, int rd,
                         int rs1, int rs2, int rs3, int rm)
{
    switch (opc) {
    case OPC_RISC_FMSUB_S:
        gen_set_rm(ctx, rm);
        gen_helper_fmsub_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                           cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    case OPC_RISC_FMSUB_D:
        gen_set_rm(ctx, rm);
        gen_helper_fmsub_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                           cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void gen_fp_fnmsub(DisasContext *ctx, uint32_t opc, int rd,
                          int rs1, int rs2, int rs3, int rm)
{
    switch (opc) {
    case OPC_RISC_FNMSUB_S:
        gen_set_rm(ctx, rm);
        gen_helper_fnmsub_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                            cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    case OPC_RISC_FNMSUB_D:
        gen_set_rm(ctx, rm);
        gen_helper_fnmsub_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                            cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void gen_fp_fnmadd(DisasContext *ctx, uint32_t opc, int rd,
                          int rs1, int rs2, int rs3, int rm)
{
    switch (opc) {
    case OPC_RISC_FNMADD_S:
        gen_set_rm(ctx, rm);
        gen_helper_fnmadd_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                            cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    case OPC_RISC_FNMADD_D:
        gen_set_rm(ctx, rm);
        gen_helper_fnmadd_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1],
                            cpu_fpr[rs2], cpu_fpr[rs3]);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void gen_fp_arith(DisasContext *ctx, uint32_t opc, int rd,
                         int rs1, int rs2, int rm)
{
    TCGv t0 = NULL;

    if (!(ctx->flags & TB_FLAGS_FP_ENABLE)) {
        goto do_illegal;
    }

    switch (opc) {
    case OPC_RISC_FADD_S:
        gen_set_rm(ctx, rm);
        gen_helper_fadd_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FSUB_S:
        gen_set_rm(ctx, rm);
        gen_helper_fsub_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FMUL_S:
        gen_set_rm(ctx, rm);
        gen_helper_fmul_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FDIV_S:
        gen_set_rm(ctx, rm);
        gen_helper_fdiv_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FSQRT_S:
        gen_set_rm(ctx, rm);
        gen_helper_fsqrt_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1]);
        break;
    case OPC_RISC_FSGNJ_S:
        gen_fsgnj(ctx, rd, rs1, rs2, rm, INT32_MIN);
        break;

    case OPC_RISC_FMIN_S:
        /* also handles: OPC_RISC_FMAX_S */
        switch (rm) {
        case 0x0:
            gen_helper_fmin_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 0x1:
            gen_helper_fmax_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        default:
            goto do_illegal;
        }
        break;

    case OPC_RISC_FEQ_S:
        /* also handles: OPC_RISC_FLT_S, OPC_RISC_FLE_S */
        t0 = tcg_temp_new();
        switch (rm) {
        case 0x0:
            gen_helper_fle_s(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 0x1:
            gen_helper_flt_s(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 0x2:
            gen_helper_feq_s(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        default:
            goto do_illegal;
        }
        gen_set_gpr(rd, t0);
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FCVT_W_S:
        /* also OPC_RISC_FCVT_WU_S, OPC_RISC_FCVT_L_S, OPC_RISC_FCVT_LU_S */
        t0 = tcg_temp_new();
        switch (rs2) {
        case 0: /* FCVT_W_S */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_w_s(t0, cpu_env, cpu_fpr[rs1]);
            break;
        case 1: /* FCVT_WU_S */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_wu_s(t0, cpu_env, cpu_fpr[rs1]);
            break;
#if defined(TARGET_RISCV64)
        case 2: /* FCVT_L_S */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_l_s(t0, cpu_env, cpu_fpr[rs1]);
            break;
        case 3: /* FCVT_LU_S */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_lu_s(t0, cpu_env, cpu_fpr[rs1]);
            break;
#endif
        default:
            goto do_illegal;
        }
        gen_set_gpr(rd, t0);
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FCVT_S_W:
        /* also OPC_RISC_FCVT_S_WU, OPC_RISC_FCVT_S_L, OPC_RISC_FCVT_S_LU */
        t0 = tcg_temp_new();
        gen_get_gpr(t0, rs1);
        switch (rs2) {
        case 0: /* FCVT_S_W */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_s_w(cpu_fpr[rd], cpu_env, t0);
            break;
        case 1: /* FCVT_S_WU */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_s_wu(cpu_fpr[rd], cpu_env, t0);
            break;
#if defined(TARGET_RISCV64)
        case 2: /* FCVT_S_L */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_s_l(cpu_fpr[rd], cpu_env, t0);
            break;
        case 3: /* FCVT_S_LU */
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_s_lu(cpu_fpr[rd], cpu_env, t0);
            break;
#endif
        default:
            goto do_illegal;
        }
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FMV_X_S:
        /* also OPC_RISC_FCLASS_S */
        t0 = tcg_temp_new();
        switch (rm) {
        case 0: /* FMV */
#if defined(TARGET_RISCV64)
            tcg_gen_ext32s_tl(t0, cpu_fpr[rs1]);
#else
            tcg_gen_extrl_i64_i32(t0, cpu_fpr[rs1]);
#endif
            break;
        case 1:
            gen_helper_fclass_s(t0, cpu_fpr[rs1]);
            break;
        default:
            goto do_illegal;
        }
        gen_set_gpr(rd, t0);
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FMV_S_X:
        t0 = tcg_temp_new();
        gen_get_gpr(t0, rs1);
#if defined(TARGET_RISCV64)
        tcg_gen_mov_i64(cpu_fpr[rd], t0);
#else
        tcg_gen_extu_i32_i64(cpu_fpr[rd], t0);
#endif
        tcg_temp_free(t0);
        break;

    /* double */
    case OPC_RISC_FADD_D:
        gen_set_rm(ctx, rm);
        gen_helper_fadd_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FSUB_D:
        gen_set_rm(ctx, rm);
        gen_helper_fsub_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FMUL_D:
        gen_set_rm(ctx, rm);
        gen_helper_fmul_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FDIV_D:
        gen_set_rm(ctx, rm);
        gen_helper_fdiv_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
        break;
    case OPC_RISC_FSQRT_D:
        gen_set_rm(ctx, rm);
        gen_helper_fsqrt_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1]);
        break;
    case OPC_RISC_FSGNJ_D:
        gen_fsgnj(ctx, rd, rs1, rs2, rm, INT64_MIN);
        break;

    case OPC_RISC_FMIN_D:
        /* also OPC_RISC_FMAX_D */
        switch (rm) {
        case 0:
            gen_helper_fmin_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 1:
            gen_helper_fmax_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        default:
            goto do_illegal;
        }
        break;

    case OPC_RISC_FCVT_S_D:
        switch (rs2) {
        case 1:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_s_d(cpu_fpr[rd], cpu_env, cpu_fpr[rs1]);
            break;
        default:
            goto do_illegal;
        }
        break;

    case OPC_RISC_FCVT_D_S:
        switch (rs2) {
        case 0:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_d_s(cpu_fpr[rd], cpu_env, cpu_fpr[rs1]);
            break;
        default:
            goto do_illegal;
        }
        break;

    case OPC_RISC_FEQ_D:
        /* also OPC_RISC_FLT_D, OPC_RISC_FLE_D */
        t0 = tcg_temp_new();
        switch (rm) {
        case 0:
            gen_helper_fle_d(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 1:
            gen_helper_flt_d(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        case 2:
            gen_helper_feq_d(t0, cpu_env, cpu_fpr[rs1], cpu_fpr[rs2]);
            break;
        default:
            goto do_illegal;
        }
        gen_set_gpr(rd, t0);
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FCVT_W_D:
        /* also OPC_RISC_FCVT_WU_D, OPC_RISC_FCVT_L_D, OPC_RISC_FCVT_LU_D */
        t0 = tcg_temp_new();
        switch (rs2) {
        case 0:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_w_d(t0, cpu_env, cpu_fpr[rs1]);
            break;
        case 1:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_wu_d(t0, cpu_env, cpu_fpr[rs1]);
            break;
#if defined(TARGET_RISCV64)
        case 2:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_l_d(t0, cpu_env, cpu_fpr[rs1]);
            break;
        case 3:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_lu_d(t0, cpu_env, cpu_fpr[rs1]);
            break;
#endif
        default:
            goto do_illegal;
        }
        gen_set_gpr(rd, t0);
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FCVT_D_W:
        /* also OPC_RISC_FCVT_D_WU, OPC_RISC_FCVT_D_L, OPC_RISC_FCVT_D_LU */
        t0 = tcg_temp_new();
        gen_get_gpr(t0, rs1);
        switch (rs2) {
        case 0:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_d_w(cpu_fpr[rd], cpu_env, t0);
            break;
        case 1:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_d_wu(cpu_fpr[rd], cpu_env, t0);
            break;
#if defined(TARGET_RISCV64)
        case 2:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_d_l(cpu_fpr[rd], cpu_env, t0);
            break;
        case 3:
            gen_set_rm(ctx, rm);
            gen_helper_fcvt_d_lu(cpu_fpr[rd], cpu_env, t0);
            break;
#endif
        default:
            goto do_illegal;
        }
        tcg_temp_free(t0);
        break;

    case OPC_RISC_FMV_X_D:
        /* also OPC_RISC_FCLASS_D */
        switch (rm) {
#if defined(TARGET_RISCV64)
        case 0: /* FMV */
            gen_set_gpr(rd, cpu_fpr[rs1]);
            break;
#endif
        case 1:
            t0 = tcg_temp_new();
            gen_helper_fclass_d(t0, cpu_fpr[rs1]);
            gen_set_gpr(rd, t0);
            tcg_temp_free(t0);
            break;
        default:
            goto do_illegal;
        }
        break;

#if defined(TARGET_RISCV64)
    case OPC_RISC_FMV_D_X:
        t0 = tcg_temp_new();
        gen_get_gpr(t0, rs1);
        tcg_gen_mov_tl(cpu_fpr[rd], t0);
        tcg_temp_free(t0);
        break;
#endif

    default:
    do_illegal:
        if (t0) {
            tcg_temp_free(t0);
        }
        gen_exception_illegal(ctx);
        break;
    }
}

static void gen_system(CPURISCVState *env, DisasContext *ctx, uint32_t opc,
                      int rd, int rs1, int csr)
{
    TCGv source1, csr_store, dest, rs1_pass, imm_rs1;
    source1 = tcg_temp_new();
    csr_store = tcg_temp_new();
    dest = tcg_temp_new();
    rs1_pass = tcg_temp_new();
    imm_rs1 = tcg_temp_new();
    gen_get_gpr(source1, rs1);
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    tcg_gen_movi_tl(rs1_pass, rs1);
    tcg_gen_movi_tl(csr_store, csr); /* copy into temp reg to feed to helper */

#ifndef CONFIG_USER_ONLY
    /* Extract funct7 value and check whether it matches SFENCE.VMA */
    if ((opc == OPC_RISC_ECALL) && ((csr >> 5) == 9)) {
        if (env->priv_ver == PRIV_VERSION_1_10_0) {
            /* sfence.vma */
            /* TODO: handle ASID specific fences */
            gen_helper_tlb_flush(cpu_env);
            return;
        } else {
            gen_exception_illegal(ctx);
        }
    }
#endif

    switch (opc) {
    case OPC_RISC_ECALL:
        switch (csr) {
        case 0x0: /* ECALL */
            /* always generates U-level ECALL, fixed in do_interrupt handler */
            generate_exception(ctx, RISCV_EXCP_U_ECALL);
            tcg_gen_exit_tb(NULL, 0); /* no chaining */
            ctx->base.is_jmp = DISAS_NORETURN;
            break;
        case 0x1: /* EBREAK */
            generate_exception(ctx, RISCV_EXCP_BREAKPOINT);
            tcg_gen_exit_tb(NULL, 0); /* no chaining */
            ctx->base.is_jmp = DISAS_NORETURN;
            break;
#ifndef CONFIG_USER_ONLY
        case 0x002: /* URET */
            gen_exception_illegal(ctx);
            break;
        case 0x102: /* SRET */
            if (riscv_has_ext(env, RVS)) {
                gen_helper_sret(cpu_pc, cpu_env, cpu_pc);
                tcg_gen_exit_tb(NULL, 0); /* no chaining */
                ctx->base.is_jmp = DISAS_NORETURN;
            } else {
                gen_exception_illegal(ctx);
            }
            break;
        case 0x202: /* HRET */
            gen_exception_illegal(ctx);
            break;
        case 0x302: /* MRET */
            gen_helper_mret(cpu_pc, cpu_env, cpu_pc);
            tcg_gen_exit_tb(NULL, 0); /* no chaining */
            ctx->base.is_jmp = DISAS_NORETURN;
            break;
        case 0x7b2: /* DRET */
            gen_exception_illegal(ctx);
            break;
        case 0x105: /* WFI */
            tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
            gen_helper_wfi(cpu_env);
            break;
        case 0x104: /* SFENCE.VM */
            if (env->priv_ver <= PRIV_VERSION_1_09_1) {
                gen_helper_tlb_flush(cpu_env);
            } else {
                gen_exception_illegal(ctx);
            }
            break;
#endif
        default:
            gen_exception_illegal(ctx);
            break;
        }
        break;
    default:
        tcg_gen_movi_tl(imm_rs1, rs1);
        gen_io_start();
        switch (opc) {
        case OPC_RISC_CSRRW:
            gen_helper_csrrw(dest, cpu_env, source1, csr_store);
            break;
        case OPC_RISC_CSRRS:
            gen_helper_csrrs(dest, cpu_env, source1, csr_store, rs1_pass);
            break;
        case OPC_RISC_CSRRC:
            gen_helper_csrrc(dest, cpu_env, source1, csr_store, rs1_pass);
            break;
        case OPC_RISC_CSRRWI:
            gen_helper_csrrw(dest, cpu_env, imm_rs1, csr_store);
            break;
        case OPC_RISC_CSRRSI:
            gen_helper_csrrs(dest, cpu_env, imm_rs1, csr_store, rs1_pass);
            break;
        case OPC_RISC_CSRRCI:
            gen_helper_csrrc(dest, cpu_env, imm_rs1, csr_store, rs1_pass);
            break;
        default:
            gen_exception_illegal(ctx);
            return;
        }
        gen_io_end();
        gen_set_gpr(rd, dest);
        /* end tb since we may be changing priv modes, to get mmu_index right */
        tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
        tcg_gen_exit_tb(NULL, 0); /* no chaining */
        ctx->base.is_jmp = DISAS_NORETURN;
        break;
    }
    tcg_temp_free(source1);
    tcg_temp_free(csr_store);
    tcg_temp_free(dest);
    tcg_temp_free(rs1_pass);
    tcg_temp_free(imm_rs1);
}

static void decode_RV32_64C0(DisasContext *ctx)
{
    uint8_t funct3 = extract32(ctx->opcode, 13, 3);
    uint8_t rd_rs2 = GET_C_RS2S(ctx->opcode);
    uint8_t rs1s = GET_C_RS1S(ctx->opcode);

    switch (funct3) {
    case 0:
        /* illegal */
        if (ctx->opcode == 0) {
            gen_exception_illegal(ctx);
        } else {
            /* C.ADDI4SPN -> addi rd', x2, zimm[9:2]*/
            gen_arith_imm(ctx, OPC_RISC_ADDI, rd_rs2, 2,
                          GET_C_ADDI4SPN_IMM(ctx->opcode));
        }
        break;
    case 1:
        /* C.FLD -> fld rd', offset[7:3](rs1')*/
        gen_fp_load(ctx, OPC_RISC_FLD, rd_rs2, rs1s,
                    GET_C_LD_IMM(ctx->opcode));
        /* C.LQ(RV128) */
        break;
    case 2:
        /* C.LW -> lw rd', offset[6:2](rs1') */
        gen_load(ctx, OPC_RISC_LW, rd_rs2, rs1s,
                 GET_C_LW_IMM(ctx->opcode));
        break;
    case 3:
#if defined(TARGET_RISCV64)
        /* C.LD(RV64/128) -> ld rd', offset[7:3](rs1')*/
        gen_load(ctx, OPC_RISC_LD, rd_rs2, rs1s,
                 GET_C_LD_IMM(ctx->opcode));
#else
        /* C.FLW (RV32) -> flw rd', offset[6:2](rs1')*/
        gen_fp_load(ctx, OPC_RISC_FLW, rd_rs2, rs1s,
                    GET_C_LW_IMM(ctx->opcode));
#endif
        break;
    case 4:
        /* reserved */
        gen_exception_illegal(ctx);
        break;
    case 5:
        /* C.FSD(RV32/64) -> fsd rs2', offset[7:3](rs1') */
        gen_fp_store(ctx, OPC_RISC_FSD, rs1s, rd_rs2,
                     GET_C_LD_IMM(ctx->opcode));
        /* C.SQ (RV128) */
        break;
    case 6:
        /* C.SW -> sw rs2', offset[6:2](rs1')*/
        gen_store(ctx, OPC_RISC_SW, rs1s, rd_rs2,
                  GET_C_LW_IMM(ctx->opcode));
        break;
    case 7:
#if defined(TARGET_RISCV64)
        /* C.SD (RV64/128) -> sd rs2', offset[7:3](rs1')*/
        gen_store(ctx, OPC_RISC_SD, rs1s, rd_rs2,
                  GET_C_LD_IMM(ctx->opcode));
#else
        /* C.FSW (RV32) -> fsw rs2', offset[6:2](rs1')*/
        gen_fp_store(ctx, OPC_RISC_FSW, rs1s, rd_rs2,
                     GET_C_LW_IMM(ctx->opcode));
#endif
        break;
    }
}

static void decode_RV32_64C1(CPURISCVState *env, DisasContext *ctx)
{
    uint8_t funct3 = extract32(ctx->opcode, 13, 3);
    uint8_t rd_rs1 = GET_C_RS1(ctx->opcode);
    uint8_t rs1s, rs2s;
    uint8_t funct2;

    switch (funct3) {
    case 0:
        /* C.ADDI -> addi rd, rd, nzimm[5:0] */
        gen_arith_imm(ctx, OPC_RISC_ADDI, rd_rs1, rd_rs1,
                      GET_C_IMM(ctx->opcode));
        break;
    case 1:
#if defined(TARGET_RISCV64)
        /* C.ADDIW (RV64/128) -> addiw rd, rd, imm[5:0]*/
        gen_arith_imm(ctx, OPC_RISC_ADDIW, rd_rs1, rd_rs1,
                      GET_C_IMM(ctx->opcode));
#else
        /* C.JAL(RV32) -> jal x1, offset[11:1] */
        gen_jal(env, ctx, 1, GET_C_J_IMM(ctx->opcode));
#endif
        break;
    case 2:
        /* C.LI -> addi rd, x0, imm[5:0]*/
        gen_arith_imm(ctx, OPC_RISC_ADDI, rd_rs1, 0, GET_C_IMM(ctx->opcode));
        break;
    case 3:
        if (rd_rs1 == 2) {
            /* C.ADDI16SP -> addi x2, x2, nzimm[9:4]*/
            gen_arith_imm(ctx, OPC_RISC_ADDI, 2, 2,
                          GET_C_ADDI16SP_IMM(ctx->opcode));
        } else if (rd_rs1 != 0) {
            /* C.LUI (rs1/rd =/= {0,2}) -> lui rd, nzimm[17:12]*/
            tcg_gen_movi_tl(cpu_gpr[rd_rs1],
                            GET_C_IMM(ctx->opcode) << 12);
        }
        break;
    case 4:
        funct2 = extract32(ctx->opcode, 10, 2);
        rs1s = GET_C_RS1S(ctx->opcode);
        switch (funct2) {
        case 0: /* C.SRLI(RV32) -> srli rd', rd', shamt[5:0] */
            gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_I, rs1s, rs1s,
                               GET_C_ZIMM(ctx->opcode));
            /* C.SRLI64(RV128) */
            break;
        case 1:
            /* C.SRAI -> srai rd', rd', shamt[5:0]*/
            gen_arith_imm(ctx, OPC_RISC_SHIFT_RIGHT_I, rs1s, rs1s,
                            GET_C_ZIMM(ctx->opcode) | 0x400);
            /* C.SRAI64(RV128) */
            break;
        case 2:
            /* C.ANDI -> andi rd', rd', imm[5:0]*/
            gen_arith_imm(ctx, OPC_RISC_ANDI, rs1s, rs1s,
                          GET_C_IMM(ctx->opcode));
            break;
        case 3:
            funct2 = extract32(ctx->opcode, 5, 2);
            rs2s = GET_C_RS2S(ctx->opcode);
            switch (funct2) {
            case 0:
                /* C.SUB -> sub rd', rd', rs2' */
                if (extract32(ctx->opcode, 12, 1) == 0) {
                    gen_arith(ctx, OPC_RISC_SUB, rs1s, rs1s, rs2s);
                }
#if defined(TARGET_RISCV64)
                else {
                    gen_arith(ctx, OPC_RISC_SUBW, rs1s, rs1s, rs2s);
                }
#endif
                break;
            case 1:
                /* C.XOR -> xor rs1', rs1', rs2' */
                if (extract32(ctx->opcode, 12, 1) == 0) {
                    gen_arith(ctx, OPC_RISC_XOR, rs1s, rs1s, rs2s);
                }
#if defined(TARGET_RISCV64)
                else {
                    /* C.ADDW (RV64/128) */
                    gen_arith(ctx, OPC_RISC_ADDW, rs1s, rs1s, rs2s);
                }
#endif
                break;
            case 2:
                /* C.OR -> or rs1', rs1', rs2' */
                gen_arith(ctx, OPC_RISC_OR, rs1s, rs1s, rs2s);
                break;
            case 3:
                /* C.AND -> and rs1', rs1', rs2' */
                gen_arith(ctx, OPC_RISC_AND, rs1s, rs1s, rs2s);
                break;
            }
            break;
        }
        break;
    case 5:
        /* C.J -> jal x0, offset[11:1]*/
        gen_jal(env, ctx, 0, GET_C_J_IMM(ctx->opcode));
        break;
    case 6:
        /* C.BEQZ -> beq rs1', x0, offset[8:1]*/
        rs1s = GET_C_RS1S(ctx->opcode);
        gen_branch(env, ctx, OPC_RISC_BEQ, rs1s, 0, GET_C_B_IMM(ctx->opcode));
        break;
    case 7:
        /* C.BNEZ -> bne rs1', x0, offset[8:1]*/
        rs1s = GET_C_RS1S(ctx->opcode);
        gen_branch(env, ctx, OPC_RISC_BNE, rs1s, 0, GET_C_B_IMM(ctx->opcode));
        break;
    }
}

static void decode_RV32_64C2(CPURISCVState *env, DisasContext *ctx)
{
    uint8_t rd, rs2;
    uint8_t funct3 = extract32(ctx->opcode, 13, 3);


    rd = GET_RD(ctx->opcode);

    switch (funct3) {
    case 0: /* C.SLLI -> slli rd, rd, shamt[5:0]
               C.SLLI64 -> */
        gen_arith_imm(ctx, OPC_RISC_SLLI, rd, rd, GET_C_ZIMM(ctx->opcode));
        break;
    case 1: /* C.FLDSP(RV32/64DC) -> fld rd, offset[8:3](x2) */
        gen_fp_load(ctx, OPC_RISC_FLD, rd, 2, GET_C_LDSP_IMM(ctx->opcode));
        break;
    case 2: /* C.LWSP -> lw rd, offset[7:2](x2) */
        gen_load(ctx, OPC_RISC_LW, rd, 2, GET_C_LWSP_IMM(ctx->opcode));
        break;
    case 3:
#if defined(TARGET_RISCV64)
        /* C.LDSP(RVC64) -> ld rd, offset[8:3](x2) */
        gen_load(ctx, OPC_RISC_LD, rd, 2, GET_C_LDSP_IMM(ctx->opcode));
#else
        /* C.FLWSP(RV32FC) -> flw rd, offset[7:2](x2) */
        gen_fp_load(ctx, OPC_RISC_FLW, rd, 2, GET_C_LWSP_IMM(ctx->opcode));
#endif
        break;
    case 4:
        rs2 = GET_C_RS2(ctx->opcode);

        if (extract32(ctx->opcode, 12, 1) == 0) {
            if (rs2 == 0) {
                /* C.JR -> jalr x0, rs1, 0*/
                gen_jalr(env, ctx, OPC_RISC_JALR, 0, rd, 0);
            } else {
                /* C.MV -> add rd, x0, rs2 */
                gen_arith(ctx, OPC_RISC_ADD, rd, 0, rs2);
            }
        } else {
            if (rd == 0) {
                /* C.EBREAK -> ebreak*/
                gen_system(env, ctx, OPC_RISC_ECALL, 0, 0, 0x1);
            } else {
                if (rs2 == 0) {
                    /* C.JALR -> jalr x1, rs1, 0*/
                    gen_jalr(env, ctx, OPC_RISC_JALR, 1, rd, 0);
                } else {
                    /* C.ADD -> add rd, rd, rs2 */
                    gen_arith(ctx, OPC_RISC_ADD, rd, rd, rs2);
                }
            }
        }
        break;
    case 5:
        /* C.FSDSP -> fsd rs2, offset[8:3](x2)*/
        gen_fp_store(ctx, OPC_RISC_FSD, 2, GET_C_RS2(ctx->opcode),
                     GET_C_SDSP_IMM(ctx->opcode));
        /* C.SQSP */
        break;
    case 6: /* C.SWSP -> sw rs2, offset[7:2](x2)*/
        gen_store(ctx, OPC_RISC_SW, 2, GET_C_RS2(ctx->opcode),
                  GET_C_SWSP_IMM(ctx->opcode));
        break;
    case 7:
#if defined(TARGET_RISCV64)
        /* C.SDSP(Rv64/128) -> sd rs2, offset[8:3](x2)*/
        gen_store(ctx, OPC_RISC_SD, 2, GET_C_RS2(ctx->opcode),
                  GET_C_SDSP_IMM(ctx->opcode));
#else
        /* C.FSWSP(RV32) -> fsw rs2, offset[7:2](x2) */
        gen_fp_store(ctx, OPC_RISC_FSW, 2, GET_C_RS2(ctx->opcode),
                     GET_C_SWSP_IMM(ctx->opcode));
#endif
        break;
    }
}

static void decode_RV32_64C(CPURISCVState *env, DisasContext *ctx)
{
    uint8_t op = extract32(ctx->opcode, 0, 2);

    switch (op) {
    case 0:
        decode_RV32_64C0(ctx);
        break;
    case 1:
        decode_RV32_64C1(env, ctx);
        break;
    case 2:
        decode_RV32_64C2(env, ctx);
        break;
    }
}

static void decode_RV32_64G(CPURISCVState *env, DisasContext *ctx)
{
    int rs1;
    int rs2;
    int rd;
    uint32_t op;
    target_long imm;

    /* We do not do misaligned address check here: the address should never be
     * misaligned at this point. Instructions that set PC must do the check,
     * since epc must be the address of the instruction that caused us to
     * perform the misaligned instruction fetch */

    op = MASK_OP_MAJOR(ctx->opcode);
    rs1 = GET_RS1(ctx->opcode);
    rs2 = GET_RS2(ctx->opcode);
    rd = GET_RD(ctx->opcode);
    imm = GET_IMM(ctx->opcode);

    switch (op) {
    case OPC_RISC_LUI:
        if (rd == 0) {
            break; /* NOP */
        }
        tcg_gen_movi_tl(cpu_gpr[rd], sextract64(ctx->opcode, 12, 20) << 12);
        break;
    case OPC_RISC_AUIPC:
        if (rd == 0) {
            break; /* NOP */
        }
        tcg_gen_movi_tl(cpu_gpr[rd], (sextract64(ctx->opcode, 12, 20) << 12) +
               ctx->base.pc_next);
        break;
    case OPC_RISC_JAL:
        imm = GET_JAL_IMM(ctx->opcode);
        gen_jal(env, ctx, rd, imm);
        break;
    case OPC_RISC_JALR:
        gen_jalr(env, ctx, MASK_OP_JALR(ctx->opcode), rd, rs1, imm);
        break;
    case OPC_RISC_BRANCH:
        gen_branch(env, ctx, MASK_OP_BRANCH(ctx->opcode), rs1, rs2,
                   GET_B_IMM(ctx->opcode));
        break;
    case OPC_RISC_LOAD:
        gen_load(ctx, MASK_OP_LOAD(ctx->opcode), rd, rs1, imm);
        break;
    case OPC_RISC_STORE:
        gen_store(ctx, MASK_OP_STORE(ctx->opcode), rs1, rs2,
                  GET_STORE_IMM(ctx->opcode));
        break;
    case OPC_RISC_ARITH_IMM:
#if defined(TARGET_RISCV64)
    case OPC_RISC_ARITH_IMM_W:
#endif
        if (rd == 0) {
            break; /* NOP */
        }
        gen_arith_imm(ctx, MASK_OP_ARITH_IMM(ctx->opcode), rd, rs1, imm);
        break;
    case OPC_RISC_ARITH:
#if defined(TARGET_RISCV64)
    case OPC_RISC_ARITH_W:
#endif
        if (rd == 0) {
            break; /* NOP */
        }
        gen_arith(ctx, MASK_OP_ARITH(ctx->opcode), rd, rs1, rs2);
        break;
    case OPC_RISC_FP_LOAD:
        gen_fp_load(ctx, MASK_OP_FP_LOAD(ctx->opcode), rd, rs1, imm);
        break;
    case OPC_RISC_FP_STORE:
        gen_fp_store(ctx, MASK_OP_FP_STORE(ctx->opcode), rs1, rs2,
                     GET_STORE_IMM(ctx->opcode));
        break;
    case OPC_RISC_ATOMIC:
        gen_atomic(ctx, MASK_OP_ATOMIC(ctx->opcode), rd, rs1, rs2);
        break;
    case OPC_RISC_FMADD:
        gen_fp_fmadd(ctx, MASK_OP_FP_FMADD(ctx->opcode), rd, rs1, rs2,
                     GET_RS3(ctx->opcode), GET_RM(ctx->opcode));
        break;
    case OPC_RISC_FMSUB:
        gen_fp_fmsub(ctx, MASK_OP_FP_FMSUB(ctx->opcode), rd, rs1, rs2,
                     GET_RS3(ctx->opcode), GET_RM(ctx->opcode));
        break;
    case OPC_RISC_FNMSUB:
        gen_fp_fnmsub(ctx, MASK_OP_FP_FNMSUB(ctx->opcode), rd, rs1, rs2,
                      GET_RS3(ctx->opcode), GET_RM(ctx->opcode));
        break;
    case OPC_RISC_FNMADD:
        gen_fp_fnmadd(ctx, MASK_OP_FP_FNMADD(ctx->opcode), rd, rs1, rs2,
                      GET_RS3(ctx->opcode), GET_RM(ctx->opcode));
        break;
    case OPC_RISC_FP_ARITH:
        gen_fp_arith(ctx, MASK_OP_FP_ARITH(ctx->opcode), rd, rs1, rs2,
                     GET_RM(ctx->opcode));
        break;
    case OPC_RISC_FENCE:
        if (ctx->opcode & 0x1000) {
            /* FENCE_I is a no-op in QEMU,
             * however we need to end the translation block */
            tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
            tcg_gen_exit_tb(NULL, 0);
            ctx->base.is_jmp = DISAS_NORETURN;
        } else {
            /* FENCE is a full memory barrier. */
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        }
        break;
    case OPC_RISC_SYSTEM:
        gen_system(env, ctx, MASK_OP_SYSTEM(ctx->opcode), rd, rs1,
                   (ctx->opcode & 0xFFF00000) >> 20);
        break;
    default:
        gen_exception_illegal(ctx);
        break;
    }
}

static void decode_opc(CPURISCVState *env, DisasContext *ctx)
{
    /* check for compressed insn */
    if (extract32(ctx->opcode, 0, 2) != 3) {
        if (!riscv_has_ext(env, RVC)) {
            gen_exception_illegal(ctx);
        } else {
            ctx->pc_succ_insn = ctx->base.pc_next + 2;
            decode_RV32_64C(env, ctx);
        }
    } else {
        ctx->pc_succ_insn = ctx->base.pc_next + 4;
        decode_RV32_64G(env, ctx);
    }
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->pc_succ_insn = ctx->base.pc_first;
    ctx->flags = ctx->base.tb->flags;
    ctx->mem_idx = ctx->base.tb->flags & TB_FLAGS_MMU_MASK;
    ctx->frm = -1;  /* unknown rounding mode */
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool riscv_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                      const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    ctx->base.is_jmp = DISAS_NORETURN;
    gen_exception_debug();
    /* The address covered by the breakpoint must be included in
       [tb->pc, tb->pc + tb->size) in order to for it to be
       properly cleared -- thus we increment the PC here so that
       the logic setting tb->size below does the right thing.  */
    ctx->base.pc_next += 4;
    return true;
}


static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;

    ctx->opcode = cpu_ldl_code(env, ctx->base.pc_next);
    decode_opc(env, ctx);
    ctx->base.pc_next = ctx->pc_succ_insn;

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void riscv_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .breakpoint_check   = riscv_tr_breakpoint_check,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
    .disas_log          = riscv_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb)
{
    DisasContext ctx;

    translator_loop(&riscv_tr_ops, &ctx.base, cs, tb);
}

void riscv_translate_init(void)
{
    int i;

    /* cpu_gpr[0] is a placeholder for the zero register. Do not use it. */
    /* Use the gen_set_gpr and gen_get_gpr helper functions when accessing */
    /* registers, unless you specifically block reads/writes to reg 0 */
    cpu_gpr[0] = NULL;

    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
    }

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, pc), "pc");
    load_res = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_res),
                             "load_res");
    load_val = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}
