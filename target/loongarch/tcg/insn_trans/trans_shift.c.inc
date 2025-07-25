/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static void gen_sll_w(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x1f);
    tcg_gen_shl_tl(dest, src1, t0);
}

static void gen_srl_w(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x1f);
    tcg_gen_shr_tl(dest, src1, t0);
}

static void gen_sra_w(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x1f);
    tcg_gen_sar_tl(dest, src1, t0);
}

static void gen_sll_d(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x3f);
    tcg_gen_shl_tl(dest, src1, t0);
}

static void gen_srl_d(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x3f);
    tcg_gen_shr_tl(dest, src1, t0);
}

static void gen_sra_d(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x3f);
    tcg_gen_sar_tl(dest, src1, t0);
}

static void gen_rotr_w(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, src2, 0x1f);

    tcg_gen_trunc_tl_i32(t1, src1);
    tcg_gen_trunc_tl_i32(t2, t0);

    tcg_gen_rotr_i32(t1, t1, t2);
    tcg_gen_ext_i32_tl(dest, t1);
}

static void gen_rotr_d(TCGv dest, TCGv src1, TCGv src2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, src2, 0x3f);
    tcg_gen_rotr_tl(dest, src1, t0);
}

static void gen_sari_w(TCGv dest, TCGv src1, target_long imm)
{
    tcg_gen_sextract_tl(dest, src1, imm, 32 - imm);
}

TRANS(sll_w, ALL, gen_rrr, EXT_ZERO, EXT_NONE, EXT_SIGN, gen_sll_w)
TRANS(srl_w, ALL, gen_rrr, EXT_ZERO, EXT_NONE, EXT_SIGN, gen_srl_w)
TRANS(sra_w, ALL, gen_rrr, EXT_SIGN, EXT_NONE, EXT_SIGN, gen_sra_w)
TRANS(sll_d, 64, gen_rrr, EXT_NONE, EXT_NONE, EXT_NONE, gen_sll_d)
TRANS(srl_d, 64, gen_rrr, EXT_NONE, EXT_NONE, EXT_NONE, gen_srl_d)
TRANS(sra_d, 64, gen_rrr, EXT_NONE, EXT_NONE, EXT_NONE, gen_sra_d)
TRANS(rotr_w, ALL, gen_rrr, EXT_ZERO, EXT_NONE, EXT_SIGN, gen_rotr_w)
TRANS(rotr_d, 64, gen_rrr, EXT_NONE, EXT_NONE, EXT_NONE, gen_rotr_d)
TRANS(slli_w, ALL, gen_rri_c, EXT_NONE, EXT_SIGN, tcg_gen_shli_tl)
TRANS(slli_d, 64, gen_rri_c, EXT_NONE, EXT_NONE, tcg_gen_shli_tl)
TRANS(srli_w, ALL, gen_rri_c, EXT_ZERO, EXT_SIGN, tcg_gen_shri_tl)
TRANS(srli_d, 64, gen_rri_c, EXT_NONE, EXT_NONE, tcg_gen_shri_tl)
TRANS(srai_w, ALL, gen_rri_c, EXT_NONE, EXT_NONE, gen_sari_w)
TRANS(srai_d, 64, gen_rri_c, EXT_NONE, EXT_NONE, tcg_gen_sari_tl)
TRANS(rotri_w, ALL, gen_rri_v, EXT_NONE, EXT_NONE, gen_rotr_w)
TRANS(rotri_d, 64, gen_rri_c, EXT_NONE, EXT_NONE, tcg_gen_rotri_tl)
