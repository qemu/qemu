/*
 *  S/390 translation
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2010 Alexander Graf
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
#include "s390x-internal.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "exec/cpu_ldst.h"
#include "exec/gen-icount.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "qemu/atomic128.h"


/* Information that (most) every instruction needs to manipulate.  */
typedef struct DisasContext DisasContext;
typedef struct DisasInsn DisasInsn;
typedef struct DisasFields DisasFields;

/*
 * Define a structure to hold the decoded fields.  We'll store each inside
 * an array indexed by an enum.  In order to conserve memory, we'll arrange
 * for fields that do not exist at the same time to overlap, thus the "C"
 * for compact.  For checking purposes there is an "O" for original index
 * as well that will be applied to availability bitmaps.
 */

enum DisasFieldIndexO {
    FLD_O_r1,
    FLD_O_r2,
    FLD_O_r3,
    FLD_O_m1,
    FLD_O_m3,
    FLD_O_m4,
    FLD_O_m5,
    FLD_O_m6,
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
    FLD_O_i5,
    FLD_O_v1,
    FLD_O_v2,
    FLD_O_v3,
    FLD_O_v4,
};

enum DisasFieldIndexC {
    FLD_C_r1 = 0,
    FLD_C_m1 = 0,
    FLD_C_b1 = 0,
    FLD_C_i1 = 0,
    FLD_C_v1 = 0,

    FLD_C_r2 = 1,
    FLD_C_b2 = 1,
    FLD_C_i2 = 1,

    FLD_C_r3 = 2,
    FLD_C_m3 = 2,
    FLD_C_i3 = 2,
    FLD_C_v3 = 2,

    FLD_C_m4 = 3,
    FLD_C_b4 = 3,
    FLD_C_i4 = 3,
    FLD_C_l1 = 3,
    FLD_C_v4 = 3,

    FLD_C_i5 = 4,
    FLD_C_d1 = 4,
    FLD_C_m5 = 4,

    FLD_C_d2 = 5,
    FLD_C_m6 = 5,

    FLD_C_d4 = 6,
    FLD_C_x2 = 6,
    FLD_C_l2 = 6,
    FLD_C_v2 = 6,

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

struct DisasContext {
    DisasContextBase base;
    const DisasInsn *insn;
    TCGOp *insn_start;
    DisasFields fields;
    uint64_t ex_value;
    /*
     * During translate_one(), pc_tmp is used to determine the instruction
     * to be executed after base.pc_next - e.g. next sequential instruction
     * or a branch target.
     */
    uint64_t pc_tmp;
    uint32_t ilen;
    enum cc_op cc_op;
    bool exit_to_mainloop;
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

#ifdef DEBUG_INLINE_BRANCHES
static uint64_t inline_branch_hit[CC_OP_MAX];
static uint64_t inline_branch_miss[CC_OP_MAX];
#endif

static void pc_to_link_info(TCGv_i64 out, DisasContext *s, uint64_t pc)
{
    if (s->base.tb->flags & FLAG_MASK_32) {
        if (s->base.tb->flags & FLAG_MASK_64) {
            tcg_gen_movi_i64(out, pc);
            return;
        }
        pc |= 0x80000000;
    }
    assert(!(s->base.tb->flags & FLAG_MASK_64));
    tcg_gen_deposit_i64(out, out, tcg_constant_i64(pc), 0, 32);
}

static TCGv_i64 psw_addr;
static TCGv_i64 psw_mask;
static TCGv_i64 gbea;

static TCGv_i32 cc_op;
static TCGv_i64 cc_src;
static TCGv_i64 cc_dst;
static TCGv_i64 cc_vr;

static char cpu_reg_names[16][4];
static TCGv_i64 regs[16];

void s390x_translate_init(void)
{
    int i;

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
}

static inline int vec_full_reg_offset(uint8_t reg)
{
    g_assert(reg < 32);
    return offsetof(CPUS390XState, vregs[reg][0]);
}

static inline int vec_reg_offset(uint8_t reg, uint8_t enr, MemOp es)
{
    /* Convert element size (es) - e.g. MO_8 - to bytes */
    const uint8_t bytes = 1 << es;
    int offs = enr * bytes;

    /*
     * vregs[n][0] is the lowest 8 byte and vregs[n][1] the highest 8 byte
     * of the 16 byte vector, on both, little and big endian systems.
     *
     * Big Endian (target/possible host)
     * B:  [ 0][ 1][ 2][ 3][ 4][ 5][ 6][ 7] - [ 8][ 9][10][11][12][13][14][15]
     * HW: [     0][     1][     2][     3] - [     4][     5][     6][     7]
     * W:  [             0][             1] - [             2][             3]
     * DW: [                             0] - [                             1]
     *
     * Little Endian (possible host)
     * B:  [ 7][ 6][ 5][ 4][ 3][ 2][ 1][ 0] - [15][14][13][12][11][10][ 9][ 8]
     * HW: [     3][     2][     1][     0] - [     7][     6][     5][     4]
     * W:  [             1][             0] - [             3][             2]
     * DW: [                             0] - [                             1]
     *
     * For 16 byte elements, the two 8 byte halves will not form a host
     * int128 if the host is little endian, since they're in the wrong order.
     * Some operations (e.g. xor) do not care. For operations like addition,
     * the two 8 byte elements have to be loaded separately. Let's force all
     * 16 byte operations to handle it in a special way.
     */
    g_assert(es <= MO_64);
#if !HOST_BIG_ENDIAN
    offs ^= (8 - bytes);
#endif
    return offs + vec_full_reg_offset(reg);
}

static inline int freg64_offset(uint8_t reg)
{
    g_assert(reg < 16);
    return vec_reg_offset(reg, 0, MO_64);
}

static inline int freg32_offset(uint8_t reg)
{
    g_assert(reg < 16);
    return vec_reg_offset(reg, 0, MO_32);
}

static TCGv_i64 load_reg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_mov_i64(r, regs[reg]);
    return r;
}

static TCGv_i64 load_freg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();

    tcg_gen_ld_i64(r, cpu_env, freg64_offset(reg));
    return r;
}

static TCGv_i64 load_freg32_i64(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();

    tcg_gen_ld32u_i64(r, cpu_env, freg32_offset(reg));
    return r;
}

static TCGv_i128 load_freg_128(int reg)
{
    TCGv_i64 h = load_freg(reg);
    TCGv_i64 l = load_freg(reg + 2);
    TCGv_i128 r = tcg_temp_new_i128();

    tcg_gen_concat_i64_i128(r, l, h);
    tcg_temp_free_i64(h);
    tcg_temp_free_i64(l);
    return r;
}

static void store_reg(int reg, TCGv_i64 v)
{
    tcg_gen_mov_i64(regs[reg], v);
}

static void store_freg(int reg, TCGv_i64 v)
{
    tcg_gen_st_i64(v, cpu_env, freg64_offset(reg));
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
    tcg_gen_st32_i64(v, cpu_env, freg32_offset(reg));
}

static void return_low128(TCGv_i64 dest)
{
    tcg_gen_ld_i64(dest, cpu_env, offsetof(CPUS390XState, retxl));
}

static void update_psw_addr(DisasContext *s)
{
    /* psw.addr */
    tcg_gen_movi_i64(psw_addr, s->base.pc_next);
}

static void per_branch(DisasContext *s, bool to_next)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_i64(gbea, s->base.pc_next);

    if (s->base.tb->flags & FLAG_MASK_PER) {
        TCGv_i64 next_pc = to_next ? tcg_constant_i64(s->pc_tmp) : psw_addr;
        gen_helper_per_branch(cpu_env, gbea, next_pc);
    }
#endif
}

static void per_branch_cond(DisasContext *s, TCGCond cond,
                            TCGv_i64 arg1, TCGv_i64 arg2)
{
#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER) {
        TCGLabel *lab = gen_new_label();
        tcg_gen_brcond_i64(tcg_invert_cond(cond), arg1, arg2, lab);

        tcg_gen_movi_i64(gbea, s->base.pc_next);
        gen_helper_per_branch(cpu_env, gbea, psw_addr);

        gen_set_label(lab);
    } else {
        TCGv_i64 pc = tcg_constant_i64(s->base.pc_next);
        tcg_gen_movcond_i64(cond, gbea, arg1, arg2, gbea, pc);
    }
#endif
}

static void per_breaking_event(DisasContext *s)
{
    tcg_gen_movi_i64(gbea, s->base.pc_next);
}

static void update_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC && s->cc_op != CC_OP_STATIC) {
        tcg_gen_movi_i32(cc_op, s->cc_op);
    }
}

static inline uint64_t ld_code2(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    return (uint64_t)translator_lduw(env, &s->base, pc);
}

static inline uint64_t ld_code4(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    return (uint64_t)(uint32_t)translator_ldl(env, &s->base, pc);
}

static int get_mem_index(DisasContext *s)
{
#ifdef CONFIG_USER_ONLY
    return MMU_USER_IDX;
#else
    if (!(s->base.tb->flags & FLAG_MASK_DAT)) {
        return MMU_REAL_IDX;
    }

    switch (s->base.tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> FLAG_MASK_PSW_SHIFT:
        return MMU_PRIMARY_IDX;
    case PSW_ASC_SECONDARY >> FLAG_MASK_PSW_SHIFT:
        return MMU_SECONDARY_IDX;
    case PSW_ASC_HOME >> FLAG_MASK_PSW_SHIFT:
        return MMU_HOME_IDX;
    default:
        tcg_abort();
        break;
    }
#endif
}

static void gen_exception(int excp)
{
    gen_helper_exception(cpu_env, tcg_constant_i32(excp));
}

static void gen_program_exception(DisasContext *s, int code)
{
    /* Remember what pgm exeption this was.  */
    tcg_gen_st_i32(tcg_constant_i32(code), cpu_env,
                   offsetof(CPUS390XState, int_pgm_code));

    tcg_gen_st_i32(tcg_constant_i32(s->ilen), cpu_env,
                   offsetof(CPUS390XState, int_pgm_ilen));

    /* update the psw */
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

static inline void gen_data_exception(uint8_t dxc)
{
    gen_helper_data_exception(cpu_env, tcg_constant_i32(dxc));
}

static inline void gen_trap(DisasContext *s)
{
    /* Set DXC to 0xff */
    gen_data_exception(0xff);
}

static void gen_addi_and_wrap_i64(DisasContext *s, TCGv_i64 dst, TCGv_i64 src,
                                  int64_t imm)
{
    tcg_gen_addi_i64(dst, src, imm);
    if (!(s->base.tb->flags & FLAG_MASK_64)) {
        if (s->base.tb->flags & FLAG_MASK_32) {
            tcg_gen_andi_i64(dst, dst, 0x7fffffff);
        } else {
            tcg_gen_andi_i64(dst, dst, 0x00ffffff);
        }
    }
}

static TCGv_i64 get_address(DisasContext *s, int x2, int b2, int d2)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    /*
     * Note that d2 is limited to 20 bits, signed.  If we crop negative
     * displacements early we create larger immediate addends.
     */
    if (b2 && x2) {
        tcg_gen_add_i64(tmp, regs[b2], regs[x2]);
        gen_addi_and_wrap_i64(s, tmp, tmp, d2);
    } else if (b2) {
        gen_addi_and_wrap_i64(s, tmp, regs[b2], d2);
    } else if (x2) {
        gen_addi_and_wrap_i64(s, tmp, regs[x2], d2);
    } else if (!(s->base.tb->flags & FLAG_MASK_64)) {
        if (s->base.tb->flags & FLAG_MASK_32) {
            tcg_gen_movi_i64(tmp, d2 & 0x7fffffff);
        } else {
            tcg_gen_movi_i64(tmp, d2 & 0x00ffffff);
        }
    } else {
        tcg_gen_movi_i64(tmp, d2);
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
    TCGv_i32 local_cc_op = NULL;
    TCGv_i64 dummy = NULL;

    switch (s->cc_op) {
    default:
        dummy = tcg_constant_i64(0);
        /* FALLTHRU */
    case CC_OP_ADD_64:
    case CC_OP_SUB_64:
    case CC_OP_ADD_32:
    case CC_OP_SUB_32:
        local_cc_op = tcg_constant_i32(s->cc_op);
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
        tcg_gen_setcondi_i64(TCG_COND_NE, cc_dst, cc_dst, 0);
        tcg_gen_extrl_i64_i32(cc_op, cc_dst);
        break;
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
    case CC_OP_LCBB:
    case CC_OP_MULS_32:
        /* 1 argument */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, dummy, cc_dst, dummy);
        break;
    case CC_OP_ADDU:
    case CC_OP_ICM:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_SLA:
    case CC_OP_SUBU:
    case CC_OP_NZ_F128:
    case CC_OP_VC:
    case CC_OP_MULS_64:
        /* 2 arguments */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, cc_src, cc_dst, dummy);
        break;
    case CC_OP_ADD_64:
    case CC_OP_SUB_64:
    case CC_OP_ADD_32:
    case CC_OP_SUB_32:
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

    /* We now have cc in cc_op as constant */
    set_cc_static(s);
}

static bool use_goto_tb(DisasContext *s, uint64_t dest)
{
    if (unlikely(s->base.tb->flags & FLAG_MASK_PER)) {
        return false;
    }
    return translator_use_goto_tb(&s->base, dest);
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

    case CC_OP_ADDU:
    case CC_OP_SUBU:
        switch (mask) {
        case 8 | 2: /* result == 0 */
            cond = TCG_COND_EQ;
            break;
        case 4 | 1: /* result != 0 */
            cond = TCG_COND_NE;
            break;
        case 8 | 4: /* !carry (borrow) */
            cond = old_cc_op == CC_OP_ADDU ? TCG_COND_EQ : TCG_COND_NE;
            break;
        case 2 | 1: /* carry (!borrow) */
            cond = old_cc_op == CC_OP_ADDU ? TCG_COND_NE : TCG_COND_EQ;
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
        c->u.s32.b = tcg_constant_i32(0);
        break;
    case CC_OP_LTGT_32:
    case CC_OP_LTUGTU_32:
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
        c->u.s64.b = tcg_constant_i64(0);
        c->g1 = true;
        break;
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_64:
        c->u.s64.a = cc_src;
        c->u.s64.b = cc_dst;
        c->g1 = c->g2 = true;
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_ICM:
        c->u.s64.a = tcg_temp_new_i64();
        c->u.s64.b = tcg_constant_i64(0);
        tcg_gen_and_i64(c->u.s64.a, cc_src, cc_dst);
        break;

    case CC_OP_ADDU:
    case CC_OP_SUBU:
        c->is_64 = true;
        c->u.s64.b = tcg_constant_i64(0);
        c->g1 = true;
        switch (mask) {
        case 8 | 2:
        case 4 | 1: /* result */
            c->u.s64.a = cc_dst;
            break;
        case 8 | 4:
        case 2 | 1: /* carry */
            c->u.s64.a = cc_src;
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case CC_OP_STATIC:
        c->is_64 = false;
        c->u.s32.a = cc_op;
        c->g1 = true;
        switch (mask) {
        case 0x8 | 0x4 | 0x2: /* cc != 3 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_constant_i32(3);
            break;
        case 0x8 | 0x4 | 0x1: /* cc != 2 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_constant_i32(2);
            break;
        case 0x8 | 0x2 | 0x1: /* cc != 1 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x8 | 0x2: /* cc == 0 || cc == 2 => (cc & 1) == 0 */
            cond = TCG_COND_EQ;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_constant_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x8 | 0x4: /* cc < 2 */
            cond = TCG_COND_LTU;
            c->u.s32.b = tcg_constant_i32(2);
            break;
        case 0x8: /* cc == 0 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(0);
            break;
        case 0x4 | 0x2 | 0x1: /* cc != 0 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_constant_i32(0);
            break;
        case 0x4 | 0x1: /* cc == 1 || cc == 3 => (cc & 1) != 0 */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_constant_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x4: /* cc == 1 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x2 | 0x1: /* cc > 1 */
            cond = TCG_COND_GTU;
            c->u.s32.b = tcg_constant_i32(1);
            break;
        case 0x2: /* cc == 2 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(2);
            break;
        case 0x1: /* cc == 3 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_constant_i32(3);
            break;
        default:
            /* CC is masked by something else: (8 >> cc) & mask.  */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_constant_i32(0);
            tcg_gen_shr_i32(c->u.s32.a, tcg_constant_i32(8), cc_op);
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
#define F6(N, X1, X2, X3, X4, X5, X6) F0(N)

typedef enum {
#include "insn-format.h.inc"
} DisasFormat;

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef F6

/* This is the way fields are to be accessed out of DisasFields.  */
#define have_field(S, F)  have_field1((S), FLD_O_##F)
#define get_field(S, F)   get_field1((S), FLD_O_##F, FLD_C_##F)

static bool have_field1(const DisasContext *s, enum DisasFieldIndexO c)
{
    return (s->fields.presentO >> c) & 1;
}

static int get_field1(const DisasContext *s, enum DisasFieldIndexO o,
                      enum DisasFieldIndexC c)
{
    assert(have_field1(s, o));
    return s->fields.c[c];
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
#define V(N, B)       {  B,  4, 3, FLD_C_v##N, FLD_O_v##N }
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
#define F6(N, X1, X2, X3, X4, X5, X6)       { { X1, X2, X3, X4, X5, X6 } },

static const DisasFormatInfo format_info[] = {
#include "insn-format.h.inc"
};

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef F6
#undef R
#undef M
#undef V
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
    TCGv_i128 out_128, in1_128, in2_128;
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

/* We are not using a goto_tb (for whatever reason), but have updated
   the PC (for whatever reason), so there's no need to do it again on
   exiting the TB.  */
#define DISAS_PC_UPDATED        DISAS_TARGET_0

/* We have updated the PC and CC values.  */
#define DISAS_PC_CC_UPDATED     DISAS_TARGET_2


/* Instruction flags */
#define IF_AFP1     0x0001      /* r1 is a fp reg for HFP/FPS instructions */
#define IF_AFP2     0x0002      /* r2 is a fp reg for HFP/FPS instructions */
#define IF_AFP3     0x0004      /* r3 is a fp reg for HFP/FPS instructions */
#define IF_BFP      0x0008      /* binary floating point instruction */
#define IF_DFP      0x0010      /* decimal floating point instruction */
#define IF_PRIV     0x0020      /* privileged instruction */
#define IF_VEC      0x0040      /* vector instruction */
#define IF_IO       0x0080      /* input/output instruction */

struct DisasInsn {
    unsigned opc:16;
    unsigned flags:16;
    DisasFormat fmt:8;
    unsigned fac:8;
    unsigned spec:8;

    const char *name;

    /* Pre-process arguments before HELP_OP.  */
    void (*help_in1)(DisasContext *, DisasOps *);
    void (*help_in2)(DisasContext *, DisasOps *);
    void (*help_prep)(DisasContext *, DisasOps *);

    /*
     * Post-process output after HELP_OP.
     * Note that these are not called if HELP_OP returns DISAS_NORETURN.
     */
    void (*help_wout)(DisasContext *, DisasOps *);
    void (*help_cout)(DisasContext *, DisasOps *);

    /* Implement the operation itself.  */
    DisasJumpType (*help_op)(DisasContext *, DisasOps *);

    uint64_t data;
};

/* ====================================================================== */
/* Miscellaneous helpers, used by several operations.  */

static DisasJumpType help_goto_direct(DisasContext *s, uint64_t dest)
{
    if (dest == s->pc_tmp) {
        per_branch(s, true);
        return DISAS_NEXT;
    }
    if (use_goto_tb(s, dest)) {
        update_cc_op(s);
        per_breaking_event(s);
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(psw_addr, dest);
        tcg_gen_exit_tb(s->base.tb, 0);
        return DISAS_NORETURN;
    } else {
        tcg_gen_movi_i64(psw_addr, dest);
        per_branch(s, false);
        return DISAS_PC_UPDATED;
    }
}

static DisasJumpType help_branch(DisasContext *s, DisasCompare *c,
                                 bool is_imm, int imm, TCGv_i64 cdest)
{
    DisasJumpType ret;
    uint64_t dest = s->base.pc_next + (int64_t)imm * 2;
    TCGLabel *lab;

    /* Take care of the special cases first.  */
    if (c->cond == TCG_COND_NEVER) {
        ret = DISAS_NEXT;
        goto egress;
    }
    if (is_imm) {
        if (dest == s->pc_tmp) {
            /* Branch to next.  */
            per_branch(s, true);
            ret = DISAS_NEXT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            ret = help_goto_direct(s, dest);
            goto egress;
        }
    } else {
        if (!cdest) {
            /* E.g. bcr %r0 -> no branch.  */
            ret = DISAS_NEXT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            tcg_gen_mov_i64(psw_addr, cdest);
            per_branch(s, false);
            ret = DISAS_PC_UPDATED;
            goto egress;
        }
    }

    if (use_goto_tb(s, s->pc_tmp)) {
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
            tcg_gen_movi_i64(psw_addr, s->pc_tmp);
            tcg_gen_exit_tb(s->base.tb, 0);

            /* Branch taken.  */
            gen_set_label(lab);
            per_breaking_event(s);
            tcg_gen_goto_tb(1);
            tcg_gen_movi_i64(psw_addr, dest);
            tcg_gen_exit_tb(s->base.tb, 1);

            ret = DISAS_NORETURN;
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
            tcg_gen_movi_i64(psw_addr, s->pc_tmp);
            tcg_gen_exit_tb(s->base.tb, 0);

            gen_set_label(lab);
            if (is_imm) {
                tcg_gen_movi_i64(psw_addr, dest);
            }
            per_breaking_event(s);
            ret = DISAS_PC_UPDATED;
        }
    } else {
        /* Fallthru cannot use goto_tb.  This by itself is vanishingly rare.
           Most commonly we're single-stepping or some other condition that
           disables all use of goto_tb.  Just update the PC and exit.  */

        TCGv_i64 next = tcg_constant_i64(s->pc_tmp);
        if (is_imm) {
            cdest = tcg_constant_i64(dest);
        }

        if (c->is_64) {
            tcg_gen_movcond_i64(c->cond, psw_addr, c->u.s64.a, c->u.s64.b,
                                cdest, next);
            per_branch_cond(s, c->cond, c->u.s64.a, c->u.s64.b);
        } else {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 z = tcg_constant_i64(0);
            tcg_gen_setcond_i32(c->cond, t0, c->u.s32.a, c->u.s32.b);
            tcg_gen_extu_i32_i64(t1, t0);
            tcg_temp_free_i32(t0);
            tcg_gen_movcond_i64(TCG_COND_NE, psw_addr, t1, z, cdest, next);
            per_branch_cond(s, TCG_COND_NE, t1, z);
            tcg_temp_free_i64(t1);
        }

        ret = DISAS_PC_UPDATED;
    }

 egress:
    free_compare(c);
    return ret;
}

/* ====================================================================== */
/* The operations.  These perform the bulk of the work for any insn,
   usually after the operands have been loaded and output initialized.  */

static DisasJumpType op_abs(DisasContext *s, DisasOps *o)
{
    tcg_gen_abs_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_absf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffull);
    return DISAS_NEXT;
}

static DisasJumpType op_absf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    return DISAS_NEXT;
}

static DisasJumpType op_absf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in1, 0x7fffffffffffffffull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_add(DisasContext *s, DisasOps *o)
{
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_addu64(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);
    return DISAS_NEXT;
}

/* Compute carry into cc_src. */
static void compute_carry(DisasContext *s)
{
    switch (s->cc_op) {
    case CC_OP_ADDU:
        /* The carry value is already in cc_src (1,0). */
        break;
    case CC_OP_SUBU:
        tcg_gen_addi_i64(cc_src, cc_src, 1);
        break;
    default:
        gen_op_calc_cc(s);
        /* fall through */
    case CC_OP_STATIC:
        /* The carry flag is the msb of CC; compute into cc_src. */
        tcg_gen_extu_i32_i64(cc_src, cc_op);
        tcg_gen_shri_i64(cc_src, cc_src, 1);
        break;
    }
}

static DisasJumpType op_addc32(DisasContext *s, DisasOps *o)
{
    compute_carry(s);
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    tcg_gen_add_i64(o->out, o->out, cc_src);
    return DISAS_NEXT;
}

static DisasJumpType op_addc64(DisasContext *s, DisasOps *o)
{
    compute_carry(s);

    TCGv_i64 zero = tcg_constant_i64(0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, zero, cc_src, zero);
    tcg_gen_add2_i64(o->out, cc_src, o->out, cc_src, o->in2, zero);

    return DISAS_NEXT;
}

static DisasJumpType op_asi(DisasContext *s, DisasOps *o)
{
    bool non_atomic = !s390_has_feat(S390_FEAT_STFLE_45);

    o->in1 = tcg_temp_new_i64();
    if (non_atomic) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        /* Perform the atomic addition in memory. */
        tcg_gen_atomic_fetch_add_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    /* Recompute also for atomic case: needed for setting CC. */
    tcg_gen_add_i64(o->out, o->in1, o->in2);

    if (non_atomic) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_asiu64(DisasContext *s, DisasOps *o)
{
    bool non_atomic = !s390_has_feat(S390_FEAT_STFLE_45);

    o->in1 = tcg_temp_new_i64();
    if (non_atomic) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        /* Perform the atomic addition in memory. */
        tcg_gen_atomic_fetch_add_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    /* Recompute also for atomic case: needed for setting CC. */
    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);

    if (non_atomic) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_aeb(DisasContext *s, DisasOps *o)
{
    gen_helper_aeb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_adb(DisasContext *s, DisasOps *o)
{
    gen_helper_adb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_axb(DisasContext *s, DisasOps *o)
{
    gen_helper_axb(o->out_128, cpu_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_and(DisasContext *s, DisasOps *o)
{
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_andi(DisasContext *s, DisasOps *o)
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
    return DISAS_NEXT;
}

static DisasJumpType op_andc(DisasContext *s, DisasOps *o)
{
    tcg_gen_andc_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_orc(DisasContext *s, DisasOps *o)
{
    tcg_gen_orc_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nand(DisasContext *s, DisasOps *o)
{
    tcg_gen_nand_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nor(DisasContext *s, DisasOps *o)
{
    tcg_gen_nor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nxor(DisasContext *s, DisasOps *o)
{
    tcg_gen_eqv_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ni(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        /* Perform the atomic operation in memory. */
        tcg_gen_atomic_fetch_and_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    /* Recompute also for atomic case: needed for setting CC. */
    tcg_gen_and_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_bas(DisasContext *s, DisasOps *o)
{
    pc_to_link_info(o->out, s, s->pc_tmp);
    if (o->in2) {
        tcg_gen_mov_i64(psw_addr, o->in2);
        per_branch(s, false);
        return DISAS_PC_UPDATED;
    } else {
        return DISAS_NEXT;
    }
}

static void save_link_info(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t;

    if (s->base.tb->flags & (FLAG_MASK_32 | FLAG_MASK_64)) {
        pc_to_link_info(o->out, s, s->pc_tmp);
        return;
    }
    gen_op_calc_cc(s);
    tcg_gen_andi_i64(o->out, o->out, 0xffffffff00000000ull);
    tcg_gen_ori_i64(o->out, o->out, ((s->ilen / 2) << 30) | s->pc_tmp);
    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 16);
    tcg_gen_andi_i64(t, t, 0x0f000000);
    tcg_gen_or_i64(o->out, o->out, t);
    tcg_gen_extu_i32_i64(t, cc_op);
    tcg_gen_shli_i64(t, t, 28);
    tcg_gen_or_i64(o->out, o->out, t);
    tcg_temp_free_i64(t);
}

static DisasJumpType op_bal(DisasContext *s, DisasOps *o)
{
    save_link_info(s, o);
    if (o->in2) {
        tcg_gen_mov_i64(psw_addr, o->in2);
        per_branch(s, false);
        return DISAS_PC_UPDATED;
    } else {
        return DISAS_NEXT;
    }
}

static DisasJumpType op_basi(DisasContext *s, DisasOps *o)
{
    pc_to_link_info(o->out, s, s->pc_tmp);
    return help_goto_direct(s, s->base.pc_next + (int64_t)get_field(s, i2) * 2);
}

static DisasJumpType op_bc(DisasContext *s, DisasOps *o)
{
    int m1 = get_field(s, m1);
    bool is_imm = have_field(s, i2);
    int imm = is_imm ? get_field(s, i2) : 0;
    DisasCompare c;

    /* BCR with R2 = 0 causes no branching */
    if (have_field(s, r2) && get_field(s, r2) == 0) {
        if (m1 == 14) {
            /* Perform serialization */
            /* FIXME: check for fast-BCR-serialization facility */
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        }
        if (m1 == 15) {
            /* Perform serialization */
            /* FIXME: perform checkpoint-synchronisation */
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        }
        return DISAS_NEXT;
    }

    disas_jcc(s, &c, m1);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bct32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    bool is_imm = have_field(s, i2);
    int imm = is_imm ? get_field(s, i2) : 0;
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
    c.u.s32.b = tcg_constant_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bcth(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int imm = get_field(s, i2);
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
    c.u.s32.b = tcg_constant_i32(0);
    tcg_gen_extrl_i64_i32(c.u.s32.a, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, 1, imm, o->in2);
}

static DisasJumpType op_bct64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    bool is_imm = have_field(s, i2);
    int imm = is_imm ? get_field(s, i2) : 0;
    DisasCompare c;

    c.cond = TCG_COND_NE;
    c.is_64 = true;
    c.g1 = true;
    c.g2 = false;

    tcg_gen_subi_i64(regs[r1], regs[r1], 1);
    c.u.s64.a = regs[r1];
    c.u.s64.b = tcg_constant_i64(0);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static DisasJumpType op_bx32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    bool is_imm = have_field(s, i2);
    int imm = is_imm ? get_field(s, i2) : 0;
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

static DisasJumpType op_bx64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    bool is_imm = have_field(s, i2);
    int imm = is_imm ? get_field(s, i2) : 0;
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

static DisasJumpType op_cj(DisasContext *s, DisasOps *o)
{
    int imm, m3 = get_field(s, m3);
    bool is_imm;
    DisasCompare c;

    c.cond = ltgt_cond[m3];
    if (s->insn->data) {
        c.cond = tcg_unsigned_cond(c.cond);
    }
    c.is_64 = c.g1 = c.g2 = true;
    c.u.s64.a = o->in1;
    c.u.s64.b = o->in2;

    is_imm = have_field(s, i4);
    if (is_imm) {
        imm = get_field(s, i4);
    } else {
        imm = 0;
        o->out = get_address(s, 0, get_field(s, b4),
                             get_field(s, d4));
    }

    return help_branch(s, &c, is_imm, imm, o->out);
}

static DisasJumpType op_ceb(DisasContext *s, DisasOps *o)
{
    gen_helper_ceb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cdb(DisasContext *s, DisasOps *o)
{
    gen_helper_cdb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cxb(DisasContext *s, DisasOps *o)
{
    gen_helper_cxb(cc_op, cpu_env, o->in1_128, o->in2_128);
    set_cc_static(s);
    return DISAS_NEXT;
}

static TCGv_i32 fpinst_extract_m34(DisasContext *s, bool m3_with_fpe,
                                   bool m4_with_fpe)
{
    const bool fpe = s390_has_feat(S390_FEAT_FLOATING_POINT_EXT);
    uint8_t m3 = get_field(s, m3);
    uint8_t m4 = get_field(s, m4);

    /* m3 field was introduced with FPE */
    if (!fpe && m3_with_fpe) {
        m3 = 0;
    }
    /* m4 field was introduced with FPE */
    if (!fpe && m4_with_fpe) {
        m4 = 0;
    }

    /* Check for valid rounding modes. Mode 3 was introduced later. */
    if (m3 == 2 || m3 > 7 || (!fpe && m3 == 3)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return NULL;
    }

    return tcg_constant_i32(deposit32(m3, 4, 4, m4));
}

static DisasJumpType op_cfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfeb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfdb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cfxb(o->out, cpu_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgeb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgdb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cgxb(o->out, cpu_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfeb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfdb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clfxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clfxb(o->out, cpu_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgeb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgeb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgdb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgdb(o->out, cpu_env, o->in2, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clgxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_clgxb(o->out, cpu_env, o->in2_128, m34);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cegb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cegb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cdgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cdgb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cxgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cxgb(o->out_128, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_celgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_celgb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cdlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cdlgb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cxlgb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, false);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_cxlgb(o->out_128, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_cksm(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    TCGv_i128 pair = tcg_temp_new_i128();
    TCGv_i64 len = tcg_temp_new_i64();

    gen_helper_cksm(pair, cpu_env, o->in1, o->in2, regs[r2 + 1]);
    set_cc_static(s);
    tcg_gen_extr_i128_i64(o->out, len, pair);
    tcg_temp_free_i128(pair);

    tcg_gen_add_i64(regs[r2], regs[r2], len);
    tcg_gen_sub_i64(regs[r2 + 1], regs[r2 + 1], len);
    tcg_temp_free_i64(len);

    return DISAS_NEXT;
}

static DisasJumpType op_clc(DisasContext *s, DisasOps *o)
{
    int l = get_field(s, l1);
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
        vl = tcg_constant_i32(l);
        gen_helper_clc(cc_op, cpu_env, vl, o->addr1, o->in2);
        set_cc_static(s);
        return DISAS_NEXT;
    }
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, cc_src, cc_dst);
    return DISAS_NEXT;
}

static DisasJumpType op_clcl(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 t1, t2;

    /* r1 and r2 must be even.  */
    if (r1 & 1 || r2 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t2 = tcg_constant_i32(r2);
    gen_helper_clcl(cc_op, cpu_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clcle(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    /* r1 and r3 must be even.  */
    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_clcle(cc_op, cpu_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clclu(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    /* r1 and r3 must be even.  */
    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_clclu(cc_op, cpu_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_clm(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_constant_i32(get_field(s, m3));
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t1, o->in1);
    gen_helper_clm(cc_op, cpu_env, t1, m3, o->in2);
    set_cc_static(s);
    tcg_temp_free_i32(t1);
    return DISAS_NEXT;
}

static DisasJumpType op_clst(DisasContext *s, DisasOps *o)
{
    TCGv_i128 pair = tcg_temp_new_i128();

    gen_helper_clst(pair, cpu_env, regs[0], o->in1, o->in2);
    tcg_gen_extr_i128_i64(o->in2, o->in1, pair);
    tcg_temp_free_i128(pair);

    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_cps(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_andi_i64(t, o->in1, 0x8000000000000000ull);
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffffffffffffull);
    tcg_gen_or_i64(o->out, o->out, t);
    tcg_temp_free_i64(t);
    return DISAS_NEXT;
}

static DisasJumpType op_cs(DisasContext *s, DisasOps *o)
{
    int d2 = get_field(s, d2);
    int b2 = get_field(s, b2);
    TCGv_i64 addr, cc;

    /* Note that in1 = R3 (new value) and
       in2 = (zero-extended) R1 (expected value).  */

    addr = get_address(s, 0, b2, d2);
    tcg_gen_atomic_cmpxchg_i64(o->out, addr, o->in2, o->in1,
                               get_mem_index(s), s->insn->data | MO_ALIGN);
    tcg_temp_free_i64(addr);

    /* Are the memory and expected values (un)equal?  Note that this setcond
       produces the output CC value, thus the NE sense of the test.  */
    cc = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, cc, o->in2, o->out);
    tcg_gen_extrl_i64_i32(cc_op, cc);
    tcg_temp_free_i64(cc);
    set_cc_static(s);

    return DISAS_NEXT;
}

static DisasJumpType op_cdsg(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);

    o->out_128 = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(o->out_128, regs[r1 + 1], regs[r1]);

    /* Note out (R1:R1+1) = expected value and in2 (R3:R3+1) = new value.  */
    tcg_gen_atomic_cmpxchg_i128(o->out_128, o->addr1, o->out_128, o->in2_128,
                                get_mem_index(s), MO_BE | MO_128 | MO_ALIGN);

    /*
     * Extract result into cc_dst:cc_src, compare vs the expected value
     * in the as yet unmodified input registers, then update CC_OP.
     */
    tcg_gen_extr_i128_i64(cc_src, cc_dst, o->out_128);
    tcg_gen_xor_i64(cc_dst, cc_dst, regs[r1]);
    tcg_gen_xor_i64(cc_src, cc_src, regs[r1 + 1]);
    tcg_gen_or_i64(cc_dst, cc_dst, cc_src);
    set_cc_nz_u64(s, cc_dst);

    return DISAS_NEXT;
}

static DisasJumpType op_csst(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    TCGv_i32 t_r3 = tcg_constant_i32(r3);

    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        gen_helper_csst_parallel(cc_op, cpu_env, t_r3, o->addr1, o->in2);
    } else {
        gen_helper_csst(cc_op, cpu_env, t_r3, o->addr1, o->in2);
    }

    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_csp(DisasContext *s, DisasOps *o)
{
    MemOp mop = s->insn->data;
    TCGv_i64 addr, old, cc;
    TCGLabel *lab = gen_new_label();

    /* Note that in1 = R1 (zero-extended expected value),
       out = R1 (original reg), out2 = R1+1 (new value).  */

    addr = tcg_temp_new_i64();
    old = tcg_temp_new_i64();
    tcg_gen_andi_i64(addr, o->in2, -1ULL << (mop & MO_SIZE));
    tcg_gen_atomic_cmpxchg_i64(old, addr, o->in1, o->out2,
                               get_mem_index(s), mop | MO_ALIGN);
    tcg_temp_free_i64(addr);

    /* Are the memory and expected values (un)equal?  */
    cc = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_NE, cc, o->in1, old);
    tcg_gen_extrl_i64_i32(cc_op, cc);

    /* Write back the output now, so that it happens before the
       following branch, so that we don't need local temps.  */
    if ((mop & MO_SIZE) == MO_32) {
        tcg_gen_deposit_i64(o->out, o->out, old, 0, 32);
    } else {
        tcg_gen_mov_i64(o->out, old);
    }
    tcg_temp_free_i64(old);

    /* If the comparison was equal, and the LSB of R2 was set,
       then we need to flush the TLB (for all cpus).  */
    tcg_gen_xori_i64(cc, cc, 1);
    tcg_gen_and_i64(cc, cc, o->in2);
    tcg_gen_brcondi_i64(TCG_COND_EQ, cc, 0, lab);
    tcg_temp_free_i64(cc);

    gen_helper_purge(cpu_env);
    gen_set_label(lab);

    return DISAS_NEXT;
}
#endif

static DisasJumpType op_cvd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t2, o->in1);
    gen_helper_cvd(t1, t2);
    tcg_temp_free_i32(t2);
    tcg_gen_qemu_st64(t1, o->in2, get_mem_index(s));
    tcg_temp_free_i64(t1);
    return DISAS_NEXT;
}

static DisasJumpType op_ct(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
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
    return DISAS_NEXT;
}

static DisasJumpType op_cuXX(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 tr1, tr2, chk;

    /* R1 and R2 must both be even.  */
    if ((r1 | r2) & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    if (!s390_has_feat(S390_FEAT_ETF3_ENH)) {
        m3 = 0;
    }

    tr1 = tcg_constant_i32(r1);
    tr2 = tcg_constant_i32(r2);
    chk = tcg_constant_i32(m3);

    switch (s->insn->data) {
    case 12:
        gen_helper_cu12(cc_op, cpu_env, tr1, tr2, chk);
        break;
    case 14:
        gen_helper_cu14(cc_op, cpu_env, tr1, tr2, chk);
        break;
    case 21:
        gen_helper_cu21(cc_op, cpu_env, tr1, tr2, chk);
        break;
    case 24:
        gen_helper_cu24(cc_op, cpu_env, tr1, tr2, chk);
        break;
    case 41:
        gen_helper_cu41(cc_op, cpu_env, tr1, tr2, chk);
        break;
    case 42:
        gen_helper_cu42(cc_op, cpu_env, tr1, tr2, chk);
        break;
    default:
        g_assert_not_reached();
    }

    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_diag(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));
    TCGv_i32 func_code = tcg_constant_i32(get_field(s, i2));

    gen_helper_diag(cpu_env, r1, r3, func_code);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_divs32(DisasContext *s, DisasOps *o)
{
    gen_helper_divs32(o->out, cpu_env, o->in1, o->in2);
    tcg_gen_extr32_i64(o->out2, o->out, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_divu32(DisasContext *s, DisasOps *o)
{
    gen_helper_divu32(o->out, cpu_env, o->in1, o->in2);
    tcg_gen_extr32_i64(o->out2, o->out, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_divs64(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();

    gen_helper_divs64(t, cpu_env, o->in1, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, t);
    tcg_temp_free_i128(t);
    return DISAS_NEXT;
}

static DisasJumpType op_divu64(DisasContext *s, DisasOps *o)
{
    TCGv_i128 t = tcg_temp_new_i128();

    gen_helper_divu64(t, cpu_env, o->out, o->out2, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, t);
    tcg_temp_free_i128(t);
    return DISAS_NEXT;
}

static DisasJumpType op_deb(DisasContext *s, DisasOps *o)
{
    gen_helper_deb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ddb(DisasContext *s, DisasOps *o)
{
    gen_helper_ddb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_dxb(DisasContext *s, DisasOps *o)
{
    gen_helper_dxb(o->out_128, cpu_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_ear(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, aregs[r2]));
    return DISAS_NEXT;
}

static DisasJumpType op_ecag(DisasContext *s, DisasOps *o)
{
    /* No cache information provided.  */
    tcg_gen_movi_i64(o->out, -1);
    return DISAS_NEXT;
}

static DisasJumpType op_efpc(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, fpc));
    return DISAS_NEXT;
}

static DisasJumpType op_epsw(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i64 t = tcg_temp_new_i64();

    /* Note the "subsequently" in the PoO, which implies a defined result
       if r1 == r2.  Thus we cannot defer these writes to an output hook.  */
    tcg_gen_shri_i64(t, psw_mask, 32);
    store_reg32_i64(r1, t);
    if (r2 != 0) {
        store_reg32_i64(r2, psw_mask);
    }

    tcg_temp_free_i64(t);
    return DISAS_NEXT;
}

static DisasJumpType op_ex(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    TCGv_i32 ilen;
    TCGv_i64 v1;

    /* Nested EXECUTE is not allowed.  */
    if (unlikely(s->ex_value)) {
        gen_program_exception(s, PGM_EXECUTE);
        return DISAS_NORETURN;
    }

    update_psw_addr(s);
    update_cc_op(s);

    if (r1 == 0) {
        v1 = tcg_constant_i64(0);
    } else {
        v1 = regs[r1];
    }

    ilen = tcg_constant_i32(s->ilen);
    gen_helper_ex(cpu_env, ilen, v1, o->in2);

    return DISAS_PC_CC_UPDATED;
}

static DisasJumpType op_fieb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fieb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_fidb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fidb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_fixb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, false, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_fixb(o->out_128, cpu_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_flogr(DisasContext *s, DisasOps *o)
{
    /* We'll use the original input for cc computation, since we get to
       compare that against 0, which ought to be better than comparing
       the real output against 64.  It also lets cc_dst be a convenient
       temporary during our computation.  */
    gen_op_update1_cc_i64(s, CC_OP_FLOGR, o->in2);

    /* R1 = IN ? CLZ(IN) : 64.  */
    tcg_gen_clzi_i64(o->out, o->in2, 64);

    /* R1+1 = IN & ~(found bit).  Note that we may attempt to shift this
       value by 64, which is undefined.  But since the shift is 64 iff the
       input is zero, we still get the correct result after and'ing.  */
    tcg_gen_movi_i64(o->out2, 0x8000000000000000ull);
    tcg_gen_shr_i64(o->out2, o->out2, o->out);
    tcg_gen_andc_i64(o->out2, cc_dst, o->out2);
    return DISAS_NEXT;
}

static DisasJumpType op_icm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
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
                ccm |= 0xffull << pos;
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }

    tcg_gen_movi_i64(tmp, ccm);
    gen_op_update2_cc_i64(s, CC_OP_ICM, tmp, o->out);
    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_insi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    tcg_gen_deposit_i64(o->out, o->in1, o->in2, shift, size);
    return DISAS_NEXT;
}

static DisasJumpType op_ipm(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    gen_op_calc_cc(s);
    t1 = tcg_temp_new_i64();
    tcg_gen_extract_i64(t1, psw_mask, 40, 4);
    t2 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t2, cc_op);
    tcg_gen_deposit_i64(t1, t1, t2, 4, 60);
    tcg_gen_deposit_i64(o->out, o->out, t1, 24, 8);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_idte(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m4;

    if (s390_has_feat(S390_FEAT_LOCAL_TLB_CLEARING)) {
        m4 = tcg_constant_i32(get_field(s, m4));
    } else {
        m4 = tcg_constant_i32(0);
    }
    gen_helper_idte(cpu_env, o->in1, o->in2, m4);
    return DISAS_NEXT;
}

static DisasJumpType op_ipte(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m4;

    if (s390_has_feat(S390_FEAT_LOCAL_TLB_CLEARING)) {
        m4 = tcg_constant_i32(get_field(s, m4));
    } else {
        m4 = tcg_constant_i32(0);
    }
    gen_helper_ipte(cpu_env, o->in1, o->in2, m4);
    return DISAS_NEXT;
}

static DisasJumpType op_iske(DisasContext *s, DisasOps *o)
{
    gen_helper_iske(o->out, cpu_env, o->in2);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_msa(DisasContext *s, DisasOps *o)
{
    int r1 = have_field(s, r1) ? get_field(s, r1) : 0;
    int r2 = have_field(s, r2) ? get_field(s, r2) : 0;
    int r3 = have_field(s, r3) ? get_field(s, r3) : 0;
    TCGv_i32 t_r1, t_r2, t_r3, type;

    switch (s->insn->data) {
    case S390_FEAT_TYPE_KMA:
        if (r3 == r1 || r3 == r2) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_KMCTR:
        if (r3 & 1 || !r3) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_PPNO:
    case S390_FEAT_TYPE_KMF:
    case S390_FEAT_TYPE_KMC:
    case S390_FEAT_TYPE_KMO:
    case S390_FEAT_TYPE_KM:
        if (r1 & 1 || !r1) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_KMAC:
    case S390_FEAT_TYPE_KIMD:
    case S390_FEAT_TYPE_KLMD:
        if (r2 & 1 || !r2) {
            gen_program_exception(s, PGM_SPECIFICATION);
            return DISAS_NORETURN;
        }
        /* FALL THROUGH */
    case S390_FEAT_TYPE_PCKMO:
    case S390_FEAT_TYPE_PCC:
        break;
    default:
        g_assert_not_reached();
    };

    t_r1 = tcg_constant_i32(r1);
    t_r2 = tcg_constant_i32(r2);
    t_r3 = tcg_constant_i32(r3);
    type = tcg_constant_i32(s->insn->data);
    gen_helper_msa(cc_op, cpu_env, t_r1, t_r2, t_r3, type);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_keb(DisasContext *s, DisasOps *o)
{
    gen_helper_keb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_kdb(DisasContext *s, DisasOps *o)
{
    gen_helper_kdb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_kxb(DisasContext *s, DisasOps *o)
{
    gen_helper_kxb(cc_op, cpu_env, o->in1_128, o->in2_128);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_laa(DisasContext *s, DisasOps *o)
{
    /* The real output is indeed the original value in memory;
       recompute the addition for the computation of CC.  */
    tcg_gen_atomic_fetch_add_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    /* However, we need to recompute the addition for setting CC.  */
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lan(DisasContext *s, DisasOps *o)
{
    /* The real output is indeed the original value in memory;
       recompute the addition for the computation of CC.  */
    tcg_gen_atomic_fetch_and_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    /* However, we need to recompute the operation for setting CC.  */
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lao(DisasContext *s, DisasOps *o)
{
    /* The real output is indeed the original value in memory;
       recompute the addition for the computation of CC.  */
    tcg_gen_atomic_fetch_or_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                s->insn->data | MO_ALIGN);
    /* However, we need to recompute the operation for setting CC.  */
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lax(DisasContext *s, DisasOps *o)
{
    /* The real output is indeed the original value in memory;
       recompute the addition for the computation of CC.  */
    tcg_gen_atomic_fetch_xor_i64(o->in2, o->in2, o->in1, get_mem_index(s),
                                 s->insn->data | MO_ALIGN);
    /* However, we need to recompute the operation for setting CC.  */
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ldeb(DisasContext *s, DisasOps *o)
{
    gen_helper_ldeb(o->out, cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ledb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_ledb(o->out, cpu_env, o->in2, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_ldxb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_ldxb(o->out, cpu_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_lexb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m34 = fpinst_extract_m34(s, true, true);

    if (!m34) {
        return DISAS_NORETURN;
    }
    gen_helper_lexb(o->out, cpu_env, o->in2_128, m34);
    return DISAS_NEXT;
}

static DisasJumpType op_lxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxdb(o->out_128, cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lxeb(DisasContext *s, DisasOps *o)
{
    gen_helper_lxeb(o->out_128, cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_lde(DisasContext *s, DisasOps *o)
{
    tcg_gen_shli_i64(o->out, o->in2, 32);
    return DISAS_NEXT;
}

static DisasJumpType op_llgt(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    return DISAS_NEXT;
}

static DisasJumpType op_ld8s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8s(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld8u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8u(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld16s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16s(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld16u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16u(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld32s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32s(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld32u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32u(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_ld64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld64(o->out, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_lat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32_i64(get_field(s, r1), o->in2);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_lgat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_qemu_ld64(o->out, o->in2, get_mem_index(s));
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_lfhat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    store_reg32h_i64(get_field(s, r1), o->in2);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->in2, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_llgfat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_qemu_ld32u(o->out, o->in2, get_mem_index(s));
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_llgtat(DisasContext *s, DisasOps *o)
{
    TCGLabel *lab = gen_new_label();
    tcg_gen_andi_i64(o->out, o->in2, 0x7fffffff);
    /* The value is stored even in case of trap. */
    tcg_gen_brcondi_i64(TCG_COND_NE, o->out, 0, lab);
    gen_trap(s);
    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_loc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;

    if (have_field(s, m3)) {
        /* LOAD * ON CONDITION */
        disas_jcc(s, &c, get_field(s, m3));
    } else {
        /* SELECT */
        disas_jcc(s, &c, get_field(s, m4));
    }

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

        z = tcg_constant_i64(0);
        tcg_gen_movcond_i64(TCG_COND_NE, o->out, t, z, o->in2, o->in1);
        tcg_temp_free_i64(t);
    }

    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_lctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lctl(cpu_env, r1, o->in2, r3);
    /* Exit to main loop to reevaluate s390_cpu_exec_interrupt.  */
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_lctlg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lctlg(cpu_env, r1, o->in2, r3);
    /* Exit to main loop to reevaluate s390_cpu_exec_interrupt.  */
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_lra(DisasContext *s, DisasOps *o)
{
    gen_helper_lra(o->out, cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_lpp(DisasContext *s, DisasOps *o)
{
    tcg_gen_st_i64(o->in2, cpu_env, offsetof(CPUS390XState, pp));
    return DISAS_NEXT;
}

static DisasJumpType op_lpsw(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    per_breaking_event(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s),
                        MO_TEUL | MO_ALIGN_8);
    tcg_gen_addi_i64(o->in2, o->in2, 4);
    tcg_gen_qemu_ld32u(t2, o->in2, get_mem_index(s));
    /* Convert the 32-bit PSW_MASK into the 64-bit PSW_MASK.  */
    tcg_gen_shli_i64(t1, t1, 32);
    gen_helper_load_psw(cpu_env, t1, t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return DISAS_NORETURN;
}

static DisasJumpType op_lpswe(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    per_breaking_event(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(t1, o->in2, get_mem_index(s),
                        MO_TEUQ | MO_ALIGN_8);
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_ld64(t2, o->in2, get_mem_index(s));
    gen_helper_load_psw(cpu_env, t1, t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return DISAS_NORETURN;
}
#endif

static DisasJumpType op_lam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_lam(cpu_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_lm32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32_i64(r1, t1);
        tcg_temp_free(t1);
        return DISAS_NEXT;
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
        return DISAS_NEXT;
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

    return DISAS_NEXT;
}

static DisasJumpType op_lmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    t1 = tcg_temp_new_i64();
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
        store_reg32h_i64(r1, t1);
        tcg_temp_free(t1);
        return DISAS_NEXT;
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
        return DISAS_NEXT;
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

    return DISAS_NEXT;
}

static DisasJumpType op_lm64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t1, t2;

    /* Only one register to read. */
    if (unlikely(r1 == r3)) {
        tcg_gen_qemu_ld64(regs[r1], o->in2, get_mem_index(s));
        return DISAS_NEXT;
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
        return DISAS_NEXT;
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

    return DISAS_NEXT;
}

static DisasJumpType op_lpd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 a1, a2;
    MemOp mop = s->insn->data;

    /* In a parallel context, stop the world and single step.  */
    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        update_psw_addr(s);
        update_cc_op(s);
        gen_exception(EXCP_ATOMIC);
        return DISAS_NORETURN;
    }

    /* In a serial context, perform the two loads ... */
    a1 = get_address(s, 0, get_field(s, b1), get_field(s, d1));
    a2 = get_address(s, 0, get_field(s, b2), get_field(s, d2));
    tcg_gen_qemu_ld_i64(o->out, a1, get_mem_index(s), mop | MO_ALIGN);
    tcg_gen_qemu_ld_i64(o->out2, a2, get_mem_index(s), mop | MO_ALIGN);
    tcg_temp_free_i64(a1);
    tcg_temp_free_i64(a2);

    /* ... and indicate that we performed them while interlocked.  */
    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

static DisasJumpType op_lpq(DisasContext *s, DisasOps *o)
{
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        gen_helper_lpq(o->out, cpu_env, o->in2);
    } else if (HAVE_ATOMIC128) {
        gen_helper_lpq_parallel(o->out, cpu_env, o->in2);
    } else {
        gen_helper_exit_atomic(cpu_env);
        return DISAS_NORETURN;
    }
    return_low128(o->out2);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_lura(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld_tl(o->out, o->in2, MMU_REAL_IDX, s->insn->data);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_lzrb(DisasContext *s, DisasOps *o)
{
    tcg_gen_andi_i64(o->out, o->in2, -256);
    return DISAS_NEXT;
}

static DisasJumpType op_lcbb(DisasContext *s, DisasOps *o)
{
    const int64_t block_size = (1ull << (get_field(s, m3) + 6));

    if (get_field(s, m3) > 6) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    tcg_gen_ori_i64(o->addr1, o->addr1, -block_size);
    tcg_gen_neg_i64(o->addr1, o->addr1);
    tcg_gen_movi_i64(o->out, 16);
    tcg_gen_umin_i64(o->out, o->out, o->addr1);
    gen_op_update1_cc_i64(s, CC_OP_LCBB, o->out);
    return DISAS_NEXT;
}

static DisasJumpType op_mc(DisasContext *s, DisasOps *o)
{
    const uint16_t monitor_class = get_field(s, i2);

    if (monitor_class & 0xff00) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

#if !defined(CONFIG_USER_ONLY)
    gen_helper_monitor_call(cpu_env, o->addr1,
                            tcg_constant_i32(monitor_class));
#endif
    /* Defaults to a NOP. */
    return DISAS_NEXT;
}

static DisasJumpType op_mov2(DisasContext *s, DisasOps *o)
{
    o->out = o->in2;
    o->g_out = o->g_in2;
    o->in2 = NULL;
    o->g_in2 = false;
    return DISAS_NEXT;
}

static DisasJumpType op_mov2e(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s, b2);
    TCGv ar1 = tcg_temp_new_i64();

    o->out = o->in2;
    o->g_out = o->g_in2;
    o->in2 = NULL;
    o->g_in2 = false;

    switch (s->base.tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 0);
        break;
    case PSW_ASC_ACCREG >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 1);
        break;
    case PSW_ASC_SECONDARY >> FLAG_MASK_PSW_SHIFT:
        if (b2) {
            tcg_gen_ld32u_i64(ar1, cpu_env, offsetof(CPUS390XState, aregs[b2]));
        } else {
            tcg_gen_movi_i64(ar1, 0);
        }
        break;
    case PSW_ASC_HOME >> FLAG_MASK_PSW_SHIFT:
        tcg_gen_movi_i64(ar1, 2);
        break;
    }

    tcg_gen_st32_i64(ar1, cpu_env, offsetof(CPUS390XState, aregs[1]));
    tcg_temp_free_i64(ar1);

    return DISAS_NEXT;
}

static DisasJumpType op_movx(DisasContext *s, DisasOps *o)
{
    o->out = o->in1;
    o->out2 = o->in2;
    o->g_out = o->g_in1;
    o->g_out2 = o->g_in2;
    o->in1 = NULL;
    o->in2 = NULL;
    o->g_in1 = o->g_in2 = false;
    return DISAS_NEXT;
}

static DisasJumpType op_mvc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvc(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcrl(DisasContext *s, DisasOps *o)
{
    gen_helper_mvcrl(cpu_env, regs[0], o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcin(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvcin(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcl(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r2 = get_field(s, r2);
    TCGv_i32 t1, t2;

    /* r1 and r2 must be even.  */
    if (r1 & 1 || r2 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t2 = tcg_constant_i32(r2);
    gen_helper_mvcl(cc_op, cpu_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcle(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    /* r1 and r3 must be even.  */
    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_mvcle(cc_op, cpu_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvclu(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i32 t1, t3;

    /* r1 and r3 must be even.  */
    if (r1 & 1 || r3 & 1) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }

    t1 = tcg_constant_i32(r1);
    t3 = tcg_constant_i32(r3);
    gen_helper_mvclu(cc_op, cpu_env, t1, o->in2, t3);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcos(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    gen_helper_mvcos(cc_op, cpu_env, o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_mvcp(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, l1);
    int r3 = get_field(s, r3);
    gen_helper_mvcp(cc_op, cpu_env, regs[r1], o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvcs(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, l1);
    int r3 = get_field(s, r3);
    gen_helper_mvcs(cc_op, cpu_env, regs[r1], o->addr1, o->in2, regs[r3]);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_mvn(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvn(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvo(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvo(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mvpg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 t2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_mvpg(cc_op, cpu_env, regs[0], t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvst(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 t2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_mvst(cc_op, cpu_env, t1, t2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mvz(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_mvz(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mul(DisasContext *s, DisasOps *o)
{
    tcg_gen_mul_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mul128(DisasContext *s, DisasOps *o)
{
    tcg_gen_mulu2_i64(o->out2, o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_muls128(DisasContext *s, DisasOps *o)
{
    tcg_gen_muls2_i64(o->out2, o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_meeb(DisasContext *s, DisasOps *o)
{
    gen_helper_meeb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mdeb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdeb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mdb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_mxb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxb(o->out_128, cpu_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_mxdb(DisasContext *s, DisasOps *o)
{
    gen_helper_mxdb(o->out_128, cpu_env, o->in1_128, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_maeb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s, r3));
    gen_helper_maeb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return DISAS_NEXT;
}

static DisasJumpType op_madb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg(get_field(s, r3));
    gen_helper_madb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return DISAS_NEXT;
}

static DisasJumpType op_mseb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg32_i64(get_field(s, r3));
    gen_helper_mseb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return DISAS_NEXT;
}

static DisasJumpType op_msdb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 r3 = load_freg(get_field(s, r3));
    gen_helper_msdb(o->out, cpu_env, o->in1, o->in2, r3);
    tcg_temp_free_i64(r3);
    return DISAS_NEXT;
}

static DisasJumpType op_nabs(DisasContext *s, DisasOps *o)
{
    TCGv_i64 z = tcg_constant_i64(0);
    TCGv_i64 n = tcg_temp_new_i64();

    tcg_gen_neg_i64(n, o->in2);
    tcg_gen_movcond_i64(TCG_COND_GE, o->out, o->in2, z, n, o->in2);
    tcg_temp_free_i64(n);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x80000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in2, 0x8000000000000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_nabsf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_ori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_nc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_nc(cc_op, cpu_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_neg(DisasContext *s, DisasOps *o)
{
    tcg_gen_neg_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_negf32(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x80000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_negf64(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in2, 0x8000000000000000ull);
    return DISAS_NEXT;
}

static DisasJumpType op_negf128(DisasContext *s, DisasOps *o)
{
    tcg_gen_xori_i64(o->out, o->in1, 0x8000000000000000ull);
    tcg_gen_mov_i64(o->out2, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_oc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_oc(cc_op, cpu_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_or(DisasContext *s, DisasOps *o)
{
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ori(DisasContext *s, DisasOps *o)
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
    return DISAS_NEXT;
}

static DisasJumpType op_oi(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        /* Perform the atomic operation in memory. */
        tcg_gen_atomic_fetch_or_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                    s->insn->data);
    }

    /* Recompute also for atomic case: needed for setting CC. */
    tcg_gen_or_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_pack(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_pack(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_pka(DisasContext *s, DisasOps *o)
{
    int l2 = get_field(s, l2) + 1;
    TCGv_i32 l;

    /* The length must not exceed 32 bytes.  */
    if (l2 > 32) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l2);
    gen_helper_pka(cpu_env, o->addr1, o->in2, l);
    return DISAS_NEXT;
}

static DisasJumpType op_pku(DisasContext *s, DisasOps *o)
{
    int l2 = get_field(s, l2) + 1;
    TCGv_i32 l;

    /* The length must be even and should not exceed 64 bytes.  */
    if ((l2 & 1) || (l2 > 64)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l2);
    gen_helper_pku(cpu_env, o->addr1, o->in2, l);
    return DISAS_NEXT;
}

static DisasJumpType op_popcnt(DisasContext *s, DisasOps *o)
{
    const uint8_t m3 = get_field(s, m3);

    if ((m3 & 8) && s390_has_feat(S390_FEAT_MISC_INSTRUCTION_EXT3)) {
        tcg_gen_ctpop_i64(o->out, o->in2);
    } else {
        gen_helper_popcnt(o->out, o->in2);
    }
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_ptlb(DisasContext *s, DisasOps *o)
{
    gen_helper_ptlb(cpu_env);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_risbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s, i3);
    int i4 = get_field(s, i4);
    int i5 = get_field(s, i5);
    int do_zero = i4 & 0x80;
    uint64_t mask, imask, pmask;
    int pos, len, rot;

    /* Adjust the arguments for the specific insn.  */
    switch (s->fields.op2) {
    case 0x55: /* risbg */
    case 0x59: /* risbgn */
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
        i3 = (i3 & 31) + 32;
        i4 = (i4 & 31) + 32;
        pmask = 0x00000000ffffffffull;
        break;
    default:
        g_assert_not_reached();
    }

    /* MASK is the set of bits to be inserted from R2. */
    if (i3 <= i4) {
        /* [0...i3---i4...63] */
        mask = (-1ull >> i3) & (-1ull << (63 - i4));
    } else {
        /* [0---i4...i3---63] */
        mask = (-1ull >> i3) | (-1ull << (63 - i4));
    }
    /* For RISBLG/RISBHG, the wrapping is limited to the high/low doubleword. */
    mask &= pmask;

    /* IMASK is the set of bits to be kept from R1.  In the case of the high/low
       insns, we need to keep the other half of the register.  */
    imask = ~mask | ~pmask;
    if (do_zero) {
        imask = ~pmask;
    }

    len = i4 - i3 + 1;
    pos = 63 - i4;
    rot = i5 & 63;

    /* In some cases we can implement this with extract.  */
    if (imask == 0 && pos == 0 && len > 0 && len <= rot) {
        tcg_gen_extract_i64(o->out, o->in2, 64 - rot, len);
        return DISAS_NEXT;
    }

    /* In some cases we can implement this with deposit.  */
    if (len > 0 && (imask == 0 || ~mask == imask)) {
        /* Note that we rotate the bits to be inserted to the lsb, not to
           the position as described in the PoO.  */
        rot = (rot - pos) & 63;
    } else {
        pos = -1;
    }

    /* Rotate the input as necessary.  */
    tcg_gen_rotli_i64(o->in2, o->in2, rot);

    /* Insert the selected bits into the output.  */
    if (pos >= 0) {
        if (imask == 0) {
            tcg_gen_deposit_z_i64(o->out, o->in2, pos, len);
        } else {
            tcg_gen_deposit_i64(o->out, o->out, o->in2, pos, len);
        }
    } else if (imask == 0) {
        tcg_gen_andi_i64(o->out, o->in2, mask);
    } else {
        tcg_gen_andi_i64(o->in2, o->in2, mask);
        tcg_gen_andi_i64(o->out, o->out, imask);
        tcg_gen_or_i64(o->out, o->out, o->in2);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_rosbg(DisasContext *s, DisasOps *o)
{
    int i3 = get_field(s, i3);
    int i4 = get_field(s, i4);
    int i5 = get_field(s, i5);
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
    switch (s->fields.op2) {
    case 0x54: /* AND */
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
    return DISAS_NEXT;
}

static DisasJumpType op_rev16(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap16_i64(o->out, o->in2, TCG_BSWAP_IZ | TCG_BSWAP_OZ);
    return DISAS_NEXT;
}

static DisasJumpType op_rev32(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap32_i64(o->out, o->in2, TCG_BSWAP_IZ | TCG_BSWAP_OZ);
    return DISAS_NEXT;
}

static DisasJumpType op_rev64(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap64_i64(o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_rll32(DisasContext *s, DisasOps *o)
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
    return DISAS_NEXT;
}

static DisasJumpType op_rll64(DisasContext *s, DisasOps *o)
{
    tcg_gen_rotl_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_rrbe(DisasContext *s, DisasOps *o)
{
    gen_helper_rrbe(cc_op, cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sacf(DisasContext *s, DisasOps *o)
{
    gen_helper_sacf(cpu_env, o->in2);
    /* Addressing mode has changed, so end the block.  */
    return DISAS_TOO_MANY;
}
#endif

static DisasJumpType op_sam(DisasContext *s, DisasOps *o)
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
    if (s->base.pc_next & ~mask) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    s->pc_tmp &= mask;

    tsam = tcg_constant_i64(sam);
    tcg_gen_deposit_i64(psw_mask, psw_mask, tsam, 31, 2);

    /* Always exit the TB, since we (may have) changed execution mode.  */
    return DISAS_TOO_MANY;
}

static DisasJumpType op_sar(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_st32_i64(o->in2, cpu_env, offsetof(CPUS390XState, aregs[r1]));
    return DISAS_NEXT;
}

static DisasJumpType op_seb(DisasContext *s, DisasOps *o)
{
    gen_helper_seb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sdb(o->out, cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sxb(o->out_128, cpu_env, o->in1_128, o->in2_128);
    return DISAS_NEXT;
}

static DisasJumpType op_sqeb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqeb(o->out, cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sqdb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqdb(o->out, cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sqxb(DisasContext *s, DisasOps *o)
{
    gen_helper_sqxb(o->out_128, cpu_env, o->in2_128);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_servc(DisasContext *s, DisasOps *o)
{
    gen_helper_servc(cc_op, cpu_env, o->in2, o->in1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sigp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_sigp(cc_op, cpu_env, o->in2, r1, r3);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_soc(DisasContext *s, DisasOps *o)
{
    DisasCompare c;
    TCGv_i64 a, h;
    TCGLabel *lab;
    int r1;

    disas_jcc(s, &c, get_field(s, m3));

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

    r1 = get_field(s, r1);
    a = get_address(s, 0, get_field(s, b2), get_field(s, d2));
    switch (s->insn->data) {
    case 1: /* STOCG */
        tcg_gen_qemu_st64(regs[r1], a, get_mem_index(s));
        break;
    case 0: /* STOC */
        tcg_gen_qemu_st32(regs[r1], a, get_mem_index(s));
        break;
    case 2: /* STOCFH */
        h = tcg_temp_new_i64();
        tcg_gen_shri_i64(h, regs[r1], 32);
        tcg_gen_qemu_st32(h, a, get_mem_index(s));
        tcg_temp_free_i64(h);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free_i64(a);

    gen_set_label(lab);
    return DISAS_NEXT;
}

static DisasJumpType op_sla(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t;
    uint64_t sign = 1ull << s->insn->data;
    if (s->insn->data == 31) {
        t = tcg_temp_new_i64();
        tcg_gen_shli_i64(t, o->in1, 32);
    } else {
        t = o->in1;
    }
    gen_op_update2_cc_i64(s, CC_OP_SLA, t, o->in2);
    if (s->insn->data == 31) {
        tcg_temp_free_i64(t);
    }
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    /* The arithmetic left shift is curious in that it does not affect
       the sign bit.  Copy that over from the source unchanged.  */
    tcg_gen_andi_i64(o->out, o->out, ~sign);
    tcg_gen_andi_i64(o->in1, o->in1, sign);
    tcg_gen_or_i64(o->out, o->out, o->in1);
    return DISAS_NEXT;
}

static DisasJumpType op_sll(DisasContext *s, DisasOps *o)
{
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sra(DisasContext *s, DisasOps *o)
{
    tcg_gen_sar_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_srl(DisasContext *s, DisasOps *o)
{
    tcg_gen_shr_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sfpc(DisasContext *s, DisasOps *o)
{
    gen_helper_sfpc(cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sfas(DisasContext *s, DisasOps *o)
{
    gen_helper_sfas(cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_srnm(DisasContext *s, DisasOps *o)
{
    /* Bits other than 62 and 63 are ignored. Bit 29 is set to zero. */
    tcg_gen_andi_i64(o->addr1, o->addr1, 0x3ull);
    gen_helper_srnm(cpu_env, o->addr1);
    return DISAS_NEXT;
}

static DisasJumpType op_srnmb(DisasContext *s, DisasOps *o)
{
    /* Bits 0-55 are are ignored. */
    tcg_gen_andi_i64(o->addr1, o->addr1, 0xffull);
    gen_helper_srnm(cpu_env, o->addr1);
    return DISAS_NEXT;
}

static DisasJumpType op_srnmt(DisasContext *s, DisasOps *o)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* Bits other than 61-63 are ignored. */
    tcg_gen_andi_i64(o->addr1, o->addr1, 0x7ull);

    /* No need to call a helper, we don't implement dfp */
    tcg_gen_ld32u_i64(tmp, cpu_env, offsetof(CPUS390XState, fpc));
    tcg_gen_deposit_i64(tmp, tmp, o->addr1, 4, 3);
    tcg_gen_st32_i64(tmp, cpu_env, offsetof(CPUS390XState, fpc));

    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

static DisasJumpType op_spm(DisasContext *s, DisasOps *o)
{
    tcg_gen_extrl_i64_i32(cc_op, o->in1);
    tcg_gen_extract_i32(cc_op, cc_op, 28, 2);
    set_cc_static(s);

    tcg_gen_shri_i64(o->in1, o->in1, 24);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in1, PSW_SHIFT_MASK_PM, 4);
    return DISAS_NEXT;
}

static DisasJumpType op_ectg(DisasContext *s, DisasOps *o)
{
    int b1 = get_field(s, b1);
    int d1 = get_field(s, d1);
    int b2 = get_field(s, b2);
    int d2 = get_field(s, d2);
    int r3 = get_field(s, r3);
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* fetch all operands first */
    o->in1 = tcg_temp_new_i64();
    tcg_gen_addi_i64(o->in1, regs[b1], d1);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_addi_i64(o->in2, regs[b2], d2);
    o->addr1 = tcg_temp_new_i64();
    gen_addi_and_wrap_i64(s, o->addr1, regs[r3], 0);

    /* load the third operand into r3 before modifying anything */
    tcg_gen_qemu_ld64(regs[r3], o->addr1, get_mem_index(s));

    /* subtract CPU timer from first operand and store in GR0 */
    gen_helper_stpt(tmp, cpu_env);
    tcg_gen_sub_i64(regs[0], o->in1, tmp);

    /* store second operand in GR1 */
    tcg_gen_mov_i64(regs[1], o->in2);

    tcg_temp_free_i64(tmp);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_spka(DisasContext *s, DisasOps *o)
{
    tcg_gen_shri_i64(o->in2, o->in2, 4);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, PSW_SHIFT_KEY, 4);
    return DISAS_NEXT;
}

static DisasJumpType op_sske(DisasContext *s, DisasOps *o)
{
    gen_helper_sske(cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_ssm(DisasContext *s, DisasOps *o)
{
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, 56, 8);
    /* Exit to main loop to reevaluate s390_cpu_exec_interrupt.  */
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_stap(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, core_id));
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_stck(DisasContext *s, DisasOps *o)
{
    gen_helper_stck(o->out, cpu_env);
    /* ??? We don't implement clock states.  */
    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

static DisasJumpType op_stcke(DisasContext *s, DisasOps *o)
{
    TCGv_i64 c1 = tcg_temp_new_i64();
    TCGv_i64 c2 = tcg_temp_new_i64();
    TCGv_i64 todpr = tcg_temp_new_i64();
    gen_helper_stck(c1, cpu_env);
    /* 16 bit value store in an uint32_t (only valid bits set) */
    tcg_gen_ld32u_i64(todpr, cpu_env, offsetof(CPUS390XState, todpr));
    /* Shift the 64-bit value into its place as a zero-extended
       104-bit value.  Note that "bit positions 64-103 are always
       non-zero so that they compare differently to STCK"; we set
       the least significant bit to 1.  */
    tcg_gen_shli_i64(c2, c1, 56);
    tcg_gen_shri_i64(c1, c1, 8);
    tcg_gen_ori_i64(c2, c2, 0x10000);
    tcg_gen_or_i64(c2, c2, todpr);
    tcg_gen_qemu_st64(c1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(o->in2, o->in2, 8);
    tcg_gen_qemu_st64(c2, o->in2, get_mem_index(s));
    tcg_temp_free_i64(c1);
    tcg_temp_free_i64(c2);
    tcg_temp_free_i64(todpr);
    /* ??? We don't implement clock states.  */
    gen_op_movi_cc(s, 0);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_sck(DisasContext *s, DisasOps *o)
{
    gen_helper_sck(cc_op, cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sckc(DisasContext *s, DisasOps *o)
{
    gen_helper_sckc(cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_sckpf(DisasContext *s, DisasOps *o)
{
    gen_helper_sckpf(cpu_env, regs[0]);
    return DISAS_NEXT;
}

static DisasJumpType op_stckc(DisasContext *s, DisasOps *o)
{
    gen_helper_stckc(o->out, cpu_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stctg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stctg(cpu_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stctl(cpu_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stidp(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld_i64(o->out, cpu_env, offsetof(CPUS390XState, cpuid));
    return DISAS_NEXT;
}

static DisasJumpType op_spt(DisasContext *s, DisasOps *o)
{
    gen_helper_spt(cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_stfl(DisasContext *s, DisasOps *o)
{
    gen_helper_stfl(cpu_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stpt(DisasContext *s, DisasOps *o)
{
    gen_helper_stpt(o->out, cpu_env);
    return DISAS_NEXT;
}

static DisasJumpType op_stsi(DisasContext *s, DisasOps *o)
{
    gen_helper_stsi(cc_op, cpu_env, o->in2, regs[0], regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_spx(DisasContext *s, DisasOps *o)
{
    gen_helper_spx(cpu_env, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_xsch(DisasContext *s, DisasOps *o)
{
    gen_helper_xsch(cpu_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_csch(DisasContext *s, DisasOps *o)
{
    gen_helper_csch(cpu_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_hsch(DisasContext *s, DisasOps *o)
{
    gen_helper_hsch(cpu_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_msch(DisasContext *s, DisasOps *o)
{
    gen_helper_msch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_rchp(DisasContext *s, DisasOps *o)
{
    gen_helper_rchp(cpu_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_rsch(DisasContext *s, DisasOps *o)
{
    gen_helper_rsch(cpu_env, regs[1]);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sal(DisasContext *s, DisasOps *o)
{
    gen_helper_sal(cpu_env, regs[1]);
    return DISAS_NEXT;
}

static DisasJumpType op_schm(DisasContext *s, DisasOps *o)
{
    gen_helper_schm(cpu_env, regs[1], regs[2], o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_siga(DisasContext *s, DisasOps *o)
{
    /* From KVM code: Not provided, set CC = 3 for subchannel not operational */
    gen_op_movi_cc(s, 3);
    return DISAS_NEXT;
}

static DisasJumpType op_stcps(DisasContext *s, DisasOps *o)
{
    /* The instruction is suppressed if not provided. */
    return DISAS_NEXT;
}

static DisasJumpType op_ssch(DisasContext *s, DisasOps *o)
{
    gen_helper_ssch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stsch(DisasContext *s, DisasOps *o)
{
    gen_helper_stsch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stcrw(DisasContext *s, DisasOps *o)
{
    gen_helper_stcrw(cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tpi(DisasContext *s, DisasOps *o)
{
    gen_helper_tpi(cc_op, cpu_env, o->addr1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tsch(DisasContext *s, DisasOps *o)
{
    gen_helper_tsch(cpu_env, regs[1], o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_chsc(DisasContext *s, DisasOps *o)
{
    gen_helper_chsc(cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stpx(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld_i64(o->out, cpu_env, offsetof(CPUS390XState, psa));
    tcg_gen_andi_i64(o->out, o->out, 0x7fffe000);
    return DISAS_NEXT;
}

static DisasJumpType op_stnosm(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = get_field(s, i2);
    TCGv_i64 t;

    /* It is important to do what the instruction name says: STORE THEN.
       If we let the output hook perform the store then if we fault and
       restart, we'll have the wrong SYSTEM MASK in place.  */
    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 56);
    tcg_gen_qemu_st8(t, o->addr1, get_mem_index(s));
    tcg_temp_free_i64(t);

    if (s->fields.op == 0xac) {
        tcg_gen_andi_i64(psw_mask, psw_mask,
                         (i2 << 56) | 0x00ffffffffffffffull);
    } else {
        tcg_gen_ori_i64(psw_mask, psw_mask, i2 << 56);
    }

    /* Exit to main loop to reevaluate s390_cpu_exec_interrupt.  */
    s->exit_to_mainloop = true;
    return DISAS_TOO_MANY;
}

static DisasJumpType op_stura(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->in1, o->in2, MMU_REAL_IDX, s->insn->data);

    if (s->base.tb->flags & FLAG_MASK_PER) {
        update_psw_addr(s);
        gen_helper_per_store_real(cpu_env);
    }
    return DISAS_NEXT;
}
#endif

static DisasJumpType op_stfle(DisasContext *s, DisasOps *o)
{
    gen_helper_stfle(cc_op, cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_st8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st8(o->in1, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_st16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st16(o->in1, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_st32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st32(o->in1, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_st64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st64(o->in1, o->in2, get_mem_index(s));
    return DISAS_NEXT;
}

static DisasJumpType op_stam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));

    gen_helper_stam(cpu_env, r1, o->in2, r3);
    return DISAS_NEXT;
}

static DisasJumpType op_stcm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s, m3);
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
    return DISAS_NEXT;
}

static DisasJumpType op_stm(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    int size = s->insn->data;
    TCGv_i64 tsize = tcg_constant_i64(size);

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

    return DISAS_NEXT;
}

static DisasJumpType op_stmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    int r3 = get_field(s, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_constant_i64(4);
    TCGv_i64 t32 = tcg_constant_i64(32);

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
    return DISAS_NEXT;
}

static DisasJumpType op_stpq(DisasContext *s, DisasOps *o)
{
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        gen_helper_stpq(cpu_env, o->in2, o->out2, o->out);
    } else if (HAVE_ATOMIC128) {
        gen_helper_stpq_parallel(cpu_env, o->in2, o->out2, o->out);
    } else {
        gen_helper_exit_atomic(cpu_env);
        return DISAS_NORETURN;
    }
    return DISAS_NEXT;
}

static DisasJumpType op_srst(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_srst(cpu_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_srstu(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_srstu(cpu_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sub(DisasContext *s, DisasOps *o)
{
    tcg_gen_sub_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_subu64(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(cc_src, 0);
    tcg_gen_sub2_i64(o->out, cc_src, o->in1, cc_src, o->in2, cc_src);
    return DISAS_NEXT;
}

/* Compute borrow (0, -1) into cc_src. */
static void compute_borrow(DisasContext *s)
{
    switch (s->cc_op) {
    case CC_OP_SUBU:
        /* The borrow value is already in cc_src (0,-1). */
        break;
    default:
        gen_op_calc_cc(s);
        /* fall through */
    case CC_OP_STATIC:
        /* The carry flag is the msb of CC; compute into cc_src. */
        tcg_gen_extu_i32_i64(cc_src, cc_op);
        tcg_gen_shri_i64(cc_src, cc_src, 1);
        /* fall through */
    case CC_OP_ADDU:
        /* Convert carry (1,0) to borrow (0,-1). */
        tcg_gen_subi_i64(cc_src, cc_src, 1);
        break;
    }
}

static DisasJumpType op_subb32(DisasContext *s, DisasOps *o)
{
    compute_borrow(s);

    /* Borrow is {0, -1}, so add to subtract. */
    tcg_gen_add_i64(o->out, o->in1, cc_src);
    tcg_gen_sub_i64(o->out, o->out, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_subb64(DisasContext *s, DisasOps *o)
{
    compute_borrow(s);

    /*
     * Borrow is {0, -1}, so add to subtract; replicate the
     * borrow input to produce 128-bit -1 for the addition.
     */
    TCGv_i64 zero = tcg_constant_i64(0);
    tcg_gen_add2_i64(o->out, cc_src, o->in1, zero, cc_src, cc_src);
    tcg_gen_sub2_i64(o->out, cc_src, o->out, cc_src, o->in2, zero);

    return DISAS_NEXT;
}

static DisasJumpType op_svc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t;

    update_psw_addr(s);
    update_cc_op(s);

    t = tcg_constant_i32(get_field(s, i1) & 0xff);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_code));

    t = tcg_constant_i32(s->ilen);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_ilen));

    gen_exception(EXCP_SVC);
    return DISAS_NORETURN;
}

static DisasJumpType op_tam(DisasContext *s, DisasOps *o)
{
    int cc = 0;

    cc |= (s->base.tb->flags & FLAG_MASK_64) ? 2 : 0;
    cc |= (s->base.tb->flags & FLAG_MASK_32) ? 1 : 0;
    gen_op_movi_cc(s, cc);
    return DISAS_NEXT;
}

static DisasJumpType op_tceb(DisasContext *s, DisasOps *o)
{
    gen_helper_tceb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tcdb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcdb(cc_op, cpu_env, o->in1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tcxb(DisasContext *s, DisasOps *o)
{
    gen_helper_tcxb(cc_op, cpu_env, o->in1_128, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY

static DisasJumpType op_testblock(DisasContext *s, DisasOps *o)
{
    gen_helper_testblock(cc_op, cpu_env, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tprot(DisasContext *s, DisasOps *o)
{
    gen_helper_tprot(cc_op, cpu_env, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

#endif

static DisasJumpType op_tp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l1 = tcg_constant_i32(get_field(s, l1) + 1);

    gen_helper_tp(cc_op, cpu_env, o->addr1, l1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_tr(cpu_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_tre(DisasContext *s, DisasOps *o)
{
    TCGv_i128 pair = tcg_temp_new_i128();

    gen_helper_tre(pair, cpu_env, o->out, o->out2, o->in2);
    tcg_gen_extr_i128_i64(o->out2, o->out, pair);
    tcg_temp_free_i128(pair);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trt(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_trt(cc_op, cpu_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trtr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_trtr(cc_op, cpu_env, l, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_trXX(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));
    TCGv_i32 sizes = tcg_constant_i32(s->insn->opc & 3);
    TCGv_i32 tst = tcg_temp_new_i32();
    int m3 = get_field(s, m3);

    if (!s390_has_feat(S390_FEAT_ETF2_ENH)) {
        m3 = 0;
    }
    if (m3 & 1) {
        tcg_gen_movi_i32(tst, -1);
    } else {
        tcg_gen_extrl_i64_i32(tst, regs[0]);
        if (s->insn->opc & 3) {
            tcg_gen_ext8u_i32(tst, tst);
        } else {
            tcg_gen_ext16u_i32(tst, tst);
        }
    }
    gen_helper_trXX(cc_op, cpu_env, r1, r2, tst, sizes);

    tcg_temp_free_i32(tst);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_ts(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_constant_i32(0xff);

    tcg_gen_atomic_xchg_i32(t1, o->in2, t1, get_mem_index(s), MO_UB);
    tcg_gen_extract_i32(cc_op, t1, 7, 1);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_unpk(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_constant_i32(get_field(s, l1));

    gen_helper_unpk(cpu_env, l, o->addr1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_unpka(DisasContext *s, DisasOps *o)
{
    int l1 = get_field(s, l1) + 1;
    TCGv_i32 l;

    /* The length must not exceed 32 bytes.  */
    if (l1 > 32) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l1);
    gen_helper_unpka(cc_op, cpu_env, o->addr1, l, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_unpku(DisasContext *s, DisasOps *o)
{
    int l1 = get_field(s, l1) + 1;
    TCGv_i32 l;

    /* The length must be even and should not exceed 64 bytes.  */
    if ((l1 & 1) || (l1 > 64)) {
        gen_program_exception(s, PGM_SPECIFICATION);
        return DISAS_NORETURN;
    }
    l = tcg_constant_i32(l1);
    gen_helper_unpku(cc_op, cpu_env, o->addr1, l, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}


static DisasJumpType op_xc(DisasContext *s, DisasOps *o)
{
    int d1 = get_field(s, d1);
    int d2 = get_field(s, d2);
    int b1 = get_field(s, b1);
    int b2 = get_field(s, b2);
    int l = get_field(s, l1);
    TCGv_i32 t32;

    o->addr1 = get_address(s, 0, b1, d1);

    /* If the addresses are identical, this is a store/memset of zero.  */
    if (b1 == b2 && d1 == d2 && (l + 1) <= 32) {
        o->in2 = tcg_constant_i64(0);

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
        return DISAS_NEXT;
    }

    /* But in general we'll defer to a helper.  */
    o->in2 = get_address(s, 0, b2, d2);
    t32 = tcg_constant_i32(l);
    gen_helper_xc(cc_op, cpu_env, t32, o->addr1, o->in2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_xor(DisasContext *s, DisasOps *o)
{
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_xori(DisasContext *s, DisasOps *o)
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
    return DISAS_NEXT;
}

static DisasJumpType op_xi(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_ld_tl(o->in1, o->addr1, get_mem_index(s), s->insn->data);
    } else {
        /* Perform the atomic operation in memory. */
        tcg_gen_atomic_fetch_xor_i64(o->in1, o->addr1, o->in2, get_mem_index(s),
                                     s->insn->data);
    }

    /* Recompute also for atomic case: needed for setting CC. */
    tcg_gen_xor_i64(o->out, o->in1, o->in2);

    if (!s390_has_feat(S390_FEAT_INTERLOCKED_ACCESS_2)) {
        tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), s->insn->data);
    }
    return DISAS_NEXT;
}

static DisasJumpType op_zero(DisasContext *s, DisasOps *o)
{
    o->out = tcg_const_i64(0);
    return DISAS_NEXT;
}

static DisasJumpType op_zero2(DisasContext *s, DisasOps *o)
{
    o->out = tcg_const_i64(0);
    o->out2 = o->out;
    o->g_out2 = true;
    return DISAS_NEXT;
}

#ifndef CONFIG_USER_ONLY
static DisasJumpType op_clp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_clp(cpu_env, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcilg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_pcilg(cpu_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcistg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_pcistg(cpu_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_stpcifc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_stpcifc(cpu_env, r1, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_sic(DisasContext *s, DisasOps *o)
{
    gen_helper_sic(cpu_env, o->in1, o->in2);
    return DISAS_NEXT;
}

static DisasJumpType op_rpcit(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r2 = tcg_constant_i32(get_field(s, r2));

    gen_helper_rpcit(cpu_env, r1, r2);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_pcistb(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 r3 = tcg_constant_i32(get_field(s, r3));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_pcistb(cpu_env, r1, r3, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}

static DisasJumpType op_mpcifc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_constant_i32(get_field(s, r1));
    TCGv_i32 ar = tcg_constant_i32(get_field(s, b2));

    gen_helper_mpcifc(cpu_env, r1, o->addr1, ar);
    set_cc_static(s);
    return DISAS_NEXT;
}
#endif

#include "translate_vx.c.inc"

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
    tcg_gen_shri_i64(cc_src, o->out, 32);
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update2_cc_i64(s, CC_OP_ADDU, cc_src, cc_dst);
}

static void cout_addu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_ADDU, cc_src, o->out);
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
    tcg_gen_sari_i64(cc_src, o->out, 32);
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update2_cc_i64(s, CC_OP_SUBU, cc_src, cc_dst);
}

static void cout_subu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_SUBU, cc_src, o->out);
}

static void cout_tm32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_32, o->in1, o->in2);
}

static void cout_tm64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_64, o->in1, o->in2);
}

static void cout_muls32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_MULS_32, o->out);
}

static void cout_muls64(DisasContext *s, DisasOps *o)
{
    /* out contains "high" part, out2 contains "low" part of 128 bit result */
    gen_op_update2_cc_i64(s, CC_OP_MULS_64, o->out, o->out2);
}

/* ====================================================================== */
/* The "PREParation" generators.  These initialize the DisasOps.OUT fields
   with the TCG register to which we will write.  Used in combination with
   the "wout" generators, in some cases we need a new temporary, and in
   some cases we can write to a TCG global.  */

static void prep_new(DisasContext *s, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
}
#define SPEC_prep_new 0

static void prep_new_P(DisasContext *s, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();
}
#define SPEC_prep_new_P 0

static void prep_new_x(DisasContext *s, DisasOps *o)
{
    o->out_128 = tcg_temp_new_i128();
}
#define SPEC_prep_new_x 0

static void prep_r1(DisasContext *s, DisasOps *o)
{
    o->out = regs[get_field(s, r1)];
    o->g_out = true;
}
#define SPEC_prep_r1 0

static void prep_r1_P(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->out = regs[r1];
    o->out2 = regs[r1 + 1];
    o->g_out = o->g_out2 = true;
}
#define SPEC_prep_r1_P SPEC_r1_even

static void prep_x1(DisasContext *s, DisasOps *o)
{
    o->out_128 = load_freg_128(get_field(s, r1));
}
#define SPEC_prep_x1 SPEC_r1_f128

/* ====================================================================== */
/* The "Write OUTput" generators.  These generally perform some non-trivial
   copy of data to TCG globals, or to main memory.  The trivial cases are
   generally handled by having a "prep" generator install the TCG global
   as the destination of the operation.  */

static void wout_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->out);
}
#define SPEC_wout_r1 0

static void wout_out2_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->out2);
}
#define SPEC_wout_out2_r1 0

static void wout_r1_8(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 8);
}
#define SPEC_wout_r1_8 0

static void wout_r1_16(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 16);
}
#define SPEC_wout_r1_16 0

static void wout_r1_32(DisasContext *s, DisasOps *o)
{
    store_reg32_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_r1_32 0

static void wout_r1_32h(DisasContext *s, DisasOps *o)
{
    store_reg32h_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_r1_32h 0

static void wout_r1_P32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    store_reg32_i64(r1, o->out);
    store_reg32_i64(r1 + 1, o->out2);
}
#define SPEC_wout_r1_P32 SPEC_r1_even

static void wout_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    TCGv_i64 t = tcg_temp_new_i64();
    store_reg32_i64(r1 + 1, o->out);
    tcg_gen_shri_i64(t, o->out, 32);
    store_reg32_i64(r1, t);
    tcg_temp_free_i64(t);
}
#define SPEC_wout_r1_D32 SPEC_r1_even

static void wout_r1_D64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    tcg_gen_extr_i128_i64(regs[r1 + 1], regs[r1], o->out_128);
}
#define SPEC_wout_r1_D64 SPEC_r1_even

static void wout_r3_P32(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    store_reg32_i64(r3, o->out);
    store_reg32_i64(r3 + 1, o->out2);
}
#define SPEC_wout_r3_P32 SPEC_r3_even

static void wout_r3_P64(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    store_reg(r3, o->out);
    store_reg(r3 + 1, o->out2);
}
#define SPEC_wout_r3_P64 SPEC_r3_even

static void wout_e1(DisasContext *s, DisasOps *o)
{
    store_freg32_i64(get_field(s, r1), o->out);
}
#define SPEC_wout_e1 0

static void wout_f1(DisasContext *s, DisasOps *o)
{
    store_freg(get_field(s, r1), o->out);
}
#define SPEC_wout_f1 0

static void wout_x1(DisasContext *s, DisasOps *o)
{
    int f1 = get_field(s, r1);

    /* Split out_128 into out+out2 for cout_f128. */
    tcg_debug_assert(o->out == NULL);
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();

    tcg_gen_extr_i128_i64(o->out2, o->out, o->out_128);
    store_freg(f1, o->out);
    store_freg(f1 + 2, o->out2);
}
#define SPEC_wout_x1 SPEC_r1_f128

static void wout_x1_P(DisasContext *s, DisasOps *o)
{
    int f1 = get_field(s, r1);
    store_freg(f1, o->out);
    store_freg(f1 + 2, o->out2);
}
#define SPEC_wout_x1_P SPEC_r1_f128

static void wout_cond_r1r2_32(DisasContext *s, DisasOps *o)
{
    if (get_field(s, r1) != get_field(s, r2)) {
        store_reg32_i64(get_field(s, r1), o->out);
    }
}
#define SPEC_wout_cond_r1r2_32 0

static void wout_cond_e1e2(DisasContext *s, DisasOps *o)
{
    if (get_field(s, r1) != get_field(s, r2)) {
        store_freg32_i64(get_field(s, r1), o->out);
    }
}
#define SPEC_wout_cond_e1e2 0

static void wout_m1_8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st8(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_8 0

static void wout_m1_16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st16(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_16 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_16a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), MO_TEUW | MO_ALIGN);
}
#define SPEC_wout_m1_16a 0
#endif

static void wout_m1_32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_32 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_32a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_tl(o->out, o->addr1, get_mem_index(s), MO_TEUL | MO_ALIGN);
}
#define SPEC_wout_m1_32a 0
#endif

static void wout_m1_64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st64(o->out, o->addr1, get_mem_index(s));
}
#define SPEC_wout_m1_64 0

#ifndef CONFIG_USER_ONLY
static void wout_m1_64a(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st_i64(o->out, o->addr1, get_mem_index(s), MO_TEUQ | MO_ALIGN);
}
#define SPEC_wout_m1_64a 0
#endif

static void wout_m2_32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->in2, get_mem_index(s));
}
#define SPEC_wout_m2_32 0

static void wout_in2_r1(DisasContext *s, DisasOps *o)
{
    store_reg(get_field(s, r1), o->in2);
}
#define SPEC_wout_in2_r1 0

static void wout_in2_r1_32(DisasContext *s, DisasOps *o)
{
    store_reg32_i64(get_field(s, r1), o->in2);
}
#define SPEC_wout_in2_r1_32 0

/* ====================================================================== */
/* The "INput 1" generators.  These load the first operand to an insn.  */

static void in1_r1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r1));
}
#define SPEC_in1_r1 0

static void in1_r1_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r1)];
    o->g_in1 = true;
}
#define SPEC_in1_r1_o 0

static void in1_r1_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r1)]);
}
#define SPEC_in1_r1_32s 0

static void in1_r1_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r1)]);
}
#define SPEC_in1_r1_32u 0

static void in1_r1_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r1)], 32);
}
#define SPEC_in1_r1_sr32 0

static void in1_r1p1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r1) + 1);
}
#define SPEC_in1_r1p1 SPEC_r1_even

static void in1_r1p1_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r1) + 1];
    o->g_in1 = true;
}
#define SPEC_in1_r1p1_o SPEC_r1_even

static void in1_r1p1_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r1) + 1]);
}
#define SPEC_in1_r1p1_32s SPEC_r1_even

static void in1_r1p1_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r1) + 1]);
}
#define SPEC_in1_r1p1_32u SPEC_r1_even

static void in1_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r1 + 1], regs[r1]);
}
#define SPEC_in1_r1_D32 SPEC_r1_even

static void in1_r2(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r2));
}
#define SPEC_in1_r2 0

static void in1_r2_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r2)], 32);
}
#define SPEC_in1_r2_sr32 0

static void in1_r2_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r2)]);
}
#define SPEC_in1_r2_32u 0

static void in1_r3(DisasContext *s, DisasOps *o)
{
    o->in1 = load_reg(get_field(s, r3));
}
#define SPEC_in1_r3 0

static void in1_r3_o(DisasContext *s, DisasOps *o)
{
    o->in1 = regs[get_field(s, r3)];
    o->g_in1 = true;
}
#define SPEC_in1_r3_o 0

static void in1_r3_32s(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(s, r3)]);
}
#define SPEC_in1_r3_32s 0

static void in1_r3_32u(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(s, r3)]);
}
#define SPEC_in1_r3_32u 0

static void in1_r3_D32(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r3 + 1], regs[r3]);
}
#define SPEC_in1_r3_D32 SPEC_r3_even

static void in1_r3_sr32(DisasContext *s, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(s, r3)], 32);
}
#define SPEC_in1_r3_sr32 0

static void in1_e1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg32_i64(get_field(s, r1));
}
#define SPEC_in1_e1 0

static void in1_f1(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r1));
}
#define SPEC_in1_f1 0

static void in1_x1(DisasContext *s, DisasOps *o)
{
    o->in1_128 = load_freg_128(get_field(s, r1));
}
#define SPEC_in1_x1 SPEC_r1_f128

/* Load the high double word of an extended (128-bit) format FP number */
static void in1_x2h(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r2));
}
#define SPEC_in1_x2h SPEC_r2_f128

static void in1_f3(DisasContext *s, DisasOps *o)
{
    o->in1 = load_freg(get_field(s, r3));
}
#define SPEC_in1_f3 0

static void in1_la1(DisasContext *s, DisasOps *o)
{
    o->addr1 = get_address(s, 0, get_field(s, b1), get_field(s, d1));
}
#define SPEC_in1_la1 0

static void in1_la2(DisasContext *s, DisasOps *o)
{
    int x2 = have_field(s, x2) ? get_field(s, x2) : 0;
    o->addr1 = get_address(s, x2, get_field(s, b2), get_field(s, d2));
}
#define SPEC_in1_la2 0

static void in1_m1_8u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld8u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_8u 0

static void in1_m1_16s(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16s(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_16s 0

static void in1_m1_16u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_16u 0

static void in1_m1_32s(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32s(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_32s 0

static void in1_m1_32u(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_32u 0

static void in1_m1_64(DisasContext *s, DisasOps *o)
{
    in1_la1(s, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(o->in1, o->addr1, get_mem_index(s));
}
#define SPEC_in1_m1_64 0

/* ====================================================================== */
/* The "INput 2" generators.  These load the second operand to an insn.  */

static void in2_r1_o(DisasContext *s, DisasOps *o)
{
    o->in2 = regs[get_field(s, r1)];
    o->g_in2 = true;
}
#define SPEC_in2_r1_o 0

static void in2_r1_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(s, r1)]);
}
#define SPEC_in2_r1_16u 0

static void in2_r1_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r1)]);
}
#define SPEC_in2_r1_32u 0

static void in2_r1_D32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s, r1);
    o->in2 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in2, regs[r1 + 1], regs[r1]);
}
#define SPEC_in2_r1_D32 SPEC_r1_even

static void in2_r2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_reg(get_field(s, r2));
}
#define SPEC_in2_r2 0

static void in2_r2_o(DisasContext *s, DisasOps *o)
{
    o->in2 = regs[get_field(s, r2)];
    o->g_in2 = true;
}
#define SPEC_in2_r2_o 0

static void in2_r2_nz(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);
    if (r2 != 0) {
        o->in2 = load_reg(r2);
    }
}
#define SPEC_in2_r2_nz 0

static void in2_r2_8s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_8s 0

static void in2_r2_8u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_8u 0

static void in2_r2_16s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_16s 0

static void in2_r2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_16u 0

static void in2_r3(DisasContext *s, DisasOps *o)
{
    o->in2 = load_reg(get_field(s, r3));
}
#define SPEC_in2_r3 0

static void in2_r3_D64(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s, r3);
    o->in2_128 = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(o->in2_128, regs[r3 + 1], regs[r3]);
}
#define SPEC_in2_r3_D64 SPEC_r3_even

static void in2_r3_sr32(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(s, r3)], 32);
}
#define SPEC_in2_r3_sr32 0

static void in2_r3_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r3)]);
}
#define SPEC_in2_r3_32u 0

static void in2_r2_32s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_32s 0

static void in2_r2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(s, r2)]);
}
#define SPEC_in2_r2_32u 0

static void in2_r2_sr32(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in2, regs[get_field(s, r2)], 32);
}
#define SPEC_in2_r2_sr32 0

static void in2_e2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg32_i64(get_field(s, r2));
}
#define SPEC_in2_e2 0

static void in2_f2(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg(get_field(s, r2));
}
#define SPEC_in2_f2 0

static void in2_x2(DisasContext *s, DisasOps *o)
{
    o->in2_128 = load_freg_128(get_field(s, r2));
}
#define SPEC_in2_x2 SPEC_r2_f128

/* Load the low double word of an extended (128-bit) format FP number */
static void in2_x2l(DisasContext *s, DisasOps *o)
{
    o->in2 = load_freg(get_field(s, r2) + 2);
}
#define SPEC_in2_x2l SPEC_r2_f128

static void in2_ra2(DisasContext *s, DisasOps *o)
{
    int r2 = get_field(s, r2);

    /* Note: *don't* treat !r2 as 0, use the reg value. */
    o->in2 = tcg_temp_new_i64();
    gen_addi_and_wrap_i64(s, o->in2, regs[r2], 0);
}
#define SPEC_in2_ra2 0

static void in2_a2(DisasContext *s, DisasOps *o)
{
    int x2 = have_field(s, x2) ? get_field(s, x2) : 0;
    o->in2 = get_address(s, x2, get_field(s, b2), get_field(s, d2));
}
#define SPEC_in2_a2 0

static TCGv gen_ri2(DisasContext *s)
{
    return tcg_constant_i64(s->base.pc_next + (int64_t)get_field(s, i2) * 2);
}

static void in2_ri2(DisasContext *s, DisasOps *o)
{
    o->in2 = gen_ri2(s);
}
#define SPEC_in2_ri2 0

static void in2_sh(DisasContext *s, DisasOps *o)
{
    int b2 = get_field(s, b2);
    int d2 = get_field(s, d2);

    if (b2 == 0) {
        o->in2 = tcg_const_i64(d2 & 0x3f);
    } else {
        o->in2 = get_address(s, 0, b2, d2);
        tcg_gen_andi_i64(o->in2, o->in2, 0x3f);
    }
}
#define SPEC_in2_sh 0

static void in2_m2_8u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld8u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_8u 0

static void in2_m2_16s(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld16s(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_16s 0

static void in2_m2_16u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld16u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_16u 0

static void in2_m2_32s(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld32s(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_32s 0

static void in2_m2_32u(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld32u(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_32u 0

#ifndef CONFIG_USER_ONLY
static void in2_m2_32ua(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_tl(o->in2, o->in2, get_mem_index(s), MO_TEUL | MO_ALIGN);
}
#define SPEC_in2_m2_32ua 0
#endif

static void in2_m2_64(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
}
#define SPEC_in2_m2_64 0

static void in2_m2_64w(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
    gen_addi_and_wrap_i64(s, o->in2, o->in2, 0);
}
#define SPEC_in2_m2_64w 0

#ifndef CONFIG_USER_ONLY
static void in2_m2_64a(DisasContext *s, DisasOps *o)
{
    in2_a2(s, o);
    tcg_gen_qemu_ld_i64(o->in2, o->in2, get_mem_index(s), MO_TEUQ | MO_ALIGN);
}
#define SPEC_in2_m2_64a 0
#endif

static void in2_mri2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16u(o->in2, gen_ri2(s), get_mem_index(s));
}
#define SPEC_in2_mri2_16u 0

static void in2_mri2_32s(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32s(o->in2, gen_ri2(s), get_mem_index(s));
}
#define SPEC_in2_mri2_32s 0

static void in2_mri2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(o->in2, gen_ri2(s), get_mem_index(s));
}
#define SPEC_in2_mri2_32u 0

static void in2_mri2_64(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(o->in2, gen_ri2(s), get_mem_index(s));
}
#define SPEC_in2_mri2_64 0

static void in2_i2(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_const_i64(get_field(s, i2));
}
#define SPEC_in2_i2 0

static void in2_i2_8u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint8_t)get_field(s, i2));
}
#define SPEC_in2_i2_8u 0

static void in2_i2_16u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint16_t)get_field(s, i2));
}
#define SPEC_in2_i2_16u 0

static void in2_i2_32u(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint32_t)get_field(s, i2));
}
#define SPEC_in2_i2_32u 0

static void in2_i2_16u_shl(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = (uint16_t)get_field(s, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_16u_shl 0

static void in2_i2_32u_shl(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = (uint32_t)get_field(s, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}
#define SPEC_in2_i2_32u_shl 0

#ifndef CONFIG_USER_ONLY
static void in2_insn(DisasContext *s, DisasOps *o)
{
    o->in2 = tcg_const_i64(s->fields.raw_insn);
}
#define SPEC_in2_insn 0
#endif

/* ====================================================================== */

/* Find opc within the table of insns.  This is formulated as a switch
   statement so that (1) we get compile-time notice of cut-paste errors
   for duplicated opcodes, and (2) the compiler generates the binary
   search tree, rather than us having to post-process the table.  */

#define C(OPC, NM, FT, FC, I1, I2, P, W, OP, CC) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0, 0)

#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, 0)

#define F(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, FL) \
    E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0, FL)

#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) insn_ ## NM,

enum DisasInsnEnum {
#include "insn-data.h.inc"
};

#undef E
#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) {                   \
    .opc = OPC,                                                             \
    .flags = FL,                                                            \
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

/* Give smaller names to the various facilities.  */
#define FAC_Z           S390_FEAT_ZARCH
#define FAC_CASS        S390_FEAT_COMPARE_AND_SWAP_AND_STORE
#define FAC_DFP         S390_FEAT_DFP
#define FAC_DFPR        S390_FEAT_FLOATING_POINT_SUPPORT_ENH /* DFP-rounding */
#define FAC_DO          S390_FEAT_STFLE_45 /* distinct-operands */
#define FAC_EE          S390_FEAT_EXECUTE_EXT
#define FAC_EI          S390_FEAT_EXTENDED_IMMEDIATE
#define FAC_FPE         S390_FEAT_FLOATING_POINT_EXT
#define FAC_FPSSH       S390_FEAT_FLOATING_POINT_SUPPORT_ENH /* FPS-sign-handling */
#define FAC_FPRGR       S390_FEAT_FLOATING_POINT_SUPPORT_ENH /* FPR-GR-transfer */
#define FAC_GIE         S390_FEAT_GENERAL_INSTRUCTIONS_EXT
#define FAC_HFP_MA      S390_FEAT_HFP_MADDSUB
#define FAC_HW          S390_FEAT_STFLE_45 /* high-word */
#define FAC_IEEEE_SIM   S390_FEAT_FLOATING_POINT_SUPPORT_ENH /* IEEE-exception-simulation */
#define FAC_MIE         S390_FEAT_STFLE_49 /* misc-instruction-extensions */
#define FAC_LAT         S390_FEAT_STFLE_49 /* load-and-trap */
#define FAC_LOC         S390_FEAT_STFLE_45 /* load/store on condition 1 */
#define FAC_LOC2        S390_FEAT_STFLE_53 /* load/store on condition 2 */
#define FAC_LD          S390_FEAT_LONG_DISPLACEMENT
#define FAC_PC          S390_FEAT_STFLE_45 /* population count */
#define FAC_SCF         S390_FEAT_STORE_CLOCK_FAST
#define FAC_SFLE        S390_FEAT_STFLE
#define FAC_ILA         S390_FEAT_STFLE_45 /* interlocked-access-facility 1 */
#define FAC_MVCOS       S390_FEAT_MOVE_WITH_OPTIONAL_SPEC
#define FAC_LPP         S390_FEAT_SET_PROGRAM_PARAMETERS /* load-program-parameter */
#define FAC_DAT_ENH     S390_FEAT_DAT_ENH
#define FAC_E2          S390_FEAT_EXTENDED_TRANSLATION_2
#define FAC_EH          S390_FEAT_STFLE_49 /* execution-hint */
#define FAC_PPA         S390_FEAT_STFLE_49 /* processor-assist */
#define FAC_LZRB        S390_FEAT_STFLE_53 /* load-and-zero-rightmost-byte */
#define FAC_ETF3        S390_FEAT_EXTENDED_TRANSLATION_3
#define FAC_MSA         S390_FEAT_MSA /* message-security-assist facility */
#define FAC_MSA3        S390_FEAT_MSA_EXT_3 /* msa-extension-3 facility */
#define FAC_MSA4        S390_FEAT_MSA_EXT_4 /* msa-extension-4 facility */
#define FAC_MSA5        S390_FEAT_MSA_EXT_5 /* msa-extension-5 facility */
#define FAC_MSA8        S390_FEAT_MSA_EXT_8 /* msa-extension-8 facility */
#define FAC_ECT         S390_FEAT_EXTRACT_CPU_TIME
#define FAC_PCI         S390_FEAT_ZPCI /* z/PCI facility */
#define FAC_AIS         S390_FEAT_ADAPTER_INT_SUPPRESSION
#define FAC_V           S390_FEAT_VECTOR /* vector facility */
#define FAC_VE          S390_FEAT_VECTOR_ENH  /* vector enhancements facility 1 */
#define FAC_VE2         S390_FEAT_VECTOR_ENH2 /* vector enhancements facility 2 */
#define FAC_MIE2        S390_FEAT_MISC_INSTRUCTION_EXT2 /* miscellaneous-instruction-extensions facility 2 */
#define FAC_MIE3        S390_FEAT_MISC_INSTRUCTION_EXT3 /* miscellaneous-instruction-extensions facility 3 */

static const DisasInsn insn_info[] = {
#include "insn-data.h.inc"
};

#undef E
#define E(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D, FL) \
    case OPC: return &insn_info[insn_ ## NM];

static const DisasInsn *lookup_opc(uint16_t opc)
{
    switch (opc) {
#include "insn-data.h.inc"
    default:
        return NULL;
    }
}

#undef F
#undef E
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
    case 3: /* MSB stored in RXB */
        g_assert(f->size == 4);
        switch (f->beg) {
        case 8:
            r |= extract64(insn, 63 - 36, 1) << 4;
            break;
        case 12:
            r |= extract64(insn, 63 - 37, 1) << 4;
            break;
        case 16:
            r |= extract64(insn, 63 - 38, 1) << 4;
            break;
        case 32:
            r |= extract64(insn, 63 - 39, 1) << 4;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    default:
        abort();
    }

    /*
     * Validate that the "compressed" encoding we selected above is valid.
     * I.e. we haven't made two different original fields overlap.
     */
    assert(((o->presentC >> f->indexC) & 1) == 0);
    o->presentC |= 1 << f->indexC;
    o->presentO |= 1 << f->indexO;

    o->c[f->indexC] = r;
}

/* Lookup the insn at the current PC, extracting the operands into O and
   returning the info struct for the insn.  Returns NULL for invalid insn.  */

static const DisasInsn *extract_insn(CPUS390XState *env, DisasContext *s)
{
    uint64_t insn, pc = s->base.pc_next;
    int op, op2, ilen;
    const DisasInsn *info;

    if (unlikely(s->ex_value)) {
        /* Drop the EX data now, so that it's clear on exception paths.  */
        tcg_gen_st_i64(tcg_constant_i64(0), cpu_env,
                       offsetof(CPUS390XState, ex_value));

        /* Extract the values saved by EXECUTE.  */
        insn = s->ex_value & 0xffffffffffff0000ull;
        ilen = s->ex_value & 0xf;

        /* Register insn bytes with translator so plugins work. */
        for (int i = 0; i < ilen; i++) {
            uint8_t byte = extract64(insn, 56 - (i * 8), 8);
            translator_fake_ldb(byte, pc + i);
        }
        op = insn >> 56;
    } else {
        insn = ld_code2(env, s, pc);
        op = (insn >> 8) & 0xff;
        ilen = get_ilen(op);
        switch (ilen) {
        case 2:
            insn = insn << 48;
            break;
        case 4:
            insn = ld_code4(env, s, pc) << 32;
            break;
        case 6:
            insn = (insn << 48) | (ld_code4(env, s, pc + 2) << 16);
            break;
        default:
            g_assert_not_reached();
        }
    }
    s->pc_tmp = s->base.pc_next + ilen;
    s->ilen = ilen;

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
    case 0xb2: /* S, RRF, RRE, IE */
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
    case 0xc5: /* MII */
    case 0xc7: /* SMI */
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

    memset(&s->fields, 0, sizeof(s->fields));
    s->fields.raw_insn = insn;
    s->fields.op = op;
    s->fields.op2 = op2;

    /* Lookup the instruction.  */
    info = lookup_opc(op << 8 | op2);
    s->insn = info;

    /* If we found it, extract the operands.  */
    if (info != NULL) {
        DisasFormat fmt = info->fmt;
        int i;

        for (i = 0; i < NUM_C_FIELD; ++i) {
            extract_field(&s->fields, &format_info[fmt].op[i], insn);
        }
    }
    return info;
}

static bool is_afp_reg(int reg)
{
    return reg % 2 || reg > 6;
}

static bool is_fp_pair(int reg)
{
    /* 0,1,4,5,8,9,12,13: to exclude the others, check for single bit */
    return !(reg & 0x2);
}

static DisasJumpType translate_one(CPUS390XState *env, DisasContext *s)
{
    const DisasInsn *insn;
    DisasJumpType ret = DISAS_NEXT;
    DisasOps o = {};
    bool icount = false;

    /* Search for the insn in the table.  */
    insn = extract_insn(env, s);

    /* Update insn_start now that we know the ILEN.  */
    tcg_set_insn_start_param(s->insn_start, 2, s->ilen);

    /* Not found means unimplemented/illegal opcode.  */
    if (insn == NULL) {
        qemu_log_mask(LOG_UNIMP, "unimplemented opcode 0x%02x%02x\n",
                      s->fields.op, s->fields.op2);
        gen_illegal_opcode(s);
        ret = DISAS_NORETURN;
        goto out;
    }

#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER) {
        TCGv_i64 addr = tcg_constant_i64(s->base.pc_next);
        gen_helper_per_ifetch(cpu_env, addr);
    }
#endif

    /* process flags */
    if (insn->flags) {
        /* privileged instruction */
        if ((s->base.tb->flags & FLAG_MASK_PSTATE) && (insn->flags & IF_PRIV)) {
            gen_program_exception(s, PGM_PRIVILEGED);
            ret = DISAS_NORETURN;
            goto out;
        }

        /* if AFP is not enabled, instructions and registers are forbidden */
        if (!(s->base.tb->flags & FLAG_MASK_AFP)) {
            uint8_t dxc = 0;

            if ((insn->flags & IF_AFP1) && is_afp_reg(get_field(s, r1))) {
                dxc = 1;
            }
            if ((insn->flags & IF_AFP2) && is_afp_reg(get_field(s, r2))) {
                dxc = 1;
            }
            if ((insn->flags & IF_AFP3) && is_afp_reg(get_field(s, r3))) {
                dxc = 1;
            }
            if (insn->flags & IF_BFP) {
                dxc = 2;
            }
            if (insn->flags & IF_DFP) {
                dxc = 3;
            }
            if (insn->flags & IF_VEC) {
                dxc = 0xfe;
            }
            if (dxc) {
                gen_data_exception(dxc);
                ret = DISAS_NORETURN;
                goto out;
            }
        }

        /* if vector instructions not enabled, executing them is forbidden */
        if (insn->flags & IF_VEC) {
            if (!((s->base.tb->flags & FLAG_MASK_VECTOR))) {
                gen_data_exception(0xfe);
                ret = DISAS_NORETURN;
                goto out;
            }
        }

        /* input/output is the special case for icount mode */
        if (unlikely(insn->flags & IF_IO)) {
            icount = tb_cflags(s->base.tb) & CF_USE_ICOUNT;
            if (icount) {
                gen_io_start();
            }
        }
    }

    /* Check for insn specification exceptions.  */
    if (insn->spec) {
        if ((insn->spec & SPEC_r1_even && get_field(s, r1) & 1) ||
            (insn->spec & SPEC_r2_even && get_field(s, r2) & 1) ||
            (insn->spec & SPEC_r3_even && get_field(s, r3) & 1) ||
            (insn->spec & SPEC_r1_f128 && !is_fp_pair(get_field(s, r1))) ||
            (insn->spec & SPEC_r2_f128 && !is_fp_pair(get_field(s, r2)))) {
            gen_program_exception(s, PGM_SPECIFICATION);
            ret = DISAS_NORETURN;
            goto out;
        }
    }

    /* Implement the instruction.  */
    if (insn->help_in1) {
        insn->help_in1(s, &o);
    }
    if (insn->help_in2) {
        insn->help_in2(s, &o);
    }
    if (insn->help_prep) {
        insn->help_prep(s, &o);
    }
    if (insn->help_op) {
        ret = insn->help_op(s, &o);
    }
    if (ret != DISAS_NORETURN) {
        if (insn->help_wout) {
            insn->help_wout(s, &o);
        }
        if (insn->help_cout) {
            insn->help_cout(s, &o);
        }
    }

    /* Free any temporaries created by the helpers.  */
    if (o.out && !o.g_out) {
        tcg_temp_free_i64(o.out);
    }
    if (o.out2 && !o.g_out2) {
        tcg_temp_free_i64(o.out2);
    }
    if (o.in1 && !o.g_in1) {
        tcg_temp_free_i64(o.in1);
    }
    if (o.in2 && !o.g_in2) {
        tcg_temp_free_i64(o.in2);
    }
    if (o.addr1) {
        tcg_temp_free_i64(o.addr1);
    }
    if (o.out_128) {
        tcg_temp_free_i128(o.out_128);
    }
    if (o.in1_128) {
        tcg_temp_free_i128(o.in1_128);
    }
    if (o.in2_128) {
        tcg_temp_free_i128(o.in2_128);
    }
    /* io should be the last instruction in tb when icount is enabled */
    if (unlikely(icount && ret == DISAS_NEXT)) {
        ret = DISAS_TOO_MANY;
    }

#ifndef CONFIG_USER_ONLY
    if (s->base.tb->flags & FLAG_MASK_PER) {
        /* An exception might be triggered, save PSW if not already done.  */
        if (ret == DISAS_NEXT || ret == DISAS_TOO_MANY) {
            tcg_gen_movi_i64(psw_addr, s->pc_tmp);
        }

        /* Call the helper to check for a possible PER exception.  */
        gen_helper_per_check_exception(cpu_env);
    }
#endif

out:
    /* Advance to the next instruction.  */
    s->base.pc_next = s->pc_tmp;
    return ret;
}

static void s390x_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    /* 31-bit mode */
    if (!(dc->base.tb->flags & FLAG_MASK_64)) {
        dc->base.pc_first &= 0x7fffffff;
        dc->base.pc_next = dc->base.pc_first;
    }

    dc->cc_op = CC_OP_DYNAMIC;
    dc->ex_value = dc->base.tb->cs_base;
    dc->exit_to_mainloop = (dc->base.tb->flags & FLAG_MASK_PER) || dc->ex_value;
}

static void s390x_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void s390x_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    /* Delay the set of ilen until we've read the insn. */
    tcg_gen_insn_start(dc->base.pc_next, dc->cc_op, 0);
    dc->insn_start = tcg_last_op();
}

static target_ulong get_next_pc(CPUS390XState *env, DisasContext *s,
                                uint64_t pc)
{
    uint64_t insn = cpu_lduw_code(env, pc);

    return pc + get_ilen((insn >> 8) & 0xff);
}

static void s390x_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    CPUS390XState *env = cs->env_ptr;
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    dc->base.is_jmp = translate_one(env, dc);
    if (dc->base.is_jmp == DISAS_NEXT) {
        if (dc->ex_value ||
            !is_same_page(dcbase, dc->base.pc_next) ||
            !is_same_page(dcbase, get_next_pc(env, dc, dc->base.pc_next))) {
            dc->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void s390x_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        update_psw_addr(dc);
        /* FALLTHRU */
    case DISAS_PC_UPDATED:
        /* Next TB starts off with CC_OP_DYNAMIC, so make sure the
           cc op type is in env */
        update_cc_op(dc);
        /* FALLTHRU */
    case DISAS_PC_CC_UPDATED:
        /* Exit the TB, either by raising a debug exception or by return.  */
        if (dc->exit_to_mainloop) {
            tcg_gen_exit_tb(NULL, 0);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void s390x_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cs, FILE *logfile)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (unlikely(dc->ex_value)) {
        /* ??? Unfortunately target_disas can't use host memory.  */
        fprintf(logfile, "IN: EXECUTE %016" PRIx64, dc->ex_value);
    } else {
        fprintf(logfile, "IN: %s\n", lookup_symbol(dc->base.pc_first));
        target_disas(logfile, cs, dc->base.pc_first, dc->base.tb->size);
    }
}

static const TranslatorOps s390x_tr_ops = {
    .init_disas_context = s390x_tr_init_disas_context,
    .tb_start           = s390x_tr_tb_start,
    .insn_start         = s390x_tr_insn_start,
    .translate_insn     = s390x_tr_translate_insn,
    .tb_stop            = s390x_tr_tb_stop,
    .disas_log          = s390x_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc;

    translator_loop(cs, tb, max_insns, pc, host_pc, &s390x_tr_ops, &dc.base);
}

void s390x_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    int cc_op = data[1];

    env->psw.addr = data[0];

    /* Update the CC opcode if it is not already up-to-date.  */
    if ((cc_op != CC_OP_DYNAMIC) && (cc_op != CC_OP_STATIC)) {
        env->cc_op = cc_op;
    }

    /* Record ILEN.  */
    env->int_pgm_ilen = data[2];
}
