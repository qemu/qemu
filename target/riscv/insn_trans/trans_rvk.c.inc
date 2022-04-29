/*
 * RISC-V translation routines for the Zk[nd,ne,nh,sed,sh] Standard Extension.
 *
 * Copyright (c) 2021 Ruibo Lu, luruibo2000@163.com
 * Copyright (c) 2021 Zewen Ye, lustrew@foxmail.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_ZKND(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zknd) {              \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNE(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zkne) {              \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKNH(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zknh) {              \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKSED(ctx) do {                 \
    if (!ctx->cfg_ptr->ext_zksed) {             \
        return false;                           \
    }                                           \
} while (0)

#define REQUIRE_ZKSH(ctx) do {                  \
    if (!ctx->cfg_ptr->ext_zksh) {              \
        return false;                           \
    }                                           \
} while (0)

static bool gen_aes32_sm4(DisasContext *ctx, arg_k_aes *a,
                          void (*func)(TCGv, TCGv, TCGv, TCGv))
{
    TCGv shamt = tcg_constant_tl(a->shamt);
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    func(dest, src1, src2, shamt);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_aes32esmi(DisasContext *ctx, arg_aes32esmi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32esmi);
}

static bool trans_aes32esi(DisasContext *ctx, arg_aes32esi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32esi);
}

static bool trans_aes32dsmi(DisasContext *ctx, arg_aes32dsmi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32dsmi);
}

static bool trans_aes32dsi(DisasContext *ctx, arg_aes32dsi *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_aes32dsi);
}

static bool trans_aes64es(DisasContext *ctx, arg_aes64es *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64es, NULL);
}

static bool trans_aes64esm(DisasContext *ctx, arg_aes64esm *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNE(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64esm, NULL);
}

static bool trans_aes64ds(DisasContext *ctx, arg_aes64ds *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64ds, NULL);
}

static bool trans_aes64dsm(DisasContext *ctx, arg_aes64dsm *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64dsm, NULL);
}

static bool trans_aes64ks2(DisasContext *ctx, arg_aes64ks2 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);
    return gen_arith(ctx, a, EXT_NONE, gen_helper_aes64ks2, NULL);
}

static bool trans_aes64ks1i(DisasContext *ctx, arg_aes64ks1i *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_EITHER_EXT(ctx, zknd, zkne);

    if (a->imm > 0xA) {
        return false;
    }

    return gen_arith_imm_tl(ctx, a, EXT_NONE, gen_helper_aes64ks1i, NULL);
}

static bool trans_aes64im(DisasContext *ctx, arg_aes64im *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKND(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_helper_aes64im);
}

static bool gen_sha256(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                       void (*func)(TCGv_i32, TCGv_i32, int32_t),
                       int32_t num1, int32_t num2, int32_t num3)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, src1);
    tcg_gen_rotri_i32(t1, t0, num1);
    tcg_gen_rotri_i32(t2, t0, num2);
    tcg_gen_xor_i32(t1, t1, t2);
    func(t2, t0, num3);
    tcg_gen_xor_i32(t1, t1, t2);
    tcg_gen_ext_i32_tl(dest, t1);

    gen_set_gpr(ctx, a->rd, dest);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    return true;
}

static bool trans_sha256sig0(DisasContext *ctx, arg_sha256sig0 *a)
{
    REQUIRE_ZKNH(ctx);
    return gen_sha256(ctx, a, EXT_NONE, tcg_gen_shri_i32, 7, 18, 3);
}

static bool trans_sha256sig1(DisasContext *ctx, arg_sha256sig1 *a)
{
    REQUIRE_ZKNH(ctx);
    return gen_sha256(ctx, a, EXT_NONE, tcg_gen_shri_i32, 17, 19, 10);
}

static bool trans_sha256sum0(DisasContext *ctx, arg_sha256sum0 *a)
{
    REQUIRE_ZKNH(ctx);
    return gen_sha256(ctx, a, EXT_NONE, tcg_gen_rotri_i32, 2, 13, 22);
}

static bool trans_sha256sum1(DisasContext *ctx, arg_sha256sum1 *a)
{
    REQUIRE_ZKNH(ctx);
    return gen_sha256(ctx, a, EXT_NONE, tcg_gen_rotri_i32, 6, 11, 25);
}

static bool gen_sha512_rv32(DisasContext *ctx, arg_r *a, DisasExtend ext,
                            void (*func1)(TCGv_i64, TCGv_i64, int64_t),
                            void (*func2)(TCGv_i64, TCGv_i64, int64_t),
                            int64_t num1, int64_t num2, int64_t num3)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, ext);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(t0, src1, src2);
    func1(t1, t0, num1);
    func2(t2, t0, num2);
    tcg_gen_xor_i64(t1, t1, t2);
    tcg_gen_rotri_i64(t2, t0, num3);
    tcg_gen_xor_i64(t1, t1, t2);
    tcg_gen_trunc_i64_tl(dest, t1);

    gen_set_gpr(ctx, a->rd, dest);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return true;
}

static bool trans_sha512sum0r(DisasContext *ctx, arg_sha512sum0r *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv32(ctx, a, EXT_NONE, tcg_gen_rotli_i64,
                           tcg_gen_rotli_i64, 25, 30, 28);
}

static bool trans_sha512sum1r(DisasContext *ctx, arg_sha512sum1r *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv32(ctx, a, EXT_NONE, tcg_gen_rotli_i64,
                           tcg_gen_rotri_i64, 23, 14, 18);
}

static bool trans_sha512sig0l(DisasContext *ctx, arg_sha512sig0l *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv32(ctx, a, EXT_NONE, tcg_gen_rotri_i64,
                           tcg_gen_rotri_i64, 1, 7, 8);
}

static bool trans_sha512sig1l(DisasContext *ctx, arg_sha512sig1l *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv32(ctx, a, EXT_NONE, tcg_gen_rotli_i64,
                           tcg_gen_rotri_i64, 3, 6, 19);
}

static bool gen_sha512h_rv32(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (*func)(TCGv_i64, TCGv_i64, int64_t),
                             int64_t num1, int64_t num2, int64_t num3)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, ext);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(t0, src1, src2);
    func(t1, t0, num1);
    tcg_gen_ext32u_i64(t2, t0);
    tcg_gen_shri_i64(t2, t2, num2);
    tcg_gen_xor_i64(t1, t1, t2);
    tcg_gen_rotri_i64(t2, t0, num3);
    tcg_gen_xor_i64(t1, t1, t2);
    tcg_gen_trunc_i64_tl(dest, t1);

    gen_set_gpr(ctx, a->rd, dest);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return true;
}

static bool trans_sha512sig0h(DisasContext *ctx, arg_sha512sig0h *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512h_rv32(ctx, a, EXT_NONE, tcg_gen_rotri_i64, 1, 7, 8);
}

static bool trans_sha512sig1h(DisasContext *ctx, arg_sha512sig1h *a)
{
    REQUIRE_32BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512h_rv32(ctx, a, EXT_NONE, tcg_gen_rotli_i64, 3, 6, 19);
}

static bool gen_sha512_rv64(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                            void (*func)(TCGv_i64, TCGv_i64, int64_t),
                            int64_t num1, int64_t num2, int64_t num3)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, src1);
    tcg_gen_rotri_i64(t1, t0, num1);
    tcg_gen_rotri_i64(t2, t0, num2);
    tcg_gen_xor_i64(t1, t1, t2);
    func(t2, t0, num3);
    tcg_gen_xor_i64(t1, t1, t2);
    tcg_gen_trunc_i64_tl(dest, t1);

    gen_set_gpr(ctx, a->rd, dest);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return true;
}

static bool trans_sha512sig0(DisasContext *ctx, arg_sha512sig0 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv64(ctx, a, EXT_NONE, tcg_gen_shri_i64, 1, 8, 7);
}

static bool trans_sha512sig1(DisasContext *ctx, arg_sha512sig1 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv64(ctx, a, EXT_NONE, tcg_gen_shri_i64, 19, 61, 6);
}

static bool trans_sha512sum0(DisasContext *ctx, arg_sha512sum0 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv64(ctx, a, EXT_NONE, tcg_gen_rotri_i64, 28, 34, 39);
}

static bool trans_sha512sum1(DisasContext *ctx, arg_sha512sum1 *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_ZKNH(ctx);
    return gen_sha512_rv64(ctx, a, EXT_NONE, tcg_gen_rotri_i64, 14, 18, 41);
}

/* SM3 */
static bool gen_sm3(DisasContext *ctx, arg_r2 *a, int32_t b, int32_t c)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, src1);
    tcg_gen_rotli_i32(t1, t0, b);
    tcg_gen_xor_i32(t1, t0, t1);
    tcg_gen_rotli_i32(t0, t0, c);
    tcg_gen_xor_i32(t1, t1, t0);
    tcg_gen_ext_i32_tl(dest, t1);
    gen_set_gpr(ctx, a->rd, dest);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    return true;
}

static bool trans_sm3p0(DisasContext *ctx, arg_sm3p0 *a)
{
    REQUIRE_ZKSH(ctx);
    return gen_sm3(ctx, a, 9, 17);
}

static bool trans_sm3p1(DisasContext *ctx, arg_sm3p1 *a)
{
    REQUIRE_ZKSH(ctx);
    return gen_sm3(ctx, a, 15, 23);
}

/* SM4 */
static bool trans_sm4ed(DisasContext *ctx, arg_sm4ed *a)
{
    REQUIRE_ZKSED(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_sm4ed);
}

static bool trans_sm4ks(DisasContext *ctx, arg_sm4ks *a)
{
    REQUIRE_ZKSED(ctx);
    return gen_aes32_sm4(ctx, a, gen_helper_sm4ks);
}
