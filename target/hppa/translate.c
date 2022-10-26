/*
 * HPPA emulation cpu translation for qemu.
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/log.h"

/* Since we have a distinction between register size and address size,
   we need to redefine all of these.  */

#undef TCGv
#undef tcg_temp_new
#undef tcg_global_mem_new
#undef tcg_temp_local_new
#undef tcg_temp_free

#if TARGET_LONG_BITS == 64
#define TCGv_tl              TCGv_i64
#define tcg_temp_new_tl      tcg_temp_new_i64
#define tcg_temp_free_tl     tcg_temp_free_i64
#if TARGET_REGISTER_BITS == 64
#define tcg_gen_extu_reg_tl  tcg_gen_mov_i64
#else
#define tcg_gen_extu_reg_tl  tcg_gen_extu_i32_i64
#endif
#else
#define TCGv_tl              TCGv_i32
#define tcg_temp_new_tl      tcg_temp_new_i32
#define tcg_temp_free_tl     tcg_temp_free_i32
#define tcg_gen_extu_reg_tl  tcg_gen_mov_i32
#endif

#if TARGET_REGISTER_BITS == 64
#define TCGv_reg             TCGv_i64

#define tcg_temp_new         tcg_temp_new_i64
#define tcg_global_mem_new   tcg_global_mem_new_i64
#define tcg_temp_local_new   tcg_temp_local_new_i64
#define tcg_temp_free        tcg_temp_free_i64

#define tcg_gen_movi_reg     tcg_gen_movi_i64
#define tcg_gen_mov_reg      tcg_gen_mov_i64
#define tcg_gen_ld8u_reg     tcg_gen_ld8u_i64
#define tcg_gen_ld8s_reg     tcg_gen_ld8s_i64
#define tcg_gen_ld16u_reg    tcg_gen_ld16u_i64
#define tcg_gen_ld16s_reg    tcg_gen_ld16s_i64
#define tcg_gen_ld32u_reg    tcg_gen_ld32u_i64
#define tcg_gen_ld32s_reg    tcg_gen_ld32s_i64
#define tcg_gen_ld_reg       tcg_gen_ld_i64
#define tcg_gen_st8_reg      tcg_gen_st8_i64
#define tcg_gen_st16_reg     tcg_gen_st16_i64
#define tcg_gen_st32_reg     tcg_gen_st32_i64
#define tcg_gen_st_reg       tcg_gen_st_i64
#define tcg_gen_add_reg      tcg_gen_add_i64
#define tcg_gen_addi_reg     tcg_gen_addi_i64
#define tcg_gen_sub_reg      tcg_gen_sub_i64
#define tcg_gen_neg_reg      tcg_gen_neg_i64
#define tcg_gen_subfi_reg    tcg_gen_subfi_i64
#define tcg_gen_subi_reg     tcg_gen_subi_i64
#define tcg_gen_and_reg      tcg_gen_and_i64
#define tcg_gen_andi_reg     tcg_gen_andi_i64
#define tcg_gen_or_reg       tcg_gen_or_i64
#define tcg_gen_ori_reg      tcg_gen_ori_i64
#define tcg_gen_xor_reg      tcg_gen_xor_i64
#define tcg_gen_xori_reg     tcg_gen_xori_i64
#define tcg_gen_not_reg      tcg_gen_not_i64
#define tcg_gen_shl_reg      tcg_gen_shl_i64
#define tcg_gen_shli_reg     tcg_gen_shli_i64
#define tcg_gen_shr_reg      tcg_gen_shr_i64
#define tcg_gen_shri_reg     tcg_gen_shri_i64
#define tcg_gen_sar_reg      tcg_gen_sar_i64
#define tcg_gen_sari_reg     tcg_gen_sari_i64
#define tcg_gen_brcond_reg   tcg_gen_brcond_i64
#define tcg_gen_brcondi_reg  tcg_gen_brcondi_i64
#define tcg_gen_setcond_reg  tcg_gen_setcond_i64
#define tcg_gen_setcondi_reg tcg_gen_setcondi_i64
#define tcg_gen_mul_reg      tcg_gen_mul_i64
#define tcg_gen_muli_reg     tcg_gen_muli_i64
#define tcg_gen_div_reg      tcg_gen_div_i64
#define tcg_gen_rem_reg      tcg_gen_rem_i64
#define tcg_gen_divu_reg     tcg_gen_divu_i64
#define tcg_gen_remu_reg     tcg_gen_remu_i64
#define tcg_gen_discard_reg  tcg_gen_discard_i64
#define tcg_gen_trunc_reg_i32 tcg_gen_extrl_i64_i32
#define tcg_gen_trunc_i64_reg tcg_gen_mov_i64
#define tcg_gen_extu_i32_reg tcg_gen_extu_i32_i64
#define tcg_gen_ext_i32_reg  tcg_gen_ext_i32_i64
#define tcg_gen_extu_reg_i64 tcg_gen_mov_i64
#define tcg_gen_ext_reg_i64  tcg_gen_mov_i64
#define tcg_gen_ext8u_reg    tcg_gen_ext8u_i64
#define tcg_gen_ext8s_reg    tcg_gen_ext8s_i64
#define tcg_gen_ext16u_reg   tcg_gen_ext16u_i64
#define tcg_gen_ext16s_reg   tcg_gen_ext16s_i64
#define tcg_gen_ext32u_reg   tcg_gen_ext32u_i64
#define tcg_gen_ext32s_reg   tcg_gen_ext32s_i64
#define tcg_gen_bswap16_reg  tcg_gen_bswap16_i64
#define tcg_gen_bswap32_reg  tcg_gen_bswap32_i64
#define tcg_gen_bswap64_reg  tcg_gen_bswap64_i64
#define tcg_gen_concat_reg_i64 tcg_gen_concat32_i64
#define tcg_gen_andc_reg     tcg_gen_andc_i64
#define tcg_gen_eqv_reg      tcg_gen_eqv_i64
#define tcg_gen_nand_reg     tcg_gen_nand_i64
#define tcg_gen_nor_reg      tcg_gen_nor_i64
#define tcg_gen_orc_reg      tcg_gen_orc_i64
#define tcg_gen_clz_reg      tcg_gen_clz_i64
#define tcg_gen_ctz_reg      tcg_gen_ctz_i64
#define tcg_gen_clzi_reg     tcg_gen_clzi_i64
#define tcg_gen_ctzi_reg     tcg_gen_ctzi_i64
#define tcg_gen_clrsb_reg    tcg_gen_clrsb_i64
#define tcg_gen_ctpop_reg    tcg_gen_ctpop_i64
#define tcg_gen_rotl_reg     tcg_gen_rotl_i64
#define tcg_gen_rotli_reg    tcg_gen_rotli_i64
#define tcg_gen_rotr_reg     tcg_gen_rotr_i64
#define tcg_gen_rotri_reg    tcg_gen_rotri_i64
#define tcg_gen_deposit_reg  tcg_gen_deposit_i64
#define tcg_gen_deposit_z_reg tcg_gen_deposit_z_i64
#define tcg_gen_extract_reg  tcg_gen_extract_i64
#define tcg_gen_sextract_reg tcg_gen_sextract_i64
#define tcg_gen_extract2_reg tcg_gen_extract2_i64
#define tcg_const_reg        tcg_const_i64
#define tcg_const_local_reg  tcg_const_local_i64
#define tcg_constant_reg     tcg_constant_i64
#define tcg_gen_movcond_reg  tcg_gen_movcond_i64
#define tcg_gen_add2_reg     tcg_gen_add2_i64
#define tcg_gen_sub2_reg     tcg_gen_sub2_i64
#define tcg_gen_qemu_ld_reg  tcg_gen_qemu_ld_i64
#define tcg_gen_qemu_st_reg  tcg_gen_qemu_st_i64
#define tcg_gen_atomic_xchg_reg tcg_gen_atomic_xchg_i64
#define tcg_gen_trunc_reg_ptr   tcg_gen_trunc_i64_ptr
#else
#define TCGv_reg             TCGv_i32
#define tcg_temp_new         tcg_temp_new_i32
#define tcg_global_mem_new   tcg_global_mem_new_i32
#define tcg_temp_local_new   tcg_temp_local_new_i32
#define tcg_temp_free        tcg_temp_free_i32

#define tcg_gen_movi_reg     tcg_gen_movi_i32
#define tcg_gen_mov_reg      tcg_gen_mov_i32
#define tcg_gen_ld8u_reg     tcg_gen_ld8u_i32
#define tcg_gen_ld8s_reg     tcg_gen_ld8s_i32
#define tcg_gen_ld16u_reg    tcg_gen_ld16u_i32
#define tcg_gen_ld16s_reg    tcg_gen_ld16s_i32
#define tcg_gen_ld32u_reg    tcg_gen_ld_i32
#define tcg_gen_ld32s_reg    tcg_gen_ld_i32
#define tcg_gen_ld_reg       tcg_gen_ld_i32
#define tcg_gen_st8_reg      tcg_gen_st8_i32
#define tcg_gen_st16_reg     tcg_gen_st16_i32
#define tcg_gen_st32_reg     tcg_gen_st32_i32
#define tcg_gen_st_reg       tcg_gen_st_i32
#define tcg_gen_add_reg      tcg_gen_add_i32
#define tcg_gen_addi_reg     tcg_gen_addi_i32
#define tcg_gen_sub_reg      tcg_gen_sub_i32
#define tcg_gen_neg_reg      tcg_gen_neg_i32
#define tcg_gen_subfi_reg    tcg_gen_subfi_i32
#define tcg_gen_subi_reg     tcg_gen_subi_i32
#define tcg_gen_and_reg      tcg_gen_and_i32
#define tcg_gen_andi_reg     tcg_gen_andi_i32
#define tcg_gen_or_reg       tcg_gen_or_i32
#define tcg_gen_ori_reg      tcg_gen_ori_i32
#define tcg_gen_xor_reg      tcg_gen_xor_i32
#define tcg_gen_xori_reg     tcg_gen_xori_i32
#define tcg_gen_not_reg      tcg_gen_not_i32
#define tcg_gen_shl_reg      tcg_gen_shl_i32
#define tcg_gen_shli_reg     tcg_gen_shli_i32
#define tcg_gen_shr_reg      tcg_gen_shr_i32
#define tcg_gen_shri_reg     tcg_gen_shri_i32
#define tcg_gen_sar_reg      tcg_gen_sar_i32
#define tcg_gen_sari_reg     tcg_gen_sari_i32
#define tcg_gen_brcond_reg   tcg_gen_brcond_i32
#define tcg_gen_brcondi_reg  tcg_gen_brcondi_i32
#define tcg_gen_setcond_reg  tcg_gen_setcond_i32
#define tcg_gen_setcondi_reg tcg_gen_setcondi_i32
#define tcg_gen_mul_reg      tcg_gen_mul_i32
#define tcg_gen_muli_reg     tcg_gen_muli_i32
#define tcg_gen_div_reg      tcg_gen_div_i32
#define tcg_gen_rem_reg      tcg_gen_rem_i32
#define tcg_gen_divu_reg     tcg_gen_divu_i32
#define tcg_gen_remu_reg     tcg_gen_remu_i32
#define tcg_gen_discard_reg  tcg_gen_discard_i32
#define tcg_gen_trunc_reg_i32 tcg_gen_mov_i32
#define tcg_gen_trunc_i64_reg tcg_gen_extrl_i64_i32
#define tcg_gen_extu_i32_reg tcg_gen_mov_i32
#define tcg_gen_ext_i32_reg  tcg_gen_mov_i32
#define tcg_gen_extu_reg_i64 tcg_gen_extu_i32_i64
#define tcg_gen_ext_reg_i64  tcg_gen_ext_i32_i64
#define tcg_gen_ext8u_reg    tcg_gen_ext8u_i32
#define tcg_gen_ext8s_reg    tcg_gen_ext8s_i32
#define tcg_gen_ext16u_reg   tcg_gen_ext16u_i32
#define tcg_gen_ext16s_reg   tcg_gen_ext16s_i32
#define tcg_gen_ext32u_reg   tcg_gen_mov_i32
#define tcg_gen_ext32s_reg   tcg_gen_mov_i32
#define tcg_gen_bswap16_reg  tcg_gen_bswap16_i32
#define tcg_gen_bswap32_reg  tcg_gen_bswap32_i32
#define tcg_gen_concat_reg_i64 tcg_gen_concat_i32_i64
#define tcg_gen_andc_reg     tcg_gen_andc_i32
#define tcg_gen_eqv_reg      tcg_gen_eqv_i32
#define tcg_gen_nand_reg     tcg_gen_nand_i32
#define tcg_gen_nor_reg      tcg_gen_nor_i32
#define tcg_gen_orc_reg      tcg_gen_orc_i32
#define tcg_gen_clz_reg      tcg_gen_clz_i32
#define tcg_gen_ctz_reg      tcg_gen_ctz_i32
#define tcg_gen_clzi_reg     tcg_gen_clzi_i32
#define tcg_gen_ctzi_reg     tcg_gen_ctzi_i32
#define tcg_gen_clrsb_reg    tcg_gen_clrsb_i32
#define tcg_gen_ctpop_reg    tcg_gen_ctpop_i32
#define tcg_gen_rotl_reg     tcg_gen_rotl_i32
#define tcg_gen_rotli_reg    tcg_gen_rotli_i32
#define tcg_gen_rotr_reg     tcg_gen_rotr_i32
#define tcg_gen_rotri_reg    tcg_gen_rotri_i32
#define tcg_gen_deposit_reg  tcg_gen_deposit_i32
#define tcg_gen_deposit_z_reg tcg_gen_deposit_z_i32
#define tcg_gen_extract_reg  tcg_gen_extract_i32
#define tcg_gen_sextract_reg tcg_gen_sextract_i32
#define tcg_gen_extract2_reg tcg_gen_extract2_i32
#define tcg_const_reg        tcg_const_i32
#define tcg_const_local_reg  tcg_const_local_i32
#define tcg_constant_reg     tcg_constant_i32
#define tcg_gen_movcond_reg  tcg_gen_movcond_i32
#define tcg_gen_add2_reg     tcg_gen_add2_i32
#define tcg_gen_sub2_reg     tcg_gen_sub2_i32
#define tcg_gen_qemu_ld_reg  tcg_gen_qemu_ld_i32
#define tcg_gen_qemu_st_reg  tcg_gen_qemu_st_i32
#define tcg_gen_atomic_xchg_reg tcg_gen_atomic_xchg_i32
#define tcg_gen_trunc_reg_ptr   tcg_gen_ext_i32_ptr
#endif /* TARGET_REGISTER_BITS */

typedef struct DisasCond {
    TCGCond c;
    TCGv_reg a0, a1;
} DisasCond;

typedef struct DisasContext {
    DisasContextBase base;
    CPUState *cs;

    target_ureg iaoq_f;
    target_ureg iaoq_b;
    target_ureg iaoq_n;
    TCGv_reg iaoq_n_var;

    int ntempr, ntempl;
    TCGv_reg tempr[8];
    TCGv_tl  templ[4];

    DisasCond null_cond;
    TCGLabel *null_lab;

    uint32_t insn;
    uint32_t tb_flags;
    int mmu_idx;
    int privilege;
    bool psw_n_nonzero;

#ifdef CONFIG_USER_ONLY
    MemOp unalign;
#endif
} DisasContext;

#ifdef CONFIG_USER_ONLY
#define UNALIGN(C)  (C)->unalign
#else
#define UNALIGN(C)  0
#endif

/* Note that ssm/rsm instructions number PSW_W and PSW_E differently.  */
static int expand_sm_imm(DisasContext *ctx, int val)
{
    if (val & PSW_SM_E) {
        val = (val & ~PSW_SM_E) | PSW_E;
    }
    if (val & PSW_SM_W) {
        val = (val & ~PSW_SM_W) | PSW_W;
    }
    return val;
}

/* Inverted space register indicates 0 means sr0 not inferred from base.  */
static int expand_sr3x(DisasContext *ctx, int val)
{
    return ~val;
}

/* Convert the M:A bits within a memory insn to the tri-state value
   we use for the final M.  */
static int ma_to_m(DisasContext *ctx, int val)
{
    return val & 2 ? (val & 1 ? -1 : 1) : 0;
}

/* Convert the sign of the displacement to a pre or post-modify.  */
static int pos_to_m(DisasContext *ctx, int val)
{
    return val ? 1 : -1;
}

static int neg_to_m(DisasContext *ctx, int val)
{
    return val ? -1 : 1;
}

/* Used for branch targets and fp memory ops.  */
static int expand_shl2(DisasContext *ctx, int val)
{
    return val << 2;
}

/* Used for fp memory ops.  */
static int expand_shl3(DisasContext *ctx, int val)
{
    return val << 3;
}

/* Used for assemble_21.  */
static int expand_shl11(DisasContext *ctx, int val)
{
    return val << 11;
}


/* Include the auto-generated decoder.  */
#include "decode-insns.c.inc"

/* We are not using a goto_tb (for whatever reason), but have updated
   the iaq (for whatever reason), so don't do it again on exit.  */
#define DISAS_IAQ_N_UPDATED  DISAS_TARGET_0

/* We are exiting the TB, but have neither emitted a goto_tb, nor
   updated the iaq for the next instruction to be executed.  */
#define DISAS_IAQ_N_STALE    DISAS_TARGET_1

/* Similarly, but we want to return to the main loop immediately
   to recognize unmasked interrupts.  */
#define DISAS_IAQ_N_STALE_EXIT      DISAS_TARGET_2
#define DISAS_EXIT                  DISAS_TARGET_3

/* global register indexes */
static TCGv_reg cpu_gr[32];
static TCGv_i64 cpu_sr[4];
static TCGv_i64 cpu_srH;
static TCGv_reg cpu_iaoq_f;
static TCGv_reg cpu_iaoq_b;
static TCGv_i64 cpu_iasq_f;
static TCGv_i64 cpu_iasq_b;
static TCGv_reg cpu_sar;
static TCGv_reg cpu_psw_n;
static TCGv_reg cpu_psw_v;
static TCGv_reg cpu_psw_cb;
static TCGv_reg cpu_psw_cb_msb;

#include "exec/gen-icount.h"

void hppa_translate_init(void)
{
#define DEF_VAR(V)  { &cpu_##V, #V, offsetof(CPUHPPAState, V) }

    typedef struct { TCGv_reg *var; const char *name; int ofs; } GlobalVar;
    static const GlobalVar vars[] = {
        { &cpu_sar, "sar", offsetof(CPUHPPAState, cr[CR_SAR]) },
        DEF_VAR(psw_n),
        DEF_VAR(psw_v),
        DEF_VAR(psw_cb),
        DEF_VAR(psw_cb_msb),
        DEF_VAR(iaoq_f),
        DEF_VAR(iaoq_b),
    };

#undef DEF_VAR

    /* Use the symbolic register names that match the disassembler.  */
    static const char gr_names[32][4] = {
        "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
        "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
        "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31"
    };
    /* SR[4-7] are not global registers so that we can index them.  */
    static const char sr_names[5][4] = {
        "sr0", "sr1", "sr2", "sr3", "srH"
    };

    int i;

    cpu_gr[0] = NULL;
    for (i = 1; i < 32; i++) {
        cpu_gr[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUHPPAState, gr[i]),
                                       gr_names[i]);
    }
    for (i = 0; i < 4; i++) {
        cpu_sr[i] = tcg_global_mem_new_i64(cpu_env,
                                           offsetof(CPUHPPAState, sr[i]),
                                           sr_names[i]);
    }
    cpu_srH = tcg_global_mem_new_i64(cpu_env,
                                     offsetof(CPUHPPAState, sr[4]),
                                     sr_names[4]);

    for (i = 0; i < ARRAY_SIZE(vars); ++i) {
        const GlobalVar *v = &vars[i];
        *v->var = tcg_global_mem_new(cpu_env, v->ofs, v->name);
    }

    cpu_iasq_f = tcg_global_mem_new_i64(cpu_env,
                                        offsetof(CPUHPPAState, iasq_f),
                                        "iasq_f");
    cpu_iasq_b = tcg_global_mem_new_i64(cpu_env,
                                        offsetof(CPUHPPAState, iasq_b),
                                        "iasq_b");
}

static DisasCond cond_make_f(void)
{
    return (DisasCond){
        .c = TCG_COND_NEVER,
        .a0 = NULL,
        .a1 = NULL,
    };
}

static DisasCond cond_make_t(void)
{
    return (DisasCond){
        .c = TCG_COND_ALWAYS,
        .a0 = NULL,
        .a1 = NULL,
    };
}

static DisasCond cond_make_n(void)
{
    return (DisasCond){
        .c = TCG_COND_NE,
        .a0 = cpu_psw_n,
        .a1 = tcg_constant_reg(0)
    };
}

static DisasCond cond_make_0_tmp(TCGCond c, TCGv_reg a0)
{
    assert (c != TCG_COND_NEVER && c != TCG_COND_ALWAYS);
    return (DisasCond){
        .c = c, .a0 = a0, .a1 = tcg_constant_reg(0)
    };
}

static DisasCond cond_make_0(TCGCond c, TCGv_reg a0)
{
    TCGv_reg tmp = tcg_temp_new();
    tcg_gen_mov_reg(tmp, a0);
    return cond_make_0_tmp(c, tmp);
}

static DisasCond cond_make(TCGCond c, TCGv_reg a0, TCGv_reg a1)
{
    DisasCond r = { .c = c };

    assert (c != TCG_COND_NEVER && c != TCG_COND_ALWAYS);
    r.a0 = tcg_temp_new();
    tcg_gen_mov_reg(r.a0, a0);
    r.a1 = tcg_temp_new();
    tcg_gen_mov_reg(r.a1, a1);

    return r;
}

static void cond_free(DisasCond *cond)
{
    switch (cond->c) {
    default:
        if (cond->a0 != cpu_psw_n) {
            tcg_temp_free(cond->a0);
        }
        tcg_temp_free(cond->a1);
        cond->a0 = NULL;
        cond->a1 = NULL;
        /* fallthru */
    case TCG_COND_ALWAYS:
        cond->c = TCG_COND_NEVER;
        break;
    case TCG_COND_NEVER:
        break;
    }
}

static TCGv_reg get_temp(DisasContext *ctx)
{
    unsigned i = ctx->ntempr++;
    g_assert(i < ARRAY_SIZE(ctx->tempr));
    return ctx->tempr[i] = tcg_temp_new();
}

#ifndef CONFIG_USER_ONLY
static TCGv_tl get_temp_tl(DisasContext *ctx)
{
    unsigned i = ctx->ntempl++;
    g_assert(i < ARRAY_SIZE(ctx->templ));
    return ctx->templ[i] = tcg_temp_new_tl();
}
#endif

static TCGv_reg load_const(DisasContext *ctx, target_sreg v)
{
    TCGv_reg t = get_temp(ctx);
    tcg_gen_movi_reg(t, v);
    return t;
}

static TCGv_reg load_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0) {
        TCGv_reg t = get_temp(ctx);
        tcg_gen_movi_reg(t, 0);
        return t;
    } else {
        return cpu_gr[reg];
    }
}

static TCGv_reg dest_gpr(DisasContext *ctx, unsigned reg)
{
    if (reg == 0 || ctx->null_cond.c != TCG_COND_NEVER) {
        return get_temp(ctx);
    } else {
        return cpu_gr[reg];
    }
}

static void save_or_nullify(DisasContext *ctx, TCGv_reg dest, TCGv_reg t)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        tcg_gen_movcond_reg(ctx->null_cond.c, dest, ctx->null_cond.a0,
                            ctx->null_cond.a1, dest, t);
    } else {
        tcg_gen_mov_reg(dest, t);
    }
}

static void save_gpr(DisasContext *ctx, unsigned reg, TCGv_reg t)
{
    if (reg != 0) {
        save_or_nullify(ctx, cpu_gr[reg], t);
    }
}

#if HOST_BIG_ENDIAN
# define HI_OFS  0
# define LO_OFS  4
#else
# define HI_OFS  4
# define LO_OFS  0
#endif

static TCGv_i32 load_frw_i32(unsigned rt)
{
    TCGv_i32 ret = tcg_temp_new_i32();
    tcg_gen_ld_i32(ret, cpu_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
    return ret;
}

static TCGv_i32 load_frw0_i32(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i32(0);
    } else {
        return load_frw_i32(rt);
    }
}

static TCGv_i64 load_frw0_i64(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i64(0);
    } else {
        TCGv_i64 ret = tcg_temp_new_i64();
        tcg_gen_ld32u_i64(ret, cpu_env,
                          offsetof(CPUHPPAState, fr[rt & 31])
                          + (rt & 32 ? LO_OFS : HI_OFS));
        return ret;
    }
}

static void save_frw_i32(unsigned rt, TCGv_i32 val)
{
    tcg_gen_st_i32(val, cpu_env,
                   offsetof(CPUHPPAState, fr[rt & 31])
                   + (rt & 32 ? LO_OFS : HI_OFS));
}

#undef HI_OFS
#undef LO_OFS

static TCGv_i64 load_frd(unsigned rt)
{
    TCGv_i64 ret = tcg_temp_new_i64();
    tcg_gen_ld_i64(ret, cpu_env, offsetof(CPUHPPAState, fr[rt]));
    return ret;
}

static TCGv_i64 load_frd0(unsigned rt)
{
    if (rt == 0) {
        return tcg_const_i64(0);
    } else {
        return load_frd(rt);
    }
}

static void save_frd(unsigned rt, TCGv_i64 val)
{
    tcg_gen_st_i64(val, cpu_env, offsetof(CPUHPPAState, fr[rt]));
}

static void load_spr(DisasContext *ctx, TCGv_i64 dest, unsigned reg)
{
#ifdef CONFIG_USER_ONLY
    tcg_gen_movi_i64(dest, 0);
#else
    if (reg < 4) {
        tcg_gen_mov_i64(dest, cpu_sr[reg]);
    } else if (ctx->tb_flags & TB_FLAG_SR_SAME) {
        tcg_gen_mov_i64(dest, cpu_srH);
    } else {
        tcg_gen_ld_i64(dest, cpu_env, offsetof(CPUHPPAState, sr[reg]));
    }
#endif
}

/* Skip over the implementation of an insn that has been nullified.
   Use this when the insn is too complex for a conditional move.  */
static void nullify_over(DisasContext *ctx)
{
    if (ctx->null_cond.c != TCG_COND_NEVER) {
        /* The always condition should have been handled in the main loop.  */
        assert(ctx->null_cond.c != TCG_COND_ALWAYS);

        ctx->null_lab = gen_new_label();

        /* If we're using PSW[N], copy it to a temp because... */
        if (ctx->null_cond.a0 == cpu_psw_n) {
            ctx->null_cond.a0 = tcg_temp_new();
            tcg_gen_mov_reg(ctx->null_cond.a0, cpu_psw_n);
        }
        /* ... we clear it before branching over the implementation,
           so that (1) it's clear after nullifying this insn and
           (2) if this insn nullifies the next, PSW[N] is valid.  */
        if (ctx->psw_n_nonzero) {
            ctx->psw_n_nonzero = false;
            tcg_gen_movi_reg(cpu_psw_n, 0);
        }

        tcg_gen_brcond_reg(ctx->null_cond.c, ctx->null_cond.a0,
                           ctx->null_cond.a1, ctx->null_lab);
        cond_free(&ctx->null_cond);
    }
}

/* Save the current nullification state to PSW[N].  */
static void nullify_save(DisasContext *ctx)
{
    if (ctx->null_cond.c == TCG_COND_NEVER) {
        if (ctx->psw_n_nonzero) {
            tcg_gen_movi_reg(cpu_psw_n, 0);
        }
        return;
    }
    if (ctx->null_cond.a0 != cpu_psw_n) {
        tcg_gen_setcond_reg(ctx->null_cond.c, cpu_psw_n,
                            ctx->null_cond.a0, ctx->null_cond.a1);
        ctx->psw_n_nonzero = true;
    }
    cond_free(&ctx->null_cond);
}

/* Set a PSW[N] to X.  The intention is that this is used immediately
   before a goto_tb/exit_tb, so that there is no fallthru path to other
   code within the TB.  Therefore we do not update psw_n_nonzero.  */
static void nullify_set(DisasContext *ctx, bool x)
{
    if (ctx->psw_n_nonzero || x) {
        tcg_gen_movi_reg(cpu_psw_n, x);
    }
}

/* Mark the end of an instruction that may have been nullified.
   This is the pair to nullify_over.  Always returns true so that
   it may be tail-called from a translate function.  */
static bool nullify_end(DisasContext *ctx)
{
    TCGLabel *null_lab = ctx->null_lab;
    DisasJumpType status = ctx->base.is_jmp;

    /* For NEXT, NORETURN, STALE, we can easily continue (or exit).
       For UPDATED, we cannot update on the nullified path.  */
    assert(status != DISAS_IAQ_N_UPDATED);

    if (likely(null_lab == NULL)) {
        /* The current insn wasn't conditional or handled the condition
           applied to it without a branch, so the (new) setting of
           NULL_COND can be applied directly to the next insn.  */
        return true;
    }
    ctx->null_lab = NULL;

    if (likely(ctx->null_cond.c == TCG_COND_NEVER)) {
        /* The next instruction will be unconditional,
           and NULL_COND already reflects that.  */
        gen_set_label(null_lab);
    } else {
        /* The insn that we just executed is itself nullifying the next
           instruction.  Store the condition in the PSW[N] global.
           We asserted PSW[N] = 0 in nullify_over, so that after the
           label we have the proper value in place.  */
        nullify_save(ctx);
        gen_set_label(null_lab);
        ctx->null_cond = cond_make_n();
    }
    if (status == DISAS_NORETURN) {
        ctx->base.is_jmp = DISAS_NEXT;
    }
    return true;
}

static void copy_iaoq_entry(TCGv_reg dest, target_ureg ival, TCGv_reg vval)
{
    if (unlikely(ival == -1)) {
        tcg_gen_mov_reg(dest, vval);
    } else {
        tcg_gen_movi_reg(dest, ival);
    }
}

static inline target_ureg iaoq_dest(DisasContext *ctx, target_sreg disp)
{
    return ctx->iaoq_f + disp + 8;
}

static void gen_excp_1(int exception)
{
    gen_helper_excp(cpu_env, tcg_constant_i32(exception));
}

static void gen_excp(DisasContext *ctx, int exception)
{
    copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_f, cpu_iaoq_f);
    copy_iaoq_entry(cpu_iaoq_b, ctx->iaoq_b, cpu_iaoq_b);
    nullify_save(ctx);
    gen_excp_1(exception);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static bool gen_excp_iir(DisasContext *ctx, int exc)
{
    nullify_over(ctx);
    tcg_gen_st_reg(tcg_constant_reg(ctx->insn),
                   cpu_env, offsetof(CPUHPPAState, cr[CR_IIR]));
    gen_excp(ctx, exc);
    return nullify_end(ctx);
}

static bool gen_illegal(DisasContext *ctx)
{
    return gen_excp_iir(ctx, EXCP_ILL);
}

#ifdef CONFIG_USER_ONLY
#define CHECK_MOST_PRIVILEGED(EXCP) \
    return gen_excp_iir(ctx, EXCP)
#else
#define CHECK_MOST_PRIVILEGED(EXCP) \
    do {                                     \
        if (ctx->privilege != 0) {           \
            return gen_excp_iir(ctx, EXCP);  \
        }                                    \
    } while (0)
#endif

static bool use_goto_tb(DisasContext *ctx, target_ureg dest)
{
    return translator_use_goto_tb(&ctx->base, dest);
}

/* If the next insn is to be nullified, and it's on the same page,
   and we're not attempting to set a breakpoint on it, then we can
   totally skip the nullified insn.  This avoids creating and
   executing a TB that merely branches to the next TB.  */
static bool use_nullify_skip(DisasContext *ctx)
{
    return (((ctx->iaoq_b ^ ctx->iaoq_f) & TARGET_PAGE_MASK) == 0
            && !cpu_breakpoint_test(ctx->cs, ctx->iaoq_b, BP_ANY));
}

static void gen_goto_tb(DisasContext *ctx, int which,
                        target_ureg f, target_ureg b)
{
    if (f != -1 && b != -1 && use_goto_tb(ctx, f)) {
        tcg_gen_goto_tb(which);
        tcg_gen_movi_reg(cpu_iaoq_f, f);
        tcg_gen_movi_reg(cpu_iaoq_b, b);
        tcg_gen_exit_tb(ctx->base.tb, which);
    } else {
        copy_iaoq_entry(cpu_iaoq_f, f, cpu_iaoq_b);
        copy_iaoq_entry(cpu_iaoq_b, b, ctx->iaoq_n_var);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static bool cond_need_sv(int c)
{
    return c == 2 || c == 3 || c == 6;
}

static bool cond_need_cb(int c)
{
    return c == 4 || c == 5;
}

/*
 * Compute conditional for arithmetic.  See Page 5-3, Table 5-1, of
 * the Parisc 1.1 Architecture Reference Manual for details.
 */

static DisasCond do_cond(unsigned cf, TCGv_reg res,
                         TCGv_reg cb_msb, TCGv_reg sv)
{
    DisasCond cond;
    TCGv_reg tmp;

    switch (cf >> 1) {
    case 0: /* Never / TR    (0 / 1) */
        cond = cond_make_f();
        break;
    case 1: /* = / <>        (Z / !Z) */
        cond = cond_make_0(TCG_COND_EQ, res);
        break;
    case 2: /* < / >=        (N ^ V / !(N ^ V) */
        tmp = tcg_temp_new();
        tcg_gen_xor_reg(tmp, res, sv);
        cond = cond_make_0_tmp(TCG_COND_LT, tmp);
        break;
    case 3: /* <= / >        (N ^ V) | Z / !((N ^ V) | Z) */
        /*
         * Simplify:
         *   (N ^ V) | Z
         *   ((res < 0) ^ (sv < 0)) | !res
         *   ((res ^ sv) < 0) | !res
         *   (~(res ^ sv) >= 0) | !res
         *   !(~(res ^ sv) >> 31) | !res
         *   !(~(res ^ sv) >> 31 & res)
         */
        tmp = tcg_temp_new();
        tcg_gen_eqv_reg(tmp, res, sv);
        tcg_gen_sari_reg(tmp, tmp, TARGET_REGISTER_BITS - 1);
        tcg_gen_and_reg(tmp, tmp, res);
        cond = cond_make_0_tmp(TCG_COND_EQ, tmp);
        break;
    case 4: /* NUV / UV      (!C / C) */
        cond = cond_make_0(TCG_COND_EQ, cb_msb);
        break;
    case 5: /* ZNV / VNZ     (!C | Z / C & !Z) */
        tmp = tcg_temp_new();
        tcg_gen_neg_reg(tmp, cb_msb);
        tcg_gen_and_reg(tmp, tmp, res);
        cond = cond_make_0_tmp(TCG_COND_EQ, tmp);
        break;
    case 6: /* SV / NSV      (V / !V) */
        cond = cond_make_0(TCG_COND_LT, sv);
        break;
    case 7: /* OD / EV */
        tmp = tcg_temp_new();
        tcg_gen_andi_reg(tmp, res, 1);
        cond = cond_make_0_tmp(TCG_COND_NE, tmp);
        break;
    default:
        g_assert_not_reached();
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/* Similar, but for the special case of subtraction without borrow, we
   can use the inputs directly.  This can allow other computation to be
   deleted as unused.  */

static DisasCond do_sub_cond(unsigned cf, TCGv_reg res,
                             TCGv_reg in1, TCGv_reg in2, TCGv_reg sv)
{
    DisasCond cond;

    switch (cf >> 1) {
    case 1: /* = / <> */
        cond = cond_make(TCG_COND_EQ, in1, in2);
        break;
    case 2: /* < / >= */
        cond = cond_make(TCG_COND_LT, in1, in2);
        break;
    case 3: /* <= / > */
        cond = cond_make(TCG_COND_LE, in1, in2);
        break;
    case 4: /* << / >>= */
        cond = cond_make(TCG_COND_LTU, in1, in2);
        break;
    case 5: /* <<= / >> */
        cond = cond_make(TCG_COND_LEU, in1, in2);
        break;
    default:
        return do_cond(cf, res, NULL, sv);
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/*
 * Similar, but for logicals, where the carry and overflow bits are not
 * computed, and use of them is undefined.
 *
 * Undefined or not, hardware does not trap.  It seems reasonable to
 * assume hardware treats cases c={4,5,6} as if C=0 & V=0, since that's
 * how cases c={2,3} are treated.
 */

static DisasCond do_log_cond(unsigned cf, TCGv_reg res)
{
    switch (cf) {
    case 0:  /* never */
    case 9:  /* undef, C */
    case 11: /* undef, C & !Z */
    case 12: /* undef, V */
        return cond_make_f();

    case 1:  /* true */
    case 8:  /* undef, !C */
    case 10: /* undef, !C | Z */
    case 13: /* undef, !V */
        return cond_make_t();

    case 2:  /* == */
        return cond_make_0(TCG_COND_EQ, res);
    case 3:  /* <> */
        return cond_make_0(TCG_COND_NE, res);
    case 4:  /* < */
        return cond_make_0(TCG_COND_LT, res);
    case 5:  /* >= */
        return cond_make_0(TCG_COND_GE, res);
    case 6:  /* <= */
        return cond_make_0(TCG_COND_LE, res);
    case 7:  /* > */
        return cond_make_0(TCG_COND_GT, res);

    case 14: /* OD */
    case 15: /* EV */
        return do_cond(cf, res, NULL, NULL);

    default:
        g_assert_not_reached();
    }
}

/* Similar, but for shift/extract/deposit conditions.  */

static DisasCond do_sed_cond(unsigned orig, TCGv_reg res)
{
    unsigned c, f;

    /* Convert the compressed condition codes to standard.
       0-2 are the same as logicals (nv,<,<=), while 3 is OD.
       4-7 are the reverse of 0-3.  */
    c = orig & 3;
    if (c == 3) {
        c = 7;
    }
    f = (orig & 4) / 4;

    return do_log_cond(c * 2 + f, res);
}

/* Similar, but for unit conditions.  */

static DisasCond do_unit_cond(unsigned cf, TCGv_reg res,
                              TCGv_reg in1, TCGv_reg in2)
{
    DisasCond cond;
    TCGv_reg tmp, cb = NULL;

    if (cf & 8) {
        /* Since we want to test lots of carry-out bits all at once, do not
         * do our normal thing and compute carry-in of bit B+1 since that
         * leaves us with carry bits spread across two words.
         */
        cb = tcg_temp_new();
        tmp = tcg_temp_new();
        tcg_gen_or_reg(cb, in1, in2);
        tcg_gen_and_reg(tmp, in1, in2);
        tcg_gen_andc_reg(cb, cb, res);
        tcg_gen_or_reg(cb, cb, tmp);
        tcg_temp_free(tmp);
    }

    switch (cf >> 1) {
    case 0: /* never / TR */
    case 1: /* undefined */
    case 5: /* undefined */
        cond = cond_make_f();
        break;

    case 2: /* SBZ / NBZ */
        /* See hasless(v,1) from
         * https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
         */
        tmp = tcg_temp_new();
        tcg_gen_subi_reg(tmp, res, 0x01010101u);
        tcg_gen_andc_reg(tmp, tmp, res);
        tcg_gen_andi_reg(tmp, tmp, 0x80808080u);
        cond = cond_make_0(TCG_COND_NE, tmp);
        tcg_temp_free(tmp);
        break;

    case 3: /* SHZ / NHZ */
        tmp = tcg_temp_new();
        tcg_gen_subi_reg(tmp, res, 0x00010001u);
        tcg_gen_andc_reg(tmp, tmp, res);
        tcg_gen_andi_reg(tmp, tmp, 0x80008000u);
        cond = cond_make_0(TCG_COND_NE, tmp);
        tcg_temp_free(tmp);
        break;

    case 4: /* SDC / NDC */
        tcg_gen_andi_reg(cb, cb, 0x88888888u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    case 6: /* SBC / NBC */
        tcg_gen_andi_reg(cb, cb, 0x80808080u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    case 7: /* SHC / NHC */
        tcg_gen_andi_reg(cb, cb, 0x80008000u);
        cond = cond_make_0(TCG_COND_NE, cb);
        break;

    default:
        g_assert_not_reached();
    }
    if (cf & 8) {
        tcg_temp_free(cb);
    }
    if (cf & 1) {
        cond.c = tcg_invert_cond(cond.c);
    }

    return cond;
}

/* Compute signed overflow for addition.  */
static TCGv_reg do_add_sv(DisasContext *ctx, TCGv_reg res,
                          TCGv_reg in1, TCGv_reg in2)
{
    TCGv_reg sv = get_temp(ctx);
    TCGv_reg tmp = tcg_temp_new();

    tcg_gen_xor_reg(sv, res, in1);
    tcg_gen_xor_reg(tmp, in1, in2);
    tcg_gen_andc_reg(sv, sv, tmp);
    tcg_temp_free(tmp);

    return sv;
}

/* Compute signed overflow for subtraction.  */
static TCGv_reg do_sub_sv(DisasContext *ctx, TCGv_reg res,
                          TCGv_reg in1, TCGv_reg in2)
{
    TCGv_reg sv = get_temp(ctx);
    TCGv_reg tmp = tcg_temp_new();

    tcg_gen_xor_reg(sv, res, in1);
    tcg_gen_xor_reg(tmp, in1, in2);
    tcg_gen_and_reg(sv, sv, tmp);
    tcg_temp_free(tmp);

    return sv;
}

static void do_add(DisasContext *ctx, unsigned rt, TCGv_reg in1,
                   TCGv_reg in2, unsigned shift, bool is_l,
                   bool is_tsv, bool is_tc, bool is_c, unsigned cf)
{
    TCGv_reg dest, cb, cb_msb, sv, tmp;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new();
    cb = NULL;
    cb_msb = NULL;

    if (shift) {
        tmp = get_temp(ctx);
        tcg_gen_shli_reg(tmp, in1, shift);
        in1 = tmp;
    }

    if (!is_l || cond_need_cb(c)) {
        TCGv_reg zero = tcg_constant_reg(0);
        cb_msb = get_temp(ctx);
        tcg_gen_add2_reg(dest, cb_msb, in1, zero, in2, zero);
        if (is_c) {
            tcg_gen_add2_reg(dest, cb_msb, dest, cb_msb, cpu_psw_cb_msb, zero);
        }
        if (!is_l) {
            cb = get_temp(ctx);
            tcg_gen_xor_reg(cb, in1, in2);
            tcg_gen_xor_reg(cb, cb, dest);
        }
    } else {
        tcg_gen_add_reg(dest, in1, in2);
        if (is_c) {
            tcg_gen_add_reg(dest, dest, cpu_psw_cb_msb);
        }
    }

    /* Compute signed overflow if required.  */
    sv = NULL;
    if (is_tsv || cond_need_sv(c)) {
        sv = do_add_sv(ctx, dest, in1, in2);
        if (is_tsv) {
            /* ??? Need to include overflow from shift.  */
            gen_helper_tsv(cpu_env, sv);
        }
    }

    /* Emit any conditional trap before any writeback.  */
    cond = do_cond(cf, dest, cb_msb, sv);
    if (is_tc) {
        tmp = tcg_temp_new();
        tcg_gen_setcond_reg(cond.c, tmp, cond.a0, cond.a1);
        gen_helper_tcond(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    /* Write back the result.  */
    if (!is_l) {
        save_or_nullify(ctx, cpu_psw_cb, cb);
        save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    }
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
}

static bool do_add_reg(DisasContext *ctx, arg_rrr_cf_sh *a,
                       bool is_l, bool is_tsv, bool is_tc, bool is_c)
{
    TCGv_reg tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_add(ctx, a->t, tcg_r1, tcg_r2, a->sh, is_l, is_tsv, is_tc, is_c, a->cf);
    return nullify_end(ctx);
}

static bool do_add_imm(DisasContext *ctx, arg_rri_cf *a,
                       bool is_tsv, bool is_tc)
{
    TCGv_reg tcg_im, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_im = load_const(ctx, a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_add(ctx, a->t, tcg_im, tcg_r2, 0, 0, is_tsv, is_tc, 0, a->cf);
    return nullify_end(ctx);
}

static void do_sub(DisasContext *ctx, unsigned rt, TCGv_reg in1,
                   TCGv_reg in2, bool is_tsv, bool is_b,
                   bool is_tc, unsigned cf)
{
    TCGv_reg dest, sv, cb, cb_msb, zero, tmp;
    unsigned c = cf >> 1;
    DisasCond cond;

    dest = tcg_temp_new();
    cb = tcg_temp_new();
    cb_msb = tcg_temp_new();

    zero = tcg_constant_reg(0);
    if (is_b) {
        /* DEST,C = IN1 + ~IN2 + C.  */
        tcg_gen_not_reg(cb, in2);
        tcg_gen_add2_reg(dest, cb_msb, in1, zero, cpu_psw_cb_msb, zero);
        tcg_gen_add2_reg(dest, cb_msb, dest, cb_msb, cb, zero);
        tcg_gen_xor_reg(cb, cb, in1);
        tcg_gen_xor_reg(cb, cb, dest);
    } else {
        /* DEST,C = IN1 + ~IN2 + 1.  We can produce the same result in fewer
           operations by seeding the high word with 1 and subtracting.  */
        tcg_gen_movi_reg(cb_msb, 1);
        tcg_gen_sub2_reg(dest, cb_msb, in1, cb_msb, in2, zero);
        tcg_gen_eqv_reg(cb, in1, in2);
        tcg_gen_xor_reg(cb, cb, dest);
    }

    /* Compute signed overflow if required.  */
    sv = NULL;
    if (is_tsv || cond_need_sv(c)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
        if (is_tsv) {
            gen_helper_tsv(cpu_env, sv);
        }
    }

    /* Compute the condition.  We cannot use the special case for borrow.  */
    if (!is_b) {
        cond = do_sub_cond(cf, dest, in1, in2, sv);
    } else {
        cond = do_cond(cf, dest, cb_msb, sv);
    }

    /* Emit any conditional trap before any writeback.  */
    if (is_tc) {
        tmp = tcg_temp_new();
        tcg_gen_setcond_reg(cond.c, tmp, cond.a0, cond.a1);
        gen_helper_tcond(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    /* Write back the result.  */
    save_or_nullify(ctx, cpu_psw_cb, cb);
    save_or_nullify(ctx, cpu_psw_cb_msb, cb_msb);
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);
    tcg_temp_free(cb);
    tcg_temp_free(cb_msb);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
}

static bool do_sub_reg(DisasContext *ctx, arg_rrr_cf *a,
                       bool is_tsv, bool is_b, bool is_tc)
{
    TCGv_reg tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_sub(ctx, a->t, tcg_r1, tcg_r2, is_tsv, is_b, is_tc, a->cf);
    return nullify_end(ctx);
}

static bool do_sub_imm(DisasContext *ctx, arg_rri_cf *a, bool is_tsv)
{
    TCGv_reg tcg_im, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_im = load_const(ctx, a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_sub(ctx, a->t, tcg_im, tcg_r2, is_tsv, 0, 0, a->cf);
    return nullify_end(ctx);
}

static void do_cmpclr(DisasContext *ctx, unsigned rt, TCGv_reg in1,
                      TCGv_reg in2, unsigned cf)
{
    TCGv_reg dest, sv;
    DisasCond cond;

    dest = tcg_temp_new();
    tcg_gen_sub_reg(dest, in1, in2);

    /* Compute signed overflow if required.  */
    sv = NULL;
    if (cond_need_sv(cf >> 1)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    /* Form the condition for the compare.  */
    cond = do_sub_cond(cf, dest, in1, in2, sv);

    /* Clear.  */
    tcg_gen_movi_reg(dest, 0);
    save_gpr(ctx, rt, dest);
    tcg_temp_free(dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    ctx->null_cond = cond;
}

static void do_log(DisasContext *ctx, unsigned rt, TCGv_reg in1,
                   TCGv_reg in2, unsigned cf,
                   void (*fn)(TCGv_reg, TCGv_reg, TCGv_reg))
{
    TCGv_reg dest = dest_gpr(ctx, rt);

    /* Perform the operation, and writeback.  */
    fn(dest, in1, in2);
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (cf) {
        ctx->null_cond = do_log_cond(cf, dest);
    }
}

static bool do_log_reg(DisasContext *ctx, arg_rrr_cf *a,
                       void (*fn)(TCGv_reg, TCGv_reg, TCGv_reg))
{
    TCGv_reg tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_log(ctx, a->t, tcg_r1, tcg_r2, a->cf, fn);
    return nullify_end(ctx);
}

static void do_unit(DisasContext *ctx, unsigned rt, TCGv_reg in1,
                    TCGv_reg in2, unsigned cf, bool is_tc,
                    void (*fn)(TCGv_reg, TCGv_reg, TCGv_reg))
{
    TCGv_reg dest;
    DisasCond cond;

    if (cf == 0) {
        dest = dest_gpr(ctx, rt);
        fn(dest, in1, in2);
        save_gpr(ctx, rt, dest);
        cond_free(&ctx->null_cond);
    } else {
        dest = tcg_temp_new();
        fn(dest, in1, in2);

        cond = do_unit_cond(cf, dest, in1, in2);

        if (is_tc) {
            TCGv_reg tmp = tcg_temp_new();
            tcg_gen_setcond_reg(cond.c, tmp, cond.a0, cond.a1);
            gen_helper_tcond(cpu_env, tmp);
            tcg_temp_free(tmp);
        }
        save_gpr(ctx, rt, dest);

        cond_free(&ctx->null_cond);
        ctx->null_cond = cond;
    }
}

#ifndef CONFIG_USER_ONLY
/* The "normal" usage is SP >= 0, wherein SP == 0 selects the space
   from the top 2 bits of the base register.  There are a few system
   instructions that have a 3-bit space specifier, for which SR0 is
   not special.  To handle this, pass ~SP.  */
static TCGv_i64 space_select(DisasContext *ctx, int sp, TCGv_reg base)
{
    TCGv_ptr ptr;
    TCGv_reg tmp;
    TCGv_i64 spc;

    if (sp != 0) {
        if (sp < 0) {
            sp = ~sp;
        }
        spc = get_temp_tl(ctx);
        load_spr(ctx, spc, sp);
        return spc;
    }
    if (ctx->tb_flags & TB_FLAG_SR_SAME) {
        return cpu_srH;
    }

    ptr = tcg_temp_new_ptr();
    tmp = tcg_temp_new();
    spc = get_temp_tl(ctx);

    tcg_gen_shri_reg(tmp, base, TARGET_REGISTER_BITS - 5);
    tcg_gen_andi_reg(tmp, tmp, 030);
    tcg_gen_trunc_reg_ptr(ptr, tmp);
    tcg_temp_free(tmp);

    tcg_gen_add_ptr(ptr, ptr, cpu_env);
    tcg_gen_ld_i64(spc, ptr, offsetof(CPUHPPAState, sr[4]));
    tcg_temp_free_ptr(ptr);

    return spc;
}
#endif

static void form_gva(DisasContext *ctx, TCGv_tl *pgva, TCGv_reg *pofs,
                     unsigned rb, unsigned rx, int scale, target_sreg disp,
                     unsigned sp, int modify, bool is_phys)
{
    TCGv_reg base = load_gpr(ctx, rb);
    TCGv_reg ofs;

    /* Note that RX is mutually exclusive with DISP.  */
    if (rx) {
        ofs = get_temp(ctx);
        tcg_gen_shli_reg(ofs, cpu_gr[rx], scale);
        tcg_gen_add_reg(ofs, ofs, base);
    } else if (disp || modify) {
        ofs = get_temp(ctx);
        tcg_gen_addi_reg(ofs, base, disp);
    } else {
        ofs = base;
    }

    *pofs = ofs;
#ifdef CONFIG_USER_ONLY
    *pgva = (modify <= 0 ? ofs : base);
#else
    TCGv_tl addr = get_temp_tl(ctx);
    tcg_gen_extu_reg_tl(addr, modify <= 0 ? ofs : base);
    if (ctx->tb_flags & PSW_W) {
        tcg_gen_andi_tl(addr, addr, 0x3fffffffffffffffull);
    }
    if (!is_phys) {
        tcg_gen_or_tl(addr, addr, space_select(ctx, sp, base));
    }
    *pgva = addr;
#endif
}

/* Emit a memory load.  The modify parameter should be
 * < 0 for pre-modify,
 * > 0 for post-modify,
 * = 0 for no base register update.
 */
static void do_load_32(DisasContext *ctx, TCGv_i32 dest, unsigned rb,
                       unsigned rx, int scale, target_sreg disp,
                       unsigned sp, int modify, MemOp mop)
{
    TCGv_reg ofs;
    TCGv_tl addr;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             ctx->mmu_idx == MMU_PHYS_IDX);
    tcg_gen_qemu_ld_reg(dest, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_load_64(DisasContext *ctx, TCGv_i64 dest, unsigned rb,
                       unsigned rx, int scale, target_sreg disp,
                       unsigned sp, int modify, MemOp mop)
{
    TCGv_reg ofs;
    TCGv_tl addr;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             ctx->mmu_idx == MMU_PHYS_IDX);
    tcg_gen_qemu_ld_i64(dest, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_store_32(DisasContext *ctx, TCGv_i32 src, unsigned rb,
                        unsigned rx, int scale, target_sreg disp,
                        unsigned sp, int modify, MemOp mop)
{
    TCGv_reg ofs;
    TCGv_tl addr;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             ctx->mmu_idx == MMU_PHYS_IDX);
    tcg_gen_qemu_st_i32(src, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

static void do_store_64(DisasContext *ctx, TCGv_i64 src, unsigned rb,
                        unsigned rx, int scale, target_sreg disp,
                        unsigned sp, int modify, MemOp mop)
{
    TCGv_reg ofs;
    TCGv_tl addr;

    /* Caller uses nullify_over/nullify_end.  */
    assert(ctx->null_cond.c == TCG_COND_NEVER);

    form_gva(ctx, &addr, &ofs, rb, rx, scale, disp, sp, modify,
             ctx->mmu_idx == MMU_PHYS_IDX);
    tcg_gen_qemu_st_i64(src, addr, ctx->mmu_idx, mop | UNALIGN(ctx));
    if (modify) {
        save_gpr(ctx, rb, ofs);
    }
}

#if TARGET_REGISTER_BITS == 64
#define do_load_reg   do_load_64
#define do_store_reg  do_store_64
#else
#define do_load_reg   do_load_32
#define do_store_reg  do_store_32
#endif

static bool do_load(DisasContext *ctx, unsigned rt, unsigned rb,
                    unsigned rx, int scale, target_sreg disp,
                    unsigned sp, int modify, MemOp mop)
{
    TCGv_reg dest;

    nullify_over(ctx);

    if (modify == 0) {
        /* No base register update.  */
        dest = dest_gpr(ctx, rt);
    } else {
        /* Make sure if RT == RB, we see the result of the load.  */
        dest = get_temp(ctx);
    }
    do_load_reg(ctx, dest, rb, rx, scale, disp, sp, modify, mop);
    save_gpr(ctx, rt, dest);

    return nullify_end(ctx);
}

static bool do_floadw(DisasContext *ctx, unsigned rt, unsigned rb,
                      unsigned rx, int scale, target_sreg disp,
                      unsigned sp, int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i32();
    do_load_32(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_TEUL);
    save_frw_i32(rt, tmp);
    tcg_temp_free_i32(tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(cpu_env);
    }

    return nullify_end(ctx);
}

static bool trans_fldw(DisasContext *ctx, arg_ldst *a)
{
    return do_floadw(ctx, a->t, a->b, a->x, a->scale ? 2 : 0,
                     a->disp, a->sp, a->m);
}

static bool do_floadd(DisasContext *ctx, unsigned rt, unsigned rb,
                      unsigned rx, int scale, target_sreg disp,
                      unsigned sp, int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = tcg_temp_new_i64();
    do_load_64(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_TEUQ);
    save_frd(rt, tmp);
    tcg_temp_free_i64(tmp);

    if (rt == 0) {
        gen_helper_loaded_fr0(cpu_env);
    }

    return nullify_end(ctx);
}

static bool trans_fldd(DisasContext *ctx, arg_ldst *a)
{
    return do_floadd(ctx, a->t, a->b, a->x, a->scale ? 3 : 0,
                     a->disp, a->sp, a->m);
}

static bool do_store(DisasContext *ctx, unsigned rt, unsigned rb,
                     target_sreg disp, unsigned sp,
                     int modify, MemOp mop)
{
    nullify_over(ctx);
    do_store_reg(ctx, load_gpr(ctx, rt), rb, 0, 0, disp, sp, modify, mop);
    return nullify_end(ctx);
}

static bool do_fstorew(DisasContext *ctx, unsigned rt, unsigned rb,
                       unsigned rx, int scale, target_sreg disp,
                       unsigned sp, int modify)
{
    TCGv_i32 tmp;

    nullify_over(ctx);

    tmp = load_frw_i32(rt);
    do_store_32(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_TEUL);
    tcg_temp_free_i32(tmp);

    return nullify_end(ctx);
}

static bool trans_fstw(DisasContext *ctx, arg_ldst *a)
{
    return do_fstorew(ctx, a->t, a->b, a->x, a->scale ? 2 : 0,
                      a->disp, a->sp, a->m);
}

static bool do_fstored(DisasContext *ctx, unsigned rt, unsigned rb,
                       unsigned rx, int scale, target_sreg disp,
                       unsigned sp, int modify)
{
    TCGv_i64 tmp;

    nullify_over(ctx);

    tmp = load_frd(rt);
    do_store_64(ctx, tmp, rb, rx, scale, disp, sp, modify, MO_TEUQ);
    tcg_temp_free_i64(tmp);

    return nullify_end(ctx);
}

static bool trans_fstd(DisasContext *ctx, arg_ldst *a)
{
    return do_fstored(ctx, a->t, a->b, a->x, a->scale ? 3 : 0,
                      a->disp, a->sp, a->m);
}

static bool do_fop_wew(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i32, TCGv_env, TCGv_i32))
{
    TCGv_i32 tmp;

    nullify_over(ctx);
    tmp = load_frw0_i32(ra);

    func(tmp, cpu_env, tmp);

    save_frw_i32(rt, tmp);
    tcg_temp_free_i32(tmp);
    return nullify_end(ctx);
}

static bool do_fop_wed(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i32, TCGv_env, TCGv_i64))
{
    TCGv_i32 dst;
    TCGv_i64 src;

    nullify_over(ctx);
    src = load_frd(ra);
    dst = tcg_temp_new_i32();

    func(dst, cpu_env, src);

    tcg_temp_free_i64(src);
    save_frw_i32(rt, dst);
    tcg_temp_free_i32(dst);
    return nullify_end(ctx);
}

static bool do_fop_ded(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i64, TCGv_env, TCGv_i64))
{
    TCGv_i64 tmp;

    nullify_over(ctx);
    tmp = load_frd0(ra);

    func(tmp, cpu_env, tmp);

    save_frd(rt, tmp);
    tcg_temp_free_i64(tmp);
    return nullify_end(ctx);
}

static bool do_fop_dew(DisasContext *ctx, unsigned rt, unsigned ra,
                       void (*func)(TCGv_i64, TCGv_env, TCGv_i32))
{
    TCGv_i32 src;
    TCGv_i64 dst;

    nullify_over(ctx);
    src = load_frw0_i32(ra);
    dst = tcg_temp_new_i64();

    func(dst, cpu_env, src);

    tcg_temp_free_i32(src);
    save_frd(rt, dst);
    tcg_temp_free_i64(dst);
    return nullify_end(ctx);
}

static bool do_fop_weww(DisasContext *ctx, unsigned rt,
                        unsigned ra, unsigned rb,
                        void (*func)(TCGv_i32, TCGv_env, TCGv_i32, TCGv_i32))
{
    TCGv_i32 a, b;

    nullify_over(ctx);
    a = load_frw0_i32(ra);
    b = load_frw0_i32(rb);

    func(a, cpu_env, a, b);

    tcg_temp_free_i32(b);
    save_frw_i32(rt, a);
    tcg_temp_free_i32(a);
    return nullify_end(ctx);
}

static bool do_fop_dedd(DisasContext *ctx, unsigned rt,
                        unsigned ra, unsigned rb,
                        void (*func)(TCGv_i64, TCGv_env, TCGv_i64, TCGv_i64))
{
    TCGv_i64 a, b;

    nullify_over(ctx);
    a = load_frd0(ra);
    b = load_frd0(rb);

    func(a, cpu_env, a, b);

    tcg_temp_free_i64(b);
    save_frd(rt, a);
    tcg_temp_free_i64(a);
    return nullify_end(ctx);
}

/* Emit an unconditional branch to a direct target, which may or may not
   have already had nullification handled.  */
static bool do_dbranch(DisasContext *ctx, target_ureg dest,
                       unsigned link, bool is_n)
{
    if (ctx->null_cond.c == TCG_COND_NEVER && ctx->null_lab == NULL) {
        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }
        ctx->iaoq_n = dest;
        if (is_n) {
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
    } else {
        nullify_over(ctx);

        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }

        if (is_n && use_nullify_skip(ctx)) {
            nullify_set(ctx, 0);
            gen_goto_tb(ctx, 0, dest, dest + 4);
        } else {
            nullify_set(ctx, is_n);
            gen_goto_tb(ctx, 0, ctx->iaoq_b, dest);
        }

        nullify_end(ctx);

        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 1, ctx->iaoq_b, ctx->iaoq_n);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

/* Emit a conditional branch to a direct target.  If the branch itself
   is nullified, we should have already used nullify_over.  */
static bool do_cbranch(DisasContext *ctx, target_sreg disp, bool is_n,
                       DisasCond *cond)
{
    target_ureg dest = iaoq_dest(ctx, disp);
    TCGLabel *taken = NULL;
    TCGCond c = cond->c;
    bool n;

    assert(ctx->null_cond.c == TCG_COND_NEVER);

    /* Handle TRUE and NEVER as direct branches.  */
    if (c == TCG_COND_ALWAYS) {
        return do_dbranch(ctx, dest, 0, is_n && disp >= 0);
    }
    if (c == TCG_COND_NEVER) {
        return do_dbranch(ctx, ctx->iaoq_n, 0, is_n && disp < 0);
    }

    taken = gen_new_label();
    tcg_gen_brcond_reg(c, cond->a0, cond->a1, taken);
    cond_free(cond);

    /* Not taken: Condition not satisfied; nullify on backward branches. */
    n = is_n && disp < 0;
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 0, ctx->iaoq_n, ctx->iaoq_n + 4);
    } else {
        if (!n && ctx->null_lab) {
            gen_set_label(ctx->null_lab);
            ctx->null_lab = NULL;
        }
        nullify_set(ctx, n);
        if (ctx->iaoq_n == -1) {
            /* The temporary iaoq_n_var died at the branch above.
               Regenerate it here instead of saving it.  */
            tcg_gen_addi_reg(ctx->iaoq_n_var, cpu_iaoq_b, 4);
        }
        gen_goto_tb(ctx, 0, ctx->iaoq_b, ctx->iaoq_n);
    }

    gen_set_label(taken);

    /* Taken: Condition satisfied; nullify on forward branches.  */
    n = is_n && disp >= 0;
    if (n && use_nullify_skip(ctx)) {
        nullify_set(ctx, 0);
        gen_goto_tb(ctx, 1, dest, dest + 4);
    } else {
        nullify_set(ctx, n);
        gen_goto_tb(ctx, 1, ctx->iaoq_b, dest);
    }

    /* Not taken: the branch itself was nullified.  */
    if (ctx->null_lab) {
        gen_set_label(ctx->null_lab);
        ctx->null_lab = NULL;
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    } else {
        ctx->base.is_jmp = DISAS_NORETURN;
    }
    return true;
}

/* Emit an unconditional branch to an indirect target.  This handles
   nullification of the branch itself.  */
static bool do_ibranch(DisasContext *ctx, TCGv_reg dest,
                       unsigned link, bool is_n)
{
    TCGv_reg a0, a1, next, tmp;
    TCGCond c;

    assert(ctx->null_lab == NULL);

    if (ctx->null_cond.c == TCG_COND_NEVER) {
        if (link != 0) {
            copy_iaoq_entry(cpu_gr[link], ctx->iaoq_n, ctx->iaoq_n_var);
        }
        next = get_temp(ctx);
        tcg_gen_mov_reg(next, dest);
        if (is_n) {
            if (use_nullify_skip(ctx)) {
                tcg_gen_mov_reg(cpu_iaoq_f, next);
                tcg_gen_addi_reg(cpu_iaoq_b, next, 4);
                nullify_set(ctx, 0);
                ctx->base.is_jmp = DISAS_IAQ_N_UPDATED;
                return true;
            }
            ctx->null_cond.c = TCG_COND_ALWAYS;
        }
        ctx->iaoq_n = -1;
        ctx->iaoq_n_var = next;
    } else if (is_n && use_nullify_skip(ctx)) {
        /* The (conditional) branch, B, nullifies the next insn, N,
           and we're allowed to skip execution N (no single-step or
           tracepoint in effect).  Since the goto_ptr that we must use
           for the indirect branch consumes no special resources, we
           can (conditionally) skip B and continue execution.  */
        /* The use_nullify_skip test implies we have a known control path.  */
        tcg_debug_assert(ctx->iaoq_b != -1);
        tcg_debug_assert(ctx->iaoq_n != -1);

        /* We do have to handle the non-local temporary, DEST, before
           branching.  Since IOAQ_F is not really live at this point, we
           can simply store DEST optimistically.  Similarly with IAOQ_B.  */
        tcg_gen_mov_reg(cpu_iaoq_f, dest);
        tcg_gen_addi_reg(cpu_iaoq_b, dest, 4);

        nullify_over(ctx);
        if (link != 0) {
            tcg_gen_movi_reg(cpu_gr[link], ctx->iaoq_n);
        }
        tcg_gen_lookup_and_goto_ptr();
        return nullify_end(ctx);
    } else {
        c = ctx->null_cond.c;
        a0 = ctx->null_cond.a0;
        a1 = ctx->null_cond.a1;

        tmp = tcg_temp_new();
        next = get_temp(ctx);

        copy_iaoq_entry(tmp, ctx->iaoq_n, ctx->iaoq_n_var);
        tcg_gen_movcond_reg(c, next, a0, a1, tmp, dest);
        ctx->iaoq_n = -1;
        ctx->iaoq_n_var = next;

        if (link != 0) {
            tcg_gen_movcond_reg(c, cpu_gr[link], a0, a1, cpu_gr[link], tmp);
        }

        if (is_n) {
            /* The branch nullifies the next insn, which means the state of N
               after the branch is the inverse of the state of N that applied
               to the branch.  */
            tcg_gen_setcond_reg(tcg_invert_cond(c), cpu_psw_n, a0, a1);
            cond_free(&ctx->null_cond);
            ctx->null_cond = cond_make_n();
            ctx->psw_n_nonzero = true;
        } else {
            cond_free(&ctx->null_cond);
        }
    }
    return true;
}

/* Implement
 *    if (IAOQ_Front{30..31} < GR[b]{30..31})
 *      IAOQ_Next{30..31} ← GR[b]{30..31};
 *    else
 *      IAOQ_Next{30..31} ← IAOQ_Front{30..31};
 * which keeps the privilege level from being increased.
 */
static TCGv_reg do_ibranch_priv(DisasContext *ctx, TCGv_reg offset)
{
    TCGv_reg dest;
    switch (ctx->privilege) {
    case 0:
        /* Privilege 0 is maximum and is allowed to decrease.  */
        return offset;
    case 3:
        /* Privilege 3 is minimum and is never allowed to increase.  */
        dest = get_temp(ctx);
        tcg_gen_ori_reg(dest, offset, 3);
        break;
    default:
        dest = get_temp(ctx);
        tcg_gen_andi_reg(dest, offset, -4);
        tcg_gen_ori_reg(dest, dest, ctx->privilege);
        tcg_gen_movcond_reg(TCG_COND_GTU, dest, dest, offset, dest, offset);
        break;
    }
    return dest;
}

#ifdef CONFIG_USER_ONLY
/* On Linux, page zero is normally marked execute only + gateway.
   Therefore normal read or write is supposed to fail, but specific
   offsets have kernel code mapped to raise permissions to implement
   system calls.  Handling this via an explicit check here, rather
   in than the "be disp(sr2,r0)" instruction that probably sent us
   here, is the easiest way to handle the branch delay slot on the
   aforementioned BE.  */
static void do_page_zero(DisasContext *ctx)
{
    /* If by some means we get here with PSW[N]=1, that implies that
       the B,GATE instruction would be skipped, and we'd fault on the
       next insn within the privilaged page.  */
    switch (ctx->null_cond.c) {
    case TCG_COND_NEVER:
        break;
    case TCG_COND_ALWAYS:
        tcg_gen_movi_reg(cpu_psw_n, 0);
        goto do_sigill;
    default:
        /* Since this is always the first (and only) insn within the
           TB, we should know the state of PSW[N] from TB->FLAGS.  */
        g_assert_not_reached();
    }

    /* Check that we didn't arrive here via some means that allowed
       non-sequential instruction execution.  Normally the PSW[B] bit
       detects this by disallowing the B,GATE instruction to execute
       under such conditions.  */
    if (ctx->iaoq_b != ctx->iaoq_f + 4) {
        goto do_sigill;
    }

    switch (ctx->iaoq_f & -4) {
    case 0x00: /* Null pointer call */
        gen_excp_1(EXCP_IMP);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    case 0xb0: /* LWS */
        gen_excp_1(EXCP_SYSCALL_LWS);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    case 0xe0: /* SET_THREAD_POINTER */
        tcg_gen_st_reg(cpu_gr[26], cpu_env, offsetof(CPUHPPAState, cr[27]));
        tcg_gen_ori_reg(cpu_iaoq_f, cpu_gr[31], 3);
        tcg_gen_addi_reg(cpu_iaoq_b, cpu_iaoq_f, 4);
        ctx->base.is_jmp = DISAS_IAQ_N_UPDATED;
        break;

    case 0x100: /* SYSCALL */
        gen_excp_1(EXCP_SYSCALL);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;

    default:
    do_sigill:
        gen_excp_1(EXCP_ILL);
        ctx->base.is_jmp = DISAS_NORETURN;
        break;
    }
}
#endif

static bool trans_nop(DisasContext *ctx, arg_nop *a)
{
    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_break(DisasContext *ctx, arg_break *a)
{
    return gen_excp_iir(ctx, EXCP_BREAK);
}

static bool trans_sync(DisasContext *ctx, arg_sync *a)
{
    /* No point in nullifying the memory barrier.  */
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_mfia(DisasContext *ctx, arg_mfia *a)
{
    unsigned rt = a->t;
    TCGv_reg tmp = dest_gpr(ctx, rt);
    tcg_gen_movi_reg(tmp, ctx->iaoq_f);
    save_gpr(ctx, rt, tmp);

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_mfsp(DisasContext *ctx, arg_mfsp *a)
{
    unsigned rt = a->t;
    unsigned rs = a->sp;
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_reg t1 = tcg_temp_new();

    load_spr(ctx, t0, rs);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_reg(t1, t0);

    save_gpr(ctx, rt, t1);
    tcg_temp_free(t1);
    tcg_temp_free_i64(t0);

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_mfctl(DisasContext *ctx, arg_mfctl *a)
{
    unsigned rt = a->t;
    unsigned ctl = a->r;
    TCGv_reg tmp;

    switch (ctl) {
    case CR_SAR:
#ifdef TARGET_HPPA64
        if (a->e == 0) {
            /* MFSAR without ,W masks low 5 bits.  */
            tmp = dest_gpr(ctx, rt);
            tcg_gen_andi_reg(tmp, cpu_sar, 31);
            save_gpr(ctx, rt, tmp);
            goto done;
        }
#endif
        save_gpr(ctx, rt, cpu_sar);
        goto done;
    case CR_IT: /* Interval Timer */
        /* FIXME: Respect PSW_S bit.  */
        nullify_over(ctx);
        tmp = dest_gpr(ctx, rt);
        if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
            gen_io_start();
            gen_helper_read_interval_timer(tmp);
            ctx->base.is_jmp = DISAS_IAQ_N_STALE;
        } else {
            gen_helper_read_interval_timer(tmp);
        }
        save_gpr(ctx, rt, tmp);
        return nullify_end(ctx);
    case 26:
    case 27:
        break;
    default:
        /* All other control registers are privileged.  */
        CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);
        break;
    }

    tmp = get_temp(ctx);
    tcg_gen_ld_reg(tmp, cpu_env, offsetof(CPUHPPAState, cr[ctl]));
    save_gpr(ctx, rt, tmp);

 done:
    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_mtsp(DisasContext *ctx, arg_mtsp *a)
{
    unsigned rr = a->r;
    unsigned rs = a->sp;
    TCGv_i64 t64;

    if (rs >= 5) {
        CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);
    }
    nullify_over(ctx);

    t64 = tcg_temp_new_i64();
    tcg_gen_extu_reg_i64(t64, load_gpr(ctx, rr));
    tcg_gen_shli_i64(t64, t64, 32);

    if (rs >= 4) {
        tcg_gen_st_i64(t64, cpu_env, offsetof(CPUHPPAState, sr[rs]));
        ctx->tb_flags &= ~TB_FLAG_SR_SAME;
    } else {
        tcg_gen_mov_i64(cpu_sr[rs], t64);
    }
    tcg_temp_free_i64(t64);

    return nullify_end(ctx);
}

static bool trans_mtctl(DisasContext *ctx, arg_mtctl *a)
{
    unsigned ctl = a->t;
    TCGv_reg reg;
    TCGv_reg tmp;

    if (ctl == CR_SAR) {
        reg = load_gpr(ctx, a->r);
        tmp = tcg_temp_new();
        tcg_gen_andi_reg(tmp, reg, TARGET_REGISTER_BITS - 1);
        save_or_nullify(ctx, cpu_sar, tmp);
        tcg_temp_free(tmp);

        cond_free(&ctx->null_cond);
        return true;
    }

    /* All other control registers are privileged or read-only.  */
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_REG);

#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    reg = load_gpr(ctx, a->r);

    switch (ctl) {
    case CR_IT:
        gen_helper_write_interval_timer(cpu_env, reg);
        break;
    case CR_EIRR:
        gen_helper_write_eirr(cpu_env, reg);
        break;
    case CR_EIEM:
        gen_helper_write_eiem(cpu_env, reg);
        ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
        break;

    case CR_IIASQ:
    case CR_IIAOQ:
        /* FIXME: Respect PSW_Q bit */
        /* The write advances the queue and stores to the back element.  */
        tmp = get_temp(ctx);
        tcg_gen_ld_reg(tmp, cpu_env,
                       offsetof(CPUHPPAState, cr_back[ctl - CR_IIASQ]));
        tcg_gen_st_reg(tmp, cpu_env, offsetof(CPUHPPAState, cr[ctl]));
        tcg_gen_st_reg(reg, cpu_env,
                       offsetof(CPUHPPAState, cr_back[ctl - CR_IIASQ]));
        break;

    case CR_PID1:
    case CR_PID2:
    case CR_PID3:
    case CR_PID4:
        tcg_gen_st_reg(reg, cpu_env, offsetof(CPUHPPAState, cr[ctl]));
#ifndef CONFIG_USER_ONLY
        gen_helper_change_prot_id(cpu_env);
#endif
        break;

    default:
        tcg_gen_st_reg(reg, cpu_env, offsetof(CPUHPPAState, cr[ctl]));
        break;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_mtsarcm(DisasContext *ctx, arg_mtsarcm *a)
{
    TCGv_reg tmp = tcg_temp_new();

    tcg_gen_not_reg(tmp, load_gpr(ctx, a->r));
    tcg_gen_andi_reg(tmp, tmp, TARGET_REGISTER_BITS - 1);
    save_or_nullify(ctx, cpu_sar, tmp);
    tcg_temp_free(tmp);

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_ldsid(DisasContext *ctx, arg_ldsid *a)
{
    TCGv_reg dest = dest_gpr(ctx, a->t);

#ifdef CONFIG_USER_ONLY
    /* We don't implement space registers in user mode. */
    tcg_gen_movi_reg(dest, 0);
#else
    TCGv_i64 t0 = tcg_temp_new_i64();

    tcg_gen_mov_i64(t0, space_select(ctx, a->sp, load_gpr(ctx, a->b)));
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_reg(dest, t0);

    tcg_temp_free_i64(t0);
#endif
    save_gpr(ctx, a->t, dest);

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_rsm(DisasContext *ctx, arg_rsm *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_reg tmp;

    nullify_over(ctx);

    tmp = get_temp(ctx);
    tcg_gen_ld_reg(tmp, cpu_env, offsetof(CPUHPPAState, psw));
    tcg_gen_andi_reg(tmp, tmp, ~a->i);
    gen_helper_swap_system_mask(tmp, cpu_env, tmp);
    save_gpr(ctx, a->t, tmp);

    /* Exit the TB to recognize new interrupts, e.g. PSW_M.  */
    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool trans_ssm(DisasContext *ctx, arg_ssm *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_reg tmp;

    nullify_over(ctx);

    tmp = get_temp(ctx);
    tcg_gen_ld_reg(tmp, cpu_env, offsetof(CPUHPPAState, psw));
    tcg_gen_ori_reg(tmp, tmp, a->i);
    gen_helper_swap_system_mask(tmp, cpu_env, tmp);
    save_gpr(ctx, a->t, tmp);

    /* Exit the TB to recognize new interrupts, e.g. PSW_I.  */
    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool trans_mtsm(DisasContext *ctx, arg_mtsm *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_reg tmp, reg;
    nullify_over(ctx);

    reg = load_gpr(ctx, a->r);
    tmp = get_temp(ctx);
    gen_helper_swap_system_mask(tmp, cpu_env, reg);

    /* Exit the TB to recognize new interrupts.  */
    ctx->base.is_jmp = DISAS_IAQ_N_STALE_EXIT;
    return nullify_end(ctx);
#endif
}

static bool do_rfi(DisasContext *ctx, bool rfi_r)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);

    if (rfi_r) {
        gen_helper_rfi_r(cpu_env);
    } else {
        gen_helper_rfi(cpu_env);
    }
    /* Exit the TB to recognize new interrupts.  */
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;

    return nullify_end(ctx);
#endif
}

static bool trans_rfi(DisasContext *ctx, arg_rfi *a)
{
    return do_rfi(ctx, false);
}

static bool trans_rfi_r(DisasContext *ctx, arg_rfi_r *a)
{
    return do_rfi(ctx, true);
}

static bool trans_halt(DisasContext *ctx, arg_halt *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    gen_helper_halt(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

static bool trans_reset(DisasContext *ctx, arg_reset *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    gen_helper_reset(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

static bool trans_getshadowregs(DisasContext *ctx, arg_getshadowregs *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    nullify_over(ctx);
    gen_helper_getshadowregs(cpu_env);
    return nullify_end(ctx);
#endif
}

static bool trans_nop_addrx(DisasContext *ctx, arg_ldst *a)
{
    if (a->m) {
        TCGv_reg dest = dest_gpr(ctx, a->b);
        TCGv_reg src1 = load_gpr(ctx, a->b);
        TCGv_reg src2 = load_gpr(ctx, a->x);

        /* The only thing we need to do is the base register modification.  */
        tcg_gen_add_reg(dest, src1, src2);
        save_gpr(ctx, a->b, dest);
    }
    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_probe(DisasContext *ctx, arg_probe *a)
{
    TCGv_reg dest, ofs;
    TCGv_i32 level, want;
    TCGv_tl addr;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->t);
    form_gva(ctx, &addr, &ofs, a->b, 0, 0, 0, a->sp, 0, false);

    if (a->imm) {
        level = tcg_constant_i32(a->ri);
    } else {
        level = tcg_temp_new_i32();
        tcg_gen_trunc_reg_i32(level, load_gpr(ctx, a->ri));
        tcg_gen_andi_i32(level, level, 3);
    }
    want = tcg_constant_i32(a->write ? PAGE_WRITE : PAGE_READ);

    gen_helper_probe(dest, cpu_env, addr, level, want);

    tcg_temp_free_i32(level);

    save_gpr(ctx, a->t, dest);
    return nullify_end(ctx);
}

static bool trans_ixtlbx(DisasContext *ctx, arg_ixtlbx *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_tl addr;
    TCGv_reg ofs, reg;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, 0, 0, 0, a->sp, 0, false);
    reg = load_gpr(ctx, a->r);
    if (a->addr) {
        gen_helper_itlba(cpu_env, addr, reg);
    } else {
        gen_helper_itlbp(cpu_env, addr, reg);
    }

    /* Exit TB for TLB change if mmu is enabled.  */
    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_pxtlbx(DisasContext *ctx, arg_pxtlbx *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_tl addr;
    TCGv_reg ofs;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, a->x, 0, 0, a->sp, a->m, false);
    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }
    if (a->local) {
        gen_helper_ptlbe(cpu_env);
    } else {
        gen_helper_ptlb(cpu_env, addr);
    }

    /* Exit TB for TLB change if mmu is enabled.  */
    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

/*
 * Implement the pcxl and pcxl2 Fast TLB Insert instructions.
 * See
 *     https://parisc.wiki.kernel.org/images-parisc/a/a9/Pcxl2_ers.pdf
 *     page 13-9 (195/206)
 */
static bool trans_ixtlbxf(DisasContext *ctx, arg_ixtlbxf *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_tl addr, atl, stl;
    TCGv_reg reg;

    nullify_over(ctx);

    /*
     * FIXME:
     *  if (not (pcxl or pcxl2))
     *    return gen_illegal(ctx);
     *
     * Note for future: these are 32-bit systems; no hppa64.
     */

    atl = tcg_temp_new_tl();
    stl = tcg_temp_new_tl();
    addr = tcg_temp_new_tl();

    tcg_gen_ld32u_i64(stl, cpu_env,
                      a->data ? offsetof(CPUHPPAState, cr[CR_ISR])
                      : offsetof(CPUHPPAState, cr[CR_IIASQ]));
    tcg_gen_ld32u_i64(atl, cpu_env,
                      a->data ? offsetof(CPUHPPAState, cr[CR_IOR])
                      : offsetof(CPUHPPAState, cr[CR_IIAOQ]));
    tcg_gen_shli_i64(stl, stl, 32);
    tcg_gen_or_tl(addr, atl, stl);
    tcg_temp_free_tl(atl);
    tcg_temp_free_tl(stl);

    reg = load_gpr(ctx, a->r);
    if (a->addr) {
        gen_helper_itlba(cpu_env, addr, reg);
    } else {
        gen_helper_itlbp(cpu_env, addr, reg);
    }
    tcg_temp_free_tl(addr);

    /* Exit TB for TLB change if mmu is enabled.  */
    if (ctx->tb_flags & PSW_C) {
        ctx->base.is_jmp = DISAS_IAQ_N_STALE;
    }
    return nullify_end(ctx);
#endif
}

static bool trans_lpa(DisasContext *ctx, arg_ldst *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
#ifndef CONFIG_USER_ONLY
    TCGv_tl vaddr;
    TCGv_reg ofs, paddr;

    nullify_over(ctx);

    form_gva(ctx, &vaddr, &ofs, a->b, a->x, 0, 0, a->sp, a->m, false);

    paddr = tcg_temp_new();
    gen_helper_lpa(paddr, cpu_env, vaddr);

    /* Note that physical address result overrides base modification.  */
    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }
    save_gpr(ctx, a->t, paddr);
    tcg_temp_free(paddr);

    return nullify_end(ctx);
#endif
}

static bool trans_lci(DisasContext *ctx, arg_lci *a)
{
    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);

    /* The Coherence Index is an implementation-defined function of the
       physical address.  Two addresses with the same CI have a coherent
       view of the cache.  Our implementation is to return 0 for all,
       since the entire address space is coherent.  */
    save_gpr(ctx, a->t, tcg_constant_reg(0));

    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_add(DisasContext *ctx, arg_rrr_cf_sh *a)
{
    return do_add_reg(ctx, a, false, false, false, false);
}

static bool trans_add_l(DisasContext *ctx, arg_rrr_cf_sh *a)
{
    return do_add_reg(ctx, a, true, false, false, false);
}

static bool trans_add_tsv(DisasContext *ctx, arg_rrr_cf_sh *a)
{
    return do_add_reg(ctx, a, false, true, false, false);
}

static bool trans_add_c(DisasContext *ctx, arg_rrr_cf_sh *a)
{
    return do_add_reg(ctx, a, false, false, false, true);
}

static bool trans_add_c_tsv(DisasContext *ctx, arg_rrr_cf_sh *a)
{
    return do_add_reg(ctx, a, false, true, false, true);
}

static bool trans_sub(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, false, false, false);
}

static bool trans_sub_tsv(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, true, false, false);
}

static bool trans_sub_tc(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, false, false, true);
}

static bool trans_sub_tsv_tc(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, true, false, true);
}

static bool trans_sub_b(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, false, true, false);
}

static bool trans_sub_b_tsv(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_sub_reg(ctx, a, true, true, false);
}

static bool trans_andcm(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_log_reg(ctx, a, tcg_gen_andc_reg);
}

static bool trans_and(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_log_reg(ctx, a, tcg_gen_and_reg);
}

static bool trans_or(DisasContext *ctx, arg_rrr_cf *a)
{
    if (a->cf == 0) {
        unsigned r2 = a->r2;
        unsigned r1 = a->r1;
        unsigned rt = a->t;

        if (rt == 0) { /* NOP */
            cond_free(&ctx->null_cond);
            return true;
        }
        if (r2 == 0) { /* COPY */
            if (r1 == 0) {
                TCGv_reg dest = dest_gpr(ctx, rt);
                tcg_gen_movi_reg(dest, 0);
                save_gpr(ctx, rt, dest);
            } else {
                save_gpr(ctx, rt, cpu_gr[r1]);
            }
            cond_free(&ctx->null_cond);
            return true;
        }
#ifndef CONFIG_USER_ONLY
        /* These are QEMU extensions and are nops in the real architecture:
         *
         * or %r10,%r10,%r10 -- idle loop; wait for interrupt
         * or %r31,%r31,%r31 -- death loop; offline cpu
         *                      currently implemented as idle.
         */
        if ((rt == 10 || rt == 31) && r1 == rt && r2 == rt) { /* PAUSE */
            /* No need to check for supervisor, as userland can only pause
               until the next timer interrupt.  */
            nullify_over(ctx);

            /* Advance the instruction queue.  */
            copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_b, cpu_iaoq_b);
            copy_iaoq_entry(cpu_iaoq_b, ctx->iaoq_n, ctx->iaoq_n_var);
            nullify_set(ctx, 0);

            /* Tell the qemu main loop to halt until this cpu has work.  */
            tcg_gen_st_i32(tcg_constant_i32(1), cpu_env,
                           offsetof(CPUState, halted) - offsetof(HPPACPU, env));
            gen_excp_1(EXCP_HALTED);
            ctx->base.is_jmp = DISAS_NORETURN;

            return nullify_end(ctx);
        }
#endif
    }
    return do_log_reg(ctx, a, tcg_gen_or_reg);
}

static bool trans_xor(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_log_reg(ctx, a, tcg_gen_xor_reg);
}

static bool trans_cmpclr(DisasContext *ctx, arg_rrr_cf *a)
{
    TCGv_reg tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_cmpclr(ctx, a->t, tcg_r1, tcg_r2, a->cf);
    return nullify_end(ctx);
}

static bool trans_uxor(DisasContext *ctx, arg_rrr_cf *a)
{
    TCGv_reg tcg_r1, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    do_unit(ctx, a->t, tcg_r1, tcg_r2, a->cf, false, tcg_gen_xor_reg);
    return nullify_end(ctx);
}

static bool do_uaddcm(DisasContext *ctx, arg_rrr_cf *a, bool is_tc)
{
    TCGv_reg tcg_r1, tcg_r2, tmp;

    if (a->cf) {
        nullify_over(ctx);
    }
    tcg_r1 = load_gpr(ctx, a->r1);
    tcg_r2 = load_gpr(ctx, a->r2);
    tmp = get_temp(ctx);
    tcg_gen_not_reg(tmp, tcg_r2);
    do_unit(ctx, a->t, tcg_r1, tmp, a->cf, is_tc, tcg_gen_add_reg);
    return nullify_end(ctx);
}

static bool trans_uaddcm(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_uaddcm(ctx, a, false);
}

static bool trans_uaddcm_tc(DisasContext *ctx, arg_rrr_cf *a)
{
    return do_uaddcm(ctx, a, true);
}

static bool do_dcor(DisasContext *ctx, arg_rr_cf *a, bool is_i)
{
    TCGv_reg tmp;

    nullify_over(ctx);

    tmp = get_temp(ctx);
    tcg_gen_shri_reg(tmp, cpu_psw_cb, 3);
    if (!is_i) {
        tcg_gen_not_reg(tmp, tmp);
    }
    tcg_gen_andi_reg(tmp, tmp, 0x11111111);
    tcg_gen_muli_reg(tmp, tmp, 6);
    do_unit(ctx, a->t, load_gpr(ctx, a->r), tmp, a->cf, false,
            is_i ? tcg_gen_add_reg : tcg_gen_sub_reg);
    return nullify_end(ctx);
}

static bool trans_dcor(DisasContext *ctx, arg_rr_cf *a)
{
    return do_dcor(ctx, a, false);
}

static bool trans_dcor_i(DisasContext *ctx, arg_rr_cf *a)
{
    return do_dcor(ctx, a, true);
}

static bool trans_ds(DisasContext *ctx, arg_rrr_cf *a)
{
    TCGv_reg dest, add1, add2, addc, zero, in1, in2;

    nullify_over(ctx);

    in1 = load_gpr(ctx, a->r1);
    in2 = load_gpr(ctx, a->r2);

    add1 = tcg_temp_new();
    add2 = tcg_temp_new();
    addc = tcg_temp_new();
    dest = tcg_temp_new();
    zero = tcg_constant_reg(0);

    /* Form R1 << 1 | PSW[CB]{8}.  */
    tcg_gen_add_reg(add1, in1, in1);
    tcg_gen_add_reg(add1, add1, cpu_psw_cb_msb);

    /* Add or subtract R2, depending on PSW[V].  Proper computation of
       carry{8} requires that we subtract via + ~R2 + 1, as described in
       the manual.  By extracting and masking V, we can produce the
       proper inputs to the addition without movcond.  */
    tcg_gen_sari_reg(addc, cpu_psw_v, TARGET_REGISTER_BITS - 1);
    tcg_gen_xor_reg(add2, in2, addc);
    tcg_gen_andi_reg(addc, addc, 1);
    /* ??? This is only correct for 32-bit.  */
    tcg_gen_add2_i32(dest, cpu_psw_cb_msb, add1, zero, add2, zero);
    tcg_gen_add2_i32(dest, cpu_psw_cb_msb, dest, cpu_psw_cb_msb, addc, zero);

    tcg_temp_free(addc);

    /* Write back the result register.  */
    save_gpr(ctx, a->t, dest);

    /* Write back PSW[CB].  */
    tcg_gen_xor_reg(cpu_psw_cb, add1, add2);
    tcg_gen_xor_reg(cpu_psw_cb, cpu_psw_cb, dest);

    /* Write back PSW[V] for the division step.  */
    tcg_gen_neg_reg(cpu_psw_v, cpu_psw_cb_msb);
    tcg_gen_xor_reg(cpu_psw_v, cpu_psw_v, in2);

    /* Install the new nullification.  */
    if (a->cf) {
        TCGv_reg sv = NULL;
        if (cond_need_sv(a->cf >> 1)) {
            /* ??? The lshift is supposed to contribute to overflow.  */
            sv = do_add_sv(ctx, dest, add1, add2);
        }
        ctx->null_cond = do_cond(a->cf, dest, cpu_psw_cb_msb, sv);
    }

    tcg_temp_free(add1);
    tcg_temp_free(add2);
    tcg_temp_free(dest);

    return nullify_end(ctx);
}

static bool trans_addi(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, false, false);
}

static bool trans_addi_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, true, false);
}

static bool trans_addi_tc(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, false, true);
}

static bool trans_addi_tc_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_add_imm(ctx, a, true, true);
}

static bool trans_subi(DisasContext *ctx, arg_rri_cf *a)
{
    return do_sub_imm(ctx, a, false);
}

static bool trans_subi_tsv(DisasContext *ctx, arg_rri_cf *a)
{
    return do_sub_imm(ctx, a, true);
}

static bool trans_cmpiclr(DisasContext *ctx, arg_rri_cf *a)
{
    TCGv_reg tcg_im, tcg_r2;

    if (a->cf) {
        nullify_over(ctx);
    }

    tcg_im = load_const(ctx, a->i);
    tcg_r2 = load_gpr(ctx, a->r);
    do_cmpclr(ctx, a->t, tcg_im, tcg_r2, a->cf);

    return nullify_end(ctx);
}

static bool trans_ld(DisasContext *ctx, arg_ldst *a)
{
    return do_load(ctx, a->t, a->b, a->x, a->scale ? a->size : 0,
                   a->disp, a->sp, a->m, a->size | MO_TE);
}

static bool trans_st(DisasContext *ctx, arg_ldst *a)
{
    assert(a->x == 0 && a->scale == 0);
    return do_store(ctx, a->t, a->b, a->disp, a->sp, a->m, a->size | MO_TE);
}

static bool trans_ldc(DisasContext *ctx, arg_ldst *a)
{
    MemOp mop = MO_TE | MO_ALIGN | a->size;
    TCGv_reg zero, dest, ofs;
    TCGv_tl addr;

    nullify_over(ctx);

    if (a->m) {
        /* Base register modification.  Make sure if RT == RB,
           we see the result of the load.  */
        dest = get_temp(ctx);
    } else {
        dest = dest_gpr(ctx, a->t);
    }

    form_gva(ctx, &addr, &ofs, a->b, a->x, a->scale ? a->size : 0,
             a->disp, a->sp, a->m, ctx->mmu_idx == MMU_PHYS_IDX);

    /*
     * For hppa1.1, LDCW is undefined unless aligned mod 16.
     * However actual hardware succeeds with aligned mod 4.
     * Detect this case and log a GUEST_ERROR.
     *
     * TODO: HPPA64 relaxes the over-alignment requirement
     * with the ,co completer.
     */
    gen_helper_ldc_check(addr);

    zero = tcg_constant_reg(0);
    tcg_gen_atomic_xchg_reg(dest, addr, zero, ctx->mmu_idx, mop);

    if (a->m) {
        save_gpr(ctx, a->b, ofs);
    }
    save_gpr(ctx, a->t, dest);

    return nullify_end(ctx);
}

static bool trans_stby(DisasContext *ctx, arg_stby *a)
{
    TCGv_reg ofs, val;
    TCGv_tl addr;

    nullify_over(ctx);

    form_gva(ctx, &addr, &ofs, a->b, 0, 0, a->disp, a->sp, a->m,
             ctx->mmu_idx == MMU_PHYS_IDX);
    val = load_gpr(ctx, a->r);
    if (a->a) {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stby_e_parallel(cpu_env, addr, val);
        } else {
            gen_helper_stby_e(cpu_env, addr, val);
        }
    } else {
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            gen_helper_stby_b_parallel(cpu_env, addr, val);
        } else {
            gen_helper_stby_b(cpu_env, addr, val);
        }
    }
    if (a->m) {
        tcg_gen_andi_reg(ofs, ofs, ~3);
        save_gpr(ctx, a->b, ofs);
    }

    return nullify_end(ctx);
}

static bool trans_lda(DisasContext *ctx, arg_ldst *a)
{
    int hold_mmu_idx = ctx->mmu_idx;

    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    ctx->mmu_idx = MMU_PHYS_IDX;
    trans_ld(ctx, a);
    ctx->mmu_idx = hold_mmu_idx;
    return true;
}

static bool trans_sta(DisasContext *ctx, arg_ldst *a)
{
    int hold_mmu_idx = ctx->mmu_idx;

    CHECK_MOST_PRIVILEGED(EXCP_PRIV_OPR);
    ctx->mmu_idx = MMU_PHYS_IDX;
    trans_st(ctx, a);
    ctx->mmu_idx = hold_mmu_idx;
    return true;
}

static bool trans_ldil(DisasContext *ctx, arg_ldil *a)
{
    TCGv_reg tcg_rt = dest_gpr(ctx, a->t);

    tcg_gen_movi_reg(tcg_rt, a->i);
    save_gpr(ctx, a->t, tcg_rt);
    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_addil(DisasContext *ctx, arg_addil *a)
{
    TCGv_reg tcg_rt = load_gpr(ctx, a->r);
    TCGv_reg tcg_r1 = dest_gpr(ctx, 1);

    tcg_gen_addi_reg(tcg_r1, tcg_rt, a->i);
    save_gpr(ctx, 1, tcg_r1);
    cond_free(&ctx->null_cond);
    return true;
}

static bool trans_ldo(DisasContext *ctx, arg_ldo *a)
{
    TCGv_reg tcg_rt = dest_gpr(ctx, a->t);

    /* Special case rb == 0, for the LDI pseudo-op.
       The COPY pseudo-op is handled for free within tcg_gen_addi_tl.  */
    if (a->b == 0) {
        tcg_gen_movi_reg(tcg_rt, a->i);
    } else {
        tcg_gen_addi_reg(tcg_rt, cpu_gr[a->b], a->i);
    }
    save_gpr(ctx, a->t, tcg_rt);
    cond_free(&ctx->null_cond);
    return true;
}

static bool do_cmpb(DisasContext *ctx, unsigned r, TCGv_reg in1,
                    unsigned c, unsigned f, unsigned n, int disp)
{
    TCGv_reg dest, in2, sv;
    DisasCond cond;

    in2 = load_gpr(ctx, r);
    dest = get_temp(ctx);

    tcg_gen_sub_reg(dest, in1, in2);

    sv = NULL;
    if (cond_need_sv(c)) {
        sv = do_sub_sv(ctx, dest, in1, in2);
    }

    cond = do_sub_cond(c * 2 + f, dest, in1, in2, sv);
    return do_cbranch(ctx, disp, n, &cond);
}

static bool trans_cmpb(DisasContext *ctx, arg_cmpb *a)
{
    nullify_over(ctx);
    return do_cmpb(ctx, a->r2, load_gpr(ctx, a->r1), a->c, a->f, a->n, a->disp);
}

static bool trans_cmpbi(DisasContext *ctx, arg_cmpbi *a)
{
    nullify_over(ctx);
    return do_cmpb(ctx, a->r, load_const(ctx, a->i), a->c, a->f, a->n, a->disp);
}

static bool do_addb(DisasContext *ctx, unsigned r, TCGv_reg in1,
                    unsigned c, unsigned f, unsigned n, int disp)
{
    TCGv_reg dest, in2, sv, cb_msb;
    DisasCond cond;

    in2 = load_gpr(ctx, r);
    dest = tcg_temp_new();
    sv = NULL;
    cb_msb = NULL;

    if (cond_need_cb(c)) {
        cb_msb = get_temp(ctx);
        tcg_gen_movi_reg(cb_msb, 0);
        tcg_gen_add2_reg(dest, cb_msb, in1, cb_msb, in2, cb_msb);
    } else {
        tcg_gen_add_reg(dest, in1, in2);
    }
    if (cond_need_sv(c)) {
        sv = do_add_sv(ctx, dest, in1, in2);
    }

    cond = do_cond(c * 2 + f, dest, cb_msb, sv);
    save_gpr(ctx, r, dest);
    tcg_temp_free(dest);
    return do_cbranch(ctx, disp, n, &cond);
}

static bool trans_addb(DisasContext *ctx, arg_addb *a)
{
    nullify_over(ctx);
    return do_addb(ctx, a->r2, load_gpr(ctx, a->r1), a->c, a->f, a->n, a->disp);
}

static bool trans_addbi(DisasContext *ctx, arg_addbi *a)
{
    nullify_over(ctx);
    return do_addb(ctx, a->r, load_const(ctx, a->i), a->c, a->f, a->n, a->disp);
}

static bool trans_bb_sar(DisasContext *ctx, arg_bb_sar *a)
{
    TCGv_reg tmp, tcg_r;
    DisasCond cond;

    nullify_over(ctx);

    tmp = tcg_temp_new();
    tcg_r = load_gpr(ctx, a->r);
    tcg_gen_shl_reg(tmp, tcg_r, cpu_sar);

    cond = cond_make_0(a->c ? TCG_COND_GE : TCG_COND_LT, tmp);
    tcg_temp_free(tmp);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_bb_imm(DisasContext *ctx, arg_bb_imm *a)
{
    TCGv_reg tmp, tcg_r;
    DisasCond cond;

    nullify_over(ctx);

    tmp = tcg_temp_new();
    tcg_r = load_gpr(ctx, a->r);
    tcg_gen_shli_reg(tmp, tcg_r, a->p);

    cond = cond_make_0(a->c ? TCG_COND_GE : TCG_COND_LT, tmp);
    tcg_temp_free(tmp);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_movb(DisasContext *ctx, arg_movb *a)
{
    TCGv_reg dest;
    DisasCond cond;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->r2);
    if (a->r1 == 0) {
        tcg_gen_movi_reg(dest, 0);
    } else {
        tcg_gen_mov_reg(dest, cpu_gr[a->r1]);
    }

    cond = do_sed_cond(a->c, dest);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_movbi(DisasContext *ctx, arg_movbi *a)
{
    TCGv_reg dest;
    DisasCond cond;

    nullify_over(ctx);

    dest = dest_gpr(ctx, a->r);
    tcg_gen_movi_reg(dest, a->i);

    cond = do_sed_cond(a->c, dest);
    return do_cbranch(ctx, a->disp, a->n, &cond);
}

static bool trans_shrpw_sar(DisasContext *ctx, arg_shrpw_sar *a)
{
    TCGv_reg dest;

    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    if (a->r1 == 0) {
        tcg_gen_ext32u_reg(dest, load_gpr(ctx, a->r2));
        tcg_gen_shr_reg(dest, dest, cpu_sar);
    } else if (a->r1 == a->r2) {
        TCGv_i32 t32 = tcg_temp_new_i32();
        tcg_gen_trunc_reg_i32(t32, load_gpr(ctx, a->r2));
        tcg_gen_rotr_i32(t32, t32, cpu_sar);
        tcg_gen_extu_i32_reg(dest, t32);
        tcg_temp_free_i32(t32);
    } else {
        TCGv_i64 t = tcg_temp_new_i64();
        TCGv_i64 s = tcg_temp_new_i64();

        tcg_gen_concat_reg_i64(t, load_gpr(ctx, a->r2), load_gpr(ctx, a->r1));
        tcg_gen_extu_reg_i64(s, cpu_sar);
        tcg_gen_shr_i64(t, t, s);
        tcg_gen_trunc_i64_reg(dest, t);

        tcg_temp_free_i64(t);
        tcg_temp_free_i64(s);
    }
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_shrpw_imm(DisasContext *ctx, arg_shrpw_imm *a)
{
    unsigned sa = 31 - a->cpos;
    TCGv_reg dest, t2;

    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    t2 = load_gpr(ctx, a->r2);
    if (a->r1 == 0) {
        tcg_gen_extract_reg(dest, t2, sa, 32 - sa);
    } else if (TARGET_REGISTER_BITS == 32) {
        tcg_gen_extract2_reg(dest, t2, cpu_gr[a->r1], sa);
    } else if (a->r1 == a->r2) {
        TCGv_i32 t32 = tcg_temp_new_i32();
        tcg_gen_trunc_reg_i32(t32, t2);
        tcg_gen_rotri_i32(t32, t32, sa);
        tcg_gen_extu_i32_reg(dest, t32);
        tcg_temp_free_i32(t32);
    } else {
        TCGv_i64 t64 = tcg_temp_new_i64();
        tcg_gen_concat_reg_i64(t64, t2, cpu_gr[a->r1]);
        tcg_gen_shri_i64(t64, t64, sa);
        tcg_gen_trunc_i64_reg(dest, t64);
        tcg_temp_free_i64(t64);
    }
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_extrw_sar(DisasContext *ctx, arg_extrw_sar *a)
{
    unsigned len = 32 - a->clen;
    TCGv_reg dest, src, tmp;

    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    src = load_gpr(ctx, a->r);
    tmp = tcg_temp_new();

    /* Recall that SAR is using big-endian bit numbering.  */
    tcg_gen_xori_reg(tmp, cpu_sar, TARGET_REGISTER_BITS - 1);
    if (a->se) {
        tcg_gen_sar_reg(dest, src, tmp);
        tcg_gen_sextract_reg(dest, dest, 0, len);
    } else {
        tcg_gen_shr_reg(dest, src, tmp);
        tcg_gen_extract_reg(dest, dest, 0, len);
    }
    tcg_temp_free(tmp);
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_extrw_imm(DisasContext *ctx, arg_extrw_imm *a)
{
    unsigned len = 32 - a->clen;
    unsigned cpos = 31 - a->pos;
    TCGv_reg dest, src;

    if (a->c) {
        nullify_over(ctx);
    }

    dest = dest_gpr(ctx, a->t);
    src = load_gpr(ctx, a->r);
    if (a->se) {
        tcg_gen_sextract_reg(dest, src, cpos, len);
    } else {
        tcg_gen_extract_reg(dest, src, cpos, len);
    }
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_depwi_imm(DisasContext *ctx, arg_depwi_imm *a)
{
    unsigned len = 32 - a->clen;
    target_sreg mask0, mask1;
    TCGv_reg dest;

    if (a->c) {
        nullify_over(ctx);
    }
    if (a->cpos + len > 32) {
        len = 32 - a->cpos;
    }

    dest = dest_gpr(ctx, a->t);
    mask0 = deposit64(0, a->cpos, len, a->i);
    mask1 = deposit64(-1, a->cpos, len, a->i);

    if (a->nz) {
        TCGv_reg src = load_gpr(ctx, a->t);
        if (mask1 != -1) {
            tcg_gen_andi_reg(dest, src, mask1);
            src = dest;
        }
        tcg_gen_ori_reg(dest, src, mask0);
    } else {
        tcg_gen_movi_reg(dest, mask0);
    }
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_depw_imm(DisasContext *ctx, arg_depw_imm *a)
{
    unsigned rs = a->nz ? a->t : 0;
    unsigned len = 32 - a->clen;
    TCGv_reg dest, val;

    if (a->c) {
        nullify_over(ctx);
    }
    if (a->cpos + len > 32) {
        len = 32 - a->cpos;
    }

    dest = dest_gpr(ctx, a->t);
    val = load_gpr(ctx, a->r);
    if (rs == 0) {
        tcg_gen_deposit_z_reg(dest, val, a->cpos, len);
    } else {
        tcg_gen_deposit_reg(dest, cpu_gr[rs], val, a->cpos, len);
    }
    save_gpr(ctx, a->t, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (a->c) {
        ctx->null_cond = do_sed_cond(a->c, dest);
    }
    return nullify_end(ctx);
}

static bool do_depw_sar(DisasContext *ctx, unsigned rt, unsigned c,
                        unsigned nz, unsigned clen, TCGv_reg val)
{
    unsigned rs = nz ? rt : 0;
    unsigned len = 32 - clen;
    TCGv_reg mask, tmp, shift, dest;
    unsigned msb = 1U << (len - 1);

    dest = dest_gpr(ctx, rt);
    shift = tcg_temp_new();
    tmp = tcg_temp_new();

    /* Convert big-endian bit numbering in SAR to left-shift.  */
    tcg_gen_xori_reg(shift, cpu_sar, TARGET_REGISTER_BITS - 1);

    mask = tcg_const_reg(msb + (msb - 1));
    tcg_gen_and_reg(tmp, val, mask);
    if (rs) {
        tcg_gen_shl_reg(mask, mask, shift);
        tcg_gen_shl_reg(tmp, tmp, shift);
        tcg_gen_andc_reg(dest, cpu_gr[rs], mask);
        tcg_gen_or_reg(dest, dest, tmp);
    } else {
        tcg_gen_shl_reg(dest, tmp, shift);
    }
    tcg_temp_free(shift);
    tcg_temp_free(mask);
    tcg_temp_free(tmp);
    save_gpr(ctx, rt, dest);

    /* Install the new nullification.  */
    cond_free(&ctx->null_cond);
    if (c) {
        ctx->null_cond = do_sed_cond(c, dest);
    }
    return nullify_end(ctx);
}

static bool trans_depw_sar(DisasContext *ctx, arg_depw_sar *a)
{
    if (a->c) {
        nullify_over(ctx);
    }
    return do_depw_sar(ctx, a->t, a->c, a->nz, a->clen, load_gpr(ctx, a->r));
}

static bool trans_depwi_sar(DisasContext *ctx, arg_depwi_sar *a)
{
    if (a->c) {
        nullify_over(ctx);
    }
    return do_depw_sar(ctx, a->t, a->c, a->nz, a->clen, load_const(ctx, a->i));
}

static bool trans_be(DisasContext *ctx, arg_be *a)
{
    TCGv_reg tmp;

#ifdef CONFIG_USER_ONLY
    /* ??? It seems like there should be a good way of using
       "be disp(sr2, r0)", the canonical gateway entry mechanism
       to our advantage.  But that appears to be inconvenient to
       manage along side branch delay slots.  Therefore we handle
       entry into the gateway page via absolute address.  */
    /* Since we don't implement spaces, just branch.  Do notice the special
       case of "be disp(*,r0)" using a direct branch to disp, so that we can
       goto_tb to the TB containing the syscall.  */
    if (a->b == 0) {
        return do_dbranch(ctx, a->disp, a->l, a->n);
    }
#else
    nullify_over(ctx);
#endif

    tmp = get_temp(ctx);
    tcg_gen_addi_reg(tmp, load_gpr(ctx, a->b), a->disp);
    tmp = do_ibranch_priv(ctx, tmp);

#ifdef CONFIG_USER_ONLY
    return do_ibranch(ctx, tmp, a->l, a->n);
#else
    TCGv_i64 new_spc = tcg_temp_new_i64();

    load_spr(ctx, new_spc, a->sp);
    if (a->l) {
        copy_iaoq_entry(cpu_gr[31], ctx->iaoq_n, ctx->iaoq_n_var);
        tcg_gen_mov_i64(cpu_sr[0], cpu_iasq_f);
    }
    if (a->n && use_nullify_skip(ctx)) {
        tcg_gen_mov_reg(cpu_iaoq_f, tmp);
        tcg_gen_addi_reg(cpu_iaoq_b, cpu_iaoq_f, 4);
        tcg_gen_mov_i64(cpu_iasq_f, new_spc);
        tcg_gen_mov_i64(cpu_iasq_b, cpu_iasq_f);
    } else {
        copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_b, cpu_iaoq_b);
        if (ctx->iaoq_b == -1) {
            tcg_gen_mov_i64(cpu_iasq_f, cpu_iasq_b);
        }
        tcg_gen_mov_reg(cpu_iaoq_b, tmp);
        tcg_gen_mov_i64(cpu_iasq_b, new_spc);
        nullify_set(ctx, a->n);
    }
    tcg_temp_free_i64(new_spc);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

static bool trans_bl(DisasContext *ctx, arg_bl *a)
{
    return do_dbranch(ctx, iaoq_dest(ctx, a->disp), a->l, a->n);
}

static bool trans_b_gate(DisasContext *ctx, arg_b_gate *a)
{
    target_ureg dest = iaoq_dest(ctx, a->disp);

    nullify_over(ctx);

    /* Make sure the caller hasn't done something weird with the queue.
     * ??? This is not quite the same as the PSW[B] bit, which would be
     * expensive to track.  Real hardware will trap for
     *    b  gateway
     *    b  gateway+4  (in delay slot of first branch)
     * However, checking for a non-sequential instruction queue *will*
     * diagnose the security hole
     *    b  gateway
     *    b  evil
     * in which instructions at evil would run with increased privs.
     */
    if (ctx->iaoq_b == -1 || ctx->iaoq_b != ctx->iaoq_f + 4) {
        return gen_illegal(ctx);
    }

#ifndef CONFIG_USER_ONLY
    if (ctx->tb_flags & PSW_C) {
        CPUHPPAState *env = ctx->cs->env_ptr;
        int type = hppa_artype_for_page(env, ctx->base.pc_next);
        /* If we could not find a TLB entry, then we need to generate an
           ITLB miss exception so the kernel will provide it.
           The resulting TLB fill operation will invalidate this TB and
           we will re-translate, at which point we *will* be able to find
           the TLB entry and determine if this is in fact a gateway page.  */
        if (type < 0) {
            gen_excp(ctx, EXCP_ITLB_MISS);
            return true;
        }
        /* No change for non-gateway pages or for priv decrease.  */
        if (type >= 4 && type - 4 < ctx->privilege) {
            dest = deposit32(dest, 0, 2, type - 4);
        }
    } else {
        dest &= -4;  /* priv = 0 */
    }
#endif

    if (a->l) {
        TCGv_reg tmp = dest_gpr(ctx, a->l);
        if (ctx->privilege < 3) {
            tcg_gen_andi_reg(tmp, tmp, -4);
        }
        tcg_gen_ori_reg(tmp, tmp, ctx->privilege);
        save_gpr(ctx, a->l, tmp);
    }

    return do_dbranch(ctx, dest, 0, a->n);
}

static bool trans_blr(DisasContext *ctx, arg_blr *a)
{
    if (a->x) {
        TCGv_reg tmp = get_temp(ctx);
        tcg_gen_shli_reg(tmp, load_gpr(ctx, a->x), 3);
        tcg_gen_addi_reg(tmp, tmp, ctx->iaoq_f + 8);
        /* The computation here never changes privilege level.  */
        return do_ibranch(ctx, tmp, a->l, a->n);
    } else {
        /* BLR R0,RX is a good way to load PC+8 into RX.  */
        return do_dbranch(ctx, ctx->iaoq_f + 8, a->l, a->n);
    }
}

static bool trans_bv(DisasContext *ctx, arg_bv *a)
{
    TCGv_reg dest;

    if (a->x == 0) {
        dest = load_gpr(ctx, a->b);
    } else {
        dest = get_temp(ctx);
        tcg_gen_shli_reg(dest, load_gpr(ctx, a->x), 3);
        tcg_gen_add_reg(dest, dest, load_gpr(ctx, a->b));
    }
    dest = do_ibranch_priv(ctx, dest);
    return do_ibranch(ctx, dest, 0, a->n);
}

static bool trans_bve(DisasContext *ctx, arg_bve *a)
{
    TCGv_reg dest;

#ifdef CONFIG_USER_ONLY
    dest = do_ibranch_priv(ctx, load_gpr(ctx, a->b));
    return do_ibranch(ctx, dest, a->l, a->n);
#else
    nullify_over(ctx);
    dest = do_ibranch_priv(ctx, load_gpr(ctx, a->b));

    copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_b, cpu_iaoq_b);
    if (ctx->iaoq_b == -1) {
        tcg_gen_mov_i64(cpu_iasq_f, cpu_iasq_b);
    }
    copy_iaoq_entry(cpu_iaoq_b, -1, dest);
    tcg_gen_mov_i64(cpu_iasq_b, space_select(ctx, 0, dest));
    if (a->l) {
        copy_iaoq_entry(cpu_gr[a->l], ctx->iaoq_n, ctx->iaoq_n_var);
    }
    nullify_set(ctx, a->n);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return nullify_end(ctx);
#endif
}

/*
 * Float class 0
 */

static void gen_fcpy_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_mov_i32(dst, src);
}

static bool trans_fcpy_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fcpy_f);
}

static void gen_fcpy_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_mov_i64(dst, src);
}

static bool trans_fcpy_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fcpy_d);
}

static void gen_fabs_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_andi_i32(dst, src, INT32_MAX);
}

static bool trans_fabs_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fabs_f);
}

static void gen_fabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_andi_i64(dst, src, INT64_MAX);
}

static bool trans_fabs_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fabs_d);
}

static bool trans_fsqrt_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fsqrt_s);
}

static bool trans_fsqrt_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fsqrt_d);
}

static bool trans_frnd_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_frnd_s);
}

static bool trans_frnd_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_frnd_d);
}

static void gen_fneg_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_xori_i32(dst, src, INT32_MIN);
}

static bool trans_fneg_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fneg_f);
}

static void gen_fneg_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_xori_i64(dst, src, INT64_MIN);
}

static bool trans_fneg_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fneg_d);
}

static void gen_fnegabs_f(TCGv_i32 dst, TCGv_env unused, TCGv_i32 src)
{
    tcg_gen_ori_i32(dst, src, INT32_MIN);
}

static bool trans_fnegabs_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_fnegabs_f);
}

static void gen_fnegabs_d(TCGv_i64 dst, TCGv_env unused, TCGv_i64 src)
{
    tcg_gen_ori_i64(dst, src, INT64_MIN);
}

static bool trans_fnegabs_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_fnegabs_d);
}

/*
 * Float class 1
 */

static bool trans_fcnv_d_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_s);
}

static bool trans_fcnv_f_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_d);
}

static bool trans_fcnv_w_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_w_s);
}

static bool trans_fcnv_q_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_dw_s);
}

static bool trans_fcnv_w_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_w_d);
}

static bool trans_fcnv_q_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_dw_d);
}

static bool trans_fcnv_f_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_s_w);
}

static bool trans_fcnv_d_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_w);
}

static bool trans_fcnv_f_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_dw);
}

static bool trans_fcnv_d_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_d_dw);
}

static bool trans_fcnv_t_f_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_t_s_w);
}

static bool trans_fcnv_t_d_w(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_t_d_w);
}

static bool trans_fcnv_t_f_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_t_s_dw);
}

static bool trans_fcnv_t_d_q(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_t_d_dw);
}

static bool trans_fcnv_uw_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_uw_s);
}

static bool trans_fcnv_uq_f(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_udw_s);
}

static bool trans_fcnv_uw_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_uw_d);
}

static bool trans_fcnv_uq_d(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_udw_d);
}

static bool trans_fcnv_f_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_s_uw);
}

static bool trans_fcnv_d_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_d_uw);
}

static bool trans_fcnv_f_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_s_udw);
}

static bool trans_fcnv_d_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_d_udw);
}

static bool trans_fcnv_t_f_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wew(ctx, a->t, a->r, gen_helper_fcnv_t_s_uw);
}

static bool trans_fcnv_t_d_uw(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_wed(ctx, a->t, a->r, gen_helper_fcnv_t_d_uw);
}

static bool trans_fcnv_t_f_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_dew(ctx, a->t, a->r, gen_helper_fcnv_t_s_udw);
}

static bool trans_fcnv_t_d_uq(DisasContext *ctx, arg_fclass01 *a)
{
    return do_fop_ded(ctx, a->t, a->r, gen_helper_fcnv_t_d_udw);
}

/*
 * Float class 2
 */

static bool trans_fcmp_f(DisasContext *ctx, arg_fclass2 *a)
{
    TCGv_i32 ta, tb, tc, ty;

    nullify_over(ctx);

    ta = load_frw0_i32(a->r1);
    tb = load_frw0_i32(a->r2);
    ty = tcg_constant_i32(a->y);
    tc = tcg_constant_i32(a->c);

    gen_helper_fcmp_s(cpu_env, ta, tb, ty, tc);

    tcg_temp_free_i32(ta);
    tcg_temp_free_i32(tb);

    return nullify_end(ctx);
}

static bool trans_fcmp_d(DisasContext *ctx, arg_fclass2 *a)
{
    TCGv_i64 ta, tb;
    TCGv_i32 tc, ty;

    nullify_over(ctx);

    ta = load_frd0(a->r1);
    tb = load_frd0(a->r2);
    ty = tcg_constant_i32(a->y);
    tc = tcg_constant_i32(a->c);

    gen_helper_fcmp_d(cpu_env, ta, tb, ty, tc);

    tcg_temp_free_i64(ta);
    tcg_temp_free_i64(tb);

    return nullify_end(ctx);
}

static bool trans_ftest(DisasContext *ctx, arg_ftest *a)
{
    TCGv_reg t;

    nullify_over(ctx);

    t = get_temp(ctx);
    tcg_gen_ld32u_reg(t, cpu_env, offsetof(CPUHPPAState, fr0_shadow));

    if (a->y == 1) {
        int mask;
        bool inv = false;

        switch (a->c) {
        case 0: /* simple */
            tcg_gen_andi_reg(t, t, 0x4000000);
            ctx->null_cond = cond_make_0(TCG_COND_NE, t);
            goto done;
        case 2: /* rej */
            inv = true;
            /* fallthru */
        case 1: /* acc */
            mask = 0x43ff800;
            break;
        case 6: /* rej8 */
            inv = true;
            /* fallthru */
        case 5: /* acc8 */
            mask = 0x43f8000;
            break;
        case 9: /* acc6 */
            mask = 0x43e0000;
            break;
        case 13: /* acc4 */
            mask = 0x4380000;
            break;
        case 17: /* acc2 */
            mask = 0x4200000;
            break;
        default:
            gen_illegal(ctx);
            return true;
        }
        if (inv) {
            TCGv_reg c = load_const(ctx, mask);
            tcg_gen_or_reg(t, t, c);
            ctx->null_cond = cond_make(TCG_COND_EQ, t, c);
        } else {
            tcg_gen_andi_reg(t, t, mask);
            ctx->null_cond = cond_make_0(TCG_COND_EQ, t);
        }
    } else {
        unsigned cbit = (a->y ^ 1) - 1;

        tcg_gen_extract_reg(t, t, 21 - cbit, 1);
        ctx->null_cond = cond_make_0(TCG_COND_NE, t);
        tcg_temp_free(t);
    }

 done:
    return nullify_end(ctx);
}

/*
 * Float class 2
 */

static bool trans_fadd_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fadd_s);
}

static bool trans_fadd_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fadd_d);
}

static bool trans_fsub_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fsub_s);
}

static bool trans_fsub_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fsub_d);
}

static bool trans_fmpy_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fmpy_s);
}

static bool trans_fmpy_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fmpy_d);
}

static bool trans_fdiv_f(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_weww(ctx, a->t, a->r1, a->r2, gen_helper_fdiv_s);
}

static bool trans_fdiv_d(DisasContext *ctx, arg_fclass3 *a)
{
    return do_fop_dedd(ctx, a->t, a->r1, a->r2, gen_helper_fdiv_d);
}

static bool trans_xmpyu(DisasContext *ctx, arg_xmpyu *a)
{
    TCGv_i64 x, y;

    nullify_over(ctx);

    x = load_frw0_i64(a->r1);
    y = load_frw0_i64(a->r2);
    tcg_gen_mul_i64(x, x, y);
    save_frd(a->t, x);
    tcg_temp_free_i64(x);
    tcg_temp_free_i64(y);

    return nullify_end(ctx);
}

/* Convert the fmpyadd single-precision register encodings to standard.  */
static inline int fmpyadd_s_reg(unsigned r)
{
    return (r & 16) * 2 + 16 + (r & 15);
}

static bool do_fmpyadd_s(DisasContext *ctx, arg_mpyadd *a, bool is_sub)
{
    int tm = fmpyadd_s_reg(a->tm);
    int ra = fmpyadd_s_reg(a->ra);
    int ta = fmpyadd_s_reg(a->ta);
    int rm2 = fmpyadd_s_reg(a->rm2);
    int rm1 = fmpyadd_s_reg(a->rm1);

    nullify_over(ctx);

    do_fop_weww(ctx, tm, rm1, rm2, gen_helper_fmpy_s);
    do_fop_weww(ctx, ta, ta, ra,
                is_sub ? gen_helper_fsub_s : gen_helper_fadd_s);

    return nullify_end(ctx);
}

static bool trans_fmpyadd_f(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_s(ctx, a, false);
}

static bool trans_fmpysub_f(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_s(ctx, a, true);
}

static bool do_fmpyadd_d(DisasContext *ctx, arg_mpyadd *a, bool is_sub)
{
    nullify_over(ctx);

    do_fop_dedd(ctx, a->tm, a->rm1, a->rm2, gen_helper_fmpy_d);
    do_fop_dedd(ctx, a->ta, a->ta, a->ra,
                is_sub ? gen_helper_fsub_d : gen_helper_fadd_d);

    return nullify_end(ctx);
}

static bool trans_fmpyadd_d(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_d(ctx, a, false);
}

static bool trans_fmpysub_d(DisasContext *ctx, arg_mpyadd *a)
{
    return do_fmpyadd_d(ctx, a, true);
}

static bool trans_fmpyfadd_f(DisasContext *ctx, arg_fmpyfadd_f *a)
{
    TCGv_i32 x, y, z;

    nullify_over(ctx);
    x = load_frw0_i32(a->rm1);
    y = load_frw0_i32(a->rm2);
    z = load_frw0_i32(a->ra3);

    if (a->neg) {
        gen_helper_fmpynfadd_s(x, cpu_env, x, y, z);
    } else {
        gen_helper_fmpyfadd_s(x, cpu_env, x, y, z);
    }

    tcg_temp_free_i32(y);
    tcg_temp_free_i32(z);
    save_frw_i32(a->t, x);
    tcg_temp_free_i32(x);
    return nullify_end(ctx);
}

static bool trans_fmpyfadd_d(DisasContext *ctx, arg_fmpyfadd_d *a)
{
    TCGv_i64 x, y, z;

    nullify_over(ctx);
    x = load_frd0(a->rm1);
    y = load_frd0(a->rm2);
    z = load_frd0(a->ra3);

    if (a->neg) {
        gen_helper_fmpynfadd_d(x, cpu_env, x, y, z);
    } else {
        gen_helper_fmpyfadd_d(x, cpu_env, x, y, z);
    }

    tcg_temp_free_i64(y);
    tcg_temp_free_i64(z);
    save_frd(a->t, x);
    tcg_temp_free_i64(x);
    return nullify_end(ctx);
}

static bool trans_diag(DisasContext *ctx, arg_diag *a)
{
    qemu_log_mask(LOG_UNIMP, "DIAG opcode ignored\n");
    cond_free(&ctx->null_cond);
    return true;
}

static void hppa_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    int bound;

    ctx->cs = cs;
    ctx->tb_flags = ctx->base.tb->flags;

#ifdef CONFIG_USER_ONLY
    ctx->privilege = MMU_USER_IDX;
    ctx->mmu_idx = MMU_USER_IDX;
    ctx->iaoq_f = ctx->base.pc_first | MMU_USER_IDX;
    ctx->iaoq_b = ctx->base.tb->cs_base | MMU_USER_IDX;
    ctx->unalign = (ctx->tb_flags & TB_FLAG_UNALIGN ? MO_UNALN : MO_ALIGN);
#else
    ctx->privilege = (ctx->tb_flags >> TB_FLAG_PRIV_SHIFT) & 3;
    ctx->mmu_idx = (ctx->tb_flags & PSW_D ? ctx->privilege : MMU_PHYS_IDX);

    /* Recover the IAOQ values from the GVA + PRIV.  */
    uint64_t cs_base = ctx->base.tb->cs_base;
    uint64_t iasq_f = cs_base & ~0xffffffffull;
    int32_t diff = cs_base;

    ctx->iaoq_f = (ctx->base.pc_first & ~iasq_f) + ctx->privilege;
    ctx->iaoq_b = (diff ? ctx->iaoq_f + diff : -1);
#endif
    ctx->iaoq_n = -1;
    ctx->iaoq_n_var = NULL;

    /* Bound the number of instructions by those left on the page.  */
    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);

    ctx->ntempr = 0;
    ctx->ntempl = 0;
    memset(ctx->tempr, 0, sizeof(ctx->tempr));
    memset(ctx->templ, 0, sizeof(ctx->templ));
}

static void hppa_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    /* Seed the nullification status from PSW[N], as saved in TB->FLAGS.  */
    ctx->null_cond = cond_make_f();
    ctx->psw_n_nonzero = false;
    if (ctx->tb_flags & PSW_N) {
        ctx->null_cond.c = TCG_COND_ALWAYS;
        ctx->psw_n_nonzero = true;
    }
    ctx->null_lab = NULL;
}

static void hppa_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->iaoq_f, ctx->iaoq_b);
}

static void hppa_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUHPPAState *env = cs->env_ptr;
    DisasJumpType ret;
    int i, n;

    /* Execute one insn.  */
#ifdef CONFIG_USER_ONLY
    if (ctx->base.pc_next < TARGET_PAGE_SIZE) {
        do_page_zero(ctx);
        ret = ctx->base.is_jmp;
        assert(ret != DISAS_NEXT);
    } else
#endif
    {
        /* Always fetch the insn, even if nullified, so that we check
           the page permissions for execute.  */
        uint32_t insn = translator_ldl(env, &ctx->base, ctx->base.pc_next);

        /* Set up the IA queue for the next insn.
           This will be overwritten by a branch.  */
        if (ctx->iaoq_b == -1) {
            ctx->iaoq_n = -1;
            ctx->iaoq_n_var = get_temp(ctx);
            tcg_gen_addi_reg(ctx->iaoq_n_var, cpu_iaoq_b, 4);
        } else {
            ctx->iaoq_n = ctx->iaoq_b + 4;
            ctx->iaoq_n_var = NULL;
        }

        if (unlikely(ctx->null_cond.c == TCG_COND_ALWAYS)) {
            ctx->null_cond.c = TCG_COND_NEVER;
            ret = DISAS_NEXT;
        } else {
            ctx->insn = insn;
            if (!decode(ctx, insn)) {
                gen_illegal(ctx);
            }
            ret = ctx->base.is_jmp;
            assert(ctx->null_lab == NULL);
        }
    }

    /* Free any temporaries allocated.  */
    for (i = 0, n = ctx->ntempr; i < n; ++i) {
        tcg_temp_free(ctx->tempr[i]);
        ctx->tempr[i] = NULL;
    }
    for (i = 0, n = ctx->ntempl; i < n; ++i) {
        tcg_temp_free_tl(ctx->templ[i]);
        ctx->templ[i] = NULL;
    }
    ctx->ntempr = 0;
    ctx->ntempl = 0;

    /* Advance the insn queue.  Note that this check also detects
       a priority change within the instruction queue.  */
    if (ret == DISAS_NEXT && ctx->iaoq_b != ctx->iaoq_f + 4) {
        if (ctx->iaoq_b != -1 && ctx->iaoq_n != -1
            && use_goto_tb(ctx, ctx->iaoq_b)
            && (ctx->null_cond.c == TCG_COND_NEVER
                || ctx->null_cond.c == TCG_COND_ALWAYS)) {
            nullify_set(ctx, ctx->null_cond.c == TCG_COND_ALWAYS);
            gen_goto_tb(ctx, 0, ctx->iaoq_b, ctx->iaoq_n);
            ctx->base.is_jmp = ret = DISAS_NORETURN;
        } else {
            ctx->base.is_jmp = ret = DISAS_IAQ_N_STALE;
        }
    }
    ctx->iaoq_f = ctx->iaoq_b;
    ctx->iaoq_b = ctx->iaoq_n;
    ctx->base.pc_next += 4;

    switch (ret) {
    case DISAS_NORETURN:
    case DISAS_IAQ_N_UPDATED:
        break;

    case DISAS_NEXT:
    case DISAS_IAQ_N_STALE:
    case DISAS_IAQ_N_STALE_EXIT:
        if (ctx->iaoq_f == -1) {
            tcg_gen_mov_reg(cpu_iaoq_f, cpu_iaoq_b);
            copy_iaoq_entry(cpu_iaoq_b, ctx->iaoq_n, ctx->iaoq_n_var);
#ifndef CONFIG_USER_ONLY
            tcg_gen_mov_i64(cpu_iasq_f, cpu_iasq_b);
#endif
            nullify_save(ctx);
            ctx->base.is_jmp = (ret == DISAS_IAQ_N_STALE_EXIT
                                ? DISAS_EXIT
                                : DISAS_IAQ_N_UPDATED);
        } else if (ctx->iaoq_b == -1) {
            tcg_gen_mov_reg(cpu_iaoq_b, ctx->iaoq_n_var);
        }
        break;

    default:
        g_assert_not_reached();
    }
}

static void hppa_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    DisasJumpType is_jmp = ctx->base.is_jmp;

    switch (is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
    case DISAS_IAQ_N_STALE:
    case DISAS_IAQ_N_STALE_EXIT:
        copy_iaoq_entry(cpu_iaoq_f, ctx->iaoq_f, cpu_iaoq_f);
        copy_iaoq_entry(cpu_iaoq_b, ctx->iaoq_b, cpu_iaoq_b);
        nullify_save(ctx);
        /* FALLTHRU */
    case DISAS_IAQ_N_UPDATED:
        if (is_jmp != DISAS_IAQ_N_STALE_EXIT) {
            tcg_gen_lookup_and_goto_ptr();
            break;
        }
        /* FALLTHRU */
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void hppa_tr_disas_log(const DisasContextBase *dcbase,
                              CPUState *cs, FILE *logfile)
{
    target_ulong pc = dcbase->pc_first;

#ifdef CONFIG_USER_ONLY
    switch (pc) {
    case 0x00:
        fprintf(logfile, "IN:\n0x00000000:  (null)\n");
        return;
    case 0xb0:
        fprintf(logfile, "IN:\n0x000000b0:  light-weight-syscall\n");
        return;
    case 0xe0:
        fprintf(logfile, "IN:\n0x000000e0:  set-thread-pointer-syscall\n");
        return;
    case 0x100:
        fprintf(logfile, "IN:\n0x00000100:  syscall\n");
        return;
    }
#endif

    fprintf(logfile, "IN: %s\n", lookup_symbol(pc));
    target_disas(logfile, cs, pc, dcbase->tb->size);
}

static const TranslatorOps hppa_tr_ops = {
    .init_disas_context = hppa_tr_init_disas_context,
    .tb_start           = hppa_tr_tb_start,
    .insn_start         = hppa_tr_insn_start,
    .translate_insn     = hppa_tr_translate_insn,
    .tb_stop            = hppa_tr_tb_stop,
    .disas_log          = hppa_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext ctx;
    translator_loop(cs, tb, max_insns, pc, host_pc, &hppa_tr_ops, &ctx.base);
}
