/*
 *  LatticeMico32 main translation routines.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
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
#include "exec/helper-proto.h"
#include "tcg-op.h"

#include "exec/cpu_ldst.h"
#include "hw/lm32/lm32_pic.h"

#include "exec/helper-gen.h"

#define DISAS_LM32 1
#if DISAS_LM32
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
#endif

#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

#define MEM_INDEX 0

static TCGv_ptr cpu_env;
static TCGv cpu_R[32];
static TCGv cpu_pc;
static TCGv cpu_ie;
static TCGv cpu_icc;
static TCGv cpu_dcc;
static TCGv cpu_cc;
static TCGv cpu_cfg;
static TCGv cpu_eba;
static TCGv cpu_dc;
static TCGv cpu_deba;
static TCGv cpu_bp[4];
static TCGv cpu_wp[4];

#include "exec/gen-icount.h"

enum {
    OP_FMT_RI,
    OP_FMT_RR,
    OP_FMT_CR,
    OP_FMT_I
};

/* This is the state at translation time.  */
typedef struct DisasContext {
    target_ulong pc;

    /* Decoder.  */
    int format;
    uint32_t ir;
    uint8_t opcode;
    uint8_t r0, r1, r2, csr;
    uint16_t imm5;
    uint16_t imm16;
    uint32_t imm26;

    unsigned int delayed_branch;
    unsigned int tb_flags, synced_flags; /* tb dependent flags.  */
    int is_jmp;

    struct TranslationBlock *tb;
    int singlestep_enabled;

    uint32_t features;
    uint8_t num_breakpoints;
    uint8_t num_watchpoints;
} DisasContext;

static const char *regnames[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26/gp", "r27/fp", "r28/sp", "r29/ra",
    "r30/ea", "r31/ba", "bp0", "bp1", "bp2", "bp3", "wp0",
    "wp1", "wp2", "wp3"
};

static inline int zero_extend(unsigned int val, int width)
{
    return val & ((1 << width) - 1);
}

static inline int sign_extend(unsigned int val, int width)
{
    int sval;

    /* LSL.  */
    val <<= 32 - width;
    sval = val;
    /* ASR.  */
    sval >>= 32 - width;

    return sval;
}

static inline void t_gen_raise_exception(DisasContext *dc, uint32_t index)
{
    TCGv_i32 tmp = tcg_const_i32(index);

    gen_helper_raise_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static inline void t_gen_illegal_insn(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    gen_helper_ill(cpu_env);
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    TranslationBlock *tb;

    tb = dc->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) &&
            likely(!dc->singlestep_enabled)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (dc->singlestep_enabled) {
            t_gen_raise_exception(dc, EXCP_DEBUG);
        }
        tcg_gen_exit_tb(0);
    }
}

static void dec_add(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        if (dc->r0 == R_R0) {
            if (dc->r1 == R_R0 && dc->imm16 == 0) {
                LOG_DIS("nop\n");
            } else {
                LOG_DIS("mvi r%d, %d\n", dc->r1, sign_extend(dc->imm16, 16));
            }
        } else {
            LOG_DIS("addi r%d, r%d, %d\n", dc->r1, dc->r0,
                    sign_extend(dc->imm16, 16));
        }
    } else {
        LOG_DIS("add r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_addi_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                sign_extend(dc->imm16, 16));
    } else {
        tcg_gen_add_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_and(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("andi r%d, r%d, %d\n", dc->r1, dc->r0,
                zero_extend(dc->imm16, 16));
    } else {
        LOG_DIS("and r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_andi_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                zero_extend(dc->imm16, 16));
    } else  {
        if (dc->r0 == 0 && dc->r1 == 0 && dc->r2 == 0) {
            tcg_gen_movi_tl(cpu_pc, dc->pc + 4);
            gen_helper_hlt(cpu_env);
        } else {
            tcg_gen_and_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
        }
    }
}

static void dec_andhi(DisasContext *dc)
{
    LOG_DIS("andhi r%d, r%d, %d\n", dc->r2, dc->r0, dc->imm16);

    tcg_gen_andi_tl(cpu_R[dc->r1], cpu_R[dc->r0], (dc->imm16 << 16));
}

static void dec_b(DisasContext *dc)
{
    if (dc->r0 == R_RA) {
        LOG_DIS("ret\n");
    } else if (dc->r0 == R_EA) {
        LOG_DIS("eret\n");
    } else if (dc->r0 == R_BA) {
        LOG_DIS("bret\n");
    } else {
        LOG_DIS("b r%d\n", dc->r0);
    }

    /* restore IE.IE in case of an eret */
    if (dc->r0 == R_EA) {
        TCGv t0 = tcg_temp_new();
        int l1 = gen_new_label();
        tcg_gen_andi_tl(t0, cpu_ie, IE_EIE);
        tcg_gen_ori_tl(cpu_ie, cpu_ie, IE_IE);
        tcg_gen_brcondi_tl(TCG_COND_EQ, t0, IE_EIE, l1);
        tcg_gen_andi_tl(cpu_ie, cpu_ie, ~IE_IE);
        gen_set_label(l1);
        tcg_temp_free(t0);
    } else if (dc->r0 == R_BA) {
        TCGv t0 = tcg_temp_new();
        int l1 = gen_new_label();
        tcg_gen_andi_tl(t0, cpu_ie, IE_BIE);
        tcg_gen_ori_tl(cpu_ie, cpu_ie, IE_IE);
        tcg_gen_brcondi_tl(TCG_COND_EQ, t0, IE_BIE, l1);
        tcg_gen_andi_tl(cpu_ie, cpu_ie, ~IE_IE);
        gen_set_label(l1);
        tcg_temp_free(t0);
    }
    tcg_gen_mov_tl(cpu_pc, cpu_R[dc->r0]);

    dc->is_jmp = DISAS_JUMP;
}

static void dec_bi(DisasContext *dc)
{
    LOG_DIS("bi %d\n", sign_extend(dc->imm26 << 2, 26));

    gen_goto_tb(dc, 0, dc->pc + (sign_extend(dc->imm26 << 2, 26)));

    dc->is_jmp = DISAS_TB_JUMP;
}

static inline void gen_cond_branch(DisasContext *dc, int cond)
{
    int l1;

    l1 = gen_new_label();
    tcg_gen_brcond_tl(cond, cpu_R[dc->r0], cpu_R[dc->r1], l1);
    gen_goto_tb(dc, 0, dc->pc + 4);
    gen_set_label(l1);
    gen_goto_tb(dc, 1, dc->pc + (sign_extend(dc->imm16 << 2, 16)));
    dc->is_jmp = DISAS_TB_JUMP;
}

static void dec_be(DisasContext *dc)
{
    LOG_DIS("be r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16) * 4);

    gen_cond_branch(dc, TCG_COND_EQ);
}

static void dec_bg(DisasContext *dc)
{
    LOG_DIS("bg r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16 * 4));

    gen_cond_branch(dc, TCG_COND_GT);
}

static void dec_bge(DisasContext *dc)
{
    LOG_DIS("bge r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16) * 4);

    gen_cond_branch(dc, TCG_COND_GE);
}

static void dec_bgeu(DisasContext *dc)
{
    LOG_DIS("bgeu r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16) * 4);

    gen_cond_branch(dc, TCG_COND_GEU);
}

static void dec_bgu(DisasContext *dc)
{
    LOG_DIS("bgu r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16) * 4);

    gen_cond_branch(dc, TCG_COND_GTU);
}

static void dec_bne(DisasContext *dc)
{
    LOG_DIS("bne r%d, r%d, %d\n", dc->r0, dc->r1,
            sign_extend(dc->imm16, 16) * 4);

    gen_cond_branch(dc, TCG_COND_NE);
}

static void dec_call(DisasContext *dc)
{
    LOG_DIS("call r%d\n", dc->r0);

    tcg_gen_movi_tl(cpu_R[R_RA], dc->pc + 4);
    tcg_gen_mov_tl(cpu_pc, cpu_R[dc->r0]);

    dc->is_jmp = DISAS_JUMP;
}

static void dec_calli(DisasContext *dc)
{
    LOG_DIS("calli %d\n", sign_extend(dc->imm26, 26) * 4);

    tcg_gen_movi_tl(cpu_R[R_RA], dc->pc + 4);
    gen_goto_tb(dc, 0, dc->pc + (sign_extend(dc->imm26 << 2, 26)));

    dc->is_jmp = DISAS_TB_JUMP;
}

static inline void gen_compare(DisasContext *dc, int cond)
{
    int rX = (dc->format == OP_FMT_RR) ? dc->r2 : dc->r1;
    int rY = (dc->format == OP_FMT_RR) ? dc->r0 : dc->r0;
    int rZ = (dc->format == OP_FMT_RR) ? dc->r1 : -1;
    int i;

    if (dc->format == OP_FMT_RI) {
        switch (cond) {
        case TCG_COND_GEU:
        case TCG_COND_GTU:
            i = zero_extend(dc->imm16, 16);
            break;
        default:
            i = sign_extend(dc->imm16, 16);
            break;
        }

        tcg_gen_setcondi_tl(cond, cpu_R[rX], cpu_R[rY], i);
    } else {
        tcg_gen_setcond_tl(cond, cpu_R[rX], cpu_R[rY], cpu_R[rZ]);
    }
}

static void dec_cmpe(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpei r%d, r%d, %d\n", dc->r0, dc->r1,
                sign_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpe r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_EQ);
}

static void dec_cmpg(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpgi r%d, r%d, %d\n", dc->r0, dc->r1,
                sign_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpg r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_GT);
}

static void dec_cmpge(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpgei r%d, r%d, %d\n", dc->r0, dc->r1,
                sign_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpge r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_GE);
}

static void dec_cmpgeu(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpgeui r%d, r%d, %d\n", dc->r0, dc->r1,
                zero_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpgeu r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_GEU);
}

static void dec_cmpgu(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpgui r%d, r%d, %d\n", dc->r0, dc->r1,
                zero_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpgu r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_GTU);
}

static void dec_cmpne(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("cmpnei r%d, r%d, %d\n", dc->r0, dc->r1,
                sign_extend(dc->imm16, 16));
    } else {
        LOG_DIS("cmpne r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    gen_compare(dc, TCG_COND_NE);
}

static void dec_divu(DisasContext *dc)
{
    int l1;

    LOG_DIS("divu r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);

    if (!(dc->features & LM32_FEATURE_DIVIDE)) {
        qemu_log_mask(LOG_GUEST_ERROR, "hardware divider is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    l1 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[dc->r1], 0, l1);
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    t_gen_raise_exception(dc, EXCP_DIVIDE_BY_ZERO);
    gen_set_label(l1);
    tcg_gen_divu_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
}

static void dec_lb(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("lb r%d, (r%d+%d)\n", dc->r1, dc->r0, dc->imm16);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_ld8s(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_lbu(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("lbu r%d, (r%d+%d)\n", dc->r1, dc->r0, dc->imm16);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_ld8u(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_lh(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("lh r%d, (r%d+%d)\n", dc->r1, dc->r0, dc->imm16);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_ld16s(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_lhu(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("lhu r%d, (r%d+%d)\n", dc->r1, dc->r0, dc->imm16);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_ld16u(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_lw(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("lw r%d, (r%d+%d)\n", dc->r1, dc->r0, sign_extend(dc->imm16, 16));

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_ld32s(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_modu(DisasContext *dc)
{
    int l1;

    LOG_DIS("modu r%d, r%d, %d\n", dc->r2, dc->r0, dc->r1);

    if (!(dc->features & LM32_FEATURE_DIVIDE)) {
        qemu_log_mask(LOG_GUEST_ERROR, "hardware divider is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    l1 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[dc->r1], 0, l1);
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    t_gen_raise_exception(dc, EXCP_DIVIDE_BY_ZERO);
    gen_set_label(l1);
    tcg_gen_remu_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
}

static void dec_mul(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("muli r%d, r%d, %d\n", dc->r0, dc->r1,
                sign_extend(dc->imm16, 16));
    } else {
        LOG_DIS("mul r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (!(dc->features & LM32_FEATURE_MULTIPLY)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "hardware multiplier is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_muli_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                sign_extend(dc->imm16, 16));
    } else {
        tcg_gen_mul_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_nor(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("nori r%d, r%d, %d\n", dc->r0, dc->r1,
                zero_extend(dc->imm16, 16));
    } else {
        LOG_DIS("nor r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (dc->format == OP_FMT_RI) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_movi_tl(t0, zero_extend(dc->imm16, 16));
        tcg_gen_nor_tl(cpu_R[dc->r1], cpu_R[dc->r0], t0);
        tcg_temp_free(t0);
    } else {
        tcg_gen_nor_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_or(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("ori r%d, r%d, %d\n", dc->r1, dc->r0,
                zero_extend(dc->imm16, 16));
    } else {
        if (dc->r1 == R_R0) {
            LOG_DIS("mv r%d, r%d\n", dc->r2, dc->r0);
        } else {
            LOG_DIS("or r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
        }
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_ori_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                zero_extend(dc->imm16, 16));
    } else {
        tcg_gen_or_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_orhi(DisasContext *dc)
{
    if (dc->r0 == R_R0) {
        LOG_DIS("mvhi r%d, %d\n", dc->r1, dc->imm16);
    } else {
        LOG_DIS("orhi r%d, r%d, %d\n", dc->r1, dc->r0, dc->imm16);
    }

    tcg_gen_ori_tl(cpu_R[dc->r1], cpu_R[dc->r0], (dc->imm16 << 16));
}

static void dec_scall(DisasContext *dc)
{
    switch (dc->imm5) {
    case 2:
        LOG_DIS("break\n");
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        t_gen_raise_exception(dc, EXCP_BREAKPOINT);
        break;
    case 7:
        LOG_DIS("scall\n");
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        t_gen_raise_exception(dc, EXCP_SYSTEMCALL);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid opcode @0x%x", dc->pc);
        t_gen_illegal_insn(dc);
        break;
    }
}

static void dec_rcsr(DisasContext *dc)
{
    LOG_DIS("rcsr r%d, %d\n", dc->r2, dc->csr);

    switch (dc->csr) {
    case CSR_IE:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_ie);
        break;
    case CSR_IM:
        gen_helper_rcsr_im(cpu_R[dc->r2], cpu_env);
        break;
    case CSR_IP:
        gen_helper_rcsr_ip(cpu_R[dc->r2], cpu_env);
        break;
    case CSR_CC:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_cc);
        break;
    case CSR_CFG:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_cfg);
        break;
    case CSR_EBA:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_eba);
        break;
    case CSR_DC:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_dc);
        break;
    case CSR_DEBA:
        tcg_gen_mov_tl(cpu_R[dc->r2], cpu_deba);
        break;
    case CSR_JTX:
        gen_helper_rcsr_jtx(cpu_R[dc->r2], cpu_env);
        break;
    case CSR_JRX:
        gen_helper_rcsr_jrx(cpu_R[dc->r2], cpu_env);
        break;
    case CSR_ICC:
    case CSR_DCC:
    case CSR_BP0:
    case CSR_BP1:
    case CSR_BP2:
    case CSR_BP3:
    case CSR_WP0:
    case CSR_WP1:
    case CSR_WP2:
    case CSR_WP3:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid read access csr=%x\n", dc->csr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "read_csr: unknown csr=%x\n", dc->csr);
        break;
    }
}

static void dec_sb(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("sb (r%d+%d), r%d\n", dc->r0, dc->imm16, dc->r1);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_st8(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_sextb(DisasContext *dc)
{
    LOG_DIS("sextb r%d, r%d\n", dc->r2, dc->r0);

    if (!(dc->features & LM32_FEATURE_SIGN_EXTEND)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "hardware sign extender is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    tcg_gen_ext8s_tl(cpu_R[dc->r2], cpu_R[dc->r0]);
}

static void dec_sexth(DisasContext *dc)
{
    LOG_DIS("sexth r%d, r%d\n", dc->r2, dc->r0);

    if (!(dc->features & LM32_FEATURE_SIGN_EXTEND)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "hardware sign extender is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    tcg_gen_ext16s_tl(cpu_R[dc->r2], cpu_R[dc->r0]);
}

static void dec_sh(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("sh (r%d+%d), r%d\n", dc->r0, dc->imm16, dc->r1);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_st16(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_sl(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("sli r%d, r%d, %d\n", dc->r1, dc->r0, dc->imm5);
    } else {
        LOG_DIS("sl r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (!(dc->features & LM32_FEATURE_SHIFT)) {
        qemu_log_mask(LOG_GUEST_ERROR, "hardware shifter is not available\n");
        t_gen_illegal_insn(dc);
        return;
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_shli_tl(cpu_R[dc->r1], cpu_R[dc->r0], dc->imm5);
    } else {
        TCGv t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_R[dc->r1], 0x1f);
        tcg_gen_shl_tl(cpu_R[dc->r2], cpu_R[dc->r0], t0);
        tcg_temp_free(t0);
    }
}

static void dec_sr(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("sri r%d, r%d, %d\n", dc->r1, dc->r0, dc->imm5);
    } else {
        LOG_DIS("sr r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    /* The real CPU (w/o hardware shifter) only supports right shift by exactly
     * one bit */
    if (dc->format == OP_FMT_RI) {
        if (!(dc->features & LM32_FEATURE_SHIFT) && (dc->imm5 != 1)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "hardware shifter is not available\n");
            t_gen_illegal_insn(dc);
            return;
        }
        tcg_gen_sari_tl(cpu_R[dc->r1], cpu_R[dc->r0], dc->imm5);
    } else {
        int l1 = gen_new_label();
        int l2 = gen_new_label();
        TCGv t0 = tcg_temp_local_new();
        tcg_gen_andi_tl(t0, cpu_R[dc->r1], 0x1f);

        if (!(dc->features & LM32_FEATURE_SHIFT)) {
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 1, l1);
            t_gen_illegal_insn(dc);
            tcg_gen_br(l2);
        }

        gen_set_label(l1);
        tcg_gen_sar_tl(cpu_R[dc->r2], cpu_R[dc->r0], t0);
        gen_set_label(l2);

        tcg_temp_free(t0);
    }
}

static void dec_sru(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("srui r%d, r%d, %d\n", dc->r1, dc->r0, dc->imm5);
    } else {
        LOG_DIS("sru r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (dc->format == OP_FMT_RI) {
        if (!(dc->features & LM32_FEATURE_SHIFT) && (dc->imm5 != 1)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                    "hardware shifter is not available\n");
            t_gen_illegal_insn(dc);
            return;
        }
        tcg_gen_shri_tl(cpu_R[dc->r1], cpu_R[dc->r0], dc->imm5);
    } else {
        int l1 = gen_new_label();
        int l2 = gen_new_label();
        TCGv t0 = tcg_temp_local_new();
        tcg_gen_andi_tl(t0, cpu_R[dc->r1], 0x1f);

        if (!(dc->features & LM32_FEATURE_SHIFT)) {
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 1, l1);
            t_gen_illegal_insn(dc);
            tcg_gen_br(l2);
        }

        gen_set_label(l1);
        tcg_gen_shr_tl(cpu_R[dc->r2], cpu_R[dc->r0], t0);
        gen_set_label(l2);

        tcg_temp_free(t0);
    }
}

static void dec_sub(DisasContext *dc)
{
    LOG_DIS("sub r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);

    tcg_gen_sub_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
}

static void dec_sw(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("sw (r%d+%d), r%d\n", dc->r0, sign_extend(dc->imm16, 16), dc->r1);

    t0 = tcg_temp_new();
    tcg_gen_addi_tl(t0, cpu_R[dc->r0], sign_extend(dc->imm16, 16));
    tcg_gen_qemu_st32(cpu_R[dc->r1], t0, MEM_INDEX);
    tcg_temp_free(t0);
}

static void dec_user(DisasContext *dc)
{
    LOG_DIS("user");

    qemu_log_mask(LOG_GUEST_ERROR, "user instruction undefined\n");
    t_gen_illegal_insn(dc);
}

static void dec_wcsr(DisasContext *dc)
{
    int no;

    LOG_DIS("wcsr r%d, %d\n", dc->r1, dc->csr);

    switch (dc->csr) {
    case CSR_IE:
        tcg_gen_mov_tl(cpu_ie, cpu_R[dc->r1]);
        tcg_gen_movi_tl(cpu_pc, dc->pc + 4);
        dc->is_jmp = DISAS_UPDATE;
        break;
    case CSR_IM:
        /* mark as an io operation because it could cause an interrupt */
        if (use_icount) {
            gen_io_start();
        }
        gen_helper_wcsr_im(cpu_env, cpu_R[dc->r1]);
        tcg_gen_movi_tl(cpu_pc, dc->pc + 4);
        if (use_icount) {
            gen_io_end();
        }
        dc->is_jmp = DISAS_UPDATE;
        break;
    case CSR_IP:
        /* mark as an io operation because it could cause an interrupt */
        if (use_icount) {
            gen_io_start();
        }
        gen_helper_wcsr_ip(cpu_env, cpu_R[dc->r1]);
        tcg_gen_movi_tl(cpu_pc, dc->pc + 4);
        if (use_icount) {
            gen_io_end();
        }
        dc->is_jmp = DISAS_UPDATE;
        break;
    case CSR_ICC:
        /* TODO */
        break;
    case CSR_DCC:
        /* TODO */
        break;
    case CSR_EBA:
        tcg_gen_mov_tl(cpu_eba, cpu_R[dc->r1]);
        break;
    case CSR_DEBA:
        tcg_gen_mov_tl(cpu_deba, cpu_R[dc->r1]);
        break;
    case CSR_JTX:
        gen_helper_wcsr_jtx(cpu_env, cpu_R[dc->r1]);
        break;
    case CSR_JRX:
        gen_helper_wcsr_jrx(cpu_env, cpu_R[dc->r1]);
        break;
    case CSR_DC:
        gen_helper_wcsr_dc(cpu_env, cpu_R[dc->r1]);
        break;
    case CSR_BP0:
    case CSR_BP1:
    case CSR_BP2:
    case CSR_BP3:
        no = dc->csr - CSR_BP0;
        if (dc->num_breakpoints <= no) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "breakpoint #%i is not available\n", no);
            t_gen_illegal_insn(dc);
            break;
        }
        gen_helper_wcsr_bp(cpu_env, cpu_R[dc->r1], tcg_const_i32(no));
        break;
    case CSR_WP0:
    case CSR_WP1:
    case CSR_WP2:
    case CSR_WP3:
        no = dc->csr - CSR_WP0;
        if (dc->num_watchpoints <= no) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "watchpoint #%i is not available\n", no);
            t_gen_illegal_insn(dc);
            break;
        }
        gen_helper_wcsr_wp(cpu_env, cpu_R[dc->r1], tcg_const_i32(no));
        break;
    case CSR_CC:
    case CSR_CFG:
        qemu_log_mask(LOG_GUEST_ERROR, "invalid write access csr=%x\n",
                      dc->csr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "write_csr: unknown csr=%x\n",
                      dc->csr);
        break;
    }
}

static void dec_xnor(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("xnori r%d, r%d, %d\n", dc->r0, dc->r1,
                zero_extend(dc->imm16, 16));
    } else {
        if (dc->r1 == R_R0) {
            LOG_DIS("not r%d, r%d\n", dc->r2, dc->r0);
        } else {
            LOG_DIS("xnor r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
        }
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_xori_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                zero_extend(dc->imm16, 16));
        tcg_gen_not_tl(cpu_R[dc->r1], cpu_R[dc->r1]);
    } else {
        tcg_gen_eqv_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_xor(DisasContext *dc)
{
    if (dc->format == OP_FMT_RI) {
        LOG_DIS("xori r%d, r%d, %d\n", dc->r0, dc->r1,
                zero_extend(dc->imm16, 16));
    } else {
        LOG_DIS("xor r%d, r%d, r%d\n", dc->r2, dc->r0, dc->r1);
    }

    if (dc->format == OP_FMT_RI) {
        tcg_gen_xori_tl(cpu_R[dc->r1], cpu_R[dc->r0],
                zero_extend(dc->imm16, 16));
    } else {
        tcg_gen_xor_tl(cpu_R[dc->r2], cpu_R[dc->r0], cpu_R[dc->r1]);
    }
}

static void dec_ill(DisasContext *dc)
{
    qemu_log_mask(LOG_GUEST_ERROR, "invalid opcode 0x%02x\n", dc->opcode);
    t_gen_illegal_insn(dc);
}

typedef void (*DecoderInfo)(DisasContext *dc);
static const DecoderInfo decinfo[] = {
    dec_sru, dec_nor, dec_mul, dec_sh, dec_lb, dec_sr, dec_xor, dec_lh,
    dec_and, dec_xnor, dec_lw, dec_lhu, dec_sb, dec_add, dec_or, dec_sl,
    dec_lbu, dec_be, dec_bg, dec_bge, dec_bgeu, dec_bgu, dec_sw, dec_bne,
    dec_andhi, dec_cmpe, dec_cmpg, dec_cmpge, dec_cmpgeu, dec_cmpgu, dec_orhi,
    dec_cmpne,
    dec_sru, dec_nor, dec_mul, dec_divu, dec_rcsr, dec_sr, dec_xor, dec_ill,
    dec_and, dec_xnor, dec_ill, dec_scall, dec_sextb, dec_add, dec_or, dec_sl,
    dec_b, dec_modu, dec_sub, dec_user, dec_wcsr, dec_ill, dec_call, dec_sexth,
    dec_bi, dec_cmpe, dec_cmpg, dec_cmpge, dec_cmpgeu, dec_cmpgu, dec_calli,
    dec_cmpne
};

static inline void decode(DisasContext *dc, uint32_t ir)
{
    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
        tcg_gen_debug_insn_start(dc->pc);
    }

    dc->ir = ir;
    LOG_DIS("%8.8x\t", dc->ir);

    dc->opcode = EXTRACT_FIELD(ir, 26, 31);

    dc->imm5 = EXTRACT_FIELD(ir, 0, 4);
    dc->imm16 = EXTRACT_FIELD(ir, 0, 15);
    dc->imm26 = EXTRACT_FIELD(ir, 0, 25);

    dc->csr = EXTRACT_FIELD(ir, 21, 25);
    dc->r0 = EXTRACT_FIELD(ir, 21, 25);
    dc->r1 = EXTRACT_FIELD(ir, 16, 20);
    dc->r2 = EXTRACT_FIELD(ir, 11, 15);

    /* bit 31 seems to indicate insn type.  */
    if (ir & (1 << 31)) {
        dc->format = OP_FMT_RR;
    } else {
        dc->format = OP_FMT_RI;
    }

    assert(ARRAY_SIZE(decinfo) == 64);
    assert(dc->opcode < 64);

    decinfo[dc->opcode](dc);
}

static void check_breakpoint(CPULM32State *env, DisasContext *dc)
{
    CPUState *cs = CPU(lm32_env_get_cpu(env));
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&cs->breakpoints))) {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                tcg_gen_movi_tl(cpu_pc, dc->pc);
                t_gen_raise_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
             }
        }
    }
}

/* generate intermediate code for basic block 'tb'.  */
static inline
void gen_intermediate_code_internal(LM32CPU *cpu,
                                    TranslationBlock *tb, bool search_pc)
{
    CPUState *cs = CPU(cpu);
    CPULM32State *env = &cpu->env;
    struct DisasContext ctx, *dc = &ctx;
    uint16_t *gen_opc_end;
    uint32_t pc_start;
    int j, lj;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    dc->features = cpu->features;
    dc->num_breakpoints = cpu->num_breakpoints;
    dc->num_watchpoints = cpu->num_watchpoints;
    dc->tb = tb;

    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = cs->singlestep_enabled;

    if (pc_start & 3) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unaligned PC=%x. Ignoring lowest bits.\n", pc_start);
        pc_start &= ~3;
    }

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_tb_start();
    do {
        check_breakpoint(env, dc);

        if (search_pc) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    tcg_ctx.gen_opc_instr_start[lj++] = 0;
                }
            }
            tcg_ctx.gen_opc_pc[lj] = dc->pc;
            tcg_ctx.gen_opc_instr_start[lj] = 1;
            tcg_ctx.gen_opc_icount[lj] = num_insns;
        }

        /* Pretty disas.  */
        LOG_DIS("%8.8x:\t", dc->pc);

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        decode(dc, cpu_ldl_code(env, dc->pc));
        dc->pc += 4;
        num_insns++;

    } while (!dc->is_jmp
         && tcg_ctx.gen_opc_ptr < gen_opc_end
         && !cs->singlestep_enabled
         && !singlestep
         && (dc->pc < next_page_start)
         && num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (unlikely(cs->singlestep_enabled)) {
        if (dc->is_jmp == DISAS_NEXT) {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }
        t_gen_raise_exception(dc, EXCP_DEBUG);
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            /* indicate that the hash table must be used
               to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }

    gen_tb_end(tb, num_insns);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
        lj++;
        while (lj <= j) {
            tcg_ctx.gen_opc_instr_start[lj++] = 0;
        }
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("\n");
        log_target_disas(env, pc_start, dc->pc - pc_start, 0);
        qemu_log("\nisize=%d osize=%td\n",
            dc->pc - pc_start, tcg_ctx.gen_opc_ptr -
            tcg_ctx.gen_opc_buf);
    }
#endif
}

void gen_intermediate_code(CPULM32State *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(lm32_env_get_cpu(env), tb, false);
}

void gen_intermediate_code_pc(CPULM32State *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(lm32_env_get_cpu(env), tb, true);
}

void lm32_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    LM32CPU *cpu = LM32_CPU(cs);
    CPULM32State *env = &cpu->env;
    int i;

    if (!env || !f) {
        return;
    }

    cpu_fprintf(f, "IN: PC=%x %s\n",
                env->pc, lookup_symbol(env->pc));

    cpu_fprintf(f, "ie=%8.8x (IE=%x EIE=%x BIE=%x) im=%8.8x ip=%8.8x\n",
             env->ie,
             (env->ie & IE_IE) ? 1 : 0,
             (env->ie & IE_EIE) ? 1 : 0,
             (env->ie & IE_BIE) ? 1 : 0,
             lm32_pic_get_im(env->pic_state),
             lm32_pic_get_ip(env->pic_state));
    cpu_fprintf(f, "eba=%8.8x deba=%8.8x\n",
             env->eba,
             env->deba);

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "r%2.2d=%8.8x ", i, env->regs[i]);
        if ((i + 1) % 4 == 0) {
            cpu_fprintf(f, "\n");
        }
    }
    cpu_fprintf(f, "\n\n");
}

void restore_state_to_opc(CPULM32State *env, TranslationBlock *tb, int pc_pos)
{
    env->pc = tcg_ctx.gen_opc_pc[pc_pos];
}

void lm32_translate_init(void)
{
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    for (i = 0; i < ARRAY_SIZE(cpu_R); i++) {
        cpu_R[i] = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPULM32State, regs[i]),
                          regnames[i]);
    }

    for (i = 0; i < ARRAY_SIZE(cpu_bp); i++) {
        cpu_bp[i] = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPULM32State, bp[i]),
                          regnames[32+i]);
    }

    for (i = 0; i < ARRAY_SIZE(cpu_wp); i++) {
        cpu_wp[i] = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPULM32State, wp[i]),
                          regnames[36+i]);
    }

    cpu_pc = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, pc),
                    "pc");
    cpu_ie = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, ie),
                    "ie");
    cpu_icc = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, icc),
                    "icc");
    cpu_dcc = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, dcc),
                    "dcc");
    cpu_cc = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, cc),
                    "cc");
    cpu_cfg = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, cfg),
                    "cfg");
    cpu_eba = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, eba),
                    "eba");
    cpu_dc = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, dc),
                    "dc");
    cpu_deba = tcg_global_mem_new(TCG_AREG0,
                    offsetof(CPULM32State, deba),
                    "deba");
}

