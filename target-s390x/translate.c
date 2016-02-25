/*
 *  S/390 translation
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2010 Alexander Graf
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

/* #define DEBUG_INLINE_BRANCHES */
#define S390X_DEBUG_DISAS
/* #define S390X_DEBUG_DISAS_VERBOSE */

#ifdef S390X_DEBUG_DISAS_VERBOSE
#  define LOG_DISAS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "exec/cpu_ldst.h"

/* global register indexes */
static TCGv_env cpu_env;

#include "exec/gen-icount.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"


/* Information that (most) every instruction needs to manipulate.  */
typedef struct DisasContext DisasContext;
typedef struct DisasInsn DisasInsn;
typedef struct DisasFields DisasFields;

struct DisasContext {
    struct TranslationBlock *tb;
    const DisasInsn *insn;
    DisasFields *fields;
    uint64_t pc, next_pc;
    enum cc_op cc_op;
    bool singlestep_enabled;
};

/* Information carried about a condition to be evaluated.  */
typedef struct {
    TCGCond cond:8;
    bool is_64;
    bool g1;
    bool g2;
    union {
        struct { TCGv_i64 a, b; } s64;
        struct { TCGv_i32 a, b; } s32;
    } u;
} DisasCompare;

#define DISAS_EXCP 4

#ifdef DEBUG_INLINE_BRANCHES
static uint64_t inline_branch_hit[CC_OP_MAX];
static uint64_t inline_branch_miss[CC_OP_MAX];
#endif

static uint64_t pc_to_link_info(DisasContext *s, uint64_t pc)
{
    if (!(s->tb->flags & FLAG_MASK_64)) {
        if (s->tb->flags & FLAG_MASK_32) {
            return pc | 0x80000000;
        }
    }
    return pc;
}

void s390_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    int i;

    if (env->cc_op > 3) {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %15s\n",
                    env->psw.mask, env->psw.addr, cc_name(env->cc_op));
    } else {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %02x\n",
                    env->psw.mask, env->psw.addr, env->cc_op);
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "F%02d=%016" PRIx64, i, get_freg(env, i)->ll);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "V%02d=%016" PRIx64 "%016" PRIx64, i,
                    env->vregs[i][0].ll, env->vregs[i][1].ll);
        cpu_fprintf(f, (i % 2) ? "\n" : " ");
    }

#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "C%02d=%016" PRIx64, i, env->cregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
#endif

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        cpu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                    inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif

    cpu_fprintf(f, "\n");
}

static TCGv_i64 psw_addr;
static TCGv_i64 psw_mask;
static TCGv_i64 gbea;

static TCGv_i32 cc_op;
static TCGv_i64 cc_src;
static TCGv_i64 cc_dst;
static TCGv_i64 cc_vr;

static char cpu_reg_names[32][4];
static TCGv_i64 regs[16];
static TCGv_i64 fregs[16];

void s390x_translate_init(void)
{
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    psw_addr = tcg_global_mem_new_i64(cpu_env,
                                      offsetof(CPUS390XState, psw.addr),
                                      "psw_addr");
    psw_mask = tcg_global_mem_new_i64(cpu_env,
                                      offsetof(CPUS390XState, psw.mask),
                                      "psw_mask");
    gbea = tcg_global_mem_new_i64(cpu_env,
                                  offsetof(CPUS390XState, gbea),
                                  "gbea");

    cc_op = tcg_global_mem_new_i32(cpu_env, offsetof(CPUS390XState, cc_op),
                                   "cc_op");
    cc_src = tcg_global_mem_new_i64(cpu_env, offsetof(CPUS390XState, cc_src),
                                    "cc_src");
    cc_dst = tcg_global_mem_new_i64(cpu_env, offsetof(CPUS390XState, cc_dst),
                                    "cc_dst");
    cc_vr = tcg_global_mem_new_i64(cpu_env, offsetof(CPUS390XState, cc_vr),
                                   "cc_vr");

    for (i = 0; i < 16; i++) {
        snprintf(cpu_reg_names[i], sizeof(cpu_reg_names[0]), "r%d", i);
        regs[i] = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUS390XState, regs[i]),
                                     cpu_reg_names[i]);
    }

    for (i = 0; i < 16; i++) {
        snprintf(cpu_reg_names[i + 16], sizeof(cpu_reg_names[0]), "f%d", i);
        fregs[i] = tcg_global_mem_new(cpu_env,
                                      offsetof(CPUS390XState, vregs[i][0].d),
                                      cpu_reg_names[i + 16]);
    }
}

static TCGv_i64 load_reg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_mov_i64(r, regs[reg]);
    return r;
}

static TCGv_i64 load_freg32_i64(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_shri_i64(r, fregs[reg], 32);
    return r;
}

static void store_reg(int reg, TCGv_i64 v)
{
    tcg_gen_mov_i64(regs[reg], v);
}

static void store_freg(int reg, TCGv_i64 v)
{
    tcg_gen_mov_i64(fregs[reg], v);
}

static void store_reg32_i64(int reg, TCGv_i64 v)
{
    /* 32 bit register writes keep the upper half */
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 0, 32);
}

static void store_reg32h_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 32, 32);
}

static void store_freg32_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(fregs[reg], fregs[reg], v, 32, 32);
}

static void return_low128(TCGv_i64 dest)
{
    tcg_gen_ld_i64(dest, cpu_env, offsetof(CPUS390XState, retxl));
}

static void update_psw_addr(DisasContext *s)
{
    /* psw.addr */
    tcg_gen_movi_i64(psw_addr, s->pc);
}

static void per_branch(DisasContext *s, bool to_next)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_i64(gbea, s->pc);

    if (s->tb->flags & FLAG_MASK_PER) {
        TCGv_i64 next_pc = to_next ? tcg_const_i64(s->next_pc) : psw_addr;
        gen_helper_per_branch(cpu_env, gbea, next_pc);
        if (to_next) {
            tcg_temp_free_i64(next_pc);
        }
    }
#endif
}

static void per_branch_cond(DisasContext *s, TCGCond cond,
                            TCGv_i64 arg1, TCGv_i64 arg2)
{
#ifndef CONFIG_USER_ONLY
    if (s->tb->flags & FLAG_MASK_PER) {
        TCGLabel *lab = gen_new_label();
        tcg_gen_brcond_i64(tcg_invert_cond(cond), arg1, arg2, lab);

        tcg_gen_movi_i64(gbea, s->pc);
        gen_helper_per_branch(cpu_env, gbea, psw_addr);

        gen_set_label(lab);
    } else {
        TCGv_i64 pc = tcg_const_i64(s->pc);
        tcg_gen_movcond_i64(cond, gbea, arg1, arg2, gbea, pc);
        tcg_temp_free_i64(pc);
    }
#endif
}

static void per_breaking_event(DisasContext *s)
{
    tcg_gen_movi_i64(gbea, s->pc);
}

static void update_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC && s->cc_op != CC_OP_STATIC) {
        tcg_gen_movi_i32(cc_op, s->cc_op);
    }
}

static void potential_page_fault(DisasContext *s)
{
    update_psw_addr(s);
    update_cc_op(s);
}

static inline uint64_t ld_code2(CPUS390XState *env, uint64_t pc)
{
    return (uint64_t)cpu_lduw_code(env, pc);
}

static inline uint64_t ld_code4(CPUS390XState *env, uint64_t pc)
{
    return (uint64_t)(uint32_t)cpu_ldl_code(env, pc);
}

static int get_mem_index(DisasContext *s)
{
    switch (s->tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> 32:
        return 0;
    case PSW_ASC_SECONDARY >> 32:
        return 1;
    case PSW_ASC_HOME >> 32:
        return 2;
    default:
        tcg_abort();
        break;
    }
}

static void gen_exception(int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_program_exception(DisasContext *s, int code)
{
    TCGv_i32 tmp;

    /* Remember what pgm exeption this was.  */
    tmp = tcg_const_i32(code);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUS390XState, int_pgm_code));
    tcg_temp_free_i32(tmp);

    tmp = tcg_const_i32(s->next_pc - s->pc);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUS390XState, int_pgm_ilen));
    tcg_temp_free_i32(tmp);

    /* Advance past instruction.  */
    s->pc = s->next_pc;
    update_psw_addr(s);

    /* Save off cc.  */
    update_cc_op(s);

    /* Trigger exception.  */
    gen_exception(EXCP_PGM);
}

static inline void gen_illegal_opcode(DisasContext *s)
{
    gen_program_exception(s, PGM_OPERATION);
}

static inline void gen_trap(DisasContext *s)
{
    TCGv_i32 t;

    /* Set DXC to 0xff.  */
    t = tcg_temp_new_i32();
    tcg_gen_ld_i32(t, cpu_env, offsetof(CPUS390XState, fpc));
    tcg_gen_ori_i32(t, t, 0xff00);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, fpc));
    tcg_temp_free_i32(t);

    gen_program_exception(s, PGM_DATA);
}

#ifndef CONFIG_USER_ONLY
static void check_privileged(DisasContext *s)
{
    if (s->tb->flags & (PSW_MASK_PSTATE >> 32)) {
        gen_program_exception(s, PGM_PRIVILEGED);
    }
}
#endif

static TCGv_i64 get_address(DisasContext *s, int x2, int b2, int d2)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    bool need_31 = !(s->tb->flags & FLAG_MASK_64);

    /* Note that d2 is limited to 20 bits, signed.  If we crop negative
       displacements early we create larger immedate addends.  */

    /* Note that addi optimizes the imm==0 case.  */
    if (b2 && x2) {
        tcg_gen_add_i64(tmp, regs[b2], regs[x2]);
        tcg_gen_addi_i64(tmp, tmp, d2);
    } else if (b2) {
        tcg_gen_addi_i64(tmp, regs[b2], d2);
    } else if (x2) {
        tcg_gen_addi_i64(tmp, regs[x2], d2);
    } else {
        if (need_31) {
            d2 &= 0x7fffffff;
            need_31 = false;
        }
        tcg_gen_movi_i64(tmp, d2);
    }
    if (need_31) {
        tcg_gen_andi_i64(tmp, tmp, 0x7fffffff);
    }

    return tmp;
}

static inline bool live_cc_data(DisasContext *s)
{
    return (s->cc_op != CC_OP_DYNAMIC
            && s->cc_op != CC_OP_STATIC
            && s->cc_op > 3);
}

static inline void gen_op_movi_cc(DisasContext *s, uint32_t val)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_dst);
        tcg_gen_discard_i64(cc_vr);
    }
    s->cc_op = CC_OP_CONST0 + val;
}

static void gen_op_update1_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 dst)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_vr);
    }
    tcg_gen_mov_i64(cc_dst, dst);
    s->cc_op = op;
}

static void gen_op_update2_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_vr);
    }
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    s->cc_op = op;
}

static void gen_op_update3_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst, TCGv_i64 vr)
{
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    tcg_gen_mov_i64(cc_vr, vr);
    s->cc_op = op;
}

static void set_cc_nz_u64(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, val);
}

static void gen_set_cc_nz_f32(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F32, val);
}

static void gen_set_cc_nz_f64(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F64, val);
}

static void gen_set_cc_nz_f128(DisasContext *s, TCGv_i64 vh, TCGv_i64 vl)
{
    gen_op_update2_cc_i64(s, CC_OP_NZ_F128, vh, vl);
}

/* CC value is in env->cc_op */
static void set_cc_static(DisasContext *s)
{
    if (live_cc_data(s)) {
        tcg_gen_discard_i64(cc_src);
        tcg_gen_discard_i64(cc_dst);
        tcg_gen_discard_i64(cc_vr);
    }
    s->cc_op = CC_OP_STATIC;
}

/* calculates cc into cc_op */
static void gen_op_calc_cc(DisasContext *s)
{
    TCGv_i32 local_cc_op;
    TCGv_i64 dummy;

    TCGV_UNUSED_I32(local_cc_op);
    TCGV_UNUSED_I64(dummy);
    switch (s->cc_op) {
    default:
        dummy = tcg_const_i64(0);
        /* FALLTHRU */
    case CC_OP_ADD_64:
    case CC_OP_ADDU_64:
    case CC_OP_ADDC_64:
    case CC_OP_SUB_64:
    case CC_OP_SUBU_64:
    case CC_OP_SUBB_64:
    case CC_OP_ADD_32:
    case CC_OP_ADDU_32:
    case CC_OP_ADDC_32:
    case CC_OP_SUB_32:
    case CC_OP_SUBU_32:
    case CC_OP_SUBB_32:
        local_cc_op = tcg_const_i32(s->cc_op);
        break;
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
    case CC_OP_STATIC:
    case CC_OP_DYNAMIC:
        break;
    }

    switch (s->cc_op) {
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
        /* s->cc_op is the cc value */
        tcg_gen_movi_i32(cc_op, s->cc_op - CC_OP_CONST0);
        break;
    case CC_OP_STATIC:
        /* env->cc_op already is the cc value */
        break;
    case CC_OP_NZ:
    case CC_OP_ABS_64:
    case CC_OP_NABS_64:
    case CC_OP_ABS_32:
    case CC_OP_NABS_32:
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_COMP_32:
    case CC_OP_COMP_64:
    case CC_OP_NZ_F32:
    case CC_OP_NZ_F64:
    case CC_OP_FLOGR:
        /* 1 argument */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, dummy, cc_dst, dummy);
        break;
    case CC_OP_ICM:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_SLA_32:
    case CC_OP_SLA_64:
    case CC_OP_NZ_F128:
        /* 2 arguments */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, cc_src, cc_dst, dummy);
        break;
    case CC_OP_ADD_64:
    case CC_OP_ADDU_64:
    case CC_OP_ADDC_64:
    case CC_OP_SUB_64:
    case CC_OP_SUBU_64:
    case CC_OP_SUBB_64:
    case CC_OP_ADD_32:
    case CC_OP_ADDU_32:
    case CC_OP_ADDC_32:
    case CC_OP_SUB_32:
    case CC_OP_SUBU_32:
    case CC_OP_SUBB_32:
        /* 3 arguments */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, cc_src, cc_dst, cc_vr);
        break;
    case CC_OP_DYNAMIC:
        /* unknown operation - assume 3 arguments and cc_op in env */
        gen_helper_calc_cc(cc_op, cpu_env, cc_op, cc_src, cc_dst, cc_vr);
        break;
    default:
        tcg_abort();
    }

    if (!TCGV_IS_UNUSED_I32(local_cc_op)) {
        tcg_temp_free_i32(local_cc_op);
    }
    if (!TCGV_IS_UNUSED_I64(dummy)) {
        tcg_temp_free_i64(dummy);
    }

    /* We now have cc in cc_op as constant */
    set_cc_static(s);
}

static int use_goto_tb(DisasContext *s, uint64_t dest)
{
    /* NOTE: we handle the case where the TB spans two pages here */
    return (((dest & TARGET_PAGE_MASK) == (s->tb->pc & TARGET_PAGE_MASK)
             || (dest & TARGET_PAGE_MASK) == ((s->pc - 1) & TARGET_PAGE_MASK))
            && !s->singlestep_enabled
            && !(s->tb->cflags & CF_LAST_IO)
            && !(s->tb->flags & FLAG_MASK_PER));
}

static void account_noninline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_miss[cc_op]++;
#endif
}

static void account_inline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_hit[cc_op]++;
#endif
}

/* Table of mask values to comparison codes, given a comparison as input.
   For such, CC=3 should not be possible.  */
static const TCGCond ltgt_cond[16] = {
    TCG_COND_NEVER,  TCG_COND_NEVER,     /*    |    |    | x */
    TCG_COND_GT,     TCG_COND_GT,        /*    |    | GT | x */
    TCG_COND_LT,     TCG_COND_LT,        /*    | LT |    | x */
    TCG_COND_NE,     TCG_COND_NE,        /*    | LT | GT | x */
    TCG_COND_EQ,     TCG_COND_EQ,        /* EQ |    |    | x */
    TCG_COND_GE,     TCG_COND_GE,        /* EQ |    | GT | x */
    TCG_COND_LE,     TCG_COND_LE,        /* EQ | LT |    | x */
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,    /* EQ | LT | GT | x */
};

/* Table of mask values to comparison codes, given a logic op as input.
   For such, only CC=0 and CC=1 should be possible.  */
static const TCGCond nz_cond[16] = {
    TCG_COND_NEVER, TCG_COND_NEVER,      /*    |    | x | x */
    TCG_COND_NEVER, TCG_COND_NEVER,
    TCG_COND_NE, TCG_COND_NE,            /*    | NE | x | x */
    TCG_COND_NE, TCG_COND_NE,
    TCG_COND_EQ, TCG_COND_EQ,            /* EQ |    | x | x */
    TCG_COND_EQ, TCG_COND_EQ,
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,    /* EQ | NE | x | x */
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,
};

/* Interpret MASK in terms of S->CC_OP, and fill in C with all the
   details required to generate a TCG comparison.  */
static void disas_jcc(DisasContext *s, DisasCompare *c, uint32_t mask)
{
    TCGCond cond;
    enum cc_op old_cc_op = s->cc_op;

    if (mask == 15 || mask == 0) {
        c->cond = (mask ? TCG_COND_ALWAYS : TCG_COND_NEVER);
        c->u.s32.a = cc_op;
        c->u.s32.b = cc_op;
        c->g1 = c->g2 = true;
        c->is_64 = false;
        return;
    }

    /* Find the TCG condition for the mask + cc op.  */
    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
        cond = ltgt_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
        cond = tcg_unsigned_cond(ltgt_cond[mask]);
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_NZ:
        cond = nz_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
        switch (mask) {
        case 8:
            cond = TCG_COND_EQ;
            break;
        case 4 | 2 | 1:
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_ICM:
        switch (mask) {
        case 8:
            cond = TCG_COND_EQ;
            break;
        case 4 | 2 | 1:
        case 4 | 2:
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_FLOGR:
        switch (mask & 0xa) {
        case 8: /* src == 0 -> no one bit found */
            cond = TCG_COND_EQ;
            break;
        case 2: /* src != 0 -> one bit found */
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_ADDU_32:
    case CC_OP_ADDU_64:
        switch (mask) {
        case 8 | 2: /* vr == 0 */
            cond = TCG_COND_EQ;
            break;
        case 4 | 1: /* vr != 0 */
            cond = TCG_COND_NE;
            break;
        case 8 | 4: /* no carry -> vr >= src */
            cond = TCG_COND_GEU;
            break;
        case 2 | 1: /* carry -> vr < src */
            cond = TCG_COND_LTU;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_SUBU_32:
    case CC_OP_SUBU_64:
        /* Note that CC=0 is impossible; treat it as dont-care.  */
        switch (mask & 7) {
        case 2: /* zero -> op1 == op2 */
            cond = TCG_COND_EQ;
            break;
        case 4 | 1: /* !zero -> op1 != op2 */
            cond = TCG_COND_NE;
            break;
        case 4: /* borrow (!carry) -> op1 < op2 */
            cond = TCG_COND_LTU;
            break;
        case 2 | 1: /* !borrow (carry) -> op1 >= op2 */
            cond = TCG_COND_GEU;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    default:
    do_dynamic:
        /* Calculate cc value.  */
        gen_op_calc_cc(s);
        /* FALLTHRU */

    case CC_OP_STATIC:
        /* Jump based on CC.  We'll load up the real cond below;
           the assignment here merely avoids a compiler warning.  */
        account_noninline_branch(s, old_cc_op);
        old_cc_op = CC_OP_STATIC;
        cond = TCG_COND_NEVER;
        break;
    }

    /* Load up the arguments of the comparison.  */
    c->is_64 = true;
    c->g1 = c->g2 = false;
    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.a, cc_dst);
        c->u.s32.b = tcg_const_i32(0);
        break;
    case CC_OP_LTGT_32:
    case CC_OP_LTUGTU_32:
    case CC_OP_SUBU_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.a, cc_src);
        c->u.s32.b = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.b, cc_dst);
        break;

    case CC_OP_LTGT0_64:
    case CC_OP_NZ:
    case CC_OP_FLOGR:
        c->u.s64.a = cc_dst;
        c->u.s64.b = tcg_const_i64(0);
        c->g1 = true;
        break;
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_64:
    case CC_OP_SUBU_64:
        c->u.s64.a = cc_src;
        c->u.s64.b = cc_dst;
        c->g1 = c->g2 = true;
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_ICM:
        c->u.s64.a = tcg_temp_new_i64();
        c->u.s64.b = tcg_const_i64(0);
        tcg_gen_and_i64(c->u.s64.a, cc_src, cc_dst);
        break;

    case CC_OP_ADDU_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        c->u.s32.b = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(c->u.s32.a, cc_vr);
        if (cond == TCG_COND_EQ || cond == TCG_COND_NE) {
            tcg_gen_movi_i32(c->u.s32.b, 0);
        } else {
            tcg_gen_extrl_i64_i32(c->u.s32.b, cc_src);
        }
        break;

    case CC_OP_ADDU_64:
        c->u.s64.a = cc_vr;
        c->g1 = true;
        if (cond == TCG_COND_EQ || cond == TCG_COND_NE) {
            c->u.s64.b = tcg_const_i64(0);
        } else {
            c->u.s64.b = cc_src;
            c->g2 = true;
        }
        break;

    case CC_OP_STATIC:
        c->is_64 = false;
        c->u.s32.a = cc_op;
        c->g1 = true;
        switch (mask) {
        case 0x8 | 0x4 | 0x2: /* cc != 3 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(3);
            break;
        case 0x8 | 0x4 | 0x1: /* cc != 2 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x8 | 0x2 | 0x1: /* cc != 1 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x8 | 0x2: /* cc == 0 || cc == 2 => (cc & 1) == 0 */
            cond = TCG_COND_EQ;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x8 | 0x4: /* cc < 2 */
            cond = TCG_COND_LTU;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x8: /* cc == 0 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(0);
            break;
        case 0x4 | 0x2 | 0x1: /* cc != 0 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(0);
            break;
        case 0x4 | 0x1: /* cc == 1 || cc == 3 => (cc & 1) != 0 */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x4: /* cc == 1 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x2 | 0x1: /* cc > 1 */
            cond = TCG_COND_GTU;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x2: /* cc == 2 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x1: /* cc == 3 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(3);
            break;
        default:
            /* CC is masked by something else: (8 >> cc) & mask.  */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_const_i32(8);
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_shr_i32(c->u.s32.a, c->u.s32.a, cc_op);
            tcg_gen_andi_i32(c->u.s32.a, c->u.s32.a, mask);
            break;
        }
        break;

    default:
        abort();
    }
    c->cond = cond;
}

static void free_compare(DisasCompare *c)
{
    if (!c->g1) {
        if (c->is_64) {
            tcg_temp_free_i64(c->u.s64.a);
        } else {
            tcg_temp_free_i32(c->u.s32.a);
        }
    }
    if (!c->g2) {
        if (c->is_64) {
            tcg_temp_free_i64(c->u.s64.b);
        } else {
            tcg_temp_free_i32(c->u.s32.b);
        }
    }
}

/* ====================================================================== */
/* Define the insn format enumeration.  */
#define F0(N)                         FMT_##N,
#define F1(N, X1)                     F0(N)
#define F2(N, X1, X2)                 F0(N)
#define F3(N, X1, X2, X3)             F0(N)
#define F4(N, X1, X2, X3, X4)         F0(N)
#define F5(N, X1, X2, X3, X4, X5)     F0(N)

typedef enum {
#include "insn-format.def"
} DisasFormat;

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5

/* Define a structure to hold the decoded fields.  We'll store each inside
   an array indexed by an enum.  In order to conserve memory, we'll arrange
   for fields that do not exist at the same time to overlap, thus the "C"
   for compact.  For checking purposes there is an "O" for original index
   as well that will be applied to availability bitmaps.  */

enum DisasFieldIndexO {
    FLD_O_r1,
    FLD_O_r2,
    FLD_O_r3,
    FLD_O_m1,
    FLD_O_m3,
    FLD_O_m4,
    FLD_O_b1,
    FLD_O_b2,
    FLD_O_b4,
    FLD_O_d1,
    FLD_O_d2,
    FLD_O_d4,
    FLD_O_x2,
    FLD_O_l1,
    FLD_O_l2,
    FLD_O_i1,
    FLD_O_i2,
    FLD_O_i3,
    FLD_O_i4,
    FLD_O_i5
};

enum DisasFieldIndexC {
    FLD_C_r1 = 0,
    FLD_C_m1 = 0,
    FLD_C_b1 = 0,
    FLD_C_i1 = 0,

    FLD_C_r2 = 1,
    FLD_C_b2 = 1,
    FLD_C_i2 = 1,

    FLD_C_r3 = 2,
    FLD_C_m3 = 2,
    FLD_C_i3 = 2,

    FLD_C_m4 = 3,
    FLD_C_b4 = 3,
    FLD_C_i4 = 3,
    FLD_C_l1 = 3,

    FLD_C_i5 = 4,
    FLD_C_d1 = 4,

    FLD_C_d2 = 5,

    FLD_C_d4 = 6,
    FLD_C_x2 = 6,
    FLD_C_l2 = 6,

    NUM_C_FIELD = 7
};

struct DisasFields {
    uint64_t raw_insn;
    unsigned op:8;
    unsigned op2:8;
    unsigned presentC:16;
    unsigned int presentO;
    int c[NUM_C_FIELD];
};

/* This is the way fields are to be accessed out of DisasFields.  */
#define have_field(S, F)  have_field1((S), FLD_O_##F)
#define get_field(S, F)   get_field1((S), FLD_O_##F, FLD_C_##F)

static bool have_field1(const DisasFields *f, enum DisasFieldIndexO c)
{
    return (f->presentO >> c) & 1;
}

static int get_field1(const DisasFields *f, enum DisasFieldIndexO o,
                      enum DisasFieldIndexC c)
{
    assert(have_field1(f, o));
    return f->c[c];
}

/* Describe the layout of each field in each format.  */
typedef struct DisasField {
    unsigned int beg:8;
    unsigned int size:8;
    unsigned int type:2;
    unsigned int indexC:6;
    enum DisasFieldIndexO indexO:8;
} DisasField;

typedef struct DisasFormatInfo {
    DisasField op[NUM_C_FIELD];
} DisasFormatInfo;

#define R(N, B)       {  B,  4, 0, FLD_C_r##N, FLD_O_r##N }
#define M(N, B)       {  B,  4, 0, FLD_C_m##N, FLD_O_m##N }
#define BD(N, BB, BD) { BB,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { BD, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BXD(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BDL(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define BXDL(N)       { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define I(N, B, S)    {  B,  S, 1, FLD_C_i##N, FLD_O_i##N }
#define L(N, B, S)    {  B,  S, 0, FLD_C_l##N, FLD_O_l##N }

#define F0(N)                     { { } },
#define F1(N, X1)                 { { X1 } },
#define F2(N, X1, X2)             { { X1, X2 } },
#define F3(N, X1, X2, X3)         { { X1, X2, X3 } },
#define F4(N, X1, X2, X3, X4)     { { X1, X2, X3, X4 } },
#define F5(N, X1, X2, X3, X4, X5) { { X1, X2, X3, X4, X5 } },

static const DisasFormatInfo format_info[] = {
#include "insn-format.def"
};

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef R
#undef M
#undef BD
#undef BXD
#undef BDL
#undef BXDL
#undef I
#undef L

/* Generally, we'll extract operands into this structures, operate upon
   them, and store them back.  See the "in1", "in2", "prep", "wout" sets
   of routines below for more details.  */
typedef struct {
    bool g_out, g_out2, g_in1, g_in2;
    TCGv_i64 out, out2, in1, in2;
    TCGv_i64 addr1;
} DisasOps;

/* Instructions can place constraints on their operands, raising specification
   exceptions if they are violated.  To make this easy to automate, each "in1",
   "in2", "prep", "wout" helper will have a SPEC_<name> define that equals one
   of the following, or 0.  To make this easy to document, we'll put the
   SPEC_<name> defines next to <name>.  */

#define SPEC_r1_even    1
#define SPEC_r2_even    2
#define SPEC_r3_even    4
#define SPEC_r1_f128    8
#define SPEC_r2_f128    16

/* Return values from translate_one, indicating the state of the TB.  */
typedef enum {
    /* Continue the TB.  */
    NO_EXIT,
    /* We have emitted one or more goto_tb.  No fixup required.  */
    EXIT_GOTO_TB,
    /* We are not using a goto_tb (for whatever reason), but have updated
       the PC (for whatever reason), so there's no need to do it again on
       exiting the TB.  */
    EXIT_PC_UPDATED,
    /* We are exiting the TB, but have neither emitted a goto_tb, nor
       updated the PC for the next instruction to be executed.  */
    EXIT_PC_STALE,
    /* We are ending the TB with a noreturn function call, e.g. longjmp.
       No following code will be executed.  */
    EXIT_NORETURN,
} ExitStatus;

typedef enum DisasFacility {
    FAC_Z,                  /* zarch (default) */
    FAC_CASS,               /* compare and swap and store */
    FAC_CASS2,              /* compare and swap and store 2*/
    FAC_DFP,                /* decimal floating point */
    FAC_DFPR,               /* decimal floating point rounding */
    FAC_DO,                 /* distinct operands */
    FAC_EE,                 /* execute extensions */
    FAC_EI,                 /* extended immediate */
    FAC_FPE,                /* floating point extension */
    FAC_FPSSH,              /* floating point support sign handling */
    FAC_FPRGR,              /* FPR-GR transfer */
    FAC_GIE,                /* general instructions extension */
    FAC_HFP_MA,             /* HFP multiply-and-add/subtract */
    FAC_HW,                 /* high-word */
    FAC_IEEEE_SIM,          /* IEEE exception sumilation */
    FAC_MIE,                /* miscellaneous-instruction-extensions */
    FAC_LAT,                /* load-and-trap */
    FAC_LOC,                /* load/store on condition */
    FAC_LD,                 /* long displacement */
    FAC_PC,                 /* population count */
    FAC_SCF,                /* store clock fast */
    FAC_SFLE,               /* store facility list extended */
    FAC_ILA,                /* interlocked access facility 1 */
} DisasFacility;

struct DisasInsn {
    unsigned opc:16;
    DisasFormat fmt:8;
    DisasFacility fac:8;
    unsigned spec:8;

    const char *name;

    void (*help_in1)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_in2)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_prep)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_wout)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_cout)(DisasContext *, DisasOps *);
    ExitStatus (*help_op)(DisasContext *, DisasOps *);

    uint64_t data;
};

/* ====================================================================== */
/* Miscellaneous helpers, used by several operations.  */

static void help_l2_shift(DisasContext *s, DisasFields *f,
                          DisasOps *o, int mask)
{
    int b2 = get_field(f, b2);
    int d2 = get_field(f, d2);

    if (b2 == 0) {
        o->in2 = tcg_const_i64(d2 & mask);
    } else {
        o->in2 = get_address(s, 0, b2, d2);
        tcg_gen_andi_i64(o->in2, o->in2, mask);
    }
}

static ExitStatus help_goto_direct(DisasContext *s, uint64_t dest)
{
    if (dest == s->next_pc) {
        per_branch(s, true);
        return NO_EXIT;
    }
    if (use_goto_tb(s, dest)) {
        update_cc_op(s);
        per_breaking_event(s);
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(psw_addr, dest);
        tcg_gen_exit_tb((uintptr_t)s->tb);
        return EXIT_GOTO_TB;
    } else {
        tcg_gen_movi_i64(psw_addr, dest);
        per_branch(s, false);
        return EXIT_PC_UPDATED;
    }
}

static ExitStatus help_branch(DisasContext *s, DisasCompare *c,
                              bool is_imm, int imm, TCGv_i64 cdest)
{
    ExitStatus ret;
    uint64_t dest = s->pc + 2 * imm;
    TCGLabel *lab;

    /* Take care of the special cases first.  */
    if (c->cond == TCG_COND_NEVER) {
        ret = NO_EXIT;
        goto egress;
    }
    if (is_imm) {
        if (dest == s->next_pc) {
            /* Branch to next.  */
            per_branch(s, true);
            ret = NO_EXIT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            ret = help_goto_direct(s, dest);
            goto egress;
        }
    } else {
        if (TCGV_IS_UNUSED_I64(cdest)) {
            /* E.g. bcr %r0 -> no branch.  */
            ret = NO_EXIT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            tcg_gen_mov_i64(psw_addr, cdest);
            per_branch(s, false);
            ret = EXIT_PC_UPDATED;
            goto egress;
        }
    }

    if (use_goto_tb(s, s->next_pc)) {
        if (is_imm && use_goto_tb(s, dest)) {
            /* Both exits can use goto_tb.  */
            update_cc_op(s);

            lab = gen_new_label();
            if (c->is_64) {
                tcg_gen_brcond_i64(c->cond, c->u.s64.a, c->u.s64.b, lab);
            } else {
                tcg_gen_brcond_i32(c->cond, c->u.s32.a, c->u.s32.b, lab);
            }

            /* Branch not taken.  */
            tcg_gen_goto_tb(0);
            tcg_gen_movi_i64(psw_addr, s->next_pc);
            tcg_gen_exit_tb((uintptr_t)s->tb + 0);

            /* Branch taken.  */
            gen_set_label(lab);
            per_breaking_event(s);
            tcg_gen_goto_tb(1);
            tcg_gen_movi_i64(psw_addr, dest);
            tcg_gen_exit_tb((uintptr_t)s->tb + 1);

            ret = EXIT_GOTO_TB;
        } else {
            /* Fallthru can use goto_tb, but taken branch cannot.  */
            /* Store taken branch destination before the brcond.  This
               avoids having to allocate a new local temp to hold it.
               We'll overwrite this in the not taken case anyway.  */
            if (!is_imm) {
                tcg_gen_mov_i64(psw_addr, cdest);
            }

            lab = gen_new_label();
            if (c->is_64) {
                tcg_gen_brcond_i64(c->cond, c->u.s64.a, c->u.s64.b, lab);
            } else {
                tcg_gen_brcond_i32(c->cond, c->u.s32.a, c->u.s32.b, lab);
            }

            /* Branch not taken.  */
            update_cc_op(s);
            tcg_gen_goto_tb(0);
            tcg_gen_movi_i64(psw_addr, s->next_pc);
            tcg_gen_exit_tb((uintptr_t)s->tb + 0);

            gen_set_label(lab);
            if (is_imm) {
                tcg_gen_movi_i64(psw_addr, dest);
            }
            per_breaking_event(s);
            ret = EXIT_PC_UPDATED;
        }
    } else {
        /* Fallthru cannot use goto_tb.  This by itself is vanishingly rare.
           Most commonly we're single-stepping or some other condition that
           disables all use of goto_tb.  Just update the PC and exit.  */

        TCGv_i64 next = tcg_const_i64(s->next_pc);
        if (is_imm) {
            cdest = tcg_const_i64(dest);
        }

        if (c->is_64) {
            tcg_gen_movcond_i64(c->cond, psw_addr, c->u.s64.a, c->u.s64.b,
                                cdest, next);
            per_branch_cond(s, c->cond, c->u.s64.a, c->u.s64.b);
        } else {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 z = tcg_const_i64(0);
            tcg_gen_setcond_i32(c->cond, t0, c->u.s32.a, c->u.s32.b);
            tcg_gen_extu_i32_i64(t1, t0);
            tcg_temp_free_i32(t0);
            tcg_gen_movcond_i64(TCG_COND_NE, psw_addr, t1, z, cdest, next);
            per_branch_cond(s, TCG_COND_NE, t1, z);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(z);
        }

        if (is_imm) {
            tcg_temp_free_i64(cdest);
        }
        tcg_temp_free_i64(next);

        ret = EXIT_PC_UPDATED;
    }

 egress:
    free_compare(c);
    return ret;
}

/* ====================================================================== */
/* The operations.  These perform the bulk of the work for any insn,
   usually after the operands have been loaded and output initialized.  */

static ExitStatus op_abs(DisasContext *s, DisasOps *o)
{
    TCGv_i64 z, n;
    z = tcg_const_i64(0);
    n = tcg_temp_new_i64();
    tcg_gen_neg_i64(n, o->in2);
    tcg_gen_movcond_i64(TCG_COND_LT, o->out, o->in2, z, n, o->in2);
    tcg_temp_free_i64(n);
    tcg_temp_free_i64(z);
    return NO_EXIT;
}

static ExitStatus op_absf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffull);
    return NO_EXIT;
}

static ExitStatus op_absf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    return NO_EXIT;
}

static ExitStatus op_absf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in1, 0x7fffffffffffffffull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return NO_EXIT;
}

static ExitStatus op_add(DisasContext *s, DisasOps *o)
{
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_addc(DisasContext *s, DisasOps *o)
{
    DisasCompare cmp;
    TCGv_i64 carry;

    tcg_gen_add_i64(o->out, o->in1, o->in2);

    /* The carry flag is the msb of CC, therefore the branch mask that would
       create that comparison is 3.  Feeding the generated comparison to
       setcond produces the carry flag that we desire.  */
    disas_jcc(s, &cmp, 3);
    carry = tcg_temp_new_i64();
    if (cmp.is_64) {
        tcg_gen_setcond_i64(cmp.cond, carry, cmp.u.s64.a, cmp.u.s64.b);
    } else {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_setcond_i32(cmp.cond, t, cmp.u.s32.a, cmp.u.s32.b);
        tcg_gen_extu_i32_i64(carry, t);
        tcg_temp_free_i32(t);
    }
    free_compare(&cmp);

    tcg_gen_add_i64(o->out, o->out, carry);
    tcg_temp_free_i64(carry);
    return NO_EXIT;
}

static ExitStatus op_aeb(DisasContext *s, DisasOps *o)
{
    gen_helper_aeb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_adb(DisasContext *s, DisasOps *o)
{
    gen_helper_adb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_axb(DisasContext *s, DisasOps *o)
{
    gen_helper_axb(o->out, cpu_env, o->out, o->out2, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_and(DisasContext *s, DisasOps *o)
{
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_andi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_ori_i64(o->in2, o->in2, ~mask);
    tcg_gen_and_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_bas(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(o->out, pc_to_link_info(s, s->next_pc));
    if (!TCGV_IS_UNUSED_I64(o->in2)) {
        tcg_gen_mov_i64(psw_addr, o->in2);
        per_branch(s, false);
        return EXIT_PC_UPDATED;
    } else {
        return NO_EXIT;
    }
}

static ExitStatus op_basi(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(o->out, pc_to_link_info(s, s->next_pc));
    return help_goto_direct(s, s->pc + 2 * get_field(s->fields, i2));
}

static ExitStatus op_bc(DisasContext *s, DisasOps *o)
{
    int m1 = get_field(s->fields, m1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;

    disas_jcc(s, &c, m1);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bct32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;
    TCGv_i64 t;

    c.cond = TCG_COND_NE;
    c.is_64 = false;
    c.g1 = false;
    c.g2 = false;

    t = tcg_temp_new_i64();
    tcg_gen_subi_i64(t, regs[r1], 1);
    store_reg32_i64(r1, t);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_const_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bcth(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int imm = get_field(s->fields, i2);
    DisasCompare c;
    TCGv_i64 t;

    c.cond = TCG_COND_NE;
    c.is_64 = false;
    c.g1 = false;
    c.g2 = false;

    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, regs[r1], 32);
    tcg_gen_subi_i64(t, t, 1);
    store_reg32h_i64(r1, t);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_const_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, 1, imm, o->in2);
}

static ExitStatus op_bct64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;

    c.cond = TCG_COND_NE;
    c.is_64 = true;
    c.g1 = true;
    c.g2 = false;

    tcg_gen_subi_i64(regs[r1], regs[r1], 1);
    c.u.s64.a = regs[r1];
    c.u.s64.b = tcg_const_i64(0);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bx32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;
    TCGv_i64 t;

    c.cond = (s->insn->data ? TCG_COND_LE : TCG_COND_GT);
    c.is_64 = false;
    c.g1 = false;
    c.g2 = false;

    t = tcg_temp_new_i64();
    tcg_gen_add_i64(t, regs[r1], regs[r3]);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_gen_extrl_i64_i32(c.u.s32.b, regs[r3 | 1]);
    store_reg32_i64(r1, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bx64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;

    c.cond = (s->insn->data ? TCG_COND_LE : TCG_COND_GT);
    c.is_64 = true;

    if (r1 == (r3 | 1)) {
        c.u.s64.b = load_reg(r3 | 1);
        c.g2 = false;
    } else {
        c.u.s64.b = regs[r3 | 1];
        c.g2 = true;
    }

    tcg_gen_add_i64(regs[r1], regs[r1], regs[r3]);
    c.u.s64.a = regs[r1];
    c.g1 = true;

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_cj(DisasContext *s, DisasOps *o)
{
    int imm, m3 = get_field(s->fields, m3);
    bool is_imm;
    DisasCompare c;

    c.cond = ltgt_cond[m3];
    if (s->insn->data) {
        c.cond = tcg_unsigned_cond(c.cond);
    }
    c.is_64 = c.g1 = c.g2 = true;
    c.u.s64.a = o->in1;
    c.u.s64.b = o->in2;

    is_imm = have_field(s->fields, i4);
    if (is_imm) {
        imm = get_field(s->fields, i4);
    } else {
        imm = 0;
        o->out = get_address(s, 0, get_field(s->fields, b4),
                             get_field(s->fields, d4));
    }

    return help_branch(s, &c, is_imm, imm, o->out);
}

static ExitStatus op_ceb(DisasContext *s, DisasOps *o)
{
    gen_helper_ceb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cdb(DisasContext *s, DisasOps *o)
{
    gen_helper_cdb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cxb(DisasContext *s, DisasOps *o)
{
    gen_helper_cxb(cc_op, cpu_env, o->out, o->out2, o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cfeb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f32(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cfdb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f64(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cfxb(o->out, cpu_env, o->in1, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f128(s, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cgeb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f32(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cgdb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f64(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cgxb(o->out, cpu_env, o->in1, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f128(s, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clfeb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f32(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clfdb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f64(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clfxb(o->out, cpu_env, o->in1, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f128(s, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clgeb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f32(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clgdb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f64(s, o->in2);
    return NO_EXIT;
}

static ExitStatus op_clgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_clgxb(o->out, cpu_env, o->in1, o->in2, m3);
    tcg_temp_free_i32(m3);
    gen_set_cc_nz_f128(s, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_cegb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cegb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_cdgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cdgb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_cxgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cxgb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_celgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_celgb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_cdlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cdlgb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_cxlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_cxlgb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_cksm(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s->fields, r2);
    TCGv_i64 len = tcg_temp_new_i64();

    potential_page_fault(s);
    gen_helper_cksm(len, cpu_env, o->in1, o->in2, regs[r2 + 1]);
    set_cc_static(s);
    return_low128(o->out);

    tcg_gen_add_i64(regs[r2], regs[r2], len);
    tcg_gen_sub_i64(regs[r2 + 1], regs[r2 + 1], len);
    tcg_temp_free_i64(len);

    return NO_EXIT;
}

static ExitStatus op_clc(DisasContext *s, DisasOps *o)
{
    int l = get_field(s->fields, l1);
    TCGv_i32 vl;

    switch (l + 1) {
    case 1:
        tcg_gen_qemu_ld8u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld8u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 2:
        tcg_gen_qemu_ld16u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld16u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 4:
        tcg_gen_qemu_ld32u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld32u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 8:
        tcg_gen_qemu_ld64(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld64(cc_dst, o->in2, get_mem_index(s));
        break;
    default:
        potential_page_fault(s);
        vl = tcg_const_i32(l);
        gen_helper_clc(cc_op, cpu_env, vl, o->addr1, o->in2);
        tcg_temp_free_i32(vl);
        set_cc_static(s);
        return NO_EXIT;
    }
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, cc_src, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_clcle(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_clcle(cc_op, cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_clm(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t1, o->in1);
    potential_page_fault(s);
    gen_helper_clm(cc_op, cpu_env, t1, m3, o->in2);
    set_cc_static(s);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_clst(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_clst(o->in1, cpu_env, regs[0], o->in1, o->in2);
    set_cc_static(s);
    return_low128(o->in2);
    return NO_EXIT;
}

static ExitStatus op_cps(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_andi_i64(t, o->in1, 0x8000000000000000ull);
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    tcg_gen_or_i64(o->out, o->out, t);
    tcg_temp_free_i64(t);
    return NO_EXIT;
}

static ExitStatus op_cs(DisasContext *s, DisasOps *o)
{
    /* FIXME: needs an atomic solution for CONFIG_USER_ONLY.  */
    int d2 = get_field(s->fields, d2);
    int b2 = get_field(s->fields, b2);
    int is_64 = s->insn->data;
    TCGv_i64 addr, mem, cc, z;

    /* Note that in1 = R3 (new value) and
       in2 = (zero-extended) R1 (expected value).  */

    /* Load the memory into the (temporary) output.  While the PoO only talks
       about moving the memory to R1 on inequality, if we include equality it
       means that R1 is equal to the memory in all conditions.  */
    addr = get_address(s, 0, b2, d2);
    if (is_64) {
        tcg_gen_qemu_ld64(o->out, addr, get_mem_index(s));
    } else {
        tcg_gen_qemu_ld32u(o->out, addr, get_mem_index(s));
    }

    /* Are the memory and expected values (un)equal?  Note that this setcond
       produces the output CC value, thus the NE sense of the test.  */
    cc = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, cc, o->in2, o->out);

    /* If the memory and expected values are equal (CC==0), copy R3 to MEM.
       Recall that we are allowed to unconditionally issue the store (and
       thus any possible write trap), so (re-)store the original contents
       of MEM in case of inequality.  */
    z = tcg_const_i64(0);
    mem = tcg_temp_new_i64();
    tcg_gen_movcond_i64(TCG_COND_EQ, mem, cc, z, o->in1, o->out);
    if (is_64) {
        tcg_gen_qemu_st64(mem, addr, get_mem_index(s));
    } else {
        tcg_gen_qemu_st32(mem, addr, get_mem_index(s));
    }
    tcg_temp_free_i64(z);
    tcg_temp_free_i64(mem);
    tcg_temp_free_i64(addr);

    /* Store CC back to cc_op.  Wait until after the store so that any
       exception gets the old cc_op value.  */
    tcg_gen_extrl_i64_i32(cc_op, cc);
    tcg_temp_free_i64(cc);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cdsg(DisasContext *s, DisasOps *o)
{
    /* FIXME: needs an atomic solution for CONFIG_USER_ONLY.  */
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    int d2 = get_field(s->fields, d2);
    int b2 = get_field(s->fields, b2);
    TCGv_i64 addrh, addrl, memh, meml, outh, outl, cc, z;

    /* Note that R1:R1+1 = expected value and R3:R3+1 = new value.  */

    addrh = get_address(s, 0, b2, d2);
    addrl = get_address(s, 0, b2, d2 + 8);
    outh = tcg_temp_new_i64();
    outl = tcg_temp_new_i64();

    tcg_gen_qemu_ld64(outh, addrh, get_mem_index(s));
    tcg_gen_qemu_ld64(outl, addrl, get_mem_index(s));

    /* Fold the double-word compare with arithmetic.  */
    cc = tcg_temp_new_i64();
    z = tcg_temp_new_i64();
    tcg_gen_xor_i64(cc, outh, regs[r1]);
    tcg_gen_xor_i64(z, outl, regs[r1 + 1]);
    tcg_gen_or_i64(cc, cc, z);
    tcg_gen_movi_i64(z, 0);
    tcg_gen_setcond_i64(TCG_COND_NE, cc, cc, z);

    memh = tcg_temp_new_i64();
    meml = tcg_temp_new_i64();
    tcg_gen_movcond_i64(TCG_COND_EQ, memh, cc, z, regs[r3], outh);
    tcg_gen_movcond_i64(TCG_COND_EQ, meml, cc, z, regs[r3 + 1], outl);
    tcg_temp_free_i64(z);

    tcg_gen_qemu_st64(memh, addrh, get_mem_index(s));
    tcg_gen_qemu_st64(meml, addrl, get_mem_index(s));
    tcg_temp_free_i64(memh);
    tcg_temp_free_i64(meml);
    tcg_temp_free_i64(addrh);
    tcg_temp_free_i64(addrl);

    /* Save back state now that we've passed all exceptions.  */
    tcg_gen_mov_i64(regs[r1], outh);
    tcg_gen_mov_i64(regs[r1 + 1], outl);
    tcg_gen_extrl_i64_i32(cc_op, cc);
    tcg_temp_free_i64(outh);
    tcg_temp_free_i64(outl);
    tcg_temp_free_i64(cc);
    set_cc_static(s);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_csp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    check_privileged(s);
    gen_helper_csp(cc_op, cpu_env, r1, o->in2);
    tcg_temp_free_i32(r1);
    set_cc_static(s);
    return NO_EXIT;
}
#endif

static ExitStatus op_cvd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, o->in1);
    gen_helper_cvd(t1, t2);
    tcg_temp_free_i32(t2);
    tcg_gen_qemu_st64(t1, o->in2, get_mem_index(s));
    tcg_temp_free_i64(t1);
    return NO_EXIT;
}

static ExitStatus op_ct(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s->fields, m3);
    TCGLabel *lab = gen_new_label();
    TCGCond c;

    c = tcg_invert_cond(ltgt_cond[m3]);
    if (s->insn->data) {
        c = tcg_unsigned_cond(c);
    }
    tcg_gen_brcond_i64(c, o->in1, o->in2, lab);

    /* Trap.  */
    gen_trap(s);

    gen_set_label(lab);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_diag(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    TCGv_i32 func_code = tcg_const_i32(get_field(s->fields, i2));

    check_privileged(s);
    update_psw_addr(s);
    gen_op_calc_cc(s);

    gen_helper_diag(cpu_env, r1, r3, func_code);

    tcg_temp_free_i32(func_code);
    tcg_temp_free_i32(r3);
    tcg_temp_free_i32(r1);
    return NO_EXIT;
}
#endif

static ExitStatus op_divs32(DisasContext *s, DisasOps *o)
{
    gen_helper_divs32(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divu32(DisasContext *s, DisasOps *o)
{
    gen_helper_divu32(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divs64(DisasContext *s, DisasOps *o)
{
    gen_helper_divs64(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divu64(DisasContext *s, DisasOps *o)
{
    gen_helper_divu64(o->out2, cpu_env, o->out, o->out2, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_deb(DisasContext *s, DisasOps *o)
{
    gen_helper_deb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ddb(DisasContext *s, DisasOps *o)
{
    gen_helper_ddb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_dxb(DisasContext *s, DisasOps *o)
{
    gen_helper_dxb(o->out, cpu_env, o->out, o->out2, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_ear(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s->fields, r2);
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, aregs[r2]));
    return NO_EXIT;
}

static ExitStatus op_ecag(DisasContext *s, DisasOps *o)
{
    /* No cache information provided.  */
    tcg_gen_movi_i64(o->out, -1);
    return NO_EXIT;
}

static ExitStatus op_efpc(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, fpc));
    return NO_EXIT;
}

static ExitStatus op_epsw(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r2 = get_field(s->fields, r2);
    TCGv_i64 t = tcg_temp_new_i64();

    /* Note the "subsequently" in the PoO, which implies a defined result
       if r1 == r2.  Thus we cannot defer these writes to an output hook.  */
    tcg_gen_shri_i64(t, psw_mask, 32);
    store_reg32_i64(r1, t);
    if (r2 != 0) {
        store_reg32_i64(r2, psw_mask);
    }

    tcg_temp_free_i64(t);
    return NO_EXIT;
}

static ExitStatus op_ex(DisasContext *s, DisasOps *o)
{
    /* ??? Perhaps a better way to implement EXECUTE is to set a bit in
       tb->flags, (ab)use the tb->cs_base field as the address of
       the template in memory, and grab 8 bits of tb->flags/cflags for
       the contents of the register.  We would then recognize all this
       in gen_intermediate_code_internal, generating code for exactly
       one instruction.  This new TB then gets executed normally.

       On the other hand, this seems to be mostly used for modifying
       MVC inside of memcpy, which needs a helper call anyway.  So
       perhaps this doesn't bear thinking about any further.  */

    TCGv_i64 tmp;

    update_psw_addr(s);
    gen_op_calc_cc(s);

    tmp = tcg_const_i64(s->next_pc);
    gen_helper_ex(cc_op, cpu_env, cc_op, o->in1, o->in2, tmp);
    tcg_temp_free_i64(tmp);

    return NO_EXIT;
}

static ExitStatus op_fieb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_fieb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_fidb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_fidb(o->out, cpu_env, o->in2, m3);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_fixb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    gen_helper_fixb(o->out, cpu_env, o->in1, o->in2, m3);
    return_low128(o->out2);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_flogr(DisasContext *s, DisasOps *o)
{
    /* We'll use the original input for cc computation, since we get to
       compare that against 0, which ought to be better than comparing
       the real output against 64.  It also lets cc_dst be a convenient
       temporary during our computation.  */
    gen_op_update1_cc_i64(s, CC_OP_FLOGR, o->in2);

    /* R1 = IN ? CLZ(IN) : 64.  */
    gen_helper_clz(o->out, o->in2);

    /* R1+1 = IN & ~(found bit).  Note that we may attempt to shift this
       value by 64, which is undefined.  But since the shift is 64 iff the
       input is zero, we still get the correct result after and'ing.  */
    tcg_gen_movi_i64(o->out2, 0x8000000000000000ull);
    tcg_gen_shr_i64(o->out2, o->out2, o->out);
    tcg_gen_andc_i64(o->out2, cc_dst, o->out2);
    return NO_EXIT;
}

static ExitStatus op_icm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s->fields, m3);
    int pos, len, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();
    uint64_t ccm;

    switch (m3) {
    case 0xf:
        /* Effectively a 32-bit load.  */
        tcg_gen_qemu_ld32u(tmp, o->in2, get_mem_index(s));
        len = 32;
        goto one_insert;

    case 0xc:
    case 0x6:
    case 0x3:
        /* Effectively a 16-bit load.  */
        tcg_gen_qemu_ld16u(tmp, o->in2, get_mem_index(s));
        len = 16;
        goto one_insert;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        /* Effectively an 8-bit load.  */
        tcg_gen_qemu_ld8u(tmp, o->in2, get_mem_index(s));
        len = 8;
        goto one_insert;

    one_insert:
        pos = base + ctz32(m3) * 8;
        tcg_gen_deposit_i64(o->out, o->out, tmp, pos, len);
        ccm = ((1ull << len) - 1) << pos;
        break;

    default:
        /* This is going to be a sequence of loads and inserts.  */
        pos = base + 32 - 8;
        ccm = 0;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_qemu_ld8u(tmp, o->in2, get_mem_index(s));
                tcg_gen_addi_i64(o->in2, o->in2, 1);
                tcg_gen_deposit_i64(o->out, o->out, tmp, pos, 8);
                ccm |= 0xff << pos;
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }

    tcg_gen_movi_i64(tmp, ccm);
    gen_op_update2_cc_i64(s, CC_OP_ICM, tmp, o->out);
    tcg_temp_free_i64(tmp);
    return NO_EXIT;
}

static ExitStatus op_insi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    tcg_gen_deposit_i64(o->out, o->in1, o->in2, shift, size);
    return NO_EXIT;
}

static ExitStatus op_ipm(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1;

    gen_op_calc_cc(s);
    tcg_gen_andi_i64(o->out, o->out, ~0xff000000ull);

    t1 = tcg_temp_new_i64();
    tcg_gen_shli_i64(t1, psw_mask, 20);
    tcg_gen_shri_i64(t1, t1, 36);
    tcg_gen_or_i64(o->out, o->out, t1);

    tcg_gen_extu_i32_i64(t1, cc_op);
    tcg_gen_shli_i64(t1, t1, 28);
    tcg_gen_or_i64(o->out, o->out, t1);
    tcg_temp_free_i64(t1);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_ipte(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_ipte(cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_iske(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_iske(o->out, cpu_env, o->in2);
    return NO_EXIT;
}
#endif

static ExitStatus op_ldeb(DisasContext *s, DisasOps *o)
{
    gen_helper_ldeb(o->out, cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ledb(DisasContext *s, DisasOps *o)
{
    gen_helper_ledb(o->out, cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ldxb(DisasContext *s, DisasOps *o)
{
    gen_helper_ldxb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_lexb(DisasContext *s, DisasOps *o)
{
    gen_helper_lexb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_lxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxdb(o->out, cpu_env, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_lxeb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxeb(o->out, cpu_env, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_llgt(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    return NO_EXIT;
}

static ExitStatus op_ld8s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld8u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld16s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld16u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld32s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld32u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld64(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_lat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32_i64(get_field(s->fields, r1), o->in2);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_lgat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_qemu_ld64(o->out, o->in2, get_mem_index(s));
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_lfhat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32h_i64(get_field(s->fields, r1), o->in2);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_llgfat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_qemu_ld32u(o->out, o->in2, get_mem_index(s));
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_llgtat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_loc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;

    disas_jcc(s, &c, get_field(s->fields, m3));

    if (c.is_64) {
        tcg_gen_movcond_i64(c.cond, o->out, c.u.s64.a, c.u.s64.b,
                            o->in2, o->in1);
        free_compare(&c);
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        TCGv_i64 t, z;

        tcg_gen_setcond_i32(c.cond, t32, c.u.s32.a, c.u.s32.b);
        free_compare(&c);

        t = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(t, t32);
        tcg_temp_free_i32(t32);

        z = tcg_const_i64(0);
        tcg_gen_movcond_i64(TCG_COND_NE, o->out, t, z, o->in2, o->in1);
        tcg_temp_free_i64(t);
        tcg_temp_free_i64(z);
    }

    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_lctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lctl(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_lctlg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lctlg(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}
static ExitStatus op_lra(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lra(o->out, cpu_env, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_lpsw(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    check_privileged(s);
    per_breaking_event(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(o->in2, o->in2, 4);
    tcg_gen_qemu_ld32u(t2, o->in2, get_mem_index(s));
    /* Convert the 32-bit PSW_MASK into the 64-bit PSW_MASK.  */
    tcg_gen_shli_i64(t1, t1, 32);
    gen_helper_load_psw(cpu_env, t1, t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return EXIT_NORETURN;
}

static ExitStatus op_lpswe(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    check_privileged(s);
    per_breaking_event(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_ld64(t2, o->in2, get_mem_index(s));
    gen_helper_load_psw(cpu_env, t1, t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return EXIT_NORETURN;
}
#endif

static ExitStatus op_lam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_lam(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_lm32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32_i64(r1, t1);
        tcg_temp_free(t1);
        return NO_EXIT;
    }

    /* First load the values of the first and last registers to trigger
       possible page faults. */
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(t2, o->in2, 4 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld32u(t2, t2, get_mem_index(s));
    store_reg32_i64(r1, t1);
    store_reg32_i64(r3, t2);

    /* Only two registers to read. */
    if (((r1 + 1) & 15) == r3) {
        tcg_temp_free(t2);
        tcg_temp_free(t1);
        return NO_EXIT;
    }

    /* Then load the remaining registers. Page fault can't occur. */
    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t2, 4);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t2);
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32_i64(r1, t1);
    }
    tcg_temp_free(t2);
    tcg_temp_free(t1);

    return NO_EXIT;
}

static ExitStatus op_lmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32h_i64(r1, t1);
        tcg_temp_free(t1);
        return NO_EXIT;
    }

    /* First load the values of the first and last registers to trigger
       possible page faults. */
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(t2, o->in2, 4 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld32u(t2, t2, get_mem_index(s));
    store_reg32h_i64(r1, t1);
    store_reg32h_i64(r3, t2);

    /* Only two registers to read. */
    if (((r1 + 1) & 15) == r3) {
        tcg_temp_free(t2);
        tcg_temp_free(t1);
        return NO_EXIT;
    }

    /* Then load the remaining registers. Page fault can't occur. */
    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t2, 4);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t2);
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32h_i64(r1, t1);
    }
    tcg_temp_free(t2);
    tcg_temp_free(t1);

    return NO_EXIT;
}

static ExitStatus op_lm64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld64(regs[r1], o->in2, get_mem_index(s));
        return NO_EXIT;
    }

    /* First load the values of the first and last registers to trigger
       possible page faults. */
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(t2, o->in2, 8 * ((r3 - r1) & 15));
    tcg_gen_qemu_ld64(regs[r3], t2, get_mem_index(s));
    tcg_gen_mov_i64(regs[r1], t1);
    tcg_temp_free(t2);

    /* Only two registers to read. */
    if (((r1 + 1) & 15) == r3) {
        tcg_temp_free(t1);
        return NO_EXIT;
    }

    /* Then load the remaining registers. Page fault can't occur. */
    r3 = (r3 - 1) & 15;
    tcg_gen_movi_i64(t1, 8);
    while (r1 != r3) {
        r1 = (r1 + 1) & 15;
        tcg_gen_add_i64(o->in2, o->in2, t1);
        tcg_gen_qemu_ld64(regs[r1], o->in2, get_mem_index(s));
    }
    tcg_temp_free(t1);

    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_lura(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lura(o->out, cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_lurag(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lurag(o->out, cpu_env, o->in2);
    return NO_EXIT;
}
#endif

static ExitStatus op_mov2(DisasContext *s, DisasOps *o)
{
    o->out = o->in2;
    o->g_out = o->g_in2;
    TCGV_UNUSED_I64(o->in2);
    o->g_in2 = false;
    return NO_EXIT;
}

static ExitStatus op_mov2e(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s->fields, b2);
    TCGv ar1 = tcg_temp_new_i64();

    o->out = o->in2;
    o->g_out = o->g_in2;
    TCGV_UNUSED_I64(o->in2);
    o->g_in2 = false;

    switch (s->tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> 32:
        tcg_gen_movi_i64(ar1, 0);
        break;
    case PSW_ASC_ACCREG >> 32:
        tcg_gen_movi_i64(ar1, 1);
        break;
    case PSW_ASC_SECONDARY >> 32:
        if (b2) {
            tcg_gen_ld32u_i64(ar1, cpu_env, offsetof(CPUS390XState, aregs[b2]));
        } else {
            tcg_gen_movi_i64(ar1, 0);
        }
        break;
    case PSW_ASC_HOME >> 32:
        tcg_gen_movi_i64(ar1, 2);
        break;
    }

    tcg_gen_st32_i64(ar1, cpu_env, offsetof(CPUS390XState, aregs[1]));
    tcg_temp_free_i64(ar1);

    return NO_EXIT;
}

static ExitStatus op_movx(DisasContext *s, DisasOps *o)
{
    o->out = o->in1;
    o->out2 = o->in2;
    o->g_out = o->g_in1;
    o->g_out2 = o->g_in2;
    TCGV_UNUSED_I64(o->in1);
    TCGV_UNUSED_I64(o->in2);
    o->g_in1 = o->g_in2 = false;
    return NO_EXIT;
}

static ExitStatus op_mvc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_mvc(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    return NO_EXIT;
}

static ExitStatus op_mvcl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r2 = tcg_const_i32(get_field(s->fields, r2));
    potential_page_fault(s);
    gen_helper_mvcl(cc_op, cpu_env, r1, r2);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_mvcle(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_mvcle(cc_op, cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    set_cc_static(s);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_mvcp(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, l1);
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_mvcp(cc_op, cpu_env, regs[r1], o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_mvcs(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, l1);
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_mvcs(cc_op, cpu_env, regs[r1], o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}
#endif

static ExitStatus op_mvpg(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_mvpg(cpu_env, regs[0], o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_mvst(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_mvst(o->in1, cpu_env, regs[0], o->in1, o->in2);
    set_cc_static(s);
    return_low128(o->in2);
    return NO_EXIT;
}

static ExitStatus op_mul(DisasContext *s, DisasOps *o)
{
    tcg_gen_mul_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_mul128(DisasContext *s, DisasOps *o)
{
    tcg_gen_mulu2_i64(o->out2, o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_meeb(DisasContext *s, DisasOps *o)
{
    gen_helper_meeb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_mdeb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdeb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_mdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_mxb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxb(o->out, cpu_env, o->out, o->out2, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_mxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxdb(o->out, cpu_env, o->out, o->out2, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_maeb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s->fields, r3));
    gen_helper_maeb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return NO_EXIT;
}

static ExitStatus op_madb(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s->fields, r3);
    gen_helper_madb(o->out, cpu_env, o->in1, o->in2, fregs[r3]);
    return NO_EXIT;
}

static ExitStatus op_mseb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s->fields, r3));
    gen_helper_mseb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return NO_EXIT;
}

static ExitStatus op_msdb(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s->fields, r3);
    gen_helper_msdb(o->out, cpu_env, o->in1, o->in2, fregs[r3]);
    return NO_EXIT;
}

static ExitStatus op_nabs(DisasContext *s, DisasOps *o)
{
    TCGv_i64 z, n;
    z = tcg_const_i64(0);
    n = tcg_temp_new_i64();
    tcg_gen_neg_i64(n, o->in2);
    tcg_gen_movcond_i64(TCG_COND_GE, o->out, o->in2, z, n, o->in2);
    tcg_temp_free_i64(n);
    tcg_temp_free_i64(z);
    return NO_EXIT;
}

static ExitStatus op_nabsf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x80000000ull);
    return NO_EXIT;
}

static ExitStatus op_nabsf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x8000000000000000ull);
    return NO_EXIT;
}

static ExitStatus op_nabsf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return NO_EXIT;
}

static ExitStatus op_nc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_nc(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_neg(DisasContext *s, DisasOps *o)
{
    tcg_gen_neg_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_negf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x80000000ull);
    return NO_EXIT;
}

static ExitStatus op_negf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x8000000000000000ull);
    return NO_EXIT;
}

static ExitStatus op_negf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return NO_EXIT;
}

static ExitStatus op_oc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_oc(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_or(DisasContext *s, DisasOps *o)
{
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_or_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_popcnt(DisasContext *s, DisasOps *o)
{
    gen_helper_popcnt(o->out, o->in2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_ptlb(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_ptlb(cpu_env);
    return NO_EXIT;
}
#endif

static ExitStatus op_risbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s->fields, i3);
    int i4 = get_field(s->fields, i4);
    int i5 = get_field(s->fields, i5);
    int do_zero = i4 & 0x80;
    uint64_t mask, imask, pmask;
    int pos, len, rot;

    /* Adjust the arguments for the specific insn.  */
    switch (s->fields->op2) {
    case 0x55: /* risbg */
        i3 &= 63;
        i4 &= 63;
        pmask = ~0;
        break;
    case 0x5d: /* risbhg */
        i3 &= 31;
        i4 &= 31;
        pmask = 0xffffffff00000000ull;
        break;
    case 0x51: /* risblg */
        i3 &= 31;
        i4 &= 31;
        pmask = 0x00000000ffffffffull;
        break;
    default:
        abort();
    }

    /* MASK is the set of bits to be inserted from R2.
       Take care for I3/I4 wraparound.  */
    mask = pmask >> i3;
    if (i3 <= i4) {
        mask ^= pmask >> i4 >> 1;
    } else {
        mask |= ~(pmask >> i4 >> 1);
    }
    mask &= pmask;

    /* IMASK is the set of bits to be kept from R1.  In the case of the high/low
       insns, we need to keep the other half of the register.  */
    imask = ~mask | ~pmask;
    if (do_zero) {
        if (s->fields->op2 == 0x55) {
            imask = 0;
        } else {
            imask = ~pmask;
        }
    }

    /* In some cases we can implement this with deposit, which can be more
       efficient on some hosts.  */
    if (~mask == imask && i3 <= i4) {
        if (s->fields->op2 == 0x5d) {
            i3 += 32, i4 += 32;
        }
        /* Note that we rotate the bits to be inserted to the lsb, not to
           the position as described in the PoO.  */
        len = i4 - i3 + 1;
        pos = 63 - i4;
        rot = (i5 - pos) & 63;
    } else {
        pos = len = -1;
        rot = i5 & 63;
    }

    /* Rotate the input as necessary.  */
    tcg_gen_rotli_i64(o->in2, o->in2, rot);

    /* Insert the selected bits into the output.  */
    if (pos >= 0) {
        tcg_gen_deposit_i64(o->out, o->out, o->in2, pos, len);
    } else if (imask == 0) {
        tcg_gen_andi_i64(o->out, o->in2, mask);
    } else {
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_andi_i64(o->out, o->out, imask);
        tcg_gen_or_i64(o->out, o->out, o->in2);
    }
    return NO_EXIT;
}

static ExitStatus op_rosbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s->fields, i3);
    int i4 = get_field(s->fields, i4);
    int i5 = get_field(s->fields, i5);
    uint64_t mask;

    /* If this is a test-only form, arrange to discard the result.  */
    if (i3 & 0x80) {
        o->out = tcg_temp_new_i64();
        o->g_out = false;
    }

    i3 &= 63;
    i4 &= 63;
    i5 &= 63;

    /* MASK is the set of bits to be operated on from R2.
       Take care for I3/I4 wraparound.  */
    mask = ~0ull >> i3;
    if (i3 <= i4) {
        mask ^= ~0ull >> i4 >> 1;
    } else {
        mask |= ~(~0ull >> i4 >> 1);
    }

    /* Rotate the input as necessary.  */
    tcg_gen_rotli_i64(o->in2, o->in2, i5);

    /* Operate.  */
    switch (s->fields->op2) {
    case 0x55: /* AND */
        tcg_gen_ori_i64(o->in2, o->in2, ~mask);
        tcg_gen_and_i64(o->out, o->out, o->in2);
        break;
    case 0x56: /* OR */
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_or_i64(o->out, o->out, o->in2);
        break;
    case 0x57: /* XOR */
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_xor_i64(o->out, o->out, o->in2);
        break;
    default:
        abort();
    }

    /* Set the CC.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_rev16(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap16_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rev32(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap32_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rev64(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap64_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rll32(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 to = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t1, o->in1);
    tcg_gen_extrl_i64_i32(t2, o->in2);
    tcg_gen_rotl_i32(to, t1, t2);
    tcg_gen_extu_i32_i64(o->out, to);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(to);
    return NO_EXIT;
}

static ExitStatus op_rll64(DisasContext *s, DisasOps *o)
{
    tcg_gen_rotl_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_rrbe(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_rrbe(cc_op, cpu_env, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_sacf(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_sacf(cpu_env, o->in2);
    /* Addressing mode has changed, so end the block.  */
    return EXIT_PC_STALE;
}
#endif

static ExitStatus op_sam(DisasContext *s, DisasOps *o)
{
    int sam = s->insn->data;
    TCGv_i64 tsam;
    uint64_t mask;

    switch (sam) {
    case 0:
        mask = 0xffffff;
        break;
    case 1:
        mask = 0x7fffffff;
        break;
    default:
        mask = -1;
        break;
    }

    /* Bizarre but true, we check the address of the current insn for the
       specification exception, not the next to be executed.  Thus the PoO
       documents that Bad Things Happen two bytes before the end.  */
    if (s->pc & ~mask) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return EXIT_NORETURN;
    }
    s->next_pc &= mask;

    tsam = tcg_const_i64(sam);
    tcg_gen_deposit_i64(psw_mask, psw_mask, tsam, 31, 2);
    tcg_temp_free_i64(tsam);

    /* Always exit the TB, since we (may have) changed execution mode.  */
    return EXIT_PC_STALE;
}

static ExitStatus op_sar(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    tcg_gen_st32_i64(o->in2, cpu_env, offsetof(CPUS390XState, aregs[r1]));
    return NO_EXIT;
}

static ExitStatus op_seb(DisasContext *s, DisasOps *o)
{
    gen_helper_seb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sdb(o->out, cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sxb(o->out, cpu_env, o->out, o->out2, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_sqeb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqeb(o->out, cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sqdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqdb(o->out, cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sqxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqxb(o->out, cpu_env, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_servc(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_servc(cc_op, cpu_env, o->in2, o->in1);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_sigp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_sigp(cc_op, cpu_env, o->in2, r1, o->in1);
    tcg_temp_free_i32(r1);
    return NO_EXIT;
}
#endif

static ExitStatus op_soc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;
    TCGv_i64 a;
    TCGLabel *lab;
    int r1;

    disas_jcc(s, &c, get_field(s->fields, m3));

    /* We want to store when the condition is fulfilled, so branch
       out when it's not */
    c.cond = tcg_invert_cond(c.cond);

    lab = gen_new_label();
    if (c.is_64) {
        tcg_gen_brcond_i64(c.cond, c.u.s64.a, c.u.s64.b, lab);
    } else {
        tcg_gen_brcond_i32(c.cond, c.u.s32.a, c.u.s32.b, lab);
    }
    free_compare(&c);

    r1 = get_field(s->fields, r1);
    a = get_address(s, 0, get_field(s->fields, b2), get_field(s->fields, d2));
    if (s->insn->data) {
        tcg_gen_qemu_st64(regs[r1], a, get_mem_index(s));
    } else {
        tcg_gen_qemu_st32(regs[r1], a, get_mem_index(s));
    }
    tcg_temp_free_i64(a);

    gen_set_label(lab);
    return NO_EXIT;
}

static ExitStatus op_sla(DisasContext *s, DisasOps *o)
{
    uint64_t sign = 1ull << s->insn->data;
    enum cc_op cco = s->insn->data == 31 ? CC_OP_SLA_32 : CC_OP_SLA_64;
    gen_op_update2_cc_i64(s, cco, o->in1, o->in2);
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    /* The arithmetic left shift is curious in that it does not affect
       the sign bit.  Copy that over from the source unchanged.  */
    tcg_gen_andi_i64(o->out, o->out, ~sign);
    tcg_gen_andi_i64(o->in1, o->in1, sign);
    tcg_gen_or_i64(o->out, o->out, o->in1);
    return NO_EXIT;
}

static ExitStatus op_sll(DisasContext *s, DisasOps *o)
{
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sra(DisasContext *s, DisasOps *o)
{
    tcg_gen_sar_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_srl(DisasContext *s, DisasOps *o)
{
    tcg_gen_shr_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sfpc(DisasContext *s, DisasOps *o)
{
    gen_helper_sfpc(cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sfas(DisasContext *s, DisasOps *o)
{
    gen_helper_sfas(cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_srnm(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s->fields, b2);
    int d2 = get_field(s->fields, d2);
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    int mask, pos, len;

    switch (s->fields->op2) {
    case 0x99: /* SRNM */
        pos = 0, len = 2;
        break;
    case 0xb8: /* SRNMB */
        pos = 0, len = 3;
        break;
    case 0xb9: /* SRNMT */
        pos = 4, len = 3;
        break;
    default:
        tcg_abort();
    }
    mask = (1 << len) - 1;

    /* Insert the value into the appropriate field of the FPC.  */
    if (b2 == 0) {
        tcg_gen_movi_i64(t1, d2 & mask);
    } else {
        tcg_gen_addi_i64(t1, regs[b2], d2);
        tcg_gen_andi_i64(t1, t1, mask);
    }
    tcg_gen_ld32u_i64(t2, cpu_env, offsetof(CPUS390XState, fpc));
    tcg_gen_deposit_i64(t2, t2, t1, pos, len);
    tcg_temp_free_i64(t1);

    /* Then install the new FPC to set the rounding mode in fpu_status.  */
    gen_helper_sfpc(cpu_env, t2);
    tcg_temp_free_i64(t2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_spka(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    tcg_gen_shri_i64(o->in2, o->in2, 4);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, PSW_SHIFT_KEY - 4, 4);
    return NO_EXIT;
}

static ExitStatus op_sske(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_sske(cpu_env, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ssm(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, 56, 8);
    return NO_EXIT;
}

static ExitStatus op_stap(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    /* ??? Surely cpu address != cpu number.  In any case the previous
       version of this stored more than the required half-word, so it
       is unlikely this has ever been tested.  */
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, cpu_num));
    return NO_EXIT;
}

static ExitStatus op_stck(DisasContext *s, DisasOps *o)
{
    gen_helper_stck(o->out, cpu_env);
    /* ??? We don't implement clock states.  */
    gen_op_movi_cc(s, 0);
    return NO_EXIT;
}

static ExitStatus op_stcke(DisasContext *s, DisasOps *o)
{
    TCGv_i64 c1 = tcg_temp_new_i64();
    TCGv_i64 c2 = tcg_temp_new_i64();
    gen_helper_stck(c1, cpu_env);
    /* Shift the 64-bit value into its place as a zero-extended
       104-bit value.  Note that "bit positions 64-103 are always
       non-zero so that they compare differently to STCK"; we set
       the least significant bit to 1.  */
    tcg_gen_shli_i64(c2, c1, 56);
    tcg_gen_shri_i64(c1, c1, 8);
    tcg_gen_ori_i64(c2, c2, 0x10000);
    tcg_gen_qemu_st64(c1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_st64(c2, o->in2, get_mem_index(s));
    tcg_temp_free_i64(c1);
    tcg_temp_free_i64(c2);
    /* ??? We don't implement clock states.  */
    gen_op_movi_cc(s, 0);
    return NO_EXIT;
}

static ExitStatus op_sckc(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_sckc(cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_stckc(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_stckc(o->out, cpu_env);
    return NO_EXIT;
}

static ExitStatus op_stctg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stctg(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stctl(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stidp(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1 = tcg_temp_new_i64();

    check_privileged(s);
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, cpu_num));
    tcg_gen_ld32u_i64(t1, cpu_env, offsetof(CPUS390XState, machine_type));
    tcg_gen_deposit_i64(o->out, o->out, t1, 32, 32);
    tcg_temp_free_i64(t1);

    return NO_EXIT;
}

static ExitStatus op_spt(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_spt(cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_stfl(DisasContext *s, DisasOps *o)
{
    TCGv_i64 f, a;
    /* We really ought to have more complete indication of facilities
       that we implement.  Address this when STFLE is implemented.  */
    check_privileged(s);
    f = tcg_const_i64(0xc0000000);
    a = tcg_const_i64(200);
    tcg_gen_qemu_st32(f, a, get_mem_index(s));
    tcg_temp_free_i64(f);
    tcg_temp_free_i64(a);
    return NO_EXIT;
}

static ExitStatus op_stpt(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_stpt(o->out, cpu_env);
    return NO_EXIT;
}

static ExitStatus op_stsi(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stsi(cc_op, cpu_env, o->in2, regs[0], regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_spx(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    gen_helper_spx(cpu_env, o->in2);
    return NO_EXIT;
}

static ExitStatus op_xsch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_xsch(cpu_env, regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_csch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_csch(cpu_env, regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_hsch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_hsch(cpu_env, regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_msch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_msch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_rchp(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_rchp(cpu_env, regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_rsch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_rsch(cpu_env, regs[1]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_ssch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_ssch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_stsch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stsch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_tsch(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_tsch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_chsc(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_chsc(cpu_env, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_stpx(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    tcg_gen_ld_i64(o->out, cpu_env, offsetof(CPUS390XState, psa));
    tcg_gen_andi_i64(o->out, o->out, 0x7fffe000);
    return NO_EXIT;
}

static ExitStatus op_stnosm(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = get_field(s->fields, i2);
    TCGv_i64 t;

    check_privileged(s);

    /* It is important to do what the instruction name says: STORE THEN.
       If we let the output hook perform the store then if we fault and
       restart, we'll have the wrong SYSTEM MASK in place.  */
    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 56);
    tcg_gen_qemu_st8(t, o->addr1, get_mem_index(s));
    tcg_temp_free_i64(t);

    if (s->fields->op == 0xac) {
        tcg_gen_andi_i64(psw_mask, psw_mask,
                         (i2 << 56) | 0x00ffffffffffffffull);
    } else {
        tcg_gen_ori_i64(psw_mask, psw_mask, i2 << 56);
    }
    return NO_EXIT;
}

static ExitStatus op_stura(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stura(cpu_env, o->in2, o->in1);
    return NO_EXIT;
}

static ExitStatus op_sturg(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_sturg(cpu_env, o->in2, o->in1);
    return NO_EXIT;
}
#endif

static ExitStatus op_st8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st8(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st16(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st32(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st64(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_stam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_stam(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stcm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s->fields, m3);
    int pos, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();

    pos = base + ctz32(m3) * 8;
    switch (m3) {
    case 0xf:
        /* Effectively a 32-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st32(tmp, o->in2, get_mem_index(s));
        break;

    case 0xc:
    case 0x6:
    case 0x3:
        /* Effectively a 16-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st16(tmp, o->in2, get_mem_index(s));
        break;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        /* Effectively an 8-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st8(tmp, o->in2, get_mem_index(s));
        break;

    default:
        /* This is going to be a sequence of shifts and stores.  */
        pos = base + 32 - 8;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_shri_i64(tmp, o->in1, pos);
                tcg_gen_qemu_st8(tmp, o->in2, get_mem_index(s));
                tcg_gen_addi_i64(o->in2, o->in2, 1);
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }
    tcg_temp_free_i64(tmp);
    return NO_EXIT;
}

static ExitStatus op_stm(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    int size = s->insn->data;
    TCGv_i64 tsize = tcg_const_i64(size);

    while (1) {
        if (size == 8) {
            tcg_gen_qemu_st64(regs[r1], o->in2, get_mem_index(s));
        } else {
            tcg_gen_qemu_st32(regs[r1], o->in2, get_mem_index(s));
        }
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, tsize);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(tsize);
    return NO_EXIT;
}

static ExitStatus op_stmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_const_i64(4);
    TCGv_i64 t32 = tcg_const_i64(32);

    while (1) {
        tcg_gen_shl_i64(t, regs[r1], t32);
        tcg_gen_qemu_st32(t, o->in2, get_mem_index(s));
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t4);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(t);
    tcg_temp_free_i64(t4);
    tcg_temp_free_i64(t32);
    return NO_EXIT;
}

static ExitStatus op_srst(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_srst(o->in1, cpu_env, regs[0], o->in1, o->in2);
    set_cc_static(s);
    return_low128(o->in2);
    return NO_EXIT;
}

static ExitStatus op_sub(DisasContext *s, DisasOps *o)
{
    tcg_gen_sub_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_subb(DisasContext *s, DisasOps *o)
{
    DisasCompare cmp;
    TCGv_i64 borrow;

    tcg_gen_sub_i64(o->out, o->in1, o->in2);

    /* The !borrow flag is the msb of CC.  Since we want the inverse of
       that, we ask for a comparison of CC=0 | CC=1 -> mask of 8 | 4.  */
    disas_jcc(s, &cmp, 8 | 4);
    borrow = tcg_temp_new_i64();
    if (cmp.is_64) {
        tcg_gen_setcond_i64(cmp.cond, borrow, cmp.u.s64.a, cmp.u.s64.b);
    } else {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_setcond_i32(cmp.cond, t, cmp.u.s32.a, cmp.u.s32.b);
        tcg_gen_extu_i32_i64(borrow, t);
        tcg_temp_free_i32(t);
    }
    free_compare(&cmp);

    tcg_gen_sub_i64(o->out, o->out, borrow);
    tcg_temp_free_i64(borrow);
    return NO_EXIT;
}

static ExitStatus op_svc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t;

    update_psw_addr(s);
    update_cc_op(s);

    t = tcg_const_i32(get_field(s->fields, i1) & 0xff);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_code));
    tcg_temp_free_i32(t);

    t = tcg_const_i32(s->next_pc - s->pc);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_ilen));
    tcg_temp_free_i32(t);

    gen_exception(EXCP_SVC);
    return EXIT_NORETURN;
}

static ExitStatus op_tceb(DisasContext *s, DisasOps *o)
{
    gen_helper_tceb(cc_op, o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_tcdb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcdb(cc_op, o->in1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_tcxb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcxb(cc_op, o->out, o->out2, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_tprot(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_tprot(cc_op, o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}
#endif

static ExitStatus op_tr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_tr(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_tre(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_tre(o->out, cpu_env, o->out, o->out2, o->in2);
    return_low128(o->out2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_trt(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_trt(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_unpk(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_unpk(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    return NO_EXIT;
}

static ExitStatus op_xc(DisasContext *s, DisasOps *o)
{
    int d1 = get_field(s->fields, d1);
    int d2 = get_field(s->fields, d2);
    int b1 = get_field(s->fields, b1);
    int b2 = get_field(s->fields, b2);
    int l = get_field(s->fields, l1);
    TCGv_i32 t32;

    o->addr1 = get_address(s, 0, b1, d1);

    /* If the addresses are identical, this is a store/memset of zero.  */
    if (b1 == b2 && d1 == d2 && (l + 1) <= 32) {
        o->in2 = tcg_const_i64(0);

        l++;
        while (l >= 8) {
            tcg_gen_qemu_st64(o->in2, o->addr1, get_mem_index(s));
            l -= 8;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 8);
            }
        }
        if (l >= 4) {
            tcg_gen_qemu_st32(o->in2, o->addr1, get_mem_index(s));
            l -= 4;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 4);
            }
        }
        if (l >= 2) {
            tcg_gen_qemu_st16(o->in2, o->addr1, get_mem_index(s));
            l -= 2;
            if (l > 0) {
                tcg_gen_addi_i64(o->addr1, o->addr1, 2);
            }
        }
        if (l) {
            tcg_gen_qemu_st8(o->in2, o->addr1, get_mem_index(s));
        }
        gen_op_movi_cc(s, 0);
        return NO_EXIT;
    }

    /* But in general we'll defer to a helper.  */
    o->in2 = get_address(s, 0, b2, d2);
    t32 = tcg_const_i32(l);
    potential_page_fault(s);
    gen_helper_xc(cc_op, cpu_env, t32, o->addr1, o->in2);
    tcg_temp_free_i32(t32);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_xor(DisasContext *s, DisasOps *o)
{
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_xori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_xor_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_zero(DisasContext *s, DisasOps *o)
{
    o->out = tcg_const_i64(0);
    return NO_EXIT;
}

static ExitStatus op_zero2(DisasContext *s, DisasOps *o)
{
    o->out = tcg_const_i64(0);
    o->out2 = o->out;
    o->g_out2 = true;
    return NO_EXIT;
}

/* ====================================================================== */
/* The "Cc OUTput" generators.  Given the generated output (and in some cases
   the original inputs), update the various cc data structures in order to
   be able to compute the new condition code.  */

static void cout_abs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_32, o->out);
}

static void cout_abs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_64, o->out);
}

static void cout_adds32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_32, o->in1, o->in2, o->out);
}

static void cout_adds64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_64, o->in1, o->in2, o->out);
}

static void cout_addu32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDU_32, o->in1, o->in2, o->out);
}

static void cout_addu64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDU_64, o->in1, o->in2, o->out);
}

static void cout_addc32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDC_32, o->in1, o->in2, o->out);
}

static void cout_addc64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDC_64, o->in1, o->in2, o->out);
}

static void cout_cmps32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_32, o->in1, o->in2);
}

static void cout_cmps64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_64, o->in1, o->in2);
}

static void cout_cmpu32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_32, o->in1, o->in2);
}

static void cout_cmpu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, o->in1, o->in2);
}

static void cout_f32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F32, o->out);
}

static void cout_f64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F64, o->out);
}

static void cout_f128(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_NZ_F128, o->out, o->out2);
}

static void cout_nabs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_32, o->out);
}

static void cout_nabs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_64, o->out);
}

static void cout_neg32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_32, o->out);
}

static void cout_neg64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_64, o->out);
}

static void cout_nz32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update1_cc_i64(s, CC_OP_NZ, cc_dst);
}

static void cout_nz64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, o->out);
}

static void cout_s32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_32, o->out);
}

static void cout_s64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_64, o->out);
}

static void cout_subs32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_32, o->in1, o->in2, o->out);
}

static void cout_subs64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_64, o->in1, o->in2, o->out);
}

static void cout_subu32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBU_32, o->in1, o->in2, o->out);
}

static void cout_subu64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBU_64, o->in1, o->in2, o->out);
}

static void cout_subb32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBB_32, o->in1, o->in2, o->out);
}

static void cout_subb64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBB_64, o->in1, o->in2, o->out);
}

static void cout_tm32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_32, o->in1, o->in2);
}

static void cout_tm64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_64, o->in1, o->in2);
}

/* ====================================================================== */
/* The "PREParation" generators.  These initialize the DisasOps.OUT fields
   with the TCG register to which we will write.  Used in combination with
   the "wout" generators, in some cases we need a new temporary, and in
   some cases we can write to a TCG global.  */

static void prep_new(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
}
#define SPEC_prep_new 0

static void prep_new_P(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();
}
#define SPEC_prep_new_P 0

static void prep_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = regs[get_field(f, r1)];
    o->g_out = true;
}
#define SPEC_prep_r1 0

static void prep_r1_P(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    o->out = regs[r1];
    o->out2 = regs[r1 + 1];
    o->g_out = o->g_out2 = true;
}
#define SPEC_prep_r1_P SPEC_r1_even

static void prep_f1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = fregs[get_field(f, r1)];
    o->g_out = true;
}
#define SPEC_prep_f1 0

static void prep_x1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    o->out = fregs[r1];
    o->out2 = fregs[r1 + 2];
    o->g_out = o->g_out2 = true;
}
#define SPEC_prep_x1 SPEC_r1_f128

/* ====================================================================== */
/* The "Write OUTput" generators.  These generally perform some non-trivial
   copy of data to TCG globals, or to main memory.  The trivial cases are
   generally handled by having a "prep" generator install the TCG global
   as the destination of the operation.  */

static void wout_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_reg(get_field(f, r1), o->out);
}
#define SPEC_wout_r1 0

static void wout_r1_8(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 8);
}
#define SPEC_wout_r1_8 0

static void wout_r1_16(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 16);
}
#define SPEC_wout_r1_16 0

static void wout_r1_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_reg32_i64(get_field(f, r1), o->out);
}
#define SPEC_wout_r1_32 0

static void wout_r1_32h(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_reg32h_i64(get_field(f, r1), o->out);
}
#define SPEC_wout_r1_32h 0

static void wout_r1_P32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    store_reg32_i64(r1, o->out);
    store_reg32_i64(r1 + 1, o->out2);
}
#define SPEC_wout_r1_P32 SPEC_r1_even

static void wout_r1_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    store_reg32_i64(r1 + 1, o->out);
    tcg_gen_shri_i64(o->out, o->out, 32);
    store_reg32_i64(r1, o->out);
}
#define SPEC_wout_r1_D32 SPEC_r1_even

static void wout_e1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_freg32_i64(get_field(f, r1), o->out);
}
#define SPEC_wout_e1 0

static void wout_f1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_freg(get_field(f, r1), o->out);
}
#define SPEC_wout_f1 0

static void wout_x1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int f1 = get_field(s->fields, r1);
    store_freg(f1, o->out);
    store_freg(f1 + 2, o->out2);
}
#define SPEC_wout_x1 SPEC_r1_f128

static void wout_cond_r1r2_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    if (get_field(f, r1) != get_field(f, r2)) {
        store_reg32_i64(get_field(f, r1), o->out);
    }
}
#define SPEC_wout_cond_r1r2_32 0

static void wout_cond_e1e2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    if (get_field(f, r1) != get_field(f, r2)) {
        store_freg32_i64(get_field(f, r1), o->out);
    }
}
#define SPEC_wout_cond_e1e2 0

static void wout_m1_8(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st8(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_8 0

static void wout_m1_16(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st16(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_16 0

static void wout_m1_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_32 0

static void wout_m1_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st64(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_64 0

static void wout_m2_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->in2, get_mem_index(s));
}
#define SPEC_wout_m2_32 0

static void wout_m2_32_r1_atomic(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* XXX release reservation */
    tcg_gen_qemu_st32(o->out, o->addr1, get_mem_index(s));
    store_reg32_i64(get_field(f, r1), o->in2);
}
#define SPEC_wout_m2_32_r1_atomic 0

static void wout_m2_64_r1_atomic(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* XXX release reservation */
    tcg_gen_qemu_st64(o->out, o->addr1, get_mem_index(s));
    store_reg(get_field(f, r1), o->in2);
}
#define SPEC_wout_m2_64_r1_atomic 0

/* ====================================================================== */
/* The "INput 1" generators.  These load the first operand to an insn.  */

static void in1_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r1));
}
#define SPEC_in1_r1 0

static void in1_r1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = regs[get_field(f, r1)];
    o->g_in1 = true;
}
#define SPEC_in1_r1_o 0

static void in1_r1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(f, r1)]);
}
#define SPEC_in1_r1_32s 0

static void in1_r1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(f, r1)]);
}
#define SPEC_in1_r1_32u 0

static void in1_r1_sr32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(f, r1)], 32);
}
#define SPEC_in1_r1_sr32 0

static void in1_r1p1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r1) + 1);
}
#define SPEC_in1_r1p1 SPEC_r1_even

static void in1_r1p1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(f, r1) + 1]);
}
#define SPEC_in1_r1p1_32s SPEC_r1_even

static void in1_r1p1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(f, r1) + 1]);
}
#define SPEC_in1_r1p1_32u SPEC_r1_even

static void in1_r1_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r1 + 1], regs[r1]);
}
#define SPEC_in1_r1_D32 SPEC_r1_even

static void in1_r2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r2));
}
#define SPEC_in1_r2 0

static void in1_r2_sr32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(f, r2)], 32);
}
#define SPEC_in1_r2_sr32 0

static void in1_r3(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r3));
}
#define SPEC_in1_r3 0

static void in1_r3_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = regs[get_field(f, r3)];
    o->g_in1 = true;
}
#define SPEC_in1_r3_o 0

static void in1_r3_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(f, r3)]);
}
#define SPEC_in1_r3_32s 0

static void in1_r3_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(f, r3)]);
}
#define SPEC_in1_r3_32u 0

static void in1_r3_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r3 = get_field(f, r3);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r3 + 1], regs[r3]);
}
#define SPEC_in1_r3_D32 SPEC_r3_even

static void in1_e1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_freg32_i64(get_field(f, r1));
}
#define SPEC_in1_e1 0

static void in1_f1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = fregs[get_field(f, r1)];
    o->g_in1 = true;
}
#define SPEC_in1_f1_o 0

static void in1_x1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    o->out = fregs[r1];
    o->out2 = fregs[r1 + 2];
    o->g_out = o->g_out2 = true;
}
#define SPEC_in1_x1_o SPEC_r1_f128

static void in1_f3_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = fregs[get_field(f, r3)];
    o->g_in1 = true;
}
#define SPEC_in1_f3_o 0

static void in1_la1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->addr1 = get_address(s, 0, get_field(f, b1), get_field(f, d1));
}
#define SPEC_in1_la1 0

static void in1_la2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int x2 = have_field(f, x2) ? get_field(f, x2) : 0;
    o->addr1 = get_address(s, x2, get_field(f, b2), get_field(f, d2));
}
#define SPEC_in1_la2 0

static void in1_m1_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld8u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_8u 0

static void in1_m1_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16s(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_16s 0

static void in1_m1_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_16u 0

static void in1_m1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32s(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_32s 0

static void in1_m1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_32u 0

static void in1_m1_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_64 0

/* ====================================================================== */
/* The "INput 2" generators.  These load the second operand to an insn.  */

static void in2_r1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = regs[get_field(f, r1)];
    o->g_in2 = true;
}
#define SPEC_in2_r1_o 0

static void in2_r1_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(f, r1)]);
}
#define SPEC_in2_r1_16u 0

static void in2_r1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(f, r1)]);
}
#define SPEC_in2_r1_32u 0

static void in2_r1_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in2, regs[r1 + 1], regs[r1]);
}
#define SPEC_in2_r1_D32 SPEC_r1_even

static void in2_r2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_reg(get_field(f, r2));
}
#define SPEC_in2_r2 0

static void in2_r2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = regs[get_field(f, r2)];
    o->g_in2 = true;
}
#define SPEC_in2_r2_o 0

static void in2_r2_nz(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r2 = get_field(f, r2);
    if (r2 != 0) {
        o->in2 = load_reg(r2);
    }
}
#define SPEC_in2_r2_nz 0

static void in2_r2_8s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8s_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_8s 0

static void in2_r2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8u_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_8u 0

static void in2_r2_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16s_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_16s 0

static void in2_r2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_16u 0

static void in2_r3(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_reg(get_field(f, r3));
}
#define SPEC_in2_r3 0

static void in2_r3_sr32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(f, r3)], 32);
}
#define SPEC_in2_r3_sr32 0

static void in2_r2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_32s 0

static void in2_r2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(f, r2)]);
}
#define SPEC_in2_r2_32u 0

static void in2_r2_sr32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(f, r2)], 32);
}
#define SPEC_in2_r2_sr32 0

static void in2_e2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_freg32_i64(get_field(f, r2));
}
#define SPEC_in2_e2 0

static void in2_f2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = fregs[get_field(f, r2)];
    o->g_in2 = true;
}
#define SPEC_in2_f2_o 0

static void in2_x2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r2 = get_field(f, r2);
    o->in1 = fregs[r2];
    o->in2 = fregs[r2 + 2];
    o->g_in1 = o->g_in2 = true;
}
#define SPEC_in2_x2_o SPEC_r2_f128

static void in2_ra2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = get_address(s, 0, get_field(f, r2), 0);
}
#define SPEC_in2_ra2 0

static void in2_a2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int x2 = have_field(f, x2) ? get_field(f, x2) : 0;
    o->in2 = get_address(s, x2, get_field(f, b2), get_field(f, d2));
}
#define SPEC_in2_a2 0

static void in2_ri2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64(s->pc + (int64_t)get_field(f, i2) * 2);
}
#define SPEC_in2_ri2 0

static void in2_sh32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    help_l2_shift(s, f, o, 31);
}
#define SPEC_in2_sh32 0

static void in2_sh64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    help_l2_shift(s, f, o, 63);
}
#define SPEC_in2_sh64 0

static void in2_m2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld8u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_8u 0

static void in2_m2_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld16s(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_16s 0

static void in2_m2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld16u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_16u 0

static void in2_m2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld32s(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_32s 0

static void in2_m2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld32u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_32u 0

static void in2_m2_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_64 0

static void in2_mri2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld16u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_mri2_16u 0

static void in2_mri2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld32s(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_mri2_32s 0

static void in2_mri2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld32u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_mri2_32u 0

static void in2_mri2_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_mri2_64 0

static void in2_m2_32s_atomic(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* XXX should reserve the address */
    in1_la2(s, f, o);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32s(o->in2, o->addr1, get_mem_index(s));
}
#define SPEC_in2_m2_32s_atomic 0

static void in2_m2_64_atomic(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* XXX should reserve the address */
    in1_la2(s, f, o);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(o->in2, o->addr1, get_mem_index(s));
}
#define SPEC_in2_m2_64_atomic 0

static void in2_i2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64(get_field(f, i2));
}
#define SPEC_in2_i2 0

static void in2_i2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint8_t)get_field(f, i2));
}
#define SPEC_in2_i2_8u 0

static void in2_i2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint16_t)get_field(f, i2));
}
#define SPEC_in2_i2_16u 0

static void in2_i2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint32_t)get_field(f, i2));
}
#define SPEC_in2_i2_32u 0

static void in2_i2_16u_shl(DisasContext *s, DisasFields *f, DisasOps *o)
{
    uint64_t i2 = (uint16_t)get_field(f, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_16u_shl 0

static void in2_i2_32u_shl(DisasContext *s, DisasFields *f, DisasOps *o)
{
    uint64_t i2 = (uint32_t)get_field(f, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_32u_shl 0

#ifndef CONFIG_USER_ONLY
static void in2_insn(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64(s->fields->raw_insn);
}
#define SPEC_in2_insn 0
#endif

/* ====================================================================== */

/* Find opc within the table of insns.  This is formulated as a switch
   statement so that (1) we get compile-time notice of cut-paste errors
   for duplicated opcodes, and (2) the compiler generates the binary
   search tree, rather than us having to post-process the table.  */

#define C(OPC, NM, FT, FC, I1, I2, P, W, OP, CC) \
    D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0)

#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) insn_ ## NM,

enum DisasInsnEnum {
#include "insn-data.def"
};

#undef D
#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) {                       \
    .opc = OPC,                                                             \
    .fmt = FMT_##FT,                                                        \
    .fac = FAC_##FC,                                                        \
    .spec = SPEC_in1_##I1 | SPEC_in2_##I2 | SPEC_prep_##P | SPEC_wout_##W,  \
    .name = #NM,                                                            \
    .help_in1 = in1_##I1,                                                   \
    .help_in2 = in2_##I2,                                                   \
    .help_prep = prep_##P,                                                  \
    .help_wout = wout_##W,                                                  \
    .help_cout = cout_##CC,                                                 \
    .help_op = op_##OP,                                                     \
    .data = D                                                               \
 },

/* Allow 0 to be used for NULL in the table below.  */
#define in1_0  NULL
#define in2_0  NULL
#define prep_0  NULL
#define wout_0  NULL
#define cout_0  NULL
#define op_0  NULL

#define SPEC_in1_0 0
#define SPEC_in2_0 0
#define SPEC_prep_0 0
#define SPEC_wout_0 0

static const DisasInsn insn_info[] = {
#include "insn-data.def"
};

#undef D
#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) \
    case OPC: return &insn_info[insn_ ## NM];

static const DisasInsn *lookup_opc(uint16_t opc)
{
    switch (opc) {
#include "insn-data.def"
    default:
        return NULL;
    }
}

#undef D
#undef C

/* Extract a field from the insn.  The INSN should be left-aligned in
   the uint64_t so that we can more easily utilize the big-bit-endian
   definitions we extract from the Principals of Operation.  */

static void extract_field(DisasFields *o, const DisasField *f, uint64_t insn)
{
    uint32_t r, m;

    if (f->size == 0) {
        return;
    }

    /* Zero extract the field from the insn.  */
    r = (insn << f->beg) >> (64 - f->size);

    /* Sign-extend, or un-swap the field as necessary.  */
    switch (f->type) {
    case 0: /* unsigned */
        break;
    case 1: /* signed */
        assert(f->size <= 32);
        m = 1u << (f->size - 1);
        r = (r ^ m) - m;
        break;
    case 2: /* dl+dh split, signed 20 bit. */
        r = ((int8_t)r << 12) | (r >> 8);
        break;
    default:
        abort();
    }

    /* Validate that the "compressed" encoding we selected above is valid.
       I.e. we havn't make two different original fields overlap.  */
    assert(((o->presentC >> f->indexC) & 1) == 0);
    o->presentC |= 1 << f->indexC;
    o->presentO |= 1 << f->indexO;

    o->c[f->indexC] = r;
}

/* Lookup the insn at the current PC, extracting the operands into O and
   returning the info struct for the insn.  Returns NULL for invalid insn.  */

static const DisasInsn *extract_insn(CPUS390XState *env, DisasContext *s,
                                     DisasFields *f)
{
    uint64_t insn, pc = s->pc;
    int op, op2, ilen;
    const DisasInsn *info;

    insn = ld_code2(env, pc);
    op = (insn >> 8) & 0xff;
    ilen = get_ilen(op);
    s->next_pc = s->pc + ilen;

    switch (ilen) {
    case 2:
        insn = insn << 48;
        break;
    case 4:
        insn = ld_code4(env, pc) << 32;
        break;
    case 6:
        insn = (insn << 48) | (ld_code4(env, pc + 2) << 16);
        break;
    default:
        abort();
    }

    /* We can't actually determine the insn format until we've looked up
       the full insn opcode.  Which we can't do without locating the
       secondary opcode.  Assume by default that OP2 is at bit 40; for
       those smaller insns that don't actually have a secondary opcode
       this will correctly result in OP2 = 0. */
    switch (op) {
    case 0x01: /* E */
    case 0x80: /* S */
    case 0x82: /* S */
    case 0x93: /* S */
    case 0xb2: /* S, RRF, RRE */
    case 0xb3: /* RRE, RRD, RRF */
    case 0xb9: /* RRE, RRF */
    case 0xe5: /* SSE, SIL */
        op2 = (insn << 8) >> 56;
        break;
    case 0xa5: /* RI */
    case 0xa7: /* RI */
    case 0xc0: /* RIL */
    case 0xc2: /* RIL */
    case 0xc4: /* RIL */
    case 0xc6: /* RIL */
    case 0xc8: /* SSF */
    case 0xcc: /* RIL */
        op2 = (insn << 12) >> 60;
        break;
    case 0xd0 ... 0xdf: /* SS */
    case 0xe1: /* SS */
    case 0xe2: /* SS */
    case 0xe8: /* SS */
    case 0xe9: /* SS */
    case 0xea: /* SS */
    case 0xee ... 0xf3: /* SS */
    case 0xf8 ... 0xfd: /* SS */
        op2 = 0;
        break;
    default:
        op2 = (insn << 40) >> 56;
        break;
    }

    memset(f, 0, sizeof(*f));
    f->raw_insn = insn;
    f->op = op;
    f->op2 = op2;

    /* Lookup the instruction.  */
    info = lookup_opc(op << 8 | op2);

    /* If we found it, extract the operands.  */
    if (info != NULL) {
        DisasFormat fmt = info->fmt;
        int i;

        for (i = 0; i < NUM_C_FIELD; ++i) {
            extract_field(f, &format_info[fmt].op[i], insn);
        }
    }
    return info;
}

static ExitStatus translate_one(CPUS390XState *env, DisasContext *s)
{
    const DisasInsn *insn;
    ExitStatus ret = NO_EXIT;
    DisasFields f;
    DisasOps o;

    /* Search for the insn in the table.  */
    insn = extract_insn(env, s, &f);

    /* Not found means unimplemented/illegal opcode.  */
    if (insn == NULL) {
        qemu_log_mask(LOG_UNIMP, "unimplemented opcode 0x%02x%02x\n",
                      f.op, f.op2);
        gen_illegal_opcode(s);
        return EXIT_NORETURN;
    }

#ifndef CONFIG_USER_ONLY
    if (s->tb->flags & FLAG_MASK_PER) {
        TCGv_i64 addr = tcg_const_i64(s->pc);
        gen_helper_per_ifetch(cpu_env, addr);
        tcg_temp_free_i64(addr);
    }
#endif

    /* Check for insn specification exceptions.  */
    if (insn->spec) {
        int spec = insn->spec, excp = 0, r;

        if (spec & SPEC_r1_even) {
            r = get_field(&f, r1);
            if (r & 1) {
                excp = PGM_SPECIFICATION;
            }
        }
        if (spec & SPEC_r2_even) {
            r = get_field(&f, r2);
            if (r & 1) {
                excp = PGM_SPECIFICATION;
            }
        }
        if (spec & SPEC_r3_even) {
            r = get_field(&f, r3);
            if (r & 1) {
                excp = PGM_SPECIFICATION;
            }
        }
        if (spec & SPEC_r1_f128) {
            r = get_field(&f, r1);
            if (r > 13) {
                excp = PGM_SPECIFICATION;
            }
        }
        if (spec & SPEC_r2_f128) {
            r = get_field(&f, r2);
            if (r > 13) {
                excp = PGM_SPECIFICATION;
            }
        }
        if (excp) {
            gen_program_exception(s, excp);
            return EXIT_NORETURN;
        }
    }

    /* Set up the strutures we use to communicate with the helpers. */
    s->insn = insn;
    s->fields = &f;
    o.g_out = o.g_out2 = o.g_in1 = o.g_in2 = false;
    TCGV_UNUSED_I64(o.out);
    TCGV_UNUSED_I64(o.out2);
    TCGV_UNUSED_I64(o.in1);
    TCGV_UNUSED_I64(o.in2);
    TCGV_UNUSED_I64(o.addr1);

    /* Implement the instruction.  */
    if (insn->help_in1) {
        insn->help_in1(s, &f, &o);
    }
    if (insn->help_in2) {
        insn->help_in2(s, &f, &o);
    }
    if (insn->help_prep) {
        insn->help_prep(s, &f, &o);
    }
    if (insn->help_op) {
        ret = insn->help_op(s, &o);
    }
    if (insn->help_wout) {
        insn->help_wout(s, &f, &o);
    }
    if (insn->help_cout) {
        insn->help_cout(s, &o);
    }

    /* Free any temporaries created by the helpers.  */
    if (!TCGV_IS_UNUSED_I64(o.out) && !o.g_out) {
        tcg_temp_free_i64(o.out);
    }
    if (!TCGV_IS_UNUSED_I64(o.out2) && !o.g_out2) {
        tcg_temp_free_i64(o.out2);
    }
    if (!TCGV_IS_UNUSED_I64(o.in1) && !o.g_in1) {
        tcg_temp_free_i64(o.in1);
    }
    if (!TCGV_IS_UNUSED_I64(o.in2) && !o.g_in2) {
        tcg_temp_free_i64(o.in2);
    }
    if (!TCGV_IS_UNUSED_I64(o.addr1)) {
        tcg_temp_free_i64(o.addr1);
    }

#ifndef CONFIG_USER_ONLY
    if (s->tb->flags & FLAG_MASK_PER) {
        /* An exception might be triggered, save PSW if not already done.  */
        if (ret == NO_EXIT || ret == EXIT_PC_STALE) {
            tcg_gen_movi_i64(psw_addr, s->next_pc);
        }

        /* Save off cc.  */
        update_cc_op(s);

        /* Call the helper to check for a possible PER exception.  */
        gen_helper_per_check_exception(cpu_env);
    }
#endif

    /* Advance to the next instruction.  */
    s->pc = s->next_pc;
    return ret;
}

void gen_intermediate_code(CPUS390XState *env, struct TranslationBlock *tb)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    DisasContext dc;
    target_ulong pc_start;
    uint64_t next_page_start;
    int num_insns, max_insns;
    ExitStatus status;
    bool do_debug;

    pc_start = tb->pc;

    /* 31-bit mode */
    if (!(tb->flags & FLAG_MASK_64)) {
        pc_start &= 0x7fffffff;
    }

    dc.tb = tb;
    dc.pc = pc_start;
    dc.cc_op = CC_OP_DYNAMIC;
    do_debug = dc.singlestep_enabled = cs->singlestep_enabled;

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    gen_tb_start(tb);

    do {
        tcg_gen_insn_start(dc.pc, dc.cc_op);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, dc.pc, BP_ANY))) {
            status = EXIT_PC_STALE;
            do_debug = true;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            dc.pc += 2;
            break;
        }

        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        status = NO_EXIT;
        if (status == NO_EXIT) {
            status = translate_one(env, &dc);
        }

        /* If we reach a page boundary, are single stepping,
           or exhaust instruction count, stop generation.  */
        if (status == NO_EXIT
            && (dc.pc >= next_page_start
                || tcg_op_buf_full()
                || num_insns >= max_insns
                || singlestep
                || cs->singlestep_enabled)) {
            status = EXIT_PC_STALE;
        }
    } while (status == NO_EXIT);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    switch (status) {
    case EXIT_GOTO_TB:
    case EXIT_NORETURN:
        break;
    case EXIT_PC_STALE:
        update_psw_addr(&dc);
        /* FALLTHRU */
    case EXIT_PC_UPDATED:
        /* Next TB starts off with CC_OP_DYNAMIC, so make sure the
           cc op type is in env */
        update_cc_op(&dc);
        /* Exit the TB, either by raising a debug exception or by return.  */
        if (do_debug) {
            gen_exception(EXCP_DEBUG);
        } else {
            tcg_gen_exit_tb(0);
        }
        break;
    default:
        abort();
    }

    gen_tb_end(tb, num_insns);

    tb->size = dc.pc - pc_start;
    tb->icount = num_insns;

#if defined(S390X_DEBUG_DISAS)
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, dc.pc - pc_start, 1);
        qemu_log("\n");
    }
#endif
}

void restore_state_to_opc(CPUS390XState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    int cc_op = data[1];
    env->psw.addr = data[0];
    if ((cc_op != CC_OP_DYNAMIC) && (cc_op != CC_OP_STATIC)) {
        env->cc_op = cc_op;
    }
}
