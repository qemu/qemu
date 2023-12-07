/*
 * RISC-V translation routines for the RV64 Zacas Standard Extension.
 *
 * Copyright (c) 2020-2023 PLCT Lab
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

#define REQUIRE_ZACAS(ctx) do {          \
    if (!ctx->cfg_ptr->ext_zacas) {      \
        return false;                     \
    }                                     \
} while (0)

static bool gen_cmpxchg(DisasContext *ctx, arg_atomic *a, MemOp mop)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_address(ctx, a->rs1, 0);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    decode_save_opc(ctx);
    tcg_gen_atomic_cmpxchg_tl(dest, src1, dest, src2, ctx->mem_idx, mop);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool trans_amocas_w(DisasContext *ctx, arg_amocas_w *a)
{
    REQUIRE_ZACAS(ctx);
    return gen_cmpxchg(ctx, a, MO_ALIGN | MO_TESL);
}

static TCGv_i64 get_gpr_pair(DisasContext *ctx, int reg_num)
{
    TCGv_i64 t;

    assert(get_ol(ctx) == MXL_RV32);

    if (reg_num == 0) {
        return tcg_constant_i64(0);
    }

    t = tcg_temp_new_i64();
    tcg_gen_concat_tl_i64(t, cpu_gpr[reg_num], cpu_gpr[reg_num + 1]);
    return t;
}

static void gen_set_gpr_pair(DisasContext *ctx, int reg_num, TCGv_i64 t)
{
    assert(get_ol(ctx) == MXL_RV32);

    if (reg_num != 0) {
#ifdef TARGET_RISCV32
        tcg_gen_extr_i64_i32(cpu_gpr[reg_num], cpu_gpr[reg_num + 1], t);
#else
        tcg_gen_ext32s_i64(cpu_gpr[reg_num], t);
        tcg_gen_sari_i64(cpu_gpr[reg_num + 1], t, 32);
#endif

        if (get_xl_max(ctx) == MXL_RV128) {
            tcg_gen_sari_tl(cpu_gprh[reg_num], cpu_gpr[reg_num], 63);
            tcg_gen_sari_tl(cpu_gprh[reg_num + 1], cpu_gpr[reg_num + 1], 63);
        }
    }
}

static bool gen_cmpxchg64(DisasContext *ctx, arg_atomic *a, MemOp mop)
{
    /*
     * Encodings with odd numbered registers specified in rs2 and rd are
     * reserved.
     */
    if ((a->rs2 | a->rd) & 1) {
        return false;
    }

    TCGv_i64 dest = get_gpr_pair(ctx, a->rd);
    TCGv src1 = get_address(ctx, a->rs1, 0);
    TCGv_i64 src2 = get_gpr_pair(ctx, a->rs2);

    decode_save_opc(ctx);
    tcg_gen_atomic_cmpxchg_i64(dest, src1, dest, src2, ctx->mem_idx, mop);

    gen_set_gpr_pair(ctx, a->rd, dest);
    return true;
}

static bool trans_amocas_d(DisasContext *ctx, arg_amocas_d *a)
{
    REQUIRE_ZACAS(ctx);
    switch (get_ol(ctx)) {
    case MXL_RV32:
        return gen_cmpxchg64(ctx, a, MO_ALIGN | MO_TEUQ);
    case MXL_RV64:
    case MXL_RV128:
        return gen_cmpxchg(ctx, a, MO_ALIGN | MO_TEUQ);
    default:
        g_assert_not_reached();
    }
}

static bool trans_amocas_q(DisasContext *ctx, arg_amocas_q *a)
{
    REQUIRE_ZACAS(ctx);
    REQUIRE_64BIT(ctx);

    /*
     * Encodings with odd numbered registers specified in rs2 and rd are
     * reserved.
     */
    if ((a->rs2 | a->rd) & 1) {
        return false;
    }

#ifdef TARGET_RISCV64
    TCGv_i128 dest = tcg_temp_new_i128();
    TCGv src1 = get_address(ctx, a->rs1, 0);
    TCGv_i128 src2 = tcg_temp_new_i128();
    TCGv_i64 src2l = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv_i64 src2h = get_gpr(ctx, a->rs2 == 0 ? 0 : a->rs2 + 1, EXT_NONE);
    TCGv_i64 destl = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv_i64 desth = get_gpr(ctx, a->rd == 0 ? 0 : a->rd + 1, EXT_NONE);

    tcg_gen_concat_i64_i128(src2, src2l, src2h);
    tcg_gen_concat_i64_i128(dest, destl, desth);
    decode_save_opc(ctx);
    tcg_gen_atomic_cmpxchg_i128(dest, src1, dest, src2, ctx->mem_idx,
                                (MO_ALIGN | MO_TEUO));

    tcg_gen_extr_i128_i64(destl, desth, dest);

    if (a->rd != 0) {
        gen_set_gpr(ctx, a->rd, destl);
        gen_set_gpr(ctx, a->rd + 1, desth);
    }
#endif

    return true;
}
