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

static inline void gen_log_predicated_reg_write(int rnum, TCGv val, int slot)
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

    tcg_temp_free(slot_mask);
}

static inline void gen_log_reg_write(int rnum, TCGv val)
{
    tcg_gen_mov_tl(hex_new_value[rnum], val);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum], 1);
    }
}

static void gen_log_predicated_reg_write_pair(int rnum, TCGv_i64 val, int slot)
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

    tcg_temp_free(val32);
    tcg_temp_free(slot_mask);
}

static void gen_log_reg_write_pair(int rnum, TCGv_i64 val)
{
    /* Low word */
    tcg_gen_extrl_i64_i32(hex_new_value[rnum], val);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum], 1);
    }

    /* High word */
    tcg_gen_extrh_i64_i32(hex_new_value[rnum + 1], val);
    if (HEX_DEBUG) {
        /* Do this so HELPER(debug_commit_end) will know */
        tcg_gen_movi_tl(hex_reg_written[rnum + 1], 1);
    }
}

static inline void gen_log_pred_write(DisasContext *ctx, int pnum, TCGv val)
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

    tcg_temp_free(base_val);
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
 *     HEX_REG_P3_0          aliased to the predicate registers
 *                           -> concat the 4 predicate registers together
 *     HEX_REG_PC            actual value stored in DisasContext
 *                           -> assign from ctx->base.pc_next
 *     HEX_REG_QEMU_*_CNT    changes in current TB in DisasContext
 *                           -> add current TB changes to existing reg value
 */
static inline void gen_read_ctrl_reg(DisasContext *ctx, const int reg_num,
                                     TCGv dest)
{
    if (reg_num == HEX_REG_P3_0) {
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
    if (reg_num == HEX_REG_P3_0) {
        TCGv p3_0 = tcg_temp_new();
        gen_read_p3_0(p3_0);
        tcg_gen_concat_i32_i64(dest, p3_0, hex_gpr[reg_num + 1]);
        tcg_temp_free(p3_0);
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
        tcg_temp_free(pkt_cnt);
        tcg_temp_free(insn_cnt);
    } else if (reg_num == HEX_REG_QEMU_HVX_CNT) {
        TCGv hvx_cnt = tcg_temp_new();
        tcg_gen_addi_tl(hvx_cnt, hex_gpr[HEX_REG_QEMU_HVX_CNT],
                        ctx->num_hvx_insns);
        tcg_gen_concat_i32_i64(dest, hvx_cnt, hex_gpr[reg_num + 1]);
        tcg_temp_free(hvx_cnt);
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
    tcg_temp_free(hex_p8);
}

/*
 * Certain control registers require special handling on write
 *     HEX_REG_P3_0          aliased to the predicate registers
 *                           -> break the value across 4 predicate registers
 *     HEX_REG_QEMU_*_CNT    changes in current TB in DisasContext
 *                            -> clear the changes
 */
static inline void gen_write_ctrl_reg(DisasContext *ctx, int reg_num,
                                      TCGv val)
{
    if (reg_num == HEX_REG_P3_0) {
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
    if (reg_num == HEX_REG_P3_0) {
        TCGv val32 = tcg_temp_new();
        tcg_gen_extrl_i64_i32(val32, val);
        gen_write_p3_0(ctx, val32);
        tcg_gen_extrh_i64_i32(val32, val);
        gen_log_reg_write(reg_num + 1, val32);
        tcg_temp_free(val32);
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

static TCGv gen_get_byte(TCGv result, int N, TCGv src, bool sign)
{
    if (sign) {
        tcg_gen_sextract_tl(result, src, N * 8, 8);
    } else {
        tcg_gen_extract_tl(result, src, N * 8, 8);
    }
    return result;
}

static TCGv gen_get_byte_i64(TCGv result, int N, TCGv_i64 src, bool sign)
{
    TCGv_i64 res64 = tcg_temp_new_i64();
    if (sign) {
        tcg_gen_sextract_i64(res64, src, N * 8, 8);
    } else {
        tcg_gen_extract_i64(res64, src, N * 8, 8);
    }
    tcg_gen_extrl_i64_i32(result, res64);
    tcg_temp_free_i64(res64);

    return result;
}

static inline TCGv gen_get_half(TCGv result, int N, TCGv src, bool sign)
{
    if (sign) {
        tcg_gen_sextract_tl(result, src, N * 16, 16);
    } else {
        tcg_gen_extract_tl(result, src, N * 16, 16);
    }
    return result;
}

static inline void gen_set_half(int N, TCGv result, TCGv src)
{
    tcg_gen_deposit_tl(result, result, src, N * 16, 16);
}

static inline void gen_set_half_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 src64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(src64, src);
    tcg_gen_deposit_i64(result, result, src64, N * 16, 16);
    tcg_temp_free_i64(src64);
}

static void gen_set_byte_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 src64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(src64, src);
    tcg_gen_deposit_i64(result, result, src64, N * 8, 8);
    tcg_temp_free_i64(src64);
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
    tcg_temp_free(tmp);
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
    tcg_temp_free_i64(tmp);
    tcg_gen_br(done);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);

    gen_set_label(done);
    tcg_gen_movi_tl(hex_llsc_addr, ~0);
}

static inline void gen_store32(TCGv vaddr, TCGv src, int width, int slot)
{
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], width);
    tcg_gen_mov_tl(hex_store_val32[slot], src);
}

static inline void gen_store1(TCGv_env cpu_env, TCGv vaddr, TCGv src, int slot)
{
    gen_store32(vaddr, src, 1, slot);
}

static inline void gen_store1i(TCGv_env cpu_env, TCGv vaddr, int32_t src, int slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store1(cpu_env, vaddr, tmp, slot);
}

static inline void gen_store2(TCGv_env cpu_env, TCGv vaddr, TCGv src, int slot)
{
    gen_store32(vaddr, src, 2, slot);
}

static inline void gen_store2i(TCGv_env cpu_env, TCGv vaddr, int32_t src, int slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store2(cpu_env, vaddr, tmp, slot);
}

static inline void gen_store4(TCGv_env cpu_env, TCGv vaddr, TCGv src, int slot)
{
    gen_store32(vaddr, src, 4, slot);
}

static inline void gen_store4i(TCGv_env cpu_env, TCGv vaddr, int32_t src, int slot)
{
    TCGv tmp = tcg_constant_tl(src);
    gen_store4(cpu_env, vaddr, tmp, slot);
}

static inline void gen_store8(TCGv_env cpu_env, TCGv vaddr, TCGv_i64 src, int slot)
{
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], 8);
    tcg_gen_mov_i64(hex_store_val64[slot], src);
}

static inline void gen_store8i(TCGv_env cpu_env, TCGv vaddr, int64_t src, int slot)
{
    TCGv_i64 tmp = tcg_constant_i64(src);
    gen_store8(cpu_env, vaddr, tmp, slot);
}

static TCGv gen_8bitsof(TCGv result, TCGv value)
{
    TCGv zero = tcg_constant_tl(0);
    TCGv ones = tcg_constant_tl(0xff);
    tcg_gen_movcond_tl(TCG_COND_NE, result, value, zero, ones, zero);

    return result;
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
        TCGv cancelled = tcg_temp_local_new();
        label_end = gen_new_label();

        /* Don't do anything if the slot was cancelled */
        tcg_gen_extract_tl(cancelled, hex_slot_cancelled, slot_num, 1);
        tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
        tcg_temp_free(cancelled);
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
        TCGv cancelled = tcg_temp_local_new();
        label_end = gen_new_label();

        /* Don't do anything if the slot was cancelled */
        tcg_gen_extract_tl(cancelled, hex_slot_cancelled, slot_num, 1);
        tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
        tcg_temp_free(cancelled);
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
    tcg_temp_free_i64(tmp);
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
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(word);
    tcg_temp_free_i64(bits);
    tcg_temp_free_i64(mask);
}

static void probe_noshuf_load(TCGv va, int s, int mi)
{
    TCGv size = tcg_constant_tl(s);
    TCGv mem_idx = tcg_constant_tl(mi);
    gen_helper_probe_noshuf_load(cpu_env, va, size, mem_idx);
}

#include "tcg_funcs_generated.c.inc"
#include "tcg_func_table_generated.c.inc"
