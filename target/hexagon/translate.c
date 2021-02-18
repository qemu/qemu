/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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
#if HEX_DEBUG
TCGv hex_reg_written[TOTAL_PER_THREAD_REGS];
#endif
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

static const char * const hexagon_prednames[] = {
  "p0", "p1", "p2", "p3"
};

void gen_exception(int excp)
{
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
}

void gen_exception_debug(void)
{
    gen_exception(EXCP_DEBUG);
}

#if HEX_DEBUG
#define PACKET_BUFFER_LEN              1028
static void print_pkt(Packet *pkt)
{
    GString *buf = g_string_sized_new(PACKET_BUFFER_LEN);
    snprint_a_pkt_debug(buf, pkt);
    HEX_DEBUG_LOG("%s", buf->str);
    g_string_free(buf, true);
}
#define HEX_DEBUG_PRINT_PKT(pkt)  print_pkt(pkt)
#else
#define HEX_DEBUG_PRINT_PKT(pkt)  /* nothing */
#endif

static int read_packet_words(CPUHexagonState *env, DisasContext *ctx,
                             uint32_t words[])
{
    bool found_end = false;
    int nwords, max_words;

    memset(words, 0, PACKET_WORDS_MAX * sizeof(uint32_t));
    for (nwords = 0; !found_end && nwords < PACKET_WORDS_MAX; nwords++) {
        words[nwords] = cpu_ldl_code(env,
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

static void gen_start_packet(DisasContext *ctx, Packet *pkt)
{
    target_ulong next_PC = ctx->base.pc_next + pkt->encod_pkt_size_in_bytes;
    int i;

    /* Clear out the disassembly context */
    ctx->reg_log_idx = 0;
    bitmap_zero(ctx->regs_written, TOTAL_PER_THREAD_REGS);
    ctx->preg_log_idx = 0;
    for (i = 0; i < STORES_MAX; i++) {
        ctx->store_width[i] = 0;
    }
    tcg_gen_movi_tl(hex_pkt_has_store_s1, pkt->pkt_has_store_s1);
    ctx->s1_store_processed = 0;

#if HEX_DEBUG
    /* Handy place to set a breakpoint before the packet executes */
    gen_helper_debug_start_packet(cpu_env);
    tcg_gen_movi_tl(hex_this_PC, ctx->base.pc_next);
#endif

    /* Initialize the runtime state for packet semantics */
    if (need_pc(pkt)) {
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
    }
    if (need_slot_cancelled(pkt)) {
        tcg_gen_movi_tl(hex_slot_cancelled, 0);
    }
    if (pkt->pkt_has_cof) {
        tcg_gen_movi_tl(hex_branch_taken, 0);
        tcg_gen_movi_tl(hex_next_PC, next_PC);
    }
    if (need_pred_written(pkt)) {
        tcg_gen_movi_tl(hex_pred_written, 0);
    }
}

/*
 * The LOG_*_WRITE macros mark most of the writes in a packet
 * However, there are some implicit writes marked as attributes
 * of the applicable instructions.
 */
static void mark_implicit_reg_write(DisasContext *ctx, Insn *insn,
                                    int attrib, int rnum)
{
    if (GET_ATTRIB(insn->opcode, attrib)) {
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC);
        if (is_predicated && !is_preloaded(ctx, rnum)) {
            tcg_gen_mov_tl(hex_new_value[rnum], hex_gpr[rnum]);
        }

        ctx_log_reg_write(ctx, rnum);
    }
}

static void mark_implicit_pred_write(DisasContext *ctx, Insn *insn,
                                     int attrib, int pnum)
{
    if (GET_ATTRIB(insn->opcode, attrib)) {
        ctx_log_pred_write(ctx, pnum);
    }
}

static void mark_implicit_writes(DisasContext *ctx, Insn *insn)
{
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_FP,  HEX_REG_FP);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_SP,  HEX_REG_SP);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_LR,  HEX_REG_LR);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_LC0, HEX_REG_LC0);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_SA0, HEX_REG_SA0);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_LC1, HEX_REG_LC1);
    mark_implicit_reg_write(ctx, insn, A_IMPLICIT_WRITES_SA1, HEX_REG_SA1);

    mark_implicit_pred_write(ctx, insn, A_IMPLICIT_WRITES_P0, 0);
    mark_implicit_pred_write(ctx, insn, A_IMPLICIT_WRITES_P1, 1);
    mark_implicit_pred_write(ctx, insn, A_IMPLICIT_WRITES_P2, 2);
    mark_implicit_pred_write(ctx, insn, A_IMPLICIT_WRITES_P3, 3);
}

static void gen_insn(CPUHexagonState *env, DisasContext *ctx,
                     Insn *insn, Packet *pkt)
{
    if (insn->generate) {
        mark_implicit_writes(ctx, insn);
        insn->generate(env, ctx, insn, pkt);
    } else {
        gen_exception(HEX_EXCP_INVALID_OPCODE);
        ctx->base.is_jmp = DISAS_NORETURN;
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

static void gen_pred_writes(DisasContext *ctx, Packet *pkt)
{
    TCGv zero, control_reg, pval;
    int i;

    /* Early exit if the log is empty */
    if (!ctx->preg_log_idx) {
        return;
    }

    zero = tcg_const_tl(0);
    control_reg = tcg_temp_new();
    pval = tcg_temp_new();

    /*
     * Only endloop instructions will conditionally
     * write a predicate.  If there are no endloop
     * instructions, we can use the non-conditional
     * write of the predicates.
     */
    if (pkt->pkt_has_endloop) {
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
#if HEX_DEBUG
            /* Do this so HELPER(debug_commit_end) will know */
            tcg_gen_ori_tl(hex_pred_written, hex_pred_written, 1 << pred_num);
#endif
        }
    }

    tcg_temp_free(zero);
    tcg_temp_free(control_reg);
    tcg_temp_free(pval);
}

#if HEX_DEBUG
static inline void gen_check_store_width(DisasContext *ctx, int slot_num)
{
    TCGv slot = tcg_const_tl(slot_num);
    TCGv check = tcg_const_tl(ctx->store_width[slot_num]);
    gen_helper_debug_check_store_width(cpu_env, slot, check);
    tcg_temp_free(slot);
    tcg_temp_free(check);
}
#define HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num) \
    gen_check_store_width(ctx, slot_num)
#else
#define HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num)  /* nothing */
#endif

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

void process_store(DisasContext *ctx, Packet *pkt, int slot_num)
{
    bool is_predicated = slot_is_predicated(pkt, slot_num);
    TCGLabel *label_end = NULL;

    /*
     * We may have already processed this store
     * See CHECK_NOSHUF in macros.h
     */
    if (slot_num == 1 && ctx->s1_store_processed) {
        return;
    }
    ctx->s1_store_processed = 1;

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
            HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num);
            tcg_gen_qemu_st8(hex_store_val32[slot_num],
                             hex_store_addr[slot_num],
                             ctx->mem_idx);
            break;
        case 2:
            HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num);
            tcg_gen_qemu_st16(hex_store_val32[slot_num],
                              hex_store_addr[slot_num],
                              ctx->mem_idx);
            break;
        case 4:
            HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num);
            tcg_gen_qemu_st32(hex_store_val32[slot_num],
                              hex_store_addr[slot_num],
                              ctx->mem_idx);
            break;
        case 8:
            HEX_DEBUG_GEN_CHECK_STORE_WIDTH(ctx, slot_num);
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
                TCGv slot = tcg_const_tl(slot_num);
                gen_helper_commit_store(cpu_env, slot);
                tcg_temp_free(slot);
            }
        }
        tcg_temp_free(address);
    }
    if (is_predicated) {
        gen_set_label(label_end);
    }
}

static void process_store_log(DisasContext *ctx, Packet *pkt)
{
    /*
     *  When a packet has two stores, the hardware processes
     *  slot 1 and then slot 2.  This will be important when
     *  the memory accesses overlap.
     */
    if (pkt->pkt_has_store_s1 && !pkt->pkt_has_dczeroa) {
        process_store(ctx, pkt, 1);
    }
    if (pkt->pkt_has_store_s0 && !pkt->pkt_has_dczeroa) {
        process_store(ctx, pkt, 0);
    }
}

/* Zero out a 32-bit cache line */
static void process_dczeroa(DisasContext *ctx, Packet *pkt)
{
    if (pkt->pkt_has_dczeroa) {
        /* Store 32 bytes of zero starting at (addr & ~0x1f) */
        TCGv addr = tcg_temp_new();
        TCGv_i64 zero = tcg_const_i64(0);

        tcg_gen_andi_tl(addr, hex_dczero_addr, ~0x1f);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st64(zero, addr, ctx->mem_idx);

        tcg_temp_free(addr);
        tcg_temp_free_i64(zero);
    }
}

static void update_exec_counters(DisasContext *ctx, Packet *pkt)
{
    int num_insns = pkt->num_insns;
    int num_real_insns = 0;

    for (int i = 0; i < num_insns; i++) {
        if (!pkt->insn[i].is_endloop &&
            !pkt->insn[i].part1 &&
            !GET_ATTRIB(pkt->insn[i].opcode, A_IT_NOP)) {
            num_real_insns++;
        }
    }

    ctx->num_packets++;
    ctx->num_insns += num_real_insns;
}

static void gen_exec_counters(DisasContext *ctx)
{
    tcg_gen_addi_tl(hex_gpr[HEX_REG_QEMU_PKT_CNT],
                    hex_gpr[HEX_REG_QEMU_PKT_CNT], ctx->num_packets);
    tcg_gen_addi_tl(hex_gpr[HEX_REG_QEMU_INSN_CNT],
                    hex_gpr[HEX_REG_QEMU_INSN_CNT], ctx->num_insns);
}

static void gen_commit_packet(DisasContext *ctx, Packet *pkt)
{
    gen_reg_writes(ctx);
    gen_pred_writes(ctx, pkt);
    process_store_log(ctx, pkt);
    process_dczeroa(ctx, pkt);
    update_exec_counters(ctx, pkt);
#if HEX_DEBUG
    {
        TCGv has_st0 =
            tcg_const_tl(pkt->pkt_has_store_s0 && !pkt->pkt_has_dczeroa);
        TCGv has_st1 =
            tcg_const_tl(pkt->pkt_has_store_s1 && !pkt->pkt_has_dczeroa);

        /* Handy place to set a breakpoint at the end of execution */
        gen_helper_debug_commit_end(cpu_env, has_st0, has_st1);

        tcg_temp_free(has_st0);
        tcg_temp_free(has_st1);
    }
#endif

    if (pkt->pkt_has_cof) {
        ctx->base.is_jmp = DISAS_NORETURN;
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
        gen_exception(HEX_EXCP_INVALID_PACKET);
        ctx->base.is_jmp = DISAS_NORETURN;
        return;
    }

    if (decode_packet(nwords, words, &pkt, false) > 0) {
        HEX_DEBUG_PRINT_PKT(&pkt);
        gen_start_packet(ctx, &pkt);
        for (i = 0; i < pkt.num_insns; i++) {
            gen_insn(env, ctx, &pkt.insn[i], &pkt);
        }
        gen_commit_packet(ctx, &pkt);
        ctx->base.pc_next += pkt.encod_pkt_size_in_bytes;
    } else {
        gen_exception(HEX_EXCP_INVALID_PACKET);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static void hexagon_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->mem_idx = MMU_USER_IDX;
    ctx->num_packets = 0;
    ctx->num_insns = 0;
}

static void hexagon_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void hexagon_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static bool hexagon_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                        const CPUBreakpoint *bp)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
    ctx->base.is_jmp = DISAS_NORETURN;
    gen_exception_debug();
    /*
     * The address covered by the breakpoint must be included in
     * [tb->pc, tb->pc + tb->size) in order to for it to be
     * properly cleared -- thus we increment the PC here so that
     * the logic setting tb->size below does the right thing.
     */
    ctx->base.pc_next += 4;
    return true;
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
        HexagonCPU *hex_cpu = container_of(env, HexagonCPU, env);
        if (hex_cpu->lldb_compat && qemu_loglevel_mask(CPU_LOG_TB_CPU)) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
#if HEX_DEBUG
        /* When debugging, only put one packet per TB */
        ctx->base.is_jmp = DISAS_TOO_MANY;
#endif
    }
}

static void hexagon_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_exec_counters(ctx);
        tcg_gen_movi_tl(hex_gpr[HEX_REG_PC], ctx->base.pc_next);
        if (ctx->base.singlestep_enabled) {
            gen_exception_debug();
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    case DISAS_NORETURN:
        gen_exec_counters(ctx);
        tcg_gen_mov_tl(hex_gpr[HEX_REG_PC], hex_next_PC);
        if (ctx->base.singlestep_enabled) {
            gen_exception_debug();
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void hexagon_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}


static const TranslatorOps hexagon_tr_ops = {
    .init_disas_context = hexagon_tr_init_disas_context,
    .tb_start           = hexagon_tr_tb_start,
    .insn_start         = hexagon_tr_insn_start,
    .breakpoint_check   = hexagon_tr_breakpoint_check,
    .translate_insn     = hexagon_tr_translate_packet,
    .tb_stop            = hexagon_tr_tb_stop,
    .disas_log          = hexagon_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&hexagon_tr_ops, &ctx.base, cs, tb, max_insns);
}

#define NAME_LEN               64
static char new_value_names[TOTAL_PER_THREAD_REGS][NAME_LEN];
#if HEX_DEBUG
static char reg_written_names[TOTAL_PER_THREAD_REGS][NAME_LEN];
#endif
static char new_pred_value_names[NUM_PREGS][NAME_LEN];
static char store_addr_names[STORES_MAX][NAME_LEN];
static char store_width_names[STORES_MAX][NAME_LEN];
static char store_val32_names[STORES_MAX][NAME_LEN];
static char store_val64_names[STORES_MAX][NAME_LEN];

void hexagon_translate_init(void)
{
    int i;

    opcode_init();

#if HEX_DEBUG
    if (!qemu_logfile) {
        qemu_set_log(qemu_loglevel);
    }
#endif

    for (i = 0; i < TOTAL_PER_THREAD_REGS; i++) {
        hex_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, gpr[i]),
            hexagon_regnames[i]);

        snprintf(new_value_names[i], NAME_LEN, "new_%s", hexagon_regnames[i]);
        hex_new_value[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, new_value[i]),
            new_value_names[i]);

#if HEX_DEBUG
        snprintf(reg_written_names[i], NAME_LEN, "reg_written_%s",
                 hexagon_regnames[i]);
        hex_reg_written[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPUHexagonState, reg_written[i]),
            reg_written_names[i]);
#endif
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
}
