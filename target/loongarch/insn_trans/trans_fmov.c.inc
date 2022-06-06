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
    TCGv cond = tcg_temp_new();

    tcg_gen_ld8u_tl(cond, cpu_env, offsetof(CPULoongArchState, cf[a->ca]));
    tcg_gen_movcond_tl(TCG_COND_EQ, cpu_fpr[a->fd], cond, zero,
                       cpu_fpr[a->fj], cpu_fpr[a->fk]);
    tcg_temp_free(cond);

    return true;
}

static bool gen_f2f(DisasContext *ctx, arg_ff *a,
                    void (*func)(TCGv, TCGv), bool nanbox)
{
    TCGv dest = cpu_fpr[a->fd];
    TCGv src = cpu_fpr[a->fj];

    func(dest, src);
    if (nanbox) {
        gen_nanbox_s(cpu_fpr[a->fd], cpu_fpr[a->fd]);
    }

    return true;
}

static bool gen_r2f(DisasContext *ctx, arg_fr *a,
                    void (*func)(TCGv, TCGv))
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);

    func(cpu_fpr[a->fd], src);
    return true;
}

static bool gen_f2r(DisasContext *ctx, arg_rf *a,
                    void (*func)(TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    func(dest, cpu_fpr[a->fj]);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool trans_movgr2fcsr(DisasContext *ctx, arg_movgr2fcsr *a)
{
    uint32_t mask = fcsr_mask[a->fcsrd];
    TCGv Rj = gpr_src(ctx, a->rj, EXT_NONE);

    if (mask == UINT32_MAX) {
        tcg_gen_extrl_i64_i32(cpu_fcsr0, Rj);
    } else {
        TCGv_i32 temp = tcg_temp_new_i32();

        tcg_gen_extrl_i64_i32(temp, Rj);
        tcg_gen_andi_i32(temp, temp, mask);
        tcg_gen_andi_i32(cpu_fcsr0, cpu_fcsr0, ~mask);
        tcg_gen_or_i32(cpu_fcsr0, cpu_fcsr0, temp);
        tcg_temp_free_i32(temp);

        /*
         * Install the new rounding mode to fpu_status, if changed.
         * Note that FCSR3 is exactly the rounding mode field.
         */
        if (mask != FCSR0_M3) {
            return true;
        }
    }
    gen_helper_set_rounding_mode(cpu_env, cpu_fcsr0);

    return true;
}

static bool trans_movfcsr2gr(DisasContext *ctx, arg_movfcsr2gr *a)
{
    TCGv_i32 temp = tcg_temp_new_i32();
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    tcg_gen_andi_i32(temp, cpu_fcsr0, fcsr_mask[a->fcsrs]);
    tcg_gen_ext_i32_i64(dest, temp);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    tcg_temp_free_i32(temp);

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
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_fpr[a->fj], 0x1);
    tcg_gen_st8_tl(t0, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));
    tcg_temp_free(t0);

    return true;
}

static bool trans_movcf2fr(DisasContext *ctx, arg_movcf2fr *a)
{
    tcg_gen_ld8u_tl(cpu_fpr[a->fd], cpu_env,
                    offsetof(CPULoongArchState, cf[a->cj & 0x7]));
    return true;
}

static bool trans_movgr2cf(DisasContext *ctx, arg_movgr2cf *a)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, gpr_src(ctx, a->rj, EXT_NONE), 0x1);
    tcg_gen_st8_tl(t0, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7]));
    tcg_temp_free(t0);

    return true;
}

static bool trans_movcf2gr(DisasContext *ctx, arg_movcf2gr *a)
{
    tcg_gen_ld8u_tl(gpr_dst(ctx, a->rd, EXT_NONE), cpu_env,
                    offsetof(CPULoongArchState, cf[a->cj & 0x7]));
    return true;
}

TRANS(fmov_s, gen_f2f, tcg_gen_mov_tl, true)
TRANS(fmov_d, gen_f2f, tcg_gen_mov_tl, false)
TRANS(movgr2fr_w, gen_r2f, gen_movgr2fr_w)
TRANS(movgr2fr_d, gen_r2f, tcg_gen_mov_tl)
TRANS(movgr2frh_w, gen_r2f, gen_movgr2frh_w)
TRANS(movfr2gr_s, gen_f2r, tcg_gen_ext32s_tl)
TRANS(movfr2gr_d, gen_f2r, tcg_gen_mov_tl)
TRANS(movfrh2gr_s, gen_f2r, gen_movfrh2gr_s)
