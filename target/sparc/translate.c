/*
   SPARC translation

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>
   Copyright (C) 2003-2005 Fabrice Bellard

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "disas/disas.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "asi.h"


#define DEBUG_DISAS

#define DYNAMIC_PC  1 /* dynamic pc value */
#define JUMP_PC     2 /* dynamic pc value which takes only two values
                         according to jump_pc[T2] */

#define DISAS_EXIT  DISAS_TARGET_0

/* global register indexes */
static TCGv_ptr cpu_regwptr;
static TCGv cpu_cc_src, cpu_cc_src2, cpu_cc_dst;
static TCGv_i32 cpu_cc_op;
static TCGv_i32 cpu_psr;
static TCGv cpu_fsr, cpu_pc, cpu_npc;
static TCGv cpu_regs[32];
static TCGv cpu_y;
#ifndef CONFIG_USER_ONLY
static TCGv cpu_tbr;
#endif
static TCGv cpu_cond;
#ifdef TARGET_SPARC64
static TCGv_i32 cpu_xcc, cpu_fprs;
static TCGv cpu_gsr;
static TCGv cpu_tick_cmpr, cpu_stick_cmpr, cpu_hstick_cmpr;
static TCGv cpu_hintp, cpu_htba, cpu_hver, cpu_ssr, cpu_ver;
#else
static TCGv cpu_wim;
#endif
/* Floating point registers */
static TCGv_i64 cpu_fpr[TARGET_DPREGS];

#include "exec/gen-icount.h"

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong pc;    /* current Program Counter: integer or DYNAMIC_PC */
    target_ulong npc;   /* next PC: integer or DYNAMIC_PC or JUMP_PC */
    target_ulong jump_pc[2]; /* used when JUMP_PC pc value is used */
    int mem_idx;
    bool fpu_enabled;
    bool address_mask_32bit;
#ifndef CONFIG_USER_ONLY
    bool supervisor;
#ifdef TARGET_SPARC64
    bool hypervisor;
#endif
#endif

    uint32_t cc_op;  /* current CC operation */
    sparc_def_t *def;
    TCGv_i32 t32[3];
    TCGv ttl[5];
    int n_t32;
    int n_ttl;
#ifdef TARGET_SPARC64
    int fprs_dirty;
    int asi;
#endif
} DisasContext;

typedef struct {
    TCGCond cond;
    bool is_bool;
    bool g1, g2;
    TCGv c1, c2;
} DisasCompare;

// This function uses non-native bit order
#define GET_FIELD(X, FROM, TO)                                  \
    ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

// This function uses the order in the manuals, i.e. bit 0 is 2^0
#define GET_FIELD_SP(X, FROM, TO)               \
    GET_FIELD(X, 31 - (TO), 31 - (FROM))

#define GET_FIELDs(x,a,b) sign_extend (GET_FIELD(x,a,b), (b) - (a) + 1)
#define GET_FIELD_SPs(x,a,b) sign_extend (GET_FIELD_SP(x,a,b), ((b) - (a) + 1))

#ifdef TARGET_SPARC64
#define DFPREG(r) (((r & 1) << 5) | (r & 0x1e))
#define QFPREG(r) (((r & 1) << 5) | (r & 0x1c))
#else
#define DFPREG(r) (r & 0x1e)
#define QFPREG(r) (r & 0x1c)
#endif

#define UA2005_HTRAP_MASK 0xff
#define V8_TRAP_MASK 0x7f

static int sign_extend(int x, int len)
{
    len = 32 - len;
    return (x << len) >> len;
}

#define IS_IMM (insn & (1<<13))

static inline TCGv_i32 get_temp_i32(DisasContext *dc)
{
    TCGv_i32 t;
    assert(dc->n_t32 < ARRAY_SIZE(dc->t32));
    dc->t32[dc->n_t32++] = t = tcg_temp_new_i32();
    return t;
}

static inline TCGv get_temp_tl(DisasContext *dc)
{
    TCGv t;
    assert(dc->n_ttl < ARRAY_SIZE(dc->ttl));
    dc->ttl[dc->n_ttl++] = t = tcg_temp_new();
    return t;
}

static inline void gen_update_fprs_dirty(DisasContext *dc, int rd)
{
#if defined(TARGET_SPARC64)
    int bit = (rd < 32) ? 1 : 2;
    /* If we know we've already set this bit within the TB,
       we can avoid setting it again.  */
    if (!(dc->fprs_dirty & bit)) {
        dc->fprs_dirty |= bit;
        tcg_gen_ori_i32(cpu_fprs, cpu_fprs, bit);
    }
#endif
}

/* floating point registers moves */
static TCGv_i32 gen_load_fpr_F(DisasContext *dc, unsigned int src)
{
#if TCG_TARGET_REG_BITS == 32
    if (src & 1) {
        return TCGV_LOW(cpu_fpr[src / 2]);
    } else {
        return TCGV_HIGH(cpu_fpr[src / 2]);
    }
#else
    TCGv_i32 ret = get_temp_i32(dc);
    if (src & 1) {
        tcg_gen_extrl_i64_i32(ret, cpu_fpr[src / 2]);
    } else {
        tcg_gen_extrh_i64_i32(ret, cpu_fpr[src / 2]);
    }
    return ret;
#endif
}

static void gen_store_fpr_F(DisasContext *dc, unsigned int dst, TCGv_i32 v)
{
#if TCG_TARGET_REG_BITS == 32
    if (dst & 1) {
        tcg_gen_mov_i32(TCGV_LOW(cpu_fpr[dst / 2]), v);
    } else {
        tcg_gen_mov_i32(TCGV_HIGH(cpu_fpr[dst / 2]), v);
    }
#else
    TCGv_i64 t = (TCGv_i64)v;
    tcg_gen_deposit_i64(cpu_fpr[dst / 2], cpu_fpr[dst / 2], t,
                        (dst & 1 ? 0 : 32), 32);
#endif
    gen_update_fprs_dirty(dc, dst);
}

static TCGv_i32 gen_dest_fpr_F(DisasContext *dc)
{
    return get_temp_i32(dc);
}

static TCGv_i64 gen_load_fpr_D(DisasContext *dc, unsigned int src)
{
    src = DFPREG(src);
    return cpu_fpr[src / 2];
}

static void gen_store_fpr_D(DisasContext *dc, unsigned int dst, TCGv_i64 v)
{
    dst = DFPREG(dst);
    tcg_gen_mov_i64(cpu_fpr[dst / 2], v);
    gen_update_fprs_dirty(dc, dst);
}

static TCGv_i64 gen_dest_fpr_D(DisasContext *dc, unsigned int dst)
{
    return cpu_fpr[DFPREG(dst) / 2];
}

static void gen_op_load_fpr_QT0(unsigned int src)
{
    tcg_gen_st_i64(cpu_fpr[src / 2], cpu_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], cpu_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_load_fpr_QT1(unsigned int src)
{
    tcg_gen_st_i64(cpu_fpr[src / 2], cpu_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], cpu_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_store_QT0_fpr(unsigned int dst)
{
    tcg_gen_ld_i64(cpu_fpr[dst / 2], cpu_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_ld_i64(cpu_fpr[dst/2 + 1], cpu_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_store_fpr_Q(DisasContext *dc, unsigned int dst,
                            TCGv_i64 v1, TCGv_i64 v2)
{
    dst = QFPREG(dst);

    tcg_gen_mov_i64(cpu_fpr[dst / 2], v1);
    tcg_gen_mov_i64(cpu_fpr[dst / 2 + 1], v2);
    gen_update_fprs_dirty(dc, dst);
}

#ifdef TARGET_SPARC64
static TCGv_i64 gen_load_fpr_Q0(DisasContext *dc, unsigned int src)
{
    src = QFPREG(src);
    return cpu_fpr[src / 2];
}

static TCGv_i64 gen_load_fpr_Q1(DisasContext *dc, unsigned int src)
{
    src = QFPREG(src);
    return cpu_fpr[src / 2 + 1];
}

static void gen_move_Q(DisasContext *dc, unsigned int rd, unsigned int rs)
{
    rd = QFPREG(rd);
    rs = QFPREG(rs);

    tcg_gen_mov_i64(cpu_fpr[rd / 2], cpu_fpr[rs / 2]);
    tcg_gen_mov_i64(cpu_fpr[rd / 2 + 1], cpu_fpr[rs / 2 + 1]);
    gen_update_fprs_dirty(dc, rd);
}
#endif

/* moves */
#ifdef CONFIG_USER_ONLY
#define supervisor(dc) 0
#ifdef TARGET_SPARC64
#define hypervisor(dc) 0
#endif
#else
#ifdef TARGET_SPARC64
#define hypervisor(dc) (dc->hypervisor)
#define supervisor(dc) (dc->supervisor | dc->hypervisor)
#else
#define supervisor(dc) (dc->supervisor)
#endif
#endif

#ifdef TARGET_SPARC64
#ifndef TARGET_ABI32
#define AM_CHECK(dc) ((dc)->address_mask_32bit)
#else
#define AM_CHECK(dc) (1)
#endif
#endif

static inline void gen_address_mask(DisasContext *dc, TCGv addr)
{
#ifdef TARGET_SPARC64
    if (AM_CHECK(dc))
        tcg_gen_andi_tl(addr, addr, 0xffffffffULL);
#endif
}

static inline TCGv gen_load_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        TCGv t = get_temp_tl(dc);
        tcg_gen_movi_tl(t, 0);
        return t;
    }
}

static inline void gen_store_gpr(DisasContext *dc, int reg, TCGv v)
{
    if (reg > 0) {
        assert(reg < 32);
        tcg_gen_mov_tl(cpu_regs[reg], v);
    }
}

static inline TCGv gen_dest_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        return get_temp_tl(dc);
    }
}

static bool use_goto_tb(DisasContext *s, target_ulong pc, target_ulong npc)
{
    return translator_use_goto_tb(&s->base, pc) &&
           translator_use_goto_tb(&s->base, npc);
}

static void gen_goto_tb(DisasContext *s, int tb_num,
                        target_ulong pc, target_ulong npc)
{
    if (use_goto_tb(s, pc, npc))  {
        /* jump to same page: we can use a direct jump */
        tcg_gen_goto_tb(tb_num);
        tcg_gen_movi_tl(cpu_pc, pc);
        tcg_gen_movi_tl(cpu_npc, npc);
        tcg_gen_exit_tb(s->base.tb, tb_num);
    } else {
        /* jump to another page: currently not optimized */
        tcg_gen_movi_tl(cpu_pc, pc);
        tcg_gen_movi_tl(cpu_npc, npc);
        tcg_gen_exit_tb(NULL, 0);
    }
}

// XXX suboptimal
static inline void gen_mov_reg_N(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_NEG_SHIFT, 1);
}

static inline void gen_mov_reg_Z(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_ZERO_SHIFT, 1);
}

static inline void gen_mov_reg_V(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_OVF_SHIFT, 1);
}

static inline void gen_mov_reg_C(TCGv reg, TCGv_i32 src)
{
    tcg_gen_extu_i32_tl(reg, src);
    tcg_gen_extract_tl(reg, reg, PSR_CARRY_SHIFT, 1);
}

static inline void gen_op_add_cc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_mov_tl(cpu_cc_src, src1);
    tcg_gen_mov_tl(cpu_cc_src2, src2);
    tcg_gen_add_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);
    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static TCGv_i32 gen_add32_carry32(void)
{
    TCGv_i32 carry_32, cc_src1_32, cc_src2_32;

    /* Carry is computed from a previous add: (dst < src)  */
#if TARGET_LONG_BITS == 64
    cc_src1_32 = tcg_temp_new_i32();
    cc_src2_32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(cc_src1_32, cpu_cc_dst);
    tcg_gen_extrl_i64_i32(cc_src2_32, cpu_cc_src);
#else
    cc_src1_32 = cpu_cc_dst;
    cc_src2_32 = cpu_cc_src;
#endif

    carry_32 = tcg_temp_new_i32();
    tcg_gen_setcond_i32(TCG_COND_LTU, carry_32, cc_src1_32, cc_src2_32);

#if TARGET_LONG_BITS == 64
    tcg_temp_free_i32(cc_src1_32);
    tcg_temp_free_i32(cc_src2_32);
#endif

    return carry_32;
}

static TCGv_i32 gen_sub32_carry32(void)
{
    TCGv_i32 carry_32, cc_src1_32, cc_src2_32;

    /* Carry is computed from a previous borrow: (src1 < src2)  */
#if TARGET_LONG_BITS == 64
    cc_src1_32 = tcg_temp_new_i32();
    cc_src2_32 = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(cc_src1_32, cpu_cc_src);
    tcg_gen_extrl_i64_i32(cc_src2_32, cpu_cc_src2);
#else
    cc_src1_32 = cpu_cc_src;
    cc_src2_32 = cpu_cc_src2;
#endif

    carry_32 = tcg_temp_new_i32();
    tcg_gen_setcond_i32(TCG_COND_LTU, carry_32, cc_src1_32, cc_src2_32);

#if TARGET_LONG_BITS == 64
    tcg_temp_free_i32(cc_src1_32);
    tcg_temp_free_i32(cc_src2_32);
#endif

    return carry_32;
}

static void gen_op_addx_int(DisasContext *dc, TCGv dst, TCGv src1,
                            TCGv src2, int update_cc)
{
    TCGv_i32 carry_32;
    TCGv carry;

    switch (dc->cc_op) {
    case CC_OP_DIV:
    case CC_OP_LOGIC:
        /* Carry is known to be zero.  Fall back to plain ADD.  */
        if (update_cc) {
            gen_op_add_cc(dst, src1, src2);
        } else {
            tcg_gen_add_tl(dst, src1, src2);
        }
        return;

    case CC_OP_ADD:
    case CC_OP_TADD:
    case CC_OP_TADDTV:
        if (TARGET_LONG_BITS == 32) {
            /* We can re-use the host's hardware carry generation by using
               an ADD2 opcode.  We discard the low part of the output.
               Ideally we'd combine this operation with the add that
               generated the carry in the first place.  */
            carry = tcg_temp_new();
            tcg_gen_add2_tl(carry, dst, cpu_cc_src, src1, cpu_cc_src2, src2);
            tcg_temp_free(carry);
            goto add_done;
        }
        carry_32 = gen_add32_carry32();
        break;

    case CC_OP_SUB:
    case CC_OP_TSUB:
    case CC_OP_TSUBTV:
        carry_32 = gen_sub32_carry32();
        break;

    default:
        /* We need external help to produce the carry.  */
        carry_32 = tcg_temp_new_i32();
        gen_helper_compute_C_icc(carry_32, cpu_env);
        break;
    }

#if TARGET_LONG_BITS == 64
    carry = tcg_temp_new();
    tcg_gen_extu_i32_i64(carry, carry_32);
#else
    carry = carry_32;
#endif

    tcg_gen_add_tl(dst, src1, src2);
    tcg_gen_add_tl(dst, dst, carry);

    tcg_temp_free_i32(carry_32);
#if TARGET_LONG_BITS == 64
    tcg_temp_free(carry);
#endif

 add_done:
    if (update_cc) {
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
        tcg_gen_mov_tl(cpu_cc_dst, dst);
        tcg_gen_movi_i32(cpu_cc_op, CC_OP_ADDX);
        dc->cc_op = CC_OP_ADDX;
    }
}

static inline void gen_op_sub_cc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_mov_tl(cpu_cc_src, src1);
    tcg_gen_mov_tl(cpu_cc_src2, src2);
    tcg_gen_sub_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);
    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static void gen_op_subx_int(DisasContext *dc, TCGv dst, TCGv src1,
                            TCGv src2, int update_cc)
{
    TCGv_i32 carry_32;
    TCGv carry;

    switch (dc->cc_op) {
    case CC_OP_DIV:
    case CC_OP_LOGIC:
        /* Carry is known to be zero.  Fall back to plain SUB.  */
        if (update_cc) {
            gen_op_sub_cc(dst, src1, src2);
        } else {
            tcg_gen_sub_tl(dst, src1, src2);
        }
        return;

    case CC_OP_ADD:
    case CC_OP_TADD:
    case CC_OP_TADDTV:
        carry_32 = gen_add32_carry32();
        break;

    case CC_OP_SUB:
    case CC_OP_TSUB:
    case CC_OP_TSUBTV:
        if (TARGET_LONG_BITS == 32) {
            /* We can re-use the host's hardware carry generation by using
               a SUB2 opcode.  We discard the low part of the output.
               Ideally we'd combine this operation with the add that
               generated the carry in the first place.  */
            carry = tcg_temp_new();
            tcg_gen_sub2_tl(carry, dst, cpu_cc_src, src1, cpu_cc_src2, src2);
            tcg_temp_free(carry);
            goto sub_done;
        }
        carry_32 = gen_sub32_carry32();
        break;

    default:
        /* We need external help to produce the carry.  */
        carry_32 = tcg_temp_new_i32();
        gen_helper_compute_C_icc(carry_32, cpu_env);
        break;
    }

#if TARGET_LONG_BITS == 64
    carry = tcg_temp_new();
    tcg_gen_extu_i32_i64(carry, carry_32);
#else
    carry = carry_32;
#endif

    tcg_gen_sub_tl(dst, src1, src2);
    tcg_gen_sub_tl(dst, dst, carry);

    tcg_temp_free_i32(carry_32);
#if TARGET_LONG_BITS == 64
    tcg_temp_free(carry);
#endif

 sub_done:
    if (update_cc) {
        tcg_gen_mov_tl(cpu_cc_src, src1);
        tcg_gen_mov_tl(cpu_cc_src2, src2);
        tcg_gen_mov_tl(cpu_cc_dst, dst);
        tcg_gen_movi_i32(cpu_cc_op, CC_OP_SUBX);
        dc->cc_op = CC_OP_SUBX;
    }
}

static inline void gen_op_mulscc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv r_temp, zero, t0;

    r_temp = tcg_temp_new();
    t0 = tcg_temp_new();

    /* old op:
    if (!(env->y & 1))
        T1 = 0;
    */
    zero = tcg_const_tl(0);
    tcg_gen_andi_tl(cpu_cc_src, src1, 0xffffffff);
    tcg_gen_andi_tl(r_temp, cpu_y, 0x1);
    tcg_gen_andi_tl(cpu_cc_src2, src2, 0xffffffff);
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_cc_src2, r_temp, zero,
                       zero, cpu_cc_src2);
    tcg_temp_free(zero);

    // b2 = T0 & 1;
    // env->y = (b2 << 31) | (env->y >> 1);
    tcg_gen_extract_tl(t0, cpu_y, 1, 31);
    tcg_gen_deposit_tl(cpu_y, t0, cpu_cc_src, 31, 1);

    // b1 = N ^ V;
    gen_mov_reg_N(t0, cpu_psr);
    gen_mov_reg_V(r_temp, cpu_psr);
    tcg_gen_xor_tl(t0, t0, r_temp);
    tcg_temp_free(r_temp);

    // T0 = (b1 << 31) | (T0 >> 1);
    // src1 = T0;
    tcg_gen_shli_tl(t0, t0, 31);
    tcg_gen_shri_tl(cpu_cc_src, cpu_cc_src, 1);
    tcg_gen_or_tl(cpu_cc_src, cpu_cc_src, t0);
    tcg_temp_free(t0);

    tcg_gen_add_tl(cpu_cc_dst, cpu_cc_src, cpu_cc_src2);

    tcg_gen_mov_tl(dst, cpu_cc_dst);
}

static inline void gen_op_multiply(TCGv dst, TCGv src1, TCGv src2, int sign_ext)
{
#if TARGET_LONG_BITS == 32
    if (sign_ext) {
        tcg_gen_muls2_tl(dst, cpu_y, src1, src2);
    } else {
        tcg_gen_mulu2_tl(dst, cpu_y, src1, src2);
    }
#else
    TCGv t0 = tcg_temp_new_i64();
    TCGv t1 = tcg_temp_new_i64();

    if (sign_ext) {
        tcg_gen_ext32s_i64(t0, src1);
        tcg_gen_ext32s_i64(t1, src2);
    } else {
        tcg_gen_ext32u_i64(t0, src1);
        tcg_gen_ext32u_i64(t1, src2);
    }

    tcg_gen_mul_i64(dst, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);

    tcg_gen_shri_i64(cpu_y, dst, 32);
#endif
}

static inline void gen_op_umul(TCGv dst, TCGv src1, TCGv src2)
{
    /* zero-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 0);
}

static inline void gen_op_smul(TCGv dst, TCGv src1, TCGv src2)
{
    /* sign-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 1);
}

// 1
static inline void gen_op_eval_ba(TCGv dst)
{
    tcg_gen_movi_tl(dst, 1);
}

// Z
static inline void gen_op_eval_be(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_Z(dst, src);
}

// Z | (N ^ V)
static inline void gen_op_eval_ble(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_N(t0, src);
    gen_mov_reg_V(dst, src);
    tcg_gen_xor_tl(dst, dst, t0);
    gen_mov_reg_Z(t0, src);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// N ^ V
static inline void gen_op_eval_bl(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_V(t0, src);
    gen_mov_reg_N(dst, src);
    tcg_gen_xor_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// C | Z
static inline void gen_op_eval_bleu(TCGv dst, TCGv_i32 src)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_Z(t0, src);
    gen_mov_reg_C(dst, src);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// C
static inline void gen_op_eval_bcs(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_C(dst, src);
}

// V
static inline void gen_op_eval_bvs(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_V(dst, src);
}

// 0
static inline void gen_op_eval_bn(TCGv dst)
{
    tcg_gen_movi_tl(dst, 0);
}

// N
static inline void gen_op_eval_bneg(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_N(dst, src);
}

// !Z
static inline void gen_op_eval_bne(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_Z(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(Z | (N ^ V))
static inline void gen_op_eval_bg(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_ble(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(N ^ V)
static inline void gen_op_eval_bge(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_bl(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !(C | Z)
static inline void gen_op_eval_bgu(TCGv dst, TCGv_i32 src)
{
    gen_op_eval_bleu(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !C
static inline void gen_op_eval_bcc(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_C(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !N
static inline void gen_op_eval_bpos(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_N(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !V
static inline void gen_op_eval_bvc(TCGv dst, TCGv_i32 src)
{
    gen_mov_reg_V(dst, src);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

/*
  FPSR bit field FCC1 | FCC0:
   0 =
   1 <
   2 >
   3 unordered
*/
static inline void gen_mov_reg_FCC0(TCGv reg, TCGv src,
                                    unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC0_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

static inline void gen_mov_reg_FCC1(TCGv reg, TCGv src,
                                    unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC1_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

// !0: FCC0 | FCC1
static inline void gen_op_eval_fbne(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// 1 or 2: FCC0 ^ FCC1
static inline void gen_op_eval_fblg(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// 1 or 3: FCC0
static inline void gen_op_eval_fbul(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
}

// 1: FCC0 & !FCC1
static inline void gen_op_eval_fbl(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// 2 or 3: FCC1
static inline void gen_op_eval_fbug(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
}

// 2: !FCC0 & FCC1
static inline void gen_op_eval_fbg(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
    tcg_temp_free(t0);
}

// 3: FCC0 & FCC1
static inline void gen_op_eval_fbu(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
    tcg_temp_free(t0);
}

// 0: !(FCC0 | FCC1)
static inline void gen_op_eval_fbe(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
    tcg_temp_free(t0);
}

// 0 or 3: !(FCC0 ^ FCC1)
static inline void gen_op_eval_fbue(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
    tcg_temp_free(t0);
}

// 0 or 2: !FCC0
static inline void gen_op_eval_fbge(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !1: !(FCC0 & !FCC1)
static inline void gen_op_eval_fbuge(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
    tcg_temp_free(t0);
}

// 0 or 1: !FCC1
static inline void gen_op_eval_fble(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !2: !(!FCC0 & FCC1)
static inline void gen_op_eval_fbule(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
    tcg_gen_xori_tl(dst, dst, 0x1);
    tcg_temp_free(t0);
}

// !3: !(FCC0 & FCC1)
static inline void gen_op_eval_fbo(TCGv dst, TCGv src,
                                    unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
    tcg_temp_free(t0);
}

static inline void gen_branch2(DisasContext *dc, target_ulong pc1,
                               target_ulong pc2, TCGv r_cond)
{
    TCGLabel *l1 = gen_new_label();

    tcg_gen_brcondi_tl(TCG_COND_EQ, r_cond, 0, l1);

    gen_goto_tb(dc, 0, pc1, pc1 + 4);

    gen_set_label(l1);
    gen_goto_tb(dc, 1, pc2, pc2 + 4);
}

static void gen_branch_a(DisasContext *dc, target_ulong pc1)
{
    TCGLabel *l1 = gen_new_label();
    target_ulong npc = dc->npc;

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_cond, 0, l1);

    gen_goto_tb(dc, 0, npc, pc1);

    gen_set_label(l1);
    gen_goto_tb(dc, 1, npc + 4, npc + 8);

    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_branch_n(DisasContext *dc, target_ulong pc1)
{
    target_ulong npc = dc->npc;

    if (likely(npc != DYNAMIC_PC)) {
        dc->pc = npc;
        dc->jump_pc[0] = pc1;
        dc->jump_pc[1] = npc + 4;
        dc->npc = JUMP_PC;
    } else {
        TCGv t, z;

        tcg_gen_mov_tl(cpu_pc, cpu_npc);

        tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
        t = tcg_const_tl(pc1);
        z = tcg_const_tl(0);
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_npc, cpu_cond, z, t, cpu_npc);
        tcg_temp_free(t);
        tcg_temp_free(z);

        dc->pc = DYNAMIC_PC;
    }
}

static inline void gen_generic_branch(DisasContext *dc)
{
    TCGv npc0 = tcg_const_tl(dc->jump_pc[0]);
    TCGv npc1 = tcg_const_tl(dc->jump_pc[1]);
    TCGv zero = tcg_const_tl(0);

    tcg_gen_movcond_tl(TCG_COND_NE, cpu_npc, cpu_cond, zero, npc0, npc1);

    tcg_temp_free(npc0);
    tcg_temp_free(npc1);
    tcg_temp_free(zero);
}

/* call this function before using the condition register as it may
   have been set for a jump */
static inline void flush_cond(DisasContext *dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc);
        dc->npc = DYNAMIC_PC;
    }
}

static inline void save_npc(DisasContext *dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc);
        dc->npc = DYNAMIC_PC;
    } else if (dc->npc != DYNAMIC_PC) {
        tcg_gen_movi_tl(cpu_npc, dc->npc);
    }
}

static inline void update_psr(DisasContext *dc)
{
    if (dc->cc_op != CC_OP_FLAGS) {
        dc->cc_op = CC_OP_FLAGS;
        gen_helper_compute_psr(cpu_env);
    }
}

static inline void save_state(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    save_npc(dc);
}

static void gen_exception(DisasContext *dc, int which)
{
    TCGv_i32 t;

    save_state(dc);
    t = tcg_const_i32(which);
    gen_helper_raise_exception(cpu_env, t);
    tcg_temp_free_i32(t);
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_check_align(TCGv addr, int mask)
{
    TCGv_i32 r_mask = tcg_const_i32(mask);
    gen_helper_check_align(cpu_env, addr, r_mask);
    tcg_temp_free_i32(r_mask);
}

static inline void gen_mov_pc_npc(DisasContext *dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc);
        tcg_gen_mov_tl(cpu_pc, cpu_npc);
        dc->pc = DYNAMIC_PC;
    } else if (dc->npc == DYNAMIC_PC) {
        tcg_gen_mov_tl(cpu_pc, cpu_npc);
        dc->pc = DYNAMIC_PC;
    } else {
        dc->pc = dc->npc;
    }
}

static inline void gen_op_next_insn(void)
{
    tcg_gen_mov_tl(cpu_pc, cpu_npc);
    tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
}

static void free_compare(DisasCompare *cmp)
{
    if (!cmp->g1) {
        tcg_temp_free(cmp->c1);
    }
    if (!cmp->g2) {
        tcg_temp_free(cmp->c2);
    }
}

static void gen_compare(DisasCompare *cmp, bool xcc, unsigned int cond,
                        DisasContext *dc)
{
    static int subcc_cond[16] = {
        TCG_COND_NEVER,
        TCG_COND_EQ,
        TCG_COND_LE,
        TCG_COND_LT,
        TCG_COND_LEU,
        TCG_COND_LTU,
        -1, /* neg */
        -1, /* overflow */
        TCG_COND_ALWAYS,
        TCG_COND_NE,
        TCG_COND_GT,
        TCG_COND_GE,
        TCG_COND_GTU,
        TCG_COND_GEU,
        -1, /* pos */
        -1, /* no overflow */
    };

    static int logic_cond[16] = {
        TCG_COND_NEVER,
        TCG_COND_EQ,     /* eq:  Z */
        TCG_COND_LE,     /* le:  Z | (N ^ V) -> Z | N */
        TCG_COND_LT,     /* lt:  N ^ V -> N */
        TCG_COND_EQ,     /* leu: C | Z -> Z */
        TCG_COND_NEVER,  /* ltu: C -> 0 */
        TCG_COND_LT,     /* neg: N */
        TCG_COND_NEVER,  /* vs:  V -> 0 */
        TCG_COND_ALWAYS,
        TCG_COND_NE,     /* ne:  !Z */
        TCG_COND_GT,     /* gt:  !(Z | (N ^ V)) -> !(Z | N) */
        TCG_COND_GE,     /* ge:  !(N ^ V) -> !N */
        TCG_COND_NE,     /* gtu: !(C | Z) -> !Z */
        TCG_COND_ALWAYS, /* geu: !C -> 1 */
        TCG_COND_GE,     /* pos: !N */
        TCG_COND_ALWAYS, /* vc:  !V -> 1 */
    };

    TCGv_i32 r_src;
    TCGv r_dst;

#ifdef TARGET_SPARC64
    if (xcc) {
        r_src = cpu_xcc;
    } else {
        r_src = cpu_psr;
    }
#else
    r_src = cpu_psr;
#endif

    switch (dc->cc_op) {
    case CC_OP_LOGIC:
        cmp->cond = logic_cond[cond];
    do_compare_dst_0:
        cmp->is_bool = false;
        cmp->g2 = false;
        cmp->c2 = tcg_const_tl(0);
#ifdef TARGET_SPARC64
        if (!xcc) {
            cmp->g1 = false;
            cmp->c1 = tcg_temp_new();
            tcg_gen_ext32s_tl(cmp->c1, cpu_cc_dst);
            break;
        }
#endif
        cmp->g1 = true;
        cmp->c1 = cpu_cc_dst;
        break;

    case CC_OP_SUB:
        switch (cond) {
        case 6:  /* neg */
        case 14: /* pos */
            cmp->cond = (cond == 6 ? TCG_COND_LT : TCG_COND_GE);
            goto do_compare_dst_0;

        case 7: /* overflow */
        case 15: /* !overflow */
            goto do_dynamic;

        default:
            cmp->cond = subcc_cond[cond];
            cmp->is_bool = false;
#ifdef TARGET_SPARC64
            if (!xcc) {
                /* Note that sign-extension works for unsigned compares as
                   long as both operands are sign-extended.  */
                cmp->g1 = cmp->g2 = false;
                cmp->c1 = tcg_temp_new();
                cmp->c2 = tcg_temp_new();
                tcg_gen_ext32s_tl(cmp->c1, cpu_cc_src);
                tcg_gen_ext32s_tl(cmp->c2, cpu_cc_src2);
                break;
            }
#endif
            cmp->g1 = cmp->g2 = true;
            cmp->c1 = cpu_cc_src;
            cmp->c2 = cpu_cc_src2;
            break;
        }
        break;

    default:
    do_dynamic:
        gen_helper_compute_psr(cpu_env);
        dc->cc_op = CC_OP_FLAGS;
        /* FALLTHRU */

    case CC_OP_FLAGS:
        /* We're going to generate a boolean result.  */
        cmp->cond = TCG_COND_NE;
        cmp->is_bool = true;
        cmp->g1 = cmp->g2 = false;
        cmp->c1 = r_dst = tcg_temp_new();
        cmp->c2 = tcg_const_tl(0);

        switch (cond) {
        case 0x0:
            gen_op_eval_bn(r_dst);
            break;
        case 0x1:
            gen_op_eval_be(r_dst, r_src);
            break;
        case 0x2:
            gen_op_eval_ble(r_dst, r_src);
            break;
        case 0x3:
            gen_op_eval_bl(r_dst, r_src);
            break;
        case 0x4:
            gen_op_eval_bleu(r_dst, r_src);
            break;
        case 0x5:
            gen_op_eval_bcs(r_dst, r_src);
            break;
        case 0x6:
            gen_op_eval_bneg(r_dst, r_src);
            break;
        case 0x7:
            gen_op_eval_bvs(r_dst, r_src);
            break;
        case 0x8:
            gen_op_eval_ba(r_dst);
            break;
        case 0x9:
            gen_op_eval_bne(r_dst, r_src);
            break;
        case 0xa:
            gen_op_eval_bg(r_dst, r_src);
            break;
        case 0xb:
            gen_op_eval_bge(r_dst, r_src);
            break;
        case 0xc:
            gen_op_eval_bgu(r_dst, r_src);
            break;
        case 0xd:
            gen_op_eval_bcc(r_dst, r_src);
            break;
        case 0xe:
            gen_op_eval_bpos(r_dst, r_src);
            break;
        case 0xf:
            gen_op_eval_bvc(r_dst, r_src);
            break;
        }
        break;
    }
}

static void gen_fcompare(DisasCompare *cmp, unsigned int cc, unsigned int cond)
{
    unsigned int offset;
    TCGv r_dst;

    /* For now we still generate a straight boolean result.  */
    cmp->cond = TCG_COND_NE;
    cmp->is_bool = true;
    cmp->g1 = cmp->g2 = false;
    cmp->c1 = r_dst = tcg_temp_new();
    cmp->c2 = tcg_const_tl(0);

    switch (cc) {
    default:
    case 0x0:
        offset = 0;
        break;
    case 0x1:
        offset = 32 - 10;
        break;
    case 0x2:
        offset = 34 - 10;
        break;
    case 0x3:
        offset = 36 - 10;
        break;
    }

    switch (cond) {
    case 0x0:
        gen_op_eval_bn(r_dst);
        break;
    case 0x1:
        gen_op_eval_fbne(r_dst, cpu_fsr, offset);
        break;
    case 0x2:
        gen_op_eval_fblg(r_dst, cpu_fsr, offset);
        break;
    case 0x3:
        gen_op_eval_fbul(r_dst, cpu_fsr, offset);
        break;
    case 0x4:
        gen_op_eval_fbl(r_dst, cpu_fsr, offset);
        break;
    case 0x5:
        gen_op_eval_fbug(r_dst, cpu_fsr, offset);
        break;
    case 0x6:
        gen_op_eval_fbg(r_dst, cpu_fsr, offset);
        break;
    case 0x7:
        gen_op_eval_fbu(r_dst, cpu_fsr, offset);
        break;
    case 0x8:
        gen_op_eval_ba(r_dst);
        break;
    case 0x9:
        gen_op_eval_fbe(r_dst, cpu_fsr, offset);
        break;
    case 0xa:
        gen_op_eval_fbue(r_dst, cpu_fsr, offset);
        break;
    case 0xb:
        gen_op_eval_fbge(r_dst, cpu_fsr, offset);
        break;
    case 0xc:
        gen_op_eval_fbuge(r_dst, cpu_fsr, offset);
        break;
    case 0xd:
        gen_op_eval_fble(r_dst, cpu_fsr, offset);
        break;
    case 0xe:
        gen_op_eval_fbule(r_dst, cpu_fsr, offset);
        break;
    case 0xf:
        gen_op_eval_fbo(r_dst, cpu_fsr, offset);
        break;
    }
}

static void gen_cond(TCGv r_dst, unsigned int cc, unsigned int cond,
                     DisasContext *dc)
{
    DisasCompare cmp;
    gen_compare(&cmp, cc, cond, dc);

    /* The interface is to return a boolean in r_dst.  */
    if (cmp.is_bool) {
        tcg_gen_mov_tl(r_dst, cmp.c1);
    } else {
        tcg_gen_setcond_tl(cmp.cond, r_dst, cmp.c1, cmp.c2);
    }

    free_compare(&cmp);
}

static void gen_fcond(TCGv r_dst, unsigned int cc, unsigned int cond)
{
    DisasCompare cmp;
    gen_fcompare(&cmp, cc, cond);

    /* The interface is to return a boolean in r_dst.  */
    if (cmp.is_bool) {
        tcg_gen_mov_tl(r_dst, cmp.c1);
    } else {
        tcg_gen_setcond_tl(cmp.cond, r_dst, cmp.c1, cmp.c2);
    }

    free_compare(&cmp);
}

#ifdef TARGET_SPARC64
// Inverted logic
static const int gen_tcg_cond_reg[8] = {
    -1,
    TCG_COND_NE,
    TCG_COND_GT,
    TCG_COND_GE,
    -1,
    TCG_COND_EQ,
    TCG_COND_LE,
    TCG_COND_LT,
};

static void gen_compare_reg(DisasCompare *cmp, int cond, TCGv r_src)
{
    cmp->cond = tcg_invert_cond(gen_tcg_cond_reg[cond]);
    cmp->is_bool = false;
    cmp->g1 = true;
    cmp->g2 = false;
    cmp->c1 = r_src;
    cmp->c2 = tcg_const_tl(0);
}

static inline void gen_cond_reg(TCGv r_dst, int cond, TCGv r_src)
{
    DisasCompare cmp;
    gen_compare_reg(&cmp, cond, r_src);

    /* The interface is to return a boolean in r_dst.  */
    tcg_gen_setcond_tl(cmp.cond, r_dst, cmp.c1, cmp.c2);

    free_compare(&cmp);
}
#endif

static void do_branch(DisasContext *dc, int32_t offset, uint32_t insn, int cc)
{
    unsigned int cond = GET_FIELD(insn, 3, 6), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

#ifdef TARGET_SPARC64
    if (unlikely(AM_CHECK(dc))) {
        target &= 0xffffffffULL;
    }
#endif
    if (cond == 0x0) {
        /* unconditional not taken */
        if (a) {
            dc->pc = dc->npc + 4;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = dc->pc + 4;
        }
    } else if (cond == 0x8) {
        /* unconditional taken */
        if (a) {
            dc->pc = target;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = target;
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
        }
    } else {
        flush_cond(dc);
        gen_cond(cpu_cond, cc, cond, dc);
        if (a) {
            gen_branch_a(dc, target);
        } else {
            gen_branch_n(dc, target);
        }
    }
}

static void do_fbranch(DisasContext *dc, int32_t offset, uint32_t insn, int cc)
{
    unsigned int cond = GET_FIELD(insn, 3, 6), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

#ifdef TARGET_SPARC64
    if (unlikely(AM_CHECK(dc))) {
        target &= 0xffffffffULL;
    }
#endif
    if (cond == 0x0) {
        /* unconditional not taken */
        if (a) {
            dc->pc = dc->npc + 4;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = dc->pc + 4;
        }
    } else if (cond == 0x8) {
        /* unconditional taken */
        if (a) {
            dc->pc = target;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = target;
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
        }
    } else {
        flush_cond(dc);
        gen_fcond(cpu_cond, cc, cond);
        if (a) {
            gen_branch_a(dc, target);
        } else {
            gen_branch_n(dc, target);
        }
    }
}

#ifdef TARGET_SPARC64
static void do_branch_reg(DisasContext *dc, int32_t offset, uint32_t insn,
                          TCGv r_reg)
{
    unsigned int cond = GET_FIELD_SP(insn, 25, 27), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

    if (unlikely(AM_CHECK(dc))) {
        target &= 0xffffffffULL;
    }
    flush_cond(dc);
    gen_cond_reg(cpu_cond, cond, r_reg);
    if (a) {
        gen_branch_a(dc, target);
    } else {
        gen_branch_n(dc, target);
    }
}

static inline void gen_op_fcmps(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmps(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmps_fcc1(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmps_fcc2(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmps_fcc3(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    }
}

static inline void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpd(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpd_fcc1(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpd_fcc2(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpd_fcc3(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    }
}

static inline void gen_op_fcmpq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpq(cpu_fsr, cpu_env);
        break;
    case 1:
        gen_helper_fcmpq_fcc1(cpu_fsr, cpu_env);
        break;
    case 2:
        gen_helper_fcmpq_fcc2(cpu_fsr, cpu_env);
        break;
    case 3:
        gen_helper_fcmpq_fcc3(cpu_fsr, cpu_env);
        break;
    }
}

static inline void gen_op_fcmpes(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpes(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpes_fcc1(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpes_fcc2(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpes_fcc3(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    }
}

static inline void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmped(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmped_fcc1(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmped_fcc2(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmped_fcc3(cpu_fsr, cpu_env, r_rs1, r_rs2);
        break;
    }
}

static inline void gen_op_fcmpeq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpeq(cpu_fsr, cpu_env);
        break;
    case 1:
        gen_helper_fcmpeq_fcc1(cpu_fsr, cpu_env);
        break;
    case 2:
        gen_helper_fcmpeq_fcc2(cpu_fsr, cpu_env);
        break;
    case 3:
        gen_helper_fcmpeq_fcc3(cpu_fsr, cpu_env);
        break;
    }
}

#else

static inline void gen_op_fcmps(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmps(cpu_fsr, cpu_env, r_rs1, r_rs2);
}

static inline void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmpd(cpu_fsr, cpu_env, r_rs1, r_rs2);
}

static inline void gen_op_fcmpq(int fccno)
{
    gen_helper_fcmpq(cpu_fsr, cpu_env);
}

static inline void gen_op_fcmpes(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmpes(cpu_fsr, cpu_env, r_rs1, r_rs2);
}

static inline void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmped(cpu_fsr, cpu_env, r_rs1, r_rs2);
}

static inline void gen_op_fcmpeq(int fccno)
{
    gen_helper_fcmpeq(cpu_fsr, cpu_env);
}
#endif

static void gen_op_fpexception_im(DisasContext *dc, int fsr_flags)
{
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, FSR_FTT_NMASK);
    tcg_gen_ori_tl(cpu_fsr, cpu_fsr, fsr_flags);
    gen_exception(dc, TT_FP_EXCP);
}

static int gen_trap_ifnofpu(DisasContext *dc)
{
#if !defined(CONFIG_USER_ONLY)
    if (!dc->fpu_enabled) {
        gen_exception(dc, TT_NFPU_INSN);
        return 1;
    }
#endif
    return 0;
}

static inline void gen_op_clear_ieee_excp_and_FTT(void)
{
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, FSR_FTT_CEXC_NMASK);
}

static inline void gen_fop_FF(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i32))
{
    TCGv_i32 dst, src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, cpu_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_F(dc, rd, dst);
}

static inline void gen_ne_fop_FF(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, src);

    gen_store_fpr_F(dc, rd, dst);
}

static inline void gen_fop_FFF(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_F(dc);

    gen(dst, cpu_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_F(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static inline void gen_ne_fop_FFF(DisasContext *dc, int rd, int rs1, int rs2,
                                  void (*gen)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 dst, src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_F(dc);

    gen(dst, src1, src2);

    gen_store_fpr_F(dc, rd, dst);
}
#endif

static inline void gen_fop_DD(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i64))
{
    TCGv_i64 dst, src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_D(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static inline void gen_ne_fop_DD(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static inline void gen_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_D(dc, rd, dst);
}

#ifdef TARGET_SPARC64
static inline void gen_ne_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                                  void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}

static inline void gen_gsr_fop_DDD(DisasContext *dc, int rd, int rs1, int rs2,
                           void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_gsr, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}

static inline void gen_ne_fop_DDDD(DisasContext *dc, int rd, int rs1, int rs2,
                           void (*gen)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src0, src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);
    src0 = gen_load_fpr_D(dc, rd);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, src0, src1, src2);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static inline void gen_fop_QQ(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT1(QFPREG(rs));

    gen(cpu_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

#ifdef TARGET_SPARC64
static inline void gen_ne_fop_QQ(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT1(QFPREG(rs));

    gen(cpu_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}
#endif

static inline void gen_fop_QQQ(DisasContext *dc, int rd, int rs1, int rs2,
                               void (*gen)(TCGv_ptr))
{
    gen_op_load_fpr_QT0(QFPREG(rs1));
    gen_op_load_fpr_QT1(QFPREG(rs2));

    gen(cpu_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static inline void gen_fop_DFF(DisasContext *dc, int rd, int rs1, int rs2,
                        void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src1, src2;

    src1 = gen_load_fpr_F(dc, rs1);
    src2 = gen_load_fpr_F(dc, rs2);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_D(dc, rd, dst);
}

static inline void gen_fop_QDD(DisasContext *dc, int rd, int rs1, int rs2,
                               void (*gen)(TCGv_ptr, TCGv_i64, TCGv_i64))
{
    TCGv_i64 src1, src2;

    src1 = gen_load_fpr_D(dc, rs1);
    src2 = gen_load_fpr_D(dc, rs2);

    gen(cpu_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

#ifdef TARGET_SPARC64
static inline void gen_fop_DF(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_D(dc, rd, dst);
}
#endif

static inline void gen_ne_fop_DF(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_i64, TCGv_ptr, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env, src);

    gen_store_fpr_D(dc, rd, dst);
}

static inline void gen_fop_FD(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i32, TCGv_ptr, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    src = gen_load_fpr_D(dc, rs);
    dst = gen_dest_fpr_F(dc);

    gen(dst, cpu_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_F(dc, rd, dst);
}

static inline void gen_fop_FQ(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i32, TCGv_ptr))
{
    TCGv_i32 dst;

    gen_op_load_fpr_QT1(QFPREG(rs));
    dst = gen_dest_fpr_F(dc);

    gen(dst, cpu_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_F(dc, rd, dst);
}

static inline void gen_fop_DQ(DisasContext *dc, int rd, int rs,
                              void (*gen)(TCGv_i64, TCGv_ptr))
{
    TCGv_i64 dst;

    gen_op_load_fpr_QT1(QFPREG(rs));
    dst = gen_dest_fpr_D(dc, rd);

    gen(dst, cpu_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, cpu_env);

    gen_store_fpr_D(dc, rd, dst);
}

static inline void gen_ne_fop_QF(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_ptr, TCGv_i32))
{
    TCGv_i32 src;

    src = gen_load_fpr_F(dc, rs);

    gen(cpu_env, src);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static inline void gen_ne_fop_QD(DisasContext *dc, int rd, int rs,
                                 void (*gen)(TCGv_ptr, TCGv_i64))
{
    TCGv_i64 src;

    src = gen_load_fpr_D(dc, rs);

    gen(cpu_env, src);

    gen_op_store_QT0_fpr(QFPREG(rd));
    gen_update_fprs_dirty(dc, QFPREG(rd));
}

static void gen_swap(DisasContext *dc, TCGv dst, TCGv src,
                     TCGv addr, int mmu_idx, MemOp memop)
{
    gen_address_mask(dc, addr);
    tcg_gen_atomic_xchg_tl(dst, addr, src, mmu_idx, memop);
}

static void gen_ldstub(DisasContext *dc, TCGv dst, TCGv addr, int mmu_idx)
{
    TCGv m1 = tcg_const_tl(0xff);
    gen_address_mask(dc, addr);
    tcg_gen_atomic_xchg_tl(dst, addr, m1, mmu_idx, MO_UB);
    tcg_temp_free(m1);
}

/* asi moves */
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
typedef enum {
    GET_ASI_HELPER,
    GET_ASI_EXCP,
    GET_ASI_DIRECT,
    GET_ASI_DTWINX,
    GET_ASI_BLOCK,
    GET_ASI_SHORT,
    GET_ASI_BCOPY,
    GET_ASI_BFILL,
} ASIType;

typedef struct {
    ASIType type;
    int asi;
    int mem_idx;
    MemOp memop;
} DisasASI;

static DisasASI get_asi(DisasContext *dc, int insn, MemOp memop)
{
    int asi = GET_FIELD(insn, 19, 26);
    ASIType type = GET_ASI_HELPER;
    int mem_idx = dc->mem_idx;

#ifndef TARGET_SPARC64
    /* Before v9, all asis are immediate and privileged.  */
    if (IS_IMM) {
        gen_exception(dc, TT_ILL_INSN);
        type = GET_ASI_EXCP;
    } else if (supervisor(dc)
               /* Note that LEON accepts ASI_USERDATA in user mode, for
                  use with CASA.  Also note that previous versions of
                  QEMU allowed (and old versions of gcc emitted) ASI_P
                  for LEON, which is incorrect.  */
               || (asi == ASI_USERDATA
                   && (dc->def->features & CPU_FEATURE_CASA))) {
        switch (asi) {
        case ASI_USERDATA:   /* User data access */
            mem_idx = MMU_USER_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_KERNELDATA: /* Supervisor data access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_M_BYPASS:    /* MMU passthrough */
        case ASI_LEON_BYPASS: /* LEON MMU passthrough */
            mem_idx = MMU_PHYS_IDX;
            type = GET_ASI_DIRECT;
            break;
        case ASI_M_BCOPY: /* Block copy, sta access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_BCOPY;
            break;
        case ASI_M_BFILL: /* Block fill, stda access */
            mem_idx = MMU_KERNEL_IDX;
            type = GET_ASI_BFILL;
            break;
        }

        /* MMU_PHYS_IDX is used when the MMU is disabled to passthrough the
         * permissions check in get_physical_address(..).
         */
        mem_idx = (dc->mem_idx == MMU_PHYS_IDX) ? MMU_PHYS_IDX : mem_idx;
    } else {
        gen_exception(dc, TT_PRIV_INSN);
        type = GET_ASI_EXCP;
    }
#else
    if (IS_IMM) {
        asi = dc->asi;
    }
    /* With v9, all asis below 0x80 are privileged.  */
    /* ??? We ought to check cpu_has_hypervisor, but we didn't copy
       down that bit into DisasContext.  For the moment that's ok,
       since the direct implementations below doesn't have any ASIs
       in the restricted [0x30, 0x7f] range, and the check will be
       done properly in the helper.  */
    if (!supervisor(dc) && asi < 0x80) {
        gen_exception(dc, TT_PRIV_ACT);
        type = GET_ASI_EXCP;
    } else {
        switch (asi) {
        case ASI_REAL:      /* Bypass */
        case ASI_REAL_IO:   /* Bypass, non-cacheable */
        case ASI_REAL_L:    /* Bypass LE */
        case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
        case ASI_TWINX_REAL:   /* Real address, twinx */
        case ASI_TWINX_REAL_L: /* Real address, twinx, LE */
        case ASI_QUAD_LDD_PHYS:
        case ASI_QUAD_LDD_PHYS_L:
            mem_idx = MMU_PHYS_IDX;
            break;
        case ASI_N:  /* Nucleus */
        case ASI_NL: /* Nucleus LE */
        case ASI_TWINX_N:
        case ASI_TWINX_NL:
        case ASI_NUCLEUS_QUAD_LDD:
        case ASI_NUCLEUS_QUAD_LDD_L:
            if (hypervisor(dc)) {
                mem_idx = MMU_PHYS_IDX;
            } else {
                mem_idx = MMU_NUCLEUS_IDX;
            }
            break;
        case ASI_AIUP:  /* As if user primary */
        case ASI_AIUPL: /* As if user primary LE */
        case ASI_TWINX_AIUP:
        case ASI_TWINX_AIUP_L:
        case ASI_BLK_AIUP_4V:
        case ASI_BLK_AIUP_L_4V:
        case ASI_BLK_AIUP:
        case ASI_BLK_AIUPL:
            mem_idx = MMU_USER_IDX;
            break;
        case ASI_AIUS:  /* As if user secondary */
        case ASI_AIUSL: /* As if user secondary LE */
        case ASI_TWINX_AIUS:
        case ASI_TWINX_AIUS_L:
        case ASI_BLK_AIUS_4V:
        case ASI_BLK_AIUS_L_4V:
        case ASI_BLK_AIUS:
        case ASI_BLK_AIUSL:
            mem_idx = MMU_USER_SECONDARY_IDX;
            break;
        case ASI_S:  /* Secondary */
        case ASI_SL: /* Secondary LE */
        case ASI_TWINX_S:
        case ASI_TWINX_SL:
        case ASI_BLK_COMMIT_S:
        case ASI_BLK_S:
        case ASI_BLK_SL:
        case ASI_FL8_S:
        case ASI_FL8_SL:
        case ASI_FL16_S:
        case ASI_FL16_SL:
            if (mem_idx == MMU_USER_IDX) {
                mem_idx = MMU_USER_SECONDARY_IDX;
            } else if (mem_idx == MMU_KERNEL_IDX) {
                mem_idx = MMU_KERNEL_SECONDARY_IDX;
            }
            break;
        case ASI_P:  /* Primary */
        case ASI_PL: /* Primary LE */
        case ASI_TWINX_P:
        case ASI_TWINX_PL:
        case ASI_BLK_COMMIT_P:
        case ASI_BLK_P:
        case ASI_BLK_PL:
        case ASI_FL8_P:
        case ASI_FL8_PL:
        case ASI_FL16_P:
        case ASI_FL16_PL:
            break;
        }
        switch (asi) {
        case ASI_REAL:
        case ASI_REAL_IO:
        case ASI_REAL_L:
        case ASI_REAL_IO_L:
        case ASI_N:
        case ASI_NL:
        case ASI_AIUP:
        case ASI_AIUPL:
        case ASI_AIUS:
        case ASI_AIUSL:
        case ASI_S:
        case ASI_SL:
        case ASI_P:
        case ASI_PL:
            type = GET_ASI_DIRECT;
            break;
        case ASI_TWINX_REAL:
        case ASI_TWINX_REAL_L:
        case ASI_TWINX_N:
        case ASI_TWINX_NL:
        case ASI_TWINX_AIUP:
        case ASI_TWINX_AIUP_L:
        case ASI_TWINX_AIUS:
        case ASI_TWINX_AIUS_L:
        case ASI_TWINX_P:
        case ASI_TWINX_PL:
        case ASI_TWINX_S:
        case ASI_TWINX_SL:
        case ASI_QUAD_LDD_PHYS:
        case ASI_QUAD_LDD_PHYS_L:
        case ASI_NUCLEUS_QUAD_LDD:
        case ASI_NUCLEUS_QUAD_LDD_L:
            type = GET_ASI_DTWINX;
            break;
        case ASI_BLK_COMMIT_P:
        case ASI_BLK_COMMIT_S:
        case ASI_BLK_AIUP_4V:
        case ASI_BLK_AIUP_L_4V:
        case ASI_BLK_AIUP:
        case ASI_BLK_AIUPL:
        case ASI_BLK_AIUS_4V:
        case ASI_BLK_AIUS_L_4V:
        case ASI_BLK_AIUS:
        case ASI_BLK_AIUSL:
        case ASI_BLK_S:
        case ASI_BLK_SL:
        case ASI_BLK_P:
        case ASI_BLK_PL:
            type = GET_ASI_BLOCK;
            break;
        case ASI_FL8_S:
        case ASI_FL8_SL:
        case ASI_FL8_P:
        case ASI_FL8_PL:
            memop = MO_UB;
            type = GET_ASI_SHORT;
            break;
        case ASI_FL16_S:
        case ASI_FL16_SL:
        case ASI_FL16_P:
        case ASI_FL16_PL:
            memop = MO_TEUW;
            type = GET_ASI_SHORT;
            break;
        }
        /* The little-endian asis all have bit 3 set.  */
        if (asi & 8) {
            memop ^= MO_BSWAP;
        }
    }
#endif

    return (DisasASI){ type, asi, mem_idx, memop };
}

static void gen_ld_asi(DisasContext *dc, TCGv dst, TCGv addr,
                       int insn, MemOp memop)
{
    DisasASI da = get_asi(dc, insn, memop);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DTWINX: /* Reserved for ldda.  */
        gen_exception(dc, TT_ILL_INSN);
        break;
    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_ld_tl(dst, addr, da.mem_idx, da.memop);
        break;
    default:
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(memop);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_ld_asi(dst, cpu_env, addr, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                gen_helper_ld_asi(t64, cpu_env, addr, r_asi, r_mop);
                tcg_gen_trunc_i64_tl(dst, t64);
                tcg_temp_free_i64(t64);
            }
#endif
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);
        }
        break;
    }
}

static void gen_st_asi(DisasContext *dc, TCGv src, TCGv addr,
                       int insn, MemOp memop)
{
    DisasASI da = get_asi(dc, insn, memop);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DTWINX: /* Reserved for stda.  */
#ifndef TARGET_SPARC64
        gen_exception(dc, TT_ILL_INSN);
        break;
#else
        if (!(dc->def->features & CPU_FEATURE_HYPV)) {
            /* Pre OpenSPARC CPUs don't have these */
            gen_exception(dc, TT_ILL_INSN);
            return;
        }
        /* in OpenSPARC T1+ CPUs TWINX ASIs in store instructions
         * are ST_BLKINIT_ ASIs */
#endif
        /* fall through */
    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_st_tl(src, addr, da.mem_idx, da.memop);
        break;
#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY)
    case GET_ASI_BCOPY:
        /* Copy 32 bytes from the address in SRC to ADDR.  */
        /* ??? The original qemu code suggests 4-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv saddr = tcg_temp_new();
            TCGv daddr = tcg_temp_new();
            TCGv four = tcg_const_tl(4);
            TCGv_i32 tmp = tcg_temp_new_i32();
            int i;

            tcg_gen_andi_tl(saddr, src, -4);
            tcg_gen_andi_tl(daddr, addr, -4);
            for (i = 0; i < 32; i += 4) {
                /* Since the loads and stores are paired, allow the
                   copy to happen in the host endianness.  */
                tcg_gen_qemu_ld_i32(tmp, saddr, da.mem_idx, MO_UL);
                tcg_gen_qemu_st_i32(tmp, daddr, da.mem_idx, MO_UL);
                tcg_gen_add_tl(saddr, saddr, four);
                tcg_gen_add_tl(daddr, daddr, four);
            }

            tcg_temp_free(saddr);
            tcg_temp_free(daddr);
            tcg_temp_free(four);
            tcg_temp_free_i32(tmp);
        }
        break;
#endif
    default:
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(memop & MO_SIZE);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_st_asi(cpu_env, addr, src, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                tcg_gen_extu_tl_i64(t64, src);
                gen_helper_st_asi(cpu_env, addr, t64, r_asi, r_mop);
                tcg_temp_free_i64(t64);
            }
#endif
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);

            /* A write to a TLB register may alter page maps.  End the TB. */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}

static void gen_swap_asi(DisasContext *dc, TCGv dst, TCGv src,
                         TCGv addr, int insn)
{
    DisasASI da = get_asi(dc, insn, MO_TEUL);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        gen_swap(dc, dst, src, addr, da.mem_idx, da.memop);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void gen_cas_asi(DisasContext *dc, TCGv addr, TCGv cmpv,
                        int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUL);
    TCGv oldv;

    switch (da.type) {
    case GET_ASI_EXCP:
        return;
    case GET_ASI_DIRECT:
        oldv = tcg_temp_new();
        tcg_gen_atomic_cmpxchg_tl(oldv, addr, cmpv, gen_load_gpr(dc, rd),
                                  da.mem_idx, da.memop);
        gen_store_gpr(dc, rd, oldv);
        tcg_temp_free(oldv);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void gen_ldstub_asi(DisasContext *dc, TCGv dst, TCGv addr, int insn)
{
    DisasASI da = get_asi(dc, insn, MO_UB);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        gen_ldstub(dc, dst, addr, da.mem_idx);
        break;
    default:
        /* ??? In theory, this should be raise DAE_invalid_asi.
           But the SS-20 roms do ldstuba [%l0] #ASI_M_CTL, %o1.  */
        if (tb_cflags(dc->base.tb) & CF_PARALLEL) {
            gen_helper_exit_atomic(cpu_env);
        } else {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(MO_UB);
            TCGv_i64 s64, t64;

            save_state(dc);
            t64 = tcg_temp_new_i64();
            gen_helper_ld_asi(t64, cpu_env, addr, r_asi, r_mop);

            s64 = tcg_const_i64(0xff);
            gen_helper_st_asi(cpu_env, addr, s64, r_asi, r_mop);
            tcg_temp_free_i64(s64);
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);

            tcg_gen_trunc_i64_tl(dst, t64);
            tcg_temp_free_i64(t64);

            /* End the TB.  */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}
#endif

#ifdef TARGET_SPARC64
static void gen_ldf_asi(DisasContext *dc, TCGv addr,
                        int insn, int size, int rd)
{
    DisasASI da = get_asi(dc, insn, (size == 4 ? MO_TEUL : MO_TEUQ));
    TCGv_i32 d32;
    TCGv_i64 d64;

    switch (da.type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        switch (size) {
        case 4:
            d32 = gen_dest_fpr_F(dc);
            tcg_gen_qemu_ld_i32(d32, addr, da.mem_idx, da.memop);
            gen_store_fpr_F(dc, rd, d32);
            break;
        case 8:
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            break;
        case 16:
            d64 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(d64, addr, da.mem_idx, da.memop | MO_ALIGN_4);
            tcg_gen_addi_tl(addr, addr, 8);
            tcg_gen_qemu_ld_i64(cpu_fpr[rd/2+1], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
            tcg_temp_free_i64(d64);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for lddfa on aligned registers only.  */
        if (size == 8 && (rd & 7) == 0) {
            MemOp memop;
            TCGv eight;
            int i;

            gen_address_mask(dc, addr);

            /* The first operation checks required alignment.  */
            memop = da.memop | MO_ALIGN_64;
            eight = tcg_const_tl(8);
            for (i = 0; ; ++i) {
                tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2 + i], addr,
                                    da.mem_idx, memop);
                if (i == 7) {
                    break;
                }
                tcg_gen_add_tl(addr, addr, eight);
                memop = da.memop;
            }
            tcg_temp_free(eight);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for lddfa only.  */
        if (size == 8) {
            gen_address_mask(dc, addr);
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da.mem_idx, da.memop);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    default:
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(da.memop);

            save_state(dc);
            /* According to the table in the UA2011 manual, the only
               other asis that are valid for ldfa/lddfa/ldqfa are
               the NO_FAULT asis.  We still need a helper for these,
               but we can just use the integer asi helper for them.  */
            switch (size) {
            case 4:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, cpu_env, addr, r_asi, r_mop);
                d32 = gen_dest_fpr_F(dc);
                tcg_gen_extrl_i64_i32(d32, d64);
                tcg_temp_free_i64(d64);
                gen_store_fpr_F(dc, rd, d32);
                break;
            case 8:
                gen_helper_ld_asi(cpu_fpr[rd / 2], cpu_env, addr, r_asi, r_mop);
                break;
            case 16:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, cpu_env, addr, r_asi, r_mop);
                tcg_gen_addi_tl(addr, addr, 8);
                gen_helper_ld_asi(cpu_fpr[rd/2+1], cpu_env, addr, r_asi, r_mop);
                tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
                tcg_temp_free_i64(d64);
                break;
            default:
                g_assert_not_reached();
            }
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);
        }
        break;
    }
}

static void gen_stf_asi(DisasContext *dc, TCGv addr,
                        int insn, int size, int rd)
{
    DisasASI da = get_asi(dc, insn, (size == 4 ? MO_TEUL : MO_TEUQ));
    TCGv_i32 d32;

    switch (da.type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        switch (size) {
        case 4:
            d32 = gen_load_fpr_F(dc, rd);
            tcg_gen_qemu_st_i32(d32, addr, da.mem_idx, da.memop);
            break;
        case 8:
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_4);
            break;
        case 16:
            /* Only 4-byte alignment required.  However, it is legal for the
               cpu to signal the alignment fault, and the OS trap handler is
               required to fix it up.  Requiring 16-byte alignment here avoids
               having to probe the second page before performing the first
               write.  */
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx,
                                da.memop | MO_ALIGN_16);
            tcg_gen_addi_tl(addr, addr, 8);
            tcg_gen_qemu_st_i64(cpu_fpr[rd/2+1], addr, da.mem_idx, da.memop);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for stdfa on aligned registers only.  */
        if (size == 8 && (rd & 7) == 0) {
            MemOp memop;
            TCGv eight;
            int i;

            gen_address_mask(dc, addr);

            /* The first operation checks required alignment.  */
            memop = da.memop | MO_ALIGN_64;
            eight = tcg_const_tl(8);
            for (i = 0; ; ++i) {
                tcg_gen_qemu_st_i64(cpu_fpr[rd / 2 + i], addr,
                                    da.mem_idx, memop);
                if (i == 7) {
                    break;
                }
                tcg_gen_add_tl(addr, addr, eight);
                memop = da.memop;
            }
            tcg_temp_free(eight);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for stdfa only.  */
        if (size == 8) {
            gen_address_mask(dc, addr);
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da.mem_idx, da.memop);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    default:
        /* According to the table in the UA2011 manual, the only
           other asis that are valid for ldfa/lddfa/ldqfa are
           the PST* asis, which aren't currently handled.  */
        gen_exception(dc, TT_ILL_INSN);
        break;
    }
}

static void gen_ldda_asi(DisasContext *dc, TCGv addr, int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);
    TCGv_i64 hi = gen_dest_gpr(dc, rd);
    TCGv_i64 lo = gen_dest_gpr(dc, rd + 1);

    switch (da.type) {
    case GET_ASI_EXCP:
        return;

    case GET_ASI_DTWINX:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_ld_i64(hi, addr, da.mem_idx, da.memop | MO_ALIGN_16);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_ld_i64(lo, addr, da.mem_idx, da.memop);
        break;

    case GET_ASI_DIRECT:
        {
            TCGv_i64 tmp = tcg_temp_new_i64();

            gen_address_mask(dc, addr);
            tcg_gen_qemu_ld_i64(tmp, addr, da.mem_idx, da.memop);

            /* Note that LE ldda acts as if each 32-bit register
               result is byte swapped.  Having just performed one
               64-bit bswap, we need now to swap the writebacks.  */
            if ((da.memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr32_i64(lo, hi, tmp);
            } else {
                tcg_gen_extr32_i64(hi, lo, tmp);
            }
            tcg_temp_free_i64(tmp);
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for ldda, and this should raise DAE_invalid_asi.  However,
           real hardware allows others.  This can be seen with e.g.
           FreeBSD 10.3 wrt ASI_IC_TAG.  */
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(da.memop);
            TCGv_i64 tmp = tcg_temp_new_i64();

            save_state(dc);
            gen_helper_ld_asi(tmp, cpu_env, addr, r_asi, r_mop);
            tcg_temp_free_i32(r_asi);
            tcg_temp_free_i32(r_mop);

            /* See above.  */
            if ((da.memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr32_i64(lo, hi, tmp);
            } else {
                tcg_gen_extr32_i64(hi, lo, tmp);
            }
            tcg_temp_free_i64(tmp);
        }
        break;
    }

    gen_store_gpr(dc, rd, hi);
    gen_store_gpr(dc, rd + 1, lo);
}

static void gen_stda_asi(DisasContext *dc, TCGv hi, TCGv addr,
                         int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);
    TCGv lo = gen_load_gpr(dc, rd + 1);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DTWINX:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_st_i64(hi, addr, da.mem_idx, da.memop | MO_ALIGN_16);
        tcg_gen_addi_tl(addr, addr, 8);
        tcg_gen_qemu_st_i64(lo, addr, da.mem_idx, da.memop);
        break;

    case GET_ASI_DIRECT:
        {
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* Note that LE stda acts as if each 32-bit register result is
               byte swapped.  We will perform one 64-bit LE store, so now
               we must swap the order of the construction.  */
            if ((da.memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat32_i64(t64, lo, hi);
            } else {
                tcg_gen_concat32_i64(t64, hi, lo);
            }
            gen_address_mask(dc, addr);
            tcg_gen_qemu_st_i64(t64, addr, da.mem_idx, da.memop);
            tcg_temp_free_i64(t64);
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for stda, and this should raise DAE_invalid_asi.  */
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(da.memop);
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* See above.  */
            if ((da.memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat32_i64(t64, lo, hi);
            } else {
                tcg_gen_concat32_i64(t64, hi, lo);
            }

            save_state(dc);
            gen_helper_st_asi(cpu_env, addr, t64, r_asi, r_mop);
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);
            tcg_temp_free_i64(t64);
        }
        break;
    }
}

static void gen_casx_asi(DisasContext *dc, TCGv addr, TCGv cmpv,
                         int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);
    TCGv oldv;

    switch (da.type) {
    case GET_ASI_EXCP:
        return;
    case GET_ASI_DIRECT:
        oldv = tcg_temp_new();
        tcg_gen_atomic_cmpxchg_tl(oldv, addr, cmpv, gen_load_gpr(dc, rd),
                                  da.mem_idx, da.memop);
        gen_store_gpr(dc, rd, oldv);
        tcg_temp_free(oldv);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

#elif !defined(CONFIG_USER_ONLY)
static void gen_ldda_asi(DisasContext *dc, TCGv addr, int insn, int rd)
{
    /* ??? Work around an apparent bug in Ubuntu gcc 4.8.2-10ubuntu2+12,
       whereby "rd + 1" elicits "error: array subscript is above array".
       Since we have already asserted that rd is even, the semantics
       are unchanged.  */
    TCGv lo = gen_dest_gpr(dc, rd | 1);
    TCGv hi = gen_dest_gpr(dc, rd);
    TCGv_i64 t64 = tcg_temp_new_i64();
    DisasASI da = get_asi(dc, insn, MO_TEUQ);

    switch (da.type) {
    case GET_ASI_EXCP:
        tcg_temp_free_i64(t64);
        return;
    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_ld_i64(t64, addr, da.mem_idx, da.memop);
        break;
    default:
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(MO_UQ);

            save_state(dc);
            gen_helper_ld_asi(t64, cpu_env, addr, r_asi, r_mop);
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);
        }
        break;
    }

    tcg_gen_extr_i64_i32(lo, hi, t64);
    tcg_temp_free_i64(t64);
    gen_store_gpr(dc, rd | 1, lo);
    gen_store_gpr(dc, rd, hi);
}

static void gen_stda_asi(DisasContext *dc, TCGv hi, TCGv addr,
                         int insn, int rd)
{
    DisasASI da = get_asi(dc, insn, MO_TEUQ);
    TCGv lo = gen_load_gpr(dc, rd + 1);
    TCGv_i64 t64 = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(t64, lo, hi);

    switch (da.type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        gen_address_mask(dc, addr);
        tcg_gen_qemu_st_i64(t64, addr, da.mem_idx, da.memop);
        break;
    case GET_ASI_BFILL:
        /* Store 32 bytes of T64 to ADDR.  */
        /* ??? The original qemu code suggests 8-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv d_addr = tcg_temp_new();
            TCGv eight = tcg_const_tl(8);
            int i;

            tcg_gen_andi_tl(d_addr, addr, -8);
            for (i = 0; i < 32; i += 8) {
                tcg_gen_qemu_st_i64(t64, d_addr, da.mem_idx, da.memop);
                tcg_gen_add_tl(d_addr, d_addr, eight);
            }

            tcg_temp_free(d_addr);
            tcg_temp_free(eight);
        }
        break;
    default:
        {
            TCGv_i32 r_asi = tcg_const_i32(da.asi);
            TCGv_i32 r_mop = tcg_const_i32(MO_UQ);

            save_state(dc);
            gen_helper_st_asi(cpu_env, addr, t64, r_asi, r_mop);
            tcg_temp_free_i32(r_mop);
            tcg_temp_free_i32(r_asi);
        }
        break;
    }

    tcg_temp_free_i64(t64);
}
#endif

static TCGv get_src1(DisasContext *dc, unsigned int insn)
{
    unsigned int rs1 = GET_FIELD(insn, 13, 17);
    return gen_load_gpr(dc, rs1);
}

static TCGv get_src2(DisasContext *dc, unsigned int insn)
{
    if (IS_IMM) { /* immediate */
        target_long simm = GET_FIELDs(insn, 19, 31);
        TCGv t = get_temp_tl(dc);
        tcg_gen_movi_tl(t, simm);
        return t;
    } else {      /* register */
        unsigned int rs2 = GET_FIELD(insn, 27, 31);
        return gen_load_gpr(dc, rs2);
    }
}

#ifdef TARGET_SPARC64
static void gen_fmovs(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    TCGv_i32 c32, zero, dst, s1, s2;

    /* We have two choices here: extend the 32 bit data and use movcond_i64,
       or fold the comparison down to 32 bits and use movcond_i32.  Choose
       the later.  */
    c32 = tcg_temp_new_i32();
    if (cmp->is_bool) {
        tcg_gen_extrl_i64_i32(c32, cmp->c1);
    } else {
        TCGv_i64 c64 = tcg_temp_new_i64();
        tcg_gen_setcond_i64(cmp->cond, c64, cmp->c1, cmp->c2);
        tcg_gen_extrl_i64_i32(c32, c64);
        tcg_temp_free_i64(c64);
    }

    s1 = gen_load_fpr_F(dc, rs);
    s2 = gen_load_fpr_F(dc, rd);
    dst = gen_dest_fpr_F(dc);
    zero = tcg_const_i32(0);

    tcg_gen_movcond_i32(TCG_COND_NE, dst, c32, zero, s1, s2);

    tcg_temp_free_i32(c32);
    tcg_temp_free_i32(zero);
    gen_store_fpr_F(dc, rd, dst);
}

static void gen_fmovd(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    TCGv_i64 dst = gen_dest_fpr_D(dc, rd);
    tcg_gen_movcond_i64(cmp->cond, dst, cmp->c1, cmp->c2,
                        gen_load_fpr_D(dc, rs),
                        gen_load_fpr_D(dc, rd));
    gen_store_fpr_D(dc, rd, dst);
}

static void gen_fmovq(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
    int qd = QFPREG(rd);
    int qs = QFPREG(rs);

    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2], cmp->c1, cmp->c2,
                        cpu_fpr[qs / 2], cpu_fpr[qd / 2]);
    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2 + 1], cmp->c1, cmp->c2,
                        cpu_fpr[qs / 2 + 1], cpu_fpr[qd / 2 + 1]);

    gen_update_fprs_dirty(dc, qd);
}

#ifndef CONFIG_USER_ONLY
static inline void gen_load_trap_state_at_tl(TCGv_ptr r_tsptr, TCGv_env cpu_env)
{
    TCGv_i32 r_tl = tcg_temp_new_i32();

    /* load env->tl into r_tl */
    tcg_gen_ld_i32(r_tl, cpu_env, offsetof(CPUSPARCState, tl));

    /* tl = [0 ... MAXTL_MASK] where MAXTL_MASK must be power of 2 */
    tcg_gen_andi_i32(r_tl, r_tl, MAXTL_MASK);

    /* calculate offset to current trap state from env->ts, reuse r_tl */
    tcg_gen_muli_i32(r_tl, r_tl, sizeof (trap_state));
    tcg_gen_addi_ptr(r_tsptr, cpu_env, offsetof(CPUSPARCState, ts));

    /* tsptr = env->ts[env->tl & MAXTL_MASK] */
    {
        TCGv_ptr r_tl_tmp = tcg_temp_new_ptr();
        tcg_gen_ext_i32_ptr(r_tl_tmp, r_tl);
        tcg_gen_add_ptr(r_tsptr, r_tsptr, r_tl_tmp);
        tcg_temp_free_ptr(r_tl_tmp);
    }

    tcg_temp_free_i32(r_tl);
}
#endif

static void gen_edge(DisasContext *dc, TCGv dst, TCGv s1, TCGv s2,
                     int width, bool cc, bool left)
{
    TCGv lo1, lo2, t1, t2;
    uint64_t amask, tabl, tabr;
    int shift, imask, omask;

    if (cc) {
        tcg_gen_mov_tl(cpu_cc_src, s1);
        tcg_gen_mov_tl(cpu_cc_src2, s2);
        tcg_gen_sub_tl(cpu_cc_dst, s1, s2);
        tcg_gen_movi_i32(cpu_cc_op, CC_OP_SUB);
        dc->cc_op = CC_OP_SUB;
    }

    /* Theory of operation: there are two tables, left and right (not to
       be confused with the left and right versions of the opcode).  These
       are indexed by the low 3 bits of the inputs.  To make things "easy",
       these tables are loaded into two constants, TABL and TABR below.
       The operation index = (input & imask) << shift calculates the index
       into the constant, while val = (table >> index) & omask calculates
       the value we're looking for.  */
    switch (width) {
    case 8:
        imask = 0x7;
        shift = 3;
        omask = 0xff;
        if (left) {
            tabl = 0x80c0e0f0f8fcfeffULL;
            tabr = 0xff7f3f1f0f070301ULL;
        } else {
            tabl = 0x0103070f1f3f7fffULL;
            tabr = 0xfffefcf8f0e0c080ULL;
        }
        break;
    case 16:
        imask = 0x6;
        shift = 1;
        omask = 0xf;
        if (left) {
            tabl = 0x8cef;
            tabr = 0xf731;
        } else {
            tabl = 0x137f;
            tabr = 0xfec8;
        }
        break;
    case 32:
        imask = 0x4;
        shift = 0;
        omask = 0x3;
        if (left) {
            tabl = (2 << 2) | 3;
            tabr = (3 << 2) | 1;
        } else {
            tabl = (1 << 2) | 3;
            tabr = (3 << 2) | 2;
        }
        break;
    default:
        abort();
    }

    lo1 = tcg_temp_new();
    lo2 = tcg_temp_new();
    tcg_gen_andi_tl(lo1, s1, imask);
    tcg_gen_andi_tl(lo2, s2, imask);
    tcg_gen_shli_tl(lo1, lo1, shift);
    tcg_gen_shli_tl(lo2, lo2, shift);

    t1 = tcg_const_tl(tabl);
    t2 = tcg_const_tl(tabr);
    tcg_gen_shr_tl(lo1, t1, lo1);
    tcg_gen_shr_tl(lo2, t2, lo2);
    tcg_gen_andi_tl(dst, lo1, omask);
    tcg_gen_andi_tl(lo2, lo2, omask);

    amask = -8;
    if (AM_CHECK(dc)) {
        amask &= 0xffffffffULL;
    }
    tcg_gen_andi_tl(s1, s1, amask);
    tcg_gen_andi_tl(s2, s2, amask);

    /* We want to compute
        dst = (s1 == s2 ? lo1 : lo1 & lo2).
       We've already done dst = lo1, so this reduces to
        dst &= (s1 == s2 ? -1 : lo2)
       Which we perform by
        lo2 |= -(s1 == s2)
        dst &= lo2
    */
    tcg_gen_setcond_tl(TCG_COND_EQ, t1, s1, s2);
    tcg_gen_neg_tl(t1, t1);
    tcg_gen_or_tl(lo2, lo2, t1);
    tcg_gen_and_tl(dst, dst, lo2);

    tcg_temp_free(lo1);
    tcg_temp_free(lo2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static void gen_alignaddr(TCGv dst, TCGv s1, TCGv s2, bool left)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_add_tl(tmp, s1, s2);
    tcg_gen_andi_tl(dst, tmp, -8);
    if (left) {
        tcg_gen_neg_tl(tmp, tmp);
    }
    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, tmp, 0, 3);

    tcg_temp_free(tmp);
}

static void gen_faligndata(TCGv dst, TCGv gsr, TCGv s1, TCGv s2)
{
    TCGv t1, t2, shift;

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    shift = tcg_temp_new();

    tcg_gen_andi_tl(shift, gsr, 7);
    tcg_gen_shli_tl(shift, shift, 3);
    tcg_gen_shl_tl(t1, s1, shift);

    /* A shift of 64 does not produce 0 in TCG.  Divide this into a
       shift of (up to 63) followed by a constant shift of 1.  */
    tcg_gen_xori_tl(shift, shift, 63);
    tcg_gen_shr_tl(t2, s2, shift);
    tcg_gen_shri_tl(t2, t2, 1);

    tcg_gen_or_tl(dst, t1, t2);

    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free(shift);
}
#endif

#define CHECK_IU_FEATURE(dc, FEATURE)                      \
    if (!((dc)->def->features & CPU_FEATURE_ ## FEATURE))  \
        goto illegal_insn;
#define CHECK_FPU_FEATURE(dc, FEATURE)                     \
    if (!((dc)->def->features & CPU_FEATURE_ ## FEATURE))  \
        goto nfpu_insn;

/* before an instruction, dc->pc must be static */
static void disas_sparc_insn(DisasContext * dc, unsigned int insn)
{
    unsigned int opc, rs1, rs2, rd;
    TCGv cpu_src1, cpu_src2;
    TCGv_i32 cpu_src1_32, cpu_src2_32, cpu_dst_32;
    TCGv_i64 cpu_src1_64, cpu_src2_64, cpu_dst_64;
    target_long simm;

    opc = GET_FIELD(insn, 0, 1);
    rd = GET_FIELD(insn, 2, 6);

    switch (opc) {
    case 0:                     /* branches/sethi */
        {
            unsigned int xop = GET_FIELD(insn, 7, 9);
            int32_t target;
            switch (xop) {
#ifdef TARGET_SPARC64
            case 0x1:           /* V9 BPcc */
                {
                    int cc;

                    target = GET_FIELD_SP(insn, 0, 18);
                    target = sign_extend(target, 19);
                    target <<= 2;
                    cc = GET_FIELD_SP(insn, 20, 21);
                    if (cc == 0)
                        do_branch(dc, target, insn, 0);
                    else if (cc == 2)
                        do_branch(dc, target, insn, 1);
                    else
                        goto illegal_insn;
                    goto jmp_insn;
                }
            case 0x3:           /* V9 BPr */
                {
                    target = GET_FIELD_SP(insn, 0, 13) |
                        (GET_FIELD_SP(insn, 20, 21) << 14);
                    target = sign_extend(target, 16);
                    target <<= 2;
                    cpu_src1 = get_src1(dc, insn);
                    do_branch_reg(dc, target, insn, cpu_src1);
                    goto jmp_insn;
                }
            case 0x5:           /* V9 FBPcc */
                {
                    int cc = GET_FIELD_SP(insn, 20, 21);
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    target = GET_FIELD_SP(insn, 0, 18);
                    target = sign_extend(target, 19);
                    target <<= 2;
                    do_fbranch(dc, target, insn, cc);
                    goto jmp_insn;
                }
#else
            case 0x7:           /* CBN+x */
                {
                    goto ncp_insn;
                }
#endif
            case 0x2:           /* BN+x */
                {
                    target = GET_FIELD(insn, 10, 31);
                    target = sign_extend(target, 22);
                    target <<= 2;
                    do_branch(dc, target, insn, 0);
                    goto jmp_insn;
                }
            case 0x6:           /* FBN+x */
                {
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    target = GET_FIELD(insn, 10, 31);
                    target = sign_extend(target, 22);
                    target <<= 2;
                    do_fbranch(dc, target, insn, 0);
                    goto jmp_insn;
                }
            case 0x4:           /* SETHI */
                /* Special-case %g0 because that's the canonical nop.  */
                if (rd) {
                    uint32_t value = GET_FIELD(insn, 10, 31);
                    TCGv t = gen_dest_gpr(dc, rd);
                    tcg_gen_movi_tl(t, value << 10);
                    gen_store_gpr(dc, rd, t);
                }
                break;
            case 0x0:           /* UNIMPL */
            default:
                goto illegal_insn;
            }
            break;
        }
        break;
    case 1:                     /*CALL*/
        {
            target_long target = GET_FIELDs(insn, 2, 31) << 2;
            TCGv o7 = gen_dest_gpr(dc, 15);

            tcg_gen_movi_tl(o7, dc->pc);
            gen_store_gpr(dc, 15, o7);
            target += dc->pc;
            gen_mov_pc_npc(dc);
#ifdef TARGET_SPARC64
            if (unlikely(AM_CHECK(dc))) {
                target &= 0xffffffffULL;
            }
#endif
            dc->npc = target;
        }
        goto jmp_insn;
    case 2:                     /* FPU & Logical Operations */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            TCGv cpu_dst = get_temp_tl(dc);
            TCGv cpu_tmp0;

            if (xop == 0x3a) {  /* generate trap */
                int cond = GET_FIELD(insn, 3, 6);
                TCGv_i32 trap;
                TCGLabel *l1 = NULL;
                int mask;

                if (cond == 0) {
                    /* Trap never.  */
                    break;
                }

                save_state(dc);

                if (cond != 8) {
                    /* Conditional trap.  */
                    DisasCompare cmp;
#ifdef TARGET_SPARC64
                    /* V9 icc/xcc */
                    int cc = GET_FIELD_SP(insn, 11, 12);
                    if (cc == 0) {
                        gen_compare(&cmp, 0, cond, dc);
                    } else if (cc == 2) {
                        gen_compare(&cmp, 1, cond, dc);
                    } else {
                        goto illegal_insn;
                    }
#else
                    gen_compare(&cmp, 0, cond, dc);
#endif
                    l1 = gen_new_label();
                    tcg_gen_brcond_tl(tcg_invert_cond(cmp.cond),
                                      cmp.c1, cmp.c2, l1);
                    free_compare(&cmp);
                }

                mask = ((dc->def->features & CPU_FEATURE_HYPV) && supervisor(dc)
                        ? UA2005_HTRAP_MASK : V8_TRAP_MASK);

                /* Don't use the normal temporaries, as they may well have
                   gone out of scope with the branch above.  While we're
                   doing that we might as well pre-truncate to 32-bit.  */
                trap = tcg_temp_new_i32();

                rs1 = GET_FIELD_SP(insn, 14, 18);
                if (IS_IMM) {
                    rs2 = GET_FIELD_SP(insn, 0, 7);
                    if (rs1 == 0) {
                        tcg_gen_movi_i32(trap, (rs2 & mask) + TT_TRAP);
                        /* Signal that the trap value is fully constant.  */
                        mask = 0;
                    } else {
                        TCGv t1 = gen_load_gpr(dc, rs1);
                        tcg_gen_trunc_tl_i32(trap, t1);
                        tcg_gen_addi_i32(trap, trap, rs2);
                    }
                } else {
                    TCGv t1, t2;
                    rs2 = GET_FIELD_SP(insn, 0, 4);
                    t1 = gen_load_gpr(dc, rs1);
                    t2 = gen_load_gpr(dc, rs2);
                    tcg_gen_add_tl(t1, t1, t2);
                    tcg_gen_trunc_tl_i32(trap, t1);
                }
                if (mask != 0) {
                    tcg_gen_andi_i32(trap, trap, mask);
                    tcg_gen_addi_i32(trap, trap, TT_TRAP);
                }

                gen_helper_raise_exception(cpu_env, trap);
                tcg_temp_free_i32(trap);

                if (cond == 8) {
                    /* An unconditional trap ends the TB.  */
                    dc->base.is_jmp = DISAS_NORETURN;
                    goto jmp_insn;
                } else {
                    /* A conditional trap falls through to the next insn.  */
                    gen_set_label(l1);
                    break;
                }
            } else if (xop == 0x28) {
                rs1 = GET_FIELD(insn, 13, 17);
                switch(rs1) {
                case 0: /* rdy */
#ifndef TARGET_SPARC64
                case 0x01 ... 0x0e: /* undefined in the SPARCv8
                                       manual, rdy on the microSPARC
                                       II */
                case 0x0f:          /* stbar in the SPARCv8 manual,
                                       rdy on the microSPARC II */
                case 0x10 ... 0x1f: /* implementation-dependent in the
                                       SPARCv8 manual, rdy on the
                                       microSPARC II */
                    /* Read Asr17 */
                    if (rs1 == 0x11 && dc->def->features & CPU_FEATURE_ASR17) {
                        TCGv t = gen_dest_gpr(dc, rd);
                        /* Read Asr17 for a Leon3 monoprocessor */
                        tcg_gen_movi_tl(t, (1 << 8) | (dc->def->nwindows - 1));
                        gen_store_gpr(dc, rd, t);
                        break;
                    }
#endif
                    gen_store_gpr(dc, rd, cpu_y);
                    break;
#ifdef TARGET_SPARC64
                case 0x2: /* V9 rdccr */
                    update_psr(dc);
                    gen_helper_rdccr(cpu_dst, cpu_env);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x3: /* V9 rdasi */
                    tcg_gen_movi_tl(cpu_dst, dc->asi);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x4: /* V9 rdtick */
                    {
                        TCGv_ptr r_tickptr;
                        TCGv_i32 r_const;

                        r_tickptr = tcg_temp_new_ptr();
                        r_const = tcg_const_i32(dc->mem_idx);
                        tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                       offsetof(CPUSPARCState, tick));
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            gen_io_start();
                        }
                        gen_helper_tick_get_count(cpu_dst, cpu_env, r_tickptr,
                                                  r_const);
                        tcg_temp_free_ptr(r_tickptr);
                        tcg_temp_free_i32(r_const);
                        gen_store_gpr(dc, rd, cpu_dst);
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            /* I/O operations in icount mode must end the TB */
                            dc->base.is_jmp = DISAS_EXIT;
                        }
                    }
                    break;
                case 0x5: /* V9 rdpc */
                    {
                        TCGv t = gen_dest_gpr(dc, rd);
                        if (unlikely(AM_CHECK(dc))) {
                            tcg_gen_movi_tl(t, dc->pc & 0xffffffffULL);
                        } else {
                            tcg_gen_movi_tl(t, dc->pc);
                        }
                        gen_store_gpr(dc, rd, t);
                    }
                    break;
                case 0x6: /* V9 rdfprs */
                    tcg_gen_ext_i32_tl(cpu_dst, cpu_fprs);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0xf: /* V9 membar */
                    break; /* no effect */
                case 0x13: /* Graphics Status */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_store_gpr(dc, rd, cpu_gsr);
                    break;
                case 0x16: /* Softint */
                    tcg_gen_ld32s_tl(cpu_dst, cpu_env,
                                     offsetof(CPUSPARCState, softint));
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x17: /* Tick compare */
                    gen_store_gpr(dc, rd, cpu_tick_cmpr);
                    break;
                case 0x18: /* System tick */
                    {
                        TCGv_ptr r_tickptr;
                        TCGv_i32 r_const;

                        r_tickptr = tcg_temp_new_ptr();
                        r_const = tcg_const_i32(dc->mem_idx);
                        tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                       offsetof(CPUSPARCState, stick));
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            gen_io_start();
                        }
                        gen_helper_tick_get_count(cpu_dst, cpu_env, r_tickptr,
                                                  r_const);
                        tcg_temp_free_ptr(r_tickptr);
                        tcg_temp_free_i32(r_const);
                        gen_store_gpr(dc, rd, cpu_dst);
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            /* I/O operations in icount mode must end the TB */
                            dc->base.is_jmp = DISAS_EXIT;
                        }
                    }
                    break;
                case 0x19: /* System tick compare */
                    gen_store_gpr(dc, rd, cpu_stick_cmpr);
                    break;
                case 0x1a: /* UltraSPARC-T1 Strand status */
                    /* XXX HYPV check maybe not enough, UA2005 & UA2007 describe
                     * this ASR as impl. dep
                     */
                    CHECK_IU_FEATURE(dc, HYPV);
                    {
                        TCGv t = gen_dest_gpr(dc, rd);
                        tcg_gen_movi_tl(t, 1UL);
                        gen_store_gpr(dc, rd, t);
                    }
                    break;
                case 0x10: /* Performance Control */
                case 0x11: /* Performance Instrumentation Counter */
                case 0x12: /* Dispatch Control */
                case 0x14: /* Softint set, WO */
                case 0x15: /* Softint clear, WO */
#endif
                default:
                    goto illegal_insn;
                }
#if !defined(CONFIG_USER_ONLY)
            } else if (xop == 0x29) { /* rdpsr / UA2005 rdhpr */
#ifndef TARGET_SPARC64
                if (!supervisor(dc)) {
                    goto priv_insn;
                }
                update_psr(dc);
                gen_helper_rdpsr(cpu_dst, cpu_env);
#else
                CHECK_IU_FEATURE(dc, HYPV);
                if (!hypervisor(dc))
                    goto priv_insn;
                rs1 = GET_FIELD(insn, 13, 17);
                switch (rs1) {
                case 0: // hpstate
                    tcg_gen_ld_i64(cpu_dst, cpu_env,
                                   offsetof(CPUSPARCState, hpstate));
                    break;
                case 1: // htstate
                    // gen_op_rdhtstate();
                    break;
                case 3: // hintp
                    tcg_gen_mov_tl(cpu_dst, cpu_hintp);
                    break;
                case 5: // htba
                    tcg_gen_mov_tl(cpu_dst, cpu_htba);
                    break;
                case 6: // hver
                    tcg_gen_mov_tl(cpu_dst, cpu_hver);
                    break;
                case 31: // hstick_cmpr
                    tcg_gen_mov_tl(cpu_dst, cpu_hstick_cmpr);
                    break;
                default:
                    goto illegal_insn;
                }
#endif
                gen_store_gpr(dc, rd, cpu_dst);
                break;
            } else if (xop == 0x2a) { /* rdwim / V9 rdpr */
                if (!supervisor(dc)) {
                    goto priv_insn;
                }
                cpu_tmp0 = get_temp_tl(dc);
#ifdef TARGET_SPARC64
                rs1 = GET_FIELD(insn, 13, 17);
                switch (rs1) {
                case 0: // tpc
                    {
                        TCGv_ptr r_tsptr;

                        r_tsptr = tcg_temp_new_ptr();
                        gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                        tcg_gen_ld_tl(cpu_tmp0, r_tsptr,
                                      offsetof(trap_state, tpc));
                        tcg_temp_free_ptr(r_tsptr);
                    }
                    break;
                case 1: // tnpc
                    {
                        TCGv_ptr r_tsptr;

                        r_tsptr = tcg_temp_new_ptr();
                        gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                        tcg_gen_ld_tl(cpu_tmp0, r_tsptr,
                                      offsetof(trap_state, tnpc));
                        tcg_temp_free_ptr(r_tsptr);
                    }
                    break;
                case 2: // tstate
                    {
                        TCGv_ptr r_tsptr;

                        r_tsptr = tcg_temp_new_ptr();
                        gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                        tcg_gen_ld_tl(cpu_tmp0, r_tsptr,
                                      offsetof(trap_state, tstate));
                        tcg_temp_free_ptr(r_tsptr);
                    }
                    break;
                case 3: // tt
                    {
                        TCGv_ptr r_tsptr = tcg_temp_new_ptr();

                        gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                        tcg_gen_ld32s_tl(cpu_tmp0, r_tsptr,
                                         offsetof(trap_state, tt));
                        tcg_temp_free_ptr(r_tsptr);
                    }
                    break;
                case 4: // tick
                    {
                        TCGv_ptr r_tickptr;
                        TCGv_i32 r_const;

                        r_tickptr = tcg_temp_new_ptr();
                        r_const = tcg_const_i32(dc->mem_idx);
                        tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                       offsetof(CPUSPARCState, tick));
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            gen_io_start();
                        }
                        gen_helper_tick_get_count(cpu_tmp0, cpu_env,
                                                  r_tickptr, r_const);
                        tcg_temp_free_ptr(r_tickptr);
                        tcg_temp_free_i32(r_const);
                        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                            /* I/O operations in icount mode must end the TB */
                            dc->base.is_jmp = DISAS_EXIT;
                        }
                    }
                    break;
                case 5: // tba
                    tcg_gen_mov_tl(cpu_tmp0, cpu_tbr);
                    break;
                case 6: // pstate
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, pstate));
                    break;
                case 7: // tl
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, tl));
                    break;
                case 8: // pil
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, psrpil));
                    break;
                case 9: // cwp
                    gen_helper_rdcwp(cpu_tmp0, cpu_env);
                    break;
                case 10: // cansave
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, cansave));
                    break;
                case 11: // canrestore
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, canrestore));
                    break;
                case 12: // cleanwin
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, cleanwin));
                    break;
                case 13: // otherwin
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, otherwin));
                    break;
                case 14: // wstate
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, wstate));
                    break;
                case 16: // UA2005 gl
                    CHECK_IU_FEATURE(dc, GL);
                    tcg_gen_ld32s_tl(cpu_tmp0, cpu_env,
                                     offsetof(CPUSPARCState, gl));
                    break;
                case 26: // UA2005 strand status
                    CHECK_IU_FEATURE(dc, HYPV);
                    if (!hypervisor(dc))
                        goto priv_insn;
                    tcg_gen_mov_tl(cpu_tmp0, cpu_ssr);
                    break;
                case 31: // ver
                    tcg_gen_mov_tl(cpu_tmp0, cpu_ver);
                    break;
                case 15: // fq
                default:
                    goto illegal_insn;
                }
#else
                tcg_gen_ext_i32_tl(cpu_tmp0, cpu_wim);
#endif
                gen_store_gpr(dc, rd, cpu_tmp0);
                break;
#endif
#if defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY)
            } else if (xop == 0x2b) { /* rdtbr / V9 flushw */
#ifdef TARGET_SPARC64
                gen_helper_flushw(cpu_env);
#else
                if (!supervisor(dc))
                    goto priv_insn;
                gen_store_gpr(dc, rd, cpu_tbr);
#endif
                break;
#endif
            } else if (xop == 0x34) {   /* FPU Operations */
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);

                switch (xop) {
                case 0x1: /* fmovs */
                    cpu_src1_32 = gen_load_fpr_F(dc, rs2);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x5: /* fnegs */
                    gen_ne_fop_FF(dc, rd, rs2, gen_helper_fnegs);
                    break;
                case 0x9: /* fabss */
                    gen_ne_fop_FF(dc, rd, rs2, gen_helper_fabss);
                    break;
                case 0x29: /* fsqrts */
                    CHECK_FPU_FEATURE(dc, FSQRT);
                    gen_fop_FF(dc, rd, rs2, gen_helper_fsqrts);
                    break;
                case 0x2a: /* fsqrtd */
                    CHECK_FPU_FEATURE(dc, FSQRT);
                    gen_fop_DD(dc, rd, rs2, gen_helper_fsqrtd);
                    break;
                case 0x2b: /* fsqrtq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQ(dc, rd, rs2, gen_helper_fsqrtq);
                    break;
                case 0x41: /* fadds */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fadds);
                    break;
                case 0x42: /* faddd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_faddd);
                    break;
                case 0x43: /* faddq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_faddq);
                    break;
                case 0x45: /* fsubs */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fsubs);
                    break;
                case 0x46: /* fsubd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fsubd);
                    break;
                case 0x47: /* fsubq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fsubq);
                    break;
                case 0x49: /* fmuls */
                    CHECK_FPU_FEATURE(dc, FMUL);
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fmuls);
                    break;
                case 0x4a: /* fmuld */
                    CHECK_FPU_FEATURE(dc, FMUL);
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld);
                    break;
                case 0x4b: /* fmulq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    CHECK_FPU_FEATURE(dc, FMUL);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fmulq);
                    break;
                case 0x4d: /* fdivs */
                    gen_fop_FFF(dc, rd, rs1, rs2, gen_helper_fdivs);
                    break;
                case 0x4e: /* fdivd */
                    gen_fop_DDD(dc, rd, rs1, rs2, gen_helper_fdivd);
                    break;
                case 0x4f: /* fdivq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QQQ(dc, rd, rs1, rs2, gen_helper_fdivq);
                    break;
                case 0x69: /* fsmuld */
                    CHECK_FPU_FEATURE(dc, FSMULD);
                    gen_fop_DFF(dc, rd, rs1, rs2, gen_helper_fsmuld);
                    break;
                case 0x6e: /* fdmulq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_QDD(dc, rd, rs1, rs2, gen_helper_fdmulq);
                    break;
                case 0xc4: /* fitos */
                    gen_fop_FF(dc, rd, rs2, gen_helper_fitos);
                    break;
                case 0xc6: /* fdtos */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fdtos);
                    break;
                case 0xc7: /* fqtos */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_FQ(dc, rd, rs2, gen_helper_fqtos);
                    break;
                case 0xc8: /* fitod */
                    gen_ne_fop_DF(dc, rd, rs2, gen_helper_fitod);
                    break;
                case 0xc9: /* fstod */
                    gen_ne_fop_DF(dc, rd, rs2, gen_helper_fstod);
                    break;
                case 0xcb: /* fqtod */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_DQ(dc, rd, rs2, gen_helper_fqtod);
                    break;
                case 0xcc: /* fitoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QF(dc, rd, rs2, gen_helper_fitoq);
                    break;
                case 0xcd: /* fstoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QF(dc, rd, rs2, gen_helper_fstoq);
                    break;
                case 0xce: /* fdtoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QD(dc, rd, rs2, gen_helper_fdtoq);
                    break;
                case 0xd1: /* fstoi */
                    gen_fop_FF(dc, rd, rs2, gen_helper_fstoi);
                    break;
                case 0xd2: /* fdtoi */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fdtoi);
                    break;
                case 0xd3: /* fqtoi */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_FQ(dc, rd, rs2, gen_helper_fqtoi);
                    break;
#ifdef TARGET_SPARC64
                case 0x2: /* V9 fmovd */
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x3: /* V9 fmovq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_move_Q(dc, rd, rs2);
                    break;
                case 0x6: /* V9 fnegd */
                    gen_ne_fop_DD(dc, rd, rs2, gen_helper_fnegd);
                    break;
                case 0x7: /* V9 fnegq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QQ(dc, rd, rs2, gen_helper_fnegq);
                    break;
                case 0xa: /* V9 fabsd */
                    gen_ne_fop_DD(dc, rd, rs2, gen_helper_fabsd);
                    break;
                case 0xb: /* V9 fabsq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QQ(dc, rd, rs2, gen_helper_fabsq);
                    break;
                case 0x81: /* V9 fstox */
                    gen_fop_DF(dc, rd, rs2, gen_helper_fstox);
                    break;
                case 0x82: /* V9 fdtox */
                    gen_fop_DD(dc, rd, rs2, gen_helper_fdtox);
                    break;
                case 0x83: /* V9 fqtox */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_fop_DQ(dc, rd, rs2, gen_helper_fqtox);
                    break;
                case 0x84: /* V9 fxtos */
                    gen_fop_FD(dc, rd, rs2, gen_helper_fxtos);
                    break;
                case 0x88: /* V9 fxtod */
                    gen_fop_DD(dc, rd, rs2, gen_helper_fxtod);
                    break;
                case 0x8c: /* V9 fxtoq */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_ne_fop_QD(dc, rd, rs2, gen_helper_fxtoq);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else if (xop == 0x35) {   /* FPU Operations */
#ifdef TARGET_SPARC64
                int cond;
#endif
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);

#ifdef TARGET_SPARC64
#define FMOVR(sz)                                                  \
                do {                                               \
                    DisasCompare cmp;                              \
                    cond = GET_FIELD_SP(insn, 10, 12);             \
                    cpu_src1 = get_src1(dc, insn);                 \
                    gen_compare_reg(&cmp, cond, cpu_src1);         \
                    gen_fmov##sz(dc, &cmp, rd, rs2);               \
                    free_compare(&cmp);                            \
                } while (0)

                if ((xop & 0x11f) == 0x005) { /* V9 fmovsr */
                    FMOVR(s);
                    break;
                } else if ((xop & 0x11f) == 0x006) { // V9 fmovdr
                    FMOVR(d);
                    break;
                } else if ((xop & 0x11f) == 0x007) { // V9 fmovqr
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    FMOVR(q);
                    break;
                }
#undef FMOVR
#endif
                switch (xop) {
#ifdef TARGET_SPARC64
#define FMOVCC(fcc, sz)                                                 \
                    do {                                                \
                        DisasCompare cmp;                               \
                        cond = GET_FIELD_SP(insn, 14, 17);              \
                        gen_fcompare(&cmp, fcc, cond);                  \
                        gen_fmov##sz(dc, &cmp, rd, rs2);                \
                        free_compare(&cmp);                             \
                    } while (0)

                    case 0x001: /* V9 fmovscc %fcc0 */
                        FMOVCC(0, s);
                        break;
                    case 0x002: /* V9 fmovdcc %fcc0 */
                        FMOVCC(0, d);
                        break;
                    case 0x003: /* V9 fmovqcc %fcc0 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(0, q);
                        break;
                    case 0x041: /* V9 fmovscc %fcc1 */
                        FMOVCC(1, s);
                        break;
                    case 0x042: /* V9 fmovdcc %fcc1 */
                        FMOVCC(1, d);
                        break;
                    case 0x043: /* V9 fmovqcc %fcc1 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(1, q);
                        break;
                    case 0x081: /* V9 fmovscc %fcc2 */
                        FMOVCC(2, s);
                        break;
                    case 0x082: /* V9 fmovdcc %fcc2 */
                        FMOVCC(2, d);
                        break;
                    case 0x083: /* V9 fmovqcc %fcc2 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(2, q);
                        break;
                    case 0x0c1: /* V9 fmovscc %fcc3 */
                        FMOVCC(3, s);
                        break;
                    case 0x0c2: /* V9 fmovdcc %fcc3 */
                        FMOVCC(3, d);
                        break;
                    case 0x0c3: /* V9 fmovqcc %fcc3 */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(3, q);
                        break;
#undef FMOVCC
#define FMOVCC(xcc, sz)                                                 \
                    do {                                                \
                        DisasCompare cmp;                               \
                        cond = GET_FIELD_SP(insn, 14, 17);              \
                        gen_compare(&cmp, xcc, cond, dc);               \
                        gen_fmov##sz(dc, &cmp, rd, rs2);                \
                        free_compare(&cmp);                             \
                    } while (0)

                    case 0x101: /* V9 fmovscc %icc */
                        FMOVCC(0, s);
                        break;
                    case 0x102: /* V9 fmovdcc %icc */
                        FMOVCC(0, d);
                        break;
                    case 0x103: /* V9 fmovqcc %icc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(0, q);
                        break;
                    case 0x181: /* V9 fmovscc %xcc */
                        FMOVCC(1, s);
                        break;
                    case 0x182: /* V9 fmovdcc %xcc */
                        FMOVCC(1, d);
                        break;
                    case 0x183: /* V9 fmovqcc %xcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        FMOVCC(1, q);
                        break;
#undef FMOVCC
#endif
                    case 0x51: /* fcmps, V9 %fcc */
                        cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                        cpu_src2_32 = gen_load_fpr_F(dc, rs2);
                        gen_op_fcmps(rd & 3, cpu_src1_32, cpu_src2_32);
                        break;
                    case 0x52: /* fcmpd, V9 %fcc */
                        cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                        cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                        gen_op_fcmpd(rd & 3, cpu_src1_64, cpu_src2_64);
                        break;
                    case 0x53: /* fcmpq, V9 %fcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fcmpq(rd & 3);
                        break;
                    case 0x55: /* fcmpes, V9 %fcc */
                        cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                        cpu_src2_32 = gen_load_fpr_F(dc, rs2);
                        gen_op_fcmpes(rd & 3, cpu_src1_32, cpu_src2_32);
                        break;
                    case 0x56: /* fcmped, V9 %fcc */
                        cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                        cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                        gen_op_fcmped(rd & 3, cpu_src1_64, cpu_src2_64);
                        break;
                    case 0x57: /* fcmpeq, V9 %fcc */
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fcmpeq(rd & 3);
                        break;
                    default:
                        goto illegal_insn;
                }
            } else if (xop == 0x2) {
                TCGv dst = gen_dest_gpr(dc, rd);
                rs1 = GET_FIELD(insn, 13, 17);
                if (rs1 == 0) {
                    /* clr/mov shortcut : or %g0, x, y -> mov x, y */
                    if (IS_IMM) {       /* immediate */
                        simm = GET_FIELDs(insn, 19, 31);
                        tcg_gen_movi_tl(dst, simm);
                        gen_store_gpr(dc, rd, dst);
                    } else {            /* register */
                        rs2 = GET_FIELD(insn, 27, 31);
                        if (rs2 == 0) {
                            tcg_gen_movi_tl(dst, 0);
                            gen_store_gpr(dc, rd, dst);
                        } else {
                            cpu_src2 = gen_load_gpr(dc, rs2);
                            gen_store_gpr(dc, rd, cpu_src2);
                        }
                    }
                } else {
                    cpu_src1 = get_src1(dc, insn);
                    if (IS_IMM) {       /* immediate */
                        simm = GET_FIELDs(insn, 19, 31);
                        tcg_gen_ori_tl(dst, cpu_src1, simm);
                        gen_store_gpr(dc, rd, dst);
                    } else {            /* register */
                        rs2 = GET_FIELD(insn, 27, 31);
                        if (rs2 == 0) {
                            /* mov shortcut:  or x, %g0, y -> mov x, y */
                            gen_store_gpr(dc, rd, cpu_src1);
                        } else {
                            cpu_src2 = gen_load_gpr(dc, rs2);
                            tcg_gen_or_tl(dst, cpu_src1, cpu_src2);
                            gen_store_gpr(dc, rd, dst);
                        }
                    }
                }
#ifdef TARGET_SPARC64
            } else if (xop == 0x25) { /* sll, V9 sllx */
                cpu_src1 = get_src1(dc, insn);
                if (IS_IMM) {   /* immediate */
                    simm = GET_FIELDs(insn, 20, 31);
                    if (insn & (1 << 12)) {
                        tcg_gen_shli_i64(cpu_dst, cpu_src1, simm & 0x3f);
                    } else {
                        tcg_gen_shli_i64(cpu_dst, cpu_src1, simm & 0x1f);
                    }
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    cpu_tmp0 = get_temp_tl(dc);
                    if (insn & (1 << 12)) {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x3f);
                    } else {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x1f);
                    }
                    tcg_gen_shl_i64(cpu_dst, cpu_src1, cpu_tmp0);
                }
                gen_store_gpr(dc, rd, cpu_dst);
            } else if (xop == 0x26) { /* srl, V9 srlx */
                cpu_src1 = get_src1(dc, insn);
                if (IS_IMM) {   /* immediate */
                    simm = GET_FIELDs(insn, 20, 31);
                    if (insn & (1 << 12)) {
                        tcg_gen_shri_i64(cpu_dst, cpu_src1, simm & 0x3f);
                    } else {
                        tcg_gen_andi_i64(cpu_dst, cpu_src1, 0xffffffffULL);
                        tcg_gen_shri_i64(cpu_dst, cpu_dst, simm & 0x1f);
                    }
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    cpu_tmp0 = get_temp_tl(dc);
                    if (insn & (1 << 12)) {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x3f);
                        tcg_gen_shr_i64(cpu_dst, cpu_src1, cpu_tmp0);
                    } else {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x1f);
                        tcg_gen_andi_i64(cpu_dst, cpu_src1, 0xffffffffULL);
                        tcg_gen_shr_i64(cpu_dst, cpu_dst, cpu_tmp0);
                    }
                }
                gen_store_gpr(dc, rd, cpu_dst);
            } else if (xop == 0x27) { /* sra, V9 srax */
                cpu_src1 = get_src1(dc, insn);
                if (IS_IMM) {   /* immediate */
                    simm = GET_FIELDs(insn, 20, 31);
                    if (insn & (1 << 12)) {
                        tcg_gen_sari_i64(cpu_dst, cpu_src1, simm & 0x3f);
                    } else {
                        tcg_gen_ext32s_i64(cpu_dst, cpu_src1);
                        tcg_gen_sari_i64(cpu_dst, cpu_dst, simm & 0x1f);
                    }
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    cpu_tmp0 = get_temp_tl(dc);
                    if (insn & (1 << 12)) {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x3f);
                        tcg_gen_sar_i64(cpu_dst, cpu_src1, cpu_tmp0);
                    } else {
                        tcg_gen_andi_i64(cpu_tmp0, cpu_src2, 0x1f);
                        tcg_gen_ext32s_i64(cpu_dst, cpu_src1);
                        tcg_gen_sar_i64(cpu_dst, cpu_dst, cpu_tmp0);
                    }
                }
                gen_store_gpr(dc, rd, cpu_dst);
#endif
            } else if (xop < 0x36) {
                if (xop < 0x20) {
                    cpu_src1 = get_src1(dc, insn);
                    cpu_src2 = get_src2(dc, insn);
                    switch (xop & ~0x10) {
                    case 0x0: /* add */
                        if (xop & 0x10) {
                            gen_op_add_cc(cpu_dst, cpu_src1, cpu_src2);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_ADD);
                            dc->cc_op = CC_OP_ADD;
                        } else {
                            tcg_gen_add_tl(cpu_dst, cpu_src1, cpu_src2);
                        }
                        break;
                    case 0x1: /* and */
                        tcg_gen_and_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x2: /* or */
                        tcg_gen_or_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x3: /* xor */
                        tcg_gen_xor_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x4: /* sub */
                        if (xop & 0x10) {
                            gen_op_sub_cc(cpu_dst, cpu_src1, cpu_src2);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_SUB);
                            dc->cc_op = CC_OP_SUB;
                        } else {
                            tcg_gen_sub_tl(cpu_dst, cpu_src1, cpu_src2);
                        }
                        break;
                    case 0x5: /* andn */
                        tcg_gen_andc_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x6: /* orn */
                        tcg_gen_orc_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x7: /* xorn */
                        tcg_gen_eqv_tl(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0x8: /* addx, V9 addc */
                        gen_op_addx_int(dc, cpu_dst, cpu_src1, cpu_src2,
                                        (xop & 0x10));
                        break;
#ifdef TARGET_SPARC64
                    case 0x9: /* V9 mulx */
                        tcg_gen_mul_i64(cpu_dst, cpu_src1, cpu_src2);
                        break;
#endif
                    case 0xa: /* umul */
                        CHECK_IU_FEATURE(dc, MUL);
                        gen_op_umul(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0xb: /* smul */
                        CHECK_IU_FEATURE(dc, MUL);
                        gen_op_smul(cpu_dst, cpu_src1, cpu_src2);
                        if (xop & 0x10) {
                            tcg_gen_mov_tl(cpu_cc_dst, cpu_dst);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_LOGIC);
                            dc->cc_op = CC_OP_LOGIC;
                        }
                        break;
                    case 0xc: /* subx, V9 subc */
                        gen_op_subx_int(dc, cpu_dst, cpu_src1, cpu_src2,
                                        (xop & 0x10));
                        break;
#ifdef TARGET_SPARC64
                    case 0xd: /* V9 udivx */
                        gen_helper_udivx(cpu_dst, cpu_env, cpu_src1, cpu_src2);
                        break;
#endif
                    case 0xe: /* udiv */
                        CHECK_IU_FEATURE(dc, DIV);
                        if (xop & 0x10) {
                            gen_helper_udiv_cc(cpu_dst, cpu_env, cpu_src1,
                                               cpu_src2);
                            dc->cc_op = CC_OP_DIV;
                        } else {
                            gen_helper_udiv(cpu_dst, cpu_env, cpu_src1,
                                            cpu_src2);
                        }
                        break;
                    case 0xf: /* sdiv */
                        CHECK_IU_FEATURE(dc, DIV);
                        if (xop & 0x10) {
                            gen_helper_sdiv_cc(cpu_dst, cpu_env, cpu_src1,
                                               cpu_src2);
                            dc->cc_op = CC_OP_DIV;
                        } else {
                            gen_helper_sdiv(cpu_dst, cpu_env, cpu_src1,
                                            cpu_src2);
                        }
                        break;
                    default:
                        goto illegal_insn;
                    }
                    gen_store_gpr(dc, rd, cpu_dst);
                } else {
                    cpu_src1 = get_src1(dc, insn);
                    cpu_src2 = get_src2(dc, insn);
                    switch (xop) {
                    case 0x20: /* taddcc */
                        gen_op_add_cc(cpu_dst, cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        tcg_gen_movi_i32(cpu_cc_op, CC_OP_TADD);
                        dc->cc_op = CC_OP_TADD;
                        break;
                    case 0x21: /* tsubcc */
                        gen_op_sub_cc(cpu_dst, cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        tcg_gen_movi_i32(cpu_cc_op, CC_OP_TSUB);
                        dc->cc_op = CC_OP_TSUB;
                        break;
                    case 0x22: /* taddcctv */
                        gen_helper_taddcctv(cpu_dst, cpu_env,
                                            cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        dc->cc_op = CC_OP_TADDTV;
                        break;
                    case 0x23: /* tsubcctv */
                        gen_helper_tsubcctv(cpu_dst, cpu_env,
                                            cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        dc->cc_op = CC_OP_TSUBTV;
                        break;
                    case 0x24: /* mulscc */
                        update_psr(dc);
                        gen_op_mulscc(cpu_dst, cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        tcg_gen_movi_i32(cpu_cc_op, CC_OP_ADD);
                        dc->cc_op = CC_OP_ADD;
                        break;
#ifndef TARGET_SPARC64
                    case 0x25:  /* sll */
                        if (IS_IMM) { /* immediate */
                            simm = GET_FIELDs(insn, 20, 31);
                            tcg_gen_shli_tl(cpu_dst, cpu_src1, simm & 0x1f);
                        } else { /* register */
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_andi_tl(cpu_tmp0, cpu_src2, 0x1f);
                            tcg_gen_shl_tl(cpu_dst, cpu_src1, cpu_tmp0);
                        }
                        gen_store_gpr(dc, rd, cpu_dst);
                        break;
                    case 0x26:  /* srl */
                        if (IS_IMM) { /* immediate */
                            simm = GET_FIELDs(insn, 20, 31);
                            tcg_gen_shri_tl(cpu_dst, cpu_src1, simm & 0x1f);
                        } else { /* register */
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_andi_tl(cpu_tmp0, cpu_src2, 0x1f);
                            tcg_gen_shr_tl(cpu_dst, cpu_src1, cpu_tmp0);
                        }
                        gen_store_gpr(dc, rd, cpu_dst);
                        break;
                    case 0x27:  /* sra */
                        if (IS_IMM) { /* immediate */
                            simm = GET_FIELDs(insn, 20, 31);
                            tcg_gen_sari_tl(cpu_dst, cpu_src1, simm & 0x1f);
                        } else { /* register */
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_andi_tl(cpu_tmp0, cpu_src2, 0x1f);
                            tcg_gen_sar_tl(cpu_dst, cpu_src1, cpu_tmp0);
                        }
                        gen_store_gpr(dc, rd, cpu_dst);
                        break;
#endif
                    case 0x30:
                        {
                            cpu_tmp0 = get_temp_tl(dc);
                            switch(rd) {
                            case 0: /* wry */
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                tcg_gen_andi_tl(cpu_y, cpu_tmp0, 0xffffffff);
                                break;
#ifndef TARGET_SPARC64
                            case 0x01 ... 0x0f: /* undefined in the
                                                   SPARCv8 manual, nop
                                                   on the microSPARC
                                                   II */
                            case 0x10 ... 0x1f: /* implementation-dependent
                                                   in the SPARCv8
                                                   manual, nop on the
                                                   microSPARC II */
                                if ((rd == 0x13) && (dc->def->features &
                                                     CPU_FEATURE_POWERDOWN)) {
                                    /* LEON3 power-down */
                                    save_state(dc);
                                    gen_helper_power_down(cpu_env);
                                }
                                break;
#else
                            case 0x2: /* V9 wrccr */
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                gen_helper_wrccr(cpu_env, cpu_tmp0);
                                tcg_gen_movi_i32(cpu_cc_op, CC_OP_FLAGS);
                                dc->cc_op = CC_OP_FLAGS;
                                break;
                            case 0x3: /* V9 wrasi */
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                tcg_gen_andi_tl(cpu_tmp0, cpu_tmp0, 0xff);
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState, asi));
                                /* End TB to notice changed ASI.  */
                                save_state(dc);
                                gen_op_next_insn();
                                tcg_gen_exit_tb(NULL, 0);
                                dc->base.is_jmp = DISAS_NORETURN;
                                break;
                            case 0x6: /* V9 wrfprs */
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                tcg_gen_trunc_tl_i32(cpu_fprs, cpu_tmp0);
                                dc->fprs_dirty = 0;
                                save_state(dc);
                                gen_op_next_insn();
                                tcg_gen_exit_tb(NULL, 0);
                                dc->base.is_jmp = DISAS_NORETURN;
                                break;
                            case 0xf: /* V9 sir, nop if user */
#if !defined(CONFIG_USER_ONLY)
                                if (supervisor(dc)) {
                                    ; // XXX
                                }
#endif
                                break;
                            case 0x13: /* Graphics Status */
                                if (gen_trap_ifnofpu(dc)) {
                                    goto jmp_insn;
                                }
                                tcg_gen_xor_tl(cpu_gsr, cpu_src1, cpu_src2);
                                break;
                            case 0x14: /* Softint set */
                                if (!supervisor(dc))
                                    goto illegal_insn;
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                gen_helper_set_softint(cpu_env, cpu_tmp0);
                                break;
                            case 0x15: /* Softint clear */
                                if (!supervisor(dc))
                                    goto illegal_insn;
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                gen_helper_clear_softint(cpu_env, cpu_tmp0);
                                break;
                            case 0x16: /* Softint write */
                                if (!supervisor(dc))
                                    goto illegal_insn;
                                tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                                gen_helper_write_softint(cpu_env, cpu_tmp0);
                                break;
                            case 0x17: /* Tick compare */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                {
                                    TCGv_ptr r_tickptr;

                                    tcg_gen_xor_tl(cpu_tick_cmpr, cpu_src1,
                                                   cpu_src2);
                                    r_tickptr = tcg_temp_new_ptr();
                                    tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                                   offsetof(CPUSPARCState, tick));
                                    if (tb_cflags(dc->base.tb) &
                                           CF_USE_ICOUNT) {
                                        gen_io_start();
                                    }
                                    gen_helper_tick_set_limit(r_tickptr,
                                                              cpu_tick_cmpr);
                                    tcg_temp_free_ptr(r_tickptr);
                                    /* End TB to handle timer interrupt */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;
                            case 0x18: /* System tick */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                {
                                    TCGv_ptr r_tickptr;

                                    tcg_gen_xor_tl(cpu_tmp0, cpu_src1,
                                                   cpu_src2);
                                    r_tickptr = tcg_temp_new_ptr();
                                    tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                                   offsetof(CPUSPARCState, stick));
                                    if (tb_cflags(dc->base.tb) &
                                           CF_USE_ICOUNT) {
                                        gen_io_start();
                                    }
                                    gen_helper_tick_set_count(r_tickptr,
                                                              cpu_tmp0);
                                    tcg_temp_free_ptr(r_tickptr);
                                    /* End TB to handle timer interrupt */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;
                            case 0x19: /* System tick compare */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                {
                                    TCGv_ptr r_tickptr;

                                    tcg_gen_xor_tl(cpu_stick_cmpr, cpu_src1,
                                                   cpu_src2);
                                    r_tickptr = tcg_temp_new_ptr();
                                    tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                                   offsetof(CPUSPARCState, stick));
                                    if (tb_cflags(dc->base.tb) &
                                           CF_USE_ICOUNT) {
                                        gen_io_start();
                                    }
                                    gen_helper_tick_set_limit(r_tickptr,
                                                              cpu_stick_cmpr);
                                    tcg_temp_free_ptr(r_tickptr);
                                    /* End TB to handle timer interrupt */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;

                            case 0x10: /* Performance Control */
                            case 0x11: /* Performance Instrumentation
                                          Counter */
                            case 0x12: /* Dispatch Control */
#endif
                            default:
                                goto illegal_insn;
                            }
                        }
                        break;
#if !defined(CONFIG_USER_ONLY)
                    case 0x31: /* wrpsr, V9 saved, restored */
                        {
                            if (!supervisor(dc))
                                goto priv_insn;
#ifdef TARGET_SPARC64
                            switch (rd) {
                            case 0:
                                gen_helper_saved(cpu_env);
                                break;
                            case 1:
                                gen_helper_restored(cpu_env);
                                break;
                            case 2: /* UA2005 allclean */
                            case 3: /* UA2005 otherw */
                            case 4: /* UA2005 normalw */
                            case 5: /* UA2005 invalw */
                                // XXX
                            default:
                                goto illegal_insn;
                            }
#else
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                            gen_helper_wrpsr(cpu_env, cpu_tmp0);
                            tcg_gen_movi_i32(cpu_cc_op, CC_OP_FLAGS);
                            dc->cc_op = CC_OP_FLAGS;
                            save_state(dc);
                            gen_op_next_insn();
                            tcg_gen_exit_tb(NULL, 0);
                            dc->base.is_jmp = DISAS_NORETURN;
#endif
                        }
                        break;
                    case 0x32: /* wrwim, V9 wrpr */
                        {
                            if (!supervisor(dc))
                                goto priv_insn;
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
#ifdef TARGET_SPARC64
                            switch (rd) {
                            case 0: // tpc
                                {
                                    TCGv_ptr r_tsptr;

                                    r_tsptr = tcg_temp_new_ptr();
                                    gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                                    tcg_gen_st_tl(cpu_tmp0, r_tsptr,
                                                  offsetof(trap_state, tpc));
                                    tcg_temp_free_ptr(r_tsptr);
                                }
                                break;
                            case 1: // tnpc
                                {
                                    TCGv_ptr r_tsptr;

                                    r_tsptr = tcg_temp_new_ptr();
                                    gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                                    tcg_gen_st_tl(cpu_tmp0, r_tsptr,
                                                  offsetof(trap_state, tnpc));
                                    tcg_temp_free_ptr(r_tsptr);
                                }
                                break;
                            case 2: // tstate
                                {
                                    TCGv_ptr r_tsptr;

                                    r_tsptr = tcg_temp_new_ptr();
                                    gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                                    tcg_gen_st_tl(cpu_tmp0, r_tsptr,
                                                  offsetof(trap_state,
                                                           tstate));
                                    tcg_temp_free_ptr(r_tsptr);
                                }
                                break;
                            case 3: // tt
                                {
                                    TCGv_ptr r_tsptr;

                                    r_tsptr = tcg_temp_new_ptr();
                                    gen_load_trap_state_at_tl(r_tsptr, cpu_env);
                                    tcg_gen_st32_tl(cpu_tmp0, r_tsptr,
                                                    offsetof(trap_state, tt));
                                    tcg_temp_free_ptr(r_tsptr);
                                }
                                break;
                            case 4: // tick
                                {
                                    TCGv_ptr r_tickptr;

                                    r_tickptr = tcg_temp_new_ptr();
                                    tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                                   offsetof(CPUSPARCState, tick));
                                    if (tb_cflags(dc->base.tb) &
                                           CF_USE_ICOUNT) {
                                        gen_io_start();
                                    }
                                    gen_helper_tick_set_count(r_tickptr,
                                                              cpu_tmp0);
                                    tcg_temp_free_ptr(r_tickptr);
                                    /* End TB to handle timer interrupt */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;
                            case 5: // tba
                                tcg_gen_mov_tl(cpu_tbr, cpu_tmp0);
                                break;
                            case 6: // pstate
                                save_state(dc);
                                if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                    gen_io_start();
                                }
                                gen_helper_wrpstate(cpu_env, cpu_tmp0);
                                if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                    /* I/O ops in icount mode must end the TB */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                dc->npc = DYNAMIC_PC;
                                break;
                            case 7: // tl
                                save_state(dc);
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                               offsetof(CPUSPARCState, tl));
                                dc->npc = DYNAMIC_PC;
                                break;
                            case 8: // pil
                                if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                    gen_io_start();
                                }
                                gen_helper_wrpil(cpu_env, cpu_tmp0);
                                if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                    /* I/O ops in icount mode must end the TB */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;
                            case 9: // cwp
                                gen_helper_wrcwp(cpu_env, cpu_tmp0);
                                break;
                            case 10: // cansave
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState,
                                                         cansave));
                                break;
                            case 11: // canrestore
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState,
                                                         canrestore));
                                break;
                            case 12: // cleanwin
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState,
                                                         cleanwin));
                                break;
                            case 13: // otherwin
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState,
                                                         otherwin));
                                break;
                            case 14: // wstate
                                tcg_gen_st32_tl(cpu_tmp0, cpu_env,
                                                offsetof(CPUSPARCState,
                                                         wstate));
                                break;
                            case 16: // UA2005 gl
                                CHECK_IU_FEATURE(dc, GL);
                                gen_helper_wrgl(cpu_env, cpu_tmp0);
                                break;
                            case 26: // UA2005 strand status
                                CHECK_IU_FEATURE(dc, HYPV);
                                if (!hypervisor(dc))
                                    goto priv_insn;
                                tcg_gen_mov_tl(cpu_ssr, cpu_tmp0);
                                break;
                            default:
                                goto illegal_insn;
                            }
#else
                            tcg_gen_trunc_tl_i32(cpu_wim, cpu_tmp0);
                            if (dc->def->nwindows != 32) {
                                tcg_gen_andi_tl(cpu_wim, cpu_wim,
                                                (1 << dc->def->nwindows) - 1);
                            }
#endif
                        }
                        break;
                    case 0x33: /* wrtbr, UA2005 wrhpr */
                        {
#ifndef TARGET_SPARC64
                            if (!supervisor(dc))
                                goto priv_insn;
                            tcg_gen_xor_tl(cpu_tbr, cpu_src1, cpu_src2);
#else
                            CHECK_IU_FEATURE(dc, HYPV);
                            if (!hypervisor(dc))
                                goto priv_insn;
                            cpu_tmp0 = get_temp_tl(dc);
                            tcg_gen_xor_tl(cpu_tmp0, cpu_src1, cpu_src2);
                            switch (rd) {
                            case 0: // hpstate
                                tcg_gen_st_i64(cpu_tmp0, cpu_env,
                                               offsetof(CPUSPARCState,
                                                        hpstate));
                                save_state(dc);
                                gen_op_next_insn();
                                tcg_gen_exit_tb(NULL, 0);
                                dc->base.is_jmp = DISAS_NORETURN;
                                break;
                            case 1: // htstate
                                // XXX gen_op_wrhtstate();
                                break;
                            case 3: // hintp
                                tcg_gen_mov_tl(cpu_hintp, cpu_tmp0);
                                break;
                            case 5: // htba
                                tcg_gen_mov_tl(cpu_htba, cpu_tmp0);
                                break;
                            case 31: // hstick_cmpr
                                {
                                    TCGv_ptr r_tickptr;

                                    tcg_gen_mov_tl(cpu_hstick_cmpr, cpu_tmp0);
                                    r_tickptr = tcg_temp_new_ptr();
                                    tcg_gen_ld_ptr(r_tickptr, cpu_env,
                                                   offsetof(CPUSPARCState, hstick));
                                    if (tb_cflags(dc->base.tb) &
                                           CF_USE_ICOUNT) {
                                        gen_io_start();
                                    }
                                    gen_helper_tick_set_limit(r_tickptr,
                                                              cpu_hstick_cmpr);
                                    tcg_temp_free_ptr(r_tickptr);
                                    /* End TB to handle timer interrupt */
                                    dc->base.is_jmp = DISAS_EXIT;
                                }
                                break;
                            case 6: // hver readonly
                            default:
                                goto illegal_insn;
                            }
#endif
                        }
                        break;
#endif
#ifdef TARGET_SPARC64
                    case 0x2c: /* V9 movcc */
                        {
                            int cc = GET_FIELD_SP(insn, 11, 12);
                            int cond = GET_FIELD_SP(insn, 14, 17);
                            DisasCompare cmp;
                            TCGv dst;

                            if (insn & (1 << 18)) {
                                if (cc == 0) {
                                    gen_compare(&cmp, 0, cond, dc);
                                } else if (cc == 2) {
                                    gen_compare(&cmp, 1, cond, dc);
                                } else {
                                    goto illegal_insn;
                                }
                            } else {
                                gen_fcompare(&cmp, cc, cond);
                            }

                            /* The get_src2 above loaded the normal 13-bit
                               immediate field, not the 11-bit field we have
                               in movcc.  But it did handle the reg case.  */
                            if (IS_IMM) {
                                simm = GET_FIELD_SPs(insn, 0, 10);
                                tcg_gen_movi_tl(cpu_src2, simm);
                            }

                            dst = gen_load_gpr(dc, rd);
                            tcg_gen_movcond_tl(cmp.cond, dst,
                                               cmp.c1, cmp.c2,
                                               cpu_src2, dst);
                            free_compare(&cmp);
                            gen_store_gpr(dc, rd, dst);
                            break;
                        }
                    case 0x2d: /* V9 sdivx */
                        gen_helper_sdivx(cpu_dst, cpu_env, cpu_src1, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        break;
                    case 0x2e: /* V9 popc */
                        tcg_gen_ctpop_tl(cpu_dst, cpu_src2);
                        gen_store_gpr(dc, rd, cpu_dst);
                        break;
                    case 0x2f: /* V9 movr */
                        {
                            int cond = GET_FIELD_SP(insn, 10, 12);
                            DisasCompare cmp;
                            TCGv dst;

                            gen_compare_reg(&cmp, cond, cpu_src1);

                            /* The get_src2 above loaded the normal 13-bit
                               immediate field, not the 10-bit field we have
                               in movr.  But it did handle the reg case.  */
                            if (IS_IMM) {
                                simm = GET_FIELD_SPs(insn, 0, 9);
                                tcg_gen_movi_tl(cpu_src2, simm);
                            }

                            dst = gen_load_gpr(dc, rd);
                            tcg_gen_movcond_tl(cmp.cond, dst,
                                               cmp.c1, cmp.c2,
                                               cpu_src2, dst);
                            free_compare(&cmp);
                            gen_store_gpr(dc, rd, dst);
                            break;
                        }
#endif
                    default:
                        goto illegal_insn;
                    }
                }
            } else if (xop == 0x36) { /* UltraSparc shutdown, VIS, V8 CPop1 */
#ifdef TARGET_SPARC64
                int opf = GET_FIELD_SP(insn, 5, 13);
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }

                switch (opf) {
                case 0x000: /* VIS I edge8cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x001: /* VIS II edge8n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x002: /* VIS I edge8lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x003: /* VIS II edge8ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 8, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x004: /* VIS I edge16cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x005: /* VIS II edge16n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x006: /* VIS I edge16lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x007: /* VIS II edge16ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 16, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x008: /* VIS I edge32cc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 1, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x009: /* VIS II edge32n */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 0, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x00a: /* VIS I edge32lcc */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 1, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x00b: /* VIS II edge32ln */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_edge(dc, cpu_dst, cpu_src1, cpu_src2, 32, 0, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x010: /* VIS I array8 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x012: /* VIS I array16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_shli_i64(cpu_dst, cpu_dst, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x014: /* VIS I array32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_helper_array8(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_shli_i64(cpu_dst, cpu_dst, 2);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x018: /* VIS I alignaddr */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_alignaddr(cpu_dst, cpu_src1, cpu_src2, 0);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x01a: /* VIS I alignaddrl */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_alignaddr(cpu_dst, cpu_src1, cpu_src2, 1);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x019: /* VIS II bmask */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    cpu_src1 = gen_load_gpr(dc, rs1);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    tcg_gen_add_tl(cpu_dst, cpu_src1, cpu_src2);
                    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, cpu_dst, 32, 32);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x020: /* VIS I fcmple16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmple16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x022: /* VIS I fcmpne16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpne16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x024: /* VIS I fcmple32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmple32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x026: /* VIS I fcmpne32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpne32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x028: /* VIS I fcmpgt16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpgt16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02a: /* VIS I fcmpeq16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpeq16(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02c: /* VIS I fcmpgt32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpgt32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x02e: /* VIS I fcmpeq32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    cpu_src2_64 = gen_load_fpr_D(dc, rs2);
                    gen_helper_fcmpeq32(cpu_dst, cpu_src1_64, cpu_src2_64);
                    gen_store_gpr(dc, rd, cpu_dst);
                    break;
                case 0x031: /* VIS I fmul8x16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16);
                    break;
                case 0x033: /* VIS I fmul8x16au */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16au);
                    break;
                case 0x035: /* VIS I fmul8x16al */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8x16al);
                    break;
                case 0x036: /* VIS I fmul8sux16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8sux16);
                    break;
                case 0x037: /* VIS I fmul8ulx16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmul8ulx16);
                    break;
                case 0x038: /* VIS I fmuld8sux16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld8sux16);
                    break;
                case 0x039: /* VIS I fmuld8ulx16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fmuld8ulx16);
                    break;
                case 0x03a: /* VIS I fpack32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpack32);
                    break;
                case 0x03b: /* VIS I fpack16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    gen_helper_fpack16(cpu_dst_32, cpu_gsr, cpu_src1_64);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x03d: /* VIS I fpackfix */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    gen_helper_fpackfix(cpu_dst_32, cpu_gsr, cpu_src1_64);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x03e: /* VIS I pdist */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDDD(dc, rd, rs1, rs2, gen_helper_pdist);
                    break;
                case 0x048: /* VIS I faligndata */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_faligndata);
                    break;
                case 0x04b: /* VIS I fpmerge */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpmerge);
                    break;
                case 0x04c: /* VIS II bshuffle */
                    CHECK_FPU_FEATURE(dc, VIS2);
                    gen_gsr_fop_DDD(dc, rd, rs1, rs2, gen_helper_bshuffle);
                    break;
                case 0x04d: /* VIS I fexpand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fexpand);
                    break;
                case 0x050: /* VIS I fpadd16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpadd16);
                    break;
                case 0x051: /* VIS I fpadd16s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, gen_helper_fpadd16s);
                    break;
                case 0x052: /* VIS I fpadd32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpadd32);
                    break;
                case 0x053: /* VIS I fpadd32s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_add_i32);
                    break;
                case 0x054: /* VIS I fpsub16 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpsub16);
                    break;
                case 0x055: /* VIS I fpsub16s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, gen_helper_fpsub16s);
                    break;
                case 0x056: /* VIS I fpsub32 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, gen_helper_fpsub32);
                    break;
                case 0x057: /* VIS I fpsub32s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_sub_i32);
                    break;
                case 0x060: /* VIS I fzero */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_movi_i64(cpu_dst_64, 0);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                case 0x061: /* VIS I fzeros */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_movi_i32(cpu_dst_32, 0);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x062: /* VIS I fnor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_nor_i64);
                    break;
                case 0x063: /* VIS I fnors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_nor_i32);
                    break;
                case 0x064: /* VIS I fandnot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_andc_i64);
                    break;
                case 0x065: /* VIS I fandnot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_andc_i32);
                    break;
                case 0x066: /* VIS I fnot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DD(dc, rd, rs2, tcg_gen_not_i64);
                    break;
                case 0x067: /* VIS I fnot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FF(dc, rd, rs2, tcg_gen_not_i32);
                    break;
                case 0x068: /* VIS I fandnot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs2, rs1, tcg_gen_andc_i64);
                    break;
                case 0x069: /* VIS I fandnot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs2, rs1, tcg_gen_andc_i32);
                    break;
                case 0x06a: /* VIS I fnot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DD(dc, rd, rs1, tcg_gen_not_i64);
                    break;
                case 0x06b: /* VIS I fnot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FF(dc, rd, rs1, tcg_gen_not_i32);
                    break;
                case 0x06c: /* VIS I fxor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_xor_i64);
                    break;
                case 0x06d: /* VIS I fxors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_xor_i32);
                    break;
                case 0x06e: /* VIS I fnand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_nand_i64);
                    break;
                case 0x06f: /* VIS I fnands */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_nand_i32);
                    break;
                case 0x070: /* VIS I fand */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_and_i64);
                    break;
                case 0x071: /* VIS I fands */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_and_i32);
                    break;
                case 0x072: /* VIS I fxnor */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_eqv_i64);
                    break;
                case 0x073: /* VIS I fxnors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_eqv_i32);
                    break;
                case 0x074: /* VIS I fsrc1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs1);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x075: /* VIS I fsrc1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_32 = gen_load_fpr_F(dc, rs1);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x076: /* VIS I fornot2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_orc_i64);
                    break;
                case 0x077: /* VIS I fornot2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_orc_i32);
                    break;
                case 0x078: /* VIS I fsrc2 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_64 = gen_load_fpr_D(dc, rs2);
                    gen_store_fpr_D(dc, rd, cpu_src1_64);
                    break;
                case 0x079: /* VIS I fsrc2s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_src1_32 = gen_load_fpr_F(dc, rs2);
                    gen_store_fpr_F(dc, rd, cpu_src1_32);
                    break;
                case 0x07a: /* VIS I fornot1 */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs2, rs1, tcg_gen_orc_i64);
                    break;
                case 0x07b: /* VIS I fornot1s */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs2, rs1, tcg_gen_orc_i32);
                    break;
                case 0x07c: /* VIS I for */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_DDD(dc, rd, rs1, rs2, tcg_gen_or_i64);
                    break;
                case 0x07d: /* VIS I fors */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    gen_ne_fop_FFF(dc, rd, rs1, rs2, tcg_gen_or_i32);
                    break;
                case 0x07e: /* VIS I fone */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_movi_i64(cpu_dst_64, -1);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                case 0x07f: /* VIS I fones */
                    CHECK_FPU_FEATURE(dc, VIS1);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_movi_i32(cpu_dst_32, -1);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x080: /* VIS I shutdown */
                case 0x081: /* VIS II siam */
                    // XXX
                    goto illegal_insn;
                default:
                    goto illegal_insn;
                }
#else
                goto ncp_insn;
#endif
            } else if (xop == 0x37) { /* V8 CPop2, V9 impdep2 */
#ifdef TARGET_SPARC64
                goto illegal_insn;
#else
                goto ncp_insn;
#endif
#ifdef TARGET_SPARC64
            } else if (xop == 0x39) { /* V9 return */
                save_state(dc);
                cpu_src1 = get_src1(dc, insn);
                cpu_tmp0 = get_temp_tl(dc);
                if (IS_IMM) {   /* immediate */
                    simm = GET_FIELDs(insn, 19, 31);
                    tcg_gen_addi_tl(cpu_tmp0, cpu_src1, simm);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    if (rs2) {
                        cpu_src2 = gen_load_gpr(dc, rs2);
                        tcg_gen_add_tl(cpu_tmp0, cpu_src1, cpu_src2);
                    } else {
                        tcg_gen_mov_tl(cpu_tmp0, cpu_src1);
                    }
                }
                gen_helper_restore(cpu_env);
                gen_mov_pc_npc(dc);
                gen_check_align(cpu_tmp0, 3);
                tcg_gen_mov_tl(cpu_npc, cpu_tmp0);
                dc->npc = DYNAMIC_PC;
                goto jmp_insn;
#endif
            } else {
                cpu_src1 = get_src1(dc, insn);
                cpu_tmp0 = get_temp_tl(dc);
                if (IS_IMM) {   /* immediate */
                    simm = GET_FIELDs(insn, 19, 31);
                    tcg_gen_addi_tl(cpu_tmp0, cpu_src1, simm);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    if (rs2) {
                        cpu_src2 = gen_load_gpr(dc, rs2);
                        tcg_gen_add_tl(cpu_tmp0, cpu_src1, cpu_src2);
                    } else {
                        tcg_gen_mov_tl(cpu_tmp0, cpu_src1);
                    }
                }
                switch (xop) {
                case 0x38:      /* jmpl */
                    {
                        TCGv t = gen_dest_gpr(dc, rd);
                        tcg_gen_movi_tl(t, dc->pc);
                        gen_store_gpr(dc, rd, t);

                        gen_mov_pc_npc(dc);
                        gen_check_align(cpu_tmp0, 3);
                        gen_address_mask(dc, cpu_tmp0);
                        tcg_gen_mov_tl(cpu_npc, cpu_tmp0);
                        dc->npc = DYNAMIC_PC;
                    }
                    goto jmp_insn;
#if !defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
                case 0x39:      /* rett, V9 return */
                    {
                        if (!supervisor(dc))
                            goto priv_insn;
                        gen_mov_pc_npc(dc);
                        gen_check_align(cpu_tmp0, 3);
                        tcg_gen_mov_tl(cpu_npc, cpu_tmp0);
                        dc->npc = DYNAMIC_PC;
                        gen_helper_rett(cpu_env);
                    }
                    goto jmp_insn;
#endif
                case 0x3b: /* flush */
                    if (!((dc)->def->features & CPU_FEATURE_FLUSH))
                        goto unimp_flush;
                    /* nop */
                    break;
                case 0x3c:      /* save */
                    gen_helper_save(cpu_env);
                    gen_store_gpr(dc, rd, cpu_tmp0);
                    break;
                case 0x3d:      /* restore */
                    gen_helper_restore(cpu_env);
                    gen_store_gpr(dc, rd, cpu_tmp0);
                    break;
#if !defined(CONFIG_USER_ONLY) && defined(TARGET_SPARC64)
                case 0x3e:      /* V9 done/retry */
                    {
                        switch (rd) {
                        case 0:
                            if (!supervisor(dc))
                                goto priv_insn;
                            dc->npc = DYNAMIC_PC;
                            dc->pc = DYNAMIC_PC;
                            if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                gen_io_start();
                            }
                            gen_helper_done(cpu_env);
                            goto jmp_insn;
                        case 1:
                            if (!supervisor(dc))
                                goto priv_insn;
                            dc->npc = DYNAMIC_PC;
                            dc->pc = DYNAMIC_PC;
                            if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
                                gen_io_start();
                            }
                            gen_helper_retry(cpu_env);
                            goto jmp_insn;
                        default:
                            goto illegal_insn;
                        }
                    }
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            }
            break;
        }
        break;
    case 3:                     /* load/store instructions */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            /* ??? gen_address_mask prevents us from using a source
               register directly.  Always generate a temporary.  */
            TCGv cpu_addr = get_temp_tl(dc);

            tcg_gen_mov_tl(cpu_addr, get_src1(dc, insn));
            if (xop == 0x3c || xop == 0x3e) {
                /* V9 casa/casxa : no offset */
            } else if (IS_IMM) {     /* immediate */
                simm = GET_FIELDs(insn, 19, 31);
                if (simm != 0) {
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, simm);
                }
            } else {            /* register */
                rs2 = GET_FIELD(insn, 27, 31);
                if (rs2 != 0) {
                    tcg_gen_add_tl(cpu_addr, cpu_addr, gen_load_gpr(dc, rs2));
                }
            }
            if (xop < 4 || (xop > 7 && xop < 0x14 && xop != 0x0e) ||
                (xop > 0x17 && xop <= 0x1d ) ||
                (xop > 0x2c && xop <= 0x33) || xop == 0x1f || xop == 0x3d) {
                TCGv cpu_val = gen_dest_gpr(dc, rd);

                switch (xop) {
                case 0x0:       /* ld, V9 lduw, load unsigned word */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld32u(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x1:       /* ldub, load unsigned byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld8u(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x2:       /* lduh, load unsigned halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld16u(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x3:       /* ldd, load double word */
                    if (rd & 1)
                        goto illegal_insn;
                    else {
                        TCGv_i64 t64;

                        gen_address_mask(dc, cpu_addr);
                        t64 = tcg_temp_new_i64();
                        tcg_gen_qemu_ld64(t64, cpu_addr, dc->mem_idx);
                        tcg_gen_trunc_i64_tl(cpu_val, t64);
                        tcg_gen_ext32u_tl(cpu_val, cpu_val);
                        gen_store_gpr(dc, rd + 1, cpu_val);
                        tcg_gen_shri_i64(t64, t64, 32);
                        tcg_gen_trunc_i64_tl(cpu_val, t64);
                        tcg_temp_free_i64(t64);
                        tcg_gen_ext32u_tl(cpu_val, cpu_val);
                    }
                    break;
                case 0x9:       /* ldsb, load signed byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld8s(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0xa:       /* ldsh, load signed halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld16s(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0xd:       /* ldstub */
                    gen_ldstub(dc, cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x0f:
                    /* swap, swap register with memory. Also atomically */
                    CHECK_IU_FEATURE(dc, SWAP);
                    cpu_src1 = gen_load_gpr(dc, rd);
                    gen_swap(dc, cpu_val, cpu_src1, cpu_addr,
                             dc->mem_idx, MO_TEUL);
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x10:      /* lda, V9 lduwa, load word alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUL);
                    break;
                case 0x11:      /* lduba, load unsigned byte alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_UB);
                    break;
                case 0x12:      /* lduha, load unsigned halfword alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUW);
                    break;
                case 0x13:      /* ldda, load double word alternate */
                    if (rd & 1) {
                        goto illegal_insn;
                    }
                    gen_ldda_asi(dc, cpu_addr, insn, rd);
                    goto skip_move;
                case 0x19:      /* ldsba, load signed byte alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_SB);
                    break;
                case 0x1a:      /* ldsha, load signed halfword alternate */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TESW);
                    break;
                case 0x1d:      /* ldstuba -- XXX: should be atomically */
                    gen_ldstub_asi(dc, cpu_val, cpu_addr, insn);
                    break;
                case 0x1f:      /* swapa, swap reg with alt. memory. Also
                                   atomically */
                    CHECK_IU_FEATURE(dc, SWAP);
                    cpu_src1 = gen_load_gpr(dc, rd);
                    gen_swap_asi(dc, cpu_val, cpu_src1, cpu_addr, insn);
                    break;

#ifndef TARGET_SPARC64
                case 0x30: /* ldc */
                case 0x31: /* ldcsr */
                case 0x33: /* lddc */
                    goto ncp_insn;
#endif
#endif
#ifdef TARGET_SPARC64
                case 0x08: /* V9 ldsw */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld32s(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x0b: /* V9 ldx */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_ld64(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x18: /* V9 ldswa */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TESL);
                    break;
                case 0x1b: /* V9 ldxa */
                    gen_ld_asi(dc, cpu_val, cpu_addr, insn, MO_TEUQ);
                    break;
                case 0x2d: /* V9 prefetch, no effect */
                    goto skip_move;
                case 0x30: /* V9 ldfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 4, rd);
                    gen_update_fprs_dirty(dc, rd);
                    goto skip_move;
                case 0x33: /* V9 lddfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 8, DFPREG(rd));
                    gen_update_fprs_dirty(dc, DFPREG(rd));
                    goto skip_move;
                case 0x3d: /* V9 prefetcha, no effect */
                    goto skip_move;
                case 0x32: /* V9 ldqfa */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_ldf_asi(dc, cpu_addr, insn, 16, QFPREG(rd));
                    gen_update_fprs_dirty(dc, QFPREG(rd));
                    goto skip_move;
#endif
                default:
                    goto illegal_insn;
                }
                gen_store_gpr(dc, rd, cpu_val);
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
            skip_move: ;
#endif
            } else if (xop >= 0x20 && xop < 0x24) {
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                switch (xop) {
                case 0x20:      /* ldf, load fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_dst_32 = gen_dest_fpr_F(dc);
                    tcg_gen_qemu_ld_i32(cpu_dst_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL);
                    gen_store_fpr_F(dc, rd, cpu_dst_32);
                    break;
                case 0x21:      /* ldfsr, V9 ldxfsr */
#ifdef TARGET_SPARC64
                    gen_address_mask(dc, cpu_addr);
                    if (rd == 1) {
                        TCGv_i64 t64 = tcg_temp_new_i64();
                        tcg_gen_qemu_ld_i64(t64, cpu_addr,
                                            dc->mem_idx, MO_TEUQ);
                        gen_helper_ldxfsr(cpu_fsr, cpu_env, cpu_fsr, t64);
                        tcg_temp_free_i64(t64);
                        break;
                    }
#endif
                    cpu_dst_32 = get_temp_i32(dc);
                    tcg_gen_qemu_ld_i32(cpu_dst_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL);
                    gen_helper_ldfsr(cpu_fsr, cpu_env, cpu_fsr, cpu_dst_32);
                    break;
                case 0x22:      /* ldqf, load quad fpreg */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_64 = tcg_temp_new_i64();
                    tcg_gen_qemu_ld_i64(cpu_src1_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, 8);
                    cpu_src2_64 = tcg_temp_new_i64();
                    tcg_gen_qemu_ld_i64(cpu_src2_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    gen_store_fpr_Q(dc, rd, cpu_src1_64, cpu_src2_64);
                    tcg_temp_free_i64(cpu_src1_64);
                    tcg_temp_free_i64(cpu_src2_64);
                    break;
                case 0x23:      /* lddf, load double fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_dst_64 = gen_dest_fpr_D(dc, rd);
                    tcg_gen_qemu_ld_i64(cpu_dst_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    gen_store_fpr_D(dc, rd, cpu_dst_64);
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop < 8 || (xop >= 0x14 && xop < 0x18) ||
                       xop == 0xe || xop == 0x1e) {
                TCGv cpu_val = gen_load_gpr(dc, rd);

                switch (xop) {
                case 0x4: /* st, store word */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st32(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x5: /* stb, store byte */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st8(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x6: /* sth, store halfword */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st16(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x7: /* std, store double word */
                    if (rd & 1)
                        goto illegal_insn;
                    else {
                        TCGv_i64 t64;
                        TCGv lo;

                        gen_address_mask(dc, cpu_addr);
                        lo = gen_load_gpr(dc, rd + 1);
                        t64 = tcg_temp_new_i64();
                        tcg_gen_concat_tl_i64(t64, lo, cpu_val);
                        tcg_gen_qemu_st64(t64, cpu_addr, dc->mem_idx);
                        tcg_temp_free_i64(t64);
                    }
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x14: /* sta, V9 stwa, store word alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUL);
                    break;
                case 0x15: /* stba, store byte alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_UB);
                    break;
                case 0x16: /* stha, store halfword alternate */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUW);
                    break;
                case 0x17: /* stda, store double word alternate */
                    if (rd & 1) {
                        goto illegal_insn;
                    }
                    gen_stda_asi(dc, cpu_val, cpu_addr, insn, rd);
                    break;
#endif
#ifdef TARGET_SPARC64
                case 0x0e: /* V9 stx */
                    gen_address_mask(dc, cpu_addr);
                    tcg_gen_qemu_st64(cpu_val, cpu_addr, dc->mem_idx);
                    break;
                case 0x1e: /* V9 stxa */
                    gen_st_asi(dc, cpu_val, cpu_addr, insn, MO_TEUQ);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x23 && xop < 0x28) {
                if (gen_trap_ifnofpu(dc)) {
                    goto jmp_insn;
                }
                switch (xop) {
                case 0x24: /* stf, store fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_32 = gen_load_fpr_F(dc, rd);
                    tcg_gen_qemu_st_i32(cpu_src1_32, cpu_addr,
                                        dc->mem_idx, MO_TEUL);
                    break;
                case 0x25: /* stfsr, V9 stxfsr */
                    {
#ifdef TARGET_SPARC64
                        gen_address_mask(dc, cpu_addr);
                        if (rd == 1) {
                            tcg_gen_qemu_st64(cpu_fsr, cpu_addr, dc->mem_idx);
                            break;
                        }
#endif
                        tcg_gen_qemu_st32(cpu_fsr, cpu_addr, dc->mem_idx);
                    }
                    break;
                case 0x26:
#ifdef TARGET_SPARC64
                    /* V9 stqf, store quad fpreg */
                    CHECK_FPU_FEATURE(dc, FLOAT128);
                    gen_address_mask(dc, cpu_addr);
                    /* ??? While stqf only requires 4-byte alignment, it is
                       legal for the cpu to signal the unaligned exception.
                       The OS trap handler is then required to fix it up.
                       For qemu, this avoids having to probe the second page
                       before performing the first write.  */
                    cpu_src1_64 = gen_load_fpr_Q0(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr,
                                        dc->mem_idx, MO_TEUQ | MO_ALIGN_16);
                    tcg_gen_addi_tl(cpu_addr, cpu_addr, 8);
                    cpu_src2_64 = gen_load_fpr_Q1(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr,
                                        dc->mem_idx, MO_TEUQ);
                    break;
#else /* !TARGET_SPARC64 */
                    /* stdfq, store floating point queue */
#if defined(CONFIG_USER_ONLY)
                    goto illegal_insn;
#else
                    if (!supervisor(dc))
                        goto priv_insn;
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    goto nfq_insn;
#endif
#endif
                case 0x27: /* stdf, store double fpreg */
                    gen_address_mask(dc, cpu_addr);
                    cpu_src1_64 = gen_load_fpr_D(dc, rd);
                    tcg_gen_qemu_st_i64(cpu_src1_64, cpu_addr, dc->mem_idx,
                                        MO_TEUQ | MO_ALIGN_4);
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x33 && xop < 0x3f) {
                switch (xop) {
#ifdef TARGET_SPARC64
                case 0x34: /* V9 stfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_stf_asi(dc, cpu_addr, insn, 4, rd);
                    break;
                case 0x36: /* V9 stqfa */
                    {
                        CHECK_FPU_FEATURE(dc, FLOAT128);
                        if (gen_trap_ifnofpu(dc)) {
                            goto jmp_insn;
                        }
                        gen_stf_asi(dc, cpu_addr, insn, 16, QFPREG(rd));
                    }
                    break;
                case 0x37: /* V9 stdfa */
                    if (gen_trap_ifnofpu(dc)) {
                        goto jmp_insn;
                    }
                    gen_stf_asi(dc, cpu_addr, insn, 8, DFPREG(rd));
                    break;
                case 0x3e: /* V9 casxa */
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_casx_asi(dc, cpu_addr, cpu_src2, insn, rd);
                    break;
#else
                case 0x34: /* stc */
                case 0x35: /* stcsr */
                case 0x36: /* stdcq */
                case 0x37: /* stdc */
                    goto ncp_insn;
#endif
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x3c: /* V9 or LEON3 casa */
#ifndef TARGET_SPARC64
                    CHECK_IU_FEATURE(dc, CASA);
#endif
                    rs2 = GET_FIELD(insn, 27, 31);
                    cpu_src2 = gen_load_gpr(dc, rs2);
                    gen_cas_asi(dc, cpu_addr, cpu_src2, insn, rd);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else {
                goto illegal_insn;
            }
        }
        break;
    }
    /* default case for non jump instructions */
    if (dc->npc == DYNAMIC_PC) {
        dc->pc = DYNAMIC_PC;
        gen_op_next_insn();
    } else if (dc->npc == JUMP_PC) {
        /* we can do a static jump */
        gen_branch2(dc, dc->jump_pc[0], dc->jump_pc[1], cpu_cond);
        dc->base.is_jmp = DISAS_NORETURN;
    } else {
        dc->pc = dc->npc;
        dc->npc = dc->npc + 4;
    }
 jmp_insn:
    goto egress;
 illegal_insn:
    gen_exception(dc, TT_ILL_INSN);
    goto egress;
 unimp_flush:
    gen_exception(dc, TT_UNIMP_FLUSH);
    goto egress;
#if !defined(CONFIG_USER_ONLY)
 priv_insn:
    gen_exception(dc, TT_PRIV_INSN);
    goto egress;
#endif
 nfpu_insn:
    gen_op_fpexception_im(dc, FSR_FTT_UNIMPFPOP);
    goto egress;
#if !defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
 nfq_insn:
    gen_op_fpexception_im(dc, FSR_FTT_SEQ_ERROR);
    goto egress;
#endif
#ifndef TARGET_SPARC64
 ncp_insn:
    gen_exception(dc, TT_NCP_INSN);
    goto egress;
#endif
 egress:
    if (dc->n_t32 != 0) {
        int i;
        for (i = dc->n_t32 - 1; i >= 0; --i) {
            tcg_temp_free_i32(dc->t32[i]);
        }
        dc->n_t32 = 0;
    }
    if (dc->n_ttl != 0) {
        int i;
        for (i = dc->n_ttl - 1; i >= 0; --i) {
            tcg_temp_free(dc->ttl[i]);
        }
        dc->n_ttl = 0;
    }
}

static void sparc_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cs->env_ptr;
    int bound;

    dc->pc = dc->base.pc_first;
    dc->npc = (target_ulong)dc->base.tb->cs_base;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->mem_idx = dc->base.tb->flags & TB_FLAG_MMU_MASK;
    dc->def = &env->def;
    dc->fpu_enabled = tb_fpu_enabled(dc->base.tb->flags);
    dc->address_mask_32bit = tb_am_enabled(dc->base.tb->flags);
#ifndef CONFIG_USER_ONLY
    dc->supervisor = (dc->base.tb->flags & TB_FLAG_SUPER) != 0;
#endif
#ifdef TARGET_SPARC64
    dc->fprs_dirty = 0;
    dc->asi = (dc->base.tb->flags >> TB_FLAG_ASI_SHIFT) & 0xff;
#ifndef CONFIG_USER_ONLY
    dc->hypervisor = (dc->base.tb->flags & TB_FLAG_HYPER) != 0;
#endif
#endif
    /*
     * if we reach a page boundary, we stop generation so that the
     * PC of a TT_TFAULT exception is always in the right page
     */
    bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;
    dc->base.max_insns = MIN(dc->base.max_insns, bound);
}

static void sparc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void sparc_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (dc->npc & JUMP_PC) {
        assert(dc->jump_pc[1] == dc->pc + 4);
        tcg_gen_insn_start(dc->pc, dc->jump_pc[0] | JUMP_PC);
    } else {
        tcg_gen_insn_start(dc->pc, dc->npc);
    }
}

static void sparc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cs->env_ptr;
    unsigned int insn;

    insn = translator_ldl(env, &dc->base, dc->pc);
    dc->base.pc_next += 4;
    disas_sparc_insn(dc, insn);

    if (dc->base.is_jmp == DISAS_NORETURN) {
        return;
    }
    if (dc->pc != dc->base.pc_next) {
        dc->base.is_jmp = DISAS_TOO_MANY;
    }
}

static void sparc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        if (dc->pc != DYNAMIC_PC &&
            (dc->npc != DYNAMIC_PC && dc->npc != JUMP_PC)) {
            /* static PC and NPC: we can use direct chaining */
            gen_goto_tb(dc, 0, dc->pc, dc->npc);
        } else {
            if (dc->pc != DYNAMIC_PC) {
                tcg_gen_movi_tl(cpu_pc, dc->pc);
            }
            save_npc(dc);
            tcg_gen_exit_tb(NULL, 0);
        }
        break;

    case DISAS_NORETURN:
       break;

    case DISAS_EXIT:
        /* Exit TB */
        save_state(dc);
        tcg_gen_exit_tb(NULL, 0);
        break;

    default:
        g_assert_not_reached();
    }
}

static void sparc_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps sparc_tr_ops = {
    .init_disas_context = sparc_tr_init_disas_context,
    .tb_start           = sparc_tr_tb_start,
    .insn_start         = sparc_tr_insn_start,
    .translate_insn     = sparc_tr_translate_insn,
    .tb_stop            = sparc_tr_tb_stop,
    .disas_log          = sparc_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc = {};

    translator_loop(cs, tb, max_insns, pc, host_pc, &sparc_tr_ops, &dc.base);
}

void sparc_tcg_init(void)
{
    static const char gregnames[32][4] = {
        "g0", "g1", "g2", "g3", "g4", "g5", "g6", "g7",
        "o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7",
        "l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7",
        "i0", "i1", "i2", "i3", "i4", "i5", "i6", "i7",
    };
    static const char fregnames[32][4] = {
        "f0", "f2", "f4", "f6", "f8", "f10", "f12", "f14",
        "f16", "f18", "f20", "f22", "f24", "f26", "f28", "f30",
        "f32", "f34", "f36", "f38", "f40", "f42", "f44", "f46",
        "f48", "f50", "f52", "f54", "f56", "f58", "f60", "f62",
    };

    static const struct { TCGv_i32 *ptr; int off; const char *name; } r32[] = {
#ifdef TARGET_SPARC64
        { &cpu_xcc, offsetof(CPUSPARCState, xcc), "xcc" },
        { &cpu_fprs, offsetof(CPUSPARCState, fprs), "fprs" },
#else
        { &cpu_wim, offsetof(CPUSPARCState, wim), "wim" },
#endif
        { &cpu_cc_op, offsetof(CPUSPARCState, cc_op), "cc_op" },
        { &cpu_psr, offsetof(CPUSPARCState, psr), "psr" },
    };

    static const struct { TCGv *ptr; int off; const char *name; } rtl[] = {
#ifdef TARGET_SPARC64
        { &cpu_gsr, offsetof(CPUSPARCState, gsr), "gsr" },
        { &cpu_tick_cmpr, offsetof(CPUSPARCState, tick_cmpr), "tick_cmpr" },
        { &cpu_stick_cmpr, offsetof(CPUSPARCState, stick_cmpr), "stick_cmpr" },
        { &cpu_hstick_cmpr, offsetof(CPUSPARCState, hstick_cmpr),
          "hstick_cmpr" },
        { &cpu_hintp, offsetof(CPUSPARCState, hintp), "hintp" },
        { &cpu_htba, offsetof(CPUSPARCState, htba), "htba" },
        { &cpu_hver, offsetof(CPUSPARCState, hver), "hver" },
        { &cpu_ssr, offsetof(CPUSPARCState, ssr), "ssr" },
        { &cpu_ver, offsetof(CPUSPARCState, version), "ver" },
#endif
        { &cpu_cond, offsetof(CPUSPARCState, cond), "cond" },
        { &cpu_cc_src, offsetof(CPUSPARCState, cc_src), "cc_src" },
        { &cpu_cc_src2, offsetof(CPUSPARCState, cc_src2), "cc_src2" },
        { &cpu_cc_dst, offsetof(CPUSPARCState, cc_dst), "cc_dst" },
        { &cpu_fsr, offsetof(CPUSPARCState, fsr), "fsr" },
        { &cpu_pc, offsetof(CPUSPARCState, pc), "pc" },
        { &cpu_npc, offsetof(CPUSPARCState, npc), "npc" },
        { &cpu_y, offsetof(CPUSPARCState, y), "y" },
#ifndef CONFIG_USER_ONLY
        { &cpu_tbr, offsetof(CPUSPARCState, tbr), "tbr" },
#endif
    };

    unsigned int i;

    cpu_regwptr = tcg_global_mem_new_ptr(cpu_env,
                                         offsetof(CPUSPARCState, regwptr),
                                         "regwptr");

    for (i = 0; i < ARRAY_SIZE(r32); ++i) {
        *r32[i].ptr = tcg_global_mem_new_i32(cpu_env, r32[i].off, r32[i].name);
    }

    for (i = 0; i < ARRAY_SIZE(rtl); ++i) {
        *rtl[i].ptr = tcg_global_mem_new(cpu_env, rtl[i].off, rtl[i].name);
    }

    cpu_regs[0] = NULL;
    for (i = 1; i < 8; ++i) {
        cpu_regs[i] = tcg_global_mem_new(cpu_env,
                                         offsetof(CPUSPARCState, gregs[i]),
                                         gregnames[i]);
    }

    for (i = 8; i < 32; ++i) {
        cpu_regs[i] = tcg_global_mem_new(cpu_regwptr,
                                         (i - 8) * sizeof(target_ulong),
                                         gregnames[i]);
    }

    for (i = 0; i < TARGET_DPREGS; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
                                            offsetof(CPUSPARCState, fpr[i]),
                                            fregnames[i]);
    }
}

void sparc_restore_state_to_opc(CPUState *cs,
                                const TranslationBlock *tb,
                                const uint64_t *data)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    target_ulong pc = data[0];
    target_ulong npc = data[1];

    env->pc = pc;
    if (npc == DYNAMIC_PC) {
        /* dynamic NPC: already stored */
    } else if (npc & JUMP_PC) {
        /* jump PC: use 'cond' and the jump targets of the translation */
        if (env->cond) {
            env->npc = npc & ~3;
        } else {
            env->npc = pc + 4;
        }
    } else {
        env->npc = npc;
    }
}
