/*
 *  Copyright(c) 2019-2020 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_TRANSLATE_H
#define HEXAGON_TRANSLATE_H

#include "cpu.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"
#include "internal.h"

typedef struct DisasContext {
    DisasContextBase base;
    uint32_t mem_idx;
    int ctx_reg_log[REG_WRITES_MAX];
    int ctx_reg_log_idx;
    int ctx_preg_log[PRED_WRITES_MAX];
    int ctx_preg_log_idx;
    uint8_t ctx_store_width[STORES_MAX];
    int ctx_temp_vregs_idx;
    int ctx_temp_qregs_idx;
    int ctx_vreg_log[NUM_VREGS];
    int ctx_vreg_is_predicated[NUM_VREGS];
    int ctx_vreg_log_idx;
    int ctx_qreg_log[NUM_QREGS];
    int ctx_qreg_is_predicated[NUM_QREGS];
    int ctx_qreg_log_idx;
} DisasContext;

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

extern TCGv hex_gpr[TOTAL_PER_THREAD_REGS];
extern TCGv hex_pred[NUM_PREGS];
extern TCGv hex_next_PC;
extern TCGv hex_this_PC;
extern TCGv hex_slot_cancelled;
extern TCGv hex_branch_taken;
extern TCGv hex_new_value[TOTAL_PER_THREAD_REGS];
extern TCGv hex_reg_written[TOTAL_PER_THREAD_REGS];
extern TCGv hex_new_pred_value[NUM_PREGS];
extern TCGv hex_pred_written;
extern TCGv hex_store_addr[STORES_MAX];
extern TCGv hex_store_width[STORES_MAX];
extern TCGv hex_store_val32[STORES_MAX];
extern TCGv_i64 hex_store_val64[STORES_MAX];
extern TCGv hex_dczero_addr;
extern TCGv llsc_addr;
extern TCGv llsc_val;
extern TCGv_i64 llsc_val_i64;
extern TCGv hex_is_gather_store_insn;
extern TCGv hex_gather_issued;
extern TCGv hex_VRegs_updated_tmp;
extern TCGv hex_VRegs_updated;
extern TCGv hex_VRegs_select;
extern TCGv hex_QRegs_updated;

static inline void gen_slot_cancelled_check(TCGv check, int slot_num)
{
    TCGv mask = tcg_const_tl(1 << slot_num);
    TCGv one = tcg_const_tl(1);
    TCGv zero = tcg_const_tl(0);

    tcg_gen_and_tl(mask, hex_slot_cancelled, mask);
    tcg_gen_movcond_tl(TCG_COND_NE, check, mask, zero, one, zero);

    tcg_temp_free(one);
    tcg_temp_free(zero);
    tcg_temp_free(mask);
}

extern void gen_exception(int excp);
extern void gen_exception_debug(void);

extern void gen_memcpy(TCGv_ptr dest, TCGv_ptr src, size_t n);

#endif
