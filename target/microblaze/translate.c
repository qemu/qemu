/*
 *  Xilinx MicroBlaze emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias.
 *  Copyright (c) 2009-2012 PetaLogix Qld Pty Ltd.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/helper-proto.h"
#include "microblaze-decode.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "qemu/qemu-print.h"

#include "trace-tcg.h"
#include "exec/log.h"


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

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

static TCGv_i32 env_debug;
static TCGv_i32 cpu_R[32];
static TCGv_i64 cpu_SR[14];
static TCGv_i32 env_imm;
static TCGv_i32 env_btaken;
static TCGv_i64 env_btarget;
static TCGv_i32 env_iflags;
static TCGv env_res_addr;
static TCGv_i32 env_res_val;

#include "exec/gen-icount.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
    MicroBlazeCPU *cpu;
    uint32_t pc;

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
    "rpc", "rmsr", "sr2", "rear", "sr4", "resr", "sr6", "rfsr",
    "sr8", "sr9", "sr10", "rbtr", "sr12", "redr"
};

static inline void t_sync_flags(DisasContext *dc)
{
    /* Synch the tb dependent flags between translator and runtime.  */
    if (dc->tb_flags != dc->synced_flags) {
        tcg_gen_movi_i32(env_iflags, dc->tb_flags);
        dc->synced_flags = dc->tb_flags;
    }
}

static inline void t_gen_raise_exception(DisasContext *dc, uint32_t index)
{
    TCGv_i32 tmp = tcg_const_i32(index);

    t_sync_flags(dc);
    tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc);
    gen_helper_raise_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
    dc->is_jmp = DISAS_UPDATE;
}

static inline bool use_goto_tb(DisasContext *dc, target_ulong dest)
{
#ifndef CONFIG_USER_ONLY
    return (dc->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static void gen_goto_tb(DisasContext *dc, int n, target_ulong dest)
{
    if (use_goto_tb(dc, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i64(cpu_SR[SR_PC], dest);
        tcg_gen_exit_tb(dc->tb, n);
    } else {
        tcg_gen_movi_i64(cpu_SR[SR_PC], dest);
        tcg_gen_exit_tb(NULL, 0);
    }
}

static void read_carry(DisasContext *dc, TCGv_i32 d)
{
    tcg_gen_extrl_i64_i32(d, cpu_SR[SR_MSR]);
    tcg_gen_shri_i32(d, d, 31);
}

/*
 * write_carry sets the carry bits in MSR based on bit 0 of v.
 * v[31:1] are ignored.
 */
static void write_carry(DisasContext *dc, TCGv_i32 v)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, v);
    /* Deposit bit 0 into MSR_C and the alias MSR_CC.  */
    tcg_gen_deposit_i64(cpu_SR[SR_MSR], cpu_SR[SR_MSR], t0, 2, 1);
    tcg_gen_deposit_i64(cpu_SR[SR_MSR], cpu_SR[SR_MSR], t0, 31, 1);
    tcg_temp_free_i64(t0);
}

static void write_carryi(DisasContext *dc, bool carry)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_movi_i32(t0, carry);
    write_carry(dc, t0);
    tcg_temp_free_i32(t0);
}

/*
 * Returns true if the insn an illegal operation.
 * If exceptions are enabled, an exception is raised.
 */
static bool trap_illegal(DisasContext *dc, bool cond)
{
    if (cond && (dc->tb_flags & MSR_EE_FLAG)
        && (dc->cpu->env.pvr.regs[2] & PVR2_ILL_OPCODE_EXC_MASK)) {
        tcg_gen_movi_i64(cpu_SR[SR_ESR], ESR_EC_ILLEGAL_OP);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }
    return cond;
}

/*
 * Returns true if the insn is illegal in userspace.
 * If exceptions are enabled, an exception is raised.
 */
static bool trap_userspace(DisasContext *dc, bool cond)
{
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);
    bool cond_user = cond && mem_index == MMU_USER_IDX;

    if (cond_user && (dc->tb_flags & MSR_EE_FLAG)) {
        tcg_gen_movi_i64(cpu_SR[SR_ESR], ESR_EC_PRIVINSN);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }
    return cond_user;
}

/* True if ALU operand b is a small immediate that may deserve
   faster treatment.  */
static inline int dec_alu_op_b_is_small_imm(DisasContext *dc)
{
    /* Immediate insn without the imm prefix ?  */
    return dc->type_b && !(dc->tb_flags & IMM_FLAG);
}

static inline TCGv_i32 *dec_alu_op_b(DisasContext *dc)
{
    if (dc->type_b) {
        if (dc->tb_flags & IMM_FLAG)
            tcg_gen_ori_i32(env_imm, env_imm, dc->imm);
        else
            tcg_gen_movi_i32(env_imm, (int32_t)((int16_t)dc->imm));
        return &env_imm;
    } else
        return &cpu_R[dc->rb];
}

static void dec_add(DisasContext *dc)
{
    unsigned int k, c;
    TCGv_i32 cf;

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
            tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));

            if (c) {
                /* c - Add carry into the result.  */
                cf = tcg_temp_new_i32();

                read_carry(dc, cf);
                tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->rd], cf);
                tcg_temp_free_i32(cf);
            }
        }
        return;
    }

    /* From now on, we can assume k is zero.  So we need to update MSR.  */
    /* Extract carry.  */
    cf = tcg_temp_new_i32();
    if (c) {
        read_carry(dc, cf);
    } else {
        tcg_gen_movi_i32(cf, 0);
    }

    if (dc->rd) {
        TCGv_i32 ncf = tcg_temp_new_i32();
        gen_helper_carry(ncf, cpu_R[dc->ra], *(dec_alu_op_b(dc)), cf);
        tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
        tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->rd], cf);
        write_carry(dc, ncf);
        tcg_temp_free_i32(ncf);
    } else {
        gen_helper_carry(cf, cpu_R[dc->ra], *(dec_alu_op_b(dc)), cf);
        write_carry(dc, cf);
    }
    tcg_temp_free_i32(cf);
}

static void dec_sub(DisasContext *dc)
{
    unsigned int u, cmp, k, c;
    TCGv_i32 cf, na;

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
            tcg_gen_sub_i32(cpu_R[dc->rd], *(dec_alu_op_b(dc)), cpu_R[dc->ra]);

            if (c) {
                /* c - Add carry into the result.  */
                cf = tcg_temp_new_i32();

                read_carry(dc, cf);
                tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->rd], cf);
                tcg_temp_free_i32(cf);
            }
        }
        return;
    }

    /* From now on, we can assume k is zero.  So we need to update MSR.  */
    /* Extract carry. And complement a into na.  */
    cf = tcg_temp_new_i32();
    na = tcg_temp_new_i32();
    if (c) {
        read_carry(dc, cf);
    } else {
        tcg_gen_movi_i32(cf, 1);
    }

    /* d = b + ~a + c. carry defaults to 1.  */
    tcg_gen_not_i32(na, cpu_R[dc->ra]);

    if (dc->rd) {
        TCGv_i32 ncf = tcg_temp_new_i32();
        gen_helper_carry(ncf, na, *(dec_alu_op_b(dc)), cf);
        tcg_gen_add_i32(cpu_R[dc->rd], na, *(dec_alu_op_b(dc)));
        tcg_gen_add_i32(cpu_R[dc->rd], cpu_R[dc->rd], cf);
        write_carry(dc, ncf);
        tcg_temp_free_i32(ncf);
    } else {
        gen_helper_carry(cf, na, *(dec_alu_op_b(dc)), cf);
        write_carry(dc, cf);
    }
    tcg_temp_free_i32(cf);
    tcg_temp_free_i32(na);
}

static void dec_pattern(DisasContext *dc)
{
    unsigned int mode;

    if (trap_illegal(dc, !dc->cpu->cfg.use_pcmp_instr)) {
        return;
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
                tcg_gen_setcond_i32(TCG_COND_EQ, cpu_R[dc->rd],
                                   cpu_R[dc->ra], cpu_R[dc->rb]);
            }
            break;
        case 3:
            LOG_DIS("pcmpne r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            if (dc->rd) {
                tcg_gen_setcond_i32(TCG_COND_NE, cpu_R[dc->rd],
                                   cpu_R[dc->ra], cpu_R[dc->rb]);
            }
            break;
        default:
            cpu_abort(CPU(dc->cpu),
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
        tcg_gen_andc_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
    } else
        tcg_gen_and_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static void dec_or(DisasContext *dc)
{
    if (!dc->type_b && (dc->imm & (1 << 10))) {
        dec_pattern(dc);
        return;
    }

    LOG_DIS("or r%d r%d r%d imm=%x\n", dc->rd, dc->ra, dc->rb, dc->imm);
    if (dc->rd)
        tcg_gen_or_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static void dec_xor(DisasContext *dc)
{
    if (!dc->type_b && (dc->imm & (1 << 10))) {
        dec_pattern(dc);
        return;
    }

    LOG_DIS("xor r%d\n", dc->rd);
    if (dc->rd)
        tcg_gen_xor_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
}

static inline void msr_read(DisasContext *dc, TCGv_i32 d)
{
    tcg_gen_extrl_i64_i32(d, cpu_SR[SR_MSR]);
}

static inline void msr_write(DisasContext *dc, TCGv_i32 v)
{
    TCGv_i64 t;

    t = tcg_temp_new_i64();
    dc->cpustate_changed = 1;
    /* PVR bit is not writable.  */
    tcg_gen_extu_i32_i64(t, v);
    tcg_gen_andi_i64(t, t, ~MSR_PVR);
    tcg_gen_andi_i64(cpu_SR[SR_MSR], cpu_SR[SR_MSR], MSR_PVR);
    tcg_gen_or_i64(cpu_SR[SR_MSR], cpu_SR[SR_MSR], t);
    tcg_temp_free_i64(t);
}

static void dec_msr(DisasContext *dc)
{
    CPUState *cs = CPU(dc->cpu);
    TCGv_i32 t0, t1;
    unsigned int sr, rn;
    bool to, clrset, extended = false;

    sr = extract32(dc->imm, 0, 14);
    to = extract32(dc->imm, 14, 1);
    clrset = extract32(dc->imm, 15, 1) == 0;
    dc->type_b = 1;
    if (to) {
        dc->cpustate_changed = 1;
    }

    /* Extended MSRs are only available if addr_size > 32.  */
    if (dc->cpu->cfg.addr_size > 32) {
        /* The E-bit is encoded differently for To/From MSR.  */
        static const unsigned int e_bit[] = { 19, 24 };

        extended = extract32(dc->imm, e_bit[to], 1);
    }

    /* msrclr and msrset.  */
    if (clrset) {
        bool clr = extract32(dc->ir, 16, 1);

        LOG_DIS("msr%s r%d imm=%x\n", clr ? "clr" : "set",
                dc->rd, dc->imm);

        if (!dc->cpu->cfg.use_msr_instr) {
            /* nop??? */
            return;
        }

        if (trap_userspace(dc, dc->imm != 4 && dc->imm != 0)) {
            return;
        }

        if (dc->rd)
            msr_read(dc, cpu_R[dc->rd]);

        t0 = tcg_temp_new_i32();
        t1 = tcg_temp_new_i32();
        msr_read(dc, t0);
        tcg_gen_mov_i32(t1, *(dec_alu_op_b(dc)));

        if (clr) {
            tcg_gen_not_i32(t1, t1);
            tcg_gen_and_i32(t0, t0, t1);
        } else
            tcg_gen_or_i32(t0, t0, t1);
        msr_write(dc, t0);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
        tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc + 4);
        dc->is_jmp = DISAS_UPDATE;
        return;
    }

    if (trap_userspace(dc, to)) {
        return;
    }

#if !defined(CONFIG_USER_ONLY)
    /* Catch read/writes to the mmu block.  */
    if ((sr & ~0xff) == 0x1000) {
        TCGv_i32 tmp_ext = tcg_const_i32(extended);
        TCGv_i32 tmp_sr;

        sr &= 7;
        tmp_sr = tcg_const_i32(sr);
        LOG_DIS("m%ss sr%d r%d imm=%x\n", to ? "t" : "f", sr, dc->ra, dc->imm);
        if (to) {
            gen_helper_mmu_write(cpu_env, tmp_ext, tmp_sr, cpu_R[dc->ra]);
        } else {
            gen_helper_mmu_read(cpu_R[dc->rd], cpu_env, tmp_ext, tmp_sr);
        }
        tcg_temp_free_i32(tmp_sr);
        tcg_temp_free_i32(tmp_ext);
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
            case SR_EAR:
            case SR_ESR:
            case SR_FSR:
                tcg_gen_extu_i32_i64(cpu_SR[sr], cpu_R[dc->ra]);
                break;
            case 0x800:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, slr));
                break;
            case 0x802:
                tcg_gen_st_i32(cpu_R[dc->ra],
                               cpu_env, offsetof(CPUMBState, shr));
                break;
            default:
                cpu_abort(CPU(dc->cpu), "unknown mts reg %x\n", sr);
                break;
        }
    } else {
        LOG_DIS("m%ss r%d sr%x imm=%x\n", to ? "t" : "f", dc->rd, sr, dc->imm);

        switch (sr) {
            case 0:
                tcg_gen_movi_i32(cpu_R[dc->rd], dc->pc);
                break;
            case 1:
                msr_read(dc, cpu_R[dc->rd]);
                break;
            case SR_EAR:
                if (extended) {
                    tcg_gen_extrh_i64_i32(cpu_R[dc->rd], cpu_SR[sr]);
                    break;
                }
            case SR_ESR:
            case SR_FSR:
            case SR_BTR:
                tcg_gen_extrl_i64_i32(cpu_R[dc->rd], cpu_SR[sr]);
                break;
            case 0x800:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, slr));
                break;
            case 0x802:
                tcg_gen_ld_i32(cpu_R[dc->rd],
                               cpu_env, offsetof(CPUMBState, shr));
                break;
            case 0x2000 ... 0x200c:
                rn = sr & 0xf;
                tcg_gen_ld_i32(cpu_R[dc->rd],
                              cpu_env, offsetof(CPUMBState, pvr.regs[rn]));
                break;
            default:
                cpu_abort(cs, "unknown mfs reg %x\n", sr);
                break;
        }
    }

    if (dc->rd == 0) {
        tcg_gen_movi_i32(cpu_R[0], 0);
    }
}

/* Multiplier unit.  */
static void dec_mul(DisasContext *dc)
{
    TCGv_i32 tmp;
    unsigned int subcode;

    if (trap_illegal(dc, !dc->cpu->cfg.use_hw_mul)) {
        return;
    }

    subcode = dc->imm & 3;

    if (dc->type_b) {
        LOG_DIS("muli r%d r%d %x\n", dc->rd, dc->ra, dc->imm);
        tcg_gen_mul_i32(cpu_R[dc->rd], cpu_R[dc->ra], *(dec_alu_op_b(dc)));
        return;
    }

    /* mulh, mulhsu and mulhu are not available if C_USE_HW_MUL is < 2.  */
    if (subcode >= 1 && subcode <= 3 && dc->cpu->cfg.use_hw_mul < 2) {
        /* nop??? */
    }

    tmp = tcg_temp_new_i32();
    switch (subcode) {
        case 0:
            LOG_DIS("mul r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            tcg_gen_mul_i32(cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 1:
            LOG_DIS("mulh r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            tcg_gen_muls2_i32(tmp, cpu_R[dc->rd],
                              cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 2:
            LOG_DIS("mulhsu r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            tcg_gen_mulsu2_i32(tmp, cpu_R[dc->rd],
                               cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        case 3:
            LOG_DIS("mulhu r%d r%d r%d\n", dc->rd, dc->ra, dc->rb);
            tcg_gen_mulu2_i32(tmp, cpu_R[dc->rd], cpu_R[dc->ra], cpu_R[dc->rb]);
            break;
        default:
            cpu_abort(CPU(dc->cpu), "unknown MUL insn %x\n", subcode);
            break;
    }
    tcg_temp_free_i32(tmp);
}

/* Div unit.  */
static void dec_div(DisasContext *dc)
{
    unsigned int u;

    u = dc->imm & 2; 
    LOG_DIS("div\n");

    if (trap_illegal(dc, !dc->cpu->cfg.use_div)) {
        return;
    }

    if (u)
        gen_helper_divu(cpu_R[dc->rd], cpu_env, *(dec_alu_op_b(dc)),
                        cpu_R[dc->ra]);
    else
        gen_helper_divs(cpu_R[dc->rd], cpu_env, *(dec_alu_op_b(dc)),
                        cpu_R[dc->ra]);
    if (!dc->rd)
        tcg_gen_movi_i32(cpu_R[dc->rd], 0);
}

static void dec_barrel(DisasContext *dc)
{
    TCGv_i32 t0;
    unsigned int imm_w, imm_s;
    bool s, t, e = false, i = false;

    if (trap_illegal(dc, !dc->cpu->cfg.use_barrel)) {
        return;
    }

    if (dc->type_b) {
        /* Insert and extract are only available in immediate mode.  */
        i = extract32(dc->imm, 15, 1);
        e = extract32(dc->imm, 14, 1);
    }
    s = extract32(dc->imm, 10, 1);
    t = extract32(dc->imm, 9, 1);
    imm_w = extract32(dc->imm, 6, 5);
    imm_s = extract32(dc->imm, 0, 5);

    LOG_DIS("bs%s%s%s r%d r%d r%d\n",
            e ? "e" : "",
            s ? "l" : "r", t ? "a" : "l", dc->rd, dc->ra, dc->rb);

    if (e) {
        if (imm_w + imm_s > 32 || imm_w == 0) {
            /* These inputs have an undefined behavior.  */
            qemu_log_mask(LOG_GUEST_ERROR, "bsefi: Bad input w=%d s=%d\n",
                          imm_w, imm_s);
        } else {
            tcg_gen_extract_i32(cpu_R[dc->rd], cpu_R[dc->ra], imm_s, imm_w);
        }
    } else if (i) {
        int width = imm_w - imm_s + 1;

        if (imm_w < imm_s) {
            /* These inputs have an undefined behavior.  */
            qemu_log_mask(LOG_GUEST_ERROR, "bsifi: Bad input w=%d s=%d\n",
                          imm_w, imm_s);
        } else {
            tcg_gen_deposit_i32(cpu_R[dc->rd], cpu_R[dc->rd], cpu_R[dc->ra],
                                imm_s, width);
        }
    } else {
        t0 = tcg_temp_new_i32();

        tcg_gen_mov_i32(t0, *(dec_alu_op_b(dc)));
        tcg_gen_andi_i32(t0, t0, 31);

        if (s) {
            tcg_gen_shl_i32(cpu_R[dc->rd], cpu_R[dc->ra], t0);
        } else {
            if (t) {
                tcg_gen_sar_i32(cpu_R[dc->rd], cpu_R[dc->ra], t0);
            } else {
                tcg_gen_shr_i32(cpu_R[dc->rd], cpu_R[dc->ra], t0);
            }
        }
        tcg_temp_free_i32(t0);
    }
}

static void dec_bit(DisasContext *dc)
{
    CPUState *cs = CPU(dc->cpu);
    TCGv_i32 t0;
    unsigned int op;

    op = dc->ir & ((1 << 9) - 1);
    switch (op) {
        case 0x21:
            /* src.  */
            t0 = tcg_temp_new_i32();

            LOG_DIS("src r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_extrl_i64_i32(t0, cpu_SR[SR_MSR]);
            tcg_gen_andi_i32(t0, t0, MSR_CC);
            write_carry(dc, cpu_R[dc->ra]);
            if (dc->rd) {
                tcg_gen_shri_i32(cpu_R[dc->rd], cpu_R[dc->ra], 1);
                tcg_gen_or_i32(cpu_R[dc->rd], cpu_R[dc->rd], t0);
            }
            tcg_temp_free_i32(t0);
            break;

        case 0x1:
        case 0x41:
            /* srl.  */
            LOG_DIS("srl r%d r%d\n", dc->rd, dc->ra);

            /* Update carry. Note that write carry only looks at the LSB.  */
            write_carry(dc, cpu_R[dc->ra]);
            if (dc->rd) {
                if (op == 0x41)
                    tcg_gen_shri_i32(cpu_R[dc->rd], cpu_R[dc->ra], 1);
                else
                    tcg_gen_sari_i32(cpu_R[dc->rd], cpu_R[dc->ra], 1);
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
            trap_userspace(dc, true);
            break;
        case 0x68:
            /* wic.  */
            LOG_DIS("wic r%d\n", dc->ra);
            trap_userspace(dc, true);
            break;
        case 0xe0:
            if (trap_illegal(dc, !dc->cpu->cfg.use_pcmp_instr)) {
                return;
            }
            if (dc->cpu->cfg.use_pcmp_instr) {
                tcg_gen_clzi_i32(cpu_R[dc->rd], cpu_R[dc->ra], 32);
            }
            break;
        case 0x1e0:
            /* swapb */
            LOG_DIS("swapb r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_bswap32_i32(cpu_R[dc->rd], cpu_R[dc->ra]);
            break;
        case 0x1e2:
            /*swaph */
            LOG_DIS("swaph r%d r%d\n", dc->rd, dc->ra);
            tcg_gen_rotri_i32(cpu_R[dc->rd], cpu_R[dc->ra], 16);
            break;
        default:
            cpu_abort(cs, "unknown bit oc=%x op=%x rd=%d ra=%d rb=%d\n",
                      dc->pc, op, dc->rd, dc->ra, dc->rb);
            break;
    }
}

static inline void sync_jmpstate(DisasContext *dc)
{
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->jmp == JMP_DIRECT) {
            tcg_gen_movi_i32(env_btaken, 1);
        }
        dc->jmp = JMP_INDIRECT;
        tcg_gen_movi_i64(env_btarget, dc->jmp_pc);
    }
}

static void dec_imm(DisasContext *dc)
{
    LOG_DIS("imm %x\n", dc->imm << 16);
    tcg_gen_movi_i32(env_imm, (dc->imm << 16));
    dc->tb_flags |= IMM_FLAG;
    dc->clear_imm = 0;
}

static inline void compute_ldst_addr(DisasContext *dc, bool ea, TCGv t)
{
    bool extimm = dc->tb_flags & IMM_FLAG;
    /* Should be set to true if r1 is used by loadstores.  */
    bool stackprot = false;
    TCGv_i32 t32;

    /* All load/stores use ra.  */
    if (dc->ra == 1 && dc->cpu->cfg.stackprot) {
        stackprot = true;
    }

    /* Treat the common cases first.  */
    if (!dc->type_b) {
        if (ea) {
            int addr_size = dc->cpu->cfg.addr_size;

            if (addr_size == 32) {
                tcg_gen_extu_i32_tl(t, cpu_R[dc->rb]);
                return;
            }

            tcg_gen_concat_i32_i64(t, cpu_R[dc->rb], cpu_R[dc->ra]);
            if (addr_size < 64) {
                /* Mask off out of range bits.  */
                tcg_gen_andi_i64(t, t, MAKE_64BIT_MASK(0, addr_size));
            }
            return;
        }

        /* If any of the regs is r0, set t to the value of the other reg.  */
        if (dc->ra == 0) {
            tcg_gen_extu_i32_tl(t, cpu_R[dc->rb]);
            return;
        } else if (dc->rb == 0) {
            tcg_gen_extu_i32_tl(t, cpu_R[dc->ra]);
            return;
        }

        if (dc->rb == 1 && dc->cpu->cfg.stackprot) {
            stackprot = true;
        }

        t32 = tcg_temp_new_i32();
        tcg_gen_add_i32(t32, cpu_R[dc->ra], cpu_R[dc->rb]);
        tcg_gen_extu_i32_tl(t, t32);
        tcg_temp_free_i32(t32);

        if (stackprot) {
            gen_helper_stackprot(cpu_env, t);
        }
        return;
    }
    /* Immediate.  */
    t32 = tcg_temp_new_i32();
    if (!extimm) {
        tcg_gen_addi_i32(t32, cpu_R[dc->ra], (int16_t)dc->imm);
    } else {
        tcg_gen_add_i32(t32, cpu_R[dc->ra], *(dec_alu_op_b(dc)));
    }
    tcg_gen_extu_i32_tl(t, t32);
    tcg_temp_free_i32(t32);

    if (stackprot) {
        gen_helper_stackprot(cpu_env, t);
    }
    return;
}

static void dec_load(DisasContext *dc)
{
    TCGv_i32 v;
    TCGv addr;
    unsigned int size;
    bool rev = false, ex = false, ea = false;
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);
    TCGMemOp mop;

    mop = dc->opcode & 3;
    size = 1 << mop;
    if (!dc->type_b) {
        ea = extract32(dc->ir, 7, 1);
        rev = extract32(dc->ir, 9, 1);
        ex = extract32(dc->ir, 10, 1);
    }
    mop |= MO_TE;
    if (rev) {
        mop ^= MO_BSWAP;
    }

    if (trap_illegal(dc, size > 4)) {
        return;
    }

    if (trap_userspace(dc, ea)) {
        return;
    }

    LOG_DIS("l%d%s%s%s%s\n", size, dc->type_b ? "i" : "", rev ? "r" : "",
                                                        ex ? "x" : "",
                                                        ea ? "ea" : "");

    t_sync_flags(dc);
    addr = tcg_temp_new();
    compute_ldst_addr(dc, ea, addr);
    /* Extended addressing bypasses the MMU.  */
    mem_index = ea ? MMU_NOMMU_IDX : mem_index;

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

                tcg_gen_andi_tl(low, addr, 3);
                tcg_gen_sub_tl(low, tcg_const_tl(3), low);
                tcg_gen_andi_tl(addr, addr, ~3);
                tcg_gen_or_tl(addr, addr, low);
                tcg_temp_free(low);
                break;
            }

            case 2:
                /* 00 -> 10
                   10 -> 00.  */
                tcg_gen_xori_tl(addr, addr, 2);
                break;
            default:
                cpu_abort(CPU(dc->cpu), "Invalid reverse size\n");
                break;
        }
    }

    /* lwx does not throw unaligned access errors, so force alignment */
    if (ex) {
        tcg_gen_andi_tl(addr, addr, ~3);
    }

    /* If we get a fault on a dslot, the jmpstate better be in sync.  */
    sync_jmpstate(dc);

    /* Verify alignment if needed.  */
    /*
     * Microblaze gives MMU faults priority over faults due to
     * unaligned addresses. That's why we speculatively do the load
     * into v. If the load succeeds, we verify alignment of the
     * address and if that succeeds we write into the destination reg.
     */
    v = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(v, addr, mem_index, mop);

    if ((dc->cpu->env.pvr.regs[2] & PVR2_UNALIGNED_EXC_MASK) && size > 1) {
        tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc);
        gen_helper_memalign(cpu_env, addr, tcg_const_i32(dc->rd),
                            tcg_const_i32(0), tcg_const_i32(size - 1));
    }

    if (ex) {
        tcg_gen_mov_tl(env_res_addr, addr);
        tcg_gen_mov_i32(env_res_val, v);
    }
    if (dc->rd) {
        tcg_gen_mov_i32(cpu_R[dc->rd], v);
    }
    tcg_temp_free_i32(v);

    if (ex) { /* lwx */
        /* no support for AXI exclusive so always clear C */
        write_carryi(dc, 0);
    }

    tcg_temp_free(addr);
}

static void dec_store(DisasContext *dc)
{
    TCGv addr;
    TCGLabel *swx_skip = NULL;
    unsigned int size;
    bool rev = false, ex = false, ea = false;
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);
    TCGMemOp mop;

    mop = dc->opcode & 3;
    size = 1 << mop;
    if (!dc->type_b) {
        ea = extract32(dc->ir, 7, 1);
        rev = extract32(dc->ir, 9, 1);
        ex = extract32(dc->ir, 10, 1);
    }
    mop |= MO_TE;
    if (rev) {
        mop ^= MO_BSWAP;
    }

    if (trap_illegal(dc, size > 4)) {
        return;
    }

    trap_userspace(dc, ea);

    LOG_DIS("s%d%s%s%s%s\n", size, dc->type_b ? "i" : "", rev ? "r" : "",
                                                        ex ? "x" : "",
                                                        ea ? "ea" : "");
    t_sync_flags(dc);
    /* If we get a fault on a dslot, the jmpstate better be in sync.  */
    sync_jmpstate(dc);
    /* SWX needs a temp_local.  */
    addr = ex ? tcg_temp_local_new() : tcg_temp_new();
    compute_ldst_addr(dc, ea, addr);
    /* Extended addressing bypasses the MMU.  */
    mem_index = ea ? MMU_NOMMU_IDX : mem_index;

    if (ex) { /* swx */
        TCGv_i32 tval;

        /* swx does not throw unaligned access errors, so force alignment */
        tcg_gen_andi_tl(addr, addr, ~3);

        write_carryi(dc, 1);
        swx_skip = gen_new_label();
        tcg_gen_brcond_tl(TCG_COND_NE, env_res_addr, addr, swx_skip);

        /* Compare the value loaded at lwx with current contents of
           the reserved location.
           FIXME: This only works for system emulation where we can expect
           this compare and the following write to be atomic. For user
           emulation we need to add atomicity between threads.  */
        tval = tcg_temp_new_i32();
        tcg_gen_qemu_ld_i32(tval, addr, cpu_mmu_index(&dc->cpu->env, false),
                            MO_TEUL);
        tcg_gen_brcond_i32(TCG_COND_NE, env_res_val, tval, swx_skip);
        write_carryi(dc, 0);
        tcg_temp_free_i32(tval);
    }

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

                tcg_gen_andi_tl(low, addr, 3);
                tcg_gen_sub_tl(low, tcg_const_tl(3), low);
                tcg_gen_andi_tl(addr, addr, ~3);
                tcg_gen_or_tl(addr, addr, low);
                tcg_temp_free(low);
                break;
            }

            case 2:
                /* 00 -> 10
                   10 -> 00.  */
                /* Force addr into the temp.  */
                tcg_gen_xori_tl(addr, addr, 2);
                break;
            default:
                cpu_abort(CPU(dc->cpu), "Invalid reverse size\n");
                break;
        }
    }
    tcg_gen_qemu_st_i32(cpu_R[dc->rd], addr, mem_index, mop);

    /* Verify alignment if needed.  */
    if ((dc->cpu->env.pvr.regs[2] & PVR2_UNALIGNED_EXC_MASK) && size > 1) {
        tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc);
        /* FIXME: if the alignment is wrong, we should restore the value
         *        in memory. One possible way to achieve this is to probe
         *        the MMU prior to the memaccess, thay way we could put
         *        the alignment checks in between the probe and the mem
         *        access.
         */
        gen_helper_memalign(cpu_env, addr, tcg_const_i32(dc->rd),
                            tcg_const_i32(1), tcg_const_i32(size - 1));
    }

    if (ex) {
        gen_set_label(swx_skip);
    }

    tcg_temp_free(addr);
}

static inline void eval_cc(DisasContext *dc, unsigned int cc,
                           TCGv_i32 d, TCGv_i32 a)
{
    static const int mb_to_tcg_cc[] = {
        [CC_EQ] = TCG_COND_EQ,
        [CC_NE] = TCG_COND_NE,
        [CC_LT] = TCG_COND_LT,
        [CC_LE] = TCG_COND_LE,
        [CC_GE] = TCG_COND_GE,
        [CC_GT] = TCG_COND_GT,
    };

    switch (cc) {
    case CC_EQ:
    case CC_NE:
    case CC_LT:
    case CC_LE:
    case CC_GE:
    case CC_GT:
        tcg_gen_setcondi_i32(mb_to_tcg_cc[cc], d, a, 0);
        break;
    default:
        cpu_abort(CPU(dc->cpu), "Unknown condition code %x.\n", cc);
        break;
    }
}

static void eval_cond_jmp(DisasContext *dc, TCGv_i64 pc_true, TCGv_i64 pc_false)
{
    TCGv_i64 tmp_btaken = tcg_temp_new_i64();
    TCGv_i64 tmp_zero = tcg_const_i64(0);

    tcg_gen_extu_i32_i64(tmp_btaken, env_btaken);
    tcg_gen_movcond_i64(TCG_COND_NE, cpu_SR[SR_PC],
                        tmp_btaken, tmp_zero,
                        pc_true, pc_false);

    tcg_temp_free_i64(tmp_btaken);
    tcg_temp_free_i64(tmp_zero);
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
        tcg_gen_st_i32(tcg_const_i32(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                      cpu_env, offsetof(CPUMBState, bimm));
    }

    if (dec_alu_op_b_is_small_imm(dc)) {
        int32_t offset = (int32_t)((int16_t)dc->imm); /* sign-extend.  */

        tcg_gen_movi_i64(env_btarget, dc->pc + offset);
        dc->jmp = JMP_DIRECT_CC;
        dc->jmp_pc = dc->pc + offset;
    } else {
        dc->jmp = JMP_INDIRECT;
        tcg_gen_extu_i32_i64(env_btarget, *(dec_alu_op_b(dc)));
        tcg_gen_addi_i64(env_btarget, env_btarget, dc->pc);
        tcg_gen_andi_i64(env_btarget, env_btarget, UINT32_MAX);
    }
    eval_cc(dc, cc, env_btaken, cpu_R[dc->ra]);
}

static void dec_br(DisasContext *dc)
{
    unsigned int dslot, link, abs, mbar;

    dslot = dc->ir & (1 << 20);
    abs = dc->ir & (1 << 19);
    link = dc->ir & (1 << 18);

    /* Memory barrier.  */
    mbar = (dc->ir >> 16) & 31;
    if (mbar == 2 && dc->imm == 4) {
        /* mbar IMM & 16 decodes to sleep.  */
        if (dc->rd & 16) {
            TCGv_i32 tmp_hlt = tcg_const_i32(EXCP_HLT);
            TCGv_i32 tmp_1 = tcg_const_i32(1);

            LOG_DIS("sleep\n");

            t_sync_flags(dc);
            tcg_gen_st_i32(tmp_1, cpu_env,
                           -offsetof(MicroBlazeCPU, env)
                           +offsetof(CPUState, halted));
            tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc + 4);
            gen_helper_raise_exception(cpu_env, tmp_hlt);
            tcg_temp_free_i32(tmp_hlt);
            tcg_temp_free_i32(tmp_1);
            return;
        }
        LOG_DIS("mbar %d\n", dc->rd);
        /* Break the TB.  */
        dc->cpustate_changed = 1;
        return;
    }

    LOG_DIS("br%s%s%s%s imm=%x\n",
             abs ? "a" : "", link ? "l" : "",
             dc->type_b ? "i" : "", dslot ? "d" : "",
             dc->imm);

    dc->delayed_branch = 1;
    if (dslot) {
        dc->delayed_branch = 2;
        dc->tb_flags |= D_FLAG;
        tcg_gen_st_i32(tcg_const_i32(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                      cpu_env, offsetof(CPUMBState, bimm));
    }
    if (link && dc->rd)
        tcg_gen_movi_i32(cpu_R[dc->rd], dc->pc);

    dc->jmp = JMP_INDIRECT;
    if (abs) {
        tcg_gen_movi_i32(env_btaken, 1);
        tcg_gen_extu_i32_i64(env_btarget, *(dec_alu_op_b(dc)));
        if (link && !dslot) {
            if (!(dc->tb_flags & IMM_FLAG) && (dc->imm == 8 || dc->imm == 0x18))
                t_gen_raise_exception(dc, EXCP_BREAK);
            if (dc->imm == 0) {
                if (trap_userspace(dc, true)) {
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
            tcg_gen_movi_i32(env_btaken, 1);
            tcg_gen_extu_i32_i64(env_btarget, *(dec_alu_op_b(dc)));
            tcg_gen_addi_i64(env_btarget, env_btarget, dc->pc);
            tcg_gen_andi_i64(env_btarget, env_btarget, UINT32_MAX);
        }
    }
}

static inline void do_rti(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t1, cpu_SR[SR_MSR]);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_ori_i32(t1, t1, MSR_IE);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTI_FLAG;
}

static inline void do_rtb(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t1, cpu_SR[SR_MSR]);
    tcg_gen_andi_i32(t1, t1, ~MSR_BIP);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTB_FLAG;
}

static inline void do_rte(DisasContext *dc)
{
    TCGv_i32 t0, t1;
    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t1, cpu_SR[SR_MSR]);
    tcg_gen_ori_i32(t1, t1, MSR_EE);
    tcg_gen_andi_i32(t1, t1, ~MSR_EIP);
    tcg_gen_shri_i32(t0, t1, 1);
    tcg_gen_andi_i32(t0, t0, (MSR_VM | MSR_UM));

    tcg_gen_andi_i32(t1, t1, ~(MSR_VM | MSR_UM));
    tcg_gen_or_i32(t1, t1, t0);
    msr_write(dc, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t0);
    dc->tb_flags &= ~DRTE_FLAG;
}

static void dec_rts(DisasContext *dc)
{
    unsigned int b_bit, i_bit, e_bit;
    TCGv_i64 tmp64;

    i_bit = dc->ir & (1 << 21);
    b_bit = dc->ir & (1 << 22);
    e_bit = dc->ir & (1 << 23);

    if (trap_userspace(dc, i_bit || b_bit || e_bit)) {
        return;
    }

    dc->delayed_branch = 2;
    dc->tb_flags |= D_FLAG;
    tcg_gen_st_i32(tcg_const_i32(dc->type_b && (dc->tb_flags & IMM_FLAG)),
                  cpu_env, offsetof(CPUMBState, bimm));

    if (i_bit) {
        LOG_DIS("rtid ir=%x\n", dc->ir);
        dc->tb_flags |= DRTI_FLAG;
    } else if (b_bit) {
        LOG_DIS("rtbd ir=%x\n", dc->ir);
        dc->tb_flags |= DRTB_FLAG;
    } else if (e_bit) {
        LOG_DIS("rted ir=%x\n", dc->ir);
        dc->tb_flags |= DRTE_FLAG;
    } else
        LOG_DIS("rts ir=%x\n", dc->ir);

    dc->jmp = JMP_INDIRECT;
    tcg_gen_movi_i32(env_btaken, 1);

    tmp64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(env_btarget, *(dec_alu_op_b(dc)));
    tcg_gen_extu_i32_i64(tmp64, cpu_R[dc->ra]);
    tcg_gen_add_i64(env_btarget, env_btarget, tmp64);
    tcg_gen_andi_i64(env_btarget, env_btarget, UINT32_MAX);
    tcg_temp_free_i64(tmp64);
}

static int dec_check_fpuv2(DisasContext *dc)
{
    if ((dc->cpu->cfg.use_fpu != 2) && (dc->tb_flags & MSR_EE_FLAG)) {
        tcg_gen_movi_i64(cpu_SR[SR_ESR], ESR_EC_FPU);
        t_gen_raise_exception(dc, EXCP_HW_EXCP);
    }
    return (dc->cpu->cfg.use_fpu == 2) ? 0 : PVR2_USE_FPU2_MASK;
}

static void dec_fpu(DisasContext *dc)
{
    unsigned int fpu_insn;

    if (trap_illegal(dc, !dc->cpu->cfg.use_fpu)) {
        return;
    }

    fpu_insn = (dc->ir >> 7) & 7;

    switch (fpu_insn) {
        case 0:
            gen_helper_fadd(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra],
                            cpu_R[dc->rb]);
            break;

        case 1:
            gen_helper_frsub(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra],
                             cpu_R[dc->rb]);
            break;

        case 2:
            gen_helper_fmul(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra],
                            cpu_R[dc->rb]);
            break;

        case 3:
            gen_helper_fdiv(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra],
                            cpu_R[dc->rb]);
            break;

        case 4:
            switch ((dc->ir >> 4) & 7) {
                case 0:
                    gen_helper_fcmp_un(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 1:
                    gen_helper_fcmp_lt(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 2:
                    gen_helper_fcmp_eq(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 3:
                    gen_helper_fcmp_le(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 4:
                    gen_helper_fcmp_gt(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 5:
                    gen_helper_fcmp_ne(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                case 6:
                    gen_helper_fcmp_ge(cpu_R[dc->rd], cpu_env,
                                       cpu_R[dc->ra], cpu_R[dc->rb]);
                    break;
                default:
                    qemu_log_mask(LOG_UNIMP,
                                  "unimplemented fcmp fpu_insn=%x pc=%x"
                                  " opc=%x\n",
                                  fpu_insn, dc->pc, dc->opcode);
                    dc->abort_at_next_insn = 1;
                    break;
            }
            break;

        case 5:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_flt(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra]);
            break;

        case 6:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_fint(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra]);
            break;

        case 7:
            if (!dec_check_fpuv2(dc)) {
                return;
            }
            gen_helper_fsqrt(cpu_R[dc->rd], cpu_env, cpu_R[dc->ra]);
            break;

        default:
            qemu_log_mask(LOG_UNIMP, "unimplemented FPU insn fpu_insn=%x pc=%x"
                          " opc=%x\n",
                          fpu_insn, dc->pc, dc->opcode);
            dc->abort_at_next_insn = 1;
            break;
    }
}

static void dec_null(DisasContext *dc)
{
    if (trap_illegal(dc, true)) {
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "unknown insn pc=%x opc=%x\n", dc->pc, dc->opcode);
    dc->abort_at_next_insn = 1;
}

/* Insns connected to FSL or AXI stream attached devices.  */
static void dec_stream(DisasContext *dc)
{
    TCGv_i32 t_id, t_ctrl;
    int ctrl;

    LOG_DIS("%s%s imm=%x\n", dc->rd ? "get" : "put",
            dc->type_b ? "" : "d", dc->imm);

    if (trap_userspace(dc, true)) {
        return;
    }

    t_id = tcg_temp_new_i32();
    if (dc->type_b) {
        tcg_gen_movi_i32(t_id, dc->imm & 0xf);
        ctrl = dc->imm >> 10;
    } else {
        tcg_gen_andi_i32(t_id, cpu_R[dc->rb], 0xf);
        ctrl = dc->imm >> 5;
    }

    t_ctrl = tcg_const_i32(ctrl);

    if (dc->rd == 0) {
        gen_helper_put(t_id, t_ctrl, cpu_R[dc->ra]);
    } else {
        gen_helper_get(cpu_R[dc->rd], t_id, t_ctrl);
    }
    tcg_temp_free_i32(t_id);
    tcg_temp_free_i32(t_ctrl);
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

static inline void decode(DisasContext *dc, uint32_t ir)
{
    int i;

    dc->ir = ir;
    LOG_DIS("%8.8x\t", dc->ir);

    if (ir == 0) {
        trap_illegal(dc, dc->cpu->env.pvr.regs[2] & PVR2_OPCODE_0x0_ILL_MASK);
        /* Don't decode nop/zero instructions any further.  */
        return;
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

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    CPUMBState *env = cs->env_ptr;
    MicroBlazeCPU *cpu = env_archcpu(env);
    uint32_t pc_start;
    struct DisasContext ctx;
    struct DisasContext *dc = &ctx;
    uint32_t page_start, org_flags;
    uint32_t npc;
    int num_insns;

    pc_start = tb->pc;
    dc->cpu = cpu;
    dc->tb = tb;
    org_flags = dc->synced_flags = dc->tb_flags = tb->flags;

    dc->is_jmp = DISAS_NEXT;
    dc->jmp = 0;
    dc->delayed_branch = !!(dc->tb_flags & D_FLAG);
    if (dc->delayed_branch) {
        dc->jmp = JMP_INDIRECT;
    }
    dc->pc = pc_start;
    dc->singlestep_enabled = cs->singlestep_enabled;
    dc->cpustate_changed = 0;
    dc->abort_at_next_insn = 0;

    if (pc_start & 3) {
        cpu_abort(cs, "Microblaze: unaligned PC=%x\n", pc_start);
    }

    page_start = pc_start & TARGET_PAGE_MASK;
    num_insns = 0;

    gen_tb_start(tb);
    do
    {
        tcg_gen_insn_start(dc->pc);
        num_insns++;

#if SIM_COMPAT
        if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
            tcg_gen_movi_i64(cpu_SR[SR_PC], dc->pc);
            gen_helper_debug();
        }
#endif

        if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
            t_gen_raise_exception(dc, EXCP_DEBUG);
            dc->is_jmp = DISAS_UPDATE;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            dc->pc += 4;
            break;
        }

        /* Pretty disas.  */
        LOG_DIS("%8.8x:\t", dc->pc);

        if (num_insns == max_insns && (tb_cflags(tb) & CF_LAST_IO)) {
            gen_io_start();
        }

        dc->clear_imm = 1;
        decode(dc, cpu_ldl_code(env, dc->pc));
        if (dc->clear_imm)
            dc->tb_flags &= ~IMM_FLAG;
        dc->pc += 4;

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
                    eval_cond_jmp(dc, env_btarget, tcg_const_i64(dc->pc));
                    dc->is_jmp = DISAS_JUMP;
                } else if (dc->jmp == JMP_DIRECT) {
                    t_sync_flags(dc);
                    gen_goto_tb(dc, 0, dc->jmp_pc);
                    dc->is_jmp = DISAS_TB_JUMP;
                } else if (dc->jmp == JMP_DIRECT_CC) {
                    TCGLabel *l1 = gen_new_label();
                    t_sync_flags(dc);
                    /* Conditional jmp.  */
                    tcg_gen_brcondi_i32(TCG_COND_NE, env_btaken, 0, l1);
                    gen_goto_tb(dc, 1, dc->pc);
                    gen_set_label(l1);
                    gen_goto_tb(dc, 0, dc->jmp_pc);

                    dc->is_jmp = DISAS_TB_JUMP;
                }
                break;
            }
        }
        if (cs->singlestep_enabled) {
            break;
        }
    } while (!dc->is_jmp && !dc->cpustate_changed
             && !tcg_op_buf_full()
             && !singlestep
             && (dc->pc - page_start < TARGET_PAGE_SIZE)
             && num_insns < max_insns);

    npc = dc->pc;
    if (dc->jmp == JMP_DIRECT || dc->jmp == JMP_DIRECT_CC) {
        if (dc->tb_flags & D_FLAG) {
            dc->is_jmp = DISAS_UPDATE;
            tcg_gen_movi_i64(cpu_SR[SR_PC], npc);
            sync_jmpstate(dc);
        } else
            npc = dc->jmp_pc;
    }

    if (tb_cflags(tb) & CF_LAST_IO)
        gen_io_end();
    /* Force an update if the per-tb cpu state has changed.  */
    if (dc->is_jmp == DISAS_NEXT
        && (dc->cpustate_changed || org_flags != dc->tb_flags)) {
        dc->is_jmp = DISAS_UPDATE;
        tcg_gen_movi_i64(cpu_SR[SR_PC], npc);
    }
    t_sync_flags(dc);

    if (unlikely(cs->singlestep_enabled)) {
        TCGv_i32 tmp = tcg_const_i32(EXCP_DEBUG);

        if (dc->is_jmp != DISAS_JUMP) {
            tcg_gen_movi_i64(cpu_SR[SR_PC], npc);
        }
        gen_helper_raise_exception(cpu_env, tmp);
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
                tcg_gen_exit_tb(NULL, 0);
                break;
            case DISAS_TB_JUMP:
                /* nothing more to generate */
                break;
        }
    }
    gen_tb_end(tb, num_insns);

    tb->size = dc->pc - pc_start;
    tb->icount = num_insns;

#ifdef DEBUG_DISAS
#if !SIM_COMPAT
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(pc_start)) {
        qemu_log_lock();
        qemu_log("--------------\n");
        log_target_disas(cs, pc_start, dc->pc - pc_start);
        qemu_log_unlock();
    }
#endif
#endif
    assert(!dc->abort_at_next_insn);
}

void mb_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    MicroBlazeCPU *cpu = MICROBLAZE_CPU(cs);
    CPUMBState *env = &cpu->env;
    int i;

    if (!env) {
        return;
    }

    qemu_fprintf(f, "IN: PC=%" PRIx64 " %s\n",
                 env->sregs[SR_PC], lookup_symbol(env->sregs[SR_PC]));
    qemu_fprintf(f, "rmsr=%" PRIx64 " resr=%" PRIx64 " rear=%" PRIx64 " "
                 "debug=%x imm=%x iflags=%x fsr=%" PRIx64 "\n",
                 env->sregs[SR_MSR], env->sregs[SR_ESR], env->sregs[SR_EAR],
                 env->debug, env->imm, env->iflags, env->sregs[SR_FSR]);
    qemu_fprintf(f, "btaken=%d btarget=%" PRIx64 " mode=%s(saved=%s) "
                 "eip=%d ie=%d\n",
                 env->btaken, env->btarget,
                 (env->sregs[SR_MSR] & MSR_UM) ? "user" : "kernel",
                 (env->sregs[SR_MSR] & MSR_UMS) ? "user" : "kernel",
                 (bool)(env->sregs[SR_MSR] & MSR_EIP),
                 (bool)(env->sregs[SR_MSR] & MSR_IE));

    for (i = 0; i < 32; i++) {
        qemu_fprintf(f, "r%2.2d=%8.8x ", i, env->regs[i]);
        if ((i + 1) % 4 == 0)
            qemu_fprintf(f, "\n");
        }
    qemu_fprintf(f, "\n\n");
}

void mb_tcg_init(void)
{
    int i;

    env_debug = tcg_global_mem_new_i32(cpu_env,
                    offsetof(CPUMBState, debug),
                    "debug0");
    env_iflags = tcg_global_mem_new_i32(cpu_env,
                    offsetof(CPUMBState, iflags),
                    "iflags");
    env_imm = tcg_global_mem_new_i32(cpu_env,
                    offsetof(CPUMBState, imm),
                    "imm");
    env_btarget = tcg_global_mem_new_i64(cpu_env,
                     offsetof(CPUMBState, btarget),
                     "btarget");
    env_btaken = tcg_global_mem_new_i32(cpu_env,
                     offsetof(CPUMBState, btaken),
                     "btaken");
    env_res_addr = tcg_global_mem_new(cpu_env,
                     offsetof(CPUMBState, res_addr),
                     "res_addr");
    env_res_val = tcg_global_mem_new_i32(cpu_env,
                     offsetof(CPUMBState, res_val),
                     "res_val");
    for (i = 0; i < ARRAY_SIZE(cpu_R); i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
                          offsetof(CPUMBState, regs[i]),
                          regnames[i]);
    }
    for (i = 0; i < ARRAY_SIZE(cpu_SR); i++) {
        cpu_SR[i] = tcg_global_mem_new_i64(cpu_env,
                          offsetof(CPUMBState, sregs[i]),
                          special_regnames[i]);
    }
}

void restore_state_to_opc(CPUMBState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->sregs[SR_PC] = data[0];
}
