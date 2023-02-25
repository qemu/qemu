/*
 *  CRIS emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2008 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * FIXME:
 * The condition code translation is in need of attention.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "mmu.h"
#include "exec/cpu_ldst.h"
#include "exec/translator.h"
#include "crisv32-decode.h"
#include "qemu/qemu-print.h"

#include "exec/helper-gen.h"

#include "exec/log.h"


#define DISAS_CRIS 0
#if DISAS_CRIS
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
#endif

#define D(x)
#define BUG() (gen_BUG(dc, __FILE__, __LINE__))
#define BUG_ON(x) ({if (x) BUG();})

/*
 * Target-specific is_jmp field values
 */
/* Only pc was modified dynamically */
#define DISAS_JUMP          DISAS_TARGET_0
/* Cpu state was modified dynamically, including pc */
#define DISAS_UPDATE        DISAS_TARGET_1
/* Cpu state was modified dynamically, excluding pc -- use npc */
#define DISAS_UPDATE_NEXT   DISAS_TARGET_2
/* PC update for delayed branch, see cpustate_changed otherwise */
#define DISAS_DBRANCH       DISAS_TARGET_3

/* Used by the decoder.  */
#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

#define CC_MASK_NZ 0xc
#define CC_MASK_NZV 0xe
#define CC_MASK_NZVC 0xf
#define CC_MASK_RNZV 0x10e

static TCGv cpu_R[16];
static TCGv cpu_PR[16];
static TCGv cc_x;
static TCGv cc_src;
static TCGv cc_dest;
static TCGv cc_result;
static TCGv cc_op;
static TCGv cc_size;
static TCGv cc_mask;

static TCGv env_btaken;
static TCGv env_btarget;
static TCGv env_pc;

#include "exec/gen-icount.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
    DisasContextBase base;

    CRISCPU *cpu;
    target_ulong pc, ppc;

    /* Decoder.  */
        unsigned int (*decoder)(CPUCRISState *env, struct DisasContext *dc);
    uint32_t ir;
    uint32_t opcode;
    unsigned int op1;
    unsigned int op2;
    unsigned int zsize, zzsize;
    unsigned int mode;
    unsigned int postinc;

    unsigned int size;
    unsigned int src;
    unsigned int dst;
    unsigned int cond;

    int update_cc;
    int cc_op;
    int cc_size;
    uint32_t cc_mask;

    int cc_size_uptodate; /* -1 invalid or last written value.  */

    int cc_x_uptodate;  /* 1 - ccs, 2 - known | X_FLAG. 0 not up-to-date.  */
    int flags_uptodate; /* Whether or not $ccs is up-to-date.  */
    int flags_x;

    int clear_x; /* Clear x after this insn?  */
    int clear_prefix; /* Clear prefix after this insn?  */
    int clear_locked_irq; /* Clear the irq lockout.  */
    int cpustate_changed;
    unsigned int tb_flags; /* tb dependent flags.  */

#define JMP_NOJMP     0
#define JMP_DIRECT    1
#define JMP_DIRECT_CC 2
#define JMP_INDIRECT  3
    int jmp; /* 0=nojmp, 1=direct, 2=indirect.  */
    uint32_t jmp_pc;

    int delayed_branch;
} DisasContext;

static void gen_BUG(DisasContext *dc, const char *file, int line)
{
    cpu_abort(CPU(dc->cpu), "%s:%d pc=%x\n", file, line, dc->pc);
}

static const char * const regnames_v32[] =
{
    "$r0", "$r1", "$r2", "$r3",
    "$r4", "$r5", "$r6", "$r7",
    "$r8", "$r9", "$r10", "$r11",
    "$r12", "$r13", "$sp", "$acr",
};

static const char * const pregnames_v32[] =
{
    "$bz", "$vr", "$pid", "$srs",
    "$wz", "$exs", "$eda", "$mof",
    "$dz", "$ebp", "$erp", "$srp",
    "$nrp", "$ccs", "$usp", "$spc",
};

/* We need this table to handle preg-moves with implicit width.  */
static const int preg_sizes[] = {
    1, /* bz.  */
    1, /* vr.  */
    4, /* pid.  */
    1, /* srs.  */
    2, /* wz.  */
    4, 4, 4,
    4, 4, 4, 4,
    4, 4, 4, 4,
};

#define t_gen_mov_TN_env(tn, member) \
    tcg_gen_ld_tl(tn, cpu_env, offsetof(CPUCRISState, member))
#define t_gen_mov_env_TN(member, tn) \
    tcg_gen_st_tl(tn, cpu_env, offsetof(CPUCRISState, member))
#define t_gen_movi_env_TN(member, c) \
    do { \
        TCGv tc = tcg_const_tl(c); \
        t_gen_mov_env_TN(member, tc); \
        tcg_temp_free(tc); \
    } while (0)

static inline void t_gen_mov_TN_preg(TCGv tn, int r)
{
    assert(r >= 0 && r <= 15);
    if (r == PR_BZ || r == PR_WZ || r == PR_DZ) {
        tcg_gen_movi_tl(tn, 0);
    } else if (r == PR_VR) {
        tcg_gen_movi_tl(tn, 32);
    } else {
        tcg_gen_mov_tl(tn, cpu_PR[r]);
    }
}
static inline void t_gen_mov_preg_TN(DisasContext *dc, int r, TCGv tn)
{
    assert(r >= 0 && r <= 15);
    if (r == PR_BZ || r == PR_WZ || r == PR_DZ) {
        return;
    } else if (r == PR_SRS) {
        tcg_gen_andi_tl(cpu_PR[r], tn, 3);
    } else {
        if (r == PR_PID) {
            gen_helper_tlb_flush_pid(cpu_env, tn);
        }
        if (dc->tb_flags & S_FLAG && r == PR_SPC) {
            gen_helper_spc_write(cpu_env, tn);
        } else if (r == PR_CCS) {
            dc->cpustate_changed = 1;
        }
        tcg_gen_mov_tl(cpu_PR[r], tn);
    }
}

/* Sign extend at translation time.  */
static int sign_extend(unsigned int val, unsigned int width)
{
    int sval;

    /* LSL.  */
    val <<= 31 - width;
    sval = val;
    /* ASR.  */
    sval >>= 31 - width;
    return sval;
}

static int cris_fetch(CPUCRISState *env, DisasContext *dc, uint32_t addr,
              unsigned int size, unsigned int sign)
{
    int r;

    switch (size) {
    case 4:
    {
        r = cpu_ldl_code(env, addr);
        break;
    }
    case 2:
    {
        if (sign) {
            r = cpu_ldsw_code(env, addr);
        } else {
            r = cpu_lduw_code(env, addr);
        }
        break;
    }
    case 1:
    {
        if (sign) {
            r = cpu_ldsb_code(env, addr);
        } else {
            r = cpu_ldub_code(env, addr);
        }
        break;
    }
    default:
        cpu_abort(CPU(dc->cpu), "Invalid fetch size %d\n", size);
        break;
    }
    return r;
}

static void cris_lock_irq(DisasContext *dc)
{
    dc->clear_locked_irq = 0;
    t_gen_movi_env_TN(locked_irq, 1);
}

static inline void t_gen_raise_exception(uint32_t index)
{
        TCGv_i32 tmp = tcg_const_i32(index);
        gen_helper_raise_exception(cpu_env, tmp);
        tcg_temp_free_i32(tmp);
}

static void t_gen_lsl(TCGv d, TCGv a, TCGv b)
{
    TCGv t0, t_31;

    t0 = tcg_temp_new();
    t_31 = tcg_const_tl(31);
    tcg_gen_shl_tl(d, a, b);

    tcg_gen_sub_tl(t0, t_31, b);
    tcg_gen_sar_tl(t0, t0, t_31);
    tcg_gen_and_tl(t0, t0, d);
    tcg_gen_xor_tl(d, d, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t_31);
}

static void t_gen_lsr(TCGv d, TCGv a, TCGv b)
{
    TCGv t0, t_31;

    t0 = tcg_temp_new();
    t_31 = tcg_temp_new();
    tcg_gen_shr_tl(d, a, b);

    tcg_gen_movi_tl(t_31, 31);
    tcg_gen_sub_tl(t0, t_31, b);
    tcg_gen_sar_tl(t0, t0, t_31);
    tcg_gen_and_tl(t0, t0, d);
    tcg_gen_xor_tl(d, d, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t_31);
}

static void t_gen_asr(TCGv d, TCGv a, TCGv b)
{
    TCGv t0, t_31;

    t0 = tcg_temp_new();
    t_31 = tcg_temp_new();
    tcg_gen_sar_tl(d, a, b);

    tcg_gen_movi_tl(t_31, 31);
    tcg_gen_sub_tl(t0, t_31, b);
    tcg_gen_sar_tl(t0, t0, t_31);
    tcg_gen_or_tl(d, d, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t_31);
}

static void t_gen_cris_dstep(TCGv d, TCGv a, TCGv b)
{
    TCGv t = tcg_temp_new();

    /*
     * d <<= 1
     * if (d >= s)
     *    d -= s;
     */
    tcg_gen_shli_tl(d, a, 1);
    tcg_gen_sub_tl(t, d, b);
    tcg_gen_movcond_tl(TCG_COND_GEU, d, d, b, t, d);
    tcg_temp_free(t);
}

static void t_gen_cris_mstep(TCGv d, TCGv a, TCGv b, TCGv ccs)
{
    TCGv t;

    /*
     * d <<= 1
     * if (n)
     *    d += s;
     */
    t = tcg_temp_new();
    tcg_gen_shli_tl(d, a, 1);
    tcg_gen_shli_tl(t, ccs, 31 - 3);
    tcg_gen_sari_tl(t, t, 31);
    tcg_gen_and_tl(t, t, b);
    tcg_gen_add_tl(d, d, t);
    tcg_temp_free(t);
}

/* Extended arithmetics on CRIS.  */
static inline void t_gen_add_flag(TCGv d, int flag)
{
    TCGv c;

    c = tcg_temp_new();
    t_gen_mov_TN_preg(c, PR_CCS);
    /* Propagate carry into d.  */
    tcg_gen_andi_tl(c, c, 1 << flag);
    if (flag) {
        tcg_gen_shri_tl(c, c, flag);
    }
    tcg_gen_add_tl(d, d, c);
    tcg_temp_free(c);
}

static inline void t_gen_addx_carry(DisasContext *dc, TCGv d)
{
    if (dc->flags_x) {
        TCGv c = tcg_temp_new();

        t_gen_mov_TN_preg(c, PR_CCS);
        /* C flag is already at bit 0.  */
        tcg_gen_andi_tl(c, c, C_FLAG);
        tcg_gen_add_tl(d, d, c);
        tcg_temp_free(c);
    }
}

static inline void t_gen_subx_carry(DisasContext *dc, TCGv d)
{
    if (dc->flags_x) {
        TCGv c = tcg_temp_new();

        t_gen_mov_TN_preg(c, PR_CCS);
        /* C flag is already at bit 0.  */
        tcg_gen_andi_tl(c, c, C_FLAG);
        tcg_gen_sub_tl(d, d, c);
        tcg_temp_free(c);
    }
}

/* Swap the two bytes within each half word of the s operand.
   T0 = ((T0 << 8) & 0xff00ff00) | ((T0 >> 8) & 0x00ff00ff)  */
static inline void t_gen_swapb(TCGv d, TCGv s)
{
    TCGv t, org_s;

    t = tcg_temp_new();
    org_s = tcg_temp_new();

    /* d and s may refer to the same object.  */
    tcg_gen_mov_tl(org_s, s);
    tcg_gen_shli_tl(t, org_s, 8);
    tcg_gen_andi_tl(d, t, 0xff00ff00);
    tcg_gen_shri_tl(t, org_s, 8);
    tcg_gen_andi_tl(t, t, 0x00ff00ff);
    tcg_gen_or_tl(d, d, t);
    tcg_temp_free(t);
    tcg_temp_free(org_s);
}

/* Swap the halfwords of the s operand.  */
static inline void t_gen_swapw(TCGv d, TCGv s)
{
    TCGv t;
    /* d and s refer the same object.  */
    t = tcg_temp_new();
    tcg_gen_mov_tl(t, s);
    tcg_gen_shli_tl(d, t, 16);
    tcg_gen_shri_tl(t, t, 16);
    tcg_gen_or_tl(d, d, t);
    tcg_temp_free(t);
}

/* Reverse the within each byte.
   T0 = (((T0 << 7) & 0x80808080) |
   ((T0 << 5) & 0x40404040) |
   ((T0 << 3) & 0x20202020) |
   ((T0 << 1) & 0x10101010) |
   ((T0 >> 1) & 0x08080808) |
   ((T0 >> 3) & 0x04040404) |
   ((T0 >> 5) & 0x02020202) |
   ((T0 >> 7) & 0x01010101));
 */
static void t_gen_swapr(TCGv d, TCGv s)
{
    static const struct {
        int shift; /* LSL when positive, LSR when negative.  */
        uint32_t mask;
    } bitrev[] = {
        {7, 0x80808080},
        {5, 0x40404040},
        {3, 0x20202020},
        {1, 0x10101010},
        {-1, 0x08080808},
        {-3, 0x04040404},
        {-5, 0x02020202},
        {-7, 0x01010101}
    };
    int i;
    TCGv t, org_s;

    /* d and s refer the same object.  */
    t = tcg_temp_new();
    org_s = tcg_temp_new();
    tcg_gen_mov_tl(org_s, s);

    tcg_gen_shli_tl(t, org_s,  bitrev[0].shift);
    tcg_gen_andi_tl(d, t,  bitrev[0].mask);
    for (i = 1; i < ARRAY_SIZE(bitrev); i++) {
        if (bitrev[i].shift >= 0) {
            tcg_gen_shli_tl(t, org_s,  bitrev[i].shift);
        } else {
            tcg_gen_shri_tl(t, org_s,  -bitrev[i].shift);
        }
        tcg_gen_andi_tl(t, t,  bitrev[i].mask);
        tcg_gen_or_tl(d, d, t);
    }
    tcg_temp_free(t);
    tcg_temp_free(org_s);
}

static bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
    return translator_use_goto_tb(&dc->base, dest);
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(env_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, n);
    } else {
        tcg_gen_movi_tl(env_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static inline void cris_clear_x_flag(DisasContext *dc)
{
    if (dc->flags_x) {
        dc->flags_uptodate = 0;
    }
    dc->flags_x = 0;
}

static void cris_flush_cc_state(DisasContext *dc)
{
    if (dc->cc_size_uptodate != dc->cc_size) {
        tcg_gen_movi_tl(cc_size, dc->cc_size);
        dc->cc_size_uptodate = dc->cc_size;
    }
    tcg_gen_movi_tl(cc_op, dc->cc_op);
    tcg_gen_movi_tl(cc_mask, dc->cc_mask);
}

static void cris_evaluate_flags(DisasContext *dc)
{
    if (dc->flags_uptodate) {
        return;
    }

    cris_flush_cc_state(dc);

    switch (dc->cc_op) {
    case CC_OP_MCP:
        gen_helper_evaluate_flags_mcp(cpu_PR[PR_CCS], cpu_env,
                cpu_PR[PR_CCS], cc_src,
                cc_dest, cc_result);
        break;
    case CC_OP_MULS:
        gen_helper_evaluate_flags_muls(cpu_PR[PR_CCS], cpu_env,
                cpu_PR[PR_CCS], cc_result,
                cpu_PR[PR_MOF]);
        break;
    case CC_OP_MULU:
        gen_helper_evaluate_flags_mulu(cpu_PR[PR_CCS], cpu_env,
                cpu_PR[PR_CCS], cc_result,
                cpu_PR[PR_MOF]);
        break;
    case CC_OP_MOVE:
    case CC_OP_AND:
    case CC_OP_OR:
    case CC_OP_XOR:
    case CC_OP_ASR:
    case CC_OP_LSR:
    case CC_OP_LSL:
        switch (dc->cc_size) {
        case 4:
            gen_helper_evaluate_flags_move_4(cpu_PR[PR_CCS],
                    cpu_env, cpu_PR[PR_CCS], cc_result);
            break;
        case 2:
            gen_helper_evaluate_flags_move_2(cpu_PR[PR_CCS],
                    cpu_env, cpu_PR[PR_CCS], cc_result);
            break;
        default:
            gen_helper_evaluate_flags(cpu_env);
            break;
        }
        break;
    case CC_OP_FLAGS:
        /* live.  */
        break;
    case CC_OP_SUB:
    case CC_OP_CMP:
        if (dc->cc_size == 4) {
            gen_helper_evaluate_flags_sub_4(cpu_PR[PR_CCS], cpu_env,
                    cpu_PR[PR_CCS], cc_src, cc_dest, cc_result);
        } else {
            gen_helper_evaluate_flags(cpu_env);
        }

        break;
    default:
        switch (dc->cc_size) {
        case 4:
            gen_helper_evaluate_flags_alu_4(cpu_PR[PR_CCS], cpu_env,
                    cpu_PR[PR_CCS], cc_src, cc_dest, cc_result);
            break;
        default:
            gen_helper_evaluate_flags(cpu_env);
            break;
        }
        break;
    }

    if (dc->flags_x) {
        tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], X_FLAG);
    } else if (dc->cc_op == CC_OP_FLAGS) {
        tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~X_FLAG);
    }
    dc->flags_uptodate = 1;
}

static void cris_cc_mask(DisasContext *dc, unsigned int mask)
{
    uint32_t ovl;

    if (!mask) {
        dc->update_cc = 0;
        return;
    }

    /* Check if we need to evaluate the condition codes due to
       CC overlaying.  */
    ovl = (dc->cc_mask ^ mask) & ~mask;
    if (ovl) {
        /* TODO: optimize this case. It trigs all the time.  */
        cris_evaluate_flags(dc);
    }
    dc->cc_mask = mask;
    dc->update_cc = 1;
}

static void cris_update_cc_op(DisasContext *dc, int op, int size)
{
    dc->cc_op = op;
    dc->cc_size = size;
    dc->flags_uptodate = 0;
}

static inline void cris_update_cc_x(DisasContext *dc)
{
    /* Save the x flag state at the time of the cc snapshot.  */
    if (dc->cc_x_uptodate == (2 | dc->flags_x)) {
        return;
    }
    tcg_gen_movi_tl(cc_x, dc->flags_x);
    dc->cc_x_uptodate = 2 | dc->flags_x;
}

/* Update cc prior to executing ALU op. Needs source operands untouched.  */
static void cris_pre_alu_update_cc(DisasContext *dc, int op, 
                   TCGv dst, TCGv src, int size)
{
    if (dc->update_cc) {
        cris_update_cc_op(dc, op, size);
        tcg_gen_mov_tl(cc_src, src);

        if (op != CC_OP_MOVE
            && op != CC_OP_AND
            && op != CC_OP_OR
            && op != CC_OP_XOR
            && op != CC_OP_ASR
            && op != CC_OP_LSR
            && op != CC_OP_LSL) {
            tcg_gen_mov_tl(cc_dest, dst);
        }

        cris_update_cc_x(dc);
    }
}

/* Update cc after executing ALU op. needs the result.  */
static inline void cris_update_result(DisasContext *dc, TCGv res)
{
    if (dc->update_cc) {
        tcg_gen_mov_tl(cc_result, res);
    }
}

/* Returns one if the write back stage should execute.  */
static void cris_alu_op_exec(DisasContext *dc, int op, 
                   TCGv dst, TCGv a, TCGv b, int size)
{
    /* Emit the ALU insns.  */
    switch (op) {
    case CC_OP_ADD:
        tcg_gen_add_tl(dst, a, b);
        /* Extended arithmetics.  */
        t_gen_addx_carry(dc, dst);
        break;
    case CC_OP_ADDC:
        tcg_gen_add_tl(dst, a, b);
        t_gen_add_flag(dst, 0); /* C_FLAG.  */
        break;
    case CC_OP_MCP:
        tcg_gen_add_tl(dst, a, b);
        t_gen_add_flag(dst, 8); /* R_FLAG.  */
        break;
    case CC_OP_SUB:
        tcg_gen_sub_tl(dst, a, b);
        /* Extended arithmetics.  */
        t_gen_subx_carry(dc, dst);
        break;
    case CC_OP_MOVE:
        tcg_gen_mov_tl(dst, b);
        break;
    case CC_OP_OR:
        tcg_gen_or_tl(dst, a, b);
        break;
    case CC_OP_AND:
        tcg_gen_and_tl(dst, a, b);
        break;
    case CC_OP_XOR:
        tcg_gen_xor_tl(dst, a, b);
        break;
    case CC_OP_LSL:
        t_gen_lsl(dst, a, b);
        break;
    case CC_OP_LSR:
        t_gen_lsr(dst, a, b);
        break;
    case CC_OP_ASR:
        t_gen_asr(dst, a, b);
        break;
    case CC_OP_NEG:
        tcg_gen_neg_tl(dst, b);
        /* Extended arithmetics.  */
        t_gen_subx_carry(dc, dst);
        break;
    case CC_OP_LZ:
        tcg_gen_clzi_tl(dst, b, TARGET_LONG_BITS);
        break;
    case CC_OP_MULS:
        tcg_gen_muls2_tl(dst, cpu_PR[PR_MOF], a, b);
        break;
    case CC_OP_MULU:
        tcg_gen_mulu2_tl(dst, cpu_PR[PR_MOF], a, b);
        break;
    case CC_OP_DSTEP:
        t_gen_cris_dstep(dst, a, b);
        break;
    case CC_OP_MSTEP:
        t_gen_cris_mstep(dst, a, b, cpu_PR[PR_CCS]);
        break;
    case CC_OP_BOUND:
        tcg_gen_movcond_tl(TCG_COND_LEU, dst, a, b, a, b);
        break;
    case CC_OP_CMP:
        tcg_gen_sub_tl(dst, a, b);
        /* Extended arithmetics.  */
        t_gen_subx_carry(dc, dst);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "illegal ALU op.\n");
        BUG();
        break;
    }

    if (size == 1) {
        tcg_gen_andi_tl(dst, dst, 0xff);
    } else if (size == 2) {
        tcg_gen_andi_tl(dst, dst, 0xffff);
    }
}

static void cris_alu(DisasContext *dc, int op,
                   TCGv d, TCGv op_a, TCGv op_b, int size)
{
    TCGv tmp;
    int writeback;

    writeback = 1;

    if (op == CC_OP_CMP) {
        tmp = tcg_temp_new();
        writeback = 0;
    } else if (size == 4) {
        tmp = d;
        writeback = 0;
    } else {
        tmp = tcg_temp_new();
    }


    cris_pre_alu_update_cc(dc, op, op_a, op_b, size);
    cris_alu_op_exec(dc, op, tmp, op_a, op_b, size);
    cris_update_result(dc, tmp);

    /* Writeback.  */
    if (writeback) {
        if (size == 1) {
            tcg_gen_andi_tl(d, d, ~0xff);
        } else {
            tcg_gen_andi_tl(d, d, ~0xffff);
        }
        tcg_gen_or_tl(d, d, tmp);
    }
    if (tmp != d) {
        tcg_temp_free(tmp);
    }
}

static int arith_cc(DisasContext *dc)
{
    if (dc->update_cc) {
        switch (dc->cc_op) {
        case CC_OP_ADDC: return 1;
        case CC_OP_ADD: return 1;
        case CC_OP_SUB: return 1;
        case CC_OP_DSTEP: return 1;
        case CC_OP_LSL: return 1;
        case CC_OP_LSR: return 1;
        case CC_OP_ASR: return 1;
        case CC_OP_CMP: return 1;
        case CC_OP_NEG: return 1;
        case CC_OP_OR: return 1;
        case CC_OP_AND: return 1;
        case CC_OP_XOR: return 1;
        case CC_OP_MULU: return 1;
        case CC_OP_MULS: return 1;
        default:
            return 0;
        }
    }
    return 0;
}

static void gen_tst_cc (DisasContext *dc, TCGv cc, int cond)
{
    int arith_opt, move_opt;

    /* TODO: optimize more condition codes.  */

    /*
     * If the flags are live, we've gotta look into the bits of CCS.
     * Otherwise, if we just did an arithmetic operation we try to
     * evaluate the condition code faster.
     *
     * When this function is done, T0 should be non-zero if the condition
     * code is true.
     */
    arith_opt = arith_cc(dc) && !dc->flags_uptodate;
    move_opt = (dc->cc_op == CC_OP_MOVE);
    switch (cond) {
    case CC_EQ:
        if ((arith_opt || move_opt)
                && dc->cc_x_uptodate != (2 | X_FLAG)) {
            tcg_gen_setcondi_tl(TCG_COND_EQ, cc, cc_result, 0);
        } else {
            cris_evaluate_flags(dc);
            tcg_gen_andi_tl(cc,
                    cpu_PR[PR_CCS], Z_FLAG);
        }
        break;
    case CC_NE:
        if ((arith_opt || move_opt)
                && dc->cc_x_uptodate != (2 | X_FLAG)) {
            tcg_gen_mov_tl(cc, cc_result);
        } else {
            cris_evaluate_flags(dc);
            tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
                    Z_FLAG);
            tcg_gen_andi_tl(cc, cc, Z_FLAG);
        }
        break;
    case CC_CS:
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], C_FLAG);
        break;
    case CC_CC:
        cris_evaluate_flags(dc);
        tcg_gen_xori_tl(cc, cpu_PR[PR_CCS], C_FLAG);
        tcg_gen_andi_tl(cc, cc, C_FLAG);
        break;
    case CC_VS:
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], V_FLAG);
        break;
    case CC_VC:
        cris_evaluate_flags(dc);
        tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
                V_FLAG);
        tcg_gen_andi_tl(cc, cc, V_FLAG);
        break;
    case CC_PL:
        if (arith_opt || move_opt) {
            int bits = 31;

            if (dc->cc_size == 1) {
                bits = 7;
            } else if (dc->cc_size == 2) {
                bits = 15;
            }

            tcg_gen_shri_tl(cc, cc_result, bits);
            tcg_gen_xori_tl(cc, cc, 1);
        } else {
            cris_evaluate_flags(dc);
            tcg_gen_xori_tl(cc, cpu_PR[PR_CCS],
                    N_FLAG);
            tcg_gen_andi_tl(cc, cc, N_FLAG);
        }
        break;
    case CC_MI:
        if (arith_opt || move_opt) {
            int bits = 31;

            if (dc->cc_size == 1) {
                bits = 7;
            } else if (dc->cc_size == 2) {
                bits = 15;
            }

            tcg_gen_shri_tl(cc, cc_result, bits);
            tcg_gen_andi_tl(cc, cc, 1);
        } else {
            cris_evaluate_flags(dc);
            tcg_gen_andi_tl(cc, cpu_PR[PR_CCS],
                    N_FLAG);
        }
        break;
    case CC_LS:
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(cc, cpu_PR[PR_CCS],
                C_FLAG | Z_FLAG);
        break;
    case CC_HI:
        cris_evaluate_flags(dc);
        {
            TCGv tmp;

            tmp = tcg_temp_new();
            tcg_gen_xori_tl(tmp, cpu_PR[PR_CCS],
                    C_FLAG | Z_FLAG);
            /* Overlay the C flag on top of the Z.  */
            tcg_gen_shli_tl(cc, tmp, 2);
            tcg_gen_and_tl(cc, tmp, cc);
            tcg_gen_andi_tl(cc, cc, Z_FLAG);

            tcg_temp_free(tmp);
        }
        break;
    case CC_GE:
        cris_evaluate_flags(dc);
        /* Overlay the V flag on top of the N.  */
        tcg_gen_shli_tl(cc, cpu_PR[PR_CCS], 2);
        tcg_gen_xor_tl(cc,
                cpu_PR[PR_CCS], cc);
        tcg_gen_andi_tl(cc, cc, N_FLAG);
        tcg_gen_xori_tl(cc, cc, N_FLAG);
        break;
    case CC_LT:
        cris_evaluate_flags(dc);
        /* Overlay the V flag on top of the N.  */
        tcg_gen_shli_tl(cc, cpu_PR[PR_CCS], 2);
        tcg_gen_xor_tl(cc,
                cpu_PR[PR_CCS], cc);
        tcg_gen_andi_tl(cc, cc, N_FLAG);
        break;
    case CC_GT:
        cris_evaluate_flags(dc);
        {
            TCGv n, z;

            n = tcg_temp_new();
            z = tcg_temp_new();

            /* To avoid a shift we overlay everything on
                   the V flag.  */
            tcg_gen_shri_tl(n, cpu_PR[PR_CCS], 2);
            tcg_gen_shri_tl(z, cpu_PR[PR_CCS], 1);
            /* invert Z.  */
            tcg_gen_xori_tl(z, z, 2);

            tcg_gen_xor_tl(n, n, cpu_PR[PR_CCS]);
            tcg_gen_xori_tl(n, n, 2);
            tcg_gen_and_tl(cc, z, n);
            tcg_gen_andi_tl(cc, cc, 2);

            tcg_temp_free(n);
            tcg_temp_free(z);
        }
        break;
    case CC_LE:
        cris_evaluate_flags(dc);
        {
            TCGv n, z;

            n = tcg_temp_new();
            z = tcg_temp_new();

            /* To avoid a shift we overlay everything on
                   the V flag.  */
            tcg_gen_shri_tl(n, cpu_PR[PR_CCS], 2);
            tcg_gen_shri_tl(z, cpu_PR[PR_CCS], 1);

            tcg_gen_xor_tl(n, n, cpu_PR[PR_CCS]);
            tcg_gen_or_tl(cc, z, n);
            tcg_gen_andi_tl(cc, cc, 2);

            tcg_temp_free(n);
            tcg_temp_free(z);
        }
        break;
    case CC_P:
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(cc, cpu_PR[PR_CCS], P_FLAG);
        break;
    case CC_A:
        tcg_gen_movi_tl(cc, 1);
        break;
    default:
        BUG();
        break;
    };
}

static void cris_store_direct_jmp(DisasContext *dc)
{
    /* Store the direct jmp state into the cpu-state.  */
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->jmp == JMP_DIRECT) {
            tcg_gen_movi_tl(env_btaken, 1);
        }
        tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
        dc->jmp = JMP_INDIRECT;
    }
}

static void cris_prepare_cc_branch (DisasContext *dc, 
                    int offset, int cond)
{
    /* This helps us re-schedule the micro-code to insns in delay-slots
       before the actual jump.  */
    dc->delayed_branch = 2;
    dc->jmp = JMP_DIRECT_CC;
    dc->jmp_pc = dc->pc + offset;

    gen_tst_cc(dc, env_btaken, cond);
    tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
}


/* jumps, when the dest is in a live reg for example. Direct should be set
   when the dest addr is constant to allow tb chaining.  */
static inline void cris_prepare_jmp (DisasContext *dc, unsigned int type)
{
    /* This helps us re-schedule the micro-code to insns in delay-slots
       before the actual jump.  */
    dc->delayed_branch = 2;
    dc->jmp = type;
    if (type == JMP_INDIRECT) {
        tcg_gen_movi_tl(env_btaken, 1);
    }
}

static void gen_load64(DisasContext *dc, TCGv_i64 dst, TCGv addr)
{
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);

    /* If we get a fault on a delayslot we must keep the jmp state in
       the cpu-state to be able to re-execute the jmp.  */
    if (dc->delayed_branch == 1) {
        cris_store_direct_jmp(dc);
    }

    tcg_gen_qemu_ld_i64(dst, addr, mem_index, MO_TEUQ);
}

static void gen_load(DisasContext *dc, TCGv dst, TCGv addr, 
             unsigned int size, int sign)
{
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);

    /* If we get a fault on a delayslot we must keep the jmp state in
       the cpu-state to be able to re-execute the jmp.  */
    if (dc->delayed_branch == 1) {
        cris_store_direct_jmp(dc);
    }

    tcg_gen_qemu_ld_tl(dst, addr, mem_index,
                       MO_TE + ctz32(size) + (sign ? MO_SIGN : 0));
}

static void gen_store (DisasContext *dc, TCGv addr, TCGv val,
               unsigned int size)
{
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);

    /* If we get a fault on a delayslot we must keep the jmp state in
       the cpu-state to be able to re-execute the jmp.  */
    if (dc->delayed_branch == 1) {
        cris_store_direct_jmp(dc);
    }


    /* Conditional writes. We only support the kind were X and P are known
       at translation time.  */
    if (dc->flags_x && (dc->tb_flags & P_FLAG)) {
        dc->postinc = 0;
        cris_evaluate_flags(dc);
        tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], C_FLAG);
        return;
    }

    tcg_gen_qemu_st_tl(val, addr, mem_index, MO_TE + ctz32(size));

    if (dc->flags_x) {
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~C_FLAG);
    }
}

static inline void t_gen_sext(TCGv d, TCGv s, int size)
{
    if (size == 1) {
        tcg_gen_ext8s_i32(d, s);
    } else if (size == 2) {
        tcg_gen_ext16s_i32(d, s);
    } else {
        tcg_gen_mov_tl(d, s);
    }
}

static inline void t_gen_zext(TCGv d, TCGv s, int size)
{
    if (size == 1) {
        tcg_gen_ext8u_i32(d, s);
    } else if (size == 2) {
        tcg_gen_ext16u_i32(d, s);
    } else {
        tcg_gen_mov_tl(d, s);
    }
}

#if DISAS_CRIS
static char memsize_char(int size)
{
    switch (size) {
    case 1: return 'b';
    case 2: return 'w';
    case 4: return 'd';
    default:
        return 'x';
    }
}
#endif

static inline unsigned int memsize_z(DisasContext *dc)
{
    return dc->zsize + 1;
}

static inline unsigned int memsize_zz(DisasContext *dc)
{
    switch (dc->zzsize) {
    case 0: return 1;
    case 1: return 2;
    default:
        return 4;
    }
}

static inline void do_postinc (DisasContext *dc, int size)
{
    if (dc->postinc) {
        tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], size);
    }
}

static inline void dec_prep_move_r(DisasContext *dc, int rs, int rd,
                   int size, int s_ext, TCGv dst)
{
    if (s_ext) {
        t_gen_sext(dst, cpu_R[rs], size);
    } else {
        t_gen_zext(dst, cpu_R[rs], size);
    }
}

/* Prepare T0 and T1 for a register alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static void dec_prep_alu_r(DisasContext *dc, int rs, int rd,
              int size, int s_ext, TCGv dst, TCGv src)
{
    dec_prep_move_r(dc, rs, rd, size, s_ext, src);

    if (s_ext) {
        t_gen_sext(dst, cpu_R[rd], size);
    } else {
        t_gen_zext(dst, cpu_R[rd], size);
    }
}

static int dec_prep_move_m(CPUCRISState *env, DisasContext *dc,
                           int s_ext, int memsize, TCGv dst)
{
    unsigned int rs;
    uint32_t imm;
    int is_imm;
    int insn_len = 2;

    rs = dc->op1;
    is_imm = rs == 15 && dc->postinc;

    /* Load [$rs] onto T1.  */
    if (is_imm) {
        insn_len = 2 + memsize;
        if (memsize == 1) {
            insn_len++;
        }

        imm = cris_fetch(env, dc, dc->pc + 2, memsize, s_ext);
        tcg_gen_movi_tl(dst, imm);
        dc->postinc = 0;
    } else {
        cris_flush_cc_state(dc);
        gen_load(dc, dst, cpu_R[rs], memsize, 0);
        if (s_ext) {
            t_gen_sext(dst, dst, memsize);
        } else {
            t_gen_zext(dst, dst, memsize);
        }
    }
    return insn_len;
}

/* Prepare T0 and T1 for a memory + alu operation.
   s_ext decides if the operand1 should be sign-extended or zero-extended when
   needed.  */
static int dec_prep_alu_m(CPUCRISState *env, DisasContext *dc,
                          int s_ext, int memsize, TCGv dst, TCGv src)
{
    int insn_len;

    insn_len = dec_prep_move_m(env, dc, s_ext, memsize, src);
    tcg_gen_mov_tl(dst, cpu_R[dc->op2]);
    return insn_len;
}

#if DISAS_CRIS
static const char *cc_name(int cc)
{
    static const char * const cc_names[16] = {
        "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi",
        "ls", "hi", "ge", "lt", "gt", "le", "a", "p"
    };
    assert(cc < 16);
    return cc_names[cc];
}
#endif

/* Start of insn decoders.  */

static int dec_bccq(CPUCRISState *env, DisasContext *dc)
{
    int32_t offset;
    int sign;
    uint32_t cond = dc->op2;

    offset = EXTRACT_FIELD(dc->ir, 1, 7);
    sign = EXTRACT_FIELD(dc->ir, 0, 0);

    offset *= 2;
    offset |= sign << 8;
    offset = sign_extend(offset, 8);

    LOG_DIS("b%s %x\n", cc_name(cond), dc->pc + offset);

    /* op2 holds the condition-code.  */
    cris_cc_mask(dc, 0);
    cris_prepare_cc_branch(dc, offset, cond);
    return 2;
}
static int dec_addoq(CPUCRISState *env, DisasContext *dc)
{
    int32_t imm;

    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 7);
    imm = sign_extend(dc->op1, 7);

    LOG_DIS("addoq %d, $r%u\n", imm, dc->op2);
    cris_cc_mask(dc, 0);
    /* Fetch register operand,  */
    tcg_gen_addi_tl(cpu_R[R_ACR], cpu_R[dc->op2], imm);

    return 2;
}
static int dec_addq(CPUCRISState *env, DisasContext *dc)
{
    TCGv c;
    LOG_DIS("addq %u, $r%u\n", dc->op1, dc->op2);

    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

    cris_cc_mask(dc, CC_MASK_NZVC);

    c = tcg_const_tl(dc->op1);
    cris_alu(dc, CC_OP_ADD,
            cpu_R[dc->op2], cpu_R[dc->op2], c, 4);
    tcg_temp_free(c);
    return 2;
}
static int dec_moveq(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;

    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
    imm = sign_extend(dc->op1, 5);
    LOG_DIS("moveq %d, $r%u\n", imm, dc->op2);

    tcg_gen_movi_tl(cpu_R[dc->op2], imm);
    return 2;
}
static int dec_subq(CPUCRISState *env, DisasContext *dc)
{
    TCGv c;
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);

    LOG_DIS("subq %u, $r%u\n", dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZVC);
    c = tcg_const_tl(dc->op1);
    cris_alu(dc, CC_OP_SUB,
            cpu_R[dc->op2], cpu_R[dc->op2], c, 4);
    tcg_temp_free(c);
    return 2;
}
static int dec_cmpq(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;
    TCGv c;
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
    imm = sign_extend(dc->op1, 5);

    LOG_DIS("cmpq %d, $r%d\n", imm, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZVC);

    c = tcg_const_tl(imm);
    cris_alu(dc, CC_OP_CMP,
            cpu_R[dc->op2], cpu_R[dc->op2], c, 4);
    tcg_temp_free(c);
    return 2;
}
static int dec_andq(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;
    TCGv c;
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
    imm = sign_extend(dc->op1, 5);

    LOG_DIS("andq %d, $r%d\n", imm, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);

    c = tcg_const_tl(imm);
    cris_alu(dc, CC_OP_AND,
            cpu_R[dc->op2], cpu_R[dc->op2], c, 4);
    tcg_temp_free(c);
    return 2;
}
static int dec_orq(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;
    TCGv c;
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 5);
    imm = sign_extend(dc->op1, 5);
    LOG_DIS("orq %d, $r%d\n", imm, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);

    c = tcg_const_tl(imm);
    cris_alu(dc, CC_OP_OR,
            cpu_R[dc->op2], cpu_R[dc->op2], c, 4);
    tcg_temp_free(c);
    return 2;
}
static int dec_btstq(CPUCRISState *env, DisasContext *dc)
{
    TCGv c;
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
    LOG_DIS("btstq %u, $r%d\n", dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    c = tcg_const_tl(dc->op1);
    cris_evaluate_flags(dc);
    gen_helper_btst(cpu_PR[PR_CCS], cpu_env, cpu_R[dc->op2],
            c, cpu_PR[PR_CCS]);
    tcg_temp_free(c);
    cris_alu(dc, CC_OP_MOVE,
         cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op2], 4);
    cris_update_cc_op(dc, CC_OP_FLAGS, 4);
    dc->flags_uptodate = 1;
    return 2;
}
static int dec_asrq(CPUCRISState *env, DisasContext *dc)
{
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
    LOG_DIS("asrq %u, $r%d\n", dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);

    tcg_gen_sari_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);
    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2],
            cpu_R[dc->op2], cpu_R[dc->op2], 4);
    return 2;
}
static int dec_lslq(CPUCRISState *env, DisasContext *dc)
{
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
    LOG_DIS("lslq %u, $r%d\n", dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);

    tcg_gen_shli_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);

    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2],
            cpu_R[dc->op2], cpu_R[dc->op2], 4);
    return 2;
}
static int dec_lsrq(CPUCRISState *env, DisasContext *dc)
{
    dc->op1 = EXTRACT_FIELD(dc->ir, 0, 4);
    LOG_DIS("lsrq %u, $r%d\n", dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);

    tcg_gen_shri_tl(cpu_R[dc->op2], cpu_R[dc->op2], dc->op1);
    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2],
            cpu_R[dc->op2], cpu_R[dc->op2], 4);
    return 2;
}

static int dec_move_r(CPUCRISState *env, DisasContext *dc)
{
    int size = memsize_zz(dc);

    LOG_DIS("move.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    if (size == 4) {
        dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, cpu_R[dc->op2]);
        cris_cc_mask(dc, CC_MASK_NZ);
        cris_update_cc_op(dc, CC_OP_MOVE, 4);
        cris_update_cc_x(dc);
        cris_update_result(dc, cpu_R[dc->op2]);
    } else {
        TCGv t0;

        t0 = tcg_temp_new();
        dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, t0);
        cris_alu(dc, CC_OP_MOVE,
             cpu_R[dc->op2],
             cpu_R[dc->op2], t0, size);
        tcg_temp_free(t0);
    }
    return 2;
}

static int dec_scc_r(CPUCRISState *env, DisasContext *dc)
{
    int cond = dc->op2;

    LOG_DIS("s%s $r%u\n",
            cc_name(cond), dc->op1);

    gen_tst_cc(dc, cpu_R[dc->op1], cond);
    tcg_gen_setcondi_tl(TCG_COND_NE, cpu_R[dc->op1], cpu_R[dc->op1], 0);

    cris_cc_mask(dc, 0);
    return 2;
}

static inline void cris_alu_alloc_temps(DisasContext *dc, int size, TCGv *t)
{
    if (size == 4) {
        t[0] = cpu_R[dc->op2];
        t[1] = cpu_R[dc->op1];
    } else {
        t[0] = tcg_temp_new();
        t[1] = tcg_temp_new();
    }
}

static int dec_and_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("and.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);

    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
    cris_alu(dc, CC_OP_AND, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_lz_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    LOG_DIS("lz $r%u, $r%u\n",
            dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);
    t0 = tcg_temp_new();
    dec_prep_alu_r(dc, dc->op1, dc->op2, 4, 0, cpu_R[dc->op2], t0);
    cris_alu(dc, CC_OP_LZ, cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

static int dec_lsl_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("lsl.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
    tcg_gen_andi_tl(t[1], t[1], 63);
    cris_alu(dc, CC_OP_LSL, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_lsr_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("lsr.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
    tcg_gen_andi_tl(t[1], t[1], 63);
    cris_alu(dc, CC_OP_LSR, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_asr_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("asr.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1, t[0], t[1]);
    tcg_gen_andi_tl(t[1], t[1], 63);
    cris_alu(dc, CC_OP_ASR, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_muls_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("muls.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZV);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 1, t[0], t[1]);

    cris_alu(dc, CC_OP_MULS, cpu_R[dc->op2], t[0], t[1], 4);
    return 2;
}

static int dec_mulu_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);

    LOG_DIS("mulu.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZV);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

    cris_alu(dc, CC_OP_MULU, cpu_R[dc->op2], t[0], t[1], 4);
    return 2;
}


static int dec_dstep_r(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("dstep $r%u, $r%u\n", dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_DSTEP,
            cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op1], 4);
    return 2;
}

static int dec_xor_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("xor.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    BUG_ON(size != 4); /* xor is dword.  */
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

    cris_alu(dc, CC_OP_XOR, cpu_R[dc->op2], t[0], t[1], 4);
    return 2;
}

static int dec_bound_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv l0;
    int size = memsize_zz(dc);
    LOG_DIS("bound.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);
    l0 = tcg_temp_new();
    dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, l0);
    cris_alu(dc, CC_OP_BOUND, cpu_R[dc->op2], cpu_R[dc->op2], l0, 4);
    tcg_temp_free(l0);
    return 2;
}

static int dec_cmp_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("cmp.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

    cris_alu(dc, CC_OP_CMP, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_abs_r(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("abs $r%u, $r%u\n",
            dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);

    tcg_gen_abs_tl(cpu_R[dc->op2], cpu_R[dc->op1]);
    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op2], 4);
    return 2;
}

static int dec_add_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("add.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

    cris_alu(dc, CC_OP_ADD, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_addc_r(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("addc $r%u, $r%u\n",
            dc->op1, dc->op2);
    cris_evaluate_flags(dc);

    /* Set for this insn.  */
    dc->flags_x = X_FLAG;

    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_ADDC,
         cpu_R[dc->op2], cpu_R[dc->op2], cpu_R[dc->op1], 4);
    return 2;
}

static int dec_mcp_r(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("mcp $p%u, $r%u\n",
             dc->op2, dc->op1);
    cris_evaluate_flags(dc);
    cris_cc_mask(dc, CC_MASK_RNZV);
    cris_alu(dc, CC_OP_MCP,
            cpu_R[dc->op1], cpu_R[dc->op1], cpu_PR[dc->op2], 4);
    return 2;
}

#if DISAS_CRIS
static char * swapmode_name(int mode, char *modename) {
    int i = 0;
    if (mode & 8) {
        modename[i++] = 'n';
    }
    if (mode & 4) {
        modename[i++] = 'w';
    }
    if (mode & 2) {
        modename[i++] = 'b';
    }
    if (mode & 1) {
        modename[i++] = 'r';
    }
    modename[i++] = 0;
    return modename;
}
#endif

static int dec_swap_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
#if DISAS_CRIS
    char modename[4];
#endif
    LOG_DIS("swap%s $r%u\n",
             swapmode_name(dc->op2, modename), dc->op1);

    cris_cc_mask(dc, CC_MASK_NZ);
    t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[dc->op1]);
    if (dc->op2 & 8) {
        tcg_gen_not_tl(t0, t0);
    }
    if (dc->op2 & 4) {
        t_gen_swapw(t0, t0);
    }
    if (dc->op2 & 2) {
        t_gen_swapb(t0, t0);
    }
    if (dc->op2 & 1) {
        t_gen_swapr(t0, t0);
    }
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op1], cpu_R[dc->op1], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

static int dec_or_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("or.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
    cris_alu(dc, CC_OP_OR, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_addi_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    LOG_DIS("addi.%c $r%u, $r%u\n",
            memsize_char(memsize_zz(dc)), dc->op2, dc->op1);
    cris_cc_mask(dc, 0);
    t0 = tcg_temp_new();
    tcg_gen_shli_tl(t0, cpu_R[dc->op2], dc->zzsize);
    tcg_gen_add_tl(cpu_R[dc->op1], cpu_R[dc->op1], t0);
    tcg_temp_free(t0);
    return 2;
}

static int dec_addi_acr(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    LOG_DIS("addi.%c $r%u, $r%u, $acr\n",
          memsize_char(memsize_zz(dc)), dc->op2, dc->op1);
    cris_cc_mask(dc, 0);
    t0 = tcg_temp_new();
    tcg_gen_shli_tl(t0, cpu_R[dc->op2], dc->zzsize);
    tcg_gen_add_tl(cpu_R[R_ACR], cpu_R[dc->op1], t0);
    tcg_temp_free(t0);
    return 2;
}

static int dec_neg_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("neg.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);

    cris_alu(dc, CC_OP_NEG, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

static int dec_btst_r(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("btst $r%u, $r%u\n",
            dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_evaluate_flags(dc);
        gen_helper_btst(cpu_PR[PR_CCS], cpu_env, cpu_R[dc->op2],
            cpu_R[dc->op1], cpu_PR[PR_CCS]);
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2],
         cpu_R[dc->op2], cpu_R[dc->op2], 4);
    cris_update_cc_op(dc, CC_OP_FLAGS, 4);
    dc->flags_uptodate = 1;
    return 2;
}

static int dec_sub_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int size = memsize_zz(dc);
    LOG_DIS("sub.%c $r%u, $r%u\n",
            memsize_char(size), dc->op1, dc->op2);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu_alloc_temps(dc, size, t);
    dec_prep_alu_r(dc, dc->op1, dc->op2, size, 0, t[0], t[1]);
    cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], t[0], t[1], size);
    return 2;
}

/* Zero extension. From size to dword.  */
static int dec_movu_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("movu.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    t0 = tcg_temp_new();
    dec_prep_move_r(dc, dc->op1, dc->op2, size, 0, t0);
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

/* Sign extension. From size to dword.  */
static int dec_movs_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("movs.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZ);
    t0 = tcg_temp_new();
    /* Size can only be qi or hi.  */
    t_gen_sext(t0, cpu_R[dc->op1], size);
    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2], cpu_R[dc->op1], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

/* zero extension. From size to dword.  */
static int dec_addu_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("addu.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t0 = tcg_temp_new();
    /* Size can only be qi or hi.  */
    t_gen_zext(t0, cpu_R[dc->op1], size);
    cris_alu(dc, CC_OP_ADD, cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

/* Sign extension. From size to dword.  */
static int dec_adds_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("adds.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t0 = tcg_temp_new();
    /* Size can only be qi or hi.  */
    t_gen_sext(t0, cpu_R[dc->op1], size);
    cris_alu(dc, CC_OP_ADD,
            cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

/* Zero extension. From size to dword.  */
static int dec_subu_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("subu.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t0 = tcg_temp_new();
    /* Size can only be qi or hi.  */
    t_gen_zext(t0, cpu_R[dc->op1], size);
    cris_alu(dc, CC_OP_SUB,
            cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

/* Sign extension. From size to dword.  */
static int dec_subs_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int size = memsize_z(dc);
    LOG_DIS("subs.%c $r%u, $r%u\n",
            memsize_char(size),
            dc->op1, dc->op2);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t0 = tcg_temp_new();
    /* Size can only be qi or hi.  */
    t_gen_sext(t0, cpu_R[dc->op1], size);
    cris_alu(dc, CC_OP_SUB,
            cpu_R[dc->op2], cpu_R[dc->op2], t0, 4);
    tcg_temp_free(t0);
    return 2;
}

static int dec_setclrf(CPUCRISState *env, DisasContext *dc)
{
    uint32_t flags;
    int set = (~dc->opcode >> 2) & 1;


    flags = (EXTRACT_FIELD(dc->ir, 12, 15) << 4)
        | EXTRACT_FIELD(dc->ir, 0, 3);
    if (set && flags == 0) {
        LOG_DIS("nop\n");
        return 2;
    } else if (!set && (flags & 0x20)) {
        LOG_DIS("di\n");
    } else {
        LOG_DIS("%sf %x\n", set ? "set" : "clr", flags);
    }

    /* User space is not allowed to touch these. Silently ignore.  */
    if (dc->tb_flags & U_FLAG) {
        flags &= ~(S_FLAG | I_FLAG | U_FLAG);
    }

    if (flags & X_FLAG) {
        if (set) {
            dc->flags_x = X_FLAG;
        } else {
            dc->flags_x = 0;
        }
    }

    /* Break the TB if any of the SPI flag changes.  */
    if (flags & (P_FLAG | S_FLAG)) {
        tcg_gen_movi_tl(env_pc, dc->pc + 2);
        dc->base.is_jmp = DISAS_UPDATE;
        dc->cpustate_changed = 1;
    }

    /* For the I flag, only act on posedge.  */
    if ((flags & I_FLAG)) {
        tcg_gen_movi_tl(env_pc, dc->pc + 2);
        dc->base.is_jmp = DISAS_UPDATE;
        dc->cpustate_changed = 1;
    }


    /* Simply decode the flags.  */
    cris_evaluate_flags(dc);
    cris_update_cc_op(dc, CC_OP_FLAGS, 4);
    cris_update_cc_x(dc);
    tcg_gen_movi_tl(cc_op, dc->cc_op);

    if (set) {
        if (!(dc->tb_flags & U_FLAG) && (flags & U_FLAG)) {
            /* Enter user mode.  */
            t_gen_mov_env_TN(ksp, cpu_R[R_SP]);
            tcg_gen_mov_tl(cpu_R[R_SP], cpu_PR[PR_USP]);
            dc->cpustate_changed = 1;
        }
        tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], flags);
    } else {
        tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~flags);
    }

    dc->flags_uptodate = 1;
    dc->clear_x = 0;
    return 2;
}

static int dec_move_rs(CPUCRISState *env, DisasContext *dc)
{
    TCGv c2, c1;
    LOG_DIS("move $r%u, $s%u\n", dc->op1, dc->op2);
    c1 = tcg_const_tl(dc->op1);
    c2 = tcg_const_tl(dc->op2);
    cris_cc_mask(dc, 0);
    gen_helper_movl_sreg_reg(cpu_env, c2, c1);
    tcg_temp_free(c1);
    tcg_temp_free(c2);
    return 2;
}
static int dec_move_sr(CPUCRISState *env, DisasContext *dc)
{
    TCGv c2, c1;
    LOG_DIS("move $s%u, $r%u\n", dc->op2, dc->op1);
    c1 = tcg_const_tl(dc->op1);
    c2 = tcg_const_tl(dc->op2);
    cris_cc_mask(dc, 0);
    gen_helper_movl_reg_sreg(cpu_env, c1, c2);
    tcg_temp_free(c1);
    tcg_temp_free(c2);
    return 2;
}

static int dec_move_rp(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    LOG_DIS("move $r%u, $p%u\n", dc->op1, dc->op2);
    cris_cc_mask(dc, 0);

    t[0] = tcg_temp_new();
    if (dc->op2 == PR_CCS) {
        cris_evaluate_flags(dc);
        tcg_gen_mov_tl(t[0], cpu_R[dc->op1]);
        if (dc->tb_flags & U_FLAG) {
            t[1] = tcg_temp_new();
            /* User space is not allowed to touch all flags.  */
            tcg_gen_andi_tl(t[0], t[0], 0x39f);
            tcg_gen_andi_tl(t[1], cpu_PR[PR_CCS], ~0x39f);
            tcg_gen_or_tl(t[0], t[1], t[0]);
            tcg_temp_free(t[1]);
        }
    } else {
        tcg_gen_mov_tl(t[0], cpu_R[dc->op1]);
    }

    t_gen_mov_preg_TN(dc, dc->op2, t[0]);
    if (dc->op2 == PR_CCS) {
        cris_update_cc_op(dc, CC_OP_FLAGS, 4);
        dc->flags_uptodate = 1;
    }
    tcg_temp_free(t[0]);
    return 2;
}
static int dec_move_pr(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    LOG_DIS("move $p%u, $r%u\n", dc->op2, dc->op1);
    cris_cc_mask(dc, 0);

    if (dc->op2 == PR_CCS) {
        cris_evaluate_flags(dc);
    }

    if (dc->op2 == PR_DZ) {
        tcg_gen_movi_tl(cpu_R[dc->op1], 0);
    } else {
        t0 = tcg_temp_new();
        t_gen_mov_TN_preg(t0, dc->op2);
        cris_alu(dc, CC_OP_MOVE,
                cpu_R[dc->op1], cpu_R[dc->op1], t0,
                preg_sizes[dc->op2]);
        tcg_temp_free(t0);
    }
    return 2;
}

static int dec_move_mr(CPUCRISState *env, DisasContext *dc)
{
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("move.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
                    dc->op2);

    if (memsize == 4) {
        insn_len = dec_prep_move_m(env, dc, 0, 4, cpu_R[dc->op2]);
        cris_cc_mask(dc, CC_MASK_NZ);
        cris_update_cc_op(dc, CC_OP_MOVE, 4);
        cris_update_cc_x(dc);
        cris_update_result(dc, cpu_R[dc->op2]);
    } else {
        TCGv t0;

        t0 = tcg_temp_new();
        insn_len = dec_prep_move_m(env, dc, 0, memsize, t0);
        cris_cc_mask(dc, CC_MASK_NZ);
        cris_alu(dc, CC_OP_MOVE,
                cpu_R[dc->op2], cpu_R[dc->op2], t0, memsize);
        tcg_temp_free(t0);
    }
    do_postinc(dc, memsize);
    return insn_len;
}

static inline void cris_alu_m_alloc_temps(TCGv *t)
{
    t[0] = tcg_temp_new();
    t[1] = tcg_temp_new();
}

static inline void cris_alu_m_free_temps(TCGv *t)
{
    tcg_temp_free(t[0]);
    tcg_temp_free(t[1]);
}

static int dec_movs_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("movs.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
    /* sign extend.  */
        insn_len = dec_prep_alu_m(env, dc, 1, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_MOVE,
            cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_addu_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("addu.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
    /* sign extend.  */
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_ADD,
            cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_adds_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("adds.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
    /* sign extend.  */
        insn_len = dec_prep_alu_m(env, dc, 1, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_ADD, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_subu_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("subu.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
    /* sign extend.  */
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_subs_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("subs.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
    /* sign extend.  */
        insn_len = dec_prep_alu_m(env, dc, 1, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_movu_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;

    LOG_DIS("movu.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_cmpu_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("cmpu.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_CMP, cpu_R[dc->op2], cpu_R[dc->op2], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_cmps_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_z(dc);
    int insn_len;
    LOG_DIS("cmps.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 1, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_CMP,
            cpu_R[dc->op2], cpu_R[dc->op2], t[1],
            memsize_zz(dc));
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_cmp_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("cmp.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_CMP,
            cpu_R[dc->op2], cpu_R[dc->op2], t[1],
            memsize_zz(dc));
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_test_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2], c;
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("test.%c [$r%u%s] op2=%x\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_evaluate_flags(dc);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~3);

    c = tcg_const_tl(0);
    cris_alu(dc, CC_OP_CMP,
         cpu_R[dc->op2], t[1], c, memsize_zz(dc));
    tcg_temp_free(c);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_and_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("and.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_AND, cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_add_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("add.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_ADD,
         cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_addo_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("add.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 1, memsize, t[0], t[1]);
    cris_cc_mask(dc, 0);
    cris_alu(dc, CC_OP_ADD, cpu_R[R_ACR], t[0], t[1], 4);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_bound_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv l[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("bound.%c [$r%u%s, $r%u\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    l[0] = tcg_temp_new();
    l[1] = tcg_temp_new();
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, l[0], l[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_BOUND, cpu_R[dc->op2], l[0], l[1], 4);
    do_postinc(dc, memsize);
    tcg_temp_free(l[0]);
    tcg_temp_free(l[1]);
    return insn_len;
}

static int dec_addc_mr(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int insn_len = 2;
    LOG_DIS("addc [$r%u%s, $r%u\n",
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2);

    cris_evaluate_flags(dc);

    /* Set for this insn.  */
    dc->flags_x = X_FLAG;

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, 4, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_ADDC, cpu_R[dc->op2], t[0], t[1], 4);
    do_postinc(dc, 4);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_sub_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("sub.%c [$r%u%s, $r%u ir=%x zz=%x\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2, dc->ir, dc->zzsize);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZVC);
    cris_alu(dc, CC_OP_SUB, cpu_R[dc->op2], t[0], t[1], memsize);
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_or_m(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len;
    LOG_DIS("or.%c [$r%u%s, $r%u pc=%x\n",
            memsize_char(memsize),
            dc->op1, dc->postinc ? "+]" : "]",
            dc->op2, dc->pc);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, CC_MASK_NZ);
    cris_alu(dc, CC_OP_OR,
            cpu_R[dc->op2], t[0], t[1], memsize_zz(dc));
    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_move_mp(CPUCRISState *env, DisasContext *dc)
{
    TCGv t[2];
    int memsize = memsize_zz(dc);
    int insn_len = 2;

    LOG_DIS("move.%c [$r%u%s, $p%u\n",
            memsize_char(memsize),
            dc->op1,
            dc->postinc ? "+]" : "]",
            dc->op2);

    cris_alu_m_alloc_temps(t);
        insn_len = dec_prep_alu_m(env, dc, 0, memsize, t[0], t[1]);
    cris_cc_mask(dc, 0);
    if (dc->op2 == PR_CCS) {
        cris_evaluate_flags(dc);
        if (dc->tb_flags & U_FLAG) {
            /* User space is not allowed to touch all flags.  */
            tcg_gen_andi_tl(t[1], t[1], 0x39f);
            tcg_gen_andi_tl(t[0], cpu_PR[PR_CCS], ~0x39f);
            tcg_gen_or_tl(t[1], t[0], t[1]);
        }
    }

    t_gen_mov_preg_TN(dc, dc->op2, t[1]);

    do_postinc(dc, memsize);
    cris_alu_m_free_temps(t);
    return insn_len;
}

static int dec_move_pm(CPUCRISState *env, DisasContext *dc)
{
    TCGv t0;
    int memsize;

    memsize = preg_sizes[dc->op2];

    LOG_DIS("move.%c $p%u, [$r%u%s\n",
            memsize_char(memsize),
            dc->op2, dc->op1, dc->postinc ? "+]" : "]");

    /* prepare store. Address in T0, value in T1.  */
    if (dc->op2 == PR_CCS) {
        cris_evaluate_flags(dc);
    }
    t0 = tcg_temp_new();
    t_gen_mov_TN_preg(t0, dc->op2);
    cris_flush_cc_state(dc);
    gen_store(dc, cpu_R[dc->op1], t0, memsize);
    tcg_temp_free(t0);

    cris_cc_mask(dc, 0);
    if (dc->postinc) {
        tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], memsize);
    }
    return 2;
}

static int dec_movem_mr(CPUCRISState *env, DisasContext *dc)
{
    TCGv_i64 tmp[16];
    TCGv tmp32;
    TCGv addr;
    int i;
    int nr = dc->op2 + 1;

    LOG_DIS("movem [$r%u%s, $r%u\n", dc->op1,
            dc->postinc ? "+]" : "]", dc->op2);

    addr = tcg_temp_new();
    /* There are probably better ways of doing this.  */
    cris_flush_cc_state(dc);
    for (i = 0; i < (nr >> 1); i++) {
        tmp[i] = tcg_temp_new_i64();
        tcg_gen_addi_tl(addr, cpu_R[dc->op1], i * 8);
        gen_load64(dc, tmp[i], addr);
    }
    if (nr & 1) {
        tmp32 = tcg_temp_new_i32();
        tcg_gen_addi_tl(addr, cpu_R[dc->op1], i * 8);
        gen_load(dc, tmp32, addr, 4, 0);
    } else {
        tmp32 = NULL;
    }
    tcg_temp_free(addr);

    for (i = 0; i < (nr >> 1); i++) {
        tcg_gen_extrl_i64_i32(cpu_R[i * 2], tmp[i]);
        tcg_gen_shri_i64(tmp[i], tmp[i], 32);
        tcg_gen_extrl_i64_i32(cpu_R[i * 2 + 1], tmp[i]);
        tcg_temp_free_i64(tmp[i]);
    }
    if (nr & 1) {
        tcg_gen_mov_tl(cpu_R[dc->op2], tmp32);
        tcg_temp_free(tmp32);
    }

    /* writeback the updated pointer value.  */
    if (dc->postinc) {
        tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], nr * 4);
    }

    /* gen_load might want to evaluate the previous insns flags.  */
    cris_cc_mask(dc, 0);
    return 2;
}

static int dec_movem_rm(CPUCRISState *env, DisasContext *dc)
{
    TCGv tmp;
    TCGv addr;
    int i;

    LOG_DIS("movem $r%u, [$r%u%s\n", dc->op2, dc->op1,
            dc->postinc ? "+]" : "]");

    cris_flush_cc_state(dc);

    tmp = tcg_temp_new();
    addr = tcg_temp_new();
    tcg_gen_movi_tl(tmp, 4);
    tcg_gen_mov_tl(addr, cpu_R[dc->op1]);
    for (i = 0; i <= dc->op2; i++) {
        /* Displace addr.  */
        /* Perform the store.  */
        gen_store(dc, addr, cpu_R[i], 4);
        tcg_gen_add_tl(addr, addr, tmp);
    }
    if (dc->postinc) {
        tcg_gen_mov_tl(cpu_R[dc->op1], addr);
    }
    cris_cc_mask(dc, 0);
    tcg_temp_free(tmp);
    tcg_temp_free(addr);
    return 2;
}

static int dec_move_rm(CPUCRISState *env, DisasContext *dc)
{
    int memsize;

    memsize = memsize_zz(dc);

    LOG_DIS("move.%c $r%u, [$r%u]\n",
            memsize_char(memsize), dc->op2, dc->op1);

    /* prepare store.  */
    cris_flush_cc_state(dc);
    gen_store(dc, cpu_R[dc->op1], cpu_R[dc->op2], memsize);

    if (dc->postinc) {
        tcg_gen_addi_tl(cpu_R[dc->op1], cpu_R[dc->op1], memsize);
    }
    cris_cc_mask(dc, 0);
    return 2;
}

static int dec_lapcq(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("lapcq %x, $r%u\n",
            dc->pc + dc->op1*2, dc->op2);
    cris_cc_mask(dc, 0);
    tcg_gen_movi_tl(cpu_R[dc->op2], dc->pc + dc->op1 * 2);
    return 2;
}

static int dec_lapc_im(CPUCRISState *env, DisasContext *dc)
{
    unsigned int rd;
    int32_t imm;
    int32_t pc;

    rd = dc->op2;

    cris_cc_mask(dc, 0);
    imm = cris_fetch(env, dc, dc->pc + 2, 4, 0);
    LOG_DIS("lapc 0x%x, $r%u\n", imm + dc->pc, dc->op2);

    pc = dc->pc;
    pc += imm;
    tcg_gen_movi_tl(cpu_R[rd], pc);
    return 6;
}

/* Jump to special reg.  */
static int dec_jump_p(CPUCRISState *env, DisasContext *dc)
{
    LOG_DIS("jump $p%u\n", dc->op2);

    if (dc->op2 == PR_CCS) {
        cris_evaluate_flags(dc);
    }
    t_gen_mov_TN_preg(env_btarget, dc->op2);
    /* rete will often have low bit set to indicate delayslot.  */
    tcg_gen_andi_tl(env_btarget, env_btarget, ~1);
    cris_cc_mask(dc, 0);
    cris_prepare_jmp(dc, JMP_INDIRECT);
    return 2;
}

/* Jump and save.  */
static int dec_jas_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv c;
    LOG_DIS("jas $r%u, $p%u\n", dc->op1, dc->op2);
    cris_cc_mask(dc, 0);
    /* Store the return address in Pd.  */
    tcg_gen_mov_tl(env_btarget, cpu_R[dc->op1]);
    if (dc->op2 > 15) {
        abort();
    }
    c = tcg_const_tl(dc->pc + 4);
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);

    cris_prepare_jmp(dc, JMP_INDIRECT);
    return 2;
}

static int dec_jas_im(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;
    TCGv c;

    imm = cris_fetch(env, dc, dc->pc + 2, 4, 0);

    LOG_DIS("jas 0x%x\n", imm);
    cris_cc_mask(dc, 0);
    c = tcg_const_tl(dc->pc + 8);
    /* Store the return address in Pd.  */
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);

    dc->jmp_pc = imm;
    cris_prepare_jmp(dc, JMP_DIRECT);
    return 6;
}

static int dec_jasc_im(CPUCRISState *env, DisasContext *dc)
{
    uint32_t imm;
    TCGv c;

    imm = cris_fetch(env, dc, dc->pc + 2, 4, 0);

    LOG_DIS("jasc 0x%x\n", imm);
    cris_cc_mask(dc, 0);
    c = tcg_const_tl(dc->pc + 8 + 4);
    /* Store the return address in Pd.  */
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);

    dc->jmp_pc = imm;
    cris_prepare_jmp(dc, JMP_DIRECT);
    return 6;
}

static int dec_jasc_r(CPUCRISState *env, DisasContext *dc)
{
    TCGv c;
    LOG_DIS("jasc_r $r%u, $p%u\n", dc->op1, dc->op2);
    cris_cc_mask(dc, 0);
    /* Store the return address in Pd.  */
    tcg_gen_mov_tl(env_btarget, cpu_R[dc->op1]);
    c = tcg_const_tl(dc->pc + 4 + 4);
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);
    cris_prepare_jmp(dc, JMP_INDIRECT);
    return 2;
}

static int dec_bcc_im(CPUCRISState *env, DisasContext *dc)
{
    int32_t offset;
    uint32_t cond = dc->op2;

    offset = cris_fetch(env, dc, dc->pc + 2, 2, 1);

    LOG_DIS("b%s %d pc=%x dst=%x\n",
            cc_name(cond), offset,
            dc->pc, dc->pc + offset);

    cris_cc_mask(dc, 0);
    /* op2 holds the condition-code.  */
    cris_prepare_cc_branch(dc, offset, cond);
    return 4;
}

static int dec_bas_im(CPUCRISState *env, DisasContext *dc)
{
    int32_t simm;
    TCGv c;

    simm = cris_fetch(env, dc, dc->pc + 2, 4, 0);

    LOG_DIS("bas 0x%x, $p%u\n", dc->pc + simm, dc->op2);
    cris_cc_mask(dc, 0);
    c = tcg_const_tl(dc->pc + 8);
    /* Store the return address in Pd.  */
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);

    dc->jmp_pc = dc->pc + simm;
    cris_prepare_jmp(dc, JMP_DIRECT);
    return 6;
}

static int dec_basc_im(CPUCRISState *env, DisasContext *dc)
{
    int32_t simm;
    TCGv c;
    simm = cris_fetch(env, dc, dc->pc + 2, 4, 0);

    LOG_DIS("basc 0x%x, $p%u\n", dc->pc + simm, dc->op2);
    cris_cc_mask(dc, 0);
    c = tcg_const_tl(dc->pc + 12);
    /* Store the return address in Pd.  */
    t_gen_mov_preg_TN(dc, dc->op2, c);
    tcg_temp_free(c);

    dc->jmp_pc = dc->pc + simm;
    cris_prepare_jmp(dc, JMP_DIRECT);
    return 6;
}

static int dec_rfe_etc(CPUCRISState *env, DisasContext *dc)
{
    cris_cc_mask(dc, 0);

    if (dc->op2 == 15) {
        tcg_gen_st_i32(tcg_const_i32(1), cpu_env,
                       -offsetof(CRISCPU, env) + offsetof(CPUState, halted));
        tcg_gen_movi_tl(env_pc, dc->pc + 2);
        t_gen_raise_exception(EXCP_HLT);
        dc->base.is_jmp = DISAS_NORETURN;
        return 2;
    }

    switch (dc->op2 & 7) {
    case 2:
        /* rfe.  */
        LOG_DIS("rfe\n");
        cris_evaluate_flags(dc);
        gen_helper_rfe(cpu_env);
        dc->base.is_jmp = DISAS_UPDATE;
        dc->cpustate_changed = true;
        break;
    case 5:
        /* rfn.  */
        LOG_DIS("rfn\n");
        cris_evaluate_flags(dc);
        gen_helper_rfn(cpu_env);
        dc->base.is_jmp = DISAS_UPDATE;
        dc->cpustate_changed = true;
        break;
    case 6:
        LOG_DIS("break %d\n", dc->op1);
        cris_evaluate_flags(dc);
        /* break.  */
        tcg_gen_movi_tl(env_pc, dc->pc + 2);

        /* Breaks start at 16 in the exception vector.  */
        t_gen_movi_env_TN(trap_vector, dc->op1 + 16);
        t_gen_raise_exception(EXCP_BREAK);
        dc->base.is_jmp = DISAS_NORETURN;
        break;
    default:
        printf("op2=%x\n", dc->op2);
        BUG();
        break;

    }
    return 2;
}

static int dec_ftag_fidx_d_m(CPUCRISState *env, DisasContext *dc)
{
    return 2;
}

static int dec_ftag_fidx_i_m(CPUCRISState *env, DisasContext *dc)
{
    return 2;
}

static int dec_null(CPUCRISState *env, DisasContext *dc)
{
    printf("unknown insn pc=%x opc=%x op1=%x op2=%x\n",
        dc->pc, dc->opcode, dc->op1, dc->op2);
    fflush(NULL);
    BUG();
    return 2;
}

static const struct decoder_info {
    struct {
        uint32_t bits;
        uint32_t mask;
    };
    int (*dec)(CPUCRISState *env, DisasContext *dc);
} decinfo[] = {
    /* Order matters here.  */
    {DEC_MOVEQ, dec_moveq},
    {DEC_BTSTQ, dec_btstq},
    {DEC_CMPQ, dec_cmpq},
    {DEC_ADDOQ, dec_addoq},
    {DEC_ADDQ, dec_addq},
    {DEC_SUBQ, dec_subq},
    {DEC_ANDQ, dec_andq},
    {DEC_ORQ, dec_orq},
    {DEC_ASRQ, dec_asrq},
    {DEC_LSLQ, dec_lslq},
    {DEC_LSRQ, dec_lsrq},
    {DEC_BCCQ, dec_bccq},

    {DEC_BCC_IM, dec_bcc_im},
    {DEC_JAS_IM, dec_jas_im},
    {DEC_JAS_R, dec_jas_r},
    {DEC_JASC_IM, dec_jasc_im},
    {DEC_JASC_R, dec_jasc_r},
    {DEC_BAS_IM, dec_bas_im},
    {DEC_BASC_IM, dec_basc_im},
    {DEC_JUMP_P, dec_jump_p},
    {DEC_LAPC_IM, dec_lapc_im},
    {DEC_LAPCQ, dec_lapcq},

    {DEC_RFE_ETC, dec_rfe_etc},
    {DEC_ADDC_MR, dec_addc_mr},

    {DEC_MOVE_MP, dec_move_mp},
    {DEC_MOVE_PM, dec_move_pm},
    {DEC_MOVEM_MR, dec_movem_mr},
    {DEC_MOVEM_RM, dec_movem_rm},
    {DEC_MOVE_PR, dec_move_pr},
    {DEC_SCC_R, dec_scc_r},
    {DEC_SETF, dec_setclrf},
    {DEC_CLEARF, dec_setclrf},

    {DEC_MOVE_SR, dec_move_sr},
    {DEC_MOVE_RP, dec_move_rp},
    {DEC_SWAP_R, dec_swap_r},
    {DEC_ABS_R, dec_abs_r},
    {DEC_LZ_R, dec_lz_r},
    {DEC_MOVE_RS, dec_move_rs},
    {DEC_BTST_R, dec_btst_r},
    {DEC_ADDC_R, dec_addc_r},

    {DEC_DSTEP_R, dec_dstep_r},
    {DEC_XOR_R, dec_xor_r},
    {DEC_MCP_R, dec_mcp_r},
    {DEC_CMP_R, dec_cmp_r},

    {DEC_ADDI_R, dec_addi_r},
    {DEC_ADDI_ACR, dec_addi_acr},

    {DEC_ADD_R, dec_add_r},
    {DEC_SUB_R, dec_sub_r},

    {DEC_ADDU_R, dec_addu_r},
    {DEC_ADDS_R, dec_adds_r},
    {DEC_SUBU_R, dec_subu_r},
    {DEC_SUBS_R, dec_subs_r},
    {DEC_LSL_R, dec_lsl_r},

    {DEC_AND_R, dec_and_r},
    {DEC_OR_R, dec_or_r},
    {DEC_BOUND_R, dec_bound_r},
    {DEC_ASR_R, dec_asr_r},
    {DEC_LSR_R, dec_lsr_r},

    {DEC_MOVU_R, dec_movu_r},
    {DEC_MOVS_R, dec_movs_r},
    {DEC_NEG_R, dec_neg_r},
    {DEC_MOVE_R, dec_move_r},

    {DEC_FTAG_FIDX_I_M, dec_ftag_fidx_i_m},
    {DEC_FTAG_FIDX_D_M, dec_ftag_fidx_d_m},

    {DEC_MULS_R, dec_muls_r},
    {DEC_MULU_R, dec_mulu_r},

    {DEC_ADDU_M, dec_addu_m},
    {DEC_ADDS_M, dec_adds_m},
    {DEC_SUBU_M, dec_subu_m},
    {DEC_SUBS_M, dec_subs_m},

    {DEC_CMPU_M, dec_cmpu_m},
    {DEC_CMPS_M, dec_cmps_m},
    {DEC_MOVU_M, dec_movu_m},
    {DEC_MOVS_M, dec_movs_m},

    {DEC_CMP_M, dec_cmp_m},
    {DEC_ADDO_M, dec_addo_m},
    {DEC_BOUND_M, dec_bound_m},
    {DEC_ADD_M, dec_add_m},
    {DEC_SUB_M, dec_sub_m},
    {DEC_AND_M, dec_and_m},
    {DEC_OR_M, dec_or_m},
    {DEC_MOVE_RM, dec_move_rm},
    {DEC_TEST_M, dec_test_m},
    {DEC_MOVE_MR, dec_move_mr},

    {{0, 0}, dec_null}
};

static unsigned int crisv32_decoder(CPUCRISState *env, DisasContext *dc)
{
    int insn_len = 2;
    int i;

    /* Load a halfword onto the instruction register.  */
        dc->ir = cris_fetch(env, dc, dc->pc, 2, 0);

    /* Now decode it.  */
    dc->opcode   = EXTRACT_FIELD(dc->ir, 4, 11);
    dc->op1      = EXTRACT_FIELD(dc->ir, 0, 3);
    dc->op2      = EXTRACT_FIELD(dc->ir, 12, 15);
    dc->zsize    = EXTRACT_FIELD(dc->ir, 4, 4);
    dc->zzsize   = EXTRACT_FIELD(dc->ir, 4, 5);
    dc->postinc  = EXTRACT_FIELD(dc->ir, 10, 10);

    /* Large switch for all insns.  */
    for (i = 0; i < ARRAY_SIZE(decinfo); i++) {
        if ((dc->opcode & decinfo[i].mask) == decinfo[i].bits) {
            insn_len = decinfo[i].dec(env, dc);
            break;
        }
    }

#if !defined(CONFIG_USER_ONLY)
    /* Single-stepping ?  */
    if (dc->tb_flags & S_FLAG) {
        TCGLabel *l1 = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_NE, cpu_PR[PR_SPC], dc->pc, l1);
        /* We treat SPC as a break with an odd trap vector.  */
        cris_evaluate_flags(dc);
        t_gen_movi_env_TN(trap_vector, 3);
        tcg_gen_movi_tl(env_pc, dc->pc + insn_len);
        tcg_gen_movi_tl(cpu_PR[PR_SPC], dc->pc + insn_len);
        t_gen_raise_exception(EXCP_BREAK);
        gen_set_label(l1);
    }
#endif
    return insn_len;
}

#include "translate_v10.c.inc"

/*
 * Delay slots on QEMU/CRIS.
 *
 * If an exception hits on a delayslot, the core will let ERP (the Exception
 * Return Pointer) point to the branch (the previous) insn and set the lsb to
 * to give SW a hint that the exception actually hit on the dslot.
 *
 * CRIS expects all PC addresses to be 16-bit aligned. The lsb is ignored by
 * the core and any jmp to an odd addresses will mask off that lsb. It is 
 * simply there to let sw know there was an exception on a dslot.
 *
 * When the software returns from an exception, the branch will re-execute.
 * On QEMU care needs to be taken when a branch+delayslot sequence is broken
 * and the branch and delayslot don't share pages.
 *
 * The TB contaning the branch insn will set up env->btarget and evaluate 
 * env->btaken. When the translation loop exits we will note that the branch 
 * sequence is broken and let env->dslot be the size of the branch insn (those
 * vary in length).
 *
 * The TB contaning the delayslot will have the PC of its real insn (i.e no lsb
 * set). It will also expect to have env->dslot setup with the size of the 
 * delay slot so that env->pc - env->dslot point to the branch insn. This TB 
 * will execute the dslot and take the branch, either to btarget or just one 
 * insn ahead.
 *
 * When exceptions occur, we check for env->dslot in do_interrupt to detect 
 * broken branch sequences and setup $erp accordingly (i.e let it point to the
 * branch and set lsb). Then env->dslot gets cleared so that the exception 
 * handler can enter. When returning from exceptions (jump $erp) the lsb gets
 * masked off and we will reexecute the branch insn.
 *
 */

static void cris_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUCRISState *env = cs->env_ptr;
    uint32_t tb_flags = dc->base.tb->flags;
    uint32_t pc_start;

    if (env->pregs[PR_VR] == 32) {
        dc->decoder = crisv32_decoder;
        dc->clear_locked_irq = 0;
    } else {
        dc->decoder = crisv10_decoder;
        dc->clear_locked_irq = 1;
    }

    /*
     * Odd PC indicates that branch is rexecuting due to exception in the
     * delayslot, like in real hw.
     */
    pc_start = dc->base.pc_first & ~1;
    dc->base.pc_first = pc_start;
    dc->base.pc_next = pc_start;

    dc->cpu = env_archcpu(env);
    dc->ppc = pc_start;
    dc->pc = pc_start;
    dc->flags_uptodate = 1;
    dc->flags_x = tb_flags & X_FLAG;
    dc->cc_x_uptodate = 0;
    dc->cc_mask = 0;
    dc->update_cc = 0;
    dc->clear_prefix = 0;
    dc->cpustate_changed = 0;

    cris_update_cc_op(dc, CC_OP_FLAGS, 4);
    dc->cc_size_uptodate = -1;

    /* Decode TB flags.  */
    dc->tb_flags = tb_flags & (S_FLAG | P_FLAG | U_FLAG | X_FLAG | PFIX_FLAG);
    dc->delayed_branch = !!(tb_flags & 7);
    if (dc->delayed_branch) {
        dc->jmp = JMP_INDIRECT;
    } else {
        dc->jmp = JMP_NOJMP;
    }
}

static void cris_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void cris_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->delayed_branch == 1 ? dc->ppc | 1 : dc->pc);
}

static void cris_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUCRISState *env = cs->env_ptr;
    unsigned int insn_len;

    /* Pretty disas.  */
    LOG_DIS("%8.8x:\t", dc->pc);

    dc->clear_x = 1;

    insn_len = dc->decoder(env, dc);
    dc->ppc = dc->pc;
    dc->pc += insn_len;
    dc->base.pc_next += insn_len;

    if (dc->base.is_jmp == DISAS_NORETURN) {
        return;
    }

    if (dc->clear_x) {
        cris_clear_x_flag(dc);
    }

    /*
     * All branches are delayed branches, handled immediately below.
     * We don't expect to see odd combinations of exit conditions.
     */
    assert(dc->base.is_jmp == DISAS_NEXT || dc->cpustate_changed);

    if (dc->delayed_branch && --dc->delayed_branch == 0) {
        dc->base.is_jmp = DISAS_DBRANCH;
        return;
    }

    if (dc->base.is_jmp != DISAS_NEXT) {
        return;
    }

    /* Force an update if the per-tb cpu state has changed.  */
    if (dc->cpustate_changed) {
        dc->base.is_jmp = DISAS_UPDATE_NEXT;
        return;
    }

    /*
     * FIXME: Only the first insn in the TB should cross a page boundary.
     * If we can detect the length of the next insn easily, we should.
     * In the meantime, simply stop when we do cross.
     */
    if ((dc->pc ^ dc->base.pc_first) & TARGET_PAGE_MASK) {
        dc->base.is_jmp = DISAS_TOO_MANY;
    }
}

static void cris_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    DisasJumpType is_jmp = dc->base.is_jmp;
    target_ulong npc = dc->pc;

    if (is_jmp == DISAS_NORETURN) {
        /* If we have a broken branch+delayslot sequence, it's too late. */
        assert(dc->delayed_branch != 1);
        return;
    }

    if (dc->clear_locked_irq) {
        t_gen_movi_env_TN(locked_irq, 0);
    }

    /* Broken branch+delayslot sequence.  */
    if (dc->delayed_branch == 1) {
        /* Set env->dslot to the size of the branch insn.  */
        t_gen_movi_env_TN(dslot, dc->pc - dc->ppc);
        cris_store_direct_jmp(dc);
    }

    cris_evaluate_flags(dc);

    /* Evaluate delayed branch destination and fold to another is_jmp case. */
    if (is_jmp == DISAS_DBRANCH) {
        if (dc->base.tb->flags & 7) {
            t_gen_movi_env_TN(dslot, 0);
        }

        switch (dc->jmp) {
        case JMP_DIRECT:
            npc = dc->jmp_pc;
            is_jmp = dc->cpustate_changed ? DISAS_UPDATE_NEXT : DISAS_TOO_MANY;
            break;

        case JMP_DIRECT_CC:
            /*
             * Use a conditional branch if either taken or not-taken path
             * can use goto_tb.  If neither can, then treat it as indirect.
             */
            if (likely(!dc->cpustate_changed)
                && (use_goto_tb(dc, dc->jmp_pc) || use_goto_tb(dc, npc))) {
                TCGLabel *not_taken = gen_new_label();

                tcg_gen_brcondi_tl(TCG_COND_EQ, env_btaken, 0, not_taken);
                gen_goto_tb(dc, 1, dc->jmp_pc);
                gen_set_label(not_taken);

                /* not-taken case handled below. */
                is_jmp = DISAS_TOO_MANY;
                break;
            }
            tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
            /* fall through */

        case JMP_INDIRECT:
            tcg_gen_movcond_tl(TCG_COND_NE, env_pc,
                               env_btaken, tcg_constant_tl(0),
                               env_btarget, tcg_constant_tl(npc));
            is_jmp = dc->cpustate_changed ? DISAS_UPDATE : DISAS_JUMP;

            /*
             * We have now consumed btaken and btarget.  Hint to the
             * tcg compiler that the writeback to env may be dropped.
             */
            tcg_gen_discard_tl(env_btaken);
            tcg_gen_discard_tl(env_btarget);
            break;

        default:
            g_assert_not_reached();
        }
    }

    switch (is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(dc, 0, npc);
        break;
    case DISAS_UPDATE_NEXT:
        tcg_gen_movi_tl(env_pc, npc);
        /* fall through */
    case DISAS_JUMP:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_UPDATE:
        /* Indicate that interupts must be re-evaluated before the next TB. */
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void cris_tr_disas_log(const DisasContextBase *dcbase,
                              CPUState *cpu, FILE *logfile)
{
    if (!DISAS_CRIS) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
        target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
    }
}

static const TranslatorOps cris_tr_ops = {
    .init_disas_context = cris_tr_init_disas_context,
    .tb_start           = cris_tr_tb_start,
    .insn_start         = cris_tr_insn_start,
    .translate_insn     = cris_tr_translate_insn,
    .tb_stop            = cris_tr_tb_stop,
    .disas_log          = cris_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc;
    translator_loop(cs, tb, max_insns, pc, host_pc, &cris_tr_ops, &dc.base);
}

void cris_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CRISCPU *cpu = CRIS_CPU(cs);
    CPUCRISState *env = &cpu->env;
    const char * const *regnames;
    const char * const *pregnames;
    int i;

    if (!env) {
        return;
    }
    if (env->pregs[PR_VR] < 32) {
        pregnames = pregnames_v10;
        regnames = regnames_v10;
    } else {
        pregnames = pregnames_v32;
        regnames = regnames_v32;
    }

    qemu_fprintf(f, "PC=%x CCS=%x btaken=%d btarget=%x\n"
                 "cc_op=%d cc_src=%d cc_dest=%d cc_result=%x cc_mask=%x\n",
                 env->pc, env->pregs[PR_CCS], env->btaken, env->btarget,
                 env->cc_op,
                 env->cc_src, env->cc_dest, env->cc_result, env->cc_mask);


    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "%s=%8.8x ", regnames[i], env->regs[i]);
        if ((i + 1) % 4 == 0) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "\nspecial regs:\n");
    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "%s=%8.8x ", pregnames[i], env->pregs[i]);
        if ((i + 1) % 4 == 0) {
            qemu_fprintf(f, "\n");
        }
    }
    if (env->pregs[PR_VR] >= 32) {
        uint32_t srs = env->pregs[PR_SRS];
        qemu_fprintf(f, "\nsupport function regs bank %x:\n", srs);
        if (srs < ARRAY_SIZE(env->sregs)) {
            for (i = 0; i < 16; i++) {
                qemu_fprintf(f, "s%2.2d=%8.8x ",
                             i, env->sregs[srs][i]);
                if ((i + 1) % 4 == 0) {
                    qemu_fprintf(f, "\n");
                }
            }
        }
    }
    qemu_fprintf(f, "\n\n");

}

void cris_initialize_tcg(void)
{
    int i;

    cc_x = tcg_global_mem_new(cpu_env,
                              offsetof(CPUCRISState, cc_x), "cc_x");
    cc_src = tcg_global_mem_new(cpu_env,
                                offsetof(CPUCRISState, cc_src), "cc_src");
    cc_dest = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_dest),
                                 "cc_dest");
    cc_result = tcg_global_mem_new(cpu_env,
                                   offsetof(CPUCRISState, cc_result),
                                   "cc_result");
    cc_op = tcg_global_mem_new(cpu_env,
                               offsetof(CPUCRISState, cc_op), "cc_op");
    cc_size = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_size),
                                 "cc_size");
    cc_mask = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_mask),
                                 "cc_mask");

    env_pc = tcg_global_mem_new(cpu_env,
                                offsetof(CPUCRISState, pc),
                                "pc");
    env_btarget = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUCRISState, btarget),
                                     "btarget");
    env_btaken = tcg_global_mem_new(cpu_env,
                                    offsetof(CPUCRISState, btaken),
                                    "btaken");
    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new(cpu_env,
                                      offsetof(CPUCRISState, regs[i]),
                                      regnames_v32[i]);
    }
    for (i = 0; i < 16; i++) {
        cpu_PR[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUCRISState, pregs[i]),
                                       pregnames_v32[i]);
    }
}
