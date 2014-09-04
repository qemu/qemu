/*
 * OpenRISC translation
 *
 * Copyright (c) 2011-2012 Jia Liu <proljc@gmail.com>
 *                         Feng Gao <gf91597@gmail.com>
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
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu-common.h"
#include "qemu/log.h"
#include "config.h"
#include "qemu/bitops.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#define OPENRISC_DISAS

#ifdef OPENRISC_DISAS
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
#endif

typedef struct DisasContext {
    TranslationBlock *tb;
    target_ulong pc, ppc, npc;
    uint32_t tb_flags, synced_flags, flags;
    uint32_t is_jmp;
    uint32_t mem_idx;
    int singlestep_enabled;
    uint32_t delayed_branch;
} DisasContext;

static TCGv_ptr cpu_env;
static TCGv cpu_sr;
static TCGv cpu_R[32];
static TCGv cpu_pc;
static TCGv jmp_pc;            /* l.jr/l.jalr temp pc */
static TCGv cpu_npc;
static TCGv cpu_ppc;
static TCGv_i32 env_btaken;    /* bf/bnf , F flag taken */
static TCGv_i32 fpcsr;
static TCGv machi, maclo;
static TCGv fpmaddhi, fpmaddlo;
static TCGv_i32 env_flags;
#include "exec/gen-icount.h"

void openrisc_translate_init(void)
{
    static const char * const regnames[] = {
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    };
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_sr = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOpenRISCState, sr), "sr");
    env_flags = tcg_global_mem_new_i32(TCG_AREG0,
                                       offsetof(CPUOpenRISCState, flags),
                                       "flags");
    cpu_pc = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOpenRISCState, pc), "pc");
    cpu_npc = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUOpenRISCState, npc), "npc");
    cpu_ppc = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUOpenRISCState, ppc), "ppc");
    jmp_pc = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUOpenRISCState, jmp_pc), "jmp_pc");
    env_btaken = tcg_global_mem_new_i32(TCG_AREG0,
                                        offsetof(CPUOpenRISCState, btaken),
                                        "btaken");
    fpcsr = tcg_global_mem_new_i32(TCG_AREG0,
                                   offsetof(CPUOpenRISCState, fpcsr),
                                   "fpcsr");
    machi = tcg_global_mem_new(TCG_AREG0,
                               offsetof(CPUOpenRISCState, machi),
                               "machi");
    maclo = tcg_global_mem_new(TCG_AREG0,
                               offsetof(CPUOpenRISCState, maclo),
                               "maclo");
    fpmaddhi = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUOpenRISCState, fpmaddhi),
                                  "fpmaddhi");
    fpmaddlo = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUOpenRISCState, fpmaddlo),
                                  "fpmaddlo");
    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new(TCG_AREG0,
                                      offsetof(CPUOpenRISCState, gpr[i]),
                                      regnames[i]);
    }
}

/* Writeback SR_F translation space to execution space.  */
static inline void wb_SR_F(void)
{
    int label;

    label = gen_new_label();
    tcg_gen_andi_tl(cpu_sr, cpu_sr, ~SR_F);
    tcg_gen_brcondi_tl(TCG_COND_EQ, env_btaken, 0, label);
    tcg_gen_ori_tl(cpu_sr, cpu_sr, SR_F);
    gen_set_label(label);
}

static inline int zero_extend(unsigned int val, int width)
{
    return val & ((1 << width) - 1);
}

static inline int sign_extend(unsigned int val, int width)
{
    int sval;

    /* LSL */
    val <<= TARGET_LONG_BITS - width;
    sval = val;
    /* ASR.  */
    sval >>= TARGET_LONG_BITS - width;
    return sval;
}

static inline void gen_sync_flags(DisasContext *dc)
{
    /* Sync the tb dependent flag between translate and runtime.  */
    if (dc->tb_flags != dc->synced_flags) {
        tcg_gen_movi_tl(env_flags, dc->tb_flags);
        dc->synced_flags = dc->tb_flags;
    }
}

static void gen_exception(DisasContext *dc, unsigned int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_illegal_exception(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    gen_exception(dc, EXCP_ILLEGAL);
    dc->is_jmp = DISAS_UPDATE;
}

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
static void check_ob64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OB64S)) {
        gen_illegal_exception(dc);
    }
}

static void check_of64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OF64S)) {
        gen_illegal_exception(dc);
    }
}

static void check_ov64s(DisasContext *dc)
{
    if (!(dc->flags & CPUCFGR_OV64S)) {
        gen_illegal_exception(dc);
    }
}
#endif*/

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = dc->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) &&
                                       likely(!dc->singlestep_enabled)) {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_goto_tb(n);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        if (dc->singlestep_enabled) {
            gen_exception(dc, EXCP_DEBUG);
        }
        tcg_gen_exit_tb(0);
    }
}

static void gen_jump(DisasContext *dc, uint32_t imm, uint32_t reg, uint32_t op0)
{
    target_ulong tmp_pc;
    /* N26, 26bits imm */
    tmp_pc = sign_extend((imm<<2), 26) + dc->pc;

    switch (op0) {
    case 0x00:     /* l.j */
        tcg_gen_movi_tl(jmp_pc, tmp_pc);
        break;
    case 0x01:     /* l.jal */
        tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
        tcg_gen_movi_tl(jmp_pc, tmp_pc);
        break;
    case 0x03:     /* l.bnf */
    case 0x04:     /* l.bf  */
        {
            int lab = gen_new_label();
            TCGv sr_f = tcg_temp_new();
            tcg_gen_movi_tl(jmp_pc, dc->pc+8);
            tcg_gen_andi_tl(sr_f, cpu_sr, SR_F);
            tcg_gen_brcondi_i32(op0 == 0x03 ? TCG_COND_EQ : TCG_COND_NE,
                                sr_f, SR_F, lab);
            tcg_gen_movi_tl(jmp_pc, tmp_pc);
            gen_set_label(lab);
            tcg_temp_free(sr_f);
        }
        break;
    case 0x11:     /* l.jr */
        tcg_gen_mov_tl(jmp_pc, cpu_R[reg]);
        break;
    case 0x12:     /* l.jalr */
        tcg_gen_movi_tl(cpu_R[9], (dc->pc + 8));
        tcg_gen_mov_tl(jmp_pc, cpu_R[reg]);
        break;
    default:
        gen_illegal_exception(dc);
        break;
    }

    dc->delayed_branch = 2;
    dc->tb_flags |= D_FLAG;
    gen_sync_flags(dc);
}


static void dec_calc(DisasContext *dc, uint32_t insn)
{
    uint32_t op0, op1, op2;
    uint32_t ra, rb, rd;
    op0 = extract32(insn, 0, 4);
    op1 = extract32(insn, 8, 2);
    op2 = extract32(insn, 6, 2);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);

    switch (op0) {
    case 0x0000:
        switch (op1) {
        case 0x00:    /* l.add */
            LOG_DIS("l.add r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab = gen_new_label();
                TCGv_i64 ta = tcg_temp_new_i64();
                TCGv_i64 tb = tcg_temp_new_i64();
                TCGv_i64 td = tcg_temp_local_new_i64();
                TCGv_i32 res = tcg_temp_local_new_i32();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                tcg_gen_extu_i32_i64(ta, cpu_R[ra]);
                tcg_gen_extu_i32_i64(tb, cpu_R[rb]);
                tcg_gen_add_i64(td, ta, tb);
                tcg_gen_trunc_i64_i32(res, td);
                tcg_gen_shri_i64(td, td, 31);
                tcg_gen_andi_i64(td, td, 0x3);
                /* Jump to lab when no overflow.  */
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x0, lab);
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x3, lab);
                tcg_gen_ori_i32(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                tcg_gen_andi_i32(sr_ove, cpu_sr, SR_OVE);
                tcg_gen_brcondi_i32(TCG_COND_NE, sr_ove, SR_OVE, lab);
                gen_exception(dc, EXCP_RANGE);
                gen_set_label(lab);
                tcg_gen_mov_i32(cpu_R[rd], res);
                tcg_temp_free_i64(ta);
                tcg_temp_free_i64(tb);
                tcg_temp_free_i64(td);
                tcg_temp_free_i32(res);
                tcg_temp_free_i32(sr_ove);
            }
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0001:    /* l.addc */
        switch (op1) {
        case 0x00:
            LOG_DIS("l.addc r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab = gen_new_label();
                TCGv_i64 ta = tcg_temp_new_i64();
                TCGv_i64 tb = tcg_temp_new_i64();
                TCGv_i64 tcy = tcg_temp_local_new_i64();
                TCGv_i64 td = tcg_temp_local_new_i64();
                TCGv_i32 res = tcg_temp_local_new_i32();
                TCGv_i32 sr_cy = tcg_temp_local_new_i32();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                tcg_gen_extu_i32_i64(ta, cpu_R[ra]);
                tcg_gen_extu_i32_i64(tb, cpu_R[rb]);
                tcg_gen_andi_i32(sr_cy, cpu_sr, SR_CY);
                tcg_gen_extu_i32_i64(tcy, sr_cy);
                tcg_gen_shri_i64(tcy, tcy, 10);
                tcg_gen_add_i64(td, ta, tb);
                tcg_gen_add_i64(td, td, tcy);
                tcg_gen_trunc_i64_i32(res, td);
                tcg_gen_shri_i64(td, td, 32);
                tcg_gen_andi_i64(td, td, 0x3);
                /* Jump to lab when no overflow.  */
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x0, lab);
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x3, lab);
                tcg_gen_ori_i32(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                tcg_gen_andi_i32(sr_ove, cpu_sr, SR_OVE);
                tcg_gen_brcondi_i32(TCG_COND_NE, sr_ove, SR_OVE, lab);
                gen_exception(dc, EXCP_RANGE);
                gen_set_label(lab);
                tcg_gen_mov_i32(cpu_R[rd], res);
                tcg_temp_free_i64(ta);
                tcg_temp_free_i64(tb);
                tcg_temp_free_i64(tcy);
                tcg_temp_free_i64(td);
                tcg_temp_free_i32(res);
                tcg_temp_free_i32(sr_cy);
                tcg_temp_free_i32(sr_ove);
            }
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0002:    /* l.sub */
        switch (op1) {
        case 0x00:
            LOG_DIS("l.sub r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab = gen_new_label();
                TCGv_i64 ta = tcg_temp_new_i64();
                TCGv_i64 tb = tcg_temp_new_i64();
                TCGv_i64 td = tcg_temp_local_new_i64();
                TCGv_i32 res = tcg_temp_local_new_i32();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();

                tcg_gen_extu_i32_i64(ta, cpu_R[ra]);
                tcg_gen_extu_i32_i64(tb, cpu_R[rb]);
                tcg_gen_sub_i64(td, ta, tb);
                tcg_gen_trunc_i64_i32(res, td);
                tcg_gen_shri_i64(td, td, 31);
                tcg_gen_andi_i64(td, td, 0x3);
                /* Jump to lab when no overflow.  */
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x0, lab);
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x3, lab);
                tcg_gen_ori_i32(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                tcg_gen_andi_i32(sr_ove, cpu_sr, SR_OVE);
                tcg_gen_brcondi_i32(TCG_COND_NE, sr_ove, SR_OVE, lab);
                gen_exception(dc, EXCP_RANGE);
                gen_set_label(lab);
                tcg_gen_mov_i32(cpu_R[rd], res);
                tcg_temp_free_i64(ta);
                tcg_temp_free_i64(tb);
                tcg_temp_free_i64(td);
                tcg_temp_free_i32(res);
                tcg_temp_free_i32(sr_ove);
            }
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0003:    /* l.and */
        switch (op1) {
        case 0x00:
            LOG_DIS("l.and r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_and_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0004:    /* l.or */
        switch (op1) {
        case 0x00:
            LOG_DIS("l.or r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_or_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0005:
        switch (op1) {
        case 0x00:    /* l.xor */
            LOG_DIS("l.xor r%d, r%d, r%d\n", rd, ra, rb);
            tcg_gen_xor_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0006:
        switch (op1) {
        case 0x03:    /* l.mul */
            LOG_DIS("l.mul r%d, r%d, r%d\n", rd, ra, rb);
            if (ra != 0 && rb != 0) {
                gen_helper_mul32(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
            } else {
                tcg_gen_movi_tl(cpu_R[rd], 0x0);
            }
            break;
        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0009:
        switch (op1) {
        case 0x03:    /* l.div */
            LOG_DIS("l.div r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab0 = gen_new_label();
                int lab1 = gen_new_label();
                int lab2 = gen_new_label();
                int lab3 = gen_new_label();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                if (rb == 0) {
                    tcg_gen_ori_tl(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                    tcg_gen_andi_tl(sr_ove, cpu_sr, SR_OVE);
                    tcg_gen_brcondi_tl(TCG_COND_NE, sr_ove, SR_OVE, lab0);
                    gen_exception(dc, EXCP_RANGE);
                    gen_set_label(lab0);
                } else {
                    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[rb],
                                       0x00000000, lab1);
                    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[ra],
                                       0x80000000, lab2);
                    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rb],
                                       0xffffffff, lab2);
                    gen_set_label(lab1);
                    tcg_gen_ori_tl(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                    tcg_gen_andi_tl(sr_ove, cpu_sr, SR_OVE);
                    tcg_gen_brcondi_tl(TCG_COND_NE, sr_ove, SR_OVE, lab3);
                    gen_exception(dc, EXCP_RANGE);
                    gen_set_label(lab2);
                    tcg_gen_div_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                    gen_set_label(lab3);
                }
                tcg_temp_free_i32(sr_ove);
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000a:
        switch (op1) {
        case 0x03:    /* l.divu */
            LOG_DIS("l.divu r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab0 = gen_new_label();
                int lab1 = gen_new_label();
                int lab2 = gen_new_label();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                if (rb == 0) {
                    tcg_gen_ori_tl(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                    tcg_gen_andi_tl(sr_ove, cpu_sr, SR_OVE);
                    tcg_gen_brcondi_tl(TCG_COND_NE, sr_ove, SR_OVE, lab0);
                    gen_exception(dc, EXCP_RANGE);
                    gen_set_label(lab0);
                } else {
                    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rb],
                                       0x00000000, lab1);
                    tcg_gen_ori_tl(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                    tcg_gen_andi_tl(sr_ove, cpu_sr, SR_OVE);
                    tcg_gen_brcondi_tl(TCG_COND_NE, sr_ove, SR_OVE, lab2);
                    gen_exception(dc, EXCP_RANGE);
                    gen_set_label(lab1);
                    tcg_gen_divu_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                    gen_set_label(lab2);
                }
                tcg_temp_free_i32(sr_ove);
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000b:
        switch (op1) {
        case 0x03:    /* l.mulu */
            LOG_DIS("l.mulu r%d, r%d, r%d\n", rd, ra, rb);
            if (rb != 0 && ra != 0) {
                TCGv_i64 result = tcg_temp_local_new_i64();
                TCGv_i64 tra = tcg_temp_local_new_i64();
                TCGv_i64 trb = tcg_temp_local_new_i64();
                TCGv_i64 high = tcg_temp_new_i64();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                int lab = gen_new_label();
                /* Calculate each result. */
                tcg_gen_extu_i32_i64(tra, cpu_R[ra]);
                tcg_gen_extu_i32_i64(trb, cpu_R[rb]);
                tcg_gen_mul_i64(result, tra, trb);
                tcg_temp_free_i64(tra);
                tcg_temp_free_i64(trb);
                tcg_gen_shri_i64(high, result, TARGET_LONG_BITS);
                /* Overflow or not. */
                tcg_gen_brcondi_i64(TCG_COND_EQ, high, 0x00000000, lab);
                tcg_gen_ori_tl(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                tcg_gen_andi_tl(sr_ove, cpu_sr, SR_OVE);
                tcg_gen_brcondi_tl(TCG_COND_NE, sr_ove, SR_OVE, lab);
                gen_exception(dc, EXCP_RANGE);
                gen_set_label(lab);
                tcg_temp_free_i64(high);
                tcg_gen_trunc_i64_tl(cpu_R[rd], result);
                tcg_temp_free_i64(result);
                tcg_temp_free_i32(sr_ove);
            } else {
                tcg_gen_movi_tl(cpu_R[rd], 0);
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000e:
        switch (op1) {
        case 0x00:    /* l.cmov */
            LOG_DIS("l.cmov r%d, r%d, r%d\n", rd, ra, rb);
            {
                int lab = gen_new_label();
                TCGv res = tcg_temp_local_new();
                TCGv sr_f = tcg_temp_new();
                tcg_gen_andi_tl(sr_f, cpu_sr, SR_F);
                tcg_gen_mov_tl(res, cpu_R[rb]);
                tcg_gen_brcondi_tl(TCG_COND_NE, sr_f, SR_F, lab);
                tcg_gen_mov_tl(res, cpu_R[ra]);
                gen_set_label(lab);
                tcg_gen_mov_tl(cpu_R[rd], res);
                tcg_temp_free(sr_f);
                tcg_temp_free(res);
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000f:
        switch (op1) {
        case 0x00:    /* l.ff1 */
            LOG_DIS("l.ff1 r%d, r%d, r%d\n", rd, ra, rb);
            gen_helper_ff1(cpu_R[rd], cpu_R[ra]);
            break;
        case 0x01:    /* l.fl1 */
            LOG_DIS("l.fl1 r%d, r%d, r%d\n", rd, ra, rb);
            gen_helper_fl1(cpu_R[rd], cpu_R[ra]);
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x0008:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:    /* l.sll */
                LOG_DIS("l.sll r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shl_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x01:    /* l.srl */
                LOG_DIS("l.srl r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_shr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x02:    /* l.sra */
                LOG_DIS("l.sra r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_sar_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;
            case 0x03:    /* l.ror */
                LOG_DIS("l.ror r%d, r%d, r%d\n", rd, ra, rb);
                tcg_gen_rotr_tl(cpu_R[rd], cpu_R[ra], cpu_R[rb]);
                break;

            default:
                gen_illegal_exception(dc);
                break;
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000c:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:    /* l.exths */
                LOG_DIS("l.exths r%d, r%d\n", rd, ra);
                tcg_gen_ext16s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x01:    /* l.extbs */
                LOG_DIS("l.extbs r%d, r%d\n", rd, ra);
                tcg_gen_ext8s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x02:    /* l.exthz */
                LOG_DIS("l.exthz r%d, r%d\n", rd, ra);
                tcg_gen_ext16u_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x03:    /* l.extbz */
                LOG_DIS("l.extbz r%d, r%d\n", rd, ra);
                tcg_gen_ext8u_tl(cpu_R[rd], cpu_R[ra]);
                break;

            default:
                gen_illegal_exception(dc);
                break;
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x000d:
        switch (op1) {
        case 0x00:
            switch (op2) {
            case 0x00:    /* l.extws */
                LOG_DIS("l.extws r%d, r%d\n", rd, ra);
                tcg_gen_ext32s_tl(cpu_R[rd], cpu_R[ra]);
                break;
            case 0x01:    /* l.extwz */
                LOG_DIS("l.extwz r%d, r%d\n", rd, ra);
                tcg_gen_ext32u_tl(cpu_R[rd], cpu_R[ra]);
                break;

            default:
                gen_illegal_exception(dc);
                break;
            }
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_misc(DisasContext *dc, uint32_t insn)
{
    uint32_t op0, op1;
    uint32_t ra, rb, rd;
#ifdef OPENRISC_DISAS
    uint32_t L6, K5;
#endif
    uint32_t I16, I5, I11, N26, tmp;
    TCGMemOp mop;

    op0 = extract32(insn, 26, 6);
    op1 = extract32(insn, 24, 2);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);
#ifdef OPENRISC_DISAS
    L6 = extract32(insn, 5, 6);
    K5 = extract32(insn, 0, 5);
#endif
    I16 = extract32(insn, 0, 16);
    I5 = extract32(insn, 21, 5);
    I11 = extract32(insn, 0, 11);
    N26 = extract32(insn, 0, 26);
    tmp = (I5<<11) + I11;

    switch (op0) {
    case 0x00:    /* l.j */
        LOG_DIS("l.j %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x01:    /* l.jal */
        LOG_DIS("l.jal %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x03:    /* l.bnf */
        LOG_DIS("l.bnf %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x04:    /* l.bf */
        LOG_DIS("l.bf %d\n", N26);
        gen_jump(dc, N26, 0, op0);
        break;

    case 0x05:
        switch (op1) {
        case 0x01:    /* l.nop */
            LOG_DIS("l.nop %d\n", I16);
            break;

        default:
            gen_illegal_exception(dc);
            break;
        }
        break;

    case 0x11:    /* l.jr */
        LOG_DIS("l.jr r%d\n", rb);
         gen_jump(dc, 0, rb, op0);
         break;

    case 0x12:    /* l.jalr */
        LOG_DIS("l.jalr r%d\n", rb);
        gen_jump(dc, 0, rb, op0);
        break;

    case 0x13:    /* l.maci */
        LOG_DIS("l.maci %d, r%d, %d\n", I5, ra, I11);
        {
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i32 dst = tcg_temp_new_i32();
            TCGv ttmp = tcg_const_tl(tmp);
            tcg_gen_mul_tl(dst, cpu_R[ra], ttmp);
            tcg_gen_ext_i32_i64(t1, dst);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_add_i64(t2, t2, t1);
            tcg_gen_trunc_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_i32(machi, t2);
            tcg_temp_free_i32(dst);
            tcg_temp_free(ttmp);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    case 0x09:    /* l.rfe */
        LOG_DIS("l.rfe\n");
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_rfe(cpu_env);
            dc->is_jmp = DISAS_UPDATE;
#endif
        }
        break;

    case 0x1c:    /* l.cust1 */
        LOG_DIS("l.cust1\n");
        break;

    case 0x1d:    /* l.cust2 */
        LOG_DIS("l.cust2\n");
        break;

    case 0x1e:    /* l.cust3 */
        LOG_DIS("l.cust3\n");
        break;

    case 0x1f:    /* l.cust4 */
        LOG_DIS("l.cust4\n");
        break;

    case 0x3c:    /* l.cust5 */
        LOG_DIS("l.cust5 r%d, r%d, r%d, %d, %d\n", rd, ra, rb, L6, K5);
        break;

    case 0x3d:    /* l.cust6 */
        LOG_DIS("l.cust6\n");
        break;

    case 0x3e:    /* l.cust7 */
        LOG_DIS("l.cust7\n");
        break;

    case 0x3f:    /* l.cust8 */
        LOG_DIS("l.cust8\n");
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x20:     l.ld
        LOG_DIS("l.ld r%d, r%d, %d\n", rd, ra, I16);
        check_ob64s(dc);
        mop = MO_TEQ;
        goto do_load;
#endif*/

    case 0x21:    /* l.lwz */
        LOG_DIS("l.lwz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TEUL;
        goto do_load;

    case 0x22:    /* l.lws */
        LOG_DIS("l.lws r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TESL;
        goto do_load;

    case 0x23:    /* l.lbz */
        LOG_DIS("l.lbz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_UB;
        goto do_load;

    case 0x24:    /* l.lbs */
        LOG_DIS("l.lbs r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_SB;
        goto do_load;

    case 0x25:    /* l.lhz */
        LOG_DIS("l.lhz r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TEUW;
        goto do_load;

    case 0x26:    /* l.lhs */
        LOG_DIS("l.lhs r%d, r%d, %d\n", rd, ra, I16);
        mop = MO_TESW;
        goto do_load;

    do_load:
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(I16, 16));
            tcg_gen_qemu_ld_tl(cpu_R[rd], t0, dc->mem_idx, mop);
            tcg_temp_free(t0);
        }
        break;

    case 0x27:    /* l.addi */
        LOG_DIS("l.addi r%d, r%d, %d\n", rd, ra, I16);
        {
            if (I16 == 0) {
                tcg_gen_mov_tl(cpu_R[rd], cpu_R[ra]);
            } else {
                int lab = gen_new_label();
                TCGv_i64 ta = tcg_temp_new_i64();
                TCGv_i64 td = tcg_temp_local_new_i64();
                TCGv_i32 res = tcg_temp_local_new_i32();
                TCGv_i32 sr_ove = tcg_temp_local_new_i32();
                tcg_gen_extu_i32_i64(ta, cpu_R[ra]);
                tcg_gen_addi_i64(td, ta, sign_extend(I16, 16));
                tcg_gen_trunc_i64_i32(res, td);
                tcg_gen_shri_i64(td, td, 32);
                tcg_gen_andi_i64(td, td, 0x3);
                /* Jump to lab when no overflow.  */
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x0, lab);
                tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x3, lab);
                tcg_gen_ori_i32(cpu_sr, cpu_sr, (SR_OV | SR_CY));
                tcg_gen_andi_i32(sr_ove, cpu_sr, SR_OVE);
                tcg_gen_brcondi_i32(TCG_COND_NE, sr_ove, SR_OVE, lab);
                gen_exception(dc, EXCP_RANGE);
                gen_set_label(lab);
                tcg_gen_mov_i32(cpu_R[rd], res);
                tcg_temp_free_i64(ta);
                tcg_temp_free_i64(td);
                tcg_temp_free_i32(res);
                tcg_temp_free_i32(sr_ove);
            }
        }
        break;

    case 0x28:    /* l.addic */
        LOG_DIS("l.addic r%d, r%d, %d\n", rd, ra, I16);
        {
            int lab = gen_new_label();
            TCGv_i64 ta = tcg_temp_new_i64();
            TCGv_i64 td = tcg_temp_local_new_i64();
            TCGv_i64 tcy = tcg_temp_local_new_i64();
            TCGv_i32 res = tcg_temp_local_new_i32();
            TCGv_i32 sr_cy = tcg_temp_local_new_i32();
            TCGv_i32 sr_ove = tcg_temp_local_new_i32();
            tcg_gen_extu_i32_i64(ta, cpu_R[ra]);
            tcg_gen_andi_i32(sr_cy, cpu_sr, SR_CY);
            tcg_gen_shri_i32(sr_cy, sr_cy, 10);
            tcg_gen_extu_i32_i64(tcy, sr_cy);
            tcg_gen_addi_i64(td, ta, sign_extend(I16, 16));
            tcg_gen_add_i64(td, td, tcy);
            tcg_gen_trunc_i64_i32(res, td);
            tcg_gen_shri_i64(td, td, 32);
            tcg_gen_andi_i64(td, td, 0x3);
            /* Jump to lab when no overflow.  */
            tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x0, lab);
            tcg_gen_brcondi_i64(TCG_COND_EQ, td, 0x3, lab);
            tcg_gen_ori_i32(cpu_sr, cpu_sr, (SR_OV | SR_CY));
            tcg_gen_andi_i32(sr_ove, cpu_sr, SR_OVE);
            tcg_gen_brcondi_i32(TCG_COND_NE, sr_ove, SR_OVE, lab);
            gen_exception(dc, EXCP_RANGE);
            gen_set_label(lab);
            tcg_gen_mov_i32(cpu_R[rd], res);
            tcg_temp_free_i64(ta);
            tcg_temp_free_i64(td);
            tcg_temp_free_i64(tcy);
            tcg_temp_free_i32(res);
            tcg_temp_free_i32(sr_cy);
            tcg_temp_free_i32(sr_ove);
        }
        break;

    case 0x29:    /* l.andi */
        LOG_DIS("l.andi r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_andi_tl(cpu_R[rd], cpu_R[ra], zero_extend(I16, 16));
        break;

    case 0x2a:    /* l.ori */
        LOG_DIS("l.ori r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_ori_tl(cpu_R[rd], cpu_R[ra], zero_extend(I16, 16));
        break;

    case 0x2b:    /* l.xori */
        LOG_DIS("l.xori r%d, r%d, %d\n", rd, ra, I16);
        tcg_gen_xori_tl(cpu_R[rd], cpu_R[ra], sign_extend(I16, 16));
        break;

    case 0x2c:    /* l.muli */
        LOG_DIS("l.muli r%d, r%d, %d\n", rd, ra, I16);
        if (ra != 0 && I16 != 0) {
            TCGv_i32 im = tcg_const_i32(I16);
            gen_helper_mul32(cpu_R[rd], cpu_env, cpu_R[ra], im);
            tcg_temp_free_i32(im);
        } else {
            tcg_gen_movi_tl(cpu_R[rd], 0x0);
        }
        break;

    case 0x2d:    /* l.mfspr */
        LOG_DIS("l.mfspr r%d, r%d, %d\n", rd, ra, I16);
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            TCGv_i32 ti = tcg_const_i32(I16);
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_mfspr(cpu_R[rd], cpu_env, cpu_R[rd], cpu_R[ra], ti);
            tcg_temp_free_i32(ti);
#endif
        }
        break;

    case 0x30:    /* l.mtspr */
        LOG_DIS("l.mtspr %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        {
#if defined(CONFIG_USER_ONLY)
            return;
#else
            TCGv_i32 im = tcg_const_i32(tmp);
            if (dc->mem_idx == MMU_USER_IDX) {
                gen_illegal_exception(dc);
                return;
            }
            gen_helper_mtspr(cpu_env, cpu_R[ra], cpu_R[rb], im);
            tcg_temp_free_i32(im);
#endif
        }
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x34:     l.sd
        LOG_DIS("l.sd %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        check_ob64s(dc);
        mop = MO_TEQ;
        goto do_store;
#endif*/

    case 0x35:    /* l.sw */
        LOG_DIS("l.sw %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_TEUL;
        goto do_store;

    case 0x36:    /* l.sb */
        LOG_DIS("l.sb %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_UB;
        goto do_store;

    case 0x37:    /* l.sh */
        LOG_DIS("l.sh %d, r%d, r%d, %d\n", I5, ra, rb, I11);
        mop = MO_TEUW;
        goto do_store;

    do_store:
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_addi_tl(t0, cpu_R[ra], sign_extend(tmp, 16));
            tcg_gen_qemu_st_tl(cpu_R[rb], t0, dc->mem_idx, mop);
            tcg_temp_free(t0);
        }
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_mac(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;
    op0 = extract32(insn, 0, 4);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);

    switch (op0) {
    case 0x0001:    /* l.mac */
        LOG_DIS("l.mac r%d, r%d\n", ra, rb);
        {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            tcg_gen_mul_tl(t0, cpu_R[ra], cpu_R[rb]);
            tcg_gen_ext_i32_i64(t1, t0);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_add_i64(t2, t2, t1);
            tcg_gen_trunc_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_i32(machi, t2);
            tcg_temp_free_i32(t0);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    case 0x0002:    /* l.msb */
        LOG_DIS("l.msb r%d, r%d\n", ra, rb);
        {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 t2 = tcg_temp_new_i64();
            tcg_gen_mul_tl(t0, cpu_R[ra], cpu_R[rb]);
            tcg_gen_ext_i32_i64(t1, t0);
            tcg_gen_concat_i32_i64(t2, maclo, machi);
            tcg_gen_sub_i64(t2, t2, t1);
            tcg_gen_trunc_i64_i32(maclo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_i32(machi, t2);
            tcg_temp_free_i32(t0);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;

    default:
        gen_illegal_exception(dc);
        break;
   }
}

static void dec_logic(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd, ra, L6;
    op0 = extract32(insn, 6, 2);
    rd = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    L6 = extract32(insn, 0, 6);

    switch (op0) {
    case 0x00:    /* l.slli */
        LOG_DIS("l.slli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shli_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x01:    /* l.srli */
        LOG_DIS("l.srli r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_shri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    case 0x02:    /* l.srai */
        LOG_DIS("l.srai r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_sari_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f)); break;

    case 0x03:    /* l.rori */
        LOG_DIS("l.rori r%d, r%d, %d\n", rd, ra, L6);
        tcg_gen_rotri_tl(cpu_R[rd], cpu_R[ra], (L6 & 0x1f));
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_M(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t rd;
    uint32_t K16;
    op0 = extract32(insn, 16, 1);
    rd = extract32(insn, 21, 5);
    K16 = extract32(insn, 0, 16);

    switch (op0) {
    case 0x0:    /* l.movhi */
        LOG_DIS("l.movhi  r%d, %d\n", rd, K16);
        tcg_gen_movi_tl(cpu_R[rd], (K16 << 16));
        break;

    case 0x1:    /* l.macrc */
        LOG_DIS("l.macrc  r%d\n", rd);
        tcg_gen_mov_tl(cpu_R[rd], maclo);
        tcg_gen_movi_tl(maclo, 0x0);
        tcg_gen_movi_tl(machi, 0x0);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_comp(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb;

    op0 = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);

    tcg_gen_movi_i32(env_btaken, 0x0);
    /* unsigned integers  */
    tcg_gen_ext32u_tl(cpu_R[ra], cpu_R[ra]);
    tcg_gen_ext32u_tl(cpu_R[rb], cpu_R[rb]);

    switch (op0) {
    case 0x0:    /* l.sfeq */
        LOG_DIS("l.sfeq  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_EQ, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1:    /* l.sfne */
        LOG_DIS("l.sfne  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_NE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x2:    /* l.sfgtu */
        LOG_DIS("l.sfgtu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GTU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x3:    /* l.sfgeu */
        LOG_DIS("l.sfgeu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GEU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x4:    /* l.sfltu */
        LOG_DIS("l.sfltu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LTU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x5:    /* l.sfleu */
        LOG_DIS("l.sfleu  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LEU, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xa:    /* l.sfgts */
        LOG_DIS("l.sfgts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xb:    /* l.sfges */
        LOG_DIS("l.sfges  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_GE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xc:    /* l.sflts */
        LOG_DIS("l.sflts  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LT, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    case 0xd:    /* l.sfles */
        LOG_DIS("l.sfles  r%d, r%d\n", ra, rb);
        tcg_gen_setcond_tl(TCG_COND_LE, env_btaken, cpu_R[ra], cpu_R[rb]);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
    wb_SR_F();
}

static void dec_compi(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, I16;

    op0 = extract32(insn, 21, 5);
    ra = extract32(insn, 16, 5);
    I16 = extract32(insn, 0, 16);

    tcg_gen_movi_i32(env_btaken, 0x0);
    I16 = sign_extend(I16, 16);

    switch (op0) {
    case 0x0:    /* l.sfeqi */
        LOG_DIS("l.sfeqi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_EQ, env_btaken, cpu_R[ra], I16);
        break;

    case 0x1:    /* l.sfnei */
        LOG_DIS("l.sfnei  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_NE, env_btaken, cpu_R[ra], I16);
        break;

    case 0x2:    /* l.sfgtui */
        LOG_DIS("l.sfgtui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GTU, env_btaken, cpu_R[ra], I16);
        break;

    case 0x3:    /* l.sfgeui */
        LOG_DIS("l.sfgeui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GEU, env_btaken, cpu_R[ra], I16);
        break;

    case 0x4:    /* l.sfltui */
        LOG_DIS("l.sfltui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LTU, env_btaken, cpu_R[ra], I16);
        break;

    case 0x5:    /* l.sfleui */
        LOG_DIS("l.sfleui  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LEU, env_btaken, cpu_R[ra], I16);
        break;

    case 0xa:    /* l.sfgtsi */
        LOG_DIS("l.sfgtsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GT, env_btaken, cpu_R[ra], I16);
        break;

    case 0xb:    /* l.sfgesi */
        LOG_DIS("l.sfgesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_GE, env_btaken, cpu_R[ra], I16);
        break;

    case 0xc:    /* l.sfltsi */
        LOG_DIS("l.sfltsi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LT, env_btaken, cpu_R[ra], I16);
        break;

    case 0xd:    /* l.sflesi */
        LOG_DIS("l.sflesi  r%d, %d\n", ra, I16);
        tcg_gen_setcondi_tl(TCG_COND_LE, env_btaken, cpu_R[ra], I16);
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
    wb_SR_F();
}

static void dec_sys(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
#ifdef OPENRISC_DISAS
    uint32_t K16;
#endif
    op0 = extract32(insn, 16, 8);
#ifdef OPENRISC_DISAS
    K16 = extract32(insn, 0, 16);
#endif

    switch (op0) {
    case 0x000:    /* l.sys */
        LOG_DIS("l.sys %d\n", K16);
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_SYSCALL);
        dc->is_jmp = DISAS_UPDATE;
        break;

    case 0x100:    /* l.trap */
        LOG_DIS("l.trap %d\n", K16);
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
        tcg_gen_movi_tl(cpu_pc, dc->pc);
        gen_exception(dc, EXCP_TRAP);
#endif
        break;

    case 0x300:    /* l.csync */
        LOG_DIS("l.csync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    case 0x200:    /* l.msync */
        LOG_DIS("l.msync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    case 0x270:    /* l.psync */
        LOG_DIS("l.psync\n");
#if defined(CONFIG_USER_ONLY)
        return;
#else
        if (dc->mem_idx == MMU_USER_IDX) {
            gen_illegal_exception(dc);
            return;
        }
#endif
        break;

    default:
        gen_illegal_exception(dc);
        break;
    }
}

static void dec_float(DisasContext *dc, uint32_t insn)
{
    uint32_t op0;
    uint32_t ra, rb, rd;
    op0 = extract32(insn, 0, 8);
    ra = extract32(insn, 16, 5);
    rb = extract32(insn, 11, 5);
    rd = extract32(insn, 21, 5);

    switch (op0) {
    case 0x00:    /* lf.add.s */
        LOG_DIS("lf.add.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_add_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x01:    /* lf.sub.s */
        LOG_DIS("lf.sub.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_sub_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;


    case 0x02:    /* lf.mul.s */
        LOG_DIS("lf.mul.s r%d, r%d, r%d\n", rd, ra, rb);
        if (ra != 0 && rb != 0) {
            gen_helper_float_mul_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        } else {
            tcg_gen_ori_tl(fpcsr, fpcsr, FPCSR_ZF);
            tcg_gen_movi_i32(cpu_R[rd], 0x0);
        }
        break;

    case 0x03:    /* lf.div.s */
        LOG_DIS("lf.div.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_div_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x04:    /* lf.itof.s */
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        gen_helper_itofs(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x05:    /* lf.ftoi.s */
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        gen_helper_ftois(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x06:    /* lf.rem.s */
        LOG_DIS("lf.rem.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_rem_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x07:    /* lf.madd.s */
        LOG_DIS("lf.madd.s r%d, r%d, r%d\n", rd, ra, rb);
        gen_helper_float_muladd_s(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x08:    /* lf.sfeq.s */
        LOG_DIS("lf.sfeq.s r%d, r%d\n", ra, rb);
        gen_helper_float_eq_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x09:    /* lf.sfne.s */
        LOG_DIS("lf.sfne.s r%d, r%d\n", ra, rb);
        gen_helper_float_ne_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0a:    /* lf.sfgt.s */
        LOG_DIS("lf.sfgt.s r%d, r%d\n", ra, rb);
        gen_helper_float_gt_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0b:    /* lf.sfge.s */
        LOG_DIS("lf.sfge.s r%d, r%d\n", ra, rb);
        gen_helper_float_ge_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0c:    /* lf.sflt.s */
        LOG_DIS("lf.sflt.s r%d, r%d\n", ra, rb);
        gen_helper_float_lt_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x0d:    /* lf.sfle.s */
        LOG_DIS("lf.sfle.s r%d, r%d\n", ra, rb);
        gen_helper_float_le_s(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

/* not used yet, open it when we need or64.  */
/*#ifdef TARGET_OPENRISC64
    case 0x10:     lf.add.d
        LOG_DIS("lf.add.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_add_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x11:     lf.sub.d
        LOG_DIS("lf.sub.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_sub_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x12:     lf.mul.d
        LOG_DIS("lf.mul.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        if (ra != 0 && rb != 0) {
            gen_helper_float_mul_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        } else {
            tcg_gen_ori_tl(fpcsr, fpcsr, FPCSR_ZF);
            tcg_gen_movi_i64(cpu_R[rd], 0x0);
        }
        break;

    case 0x13:     lf.div.d
        LOG_DIS("lf.div.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_div_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x14:     lf.itof.d
        LOG_DIS("lf.itof r%d, r%d\n", rd, ra);
        check_of64s(dc);
        gen_helper_itofd(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x15:     lf.ftoi.d
        LOG_DIS("lf.ftoi r%d, r%d\n", rd, ra);
        check_of64s(dc);
        gen_helper_ftoid(cpu_R[rd], cpu_env, cpu_R[ra]);
        break;

    case 0x16:     lf.rem.d
        LOG_DIS("lf.rem.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_rem_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x17:     lf.madd.d
        LOG_DIS("lf.madd.d r%d, r%d, r%d\n", rd, ra, rb);
        check_of64s(dc);
        gen_helper_float_muladd_d(cpu_R[rd], cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x18:     lf.sfeq.d
        LOG_DIS("lf.sfeq.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_eq_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1a:     lf.sfgt.d
        LOG_DIS("lf.sfgt.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_gt_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1b:     lf.sfge.d
        LOG_DIS("lf.sfge.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_ge_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x19:     lf.sfne.d
        LOG_DIS("lf.sfne.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_ne_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1c:     lf.sflt.d
        LOG_DIS("lf.sflt.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_lt_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;

    case 0x1d:     lf.sfle.d
        LOG_DIS("lf.sfle.d r%d, r%d\n", ra, rb);
        check_of64s(dc);
        gen_helper_float_le_d(env_btaken, cpu_env, cpu_R[ra], cpu_R[rb]);
        break;
#endif*/

    default:
        gen_illegal_exception(dc);
        break;
    }
    wb_SR_F();
}

static void disas_openrisc_insn(DisasContext *dc, OpenRISCCPU *cpu)
{
    uint32_t op0;
    uint32_t insn;
    insn = cpu_ldl_code(&cpu->env, dc->pc);
    op0 = extract32(insn, 26, 6);

    switch (op0) {
    case 0x06:
        dec_M(dc, insn);
        break;

    case 0x08:
        dec_sys(dc, insn);
        break;

    case 0x2e:
        dec_logic(dc, insn);
        break;

    case 0x2f:
        dec_compi(dc, insn);
        break;

    case 0x31:
        dec_mac(dc, insn);
        break;

    case 0x32:
        dec_float(dc, insn);
        break;

    case 0x38:
        dec_calc(dc, insn);
        break;

    case 0x39:
        dec_comp(dc, insn);
        break;

    default:
        dec_misc(dc, insn);
        break;
    }
}

static void check_breakpoint(OpenRISCCPU *cpu, DisasContext *dc)
{
    CPUState *cs = CPU(cpu);
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&cs->breakpoints))) {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                tcg_gen_movi_tl(cpu_pc, dc->pc);
                gen_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
            }
        }
    }
}

static inline void gen_intermediate_code_internal(OpenRISCCPU *cpu,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
    CPUState *cs = CPU(cpu);
    struct DisasContext ctx, *dc = &ctx;
    uint16_t *gen_opc_end;
    uint32_t pc_start;
    int j, k;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    dc->tb = tb;

    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;
    dc->is_jmp = DISAS_NEXT;
    dc->ppc = pc_start;
    dc->pc = pc_start;
    dc->flags = cpu->env.cpucfgr;
    dc->mem_idx = cpu_mmu_index(&cpu->env);
    dc->synced_flags = dc->tb_flags = tb->flags;
    dc->delayed_branch = !!(dc->tb_flags & D_FLAG);
    dc->singlestep_enabled = cs->singlestep_enabled;
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("-----------------------------------------\n");
        log_cpu_state(CPU(cpu), 0);
    }

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    k = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_tb_start();

    do {
        check_breakpoint(cpu, dc);
        if (search_pc) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (k < j) {
                k++;
                while (k < j) {
                    tcg_ctx.gen_opc_instr_start[k++] = 0;
                }
            }
            tcg_ctx.gen_opc_pc[k] = dc->pc;
            tcg_ctx.gen_opc_instr_start[k] = 1;
            tcg_ctx.gen_opc_icount[k] = num_insns;
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
            tcg_gen_debug_insn_start(dc->pc);
        }

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }
        dc->ppc = dc->pc - 4;
        dc->npc = dc->pc + 4;
        tcg_gen_movi_tl(cpu_ppc, dc->ppc);
        tcg_gen_movi_tl(cpu_npc, dc->npc);
        disas_openrisc_insn(dc, cpu);
        dc->pc = dc->npc;
        num_insns++;
        /* delay slot */
        if (dc->delayed_branch) {
            dc->delayed_branch--;
            if (!dc->delayed_branch) {
                dc->tb_flags &= ~D_FLAG;
                gen_sync_flags(dc);
                tcg_gen_mov_tl(cpu_pc, jmp_pc);
                tcg_gen_mov_tl(cpu_npc, jmp_pc);
                tcg_gen_movi_tl(jmp_pc, 0);
                tcg_gen_exit_tb(0);
                dc->is_jmp = DISAS_JUMP;
                break;
            }
        }
    } while (!dc->is_jmp
             && tcg_ctx.gen_opc_ptr < gen_opc_end
             && !cs->singlestep_enabled
             && !singlestep
             && (dc->pc < next_page_start)
             && num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }
    if (dc->is_jmp == DISAS_NEXT) {
        dc->is_jmp = DISAS_UPDATE;
        tcg_gen_movi_tl(cpu_pc, dc->pc);
    }
    if (unlikely(cs->singlestep_enabled)) {
        if (dc->is_jmp == DISAS_NEXT) {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }
        gen_exception(dc, EXCP_DEBUG);
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 0, dc->pc);
            break;
        default:
        case DISAS_JUMP:
            break;
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
        k++;
        while (k <= j) {
            tcg_ctx.gen_opc_instr_start[k++] = 0;
        }
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("\n");
        log_target_disas(&cpu->env, pc_start, dc->pc - pc_start, 0);
        qemu_log("\nisize=%d osize=%td\n",
            dc->pc - pc_start, tcg_ctx.gen_opc_ptr -
            tcg_ctx.gen_opc_buf);
    }
#endif
}

void gen_intermediate_code(CPUOpenRISCState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(openrisc_env_get_cpu(env), tb, 0);
}

void gen_intermediate_code_pc(CPUOpenRISCState *env,
                              struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(openrisc_env_get_cpu(env), tb, 1);
}

void openrisc_cpu_dump_state(CPUState *cs, FILE *f,
                             fprintf_function cpu_fprintf,
                             int flags)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);
    CPUOpenRISCState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "PC=%08x\n", env->pc);
    for (i = 0; i < 32; ++i) {
        cpu_fprintf(f, "R%02d=%08x%c", i, env->gpr[i],
                    (i % 4) == 3 ? '\n' : ' ');
    }
}

void restore_state_to_opc(CPUOpenRISCState *env, TranslationBlock *tb,
                          int pc_pos)
{
    env->pc = tcg_ctx.gen_opc_pc[pc_pos];
}
