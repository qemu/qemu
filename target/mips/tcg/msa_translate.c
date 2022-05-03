/*
 *  MIPS SIMD Architecture (MSA) translation routines
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
 *  Copyright (c) 2020 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "fpu_helper.h"
#include "internal.h"

static int elm_n(DisasContext *ctx, int x);
static int elm_df(DisasContext *ctx, int x);
static int bit_m(DisasContext *ctx, int x);
static int bit_df(DisasContext *ctx, int x);

static inline int plus_1(DisasContext *s, int x)
{
    return x + 1;
}

static inline int plus_2(DisasContext *s, int x)
{
    return x + 2;
}

/* Include the auto-generated decoder.  */
#include "decode-msa.c.inc"

static const char msaregnames[][6] = {
    "w0.d0",  "w0.d1",  "w1.d0",  "w1.d1",
    "w2.d0",  "w2.d1",  "w3.d0",  "w3.d1",
    "w4.d0",  "w4.d1",  "w5.d0",  "w5.d1",
    "w6.d0",  "w6.d1",  "w7.d0",  "w7.d1",
    "w8.d0",  "w8.d1",  "w9.d0",  "w9.d1",
    "w10.d0", "w10.d1", "w11.d0", "w11.d1",
    "w12.d0", "w12.d1", "w13.d0", "w13.d1",
    "w14.d0", "w14.d1", "w15.d0", "w15.d1",
    "w16.d0", "w16.d1", "w17.d0", "w17.d1",
    "w18.d0", "w18.d1", "w19.d0", "w19.d1",
    "w20.d0", "w20.d1", "w21.d0", "w21.d1",
    "w22.d0", "w22.d1", "w23.d0", "w23.d1",
    "w24.d0", "w24.d1", "w25.d0", "w25.d1",
    "w26.d0", "w26.d1", "w27.d0", "w27.d1",
    "w28.d0", "w28.d1", "w29.d0", "w29.d1",
    "w30.d0", "w30.d1", "w31.d0", "w31.d1",
};

/* Encoding of Operation Field (must be indexed by CPUMIPSMSADataFormat) */
struct dfe {
    int start;
    int length;
    uint32_t mask;
};

/*
 * Extract immediate from df/{m,n} format (used by ELM & BIT instructions).
 * Returns the immediate value, or -1 if the format does not match.
 */
static int df_extract_val(DisasContext *ctx, int x, const struct dfe *s)
{
    for (unsigned i = 0; i < 4; i++) {
        if (extract32(x, s[i].start, s[i].length) == s[i].mask) {
            return extract32(x, 0, s[i].start);
        }
    }
    return -1;
}

/*
 * Extract DataField from df/{m,n} format (used by ELM & BIT instructions).
 * Returns the DataField, or -1 if the format does not match.
 */
static int df_extract_df(DisasContext *ctx, int x, const struct dfe *s)
{
    for (unsigned i = 0; i < 4; i++) {
        if (extract32(x, s[i].start, s[i].length) == s[i].mask) {
            return i;
        }
    }
    return -1;
}

static const struct dfe df_elm[] = {
    /* Table 3.26 ELM Instruction Format */
    [DF_BYTE]   = {4, 2, 0b00},
    [DF_HALF]   = {3, 3, 0b100},
    [DF_WORD]   = {2, 4, 0b1100},
    [DF_DOUBLE] = {1, 5, 0b11100}
};

static int elm_n(DisasContext *ctx, int x)
{
    return df_extract_val(ctx, x, df_elm);
}

static int elm_df(DisasContext *ctx, int x)
{
    return df_extract_df(ctx, x, df_elm);
}

static const struct dfe df_bit[] = {
    /* Table 3.28 BIT Instruction Format */
    [DF_BYTE]   = {3, 4, 0b1110},
    [DF_HALF]   = {4, 3, 0b110},
    [DF_WORD]   = {5, 2, 0b10},
    [DF_DOUBLE] = {6, 1, 0b0}
};

static int bit_m(DisasContext *ctx, int x)
{
    return df_extract_val(ctx, x, df_bit);
}

static int bit_df(DisasContext *ctx, int x)
{
    return df_extract_df(ctx, x, df_bit);
}

static TCGv_i64 msa_wr_d[64];

void msa_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        int off;

        /*
         * The MSA vector registers are mapped on the
         * scalar floating-point unit (FPU) registers.
         */
        off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[0]);
        msa_wr_d[i * 2] = fpu_f64[i];

        off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[1]);
        msa_wr_d[i * 2 + 1] =
                tcg_global_mem_new_i64(cpu_env, off, msaregnames[i * 2 + 1]);
    }
}

/*
 * Check if MSA is enabled.
 * This function is always called with MSA available.
 * If MSA is disabled, raise an exception.
 */
static inline bool check_msa_enabled(DisasContext *ctx)
{
    if (unlikely((ctx->hflags & MIPS_HFLAG_FPU) &&
                 !(ctx->hflags & MIPS_HFLAG_F64))) {
        gen_reserved_instruction(ctx);
        return false;
    }

    if (unlikely(!(ctx->hflags & MIPS_HFLAG_MSA))) {
        generate_exception_end(ctx, EXCP_MSADIS);
        return false;
    }
    return true;
}

typedef void gen_helper_piv(TCGv_ptr, TCGv_i32, TCGv);
typedef void gen_helper_pii(TCGv_ptr, TCGv_i32, TCGv_i32);
typedef void gen_helper_piii(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32);
typedef void gen_helper_piiii(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);

#define TRANS_DF_x(TYPE, NAME, trans_func, gen_func) \
    static gen_helper_p##TYPE * const NAME##_tab[4] = { \
        gen_func##_b, gen_func##_h, gen_func##_w, gen_func##_d \
    }; \
    TRANS(NAME, trans_func, NAME##_tab[a->df])

#define TRANS_DF_iv(NAME, trans_func, gen_func) \
    TRANS_DF_x(iv, NAME, trans_func, gen_func)

#define TRANS_DF_ii(NAME, trans_func, gen_func) \
    TRANS_DF_x(ii, NAME, trans_func, gen_func)

#define TRANS_DF_iii(NAME, trans_func, gen_func) \
    TRANS_DF_x(iii, NAME, trans_func, gen_func)

#define TRANS_DF_iii_b(NAME, trans_func, gen_func) \
    static gen_helper_piii * const NAME##_tab[4] = { \
        NULL, gen_func##_h, gen_func##_w, gen_func##_d \
    }; \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    { \
        return trans_func(ctx, a, NAME##_tab[a->df]); \
    }

static void gen_check_zero_element(TCGv tresult, uint8_t df, uint8_t wt,
                                   TCGCond cond)
{
    /* generates tcg ops to check if any element is 0 */
    /* Note this function only works with MSA_WRLEN = 128 */
    uint64_t eval_zero_or_big = dup_const(df, 1);
    uint64_t eval_big = eval_zero_or_big << ((8 << df) - 1);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_subi_i64(t0, msa_wr_d[wt << 1], eval_zero_or_big);
    tcg_gen_andc_i64(t0, t0, msa_wr_d[wt << 1]);
    tcg_gen_andi_i64(t0, t0, eval_big);
    tcg_gen_subi_i64(t1, msa_wr_d[(wt << 1) + 1], eval_zero_or_big);
    tcg_gen_andc_i64(t1, t1, msa_wr_d[(wt << 1) + 1]);
    tcg_gen_andi_i64(t1, t1, eval_big);
    tcg_gen_or_i64(t0, t0, t1);
    /* if all bits are zero then all elements are not zero */
    /* if some bit is non-zero then some element is zero */
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(tresult, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static bool gen_msa_BxZ_V(DisasContext *ctx, int wt, int sa, TCGCond cond)
{
    TCGv_i64 t0;

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }
    t0 = tcg_temp_new_i64();
    tcg_gen_or_i64(t0, msa_wr_d[wt << 1], msa_wr_d[(wt << 1) + 1]);
    tcg_gen_setcondi_i64(cond, t0, t0, 0);
    tcg_gen_trunc_i64_tl(bcond, t0);
    tcg_temp_free_i64(t0);

    ctx->btarget = ctx->base.pc_next + (sa << 2) + 4;

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->sa, TCG_COND_EQ);
}

static bool trans_BNZ_V(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ_V(ctx, a->wt, a->sa, TCG_COND_NE);
}

static bool gen_msa_BxZ(DisasContext *ctx, int df, int wt, int sa, bool if_not)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        gen_reserved_instruction(ctx);
        return true;
    }

    gen_check_zero_element(bcond, df, wt, if_not ? TCG_COND_EQ : TCG_COND_NE);

    ctx->btarget = ctx->base.pc_next + (sa << 2) + 4;
    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;

    return true;
}

static bool trans_BZ(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->sa, false);
}

static bool trans_BNZ(DisasContext *ctx, arg_msa_bz *a)
{
    return gen_msa_BxZ(ctx, a->df, a->wt, a->sa, true);
}

static bool trans_msa_i8(DisasContext *ctx, arg_msa_i *a,
                         gen_helper_piii *gen_msa_i8)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_i8(cpu_env,
               tcg_constant_i32(a->wd),
               tcg_constant_i32(a->ws),
               tcg_constant_i32(a->sa));

    return true;
}

TRANS(ANDI,     trans_msa_i8, gen_helper_msa_andi_b);
TRANS(ORI,      trans_msa_i8, gen_helper_msa_ori_b);
TRANS(NORI,     trans_msa_i8, gen_helper_msa_nori_b);
TRANS(XORI,     trans_msa_i8, gen_helper_msa_xori_b);
TRANS(BMNZI,    trans_msa_i8, gen_helper_msa_bmnzi_b);
TRANS(BMZI,     trans_msa_i8, gen_helper_msa_bmzi_b);
TRANS(BSELI,    trans_msa_i8, gen_helper_msa_bseli_b);

static bool trans_SHF(DisasContext *ctx, arg_msa_i *a)
{
    if (a->df == DF_DOUBLE) {
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_helper_msa_shf_df(cpu_env,
                          tcg_constant_i32(a->df),
                          tcg_constant_i32(a->wd),
                          tcg_constant_i32(a->ws),
                          tcg_constant_i32(a->sa));

    return true;
}

static bool trans_msa_i5(DisasContext *ctx, arg_msa_i *a,
                         gen_helper_piiii *gen_msa_i5)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_i5(cpu_env,
               tcg_constant_i32(a->df),
               tcg_constant_i32(a->wd),
               tcg_constant_i32(a->ws),
               tcg_constant_i32(a->sa));

    return true;
}

TRANS(ADDVI,    trans_msa_i5, gen_helper_msa_addvi_df);
TRANS(SUBVI,    trans_msa_i5, gen_helper_msa_subvi_df);
TRANS(MAXI_S,   trans_msa_i5, gen_helper_msa_maxi_s_df);
TRANS(MAXI_U,   trans_msa_i5, gen_helper_msa_maxi_u_df);
TRANS(MINI_S,   trans_msa_i5, gen_helper_msa_mini_s_df);
TRANS(MINI_U,   trans_msa_i5, gen_helper_msa_mini_u_df);
TRANS(CLTI_S,   trans_msa_i5, gen_helper_msa_clti_s_df);
TRANS(CLTI_U,   trans_msa_i5, gen_helper_msa_clti_u_df);
TRANS(CLEI_S,   trans_msa_i5, gen_helper_msa_clei_s_df);
TRANS(CLEI_U,   trans_msa_i5, gen_helper_msa_clei_u_df);
TRANS(CEQI,     trans_msa_i5, gen_helper_msa_ceqi_df);

static bool trans_LDI(DisasContext *ctx, arg_msa_ldi *a)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_helper_msa_ldi_df(cpu_env,
                          tcg_constant_i32(a->df),
                          tcg_constant_i32(a->wd),
                          tcg_constant_i32(a->sa));

    return true;
}

static bool trans_msa_bit(DisasContext *ctx, arg_msa_bit *a,
                          gen_helper_piiii *gen_msa_bit)
{
    if (a->df < 0) {
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_bit(cpu_env,
                tcg_constant_i32(a->df),
                tcg_constant_i32(a->wd),
                tcg_constant_i32(a->ws),
                tcg_constant_i32(a->m));

    return true;
}

TRANS(SLLI,     trans_msa_bit, gen_helper_msa_slli_df);
TRANS(SRAI,     trans_msa_bit, gen_helper_msa_srai_df);
TRANS(SRLI,     trans_msa_bit, gen_helper_msa_srli_df);
TRANS(BCLRI,    trans_msa_bit, gen_helper_msa_bclri_df);
TRANS(BSETI,    trans_msa_bit, gen_helper_msa_bseti_df);
TRANS(BNEGI,    trans_msa_bit, gen_helper_msa_bnegi_df);
TRANS(BINSLI,   trans_msa_bit, gen_helper_msa_binsli_df);
TRANS(BINSRI,   trans_msa_bit, gen_helper_msa_binsri_df);
TRANS(SAT_S,    trans_msa_bit, gen_helper_msa_sat_s_df);
TRANS(SAT_U,    trans_msa_bit, gen_helper_msa_sat_u_df);
TRANS(SRARI,    trans_msa_bit, gen_helper_msa_srari_df);
TRANS(SRLRI,    trans_msa_bit, gen_helper_msa_srlri_df);

static bool trans_msa_3rf(DisasContext *ctx, arg_msa_r *a,
                          gen_helper_piiii *gen_msa_3rf)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_3rf(cpu_env,
                tcg_constant_i32(a->df),
                tcg_constant_i32(a->wd),
                tcg_constant_i32(a->ws),
                tcg_constant_i32(a->wt));

    return true;
}

static bool trans_msa_3r(DisasContext *ctx, arg_msa_r *a,
                         gen_helper_piii *gen_msa_3r)
{
    if (!gen_msa_3r) {
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_3r(cpu_env,
               tcg_constant_i32(a->wd),
               tcg_constant_i32(a->ws),
               tcg_constant_i32(a->wt));

    return true;
}

TRANS(AND_V,            trans_msa_3r,   gen_helper_msa_and_v);
TRANS(OR_V,             trans_msa_3r,   gen_helper_msa_or_v);
TRANS(NOR_V,            trans_msa_3r,   gen_helper_msa_nor_v);
TRANS(XOR_V,            trans_msa_3r,   gen_helper_msa_xor_v);
TRANS(BMNZ_V,           trans_msa_3r,   gen_helper_msa_bmnz_v);
TRANS(BMZ_V,            trans_msa_3r,   gen_helper_msa_bmz_v);
TRANS(BSEL_V,           trans_msa_3r,   gen_helper_msa_bsel_v);

TRANS_DF_iii(SLL,       trans_msa_3r,   gen_helper_msa_sll);
TRANS_DF_iii(SRA,       trans_msa_3r,   gen_helper_msa_sra);
TRANS_DF_iii(SRL,       trans_msa_3r,   gen_helper_msa_srl);
TRANS_DF_iii(BCLR,      trans_msa_3r,   gen_helper_msa_bclr);
TRANS_DF_iii(BSET,      trans_msa_3r,   gen_helper_msa_bset);
TRANS_DF_iii(BNEG,      trans_msa_3r,   gen_helper_msa_bneg);
TRANS_DF_iii(BINSL,     trans_msa_3r,   gen_helper_msa_binsl);
TRANS_DF_iii(BINSR,     trans_msa_3r,   gen_helper_msa_binsr);

TRANS_DF_iii(ADDV,      trans_msa_3r,   gen_helper_msa_addv);
TRANS_DF_iii(SUBV,      trans_msa_3r,   gen_helper_msa_subv);
TRANS_DF_iii(MAX_S,     trans_msa_3r,   gen_helper_msa_max_s);
TRANS_DF_iii(MAX_U,     trans_msa_3r,   gen_helper_msa_max_u);
TRANS_DF_iii(MIN_S,     trans_msa_3r,   gen_helper_msa_min_s);
TRANS_DF_iii(MIN_U,     trans_msa_3r,   gen_helper_msa_min_u);
TRANS_DF_iii(MAX_A,     trans_msa_3r,   gen_helper_msa_max_a);
TRANS_DF_iii(MIN_A,     trans_msa_3r,   gen_helper_msa_min_a);

TRANS_DF_iii(CEQ,       trans_msa_3r,   gen_helper_msa_ceq);
TRANS_DF_iii(CLT_S,     trans_msa_3r,   gen_helper_msa_clt_s);
TRANS_DF_iii(CLT_U,     trans_msa_3r,   gen_helper_msa_clt_u);
TRANS_DF_iii(CLE_S,     trans_msa_3r,   gen_helper_msa_cle_s);
TRANS_DF_iii(CLE_U,     trans_msa_3r,   gen_helper_msa_cle_u);

TRANS_DF_iii(ADD_A,     trans_msa_3r,   gen_helper_msa_add_a);
TRANS_DF_iii(ADDS_A,    trans_msa_3r,   gen_helper_msa_adds_a);
TRANS_DF_iii(ADDS_S,    trans_msa_3r,   gen_helper_msa_adds_s);
TRANS_DF_iii(ADDS_U,    trans_msa_3r,   gen_helper_msa_adds_u);
TRANS_DF_iii(AVE_S,     trans_msa_3r,   gen_helper_msa_ave_s);
TRANS_DF_iii(AVE_U,     trans_msa_3r,   gen_helper_msa_ave_u);
TRANS_DF_iii(AVER_S,    trans_msa_3r,   gen_helper_msa_aver_s);
TRANS_DF_iii(AVER_U,    trans_msa_3r,   gen_helper_msa_aver_u);

TRANS_DF_iii(SUBS_S,    trans_msa_3r,   gen_helper_msa_subs_s);
TRANS_DF_iii(SUBS_U,    trans_msa_3r,   gen_helper_msa_subs_u);
TRANS_DF_iii(SUBSUS_U,  trans_msa_3r,   gen_helper_msa_subsus_u);
TRANS_DF_iii(SUBSUU_S,  trans_msa_3r,   gen_helper_msa_subsuu_s);
TRANS_DF_iii(ASUB_S,    trans_msa_3r,   gen_helper_msa_asub_s);
TRANS_DF_iii(ASUB_U,    trans_msa_3r,   gen_helper_msa_asub_u);

TRANS_DF_iii(MULV,      trans_msa_3r,   gen_helper_msa_mulv);
TRANS_DF_iii(MADDV,     trans_msa_3r,   gen_helper_msa_maddv);
TRANS_DF_iii(MSUBV,     trans_msa_3r,   gen_helper_msa_msubv);
TRANS_DF_iii(DIV_S,     trans_msa_3r,   gen_helper_msa_div_s);
TRANS_DF_iii(DIV_U,     trans_msa_3r,   gen_helper_msa_div_u);
TRANS_DF_iii(MOD_S,     trans_msa_3r,   gen_helper_msa_mod_s);
TRANS_DF_iii(MOD_U,     trans_msa_3r,   gen_helper_msa_mod_u);

TRANS_DF_iii_b(DOTP_S,  trans_msa_3r,   gen_helper_msa_dotp_s);
TRANS_DF_iii_b(DOTP_U,  trans_msa_3r,   gen_helper_msa_dotp_u);
TRANS_DF_iii_b(DPADD_S, trans_msa_3r,   gen_helper_msa_dpadd_s);
TRANS_DF_iii_b(DPADD_U, trans_msa_3r,   gen_helper_msa_dpadd_u);
TRANS_DF_iii_b(DPSUB_S, trans_msa_3r,   gen_helper_msa_dpsub_s);
TRANS_DF_iii_b(DPSUB_U, trans_msa_3r,   gen_helper_msa_dpsub_u);

TRANS(SLD,              trans_msa_3rf,  gen_helper_msa_sld_df);
TRANS(SPLAT,            trans_msa_3rf,  gen_helper_msa_splat_df);
TRANS_DF_iii(PCKEV,     trans_msa_3r,   gen_helper_msa_pckev);
TRANS_DF_iii(PCKOD,     trans_msa_3r,   gen_helper_msa_pckod);
TRANS_DF_iii(ILVL,      trans_msa_3r,   gen_helper_msa_ilvl);
TRANS_DF_iii(ILVR,      trans_msa_3r,   gen_helper_msa_ilvr);
TRANS_DF_iii(ILVEV,     trans_msa_3r,   gen_helper_msa_ilvev);
TRANS_DF_iii(ILVOD,     trans_msa_3r,   gen_helper_msa_ilvod);

TRANS(VSHF,             trans_msa_3rf,  gen_helper_msa_vshf_df);
TRANS_DF_iii(SRAR,      trans_msa_3r,   gen_helper_msa_srar);
TRANS_DF_iii(SRLR,      trans_msa_3r,   gen_helper_msa_srlr);
TRANS_DF_iii_b(HADD_S,  trans_msa_3r,   gen_helper_msa_hadd_s);
TRANS_DF_iii_b(HADD_U,  trans_msa_3r,   gen_helper_msa_hadd_u);
TRANS_DF_iii_b(HSUB_S,  trans_msa_3r,   gen_helper_msa_hsub_s);
TRANS_DF_iii_b(HSUB_U,  trans_msa_3r,   gen_helper_msa_hsub_u);

static bool trans_MOVE_V(DisasContext *ctx, arg_msa_elm *a)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_helper_msa_move_v(cpu_env,
                          tcg_constant_i32(a->wd),
                          tcg_constant_i32(a->ws));

    return true;
}

static bool trans_CTCMSA(DisasContext *ctx, arg_msa_elm *a)
{
    TCGv telm;

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    telm = tcg_temp_new();

    gen_load_gpr(telm, a->ws);
    gen_helper_msa_ctcmsa(cpu_env, telm, tcg_constant_i32(a->wd));

    tcg_temp_free(telm);

    return true;
}

static bool trans_CFCMSA(DisasContext *ctx, arg_msa_elm *a)
{
    TCGv telm;

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    telm = tcg_temp_new();

    gen_helper_msa_cfcmsa(telm, cpu_env, tcg_constant_i32(a->ws));
    gen_store_gpr(telm, a->wd);

    tcg_temp_free(telm);

    return true;
}

static bool trans_msa_elm(DisasContext *ctx, arg_msa_elm_df *a,
                          gen_helper_piiii *gen_msa_elm_df)
{
    if (a->df < 0) {
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_elm_df(cpu_env,
                   tcg_constant_i32(a->df),
                   tcg_constant_i32(a->wd),
                   tcg_constant_i32(a->ws),
                   tcg_constant_i32(a->n));

    return true;
}

TRANS(SLDI,   trans_msa_elm, gen_helper_msa_sldi_df);
TRANS(SPLATI, trans_msa_elm, gen_helper_msa_splati_df);
TRANS(INSVE,  trans_msa_elm, gen_helper_msa_insve_df);

static bool trans_msa_elm_fn(DisasContext *ctx, arg_msa_elm_df *a,
                             gen_helper_piii * const gen_msa_elm[4])
{
    if (a->df < 0 || !gen_msa_elm[a->df]) {
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_elm[a->df](cpu_env,
                       tcg_constant_i32(a->wd),
                       tcg_constant_i32(a->ws),
                       tcg_constant_i32(a->n));

    return true;
}

#if defined(TARGET_MIPS64)
#define NULL_IF_MIPS32(function) function
#else
#define NULL_IF_MIPS32(function) NULL
#endif

static bool trans_COPY_U(DisasContext *ctx, arg_msa_elm_df *a)
{
    if (a->wd == 0) {
        /* Treat as NOP. */
        return true;
    }

    static gen_helper_piii * const gen_msa_copy_u[4] = {
        gen_helper_msa_copy_u_b, gen_helper_msa_copy_u_h,
        NULL_IF_MIPS32(gen_helper_msa_copy_u_w), NULL
    };

    return trans_msa_elm_fn(ctx, a, gen_msa_copy_u);
}

static bool trans_COPY_S(DisasContext *ctx, arg_msa_elm_df *a)
{
    if (a->wd == 0) {
        /* Treat as NOP. */
        return true;
    }

    static gen_helper_piii * const gen_msa_copy_s[4] = {
        gen_helper_msa_copy_s_b, gen_helper_msa_copy_s_h,
        gen_helper_msa_copy_s_w, NULL_IF_MIPS32(gen_helper_msa_copy_s_d)
    };

    return trans_msa_elm_fn(ctx, a, gen_msa_copy_s);
}

static bool trans_INSERT(DisasContext *ctx, arg_msa_elm_df *a)
{
    static gen_helper_piii * const gen_msa_insert[4] = {
        gen_helper_msa_insert_b, gen_helper_msa_insert_h,
        gen_helper_msa_insert_w, NULL_IF_MIPS32(gen_helper_msa_insert_d)
    };

    return trans_msa_elm_fn(ctx, a, gen_msa_insert);
}

TRANS(FCAF,     trans_msa_3rf, gen_helper_msa_fcaf_df);
TRANS(FCUN,     trans_msa_3rf, gen_helper_msa_fcun_df);
TRANS(FCEQ,     trans_msa_3rf, gen_helper_msa_fceq_df);
TRANS(FCUEQ,    trans_msa_3rf, gen_helper_msa_fcueq_df);
TRANS(FCLT,     trans_msa_3rf, gen_helper_msa_fclt_df);
TRANS(FCULT,    trans_msa_3rf, gen_helper_msa_fcult_df);
TRANS(FCLE,     trans_msa_3rf, gen_helper_msa_fcle_df);
TRANS(FCULE,    trans_msa_3rf, gen_helper_msa_fcule_df);
TRANS(FSAF,     trans_msa_3rf, gen_helper_msa_fsaf_df);
TRANS(FSUN,     trans_msa_3rf, gen_helper_msa_fsun_df);
TRANS(FSEQ,     trans_msa_3rf, gen_helper_msa_fseq_df);
TRANS(FSUEQ,    trans_msa_3rf, gen_helper_msa_fsueq_df);
TRANS(FSLT,     trans_msa_3rf, gen_helper_msa_fslt_df);
TRANS(FSULT,    trans_msa_3rf, gen_helper_msa_fsult_df);
TRANS(FSLE,     trans_msa_3rf, gen_helper_msa_fsle_df);
TRANS(FSULE,    trans_msa_3rf, gen_helper_msa_fsule_df);

TRANS(FADD,     trans_msa_3rf, gen_helper_msa_fadd_df);
TRANS(FSUB,     trans_msa_3rf, gen_helper_msa_fsub_df);
TRANS(FMUL,     trans_msa_3rf, gen_helper_msa_fmul_df);
TRANS(FDIV,     trans_msa_3rf, gen_helper_msa_fdiv_df);
TRANS(FMADD,    trans_msa_3rf, gen_helper_msa_fmadd_df);
TRANS(FMSUB,    trans_msa_3rf, gen_helper_msa_fmsub_df);
TRANS(FEXP2,    trans_msa_3rf, gen_helper_msa_fexp2_df);
TRANS(FEXDO,    trans_msa_3rf, gen_helper_msa_fexdo_df);
TRANS(FTQ,      trans_msa_3rf, gen_helper_msa_ftq_df);
TRANS(FMIN,     trans_msa_3rf, gen_helper_msa_fmin_df);
TRANS(FMIN_A,   trans_msa_3rf, gen_helper_msa_fmin_a_df);
TRANS(FMAX,     trans_msa_3rf, gen_helper_msa_fmax_df);
TRANS(FMAX_A,   trans_msa_3rf, gen_helper_msa_fmax_a_df);

TRANS(FCOR,     trans_msa_3rf, gen_helper_msa_fcor_df);
TRANS(FCUNE,    trans_msa_3rf, gen_helper_msa_fcune_df);
TRANS(FCNE,     trans_msa_3rf, gen_helper_msa_fcne_df);
TRANS(MUL_Q,    trans_msa_3rf, gen_helper_msa_mul_q_df);
TRANS(MADD_Q,   trans_msa_3rf, gen_helper_msa_madd_q_df);
TRANS(MSUB_Q,   trans_msa_3rf, gen_helper_msa_msub_q_df);
TRANS(FSOR,     trans_msa_3rf, gen_helper_msa_fsor_df);
TRANS(FSUNE,    trans_msa_3rf, gen_helper_msa_fsune_df);
TRANS(FSNE,     trans_msa_3rf, gen_helper_msa_fsne_df);
TRANS(MULR_Q,   trans_msa_3rf, gen_helper_msa_mulr_q_df);
TRANS(MADDR_Q,  trans_msa_3rf, gen_helper_msa_maddr_q_df);
TRANS(MSUBR_Q,  trans_msa_3rf, gen_helper_msa_msubr_q_df);

static bool trans_msa_2r(DisasContext *ctx, arg_msa_r *a,
                         gen_helper_pii *gen_msa_2r)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_2r(cpu_env, tcg_constant_i32(a->wd), tcg_constant_i32(a->ws));

    return true;
}

TRANS_DF_ii(PCNT, trans_msa_2r, gen_helper_msa_pcnt);
TRANS_DF_ii(NLOC, trans_msa_2r, gen_helper_msa_nloc);
TRANS_DF_ii(NLZC, trans_msa_2r, gen_helper_msa_nlzc);

static bool trans_FILL(DisasContext *ctx, arg_msa_r *a)
{
    if (TARGET_LONG_BITS != 64 && a->df == DF_DOUBLE) {
        /* Double format valid only for MIPS64 */
        return false;
    }

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_helper_msa_fill_df(cpu_env,
                           tcg_constant_i32(a->df),
                           tcg_constant_i32(a->wd),
                           tcg_constant_i32(a->ws));

    return true;
}

static bool trans_msa_2rf(DisasContext *ctx, arg_msa_r *a,
                          gen_helper_piii *gen_msa_2rf)
{
    if (!check_msa_enabled(ctx)) {
        return true;
    }

    gen_msa_2rf(cpu_env,
                tcg_constant_i32(a->df),
                tcg_constant_i32(a->wd),
                tcg_constant_i32(a->ws));

    return true;
}

TRANS(FCLASS,   trans_msa_2rf, gen_helper_msa_fclass_df);
TRANS(FTRUNC_S, trans_msa_2rf, gen_helper_msa_ftrunc_s_df);
TRANS(FTRUNC_U, trans_msa_2rf, gen_helper_msa_ftrunc_u_df);
TRANS(FSQRT,    trans_msa_2rf, gen_helper_msa_fsqrt_df);
TRANS(FRSQRT,   trans_msa_2rf, gen_helper_msa_frsqrt_df);
TRANS(FRCP,     trans_msa_2rf, gen_helper_msa_frcp_df);
TRANS(FRINT,    trans_msa_2rf, gen_helper_msa_frint_df);
TRANS(FLOG2,    trans_msa_2rf, gen_helper_msa_flog2_df);
TRANS(FEXUPL,   trans_msa_2rf, gen_helper_msa_fexupl_df);
TRANS(FEXUPR,   trans_msa_2rf, gen_helper_msa_fexupr_df);
TRANS(FFQL,     trans_msa_2rf, gen_helper_msa_ffql_df);
TRANS(FFQR,     trans_msa_2rf, gen_helper_msa_ffqr_df);
TRANS(FTINT_S,  trans_msa_2rf, gen_helper_msa_ftint_s_df);
TRANS(FTINT_U,  trans_msa_2rf, gen_helper_msa_ftint_u_df);
TRANS(FFINT_S,  trans_msa_2rf, gen_helper_msa_ffint_s_df);
TRANS(FFINT_U,  trans_msa_2rf, gen_helper_msa_ffint_u_df);

static bool trans_msa_ldst(DisasContext *ctx, arg_msa_i *a,
                           gen_helper_piv *gen_msa_ldst)
{
    TCGv taddr;

    if (!check_msa_enabled(ctx)) {
        return true;
    }

    taddr = tcg_temp_new();

    gen_base_offset_addr(ctx, taddr, a->ws, a->sa << a->df);
    gen_msa_ldst(cpu_env, tcg_constant_i32(a->wd), taddr);

    tcg_temp_free(taddr);

    return true;
}

TRANS_DF_iv(LD, trans_msa_ldst, gen_helper_msa_ld);
TRANS_DF_iv(ST, trans_msa_ldst, gen_helper_msa_st);

static bool trans_LSA(DisasContext *ctx, arg_r *a)
{
    return gen_lsa(ctx, a->rd, a->rt, a->rs, a->sa);
}

static bool trans_DLSA(DisasContext *ctx, arg_r *a)
{
    if (TARGET_LONG_BITS != 64) {
        return false;
    }
    return gen_dlsa(ctx, a->rd, a->rt, a->rs, a->sa);
}
