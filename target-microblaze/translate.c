/*
 *  Xilinx MicroBlaze emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias.
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

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "cpu.h"
#include "disas.h"
#include "tcg-op.h"
#include "helper.h"
#include "microblaze-decode.h"
#include "qemu-common.h"

#define GEN_HELPER 1
#include "helper.h"

#define SIM_COMPAT 0
#define DISAS_GNU 1
#define DISAS_MB 1
#if DISAS_MB && !SIM_COMPAT
#  define LOG_DIS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DIS(...) do { } while (0)
#endif

#define D(x)

#define EXTRACT_FIELD(src, start, end) \
            (((src) >> start) & ((1 << (end - start + 1)) - 1))

static TCGv env_debug;
static TCGv_ptr cpu_env;
static TCGv cpu_R[32];
static TCGv cpu_SR[18];
static TCGv env_imm;
static TCGv env_btaken;
static TCGv env_btarget;
static TCGv env_iflags;

#include "gen-icount.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
    CPUState *env;
    target_ulong pc;

    /* Decoder.  */
    int type_b;
    uint32_t ir;
    uint8_t opcode;
    uint8_t rd, ra, rb;
    uint16_t imm;

    unsigned int cpustate_changed;
    unsigned int delayed_branch;
    unsigned int tb_flags, synced_flags; /* tb dependent flags.  */
    unsigned int clear_imm;
    int is_jmp;

#define JMP_NOJMP     0
#define JMP_DIRECT    1
#define JMP_DIRECT_CC 2
#define JMP_INDIRECT  3
    unsigned int jmp;
    uint32_t jmp_pc;

    int abort_at_next_insn;
    int nr_nops;
    struct TranslationBlock *tb;
    int singlestep_enabled;
} DisasContext;

static const char *regnames[] =
{
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
};

static const char *special_regnames[] =
{
    "rpc", "rmsr", "sr2", "sr3", "sr4", "sr5", "sr6", "sr7",
    "sr8", "sr9", "sr10", "sr11", "sr12", "sr13", "sr14", "sr15",
    "sr16", "sr17", "sr18"
};

/* Sign extend at translation time.  */
static inline int sign_extend(unsigned int val, unsigned int width)
{
        int sval;

        /* LSL.  */
        val <<= 31 - width;
        sval = val;
        /* ASR.  */
        sval >>= 31 - width;
        return sval;
}

static inline void t_sync_flags(DisasContext *dc)
{
    /* Synch the tb dependant flags between translator and runtime.  */
    if (dc->tb_flags != dc->synced_flags) {
        tcg_gen_movi_tl(env_iflags, dc->tb_flags);
        dc->synced_flags = dc->tb_flags;
    }
}

static inline void t_gen_raise_exception(DisasContext *dc, uint32_t index)
{
    TCGv_i32 tmp = tcg_const_i32(index);

    t_sync_flags(dc);
    tcg_gen_movi_tl(cpu_SR[SR_PC], dc->pc);
    gen_helper_raise_exception(tmp);
    tcg_temp_free_i32(tmp);
    dc->is_jmp = DISAS_UPDATE;
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = dc->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_SR[SR_PC], dest);
        tcg_gen_exit_tb((tcg_target_long)tb + n);
    } else {
        tcg_gen_movi_tl(cpu_SR[SR_PC], dest);
        tcg_gen_exit_tb(0);
    }
}

static void read_carry(DisasContext *dc, TCGv d)
{
    tcg_gen_shri_tl(d, cpu_SR[SR_MSR], 31);
}

static void write_carry(DisasContext *dc, TCGv v)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_shli_tl(t0, v, 31);
    tcg_gen_sari_tl(t0, t0, 31);
    tcg_gen_andi_tl(t0, t0, (MSR_C | MSR_CC));
    tcg_gen_andi_tl(cpu_SR[SR_MSR], cpu_SR[SR_MSR],
                    ~(MSR_C | MSR_CC));
    tcg_gen_or_tl(cpu_SR[SR_MSR], cpu_SR[SR_MSR], t0);
    tcg_temp_free(t0);
}

/* True if ALU operand b is a small immediate that may deserve
   faster treatment.  */
static inline int dec_alu_op_b_is_small_imm(DisasContext *dc)
{
    /* Immediate insn without the imm prefix ?  */
    return dc->type_b && !(dc->tb_flags & IMM_FLAG);
}

static inline TCGv *dec_alu_op_b(DisasContext *dc)
{
    if (dc->type_b) {
        if (dc->tb_flags & IMM_FLAG)
            tcg_gen_ori_tl(env_imm, env_imm, dc->imm);
        else
            tcg_gen_movi_tl(env_imm, (int32_t)((int16_t)dc->imm));
        return &env_imm;
    } else
        return &cpu_R[dc->rb];
}

static void dec_add(DisasContext *dc)
{
    unsigned int k, c;
    TCGv cf;

    k = dc->opcode & 4;
    c = dc->opcode & 2;

    LOG_DIS("add%s%s%s r%d r%d r%d\n",
            dc->type_b ? "i" : "", k ? "k" : "", c ? "c" : "",
            dc->rd, dc->ra, dc->rb);

    /* Take care of the easy cases first.  */
    if (k) {
        /* k - keep carry, no need to update MSR.  */
        /* If rd == r0, it's a nop.  */
        if (dc->rd) {
            tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));

            if (c) {
                /* c - Add carry into the result.  */
                cf = tcg_temp_new();

                read_carry(dc, cf);
                tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->rd], cf);
                tcg_temp_free(cf);
            }
        }
        return;
    }

    /* From now on, we can assume k is zero.  So we need to update MSR.  */
    /* Extract carry.  */
    cf = tcg_temp_new();
    if (c) {
        read_carry(dc, cf);
    } else {
        tcg_gen_movi_tl(cf, 0);
    }

    if (dc->rd) {
        TCGv ncf = tcg_temp_new();
        gen_helper_carry(ncf, cpu_R[dc->ra], *(dec_alu_op_b(dc)), cf);
        tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
        tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->rd], cf);
        write_carry(dc, ncf);
        tcg_temp_free(ncf);
    } else {
        gen_helper_carry(cf, cpu_R[dc->ra], *(dec_alu_op_b(dc)), cf);
        write_carry(dc, cf);
    }
    tcg_temp_free(cf);
}

static void dec_sub(DisasContext *dc)
{
    unsigned int u, cmp, k, c;
    TCGv cf, na;

    u = dc->imm & 2;
    k = dc->opcode & 4;
    c = dc->opcode & 2;
    cmp = (dc->imm & 1) && (!dc->type_b) && k;

    if (cmp) {
        LOG_DIS("cmp%s r%d, r%d ir=%x\n", u ? "u" : "", dc->rd, dc->ra, dc->ir);
        if (dc->rd) {
            if (u)
                gen_helper_cmpu(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            else
                gen_helper_cmp(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
        }
        return;
    }

    LOG_DIS("sub%s%s r%d, r%d r%d\n",
             k ? "k" : "",  c ? "c" : "", dc->rd, dc->ra, dc->rb);

    /* Take care of the easy cases first.  */
    if (k) {
        /* k - keep carry, no need to update MSR.  */
        /* If rd == r0, it's a nop.  */
        if (dc->rd) {
            tcg_gen_sub_tl(cpu_R[dc->rd], *(dec_alu_op_b(dc)), cpu_R[dc->ra]);

            if (c) {
                /* c - Add carry into the result.  */
                cf = tcg_temp_new();

                read_carry(dc, cf);
                tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->rd], cf);
                tcg_temp_free(cf);
            }
        }
        return;
    }

    /* From now on, we can assume k is zero.  So we need to update MSR.  */
    /* Extract carry. And complement a into na.  */
    cf = tcg_temp_new();
    na = tcg_temp_new();
    if (c) {
        read_carry(dc, cf);
    } else {
        tcg_gen_movi_tl(cf, 1);
    }

    /* d = b + ~a + c. carry defaults to 1.  */
    tcg_gen_not_tl(na, cpu_R[dc->ra]);

    if (dc->rd) {
        TCGv ncf = tcg_temp_new();
        gen_helper_carry(ncf, na, *(dec_alu_op_b(dc)), cf);
        tcg_gen_add_tl(cpu_R[dc->rd], na, *(dec_alu_op_b(dc)));
        tcg_gen_add_tl(cpu_R[dc->rd], cpu_R[dc->rd], cf);
        write_carry(dc, ncf);
        tcg_temp_free(ncf);
    } else {
        gen_helper_carry(cf, na, *(dec_alu_op_b(dc)), cf);
        write_carry(dc, cf);
    }
    tcg_temp_free(cf);
    tcg_temp_free(na);
}

static void dec_pattern(DisasContext *dc)
{
    unsigned int mode;
    int l1;

    if ((dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
          && !((dc->env->pvr.regs[2] & PVR2_USE_PCMP_INSTR))) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }

    mode = dc->opcode & 3;
    switch (mode) {
        case 0:
            /* pcmpbf.  */
            LOG_DIS("pcmpbf r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            if (dc->rd)
                gen_helper_pcmpbf(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 2:
            LOG_DIS("pcmpeq r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            if (dc->rd) {
                TCGv t0 = tcg_temp_local_new();
                l1 = gen_new_label();
                tcg_gen_movi_tl(t0, 1);
                tcg_gen_brcond_tl(TCG_COND_EQ,
                                  cpu_R[dc->ra], cpu_R[dc->rb], l1);
                tcg_gen_movi_tl(t0, 0);
                gen_set_label(l1);
                tcg_gen_mov_tl(cpu_R[dc->rd], t0);
                tcg_temp_free(t0);
            }
            break;
        case 3:
            LOG_DIS("pcmpne r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            l1 = gen_new_label();
            if (dc->rd) {
                TCGv t0 = tcg_temp_local_new();
                tcg_gen_movi_tl(t0, 1);
                tcg_gen_brcond_tl(TCG_COND_NE,
                                  cpu_R[dc->ra], cpu_R[dc->rb], l1);
                tcg_gen_movi_tl(t0, 0);
                gen_set_label(l1);
                tcg_gen_mov_tl(cpu_R[dc->rd], t0);
                tcg_temp_free(t0);
            }
            break;
        default:
            cpu_abort(dc->env,
                      "unsupported pattern insn opcode=%x\n", dc->opcode);
            break;
    }
}

static void dec_and(DisasContext *dc)
{
    unsigned int not;

    if (!dc->type_b && (dc->imm & (1 << 10))) {
        dec_pattern(dc);
        return;
    }

    not = dc->opcode & (1 << 1);
    LOG_DIS("and%s\n", not ? "n" : "");

    if (!dc->rd)
        return;

    if (not) {
        TCGv t = tcg_temp_new();
        tcg_gen_not_tl(t, *(dec_alu_op_b(dc)));
        tcg_gen_and_tl(cpu_R[dc->rd], cpu_R[dc->ra], t);
        tcg_temp_free(t);
    } else
        tcg_gen_and_tl(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static void dec_or(DisasContext *dc)
{
    if (!dc->type_b && (dc->imm & (1 << 10))) {
        dec_pattern(dc);
        return;
    }

    LOG_DIS("or r%d r%d r%d imm=%x\n", dc->rd, dc->ra, dc->rb, dc->imm);
    if (dc->rd)
        tcg_gen_or_tl(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static void dec_xor(DisasContext *dc)
{
    if (!dc->type_b && (dc->imm & (1 << 10))) {
        dec_pattern(dc);
        return;
    }

    LOG_DIS("xor r%d\n", dc->rd);
    if (dc->rd)
        tcg_gen_xor_tl(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static inline void msr_read(DisasContext *dc, TCGv d)
{
    tcg_gen_mov_tl(d, cpu_SR[SR_MSR]);
}

static inline void msr_write(DisasContext *dc, TCGv v)
{
    dc->cpustate_changed = 1;
    tcg_gen_mov_tl(cpu_SR[SR_MSR], v);
    /* PVR, we have a processor version register.  */
    tcg_gen_ori_tl(cpu_SR[SR_MSR], cpu_SR[SR_MSR], (1 << 10));
}

static void dec_msr(DisasContext *dc)
{
    TCGv t0, t1;
    unsigned int sr, to, rn;
    int mem_index = cpu_mmu_index(dc->env);

    sr = dc->imm & ((1 << 14) - 1);
    to = dc->imm & (1 << 14);
    dc->type_b = 1;
    if (to)
        dc->cpustate_changed = 1;

    /* msrclr and msrset.  */
    if (!(dc->imm & (1 << 15))) {
        unsigned int clr = dc->ir & (1 << 16);

        LOG_DIS("msr%s r%d imm=%x\n", clr ? "clr" : "set",
                dc->rd, dc->imm);

        if (!(dc->env->pvr.regs[2] & PVR2_USE_MSR_INSTR)) {
            /* nop??? */
            return;
        }

        if ((dc->tb_flags & MSR_EE_FLAG)
            && mem_index == MMU_USER_IDX && (dc->imm != 4 && dc->imm != 0)) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
            return;
        }

        if (dc->rd)
            msr_read(dc, cpu_R[dc->rd]);

        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        msr_read(dc, t0);
        tcg_gen_mov_tl(t1, *(dec_alu_op_b(dc)));

        if (clr) {
            tcg_gen_not_tl(t1, t1);
            tcg_gen_and_tl(t0, t0, t1);
        } else
            tcg_gen_or_tl(t0, t0, t1);
        msr_write(dc, t0);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
	tcg_gen_movi_tl(cpu_SR[SR_PC], dc->pc + 4);
        dc->is_jmp = DISAS_UPDATE;
        return;
    }

    if (to) {
        if ((dc->tb_flags & MSR_EE_FLAG)
             && mem_index == MMU_USER_IDX) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
            return;
        }
    }

#if !defined(CONFIG_USER_ONLY)
    /* Catch read/writes to the mmu block.  */
    if ((sr & ~0xff) == 0x1000) {
        sr &= 7;
        LOG_DIS("m%ss sr%d r%d imm=%x\n", to ? "t" : "f", sr, dc->ra, dc->imm);
        if (to)
            gen_helper_mmu_write(tcg_const_tl(sr), cpu_R[dc->ra]);
        else
            gen_helper_mmu_read(cpu_R[dc->rd], tcg_const_tl(sr));
        return;
    }
#endif

    if (to) {
        LOG_DIS("m%ss sr%x r%d imm=%x\n", to ? "t" : "f", sr, dc->ra, dc->imm);
        switch (sr) {
            case 0:
                break;
            case 1:
                msr_write(dc, cpu_R[dc->ra]);
                break;
            case 0x3:
                tcg_gen_mov_tl(cpu_SR[SR_EAR], cpu_R[dc->ra]);
                break;
            case 0x5:
                tcg_gen_mov_tl(cpu_SR[SR_ESR], cpu_R[dc->ra]);
                break;
            case 0x7:
                tcg_gen_andi_tl(cpu_SR[SR_FSR], cpu_R[dc->ra], 31);
                break;
            default:
                cpu_abort(dc->env, "unknown mts reg %x\n", sr);
                break;
        }
    } else {
        LOG_DIS("m%ss r%d sr%x imm=%x\n", to ? "t" : "f", dc->rd, sr, dc->imm);

        switch (sr) {
            case 0:
                tcg_gen_movi_tl(cpu_R[dc->rd], dc->pc);
                break;
            case 1:
                msr_read(dc, cpu_R[dc->rd]);
                break;
            case 0x3:
                tcg_gen_mov_tl(cpu_R[dc->rd], cpu_SR[SR_EAR]);
                break;
            case 0x5:
                tcg_gen_mov_tl(cpu_R[dc->rd], cpu_SR[SR_ESR]);
                break;
             case 0x7:
                tcg_gen_mov_tl(cpu_R[dc->rd], cpu_SR[SR_FSR]);
                break;
            case 0xb:
                tcg_gen_mov_tl(cpu_R[dc->rd], cpu_SR[SR_BTR]);
                break;
            case 0x2000:
            case 0x2001:
            case 0x2002:
            case 0x2003:
            case 0x2004:
            case 0x2005:
            case 0x2006:
            case 0x2007:
            case 0x2008:
            case 0x2009:
            case 0x200a:
            case 0x200b:
            case 0x200c:
                rn = sr & 0xf;
                tcg_gen_ld_tl(cpu_R[dc->rd],
                              cpu_env, offsetof(CPUState, pvr.regs[rn]));
                break;
            default:
                cpu_abort(dc->env, "unknown mfs reg %x\n", sr);
                break;
        }
    }

    if (dc->rd == 0) {
        tcg_gen_movi_tl(cpu_R[0], 0);
    }
}

/* 64-bit signed mul, lower result in d and upper in d2.  */
static void t_gen_muls(TCGv d, TCGv d2, TCGv a, TCGv b)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(t0, a);
    tcg_gen_ext_i32_i64(t1, b);
    tcg_gen_mul_i64(t0, t0, t1);

    tcg_gen_trunc_i64_i32(d, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_i32(d2, t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* 64-bit unsigned muls, lower result in d and upper in d2.  */
static void t_gen_mulu(TCGv d, TCGv d2, TCGv a, TCGv b)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(t0, a);
    tcg_gen_extu_i32_i64(t1, b);
    tcg_gen_mul_i64(t0, t0, t1);

    tcg_gen_trunc_i64_i32(d, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_i32(d2, t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* Multiplier unit.  */
static void dec_mul(DisasContext *dc)
{
    TCGv d[2];
    unsigned int subcode;

    if ((dc->tb_flags & MSR_EE_FLAG)
         && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
         && !(dc->env->pvr.regs[0] & PVR0_USE_HW_MUL_MASK)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    subcode = dc->imm & 3;
    d[0] = tcg_temp_new();
    d[1] = tcg_temp_new();

    if (dc->type_b) {
        LOG_DIS("muli r%d r%d %x\n", dc->rd, dc->ra, dc->imm);
        t_gen_mulu(cpu_R[dc->rd], d[1], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
        goto done;
    }

    /* mulh, mulhsu and mulhu are not available if C_USE_HW_MUL is < 2.  */
    if (subcode >= 1 && subcode <= 3
        && !((dc->env->pvr.regs[2] & PVR2_USE_MUL64_MASK))) {
        /* nop??? */
    }

    switch (subcode) {
        case 0:
            LOG_DIS("mul r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            t_gen_mulu(cpu_R[dc->rd], d[1], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 1:
            LOG_DIS("mulh r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            t_gen_muls(d[0], cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 2:
            LOG_DIS("mulhsu r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            t_gen_muls(d[0], cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 3:
            LOG_DIS("mulhu r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            t_gen_mulu(d[0], cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        default:
            cpu_abort(dc->env, "unknown MUL insn %x\n", subcode);
            break;
    }
done:
    tcg_temp_free(d[0]);
    tcg_temp_free(d[1]);
}

/* Div unit.  */
static void dec_div(DisasContext *dc)
{
    unsigned int u;

    u = dc->imm & 2; 
    LOG_DIS("div\n");

    if ((dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
          && !((dc->env->pvr.regs[0] & PVR0_USE_DIV_MASK))) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }

    if (u)
        gen_helper_divu(cpu_R[dc->rd], *(dec_alu_op_b(dc)), cpu_R[dc->ra]);
    else
        gen_helper_divs(cpu_R[dc->rd], *(dec_alu_op_b(dc)), cpu_R[dc->ra]);
    if (!dc->rd)
        tcg_gen_movi_tl(cpu_R[dc->rd], 0);
}

static void dec_barrel(DisasContext *dc)
{
    TCGv t0;
    unsigned int s, t;

    if ((dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
          && !(dc->env->pvr.regs[0] & PVR0_USE_BARREL_MASK)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    s = dc->imm & (1 << 10);
    t = dc->imm & (1 << 9);

    LOG_DIS("bs%s%s r%d r%d r%d\n",
            s ? "l" : "r", t ? "a" : "l", dc->rd, dc->ra, dc->rb);

    t0 = tcg_temp_new();

    tcg_gen_mov_tl(t0, *(dec_alu_op_b(dc)));
    tcg_gen_andi_tl(t0, t0, 31);

    if (s)
        tcg_gen_shl_tl(cpu_R[dc->rd], cpu_R[dc->ra], t0);
    else {
        if (t)
            tcg_gen_sar_tl(cpu_R[dc->rd], cpu_R[dc->ra], t0);
        else
            tcg_gen_shr_tl(cpu_R[dc->rd], cpu_R[dc->ra], t0);
    }
}

static void dec_bit(DisasContext *dc)
{
    TCGv t0, t1;
    unsigned int op;
    int mem_index = cpu_mmu_index(dc->env);

    op = dc->ir & ((1 << 8) - 1);
    switch (op) {
        case 0x21:
            /* src.  */
            t0 = tcg_temp_new();

            LOG_DIS("src r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_andi_tl(t0, cpu_R[dc->ra], 1);
            if (dc->rd) {
                t1 = tcg_temp_new();
                read_carry(dc, t1);
                tcg_gen_shli_tl(t1, t1, 31);

                tcg_gen_shri_tl(cpu_R[dc->rd], cpu_R[dc->ra], 1);
                tcg_gen_or_tl(cpu_R[dc->rd], cpu_R[dc->rd], t1);
                tcg_temp_free(t1);
            }

            /* Update carry.  */
            write_carry(dc, t0);
            tcg_temp_free(t0);
            break;

        case 0x1:
        case 0x41:
            /* srl.  */
            t0 = tcg_temp_new();
            LOG_DIS("srl r%d r%d\n", dc->rd, dc->ra);

            /* Update carry.  */
            tcg_gen_andi_tl(t0, cpu_R[dc->ra], 1);
            write_carry(dc, t0);
            tcg_temp_free(t0);
            if (dc->rd) {
                if (op == 0x41)
                    tcg_gen_shri_tl(cpu_R[dc->rd], cpu_R[dc->ra], 1);
                else
                    tcg_gen_sari_tl(cpu_R[dc->rd], cpu_R[dc->ra], 1);
            }
            break;
        case 0x60:
            LOG_DIS("ext8s r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_ext8s_i32(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;
        case 0x61:
            LOG_DIS("ext16s r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_ext16s_i32(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;
        case 0x64:
        case 0x66:
        case 0x74:
        case 0x76:
            /* wdc.  */
            LOG_DIS("wdc r%d\n", dc->ra);
            if ((dc->tb_flags & MSR_EE_FLAG)
                 && mem_index == MMU_USER_IDX) {
                tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
                t_gen_raise_exception(dc, EXCP_HW_EXCP);
                return;
            }
            break;
        case 0x68:
            /* wic.  */
            LOG_DIS("wic r%d\n", dc->ra);
            if ((dc->tb_flags & MSR_EE_FLAG)
                 && mem_index == MMU_USER_IDX) {
                tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
                t_gen_raise_exception(dc, EXCP_HW_EXCP);
                return;
            }
            break;
        default:
            cpu_abort(dc->env, "unknown bit oc=%x op=%x rd=%d ra=%d rb=%d\n",
                     dc->pc, op, dc->rd, dc->ra, dc->rb);
            break;
    }
}

static inline void sync_jmpstate(DisasContext *dc)
{
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->jmp == JMP_DIRECT) {
            tcg_gen_movi_tl(env_btaken, 1);
        }
        dc->jmp = JMP_INDIRECT;
        tcg_gen_movi_tl(env_btarget, dc->jmp_pc);
    }
}

static void dec_imm(DisasContext *dc)
{
    LOG_DIS("imm %x\n", dc->imm << 16);
    tcg_gen_movi_tl(env_imm, (dc->imm << 16));
    dc->tb_flags |= IMM_FLAG;
    dc->clear_imm = 0;
}

static inline void gen_load(DisasContext *dc, TCGv dst, TCGv addr,
                            unsigned int size)
{
    int mem_index = cpu_mmu_index(dc->env);

    if (size == 1) {
        tcg_gen_qemu_ld8u(dst, addr, mem_index);
    } else if (size == 2) {
        tcg_gen_qemu_ld16u(dst, addr, mem_index);
    } else if (size == 4) {
        tcg_gen_qemu_ld32u(dst, addr, mem_index);
    } else
        cpu_abort(dc->env, "Incorrect load size %d\n", size);
}

static inline TCGv *compute_ldst_addr(DisasContext *dc, TCGv *t)
{
    unsigned int extimm = dc->tb_flags & IMM_FLAG;

    /* Treat the common cases first.  */
    if (!dc->type_b) {
        /* If any of the regs is r0, return a ptr to the other.  */
        if (dc->ra == 0) {
            return &cpu_R[dc->rb];
        } else if (dc->rb == 0) {
            return &cpu_R[dc->ra];
        }

        *t = tcg_temp_new();
        tcg_gen_add_tl(*t, cpu_R[dc->ra], cpu_R[dc->rb]);
        return t;
    }
    /* Immediate.  */
    if (!extimm) {
        if (dc->imm == 0) {
            return &cpu_R[dc->ra];
        }
        *t = tcg_temp_new();
        tcg_gen_movi_tl(*t, (int32_t)((int16_t)dc->imm));
        tcg_gen_add_tl(*t, cpu_R[dc->ra], *t);
    } else {
        *t = tcg_temp_new();
        tcg_gen_add_tl(*t, cpu_R[dc->ra], *(dec_alu_op_b(dc)));
    }

    return t;
}

static inline void dec_byteswap(DisasContext *dc, TCGv dst, TCGv src, int size)
{
    if (size == 4) {
        tcg_gen_bswap32_tl(dst, src);
    } else if (size == 2) {
        TCGv t = tcg_temp_new();

        /* bswap16 assumes the high bits are zero.  */
        tcg_gen_andi_tl(t, src, 0xffff);
        tcg_gen_bswap16_tl(dst, t);
        tcg_temp_free(t);
    } else {
        /* Ignore.
        cpu_abort(dc->env, "Invalid ldst byteswap size %d\n", size);
        */
    }
}

static void dec_load(DisasContext *dc)
{
    TCGv t, *addr;
    unsigned int size, rev = 0;

    size = 1 << (dc->opcode & 3);

    if (!dc->type_b) {
        rev = (dc->ir >> 9) & 1;
    }

    if (size > 4 && (dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    LOG_DIS("l%d%s%s\n", size, dc->type_b ? "i" : "", rev ? "r" : "");

    t_sync_flags(dc);
    addr = compute_ldst_addr(dc, &t);

    /*
     * When doing reverse accesses we need to do two things.
     *
     * 1. Reverse the address wrt endianness.
     * 2. Byteswap the data lanes on the way back into the CPU core.
     */
    if (rev && size != 4) {
        /* Endian reverse the address. t is addr.  */
        switch (size) {
            case 1:
            {
                /* 00 -> 11
                   01 -> 10
                   10 -> 10
                   11 -> 00 */
                TCGv low = tcg_temp_new();

                /* Force addr into the temp.  */
                if (addr != &t) {
                    t = tcg_temp_new();
                    tcg_gen_mov_tl(t, *addr);
                    addr = &t;
                }

                tcg_gen_andi_tl(low, t, 3);
                tcg_gen_sub_tl(low, tcg_const_tl(3), low);
                tcg_gen_andi_tl(t, t, ~3);
                tcg_gen_or_tl(t, t, low);
                tcg_gen_mov_tl(env_imm, t);
                tcg_temp_free(low);
                break;
            }

            case 2:
                /* 00 -> 10
                   10 -> 00.  */
                /* Force addr into the temp.  */
                if (addr != &t) {
                    t = tcg_temp_new();
                    tcg_gen_xori_tl(t, *addr, 2);
                    addr = &t;
                } else {
                    tcg_gen_xori_tl(t, t, 2);
                }
                break;
            default:
                cpu_abort(dc->env, "Invalid reverse size\n");
                break;
        }
    }

    /* If we get a fault on a dslot, the jmpstate better be in sync.  */
    sync_jmpstate(dc);

    /* Verify alignment if needed.  */
    if ((dc->env->pvr.regs[2] & PVR2_UNALIGNED_EXC_MASK) && size > 1) {
        TCGv v = tcg_temp_new();

        /*
         * Microblaze gives MMU faults priority over faults due to
         * unaligned addresses. That's why we speculatively do the load
         * into v. If the load succeeds, we verify alignment of the
         * address and if that succeeds we write into the destination reg.
         */
        gen_load(dc, v, *addr, size);

        tcg_gen_movi_tl(cpu_SR[SR_PC], dc->pc);
        gen_helper_memalign(*addr, tcg_const_tl(dc->rd),
                            tcg_const_tl(0), tcg_const_tl(size - 1));
        if (dc->rd) {
            if (rev) {
                dec_byteswap(dc, cpu_R[dc->rd], v, size);
            } else {
                tcg_gen_mov_tl(cpu_R[dc->rd], v);
            }
        }
        tcg_temp_free(v);
    } else {
        if (dc->rd) {
            gen_load(dc, cpu_R[dc->rd], *addr, size);
            if (rev) {
                dec_byteswap(dc, cpu_R[dc->rd], cpu_R[dc->rd], size);
            }
        } else {
            /* We are loading into r0, no need to reverse.  */
            gen_load(dc, env_imm, *addr, size);
        }
    }

    if (addr == &t)
        tcg_temp_free(t);
}

static void gen_store(DisasContext *dc, TCGv addr, TCGv val,
                      unsigned int size)
{
    int mem_index = cpu_mmu_index(dc->env);

    if (size == 1)
        tcg_gen_qemu_st8(val, addr, mem_index);
    else if (size == 2) {
        tcg_gen_qemu_st16(val, addr, mem_index);
    } else if (size == 4) {
        tcg_gen_qemu_st32(val, addr, mem_index);
    } else
        cpu_abort(dc->env, "Incorrect store size %d\n", size);
}

static void dec_store(DisasContext *dc)
{
    TCGv t, *addr;
    unsigned int size, rev = 0;

    size = 1 << (dc->opcode & 3);
    if (!dc->type_b) {
        rev = (dc->ir >> 9) & 1;
    }

    if (size > 4 && (dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    LOG_DIS("s%d%s%s\n", size, dc->type_b ? "i" : "", rev ? "r" : "");
    t_sync_flags(dc);
    /* If we get a fault on a dslot, the jmpstate better be in sync.  */
    sync_jmpstate(dc);
    addr = compute_ldst_addr(dc, &t);

    if (rev && size != 4) {
        /* Endian reverse the address. t is addr.  */
        switch (size) {
            case 1:
            {
                /* 00 -> 11
                   01 -> 10
                   10 -> 10
                   11 -> 00 */
                TCGv low = tcg_temp_new();

                /* Force addr into the temp.  */
                if (addr != &t) {
                    t = tcg_temp_new();
                    tcg_gen_mov_tl(t, *addr);
                    addr = &t;
                }

                tcg_gen_andi_tl(low, t, 3);
                tcg_gen_sub_tl(low, tcg_const_tl(3), low);
                tcg_gen_andi_tl(t, t, ~3);
                tcg_gen_or_tl(t, t, low);
                tcg_gen_mov_tl(env_imm, t);
                tcg_temp_free(low);
                break;
            }

            case 2:
                /* 00 -> 10
                   10 -> 00.  */
                /* Force addr into the temp.  */
                if (addr != &t) {
                    t = tcg_temp_new();
                    tcg_gen_xori_tl(t, *addr, 2);
                    addr = &t;
                } else {
                    tcg_gen_xori_tl(t, t, 2);
                }
                break;
            default:
                cpu_abort(dc->env, "Invalid reverse size\n");
                break;
        }

        if (size != 1) {
            TCGv bs_data = tcg_temp_new();
            dec_byteswap(dc, bs_data, cpu_R[dc->rd], size);
            gen_store(dc, *addr, bs_data, size);
            tcg_temp_free(bs_data);
        } else {
            gen_store(dc, *addr, cpu_R[dc->rd], size);
        }
    } else {
        if (rev) {
            TCGv bs_data = tcg_temp_new();
            dec_byteswap(dc, bs_data, cpu_R[dc->rd], size);
            gen_store(dc, *addr, bs_data, size);
            tcg_temp_free(bs_data);
        } else {
            gen_store(dc, *addr, cpu_R[dc->rd], size);
        }
    }

    /* Verify alignment if needed.  */
    if ((dc->env->pvr.regs[2] & PVR2_UNALIGNED_EXC_MASK) && size > 1) {
        tcg_gen_movi_tl(cpu_SR[SR_PC], dc->pc);
        /* FIXME: if the alignment is wrong, we should restore the value
         *        in memory. One possible way to acheive this is to probe
         *        the MMU prior to the memaccess, thay way we could put
         *        the alignment checks in between the probe and the mem
         *        access.
         */
        gen_helper_memalign(*addr, tcg_const_tl(dc->rd),
                            tcg_const_tl(1), tcg_const_tl(size - 1));
    }

    if (addr == &t)
        tcg_temp_free(t);
}

static inline void eval_cc(DisasContext *dc, unsigned int cc,
                           TCGv d, TCGv a, TCGv b)
{
    switch (cc) {
        case CC_EQ:
            tcg_gen_setcond_tl(TCG_COND_EQ, d, a, b);
            break;
        case CC_NE:
            tcg_gen_setcond_tl(TCG_COND_NE, d, a, b);
            break;
        case CC_LT:
            tcg_gen_setcond_tl(TCG_COND_LT, d, a, b);
            break;
        case CC_LE:
            tcg_gen_setcond_tl(TCG_COND_LE, d, a, b);
            break;
        case CC_GE:
            tcg_gen_setcond_tl(TCG_COND_GE, d, a, b);
            break;
        case CC_GT:
            tcg_gen_setcond_tl(TCG_COND_GT, d, a, b);
            break;
        default:
            cpu_abort(dc->env, "Unknown condition code %x.\n", cc);
            break;
    }
}

static void eval_cond_jmp(DisasContext *dc, TCGv pc_true, TCGv pc_false)
{
    int l1;

    l1 = gen_new_label();
    /* Conditional jmp.  */
    tcg_gen_mov_tl(cpu_SR[SR_PC], pc_false);
    tcg_gen_brcondi_tl(TCG_COND_EQ, env_btaken, 0, l1);
    tcg_gen_mov_tl(cpu_SR[SR_PC], pc_true);
    gen_set_label(l1);
}

static void dec_bcc(DisasContext *dc)
{
    unsigned int cc;
    unsigned int dslot;

    cc = EXTRACT_FIELD(dc->ir, 21, 23);
    dslot = dc->ir & (1 << 25);
    LOG_DIS("bcc%s r%d %x\n", dslot ? "d" : "", dc->ra, dc->imm);

    dc->delayed_branch = 1;
    if (dslot) {
        dc->delayed_branch = 2;
        dc->tb_flags |= D_FLAG;
        tcg_gen_st_tl(tcg_const_tl(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                      cpu_env, offsetof(CPUState, bimm));
    }

    if (dec_alu_op_b_is_small_imm(dc)) {
        int32_t offset = (int32_t)((int16_t)dc->imm); /* sign-extend.  */

        tcg_gen_movi_tl(env_btarget, dc->pc + offset);
        dc->jmp = JMP_DIRECT_CC;
        dc->jmp_pc = dc->pc + offset;
    } else {
        dc->jmp = JMP_INDIRECT;
        tcg_gen_movi_tl(env_btarget, dc->pc);
        tcg_gen_add_tl(env_btarget, env_btarget, *(dec_alu_op_b(dc)));
    }
    eval_cc(dc, cc, env_btaken, cpu_R[dc->ra], tcg_const_tl(0));
}

static void dec_br(DisasContext *dc)
{
    unsigned int dslot, link, abs;
    int mem_index = cpu_mmu_index(dc->env);

    dslot = dc->ir & (1 << 20);
    abs = dc->ir & (1 << 19);
    link = dc->ir & (1 << 18);
    LOG_DIS("br%s%s%s%s imm=%x\n",
             abs ? "a" : "", link ? "l" : "",
             dc->type_b ? "i" : "", dslot ? "d" : "",
             dc->imm);

    dc->delayed_branch = 1;
    if (dslot) {
        dc->delayed_branch = 2;
        dc->tb_flags |= D_FLAG;
        tcg_gen_st_tl(tcg_const_tl(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                      cpu_env, offsetof(CPUState, bimm));
    }
    if (link && dc->rd)
        tcg_gen_movi_tl(cpu_R[dc->rd], dc->pc);

    dc->jmp = JMP_INDIRECT;
    if (abs) {
        tcg_gen_movi_tl(env_btaken, 1);
        tcg_gen_mov_tl(env_btarget, *(dec_alu_op_b(dc)));
        if (link && !dslot) {
            if (!(dc->tb_flags & IMM_FLAG) && (dc->imm == 8 || dc->imm == 0x18))
                t_gen_raise_exception(dc, EXCP_BREAK);
            if (dc->imm == 0) {
                if ((dc->tb_flags & MSR_EE_FLAG) && mem_index == MMU_USER_IDX) {
                    tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
                    t_gen_raise_exception(dc, EXCP_HW_EXCP);
                    return;
                }

                t_gen_raise_exception(dc, EXCP_DEBUG);
            }
        }
    } else {
        if (dec_alu_op_b_is_small_imm(dc)) {
            dc->jmp = JMP_DIRECT;
            dc->jmp_pc = dc->pc + (int32_t)((int16_t)dc->imm);
        } else {
            tcg_gen_movi_tl(env_btaken, 1);
            tcg_gen_movi_tl(env_btarget, dc->pc);
            tcg_gen_add_tl(env_btarget, env_btarget, *(dec_alu_op_b(dc)));
        }
    }
}

static inline void do_rti(DisasContext *dc)
{
    TCGv t0, t1;
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_SR[SR_MSR], 1);
    tcg_gen_ori_tl(t1, cpu_SR[SR_MSR], MSR_IE);
    tcg_gen_andi_tl(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_tl(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_tl(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    dc->tb_flags &= ~DRTI_FLAG;
}

static inline void do_rtb(DisasContext *dc)
{
    TCGv t0, t1;
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_SR[SR_MSR], ~MSR_BIP);
    tcg_gen_shri_tl(t0, t1, 1);
    tcg_gen_andi_tl(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_tl(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_tl(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    dc->tb_flags &= ~DRTB_FLAG;
}

static inline void do_rte(DisasContext *dc)
{
    TCGv t0, t1;
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    tcg_gen_ori_tl(t1, cpu_SR[SR_MSR], MSR_EE);
    tcg_gen_andi_tl(t1, t1, ~MSR_EIP);
    tcg_gen_shri_tl(t0, t1, 1);
    tcg_gen_andi_tl(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_tl(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_tl(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    dc->tb_flags &= ~DRTE_FLAG;
}

static void dec_rts(DisasContext *dc)
{
    unsigned int b_bit, i_bit, e_bit;
    int mem_index = cpu_mmu_index(dc->env);

    i_bit = dc->ir & (1 << 21);
    b_bit = dc->ir & (1 << 22);
    e_bit = dc->ir & (1 << 23);

    dc->delayed_branch = 2;
    dc->tb_flags |= D_FLAG;
    tcg_gen_st_tl(tcg_const_tl(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                  cpu_env, offsetof(CPUState, bimm));

    if (i_bit) {
        LOG_DIS("rtid ir=%x\n", dc->ir);
        if ((dc->tb_flags & MSR_EE_FLAG)
             && mem_index == MMU_USER_IDX) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
        }
        dc->tb_flags |= DRTI_FLAG;
    } else if (b_bit) {
        LOG_DIS("rtbd ir=%x\n", dc->ir);
        if ((dc->tb_flags & MSR_EE_FLAG)
             && mem_index == MMU_USER_IDX) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
        }
        dc->tb_flags |= DRTB_FLAG;
    } else if (e_bit) {
        LOG_DIS("rted ir=%x\n", dc->ir);
        if ((dc->tb_flags & MSR_EE_FLAG)
             && mem_index == MMU_USER_IDX) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
        }
        dc->tb_flags |= DRTE_FLAG;
    } else
        LOG_DIS("rts ir=%x\n", dc->ir);

    dc->jmp = JMP_INDIRECT;
    tcg_gen_movi_tl(env_btaken, 1);
    tcg_gen_add_tl(env_btarget, cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static int dec_check_fpuv2(DisasContext *dc)
{
    int r;

    r = dc->env->pvr.regs[2] & PVR2_USE_FPU2_MASK;

    if (!r && (dc->tb_flags & MSR_EE_FLAG)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_FPU);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }
    return r;
}

static void dec_fpu(DisasContext *dc)
{
    unsigned int fpu_insn;

    if ((dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
          && !((dc->env->pvr.regs[2] & PVR2_USE_FPU_MASK))) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    fpu_insn = (dc->ir >> 7) & 7;

    switch (fpu_insn) {
        case 0:
            gen_helper_fadd(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;

        case 1:
            gen_helper_frsub(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;

        case 2:
            gen_helper_fmul(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;

        case 3:
            gen_helper_fdiv(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;

        case 4:
            switch ((dc->ir >> 4) & 7) {
                case 0:
                    gen_helper_fcmp_un(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 1:
                    gen_helper_fcmp_lt(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 2:
                    gen_helper_fcmp_eq(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 3:
                    gen_helper_fcmp_le(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 4:
                    gen_helper_fcmp_gt(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 5:
                    gen_helper_fcmp_ne(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 6:
                    gen_helper_fcmp_ge(cpu_R[dc->rd],
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                default:
                    qemu_log ("unimplemented fcmp fpu_insn=%x pc=%x opc=%x\n",
                              fpu_insn, dc->pc, dc->opcode);
                    dc->abort_at_next_insn = 1;
                    break;
            }
            break;

        case 5:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_flt(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;

        case 6:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_fint(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;

        case 7:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_fsqrt(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;

        default:
            qemu_log ("unimplemented FPU insn fpu_insn=%x pc=%x opc=%x\n",
                      fpu_insn, dc->pc, dc->opcode);
            dc->abort_at_next_insn = 1;
            break;
    }
}

static void dec_null(DisasContext *dc)
{
    if ((dc->tb_flags & MSR_EE_FLAG)
          && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }
    qemu_log ("unknown insn pc=%x opc=%x\n", dc->pc, dc->opcode);
    dc->abort_at_next_insn = 1;
}

/* Insns connected to FSL or AXI stream attached devices.  */
static void dec_stream(DisasContext *dc)
{
    int mem_index = cpu_mmu_index(dc->env);
    TCGv_i32 t_id, t_ctrl;
    int ctrl;

    LOG_DIS("%s%s imm=%x\n", dc->rd ? "get" : "put",
            dc->type_b ? "" : "d", dc->imm);

    if ((dc->tb_flags & MSR_EE_FLAG) && (mem_index == MMU_USER_IDX)) {
        tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
        return;
    }

    t_id = tcg_temp_new();
    if (dc->type_b) {
        tcg_gen_movi_tl(t_id, dc->imm & 0xf);
        ctrl = dc->imm >> 10;
    } else {
        tcg_gen_andi_tl(t_id, cpu_R[dc->rb], 0xf);
        ctrl = dc->imm >> 5;
    }

    t_ctrl = tcg_const_tl(ctrl);

    if (dc->rd == 0) {
        gen_helper_put(t_id, t_ctrl, cpu_R[dc->ra]);
    } else {
        gen_helper_get(cpu_R[dc->rd], t_id, t_ctrl);
    }
    tcg_temp_free(t_id);
    tcg_temp_free(t_ctrl);
}

static struct decoder_info {
    struct {
        uint32_t bits;
        uint32_t mask;
    };
    void (*dec)(DisasContext *dc);
} decinfo[] = {
    {DEC_ADD, dec_add},
    {DEC_SUB, dec_sub},
    {DEC_AND, dec_and},
    {DEC_XOR, dec_xor},
    {DEC_OR, dec_or},
    {DEC_BIT, dec_bit},
    {DEC_BARREL, dec_barrel},
    {DEC_LD, dec_load},
    {DEC_ST, dec_store},
    {DEC_IMM, dec_imm},
    {DEC_BR, dec_br},
    {DEC_BCC, dec_bcc},
    {DEC_RTS, dec_rts},
    {DEC_FPU, dec_fpu},
    {DEC_MUL, dec_mul},
    {DEC_DIV, dec_div},
    {DEC_MSR, dec_msr},
    {DEC_STREAM, dec_stream},
    {{0, 0}, dec_null}
};

static inline void decode(DisasContext *dc)
{
    uint32_t ir;
    int i;

    if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP)))
        tcg_gen_debug_insn_start(dc->pc);

    dc->ir = ir = ldl_code(dc->pc);
    LOG_DIS("%8.8x\t", dc->ir);

    if (dc->ir)
        dc->nr_nops = 0;
    else {
        if ((dc->tb_flags & MSR_EE_FLAG)
              && (dc->env->pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)
              && (dc->env->pvr.regs[2] & PVR2_OPCODE_0x0_ILL_MASK)) {
            tcg_gen_movi_tl(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
            t_gen_raise_exception(dc, EXCP_HW_EXCP);
            return;
        }

        LOG_DIS("nr_nops=%d\t", dc->nr_nops);
        dc->nr_nops++;
        if (dc->nr_nops > 4)
            cpu_abort(dc->env, "fetching nop sequence\n");
    }
    /* bit 2 seems to indicate insn type.  */
    dc->type_b = ir & (1 << 29);

    dc->opcode = EXTRACT_FIELD(ir, 26, 31);
    dc->rd = EXTRACT_FIELD(ir, 21, 25);
    dc->ra = EXTRACT_FIELD(ir, 16, 20);
    dc->rb = EXTRACT_FIELD(ir, 11, 15);
    dc->imm = EXTRACT_FIELD(ir, 0, 15);

    /* Large switch for all insns.  */
    for (i = 0; i < ARRAY_SIZE(decinfo); i++) {
        if ((dc->opcode & decinfo[i].mask) == decinfo[i].bits) {
            decinfo[i].dec(dc);
            break;
        }
    }
}

static void check_breakpoint(CPUState *env, DisasContext *dc)
{
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
        QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                t_gen_raise_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
             }
        }
    }
}

/* generate intermediate code for basic block 'tb'.  */
static void
gen_intermediate_code_internal(CPUState *env, TranslationBlock *tb,
                               int search_pc)
{
    uint16_t *gen_opc_end;
    uint32_t pc_start;
    int j, lj;
    struct DisasContext ctx;
    struct DisasContext *dc = &ctx;
    uint32_t next_page_start, org_flags;
    target_ulong npc;
    int num_insns;
    int max_insns;

    qemu_log_try_set_file(stderr);

    pc_start = tb->pc;
    dc->env = env;
    dc->tb = tb;
    org_flags = dc->synced_flags = dc->tb_flags = tb->flags;

    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

    dc->is_jmp = DISAS_NEXT;
    dc->jmp = 0;
    dc->delayed_branch = !!(dc->tb_flags & D_FLAG);
    if (dc->delayed_branch) {
        dc->jmp = JMP_INDIRECT;
    }
    dc->pc = pc_start;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->cpustate_changed = 0;
    dc->abort_at_next_insn = 0;
    dc->nr_nops = 0;

    if (pc_start & 3)
        cpu_abort(env, "Microblaze: unaligned PC=%x\n", pc_start);

    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
#if !SIM_COMPAT
        qemu_log("--------------\n");
        log_cpu_state(env, 0);
#endif
    }

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_icount_start();
    do
    {
#if SIM_COMPAT
        if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
            tcg_gen_movi_tl(cpu_SR[SR_PC], dc->pc);
            gen_helper_debug();
        }
#endif
        check_breakpoint(env, dc);

        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = dc->pc;
            gen_opc_instr_start[lj] = 1;
                        gen_opc_icount[lj] = num_insns;
        }

        /* Pretty disas.  */
        LOG_DIS("%8.8x:\t", dc->pc);

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();

        dc->clear_imm = 1;
	decode(dc);
        if (dc->clear_imm)
            dc->tb_flags &= ~IMM_FLAG;
        dc->pc += 4;
        num_insns++;

        if (dc->delayed_branch) {
            dc->delayed_branch--;
            if (!dc->delayed_branch) {
                if (dc->tb_flags & DRTI_FLAG)
                    do_rti(dc);
                 if (dc->tb_flags & DRTB_FLAG)
                    do_rtb(dc);
                if (dc->tb_flags & DRTE_FLAG)
                    do_rte(dc);
                /* Clear the delay slot flag.  */
                dc->tb_flags &= ~D_FLAG;
                /* If it is a direct jump, try direct chaining.  */
                if (dc->jmp == JMP_INDIRECT) {
                    eval_cond_jmp(dc, env_btarget, tcg_const_tl(dc->pc));
                    dc->is_jmp = DISAS_JUMP;
                } else if (dc->jmp == JMP_DIRECT) {
                    t_sync_flags(dc);
                    gen_goto_tb(dc, 0, dc->jmp_pc);
                    dc->is_jmp = DISAS_TB_JUMP;
                } else if (dc->jmp == JMP_DIRECT_CC) {
                    int l1;

                    t_sync_flags(dc);
                    l1 = gen_new_label();
                    /* Conditional jmp.  */
                    tcg_gen_brcondi_tl(TCG_COND_NE, env_btaken, 0, l1);
                    gen_goto_tb(dc, 1, dc->pc);
                    gen_set_label(l1);
                    gen_goto_tb(dc, 0, dc->jmp_pc);

                    dc->is_jmp = DISAS_TB_JUMP;
                }
                break;
            }
        }
        if (env->singlestep_enabled)
            break;
    } while (!dc->is_jmp && !dc->cpustate_changed
         && gen_opc_ptr < gen_opc_end
                 && !singlestep
         && (dc->pc < next_page_start)
                 && num_insns < max_insns);

    npc = dc->pc;
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->tb_flags & D_FLAG) {
            dc->is_jmp = DISAS_UPDATE;
            tcg_gen_movi_tl(cpu_SR[SR_PC], npc);
            sync_jmpstate(dc);
        } else
            npc = dc->jmp_pc;
    }

    if (tb->cflags & CF_LAST_IO)
        gen_io_end();
    /* Force an update if the per-tb cpu state has changed.  */
    if (dc->is_jmp == DISAS_NEXT
        && (dc->cpustate_changed || org_flags != dc->tb_flags)) {
        dc->is_jmp = DISAS_UPDATE;
        tcg_gen_movi_tl(cpu_SR[SR_PC], npc);
    }
    t_sync_flags(dc);

    if (unlikely(env->singlestep_enabled)) {
        TCGv_i32 tmp = tcg_const_i32(EXCP_DEBUG);

        if (dc->is_jmp != DISAS_JUMP) {
            tcg_gen_movi_tl(cpu_SR[SR_PC], npc);
        }
        gen_helper_raise_exception(tmp);
        tcg_temp_free_i32(tmp);
    } else {
        switch(dc->is_jmp) {
            case DISAS_NEXT:
                gen_goto_tb(dc, 1, npc);
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
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = dc->pc - pc_start;
                tb->icount = num_insns;
    }

#ifdef DEBUG_DISAS
#if !SIM_COMPAT
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("\n");
#if DISAS_GNU
        log_target_disas(pc_start, dc->pc - pc_start, 0);
#endif
        qemu_log("\nisize=%d osize=%td\n",
            dc->pc - pc_start, gen_opc_ptr - gen_opc_buf);
    }
#endif
#endif
    assert(!dc->abort_at_next_insn);
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state (CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                     int flags)
{
    int i;

    if (!env || !f)
        return;

    cpu_fprintf(f, "IN: PC=%x %s\n",
                env->sregs[SR_PC], lookup_symbol(env->sregs[SR_PC]));
    cpu_fprintf(f, "rmsr=%x resr=%x rear=%x debug=%x imm=%x iflags=%x fsr=%x\n",
             env->sregs[SR_MSR], env->sregs[SR_ESR], env->sregs[SR_EAR],
             env->debug, env->imm, env->iflags, env->sregs[SR_FSR]);
    cpu_fprintf(f, "btaken=%d btarget=%x mode=%s(saved=%s) eip=%d ie=%d\n",
             env->btaken, env->btarget,
             (env->sregs[SR_MSR] & MSR_UM) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_UMS) ? "user" : "kernel",
             (env->sregs[SR_MSR] & MSR_EIP),
             (env->sregs[SR_MSR] & MSR_IE));

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "r%2.2d=%8.8x ", i, env->regs[i]);
        if ((i + 1) % 4 == 0)
            cpu_fprintf(f, "\n");
        }
    cpu_fprintf(f, "\n\n");
}

CPUState *cpu_mb_init (const char *cpu_model)
{
    CPUState *env;
    static int tcg_initialized = 0;
    int i;

    env = qemu_mallocz(sizeof(CPUState));

    cpu_exec_init(env);
    cpu_reset(env);
    qemu_init_vcpu(env);
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status);

    if (tcg_initialized)
        return env;

    tcg_initialized = 1;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    env_debug = tcg_global_mem_new(TCG_AREG0, 
                    offsetof(CPUState, debug),
                    "debug0");
    env_iflags = tcg_global_mem_new(TCG_AREG0, 
                    offsetof(CPUState, iflags),
                    "iflags");
    env_imm = tcg_global_mem_new(TCG_AREG0, 
                    offsetof(CPUState, imm),
                    "imm");
    env_btarget = tcg_global_mem_new(TCG_AREG0,
                     offsetof(CPUState, btarget),
                     "btarget");
    env_btaken = tcg_global_mem_new(TCG_AREG0,
                     offsetof(CPUState, btaken),
                     "btaken");
    for (i = 0; i < ARRAY_SIZE(cpu_R); i++) {
        cpu_R[i] = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUState, regs[i]),
                          regnames[i]);
    }
    for (i = 0; i < ARRAY_SIZE(cpu_SR); i++) {
        cpu_SR[i] = tcg_global_mem_new(TCG_AREG0,
                          offsetof(CPUState, sregs[i]),
                          special_regnames[i]);
    }
#define GEN_HELPER 2
#include "helper.h"

    return env;
}

void cpu_reset (CPUState *env)
{
    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, 0);
    }

    memset(env, 0, offsetof(CPUMBState, breakpoints));
    tlb_flush(env, 1);

    env->pvr.regs[0] = PVR0_PVR_FULL_MASK \
                       | PVR0_USE_BARREL_MASK \
                       | PVR0_USE_DIV_MASK \
                       | PVR0_USE_HW_MUL_MASK \
                       | PVR0_USE_EXC_MASK \
                       | PVR0_USE_ICACHE_MASK \
                       | PVR0_USE_DCACHE_MASK \
                       | PVR0_USE_MMU \
                       | (0xb << 8);
    env->pvr.regs[2] = PVR2_D_OPB_MASK \
                        | PVR2_D_LMB_MASK \
                        | PVR2_I_OPB_MASK \
                        | PVR2_I_LMB_MASK \
                        | PVR2_USE_MSR_INSTR \
                        | PVR2_USE_PCMP_INSTR \
                        | PVR2_USE_BARREL_MASK \
                        | PVR2_USE_DIV_MASK \
                        | PVR2_USE_HW_MUL_MASK \
                        | PVR2_USE_MUL64_MASK \
                        | PVR2_USE_FPU_MASK \
                        | PVR2_USE_FPU2_MASK \
                        | PVR2_FPU_EXC_MASK \
                        | 0;
    env->pvr.regs[10] = 0x0c000000; /* Default to spartan 3a dsp family.  */
    env->pvr.regs[11] = PVR11_USE_MMU | (16 << 17);

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->sregs[SR_MSR] = MSR_EE | MSR_IE | MSR_VM | MSR_UM;
    env->pvr.regs[10] = 0x0c000000; /* Spartan 3a dsp.  */
#else
    env->sregs[SR_MSR] = 0;
    mmu_init(&env->mmu);
    env->mmu.c_mmu = 3;
    env->mmu.c_mmu_tlb_access = 3;
    env->mmu.c_mmu_zones = 16;
#endif
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, int pc_pos)
{
    env->sregs[SR_PC] = gen_opc_pc[pc_pos];
}
