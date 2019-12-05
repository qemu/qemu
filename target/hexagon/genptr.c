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

#include <stdio.h>
#define QEMU_GENERATE
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "internal.h"
#include "tcg-op.h"
#include "tcg-op-gvec.h"
#include "imported/opcodes.h"
#include "imported/utils.h"
#include "translate.h"
#include "macros.h"
#include "imported/q6v_defines.h"

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

#define fWRAP_J2_trap0(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx->base.is_jmp = DISAS_NORETURN; \
}

#define fWRAP_Y2_dczeroa(GENHLPR, SHORTCODE) SHORTCODE

#define fWRAP_LOAD(SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    SHORTCODE; \
    tcg_temp_free(tmp); \
}

#define fWRAP_L2_loadrub_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrb_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrub_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrb_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrub_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrb_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrubgp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrbgp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL1_loadrub_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL2_loadrb_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)

#define fWRAP_L2_loadruh_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrh_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadruh_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrh_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadruh_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrh_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadruhgp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrhgp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL2_loadruh_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL2_loadrh_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)

#define fWRAP_L2_loadri_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadri_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadri_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrigp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL1_loadri_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL2_loadri_sp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)

#define fWRAP_L2_loadrd_io(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrd_ur(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L4_loadrd_rr(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_L2_loadrdgp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)
#define fWRAP_SL2_loadrd_sp(GENHLPR, SHORTCODE) \
    fWRAP_LOAD(SHORTCODE)

#define fWRAP_loadbXw2(GET_EA, fGB) \
{\
    TCGv ireg = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    TCGv tmpV = tcg_temp_new(); \
    TCGv BYTE = tcg_temp_new(); \
    int i; \
    GET_EA; \
    fLOAD(1, 2, u, EA, tmpV); \
    tcg_gen_movi_tl(RdV, 0); \
    for (i = 0; i < 2; i++) { \
        fSETHALF(i, RdV, fGB(i, tmpV)); \
    } \
    tcg_temp_free(ireg); \
    tcg_temp_free(tmp); \
    tcg_temp_free(tmpV); \
    tcg_temp_free(BYTE); \
}

#define fWRAP_L2_loadbzw2_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_RI(RsV, siV), fGETUBYTE)
#define fWRAP_L4_loadbzw2_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_IRs(UiV, RtV, uiV), fGETUBYTE)
#define fWRAP_L2_loadbsw2_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_RI(RsV, siV), fGETBYTE)
#define fWRAP_L4_loadbsw2_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fEA_IRs(UiV, RtV, uiV), fGETBYTE)

#define fWRAP_L4_loadbzw2_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); }, fGETUBYTE)
#define fWRAP_L2_loadbzw2_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fPM_M(RxV, MuV), fGETUBYTE)
#define fWRAP_L2_loadbzw2_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_BREVR(RxV); fPM_M(RxV, MuV); }, fGETUBYTE)
#define fWRAP_L2_loadbzw2_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_I(RxV, siV); }, fGETUBYTE)

#define fWRAP_L4_loadbsw2_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); }, fGETBYTE)
#define fWRAP_L2_loadbsw2_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2(fPM_M(RxV, MuV), fGETBYTE)
#define fWRAP_L2_loadbsw2_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_BREVR(RxV); fPM_M(RxV, MuV); }, fGETBYTE)
#define fWRAP_L2_loadbsw2_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_I(RxV, siV); }, fGETBYTE)

#define fWRAP_L2_loadbzw2_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); }, fGETUBYTE)
#define fWRAP_L2_loadbsw2_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); }, fGETBYTE)

#define fWRAP_L2_loadbzw2_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 1), MuV); }, \
                   fGETUBYTE)
#define fWRAP_L2_loadbsw2_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw2({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 1), MuV); }, \
                   fGETBYTE)

#define fWRAP_loadbXw4(GET_EA, fGB) \
{ \
    TCGv ireg = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    TCGv tmpV = tcg_temp_new(); \
    TCGv BYTE = tcg_temp_new(); \
    int i; \
    GET_EA; \
    fLOAD(1, 4, u, EA, tmpV);  \
    tcg_gen_movi_i64(RddV, 0); \
    for (i = 0; i < 4; i++) { \
        fSETHALF(i, RddV, fGB(i, tmpV));  \
    }  \
    tcg_temp_free(ireg); \
    tcg_temp_free(tmp); \
    tcg_temp_free(tmpV); \
    tcg_temp_free(BYTE); \
}

#define fWRAP_L2_loadbzw4_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_RI(RsV, siV), fGETUBYTE)
#define fWRAP_L4_loadbzw4_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_IRs(UiV, RtV, uiV), fGETUBYTE)
#define fWRAP_L2_loadbsw4_io(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_RI(RsV, siV), fGETBYTE)
#define fWRAP_L4_loadbsw4_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fEA_IRs(UiV, RtV, uiV), fGETBYTE)

#define fWRAP_L2_loadbzw4_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); }, fGETUBYTE)
#define fWRAP_L2_loadbsw4_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); }, fGETBYTE)

#define fWRAP_L2_loadbzw4_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 2), MuV); }, \
                   fGETUBYTE)
#define fWRAP_L2_loadbsw4_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 2), MuV); }, \
                   fGETBYTE)

#define fWRAP_L4_loadbzw4_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); }, fGETUBYTE)
#define fWRAP_L2_loadbzw4_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fPM_M(RxV, MuV), fGETUBYTE)
#define fWRAP_L2_loadbzw4_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_BREVR(RxV); fPM_M(RxV, MuV); }, fGETUBYTE)
#define fWRAP_L2_loadbzw4_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_REG(RxV); fPM_I(RxV, siV); }, fGETUBYTE)
#define fWRAP_L4_loadbsw4_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); }, fGETBYTE)
#define fWRAP_L2_loadbsw4_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4(fPM_M(RxV, MuV), fGETBYTE)
#define fWRAP_L2_loadbsw4_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_BREVR(RxV); fPM_M(RxV, MuV); }, fGETBYTE)
#define fWRAP_L2_loadbsw4_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadbXw4({ fEA_REG(RxV); fPM_I(RxV, siV); }, fGETBYTE)


#define fWRAP_loadalignh(GET_EA) \
{ \
    TCGv ireg = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    TCGv tmpV = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    READ_REG_PAIR(RyyV, RyyN); \
    GET_EA;  \
    fLOAD(1, 2, u, EA, tmpV);  \
    tcg_gen_extu_i32_i64(tmp_i64, tmpV); \
    tcg_gen_shli_i64(tmp_i64, tmp_i64, 48); \
    tcg_gen_shri_i64(RyyV, RyyV, 16); \
    tcg_gen_or_i64(RyyV, RyyV, tmp_i64); \
    tcg_temp_free(ireg); \
    tcg_temp_free(tmp); \
    tcg_temp_free(tmpV); \
    tcg_temp_free_i64(tmp_i64); \
}

#define fWRAP_L4_loadalignh_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(fEA_IRs(UiV, RtV, uiV))
#define fWRAP_L2_loadalignh_io(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh(fEA_RI(RsV, siV))
#define fWRAP_L2_loadalignh_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); })
#define fWRAP_L2_loadalignh_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 1), MuV); })

#define fWRAP_L4_loadalignh_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); })
#define fWRAP_L2_loadalignh_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_REG(RxV); fPM_M(RxV, MuV); })
#define fWRAP_L2_loadalignh_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_BREVR(RxV); fPM_M(RxV, MuV); })
#define fWRAP_L2_loadalignh_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadalignh({ fEA_REG(RxV); fPM_I(RxV, siV); })

#define fWRAP_loadalignb(GET_EA) \
{ \
    TCGv ireg = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    TCGv tmpV = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    READ_REG_PAIR(RyyV, RyyN); \
    GET_EA;  \
    fLOAD(1, 1, u, EA, tmpV);  \
    tcg_gen_extu_i32_i64(tmp_i64, tmpV); \
    tcg_gen_shli_i64(tmp_i64, tmp_i64, 56); \
    tcg_gen_shri_i64(RyyV, RyyV, 8); \
    tcg_gen_or_i64(RyyV, RyyV, tmp_i64); \
    tcg_temp_free(ireg); \
    tcg_temp_free(tmp); \
    tcg_temp_free(tmpV); \
    tcg_temp_free_i64(tmp_i64); \
}

#define fWRAP_L2_loadalignb_io(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(fEA_RI(RsV, siV))
#define fWRAP_L4_loadalignb_ur(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb(fEA_IRs(UiV, RtV, uiV))
#define fWRAP_L2_loadalignb_pci(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_REG(RxV); fPM_CIRI(RxV, siV, MuV); })
#define fWRAP_L2_loadalignb_pcr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_REG(RxV); fPM_CIRR(RxV, fREAD_IREG(MuV, 0), MuV); })

#define fWRAP_L4_loadalignb_ap(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_IMM(UiV); tcg_gen_mov_tl(ReV, UiV); })
#define fWRAP_L2_loadalignb_pr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_REG(RxV); fPM_M(RxV, MuV); })
#define fWRAP_L2_loadalignb_pbr(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_BREVR(RxV); fPM_M(RxV, MuV); })
#define fWRAP_L2_loadalignb_pi(GENHLPR, SHORTCODE) \
    fWRAP_loadalignb({ fEA_REG(RxV); fPM_I(RxV, siV); })

/* Predicated loads */
#define fWRAP_PRED_LOAD(GET_EA, PRED, SIZE, SIGN) \
{ \
    TCGv LSB = tcg_temp_local_new(); \
    TCGLabel *label = gen_new_label(); \
    GET_EA; \
    PRED;  \
    PRED_LOAD_CANCEL(LSB, EA); \
    tcg_gen_movi_tl(RdV, 0); \
    tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
        fLOAD(1, SIZE, SIGN, EA, RdV); \
    gen_set_label(label); \
    tcg_temp_free(LSB); \
}

#define fWRAP_L2_ploadrubt_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLD(PtV), 1, u)
#define fWRAP_L2_ploadrubt_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV) }, fLSBOLD(PtV), 1, u)
#define fWRAP_L2_ploadrubf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV), 1, u)
#define fWRAP_L2_ploadrubf_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV), 1, u)
#define fWRAP_L2_ploadrubtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEW(PtN), 1, u)
#define fWRAP_L2_ploadrubfnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN), 1, u)
#define fWRAP_L4_ploadrubt_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV), 1, u)
#define fWRAP_L4_ploadrubf_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV), 1, u)
#define fWRAP_L4_ploadrubtnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN), 1, u)
#define fWRAP_L4_ploadrubfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN), 1, u)
#define fWRAP_L2_ploadrubtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN), 1, u)
#define fWRAP_L2_ploadrubfnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN), 1, u)
#define fWRAP_L4_ploadrubt_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLD(PtV), 1, u)
#define fWRAP_L4_ploadrubf_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLDNOT(PtV), 1, u)
#define fWRAP_L4_ploadrubtnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEW(PtN), 1, u)
#define fWRAP_L4_ploadrubfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEWNOT(PtN), 1, u)
#define fWRAP_L2_ploadrbt_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLD(PtV), 1, s)
#define fWRAP_L2_ploadrbt_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLD(PtV), 1, s)
#define fWRAP_L2_ploadrbf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV), 1, s)
#define fWRAP_L2_ploadrbf_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV), 1, s)
#define fWRAP_L2_ploadrbtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEW(PtN), 1, s)
#define fWRAP_L2_ploadrbfnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN), 1, s)
#define fWRAP_L4_ploadrbt_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV), 1, s)
#define fWRAP_L4_ploadrbf_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV), 1, s)
#define fWRAP_L4_ploadrbtnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN), 1, s)
#define fWRAP_L4_ploadrbfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN), 1, s)
#define fWRAP_L2_ploadrbtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN), 1, s)
#define fWRAP_L2_ploadrbfnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN), 1, s)
#define fWRAP_L4_ploadrbt_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLD(PtV), 1, s)
#define fWRAP_L4_ploadrbf_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLDNOT(PtV), 1, s)
#define fWRAP_L4_ploadrbtnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEW(PtN), 1, s)
#define fWRAP_L4_ploadrbfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEWNOT(PtN), 1, s)

#define fWRAP_L2_ploadruht_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLD(PtV), 2, u)
#define fWRAP_L2_ploadruht_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLD(PtV), 2, u)
#define fWRAP_L2_ploadruhf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV), 2, u)
#define fWRAP_L2_ploadruhf_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV), 2, u)
#define fWRAP_L2_ploadruhtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEW(PtN), 2, u)
#define fWRAP_L2_ploadruhfnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN), 2, u)
#define fWRAP_L4_ploadruht_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV), 2, u)
#define fWRAP_L4_ploadruhf_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV), 2, u)
#define fWRAP_L4_ploadruhtnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN), 2, u)
#define fWRAP_L4_ploadruhfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN), 2, u)
#define fWRAP_L2_ploadruhtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN), 2, u)
#define fWRAP_L2_ploadruhfnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN), 2, u)
#define fWRAP_L4_ploadruht_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLD(PtV), 2, u)
#define fWRAP_L4_ploadruhf_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLDNOT(PtV), 2, u)
#define fWRAP_L4_ploadruhtnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEW(PtN), 2, u)
#define fWRAP_L4_ploadruhfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEWNOT(PtN), 2, u)
#define fWRAP_L2_ploadrht_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLD(PtV), 2, s)
#define fWRAP_L2_ploadrht_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLD(PtV), 2, s)
#define fWRAP_L2_ploadrhf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV), 2, s)
#define fWRAP_L2_ploadrhf_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV), 2, s)
#define fWRAP_L2_ploadrhtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEW(PtN), 2, s)
#define fWRAP_L2_ploadrhfnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN), 2, s)
#define fWRAP_L4_ploadrht_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV), 2, s)
#define fWRAP_L4_ploadrhf_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV), 2, s)
#define fWRAP_L4_ploadrhtnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN), 2, s)
#define fWRAP_L4_ploadrhfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN), 2, s)
#define fWRAP_L2_ploadrhtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN), 2, s)
#define fWRAP_L2_ploadrhfnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN), 2, s)
#define fWRAP_L4_ploadrht_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLD(PtV), 2, s)
#define fWRAP_L4_ploadrhf_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLDNOT(PtV), 2, s)
#define fWRAP_L4_ploadrhtnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEW(PtN), 2, s)
#define fWRAP_L4_ploadrhfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEWNOT(PtN), 2, s)

#define fWRAP_L2_ploadrit_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLD(PtV), 4, u)
#define fWRAP_L2_ploadrit_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLD(PtV), 4, u)
#define fWRAP_L2_ploadrif_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV), 4, u)
#define fWRAP_L2_ploadrif_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV), 4, u)
#define fWRAP_L2_ploadritnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEW(PtN), 4, u)
#define fWRAP_L2_ploadrifnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN), 4, u)
#define fWRAP_L4_ploadrit_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV), 4, u)
#define fWRAP_L4_ploadrif_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV), 4, u)
#define fWRAP_L4_ploadritnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN), 4, u)
#define fWRAP_L4_ploadrifnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN), 4, u)
#define fWRAP_L2_ploadritnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN), 4, u)
#define fWRAP_L2_ploadrifnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN), 4, u)
#define fWRAP_L4_ploadrit_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLD(PtV), 4, u)
#define fWRAP_L4_ploadrif_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBOLDNOT(PtV), 4, u)
#define fWRAP_L4_ploadritnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEW(PtN), 4, u)
#define fWRAP_L4_ploadrifnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD(fEA_IMM(uiV), fLSBNEWNOT(PtN), 4, u)

#define fWRAP_PRED_LOAD_PAIR(GET_EA, PRED) \
{ \
    TCGv LSB = tcg_temp_local_new(); \
    TCGLabel *label = gen_new_label(); \
    GET_EA; \
    PRED;  \
    PRED_LOAD_CANCEL(LSB, EA); \
    tcg_gen_movi_i64(RddV, 0); \
    tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
        fLOAD(1, 8, u, EA, RddV); \
    gen_set_label(label); \
    tcg_temp_free(LSB); \
}

#define fWRAP_L2_ploadrdt_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RI(RsV, uiV), fLSBOLD(PtV))
#define fWRAP_L2_ploadrdt_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLD(PtV))
#define fWRAP_L2_ploadrdf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RI(RsV, uiV), fLSBOLDNOT(PtV))
#define fWRAP_L2_ploadrdf_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBOLDNOT(PtV))
#define fWRAP_L2_ploadrdtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RI(RsV, uiV), fLSBNEW(PtN))
#define fWRAP_L2_ploadrdfnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RI(RsV, uiV), fLSBNEWNOT(PtN))
#define fWRAP_L4_ploadrdt_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RRs(RsV, RtV, uiV), fLSBOLD(PvV))
#define fWRAP_L4_ploadrdf_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RRs(RsV, RtV, uiV), fLSBOLDNOT(PvV))
#define fWRAP_L4_ploadrdtnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RRs(RsV, RtV, uiV), fLSBNEW(PvN))
#define fWRAP_L4_ploadrdfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_RRs(RsV, RtV, uiV), fLSBNEWNOT(PvN))
#define fWRAP_L2_ploadrdtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEW(PtN))
#define fWRAP_L2_ploadrdfnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR({ fEA_REG(RxV); fPM_I(RxV, siV); }, fLSBNEWNOT(PtN))
#define fWRAP_L4_ploadrdt_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_IMM(uiV), fLSBOLD(PtV))
#define fWRAP_L4_ploadrdf_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_IMM(uiV), fLSBOLDNOT(PtV))
#define fWRAP_L4_ploadrdtnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_IMM(uiV), fLSBNEW(PtN))
#define fWRAP_L4_ploadrdfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_LOAD_PAIR(fEA_IMM(uiV), fLSBNEWNOT(PtN))

#define fWRAP_L2_loadw_locked(GENHLPR, SHORTCODE) \
    SHORTCODE
#define fWRAP_L4_loadd_locked(GENHLPR, SHORTCODE) \
    SHORTCODE

#define fWRAP_S2_storew_locked(GENHLPR, SHORTCODE) \
    { SHORTCODE; READ_PREG(PdV, PdN); }
#define fWRAP_S4_stored_locked(GENHLPR, SHORTCODE) \
    { SHORTCODE; READ_PREG(PdV, PdN); }

#define fWRAP_STORE(SHORTCODE) \
{ \
    TCGv HALF = tcg_temp_new(); \
    TCGv BYTE = tcg_temp_new(); \
    TCGv NEWREG_ST = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    SHORTCODE; \
    tcg_temp_free(HALF); \
    tcg_temp_free(BYTE); \
    tcg_temp_free(NEWREG_ST); \
    tcg_temp_free(tmp); \
}

#define fWRAP_STORE_ap(STORE) \
{ \
    TCGv HALF = tcg_temp_new(); \
    TCGv BYTE = tcg_temp_new(); \
    TCGv NEWREG_ST = tcg_temp_new(); \
    { \
        fEA_IMM(UiV); \
        STORE; \
        tcg_gen_mov_tl(ReV, UiV); \
    } \
    tcg_temp_free(HALF); \
    tcg_temp_free(BYTE); \
    tcg_temp_free(NEWREG_ST); \
}

#define fWRAP_STORE_pcr(SHIFT, STORE) \
{ \
    TCGv ireg = tcg_temp_new(); \
    TCGv HALF = tcg_temp_new(); \
    TCGv BYTE = tcg_temp_new(); \
    TCGv NEWREG_ST = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    fEA_REG(RxV); \
    fPM_CIRR(RxV, fREAD_IREG(MuV, SHIFT), MuV); \
    STORE; \
    tcg_temp_free(ireg); \
    tcg_temp_free(HALF); \
    tcg_temp_free(BYTE); \
    tcg_temp_free(NEWREG_ST); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_storerb_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerb_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerb_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 1, EA, fGETBYTE(0, RtV)))
#define fWRAP_S2_storerb_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerb_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerb_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerb_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerb_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(0, fSTORE(1, 1, EA, fGETBYTE(0, RtV)))
#define fWRAP_S4_storerb_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerbnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storeirb_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerbgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS1_storeb_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS2_storebi0(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerh_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerh_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerh_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 2, EA, fGETHALF(0, RtV)))
#define fWRAP_S2_storerh_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerh_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerh_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerh_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerh_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(0, RtV)))
#define fWRAP_S4_storerh_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storeirh_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerhgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS2_storeh_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerf_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerf_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerf_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 2, EA, fGETHALF(1, RtV)))
#define fWRAP_S2_storerf_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerf_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerf_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerf_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerf_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(1, RtV)))
#define fWRAP_S4_storerf_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerfgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storeri_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storeri_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storeri_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 4, EA, RtV))
#define fWRAP_S2_storeri_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storeri_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storeri_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storeri_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storeri_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(2, fSTORE(1, 4, EA, RtV))
#define fWRAP_S4_storeri_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerinew_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storeiri_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerigp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS1_storew_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS2_storew_sp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS2_storewi0(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerd_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerd_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerd_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 8, EA, RttV))
#define fWRAP_S2_storerd_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerd_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerd_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerd_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerd_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(3, fSTORE(1, 8, EA, RttV))
#define fWRAP_S4_storerd_rr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerdgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_SS2_stored_sp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerbnew_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerbnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerbnew_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 1, EA, fGETBYTE(0, fNEWREG_ST(NtN))))
#define fWRAP_S2_storerbnew_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerbnew_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerbnew_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerbnew_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerbnew_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(0, fSTORE(1, 1, EA, fGETBYTE(0, fNEWREG_ST(NtN))))
#define fWRAP_S2_storerbnewgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerhnew_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerhnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerhnew_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 2, EA, fGETHALF(0, fNEWREG_ST(NtN))))
#define fWRAP_S2_storerhnew_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerhnew_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerhnew_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerhnew_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerhnew_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(1, fSTORE(1, 2, EA, fGETHALF(0, fNEWREG_ST(NtN))))
#define fWRAP_S2_storerhnewgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

#define fWRAP_S2_storerinew_io(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerinew_pi(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerinew_ap(GENHLPR, SHORTCODE) \
    fWRAP_STORE_ap(fSTORE(1, 4, EA, fNEWREG_ST(NtN)))
#define fWRAP_S2_storerinew_pr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S4_storerinew_ur(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerinew_pbr(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerinew_pci(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)
#define fWRAP_S2_storerinew_pcr(GENHLPR, SHORTCODE) \
    fWRAP_STORE_pcr(2, fSTORE(1, 4, EA, fNEWREG_ST(NtN)))
#define fWRAP_S2_storerinewgp(GENHLPR, SHORTCODE) \
    fWRAP_STORE(SHORTCODE)

/* We have to brute force memops because they have C math in the semantics */
#define fWRAP_MEMOP(GENHLPR, SHORTCODE, SIZE, OP) \
    { \
        TCGv tmp = tcg_temp_new(); \
        fEA_RI(RsV, uiV); \
        fLOAD(1, SIZE, s, EA, tmp); \
        OP; \
        fSTORE(1, SIZE, EA, tmp); \
        tcg_temp_free(tmp); \
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

#define fWRAP_L4_add_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_add_tl(tmp, tmp, RtV))
#define fWRAP_L4_add_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_add_tl(tmp, tmp, RtV))
#define fWRAP_L4_add_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_add_tl(tmp, tmp, RtV))
#define fWRAP_L4_sub_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_sub_tl(tmp, tmp, RtV))
#define fWRAP_L4_sub_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_sub_tl(tmp, tmp, RtV))
#define fWRAP_L4_sub_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_sub_tl(tmp, tmp, RtV))
#define fWRAP_L4_and_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_and_tl(tmp, tmp, RtV))
#define fWRAP_L4_and_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_and_tl(tmp, tmp, RtV))
#define fWRAP_L4_and_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_and_tl(tmp, tmp, RtV))
#define fWRAP_L4_or_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_or_tl(tmp, tmp, RtV))
#define fWRAP_L4_or_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_or_tl(tmp, tmp, RtV))
#define fWRAP_L4_or_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_or_tl(tmp, tmp, RtV))
#define fWRAP_L4_iadd_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_add_tl(tmp, tmp, UiV))
#define fWRAP_L4_iadd_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_add_tl(tmp, tmp, UiV))
#define fWRAP_L4_iadd_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_add_tl(tmp, tmp, UiV))
#define fWRAP_L4_isub_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, tcg_gen_sub_tl(tmp, tmp, UiV))
#define fWRAP_L4_isub_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, tcg_gen_sub_tl(tmp, tmp, UiV))
#define fWRAP_L4_isub_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, tcg_gen_sub_tl(tmp, tmp, UiV))
#define fWRAP_L4_iand_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, gen_clrbit(tmp, UiV))
#define fWRAP_L4_iand_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, gen_clrbit(tmp, UiV))
#define fWRAP_L4_iand_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, gen_clrbit(tmp, UiV))
#define fWRAP_L4_ior_memopw_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 4, gen_setbit(tmp, UiV))
#define fWRAP_L4_ior_memopb_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 1, gen_setbit(tmp, UiV))
#define fWRAP_L4_ior_memoph_io(GENHLPR, SHORTCODE) \
    fWRAP_MEMOP(GENHLPR, SHORTCODE, 2, gen_setbit(tmp, UiV))


/* We have to brute force allocframe because it has C math in the semantics */
#define fWRAP_S2_allocframe(GENHLPR, SHORTCODE) \
{ \
    TCGv_i64 scramble_tmp = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    { fEA_RI(RxV, -8); \
      fSTORE(1, 8, EA, fFRAME_SCRAMBLE((fCAST8_8u(fREAD_LR()) << 32) | \
                                       fCAST4_4u(fREAD_FP()))); \
      fWRITE_FP(EA); \
      fFRAMECHECK(EA - uiV, EA); \
      tcg_gen_sub_tl(RxV, EA, uiV); \
    } \
    tcg_temp_free_i64(scramble_tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_SS2_allocframe(GENHLPR, SHORTCODE) \
{ \
    TCGv_i64 scramble_tmp = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    { fEA_RI(fREAD_SP(), -8); \
      fSTORE(1, 8, EA, fFRAME_SCRAMBLE((fCAST8_8u(fREAD_LR()) << 32) | \
                                       fCAST4_4u(fREAD_FP()))); \
      fWRITE_FP(EA); \
      fFRAMECHECK(EA - uiV, EA); \
      tcg_gen_sub_tl(tmp, EA, uiV); \
      fWRITE_SP(tmp); \
    } \
    tcg_temp_free_i64(scramble_tmp); \
    tcg_temp_free(tmp); \
}

/* Also have to brute force the deallocframe variants */
#define fWRAP_L2_deallocframe(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    { \
      fEA_REG(RsV); \
      fLOAD(1, 8, u, EA, tmp_i64); \
      tcg_gen_mov_i64(RddV, fFRAME_UNSCRAMBLE(tmp_i64)); \
      tcg_gen_addi_tl(tmp, EA, 8); \
      fWRITE_SP(tmp); \
    } \
    tcg_temp_free(tmp); \
    tcg_temp_free_i64(tmp_i64); \
}

#define fWRAP_SL2_deallocframe(GENHLPR, SHORTCODE) \
{ \
    TCGv WORD = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    { \
      fEA_REG(fREAD_FP()); \
      fLOAD(1, 8, u, EA, tmp_i64); \
      fFRAME_UNSCRAMBLE(tmp_i64); \
      fWRITE_LR(fGETWORD(1, tmp_i64)); \
      fWRITE_FP(fGETWORD(0, tmp_i64)); \
      tcg_gen_addi_tl(tmp, EA, 8); \
      fWRITE_SP(tmp); \
    } \
    tcg_temp_free(WORD); \
    tcg_temp_free(tmp); \
    tcg_temp_free_i64(tmp_i64); \
}

#define fWRAP_L4_return(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv WORD = tcg_temp_new(); \
    { \
      fEA_REG(RsV); \
      fLOAD(1, 8, u, EA, tmp_i64); \
      tcg_gen_mov_i64(RddV, fFRAME_UNSCRAMBLE(tmp_i64)); \
      tcg_gen_addi_tl(tmp, EA, 8); \
      fWRITE_SP(tmp); \
      fJUMPR(REG_LR, fGETWORD(1, RddV), COF_TYPE_JUMPR);\
    } \
    tcg_temp_free(tmp); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(WORD); \
}

#define fWRAP_SL2_return(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv WORD = tcg_temp_new(); \
    { \
      fEA_REG(fREAD_FP()); \
      fLOAD(1, 8, u, EA, tmp_i64); \
      fFRAME_UNSCRAMBLE(tmp_i64); \
      fWRITE_LR(fGETWORD(1, tmp_i64)); \
      fWRITE_FP(fGETWORD(0, tmp_i64)); \
      tcg_gen_addi_tl(tmp, EA, 8); \
      fWRITE_SP(tmp); \
      fJUMPR(REG_LR, fGETWORD(1, tmp_i64), COF_TYPE_JUMPR);\
    } \
    tcg_temp_free(tmp); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(WORD); \
}

static inline void gen_cond_return(TCGv pred, TCGv addr)
{
    TCGv zero = tcg_const_tl(0);
    tcg_gen_movcond_tl(TCG_COND_NE, hex_next_PC, pred, zero, addr, hex_next_PC);
    tcg_temp_free(zero);
}

#define fWRAP_COND_RETURN(PRED) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv_i64 LSB_i64 = tcg_temp_new_i64(); \
    TCGv zero = tcg_const_tl(0); \
    TCGv_i64 zero_i64 = tcg_const_i64(0); \
    TCGv_i64 unscramble = tcg_temp_new_i64(); \
    TCGv WORD = tcg_temp_new(); \
    TCGv SP = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    fEA_REG(RsV); \
    PRED; \
    tcg_gen_extu_i32_i64(LSB_i64, LSB); \
    fLOAD(1, 8, u, EA, tmp_i64); \
    tcg_gen_mov_i64(unscramble, fFRAME_UNSCRAMBLE(tmp_i64)); \
    READ_REG_PAIR(RddV, HEX_REG_FP); \
    tcg_gen_movcond_i64(TCG_COND_NE, RddV, LSB_i64, zero_i64, \
                        unscramble, RddV); \
    tcg_gen_mov_tl(SP, hex_gpr[HEX_REG_SP]); \
    tcg_gen_addi_tl(tmp, EA, 8); \
    tcg_gen_movcond_tl(TCG_COND_NE, SP, LSB, zero, tmp, SP); \
    fWRITE_SP(SP); \
    gen_cond_return(LSB, fGETWORD(1, RddV)); \
    tcg_temp_free(LSB); \
    tcg_temp_free_i64(LSB_i64); \
    tcg_temp_free(zero); \
    tcg_temp_free_i64(zero_i64); \
    tcg_temp_free_i64(unscramble); \
    tcg_temp_free(WORD); \
    tcg_temp_free(SP); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(tmp); \
}

#define fWRAP_L4_return_t(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBOLD(PvV))
#define fWRAP_L4_return_f(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBOLDNOT(PvV))
#define fWRAP_L4_return_tnew_pt(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBNEW(PvN))
#define fWRAP_L4_return_fnew_pt(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBNEWNOT(PvN))
#define fWRAP_L4_return_tnew_pnt(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBNEW(PvN))
#define fWRAP_L4_return_tnew_pnt(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBNEW(PvN))
#define fWRAP_L4_return_fnew_pnt(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN(fLSBNEWNOT(PvN))

#define fWRAP_COND_RETURN_SUBINSN(PRED) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv_i64 LSB_i64 = tcg_temp_new_i64(); \
    TCGv zero = tcg_const_tl(0); \
    TCGv_i64 zero_i64 = tcg_const_i64(0); \
    TCGv_i64 unscramble = tcg_temp_new_i64(); \
    TCGv_i64 RddV = tcg_temp_new_i64(); \
    TCGv WORD = tcg_temp_new(); \
    TCGv SP = tcg_temp_new(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    fEA_REG(fREAD_FP()); \
    PRED; \
    tcg_gen_extu_i32_i64(LSB_i64, LSB); \
    fLOAD(1, 8, u, EA, tmp_i64); \
    tcg_gen_mov_i64(unscramble, fFRAME_UNSCRAMBLE(tmp_i64)); \
    READ_REG_PAIR(RddV, HEX_REG_FP); \
    tcg_gen_movcond_i64(TCG_COND_NE, RddV, LSB_i64, zero_i64, \
                        unscramble, RddV); \
    tcg_gen_mov_tl(SP, hex_gpr[HEX_REG_SP]); \
    tcg_gen_addi_tl(tmp, EA, 8); \
    tcg_gen_movcond_tl(TCG_COND_NE, SP, LSB, zero, tmp, SP); \
    fWRITE_SP(SP); \
    WRITE_REG_PAIR(HEX_REG_FP, RddV); \
    gen_cond_return(LSB, fGETWORD(1, RddV)); \
    tcg_temp_free(LSB); \
    tcg_temp_free_i64(LSB_i64); \
    tcg_temp_free(zero); \
    tcg_temp_free_i64(zero_i64); \
    tcg_temp_free_i64(unscramble); \
    tcg_temp_free_i64(RddV); \
    tcg_temp_free(WORD); \
    tcg_temp_free(SP); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(tmp); \
}

#define fWRAP_SL2_return_t(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN_SUBINSN(fLSBOLD(fREAD_P0()))
#define fWRAP_SL2_return_f(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN_SUBINSN(fLSBOLDNOT(fREAD_P0()))
#define fWRAP_SL2_return_tnew(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN_SUBINSN(fLSBNEW0)
#define fWRAP_SL2_return_fnew(GENHLPR, SHORTCODE) \
    fWRAP_COND_RETURN_SUBINSN(fLSBNEW0NOT)

/* Instructions with multiple definitions */
#define fWRAP_LOAD_AP(RES, SIZE, SIGN) \
{ \
    fMUST_IMMEXT(UiV); \
    fEA_IMM(UiV); \
    fLOAD(1, SIZE, SIGN, EA, RES); \
    tcg_gen_mov_tl(ReV, UiV); \
}

#define fWRAP_L4_loadrub_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 1, u)
#define fWRAP_L4_loadrb_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 1, s)
#define fWRAP_L4_loadruh_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 2, u)
#define fWRAP_L4_loadrh_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 2, s)
#define fWRAP_L4_loadri_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RdV, 4, u)
#define fWRAP_L4_loadrd_ap(GENHLPR, SHORTCODE) \
    fWRAP_LOAD_AP(RddV, 8, u)

#define fWRAP_PCI(SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    SHORTCODE; \
    tcg_temp_free(tmp); \
}

#define fWRAP_L2_loadrub_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)
#define fWRAP_L2_loadrb_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)
#define fWRAP_L2_loadruh_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)
#define fWRAP_L2_loadrh_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)
#define fWRAP_L2_loadri_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)
#define fWRAP_L2_loadrd_pci(GENHLPR, SHORTCODE) \
      fWRAP_PCI(SHORTCODE)

#define fWRAP_PCR(SHIFT, LOAD) \
{ \
    TCGv ireg = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    fEA_REG(RxV); \
    fREAD_IREG(MuV, SHIFT); \
    gen_fcircadd(RxV, ireg, MuV, fREAD_CSREG(MuN)); \
    LOAD; \
    tcg_temp_free(tmp); \
    tcg_temp_free(ireg); \
}

#define fWRAP_L2_loadrub_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(0, fLOAD(1, 1, u, EA, RdV))
#define fWRAP_L2_loadrb_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(0, fLOAD(1, 1, s, EA, RdV))
#define fWRAP_L2_loadruh_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(1, fLOAD(1, 2, u, EA, RdV))
#define fWRAP_L2_loadrh_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(1, fLOAD(1, 2, s, EA, RdV))
#define fWRAP_L2_loadri_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(2, fLOAD(1, 4, u, EA, RdV))
#define fWRAP_L2_loadrd_pcr(GENHLPR, SHORTCODE) \
      fWRAP_PCR(3, fLOAD(1, 8, u, EA, RddV))

#define fWRAP_L2_loadrub_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrub_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrub_pi(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrb_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrb_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrb_pi(GENHLPR, SHORTCODE) \
      SHORTCODE;
#define fWRAP_L2_loadruh_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadruh_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadruh_pi(GENHLPR, SHORTCODE) \
      SHORTCODE;
#define fWRAP_L2_loadrh_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrh_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrh_pi(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadri_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadri_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadri_pi(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrd_pr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrd_pbr(GENHLPR, SHORTCODE) \
      SHORTCODE
#define fWRAP_L2_loadrd_pi(GENHLPR, SHORTCODE) \
      SHORTCODE

#define fWRAP_A4_addp_c(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv_i64 LSB_i64 = tcg_temp_new_i64(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_add_i64(RddV, RssV, RttV); \
    fLSBOLD(PxV); \
    tcg_gen_extu_i32_i64(LSB_i64, LSB); \
    tcg_gen_add_i64(RddV, RddV, LSB_i64); \
    fCARRY_FROM_ADD(RssV, RttV, LSB_i64); \
    tcg_gen_extrl_i64_i32(tmp, RssV); \
    f8BITSOF(PxV, tmp); \
    fHIDE(MARK_LATE_PRED_WRITE(PxN)) \
    tcg_temp_free(LSB); \
    tcg_temp_free_i64(LSB_i64); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(tmp); \
}

#define fWRAP_A4_subp_c(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv_i64 LSB_i64 = tcg_temp_new_i64(); \
    TCGv_i64 tmp_i64 = tcg_temp_new_i64(); \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_not_i64(tmp_i64, RttV); \
    tcg_gen_add_i64(RddV, RssV, tmp_i64); \
    fLSBOLD(PxV); \
    tcg_gen_extu_i32_i64(LSB_i64, LSB); \
    tcg_gen_add_i64(RddV, RddV, LSB_i64); \
    fCARRY_FROM_ADD(RssV, tmp_i64, LSB_i64); \
    tcg_gen_extrl_i64_i32(tmp, RssV); \
    f8BITSOF(PxV, tmp); \
    fHIDE(MARK_LATE_PRED_WRITE(PxN)) \
    tcg_temp_free(LSB); \
    tcg_temp_free_i64(LSB_i64); \
    tcg_temp_free_i64(tmp_i64); \
    tcg_temp_free(tmp); \
}

#define fWRAP_A5_ACS(GENHLPR, SHORTCODE) \
    { printf("FIXME: multiple definition inst needs check " #GENHLPR "\n"); \
      g_assert_not_reached(); \
    }

#define fWRAP_A6_vminub_RdP(GENHLPR, SHORTCODE) \
{ \
    TCGv BYTE = tcg_temp_new(); \
    TCGv left = tcg_temp_new(); \
    TCGv right = tcg_temp_new(); \
    TCGv tmp = tcg_temp_new(); \
    int i; \
    tcg_gen_movi_tl(PeV, 0); \
    tcg_gen_movi_i64(RddV, 0); \
    for (i = 0; i < 8; i++) { \
        fGETUBYTE(i, RttV); \
        tcg_gen_mov_tl(left, BYTE); \
        fGETUBYTE(i, RssV); \
        tcg_gen_mov_tl(right, BYTE); \
        tcg_gen_setcond_tl(TCG_COND_GT, tmp, left, right); \
        fSETBIT(i, PeV, tmp); \
        fMIN(tmp, left, right); \
        fSETBYTE(i, RddV, tmp); \
    } \
    tcg_temp_free(BYTE); \
    tcg_temp_free(left); \
    tcg_temp_free(right); \
    tcg_temp_free(tmp); \
}

#define fWRAP_F2_sfrecipa(GENHLPR, SHORTCODE) \
{ \
    gen_helper_sfrecipa_val(RdV, cpu_env, RsV, RtV);  \
    gen_helper_sfrecipa_pred(PeV, cpu_env, RsV, RtV);  \
}

#define fWRAP_F2_sfinvsqrta(GENHLPR, SHORTCODE) \
{ \
    gen_helper_sfinvsqrta_val(RdV, cpu_env, RsV); \
    gen_helper_sfinvsqrta_pred(PeV, cpu_env, RsV); \
}

/*
 * These instructions write things in their GENHLPR that must be
 * recorded in the disassembly context
 */

static inline void ctx_log_reg_write(DisasContext *ctx, int rnum)
{
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

#define fWRAP_J2_call(GENHLPR, SHORTCODE) \
{ \
    gen_call(riV); \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_callt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_callf(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_callr(GENHLPR, SHORTCODE) \
{ \
    gen_callr(RsV); \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_callrt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_callrf(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LR); \
}

#define fWRAP_J2_loop0r(GENHLPR, SHORTCODE) \
{ \
    gen_loop0r(RsV, riV, insn); \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
}

#define fWRAP_J2_loop1r(GENHLPR, SHORTCODE) \
{ \
    gen_loop1r(RsV, riV, insn); \
    ctx_log_reg_write(ctx, HEX_REG_LC1); \
    ctx_log_reg_write(ctx, HEX_REG_SA1); \
}

#define fWRAP_J2_loop0i(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
}

#define fWRAP_J2_loop1i(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC1); \
    ctx_log_reg_write(ctx, HEX_REG_SA1); \
}

#define fWRAP_J2_ploop1sr(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_ploop1si(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_ploop2sr(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_ploop2si(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_ploop3sr(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_ploop3si(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_SA0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_endloop01(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_reg_write(ctx, HEX_REG_LC1); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_endloop0(GENHLPR, SHORTCODE) \
{ \
    gen_endloop0(); \
    ctx_log_reg_write(ctx, HEX_REG_LC0); \
    ctx_log_pred_write(ctx, 3); \
}

#define fWRAP_J2_endloop1(GENHLPR, SHORTCODE) \
{ \
    gen_endloop1(); \
    ctx_log_reg_write(ctx, HEX_REG_LC1); \
}

#define fWRAP_J4_cmpeqi_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_EQ, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqi_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_EQ, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqi_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_EQ, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqi_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_EQ, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqi_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_EQ, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqi_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_EQ, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqi_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_EQ, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqi_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_EQ, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgti_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GT, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgti_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GT, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgti_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GT, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgti_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GT, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgti_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GT, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgti_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GT, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgti_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GT, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgti_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GT, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtui_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GTU, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtui_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GTU, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtui_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GTU, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtui_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_GTU, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtui_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GTU, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtui_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GTU, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtui_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GTU, true, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtui_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(1, TCG_COND_GTU, false, RsV, UiV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqn1_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_EQ, true, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqn1_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_EQ, false, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqn1_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_EQ, true, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqn1_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_EQ, false, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeqn1_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_EQ, true, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqn1_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_EQ, false, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqn1_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_EQ, true, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeqn1_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_EQ, false, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtn1_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_GT, true, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtn1_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_GT, false, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtn1_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_GT, true, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtn1_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(0, TCG_COND_GT, false, RsV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtn1_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_GT, true, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtn1_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_GT, false, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtn1_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_GT, true, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtn1_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_n1_jmp(1, TCG_COND_GT, false, RsV, riV); \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_tstbit0_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_tstbit0_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_tstbit0_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_tstbit0_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_tstbit0_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_tstbit0_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_tstbit0_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_tstbit0_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeq_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeq_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeq_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    gen_cmpnd_cmp_jmp(0, TCG_COND_EQ, true, RsV, RtV, riV); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeq_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpeq_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeq_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeq_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpeq_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgt_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgt_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgt_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgt_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgt_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgt_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgt_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgt_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtu_tp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtu_fp0_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtu_tp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtu_fp0_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_J4_cmpgtu_tp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtu_fp1_jump_nt(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtu_tp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_J4_cmpgtu_fp1_jump_t(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 1); \
}

#define fWRAP_S2_cabacdecbin(GENHLPR, SHORTCODE) \
{ \
    GENHLPR \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_SA1_cmpeqi(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    gen_compare(TCG_COND_EQ, tmp, RsV, uiV); \
    gen_log_pred_write(0, tmp); \
    tcg_temp_free(tmp); \
    ctx_log_pred_write(ctx, 0); \
}

#define fWRAP_SA1_addsp(GENHLPR, SHORTCODE) \
    tcg_gen_addi_tl(RdV, hex_gpr[HEX_REG_SP], IMMNO(0));

#define fWRAP_SA1_addrx(GENHLPR, SHORTCODE) \
    tcg_gen_add_tl(RxV, RxV, RsV);

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

/*
 *  These fWRAP macros are to speed up qemu
 */
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

#define fWRAP_A2_add(GENHLPR, SHORTCODE) \
    tcg_gen_add_tl(RdV, RsV, RtV);

#define fWRAP_A2_sub(GENHLPR, SHORTCODE) \
    tcg_gen_sub_tl(RdV, RtV, RsV);

#define fWRAP_A2_subri(GENHLPR, SHORTCODE) \
    tcg_gen_sub_tl(RdV, siV, RsV);

#define fWRAP_A2_addi(GENHLPR, SHORTCODE) \
    tcg_gen_add_tl(RdV, RsV, siV);

#define fWRAP_A2_and(GENHLPR, SHORTCODE) \
    tcg_gen_and_tl(RdV, RsV, RtV);

#define fWRAP_A2_andir(GENHLPR, SHORTCODE) \
    tcg_gen_and_tl(RdV, RsV, siV);

#define fWRAP_A2_xor(GENHLPR, SHORTCODE) \
    tcg_gen_xor_tl(RdV, RsV, RtV);

#define fWRAP_A2_tfr(GENHLPR, SHORTCODE) \
    tcg_gen_mov_tl(RdV, RsV);

#define fWRAP_SA1_tfr(GENHLPR, SHORTCODE) \
    tcg_gen_mov_tl(RdV, RsV);

#define fWRAP_A2_tfrsi(GENHLPR, SHORTCODE) \
    tcg_gen_mov_tl(RdV, siV);

#define fWRAP_A2_tfrcrr(GENHLPR, SHORTCODE) \
    tcg_gen_mov_tl(RdV, CsV); \

#define fWRAP_A2_tfrrcr(GENHLPR, SHORTCODE) \
    tcg_gen_mov_tl(CdV, RsV);

#define fWRAP_A2_nop(GENHLPR, SHORTCODE) \
    do { } while (0);

#define fWRAP_C2_cmpeq(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_EQ, PdV, RsV, RtV);

#define fWRAP_C4_cmpneq(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_NE, PdV, RsV, RtV);

#define fWRAP_C2_cmpgt(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_GT, PdV, RsV, RtV);

#define fWRAP_C2_cmpgtu(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_GTU, PdV, RsV, RtV);

#define fWRAP_C4_cmplte(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_LE, PdV, RsV, RtV);

#define fWRAP_C4_cmplteu(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_LEU, PdV, RsV, RtV);

#define fWRAP_C2_cmpeqp(GENHLPR, SHORTCODE) \
    gen_compare_i64(TCG_COND_EQ, PdV, RssV, RttV);

#define fWRAP_C2_cmpgtp(GENHLPR, SHORTCODE) \
    gen_compare_i64(TCG_COND_GT, PdV, RssV, RttV);

#define fWRAP_C2_cmpgtup(GENHLPR, SHORTCODE) \
    gen_compare_i64(TCG_COND_GTU, PdV, RssV, RttV);

#define fWRAP_C2_cmpeqi(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_EQ, PdV, RsV, siV);

#define fWRAP_C2_cmpgti(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_GT, PdV, RsV, siV);

#define fWRAP_C2_cmpgtui(GENHLPR, SHORTCODE) \
    gen_compare(TCG_COND_GTU, PdV, RsV, uiV);

#define fWRAP_SA1_zxtb(GENHLPR, SHORTCODE) \
    tcg_gen_ext8u_tl(RdV, RsV);

#define fWRAP_J2_jump(GENHLPR, SHORTCODE) \
    gen_jump(riV);

#define fWRAP_J2_jumpr(GENHLPR, SHORTCODE) \
    gen_write_new_pc(RsV);

#define fWRAP_J2_jumpt(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    fLSBOLD(PuV); \
    gen_cond_jump(LSB, riV); \
    tcg_temp_free(LSB); \
}

#define fWRAP_J2_jumpf(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    fLSBOLDNOT(PuV); \
    gen_cond_jump(LSB, riV); \
    tcg_temp_free(LSB); \
}

#define fWRAP_J2_jumprfnew(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    tcg_gen_andi_tl(LSB, PuN, 1); \
    tcg_gen_xori_tl(LSB, LSB, 1); \
    gen_cond_jumpr(LSB, RsV); \
    tcg_temp_free(LSB); \
}

#define fWRAP_J2_jumptnew(GENHLPR, SHORTCODE) \
    gen_cond_jump(PuN, riV);

#define fWRAP_J2_jumptnewpt(GENHLPR, SHORTCODE) \
    gen_cond_jump(PuN, riV);

#define fWRAP_J2_jumpfnewpt(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    fLSBNEWNOT(PuN); \
    gen_cond_jump(LSB, riV); \
    tcg_temp_free(LSB); \
}

#define fWRAP_J2_jumpfnew(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    fLSBNEWNOT(PuN); \
    gen_cond_jump(LSB, riV); \
    tcg_temp_free(LSB); \
}

#define fWRAP_J4_cmpgt_f_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_LE, NsX, RtV, riV);

#define fWRAP_J4_cmpeq_f_jumpnv_nt(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_NE, NsX, RtV, riV);

#define fWRAP_J4_cmpgt_t_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_GT, NsX, RtV, riV);

#define fWRAP_J4_cmpeqi_t_jumpnv_nt(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_EQ, NsX, UiV, riV);

#define fWRAP_J4_cmpltu_f_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_GEU, NsX, RtV, riV);

#define fWRAP_J4_cmpgtui_t_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_GTU, NsX, UiV, riV);

#define fWRAP_J4_cmpeq_f_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_NE, NsX, RtV, riV);

#define fWRAP_J4_cmpeqi_f_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_NE, NsX, UiV, riV);

#define fWRAP_J4_cmpgtu_t_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_GTU, NsX, RtV, riV);

#define fWRAP_J4_cmpgtu_f_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_LEU, NsX, RtV, riV);

#define fWRAP_J4_cmplt_t_jumpnv_t(GENHLPR, SHORTCODE) \
    gen_cmp_jumpnv(TCG_COND_LT, NsX, RtV, riV);

#define fWRAP_J4_jumpsetr(GENHLPR, SHORTCODE) \
{ \
    tcg_gen_mov_tl(RdV, RsV); \
    gen_jump(riV); \
}

#define fWRAP_S2_lsr_i_r(GENHLPR, SHORTCODE) \
    fLSHIFTR(RdV, RsV, IMMNO(0), 4_4);

#define fWRAP_S2_lsr_i_r_acc(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    fLSHIFTR(tmp, RsV, IMMNO(0), 4_4); \
    tcg_gen_add_tl(RxV, RxV, tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_asr_i_r(GENHLPR, SHORTCODE) \
    fASHIFTR(RdV, RsV, IMMNO(0), 4_4);

#define fWRAP_S2_lsr_i_r_xacc(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    fLSHIFTR(tmp, RsV, IMMNO(0), 4_4); \
    tcg_gen_xor_tl(RxV, RxV, tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_addasl_rrri(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    fASHIFTL(tmp, RsV, IMMNO(0), 4_4); \
    tcg_gen_add_tl(RdV, RtV, tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_asl_r_r_or(GENHLPR, SHORTCODE) \
    gen_asl_r_r_or(RxV, RsV, RtV);

#define fWRAP_S2_asl_i_r(GENHLPR, SHORTCODE) \
    tcg_gen_shli_tl(RdV, RsV, IMMNO(0));

#define fWRAP_S2_asl_i_r_or(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_shli_tl(tmp, RsV, IMMNO(0)); \
    tcg_gen_or_tl(RxV, RxV, tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_vsplatrb(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    int i; \
    tcg_gen_movi_tl(RdV, 0); \
    tcg_gen_andi_tl(tmp, RsV, 0xff); \
    for (i = 0; i < 4; i++) { \
        tcg_gen_shli_tl(RdV, RdV, 8); \
        tcg_gen_or_tl(RdV, RdV, tmp); \
    } \
    tcg_temp_free(tmp); \
}

#define fWRAP_SA1_seti(GENHLPR, SHORTCODE) \
    tcg_gen_movi_tl(RdV, IMMNO(0));

#define fWRAP_S2_insert(GENHLPR, SHORTCODE) \
    tcg_gen_deposit_i32(RxV, RxV, RsV, IMMNO(1), IMMNO(0));

#define fWRAP_S2_extractu(GENHLPR, SHORTCODE) \
    tcg_gen_extract_i32(RdV, RsV, IMMNO(1), IMMNO(0));

#define fWRAP_A2_combinew(GENHLPR, SHORTCODE) \
    tcg_gen_concat_i32_i64(RddV, RtV, RsV);

#define fWRAP_A2_combineii(GENHLPR, SHORTCODE) \
    tcg_gen_concat_i32_i64(RddV, SiV, siV);

#define fWRAP_A4_combineri(GENHLPR, SHORTCODE) \
    tcg_gen_concat_i32_i64(RddV, siV, RsV);

#define fWRAP_A4_combineir(GENHLPR, SHORTCODE) \
    tcg_gen_concat_i32_i64(RddV, RsV, siV);

#define fWRAP_A4_combineii(GENHLPR, SHORTCODE) \
    tcg_gen_concat_i32_i64(RddV, UiV, siV);

#define fWRAP_SA1_combine0i(GENHLPR, SHORTCODE) \
{ \
    TCGv zero = tcg_const_tl(0); \
    tcg_gen_concat_i32_i64(RddV, uiV, zero); \
    tcg_temp_free(zero); \
}

#define fWRAP_S4_ori_asl_ri(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_shli_tl(tmp, RxV, IMMNO(1)); \
    tcg_gen_ori_tl(RxV, tmp, IMMNO(0)); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S4_subaddi(GENHLPR, SHORTCODE) \
{ \
    tcg_gen_sub_tl(RdV, RsV, RuV); \
    tcg_gen_addi_tl(RdV, RdV, IMMNO(0)); \
}

#define fWRAP_SA1_inc(GENHLPR, SHORTCODE) \
    tcg_gen_addi_tl(RdV, RsV, 1);

#define fWRAP_SA1_dec(GENHLPR, SHORTCODE) \
    tcg_gen_subi_tl(RdV, RsV, 1);

#define fWRAP_SA1_clrtnew(GENHLPR, SHORTCODE) \
{ \
    TCGv mask = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    tcg_gen_movi_tl(RdV, 0); \
    tcg_gen_movi_tl(mask, 1 << insn->slot); \
    tcg_gen_or_tl(mask, hex_slot_cancelled, mask); \
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_slot_cancelled, \
                       hex_new_pred_value[0], zero, \
                       mask, hex_slot_cancelled); \
    tcg_temp_free(mask); \
    tcg_temp_free(zero); \
}

#define fWRAP_M4_mpyri_addr_u2(GENHLPR, SHORTCODE) \
{ \
    tcg_gen_muli_tl(RdV, RsV, IMMNO(0)); \
    tcg_gen_add_tl(RdV, RuV, RdV); \
}

#define WRAP_padd(PRED, ADD) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv mask = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    PRED; \
    ADD; \
    tcg_gen_movi_tl(mask, 1 << insn->slot); \
    tcg_gen_or_tl(mask, hex_slot_cancelled, mask); \
    tcg_gen_movcond_tl(TCG_COND_NE, hex_slot_cancelled, LSB, zero, \
                       hex_slot_cancelled, mask); \
    tcg_temp_free(LSB); \
    tcg_temp_free(mask); \
    tcg_temp_free(zero); \
}

#define fWRAP_A2_paddt(GENHLPR, SHORTCODE) \
    WRAP_padd(fLSBOLD(PuV), tcg_gen_add_tl(RdV, RsV, RtV));

#define fWRAP_A2_paddf(GENHLPR, SHORTCODE) \
    WRAP_padd(fLSBOLDNOT(PuV), tcg_gen_add_tl(RdV, RsV, RtV));

#define fWRAP_A2_paddit(GENHLPR, SHORTCODE) \
    WRAP_padd(fLSBOLD(PuV), tcg_gen_addi_tl(RdV, RsV, IMMNO(0)));

#define fWRAP_A2_paddif(GENHLPR, SHORTCODE) \
    WRAP_padd(fLSBOLDNOT(PuV), tcg_gen_addi_tl(RdV, RsV, IMMNO(0)));

#define fWRAP_A2_padditnew(GENHLPR, SHORTCODE) \
    WRAP_padd(fLSBNEW(PuN), tcg_gen_addi_tl(RdV, RsV, IMMNO(0)));

#define fWRAP_C2_cmoveit(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    TCGv mask = tcg_temp_new(); \
    fLSBOLD(PuV); \
    tcg_gen_movcond_tl(TCG_COND_NE, RdV, LSB, zero, siV, zero); \
    tcg_gen_movi_tl(mask, 1 << insn->slot); \
    tcg_gen_movcond_tl(TCG_COND_EQ, mask, LSB, zero, mask, zero); \
    tcg_gen_or_tl(hex_slot_cancelled, hex_slot_cancelled, mask); \
    tcg_temp_free(LSB); \
    tcg_temp_free(zero); \
    tcg_temp_free(mask); \
}

#define fWRAP_C2_cmovenewit(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    TCGv mask = tcg_temp_new(); \
    fLSBNEW(PuN); \
    tcg_gen_movcond_tl(TCG_COND_NE, RdV, LSB, zero, siV, zero); \
    tcg_gen_movi_tl(mask, 1 << insn->slot); \
    tcg_gen_movcond_tl(TCG_COND_EQ, mask, LSB, zero, mask, zero); \
    tcg_gen_or_tl(hex_slot_cancelled, hex_slot_cancelled, mask); \
    tcg_temp_free(LSB); \
    tcg_temp_free(zero); \
    tcg_temp_free(mask); \
}

#define fWRAP_C2_cmovenewif(GENHLPR, SHORTCODE) \
{ \
    TCGv LSB = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    TCGv mask = tcg_temp_new(); \
    fLSBNEWNOT(PuN); \
    tcg_gen_movcond_tl(TCG_COND_NE, RdV, LSB, zero, siV, zero); \
    tcg_gen_movi_tl(mask, 1 << insn->slot); \
    tcg_gen_movcond_tl(TCG_COND_EQ, mask, LSB, zero, mask, zero); \
    tcg_gen_or_tl(hex_slot_cancelled, hex_slot_cancelled, mask); \
    tcg_temp_free(LSB); \
    tcg_temp_free(zero); \
    tcg_temp_free(mask); \
}

#define fWRAP_S2_tstbit_i(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_andi_tl(tmp, RsV, (1 << IMMNO(0))); \
    gen_8bitsof(PdV, tmp); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S4_ntstbit_i(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_andi_tl(tmp, RsV, (1 << IMMNO(0))); \
    gen_8bitsof(PdV, tmp); \
    tcg_gen_xori_tl(PdV, PdV, 0xff); \
    tcg_temp_free(tmp); \
}

#define fWRAP_S2_setbit_i(GENHLPR, SHORTCODE) \
    tcg_gen_ori_tl(RdV, RsV, 1 << IMMNO(0));

#define fWRAP_M2_accii(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    tcg_gen_add_tl(tmp, RxV, RsV); \
    tcg_gen_addi_tl(RxV, tmp, IMMNO(0)); \
    tcg_temp_free(tmp); \
}

#define fWRAP_C2_bitsclri(GENHLPR, SHORTCODE) \
{ \
    TCGv tmp = tcg_temp_new(); \
    TCGv zero = tcg_const_tl(0); \
    tcg_gen_andi_tl(tmp, RsV, IMMNO(0)); \
    gen_compare(TCG_COND_EQ, PdV, tmp, zero); \
    tcg_temp_free(tmp); \
    tcg_temp_free(zero); \
}

#define fWRAP_SL2_jumpr31(GENHLPR, SHORTCODE) \
    gen_write_new_pc(hex_gpr[HEX_REG_LR]);

#define fWRAP_SL2_jumpr31_tnew(GENHLPR, SHORTCODE) \
    gen_cond_jumpr(hex_new_pred_value[0], hex_gpr[HEX_REG_LR]);

/* Predicated stores */
#define fWRAP_PRED_STORE(GET_EA, PRED, SRC, SIZE, INC) \
{ \
    TCGv LSB = tcg_temp_local_new(); \
    TCGv NEWREG_ST = tcg_temp_local_new(); \
    TCGv BYTE = tcg_temp_local_new(); \
    TCGv HALF = tcg_temp_local_new(); \
    TCGLabel *label = gen_new_label(); \
    GET_EA; \
    PRED;  \
    PRED_STORE_CANCEL(LSB, EA); \
    tcg_gen_brcondi_tl(TCG_COND_EQ, LSB, 0, label); \
        INC; \
        fSTORE(1, SIZE, EA, SRC); \
    gen_set_label(label); \
    tcg_temp_free(LSB); \
    tcg_temp_free(NEWREG_ST); \
    tcg_temp_free(BYTE); \
    tcg_temp_free(HALF); \
}

#define NOINC    do {} while (0)

#define fWRAP_S4_pstorerinewfnew_rr(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_RRs(RsV, RuV, uiV), fLSBNEWNOT(PvN), \
                     hex_new_value[NtX], 4, NOINC)

#define fWRAP_S2_pstorerdtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_REG(RxV), fLSBNEW(PvN), \
                     RttV, 8, tcg_gen_addi_tl(RxV, RxV, IMMNO(0)))

#define fWRAP_S4_pstorerdtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_RI(RsV, uiV), fLSBNEW(PvN), \
                     RttV, 8, NOINC)

#define fWRAP_S4_pstorerbtnew_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_RI(RsV, uiV), fLSBNEW(PvN), \
                     fGETBYTE(0, RtV), 1, NOINC)

#define fWRAP_S2_pstorerhtnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_REG(RxV), fLSBNEW(PvN), \
                     fGETHALF(0, RtV), 2, tcg_gen_addi_tl(RxV, RxV, IMMNO(0)))

#define fWRAP_S2_pstoreritnew_pi(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_REG(RxV), fLSBNEW(PvN), \
                     RtV, 4, tcg_gen_addi_tl(RxV, RxV, IMMNO(0)))

#define fWRAP_S2_pstorerif_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_RI(RsV, uiV), fLSBOLDNOT(PvV), \
                     RtV, 4, NOINC)

#define fWRAP_S4_pstorerit_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_IMM(uiV), fLSBOLD(PvV), \
                     RtV, 4, NOINC)

#define fWRAP_S2_pstorerinewf_io(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_RI(RsV, uiV), fLSBOLDNOT(PvV), \
                     hex_new_value[NtX], 4, NOINC)

#define fWRAP_S4_pstorerbnewfnew_abs(GENHLPR, SHORTCODE) \
    fWRAP_PRED_STORE(fEA_IMM(uiV), fLSBNEWNOT(PvN), \
                     fGETBYTE(0, hex_new_value[NtX]), 1, NOINC)

#include "qemu_wrap.h"

#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) \
static void generate_##TAG(CPUHexagonState *env, DisasContext *ctx, \
                           insn_t *insn) \
{ \
    GENFN \
}
#include "qemu.odef"
#undef DEF_QEMU

/* Fill in the table with NULLs because not all the opcodes have DEF_QEMU */
semantic_insn_t opcode_genptr[] = {
#define OPCODE(X)                              NULL,
#include "imported/opcodes.odef"
    NULL
#undef OPCODE
};

/* This function overwrites the NULL entries where we have a DEF_QEMU */
void init_opcode_genptr(void)
{
#define DEF_QEMU(TAG, SHORTCODE, HELPER, GENFN, HELPFN) \
    opcode_genptr[TAG] = generate_##TAG;
#include "qemu.odef"
#undef DEF_QEMU
}


