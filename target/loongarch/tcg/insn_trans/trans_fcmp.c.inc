/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

/* bit0(signaling/quiet) bit1(lt) bit2(eq) bit3(un) bit4(neq) */
static uint32_t get_fcmp_flags(DisasContext *ctx, int cond)
{
    uint32_t flags = 0;

    /*check cond , cond =[0-8,10,12] */
    if ((cond > 8) &&(cond != 10) && (cond != 12)) {
        return -1;
    }

    if (cond & 0x1) {
        flags |= FCMP_LT;
    }
    if (cond & 0x2) {
        flags |= FCMP_EQ;
    }
    if (cond & 0x4) {
        flags |= FCMP_UN;
    }
    if (cond & 0x8) {
        flags |= FCMP_GT | FCMP_LT;
    }
    return flags;
}

static bool trans_fcmp_cond_s(DisasContext *ctx, arg_fcmp_cond_s *a)
{
    TCGv var, src1, src2;
    uint32_t flags = get_fcmp_flags(ctx, a->fcond >>1);
    void (*fn)(TCGv, TCGv_env, TCGv, TCGv, TCGv_i32);

    if (flags == -1) {
        generate_exception(ctx, EXCCODE_INE);
        return true;
    }

    if (!avail_FP_SP(ctx)) {
        return false;
    }

    CHECK_FPE;

    var = tcg_temp_new();
    src1 = get_fpr(ctx, a->fj);
    src2 = get_fpr(ctx, a->fk);
    fn = (a->fcond & 1 ? gen_helper_fcmp_s_s : gen_helper_fcmp_c_s);
    fn(var, tcg_env, src1, src2, tcg_constant_i32(flags));

    tcg_gen_st8_tl(var, tcg_env, offsetof(CPULoongArchState, cf[a->cd]));
    return true;
}

static bool trans_fcmp_cond_d(DisasContext *ctx, arg_fcmp_cond_d *a)
{
    TCGv var, src1, src2;
    uint32_t flags = get_fcmp_flags(ctx, a->fcond >> 1);
    void (*fn)(TCGv, TCGv_env, TCGv, TCGv, TCGv_i32);

    if (flags == -1) {
        generate_exception(ctx, EXCCODE_INE);
        return true;
    }

    if (!avail_FP_DP(ctx)) {
        return false;
    }

    CHECK_FPE;

    var = tcg_temp_new();
    src1 = get_fpr(ctx, a->fj);
    src2 = get_fpr(ctx, a->fk);
    fn = (a->fcond & 1 ? gen_helper_fcmp_s_d : gen_helper_fcmp_c_d);
    fn(var, tcg_env, src1, src2, tcg_constant_i32(flags));

    tcg_gen_st8_tl(var, tcg_env, offsetof(CPULoongArchState, cf[a->cd]));
    return true;
}
