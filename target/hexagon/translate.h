/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#include "qemu/bitmap.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/translator.h"
#include "tcg/tcg-op.h"
#include "insn.h"
#include "internal.h"

typedef struct DisasContext {
    DisasContextBase base;
    Packet *pkt;
    Insn *insn;
    uint32_t next_PC;
    uint32_t mem_idx;
    uint32_t num_packets;
    uint32_t num_insns;
    uint32_t num_hvx_insns;
    int reg_log[REG_WRITES_MAX];
    int reg_log_idx;
    DECLARE_BITMAP(regs_written, TOTAL_PER_THREAD_REGS);
    DECLARE_BITMAP(predicated_regs, TOTAL_PER_THREAD_REGS);
    int preg_log[PRED_WRITES_MAX];
    int preg_log_idx;
    DECLARE_BITMAP(pregs_written, NUM_PREGS);
    uint8_t store_width[STORES_MAX];
    bool s1_store_processed;
    int future_vregs_idx;
    int future_vregs_num[VECTOR_TEMPS_MAX];
    int tmp_vregs_idx;
    int tmp_vregs_num[VECTOR_TEMPS_MAX];
    int vreg_log[NUM_VREGS];
    int vreg_log_idx;
    DECLARE_BITMAP(vregs_updated_tmp, NUM_VREGS);
    DECLARE_BITMAP(vregs_updated, NUM_VREGS);
    DECLARE_BITMAP(vregs_select, NUM_VREGS);
    DECLARE_BITMAP(predicated_future_vregs, NUM_VREGS);
    DECLARE_BITMAP(predicated_tmp_vregs, NUM_VREGS);
    int qreg_log[NUM_QREGS];
    int qreg_log_idx;
    bool pre_commit;
    TCGCond branch_cond;
    target_ulong branch_dest;
    bool is_tight_loop;
    bool need_pkt_has_store_s1;
} DisasContext;

static inline void ctx_log_pred_write(DisasContext *ctx, int pnum)
{
    if (!test_bit(pnum, ctx->pregs_written)) {
        ctx->preg_log[ctx->preg_log_idx] = pnum;
        ctx->preg_log_idx++;
        set_bit(pnum, ctx->pregs_written);
    }
}

static inline void ctx_log_reg_write(DisasContext *ctx, int rnum,
                                     bool is_predicated)
{
    if (rnum == HEX_REG_P3_0_ALIASED) {
        for (int i = 0; i < NUM_PREGS; i++) {
            ctx_log_pred_write(ctx, i);
        }
    } else {
        if (!test_bit(rnum, ctx->regs_written)) {
            ctx->reg_log[ctx->reg_log_idx] = rnum;
            ctx->reg_log_idx++;
            set_bit(rnum, ctx->regs_written);
        }
        if (is_predicated) {
            set_bit(rnum, ctx->predicated_regs);
        }
    }
}

static inline void ctx_log_reg_write_pair(DisasContext *ctx, int rnum,
                                          bool is_predicated)
{
    ctx_log_reg_write(ctx, rnum, is_predicated);
    ctx_log_reg_write(ctx, rnum + 1, is_predicated);
}

intptr_t ctx_future_vreg_off(DisasContext *ctx, int regnum,
                             int num, bool alloc_ok);
intptr_t ctx_tmp_vreg_off(DisasContext *ctx, int regnum,
                          int num, bool alloc_ok);

static inline void ctx_log_vreg_write(DisasContext *ctx,
                                      int rnum, VRegWriteType type,
                                      bool is_predicated)
{
    if (type != EXT_TMP) {
        if (!test_bit(rnum, ctx->vregs_updated)) {
            ctx->vreg_log[ctx->vreg_log_idx] = rnum;
            ctx->vreg_log_idx++;
            set_bit(rnum, ctx->vregs_updated);
        }

        set_bit(rnum, ctx->vregs_updated);
        if (is_predicated) {
            set_bit(rnum, ctx->predicated_future_vregs);
        }
    }
    if (type == EXT_NEW) {
        set_bit(rnum, ctx->vregs_select);
    }
    if (type == EXT_TMP) {
        set_bit(rnum, ctx->vregs_updated_tmp);
        if (is_predicated) {
            set_bit(rnum, ctx->predicated_tmp_vregs);
        }
    }
}

static inline void ctx_log_vreg_write_pair(DisasContext *ctx,
                                           int rnum, VRegWriteType type,
                                           bool is_predicated)
{
    ctx_log_vreg_write(ctx, rnum ^ 0, type, is_predicated);
    ctx_log_vreg_write(ctx, rnum ^ 1, type, is_predicated);
}

static inline void ctx_log_qreg_write(DisasContext *ctx,
                                      int rnum)
{
    ctx->qreg_log[ctx->qreg_log_idx] = rnum;
    ctx->qreg_log_idx++;
}

extern TCGv hex_gpr[TOTAL_PER_THREAD_REGS];
extern TCGv hex_pred[NUM_PREGS];
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
extern TCGv hex_llsc_addr;
extern TCGv hex_llsc_val;
extern TCGv_i64 hex_llsc_val_i64;
extern TCGv hex_vstore_addr[VSTORES_MAX];
extern TCGv hex_vstore_size[VSTORES_MAX];
extern TCGv hex_vstore_pending[VSTORES_MAX];

bool is_gather_store_insn(DisasContext *ctx);
void process_store(DisasContext *ctx, int slot_num);

FIELD(PROBE_PKT_SCALAR_STORE_S0, MMU_IDX,       0, 2)
FIELD(PROBE_PKT_SCALAR_STORE_S0, IS_PREDICATED, 2, 1)

FIELD(PROBE_PKT_SCALAR_HVX_STORES, HAS_ST0,        0, 1)
FIELD(PROBE_PKT_SCALAR_HVX_STORES, HAS_ST1,        1, 1)
FIELD(PROBE_PKT_SCALAR_HVX_STORES, HAS_HVX_STORES, 2, 1)
FIELD(PROBE_PKT_SCALAR_HVX_STORES, S0_IS_PRED,     3, 1)
FIELD(PROBE_PKT_SCALAR_HVX_STORES, S1_IS_PRED,     4, 1)

#endif
