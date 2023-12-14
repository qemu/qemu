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
#include "tcg/tcg-op-gvec.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/log.h"
#include "asi.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#ifdef TARGET_SPARC64
# define gen_helper_rdpsr(D, E)                 qemu_build_not_reached()
# define gen_helper_rett(E)                     qemu_build_not_reached()
# define gen_helper_power_down(E)               qemu_build_not_reached()
# define gen_helper_wrpsr(E, S)                 qemu_build_not_reached()
#else
# define gen_helper_clear_softint(E, S)         qemu_build_not_reached()
# define gen_helper_done(E)                     qemu_build_not_reached()
# define gen_helper_fabsd(D, S)                 qemu_build_not_reached()
# define gen_helper_flushw(E)                   qemu_build_not_reached()
# define gen_helper_fnegd(D, S)                 qemu_build_not_reached()
# define gen_helper_rdccr(D, E)                 qemu_build_not_reached()
# define gen_helper_rdcwp(D, E)                 qemu_build_not_reached()
# define gen_helper_restored(E)                 qemu_build_not_reached()
# define gen_helper_retry(E)                    qemu_build_not_reached()
# define gen_helper_saved(E)                    qemu_build_not_reached()
# define gen_helper_set_softint(E, S)           qemu_build_not_reached()
# define gen_helper_tick_get_count(D, E, T, C)  qemu_build_not_reached()
# define gen_helper_tick_set_count(P, S)        qemu_build_not_reached()
# define gen_helper_tick_set_limit(P, S)        qemu_build_not_reached()
# define gen_helper_wrccr(E, S)                 qemu_build_not_reached()
# define gen_helper_wrcwp(E, S)                 qemu_build_not_reached()
# define gen_helper_wrgl(E, S)                  qemu_build_not_reached()
# define gen_helper_write_softint(E, S)         qemu_build_not_reached()
# define gen_helper_wrpil(E, S)                 qemu_build_not_reached()
# define gen_helper_wrpstate(E, S)              qemu_build_not_reached()
# define gen_helper_fabsq                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpeq16             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpeq32             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpgt16             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpgt32             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmple16             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmple32             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpne16             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fcmpne32             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fdtox                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fexpand              ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmul8sux16           ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmul8ulx16           ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmul8x16al           ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmul8x16au           ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmul8x16             ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmuld8sux16          ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fmuld8ulx16          ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fnegq                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fpmerge              ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fqtox                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fstox                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fxtod                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fxtoq                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_fxtos                ({ qemu_build_not_reached(); NULL; })
# define gen_helper_pdist                ({ qemu_build_not_reached(); NULL; })
# define FSR_LDXFSR_MASK                        0
# define FSR_LDXFSR_OLDMASK                     0
# define MAXTL_MASK                             0
#endif

/* Dynamic PC, must exit to main loop. */
#define DYNAMIC_PC         1
/* Dynamic PC, one of two values according to jump_pc[T2]. */
#define JUMP_PC            2
/* Dynamic PC, may lookup next TB. */
#define DYNAMIC_PC_LOOKUP  3

#define DISAS_EXIT  DISAS_TARGET_0

/* global register indexes */
static TCGv_ptr cpu_regwptr;
static TCGv cpu_fsr, cpu_pc, cpu_npc;
static TCGv cpu_regs[32];
static TCGv cpu_y;
static TCGv cpu_tbr;
static TCGv cpu_cond;
static TCGv cpu_cc_N;
static TCGv cpu_cc_V;
static TCGv cpu_icc_Z;
static TCGv cpu_icc_C;
#ifdef TARGET_SPARC64
static TCGv cpu_xcc_Z;
static TCGv cpu_xcc_C;
static TCGv_i32 cpu_fprs;
static TCGv cpu_gsr;
#else
# define cpu_fprs               ({ qemu_build_not_reached(); (TCGv)NULL; })
# define cpu_gsr                ({ qemu_build_not_reached(); (TCGv)NULL; })
#endif

#ifdef TARGET_SPARC64
#define cpu_cc_Z  cpu_xcc_Z
#define cpu_cc_C  cpu_xcc_C
#else
#define cpu_cc_Z  cpu_icc_Z
#define cpu_cc_C  cpu_icc_C
#define cpu_xcc_Z ({ qemu_build_not_reached(); NULL; })
#define cpu_xcc_C ({ qemu_build_not_reached(); NULL; })
#endif

/* Floating point registers */
static TCGv_i64 cpu_fpr[TARGET_DPREGS];

#define env_field_offsetof(X)     offsetof(CPUSPARCState, X)
#ifdef TARGET_SPARC64
# define env32_field_offsetof(X)  ({ qemu_build_not_reached(); 0; })
# define env64_field_offsetof(X)  env_field_offsetof(X)
#else
# define env32_field_offsetof(X)  env_field_offsetof(X)
# define env64_field_offsetof(X)  ({ qemu_build_not_reached(); 0; })
#endif

typedef struct DisasCompare {
    TCGCond cond;
    TCGv c1;
    int c2;
} DisasCompare;

typedef struct DisasDelayException {
    struct DisasDelayException *next;
    TCGLabel *lab;
    TCGv_i32 excp;
    /* Saved state at parent insn. */
    target_ulong pc;
    target_ulong npc;
} DisasDelayException;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong pc;    /* current Program Counter: integer or DYNAMIC_PC */
    target_ulong npc;   /* next PC: integer or DYNAMIC_PC or JUMP_PC */

    /* Used when JUMP_PC value is used. */
    DisasCompare jump;
    target_ulong jump_pc[2];

    int mem_idx;
    bool cpu_cond_live;
    bool fpu_enabled;
    bool address_mask_32bit;
#ifndef CONFIG_USER_ONLY
    bool supervisor;
#ifdef TARGET_SPARC64
    bool hypervisor;
#endif
#endif

    sparc_def_t *def;
#ifdef TARGET_SPARC64
    int fprs_dirty;
    int asi;
#endif
    DisasDelayException *delay_excp_list;
} DisasContext;

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

#define IS_IMM (insn & (1<<13))

static void gen_update_fprs_dirty(DisasContext *dc, int rd)
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
    TCGv_i32 ret = tcg_temp_new_i32();
    if (src & 1) {
        tcg_gen_extrl_i64_i32(ret, cpu_fpr[src / 2]);
    } else {
        tcg_gen_extrh_i64_i32(ret, cpu_fpr[src / 2]);
    }
    return ret;
}

static void gen_store_fpr_F(DisasContext *dc, unsigned int dst, TCGv_i32 v)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(t, v);
    tcg_gen_deposit_i64(cpu_fpr[dst / 2], cpu_fpr[dst / 2], t,
                        (dst & 1 ? 0 : 32), 32);
    gen_update_fprs_dirty(dc, dst);
}

static TCGv_i32 gen_dest_fpr_F(DisasContext *dc)
{
    return tcg_temp_new_i32();
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
    tcg_gen_st_i64(cpu_fpr[src / 2], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_load_fpr_QT1(unsigned int src)
{
    tcg_gen_st_i64(cpu_fpr[src / 2], tcg_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_st_i64(cpu_fpr[src/2 + 1], tcg_env, offsetof(CPUSPARCState, qt1) +
                   offsetof(CPU_QuadU, ll.lower));
}

static void gen_op_store_QT0_fpr(unsigned int dst)
{
    tcg_gen_ld_i64(cpu_fpr[dst / 2], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.upper));
    tcg_gen_ld_i64(cpu_fpr[dst/2 + 1], tcg_env, offsetof(CPUSPARCState, qt0) +
                   offsetof(CPU_QuadU, ll.lower));
}

/* moves */
#ifdef CONFIG_USER_ONLY
#define supervisor(dc) 0
#define hypervisor(dc) 0
#else
#ifdef TARGET_SPARC64
#define hypervisor(dc) (dc->hypervisor)
#define supervisor(dc) (dc->supervisor | dc->hypervisor)
#else
#define supervisor(dc) (dc->supervisor)
#define hypervisor(dc) 0
#endif
#endif

#if !defined(TARGET_SPARC64)
# define AM_CHECK(dc)  false
#elif defined(TARGET_ABI32)
# define AM_CHECK(dc)  true
#elif defined(CONFIG_USER_ONLY)
# define AM_CHECK(dc)  false
#else
# define AM_CHECK(dc)  ((dc)->address_mask_32bit)
#endif

static void gen_address_mask(DisasContext *dc, TCGv addr)
{
    if (AM_CHECK(dc)) {
        tcg_gen_andi_tl(addr, addr, 0xffffffffULL);
    }
}

static target_ulong address_mask_i(DisasContext *dc, target_ulong addr)
{
    return AM_CHECK(dc) ? (uint32_t)addr : addr;
}

static TCGv gen_load_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        TCGv t = tcg_temp_new();
        tcg_gen_movi_tl(t, 0);
        return t;
    }
}

static void gen_store_gpr(DisasContext *dc, int reg, TCGv v)
{
    if (reg > 0) {
        assert(reg < 32);
        tcg_gen_mov_tl(cpu_regs[reg], v);
    }
}

static TCGv gen_dest_gpr(DisasContext *dc, int reg)
{
    if (reg > 0) {
        assert(reg < 32);
        return cpu_regs[reg];
    } else {
        return tcg_temp_new();
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
        /* jump to another page: we can use an indirect jump */
        tcg_gen_movi_tl(cpu_pc, pc);
        tcg_gen_movi_tl(cpu_npc, npc);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static TCGv gen_carry32(void)
{
    if (TARGET_LONG_BITS == 64) {
        TCGv t = tcg_temp_new();
        tcg_gen_extract_tl(t, cpu_icc_C, 32, 1);
        return t;
    }
    return cpu_icc_C;
}

static void gen_op_addcc_int(TCGv dst, TCGv src1, TCGv src2, TCGv cin)
{
    TCGv z = tcg_constant_tl(0);

    if (cin) {
        tcg_gen_add2_tl(cpu_cc_N, cpu_cc_C, src1, z, cin, z);
        tcg_gen_add2_tl(cpu_cc_N, cpu_cc_C, cpu_cc_N, cpu_cc_C, src2, z);
    } else {
        tcg_gen_add2_tl(cpu_cc_N, cpu_cc_C, src1, z, src2, z);
    }
    tcg_gen_xor_tl(cpu_cc_Z, src1, src2);
    tcg_gen_xor_tl(cpu_cc_V, cpu_cc_N, src2);
    tcg_gen_andc_tl(cpu_cc_V, cpu_cc_V, cpu_cc_Z);
    if (TARGET_LONG_BITS == 64) {
        /*
         * Carry-in to bit 32 is result ^ src1 ^ src2.
         * We already have the src xor term in Z, from computation of V.
         */
        tcg_gen_xor_tl(cpu_icc_C, cpu_cc_Z, cpu_cc_N);
        tcg_gen_mov_tl(cpu_icc_Z, cpu_cc_N);
    }
    tcg_gen_mov_tl(cpu_cc_Z, cpu_cc_N);
    tcg_gen_mov_tl(dst, cpu_cc_N);
}

static void gen_op_addcc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addcc_int(dst, src1, src2, NULL);
}

static void gen_op_taddcc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv t = tcg_temp_new();

    /* Save the tag bits around modification of dst. */
    tcg_gen_or_tl(t, src1, src2);

    gen_op_addcc(dst, src1, src2);

    /* Incorprate tag bits into icc.V */
    tcg_gen_andi_tl(t, t, 3);
    tcg_gen_neg_tl(t, t);
    tcg_gen_ext32u_tl(t, t);
    tcg_gen_or_tl(cpu_cc_V, cpu_cc_V, t);
}

static void gen_op_addc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_add_tl(dst, src1, src2);
    tcg_gen_add_tl(dst, dst, gen_carry32());
}

static void gen_op_addccc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_addcc_int(dst, src1, src2, gen_carry32());
}

static void gen_op_subcc_int(TCGv dst, TCGv src1, TCGv src2, TCGv cin)
{
    TCGv z = tcg_constant_tl(0);

    if (cin) {
        tcg_gen_sub2_tl(cpu_cc_N, cpu_cc_C, src1, z, cin, z);
        tcg_gen_sub2_tl(cpu_cc_N, cpu_cc_C, cpu_cc_N, cpu_cc_C, src2, z);
    } else {
        tcg_gen_sub2_tl(cpu_cc_N, cpu_cc_C, src1, z, src2, z);
    }
    tcg_gen_neg_tl(cpu_cc_C, cpu_cc_C);
    tcg_gen_xor_tl(cpu_cc_Z, src1, src2);
    tcg_gen_xor_tl(cpu_cc_V, cpu_cc_N, src1);
    tcg_gen_and_tl(cpu_cc_V, cpu_cc_V, cpu_cc_Z);
#ifdef TARGET_SPARC64
    tcg_gen_xor_tl(cpu_icc_C, cpu_cc_Z, cpu_cc_N);
    tcg_gen_mov_tl(cpu_icc_Z, cpu_cc_N);
#endif
    tcg_gen_mov_tl(cpu_cc_Z, cpu_cc_N);
    tcg_gen_mov_tl(dst, cpu_cc_N);
}

static void gen_op_subcc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subcc_int(dst, src1, src2, NULL);
}

static void gen_op_tsubcc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv t = tcg_temp_new();

    /* Save the tag bits around modification of dst. */
    tcg_gen_or_tl(t, src1, src2);

    gen_op_subcc(dst, src1, src2);

    /* Incorprate tag bits into icc.V */
    tcg_gen_andi_tl(t, t, 3);
    tcg_gen_neg_tl(t, t);
    tcg_gen_ext32u_tl(t, t);
    tcg_gen_or_tl(cpu_cc_V, cpu_cc_V, t);
}

static void gen_op_subc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_sub_tl(dst, src1, src2);
    tcg_gen_sub_tl(dst, dst, gen_carry32());
}

static void gen_op_subccc(TCGv dst, TCGv src1, TCGv src2)
{
    gen_op_subcc_int(dst, src1, src2, gen_carry32());
}

static void gen_op_mulscc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv zero = tcg_constant_tl(0);
    TCGv t_src1 = tcg_temp_new();
    TCGv t_src2 = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    tcg_gen_ext32u_tl(t_src1, src1);
    tcg_gen_ext32u_tl(t_src2, src2);

    /*
     * if (!(env->y & 1))
     *   src2 = 0;
     */
    tcg_gen_andi_tl(t0, cpu_y, 0x1);
    tcg_gen_movcond_tl(TCG_COND_EQ, t_src2, t0, zero, zero, t_src2);

    /*
     * b2 = src1 & 1;
     * y = (b2 << 31) | (y >> 1);
     */
    tcg_gen_extract_tl(t0, cpu_y, 1, 31);
    tcg_gen_deposit_tl(cpu_y, t0, src1, 31, 1);

    // b1 = N ^ V;
    tcg_gen_xor_tl(t0, cpu_cc_N, cpu_cc_V);

    /*
     * src1 = (b1 << 31) | (src1 >> 1)
     */
    tcg_gen_andi_tl(t0, t0, 1u << 31);
    tcg_gen_shri_tl(t_src1, t_src1, 1);
    tcg_gen_or_tl(t_src1, t_src1, t0);

    gen_op_addcc(dst, t_src1, t_src2);
}

static void gen_op_multiply(TCGv dst, TCGv src1, TCGv src2, int sign_ext)
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
    tcg_gen_shri_i64(cpu_y, dst, 32);
#endif
}

static void gen_op_umul(TCGv dst, TCGv src1, TCGv src2)
{
    /* zero-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 0);
}

static void gen_op_smul(TCGv dst, TCGv src1, TCGv src2)
{
    /* sign-extend truncated operands before multiplication */
    gen_op_multiply(dst, src1, src2, 1);
}

static void gen_op_sdiv(TCGv dst, TCGv src1, TCGv src2)
{
#ifdef TARGET_SPARC64
    gen_helper_sdiv(dst, tcg_env, src1, src2);
    tcg_gen_ext32s_tl(dst, dst);
#else
    TCGv_i64 t64 = tcg_temp_new_i64();
    gen_helper_sdiv(t64, tcg_env, src1, src2);
    tcg_gen_trunc_i64_tl(dst, t64);
#endif
}

static void gen_op_udivcc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv_i64 t64;

#ifdef TARGET_SPARC64
    t64 = cpu_cc_V;
#else
    t64 = tcg_temp_new_i64();
#endif

    gen_helper_udiv(t64, tcg_env, src1, src2);

#ifdef TARGET_SPARC64
    tcg_gen_ext32u_tl(cpu_cc_N, t64);
    tcg_gen_shri_tl(cpu_cc_V, t64, 32);
    tcg_gen_mov_tl(cpu_icc_Z, cpu_cc_N);
    tcg_gen_movi_tl(cpu_icc_C, 0);
#else
    tcg_gen_extr_i64_tl(cpu_cc_N, cpu_cc_V, t64);
#endif
    tcg_gen_mov_tl(cpu_cc_Z, cpu_cc_N);
    tcg_gen_movi_tl(cpu_cc_C, 0);
    tcg_gen_mov_tl(dst, cpu_cc_N);
}

static void gen_op_sdivcc(TCGv dst, TCGv src1, TCGv src2)
{
    TCGv_i64 t64;

#ifdef TARGET_SPARC64
    t64 = cpu_cc_V;
#else
    t64 = tcg_temp_new_i64();
#endif

    gen_helper_sdiv(t64, tcg_env, src1, src2);

#ifdef TARGET_SPARC64
    tcg_gen_ext32s_tl(cpu_cc_N, t64);
    tcg_gen_shri_tl(cpu_cc_V, t64, 32);
    tcg_gen_mov_tl(cpu_icc_Z, cpu_cc_N);
    tcg_gen_movi_tl(cpu_icc_C, 0);
#else
    tcg_gen_extr_i64_tl(cpu_cc_N, cpu_cc_V, t64);
#endif
    tcg_gen_mov_tl(cpu_cc_Z, cpu_cc_N);
    tcg_gen_movi_tl(cpu_cc_C, 0);
    tcg_gen_mov_tl(dst, cpu_cc_N);
}

static void gen_op_taddcctv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_taddcctv(dst, tcg_env, src1, src2);
}

static void gen_op_tsubcctv(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_tsubcctv(dst, tcg_env, src1, src2);
}

static void gen_op_popc(TCGv dst, TCGv src1, TCGv src2)
{
    tcg_gen_ctpop_tl(dst, src2);
}

#ifndef TARGET_SPARC64
static void gen_helper_array8(TCGv dst, TCGv src1, TCGv src2)
{
    g_assert_not_reached();
}
#endif

static void gen_op_array16(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_array8(dst, src1, src2);
    tcg_gen_shli_tl(dst, dst, 1);
}

static void gen_op_array32(TCGv dst, TCGv src1, TCGv src2)
{
    gen_helper_array8(dst, src1, src2);
    tcg_gen_shli_tl(dst, dst, 2);
}

static void gen_op_fpack16(TCGv_i32 dst, TCGv_i64 src)
{
#ifdef TARGET_SPARC64
    gen_helper_fpack16(dst, cpu_gsr, src);
#else
    g_assert_not_reached();
#endif
}

static void gen_op_fpackfix(TCGv_i32 dst, TCGv_i64 src)
{
#ifdef TARGET_SPARC64
    gen_helper_fpackfix(dst, cpu_gsr, src);
#else
    g_assert_not_reached();
#endif
}

static void gen_op_fpack32(TCGv_i64 dst, TCGv_i64 src1, TCGv_i64 src2)
{
#ifdef TARGET_SPARC64
    gen_helper_fpack32(dst, cpu_gsr, src1, src2);
#else
    g_assert_not_reached();
#endif
}

static void gen_op_faligndata(TCGv_i64 dst, TCGv_i64 s1, TCGv_i64 s2)
{
#ifdef TARGET_SPARC64
    TCGv t1, t2, shift;

    t1 = tcg_temp_new();
    t2 = tcg_temp_new();
    shift = tcg_temp_new();

    tcg_gen_andi_tl(shift, cpu_gsr, 7);
    tcg_gen_shli_tl(shift, shift, 3);
    tcg_gen_shl_tl(t1, s1, shift);

    /*
     * A shift of 64 does not produce 0 in TCG.  Divide this into a
     * shift of (up to 63) followed by a constant shift of 1.
     */
    tcg_gen_xori_tl(shift, shift, 63);
    tcg_gen_shr_tl(t2, s2, shift);
    tcg_gen_shri_tl(t2, t2, 1);

    tcg_gen_or_tl(dst, t1, t2);
#else
    g_assert_not_reached();
#endif
}

static void gen_op_bshuffle(TCGv_i64 dst, TCGv_i64 src1, TCGv_i64 src2)
{
#ifdef TARGET_SPARC64
    gen_helper_bshuffle(dst, cpu_gsr, src1, src2);
#else
    g_assert_not_reached();
#endif
}

// 1
static void gen_op_eval_ba(TCGv dst)
{
    tcg_gen_movi_tl(dst, 1);
}

// 0
static void gen_op_eval_bn(TCGv dst)
{
    tcg_gen_movi_tl(dst, 0);
}

/*
  FPSR bit field FCC1 | FCC0:
   0 =
   1 <
   2 >
   3 unordered
*/
static void gen_mov_reg_FCC0(TCGv reg, TCGv src,
                                    unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC0_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

static void gen_mov_reg_FCC1(TCGv reg, TCGv src, unsigned int fcc_offset)
{
    tcg_gen_shri_tl(reg, src, FSR_FCC1_SHIFT + fcc_offset);
    tcg_gen_andi_tl(reg, reg, 0x1);
}

// !0: FCC0 | FCC1
static void gen_op_eval_fbne(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
}

// 1 or 2: FCC0 ^ FCC1
static void gen_op_eval_fblg(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
}

// 1 or 3: FCC0
static void gen_op_eval_fbul(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
}

// 1: FCC0 & !FCC1
static void gen_op_eval_fbl(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
}

// 2 or 3: FCC1
static void gen_op_eval_fbug(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
}

// 2: !FCC0 & FCC1
static void gen_op_eval_fbg(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
}

// 3: FCC0 & FCC1
static void gen_op_eval_fbu(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
}

// 0: !(FCC0 | FCC1)
static void gen_op_eval_fbe(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 3: !(FCC0 ^ FCC1)
static void gen_op_eval_fbue(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_xor_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 2: !FCC0
static void gen_op_eval_fbge(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !1: !(FCC0 & !FCC1)
static void gen_op_eval_fbuge(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// 0 or 1: !FCC1
static void gen_op_eval_fble(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    gen_mov_reg_FCC1(dst, src, fcc_offset);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !2: !(!FCC0 & FCC1)
static void gen_op_eval_fbule(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_andc_tl(dst, t0, dst);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

// !3: !(FCC0 & FCC1)
static void gen_op_eval_fbo(TCGv dst, TCGv src, unsigned int fcc_offset)
{
    TCGv t0 = tcg_temp_new();
    gen_mov_reg_FCC0(dst, src, fcc_offset);
    gen_mov_reg_FCC1(t0, src, fcc_offset);
    tcg_gen_and_tl(dst, dst, t0);
    tcg_gen_xori_tl(dst, dst, 0x1);
}

static void finishing_insn(DisasContext *dc)
{
    /*
     * From here, there is no future path through an unwinding exception.
     * If the current insn cannot raise an exception, the computation of
     * cpu_cond may be able to be elided.
     */
    if (dc->cpu_cond_live) {
        tcg_gen_discard_tl(cpu_cond);
        dc->cpu_cond_live = false;
    }
}

static void gen_generic_branch(DisasContext *dc)
{
    TCGv npc0 = tcg_constant_tl(dc->jump_pc[0]);
    TCGv npc1 = tcg_constant_tl(dc->jump_pc[1]);
    TCGv c2 = tcg_constant_tl(dc->jump.c2);

    tcg_gen_movcond_tl(dc->jump.cond, cpu_npc, dc->jump.c1, c2, npc0, npc1);
}

/* call this function before using the condition register as it may
   have been set for a jump */
static void flush_cond(DisasContext *dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc);
        dc->npc = DYNAMIC_PC_LOOKUP;
    }
}

static void save_npc(DisasContext *dc)
{
    if (dc->npc & 3) {
        switch (dc->npc) {
        case JUMP_PC:
            gen_generic_branch(dc);
            dc->npc = DYNAMIC_PC_LOOKUP;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        tcg_gen_movi_tl(cpu_npc, dc->npc);
    }
}

static void save_state(DisasContext *dc)
{
    tcg_gen_movi_tl(cpu_pc, dc->pc);
    save_npc(dc);
}

static void gen_exception(DisasContext *dc, int which)
{
    finishing_insn(dc);
    save_state(dc);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(which));
    dc->base.is_jmp = DISAS_NORETURN;
}

static TCGLabel *delay_exceptionv(DisasContext *dc, TCGv_i32 excp)
{
    DisasDelayException *e = g_new0(DisasDelayException, 1);

    e->next = dc->delay_excp_list;
    dc->delay_excp_list = e;

    e->lab = gen_new_label();
    e->excp = excp;
    e->pc = dc->pc;
    /* Caller must have used flush_cond before branch. */
    assert(e->npc != JUMP_PC);
    e->npc = dc->npc;

    return e->lab;
}

static TCGLabel *delay_exception(DisasContext *dc, int excp)
{
    return delay_exceptionv(dc, tcg_constant_i32(excp));
}

static void gen_check_align(DisasContext *dc, TCGv addr, int mask)
{
    TCGv t = tcg_temp_new();
    TCGLabel *lab;

    tcg_gen_andi_tl(t, addr, mask);

    flush_cond(dc);
    lab = delay_exception(dc, TT_UNALIGNED);
    tcg_gen_brcondi_tl(TCG_COND_NE, t, 0, lab);
}

static void gen_mov_pc_npc(DisasContext *dc)
{
    finishing_insn(dc);

    if (dc->npc & 3) {
        switch (dc->npc) {
        case JUMP_PC:
            gen_generic_branch(dc);
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
            dc->pc = DYNAMIC_PC_LOOKUP;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
            dc->pc = dc->npc;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        dc->pc = dc->npc;
    }
}

static void gen_compare(DisasCompare *cmp, bool xcc, unsigned int cond,
                        DisasContext *dc)
{
    TCGv t1;

    cmp->c1 = t1 = tcg_temp_new();
    cmp->c2 = 0;

    switch (cond & 7) {
    case 0x0: /* never */
        cmp->cond = TCG_COND_NEVER;
        cmp->c1 = tcg_constant_tl(0);
        break;

    case 0x1: /* eq: Z */
        cmp->cond = TCG_COND_EQ;
        if (TARGET_LONG_BITS == 32 || xcc) {
            tcg_gen_mov_tl(t1, cpu_cc_Z);
        } else {
            tcg_gen_ext32u_tl(t1, cpu_icc_Z);
        }
        break;

    case 0x2: /* le: Z | (N ^ V) */
        /*
         * Simplify:
         *   cc_Z || (N ^ V) < 0        NE
         *   cc_Z && !((N ^ V) < 0)     EQ
         *   cc_Z & ~((N ^ V) >> TLB)   EQ
         */
        cmp->cond = TCG_COND_EQ;
        tcg_gen_xor_tl(t1, cpu_cc_N, cpu_cc_V);
        tcg_gen_sextract_tl(t1, t1, xcc ? 63 : 31, 1);
        tcg_gen_andc_tl(t1, xcc ? cpu_cc_Z : cpu_icc_Z, t1);
        if (TARGET_LONG_BITS == 64 && !xcc) {
            tcg_gen_ext32u_tl(t1, t1);
        }
        break;

    case 0x3: /* lt: N ^ V */
        cmp->cond = TCG_COND_LT;
        tcg_gen_xor_tl(t1, cpu_cc_N, cpu_cc_V);
        if (TARGET_LONG_BITS == 64 && !xcc) {
            tcg_gen_ext32s_tl(t1, t1);
        }
        break;

    case 0x4: /* leu: Z | C */
        /*
         * Simplify:
         *   cc_Z == 0 || cc_C != 0     NE
         *   cc_Z != 0 && cc_C == 0     EQ
         *   cc_Z & (cc_C ? 0 : -1)     EQ
         *   cc_Z & (cc_C - 1)          EQ
         */
        cmp->cond = TCG_COND_EQ;
        if (TARGET_LONG_BITS == 32 || xcc) {
            tcg_gen_subi_tl(t1, cpu_cc_C, 1);
            tcg_gen_and_tl(t1, t1, cpu_cc_Z);
        } else {
            tcg_gen_extract_tl(t1, cpu_icc_C, 32, 1);
            tcg_gen_subi_tl(t1, t1, 1);
            tcg_gen_and_tl(t1, t1, cpu_icc_Z);
            tcg_gen_ext32u_tl(t1, t1);
        }
        break;

    case 0x5: /* ltu: C */
        cmp->cond = TCG_COND_NE;
        if (TARGET_LONG_BITS == 32 || xcc) {
            tcg_gen_mov_tl(t1, cpu_cc_C);
        } else {
            tcg_gen_extract_tl(t1, cpu_icc_C, 32, 1);
        }
        break;

    case 0x6: /* neg: N */
        cmp->cond = TCG_COND_LT;
        if (TARGET_LONG_BITS == 32 || xcc) {
            tcg_gen_mov_tl(t1, cpu_cc_N);
        } else {
            tcg_gen_ext32s_tl(t1, cpu_cc_N);
        }
        break;

    case 0x7: /* vs: V */
        cmp->cond = TCG_COND_LT;
        if (TARGET_LONG_BITS == 32 || xcc) {
            tcg_gen_mov_tl(t1, cpu_cc_V);
        } else {
            tcg_gen_ext32s_tl(t1, cpu_cc_V);
        }
        break;
    }
    if (cond & 8) {
        cmp->cond = tcg_invert_cond(cmp->cond);
    }
}

static void gen_fcompare(DisasCompare *cmp, unsigned int cc, unsigned int cond)
{
    unsigned int offset;
    TCGv r_dst;

    /* For now we still generate a straight boolean result.  */
    cmp->cond = TCG_COND_NE;
    cmp->c1 = r_dst = tcg_temp_new();
    cmp->c2 = 0;

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

static bool gen_compare_reg(DisasCompare *cmp, int cond, TCGv r_src)
{
    static const TCGCond cond_reg[4] = {
        TCG_COND_NEVER,  /* reserved */
        TCG_COND_EQ,
        TCG_COND_LE,
        TCG_COND_LT,
    };
    TCGCond tcond;

    if ((cond & 3) == 0) {
        return false;
    }
    tcond = cond_reg[cond & 3];
    if (cond & 4) {
        tcond = tcg_invert_cond(tcond);
    }

    cmp->cond = tcond;
    cmp->c1 = tcg_temp_new();
    cmp->c2 = 0;
    tcg_gen_mov_tl(cmp->c1, r_src);
    return true;
}

static void gen_op_clear_ieee_excp_and_FTT(void)
{
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, FSR_FTT_CEXC_NMASK);
}

static void gen_op_fmovs(TCGv_i32 dst, TCGv_i32 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    tcg_gen_mov_i32(dst, src);
}

static void gen_op_fnegs(TCGv_i32 dst, TCGv_i32 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    gen_helper_fnegs(dst, src);
}

static void gen_op_fabss(TCGv_i32 dst, TCGv_i32 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    gen_helper_fabss(dst, src);
}

static void gen_op_fmovd(TCGv_i64 dst, TCGv_i64 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    tcg_gen_mov_i64(dst, src);
}

static void gen_op_fnegd(TCGv_i64 dst, TCGv_i64 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    gen_helper_fnegd(dst, src);
}

static void gen_op_fabsd(TCGv_i64 dst, TCGv_i64 src)
{
    gen_op_clear_ieee_excp_and_FTT();
    gen_helper_fabsd(dst, src);
}

#ifdef TARGET_SPARC64
static void gen_op_fcmps(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmps(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmps_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmps_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmps_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpd(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpd_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpd_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpd_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpq(cpu_fsr, tcg_env);
        break;
    case 1:
        gen_helper_fcmpq_fcc1(cpu_fsr, tcg_env);
        break;
    case 2:
        gen_helper_fcmpq_fcc2(cpu_fsr, tcg_env);
        break;
    case 3:
        gen_helper_fcmpq_fcc3(cpu_fsr, tcg_env);
        break;
    }
}

static void gen_op_fcmpes(int fccno, TCGv_i32 r_rs1, TCGv_i32 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpes(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmpes_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmpes_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmpes_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmped(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 1:
        gen_helper_fcmped_fcc1(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 2:
        gen_helper_fcmped_fcc2(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    case 3:
        gen_helper_fcmped_fcc3(cpu_fsr, tcg_env, r_rs1, r_rs2);
        break;
    }
}

static void gen_op_fcmpeq(int fccno)
{
    switch (fccno) {
    case 0:
        gen_helper_fcmpeq(cpu_fsr, tcg_env);
        break;
    case 1:
        gen_helper_fcmpeq_fcc1(cpu_fsr, tcg_env);
        break;
    case 2:
        gen_helper_fcmpeq_fcc2(cpu_fsr, tcg_env);
        break;
    case 3:
        gen_helper_fcmpeq_fcc3(cpu_fsr, tcg_env);
        break;
    }
}

#else

static void gen_op_fcmps(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmps(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpd(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmpd(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpq(int fccno)
{
    gen_helper_fcmpq(cpu_fsr, tcg_env);
}

static void gen_op_fcmpes(int fccno, TCGv r_rs1, TCGv r_rs2)
{
    gen_helper_fcmpes(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmped(int fccno, TCGv_i64 r_rs1, TCGv_i64 r_rs2)
{
    gen_helper_fcmped(cpu_fsr, tcg_env, r_rs1, r_rs2);
}

static void gen_op_fcmpeq(int fccno)
{
    gen_helper_fcmpeq(cpu_fsr, tcg_env);
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

/* asi moves */
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

/*
 * Build DisasASI.
 * For asi == -1, treat as non-asi.
 * For ask == -2, treat as immediate offset (v8 error, v9 %asi).
 */
static DisasASI resolve_asi(DisasContext *dc, int asi, MemOp memop)
{
    ASIType type = GET_ASI_HELPER;
    int mem_idx = dc->mem_idx;

    if (asi == -1) {
        /* Artificial "non-asi" case. */
        type = GET_ASI_DIRECT;
        goto done;
    }

#ifndef TARGET_SPARC64
    /* Before v9, all asis are immediate and privileged.  */
    if (asi < 0) {
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
    if (asi < 0) {
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

 done:
    return (DisasASI){ type, asi, mem_idx, memop };
}

#if defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
static void gen_helper_ld_asi(TCGv_i64 r, TCGv_env e, TCGv a,
                              TCGv_i32 asi, TCGv_i32 mop)
{
    g_assert_not_reached();
}

static void gen_helper_st_asi(TCGv_env e, TCGv a, TCGv_i64 r,
                              TCGv_i32 asi, TCGv_i32 mop)
{
    g_assert_not_reached();
}
#endif

static void gen_ld_asi(DisasContext *dc, DisasASI *da, TCGv dst, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DTWINX: /* Reserved for ldda.  */
        gen_exception(dc, TT_ILL_INSN);
        break;
    case GET_ASI_DIRECT:
        tcg_gen_qemu_ld_tl(dst, addr, da->mem_idx, da->memop | MO_ALIGN);
        break;
    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop | MO_ALIGN);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_ld_asi(dst, tcg_env, addr, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                gen_helper_ld_asi(t64, tcg_env, addr, r_asi, r_mop);
                tcg_gen_trunc_i64_tl(dst, t64);
            }
#endif
        }
        break;
    }
}

static void gen_st_asi(DisasContext *dc, DisasASI *da, TCGv src, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DTWINX: /* Reserved for stda.  */
        if (TARGET_LONG_BITS == 32) {
            gen_exception(dc, TT_ILL_INSN);
            break;
        } else if (!(dc->def->features & CPU_FEATURE_HYPV)) {
            /* Pre OpenSPARC CPUs don't have these */
            gen_exception(dc, TT_ILL_INSN);
            break;
        }
        /* In OpenSPARC T1+ CPUs TWINX ASIs in store are ST_BLKINIT_ ASIs */
        /* fall through */

    case GET_ASI_DIRECT:
        tcg_gen_qemu_st_tl(src, addr, da->mem_idx, da->memop | MO_ALIGN);
        break;

    case GET_ASI_BCOPY:
        assert(TARGET_LONG_BITS == 32);
        /* Copy 32 bytes from the address in SRC to ADDR.  */
        /* ??? The original qemu code suggests 4-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv saddr = tcg_temp_new();
            TCGv daddr = tcg_temp_new();
            TCGv four = tcg_constant_tl(4);
            TCGv_i32 tmp = tcg_temp_new_i32();
            int i;

            tcg_gen_andi_tl(saddr, src, -4);
            tcg_gen_andi_tl(daddr, addr, -4);
            for (i = 0; i < 32; i += 4) {
                /* Since the loads and stores are paired, allow the
                   copy to happen in the host endianness.  */
                tcg_gen_qemu_ld_i32(tmp, saddr, da->mem_idx, MO_UL);
                tcg_gen_qemu_st_i32(tmp, daddr, da->mem_idx, MO_UL);
                tcg_gen_add_tl(saddr, saddr, four);
                tcg_gen_add_tl(daddr, daddr, four);
            }
        }
        break;

    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop | MO_ALIGN);

            save_state(dc);
#ifdef TARGET_SPARC64
            gen_helper_st_asi(tcg_env, addr, src, r_asi, r_mop);
#else
            {
                TCGv_i64 t64 = tcg_temp_new_i64();
                tcg_gen_extu_tl_i64(t64, src);
                gen_helper_st_asi(tcg_env, addr, t64, r_asi, r_mop);
            }
#endif

            /* A write to a TLB register may alter page maps.  End the TB. */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}

static void gen_swap_asi(DisasContext *dc, DisasASI *da,
                         TCGv dst, TCGv src, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        tcg_gen_atomic_xchg_tl(dst, addr, src,
                               da->mem_idx, da->memop | MO_ALIGN);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void gen_cas_asi(DisasContext *dc, DisasASI *da,
                        TCGv oldv, TCGv newv, TCGv cmpv, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        return;
    case GET_ASI_DIRECT:
        tcg_gen_atomic_cmpxchg_tl(oldv, addr, cmpv, newv,
                                  da->mem_idx, da->memop | MO_ALIGN);
        break;
    default:
        /* ??? Should be DAE_invalid_asi.  */
        gen_exception(dc, TT_DATA_ACCESS);
        break;
    }
}

static void gen_ldstub_asi(DisasContext *dc, DisasASI *da, TCGv dst, TCGv addr)
{
    switch (da->type) {
    case GET_ASI_EXCP:
        break;
    case GET_ASI_DIRECT:
        tcg_gen_atomic_xchg_tl(dst, addr, tcg_constant_tl(0xff),
                               da->mem_idx, MO_UB);
        break;
    default:
        /* ??? In theory, this should be raise DAE_invalid_asi.
           But the SS-20 roms do ldstuba [%l0] #ASI_M_CTL, %o1.  */
        if (tb_cflags(dc->base.tb) & CF_PARALLEL) {
            gen_helper_exit_atomic(tcg_env);
        } else {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(MO_UB);
            TCGv_i64 s64, t64;

            save_state(dc);
            t64 = tcg_temp_new_i64();
            gen_helper_ld_asi(t64, tcg_env, addr, r_asi, r_mop);

            s64 = tcg_constant_i64(0xff);
            gen_helper_st_asi(tcg_env, addr, s64, r_asi, r_mop);

            tcg_gen_trunc_i64_tl(dst, t64);

            /* End the TB.  */
            dc->npc = DYNAMIC_PC;
        }
        break;
    }
}

static void gen_ldf_asi(DisasContext *dc, DisasASI *da, MemOp orig_size,
                        TCGv addr, int rd)
{
    MemOp memop = da->memop;
    MemOp size = memop & MO_SIZE;
    TCGv_i32 d32;
    TCGv_i64 d64;
    TCGv addr_tmp;

    /* TODO: Use 128-bit load/store below. */
    if (size == MO_128) {
        memop = (memop & ~MO_SIZE) | MO_64;
    }

    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        memop |= MO_ALIGN_4;
        switch (size) {
        case MO_32:
            d32 = gen_dest_fpr_F(dc);
            tcg_gen_qemu_ld_i32(d32, addr, da->mem_idx, memop);
            gen_store_fpr_F(dc, rd, d32);
            break;

        case MO_64:
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da->mem_idx, memop);
            break;

        case MO_128:
            d64 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(d64, addr, da->mem_idx, memop);
            addr_tmp = tcg_temp_new();
            tcg_gen_addi_tl(addr_tmp, addr, 8);
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2 + 1], addr_tmp, da->mem_idx, memop);
            tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for lddfa on aligned registers only.  */
        if (orig_size == MO_64 && (rd & 7) == 0) {
            /* The first operation checks required alignment.  */
            addr_tmp = tcg_temp_new();
            for (int i = 0; ; ++i) {
                tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2 + i], addr, da->mem_idx,
                                    memop | (i == 0 ? MO_ALIGN_64 : 0));
                if (i == 7) {
                    break;
                }
                tcg_gen_addi_tl(addr_tmp, addr, 8);
                addr = addr_tmp;
            }
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for lddfa only.  */
        if (orig_size == MO_64) {
            tcg_gen_qemu_ld_i64(cpu_fpr[rd / 2], addr, da->mem_idx,
                                memop | MO_ALIGN);
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    default:
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(memop | MO_ALIGN);

            save_state(dc);
            /* According to the table in the UA2011 manual, the only
               other asis that are valid for ldfa/lddfa/ldqfa are
               the NO_FAULT asis.  We still need a helper for these,
               but we can just use the integer asi helper for them.  */
            switch (size) {
            case MO_32:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, tcg_env, addr, r_asi, r_mop);
                d32 = gen_dest_fpr_F(dc);
                tcg_gen_extrl_i64_i32(d32, d64);
                gen_store_fpr_F(dc, rd, d32);
                break;
            case MO_64:
                gen_helper_ld_asi(cpu_fpr[rd / 2], tcg_env, addr,
                                  r_asi, r_mop);
                break;
            case MO_128:
                d64 = tcg_temp_new_i64();
                gen_helper_ld_asi(d64, tcg_env, addr, r_asi, r_mop);
                addr_tmp = tcg_temp_new();
                tcg_gen_addi_tl(addr_tmp, addr, 8);
                gen_helper_ld_asi(cpu_fpr[rd / 2 + 1], tcg_env, addr_tmp,
                                  r_asi, r_mop);
                tcg_gen_mov_i64(cpu_fpr[rd / 2], d64);
                break;
            default:
                g_assert_not_reached();
            }
        }
        break;
    }
}

static void gen_stf_asi(DisasContext *dc, DisasASI *da, MemOp orig_size,
                        TCGv addr, int rd)
{
    MemOp memop = da->memop;
    MemOp size = memop & MO_SIZE;
    TCGv_i32 d32;
    TCGv addr_tmp;

    /* TODO: Use 128-bit load/store below. */
    if (size == MO_128) {
        memop = (memop & ~MO_SIZE) | MO_64;
    }

    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DIRECT:
        memop |= MO_ALIGN_4;
        switch (size) {
        case MO_32:
            d32 = gen_load_fpr_F(dc, rd);
            tcg_gen_qemu_st_i32(d32, addr, da->mem_idx, memop | MO_ALIGN);
            break;
        case MO_64:
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da->mem_idx,
                                memop | MO_ALIGN_4);
            break;
        case MO_128:
            /* Only 4-byte alignment required.  However, it is legal for the
               cpu to signal the alignment fault, and the OS trap handler is
               required to fix it up.  Requiring 16-byte alignment here avoids
               having to probe the second page before performing the first
               write.  */
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da->mem_idx,
                                memop | MO_ALIGN_16);
            addr_tmp = tcg_temp_new();
            tcg_gen_addi_tl(addr_tmp, addr, 8);
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2 + 1], addr_tmp, da->mem_idx, memop);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case GET_ASI_BLOCK:
        /* Valid for stdfa on aligned registers only.  */
        if (orig_size == MO_64 && (rd & 7) == 0) {
            /* The first operation checks required alignment.  */
            addr_tmp = tcg_temp_new();
            for (int i = 0; ; ++i) {
                tcg_gen_qemu_st_i64(cpu_fpr[rd / 2 + i], addr, da->mem_idx,
                                    memop | (i == 0 ? MO_ALIGN_64 : 0));
                if (i == 7) {
                    break;
                }
                tcg_gen_addi_tl(addr_tmp, addr, 8);
                addr = addr_tmp;
            }
        } else {
            gen_exception(dc, TT_ILL_INSN);
        }
        break;

    case GET_ASI_SHORT:
        /* Valid for stdfa only.  */
        if (orig_size == MO_64) {
            tcg_gen_qemu_st_i64(cpu_fpr[rd / 2], addr, da->mem_idx,
                                memop | MO_ALIGN);
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

static void gen_ldda_asi(DisasContext *dc, DisasASI *da, TCGv addr, int rd)
{
    TCGv hi = gen_dest_gpr(dc, rd);
    TCGv lo = gen_dest_gpr(dc, rd + 1);

    switch (da->type) {
    case GET_ASI_EXCP:
        return;

    case GET_ASI_DTWINX:
#ifdef TARGET_SPARC64
        {
            MemOp mop = (da->memop & MO_BSWAP) | MO_128 | MO_ALIGN_16;
            TCGv_i128 t = tcg_temp_new_i128();

            tcg_gen_qemu_ld_i128(t, addr, da->mem_idx, mop);
            /*
             * Note that LE twinx acts as if each 64-bit register result is
             * byte swapped.  We perform one 128-bit LE load, so must swap
             * the order of the writebacks.
             */
            if ((mop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr_i128_i64(lo, hi, t);
            } else {
                tcg_gen_extr_i128_i64(hi, lo, t);
            }
        }
        break;
#else
        g_assert_not_reached();
#endif

    case GET_ASI_DIRECT:
        {
            TCGv_i64 tmp = tcg_temp_new_i64();

            tcg_gen_qemu_ld_i64(tmp, addr, da->mem_idx, da->memop | MO_ALIGN);

            /* Note that LE ldda acts as if each 32-bit register
               result is byte swapped.  Having just performed one
               64-bit bswap, we need now to swap the writebacks.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr_i64_tl(lo, hi, tmp);
            } else {
                tcg_gen_extr_i64_tl(hi, lo, tmp);
            }
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for ldda, and this should raise DAE_invalid_asi.  However,
           real hardware allows others.  This can be seen with e.g.
           FreeBSD 10.3 wrt ASI_IC_TAG.  */
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop);
            TCGv_i64 tmp = tcg_temp_new_i64();

            save_state(dc);
            gen_helper_ld_asi(tmp, tcg_env, addr, r_asi, r_mop);

            /* See above.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_extr_i64_tl(lo, hi, tmp);
            } else {
                tcg_gen_extr_i64_tl(hi, lo, tmp);
            }
        }
        break;
    }

    gen_store_gpr(dc, rd, hi);
    gen_store_gpr(dc, rd + 1, lo);
}

static void gen_stda_asi(DisasContext *dc, DisasASI *da, TCGv addr, int rd)
{
    TCGv hi = gen_load_gpr(dc, rd);
    TCGv lo = gen_load_gpr(dc, rd + 1);

    switch (da->type) {
    case GET_ASI_EXCP:
        break;

    case GET_ASI_DTWINX:
#ifdef TARGET_SPARC64
        {
            MemOp mop = (da->memop & MO_BSWAP) | MO_128 | MO_ALIGN_16;
            TCGv_i128 t = tcg_temp_new_i128();

            /*
             * Note that LE twinx acts as if each 64-bit register result is
             * byte swapped.  We perform one 128-bit LE store, so must swap
             * the order of the construction.
             */
            if ((mop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat_i64_i128(t, lo, hi);
            } else {
                tcg_gen_concat_i64_i128(t, hi, lo);
            }
            tcg_gen_qemu_st_i128(t, addr, da->mem_idx, mop);
        }
        break;
#else
        g_assert_not_reached();
#endif

    case GET_ASI_DIRECT:
        {
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* Note that LE stda acts as if each 32-bit register result is
               byte swapped.  We will perform one 64-bit LE store, so now
               we must swap the order of the construction.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat_tl_i64(t64, lo, hi);
            } else {
                tcg_gen_concat_tl_i64(t64, hi, lo);
            }
            tcg_gen_qemu_st_i64(t64, addr, da->mem_idx, da->memop | MO_ALIGN);
        }
        break;

    case GET_ASI_BFILL:
        assert(TARGET_LONG_BITS == 32);
        /* Store 32 bytes of T64 to ADDR.  */
        /* ??? The original qemu code suggests 8-byte alignment, dropping
           the low bits, but the only place I can see this used is in the
           Linux kernel with 32 byte alignment, which would make more sense
           as a cacheline-style operation.  */
        {
            TCGv_i64 t64 = tcg_temp_new_i64();
            TCGv d_addr = tcg_temp_new();
            TCGv eight = tcg_constant_tl(8);
            int i;

            tcg_gen_concat_tl_i64(t64, lo, hi);
            tcg_gen_andi_tl(d_addr, addr, -8);
            for (i = 0; i < 32; i += 8) {
                tcg_gen_qemu_st_i64(t64, d_addr, da->mem_idx, da->memop);
                tcg_gen_add_tl(d_addr, d_addr, eight);
            }
        }
        break;

    default:
        /* ??? In theory we've handled all of the ASIs that are valid
           for stda, and this should raise DAE_invalid_asi.  */
        {
            TCGv_i32 r_asi = tcg_constant_i32(da->asi);
            TCGv_i32 r_mop = tcg_constant_i32(da->memop);
            TCGv_i64 t64 = tcg_temp_new_i64();

            /* See above.  */
            if ((da->memop & MO_BSWAP) == MO_TE) {
                tcg_gen_concat_tl_i64(t64, lo, hi);
            } else {
                tcg_gen_concat_tl_i64(t64, hi, lo);
            }

            save_state(dc);
            gen_helper_st_asi(tcg_env, addr, t64, r_asi, r_mop);
        }
        break;
    }
}

static void gen_fmovs(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
#ifdef TARGET_SPARC64
    TCGv_i32 c32, zero, dst, s1, s2;
    TCGv_i64 c64 = tcg_temp_new_i64();

    /* We have two choices here: extend the 32 bit data and use movcond_i64,
       or fold the comparison down to 32 bits and use movcond_i32.  Choose
       the later.  */
    c32 = tcg_temp_new_i32();
    tcg_gen_setcondi_i64(cmp->cond, c64, cmp->c1, cmp->c2);
    tcg_gen_extrl_i64_i32(c32, c64);

    s1 = gen_load_fpr_F(dc, rs);
    s2 = gen_load_fpr_F(dc, rd);
    dst = gen_dest_fpr_F(dc);
    zero = tcg_constant_i32(0);

    tcg_gen_movcond_i32(TCG_COND_NE, dst, c32, zero, s1, s2);

    gen_store_fpr_F(dc, rd, dst);
#else
    qemu_build_not_reached();
#endif
}

static void gen_fmovd(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
#ifdef TARGET_SPARC64
    TCGv_i64 dst = gen_dest_fpr_D(dc, rd);
    tcg_gen_movcond_i64(cmp->cond, dst, cmp->c1, tcg_constant_tl(cmp->c2),
                        gen_load_fpr_D(dc, rs),
                        gen_load_fpr_D(dc, rd));
    gen_store_fpr_D(dc, rd, dst);
#else
    qemu_build_not_reached();
#endif
}

static void gen_fmovq(DisasContext *dc, DisasCompare *cmp, int rd, int rs)
{
#ifdef TARGET_SPARC64
    int qd = QFPREG(rd);
    int qs = QFPREG(rs);
    TCGv c2 = tcg_constant_tl(cmp->c2);

    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2], cmp->c1, c2,
                        cpu_fpr[qs / 2], cpu_fpr[qd / 2]);
    tcg_gen_movcond_i64(cmp->cond, cpu_fpr[qd / 2 + 1], cmp->c1, c2,
                        cpu_fpr[qs / 2 + 1], cpu_fpr[qd / 2 + 1]);

    gen_update_fprs_dirty(dc, qd);
#else
    qemu_build_not_reached();
#endif
}

#ifdef TARGET_SPARC64
static void gen_load_trap_state_at_tl(TCGv_ptr r_tsptr)
{
    TCGv_i32 r_tl = tcg_temp_new_i32();

    /* load env->tl into r_tl */
    tcg_gen_ld_i32(r_tl, tcg_env, offsetof(CPUSPARCState, tl));

    /* tl = [0 ... MAXTL_MASK] where MAXTL_MASK must be power of 2 */
    tcg_gen_andi_i32(r_tl, r_tl, MAXTL_MASK);

    /* calculate offset to current trap state from env->ts, reuse r_tl */
    tcg_gen_muli_i32(r_tl, r_tl, sizeof (trap_state));
    tcg_gen_addi_ptr(r_tsptr, tcg_env, offsetof(CPUSPARCState, ts));

    /* tsptr = env->ts[env->tl & MAXTL_MASK] */
    {
        TCGv_ptr r_tl_tmp = tcg_temp_new_ptr();
        tcg_gen_ext_i32_ptr(r_tl_tmp, r_tl);
        tcg_gen_add_ptr(r_tsptr, r_tsptr, r_tl_tmp);
    }
}
#endif

static int extract_dfpreg(DisasContext *dc, int x)
{
    return DFPREG(x);
}

static int extract_qfpreg(DisasContext *dc, int x)
{
    return QFPREG(x);
}

/* Include the auto-generated decoder.  */
#include "decode-insns.c.inc"

#define TRANS(NAME, AVAIL, FUNC, ...) \
    static bool trans_##NAME(DisasContext *dc, arg_##NAME *a) \
    { return avail_##AVAIL(dc) && FUNC(dc, __VA_ARGS__); }

#define avail_ALL(C)      true
#ifdef TARGET_SPARC64
# define avail_32(C)      false
# define avail_ASR17(C)   false
# define avail_CASA(C)    true
# define avail_DIV(C)     true
# define avail_MUL(C)     true
# define avail_POWERDOWN(C) false
# define avail_64(C)      true
# define avail_GL(C)      ((C)->def->features & CPU_FEATURE_GL)
# define avail_HYPV(C)    ((C)->def->features & CPU_FEATURE_HYPV)
# define avail_VIS1(C)    ((C)->def->features & CPU_FEATURE_VIS1)
# define avail_VIS2(C)    ((C)->def->features & CPU_FEATURE_VIS2)
#else
# define avail_32(C)      true
# define avail_ASR17(C)   ((C)->def->features & CPU_FEATURE_ASR17)
# define avail_CASA(C)    ((C)->def->features & CPU_FEATURE_CASA)
# define avail_DIV(C)     ((C)->def->features & CPU_FEATURE_DIV)
# define avail_MUL(C)     ((C)->def->features & CPU_FEATURE_MUL)
# define avail_POWERDOWN(C) ((C)->def->features & CPU_FEATURE_POWERDOWN)
# define avail_64(C)      false
# define avail_GL(C)      false
# define avail_HYPV(C)    false
# define avail_VIS1(C)    false
# define avail_VIS2(C)    false
#endif

/* Default case for non jump instructions. */
static bool advance_pc(DisasContext *dc)
{
    TCGLabel *l1;

    finishing_insn(dc);

    if (dc->npc & 3) {
        switch (dc->npc) {
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            dc->pc = dc->npc;
            tcg_gen_mov_tl(cpu_pc, cpu_npc);
            tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
            break;

        case JUMP_PC:
            /* we can do a static jump */
            l1 = gen_new_label();
            tcg_gen_brcondi_tl(dc->jump.cond, dc->jump.c1, dc->jump.c2, l1);

            /* jump not taken */
            gen_goto_tb(dc, 1, dc->jump_pc[1], dc->jump_pc[1] + 4);

            /* jump taken */
            gen_set_label(l1);
            gen_goto_tb(dc, 0, dc->jump_pc[0], dc->jump_pc[0] + 4);

            dc->base.is_jmp = DISAS_NORETURN;
            break;

        default:
            g_assert_not_reached();
        }
    } else {
        dc->pc = dc->npc;
        dc->npc = dc->npc + 4;
    }
    return true;
}

/*
 * Major opcodes 00 and 01 -- branches, call, and sethi
 */

static bool advance_jump_cond(DisasContext *dc, DisasCompare *cmp,
                              bool annul, int disp)
{
    target_ulong dest = address_mask_i(dc, dc->pc + disp * 4);
    target_ulong npc;

    finishing_insn(dc);

    if (cmp->cond == TCG_COND_ALWAYS) {
        if (annul) {
            dc->pc = dest;
            dc->npc = dest + 4;
        } else {
            gen_mov_pc_npc(dc);
            dc->npc = dest;
        }
        return true;
    }

    if (cmp->cond == TCG_COND_NEVER) {
        npc = dc->npc;
        if (npc & 3) {
            gen_mov_pc_npc(dc);
            if (annul) {
                tcg_gen_addi_tl(cpu_pc, cpu_pc, 4);
            }
            tcg_gen_addi_tl(cpu_npc, cpu_pc, 4);
        } else {
            dc->pc = npc + (annul ? 4 : 0);
            dc->npc = dc->pc + 4;
        }
        return true;
    }

    flush_cond(dc);
    npc = dc->npc;

    if (annul) {
        TCGLabel *l1 = gen_new_label();

        tcg_gen_brcondi_tl(tcg_invert_cond(cmp->cond), cmp->c1, cmp->c2, l1);
        gen_goto_tb(dc, 0, npc, dest);
        gen_set_label(l1);
        gen_goto_tb(dc, 1, npc + 4, npc + 8);

        dc->base.is_jmp = DISAS_NORETURN;
    } else {
        if (npc & 3) {
            switch (npc) {
            case DYNAMIC_PC:
            case DYNAMIC_PC_LOOKUP:
                tcg_gen_mov_tl(cpu_pc, cpu_npc);
                tcg_gen_addi_tl(cpu_npc, cpu_npc, 4);
                tcg_gen_movcond_tl(cmp->cond, cpu_npc,
                                   cmp->c1, tcg_constant_tl(cmp->c2),
                                   tcg_constant_tl(dest), cpu_npc);
                dc->pc = npc;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            dc->pc = npc;
            dc->npc = JUMP_PC;
            dc->jump = *cmp;
            dc->jump_pc[0] = dest;
            dc->jump_pc[1] = npc + 4;

            /* The condition for cpu_cond is always NE -- normalize. */
            if (cmp->cond == TCG_COND_NE) {
                tcg_gen_xori_tl(cpu_cond, cmp->c1, cmp->c2);
            } else {
                tcg_gen_setcondi_tl(cmp->cond, cpu_cond, cmp->c1, cmp->c2);
            }
            dc->cpu_cond_live = true;
        }
    }
    return true;
}

static bool raise_priv(DisasContext *dc)
{
    gen_exception(dc, TT_PRIV_INSN);
    return true;
}

static bool raise_unimpfpop(DisasContext *dc)
{
    gen_op_fpexception_im(dc, FSR_FTT_UNIMPFPOP);
    return true;
}

static bool gen_trap_float128(DisasContext *dc)
{
    if (dc->def->features & CPU_FEATURE_FLOAT128) {
        return false;
    }
    return raise_unimpfpop(dc);
}

static bool do_bpcc(DisasContext *dc, arg_bcc *a)
{
    DisasCompare cmp;

    gen_compare(&cmp, a->cc, a->cond, dc);
    return advance_jump_cond(dc, &cmp, a->a, a->i);
}

TRANS(Bicc, ALL, do_bpcc, a)
TRANS(BPcc,  64, do_bpcc, a)

static bool do_fbpfcc(DisasContext *dc, arg_bcc *a)
{
    DisasCompare cmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    gen_fcompare(&cmp, a->cc, a->cond);
    return advance_jump_cond(dc, &cmp, a->a, a->i);
}

TRANS(FBPfcc,  64, do_fbpfcc, a)
TRANS(FBfcc,  ALL, do_fbpfcc, a)

static bool trans_BPr(DisasContext *dc, arg_BPr *a)
{
    DisasCompare cmp;

    if (!avail_64(dc)) {
        return false;
    }
    if (!gen_compare_reg(&cmp, a->cond, gen_load_gpr(dc, a->rs1))) {
        return false;
    }
    return advance_jump_cond(dc, &cmp, a->a, a->i);
}

static bool trans_CALL(DisasContext *dc, arg_CALL *a)
{
    target_long target = address_mask_i(dc, dc->pc + a->i * 4);

    gen_store_gpr(dc, 15, tcg_constant_tl(dc->pc));
    gen_mov_pc_npc(dc);
    dc->npc = target;
    return true;
}

static bool trans_NCP(DisasContext *dc, arg_NCP *a)
{
    /*
     * For sparc32, always generate the no-coprocessor exception.
     * For sparc64, always generate illegal instruction.
     */
#ifdef TARGET_SPARC64
    return false;
#else
    gen_exception(dc, TT_NCP_INSN);
    return true;
#endif
}

static bool trans_SETHI(DisasContext *dc, arg_SETHI *a)
{
    /* Special-case %g0 because that's the canonical nop.  */
    if (a->rd) {
        gen_store_gpr(dc, a->rd, tcg_constant_tl((uint32_t)a->i << 10));
    }
    return advance_pc(dc);
}

/*
 * Major Opcode 10 -- integer, floating-point, vis, and system insns.
 */

static bool do_tcc(DisasContext *dc, int cond, int cc,
                   int rs1, bool imm, int rs2_or_imm)
{
    int mask = ((dc->def->features & CPU_FEATURE_HYPV) && supervisor(dc)
                ? UA2005_HTRAP_MASK : V8_TRAP_MASK);
    DisasCompare cmp;
    TCGLabel *lab;
    TCGv_i32 trap;

    /* Trap never.  */
    if (cond == 0) {
        return advance_pc(dc);
    }

    /*
     * Immediate traps are the most common case.  Since this value is
     * live across the branch, it really pays to evaluate the constant.
     */
    if (rs1 == 0 && (imm || rs2_or_imm == 0)) {
        trap = tcg_constant_i32((rs2_or_imm & mask) + TT_TRAP);
    } else {
        trap = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(trap, gen_load_gpr(dc, rs1));
        if (imm) {
            tcg_gen_addi_i32(trap, trap, rs2_or_imm);
        } else {
            TCGv_i32 t2 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, gen_load_gpr(dc, rs2_or_imm));
            tcg_gen_add_i32(trap, trap, t2);
        }
        tcg_gen_andi_i32(trap, trap, mask);
        tcg_gen_addi_i32(trap, trap, TT_TRAP);
    }

    finishing_insn(dc);

    /* Trap always.  */
    if (cond == 8) {
        save_state(dc);
        gen_helper_raise_exception(tcg_env, trap);
        dc->base.is_jmp = DISAS_NORETURN;
        return true;
    }

    /* Conditional trap.  */
    flush_cond(dc);
    lab = delay_exceptionv(dc, trap);
    gen_compare(&cmp, cc, cond, dc);
    tcg_gen_brcondi_tl(cmp.cond, cmp.c1, cmp.c2, lab);

    return advance_pc(dc);
}

static bool trans_Tcc_r(DisasContext *dc, arg_Tcc_r *a)
{
    if (avail_32(dc) && a->cc) {
        return false;
    }
    return do_tcc(dc, a->cond, a->cc, a->rs1, false, a->rs2);
}

static bool trans_Tcc_i_v7(DisasContext *dc, arg_Tcc_i_v7 *a)
{
    if (avail_64(dc)) {
        return false;
    }
    return do_tcc(dc, a->cond, 0, a->rs1, true, a->i);
}

static bool trans_Tcc_i_v9(DisasContext *dc, arg_Tcc_i_v9 *a)
{
    if (avail_32(dc)) {
        return false;
    }
    return do_tcc(dc, a->cond, a->cc, a->rs1, true, a->i);
}

static bool trans_STBAR(DisasContext *dc, arg_STBAR *a)
{
    tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
    return advance_pc(dc);
}

static bool trans_MEMBAR(DisasContext *dc, arg_MEMBAR *a)
{
    if (avail_32(dc)) {
        return false;
    }
    if (a->mmask) {
        /* Note TCG_MO_* was modeled on sparc64, so mmask matches. */
        tcg_gen_mb(a->mmask | TCG_BAR_SC);
    }
    if (a->cmask) {
        /* For #Sync, etc, end the TB to recognize interrupts. */
        dc->base.is_jmp = DISAS_EXIT;
    }
    return advance_pc(dc);
}

static bool do_rd_special(DisasContext *dc, bool priv, int rd,
                          TCGv (*func)(DisasContext *, TCGv))
{
    if (!priv) {
        return raise_priv(dc);
    }
    gen_store_gpr(dc, rd, func(dc, gen_dest_gpr(dc, rd)));
    return advance_pc(dc);
}

static TCGv do_rdy(DisasContext *dc, TCGv dst)
{
    return cpu_y;
}

static bool trans_RDY(DisasContext *dc, arg_RDY *a)
{
    /*
     * TODO: Need a feature bit for sparcv8.  In the meantime, treat all
     * 32-bit cpus like sparcv7, which ignores the rs1 field.
     * This matches after all other ASR, so Leon3 Asr17 is handled first.
     */
    if (avail_64(dc) && a->rs1 != 0) {
        return false;
    }
    return do_rd_special(dc, true, a->rd, do_rdy);
}

static TCGv do_rd_leon3_config(DisasContext *dc, TCGv dst)
{
    uint32_t val;

    /*
     * TODO: There are many more fields to be filled,
     * some of which are writable.
     */
    val = dc->def->nwindows - 1;   /* [4:0] NWIN */
    val |= 1 << 8;                 /* [8]   V8   */

    return tcg_constant_tl(val);
}

TRANS(RDASR17, ASR17, do_rd_special, true, a->rd, do_rd_leon3_config)

static TCGv do_rdccr(DisasContext *dc, TCGv dst)
{
    gen_helper_rdccr(dst, tcg_env);
    return dst;
}

TRANS(RDCCR, 64, do_rd_special, true, a->rd, do_rdccr)

static TCGv do_rdasi(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    return tcg_constant_tl(dc->asi);
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDASI, 64, do_rd_special, true, a->rd, do_rdasi)

static TCGv do_rdtick(DisasContext *dc, TCGv dst)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_tick_get_count(dst, tcg_env, r_tickptr,
                              tcg_constant_i32(dc->mem_idx));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDTICK, 64, do_rd_special, true, a->rd, do_rdtick)

static TCGv do_rdpc(DisasContext *dc, TCGv dst)
{
    return tcg_constant_tl(address_mask_i(dc, dc->pc));
}

TRANS(RDPC, 64, do_rd_special, true, a->rd, do_rdpc)

static TCGv do_rdfprs(DisasContext *dc, TCGv dst)
{
    tcg_gen_ext_i32_tl(dst, cpu_fprs);
    return dst;
}

TRANS(RDFPRS, 64, do_rd_special, true, a->rd, do_rdfprs)

static TCGv do_rdgsr(DisasContext *dc, TCGv dst)
{
    gen_trap_ifnofpu(dc);
    return cpu_gsr;
}

TRANS(RDGSR, 64, do_rd_special, true, a->rd, do_rdgsr)

static TCGv do_rdsoftint(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(softint));
    return dst;
}

TRANS(RDSOFTINT, 64, do_rd_special, supervisor(dc), a->rd, do_rdsoftint)

static TCGv do_rdtick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(tick_cmpr));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDTICK_CMPR, 64, do_rd_special, true, a->rd, do_rdtick_cmpr)

static TCGv do_rdstick(DisasContext *dc, TCGv dst)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(stick));
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_tick_get_count(dst, tcg_env, r_tickptr,
                              tcg_constant_i32(dc->mem_idx));
    return dst;
}

/* TODO: non-priv access only allowed when enabled. */
TRANS(RDSTICK, 64, do_rd_special, true, a->rd, do_rdstick)

static TCGv do_rdstick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(stick_cmpr));
    return dst;
}

/* TODO: supervisor access only allowed when enabled by hypervisor. */
TRANS(RDSTICK_CMPR, 64, do_rd_special, supervisor(dc), a->rd, do_rdstick_cmpr)

/*
 * UltraSPARC-T1 Strand status.
 * HYPV check maybe not enough, UA2005 & UA2007 describe
 * this ASR as impl. dep
 */
static TCGv do_rdstrand_status(DisasContext *dc, TCGv dst)
{
    return tcg_constant_tl(1);
}

TRANS(RDSTRAND_STATUS, HYPV, do_rd_special, true, a->rd, do_rdstrand_status)

static TCGv do_rdpsr(DisasContext *dc, TCGv dst)
{
    gen_helper_rdpsr(dst, tcg_env);
    return dst;
}

TRANS(RDPSR, 32, do_rd_special, supervisor(dc), a->rd, do_rdpsr)

static TCGv do_rdhpstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hpstate));
    return dst;
}

TRANS(RDHPR_hpstate, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhpstate)

static TCGv do_rdhtstate(DisasContext *dc, TCGv dst)
{
    TCGv_i32 tl = tcg_temp_new_i32();
    TCGv_ptr tp = tcg_temp_new_ptr();

    tcg_gen_ld_i32(tl, tcg_env, env64_field_offsetof(tl));
    tcg_gen_andi_i32(tl, tl, MAXTL_MASK);
    tcg_gen_shli_i32(tl, tl, 3);
    tcg_gen_ext_i32_ptr(tp, tl);
    tcg_gen_add_ptr(tp, tp, tcg_env);

    tcg_gen_ld_tl(dst, tp, env64_field_offsetof(htstate));
    return dst;
}

TRANS(RDHPR_htstate, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhtstate)

static TCGv do_rdhintp(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hintp));
    return dst;
}

TRANS(RDHPR_hintp, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhintp)

static TCGv do_rdhtba(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(htba));
    return dst;
}

TRANS(RDHPR_htba, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhtba)

static TCGv do_rdhver(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hver));
    return dst;
}

TRANS(RDHPR_hver, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdhver)

static TCGv do_rdhstick_cmpr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(hstick_cmpr));
    return dst;
}

TRANS(RDHPR_hstick_cmpr, HYPV, do_rd_special, hypervisor(dc), a->rd,
      do_rdhstick_cmpr)

static TCGv do_rdwim(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env32_field_offsetof(wim));
    return dst;
}

TRANS(RDWIM, 32, do_rd_special, supervisor(dc), a->rd, do_rdwim)

static TCGv do_rdtpc(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tpc));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tpc, 64, do_rd_special, supervisor(dc), a->rd, do_rdtpc)

static TCGv do_rdtnpc(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tnpc));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tnpc, 64, do_rd_special, supervisor(dc), a->rd, do_rdtnpc)

static TCGv do_rdtstate(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld_tl(dst, r_tsptr, offsetof(trap_state, tstate));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdtstate)

static TCGv do_rdtt(DisasContext *dc, TCGv dst)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_ld32s_tl(dst, r_tsptr, offsetof(trap_state, tt));
    return dst;
#else
    qemu_build_not_reached();
#endif
}

TRANS(RDPR_tt, 64, do_rd_special, supervisor(dc), a->rd, do_rdtt)
TRANS(RDPR_tick, 64, do_rd_special, supervisor(dc), a->rd, do_rdtick)

static TCGv do_rdtba(DisasContext *dc, TCGv dst)
{
    return cpu_tbr;
}

TRANS(RDTBR, 32, do_rd_special, supervisor(dc), a->rd, do_rdtba)
TRANS(RDPR_tba, 64, do_rd_special, supervisor(dc), a->rd, do_rdtba)

static TCGv do_rdpstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(pstate));
    return dst;
}

TRANS(RDPR_pstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdpstate)

static TCGv do_rdtl(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(tl));
    return dst;
}

TRANS(RDPR_tl, 64, do_rd_special, supervisor(dc), a->rd, do_rdtl)

static TCGv do_rdpil(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env_field_offsetof(psrpil));
    return dst;
}

TRANS(RDPR_pil, 64, do_rd_special, supervisor(dc), a->rd, do_rdpil)

static TCGv do_rdcwp(DisasContext *dc, TCGv dst)
{
    gen_helper_rdcwp(dst, tcg_env);
    return dst;
}

TRANS(RDPR_cwp, 64, do_rd_special, supervisor(dc), a->rd, do_rdcwp)

static TCGv do_rdcansave(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(cansave));
    return dst;
}

TRANS(RDPR_cansave, 64, do_rd_special, supervisor(dc), a->rd, do_rdcansave)

static TCGv do_rdcanrestore(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(canrestore));
    return dst;
}

TRANS(RDPR_canrestore, 64, do_rd_special, supervisor(dc), a->rd,
      do_rdcanrestore)

static TCGv do_rdcleanwin(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(cleanwin));
    return dst;
}

TRANS(RDPR_cleanwin, 64, do_rd_special, supervisor(dc), a->rd, do_rdcleanwin)

static TCGv do_rdotherwin(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(otherwin));
    return dst;
}

TRANS(RDPR_otherwin, 64, do_rd_special, supervisor(dc), a->rd, do_rdotherwin)

static TCGv do_rdwstate(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(wstate));
    return dst;
}

TRANS(RDPR_wstate, 64, do_rd_special, supervisor(dc), a->rd, do_rdwstate)

static TCGv do_rdgl(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld32s_tl(dst, tcg_env, env64_field_offsetof(gl));
    return dst;
}

TRANS(RDPR_gl, GL, do_rd_special, supervisor(dc), a->rd, do_rdgl)

/* UA2005 strand status */
static TCGv do_rdssr(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(ssr));
    return dst;
}

TRANS(RDPR_strand_status, HYPV, do_rd_special, hypervisor(dc), a->rd, do_rdssr)

static TCGv do_rdver(DisasContext *dc, TCGv dst)
{
    tcg_gen_ld_tl(dst, tcg_env, env64_field_offsetof(version));
    return dst;
}

TRANS(RDPR_ver, 64, do_rd_special, supervisor(dc), a->rd, do_rdver)

static bool trans_FLUSHW(DisasContext *dc, arg_FLUSHW *a)
{
    if (avail_64(dc)) {
        gen_helper_flushw(tcg_env);
        return advance_pc(dc);
    }
    return false;
}

static bool do_wr_special(DisasContext *dc, arg_r_r_ri *a, bool priv,
                          void (*func)(DisasContext *, TCGv))
{
    TCGv src;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && (a->rs2_or_imm & ~0x1f)) {
        return false;
    }
    if (!priv) {
        return raise_priv(dc);
    }

    if (a->rs1 == 0 && (a->imm || a->rs2_or_imm == 0)) {
        src = tcg_constant_tl(a->rs2_or_imm);
    } else {
        TCGv src1 = gen_load_gpr(dc, a->rs1);
        if (a->rs2_or_imm == 0) {
            src = src1;
        } else {
            src = tcg_temp_new();
            if (a->imm) {
                tcg_gen_xori_tl(src, src1, a->rs2_or_imm);
            } else {
                tcg_gen_xor_tl(src, src1, gen_load_gpr(dc, a->rs2_or_imm));
            }
        }
    }
    func(dc, src);
    return advance_pc(dc);
}

static void do_wry(DisasContext *dc, TCGv src)
{
    tcg_gen_ext32u_tl(cpu_y, src);
}

TRANS(WRY, ALL, do_wr_special, a, true, do_wry)

static void do_wrccr(DisasContext *dc, TCGv src)
{
    gen_helper_wrccr(tcg_env, src);
}

TRANS(WRCCR, 64, do_wr_special, a, true, do_wrccr)

static void do_wrasi(DisasContext *dc, TCGv src)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_ext8u_tl(tmp, src);
    tcg_gen_st32_tl(tmp, tcg_env, env64_field_offsetof(asi));
    /* End TB to notice changed ASI. */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRASI, 64, do_wr_special, a, true, do_wrasi)

static void do_wrfprs(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    tcg_gen_trunc_tl_i32(cpu_fprs, src);
    dc->fprs_dirty = 0;
    dc->base.is_jmp = DISAS_EXIT;
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRFPRS, 64, do_wr_special, a, true, do_wrfprs)

static void do_wrgsr(DisasContext *dc, TCGv src)
{
    gen_trap_ifnofpu(dc);
    tcg_gen_mov_tl(cpu_gsr, src);
}

TRANS(WRGSR, 64, do_wr_special, a, true, do_wrgsr)

static void do_wrsoftint_set(DisasContext *dc, TCGv src)
{
    gen_helper_set_softint(tcg_env, src);
}

TRANS(WRSOFTINT_SET, 64, do_wr_special, a, supervisor(dc), do_wrsoftint_set)

static void do_wrsoftint_clr(DisasContext *dc, TCGv src)
{
    gen_helper_clear_softint(tcg_env, src);
}

TRANS(WRSOFTINT_CLR, 64, do_wr_special, a, supervisor(dc), do_wrsoftint_clr)

static void do_wrsoftint(DisasContext *dc, TCGv src)
{
    gen_helper_write_softint(tcg_env, src);
}

TRANS(WRSOFTINT, 64, do_wr_special, a, supervisor(dc), do_wrsoftint)

static void do_wrtick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(tick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRTICK_CMPR, 64, do_wr_special, a, supervisor(dc), do_wrtick_cmpr)

static void do_wrstick(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, offsetof(CPUSPARCState, stick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_count(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRSTICK, 64, do_wr_special, a, supervisor(dc), do_wrstick)

static void do_wrstick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(stick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(stick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRSTICK_CMPR, 64, do_wr_special, a, supervisor(dc), do_wrstick_cmpr)

static void do_wrpowerdown(DisasContext *dc, TCGv src)
{
    finishing_insn(dc);
    save_state(dc);
    gen_helper_power_down(tcg_env);
}

TRANS(WRPOWERDOWN, POWERDOWN, do_wr_special, a, supervisor(dc), do_wrpowerdown)

static void do_wrpsr(DisasContext *dc, TCGv src)
{
    gen_helper_wrpsr(tcg_env, src);
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRPSR, 32, do_wr_special, a, supervisor(dc), do_wrpsr)

static void do_wrwim(DisasContext *dc, TCGv src)
{
    target_ulong mask = MAKE_64BIT_MASK(0, dc->def->nwindows);
    TCGv tmp = tcg_temp_new();

    tcg_gen_andi_tl(tmp, src, mask);
    tcg_gen_st_tl(tmp, tcg_env, env32_field_offsetof(wim));
}

TRANS(WRWIM, 32, do_wr_special, a, supervisor(dc), do_wrwim)

static void do_wrtpc(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tpc));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tpc, 64, do_wr_special, a, supervisor(dc), do_wrtpc)

static void do_wrtnpc(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tnpc));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tnpc, 64, do_wr_special, a, supervisor(dc), do_wrtnpc)

static void do_wrtstate(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st_tl(src, r_tsptr, offsetof(trap_state, tstate));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tstate, 64, do_wr_special, a, supervisor(dc), do_wrtstate)

static void do_wrtt(DisasContext *dc, TCGv src)
{
#ifdef TARGET_SPARC64
    TCGv_ptr r_tsptr = tcg_temp_new_ptr();

    gen_load_trap_state_at_tl(r_tsptr);
    tcg_gen_st32_tl(src, r_tsptr, offsetof(trap_state, tt));
#else
    qemu_build_not_reached();
#endif
}

TRANS(WRPR_tt, 64, do_wr_special, a, supervisor(dc), do_wrtt)

static void do_wrtick(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(tick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_count(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRPR_tick, 64, do_wr_special, a, supervisor(dc), do_wrtick)

static void do_wrtba(DisasContext *dc, TCGv src)
{
    tcg_gen_mov_tl(cpu_tbr, src);
}

TRANS(WRPR_tba, 64, do_wr_special, a, supervisor(dc), do_wrtba)

static void do_wrpstate(DisasContext *dc, TCGv src)
{
    save_state(dc);
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_wrpstate(tcg_env, src);
    dc->npc = DYNAMIC_PC;
}

TRANS(WRPR_pstate, 64, do_wr_special, a, supervisor(dc), do_wrpstate)

static void do_wrtl(DisasContext *dc, TCGv src)
{
    save_state(dc);
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(tl));
    dc->npc = DYNAMIC_PC;
}

TRANS(WRPR_tl, 64, do_wr_special, a, supervisor(dc), do_wrtl)

static void do_wrpil(DisasContext *dc, TCGv src)
{
    if (translator_io_start(&dc->base)) {
        dc->base.is_jmp = DISAS_EXIT;
    }
    gen_helper_wrpil(tcg_env, src);
}

TRANS(WRPR_pil, 64, do_wr_special, a, supervisor(dc), do_wrpil)

static void do_wrcwp(DisasContext *dc, TCGv src)
{
    gen_helper_wrcwp(tcg_env, src);
}

TRANS(WRPR_cwp, 64, do_wr_special, a, supervisor(dc), do_wrcwp)

static void do_wrcansave(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(cansave));
}

TRANS(WRPR_cansave, 64, do_wr_special, a, supervisor(dc), do_wrcansave)

static void do_wrcanrestore(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(canrestore));
}

TRANS(WRPR_canrestore, 64, do_wr_special, a, supervisor(dc), do_wrcanrestore)

static void do_wrcleanwin(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(cleanwin));
}

TRANS(WRPR_cleanwin, 64, do_wr_special, a, supervisor(dc), do_wrcleanwin)

static void do_wrotherwin(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(otherwin));
}

TRANS(WRPR_otherwin, 64, do_wr_special, a, supervisor(dc), do_wrotherwin)

static void do_wrwstate(DisasContext *dc, TCGv src)
{
    tcg_gen_st32_tl(src, tcg_env, env64_field_offsetof(wstate));
}

TRANS(WRPR_wstate, 64, do_wr_special, a, supervisor(dc), do_wrwstate)

static void do_wrgl(DisasContext *dc, TCGv src)
{
    gen_helper_wrgl(tcg_env, src);
}

TRANS(WRPR_gl, GL, do_wr_special, a, supervisor(dc), do_wrgl)

/* UA2005 strand status */
static void do_wrssr(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(ssr));
}

TRANS(WRPR_strand_status, HYPV, do_wr_special, a, hypervisor(dc), do_wrssr)

TRANS(WRTBR, 32, do_wr_special, a, supervisor(dc), do_wrtba)

static void do_wrhpstate(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hpstate));
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRHPR_hpstate, HYPV, do_wr_special, a, hypervisor(dc), do_wrhpstate)

static void do_wrhtstate(DisasContext *dc, TCGv src)
{
    TCGv_i32 tl = tcg_temp_new_i32();
    TCGv_ptr tp = tcg_temp_new_ptr();

    tcg_gen_ld_i32(tl, tcg_env, env64_field_offsetof(tl));
    tcg_gen_andi_i32(tl, tl, MAXTL_MASK);
    tcg_gen_shli_i32(tl, tl, 3);
    tcg_gen_ext_i32_ptr(tp, tl);
    tcg_gen_add_ptr(tp, tp, tcg_env);

    tcg_gen_st_tl(src, tp, env64_field_offsetof(htstate));
}

TRANS(WRHPR_htstate, HYPV, do_wr_special, a, hypervisor(dc), do_wrhtstate)

static void do_wrhintp(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hintp));
}

TRANS(WRHPR_hintp, HYPV, do_wr_special, a, hypervisor(dc), do_wrhintp)

static void do_wrhtba(DisasContext *dc, TCGv src)
{
    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(htba));
}

TRANS(WRHPR_htba, HYPV, do_wr_special, a, hypervisor(dc), do_wrhtba)

static void do_wrhstick_cmpr(DisasContext *dc, TCGv src)
{
    TCGv_ptr r_tickptr = tcg_temp_new_ptr();

    tcg_gen_st_tl(src, tcg_env, env64_field_offsetof(hstick_cmpr));
    tcg_gen_ld_ptr(r_tickptr, tcg_env, env64_field_offsetof(hstick));
    translator_io_start(&dc->base);
    gen_helper_tick_set_limit(r_tickptr, src);
    /* End TB to handle timer interrupt */
    dc->base.is_jmp = DISAS_EXIT;
}

TRANS(WRHPR_hstick_cmpr, HYPV, do_wr_special, a, hypervisor(dc),
      do_wrhstick_cmpr)

static bool do_saved_restored(DisasContext *dc, bool saved)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }
    if (saved) {
        gen_helper_saved(tcg_env);
    } else {
        gen_helper_restored(tcg_env);
    }
    return advance_pc(dc);
}

TRANS(SAVED, 64, do_saved_restored, true)
TRANS(RESTORED, 64, do_saved_restored, false)

static bool trans_NOP(DisasContext *dc, arg_NOP *a)
{
    return advance_pc(dc);
}

/*
 * TODO: Need a feature bit for sparcv8.
 * In the meantime, treat all 32-bit cpus like sparcv7.
 */
TRANS(NOP_v7, 32, trans_NOP, a)
TRANS(NOP_v9, 64, trans_NOP, a)

static bool do_arith_int(DisasContext *dc, arg_r_r_ri_cc *a,
                         void (*func)(TCGv, TCGv, TCGv),
                         void (*funci)(TCGv, TCGv, target_long),
                         bool logic_cc)
{
    TCGv dst, src1;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    if (logic_cc) {
        dst = cpu_cc_N;
    } else {
        dst = gen_dest_gpr(dc, a->rd);
    }
    src1 = gen_load_gpr(dc, a->rs1);

    if (a->imm || a->rs2_or_imm == 0) {
        if (funci) {
            funci(dst, src1, a->rs2_or_imm);
        } else {
            func(dst, src1, tcg_constant_tl(a->rs2_or_imm));
        }
    } else {
        func(dst, src1, cpu_regs[a->rs2_or_imm]);
    }

    if (logic_cc) {
        if (TARGET_LONG_BITS == 64) {
            tcg_gen_mov_tl(cpu_icc_Z, cpu_cc_N);
            tcg_gen_movi_tl(cpu_icc_C, 0);
        }
        tcg_gen_mov_tl(cpu_cc_Z, cpu_cc_N);
        tcg_gen_movi_tl(cpu_cc_C, 0);
        tcg_gen_movi_tl(cpu_cc_V, 0);
    }

    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool do_arith(DisasContext *dc, arg_r_r_ri_cc *a,
                     void (*func)(TCGv, TCGv, TCGv),
                     void (*funci)(TCGv, TCGv, target_long),
                     void (*func_cc)(TCGv, TCGv, TCGv))
{
    if (a->cc) {
        return do_arith_int(dc, a, func_cc, NULL, false);
    }
    return do_arith_int(dc, a, func, funci, false);
}

static bool do_logic(DisasContext *dc, arg_r_r_ri_cc *a,
                     void (*func)(TCGv, TCGv, TCGv),
                     void (*funci)(TCGv, TCGv, target_long))
{
    return do_arith_int(dc, a, func, funci, a->cc);
}

TRANS(ADD, ALL, do_arith, a, tcg_gen_add_tl, tcg_gen_addi_tl, gen_op_addcc)
TRANS(SUB, ALL, do_arith, a, tcg_gen_sub_tl, tcg_gen_subi_tl, gen_op_subcc)
TRANS(ADDC, ALL, do_arith, a, gen_op_addc, NULL, gen_op_addccc)
TRANS(SUBC, ALL, do_arith, a, gen_op_subc, NULL, gen_op_subccc)

TRANS(TADDcc, ALL, do_arith, a, NULL, NULL, gen_op_taddcc)
TRANS(TSUBcc, ALL, do_arith, a, NULL, NULL, gen_op_tsubcc)
TRANS(TADDccTV, ALL, do_arith, a, NULL, NULL, gen_op_taddcctv)
TRANS(TSUBccTV, ALL, do_arith, a, NULL, NULL, gen_op_tsubcctv)

TRANS(AND, ALL, do_logic, a, tcg_gen_and_tl, tcg_gen_andi_tl)
TRANS(XOR, ALL, do_logic, a, tcg_gen_xor_tl, tcg_gen_xori_tl)
TRANS(ANDN, ALL, do_logic, a, tcg_gen_andc_tl, NULL)
TRANS(ORN, ALL, do_logic, a, tcg_gen_orc_tl, NULL)
TRANS(XORN, ALL, do_logic, a, tcg_gen_eqv_tl, NULL)

TRANS(MULX, 64, do_arith, a, tcg_gen_mul_tl, tcg_gen_muli_tl, NULL)
TRANS(UMUL, MUL, do_logic, a, gen_op_umul, NULL)
TRANS(SMUL, MUL, do_logic, a, gen_op_smul, NULL)
TRANS(MULScc, ALL, do_arith, a, NULL, NULL, gen_op_mulscc)

TRANS(UDIVcc, DIV, do_arith, a, NULL, NULL, gen_op_udivcc)
TRANS(SDIV, DIV, do_arith, a, gen_op_sdiv, NULL, gen_op_sdivcc)

/* TODO: Should have feature bit -- comes in with UltraSparc T2. */
TRANS(POPC, 64, do_arith, a, gen_op_popc, NULL, NULL)

static bool trans_OR(DisasContext *dc, arg_r_r_ri_cc *a)
{
    /* OR with %g0 is the canonical alias for MOV. */
    if (!a->cc && a->rs1 == 0) {
        if (a->imm || a->rs2_or_imm == 0) {
            gen_store_gpr(dc, a->rd, tcg_constant_tl(a->rs2_or_imm));
        } else if (a->rs2_or_imm & ~0x1f) {
            /* For simplicity, we under-decoded the rs2 form. */
            return false;
        } else {
            gen_store_gpr(dc, a->rd, cpu_regs[a->rs2_or_imm]);
        }
        return advance_pc(dc);
    }
    return do_logic(dc, a, tcg_gen_or_tl, tcg_gen_ori_tl);
}

static bool trans_UDIV(DisasContext *dc, arg_r_r_ri *a)
{
    TCGv_i64 t1, t2;
    TCGv dst;

    if (!avail_DIV(dc)) {
        return false;
    }
    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    if (unlikely(a->rs2_or_imm == 0)) {
        gen_exception(dc, TT_DIV_ZERO);
        return true;
    }

    if (a->imm) {
        t2 = tcg_constant_i64((uint32_t)a->rs2_or_imm);
    } else {
        TCGLabel *lab;
        TCGv_i32 n2;

        finishing_insn(dc);
        flush_cond(dc);

        n2 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(n2, cpu_regs[a->rs2_or_imm]);

        lab = delay_exception(dc, TT_DIV_ZERO);
        tcg_gen_brcondi_i32(TCG_COND_EQ, n2, 0, lab);

        t2 = tcg_temp_new_i64();
#ifdef TARGET_SPARC64
        tcg_gen_ext32u_i64(t2, cpu_regs[a->rs2_or_imm]);
#else
        tcg_gen_extu_i32_i64(t2, cpu_regs[a->rs2_or_imm]);
#endif
    }

    t1 = tcg_temp_new_i64();
    tcg_gen_concat_tl_i64(t1, gen_load_gpr(dc, a->rs1), cpu_y);

    tcg_gen_divu_i64(t1, t1, t2);
    tcg_gen_umin_i64(t1, t1, tcg_constant_i64(UINT32_MAX));

    dst = gen_dest_gpr(dc, a->rd);
    tcg_gen_trunc_i64_tl(dst, t1);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool trans_UDIVX(DisasContext *dc, arg_r_r_ri *a)
{
    TCGv dst, src1, src2;

    if (!avail_64(dc)) {
        return false;
    }
    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    if (unlikely(a->rs2_or_imm == 0)) {
        gen_exception(dc, TT_DIV_ZERO);
        return true;
    }

    if (a->imm) {
        src2 = tcg_constant_tl(a->rs2_or_imm);
    } else {
        TCGLabel *lab;

        finishing_insn(dc);
        flush_cond(dc);

        lab = delay_exception(dc, TT_DIV_ZERO);
        src2 = cpu_regs[a->rs2_or_imm];
        tcg_gen_brcondi_tl(TCG_COND_EQ, src2, 0, lab);
    }

    dst = gen_dest_gpr(dc, a->rd);
    src1 = gen_load_gpr(dc, a->rs1);

    tcg_gen_divu_tl(dst, src1, src2);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool trans_SDIVX(DisasContext *dc, arg_r_r_ri *a)
{
    TCGv dst, src1, src2;

    if (!avail_64(dc)) {
        return false;
    }
    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    if (unlikely(a->rs2_or_imm == 0)) {
        gen_exception(dc, TT_DIV_ZERO);
        return true;
    }

    dst = gen_dest_gpr(dc, a->rd);
    src1 = gen_load_gpr(dc, a->rs1);

    if (a->imm) {
        if (unlikely(a->rs2_or_imm == -1)) {
            tcg_gen_neg_tl(dst, src1);
            gen_store_gpr(dc, a->rd, dst);
            return advance_pc(dc);
        }
        src2 = tcg_constant_tl(a->rs2_or_imm);
    } else {
        TCGLabel *lab;
        TCGv t1, t2;

        finishing_insn(dc);
        flush_cond(dc);

        lab = delay_exception(dc, TT_DIV_ZERO);
        src2 = cpu_regs[a->rs2_or_imm];
        tcg_gen_brcondi_tl(TCG_COND_EQ, src2, 0, lab);

        /*
         * Need to avoid INT64_MIN / -1, which will trap on x86 host.
         * Set SRC2 to 1 as a new divisor, to produce the correct result.
         */
        t1 = tcg_temp_new();
        t2 = tcg_temp_new();
        tcg_gen_setcondi_tl(TCG_COND_EQ, t1, src1, (target_long)INT64_MIN);
        tcg_gen_setcondi_tl(TCG_COND_EQ, t2, src2, -1);
        tcg_gen_and_tl(t1, t1, t2);
        tcg_gen_movcond_tl(TCG_COND_NE, t1, t1, tcg_constant_tl(0),
                           tcg_constant_tl(1), src2);
        src2 = t1;
    }

    tcg_gen_div_tl(dst, src1, src2);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool gen_edge(DisasContext *dc, arg_r_r_r *a,
                     int width, bool cc, bool left)
{
    TCGv dst, s1, s2, lo1, lo2;
    uint64_t amask, tabl, tabr;
    int shift, imask, omask;

    dst = gen_dest_gpr(dc, a->rd);
    s1 = gen_load_gpr(dc, a->rs1);
    s2 = gen_load_gpr(dc, a->rs2);

    if (cc) {
        gen_op_subcc(cpu_cc_N, s1, s2);
    }

    /*
     * Theory of operation: there are two tables, left and right (not to
     * be confused with the left and right versions of the opcode).  These
     * are indexed by the low 3 bits of the inputs.  To make things "easy",
     * these tables are loaded into two constants, TABL and TABR below.
     * The operation index = (input & imask) << shift calculates the index
     * into the constant, while val = (table >> index) & omask calculates
     * the value we're looking for.
     */
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

    tcg_gen_shr_tl(lo1, tcg_constant_tl(tabl), lo1);
    tcg_gen_shr_tl(lo2, tcg_constant_tl(tabr), lo2);
    tcg_gen_andi_tl(lo1, lo1, omask);
    tcg_gen_andi_tl(lo2, lo2, omask);

    amask = address_mask_i(dc, -8);
    tcg_gen_andi_tl(s1, s1, amask);
    tcg_gen_andi_tl(s2, s2, amask);

    /* Compute dst = (s1 == s2 ? lo1 : lo1 & lo2). */
    tcg_gen_and_tl(lo2, lo2, lo1);
    tcg_gen_movcond_tl(TCG_COND_EQ, dst, s1, s2, lo1, lo2);

    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(EDGE8cc, VIS1, gen_edge, a, 8, 1, 0)
TRANS(EDGE8Lcc, VIS1, gen_edge, a, 8, 1, 1)
TRANS(EDGE16cc, VIS1, gen_edge, a, 16, 1, 0)
TRANS(EDGE16Lcc, VIS1, gen_edge, a, 16, 1, 1)
TRANS(EDGE32cc, VIS1, gen_edge, a, 32, 1, 0)
TRANS(EDGE32Lcc, VIS1, gen_edge, a, 32, 1, 1)

TRANS(EDGE8N, VIS2, gen_edge, a, 8, 0, 0)
TRANS(EDGE8LN, VIS2, gen_edge, a, 8, 0, 1)
TRANS(EDGE16N, VIS2, gen_edge, a, 16, 0, 0)
TRANS(EDGE16LN, VIS2, gen_edge, a, 16, 0, 1)
TRANS(EDGE32N, VIS2, gen_edge, a, 32, 0, 0)
TRANS(EDGE32LN, VIS2, gen_edge, a, 32, 0, 1)

static bool do_rrr(DisasContext *dc, arg_r_r_r *a,
                   void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dst = gen_dest_gpr(dc, a->rd);
    TCGv src1 = gen_load_gpr(dc, a->rs1);
    TCGv src2 = gen_load_gpr(dc, a->rs2);

    func(dst, src1, src2);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(ARRAY8, VIS1, do_rrr, a, gen_helper_array8)
TRANS(ARRAY16, VIS1, do_rrr, a, gen_op_array16)
TRANS(ARRAY32, VIS1, do_rrr, a, gen_op_array32)

static void gen_op_alignaddr(TCGv dst, TCGv s1, TCGv s2)
{
#ifdef TARGET_SPARC64
    TCGv tmp = tcg_temp_new();

    tcg_gen_add_tl(tmp, s1, s2);
    tcg_gen_andi_tl(dst, tmp, -8);
    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, tmp, 0, 3);
#else
    g_assert_not_reached();
#endif
}

static void gen_op_alignaddrl(TCGv dst, TCGv s1, TCGv s2)
{
#ifdef TARGET_SPARC64
    TCGv tmp = tcg_temp_new();

    tcg_gen_add_tl(tmp, s1, s2);
    tcg_gen_andi_tl(dst, tmp, -8);
    tcg_gen_neg_tl(tmp, tmp);
    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, tmp, 0, 3);
#else
    g_assert_not_reached();
#endif
}

TRANS(ALIGNADDR, VIS1, do_rrr, a, gen_op_alignaddr)
TRANS(ALIGNADDRL, VIS1, do_rrr, a, gen_op_alignaddrl)

static void gen_op_bmask(TCGv dst, TCGv s1, TCGv s2)
{
#ifdef TARGET_SPARC64
    tcg_gen_add_tl(dst, s1, s2);
    tcg_gen_deposit_tl(cpu_gsr, cpu_gsr, dst, 32, 32);
#else
    g_assert_not_reached();
#endif
}

TRANS(BMASK, VIS2, do_rrr, a, gen_op_bmask)

static bool do_shift_r(DisasContext *dc, arg_shiftr *a, bool l, bool u)
{
    TCGv dst, src1, src2;

    /* Reject 64-bit shifts for sparc32. */
    if (avail_32(dc) && a->x) {
        return false;
    }

    src2 = tcg_temp_new();
    tcg_gen_andi_tl(src2, gen_load_gpr(dc, a->rs2), a->x ? 63 : 31);
    src1 = gen_load_gpr(dc, a->rs1);
    dst = gen_dest_gpr(dc, a->rd);

    if (l) {
        tcg_gen_shl_tl(dst, src1, src2);
        if (!a->x) {
            tcg_gen_ext32u_tl(dst, dst);
        }
    } else if (u) {
        if (!a->x) {
            tcg_gen_ext32u_tl(dst, src1);
            src1 = dst;
        }
        tcg_gen_shr_tl(dst, src1, src2);
    } else {
        if (!a->x) {
            tcg_gen_ext32s_tl(dst, src1);
            src1 = dst;
        }
        tcg_gen_sar_tl(dst, src1, src2);
    }
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(SLL_r, ALL, do_shift_r, a, true, true)
TRANS(SRL_r, ALL, do_shift_r, a, false, true)
TRANS(SRA_r, ALL, do_shift_r, a, false, false)

static bool do_shift_i(DisasContext *dc, arg_shifti *a, bool l, bool u)
{
    TCGv dst, src1;

    /* Reject 64-bit shifts for sparc32. */
    if (avail_32(dc) && (a->x || a->i >= 32)) {
        return false;
    }

    src1 = gen_load_gpr(dc, a->rs1);
    dst = gen_dest_gpr(dc, a->rd);

    if (avail_32(dc) || a->x) {
        if (l) {
            tcg_gen_shli_tl(dst, src1, a->i);
        } else if (u) {
            tcg_gen_shri_tl(dst, src1, a->i);
        } else {
            tcg_gen_sari_tl(dst, src1, a->i);
        }
    } else {
        if (l) {
            tcg_gen_deposit_z_tl(dst, src1, a->i, 32 - a->i);
        } else if (u) {
            tcg_gen_extract_tl(dst, src1, a->i, 32 - a->i);
        } else {
            tcg_gen_sextract_tl(dst, src1, a->i, 32 - a->i);
        }
    }
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(SLL_i, ALL, do_shift_i, a, true, true)
TRANS(SRL_i, ALL, do_shift_i, a, false, true)
TRANS(SRA_i, ALL, do_shift_i, a, false, false)

static TCGv gen_rs2_or_imm(DisasContext *dc, bool imm, int rs2_or_imm)
{
    /* For simplicity, we under-decoded the rs2 form. */
    if (!imm && rs2_or_imm & ~0x1f) {
        return NULL;
    }
    if (imm || rs2_or_imm == 0) {
        return tcg_constant_tl(rs2_or_imm);
    } else {
        return cpu_regs[rs2_or_imm];
    }
}

static bool do_mov_cond(DisasContext *dc, DisasCompare *cmp, int rd, TCGv src2)
{
    TCGv dst = gen_load_gpr(dc, rd);
    TCGv c2 = tcg_constant_tl(cmp->c2);

    tcg_gen_movcond_tl(cmp->cond, dst, cmp->c1, c2, src2, dst);
    gen_store_gpr(dc, rd, dst);
    return advance_pc(dc);
}

static bool trans_MOVcc(DisasContext *dc, arg_MOVcc *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    gen_compare(&cmp, a->cc, a->cond, dc);
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool trans_MOVfcc(DisasContext *dc, arg_MOVfcc *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    gen_fcompare(&cmp, a->cc, a->cond);
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool trans_MOVR(DisasContext *dc, arg_MOVR *a)
{
    TCGv src2 = gen_rs2_or_imm(dc, a->imm, a->rs2_or_imm);
    DisasCompare cmp;

    if (src2 == NULL) {
        return false;
    }
    if (!gen_compare_reg(&cmp, a->cond, gen_load_gpr(dc, a->rs1))) {
        return false;
    }
    return do_mov_cond(dc, &cmp, a->rd, src2);
}

static bool do_add_special(DisasContext *dc, arg_r_r_ri *a,
                           bool (*func)(DisasContext *dc, int rd, TCGv src))
{
    TCGv src1, sum;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!a->imm && a->rs2_or_imm & ~0x1f) {
        return false;
    }

    /*
     * Always load the sum into a new temporary.
     * This is required to capture the value across a window change,
     * e.g. SAVE and RESTORE, and may be optimized away otherwise.
     */
    sum = tcg_temp_new();
    src1 = gen_load_gpr(dc, a->rs1);
    if (a->imm || a->rs2_or_imm == 0) {
        tcg_gen_addi_tl(sum, src1, a->rs2_or_imm);
    } else {
        tcg_gen_add_tl(sum, src1, cpu_regs[a->rs2_or_imm]);
    }
    return func(dc, a->rd, sum);
}

static bool do_jmpl(DisasContext *dc, int rd, TCGv src)
{
    /*
     * Preserve pc across advance, so that we can delay
     * the writeback to rd until after src is consumed.
     */
    target_ulong cur_pc = dc->pc;

    gen_check_align(dc, src, 3);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_address_mask(dc, cpu_npc);
    gen_store_gpr(dc, rd, tcg_constant_tl(cur_pc));

    dc->npc = DYNAMIC_PC_LOOKUP;
    return true;
}

TRANS(JMPL, ALL, do_add_special, a, do_jmpl)

static bool do_rett(DisasContext *dc, int rd, TCGv src)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }

    gen_check_align(dc, src, 3);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_helper_rett(tcg_env);

    dc->npc = DYNAMIC_PC;
    return true;
}

TRANS(RETT, 32, do_add_special, a, do_rett)

static bool do_return(DisasContext *dc, int rd, TCGv src)
{
    gen_check_align(dc, src, 3);
    gen_helper_restore(tcg_env);

    gen_mov_pc_npc(dc);
    tcg_gen_mov_tl(cpu_npc, src);
    gen_address_mask(dc, cpu_npc);

    dc->npc = DYNAMIC_PC_LOOKUP;
    return true;
}

TRANS(RETURN, 64, do_add_special, a, do_return)

static bool do_save(DisasContext *dc, int rd, TCGv src)
{
    gen_helper_save(tcg_env);
    gen_store_gpr(dc, rd, src);
    return advance_pc(dc);
}

TRANS(SAVE, ALL, do_add_special, a, do_save)

static bool do_restore(DisasContext *dc, int rd, TCGv src)
{
    gen_helper_restore(tcg_env);
    gen_store_gpr(dc, rd, src);
    return advance_pc(dc);
}

TRANS(RESTORE, ALL, do_add_special, a, do_restore)

static bool do_done_retry(DisasContext *dc, bool done)
{
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }
    dc->npc = DYNAMIC_PC;
    dc->pc = DYNAMIC_PC;
    translator_io_start(&dc->base);
    if (done) {
        gen_helper_done(tcg_env);
    } else {
        gen_helper_retry(tcg_env);
    }
    return true;
}

TRANS(DONE, 64, do_done_retry, true)
TRANS(RETRY, 64, do_done_retry, false)

/*
 * Major opcode 11 -- load and store instructions
 */

static TCGv gen_ldst_addr(DisasContext *dc, int rs1, bool imm, int rs2_or_imm)
{
    TCGv addr, tmp = NULL;

    /* For simplicity, we under-decoded the rs2 form. */
    if (!imm && rs2_or_imm & ~0x1f) {
        return NULL;
    }

    addr = gen_load_gpr(dc, rs1);
    if (rs2_or_imm) {
        tmp = tcg_temp_new();
        if (imm) {
            tcg_gen_addi_tl(tmp, addr, rs2_or_imm);
        } else {
            tcg_gen_add_tl(tmp, addr, cpu_regs[rs2_or_imm]);
        }
        addr = tmp;
    }
    if (AM_CHECK(dc)) {
        if (!tmp) {
            tmp = tcg_temp_new();
        }
        tcg_gen_ext32u_tl(tmp, addr);
        addr = tmp;
    }
    return addr;
}

static bool do_ld_gpr(DisasContext *dc, arg_r_r_ri_asi *a, MemOp mop)
{
    TCGv reg, addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    DisasASI da;

    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, mop);

    reg = gen_dest_gpr(dc, a->rd);
    gen_ld_asi(dc, &da, reg, addr);
    gen_store_gpr(dc, a->rd, reg);
    return advance_pc(dc);
}

TRANS(LDUW, ALL, do_ld_gpr, a, MO_TEUL)
TRANS(LDUB, ALL, do_ld_gpr, a, MO_UB)
TRANS(LDUH, ALL, do_ld_gpr, a, MO_TEUW)
TRANS(LDSB, ALL, do_ld_gpr, a, MO_SB)
TRANS(LDSH, ALL, do_ld_gpr, a, MO_TESW)
TRANS(LDSW, 64, do_ld_gpr, a, MO_TESL)
TRANS(LDX, 64, do_ld_gpr, a, MO_TEUQ)

static bool do_st_gpr(DisasContext *dc, arg_r_r_ri_asi *a, MemOp mop)
{
    TCGv reg, addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    DisasASI da;

    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, mop);

    reg = gen_load_gpr(dc, a->rd);
    gen_st_asi(dc, &da, reg, addr);
    return advance_pc(dc);
}

TRANS(STW, ALL, do_st_gpr, a, MO_TEUL)
TRANS(STB, ALL, do_st_gpr, a, MO_UB)
TRANS(STH, ALL, do_st_gpr, a, MO_TEUW)
TRANS(STX, 64, do_st_gpr, a, MO_TEUQ)

static bool trans_LDD(DisasContext *dc, arg_r_r_ri_asi *a)
{
    TCGv addr;
    DisasASI da;

    if (a->rd & 1) {
        return false;
    }
    addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, MO_TEUQ);
    gen_ldda_asi(dc, &da, addr, a->rd);
    return advance_pc(dc);
}

static bool trans_STD(DisasContext *dc, arg_r_r_ri_asi *a)
{
    TCGv addr;
    DisasASI da;

    if (a->rd & 1) {
        return false;
    }
    addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, MO_TEUQ);
    gen_stda_asi(dc, &da, addr, a->rd);
    return advance_pc(dc);
}

static bool trans_LDSTUB(DisasContext *dc, arg_r_r_ri_asi *a)
{
    TCGv addr, reg;
    DisasASI da;

    addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, MO_UB);

    reg = gen_dest_gpr(dc, a->rd);
    gen_ldstub_asi(dc, &da, reg, addr);
    gen_store_gpr(dc, a->rd, reg);
    return advance_pc(dc);
}

static bool trans_SWAP(DisasContext *dc, arg_r_r_ri_asi *a)
{
    TCGv addr, dst, src;
    DisasASI da;

    addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, MO_TEUL);

    dst = gen_dest_gpr(dc, a->rd);
    src = gen_load_gpr(dc, a->rd);
    gen_swap_asi(dc, &da, dst, src, addr);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool do_casa(DisasContext *dc, arg_r_r_ri_asi *a, MemOp mop)
{
    TCGv addr, o, n, c;
    DisasASI da;

    addr = gen_ldst_addr(dc, a->rs1, true, 0);
    if (addr == NULL) {
        return false;
    }
    da = resolve_asi(dc, a->asi, mop);

    o = gen_dest_gpr(dc, a->rd);
    n = gen_load_gpr(dc, a->rd);
    c = gen_load_gpr(dc, a->rs2_or_imm);
    gen_cas_asi(dc, &da, o, n, c, addr);
    gen_store_gpr(dc, a->rd, o);
    return advance_pc(dc);
}

TRANS(CASA, CASA, do_casa, a, MO_TEUL)
TRANS(CASXA, 64, do_casa, a, MO_TEUQ)

static bool do_ld_fpr(DisasContext *dc, arg_r_r_ri_asi *a, MemOp sz)
{
    TCGv addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    DisasASI da;

    if (addr == NULL) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (sz == MO_128 && gen_trap_float128(dc)) {
        return true;
    }
    da = resolve_asi(dc, a->asi, MO_TE | sz);
    gen_ldf_asi(dc, &da, sz, addr, a->rd);
    gen_update_fprs_dirty(dc, a->rd);
    return advance_pc(dc);
}

TRANS(LDF, ALL, do_ld_fpr, a, MO_32)
TRANS(LDDF, ALL, do_ld_fpr, a, MO_64)
TRANS(LDQF, ALL, do_ld_fpr, a, MO_128)

TRANS(LDFA, 64, do_ld_fpr, a, MO_32)
TRANS(LDDFA, 64, do_ld_fpr, a, MO_64)
TRANS(LDQFA, 64, do_ld_fpr, a, MO_128)

static bool do_st_fpr(DisasContext *dc, arg_r_r_ri_asi *a, MemOp sz)
{
    TCGv addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    DisasASI da;

    if (addr == NULL) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (sz == MO_128 && gen_trap_float128(dc)) {
        return true;
    }
    da = resolve_asi(dc, a->asi, MO_TE | sz);
    gen_stf_asi(dc, &da, sz, addr, a->rd);
    return advance_pc(dc);
}

TRANS(STF, ALL, do_st_fpr, a, MO_32)
TRANS(STDF, ALL, do_st_fpr, a, MO_64)
TRANS(STQF, ALL, do_st_fpr, a, MO_128)

TRANS(STFA, 64, do_st_fpr, a, MO_32)
TRANS(STDFA, 64, do_st_fpr, a, MO_64)
TRANS(STQFA, 64, do_st_fpr, a, MO_128)

static bool trans_STDFQ(DisasContext *dc, arg_STDFQ *a)
{
    if (!avail_32(dc)) {
        return false;
    }
    if (!supervisor(dc)) {
        return raise_priv(dc);
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    gen_op_fpexception_im(dc, FSR_FTT_SEQ_ERROR);
    return true;
}

static bool do_ldfsr(DisasContext *dc, arg_r_r_ri *a, MemOp mop,
                     target_ulong new_mask, target_ulong old_mask)
{
    TCGv tmp, addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    tmp = tcg_temp_new();
    tcg_gen_qemu_ld_tl(tmp, addr, dc->mem_idx, mop | MO_ALIGN);
    tcg_gen_andi_tl(tmp, tmp, new_mask);
    tcg_gen_andi_tl(cpu_fsr, cpu_fsr, old_mask);
    tcg_gen_or_tl(cpu_fsr, cpu_fsr, tmp);
    gen_helper_set_fsr(tcg_env, cpu_fsr);
    return advance_pc(dc);
}

TRANS(LDFSR, ALL, do_ldfsr, a, MO_TEUL, FSR_LDFSR_MASK, FSR_LDFSR_OLDMASK)
TRANS(LDXFSR, 64, do_ldfsr, a, MO_TEUQ, FSR_LDXFSR_MASK, FSR_LDXFSR_OLDMASK)

static bool do_stfsr(DisasContext *dc, arg_r_r_ri *a, MemOp mop)
{
    TCGv addr = gen_ldst_addr(dc, a->rs1, a->imm, a->rs2_or_imm);
    if (addr == NULL) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    tcg_gen_qemu_st_tl(cpu_fsr, addr, dc->mem_idx, mop | MO_ALIGN);
    return advance_pc(dc);
}

TRANS(STFSR, ALL, do_stfsr, a, MO_TEUL)
TRANS(STXFSR, 64, do_stfsr, a, MO_TEUQ)

static bool do_fc(DisasContext *dc, int rd, bool c)
{
    uint64_t mask;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    if (rd & 1) {
        mask = MAKE_64BIT_MASK(0, 32);
    } else {
        mask = MAKE_64BIT_MASK(32, 32);
    }
    if (c) {
        tcg_gen_ori_i64(cpu_fpr[rd / 2], cpu_fpr[rd / 2], mask);
    } else {
        tcg_gen_andi_i64(cpu_fpr[rd / 2], cpu_fpr[rd / 2], ~mask);
    }
    gen_update_fprs_dirty(dc, rd);
    return advance_pc(dc);
}

TRANS(FZEROs, VIS1, do_fc, a->rd, 0)
TRANS(FONEs, VIS1, do_fc, a->rd, 1)

static bool do_dc(DisasContext *dc, int rd, int64_t c)
{
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    tcg_gen_movi_i64(cpu_fpr[rd / 2], c);
    gen_update_fprs_dirty(dc, rd);
    return advance_pc(dc);
}

TRANS(FZEROd, VIS1, do_dc, a->rd, 0)
TRANS(FONEd, VIS1, do_dc, a->rd, -1)

static bool do_ff(DisasContext *dc, arg_r_r *a,
                  void (*func)(TCGv_i32, TCGv_i32))
{
    TCGv_i32 tmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    tmp = gen_load_fpr_F(dc, a->rs);
    func(tmp, tmp);
    gen_store_fpr_F(dc, a->rd, tmp);
    return advance_pc(dc);
}

TRANS(FMOVs, ALL, do_ff, a, gen_op_fmovs)
TRANS(FNEGs, ALL, do_ff, a, gen_op_fnegs)
TRANS(FABSs, ALL, do_ff, a, gen_op_fabss)
TRANS(FSRCs, VIS1, do_ff, a, tcg_gen_mov_i32)
TRANS(FNOTs, VIS1, do_ff, a, tcg_gen_not_i32)

static bool do_fd(DisasContext *dc, arg_r_r *a,
                  void (*func)(TCGv_i32, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    dst = gen_dest_fpr_F(dc);
    src = gen_load_fpr_D(dc, a->rs);
    func(dst, src);
    gen_store_fpr_F(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FPACK16, VIS1, do_fd, a, gen_op_fpack16)
TRANS(FPACKFIX, VIS1, do_fd, a, gen_op_fpackfix)

static bool do_env_ff(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i32, TCGv_env, TCGv_i32))
{
    TCGv_i32 tmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    tmp = gen_load_fpr_F(dc, a->rs);
    func(tmp, tcg_env, tmp);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_F(dc, a->rd, tmp);
    return advance_pc(dc);
}

TRANS(FSQRTs, ALL, do_env_ff, a, gen_helper_fsqrts)
TRANS(FiTOs, ALL, do_env_ff, a, gen_helper_fitos)
TRANS(FsTOi, ALL, do_env_ff, a, gen_helper_fstoi)

static bool do_env_fd(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i32, TCGv_env, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    dst = gen_dest_fpr_F(dc);
    src = gen_load_fpr_D(dc, a->rs);
    func(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_F(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FdTOs, ALL, do_env_fd, a, gen_helper_fdtos)
TRANS(FdTOi, ALL, do_env_fd, a, gen_helper_fdtoi)
TRANS(FxTOs, 64, do_env_fd, a, gen_helper_fxtos)

static bool do_dd(DisasContext *dc, arg_r_r *a,
                  void (*func)(TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    dst = gen_dest_fpr_D(dc, a->rd);
    src = gen_load_fpr_D(dc, a->rs);
    func(dst, src);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FMOVd, 64, do_dd, a, gen_op_fmovd)
TRANS(FNEGd, 64, do_dd, a, gen_op_fnegd)
TRANS(FABSd, 64, do_dd, a, gen_op_fabsd)
TRANS(FSRCd, VIS1, do_dd, a, tcg_gen_mov_i64)
TRANS(FNOTd, VIS1, do_dd, a, tcg_gen_not_i64)

static bool do_env_dd(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i64, TCGv_env, TCGv_i64))
{
    TCGv_i64 dst, src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    dst = gen_dest_fpr_D(dc, a->rd);
    src = gen_load_fpr_D(dc, a->rs);
    func(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FSQRTd, ALL, do_env_dd, a, gen_helper_fsqrtd)
TRANS(FxTOd, 64, do_env_dd, a, gen_helper_fxtod)
TRANS(FdTOx, 64, do_env_dd, a, gen_helper_fdtox)

static bool do_env_df(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i64, TCGv_env, TCGv_i32))
{
    TCGv_i64 dst;
    TCGv_i32 src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    dst = gen_dest_fpr_D(dc, a->rd);
    src = gen_load_fpr_F(dc, a->rs);
    func(dst, tcg_env, src);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FiTOd, ALL, do_env_df, a, gen_helper_fitod)
TRANS(FsTOd, ALL, do_env_df, a, gen_helper_fstod)
TRANS(FsTOx, 64, do_env_df, a, gen_helper_fstox)

static bool trans_FMOVq(DisasContext *dc, arg_FMOVq *a)
{
    int rd, rs;

    if (!avail_64(dc)) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    rd = QFPREG(a->rd);
    rs = QFPREG(a->rs);
    tcg_gen_mov_i64(cpu_fpr[rd / 2], cpu_fpr[rs / 2]);
    tcg_gen_mov_i64(cpu_fpr[rd / 2 + 1], cpu_fpr[rs / 2 + 1]);
    gen_update_fprs_dirty(dc, rd);
    return advance_pc(dc);
}

static bool do_qq(DisasContext *dc, arg_r_r *a,
                  void (*func)(TCGv_env))
{
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT1(QFPREG(a->rs));
    func(tcg_env);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

TRANS(FNEGq, 64, do_qq, a, gen_helper_fnegq)
TRANS(FABSq, 64, do_qq, a, gen_helper_fabsq)

static bool do_env_qq(DisasContext *dc, arg_r_r *a,
                       void (*func)(TCGv_env))
{
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT1(QFPREG(a->rs));
    func(tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

TRANS(FSQRTq, ALL, do_env_qq, a, gen_helper_fsqrtq)

static bool do_env_fq(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i32, TCGv_env))
{
    TCGv_i32 dst;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT1(QFPREG(a->rs));
    dst = gen_dest_fpr_F(dc);
    func(dst, tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_F(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FqTOs, ALL, do_env_fq, a, gen_helper_fqtos)
TRANS(FqTOi, ALL, do_env_fq, a, gen_helper_fqtoi)

static bool do_env_dq(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_i64, TCGv_env))
{
    TCGv_i64 dst;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT1(QFPREG(a->rs));
    dst = gen_dest_fpr_D(dc, a->rd);
    func(dst, tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FqTOd, ALL, do_env_dq, a, gen_helper_fqtod)
TRANS(FqTOx, 64, do_env_dq, a, gen_helper_fqtox)

static bool do_env_qf(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_env, TCGv_i32))
{
    TCGv_i32 src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src = gen_load_fpr_F(dc, a->rs);
    func(tcg_env, src);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

TRANS(FiTOq, ALL, do_env_qf, a, gen_helper_fitoq)
TRANS(FsTOq, ALL, do_env_qf, a, gen_helper_fstoq)

static bool do_env_qd(DisasContext *dc, arg_r_r *a,
                      void (*func)(TCGv_env, TCGv_i64))
{
    TCGv_i64 src;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src = gen_load_fpr_D(dc, a->rs);
    func(tcg_env, src);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

TRANS(FdTOq, ALL, do_env_qd, a, gen_helper_fdtoq)
TRANS(FxTOq, 64, do_env_qd, a, gen_helper_fxtoq)

static bool do_fff(DisasContext *dc, arg_r_r_r *a,
                   void (*func)(TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    src1 = gen_load_fpr_F(dc, a->rs1);
    src2 = gen_load_fpr_F(dc, a->rs2);
    func(src1, src1, src2);
    gen_store_fpr_F(dc, a->rd, src1);
    return advance_pc(dc);
}

TRANS(FPADD16s, VIS1, do_fff, a, tcg_gen_vec_add16_i32)
TRANS(FPADD32s, VIS1, do_fff, a, tcg_gen_add_i32)
TRANS(FPSUB16s, VIS1, do_fff, a, tcg_gen_vec_sub16_i32)
TRANS(FPSUB32s, VIS1, do_fff, a, tcg_gen_sub_i32)
TRANS(FNORs, VIS1, do_fff, a, tcg_gen_nor_i32)
TRANS(FANDNOTs, VIS1, do_fff, a, tcg_gen_andc_i32)
TRANS(FXORs, VIS1, do_fff, a, tcg_gen_xor_i32)
TRANS(FNANDs, VIS1, do_fff, a, tcg_gen_nand_i32)
TRANS(FANDs, VIS1, do_fff, a, tcg_gen_and_i32)
TRANS(FXNORs, VIS1, do_fff, a, tcg_gen_eqv_i32)
TRANS(FORNOTs, VIS1, do_fff, a, tcg_gen_orc_i32)
TRANS(FORs, VIS1, do_fff, a, tcg_gen_or_i32)

static bool do_env_fff(DisasContext *dc, arg_r_r_r *a,
                       void (*func)(TCGv_i32, TCGv_env, TCGv_i32, TCGv_i32))
{
    TCGv_i32 src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src1 = gen_load_fpr_F(dc, a->rs1);
    src2 = gen_load_fpr_F(dc, a->rs2);
    func(src1, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_F(dc, a->rd, src1);
    return advance_pc(dc);
}

TRANS(FADDs, ALL, do_env_fff, a, gen_helper_fadds)
TRANS(FSUBs, ALL, do_env_fff, a, gen_helper_fsubs)
TRANS(FMULs, ALL, do_env_fff, a, gen_helper_fmuls)
TRANS(FDIVs, ALL, do_env_fff, a, gen_helper_fdivs)

static bool do_ddd(DisasContext *dc, arg_r_r_r *a,
                   void (*func)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    dst = gen_dest_fpr_D(dc, a->rd);
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    func(dst, src1, src2);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FMUL8x16, VIS1, do_ddd, a, gen_helper_fmul8x16)
TRANS(FMUL8x16AU, VIS1, do_ddd, a, gen_helper_fmul8x16au)
TRANS(FMUL8x16AL, VIS1, do_ddd, a, gen_helper_fmul8x16al)
TRANS(FMUL8SUx16, VIS1, do_ddd, a, gen_helper_fmul8sux16)
TRANS(FMUL8ULx16, VIS1, do_ddd, a, gen_helper_fmul8ulx16)
TRANS(FMULD8SUx16, VIS1, do_ddd, a, gen_helper_fmuld8sux16)
TRANS(FMULD8ULx16, VIS1, do_ddd, a, gen_helper_fmuld8ulx16)
TRANS(FPMERGE, VIS1, do_ddd, a, gen_helper_fpmerge)
TRANS(FEXPAND, VIS1, do_ddd, a, gen_helper_fexpand)

TRANS(FPADD16, VIS1, do_ddd, a, tcg_gen_vec_add16_i64)
TRANS(FPADD32, VIS1, do_ddd, a, tcg_gen_vec_add32_i64)
TRANS(FPSUB16, VIS1, do_ddd, a, tcg_gen_vec_sub16_i64)
TRANS(FPSUB32, VIS1, do_ddd, a, tcg_gen_vec_sub32_i64)
TRANS(FNORd, VIS1, do_ddd, a, tcg_gen_nor_i64)
TRANS(FANDNOTd, VIS1, do_ddd, a, tcg_gen_andc_i64)
TRANS(FXORd, VIS1, do_ddd, a, tcg_gen_xor_i64)
TRANS(FNANDd, VIS1, do_ddd, a, tcg_gen_nand_i64)
TRANS(FANDd, VIS1, do_ddd, a, tcg_gen_and_i64)
TRANS(FXNORd, VIS1, do_ddd, a, tcg_gen_eqv_i64)
TRANS(FORNOTd, VIS1, do_ddd, a, tcg_gen_orc_i64)
TRANS(FORd, VIS1, do_ddd, a, tcg_gen_or_i64)

TRANS(FPACK32, VIS1, do_ddd, a, gen_op_fpack32)
TRANS(FALIGNDATAg, VIS1, do_ddd, a, gen_op_faligndata)
TRANS(BSHUFFLE, VIS2, do_ddd, a, gen_op_bshuffle)

static bool do_rdd(DisasContext *dc, arg_r_r_r *a,
                   void (*func)(TCGv, TCGv_i64, TCGv_i64))
{
    TCGv_i64 src1, src2;
    TCGv dst;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    dst = gen_dest_gpr(dc, a->rd);
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    func(dst, src1, src2);
    gen_store_gpr(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FPCMPLE16, VIS1, do_rdd, a, gen_helper_fcmple16)
TRANS(FPCMPNE16, VIS1, do_rdd, a, gen_helper_fcmpne16)
TRANS(FPCMPGT16, VIS1, do_rdd, a, gen_helper_fcmpgt16)
TRANS(FPCMPEQ16, VIS1, do_rdd, a, gen_helper_fcmpeq16)

TRANS(FPCMPLE32, VIS1, do_rdd, a, gen_helper_fcmple32)
TRANS(FPCMPNE32, VIS1, do_rdd, a, gen_helper_fcmpne32)
TRANS(FPCMPGT32, VIS1, do_rdd, a, gen_helper_fcmpgt32)
TRANS(FPCMPEQ32, VIS1, do_rdd, a, gen_helper_fcmpeq32)

static bool do_env_ddd(DisasContext *dc, arg_r_r_r *a,
                       void (*func)(TCGv_i64, TCGv_env, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    dst = gen_dest_fpr_D(dc, a->rd);
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    func(dst, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(FADDd, ALL, do_env_ddd, a, gen_helper_faddd)
TRANS(FSUBd, ALL, do_env_ddd, a, gen_helper_fsubd)
TRANS(FMULd, ALL, do_env_ddd, a, gen_helper_fmuld)
TRANS(FDIVd, ALL, do_env_ddd, a, gen_helper_fdivd)

static bool trans_FsMULd(DisasContext *dc, arg_r_r_r *a)
{
    TCGv_i64 dst;
    TCGv_i32 src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (!(dc->def->features & CPU_FEATURE_FSMULD)) {
        return raise_unimpfpop(dc);
    }

    gen_op_clear_ieee_excp_and_FTT();
    dst = gen_dest_fpr_D(dc, a->rd);
    src1 = gen_load_fpr_F(dc, a->rs1);
    src2 = gen_load_fpr_F(dc, a->rs2);
    gen_helper_fsmuld(dst, tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

static bool do_dddd(DisasContext *dc, arg_r_r_r *a,
                    void (*func)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 dst, src0, src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    dst  = gen_dest_fpr_D(dc, a->rd);
    src0 = gen_load_fpr_D(dc, a->rd);
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    func(dst, src0, src1, src2);
    gen_store_fpr_D(dc, a->rd, dst);
    return advance_pc(dc);
}

TRANS(PDIST, VIS1, do_dddd, a, gen_helper_pdist)

static bool do_env_qqq(DisasContext *dc, arg_r_r_r *a,
                       void (*func)(TCGv_env))
{
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT0(QFPREG(a->rs1));
    gen_op_load_fpr_QT1(QFPREG(a->rs2));
    func(tcg_env);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

TRANS(FADDq, ALL, do_env_qqq, a, gen_helper_faddq)
TRANS(FSUBq, ALL, do_env_qqq, a, gen_helper_fsubq)
TRANS(FMULq, ALL, do_env_qqq, a, gen_helper_fmulq)
TRANS(FDIVq, ALL, do_env_qqq, a, gen_helper_fdivq)

static bool trans_FdMULq(DisasContext *dc, arg_r_r_r *a)
{
    TCGv_i64 src1, src2;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    gen_helper_fdmulq(tcg_env, src1, src2);
    gen_helper_check_ieee_exceptions(cpu_fsr, tcg_env);
    gen_op_store_QT0_fpr(QFPREG(a->rd));
    gen_update_fprs_dirty(dc, QFPREG(a->rd));
    return advance_pc(dc);
}

static bool do_fmovr(DisasContext *dc, arg_FMOVRs *a, bool is_128,
                     void (*func)(DisasContext *, DisasCompare *, int, int))
{
    DisasCompare cmp;

    if (!gen_compare_reg(&cmp, a->cond, gen_load_gpr(dc, a->rs1))) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (is_128 && gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    func(dc, &cmp, a->rd, a->rs2);
    return advance_pc(dc);
}

TRANS(FMOVRs, 64, do_fmovr, a, false, gen_fmovs)
TRANS(FMOVRd, 64, do_fmovr, a, false, gen_fmovd)
TRANS(FMOVRq, 64, do_fmovr, a, true, gen_fmovq)

static bool do_fmovcc(DisasContext *dc, arg_FMOVscc *a, bool is_128,
                      void (*func)(DisasContext *, DisasCompare *, int, int))
{
    DisasCompare cmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (is_128 && gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_compare(&cmp, a->cc, a->cond, dc);
    func(dc, &cmp, a->rd, a->rs2);
    return advance_pc(dc);
}

TRANS(FMOVscc, 64, do_fmovcc, a, false, gen_fmovs)
TRANS(FMOVdcc, 64, do_fmovcc, a, false, gen_fmovd)
TRANS(FMOVqcc, 64, do_fmovcc, a, true, gen_fmovq)

static bool do_fmovfcc(DisasContext *dc, arg_FMOVsfcc *a, bool is_128,
                       void (*func)(DisasContext *, DisasCompare *, int, int))
{
    DisasCompare cmp;

    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (is_128 && gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_fcompare(&cmp, a->cc, a->cond);
    func(dc, &cmp, a->rd, a->rs2);
    return advance_pc(dc);
}

TRANS(FMOVsfcc, 64, do_fmovfcc, a, false, gen_fmovs)
TRANS(FMOVdfcc, 64, do_fmovfcc, a, false, gen_fmovd)
TRANS(FMOVqfcc, 64, do_fmovfcc, a, true, gen_fmovq)

static bool do_fcmps(DisasContext *dc, arg_FCMPs *a, bool e)
{
    TCGv_i32 src1, src2;

    if (avail_32(dc) && a->cc != 0) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src1 = gen_load_fpr_F(dc, a->rs1);
    src2 = gen_load_fpr_F(dc, a->rs2);
    if (e) {
        gen_op_fcmpes(a->cc, src1, src2);
    } else {
        gen_op_fcmps(a->cc, src1, src2);
    }
    return advance_pc(dc);
}

TRANS(FCMPs, ALL, do_fcmps, a, false)
TRANS(FCMPEs, ALL, do_fcmps, a, true)

static bool do_fcmpd(DisasContext *dc, arg_FCMPd *a, bool e)
{
    TCGv_i64 src1, src2;

    if (avail_32(dc) && a->cc != 0) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    src1 = gen_load_fpr_D(dc, a->rs1);
    src2 = gen_load_fpr_D(dc, a->rs2);
    if (e) {
        gen_op_fcmped(a->cc, src1, src2);
    } else {
        gen_op_fcmpd(a->cc, src1, src2);
    }
    return advance_pc(dc);
}

TRANS(FCMPd, ALL, do_fcmpd, a, false)
TRANS(FCMPEd, ALL, do_fcmpd, a, true)

static bool do_fcmpq(DisasContext *dc, arg_FCMPq *a, bool e)
{
    if (avail_32(dc) && a->cc != 0) {
        return false;
    }
    if (gen_trap_ifnofpu(dc)) {
        return true;
    }
    if (gen_trap_float128(dc)) {
        return true;
    }

    gen_op_clear_ieee_excp_and_FTT();
    gen_op_load_fpr_QT0(QFPREG(a->rs1));
    gen_op_load_fpr_QT1(QFPREG(a->rs2));
    if (e) {
        gen_op_fcmpeq(a->cc);
    } else {
        gen_op_fcmpq(a->cc);
    }
    return advance_pc(dc);
}

TRANS(FCMPq, ALL, do_fcmpq, a, false)
TRANS(FCMPEq, ALL, do_fcmpq, a, true)

static void sparc_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cpu_env(cs);
    int bound;

    dc->pc = dc->base.pc_first;
    dc->npc = (target_ulong)dc->base.tb->cs_base;
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
    target_ulong npc = dc->npc;

    if (npc & 3) {
        switch (npc) {
        case JUMP_PC:
            assert(dc->jump_pc[1] == dc->pc + 4);
            npc = dc->jump_pc[0] | JUMP_PC;
            break;
        case DYNAMIC_PC:
        case DYNAMIC_PC_LOOKUP:
            npc = DYNAMIC_PC;
            break;
        default:
            g_assert_not_reached();
        }
    }
    tcg_gen_insn_start(dc->pc, npc);
}

static void sparc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUSPARCState *env = cpu_env(cs);
    unsigned int insn;

    insn = translator_ldl(env, &dc->base, dc->pc);
    dc->base.pc_next += 4;

    if (!decode(dc, insn)) {
        gen_exception(dc, TT_ILL_INSN);
    }

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
    DisasDelayException *e, *e_next;
    bool may_lookup;

    finishing_insn(dc);

    switch (dc->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        if (((dc->pc | dc->npc) & 3) == 0) {
            /* static PC and NPC: we can use direct chaining */
            gen_goto_tb(dc, 0, dc->pc, dc->npc);
            break;
        }

        may_lookup = true;
        if (dc->pc & 3) {
            switch (dc->pc) {
            case DYNAMIC_PC_LOOKUP:
                break;
            case DYNAMIC_PC:
                may_lookup = false;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            tcg_gen_movi_tl(cpu_pc, dc->pc);
        }

        if (dc->npc & 3) {
            switch (dc->npc) {
            case JUMP_PC:
                gen_generic_branch(dc);
                break;
            case DYNAMIC_PC:
                may_lookup = false;
                break;
            case DYNAMIC_PC_LOOKUP:
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            tcg_gen_movi_tl(cpu_npc, dc->npc);
        }
        if (may_lookup) {
            tcg_gen_lookup_and_goto_ptr();
        } else {
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

    for (e = dc->delay_excp_list; e ; e = e_next) {
        gen_set_label(e->lab);

        tcg_gen_movi_tl(cpu_pc, e->pc);
        if (e->npc % 4 == 0) {
            tcg_gen_movi_tl(cpu_npc, e->npc);
        }
        gen_helper_raise_exception(tcg_env, e->excp);

        e_next = e->next;
        g_free(e);
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

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int *max_insns,
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

    static const struct { TCGv *ptr; int off; const char *name; } rtl[] = {
#ifdef TARGET_SPARC64
        { &cpu_gsr, offsetof(CPUSPARCState, gsr), "gsr" },
        { &cpu_xcc_Z, offsetof(CPUSPARCState, xcc_Z), "xcc_Z" },
        { &cpu_xcc_C, offsetof(CPUSPARCState, xcc_C), "xcc_C" },
#endif
        { &cpu_cc_N, offsetof(CPUSPARCState, cc_N), "cc_N" },
        { &cpu_cc_V, offsetof(CPUSPARCState, cc_V), "cc_V" },
        { &cpu_icc_Z, offsetof(CPUSPARCState, icc_Z), "icc_Z" },
        { &cpu_icc_C, offsetof(CPUSPARCState, icc_C), "icc_C" },
        { &cpu_cond, offsetof(CPUSPARCState, cond), "cond" },
        { &cpu_fsr, offsetof(CPUSPARCState, fsr), "fsr" },
        { &cpu_pc, offsetof(CPUSPARCState, pc), "pc" },
        { &cpu_npc, offsetof(CPUSPARCState, npc), "npc" },
        { &cpu_y, offsetof(CPUSPARCState, y), "y" },
        { &cpu_tbr, offsetof(CPUSPARCState, tbr), "tbr" },
    };

    unsigned int i;

    cpu_regwptr = tcg_global_mem_new_ptr(tcg_env,
                                         offsetof(CPUSPARCState, regwptr),
                                         "regwptr");

    for (i = 0; i < ARRAY_SIZE(rtl); ++i) {
        *rtl[i].ptr = tcg_global_mem_new(tcg_env, rtl[i].off, rtl[i].name);
    }

    cpu_regs[0] = NULL;
    for (i = 1; i < 8; ++i) {
        cpu_regs[i] = tcg_global_mem_new(tcg_env,
                                         offsetof(CPUSPARCState, gregs[i]),
                                         gregnames[i]);
    }

    for (i = 8; i < 32; ++i) {
        cpu_regs[i] = tcg_global_mem_new(cpu_regwptr,
                                         (i - 8) * sizeof(target_ulong),
                                         gregnames[i]);
    }

    for (i = 0; i < TARGET_DPREGS; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(tcg_env,
                                            offsetof(CPUSPARCState, fpr[i]),
                                            fregnames[i]);
    }

#ifdef TARGET_SPARC64
    cpu_fprs = tcg_global_mem_new_i32(tcg_env,
                                      offsetof(CPUSPARCState, fprs), "fprs");
#endif
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
