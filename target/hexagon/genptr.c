/*
 *  Copyright(c) 2019-2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "insn.h"
#include "opcodes.h"
#include "translate.h"
#define QEMU_GENERATE       /* Used internally by macros.h */
#include "macros.h"
#include "mmvec/macros.h"
#undef QEMU_GENERATE
#include "gen_tcg.h"
#include "gen_tcg_hvx.h"
#include "genptr.h"

TCGv gen_read_reg(TCGv result, int num)
{
    tcg_gen_mov_tl(result, hex_gpr[num]);
    return result;
}

TCGv gen_read_preg(TCGv pred, uint8_t num)
{
    tcg_gen_mov_tl(pred, hex_pred[num]);
    return pred;
}

#define IMMUTABLE (~0)

static const target_ulong reg_immut_masks[TOTAL_PER_THREAD_REGS] = {
    [HEX_REG_USR] = 0xc13000c0,
    [HEX_REG_PC] = IMMUTABLE,
    [HEX_REG_GP] = 0x3f,
    [HEX_REG_UPCYCLELO] = IMMUTABLE,
    [HEX_REG_UPCYCLEHI] = IMMUTABLE,
    [HEX_REG_UTIMERLO] = IMMUTABLE,
    [HEX_REG_UTIMERHI] = IMMUTABLE,
};

static inline void gen_masked_reg_write(TCGv new_val, TCGv cur_val,
                                        target_ulong reg_mask)
{
    if (reg_mask) {
        TCGv tmp = tcg_temp_new();

        /* new_val = (new_val & ~reg_mask) | (cur_val & reg_mask) */
        tcg_gen_andi_tl(new_val, new_val, ~reg_mask);
        tcg_gen_andi_tl(tmp, cur_val, reg_mask);
        tcg_gen_or_tl(new_val, new_val, tmp);
    }
}

static inline void gen_log_predicated_reg_write(int rnum, TCGv val,
                                                uint32_t slot)
{
    TCGv zero = tcg_constant_tl(0);
    TCGv slot_mask = tcg_temp_new();

    tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum], slot_mask, zero,
                           val, hex_new_value[rnum]);
    if (HEX_DEBUG) {
        /*
         * Do this so HELPER(debug_commit_end) will know
         *
         * Note that slot_mask indicates the value is not written
         * (i.e., slot was cancelled), so we create a true/false value before
         * or'ing with hex_reg_written[rnum].
         */
        tcg_gen_setcond_tl(TCG_COND_EQ, slot_mask, slot_mask, zero);
        tcg_gen_or_tl(hex_reg_written[rnum], hex_reg_written[rnum], slot_mask);
    }
}

void gen_log_reg_write(int rnum, TCGv val)
{
    const target_ulong reg_mask = reg_immut_masks[rnum];

    gen_masked_reg_write(val, hex_gpr[rnum], reg_mask);
    tcg_gen_mov_tl(hex_new_value[rnum], val);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum], 1);
    }
}

static void gen_log_predicated_reg_write_pair(int rnum, TCGv_i64 val,
                                              uint32_t slot)
{
    TCGv val32 = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);
    TCGv slot_mask = tcg_temp_new();

    tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
    /* Low word */
    tcg_gen_extrl_i64_i32(val32, val);
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum],
                       slot_mask, zero,
                       val32, hex_new_value[rnum]);
    /* High word */
    tcg_gen_extrh_i64_i32(val32, val);
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum + 1],
                       slot_mask, zero,
                       val32, hex_new_value[rnum + 1]);
    if (HEX_DEBUG) {
        /*
         * Do this so HELPER(debug_commit_end) will know
         *
         * Note that slot_mask indicates the value is not written
         * (i.e., slot was cancelled), so we create a true/false value before
         * or'ing with hex_reg_written[rnum].
         */
        tcg_gen_setcond_tl(TCG_COND_EQ, slot_mask, slot_mask, zero);
        tcg_gen_or_tl(hex_reg_written[rnum], hex_reg_written[rnum], slot_mask);
        tcg_gen_or_tl(hex_reg_written[rnum + 1], hex_reg_written[rnum + 1],
                      slot_mask);
    }
}

static void gen_log_reg_write_pair(int rnum, TCGv_i64 val)
{
    const target_ulong reg_mask_low = reg_immut_masks[rnum];
    const target_ulong reg_mask_high = reg_immut_masks[rnum + 1];
    TCGv val32 = tcg_temp_new();

    /* Low word */
    tcg_gen_extrl_i64_i32(val32, val);
    gen_masked_reg_write(val32, hex_gpr[rnum], reg_mask_low);
    tcg_gen_mov_tl(hex_new_value[rnum], val32);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum], 1);
    }

    /* High word */
    tcg_gen_extrh_i64_i32(val32, val);
    gen_masked_reg_write(val32, hex_gpr[rnum + 1], reg_mask_high);
    tcg_gen_mov_tl(hex_new_value[rnum + 1], val32);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum + 1], 1);
    }
}

void gen_log_pred_write(DisasContext *ctx, int pnum, TCGv val)
{
    TCGv base_val = tcg_temp_new();

    tcg_gen_andi_tl(base_val, val, 0xff);

    /*
     * Section 6.1.3 of the Hexagon V67 Programmer's Reference Manual
     *
     * Multiple writes to the same preg are and'ed together
     * If this is the first predicate write in the packet, do a
     * straight assignment.  Otherwise, do an and.
     */
    if (!test_bit(pnum, ctx->pregs_written)) {
        tcg_gen_mov_tl(hex_new_pred_value[pnum], base_val);
    } else {
        tcg_gen_and_tl(hex_new_pred_value[pnum],
                       hex_new_pred_value[pnum], base_val);
    }
    tcg_gen_ori_tl(hex_pred_written, hex_pred_written, 1 << pnum);
}

static inline void gen_read_p3_0(TCGv control_reg)
{
    tcg_gen_movi_tl(control_reg, 0);
    for (int i = 0; i < NUM_PREGS; i++) {
        tcg_gen_deposit_tl(control_reg, control_reg, hex_pred[i], i * 8, 8);
    }
}

/*
 * Certain control registers require special handling on read
 *     HEX_REG_P3_0_ALIASED  aliased to the predicate registers
 *                           -> concat the 4 predicate registers together
 *     HEX_REG_PC            actual value stored in DisasContext
 *                           -> assign from ctx->base.pc_next
 *     HEX_REG_QEMU_*_CNT    changes in current TB in DisasContext
 *                           -> add current TB changes to existing reg value
 */
static inline void gen_read_ctrl_reg(DisasContext *ctx, const int reg_num,
                                     TCGv dest)
{
    if (reg_num == HEX_REG_P3_0_ALIASED) {
        gen_read_p3_0(dest);
    } else if (reg_num == HEX_REG_PC) {
        tcg_gen_movi_tl(dest, ctx->base.pc_next);
    } else if (reg_num == HEX_REG_QEMU_PKT_CNT) {
        tcg_gen_addi_tl(dest, hex_gpr[HEX_REG_QEMU_PKT_CNT],
                        ctx->num_packets);
    } else if (reg_num == HEX_REG_QEMU_INSN_CNT) {
        tcg_gen_addi_tl(dest, hex_gpr[HEX_REG_QEMU_INSN_CNT],
                        ctx->num_insns);
    } else if (reg_num == HEX_REG_QEMU_HVX_CNT) {
        tcg_gen_addi_tl(dest, hex_gpr[HEX_REG_QEMU_HVX_CNT],
                        ctx->num_hvx_insns);
    } else {
        tcg_gen_mov_tl(dest, hex_gpr[reg_num]);
    }
}

static inline void gen_read_ctrl_reg_pair(DisasContext *ctx, const int reg_num,
                                          TCGv_i64 dest)
{
    if (reg_num == HEX_REG_P3_0_ALIASED) {
        TCGv p3_0 = tcg_temp_new();
        gen_read_p3_0(p3_0);
        tcg_gen_concat_i32_i64(dest, p3_0, hex_gpr[reg_num + 1]);
    } else if (reg_num == HEX_REG_PC - 1) {
        TCGv pc = tcg_constant_tl(ctx->base.pc_next);
        tcg_gen_concat_i32_i64(dest, hex_gpr[reg_num], pc);
    } else if (reg_num == HEX_REG_QEMU_PKT_CNT) {
        TCGv pkt_cnt = tcg_temp_new();
        TCGv insn_cnt = tcg_temp_new();
        tcg_gen_addi_tl(pkt_cnt, hex_gpr[HEX_REG_QEMU_PKT_CNT],
                        ctx->num_packets);
        tcg_gen_addi_tl(insn_cnt, hex_gpr[HEX_REG_QEMU_INSN_CNT],
                        ctx->num_insns);
        tcg_gen_concat_i32_i64(dest, pkt_cnt, insn_cnt);
    } else if (reg_num == HEX_REG_QEMU_HVX_CNT) {
        TCGv hvx_cnt = tcg_temp_new();
        tcg_gen_addi_tl(hvx_cnt, hex_gpr[HEX_REG_QEMU_HVX_CNT],
                        ctx->num_hvx_insns);
        tcg_gen_concat_i32_i64(dest, hvx_cnt, hex_gpr[reg_num + 1]);
    } else {
        tcg_gen_concat_i32_i64(dest,
            hex_gpr[reg_num],
            hex_gpr[reg_num + 1]);
    }
}

static void gen_write_p3_0(DisasContext *ctx, TCGv control_reg)
{
    TCGv hex_p8 = tcg_temp_new();
    for (int i = 0; i < NUM_PREGS; i++) {
        tcg_gen_extract_tl(hex_p8, control_reg, i * 8, 8);
        gen_log_pred_write(ctx, i, hex_p8);
        ctx_log_pred_write(ctx, i);
    }
}

/*
 * Certain control registers require special handling on write
 *     HEX_REG_P3_0_ALIASED  aliased to the predicate registers
 *                           -> break the value across 4 predicate registers
 *     HEX_REG_QEMU_*_CNT    changes in current TB in DisasContext
 *                            -> clear the changes
 */
static inline void gen_write_ctrl_reg(DisasContext *ctx, int reg_num,
                                      TCGv val)
{
    if (reg_num == HEX_REG_P3_0_ALIASED) {
        gen_write_p3_0(ctx, val);
    } else {
        gen_log_reg_write(reg_num, val);
        ctx_log_reg_write(ctx, reg_num);
        if (reg_num == HEX_REG_QEMU_PKT_CNT) {
            ctx->num_packets = 0;
        }
        if (reg_num == HEX_REG_QEMU_INSN_CNT) {
            ctx->num_insns = 0;
        }
        if (reg_num == HEX_REG_QEMU_HVX_CNT) {
            ctx->num_hvx_insns = 0;
        }
    }
}

static inline void gen_write_ctrl_reg_pair(DisasContext *ctx, int reg_num,
                                           TCGv_i64 val)
{
    if (reg_num == HEX_REG_P3_0_ALIASED) {
        TCGv val32 = tcg_temp_new();
        tcg_gen_extrl_i64_i32(val32, val);
        gen_write_p3_0(ctx, val32);
        tcg_gen_extrh_i64_i32(val32, val);
        gen_log_reg_write(reg_num + 1, val32);
        ctx_log_reg_write(ctx, reg_num + 1);
    } else {
        gen_log_reg_write_pair(reg_num, val);
        ctx_log_reg_write_pair(ctx, reg_num);
        if (reg_num == HEX_REG_QEMU_PKT_CNT) {
            ctx->num_packets = 0;
            ctx->num_insns = 0;
        }
        if (reg_num == HEX_REG_QEMU_HVX_CNT) {
            ctx->num_hvx_insns = 0;
        }
    }
}

TCGv gen_get_byte(TCGv result, int N, TCGv src, bool sign)
{
    if (sign) {
        tcg_gen_sextract_tl(result, src, N * 8, 8);
    } else {
        tcg_gen_extract_tl(result, src, N * 8, 8);
    }
    return result;
}

TCGv gen_get_byte_i64(TCGv result, int N, TCGv_i64 src, bool sign)
{
    TCGv_i64 res64 = tcg_temp_new_i64();
    if (sign) {
        tcg_gen_sextract_i64(res64, src, N * 8, 8);
    } else {
        tcg_gen_extract_i64(res64, src, N * 8, 8);
    }
    tcg_gen_extrl_i64_i32(result, res64);

    return result;
}

TCGv gen_get_half(TCGv result, int N, TCGv src, bool sign)
{
    if (sign) {
        tcg_gen_sextract_tl(result, src, N * 16, 16);
    } else {
        tcg_gen_extract_tl(result, src, N * 16, 16);
    }
    return result;
}

void gen_set_half(int N, TCGv result, TCGv src)
{
    tcg_gen_deposit_tl(result, result, src, N * 16, 16);
}

void gen_set_half_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 src64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(src64, src);
    tcg_gen_deposit_i64(result, result, src64, N * 16, 16);
}

void gen_set_byte_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 src64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(src64, src);
    tcg_gen_deposit_i64(result, result, src64, N * 8, 8);
}

static inline void gen_load_locked4u(TCGv dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld32u(dest, vaddr, mem_index);
    tcg_gen_mov_tl(hex_llsc_addr, vaddr);
    tcg_gen_mov_tl(hex_llsc_val, dest);
}

static inline void gen_load_locked8u(TCGv_i64 dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld64(dest, vaddr, mem_index);
    tcg_gen_mov_tl(hex_llsc_addr, vaddr);
    tcg_gen_mov_i64(hex_llsc_val_i64, dest);
}

static inline void gen_store_conditional4(DisasContext *ctx,
                                          TCGv pred, TCGv vaddr, TCGv src)
{
    TCGLabel *fail = gen_new_label();
    TCGLabel *done = gen_new_label();
    TCGv one, zero, tmp;

    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, hex_llsc_addr, fail);

    one = tcg_constant_tl(0xff);
    zero = tcg_constant_tl(0);
    tmp = tcg_temp_new();
    tcg_gen_atomic_cmpxchg_tl(tmp, hex_llsc_addr, hex_llsc_val, src,
                              ctx->mem_idx, MO_32);
    tcg_gen_movcond_tl(TCG_COND_EQ, pred, tmp, hex_llsc_val,
                       one, zero);
    tcg_gen_br(done);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);

    gen_set_label(done);
    tcg_gen_movi_tl(hex_llsc_addr, ~0);
}

static inline void gen_store_conditional8(DisasContext *ctx,
                                          TCGv pred, TCGv vaddr, TCGv_i64 src)
{
    TCGLabel *fail = gen_new_label();
    TCGLabel *done = gen_new_label();
    TCGv_i64 one, zero, tmp;

    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, hex_llsc_addr, fail);

    one = tcg_constant_i64(0xff);
    zero = tcg_constant_i64(0);
    tmp = tcg_temp_new_i64();
    tcg_gen_atomic_cmpxchg_i64(tmp, hex_llsc_addr, hex_llsc_val_i64, src,
                               ctx->mem_idx, MO_64);
    tcg_gen_movcond_i64(TCG_COND_EQ, tmp, tmp, hex_llsc_val_i64,
                        one, zero);
    tcg_gen_extrl_i64_i32(pred, tmp);
    tcg_gen_br(done);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);

    gen_set_label(done);
    tcg_gen_movi_tl(hex_llsc_addr, ~0);
}

void gen_store32(TCGv vaddr, TCGv src, int width, uint32_t slot)
{
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], width);
    tcg_gen_mov_tl(hex_store_val32[slot], src);
}

void gen_store1(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot)
{
    gen_store32(vaddr, src, 1, slot);
}

void gen_store1i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store1(cpu_env, vaddr, tmp, slot);
}

void gen_store2(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot)
{
    gen_store32(vaddr, src, 2, slot);
}

void gen_store2i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store2(cpu_env, vaddr, tmp, slot);
}

void gen_store4(TCGv_env cpu_env, TCGv vaddr, TCGv src, uint32_t slot)
{
    gen_store32(vaddr, src, 4, slot);
}

void gen_store4i(TCGv_env cpu_env, TCGv vaddr, int32_t src, uint32_t slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store4(cpu_env, vaddr, tmp, slot);
}

void gen_store8(TCGv_env cpu_env, TCGv vaddr, TCGv_i64 src, uint32_t slot)
{
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], 8);
    tcg_gen_mov_i64(hex_store_val64[slot], src);
}

void gen_store8i(TCGv_env cpu_env, TCGv vaddr, int64_t src, uint32_t slot)
{
    TCGv_i64 tmp = tcg_constant_i64(src);
    gen_store8(cpu_env, vaddr, tmp, slot);
}

TCGv gen_8bitsof(TCGv result, TCGv value)
{
    TCGv zero = tcg_constant_tl(0);
    TCGv ones = tcg_constant_tl(0xff);
    tcg_gen_movcond_tl(TCG_COND_NE, result, value, zero, ones, zero);

    return result;
}

static void gen_write_new_pc_addr(DisasContext *ctx, TCGv addr,
                                  TCGCond cond, TCGv pred)
{
    TCGLabel *pred_false = NULL;
    if (cond != TCG_COND_ALWAYS) {
        pred_false = gen_new_label();
        tcg_gen_brcondi_tl(cond, pred, 0, pred_false);
    }

    if (ctx->pkt->pkt_has_multi_cof) {
        /* If there are multiple branches in a packet, ignore the second one */
        tcg_gen_movcond_tl(TCG_COND_NE, hex_gpr[HEX_REG_PC],
                           hex_branch_taken, tcg_constant_tl(0),
                           hex_gpr[HEX_REG_PC], addr);
        tcg_gen_movi_tl(hex_branch_taken, 1);
    } else {
        tcg_gen_mov_tl(hex_gpr[HEX_REG_PC], addr);
    }

    if (cond != TCG_COND_ALWAYS) {
        gen_set_label(pred_false);
    }
}

static void gen_write_new_pc_pcrel(DisasContext *ctx, int pc_off,
                                   TCGCond cond, TCGv pred)
{
    target_ulong dest = ctx->pkt->pc + pc_off;
    if (ctx->pkt->pkt_has_multi_cof) {
        gen_write_new_pc_addr(ctx, tcg_constant_tl(dest), cond, pred);
    } else {
        /* Defer this jump to the end of the TB */
        ctx->branch_cond = TCG_COND_ALWAYS;
        if (pred != NULL) {
            ctx->branch_cond = cond;
            tcg_gen_mov_tl(hex_branch_taken, pred);
        }
        ctx->branch_dest = dest;
    }
}

void gen_set_usr_field(int field, TCGv val)
{
    tcg_gen_deposit_tl(hex_new_value[HEX_REG_USR], hex_new_value[HEX_REG_USR],
                       val,
                       reg_field_info[field].offset,
                       reg_field_info[field].width);
}

void gen_set_usr_fieldi(int field, int x)
{
    if (reg_field_info[field].width == 1) {
        target_ulong bit = 1 << reg_field_info[field].offset;
        if ((x & 1) == 1) {
            tcg_gen_ori_tl(hex_new_value[HEX_REG_USR],
                           hex_new_value[HEX_REG_USR],
                           bit);
        } else {
            tcg_gen_andi_tl(hex_new_value[HEX_REG_USR],
                            hex_new_value[HEX_REG_USR],
                            ~bit);
        }
    } else {
        TCGv val = tcg_constant_tl(x);
        gen_set_usr_field(field, val);
    }
}

static void gen_compare(TCGCond cond, TCGv res, TCGv arg1, TCGv arg2)
{
    TCGv one = tcg_constant_tl(0xff);
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_movcond_tl(cond, res, arg1, arg2, one, zero);
}

static void gen_cond_jumpr(DisasContext *ctx, TCGv dst_pc,
                           TCGCond cond, TCGv pred)
{
    gen_write_new_pc_addr(ctx, dst_pc, cond, pred);
}

static void gen_cond_jump(DisasContext *ctx, TCGCond cond, TCGv pred,
                          int pc_off)
{
    gen_write_new_pc_pcrel(ctx, pc_off, cond, pred);
}

static void gen_cmpnd_cmp_jmp(DisasContext *ctx,
                              int pnum, TCGCond cond1, TCGv arg1, TCGv arg2,
                              TCGCond cond2, int pc_off)
{
    if (ctx->insn->part1) {
        TCGv pred = tcg_temp_new();
        gen_compare(cond1, pred, arg1, arg2);
        gen_log_pred_write(ctx, pnum, pred);
    } else {
        TCGv pred = tcg_temp_new();
        tcg_gen_mov_tl(pred, hex_new_pred_value[pnum]);
        gen_cond_jump(ctx, cond2, pred, pc_off);
    }
}

static void gen_cmpnd_cmp_jmp_t(DisasContext *ctx,
                                int pnum, TCGCond cond, TCGv arg1, TCGv arg2,
                                int pc_off)
{
    gen_cmpnd_cmp_jmp(ctx, pnum, cond, arg1, arg2, TCG_COND_EQ, pc_off);
}

static void gen_cmpnd_cmp_jmp_f(DisasContext *ctx,
                                int pnum, TCGCond cond, TCGv arg1, TCGv arg2,
                                int pc_off)
{
    gen_cmpnd_cmp_jmp(ctx, pnum, cond, arg1, arg2, TCG_COND_NE, pc_off);
}

static void gen_cmpnd_cmpi_jmp_t(DisasContext *ctx,
                                 int pnum, TCGCond cond, TCGv arg1, int arg2,
                                 int pc_off)
{
    TCGv tmp = tcg_constant_tl(arg2);
    gen_cmpnd_cmp_jmp(ctx, pnum, cond, arg1, tmp, TCG_COND_EQ, pc_off);
}

static void gen_cmpnd_cmpi_jmp_f(DisasContext *ctx,
                                 int pnum, TCGCond cond, TCGv arg1, int arg2,
                                 int pc_off)
{
    TCGv tmp = tcg_constant_tl(arg2);
    gen_cmpnd_cmp_jmp(ctx, pnum, cond, arg1, tmp, TCG_COND_NE, pc_off);
}

static void gen_cmpnd_cmp_n1_jmp_t(DisasContext *ctx, int pnum, TCGCond cond,
                                   TCGv arg, int pc_off)
{
    gen_cmpnd_cmpi_jmp_t(ctx, pnum, cond, arg, -1, pc_off);
}

static void gen_cmpnd_cmp_n1_jmp_f(DisasContext *ctx, int pnum, TCGCond cond,
                                   TCGv arg, int pc_off)
{
    gen_cmpnd_cmpi_jmp_f(ctx, pnum, cond, arg, -1, pc_off);
}

static void gen_cmpnd_tstbit0_jmp(DisasContext *ctx,
                                  int pnum, TCGv arg, TCGCond cond, int pc_off)
{
    if (ctx->insn->part1) {
        TCGv pred = tcg_temp_new();
        tcg_gen_andi_tl(pred, arg, 1);
        gen_8bitsof(pred, pred);
        gen_log_pred_write(ctx, pnum, pred);
    } else {
        TCGv pred = tcg_temp_new();
        tcg_gen_mov_tl(pred, hex_new_pred_value[pnum]);
        gen_cond_jump(ctx, cond, pred, pc_off);
    }
}

static void gen_testbit0_jumpnv(DisasContext *ctx,
                                TCGv arg, TCGCond cond, int pc_off)
{
    TCGv pred = tcg_temp_new();
    tcg_gen_andi_tl(pred, arg, 1);
    gen_cond_jump(ctx, cond, pred, pc_off);
}

static void gen_jump(DisasContext *ctx, int pc_off)
{
    gen_write_new_pc_pcrel(ctx, pc_off, TCG_COND_ALWAYS, NULL);
}

static void gen_jumpr(DisasContext *ctx, TCGv new_pc)
{
    gen_write_new_pc_addr(ctx, new_pc, TCG_COND_ALWAYS, NULL);
}

static void gen_call(DisasContext *ctx, int pc_off)
{
    TCGv next_PC =
        tcg_constant_tl(ctx->pkt->pc + ctx->pkt->encod_pkt_size_in_bytes);
    gen_log_reg_write(HEX_REG_LR, next_PC);
    gen_write_new_pc_pcrel(ctx, pc_off, TCG_COND_ALWAYS, NULL);
}

static void gen_cond_call(DisasContext *ctx, TCGv pred,
                          TCGCond cond, int pc_off)
{
    TCGv next_PC;
    TCGv lsb = tcg_temp_new();
    TCGLabel *skip = gen_new_label();
    tcg_gen_andi_tl(lsb, pred, 1);
    gen_write_new_pc_pcrel(ctx, pc_off, cond, lsb);
    tcg_gen_brcondi_tl(cond, lsb, 0, skip);
    next_PC =
        tcg_constant_tl(ctx->pkt->pc + ctx->pkt->encod_pkt_size_in_bytes);
    gen_log_reg_write(HEX_REG_LR, next_PC);
    gen_set_label(skip);
}

static void gen_endloop0(DisasContext *ctx)
{
    TCGv lpcfg = tcg_temp_new();

    GET_USR_FIELD(USR_LPCFG, lpcfg);

    /*
     *    if (lpcfg == 1) {
     *        hex_new_pred_value[3] = 0xff;
     *        hex_pred_written |= 1 << 3;
     *    }
     */
    TCGLabel *label1 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_NE, lpcfg, 1, label1);
    {
        tcg_gen_movi_tl(hex_new_pred_value[3], 0xff);
        tcg_gen_ori_tl(hex_pred_written, hex_pred_written, 1 << 3);
    }
    gen_set_label(label1);

    /*
     *    if (lpcfg) {
     *        SET_USR_FIELD(USR_LPCFG, lpcfg - 1);
     *    }
     */
    TCGLabel *label2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, lpcfg, 0, label2);
    {
        tcg_gen_subi_tl(lpcfg, lpcfg, 1);
        SET_USR_FIELD(USR_LPCFG, lpcfg);
    }
    gen_set_label(label2);

    /*
     * If we're in a tight loop, we'll do this at the end of the TB to take
     * advantage of direct block chaining.
     */
    if (!ctx->is_tight_loop) {
        /*
         *    if (hex_gpr[HEX_REG_LC0] > 1) {
         *        PC = hex_gpr[HEX_REG_SA0];
         *        hex_new_value[HEX_REG_LC0] = hex_gpr[HEX_REG_LC0] - 1;
         *    }
         */
        TCGLabel *label3 = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_LEU, hex_gpr[HEX_REG_LC0], 1, label3);
        {
            gen_jumpr(ctx, hex_gpr[HEX_REG_SA0]);
            tcg_gen_subi_tl(hex_new_value[HEX_REG_LC0],
                            hex_gpr[HEX_REG_LC0], 1);
        }
        gen_set_label(label3);
    }
}

static void gen_cmp_jumpnv(DisasContext *ctx,
                           TCGCond cond, TCGv val, TCGv src, int pc_off)
{
    TCGv pred = tcg_temp_new();
    tcg_gen_setcond_tl(cond, pred, val, src);
    gen_cond_jump(ctx, TCG_COND_EQ, pred, pc_off);
}

static void gen_cmpi_jumpnv(DisasContext *ctx,
                            TCGCond cond, TCGv val, int src, int pc_off)
{
    TCGv pred = tcg_temp_new();
    tcg_gen_setcondi_tl(cond, pred, val, src);
    gen_cond_jump(ctx, TCG_COND_EQ, pred, pc_off);
}

/* Shift left with saturation */
static void gen_shl_sat(TCGv dst, TCGv src, TCGv shift_amt)
{
    TCGv sh32 = tcg_temp_new();
    TCGv dst_sar = tcg_temp_new();
    TCGv ovf = tcg_temp_new();
    TCGv satval = tcg_temp_new();
    TCGv min = tcg_constant_tl(0x80000000);
    TCGv max = tcg_constant_tl(0x7fffffff);

    /*
     *    Possible values for shift_amt are 0 .. 64
     *    We need special handling for values above 31
     *
     *    sh32 = shift & 31;
     *    dst = sh32 == shift ? src : 0;
     *    dst <<= sh32;
     *    dst_sar = dst >> sh32;
     *    satval = src < 0 ? min : max;
     *    if (dst_asr != src) {
     *        usr.OVF |= 1;
     *        dst = satval;
     *    }
     */

    tcg_gen_andi_tl(sh32, shift_amt, 31);
    tcg_gen_movcond_tl(TCG_COND_EQ, dst, sh32, shift_amt,
                       src, tcg_constant_tl(0));
    tcg_gen_shl_tl(dst, dst, sh32);
    tcg_gen_sar_tl(dst_sar, dst, sh32);
    tcg_gen_movcond_tl(TCG_COND_LT, satval, src, tcg_constant_tl(0), min, max);

    tcg_gen_setcond_tl(TCG_COND_NE, ovf, dst_sar, src);
    tcg_gen_shli_tl(ovf, ovf, reg_field_info[USR_OVF].offset);
    tcg_gen_or_tl(hex_new_value[HEX_REG_USR], hex_new_value[HEX_REG_USR], ovf);

    tcg_gen_movcond_tl(TCG_COND_EQ, dst, dst_sar, src, dst, satval);
}

static void gen_sar(TCGv dst, TCGv src, TCGv shift_amt)
{
    /*
     *  Shift arithmetic right
     *  Robust when shift_amt is >31 bits
     */
    TCGv tmp = tcg_temp_new();
    tcg_gen_umin_tl(tmp, shift_amt, tcg_constant_tl(31));
    tcg_gen_sar_tl(dst, src, tmp);
}

/* Bidirectional shift right with saturation */
static void gen_asr_r_r_sat(TCGv RdV, TCGv RsV, TCGv RtV)
{
    TCGv shift_amt = tcg_temp_new();
    TCGLabel *positive = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_sextract_i32(shift_amt, RtV, 0, 7);
    tcg_gen_brcondi_tl(TCG_COND_GE, shift_amt, 0, positive);

    /* Negative shift amount => shift left */
    tcg_gen_neg_tl(shift_amt, shift_amt);
    gen_shl_sat(RdV, RsV, shift_amt);
    tcg_gen_br(done);

    gen_set_label(positive);
    /* Positive shift amount => shift right */
    gen_sar(RdV, RsV, shift_amt);

    gen_set_label(done);
}

/* Bidirectional shift left with saturation */
static void gen_asl_r_r_sat(TCGv RdV, TCGv RsV, TCGv RtV)
{
    TCGv shift_amt = tcg_temp_new();
    TCGLabel *positive = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_sextract_i32(shift_amt, RtV, 0, 7);
    tcg_gen_brcondi_tl(TCG_COND_GE, shift_amt, 0, positive);

    /* Negative shift amount => shift right */
    tcg_gen_neg_tl(shift_amt, shift_amt);
    gen_sar(RdV, RsV, shift_amt);
    tcg_gen_br(done);

    gen_set_label(positive);
    /* Positive shift amount => shift left */
    gen_shl_sat(RdV, RsV, shift_amt);

    gen_set_label(done);
}

static intptr_t vreg_src_off(DisasContext *ctx, int num)
{
    intptr_t offset = offsetof(CPUHexagonState, VRegs[num]);

    if (test_bit(num, ctx->vregs_select)) {
        offset = ctx_future_vreg_off(ctx, num, 1, false);
    }
    if (test_bit(num, ctx->vregs_updated_tmp)) {
        offset = ctx_tmp_vreg_off(ctx, num, 1, false);
    }
    return offset;
}

static void gen_log_vreg_write(DisasContext *ctx, intptr_t srcoff, int num,
                               VRegWriteType type, int slot_num,
                               bool is_predicated)
{
    TCGLabel *label_end = NULL;
    intptr_t dstoff;

    if (is_predicated) {
        TCGv cancelled = tcg_temp_new();
        label_end = gen_new_label();

        /* Don't do anything if the slot was cancelled */
        tcg_gen_extract_tl(cancelled, hex_slot_cancelled, slot_num, 1);
        tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
    }

    if (type != EXT_TMP) {
        dstoff = ctx_future_vreg_off(ctx, num, 1, true);
        tcg_gen_gvec_mov(MO_64, dstoff, srcoff,
                         sizeof(MMVector), sizeof(MMVector));
        tcg_gen_ori_tl(hex_VRegs_updated, hex_VRegs_updated, 1 << num);
    } else {
        dstoff = ctx_tmp_vreg_off(ctx, num, 1, false);
        tcg_gen_gvec_mov(MO_64, dstoff, srcoff,
                         sizeof(MMVector), sizeof(MMVector));
    }

    if (is_predicated) {
        gen_set_label(label_end);
    }
}

static void gen_log_vreg_write_pair(DisasContext *ctx, intptr_t srcoff, int num,
                                    VRegWriteType type, int slot_num,
                                    bool is_predicated)
{
    gen_log_vreg_write(ctx, srcoff, num ^ 0, type, slot_num, is_predicated);
    srcoff += sizeof(MMVector);
    gen_log_vreg_write(ctx, srcoff, num ^ 1, type, slot_num, is_predicated);
}

static void gen_log_qreg_write(intptr_t srcoff, int num, int vnew,
                               int slot_num, bool is_predicated)
{
    TCGLabel *label_end = NULL;
    intptr_t dstoff;

    if (is_predicated) {
        TCGv cancelled = tcg_temp_new();
        label_end = gen_new_label();

        /* Don't do anything if the slot was cancelled */
        tcg_gen_extract_tl(cancelled, hex_slot_cancelled, slot_num, 1);
        tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
    }

    dstoff = offsetof(CPUHexagonState, future_QRegs[num]);
    tcg_gen_gvec_mov(MO_64, dstoff, srcoff, sizeof(MMQReg), sizeof(MMQReg));

    if (is_predicated) {
        tcg_gen_ori_tl(hex_QRegs_updated, hex_QRegs_updated, 1 << num);
        gen_set_label(label_end);
    }
}

static void gen_vreg_load(DisasContext *ctx, intptr_t dstoff, TCGv src,
                          bool aligned)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    if (aligned) {
        tcg_gen_andi_tl(src, src, ~((int32_t)sizeof(MMVector) - 1));
    }
    for (int i = 0; i < sizeof(MMVector) / 8; i++) {
        tcg_gen_qemu_ld64(tmp, src, ctx->mem_idx);
        tcg_gen_addi_tl(src, src, 8);
        tcg_gen_st_i64(tmp, cpu_env, dstoff + i * 8);
    }
}

static void gen_vreg_store(DisasContext *ctx, TCGv EA, intptr_t srcoff,
                           int slot, bool aligned)
{
    intptr_t dstoff = offsetof(CPUHexagonState, vstore[slot].data);
    intptr_t maskoff = offsetof(CPUHexagonState, vstore[slot].mask);

    if (is_gather_store_insn(ctx)) {
        TCGv sl = tcg_constant_tl(slot);
        gen_helper_gather_store(cpu_env, EA, sl);
        return;
    }

    tcg_gen_movi_tl(hex_vstore_pending[slot], 1);
    if (aligned) {
        tcg_gen_andi_tl(hex_vstore_addr[slot], EA,
                        ~((int32_t)sizeof(MMVector) - 1));
    } else {
        tcg_gen_mov_tl(hex_vstore_addr[slot], EA);
    }
    tcg_gen_movi_tl(hex_vstore_size[slot], sizeof(MMVector));

    /* Copy the data to the vstore buffer */
    tcg_gen_gvec_mov(MO_64, dstoff, srcoff, sizeof(MMVector), sizeof(MMVector));
    /* Set the mask to all 1's */
    tcg_gen_gvec_dup_imm(MO_64, maskoff, sizeof(MMQReg), sizeof(MMQReg), ~0LL);
}

static void gen_vreg_masked_store(DisasContext *ctx, TCGv EA, intptr_t srcoff,
                                  intptr_t bitsoff, int slot, bool invert)
{
    intptr_t dstoff = offsetof(CPUHexagonState, vstore[slot].data);
    intptr_t maskoff = offsetof(CPUHexagonState, vstore[slot].mask);

    tcg_gen_movi_tl(hex_vstore_pending[slot], 1);
    tcg_gen_andi_tl(hex_vstore_addr[slot], EA,
                    ~((int32_t)sizeof(MMVector) - 1));
    tcg_gen_movi_tl(hex_vstore_size[slot], sizeof(MMVector));

    /* Copy the data to the vstore buffer */
    tcg_gen_gvec_mov(MO_64, dstoff, srcoff, sizeof(MMVector), sizeof(MMVector));
    /* Copy the mask */
    tcg_gen_gvec_mov(MO_64, maskoff, bitsoff, sizeof(MMQReg), sizeof(MMQReg));
    if (invert) {
        tcg_gen_gvec_not(MO_64, maskoff, maskoff,
                         sizeof(MMQReg), sizeof(MMQReg));
    }
}

static void vec_to_qvec(size_t size, intptr_t dstoff, intptr_t srcoff)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 word = tcg_temp_new_i64();
    TCGv_i64 bits = tcg_temp_new_i64();
    TCGv_i64 mask = tcg_temp_new_i64();
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 ones = tcg_constant_i64(~0);

    for (int i = 0; i < sizeof(MMVector) / 8; i++) {
        tcg_gen_ld_i64(tmp, cpu_env, srcoff + i * 8);
        tcg_gen_movi_i64(mask, 0);

        for (int j = 0; j < 8; j += size) {
            tcg_gen_extract_i64(word, tmp, j * 8, size * 8);
            tcg_gen_movcond_i64(TCG_COND_NE, bits, word, zero, ones, zero);
            tcg_gen_deposit_i64(mask, mask, bits, j, size);
        }

        tcg_gen_st8_i64(mask, cpu_env, dstoff + i);
    }
}

void probe_noshuf_load(TCGv va, int s, int mi)
{
    TCGv size = tcg_constant_tl(s);
    TCGv mem_idx = tcg_constant_tl(mi);
    gen_helper_probe_noshuf_load(cpu_env, va, size, mem_idx);
}

/*
 * Note: Since this function might branch, `val` is
 * required to be a `tcg_temp_local`.
 */
void gen_set_usr_field_if(int field, TCGv val)
{
    /* Sets the USR field if `val` is non-zero */
    if (reg_field_info[field].width == 1) {
        TCGv tmp = tcg_temp_new();
        tcg_gen_extract_tl(tmp, val, 0, reg_field_info[field].width);
        tcg_gen_shli_tl(tmp, tmp, reg_field_info[field].offset);
        tcg_gen_or_tl(hex_new_value[HEX_REG_USR],
                      hex_new_value[HEX_REG_USR],
                      tmp);
    } else {
        TCGLabel *skip_label = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, val, 0, skip_label);
        gen_set_usr_field(field, val);
        gen_set_label(skip_label);
    }
}

void gen_sat_i32(TCGv dest, TCGv source, int width)
{
    TCGv max_val = tcg_constant_tl((1 << (width - 1)) - 1);
    TCGv min_val = tcg_constant_tl(-(1 << (width - 1)));
    tcg_gen_smin_tl(dest, source, max_val);
    tcg_gen_smax_tl(dest, dest, min_val);
}

void gen_sat_i32_ovfl(TCGv ovfl, TCGv dest, TCGv source, int width)
{
    gen_sat_i32(dest, source, width);
    tcg_gen_setcond_tl(TCG_COND_NE, ovfl, source, dest);
}

void gen_satu_i32(TCGv dest, TCGv source, int width)
{
    TCGv max_val = tcg_constant_tl((1 << width) - 1);
    TCGv zero = tcg_constant_tl(0);
    tcg_gen_movcond_tl(TCG_COND_GTU, dest, source, max_val, max_val, source);
    tcg_gen_movcond_tl(TCG_COND_LT, dest, source, zero, zero, dest);
}

void gen_satu_i32_ovfl(TCGv ovfl, TCGv dest, TCGv source, int width)
{
    gen_satu_i32(dest, source, width);
    tcg_gen_setcond_tl(TCG_COND_NE, ovfl, source, dest);
}

void gen_sat_i64(TCGv_i64 dest, TCGv_i64 source, int width)
{
    TCGv_i64 max_val = tcg_constant_i64((1LL << (width - 1)) - 1LL);
    TCGv_i64 min_val = tcg_constant_i64(-(1LL << (width - 1)));
    tcg_gen_smin_i64(dest, source, max_val);
    tcg_gen_smax_i64(dest, dest, min_val);
}

void gen_sat_i64_ovfl(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width)
{
    TCGv_i64 ovfl_64;
    gen_sat_i64(dest, source, width);
    ovfl_64 = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, ovfl_64, dest, source);
    tcg_gen_trunc_i64_tl(ovfl, ovfl_64);
}

void gen_satu_i64(TCGv_i64 dest, TCGv_i64 source, int width)
{
    TCGv_i64 max_val = tcg_constant_i64((1LL << width) - 1LL);
    TCGv_i64 zero = tcg_constant_i64(0);
    tcg_gen_movcond_i64(TCG_COND_GTU, dest, source, max_val, max_val, source);
    tcg_gen_movcond_i64(TCG_COND_LT, dest, source, zero, zero, dest);
}

void gen_satu_i64_ovfl(TCGv ovfl, TCGv_i64 dest, TCGv_i64 source, int width)
{
    TCGv_i64 ovfl_64;
    gen_satu_i64(dest, source, width);
    ovfl_64 = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, ovfl_64, dest, source);
    tcg_gen_trunc_i64_tl(ovfl, ovfl_64);
}

/* Implements the fADDSAT64 macro in TCG */
void gen_add_sat_i64(TCGv_i64 ret, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 sum = tcg_temp_new_i64();
    TCGv_i64 xor = tcg_temp_new_i64();
    TCGv_i64 cond1 = tcg_temp_new_i64();
    TCGv_i64 cond2 = tcg_temp_new_i64();
    TCGv_i64 cond3 = tcg_temp_new_i64();
    TCGv_i64 mask = tcg_constant_i64(0x8000000000000000ULL);
    TCGv_i64 max_pos = tcg_constant_i64(0x7FFFFFFFFFFFFFFFLL);
    TCGv_i64 max_neg = tcg_constant_i64(0x8000000000000000LL);
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGLabel *no_ovfl_label = gen_new_label();
    TCGLabel *ovfl_label = gen_new_label();
    TCGLabel *ret_label = gen_new_label();

    tcg_gen_add_i64(sum, a, b);
    tcg_gen_xor_i64(xor, a, b);

    /* if (xor & mask) */
    tcg_gen_and_i64(cond1, xor, mask);
    tcg_gen_brcondi_i64(TCG_COND_NE, cond1, 0, no_ovfl_label);

    /* else if ((a ^ sum) & mask) */
    tcg_gen_xor_i64(cond2, a, sum);
    tcg_gen_and_i64(cond2, cond2, mask);
    tcg_gen_brcondi_i64(TCG_COND_NE, cond2, 0, ovfl_label);
    /* fallthrough to no_ovfl_label branch */

    /* if branch */
    gen_set_label(no_ovfl_label);
    tcg_gen_mov_i64(ret, sum);
    tcg_gen_br(ret_label);

    /* else if branch */
    gen_set_label(ovfl_label);
    tcg_gen_and_i64(cond3, sum, mask);
    tcg_gen_movcond_i64(TCG_COND_NE, ret, cond3, zero, max_pos, max_neg);
    SET_USR_FIELD(USR_OVF, 1);

    gen_set_label(ret_label);
}

#include "tcg_funcs_generated.c.inc"
#include "tcg_func_table_generated.c.inc"
