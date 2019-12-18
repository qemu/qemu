/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef GENPTR_HELPERS_H
#define GENPTR_HELPERS_H

static inline TCGv gen_read_reg(TCGv result, int num)
{
    tcg_gen_mov_tl(result, hex_gpr[num]);
    return result;
}

static inline TCGv gen_read_preg(TCGv pred, uint8_t num)
{
    tcg_gen_mov_tl(pred, hex_pred[num]);
    return pred;
}

static inline TCGv gen_newreg_st(TCGv result, TCGv_env cpu_env, TCGv rnum)
{
    gen_helper_new_value(result, cpu_env, rnum);
    return result;
}

static inline bool is_preloaded(DisasContext *ctx, int num)
{
    int i;
    for (i = 0; i < ctx->ctx_reg_log_idx; i++) {
        if (ctx->ctx_reg_log[i] == num) {
            return true;
        }
    }
    return false;
}

static inline void gen_log_reg_write(int rnum, TCGv val, int slot,
                                     int is_predicated)
{
    if (is_predicated) {
        TCGv one = tcg_const_tl(1);
        TCGv zero = tcg_const_tl(0);
        TCGv slot_mask = tcg_temp_new();

        tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum], slot_mask, zero,
                           val, hex_new_value[rnum]);

        tcg_temp_free(one);
        tcg_temp_free(zero);
        tcg_temp_free(slot_mask);
    } else {
        tcg_gen_mov_tl(hex_new_value[rnum], val);
    }
}

static inline void gen_log_reg_write_pair(int rnum, TCGv_i64 val, int slot,
                                          int is_predicated)
{
    TCGv val32 = tcg_temp_new();

    if (is_predicated) {
        TCGv one = tcg_const_tl(1);
        TCGv zero = tcg_const_tl(0);
        TCGv slot_mask = tcg_temp_new();

        tcg_gen_andi_tl(slot_mask, hex_slot_cancelled, 1 << slot);
        /* Low word */
        tcg_gen_extrl_i64_i32(val32, val);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum], slot_mask, zero,
                           val32, hex_new_value[rnum]);
        /* High word */
        tcg_gen_extrh_i64_i32(val32, val);
        tcg_gen_movcond_tl(TCG_COND_EQ, hex_new_value[rnum + 1],
                           slot_mask, zero,
                           val32, hex_new_value[rnum + 1]);

        tcg_temp_free(one);
        tcg_temp_free(zero);
        tcg_temp_free(slot_mask);
    } else {
        /* Low word */
        tcg_gen_extrl_i64_i32(val32, val);
        tcg_gen_mov_tl(hex_new_value[rnum], val32);
        /* High word */
        tcg_gen_extrh_i64_i32(val32, val);
        tcg_gen_mov_tl(hex_new_value[rnum + 1], val32);
    }

    tcg_temp_free(val32);
}

static inline void gen_log_pred_write(int pnum, TCGv val)
{
    TCGv zero = tcg_const_tl(0);
    TCGv base_val = tcg_temp_local_new();
    TCGv and_val = tcg_temp_local_new();

    /* Multiple writes to the same preg are and'ed together */
    tcg_gen_andi_tl(base_val, val, 0xff);
    tcg_gen_and_tl(and_val, base_val, hex_new_pred_value[pnum]);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_new_pred_value[pnum],
                       hex_pred_written[pnum], zero,
                       and_val, base_val);
    tcg_gen_movi_tl(hex_pred_written[pnum], 1);

    tcg_temp_free(zero);
    tcg_temp_free(base_val);
    tcg_temp_free(and_val);
}

static inline void gen_read_p3_0(TCGv control_reg)
{
    TCGv pval = tcg_temp_new();
    int i;
    tcg_gen_movi_tl(control_reg, 0);
    for (i = NUM_PREGS - 1; i >= 0; i--) {
        tcg_gen_shli_tl(control_reg, control_reg, 8);
        tcg_gen_andi_tl(pval, hex_pred[i], 0xff);
        tcg_gen_or_tl(control_reg, control_reg, pval);
    }
    tcg_temp_free(pval);
}

static inline void gen_write_p3_0(TCGv tmp)
{
    TCGv control_reg = tcg_temp_new();
    TCGv pred_val = tcg_temp_new();
    int i;

    tcg_gen_mov_tl(control_reg, tmp);
    for (i = 0; i < NUM_PREGS; i++) {
        tcg_gen_andi_tl(pred_val, control_reg, 0xff);
        tcg_gen_mov_tl(hex_pred[i], pred_val);
        tcg_gen_shri_tl(control_reg, control_reg, 8);
    }
    tcg_temp_free(control_reg);
    tcg_temp_free(pred_val);
}

static inline TCGv gen_get_byte(TCGv result, int N, TCGv src, bool sign)
{
    TCGv shift = tcg_const_tl(8 * N);
    TCGv mask = tcg_const_tl(0xff);

    tcg_gen_shr_tl(result, src, shift);
    tcg_gen_and_tl(result, result, mask);
    if (sign) {
        tcg_gen_ext8s_tl(result, result);
    } else {
        tcg_gen_ext8u_tl(result, result);
    }
    tcg_temp_free(mask);
    tcg_temp_free(shift);

    return result;
}

static inline TCGv gen_get_byte_i64(TCGv result, int N, TCGv_i64 src, bool sign)
{
    TCGv_i64 result_i64 = tcg_temp_new_i64();
    TCGv_i64 shift = tcg_const_i64(8 * N);
    TCGv_i64 mask = tcg_const_i64(0xff);
    tcg_gen_shr_i64(result_i64, src, shift);
    tcg_gen_and_i64(result_i64, result_i64, mask);
    tcg_gen_extrl_i64_i32(result, result_i64);
    if (sign) {
        tcg_gen_ext8s_tl(result, result);
    } else {
        tcg_gen_ext8u_tl(result, result);
    }
    tcg_temp_free_i64(result_i64);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(mask);

    return result;

}
static inline TCGv gen_get_half(TCGv result, int N, TCGv src, bool sign)
{
    TCGv shift = tcg_const_tl(16 * N);
    TCGv mask = tcg_const_tl(0xffff);

    tcg_gen_shr_tl(result, src, shift);
    tcg_gen_and_tl(result, result, mask);
    if (sign) {
        tcg_gen_ext16s_tl(result, result);
    } else {
        tcg_gen_ext16u_tl(result, result);
    }
    tcg_temp_free(mask);
    tcg_temp_free(shift);

    return result;
}

static inline void gen_set_half(int N, TCGv result, TCGv src)
{
    TCGv mask1 = tcg_const_tl(~(0xffff << (N * 16)));
    TCGv mask2 = tcg_const_tl(0xffff);
    TCGv shift = tcg_const_tl(N * 16);
    TCGv tmp = tcg_temp_new();

    tcg_gen_and_tl(result, result, mask1);
    tcg_gen_and_tl(tmp, src, mask2);
    tcg_gen_shli_tl(tmp, tmp, N * 16);
    tcg_gen_or_tl(result, result, tmp);

    tcg_temp_free(mask1);
    tcg_temp_free(mask2);
    tcg_temp_free(shift);
    tcg_temp_free(tmp);
}

static inline void gen_set_half_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 mask1 = tcg_const_i64(~(0xffffLL << (N * 16)));
    TCGv_i64 mask2 = tcg_const_i64(0xffffLL);
    TCGv_i64 shift = tcg_const_i64(N * 16);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_and_i64(result, result, mask1);
    tcg_gen_concat_i32_i64(tmp, src, src);
    tcg_gen_and_i64(tmp, tmp, mask2);
    tcg_gen_shli_i64(tmp, tmp, N * 16);
    tcg_gen_or_i64(result, result, tmp);

    tcg_temp_free_i64(mask1);
    tcg_temp_free_i64(mask2);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(tmp);
}

static inline void gen_set_byte(int N, TCGv result, TCGv src)
{
    TCGv mask1 = tcg_const_tl(~(0xff << (N * 8)));
    TCGv mask2 = tcg_const_tl(0xff);
    TCGv shift = tcg_const_tl(N * 8);
    TCGv tmp = tcg_temp_new();

    tcg_gen_and_tl(result, result, mask1);
    tcg_gen_and_tl(tmp, src, mask2);
    tcg_gen_shli_tl(tmp, tmp, N * 8);
    tcg_gen_or_tl(result, result, tmp);

    tcg_temp_free(mask1);
    tcg_temp_free(mask2);
    tcg_temp_free(shift);
    tcg_temp_free(tmp);
}

static inline void gen_set_byte_i64(int N, TCGv_i64 result, TCGv src)
{
    TCGv_i64 mask1 = tcg_const_i64(~(0xffLL << (N * 8)));
    TCGv_i64 mask2 = tcg_const_i64(0xffLL);
    TCGv_i64 shift = tcg_const_i64(N * 8);
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_and_i64(result, result, mask1);
    tcg_gen_concat_i32_i64(tmp, src, src);
    tcg_gen_and_i64(tmp, tmp, mask2);
    tcg_gen_shli_i64(tmp, tmp, N * 8);
    tcg_gen_or_i64(result, result, tmp);

    tcg_temp_free_i64(mask1);
    tcg_temp_free_i64(mask2);
    tcg_temp_free_i64(shift);
    tcg_temp_free_i64(tmp);
}

static inline TCGv gen_get_word(TCGv result, int N, TCGv_i64 src, bool sign)
{
    if (N == 0) {
        tcg_gen_extrl_i64_i32(result, src);
    } else if (N == 1) {
        tcg_gen_extrh_i64_i32(result, src);
    } else {
      g_assert_not_reached();
    }
    return result;
}

static inline TCGv_i64 gen_get_word_i64(TCGv_i64 result, int N, TCGv_i64 src,
                                        bool sign)
{
    TCGv word = tcg_temp_new();
    gen_get_word(word, N, src, sign);
    if (sign) {
        tcg_gen_ext_i32_i64(result, word);
    } else {
        tcg_gen_extu_i32_i64(result, word);
    }
    tcg_temp_free(word);
    return result;
}

static inline TCGv gen_set_bit(int i, TCGv result, TCGv src)
{
    TCGv mask = tcg_const_tl(~(1 << i));
    TCGv bit = tcg_temp_new();
    tcg_gen_shli_tl(bit, src, i);
    tcg_gen_and_tl(result, result, mask);
    tcg_gen_or_tl(result, result, bit);
    tcg_temp_free(mask);
    tcg_temp_free(bit);

    return result;
}

static inline void gen_load_locked4u(TCGv dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld32u(dest, vaddr, mem_index);
    tcg_gen_mov_tl(llsc_addr, vaddr);
    tcg_gen_mov_tl(llsc_val, dest);
}

static inline void gen_load_locked8u(TCGv_i64 dest, TCGv vaddr, int mem_index)
{
    tcg_gen_qemu_ld64(dest, vaddr, mem_index);
    tcg_gen_mov_tl(llsc_addr, vaddr);
    tcg_gen_mov_i64(llsc_val_i64, dest);
}

static inline void gen_store_conditional4(CPUHexagonState *env,
                                          DisasContext *ctx, int prednum,
                                          TCGv pred, TCGv vaddr, TCGv src)
{
    TCGv tmp = tcg_temp_new();
    TCGLabel *fail = gen_new_label();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_addr));
    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, tmp, fail);
    tcg_gen_movi_tl(tmp, prednum);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_reg));
    tcg_gen_st_tl(src, cpu_env, offsetof(CPUHexagonState, llsc_newval));
    gen_exception(HEX_EXCP_SC4);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);
    tcg_temp_free(tmp);
}

static inline void gen_store_conditional8(CPUHexagonState *env,
                                          DisasContext *ctx, int prednum,
                                          TCGv pred, TCGv vaddr, TCGv_i64 src)
{
    TCGv tmp = tcg_temp_new();
    TCGLabel *fail = gen_new_label();

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_addr));
    tcg_gen_brcond_tl(TCG_COND_NE, vaddr, tmp, fail);
    tcg_gen_movi_tl(tmp, prednum);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPUHexagonState, llsc_reg));
    tcg_gen_st_i64(src, cpu_env, offsetof(CPUHexagonState, llsc_newval_i64));
    gen_exception(HEX_EXCP_SC8);

    gen_set_label(fail);
    tcg_gen_movi_tl(pred, 0);
    tcg_temp_free(tmp);
}

static inline void gen_store32(TCGv vaddr, TCGv src, int width, int slot)
{
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], width);
    tcg_gen_mov_tl(hex_store_val32[slot], src);
}

static inline void gen_store1(TCGv_env cpu_env, TCGv vaddr, TCGv src,
                              DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(slot);
    gen_store32(vaddr, src, 1, slot);
    tcg_temp_free(tmp);
    ctx->ctx_store_width[slot] = 1;
}

static inline void gen_store1i(TCGv_env cpu_env, TCGv vaddr, int32_t src,
                               DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(src);
    gen_store1(cpu_env, vaddr, tmp, ctx, slot);
    tcg_temp_free(tmp);
}

static inline void gen_store2(TCGv_env cpu_env, TCGv vaddr, TCGv src,
                              DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(slot);
    gen_store32(vaddr, src, 2, slot);
    tcg_temp_free(tmp);
    ctx->ctx_store_width[slot] = 2;
}

static inline void gen_store2i(TCGv_env cpu_env, TCGv vaddr, int32_t src,
                               DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(src);
    gen_store2(cpu_env, vaddr, tmp, ctx, slot);
    tcg_temp_free(tmp);
}

static inline void gen_store4(TCGv_env cpu_env, TCGv vaddr, TCGv src,
                              DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(slot);
    gen_store32(vaddr, src, 4, slot);
    tcg_temp_free(tmp);
    ctx->ctx_store_width[slot] = 4;
}

static inline void gen_store4i(TCGv_env cpu_env, TCGv vaddr, int32_t src,
                               DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(src);
    gen_store4(cpu_env, vaddr, tmp, ctx, slot);
    tcg_temp_free(tmp);
}

static inline void gen_store8(TCGv_env cpu_env, TCGv vaddr, TCGv_i64 src,
                              DisasContext *ctx, int slot)
{
    TCGv tmp = tcg_const_tl(slot);
    tcg_gen_mov_tl(hex_store_addr[slot], vaddr);
    tcg_gen_movi_tl(hex_store_width[slot], 8);
    tcg_gen_mov_i64(hex_store_val64[slot], src);
    tcg_temp_free(tmp);
    ctx->ctx_store_width[slot] = 8;
}

static inline void gen_store8i(TCGv_env cpu_env, TCGv vaddr, int64_t src,
                               DisasContext *ctx, int slot)
{
    TCGv_i64 tmp = tcg_const_i64(src);
    gen_store8(cpu_env, vaddr, tmp, ctx, slot);
    tcg_temp_free_i64(tmp);
}

static inline TCGv_i64 gen_carry_from_add64(TCGv_i64 result, TCGv_i64 a,
                                            TCGv_i64 b, TCGv_i64 c)
{
    TCGv_i64 WORD = tcg_temp_new_i64();
    TCGv_i64 tmpa = tcg_temp_new_i64();
    TCGv_i64 tmpb = tcg_temp_new_i64();
    TCGv_i64 tmpc = tcg_temp_new_i64();

    tcg_gen_mov_i64(tmpa, fGETUWORD(0, a));
    tcg_gen_mov_i64(tmpb, fGETUWORD(0, b));
    tcg_gen_add_i64(tmpc, tmpa, tmpb);
    tcg_gen_add_i64(tmpc, tmpc, c);
    tcg_gen_mov_i64(tmpa, fGETUWORD(1, a));
    tcg_gen_mov_i64(tmpb, fGETUWORD(1, b));
    tcg_gen_add_i64(tmpc, tmpa, tmpb);
    tcg_gen_add_i64(tmpc, tmpc, fGETUWORD(1, tmpc));
    tcg_gen_mov_i64(result, fGETUWORD(1, tmpc));

    tcg_temp_free_i64(WORD);
    tcg_temp_free_i64(tmpa);
    tcg_temp_free_i64(tmpb);
    tcg_temp_free_i64(tmpc);
    return result;
}

static inline TCGv gen_8bitsof(TCGv result, TCGv value)
{
    TCGv zero = tcg_const_tl(0);
    TCGv ones = tcg_const_tl(0xff);
    tcg_gen_movcond_tl(TCG_COND_NE, result, value, zero, ones, zero);
    tcg_temp_free(zero);
    tcg_temp_free(ones);

    return result;
}

static inline void gen_write_new_pc(TCGv addr)
{
    /* If there are multiple branches in a packet, ignore the second one */
    TCGv zero = tcg_const_tl(0);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_next_PC, hex_branch_taken, zero,
                       hex_next_PC, addr);
    tcg_gen_movi_tl(hex_branch_taken, 1);
    tcg_temp_free(zero);
}

static inline void gen_set_usr_field(int field, TCGv val)
{
    tcg_gen_deposit_tl(hex_gpr[HEX_REG_USR], hex_gpr[HEX_REG_USR], val,
                       reg_field_info[field].offset,
                       reg_field_info[field].width);
}

static inline void gen_set_usr_fieldi(int field, int x)
{
    TCGv val = tcg_const_tl(x);
    gen_set_usr_field(field, val);
    tcg_temp_free(val);
}

static inline void gen_clrbit(TCGv tmp, TCGv bit)
{
    TCGv one = tcg_const_tl(1);
    TCGv mask = tcg_temp_new();

    tcg_gen_shl_tl(mask, one, bit);
    tcg_gen_not_tl(mask, mask);
    tcg_gen_and_tl(tmp, tmp, mask);

    tcg_temp_free(one);
    tcg_temp_free(mask);
}

static inline void gen_setbit(TCGv tmp, TCGv bit)
{
    TCGv one = tcg_const_tl(1);
    TCGv mask = tcg_temp_new();

    tcg_gen_shl_tl(mask, one, bit);
    tcg_gen_or_tl(tmp, tmp, mask);

    tcg_temp_free(one);
    tcg_temp_free(mask);
}

static inline void gen_cond_return(TCGv pred, TCGv addr)
{
    TCGv zero = tcg_const_tl(0);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_next_PC, pred, zero, addr, hex_next_PC);
    tcg_temp_free(zero);
}

static inline void ctx_log_reg_write(DisasContext *ctx, int rnum)
{
#if HEX_DEBUG
    int i;
    for (i = 0; i < ctx->ctx_reg_log_idx; i++) {
        if (ctx->ctx_reg_log[i] == rnum) {
            HEX_DEBUG_LOG("WARNING: Multiple writes to r%d\n", rnum);
        }
    }
#endif
    ctx->ctx_reg_log[ctx->ctx_reg_log_idx] = rnum;
    ctx->ctx_reg_log_idx++;
}

static inline void ctx_log_pred_write(DisasContext *ctx, int pnum)
{
    ctx->ctx_preg_log[ctx->ctx_preg_log_idx] = pnum;
    ctx->ctx_preg_log_idx++;
}

static inline void ctx_log_vreg_write(DisasContext *ctx,
                                      int rnum, int is_predicated)
{
    ctx->ctx_vreg_log[ctx->ctx_vreg_log_idx] = rnum;
    ctx->ctx_vreg_is_predicated[ctx->ctx_vreg_log_idx] = is_predicated;
    ctx->ctx_vreg_log_idx++;
}

static inline void ctx_log_qreg_write(DisasContext *ctx,
                                      int rnum, int is_predicated)
{
    ctx->ctx_qreg_log[ctx->ctx_qreg_log_idx] = rnum;
    ctx->ctx_qreg_is_predicated[ctx->ctx_qreg_log_idx] = is_predicated;
    ctx->ctx_qreg_log_idx++;
}

static inline void gen_loop0r(TCGv RsV, TCGv riV, insn_t *insn)
{
    TCGv tmp = tcg_temp_new();
    fIMMEXT(riV);
    fPCALIGN(riV);
    /* fWRITE_LOOP_REGS0( fREAD_PC()+riV, RsV); */
    tcg_gen_add_tl(tmp, hex_gpr[HEX_REG_PC], riV);
    gen_log_reg_write(HEX_REG_LC0, RsV, insn->slot, 0);
    gen_log_reg_write(HEX_REG_SA0, tmp, insn->slot, 0);
    fSET_LPCFG(0);
    tcg_temp_free(tmp);
}

static inline void gen_loop1r(TCGv RsV, TCGv riV, insn_t *insn)
{
    TCGv tmp = tcg_temp_new();
    fIMMEXT(riV);
    fPCALIGN(riV);
    /* fWRITE_LOOP_REGS1( fREAD_PC()+riV, RsV); */
    tcg_gen_add_tl(tmp, hex_gpr[HEX_REG_PC], riV);
    gen_log_reg_write(HEX_REG_LC1, RsV, insn->slot, 0);
    gen_log_reg_write(HEX_REG_SA1, tmp, insn->slot, 0);
    tcg_temp_free(tmp);
}

static inline void gen_compare(TCGCond cond, TCGv res, TCGv arg1, TCGv arg2)
{
    TCGv one = tcg_const_tl(0xff);
    TCGv zero = tcg_const_tl(0);

    tcg_gen_movcond_tl(cond, res, arg1, arg2, one, zero);

    tcg_temp_free(one);
    tcg_temp_free(zero);
}

static inline void gen_compare_i64(TCGCond cond, TCGv res,
                                   TCGv_i64 arg1, TCGv_i64 arg2)
{
    TCGv_i64 one = tcg_const_i64(0xff);
    TCGv_i64 zero = tcg_const_i64(0);
    TCGv_i64 temp = tcg_temp_new_i64();

    tcg_gen_movcond_i64(cond, temp, arg1, arg2, one, zero);
    tcg_gen_extrl_i64_i32(res, temp);
    tcg_gen_andi_tl(res, res, 0xff);

    tcg_temp_free_i64(one);
    tcg_temp_free_i64(zero);
    tcg_temp_free_i64(temp);
}

static inline void gen_cmpnd_cmp_jmp(int pnum, TCGCond cond, bool sense,
                                     TCGv arg1, TCGv arg2, TCGv pc_off)
{
    TCGv new_pc = tcg_temp_new();
    TCGv pred = tcg_temp_new();
    TCGv zero = tcg_const_tl(0);
    TCGv one = tcg_const_tl(1);

    tcg_gen_add_tl(new_pc, hex_gpr[HEX_REG_PC], pc_off);
    gen_compare(cond, pred, arg1, arg2);
    gen_log_pred_write(pnum, pred);
    if (!sense) {
        tcg_gen_xori_tl(pred, pred, 0xff);
    }

    /* If there are multiple branches in a packet, ignore the second one */
    tcg_gen_movcond_tl(TCG_COND_NE, pred, hex_branch_taken, zero, zero, pred);

    tcg_gen_movcond_tl(TCG_COND_NE, hex_next_PC, pred, zero,
                       new_pc, hex_next_PC);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_branch_taken, pred, zero,
                       one, hex_branch_taken);

    tcg_temp_free(new_pc);
    tcg_temp_free(pred);
    tcg_temp_free(zero);
    tcg_temp_free(one);
}

static inline void gen_cmpnd_cmp_n1_jmp(int pnum, TCGCond cond, bool sense,
                                        TCGv arg, TCGv pc_off)
{
    TCGv n1 = tcg_const_tl(-1);
    gen_cmpnd_cmp_jmp(pnum, cond, sense, arg, n1, pc_off);
    tcg_temp_free(n1);
}

void gen_memcpy(TCGv_ptr dest, TCGv_ptr src, size_t n)
{
    TCGv_ptr d = tcg_temp_new_ptr();
    TCGv_ptr s = tcg_temp_new_ptr();
    int i;

    tcg_gen_addi_ptr(d, dest, 0);
    tcg_gen_addi_ptr(s, src, 0);
    if (n % 8 == 0) {
        TCGv_i64 temp = tcg_temp_new_i64();
        for (i = 0; i < n / 8; i++) {
            tcg_gen_ld_i64(temp, s, 0);
            tcg_gen_st_i64(temp, d, 0);
            tcg_gen_addi_ptr(s, s, 8);
            tcg_gen_addi_ptr(d, d, 8);
        }
        tcg_temp_free_i64(temp);
    } else if (n % 4 == 0) {
        TCGv temp = tcg_temp_new();
        for (i = 0; i < n / 4; i++) {
            tcg_gen_ld32u_tl(temp, s, 0);
            tcg_gen_st32_tl(temp, d, 0);
            tcg_gen_addi_ptr(s, s, 4);
            tcg_gen_addi_ptr(d, d, 4);
        }
        tcg_temp_free(temp);
    } else if (n % 2 == 0) {
        TCGv temp = tcg_temp_new();
        for (i = 0; i < n / 2; i++) {
            tcg_gen_ld16u_tl(temp, s, 0);
            tcg_gen_st16_tl(temp, d, 0);
            tcg_gen_addi_ptr(s, s, 2);
            tcg_gen_addi_ptr(d, d, 2);
        }
        tcg_temp_free(temp);
    } else {
        TCGv temp = tcg_temp_new();
        for (i = 0; i < n; i++) {
            tcg_gen_ld8u_tl(temp, s, 0);
            tcg_gen_st8_tl(temp, d, 0);
            tcg_gen_addi_ptr(s, s, 1);
            tcg_gen_addi_ptr(d, d, 1);
        }
        tcg_temp_free(temp);
    }

    tcg_temp_free_ptr(d);
    tcg_temp_free_ptr(s);
}

static inline void gen_jump(TCGv pc_off)
{
    TCGv new_pc = tcg_temp_new();
    tcg_gen_add_tl(new_pc, hex_gpr[HEX_REG_PC], pc_off);
    gen_write_new_pc(new_pc);
    tcg_temp_free(new_pc);
}

static inline void gen_cond_jumpr(TCGv pred, TCGv dst_pc)
{
    TCGv zero = tcg_const_tl(0);
    TCGv one = tcg_const_tl(1);
    TCGv new_pc = tcg_temp_new();

    tcg_gen_movcond_tl(TCG_COND_EQ, new_pc, pred, zero, hex_next_PC, dst_pc);

    /* If there are multiple jumps in a packet, only the first one is taken */
    tcg_gen_movcond_tl(TCG_COND_NE, hex_next_PC, hex_branch_taken, zero,
                       hex_next_PC, new_pc);
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_branch_taken, pred, zero,
                       hex_branch_taken, one);

    tcg_temp_free(zero);
    tcg_temp_free(one);
    tcg_temp_free(new_pc);
}

static inline void gen_cond_jump(TCGv pred, TCGv pc_off)
{
    TCGv new_pc = tcg_temp_new();

    tcg_gen_add_tl(new_pc, hex_gpr[HEX_REG_PC], pc_off);
    gen_cond_jumpr(pred, new_pc);

    tcg_temp_free(new_pc);
}

static inline void gen_call(TCGv pc_off)
{
    gen_log_reg_write(HEX_REG_LR, hex_next_PC, 4, false);
    gen_jump(pc_off);
}

static inline void gen_callr(TCGv new_pc)
{
    gen_log_reg_write(HEX_REG_LR, hex_next_PC, 4, false);
    gen_write_new_pc(new_pc);
}

static inline void gen_endloop0(void)
{
    TCGv lpcfg = tcg_temp_local_new();

    GET_USR_FIELD(USR_LPCFG, lpcfg);

    /*
     *    if (lpcfg == 1) {
     *        hex_new_pred_value[3] = 0xff;
     *        hex_pred_written[3] = 1;
     *    }
     */
    TCGLabel *label1 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_NE, lpcfg, 1, label1);
    {
        tcg_gen_movi_tl(hex_new_pred_value[3], 0xff);
        tcg_gen_movi_tl(hex_pred_written[3], 1);
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
     *    if (hex_gpr[HEX_REG_LC0] > 1) {
     *        hex_next_PC = hex_gpr[HEX_REG_SA0];
     *        hex_branch_taken = 1;
     *        hex_gpr[HEX_REG_LC0] = hex_gpr[HEX_REG_LC0] - 1;
     *    }
     */
    TCGLabel *label3 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_LEU, hex_gpr[HEX_REG_LC0], 1, label3);
    {
        tcg_gen_mov_tl(hex_next_PC, hex_gpr[HEX_REG_SA0]);
        tcg_gen_movi_tl(hex_branch_taken, 1);
        TCGv lc0 = tcg_temp_local_new();
        tcg_gen_mov_tl(lc0, hex_gpr[HEX_REG_LC0]);
        tcg_gen_subi_tl(lc0, lc0, 1);
        tcg_gen_mov_tl(hex_new_value[HEX_REG_LC0], lc0);
        tcg_temp_free(lc0);
    }
    gen_set_label(label3);

    tcg_temp_free(lpcfg);
}

static inline void gen_endloop1(void)
{
    /*
     *    if (hex_gpr[HEX_REG_LC1] > 1) {
     *        hex_next_PC = hex_gpr[HEX_REG_SA1];
     *        hex_branch_taken = 1;
     *        hex_gpr[HEX_REG_LC1] = hex_gpr[HEX_REG_LC1] - 1;
     *    }
     */
    TCGLabel *label = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_LEU, hex_gpr[HEX_REG_LC1], 1, label);
    {
        tcg_gen_mov_tl(hex_next_PC, hex_gpr[HEX_REG_SA1]);
        tcg_gen_movi_tl(hex_branch_taken, 1);
        TCGv lc1 = tcg_temp_local_new();
        tcg_gen_mov_tl(lc1, hex_gpr[HEX_REG_LC1]);
        tcg_gen_subi_tl(lc1, lc1, 1);
        tcg_gen_mov_tl(hex_new_value[HEX_REG_LC1], lc1);
        tcg_temp_free(lc1);
    }
    gen_set_label(label);
}

static inline uint32_t new_temp_vreg_offset(DisasContext *ctx, int num)
{
    uint32_t offset =
        offsetof(CPUHexagonState, temp_vregs[ctx->ctx_temp_vregs_idx]);

    HEX_DEBUG_LOG("new_temp_vreg_offset: %d\n", ctx->ctx_temp_vregs_idx);
    g_assert(ctx->ctx_temp_vregs_idx + num - 1 < TEMP_VECTORS_MAX);
    ctx->ctx_temp_vregs_idx += num;
    return offset;
}

static inline uint32_t new_temp_qreg_offset(DisasContext *ctx)
{
    uint32_t offset =
        offsetof(CPUHexagonState, temp_qregs[ctx->ctx_temp_qregs_idx]);

    HEX_DEBUG_LOG("new_temp_qreg_offset: %d\n", ctx->ctx_temp_qregs_idx);
    g_assert(ctx->ctx_temp_qregs_idx < TEMP_VECTORS_MAX);
    ctx->ctx_temp_qregs_idx++;
    return offset;
}

static inline void gen_read_qreg(TCGv_ptr var, int num, int vtmp)
{
    uint32_t offset = offsetof(CPUHexagonState, QRegs[(num)]);
    TCGv_ptr src = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(src, cpu_env, offset);
    gen_memcpy(var, src, sizeof(mmqreg_t));
    tcg_temp_free_ptr(src);
}

static inline void gen_read_vreg(TCGv_ptr var, int num, int vtmp)
{
    TCGv zero = tcg_const_tl(0);
    TCGv offset_future =
        tcg_const_tl(offsetof(CPUHexagonState, future_VRegs[num]));
    TCGv offset_vregs =
        tcg_const_tl(offsetof(CPUHexagonState, VRegs[num]));
    TCGv offset_tmp_vregs =
        tcg_const_tl(offsetof(CPUHexagonState, tmp_VRegs[num]));
    TCGv offset = tcg_temp_new();
    TCGv_ptr offset_ptr = tcg_temp_new_ptr();
    TCGv_ptr ptr_src = tcg_temp_new_ptr();
    TCGv new_written = tcg_temp_new();
    TCGv tmp_written = tcg_temp_new();

    /*
     *  new_written = (hex_VRegs_select >> num) & 1;
     *  offset = new_written ? offset_future, offset_vregs;
     */
    tcg_gen_shri_tl(new_written, hex_VRegs_select, num);
    tcg_gen_andi_tl(new_written, new_written, 1);
    tcg_gen_movcond_tl(TCG_COND_NE, offset, new_written, zero,
                       offset_future, offset_vregs);

    /*
     * tmp_written = (hex_VRegs_updated_tmp >> num) & 1;
     * if (tmp_written) offset = offset_tmp_vregs;
     */
    tcg_gen_shri_tl(tmp_written, hex_VRegs_updated_tmp, num);
    tcg_gen_andi_tl(tmp_written, tmp_written, 1);
    tcg_gen_movcond_tl(TCG_COND_NE, offset, tmp_written, zero,
                       offset_tmp_vregs, offset);

    if (vtmp == EXT_TMP) {
        TCGv vregs_updated = tcg_temp_new();
        TCGv temp = tcg_temp_new();

        /*
         * vregs_updated = hex_VRegs_updates & (1 << num);
         * if (vregs_updated) {
         *     offset = offset_future;
         *     hex_VRegs_updated ^= (1 << num);
         * }
         */
        tcg_gen_andi_tl(vregs_updated, hex_VRegs_updated, 1 << num);
        tcg_gen_movcond_tl(TCG_COND_NE, offset, vregs_updated, zero,
                           offset_future, offset);
        tcg_gen_xori_tl(temp, hex_VRegs_updated, 1 << num);
        tcg_gen_movcond_tl(TCG_COND_NE, hex_VRegs_updated, vregs_updated, zero,
                           temp, hex_VRegs_updated);

        tcg_temp_free(vregs_updated);
        tcg_temp_free(temp);
    }

    tcg_gen_ext_i32_ptr(offset_ptr, offset);
    tcg_gen_add_ptr(ptr_src, cpu_env, offset_ptr);
    gen_memcpy(var, ptr_src, sizeof(mmvector_t));

    tcg_temp_free(zero);
    tcg_temp_free(offset_future);
    tcg_temp_free(offset_vregs);
    tcg_temp_free(offset_tmp_vregs);
    tcg_temp_free(offset);
    tcg_temp_free_ptr(offset_ptr);
    tcg_temp_free_ptr(ptr_src);
    tcg_temp_free(new_written);
    tcg_temp_free(tmp_written);
}

static inline void gen_read_vreg_pair(TCGv_ptr var, int num, int vtmp)
{
    TCGv_ptr v0 = tcg_temp_new_ptr();
    TCGv_ptr v1 = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(v0, var, offsetof(mmvector_pair_t, v[0]));
    gen_read_vreg(v0, num ^ 0, vtmp);
    tcg_gen_addi_ptr(v1, var, offsetof(mmvector_pair_t, v[1]));
    gen_read_vreg(v1, num ^ 1, vtmp);
    tcg_temp_free_ptr(v0);
    tcg_temp_free_ptr(v1);
}

static inline void gen_log_ext_vreg_write(TCGv_ptr var, int num, int vnew,
                                          int slot_num)
{
    TCGv cancelled = tcg_temp_local_new();
    TCGLabel *label_end = gen_new_label();

    /* Don't do anything if the slot was cancelled */
    gen_slot_cancelled_check(cancelled, slot_num);
    tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
    {
        TCGv mask = tcg_const_tl(1 << num);
        TCGv_ptr dst = tcg_temp_new_ptr();
        if (vnew != EXT_TMP) {
            tcg_gen_or_tl(hex_VRegs_updated, hex_VRegs_updated, mask);
        }
        if (vnew == EXT_NEW) {
            tcg_gen_or_tl(hex_VRegs_select, hex_VRegs_select, mask);
        }
        if (vnew == EXT_TMP) {
            tcg_gen_or_tl(hex_VRegs_updated_tmp, hex_VRegs_updated_tmp, mask);
        }
        tcg_gen_addi_ptr(dst, cpu_env,
                         offsetof(CPUHexagonState, future_VRegs[num]));
        gen_memcpy(dst, var, sizeof(mmvector_t));
        if (vnew == EXT_TMP) {
            TCGv_ptr src = tcg_temp_new_ptr();
            tcg_gen_addi_ptr(dst, cpu_env,
                             offsetof(CPUHexagonState, tmp_VRegs[num]));
            tcg_gen_addi_ptr(src, cpu_env,
                             offsetof(CPUHexagonState, future_VRegs[num]));
            gen_memcpy(dst, src, sizeof(mmvector_t));
            tcg_temp_free_ptr(src);
        }
        tcg_temp_free(mask);
        tcg_temp_free_ptr(dst);
    }
    gen_set_label(label_end);

    tcg_temp_free(cancelled);
}

static inline void gen_log_ext_vreg_write_pair(TCGv_ptr var, int num, int vnew,
                                               int slot_num)
{
    TCGv_ptr v0 = tcg_temp_local_new_ptr();
    TCGv_ptr v1 = tcg_temp_local_new_ptr();
    tcg_gen_addi_ptr(v0, var, offsetof(mmvector_pair_t, v[0]));
    gen_log_ext_vreg_write(v0, num ^ 0, vnew, slot_num);
    tcg_gen_addi_ptr(v1, var, offsetof(mmvector_pair_t, v[1]));
    gen_log_ext_vreg_write(v1, num ^ 1, vnew, slot_num);
    tcg_temp_free_ptr(v0);
    tcg_temp_free_ptr(v1);
}

static inline void gen_log_ext_qreg_write(TCGv_ptr var, int num, int vnew,
                                          int slot_num)
{
    TCGv cancelled = tcg_temp_local_new();
    TCGLabel *label_end = gen_new_label();

    /* Don't do anything if the slot was cancelled */
    gen_slot_cancelled_check(cancelled, slot_num);
    tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
    {
        TCGv_ptr dst = tcg_temp_new_ptr();
        tcg_gen_addi_ptr(dst, cpu_env,
                         offsetof(CPUHexagonState, future_QRegs[num]));
        gen_memcpy(dst, var, sizeof(mmqreg_t));
        tcg_gen_ori_tl(hex_QRegs_updated, hex_QRegs_updated, 1 << num);
        tcg_temp_free_ptr(dst);
    }
    gen_set_label(label_end);

    tcg_temp_free(cancelled);
}

static inline void gen_lshiftr_4_4u(TCGv dst, TCGv src, int32_t shift_amt)
{
    if (shift_amt >= 64) {
        tcg_gen_movi_tl(dst, 0);
    } else {
        tcg_gen_shri_tl(dst, src, shift_amt);
    }
}

static inline void gen_ashiftr_4_4s(TCGv dst, TCGv src, int32_t shift_amt)
{
    tcg_gen_sari_tl(dst, src, shift_amt);
}

static inline void gen_ashiftl_4_4s(TCGv dst, TCGv src, int32_t shift_amt)
{
    if (shift_amt >= 64) {
        tcg_gen_movi_tl(dst, 0);
    } else {
        tcg_gen_shli_tl(dst, src, shift_amt);
    }
}

static inline void gen_cmp_jumpnv(TCGCond cond, int rnum, TCGv src, TCGv pc_off)
{
    TCGv pred = tcg_temp_new();
    tcg_gen_setcond_tl(cond, pred, hex_new_value[rnum], src);
    gen_cond_jump(pred, pc_off);
    tcg_temp_free(pred);
}

static inline void gen_asl_r_r_or(TCGv RxV, TCGv RsV, TCGv RtV)
{
    TCGv zero = tcg_const_tl(0);
    TCGv shift_amt = tcg_temp_new();
    TCGv_i64 shift_amt_i64 = tcg_temp_new_i64();
    TCGv_i64 shift_left_val_i64 = tcg_temp_new_i64();
    TCGv shift_left_val = tcg_temp_new();
    TCGv_i64 shift_right_val_i64 = tcg_temp_new_i64();
    TCGv shift_right_val = tcg_temp_new();
    TCGv or_val = tcg_temp_new();

    /* Sign extend 7->32 bits */
    tcg_gen_shli_tl(shift_amt, RtV, 32 - 7);
    tcg_gen_sari_tl(shift_amt, shift_amt, 32 - 7);
    tcg_gen_ext_i32_i64(shift_amt_i64, shift_amt);

    tcg_gen_ext_i32_i64(shift_left_val_i64, RsV);
    tcg_gen_shl_i64(shift_left_val_i64, shift_left_val_i64, shift_amt_i64);
    tcg_gen_extrl_i64_i32(shift_left_val, shift_left_val_i64);

    /* ((-(SHAMT)) - 1) */
    tcg_gen_neg_i64(shift_amt_i64, shift_amt_i64);
    tcg_gen_subi_i64(shift_amt_i64, shift_amt_i64, 1);

    tcg_gen_ext_i32_i64(shift_right_val_i64, RsV);
    tcg_gen_sar_i64(shift_right_val_i64, shift_right_val_i64, shift_amt_i64);
    tcg_gen_sari_i64(shift_right_val_i64, shift_right_val_i64, 1);
    tcg_gen_extrl_i64_i32(shift_right_val, shift_right_val_i64);

    tcg_gen_movcond_tl(TCG_COND_GE, or_val, shift_amt, zero,
                       shift_left_val, shift_right_val);
    tcg_gen_or_tl(RxV, RxV, or_val);

    tcg_temp_free(zero);
    tcg_temp_free(shift_amt);
    tcg_temp_free_i64(shift_amt_i64);
    tcg_temp_free_i64(shift_left_val_i64);
    tcg_temp_free(shift_left_val);
    tcg_temp_free_i64(shift_right_val_i64);
    tcg_temp_free(shift_right_val);
    tcg_temp_free(or_val);
}

#endif
