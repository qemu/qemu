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

#define QEMU_GENERATE
#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "internal.h"
#include "attribs.h"
#include "insn.h"
#include "decode.h"
#include "translate.h"
#include "printinsn.h"

TCGv hex_gpr[TOTAL_PER_THREAD_REGS];
TCGv hex_pred[NUM_PREGS];
TCGv hex_next_PC;
TCGv hex_this_PC;
TCGv hex_slot_cancelled;
TCGv hex_branch_taken;
TCGv hex_new_value[TOTAL_PER_THREAD_REGS];
TCGv hex_reg_written[TOTAL_PER_THREAD_REGS];
TCGv hex_new_pred_value[NUM_PREGS];
TCGv hex_pred_written;
TCGv hex_store_addr[STORES_MAX];
TCGv hex_store_width[STORES_MAX];
TCGv hex_store_val32[STORES_MAX];
TCGv_i64 hex_store_val64[STORES_MAX];
TCGv hex_pkt_has_store_s1;
TCGv hex_dczero_addr;
TCGv hex_llsc_addr;
TCGv hex_llsc_val;
TCGv_i64 hex_llsc_val_i64;
TCGv hex_VRegs_updated;
TCGv hex_QRegs_updated;
TCGv hex_vstore_addr[VSTORES_MAX];
TCGv hex_vstore_size[VSTORES_MAX];
TCGv hex_vstore_pending[VSTORES_MAX];

static const char * const hexagon_prednames[] = {
  "p0", "p1", "p2", "p3"
};

intptr_t ctx_future_vreg_off(DisasContext *ctx, int regnum,
                          int num, bool alloc_ok)
{
    intptr_t offset;

    /* See if it is already allocated */
    for (int i = 0; i < ctx->future_vregs_idx; i++) {
        if (ctx->future_vregs_num[i] == regnum) {
            return offsetof(CPUHexagonState, future_VRegs[i]);
        }
    }

    g_assert(alloc_ok);
    offset = offsetof(CPUHexagonState, future_VRegs[ctx->future_vregs_idx]);
    for (int i = 0; i < num; i++) {
        ctx->future_vregs_num[ctx->future_vregs_idx + i] = regnum++;
    }
    ctx->future_vregs_idx += num;
    g_assert(ctx->future_vregs_idx <= VECTOR_TEMPS_MAX);
    return offset;
}

intptr_t ctx_tmp_vreg_off(DisasContext *ctx, int regnum,
                          int num, bool alloc_ok)
{
    intptr_t offset;

    /* See if it is already allocated */
    for (int i = 0; i < ctx->tmp_vregs_idx; i++) {
        if (ctx->tmp_vregs_num[i] == regnum) {
            return offsetof(CPUHexagonState, tmp_VRegs[i]);
        }
    }

    g_assert(alloc_ok);
    offset = offsetof(CPUHexagonState, tmp_VRegs[ctx->tmp_vregs_idx]);
    for (int i = 0; i < num; i++) {
        ctx->tmp_vregs_num[ctx->tmp_vregs_idx + i] = regnum++;
    }
    ctx->tmp_vregs_idx += num;
    g_assert(ctx->tmp_vregs_idx <= VECTOR_TEMPS_MAX);
    return offset;
}

static void gen_exception_raw(int excp)
{
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(excp));
}

static void gen_exec_counters(DisasContext *ctx)
{
    tcg_gen_addi_tl(hex_gpr[HEX_REG_QEMU_PKT_CNT],
                    hex_gpr[HEX_REG_QEMU_PKT_CNT], ctx->num_packets);
    tcg_gen_addi_tl(hex_gpr[HEX_REG_QEMU_INSN_CNT],
                    hex_gpr[HEX_REG_QEMU_INSN_CNT], ctx->num_insns);
    tcg_gen_addi_tl(hex_gpr[HEX_REG_QEMU_HVX_CNT],
                    hex_gpr[HEX_REG_QEMU_HVX_CNT], ctx->num_hvx_insns);
}

static void gen_end_tb(DisasContext *ctx)
{
    gen_exec_counters(ctx);
    tcg_gen_mov_tl(hex_gpr[HEX_REG_PC], hex_next_PC);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_end_tb(DisasContext *ctx, int excp)
{
    gen_exec_counters(ctx);
    tcg_gen_mov_tl(hex_gpr[HEX_REG_PC], hex_next_PC);
    gen_exception_raw(excp);
    ctx->base.is_jmp = DISAS_NORETURN;

}

#define PACKET_BUFFER_LEN              1028
static void print_pkt(Packet *pkt)
{
    GString *buf = g_string_sized_new(PACKET_BUFFER_LEN);
    snprint_a_pkt_debug(buf, pkt);
    HEX_DEBUG_LOG("%s", buf->str);
    g_string_free(buf, true);
}
#define HEX_DEBUG_PRINT_PKT(pkt) \
    do { \
        if (HEX_DEBUG) { \
            print_pkt(pkt); \
        } \
    } while (0)

static int read_packet_words(CPUHexagonState *env, DisasContext *ctx,
                             uint32_t words[])
{
    bool found_end = false;
    int nwords, max_words;

    memset(words, 0, PACKET_WORDS_MAX * sizeof(uint32_t));
    for (nwords = 0; !found_end && nwords < PACKET_WORDS_MAX; nwords++) {
        words[nwords] =
            translator_ldl(env, &ctx->base,
                           ctx->base.pc_next + nwords * sizeof(uint32_t));
        found_end = is_packet_end(words[nwords]);
    }
    if (!found_end) {
        /* Read too many words without finding the end */
        return 0;
    }

    /* Check for page boundary crossing */
    max_words = -(ctx->base.pc_next | TARGET_PAGE_MASK) / sizeof(uint32_t);
    if (nwords > max_words) {
        /* We can only cross a page boundary at the beginning of a TB */
        g_assert(ctx->base.num_insns == 1);
    }

    HEX_DEBUG_LOG("decode_packet: pc = 0x%x\n", ctx->base.pc_next);
    HEX_DEBUG_LOG("    words = { ");
    for (int i = 0; i < nwords; i++) {
        HEX_DEBUG_LOG("0x%x, ", words[i]);
    }
    HEX_DEBUG_LOG("}\n");

    return nwords;
}

static bool check_for_attrib(Packet *pkt, int attrib)
{
    for (int i = 0; i < pkt->num_insns; i++) {
        if (GET_ATTRIB(pkt->insn[i].opcode, attrib)) {
            return true;
        }
    }
    return false;
}

static bool need_pc(Packet *pkt)
{
    return check_for_attrib(pkt, A_IMPLICIT_READS_PC);
}

static bool need_slot_cancelled(Packet *pkt)
{
    return check_for_attrib(pkt, A_CONDEXEC);
}

static bool need_pred_written(Packet *pkt)
{
    return check_for_attrib(pkt, A_WRITES_PRED_REG);
}

static void gen_start_packet(DisasContext *ctx)
{
    Packet *pkt = ctx->pkt;
    target_ulong next_PC = ctx->base.pc_next + pkt->encod_pkt_size_in_bytes;
    int i;

    /* Clear out the disassembly context */
    ctx->reg_log_idx = 0;
    bitmap_zero(ctx->regs_written, TOTAL_PER_THREAD_REGS);
    ctx->preg_log_idx = 0;
    bitmap_zero(ctx->pregs_written, NUM_PREGS);
    ctx->future_vregs_idx = 0;
    ctx->tmp_vregs_idx = 0;
    ctx->vreg_log_idx = 0;
    bitmap_zero(ctx->vregs_updated_tmp, NUM_VREGS);
    bitmap_zero(ctx->vregs_updated, NUM_VREGS);
    bitmap_zero(ctx->vregs_select, NUM_VREGS);
    ctx->qreg_log_idx = 0;
    for (i = 0; i < STORES_MAX; i++) {
        ctx->store_width[i] = 0;
    }
    tcg_gen_movi_tl(hex_pkt_has_store_s1, pkt->pkt_has_store_s1);
    ctx->s1_store_processed = false;
    ctx->pre_commit = true;

    if (HEX_DEBUG) {
        /* Handy place to set a breakpoint before the packet executes */
        gen_helper_debug_start_packet(cpu_env);
        tcg_gen_movi_tl(hex_this_PC, ctx->base.pc_next);
    }

    /* Initialize the runtime state for packet semantics */
    if (need_pc(pkt)) {
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
    }
    if (need_slot_cancelled(pkt)) {
        tcg_gen_movi_tl(hex_slot_cancelled, 0);
    }
    if (pkt->pkt_has_cof) {
        if (pkt->pkt_has_multi_cof) {
            tcg_gen_movi_tl(hex_branch_taken, 0);
        }
        tcg_gen_movi_tl(hex_next_PC, next_PC);
    }
    if (need_pred_written(pkt)) {
        tcg_gen_movi_tl(hex_pred_written, 0);
    }

    if (pkt->pkt_has_hvx) {
        tcg_gen_movi_tl(hex_VRegs_updated, 0);
        tcg_gen_movi_tl(hex_QRegs_updated, 0);
    }
}

bool is_gather_store_insn(DisasContext *ctx)
{
    Packet *pkt = ctx->pkt;
    Insn *insn = ctx->insn;
    if (GET_ATTRIB(insn->opcode, A_CVI_NEW) &&
        insn->new_value_producer_slot == 1) {
        /* Look for gather instruction */
        for (int i = 0; i < pkt->num_insns; i++) {
            Insn *in = &pkt->insn[i];
            if (GET_ATTRIB(in->opcode, A_CVI_GATHER) && in->slot == 1) {
                return true;
            }
        }
    }
    return false;
}

/*
 * The LOG_*_WRITE macros mark most of the writes in a packet
 * However, there are some implicit writes marked as attributes
 * of the applicable instructions.
 */
static void mark_implicit_reg_write(DisasContext *ctx, int attrib, int rnum)
{
    uint16_t opcode = ctx->insn->opcode;
    if (GET_ATTRIB(opcode, attrib)) {
        /*
         * USR is used to set overflow and FP exceptions,
         * so treat it as conditional
         */
        bool is_predicated = GET_ATTRIB(opcode, A_CONDEXEC) ||
                             rnum == HEX_REG_USR;
        if (is_predicated && !is_preloaded(ctx, rnum)) {
            tcg_gen_mov_tl(hex_new_value[rnum], hex_gpr[rnum]);
        }

        ctx_log_reg_write(ctx, rnum);
    }
}

static void mark_implicit_pred_write(DisasContext *ctx, int attrib, int pnum)
{
    if (GET_ATTRIB(ctx->insn->opcode, attrib)) {
        ctx_log_pred_write(ctx, pnum);
    }
}

static void mark_implicit_reg_writes(DisasContext *ctx)
{
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_FP,  HEX_REG_FP);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_SP,  HEX_REG_SP);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_LR,  HEX_REG_LR);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_LC0, HEX_REG_LC0);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_SA0, HEX_REG_SA0);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_LC1, HEX_REG_LC1);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_SA1, HEX_REG_SA1);
    mark_implicit_reg_write(ctx, A_IMPLICIT_WRITES_USR, HEX_REG_USR);
    mark_implicit_reg_write(ctx, A_FPOP, HEX_REG_USR);
}

static void mark_implicit_pred_writes(DisasContext *ctx)
{
    mark_implicit_pred_write(ctx, A_IMPLICIT_WRITES_P0, 0);
    mark_implicit_pred_write(ctx, A_IMPLICIT_WRITES_P1, 1);
    mark_implicit_pred_write(ctx, A_IMPLICIT_WRITES_P2, 2);
    mark_implicit_pred_write(ctx, A_IMPLICIT_WRITES_P3, 3);
}

static void mark_store_width(DisasContext *ctx)
{
    uint16_t opcode = ctx->insn->opcode;
    uint32_t slot = ctx->insn->slot;
    uint8_t width = 0;

    if (GET_ATTRIB(opcode, A_SCALAR_STORE)) {
        if (GET_ATTRIB(opcode, A_MEMSIZE_1B)) {
            width |= 1;
        }
        if (GET_ATTRIB(opcode, A_MEMSIZE_2B)) {
            width |= 2;
        }
        if (GET_ATTRIB(opcode, A_MEMSIZE_4B)) {
            width |= 4;
        }
        if (GET_ATTRIB(opcode, A_MEMSIZE_8B)) {
            width |= 8;
        }
        tcg_debug_assert(is_power_of_2(width));
        ctx->store_width[slot] = width;
    }
}

static void gen_insn(DisasContext *ctx)
{
    if (ctx->insn->generate) {
        mark_implicit_reg_writes(ctx);
        ctx->insn->generate(ctx);
        mark_implicit_pred_writes(ctx);
        mark_store_width(ctx);
    } else {
        gen_exception_end_tb(ctx, HEX_EXCP_INVALID_OPCODE);
    }
}

/*
 * Helpers for generating the packet commit
 */
static void gen_reg_writes(DisasContext *ctx)
{
    int i;

    for (i = 0; i < ctx->reg_log_idx; i++) {
        int reg_num = ctx->reg_log[i];

        tcg_gen_mov_tl(hex_gpr[reg_num], hex_new_value[reg_num]);
    }
}

static void gen_pred_writes(DisasContext *ctx)
{
    int i;

    /* Early exit if the log is empty */
    if (!ctx->preg_log_idx) {
        return;
    }

    /*
     * Only endloop instructions will conditionally
     * write a predicate.  If there are no endloop
     * instructions, we can use the non-conditional
     * write of the predicates.
     */
    if (ctx->pkt->pkt_has_endloop) {
        TCGv zero = tcg_constant_tl(0);
        TCGv pred_written = tcg_temp_new();
        for (i = 0; i < ctx->preg_log_idx; i++) {
            int pred_num = ctx->preg_log[i];

            tcg_gen_andi_tl(pred_written, hex_pred_written, 1 << pred_num);
            tcg_gen_movcond_tl(TCG_COND_NE, hex_pred[pred_num],
                               pred_written, zero,
                               hex_new_pred_value[pred_num],
                               hex_pred[pred_num]);
        }
        tcg_temp_free(pred_written);
    } else {
        for (i = 0; i < ctx->preg_log_idx; i++) {
            int pred_num = ctx->preg_log[i];
            tcg_gen_mov_tl(hex_pred[pred_num], hex_new_pred_value[pred_num]);
            if (HEX_DEBUG) {
                /* Do this so HELPER(debug_commit_end) will know */
                tcg_gen_ori_tl(hex_pred_written, hex_pred_written,
                               1 << pred_num);
            }
        }
    }
}

static void gen_check_store_width(DisasContext *ctx, int slot_num)
{
    if (HEX_DEBUG) {
        TCGv slot = tcg_constant_tl(slot_num);
        TCGv check = tcg_constant_tl(ctx->store_width[slot_num]);
        gen_helper_debug_check_store_width(cpu_env, slot, check);
    }
}

static bool slot_is_predicated(Packet *pkt, int slot_num)
{
    for (int i = 0; i < pkt->num_insns; i++) {
        if (pkt->insn[i].slot == slot_num) {
            return GET_ATTRIB(pkt->insn[i].opcode, A_CONDEXEC);
        }
    }
    /* If we get to here, we didn't find an instruction in the requested slot */
    g_assert_not_reached();
}

void process_store(DisasContext *ctx, int slot_num)
{
    bool is_predicated = slot_is_predicated(ctx->pkt, slot_num);
    TCGLabel *label_end = NULL;

    /*
     * We may have already processed this store
     * See CHECK_NOSHUF in macros.h
     */
    if (slot_num == 1 && ctx->s1_store_processed) {
        return;
    }
    ctx->s1_store_processed = true;

    if (is_predicated) {
        TCGv cancelled = tcg_temp_new();
        label_end = gen_new_label();

        /* Don't do anything if the slot was cancelled */
        tcg_gen_extract_tl(cancelled, hex_slot_cancelled, slot_num, 1);
        tcg_gen_brcondi_tl(TCG_COND_NE, cancelled, 0, label_end);
        tcg_temp_free(cancelled);
    }
    {
        TCGv address = tcg_temp_local_new();
        tcg_gen_mov_tl(address, hex_store_addr[slot_num]);

        /*
         * If we know the width from the DisasContext, we can
         * generate much cleaner code.
         * Unfortunately, not all instructions execute the fSTORE
         * macro during code generation.  Anything that uses the
         * generic helper will have this problem.  Instructions
         * that use fWRAP to generate proper TCG code will be OK.
         */
        switch (ctx->store_width[slot_num]) {
        case 1:
            gen_check_store_width(ctx, slot_num);
            tcg_gen_qemu_st8(hex_store_val32[slot_num],
                             hex_store_addr[slot_num],
                             ctx->mem_idx);
            break;
        case 2:
            gen_check_store_width(ctx, slot_num);
            tcg_gen_qemu_st16(hex_store_val32[slot_num],
                              hex_store_addr[slot_num],
                              ctx->mem_idx);
            break;
        case 4:
            gen_check_store_width(ctx, slot_num);
            tcg_gen_qemu_st32(hex_store_val32[slot_num],
                              hex_store_addr[slot_num],
                              ctx->mem_idx);
            break;
        case 8:
            gen_check_store_width(ctx, slot_num);
            tcg_gen_qemu_st64(hex_store_val64[slot_num],
                              hex_store_addr[slot_num],
                              ctx->mem_idx);
            break;
        default:
            {
                /*
                 * If we get to here, we don't know the width at
                 * TCG generation time, we'll use a helper to
                 * avoid branching based on the width at runtime.
                 */
                TCGv slot = tcg_constant_tl(slot_num);
                gen_helper_commit_store(cpu_env, slot);
            }
        }
        tcg_temp_free(address);
    }
    if (is_predicated) {
        gen_set_label(label_end);
    }
}

static void process_store_log(DisasContext *ctx)
{
    /*
     *  When a packet has two stores, the hardware processes
     *  slot 1 and then slot 0.  This will be important when
     *  the memory accesses overlap.
     */
    Packet *pkt = ctx->pkt;
    if (pkt->pkt_has_store_s1) {
        g_assert(!pkt->pkt_has_dczeroa);
        process_store(ctx, 1);
    }
    if (pkt->pkt_has_store_s0) {
        g_assert(!pkt->pkt_has_dczeroa);
        process_store(ctx, 0);
    }
}

/* Zero out a 32-bit cache line */
static void process_dczeroa(DisasContext *ctx)
{
    if (ctx->pkt->pkt_has_dczeroa) {
        /* Store 32 bytes of zero starting at (addr & ~0x1f) */
        TCGv addr = tcg_temp_new();
        TCGv_i64 zero = tcg_constant_i64(0);

        tcg_gen_andi_tl(addr, hex_dczero_addr, ~0x1f);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);

        tcg_temp_free(addr);
    }
}

static bool pkt_has_hvx_store(Packet *pkt)
{
    int i;
    for (i = 0; i < pkt->num_insns; i++) {
        int opcode = pkt->insn[i].opcode;
        if (GET_ATTRIB(opcode, A_CVI) && GET_ATTRIB(opcode, A_STORE)) {
            return true;
        }
    }
    return false;
}

static void gen_commit_hvx(DisasContext *ctx)
{
    int i;

    /*
     *    for (i = 0; i < ctx->vreg_log_idx; i++) {
     *        int rnum = ctx->vreg_log[i];
     *        if (ctx->vreg_is_predicated[i]) {
     *            if (env->VRegs_updated & (1 << rnum)) {
     *                env->VRegs[rnum] = env->future_VRegs[rnum];
     *            }
     *        } else {
     *            env->VRegs[rnum] = env->future_VRegs[rnum];
     *        }
     *    }
     */
    for (i = 0; i < ctx->vreg_log_idx; i++) {
        int rnum = ctx->vreg_log[i];
        bool is_predicated = ctx->vreg_is_predicated[i];
        intptr_t dstoff = offsetof(CPUHexagonState, VRegs[rnum]);
        intptr_t srcoff = ctx_future_vreg_off(ctx, rnum, 1, false);
        size_t size = sizeof(MMVector);

        if (is_predicated) {
            TCGv cmp = tcg_temp_new();
            TCGLabel *label_skip = gen_new_label();

            tcg_gen_andi_tl(cmp, hex_VRegs_updated, 1 << rnum);
            tcg_gen_brcondi_tl(TCG_COND_EQ, cmp, 0, label_skip);
            tcg_temp_free(cmp);
            tcg_gen_gvec_mov(MO_64, dstoff, srcoff, size, size);
            gen_set_label(label_skip);
        } else {
            tcg_gen_gvec_mov(MO_64, dstoff, srcoff, size, size);
        }
    }

    /*
     *    for (i = 0; i < ctx->qreg_log_idx; i++) {
     *        int rnum = ctx->qreg_log[i];
     *        if (ctx->qreg_is_predicated[i]) {
     *            if (env->QRegs_updated) & (1 << rnum)) {
     *                env->QRegs[rnum] = env->future_QRegs[rnum];
     *            }
     *        } else {
     *            env->QRegs[rnum] = env->future_QRegs[rnum];
     *        }
     *    }
     */
    for (i = 0; i < ctx->qreg_log_idx; i++) {
        int rnum = ctx->qreg_log[i];
        bool is_predicated = ctx->qreg_is_predicated[i];
        intptr_t dstoff = offsetof(CPUHexagonState, QRegs[rnum]);
        intptr_t srcoff = offsetof(CPUHexagonState, future_QRegs[rnum]);
        size_t size = sizeof(MMQReg);

        if (is_predicated) {
            TCGv cmp = tcg_temp_new();
            TCGLabel *label_skip = gen_new_label();

            tcg_gen_andi_tl(cmp, hex_QRegs_updated, 1 << rnum);
            tcg_gen_brcondi_tl(TCG_COND_EQ, cmp, 0, label_skip);
            tcg_temp_free(cmp);
            tcg_gen_gvec_mov(MO_64, dstoff, srcoff, size, size);
            gen_set_label(label_skip);
        } else {
            tcg_gen_gvec_mov(MO_64, dstoff, srcoff, size, size);
        }
    }

    if (pkt_has_hvx_store(ctx->pkt)) {
        gen_helper_commit_hvx_stores(cpu_env);
    }
}

static void update_exec_counters(DisasContext *ctx)
{
    Packet *pkt = ctx->pkt;
    int num_insns = pkt->num_insns;
    int num_real_insns = 0;
    int num_hvx_insns = 0;

    for (int i = 0; i < num_insns; i++) {
        if (!pkt->insn[i].is_endloop &&
            !pkt->insn[i].part1 &&
            !GET_ATTRIB(pkt->insn[i].opcode, A_IT_NOP)) {
            num_real_insns++;
        }
        if (GET_ATTRIB(pkt->insn[i].opcode, A_CVI)) {
            num_hvx_insns++;
        }
    }

    ctx->num_packets++;
    ctx->num_insns += num_real_insns;
    ctx->num_hvx_insns += num_hvx_insns;
}

static void gen_commit_packet(DisasContext *ctx)
{
    /*
     * If there is more than one store in a packet, make sure they are all OK
     * before proceeding with the rest of the packet commit.
     *
     * dczeroa has to be the only store operation in the packet, so we go
     * ahead and process that first.
     *
     * When there is an HVX store, there can also be a scalar store in either
     * slot 0 or slot1, so we create a mask for the helper to indicate what
     * work to do.
     *
     * When there are two scalar stores, we probe the one in slot 0.
     *
     * Note that we don't call the probe helper for packets with only one
     * store.  Therefore, we call process_store_log before anything else
     * involved in committing the packet.
     */
    Packet *pkt = ctx->pkt;
    bool has_store_s0 = pkt->pkt_has_store_s0;
    bool has_store_s1 = (pkt->pkt_has_store_s1 && !ctx->s1_store_processed);
    bool has_hvx_store = pkt_has_hvx_store(pkt);
    if (pkt->pkt_has_dczeroa) {
        /*
         * The dczeroa will be the store in slot 0, check that we don't have
         * a store in slot 1 or an HVX store.
         */
        g_assert(!has_store_s1 && !has_hvx_store);
        process_dczeroa(ctx);
    } else if (has_hvx_store) {
        TCGv mem_idx = tcg_constant_tl(ctx->mem_idx);

        if (!has_store_s0 && !has_store_s1) {
            gen_helper_probe_hvx_stores(cpu_env, mem_idx);
        } else {
            int mask = 0;
            TCGv mask_tcgv;

            if (has_store_s0) {
                mask |= (1 << 0);
            }
            if (has_store_s1) {
                mask |= (1 << 1);
            }
            if (has_hvx_store) {
                mask |= (1 << 2);
            }
            mask_tcgv = tcg_constant_tl(mask);
            gen_helper_probe_pkt_scalar_hvx_stores(cpu_env, mask_tcgv, mem_idx);
        }
    } else if (has_store_s0 && has_store_s1) {
        /*
         * process_store_log will execute the slot 1 store first,
         * so we only have to probe the store in slot 0
         */
        TCGv mem_idx = tcg_constant_tl(ctx->mem_idx);
        gen_helper_probe_pkt_scalar_store_s0(cpu_env, mem_idx);
    }

    process_store_log(ctx);

    gen_reg_writes(ctx);
    gen_pred_writes(ctx);
    if (pkt->pkt_has_hvx) {
        gen_commit_hvx(ctx);
    }
    update_exec_counters(ctx);
    if (HEX_DEBUG) {
        TCGv has_st0 =
            tcg_constant_tl(pkt->pkt_has_store_s0 && !pkt->pkt_has_dczeroa);
        TCGv has_st1 =
            tcg_constant_tl(pkt->pkt_has_store_s1 && !pkt->pkt_has_dczeroa);

        /* Handy place to set a breakpoint at the end of execution */
        gen_helper_debug_commit_end(cpu_env, has_st0, has_st1);
    }

    if (pkt->vhist_insn != NULL) {
        ctx->pre_commit = false;
        ctx->insn = pkt->vhist_insn;
        pkt->vhist_insn->generate(ctx);
    }

    if (pkt->pkt_has_cof) {
        gen_end_tb(ctx);
    }
}

static void decode_and_translate_packet(CPUHexagonState *env, DisasContext *ctx)
{
    uint32_t words[PACKET_WORDS_MAX];
    int nwords;
    Packet pkt;
    int i;

    nwords = read_packet_words(env, ctx, words);
    if (!nwords) {
        gen_exception_end_tb(ctx, HEX_EXCP_INVALID_PACKET);
        return;
    }

    if (decode_packet(nwords, words, &pkt, false) > 0) {
        HEX_DEBUG_PRINT_PKT(&pkt);
        ctx->pkt = &pkt;
        gen_start_packet(ctx);
        for (i = 0; i < pkt.num_insns; i++) {
            ctx->insn = &pkt.insn[i];
            gen_insn(ctx);
        }
        gen_commit_packet(ctx);
        ctx->base.pc_next += pkt.encod_pkt_size_in_bytes;
    } else {
        gen_exception_end_tb(ctx, HEX_EXCP_INVALID_PACKET);
    }
}

static void hexagon_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->mem_idx = MMU_USER_IDX;
    ctx->num_packets = 0;
    ctx->num_insns = 0;
    ctx->num_hvx_insns = 0;
}

static void hexagon_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void hexagon_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool pkt_crosses_page(CPUHexagonState *env, DisasContext *ctx)
{
    target_ulong page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
    bool found_end = false;
    int nwords;

    for (nwords = 0; !found_end && nwords < PACKET_WORDS_MAX; nwords++) {
        uint32_t word = cpu_ldl_code(env,
                            ctx->base.pc_next + nwords * sizeof(uint32_t));
        found_end = is_packet_end(word);
    }
    uint32_t next_ptr =  ctx->base.pc_next + nwords * sizeof(uint32_t);
    return found_end && next_ptr - page_start >= TARGET_PAGE_SIZE;
}

static void hexagon_tr_translate_packet(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUHexagonState *env = cpu->env_ptr;

    decode_and_translate_packet(env, ctx);

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        target_ulong bytes_max = PACKET_WORDS_MAX * sizeof(target_ulong);

        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE ||
            (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE - bytes_max &&
             pkt_crosses_page(env, ctx))) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }

        /*
         * The CPU log is used to compare against LLDB single stepping,
         * so end the TLB after every packet.
         */
        HexagonCPU *hex_cpu = env_archcpu(env);
        if (hex_cpu->lldb_compat && qemu_loglevel_mask(CPU_LOG_TB_CPU)) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void hexagon_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_exec_counters(ctx);
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
        tcg_gen_exit_tb(NULL, 0);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void hexagon_tr_disas_log(const DisasContextBase *dcbase,
                                 CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}


static const TranslatorOps hexagon_tr_ops = {
    .init_disas_context = hexagon_tr_init_disas_context,
    .tb_start           = hexagon_tr_tb_start,
    .insn_start         = hexagon_tr_insn_start,
    .translate_insn     = hexagon_tr_translate_packet,
    .tb_stop            = hexagon_tr_tb_stop,
    .disas_log          = hexagon_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc,
                    &hexagon_tr_ops, &ctx.base);
}

#define NAME_LEN               64
static char new_value_names[TOTAL_PER_THREAD_REGS][NAME_LEN];
static char reg_written_names[TOTAL_PER_THREAD_REGS][NAME_LEN];
static char new_pred_value_names[NUM_PREGS][NAME_LEN];
static char store_addr_names[STORES_MAX][NAME_LEN];
static char store_width_names[STORES_MAX][NAME_LEN];
static char store_val32_names[STORES_MAX][NAME_LEN];
static char store_val64_names[STORES_MAX][NAME_LEN];
static char vstore_addr_names[VSTORES_MAX][NAME_LEN];
static char vstore_size_names[VSTORES_MAX][NAME_LEN];
static char vstore_pending_names[VSTORES_MAX][NAME_LEN];

void hexagon_translate_init(void)
{
    int i;

    opcode_init();

    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        hex_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, gpr[i]),
            hexagon_regnames[i]);

        snprintf(new_value_names[i], NAME_LEN, "new_%s", hexagon_regnames[i]);
        hex_new_value[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, new_value[i]),
            new_value_names[i]);

        if (HEX_DEBUG) {
            snprintf(reg_written_names[i], NAME_LEN, "reg_written_%s",
                     hexagon_regnames[i]);
            hex_reg_written[i] = tcg_global_mem_new(cpu_env,
                offsetof(CPUHexagonState, reg_written[i]),
                reg_written_names[i]);
        }
    }
    for (i = 0; i < NUM_PREGS; i++) {
        hex_pred[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, pred[i]),
            hexagon_prednames[i]);

        snprintf(new_pred_value_names[i], NAME_LEN, "new_pred_%s",
                 hexagon_prednames[i]);
        hex_new_pred_value[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, new_pred_value[i]),
            new_pred_value_names[i]);
    }
    hex_pred_written = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, pred_written), "pred_written");
    hex_next_PC = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, next_PC), "next_PC");
    hex_this_PC = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, this_PC), "this_PC");
    hex_slot_cancelled = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, slot_cancelled), "slot_cancelled");
    hex_branch_taken = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, branch_taken), "branch_taken");
    hex_pkt_has_store_s1 = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, pkt_has_store_s1), "pkt_has_store_s1");
    hex_dczero_addr = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, dczero_addr), "dczero_addr");
    hex_llsc_addr = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, llsc_addr), "llsc_addr");
    hex_llsc_val = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, llsc_val), "llsc_val");
    hex_llsc_val_i64 = tcg_global_mem_new_i64(cpu_env,
        offsetof(CPUHexagonState, llsc_val_i64), "llsc_val_i64");
    hex_VRegs_updated = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, VRegs_updated), "VRegs_updated");
    hex_QRegs_updated = tcg_global_mem_new(cpu_env,
        offsetof(CPUHexagonState, QRegs_updated), "QRegs_updated");
    for (i = 0; i < STORES_MAX; i++) {
        snprintf(store_addr_names[i], NAME_LEN, "store_addr_%d", i);
        hex_store_addr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, mem_log_stores[i].va),
            store_addr_names[i]);

        snprintf(store_width_names[i], NAME_LEN, "store_width_%d", i);
        hex_store_width[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, mem_log_stores[i].width),
            store_width_names[i]);

        snprintf(store_val32_names[i], NAME_LEN, "store_val32_%d", i);
        hex_store_val32[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, mem_log_stores[i].data32),
            store_val32_names[i]);

        snprintf(store_val64_names[i], NAME_LEN, "store_val64_%d", i);
        hex_store_val64[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPUHexagonState, mem_log_stores[i].data64),
            store_val64_names[i]);
    }
    for (int i = 0; i < VSTORES_MAX; i++) {
        snprintf(vstore_addr_names[i], NAME_LEN, "vstore_addr_%d", i);
        hex_vstore_addr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, vstore[i].va),
            vstore_addr_names[i]);

        snprintf(vstore_size_names[i], NAME_LEN, "vstore_size_%d", i);
        hex_vstore_size[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, vstore[i].size),
            vstore_size_names[i]);

        snprintf(vstore_pending_names[i], NAME_LEN, "vstore_pending_%d", i);
        hex_vstore_pending[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, vstore_pending[i]),
            vstore_pending_names[i]);
    }
}
