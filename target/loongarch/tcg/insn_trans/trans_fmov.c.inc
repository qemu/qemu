/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static const uint32_t fcsr_mask[4] = {
    UINT32_MAX, FCSR0_M1, FCSR0_M2, FCSR0_M3
};

static bool trans_fsel(DisasContext *ctx, arg_fsel *a)
{
    TCGv zero = tcg_constant_tl(0);
    TCGv dest = get_fpr(ctx, a->fd);
    TCGv src1 = get_fpr(ctx, a->fj);
    TCGv src2 = get_fpr(ctx, a->fk);
    TCGv cond;

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    cond = tcg_temp_new();
    tcg_gen_ld8u_tl(cond, tcg_env, offsetof(CPULoongArchState, cf[a->ca]));
    tcg_gen_movcond_tl(TCG_COND_EQ, dest, cond, zero, src1, src2);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_f2f(DisasContext *ctx, arg_ff *a,
                    void (*func)(TCGv, TCGv), bool nanbox)
{
    TCGv dest = get_fpr(ctx, a->fd);
    TCGv src = get_fpr(ctx, a->fj);

    CHECK_FPE;

    func(dest, src);
    if (nanbox) {
        gen_nanbox_s(dest, dest);
    }
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_r2f(DisasContext *ctx, arg_fr *a,
                    void (*func)(TCGv, TCGv))
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv dest = get_fpr(ctx, a->fd);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    func(dest, src);
    set_fpr(a->fd, dest);

    return true;
}

static bool gen_f2r(DisasContext *ctx, arg_rf *a,
                    void (*func)(TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src = get_fpr(ctx, a->fj);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    func(dest, src);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool trans_movgr2fcsr(DisasContext *ctx, arg_movgr2fcsr *a)
{
    uint32_t mask = fcsr_mask[a->fcsrd];
    TCGv Rj = gpr_src(ctx, a->rj, EXT_NONE);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    if (mask == UINT32_MAX) {
        tcg_gen_st32_i64(Rj, tcg_env, offsetof(CPULoongArchState, fcsr0));
    } else {
        TCGv_i32 fcsr0 = tcg_temp_new_i32();
        TCGv_i32 temp = tcg_temp_new_i32();

        tcg_gen_ld_i32(fcsr0, tcg_env, offsetof(CPULoongArchState, fcsr0));
        tcg_gen_extrl_i64_i32(temp, Rj);
        tcg_gen_andi_i32(temp, temp, mask);
        tcg_gen_andi_i32(fcsr0, fcsr0, ~mask);
        tcg_gen_or_i32(fcsr0, fcsr0, temp);
        tcg_gen_st_i32(fcsr0, tcg_env, offsetof(CPULoongArchState, fcsr0));
    }

    /*
     * Install the new rounding mode to fpu_status, if changed.
     * Note that FCSR3 is exactly the rounding mode field.
     */
    if (mask & FCSR0_M3) {
        gen_helper_set_rounding_mode(tcg_env);
    }
    return true;
}

static bool trans_movfcsr2gr(DisasContext *ctx, arg_movfcsr2gr *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    tcg_gen_ld32u_i64(dest, tcg_env, offsetof(CPULoongArchState, fcsr0));
    tcg_gen_andi_i64(dest, dest, fcsr_mask[a->fcsrs]);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static void gen_movgr2fr_w(TCGv dest, TCGv src)
{
    tcg_gen_deposit_i64(dest, dest, src, 0, 32);
}

static void gen_movgr2frh_w(TCGv dest, TCGv src)
{
    tcg_gen_deposit_i64(dest, dest, src, 32, 32);
}

static void gen_movfrh2gr_s(TCGv dest, TCGv src)
{
    tcg_gen_sextract_tl(dest, src, 32, 32);
}

static bool trans_movfr2cf(DisasContext *ctx, arg_movfr2cf *a)
{
    TCGv t0;
    TCGv src = get_fpr(ctx, a->fj);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src, 0x1);
    tcg_gen_st8_tl(t0, tcg_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));

    return true;
}

static bool trans_movcf2fr(DisasContext *ctx, arg_movcf2fr *a)
{
    TCGv dest = get_fpr(ctx, a->fd);

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    tcg_gen_ld8u_tl(dest, tcg_env,
                    offsetof(CPULoongArchState, cf[a->cj & 0x7]));
    set_fpr(a->fd, dest);

    return true;
}

static bool trans_movgr2cf(DisasContext *ctx, arg_movgr2cf *a)
{
    TCGv t0;

    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, gpr_src(ctx, a->rj, EXT_NONE), 0x1);
    tcg_gen_st8_tl(t0, tcg_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));

    return true;
}

static bool trans_movcf2gr(DisasContext *ctx, arg_movcf2gr *a)
{
    if (!avail_FP(ctx)) {
        return false;
    }

    CHECK_FPE;

    tcg_gen_ld8u_tl(gpr_dst(ctx, a->rd, EXT_NONE), tcg_env,
                    offsetof(CPULoongArchState, cf[a->cj & 0x7]));
    return true;
}

TRANS(fmov_s, FP_SP, gen_f2f, tcg_gen_mov_tl, true)
TRANS(fmov_d, FP_DP, gen_f2f, tcg_gen_mov_tl, false)
TRANS(movgr2fr_w, FP_SP, gen_r2f, gen_movgr2fr_w)
TRANS(movgr2fr_d, 64, gen_r2f, tcg_gen_mov_tl)
TRANS(movgr2frh_w, FP_DP, gen_r2f, gen_movgr2frh_w)
TRANS(movfr2gr_s, FP_SP, gen_f2r, tcg_gen_ext32s_tl)
TRANS(movfr2gr_d, 64, gen_f2r, tcg_gen_mov_tl)
TRANS(movfrh2gr_s, FP_DP, gen_f2r, gen_movfrh2gr_s)
