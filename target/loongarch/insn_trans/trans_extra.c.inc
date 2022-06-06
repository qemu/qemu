/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool trans_break(DisasContext *ctx, arg_break *a)
{
    generate_exception(ctx, EXCCODE_BRK);
    return true;
}

static bool trans_syscall(DisasContext *ctx, arg_syscall *a)
{
    generate_exception(ctx, EXCCODE_SYS);
    return true;
}

static bool trans_asrtle_d(DisasContext *ctx, arg_asrtle_d * a)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_d(cpu_env, src1, src2);
    return true;
}

static bool trans_asrtgt_d(DisasContext *ctx, arg_asrtgt_d * a)
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_d(cpu_env, src1, src2);
    return true;
}

static bool gen_rdtime(DisasContext *ctx, arg_rr *a,
                       bool word, bool high)
{
    TCGv dst1 = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv dst2 = gpr_dst(ctx, a->rj, EXT_NONE);

    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_rdtime_d(dst1, cpu_env);
    if (word) {
        tcg_gen_sextract_tl(dst1, dst1, high ? 32 : 0, 32);
    }
    tcg_gen_ld_i64(dst2, cpu_env, offsetof(CPULoongArchState, CSR_TID));

    return true;
}

static bool trans_rdtimel_w(DisasContext *ctx, arg_rdtimel_w *a)
{
    return gen_rdtime(ctx, a, 1, 0);
}

static bool trans_rdtimeh_w(DisasContext *ctx, arg_rdtimeh_w *a)
{
    return gen_rdtime(ctx, a, 1, 1);
}

static bool trans_rdtime_d(DisasContext *ctx, arg_rdtime_d *a)
{
    return gen_rdtime(ctx, a, 0, 0);
}

static bool trans_cpucfg(DisasContext *ctx, arg_cpucfg *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    gen_helper_cpucfg(dest, cpu_env, src1);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_crc(DisasContext *ctx, arg_rrr *a,
                    void (*func)(TCGv, TCGv, TCGv, TCGv),
                    TCGv tsz)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_SIGN);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    func(dest, src2, src1, tsz);
    gen_set_gpr(a->rd, dest, EXT_SIGN);

    return true;
}

TRANS(crc_w_b_w, gen_crc, gen_helper_crc32, tcg_constant_tl(1))
TRANS(crc_w_h_w, gen_crc, gen_helper_crc32, tcg_constant_tl(2))
TRANS(crc_w_w_w, gen_crc, gen_helper_crc32, tcg_constant_tl(4))
TRANS(crc_w_d_w, gen_crc, gen_helper_crc32, tcg_constant_tl(8))
TRANS(crcc_w_b_w, gen_crc, gen_helper_crc32c, tcg_constant_tl(1))
TRANS(crcc_w_h_w, gen_crc, gen_helper_crc32c, tcg_constant_tl(2))
TRANS(crcc_w_w_w, gen_crc, gen_helper_crc32c, tcg_constant_tl(4))
TRANS(crcc_w_d_w, gen_crc, gen_helper_crc32c, tcg_constant_tl(8))
