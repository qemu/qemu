/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch vector translate functions
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

static bool check_vec(DisasContext *ctx, uint32_t oprsz)
{
    if ((oprsz == 16) && ((ctx->base.tb->flags & HW_FLAGS_EUEN_SXE) == 0)) {
        generate_exception(ctx, EXCCODE_SXD);
        return false;
    }

    if ((oprsz == 32) && ((ctx->base.tb->flags & HW_FLAGS_EUEN_ASXE) == 0)) {
        generate_exception(ctx, EXCCODE_ASXD);
        return false;
    }

    return true;
}

static bool gen_vvvv_ptr_vl(DisasContext *ctx, arg_vvvv *a, uint32_t oprsz,
                            gen_helper_gvec_4_ptr *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_4_ptr(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       vec_full_offset(a->vk),
                       vec_full_offset(a->va),
                       tcg_env,
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vvvv_ptr(DisasContext *ctx, arg_vvvv *a,
                         gen_helper_gvec_4_ptr *fn)
{
    return gen_vvvv_ptr_vl(ctx, a, 16, fn);
}

static bool gen_xxxx_ptr(DisasContext *ctx, arg_vvvv *a,
                         gen_helper_gvec_4_ptr *fn)
{
    return gen_vvvv_ptr_vl(ctx, a, 32, fn);
}

static bool gen_vvvv_vl(DisasContext *ctx, arg_vvvv *a, uint32_t oprsz,
                        gen_helper_gvec_4 *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_4_ool(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       vec_full_offset(a->vk),
                       vec_full_offset(a->va),
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vvvv(DisasContext *ctx, arg_vvvv *a,
                     gen_helper_gvec_4 *fn)
{
    return gen_vvvv_vl(ctx, a, 16, fn);
}

static bool gen_xxxx(DisasContext *ctx, arg_vvvv *a,
                     gen_helper_gvec_4 *fn)
{
    return gen_vvvv_vl(ctx, a, 32, fn);
}

static bool gen_vvv_ptr_vl(DisasContext *ctx, arg_vvv *a, uint32_t oprsz,
                           gen_helper_gvec_3_ptr *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }
    tcg_gen_gvec_3_ptr(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       vec_full_offset(a->vk),
                       tcg_env,
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vvv_ptr(DisasContext *ctx, arg_vvv *a,
                        gen_helper_gvec_3_ptr *fn)
{
    return gen_vvv_ptr_vl(ctx, a, 16, fn);
}

static bool gen_xxx_ptr(DisasContext *ctx, arg_vvv *a,
                        gen_helper_gvec_3_ptr *fn)
{
    return gen_vvv_ptr_vl(ctx, a, 32, fn);
}

static bool gen_vvv_vl(DisasContext *ctx, arg_vvv *a, uint32_t oprsz,
                       gen_helper_gvec_3 *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_3_ool(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       vec_full_offset(a->vk),
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vvv(DisasContext *ctx, arg_vvv *a, gen_helper_gvec_3 *fn)
{
    return gen_vvv_vl(ctx, a, 16, fn);
}

static bool gen_xxx(DisasContext *ctx, arg_vvv *a, gen_helper_gvec_3 *fn)
{
    return gen_vvv_vl(ctx, a, 32, fn);
}

static bool gen_vv_ptr_vl(DisasContext *ctx, arg_vv *a, uint32_t oprsz,
                          gen_helper_gvec_2_ptr *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_2_ptr(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       tcg_env,
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vv_ptr(DisasContext *ctx, arg_vv *a,
                       gen_helper_gvec_2_ptr *fn)
{
    return gen_vv_ptr_vl(ctx, a, 16, fn);
}

static bool gen_xx_ptr(DisasContext *ctx, arg_vv *a,
                       gen_helper_gvec_2_ptr *fn)
{
    return gen_vv_ptr_vl(ctx, a, 32, fn);
}

static bool gen_vv_vl(DisasContext *ctx, arg_vv *a, uint32_t oprsz,
                      gen_helper_gvec_2 *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_2_ool(vec_full_offset(a->vd),
                       vec_full_offset(a->vj),
                       oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vv(DisasContext *ctx, arg_vv *a, gen_helper_gvec_2 *fn)
{
    return gen_vv_vl(ctx, a, 16, fn);
}

static bool gen_xx(DisasContext *ctx, arg_vv *a, gen_helper_gvec_2 *fn)
{
    return gen_vv_vl(ctx, a, 32, fn);
}

static bool gen_vv_i_vl(DisasContext *ctx, arg_vv_i *a, uint32_t oprsz,
                        gen_helper_gvec_2i *fn)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_2i_ool(vec_full_offset(a->vd),
                        vec_full_offset(a->vj),
                        tcg_constant_i64(a->imm),
                        oprsz, ctx->vl / 8, 0, fn);
    return true;
}

static bool gen_vv_i(DisasContext *ctx, arg_vv_i *a, gen_helper_gvec_2i *fn)
{
    return gen_vv_i_vl(ctx, a, 16, fn);
}

static bool gen_xx_i(DisasContext *ctx, arg_vv_i *a, gen_helper_gvec_2i *fn)
{
    return gen_vv_i_vl(ctx, a, 32, fn);
}

static bool gen_cv_vl(DisasContext *ctx, arg_cv *a, uint32_t sz,
                      void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    if (!check_vec(ctx, sz)) {
        return true;
    }

    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 cd = tcg_constant_i32(a->cd);
    TCGv_i32 oprsz = tcg_constant_i32(sz);

    func(tcg_env, oprsz, cd, vj);
    return true;
}

static bool gen_cv(DisasContext *ctx, arg_cv *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    return gen_cv_vl(ctx, a, 16, func);
}

static bool gen_cx(DisasContext *ctx, arg_cv *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    return gen_cv_vl(ctx, a, 32, func);
}

static bool gvec_vvv_vl(DisasContext *ctx, arg_vvv *a,
                        uint32_t oprsz, MemOp mop,
                        void (*func)(unsigned, uint32_t, uint32_t,
                                     uint32_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs = vec_full_offset(a->vd);
    uint32_t vj_ofs = vec_full_offset(a->vj);
    uint32_t vk_ofs = vec_full_offset(a->vk);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    func(mop, vd_ofs, vj_ofs, vk_ofs, oprsz, ctx->vl / 8);
    return true;
}

static bool gvec_vvv(DisasContext *ctx, arg_vvv *a, MemOp mop,
                     void (*func)(unsigned, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t))
{
    return gvec_vvv_vl(ctx, a, 16, mop, func);
}

static bool gvec_xxx(DisasContext *ctx, arg_vvv *a, MemOp mop,
                     void (*func)(unsigned, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t))
{
    return gvec_vvv_vl(ctx, a, 32, mop, func);
}

static bool gvec_vv_vl(DisasContext *ctx, arg_vv *a,
                       uint32_t oprsz, MemOp mop,
                       void (*func)(unsigned, uint32_t, uint32_t,
                                    uint32_t, uint32_t))
{
    uint32_t vd_ofs = vec_full_offset(a->vd);
    uint32_t vj_ofs = vec_full_offset(a->vj);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    func(mop, vd_ofs, vj_ofs, oprsz, ctx->vl / 8);
    return true;
}


static bool gvec_vv(DisasContext *ctx, arg_vv *a, MemOp mop,
                    void (*func)(unsigned, uint32_t, uint32_t,
                                 uint32_t, uint32_t))
{
    return gvec_vv_vl(ctx, a, 16, mop, func);
}

static bool gvec_xx(DisasContext *ctx, arg_vv *a, MemOp mop,
                    void (*func)(unsigned, uint32_t, uint32_t,
                                 uint32_t, uint32_t))
{
    return gvec_vv_vl(ctx, a, 32, mop, func);
}

static bool gvec_vv_i_vl(DisasContext *ctx, arg_vv_i *a,
                         uint32_t oprsz, MemOp mop,
                         void (*func)(unsigned, uint32_t, uint32_t,
                                      int64_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs = vec_full_offset(a->vd);
    uint32_t vj_ofs = vec_full_offset(a->vj);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    func(mop, vd_ofs, vj_ofs, a->imm, oprsz, ctx->vl / 8);
    return true;
}

static bool gvec_vv_i(DisasContext *ctx, arg_vv_i *a, MemOp mop,
                      void (*func)(unsigned, uint32_t, uint32_t,
                                   int64_t, uint32_t, uint32_t))
{
    return gvec_vv_i_vl(ctx, a, 16, mop, func);
}

static bool gvec_xx_i(DisasContext *ctx, arg_vv_i *a, MemOp mop,
                      void (*func)(unsigned, uint32_t, uint32_t,
                                   int64_t, uint32_t, uint32_t))
{
    return gvec_vv_i_vl(ctx,a, 32, mop, func);
}

static bool gvec_subi_vl(DisasContext *ctx, arg_vv_i *a,
                         uint32_t oprsz, MemOp mop)
{
    uint32_t vd_ofs = vec_full_offset(a->vd);
    uint32_t vj_ofs = vec_full_offset(a->vj);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_addi(mop, vd_ofs, vj_ofs, -a->imm, oprsz, ctx->vl / 8);
    return true;
}

static bool gvec_subi(DisasContext *ctx, arg_vv_i *a, MemOp mop)
{
    return gvec_subi_vl(ctx, a, 16, mop);
}

static bool gvec_xsubi(DisasContext *ctx, arg_vv_i *a, MemOp mop)
{
    return gvec_subi_vl(ctx, a, 32, mop);
}

TRANS(vadd_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_add)
TRANS(vadd_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_add)
TRANS(vadd_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_add)
TRANS(vadd_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_add)
TRANS(xvadd_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_add)
TRANS(xvadd_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_add)
TRANS(xvadd_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_add)
TRANS(xvadd_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_add)

static bool gen_vaddsub_q_vl(DisasContext *ctx, arg_vvv *a, uint32_t oprsz,
                             void (*func)(TCGv_i64, TCGv_i64, TCGv_i64,
                                          TCGv_i64, TCGv_i64, TCGv_i64))
{
    int i;
    TCGv_i64 rh, rl, ah, al, bh, bl;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    rh = tcg_temp_new_i64();
    rl = tcg_temp_new_i64();
    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();
    bh = tcg_temp_new_i64();
    bl = tcg_temp_new_i64();

    for (i = 0; i < oprsz / 16; i++) {
        get_vreg64(ah, a->vj, 1 + i * 2);
        get_vreg64(al, a->vj, i * 2);
        get_vreg64(bh, a->vk, 1 + i * 2);
        get_vreg64(bl, a->vk, i * 2);

        func(rl, rh, al, ah, bl, bh);

        set_vreg64(rh, a->vd, 1 + i * 2);
        set_vreg64(rl, a->vd, i * 2);
    }
    return true;
}

static bool gen_vaddsub_q(DisasContext *ctx, arg_vvv *a,
                          void (*func)(TCGv_i64, TCGv_i64, TCGv_i64,
                                       TCGv_i64, TCGv_i64, TCGv_i64))
{
    return gen_vaddsub_q_vl(ctx, a, 16, func);
}

static bool gen_xvaddsub_q(DisasContext *ctx, arg_vvv *a,
                           void (*func)(TCGv_i64, TCGv_i64, TCGv_i64,
                                        TCGv_i64, TCGv_i64, TCGv_i64))
{
    return gen_vaddsub_q_vl(ctx, a, 32, func);
}

TRANS(vsub_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_sub)
TRANS(vsub_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_sub)
TRANS(vsub_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_sub)
TRANS(vsub_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_sub)
TRANS(xvsub_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_sub)
TRANS(xvsub_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_sub)
TRANS(xvsub_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_sub)
TRANS(xvsub_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_sub)

TRANS(vadd_q, LSX, gen_vaddsub_q, tcg_gen_add2_i64)
TRANS(vsub_q, LSX, gen_vaddsub_q, tcg_gen_sub2_i64)
TRANS(xvadd_q, LASX, gen_xvaddsub_q, tcg_gen_add2_i64)
TRANS(xvsub_q, LASX, gen_xvaddsub_q, tcg_gen_sub2_i64)

TRANS(vaddi_bu, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_addi)
TRANS(vaddi_hu, LSX, gvec_vv_i, MO_16, tcg_gen_gvec_addi)
TRANS(vaddi_wu, LSX, gvec_vv_i, MO_32, tcg_gen_gvec_addi)
TRANS(vaddi_du, LSX, gvec_vv_i, MO_64, tcg_gen_gvec_addi)
TRANS(vsubi_bu, LSX, gvec_subi, MO_8)
TRANS(vsubi_hu, LSX, gvec_subi, MO_16)
TRANS(vsubi_wu, LSX, gvec_subi, MO_32)
TRANS(vsubi_du, LSX, gvec_subi, MO_64)
TRANS(xvaddi_bu, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_addi)
TRANS(xvaddi_hu, LASX, gvec_xx_i, MO_16, tcg_gen_gvec_addi)
TRANS(xvaddi_wu, LASX, gvec_xx_i, MO_32, tcg_gen_gvec_addi)
TRANS(xvaddi_du, LASX, gvec_xx_i, MO_64, tcg_gen_gvec_addi)
TRANS(xvsubi_bu, LASX, gvec_xsubi, MO_8)
TRANS(xvsubi_hu, LASX, gvec_xsubi, MO_16)
TRANS(xvsubi_wu, LASX, gvec_xsubi, MO_32)
TRANS(xvsubi_du, LASX, gvec_xsubi, MO_64)

TRANS(vneg_b, LSX, gvec_vv, MO_8, tcg_gen_gvec_neg)
TRANS(vneg_h, LSX, gvec_vv, MO_16, tcg_gen_gvec_neg)
TRANS(vneg_w, LSX, gvec_vv, MO_32, tcg_gen_gvec_neg)
TRANS(vneg_d, LSX, gvec_vv, MO_64, tcg_gen_gvec_neg)
TRANS(xvneg_b, LASX, gvec_xx, MO_8, tcg_gen_gvec_neg)
TRANS(xvneg_h, LASX, gvec_xx, MO_16, tcg_gen_gvec_neg)
TRANS(xvneg_w, LASX, gvec_xx, MO_32, tcg_gen_gvec_neg)
TRANS(xvneg_d, LASX, gvec_xx, MO_64, tcg_gen_gvec_neg)

TRANS(vsadd_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_ssadd)
TRANS(vsadd_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_ssadd)
TRANS(vsadd_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_ssadd)
TRANS(vsadd_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_ssadd)
TRANS(vsadd_bu, LSX, gvec_vvv, MO_8, tcg_gen_gvec_usadd)
TRANS(vsadd_hu, LSX, gvec_vvv, MO_16, tcg_gen_gvec_usadd)
TRANS(vsadd_wu, LSX, gvec_vvv, MO_32, tcg_gen_gvec_usadd)
TRANS(vsadd_du, LSX, gvec_vvv, MO_64, tcg_gen_gvec_usadd)
TRANS(vssub_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_sssub)
TRANS(vssub_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_sssub)
TRANS(vssub_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_sssub)
TRANS(vssub_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_sssub)
TRANS(vssub_bu, LSX, gvec_vvv, MO_8, tcg_gen_gvec_ussub)
TRANS(vssub_hu, LSX, gvec_vvv, MO_16, tcg_gen_gvec_ussub)
TRANS(vssub_wu, LSX, gvec_vvv, MO_32, tcg_gen_gvec_ussub)
TRANS(vssub_du, LSX, gvec_vvv, MO_64, tcg_gen_gvec_ussub)

TRANS(xvsadd_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_ssadd)
TRANS(xvsadd_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_ssadd)
TRANS(xvsadd_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_ssadd)
TRANS(xvsadd_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_ssadd)
TRANS(xvsadd_bu, LASX, gvec_xxx, MO_8, tcg_gen_gvec_usadd)
TRANS(xvsadd_hu, LASX, gvec_xxx, MO_16, tcg_gen_gvec_usadd)
TRANS(xvsadd_wu, LASX, gvec_xxx, MO_32, tcg_gen_gvec_usadd)
TRANS(xvsadd_du, LASX, gvec_xxx, MO_64, tcg_gen_gvec_usadd)
TRANS(xvssub_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_sssub)
TRANS(xvssub_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_sssub)
TRANS(xvssub_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_sssub)
TRANS(xvssub_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_sssub)
TRANS(xvssub_bu, LASX, gvec_xxx, MO_8, tcg_gen_gvec_ussub)
TRANS(xvssub_hu, LASX, gvec_xxx, MO_16, tcg_gen_gvec_ussub)
TRANS(xvssub_wu, LASX, gvec_xxx, MO_32, tcg_gen_gvec_ussub)
TRANS(xvssub_du, LASX, gvec_xxx, MO_64, tcg_gen_gvec_ussub)

TRANS(vhaddw_h_b, LSX, gen_vvv, gen_helper_vhaddw_h_b)
TRANS(vhaddw_w_h, LSX, gen_vvv, gen_helper_vhaddw_w_h)
TRANS(vhaddw_d_w, LSX, gen_vvv, gen_helper_vhaddw_d_w)
TRANS(vhaddw_q_d, LSX, gen_vvv, gen_helper_vhaddw_q_d)
TRANS(vhaddw_hu_bu, LSX, gen_vvv, gen_helper_vhaddw_hu_bu)
TRANS(vhaddw_wu_hu, LSX, gen_vvv, gen_helper_vhaddw_wu_hu)
TRANS(vhaddw_du_wu, LSX, gen_vvv, gen_helper_vhaddw_du_wu)
TRANS(vhaddw_qu_du, LSX, gen_vvv, gen_helper_vhaddw_qu_du)
TRANS(vhsubw_h_b, LSX, gen_vvv, gen_helper_vhsubw_h_b)
TRANS(vhsubw_w_h, LSX, gen_vvv, gen_helper_vhsubw_w_h)
TRANS(vhsubw_d_w, LSX, gen_vvv, gen_helper_vhsubw_d_w)
TRANS(vhsubw_q_d, LSX, gen_vvv, gen_helper_vhsubw_q_d)
TRANS(vhsubw_hu_bu, LSX, gen_vvv, gen_helper_vhsubw_hu_bu)
TRANS(vhsubw_wu_hu, LSX, gen_vvv, gen_helper_vhsubw_wu_hu)
TRANS(vhsubw_du_wu, LSX, gen_vvv, gen_helper_vhsubw_du_wu)
TRANS(vhsubw_qu_du, LSX, gen_vvv, gen_helper_vhsubw_qu_du)

TRANS(xvhaddw_h_b, LASX, gen_xxx, gen_helper_vhaddw_h_b)
TRANS(xvhaddw_w_h, LASX, gen_xxx, gen_helper_vhaddw_w_h)
TRANS(xvhaddw_d_w, LASX, gen_xxx, gen_helper_vhaddw_d_w)
TRANS(xvhaddw_q_d, LASX, gen_xxx, gen_helper_vhaddw_q_d)
TRANS(xvhaddw_hu_bu, LASX, gen_xxx, gen_helper_vhaddw_hu_bu)
TRANS(xvhaddw_wu_hu, LASX, gen_xxx, gen_helper_vhaddw_wu_hu)
TRANS(xvhaddw_du_wu, LASX, gen_xxx, gen_helper_vhaddw_du_wu)
TRANS(xvhaddw_qu_du, LASX, gen_xxx, gen_helper_vhaddw_qu_du)
TRANS(xvhsubw_h_b, LASX, gen_xxx, gen_helper_vhsubw_h_b)
TRANS(xvhsubw_w_h, LASX, gen_xxx, gen_helper_vhsubw_w_h)
TRANS(xvhsubw_d_w, LASX, gen_xxx, gen_helper_vhsubw_d_w)
TRANS(xvhsubw_q_d, LASX, gen_xxx, gen_helper_vhsubw_q_d)
TRANS(xvhsubw_hu_bu, LASX, gen_xxx, gen_helper_vhsubw_hu_bu)
TRANS(xvhsubw_wu_hu, LASX, gen_xxx, gen_helper_vhsubw_wu_hu)
TRANS(xvhsubw_du_wu, LASX, gen_xxx, gen_helper_vhsubw_du_wu)
TRANS(xvhsubw_qu_du, LASX, gen_xxx, gen_helper_vhsubw_qu_du)

static void gen_vaddwev_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Sign-extend the even elements from a */
    tcg_gen_shli_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t1, t1, halfbits);

    /* Sign-extend the even elements from b */
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);

    tcg_gen_add_vec(vece, t, t1, t2);
}

static void gen_vaddwev_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t1, a);
    tcg_gen_ext16s_i32(t2, b);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwev_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(t1, a);
    tcg_gen_ext32s_i64(t2, b);
    tcg_gen_add_i64(t, t1, t2);
}

static void do_vaddwev_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_vaddwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_h,
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_vaddwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_w,
            .fniv = gen_vaddwev_s,
            .fno = gen_helper_vaddwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwev_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwev_h_b, LSX, gvec_vvv, MO_8, do_vaddwev_s)
TRANS(vaddwev_w_h, LSX, gvec_vvv, MO_16, do_vaddwev_s)
TRANS(vaddwev_d_w, LSX, gvec_vvv, MO_32, do_vaddwev_s)
TRANS(vaddwev_q_d, LSX, gvec_vvv, MO_64, do_vaddwev_s)
TRANS(xvaddwev_h_b, LASX, gvec_xxx, MO_8, do_vaddwev_s)
TRANS(xvaddwev_w_h, LASX, gvec_xxx, MO_16, do_vaddwev_s)
TRANS(xvaddwev_d_w, LASX, gvec_xxx, MO_32, do_vaddwev_s)
TRANS(xvaddwev_q_d, LASX, gvec_xxx, MO_64, do_vaddwev_s)

static void gen_vaddwod_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t1, a, 16);
    tcg_gen_sari_i32(t2, b, 16);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwod_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_sari_i64(t1, a, 32);
    tcg_gen_sari_i64(t2, b, 32);
    tcg_gen_add_i64(t, t1, t2);
}

static void gen_vaddwod_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Sign-extend the odd elements for vector */
    tcg_gen_sari_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);

    tcg_gen_add_vec(vece, t, t1, t2);
}

static void do_vaddwod_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_vaddwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_h,
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_vaddwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_w,
            .fniv = gen_vaddwod_s,
            .fno = gen_helper_vaddwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwod_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwod_h_b, LSX, gvec_vvv, MO_8, do_vaddwod_s)
TRANS(vaddwod_w_h, LSX, gvec_vvv, MO_16, do_vaddwod_s)
TRANS(vaddwod_d_w, LSX, gvec_vvv, MO_32, do_vaddwod_s)
TRANS(vaddwod_q_d, LSX, gvec_vvv, MO_64, do_vaddwod_s)
TRANS(xvaddwod_h_b, LASX, gvec_xxx, MO_8, do_vaddwod_s)
TRANS(xvaddwod_w_h, LASX, gvec_xxx, MO_16, do_vaddwod_s)
TRANS(xvaddwod_d_w, LASX, gvec_xxx, MO_32, do_vaddwod_s)
TRANS(xvaddwod_q_d, LASX, gvec_xxx, MO_64, do_vaddwod_s)


static void gen_vsubwev_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Sign-extend the even elements from a */
    tcg_gen_shli_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t1, t1, halfbits);

    /* Sign-extend the even elements from b */
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);

    tcg_gen_sub_vec(vece, t, t1, t2);
}

static void gen_vsubwev_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t1, a);
    tcg_gen_ext16s_i32(t2, b);
    tcg_gen_sub_i32(t, t1, t2);
}

static void gen_vsubwev_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(t1, a);
    tcg_gen_ext32s_i64(t2, b);
    tcg_gen_sub_i64(t, t1, t2);
}

static void do_vsubwev_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_vsubwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwev_w_h,
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_vsubwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwev_d_w,
            .fniv = gen_vsubwev_s,
            .fno = gen_helper_vsubwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vsubwev_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vsubwev_h_b, LSX, gvec_vvv, MO_8, do_vsubwev_s)
TRANS(vsubwev_w_h, LSX, gvec_vvv, MO_16, do_vsubwev_s)
TRANS(vsubwev_d_w, LSX, gvec_vvv, MO_32, do_vsubwev_s)
TRANS(vsubwev_q_d, LSX, gvec_vvv, MO_64, do_vsubwev_s)
TRANS(xvsubwev_h_b, LASX, gvec_xxx, MO_8, do_vsubwev_s)
TRANS(xvsubwev_w_h, LASX, gvec_xxx, MO_16, do_vsubwev_s)
TRANS(xvsubwev_d_w, LASX, gvec_xxx, MO_32, do_vsubwev_s)
TRANS(xvsubwev_q_d, LASX, gvec_xxx, MO_64, do_vsubwev_s)

static void gen_vsubwod_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Sign-extend the odd elements for vector */
    tcg_gen_sari_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);

    tcg_gen_sub_vec(vece, t, t1, t2);
}

static void gen_vsubwod_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t1, a, 16);
    tcg_gen_sari_i32(t2, b, 16);
    tcg_gen_sub_i32(t, t1, t2);
}

static void gen_vsubwod_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_sari_i64(t1, a, 32);
    tcg_gen_sari_i64(t2, b, 32);
    tcg_gen_sub_i64(t, t1, t2);
}

static void do_vsubwod_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_vsubwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwod_w_h,
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_vsubwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwod_d_w,
            .fniv = gen_vsubwod_s,
            .fno = gen_helper_vsubwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vsubwod_q_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vsubwod_h_b, LSX, gvec_vvv, MO_8, do_vsubwod_s)
TRANS(vsubwod_w_h, LSX, gvec_vvv, MO_16, do_vsubwod_s)
TRANS(vsubwod_d_w, LSX, gvec_vvv, MO_32, do_vsubwod_s)
TRANS(vsubwod_q_d, LSX, gvec_vvv, MO_64, do_vsubwod_s)
TRANS(xvsubwod_h_b, LASX, gvec_xxx, MO_8, do_vsubwod_s)
TRANS(xvsubwod_w_h, LASX, gvec_xxx, MO_16, do_vsubwod_s)
TRANS(xvsubwod_d_w, LASX, gvec_xxx, MO_32, do_vsubwod_s)
TRANS(xvsubwod_q_d, LASX, gvec_xxx, MO_64, do_vsubwod_s)

static void gen_vaddwev_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, t3);
    tcg_gen_and_vec(vece, t2, b, t3);
    tcg_gen_add_vec(vece, t, t1, t2);
}

static void gen_vaddwev_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t1, a);
    tcg_gen_ext16u_i32(t2, b);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwev_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(t1, a);
    tcg_gen_ext32u_i64(t2, b);
    tcg_gen_add_i64(t, t1, t2);
}

static void do_vaddwev_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_vaddwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_hu,
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_vaddwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_wu,
            .fniv = gen_vaddwev_u,
            .fno = gen_helper_vaddwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwev_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwev_h_bu, LSX, gvec_vvv, MO_8, do_vaddwev_u)
TRANS(vaddwev_w_hu, LSX, gvec_vvv, MO_16, do_vaddwev_u)
TRANS(vaddwev_d_wu, LSX, gvec_vvv, MO_32, do_vaddwev_u)
TRANS(vaddwev_q_du, LSX, gvec_vvv, MO_64, do_vaddwev_u)
TRANS(xvaddwev_h_bu, LASX, gvec_xxx, MO_8, do_vaddwev_u)
TRANS(xvaddwev_w_hu, LASX, gvec_xxx, MO_16, do_vaddwev_u)
TRANS(xvaddwev_d_wu, LASX, gvec_xxx, MO_32, do_vaddwev_u)
TRANS(xvaddwev_q_du, LASX, gvec_xxx, MO_64, do_vaddwev_u)

static void gen_vaddwod_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Zero-extend the odd elements for vector */
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_shri_vec(vece, t2, b, halfbits);

    tcg_gen_add_vec(vece, t, t1, t2);
}

static void gen_vaddwod_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t1, a, 16);
    tcg_gen_shri_i32(t2, b, 16);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwod_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, a, 32);
    tcg_gen_shri_i64(t2, b, 32);
    tcg_gen_add_i64(t, t1, t2);
}

static void do_vaddwod_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_vaddwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_hu,
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_vaddwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_wu,
            .fniv = gen_vaddwod_u,
            .fno = gen_helper_vaddwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwod_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwod_h_bu, LSX, gvec_vvv, MO_8, do_vaddwod_u)
TRANS(vaddwod_w_hu, LSX, gvec_vvv, MO_16, do_vaddwod_u)
TRANS(vaddwod_d_wu, LSX, gvec_vvv, MO_32, do_vaddwod_u)
TRANS(vaddwod_q_du, LSX, gvec_vvv, MO_64, do_vaddwod_u)
TRANS(xvaddwod_h_bu, LASX, gvec_xxx, MO_8, do_vaddwod_u)
TRANS(xvaddwod_w_hu, LASX, gvec_xxx, MO_16, do_vaddwod_u)
TRANS(xvaddwod_d_wu, LASX, gvec_xxx, MO_32, do_vaddwod_u)
TRANS(xvaddwod_q_du, LASX, gvec_xxx, MO_64, do_vaddwod_u)

static void gen_vsubwev_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, t3);
    tcg_gen_and_vec(vece, t2, b, t3);
    tcg_gen_sub_vec(vece, t, t1, t2);
}

static void gen_vsubwev_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t1, a);
    tcg_gen_ext16u_i32(t2, b);
    tcg_gen_sub_i32(t, t1, t2);
}

static void gen_vsubwev_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(t1, a);
    tcg_gen_ext32u_i64(t2, b);
    tcg_gen_sub_i64(t, t1, t2);
}

static void do_vsubwev_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_vsubwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwev_w_hu,
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_vsubwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwev_d_wu,
            .fniv = gen_vsubwev_u,
            .fno = gen_helper_vsubwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vsubwev_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vsubwev_h_bu, LSX, gvec_vvv, MO_8, do_vsubwev_u)
TRANS(vsubwev_w_hu, LSX, gvec_vvv, MO_16, do_vsubwev_u)
TRANS(vsubwev_d_wu, LSX, gvec_vvv, MO_32, do_vsubwev_u)
TRANS(vsubwev_q_du, LSX, gvec_vvv, MO_64, do_vsubwev_u)
TRANS(xvsubwev_h_bu, LASX, gvec_xxx, MO_8, do_vsubwev_u)
TRANS(xvsubwev_w_hu, LASX, gvec_xxx, MO_16, do_vsubwev_u)
TRANS(xvsubwev_d_wu, LASX, gvec_xxx, MO_32, do_vsubwev_u)
TRANS(xvsubwev_q_du, LASX, gvec_xxx, MO_64, do_vsubwev_u)

static void gen_vsubwod_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Zero-extend the odd elements for vector */
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_shri_vec(vece, t2, b, halfbits);

    tcg_gen_sub_vec(vece, t, t1, t2);
}

static void gen_vsubwod_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t1, a, 16);
    tcg_gen_shri_i32(t2, b, 16);
    tcg_gen_sub_i32(t, t1, t2);
}

static void gen_vsubwod_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, a, 32);
    tcg_gen_shri_i64(t2, b, 32);
    tcg_gen_sub_i64(t, t1, t2);
}

static void do_vsubwod_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_vsubwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vsubwod_w_hu,
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_vsubwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vsubwod_d_wu,
            .fniv = gen_vsubwod_u,
            .fno = gen_helper_vsubwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vsubwod_q_du,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vsubwod_h_bu, LSX, gvec_vvv, MO_8, do_vsubwod_u)
TRANS(vsubwod_w_hu, LSX, gvec_vvv, MO_16, do_vsubwod_u)
TRANS(vsubwod_d_wu, LSX, gvec_vvv, MO_32, do_vsubwod_u)
TRANS(vsubwod_q_du, LSX, gvec_vvv, MO_64, do_vsubwod_u)
TRANS(xvsubwod_h_bu, LASX, gvec_xxx, MO_8, do_vsubwod_u)
TRANS(xvsubwod_w_hu, LASX, gvec_xxx, MO_16, do_vsubwod_u)
TRANS(xvsubwod_d_wu, LASX, gvec_xxx, MO_32, do_vsubwod_u)
TRANS(xvsubwod_q_du, LASX, gvec_xxx, MO_64, do_vsubwod_u)

static void gen_vaddwev_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, halfbits));

    /* Zero-extend the even elements from a */
    tcg_gen_and_vec(vece, t1, a, t3);

    /* Sign-extend the even elements from b */
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);

    tcg_gen_add_vec(vece, t, t1, t2);
}

static void gen_vaddwev_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t1, a);
    tcg_gen_ext16s_i32(t2, b);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwev_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(t1, a);
    tcg_gen_ext32s_i64(t2, b);
    tcg_gen_add_i64(t, t1, t2);
}

static void do_vaddwev_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                           uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_vaddwev_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwev_w_hu_h,
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_vaddwev_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwev_d_wu_w,
            .fniv = gen_vaddwev_u_s,
            .fno = gen_helper_vaddwev_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwev_q_du_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwev_h_bu_b, LSX, gvec_vvv, MO_8, do_vaddwev_u_s)
TRANS(vaddwev_w_hu_h, LSX, gvec_vvv, MO_16, do_vaddwev_u_s)
TRANS(vaddwev_d_wu_w, LSX, gvec_vvv, MO_32, do_vaddwev_u_s)
TRANS(vaddwev_q_du_d, LSX, gvec_vvv, MO_64, do_vaddwev_u_s)
TRANS(xvaddwev_h_bu_b, LASX, gvec_xxx, MO_8, do_vaddwev_u_s)
TRANS(xvaddwev_w_hu_h, LASX, gvec_xxx, MO_16, do_vaddwev_u_s)
TRANS(xvaddwev_d_wu_w, LASX, gvec_xxx, MO_32, do_vaddwev_u_s)
TRANS(xvaddwev_q_du_d, LASX, gvec_xxx, MO_64, do_vaddwev_u_s)

static void gen_vaddwod_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    /* Zero-extend the odd elements from a */
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    /* Sign-extend the odd elements from b */
    tcg_gen_sari_vec(vece, t2, b, halfbits);

    tcg_gen_add_vec(vece, t, t1, t2);
}

static void gen_vaddwod_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t1, a, 16);
    tcg_gen_sari_i32(t2, b, 16);
    tcg_gen_add_i32(t, t1, t2);
}

static void gen_vaddwod_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, a, 32);
    tcg_gen_sari_i64(t2, b, 32);
    tcg_gen_add_i64(t, t1, t2);
}

static void do_vaddwod_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                           uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec,  INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_vaddwod_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vaddwod_w_hu_h,
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_vaddwod_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vaddwod_d_wu_w,
            .fniv = gen_vaddwod_u_s,
            .fno = gen_helper_vaddwod_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_vaddwod_q_du_d,
            .vece = MO_128
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vaddwod_h_bu_b, LSX, gvec_vvv, MO_8, do_vaddwod_u_s)
TRANS(vaddwod_w_hu_h, LSX, gvec_vvv, MO_16, do_vaddwod_u_s)
TRANS(vaddwod_d_wu_w, LSX, gvec_vvv, MO_32, do_vaddwod_u_s)
TRANS(vaddwod_q_du_d, LSX, gvec_vvv, MO_64, do_vaddwod_u_s)
TRANS(xvaddwod_h_bu_b, LSX, gvec_xxx, MO_8, do_vaddwod_u_s)
TRANS(xvaddwod_w_hu_h, LSX, gvec_xxx, MO_16, do_vaddwod_u_s)
TRANS(xvaddwod_d_wu_w, LSX, gvec_xxx, MO_32, do_vaddwod_u_s)
TRANS(xvaddwod_q_du_d, LSX, gvec_xxx, MO_64, do_vaddwod_u_s)

static void do_vavg(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                    void (*gen_shr_vec)(unsigned, TCGv_vec,
                                        TCGv_vec, int64_t),
                    void (*gen_round_vec)(unsigned, TCGv_vec,
                                          TCGv_vec, TCGv_vec))
{
    TCGv_vec tmp = tcg_temp_new_vec_matching(t);
    gen_round_vec(vece, tmp, a, b);
    tcg_gen_and_vec(vece, tmp, tmp, tcg_constant_vec_matching(t, vece, 1));
    gen_shr_vec(vece, a, a, 1);
    gen_shr_vec(vece, b, b, 1);
    tcg_gen_add_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, t, t, tmp);
}

static void gen_vavg_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_sari_vec, tcg_gen_and_vec);
}

static void gen_vavg_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_shri_vec, tcg_gen_and_vec);
}

static void gen_vavgr_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_sari_vec, tcg_gen_or_vec);
}

static void gen_vavgr_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_shri_vec, tcg_gen_or_vec);
}

static void do_vavg_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_vavg_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_vavg_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_vavg_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavg_s,
            .fno = gen_helper_vavg_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

static void do_vavg_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_vavg_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_vavg_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_vavg_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavg_u,
            .fno = gen_helper_vavg_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vavg_b, LSX, gvec_vvv, MO_8, do_vavg_s)
TRANS(vavg_h, LSX, gvec_vvv, MO_16, do_vavg_s)
TRANS(vavg_w, LSX, gvec_vvv, MO_32, do_vavg_s)
TRANS(vavg_d, LSX, gvec_vvv, MO_64, do_vavg_s)
TRANS(vavg_bu, LSX, gvec_vvv, MO_8, do_vavg_u)
TRANS(vavg_hu, LSX, gvec_vvv, MO_16, do_vavg_u)
TRANS(vavg_wu, LSX, gvec_vvv, MO_32, do_vavg_u)
TRANS(vavg_du, LSX, gvec_vvv, MO_64, do_vavg_u)
TRANS(xvavg_b, LASX, gvec_xxx, MO_8, do_vavg_s)
TRANS(xvavg_h, LASX, gvec_xxx, MO_16, do_vavg_s)
TRANS(xvavg_w, LASX, gvec_xxx, MO_32, do_vavg_s)
TRANS(xvavg_d, LASX, gvec_xxx, MO_64, do_vavg_s)
TRANS(xvavg_bu, LASX, gvec_xxx, MO_8, do_vavg_u)
TRANS(xvavg_hu, LASX, gvec_xxx, MO_16, do_vavg_u)
TRANS(xvavg_wu, LASX, gvec_xxx, MO_32, do_vavg_u)
TRANS(xvavg_du, LASX, gvec_xxx, MO_64, do_vavg_u)

static void do_vavgr_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_vavgr_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_vavgr_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_vavgr_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavgr_s,
            .fno = gen_helper_vavgr_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

static void do_vavgr_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_vavgr_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_vavgr_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_vavgr_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vavgr_u,
            .fno = gen_helper_vavgr_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vavgr_b, LSX, gvec_vvv, MO_8, do_vavgr_s)
TRANS(vavgr_h, LSX, gvec_vvv, MO_16, do_vavgr_s)
TRANS(vavgr_w, LSX, gvec_vvv, MO_32, do_vavgr_s)
TRANS(vavgr_d, LSX, gvec_vvv, MO_64, do_vavgr_s)
TRANS(vavgr_bu, LSX, gvec_vvv, MO_8, do_vavgr_u)
TRANS(vavgr_hu, LSX, gvec_vvv, MO_16, do_vavgr_u)
TRANS(vavgr_wu, LSX, gvec_vvv, MO_32, do_vavgr_u)
TRANS(vavgr_du, LSX, gvec_vvv, MO_64, do_vavgr_u)
TRANS(xvavgr_b, LASX, gvec_xxx, MO_8, do_vavgr_s)
TRANS(xvavgr_h, LASX, gvec_xxx, MO_16, do_vavgr_s)
TRANS(xvavgr_w, LASX, gvec_xxx, MO_32, do_vavgr_s)
TRANS(xvavgr_d, LASX, gvec_xxx, MO_64, do_vavgr_s)
TRANS(xvavgr_bu, LASX, gvec_xxx, MO_8, do_vavgr_u)
TRANS(xvavgr_hu, LASX, gvec_xxx, MO_16, do_vavgr_u)
TRANS(xvavgr_wu, LASX, gvec_xxx, MO_32, do_vavgr_u)
TRANS(xvavgr_du, LASX, gvec_xxx, MO_64, do_vavgr_u)

static void gen_vabsd_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_smax_vec(vece, t, a, b);
    tcg_gen_smin_vec(vece, a, a, b);
    tcg_gen_sub_vec(vece, t, t, a);
}

static void do_vabsd_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, INDEX_op_smin_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_vabsd_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_vabsd_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_vabsd_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vabsd_s,
            .fno = gen_helper_vabsd_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

static void gen_vabsd_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_umax_vec(vece, t, a, b);
    tcg_gen_umin_vec(vece, a, a, b);
    tcg_gen_sub_vec(vece, t, t, a);
}

static void do_vabsd_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umax_vec, INDEX_op_umin_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_vabsd_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_vabsd_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_vabsd_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vabsd_u,
            .fno = gen_helper_vabsd_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vabsd_b, LSX, gvec_vvv, MO_8, do_vabsd_s)
TRANS(vabsd_h, LSX, gvec_vvv, MO_16, do_vabsd_s)
TRANS(vabsd_w, LSX, gvec_vvv, MO_32, do_vabsd_s)
TRANS(vabsd_d, LSX, gvec_vvv, MO_64, do_vabsd_s)
TRANS(vabsd_bu, LSX, gvec_vvv, MO_8, do_vabsd_u)
TRANS(vabsd_hu, LSX, gvec_vvv, MO_16, do_vabsd_u)
TRANS(vabsd_wu, LSX, gvec_vvv, MO_32, do_vabsd_u)
TRANS(vabsd_du, LSX, gvec_vvv, MO_64, do_vabsd_u)
TRANS(xvabsd_b, LASX, gvec_xxx, MO_8, do_vabsd_s)
TRANS(xvabsd_h, LASX, gvec_xxx, MO_16, do_vabsd_s)
TRANS(xvabsd_w, LASX, gvec_xxx, MO_32, do_vabsd_s)
TRANS(xvabsd_d, LASX, gvec_xxx, MO_64, do_vabsd_s)
TRANS(xvabsd_bu, LASX, gvec_xxx, MO_8, do_vabsd_u)
TRANS(xvabsd_hu, LASX, gvec_xxx, MO_16, do_vabsd_u)
TRANS(xvabsd_wu, LASX, gvec_xxx, MO_32, do_vabsd_u)
TRANS(xvabsd_du, LASX, gvec_xxx, MO_64, do_vabsd_u)

static void gen_vadda(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);

    tcg_gen_abs_vec(vece, t1, a);
    tcg_gen_abs_vec(vece, t2, b);
    tcg_gen_add_vec(vece, t, t1, t2);
}

static void do_vadda(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                     uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_abs_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vadda,
            .fno = gen_helper_vadda_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_vadda_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_vadda_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vadda,
            .fno = gen_helper_vadda_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vadda_b, LSX, gvec_vvv, MO_8, do_vadda)
TRANS(vadda_h, LSX, gvec_vvv, MO_16, do_vadda)
TRANS(vadda_w, LSX, gvec_vvv, MO_32, do_vadda)
TRANS(vadda_d, LSX, gvec_vvv, MO_64, do_vadda)
TRANS(xvadda_b, LASX, gvec_xxx, MO_8, do_vadda)
TRANS(xvadda_h, LASX, gvec_xxx, MO_16, do_vadda)
TRANS(xvadda_w, LASX, gvec_xxx, MO_32, do_vadda)
TRANS(xvadda_d, LASX, gvec_xxx, MO_64, do_vadda)

TRANS(vmax_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_smax)
TRANS(vmax_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_smax)
TRANS(vmax_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_smax)
TRANS(vmax_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_smax)
TRANS(vmax_bu, LSX, gvec_vvv, MO_8, tcg_gen_gvec_umax)
TRANS(vmax_hu, LSX, gvec_vvv, MO_16, tcg_gen_gvec_umax)
TRANS(vmax_wu, LSX, gvec_vvv, MO_32, tcg_gen_gvec_umax)
TRANS(vmax_du, LSX, gvec_vvv, MO_64, tcg_gen_gvec_umax)
TRANS(xvmax_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_smax)
TRANS(xvmax_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_smax)
TRANS(xvmax_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_smax)
TRANS(xvmax_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_smax)
TRANS(xvmax_bu, LASX, gvec_xxx, MO_8, tcg_gen_gvec_umax)
TRANS(xvmax_hu, LASX, gvec_xxx, MO_16, tcg_gen_gvec_umax)
TRANS(xvmax_wu, LASX, gvec_xxx, MO_32, tcg_gen_gvec_umax)
TRANS(xvmax_du, LASX, gvec_xxx, MO_64, tcg_gen_gvec_umax)

TRANS(vmin_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_smin)
TRANS(vmin_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_smin)
TRANS(vmin_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_smin)
TRANS(vmin_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_smin)
TRANS(vmin_bu, LSX, gvec_vvv, MO_8, tcg_gen_gvec_umin)
TRANS(vmin_hu, LSX, gvec_vvv, MO_16, tcg_gen_gvec_umin)
TRANS(vmin_wu, LSX, gvec_vvv, MO_32, tcg_gen_gvec_umin)
TRANS(vmin_du, LSX, gvec_vvv, MO_64, tcg_gen_gvec_umin)
TRANS(xvmin_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_smin)
TRANS(xvmin_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_smin)
TRANS(xvmin_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_smin)
TRANS(xvmin_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_smin)
TRANS(xvmin_bu, LASX, gvec_xxx, MO_8, tcg_gen_gvec_umin)
TRANS(xvmin_hu, LASX, gvec_xxx, MO_16, tcg_gen_gvec_umin)
TRANS(xvmin_wu, LASX, gvec_xxx, MO_32, tcg_gen_gvec_umin)
TRANS(xvmin_du, LASX, gvec_xxx, MO_64, tcg_gen_gvec_umin)

static void gen_vmini_s(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    tcg_gen_smin_vec(vece, t, a, tcg_constant_vec_matching(t, vece, imm));
}

static void gen_vmini_u(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    tcg_gen_umin_vec(vece, t, a, tcg_constant_vec_matching(t, vece, imm));
}

static void gen_vmaxi_s(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    tcg_gen_smax_vec(vece, t, a, tcg_constant_vec_matching(t, vece, imm));
}

static void gen_vmaxi_u(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    tcg_gen_umax_vec(vece, t, a, tcg_constant_vec_matching(t, vece, imm));
}

static void do_vmini_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smin_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_vmini_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_vmini_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_vmini_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmini_s,
            .fnoi = gen_helper_vmini_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

static void do_vmini_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umin_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_vmini_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_vmini_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_vmini_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmini_u,
            .fnoi = gen_helper_vmini_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(vmini_b, LSX, gvec_vv_i, MO_8, do_vmini_s)
TRANS(vmini_h, LSX, gvec_vv_i, MO_16, do_vmini_s)
TRANS(vmini_w, LSX, gvec_vv_i, MO_32, do_vmini_s)
TRANS(vmini_d, LSX, gvec_vv_i, MO_64, do_vmini_s)
TRANS(vmini_bu, LSX, gvec_vv_i, MO_8, do_vmini_u)
TRANS(vmini_hu, LSX, gvec_vv_i, MO_16, do_vmini_u)
TRANS(vmini_wu, LSX, gvec_vv_i, MO_32, do_vmini_u)
TRANS(vmini_du, LSX, gvec_vv_i, MO_64, do_vmini_u)
TRANS(xvmini_b, LASX, gvec_xx_i, MO_8, do_vmini_s)
TRANS(xvmini_h, LASX, gvec_xx_i, MO_16, do_vmini_s)
TRANS(xvmini_w, LASX, gvec_xx_i, MO_32, do_vmini_s)
TRANS(xvmini_d, LASX, gvec_xx_i, MO_64, do_vmini_s)
TRANS(xvmini_bu, LASX, gvec_xx_i, MO_8, do_vmini_u)
TRANS(xvmini_hu, LASX, gvec_xx_i, MO_16, do_vmini_u)
TRANS(xvmini_wu, LASX, gvec_xx_i, MO_32, do_vmini_u)
TRANS(xvmini_du, LASX, gvec_xx_i, MO_64, do_vmini_u)

static void do_vmaxi_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_vmaxi_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_vmaxi_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_vmaxi_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmaxi_s,
            .fnoi = gen_helper_vmaxi_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

static void do_vmaxi_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umax_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_vmaxi_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_vmaxi_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_vmaxi_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vmaxi_u,
            .fnoi = gen_helper_vmaxi_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(vmaxi_b, LSX, gvec_vv_i, MO_8, do_vmaxi_s)
TRANS(vmaxi_h, LSX, gvec_vv_i, MO_16, do_vmaxi_s)
TRANS(vmaxi_w, LSX, gvec_vv_i, MO_32, do_vmaxi_s)
TRANS(vmaxi_d, LSX, gvec_vv_i, MO_64, do_vmaxi_s)
TRANS(vmaxi_bu, LSX, gvec_vv_i, MO_8, do_vmaxi_u)
TRANS(vmaxi_hu, LSX, gvec_vv_i, MO_16, do_vmaxi_u)
TRANS(vmaxi_wu, LSX, gvec_vv_i, MO_32, do_vmaxi_u)
TRANS(vmaxi_du, LSX, gvec_vv_i, MO_64, do_vmaxi_u)
TRANS(xvmaxi_b, LASX, gvec_xx_i, MO_8, do_vmaxi_s)
TRANS(xvmaxi_h, LASX, gvec_xx_i, MO_16, do_vmaxi_s)
TRANS(xvmaxi_w, LASX, gvec_xx_i, MO_32, do_vmaxi_s)
TRANS(xvmaxi_d, LASX, gvec_xx_i, MO_64, do_vmaxi_s)
TRANS(xvmaxi_bu, LASX, gvec_xx_i, MO_8, do_vmaxi_u)
TRANS(xvmaxi_hu, LASX, gvec_xx_i, MO_16, do_vmaxi_u)
TRANS(xvmaxi_wu, LASX, gvec_xx_i, MO_32, do_vmaxi_u)
TRANS(xvmaxi_du, LASX, gvec_xx_i, MO_64, do_vmaxi_u)

TRANS(vmul_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_mul)
TRANS(vmul_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_mul)
TRANS(vmul_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_mul)
TRANS(vmul_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_mul)
TRANS(xvmul_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_mul)
TRANS(xvmul_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_mul)
TRANS(xvmul_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_mul)
TRANS(xvmul_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_mul)

static void gen_vmuh_w(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 discard = tcg_temp_new_i32();
    tcg_gen_muls2_i32(discard, t, a, b);
}

static void gen_vmuh_d(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 discard = tcg_temp_new_i64();
    tcg_gen_muls2_i64(discard, t, a, b);
}

static void do_vmuh_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 op[4] = {
        {
            .fno = gen_helper_vmuh_b,
            .vece = MO_8
        },
        {
            .fno = gen_helper_vmuh_h,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmuh_w,
            .fno = gen_helper_vmuh_w,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmuh_d,
            .fno = gen_helper_vmuh_d,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmuh_b, LSX, gvec_vvv, MO_8, do_vmuh_s)
TRANS(vmuh_h, LSX, gvec_vvv, MO_16, do_vmuh_s)
TRANS(vmuh_w, LSX, gvec_vvv, MO_32, do_vmuh_s)
TRANS(vmuh_d, LSX, gvec_vvv, MO_64, do_vmuh_s)
TRANS(xvmuh_b, LASX, gvec_xxx, MO_8, do_vmuh_s)
TRANS(xvmuh_h, LASX, gvec_xxx, MO_16, do_vmuh_s)
TRANS(xvmuh_w, LASX, gvec_xxx, MO_32, do_vmuh_s)
TRANS(xvmuh_d, LASX, gvec_xxx, MO_64, do_vmuh_s)

static void gen_vmuh_wu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 discard = tcg_temp_new_i32();
    tcg_gen_mulu2_i32(discard, t, a, b);
}

static void gen_vmuh_du(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 discard = tcg_temp_new_i64();
    tcg_gen_mulu2_i64(discard, t, a, b);
}

static void do_vmuh_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const GVecGen3 op[4] = {
        {
            .fno = gen_helper_vmuh_bu,
            .vece = MO_8
        },
        {
            .fno = gen_helper_vmuh_hu,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmuh_wu,
            .fno = gen_helper_vmuh_wu,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmuh_du,
            .fno = gen_helper_vmuh_du,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmuh_bu, LSX, gvec_vvv, MO_8,  do_vmuh_u)
TRANS(vmuh_hu, LSX, gvec_vvv, MO_16, do_vmuh_u)
TRANS(vmuh_wu, LSX, gvec_vvv, MO_32, do_vmuh_u)
TRANS(vmuh_du, LSX, gvec_vvv, MO_64, do_vmuh_u)
TRANS(xvmuh_bu, LASX, gvec_xxx, MO_8,  do_vmuh_u)
TRANS(xvmuh_hu, LASX, gvec_xxx, MO_16, do_vmuh_u)
TRANS(xvmuh_wu, LASX, gvec_xxx, MO_32, do_vmuh_u)
TRANS(xvmuh_du, LASX, gvec_xxx, MO_64, do_vmuh_u)

static void gen_vmulwev_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    tcg_gen_shli_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t1, t1, halfbits);
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwev_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t1, a);
    tcg_gen_ext16s_i32(t2, b);
    tcg_gen_mul_i32(t, t1, t2);
}

static void gen_vmulwev_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(t1, a);
    tcg_gen_ext32s_i64(t2, b);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwev_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_vmulwev_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_h,
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_vmulwev_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_w,
            .fniv = gen_vmulwev_s,
            .fno = gen_helper_vmulwev_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwev_h_b, LSX, gvec_vvv, MO_8, do_vmulwev_s)
TRANS(vmulwev_w_h, LSX, gvec_vvv, MO_16, do_vmulwev_s)
TRANS(vmulwev_d_w, LSX, gvec_vvv, MO_32, do_vmulwev_s)
TRANS(xvmulwev_h_b, LASX, gvec_xxx, MO_8, do_vmulwev_s)
TRANS(xvmulwev_w_h, LASX, gvec_xxx, MO_16, do_vmulwev_s)
TRANS(xvmulwev_d_w, LASX, gvec_xxx, MO_32, do_vmulwev_s)

static void tcg_gen_mulus2_i64(TCGv_i64 rl, TCGv_i64 rh,
                               TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_mulsu2_i64(rl, rh, arg2, arg1);
}

static bool gen_vmul_q_vl(DisasContext *ctx,
                          arg_vvv *a, uint32_t oprsz, int idx1, int idx2,
                          void (*func)(TCGv_i64, TCGv_i64,
                                       TCGv_i64, TCGv_i64))
{
    TCGv_i64 rh, rl, arg1, arg2;
    int i;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    rh = tcg_temp_new_i64();
    rl = tcg_temp_new_i64();
    arg1 = tcg_temp_new_i64();
    arg2 = tcg_temp_new_i64();

    for (i = 0; i < oprsz / 16; i++) {
        get_vreg64(arg1, a->vj, 2 * i + idx1);
        get_vreg64(arg2, a->vk, 2 * i + idx2);

        func(rl, rh, arg1, arg2);

        set_vreg64(rh, a->vd, 2 * i + 1);
        set_vreg64(rl, a->vd, 2 * i);
    }

    return true;
}

static bool gen_vmul_q(DisasContext *ctx, arg_vvv *a, int idx1, int idx2,
                       void (*func)(TCGv_i64, TCGv_i64,
                                    TCGv_i64, TCGv_i64))
{
    return gen_vmul_q_vl(ctx, a, 16, idx1, idx2, func);
}

static bool gen_xvmul_q(DisasContext *ctx, arg_vvv *a, int idx1, int idx2,
                        void (*func)(TCGv_i64, TCGv_i64,
                                     TCGv_i64, TCGv_i64))
{
    return gen_vmul_q_vl(ctx, a, 32, idx1, idx2, func);
}

TRANS(vmulwev_q_d, LSX, gen_vmul_q, 0, 0, tcg_gen_muls2_i64)
TRANS(vmulwod_q_d, LSX, gen_vmul_q, 1, 1, tcg_gen_muls2_i64)
TRANS(vmulwev_q_du, LSX, gen_vmul_q, 0, 0, tcg_gen_mulu2_i64)
TRANS(vmulwod_q_du, LSX, gen_vmul_q, 1, 1, tcg_gen_mulu2_i64)
TRANS(vmulwev_q_du_d, LSX, gen_vmul_q, 0, 0, tcg_gen_mulus2_i64)
TRANS(vmulwod_q_du_d, LSX, gen_vmul_q, 1, 1, tcg_gen_mulus2_i64)
TRANS(xvmulwev_q_d, LASX, gen_xvmul_q, 0, 0, tcg_gen_muls2_i64)
TRANS(xvmulwod_q_d, LASX, gen_xvmul_q, 1, 1, tcg_gen_muls2_i64)
TRANS(xvmulwev_q_du, LASX, gen_xvmul_q, 0, 0, tcg_gen_mulu2_i64)
TRANS(xvmulwod_q_du, LASX, gen_xvmul_q, 1, 1, tcg_gen_mulu2_i64)
TRANS(xvmulwev_q_du_d, LASX, gen_xvmul_q, 0, 0, tcg_gen_mulus2_i64)
TRANS(xvmulwod_q_du_d, LASX, gen_xvmul_q, 1, 1, tcg_gen_mulus2_i64)

static void gen_vmulwod_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    tcg_gen_sari_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwod_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t1, a, 16);
    tcg_gen_sari_i32(t2, b, 16);
    tcg_gen_mul_i32(t, t1, t2);
}

static void gen_vmulwod_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_sari_i64(t1, a, 32);
    tcg_gen_sari_i64(t2, b, 32);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwod_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_vmulwod_h_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_h,
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_vmulwod_w_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_w,
            .fniv = gen_vmulwod_s,
            .fno = gen_helper_vmulwod_d_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwod_h_b, LSX, gvec_vvv, MO_8, do_vmulwod_s)
TRANS(vmulwod_w_h, LSX, gvec_vvv, MO_16, do_vmulwod_s)
TRANS(vmulwod_d_w, LSX, gvec_vvv, MO_32, do_vmulwod_s)
TRANS(xvmulwod_h_b, LASX, gvec_xxx, MO_8, do_vmulwod_s)
TRANS(xvmulwod_w_h, LASX, gvec_xxx, MO_16, do_vmulwod_s)
TRANS(xvmulwod_d_w, LASX, gvec_xxx, MO_32, do_vmulwod_s)

static void gen_vmulwev_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, mask;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    mask = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, mask);
    tcg_gen_and_vec(vece, t2, b, mask);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwev_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t1, a);
    tcg_gen_ext16u_i32(t2, b);
    tcg_gen_mul_i32(t, t1, t2);
}

static void gen_vmulwev_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(t1, a);
    tcg_gen_ext32u_i64(t2, b);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwev_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_vmulwev_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_hu,
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_vmulwev_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_wu,
            .fniv = gen_vmulwev_u,
            .fno = gen_helper_vmulwev_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwev_h_bu, LSX, gvec_vvv, MO_8, do_vmulwev_u)
TRANS(vmulwev_w_hu, LSX, gvec_vvv, MO_16, do_vmulwev_u)
TRANS(vmulwev_d_wu, LSX, gvec_vvv, MO_32, do_vmulwev_u)
TRANS(xvmulwev_h_bu, LASX, gvec_xxx, MO_8, do_vmulwev_u)
TRANS(xvmulwev_w_hu, LASX, gvec_xxx, MO_16, do_vmulwev_u)
TRANS(xvmulwev_d_wu, LASX, gvec_xxx, MO_32, do_vmulwev_u)

static void gen_vmulwod_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_shri_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwod_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t1, a, 16);
    tcg_gen_shri_i32(t2, b, 16);
    tcg_gen_mul_i32(t, t1, t2);
}

static void gen_vmulwod_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, a, 32);
    tcg_gen_shri_i64(t2, b, 32);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwod_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                         uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_vmulwod_h_bu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_hu,
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_vmulwod_w_hu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_wu,
            .fniv = gen_vmulwod_u,
            .fno = gen_helper_vmulwod_d_wu,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwod_h_bu, LSX, gvec_vvv, MO_8, do_vmulwod_u)
TRANS(vmulwod_w_hu, LSX, gvec_vvv, MO_16, do_vmulwod_u)
TRANS(vmulwod_d_wu, LSX, gvec_vvv, MO_32, do_vmulwod_u)
TRANS(xvmulwod_h_bu, LASX, gvec_xxx, MO_8, do_vmulwod_u)
TRANS(xvmulwod_w_hu, LASX, gvec_xxx, MO_16, do_vmulwod_u)
TRANS(xvmulwod_d_wu, LASX, gvec_xxx, MO_32, do_vmulwod_u)

static void gen_vmulwev_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, mask;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    mask = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, mask);
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwev_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t1, a);
    tcg_gen_ext16s_i32(t2, b);
    tcg_gen_mul_i32(t, t1, t2);
}

static void gen_vmulwev_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(t1, a);
    tcg_gen_ext32s_i64(t2, b);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwev_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                           uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_vmulwev_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwev_w_hu_h,
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_vmulwev_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwev_d_wu_w,
            .fniv = gen_vmulwev_u_s,
            .fno = gen_helper_vmulwev_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwev_h_bu_b, LSX, gvec_vvv, MO_8, do_vmulwev_u_s)
TRANS(vmulwev_w_hu_h, LSX, gvec_vvv, MO_16, do_vmulwev_u_s)
TRANS(vmulwev_d_wu_w, LSX, gvec_vvv, MO_32, do_vmulwev_u_s)
TRANS(xvmulwev_h_bu_b, LASX, gvec_xxx, MO_8, do_vmulwev_u_s)
TRANS(xvmulwev_w_hu_h, LASX, gvec_xxx, MO_16, do_vmulwev_u_s)
TRANS(xvmulwev_d_wu_w, LASX, gvec_xxx, MO_32, do_vmulwev_u_s)

static void gen_vmulwod_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t, t1, t2);
}

static void gen_vmulwod_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1, t2;

    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t1, a, 16);
    tcg_gen_sari_i32(t2, b, 16);
    tcg_gen_mul_i32(t, t1, t2);
}
static void gen_vmulwod_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1, t2;

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_shri_i64(t1, a, 32);
    tcg_gen_sari_i64(t2, b, 32);
    tcg_gen_mul_i64(t, t1, t2);
}

static void do_vmulwod_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                           uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec, INDEX_op_mul_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_vmulwod_h_bu_b,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmulwod_w_hu_h,
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_vmulwod_w_hu_h,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmulwod_d_wu_w,
            .fniv = gen_vmulwod_u_s,
            .fno = gen_helper_vmulwod_d_wu_w,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmulwod_h_bu_b, LSX, gvec_vvv, MO_8, do_vmulwod_u_s)
TRANS(vmulwod_w_hu_h, LSX, gvec_vvv, MO_16, do_vmulwod_u_s)
TRANS(vmulwod_d_wu_w, LSX, gvec_vvv, MO_32, do_vmulwod_u_s)
TRANS(xvmulwod_h_bu_b, LASX, gvec_xxx, MO_8, do_vmulwod_u_s)
TRANS(xvmulwod_w_hu_h, LASX, gvec_xxx, MO_16, do_vmulwod_u_s)
TRANS(xvmulwod_d_wu_w, LASX, gvec_xxx, MO_32, do_vmulwod_u_s)

static void gen_vmadd(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1;

    t1 = tcg_temp_new_vec_matching(t);
    tcg_gen_mul_vec(vece, t1, a, b);
    tcg_gen_add_vec(vece, t, t, t1);
}

static void gen_vmadd_w(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    tcg_gen_mul_i32(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmadd_d(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    tcg_gen_mul_i64(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmadd(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                     uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vmadd,
            .fno = gen_helper_vmadd_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmadd,
            .fno = gen_helper_vmadd_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmadd_w,
            .fniv = gen_vmadd,
            .fno = gen_helper_vmadd_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmadd_d,
            .fniv = gen_vmadd,
            .fno = gen_helper_vmadd_d,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmadd_b, LSX, gvec_vvv, MO_8, do_vmadd)
TRANS(vmadd_h, LSX, gvec_vvv, MO_16, do_vmadd)
TRANS(vmadd_w, LSX, gvec_vvv, MO_32, do_vmadd)
TRANS(vmadd_d, LSX, gvec_vvv, MO_64, do_vmadd)
TRANS(xvmadd_b, LASX, gvec_xxx, MO_8, do_vmadd)
TRANS(xvmadd_h, LASX, gvec_xxx, MO_16, do_vmadd)
TRANS(xvmadd_w, LASX, gvec_xxx, MO_32, do_vmadd)
TRANS(xvmadd_d, LASX, gvec_xxx, MO_64, do_vmadd)

static void gen_vmsub(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1;

    t1 = tcg_temp_new_vec_matching(t);
    tcg_gen_mul_vec(vece, t1, a, b);
    tcg_gen_sub_vec(vece, t, t, t1);
}

static void gen_vmsub_w(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    tcg_gen_mul_i32(t1, a, b);
    tcg_gen_sub_i32(t, t, t1);
}

static void gen_vmsub_d(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    tcg_gen_mul_i64(t1, a, b);
    tcg_gen_sub_i64(t, t, t1);
}

static void do_vmsub(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                     uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_sub_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vmsub,
            .fno = gen_helper_vmsub_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vmsub,
            .fno = gen_helper_vmsub_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmsub_w,
            .fniv = gen_vmsub,
            .fno = gen_helper_vmsub_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmsub_d,
            .fniv = gen_vmsub,
            .fno = gen_helper_vmsub_d,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmsub_b, LSX, gvec_vvv, MO_8, do_vmsub)
TRANS(vmsub_h, LSX, gvec_vvv, MO_16, do_vmsub)
TRANS(vmsub_w, LSX, gvec_vvv, MO_32, do_vmsub)
TRANS(vmsub_d, LSX, gvec_vvv, MO_64, do_vmsub)
TRANS(xvmsub_b, LASX, gvec_xxx, MO_8, do_vmsub)
TRANS(xvmsub_h, LASX, gvec_xxx, MO_16, do_vmsub)
TRANS(xvmsub_w, LASX, gvec_xxx, MO_32, do_vmsub)
TRANS(xvmsub_d, LASX, gvec_xxx, MO_64, do_vmsub)

static void gen_vmaddwev_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_temp_new_vec_matching(t);
    tcg_gen_shli_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t1, t1, halfbits);
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);
    tcg_gen_mul_vec(vece, t3, t1, t2);
    tcg_gen_add_vec(vece, t, t, t3);
}

static void gen_vmaddwev_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwev_w_h(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwev_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwev_d_w(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwev_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                          uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_vmaddwev_h_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_h,
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_vmaddwev_w_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_w,
            .fniv = gen_vmaddwev_s,
            .fno = gen_helper_vmaddwev_d_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwev_h_b, LSX, gvec_vvv, MO_8, do_vmaddwev_s)
TRANS(vmaddwev_w_h, LSX, gvec_vvv, MO_16, do_vmaddwev_s)
TRANS(vmaddwev_d_w, LSX, gvec_vvv, MO_32, do_vmaddwev_s)
TRANS(xvmaddwev_h_b, LASX, gvec_xxx, MO_8, do_vmaddwev_s)
TRANS(xvmaddwev_w_h, LASX, gvec_xxx, MO_16, do_vmaddwev_s)
TRANS(xvmaddwev_d_w, LASX, gvec_xxx, MO_32, do_vmaddwev_s)

static bool gen_vmadd_q_vl(DisasContext * ctx,
                           arg_vvv *a, uint32_t oprsz, int idx1, int idx2,
                           void (*func)(TCGv_i64, TCGv_i64,
                                        TCGv_i64, TCGv_i64))
{
    TCGv_i64 rh, rl, arg1, arg2, th, tl;
    int i;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    rh = tcg_temp_new_i64();
    rl = tcg_temp_new_i64();
    arg1 = tcg_temp_new_i64();
    arg2 = tcg_temp_new_i64();
    th = tcg_temp_new_i64();
    tl = tcg_temp_new_i64();

    for (i = 0; i < oprsz / 16; i++) {
        get_vreg64(arg1, a->vj, 2 * i + idx1);
        get_vreg64(arg2, a->vk, 2 * i + idx2);
        get_vreg64(rh, a->vd, 2 * i + 1);
        get_vreg64(rl, a->vd, 2 * i);

        func(tl, th, arg1, arg2);
        tcg_gen_add2_i64(rl, rh, rl, rh, tl, th);

        set_vreg64(rh, a->vd, 2 * i + 1);
        set_vreg64(rl, a->vd, 2 * i);
    }

    return true;
}

static bool gen_vmadd_q(DisasContext *ctx, arg_vvv *a, int idx1, int idx2,
                        void (*func)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    return gen_vmadd_q_vl(ctx, a, 16, idx1, idx2, func);
}

static bool gen_xvmadd_q(DisasContext *ctx, arg_vvv *a, int idx1, int idx2,
                         void (*func)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    return gen_vmadd_q_vl(ctx, a, 32, idx1, idx2, func);
}

TRANS(vmaddwev_q_d, LSX, gen_vmadd_q, 0, 0, tcg_gen_muls2_i64)
TRANS(vmaddwod_q_d, LSX, gen_vmadd_q, 1, 1, tcg_gen_muls2_i64)
TRANS(vmaddwev_q_du, LSX, gen_vmadd_q, 0, 0, tcg_gen_mulu2_i64)
TRANS(vmaddwod_q_du, LSX, gen_vmadd_q, 1, 1, tcg_gen_mulu2_i64)
TRANS(vmaddwev_q_du_d, LSX, gen_vmadd_q, 0, 0, tcg_gen_mulus2_i64)
TRANS(vmaddwod_q_du_d, LSX, gen_vmadd_q, 1, 1, tcg_gen_mulus2_i64)
TRANS(xvmaddwev_q_d, LASX, gen_xvmadd_q, 0, 0, tcg_gen_muls2_i64)
TRANS(xvmaddwod_q_d, LASX, gen_xvmadd_q, 1, 1, tcg_gen_muls2_i64)
TRANS(xvmaddwev_q_du, LASX, gen_xvmadd_q, 0, 0, tcg_gen_mulu2_i64)
TRANS(xvmaddwod_q_du, LASX, gen_xvmadd_q, 1, 1, tcg_gen_mulu2_i64)
TRANS(xvmaddwev_q_du_d, LASX, gen_xvmadd_q, 0, 0, tcg_gen_mulus2_i64)
TRANS(xvmaddwod_q_du_d, LASX, gen_xvmadd_q, 1, 1, tcg_gen_mulus2_i64)

static void gen_vmaddwod_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_temp_new_vec_matching(t);
    tcg_gen_sari_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t3, t1, t2);
    tcg_gen_add_vec(vece, t, t, t3);
}

static void gen_vmaddwod_w_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwod_w_h(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwod_d_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwod_d_w(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwod_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                          uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_vmaddwod_h_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_h,
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_vmaddwod_w_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_w,
            .fniv = gen_vmaddwod_s,
            .fno = gen_helper_vmaddwod_d_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwod_h_b, LSX, gvec_vvv, MO_8, do_vmaddwod_s)
TRANS(vmaddwod_w_h, LSX, gvec_vvv, MO_16, do_vmaddwod_s)
TRANS(vmaddwod_d_w, LSX, gvec_vvv, MO_32, do_vmaddwod_s)
TRANS(xvmaddwod_h_b, LASX, gvec_xxx, MO_8, do_vmaddwod_s)
TRANS(xvmaddwod_w_h, LASX, gvec_xxx, MO_16, do_vmaddwod_s)
TRANS(xvmaddwod_d_w, LASX, gvec_xxx, MO_32, do_vmaddwod_s)

static void gen_vmaddwev_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, mask;

    t1 = tcg_temp_new_vec_matching(t);
    t2 = tcg_temp_new_vec_matching(b);
    mask = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, mask);
    tcg_gen_and_vec(vece, t2, b, mask);
    tcg_gen_mul_vec(vece, t1, t1, t2);
    tcg_gen_add_vec(vece, t, t, t1);
}

static void gen_vmaddwev_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwev_w_hu(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwev_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwev_d_wu(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwev_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                          uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_vmaddwev_h_bu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_hu,
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_vmaddwev_w_hu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_wu,
            .fniv = gen_vmaddwev_u,
            .fno = gen_helper_vmaddwev_d_wu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwev_h_bu, LSX, gvec_vvv, MO_8, do_vmaddwev_u)
TRANS(vmaddwev_w_hu, LSX, gvec_vvv, MO_16, do_vmaddwev_u)
TRANS(vmaddwev_d_wu, LSX, gvec_vvv, MO_32, do_vmaddwev_u)
TRANS(xvmaddwev_h_bu, LASX, gvec_xxx, MO_8, do_vmaddwev_u)
TRANS(xvmaddwev_w_hu, LASX, gvec_xxx, MO_16, do_vmaddwev_u)
TRANS(xvmaddwev_d_wu, LASX, gvec_xxx, MO_32, do_vmaddwev_u)

static void gen_vmaddwod_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_temp_new_vec_matching(t);
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_shri_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t3, t1, t2);
    tcg_gen_add_vec(vece, t, t, t3);
}

static void gen_vmaddwod_w_hu(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwod_w_hu(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwod_d_wu(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwod_d_wu(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwod_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                          uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_vmaddwod_h_bu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_hu,
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_vmaddwod_w_hu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_wu,
            .fniv = gen_vmaddwod_u,
            .fno = gen_helper_vmaddwod_d_wu,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwod_h_bu, LSX, gvec_vvv, MO_8, do_vmaddwod_u)
TRANS(vmaddwod_w_hu, LSX, gvec_vvv, MO_16, do_vmaddwod_u)
TRANS(vmaddwod_d_wu, LSX, gvec_vvv, MO_32, do_vmaddwod_u)
TRANS(xvmaddwod_h_bu, LASX, gvec_xxx, MO_8, do_vmaddwod_u)
TRANS(xvmaddwod_w_hu, LASX, gvec_xxx, MO_16, do_vmaddwod_u)
TRANS(xvmaddwod_d_wu, LASX, gvec_xxx, MO_32, do_vmaddwod_u)

static void gen_vmaddwev_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, mask;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    mask = tcg_constant_vec_matching(t, vece, MAKE_64BIT_MASK(0, 4 << vece));
    tcg_gen_and_vec(vece, t1, a, mask);
    tcg_gen_shli_vec(vece, t2, b, halfbits);
    tcg_gen_sari_vec(vece, t2, t2, halfbits);
    tcg_gen_mul_vec(vece, t1, t1, t2);
    tcg_gen_add_vec(vece, t, t, t1);
}

static void gen_vmaddwev_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwev_w_hu_h(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwev_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwev_d_wu_w(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwev_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                            uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_vmaddwev_h_bu_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwev_w_hu_h,
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_vmaddwev_w_hu_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwev_d_wu_w,
            .fniv = gen_vmaddwev_u_s,
            .fno = gen_helper_vmaddwev_d_wu_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwev_h_bu_b, LSX, gvec_vvv, MO_8, do_vmaddwev_u_s)
TRANS(vmaddwev_w_hu_h, LSX, gvec_vvv, MO_16, do_vmaddwev_u_s)
TRANS(vmaddwev_d_wu_w, LSX, gvec_vvv, MO_32, do_vmaddwev_u_s)
TRANS(xvmaddwev_h_bu_b, LASX, gvec_xxx, MO_8, do_vmaddwev_u_s)
TRANS(xvmaddwev_w_hu_h, LASX, gvec_xxx, MO_16, do_vmaddwev_u_s)
TRANS(xvmaddwev_d_wu_w, LASX, gvec_xxx, MO_32, do_vmaddwev_u_s)

static void gen_vmaddwod_u_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, t2, t3;
    int halfbits = 4 << vece;

    t1 = tcg_temp_new_vec_matching(a);
    t2 = tcg_temp_new_vec_matching(b);
    t3 = tcg_temp_new_vec_matching(t);
    tcg_gen_shri_vec(vece, t1, a, halfbits);
    tcg_gen_sari_vec(vece, t2, b, halfbits);
    tcg_gen_mul_vec(vece, t3, t1, t2);
    tcg_gen_add_vec(vece, t, t, t3);
}

static void gen_vmaddwod_w_hu_h(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i32 t1;

    t1 = tcg_temp_new_i32();
    gen_vmulwod_w_hu_h(t1, a, b);
    tcg_gen_add_i32(t, t, t1);
}

static void gen_vmaddwod_d_wu_w(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    TCGv_i64 t1;

    t1 = tcg_temp_new_i64();
    gen_vmulwod_d_wu_w(t1, a, b);
    tcg_gen_add_i64(t, t, t1);
}

static void do_vmaddwod_u_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                            uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, INDEX_op_sari_vec,
        INDEX_op_mul_vec, INDEX_op_add_vec, 0
        };
    static const GVecGen3 op[3] = {
        {
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_vmaddwod_h_bu_b,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fni4 = gen_vmaddwod_w_hu_h,
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_vmaddwod_w_hu_h,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vmaddwod_d_wu_w,
            .fniv = gen_vmaddwod_u_s,
            .fno = gen_helper_vmaddwod_d_wu_w,
            .load_dest = true,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vmaddwod_h_bu_b, LSX, gvec_vvv, MO_8, do_vmaddwod_u_s)
TRANS(vmaddwod_w_hu_h, LSX, gvec_vvv, MO_16, do_vmaddwod_u_s)
TRANS(vmaddwod_d_wu_w, LSX, gvec_vvv, MO_32, do_vmaddwod_u_s)
TRANS(xvmaddwod_h_bu_b, LASX, gvec_xxx, MO_8, do_vmaddwod_u_s)
TRANS(xvmaddwod_w_hu_h, LASX, gvec_xxx, MO_16, do_vmaddwod_u_s)
TRANS(xvmaddwod_d_wu_w, LASX, gvec_xxx, MO_32, do_vmaddwod_u_s)

TRANS(vdiv_b, LSX, gen_vvv, gen_helper_vdiv_b)
TRANS(vdiv_h, LSX, gen_vvv, gen_helper_vdiv_h)
TRANS(vdiv_w, LSX, gen_vvv, gen_helper_vdiv_w)
TRANS(vdiv_d, LSX, gen_vvv, gen_helper_vdiv_d)
TRANS(vdiv_bu, LSX, gen_vvv, gen_helper_vdiv_bu)
TRANS(vdiv_hu, LSX, gen_vvv, gen_helper_vdiv_hu)
TRANS(vdiv_wu, LSX, gen_vvv, gen_helper_vdiv_wu)
TRANS(vdiv_du, LSX, gen_vvv, gen_helper_vdiv_du)
TRANS(vmod_b, LSX, gen_vvv, gen_helper_vmod_b)
TRANS(vmod_h, LSX, gen_vvv, gen_helper_vmod_h)
TRANS(vmod_w, LSX, gen_vvv, gen_helper_vmod_w)
TRANS(vmod_d, LSX, gen_vvv, gen_helper_vmod_d)
TRANS(vmod_bu, LSX, gen_vvv, gen_helper_vmod_bu)
TRANS(vmod_hu, LSX, gen_vvv, gen_helper_vmod_hu)
TRANS(vmod_wu, LSX, gen_vvv, gen_helper_vmod_wu)
TRANS(vmod_du, LSX, gen_vvv, gen_helper_vmod_du)
TRANS(xvdiv_b, LASX, gen_xxx, gen_helper_vdiv_b)
TRANS(xvdiv_h, LASX, gen_xxx, gen_helper_vdiv_h)
TRANS(xvdiv_w, LASX, gen_xxx, gen_helper_vdiv_w)
TRANS(xvdiv_d, LASX, gen_xxx, gen_helper_vdiv_d)
TRANS(xvdiv_bu, LASX, gen_xxx, gen_helper_vdiv_bu)
TRANS(xvdiv_hu, LASX, gen_xxx, gen_helper_vdiv_hu)
TRANS(xvdiv_wu, LASX, gen_xxx, gen_helper_vdiv_wu)
TRANS(xvdiv_du, LASX, gen_xxx, gen_helper_vdiv_du)
TRANS(xvmod_b, LASX, gen_xxx, gen_helper_vmod_b)
TRANS(xvmod_h, LASX, gen_xxx, gen_helper_vmod_h)
TRANS(xvmod_w, LASX, gen_xxx, gen_helper_vmod_w)
TRANS(xvmod_d, LASX, gen_xxx, gen_helper_vmod_d)
TRANS(xvmod_bu, LASX, gen_xxx, gen_helper_vmod_bu)
TRANS(xvmod_hu, LASX, gen_xxx, gen_helper_vmod_hu)
TRANS(xvmod_wu, LASX, gen_xxx, gen_helper_vmod_wu)
TRANS(xvmod_du, LASX, gen_xxx, gen_helper_vmod_du)

static void gen_vsat_s(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec max)
{
    TCGv_vec min;

    min = tcg_temp_new_vec_matching(t);
    tcg_gen_not_vec(vece, min, max);
    tcg_gen_smax_vec(vece, t, a, min);
    tcg_gen_smin_vec(vece, t, t, max);
}

static void do_vsat_s(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_smax_vec, INDEX_op_smin_vec, 0
        };
    static const GVecGen2s op[4] = {
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_vsat_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_vsat_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_vsat_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsat_s,
            .fno = gen_helper_vsat_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2s(vd_ofs, vj_ofs, oprsz, maxsz,
                    tcg_constant_i64((1ll<< imm) -1), &op[vece]);
}

TRANS(vsat_b, LSX, gvec_vv_i, MO_8, do_vsat_s)
TRANS(vsat_h, LSX, gvec_vv_i, MO_16, do_vsat_s)
TRANS(vsat_w, LSX, gvec_vv_i, MO_32, do_vsat_s)
TRANS(vsat_d, LSX, gvec_vv_i, MO_64, do_vsat_s)
TRANS(xvsat_b, LASX, gvec_xx_i, MO_8, do_vsat_s)
TRANS(xvsat_h, LASX, gvec_xx_i, MO_16, do_vsat_s)
TRANS(xvsat_w, LASX, gvec_xx_i, MO_32, do_vsat_s)
TRANS(xvsat_d, LASX, gvec_xx_i, MO_64, do_vsat_s)

static void gen_vsat_u(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec max)
{
    tcg_gen_umin_vec(vece, t, a, max);
}

static void do_vsat_u(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    uint64_t max;
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umin_vec, 0
        };
    static const GVecGen2s op[4] = {
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_vsat_bu,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_vsat_hu,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_vsat_wu,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsat_u,
            .fno = gen_helper_vsat_du,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    max = (imm == 0x3f) ? UINT64_MAX : (1ull << (imm + 1)) - 1;
    tcg_gen_gvec_2s(vd_ofs, vj_ofs, oprsz, maxsz,
                    tcg_constant_i64(max), &op[vece]);
}

TRANS(vsat_bu, LSX, gvec_vv_i, MO_8, do_vsat_u)
TRANS(vsat_hu, LSX, gvec_vv_i, MO_16, do_vsat_u)
TRANS(vsat_wu, LSX, gvec_vv_i, MO_32, do_vsat_u)
TRANS(vsat_du, LSX, gvec_vv_i, MO_64, do_vsat_u)
TRANS(xvsat_bu, LASX, gvec_xx_i, MO_8, do_vsat_u)
TRANS(xvsat_hu, LASX, gvec_xx_i, MO_16, do_vsat_u)
TRANS(xvsat_wu, LASX, gvec_xx_i, MO_32, do_vsat_u)
TRANS(xvsat_du, LASX, gvec_xx_i, MO_64, do_vsat_u)

TRANS(vexth_h_b, LSX, gen_vv, gen_helper_vexth_h_b)
TRANS(vexth_w_h, LSX, gen_vv, gen_helper_vexth_w_h)
TRANS(vexth_d_w, LSX, gen_vv, gen_helper_vexth_d_w)
TRANS(vexth_q_d, LSX, gen_vv, gen_helper_vexth_q_d)
TRANS(vexth_hu_bu, LSX, gen_vv, gen_helper_vexth_hu_bu)
TRANS(vexth_wu_hu, LSX, gen_vv, gen_helper_vexth_wu_hu)
TRANS(vexth_du_wu, LSX, gen_vv, gen_helper_vexth_du_wu)
TRANS(vexth_qu_du, LSX, gen_vv, gen_helper_vexth_qu_du)
TRANS(xvexth_h_b, LASX, gen_xx, gen_helper_vexth_h_b)
TRANS(xvexth_w_h, LASX, gen_xx, gen_helper_vexth_w_h)
TRANS(xvexth_d_w, LASX, gen_xx, gen_helper_vexth_d_w)
TRANS(xvexth_q_d, LASX, gen_xx, gen_helper_vexth_q_d)
TRANS(xvexth_hu_bu, LASX, gen_xx, gen_helper_vexth_hu_bu)
TRANS(xvexth_wu_hu, LASX, gen_xx, gen_helper_vexth_wu_hu)
TRANS(xvexth_du_wu, LASX, gen_xx, gen_helper_vexth_du_wu)
TRANS(xvexth_qu_du, LASX, gen_xx, gen_helper_vexth_qu_du)

TRANS(vext2xv_h_b, LASX, gen_xx, gen_helper_vext2xv_h_b)
TRANS(vext2xv_w_b, LASX, gen_xx, gen_helper_vext2xv_w_b)
TRANS(vext2xv_d_b, LASX, gen_xx, gen_helper_vext2xv_d_b)
TRANS(vext2xv_w_h, LASX, gen_xx, gen_helper_vext2xv_w_h)
TRANS(vext2xv_d_h, LASX, gen_xx, gen_helper_vext2xv_d_h)
TRANS(vext2xv_d_w, LASX, gen_xx, gen_helper_vext2xv_d_w)
TRANS(vext2xv_hu_bu, LASX, gen_xx, gen_helper_vext2xv_hu_bu)
TRANS(vext2xv_wu_bu, LASX, gen_xx, gen_helper_vext2xv_wu_bu)
TRANS(vext2xv_du_bu, LASX, gen_xx, gen_helper_vext2xv_du_bu)
TRANS(vext2xv_wu_hu, LASX, gen_xx, gen_helper_vext2xv_wu_hu)
TRANS(vext2xv_du_hu, LASX, gen_xx, gen_helper_vext2xv_du_hu)
TRANS(vext2xv_du_wu, LASX, gen_xx, gen_helper_vext2xv_du_wu)

static void gen_vsigncov(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t1, zero;

    t1 = tcg_temp_new_vec_matching(t);
    zero = tcg_constant_vec_matching(t, vece, 0);

    tcg_gen_neg_vec(vece, t1, b);
    tcg_gen_cmpsel_vec(TCG_COND_LT, vece, t, a, zero, t1, b);
    tcg_gen_cmpsel_vec(TCG_COND_EQ, vece, t, a, zero, zero, t);
}

static void do_vsigncov(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                        uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_neg_vec, INDEX_op_cmpsel_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_vsigncov_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_vsigncov_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_vsigncov_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vsigncov,
            .fno = gen_helper_vsigncov_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vsigncov_b, LSX, gvec_vvv, MO_8, do_vsigncov)
TRANS(vsigncov_h, LSX, gvec_vvv, MO_16, do_vsigncov)
TRANS(vsigncov_w, LSX, gvec_vvv, MO_32, do_vsigncov)
TRANS(vsigncov_d, LSX, gvec_vvv, MO_64, do_vsigncov)
TRANS(xvsigncov_b, LASX, gvec_xxx, MO_8, do_vsigncov)
TRANS(xvsigncov_h, LASX, gvec_xxx, MO_16, do_vsigncov)
TRANS(xvsigncov_w, LASX, gvec_xxx, MO_32, do_vsigncov)
TRANS(xvsigncov_d, LASX, gvec_xxx, MO_64, do_vsigncov)

TRANS(vmskltz_b, LSX, gen_vv, gen_helper_vmskltz_b)
TRANS(vmskltz_h, LSX, gen_vv, gen_helper_vmskltz_h)
TRANS(vmskltz_w, LSX, gen_vv, gen_helper_vmskltz_w)
TRANS(vmskltz_d, LSX, gen_vv, gen_helper_vmskltz_d)
TRANS(vmskgez_b, LSX, gen_vv, gen_helper_vmskgez_b)
TRANS(vmsknz_b, LSX, gen_vv, gen_helper_vmsknz_b)
TRANS(xvmskltz_b, LASX, gen_xx, gen_helper_vmskltz_b)
TRANS(xvmskltz_h, LASX, gen_xx, gen_helper_vmskltz_h)
TRANS(xvmskltz_w, LASX, gen_xx, gen_helper_vmskltz_w)
TRANS(xvmskltz_d, LASX, gen_xx, gen_helper_vmskltz_d)
TRANS(xvmskgez_b, LASX, gen_xx, gen_helper_vmskgez_b)
TRANS(xvmsknz_b, LASX, gen_xx, gen_helper_vmsknz_b)

#define EXPAND_BYTE(bit)  ((uint64_t)(bit ? 0xff : 0))

static uint64_t vldi_get_value(DisasContext *ctx, uint32_t imm)
{
    int mode;
    uint64_t data = 0, t;

    /*
     * imm bit [11:8] is mode, mode value is 0-12.
     * other values are invalid.
     */
    mode = (imm >> 8) & 0xf;
    t =  imm & 0xff;
    switch (mode) {
    case 0:
        /* data: {2{24'0, imm[7:0]}} */
        data =  (t << 32) | t ;
        break;
    case 1:
        /* data: {2{16'0, imm[7:0], 8'0}} */
        data = (t << 40) | (t << 8);
        break;
    case 2:
        /* data: {2{8'0, imm[7:0], 16'0}} */
        data = (t << 48) | (t << 16);
        break;
    case 3:
        /* data: {2{imm[7:0], 24'0}} */
        data = (t << 56) | (t << 24);
        break;
    case 4:
        /* data: {4{8'0, imm[7:0]}} */
        data = (t << 48) | (t << 32) | (t << 16) | t;
        break;
    case 5:
        /* data: {4{imm[7:0], 8'0}} */
        data = (t << 56) |(t << 40) | (t << 24) | (t << 8);
        break;
    case 6:
        /* data: {2{16'0, imm[7:0], 8'1}} */
        data = (t << 40) | ((uint64_t)0xff << 32) | (t << 8) | 0xff;
        break;
    case 7:
        /* data: {2{8'0, imm[7:0], 16'1}} */
        data = (t << 48) | ((uint64_t)0xffff << 32) | (t << 16) | 0xffff;
        break;
    case 8:
        /* data: {8{imm[7:0]}} */
        data =(t << 56) | (t << 48) | (t << 40) | (t << 32) |
              (t << 24) | (t << 16) | (t << 8) | t;
        break;
    case 9:
        /* data: {{8{imm[7]}, ..., 8{imm[0]}}} */
        {
            uint64_t b0,b1,b2,b3,b4,b5,b6,b7;
            b0 = t& 0x1;
            b1 = (t & 0x2) >> 1;
            b2 = (t & 0x4) >> 2;
            b3 = (t & 0x8) >> 3;
            b4 = (t & 0x10) >> 4;
            b5 = (t & 0x20) >> 5;
            b6 = (t & 0x40) >> 6;
            b7 = (t & 0x80) >> 7;
            data = (EXPAND_BYTE(b7) << 56) |
                   (EXPAND_BYTE(b6) << 48) |
                   (EXPAND_BYTE(b5) << 40) |
                   (EXPAND_BYTE(b4) << 32) |
                   (EXPAND_BYTE(b3) << 24) |
                   (EXPAND_BYTE(b2) << 16) |
                   (EXPAND_BYTE(b1) <<  8) |
                   EXPAND_BYTE(b0);
        }
        break;
    case 10:
        /* data: {2{imm[7], ~imm[6], {5{imm[6]}}, imm[5:0], 19'0}} */
        {
            uint64_t b6, b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 6) | ((1-b6) << 5) | (uint64_t)(b6 ? 0x1f : 0);
            data  = (t1 << 57) | (t0 << 51) | (t1 << 25) | (t0 << 19);
        }
        break;
    case 11:
        /* data: {32'0, imm[7], ~{imm[6]}, 5{imm[6]}, imm[5:0], 19'0} */
        {
            uint64_t b6,b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 6) | ((1-b6) << 5) | (b6 ? 0x1f : 0);
            data = (t1 << 25) | (t0 << 19);
        }
        break;
    case 12:
        /* data: {imm[7], ~imm[6], 8{imm[6]}, imm[5:0], 48'0} */
        {
            uint64_t b6,b7;
            uint64_t t0, t1;
            b6 = (imm & 0x40) >> 6;
            b7 = (imm & 0x80) >> 7;
            t0 = (imm & 0x3f);
            t1 = (b7 << 9) | ((1-b6) << 8) | (b6 ? 0xff : 0);
            data = (t1 << 54) | (t0 << 48);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return data;
}

static bool check_valid_vldi_mode(arg_vldi *a)
{
   return  extract32(a->imm, 8, 4) <= 12;
}

static bool gen_vldi(DisasContext *ctx, arg_vldi *a, uint32_t oprsz)
{
    int sel, vece;
    uint64_t value;

    sel = (a->imm >> 12) & 0x1;

    if (sel && !check_valid_vldi_mode(a)) {
        generate_exception(ctx, EXCCODE_INE);
        return true;
    }

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    if (sel) {
        value = vldi_get_value(ctx, a->imm);
        vece = MO_64;
    } else {
        value = ((int32_t)(a->imm << 22)) >> 22;
        vece = (a->imm >> 10) & 0x3;
    }

    tcg_gen_gvec_dup_i64(vece, vec_full_offset(a->vd), oprsz, ctx->vl/8,
                         tcg_constant_i64(value));
    return true;
}

TRANS(vldi, LSX, gen_vldi, 16)
TRANS(xvldi, LASX, gen_vldi, 32)

static bool gen_vandn_v(DisasContext *ctx, arg_vvv *a, uint32_t oprsz)
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);
    vk_ofs = vec_full_offset(a->vk);

    tcg_gen_gvec_andc(MO_64, vd_ofs, vk_ofs, vj_ofs, oprsz, ctx->vl / 8);
    return true;
}

static void gen_vnori(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    TCGv_vec t1;

    t1 = tcg_constant_vec_matching(t, vece, imm);
    tcg_gen_nor_vec(vece, t, a, t1);
}

static void gen_vnori_b(TCGv_i64 t, TCGv_i64 a, int64_t imm)
{
    tcg_gen_movi_i64(t, dup_const(MO_8, imm));
    tcg_gen_nor_i64(t, a, t);
}

static void do_vnori_b(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_nor_vec, 0
        };
    static const GVecGen2i op = {
       .fni8 = gen_vnori_b,
       .fniv = gen_vnori,
       .fnoi = gen_helper_vnori_b,
       .opt_opc = vecop_list,
       .vece = MO_8
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op);
}

TRANS(vand_v, LSX, gvec_vvv, MO_64, tcg_gen_gvec_and)
TRANS(vor_v, LSX, gvec_vvv, MO_64, tcg_gen_gvec_or)
TRANS(vxor_v, LSX, gvec_vvv, MO_64, tcg_gen_gvec_xor)
TRANS(vnor_v, LSX, gvec_vvv, MO_64, tcg_gen_gvec_nor)
TRANS(vandn_v, LSX, gen_vandn_v, 16)
TRANS(vorn_v, LSX, gvec_vvv, MO_64, tcg_gen_gvec_orc)
TRANS(vandi_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_andi)
TRANS(vori_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_ori)
TRANS(vxori_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_xori)
TRANS(vnori_b, LSX, gvec_vv_i, MO_8, do_vnori_b)
TRANS(xvand_v, LASX, gvec_xxx, MO_64, tcg_gen_gvec_and)
TRANS(xvor_v, LASX, gvec_xxx, MO_64, tcg_gen_gvec_or)
TRANS(xvxor_v, LASX, gvec_xxx, MO_64, tcg_gen_gvec_xor)
TRANS(xvnor_v, LASX, gvec_xxx, MO_64, tcg_gen_gvec_nor)
TRANS(xvandn_v, LASX, gen_vandn_v, 32)
TRANS(xvorn_v, LASX, gvec_xxx, MO_64, tcg_gen_gvec_orc)
TRANS(xvandi_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_andi)
TRANS(xvori_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_ori)
TRANS(xvxori_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_xori)
TRANS(xvnori_b, LASX, gvec_xx_i, MO_8, do_vnori_b)

TRANS(vsll_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_shlv)
TRANS(vsll_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_shlv)
TRANS(vsll_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_shlv)
TRANS(vsll_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_shlv)
TRANS(vslli_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_shli)
TRANS(vslli_h, LSX, gvec_vv_i, MO_16, tcg_gen_gvec_shli)
TRANS(vslli_w, LSX, gvec_vv_i, MO_32, tcg_gen_gvec_shli)
TRANS(vslli_d, LSX, gvec_vv_i, MO_64, tcg_gen_gvec_shli)
TRANS(xvsll_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_shlv)
TRANS(xvsll_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_shlv)
TRANS(xvsll_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_shlv)
TRANS(xvsll_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_shlv)
TRANS(xvslli_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_shli)
TRANS(xvslli_h, LASX, gvec_xx_i, MO_16, tcg_gen_gvec_shli)
TRANS(xvslli_w, LASX, gvec_xx_i, MO_32, tcg_gen_gvec_shli)
TRANS(xvslli_d, LASX, gvec_xx_i, MO_64, tcg_gen_gvec_shli)

TRANS(vsrl_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_shrv)
TRANS(vsrl_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_shrv)
TRANS(vsrl_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_shrv)
TRANS(vsrl_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_shrv)
TRANS(vsrli_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_shri)
TRANS(vsrli_h, LSX, gvec_vv_i, MO_16, tcg_gen_gvec_shri)
TRANS(vsrli_w, LSX, gvec_vv_i, MO_32, tcg_gen_gvec_shri)
TRANS(vsrli_d, LSX, gvec_vv_i, MO_64, tcg_gen_gvec_shri)
TRANS(xvsrl_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_shrv)
TRANS(xvsrl_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_shrv)
TRANS(xvsrl_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_shrv)
TRANS(xvsrl_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_shrv)
TRANS(xvsrli_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_shri)
TRANS(xvsrli_h, LASX, gvec_xx_i, MO_16, tcg_gen_gvec_shri)
TRANS(xvsrli_w, LASX, gvec_xx_i, MO_32, tcg_gen_gvec_shri)
TRANS(xvsrli_d, LASX, gvec_xx_i, MO_64, tcg_gen_gvec_shri)

TRANS(vsra_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_sarv)
TRANS(vsra_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_sarv)
TRANS(vsra_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_sarv)
TRANS(vsra_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_sarv)
TRANS(vsrai_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_sari)
TRANS(vsrai_h, LSX, gvec_vv_i, MO_16, tcg_gen_gvec_sari)
TRANS(vsrai_w, LSX, gvec_vv_i, MO_32, tcg_gen_gvec_sari)
TRANS(vsrai_d, LSX, gvec_vv_i, MO_64, tcg_gen_gvec_sari)
TRANS(xvsra_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_sarv)
TRANS(xvsra_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_sarv)
TRANS(xvsra_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_sarv)
TRANS(xvsra_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_sarv)
TRANS(xvsrai_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_sari)
TRANS(xvsrai_h, LASX, gvec_xx_i, MO_16, tcg_gen_gvec_sari)
TRANS(xvsrai_w, LASX, gvec_xx_i, MO_32, tcg_gen_gvec_sari)
TRANS(xvsrai_d, LASX, gvec_xx_i, MO_64, tcg_gen_gvec_sari)

TRANS(vrotr_b, LSX, gvec_vvv, MO_8, tcg_gen_gvec_rotrv)
TRANS(vrotr_h, LSX, gvec_vvv, MO_16, tcg_gen_gvec_rotrv)
TRANS(vrotr_w, LSX, gvec_vvv, MO_32, tcg_gen_gvec_rotrv)
TRANS(vrotr_d, LSX, gvec_vvv, MO_64, tcg_gen_gvec_rotrv)
TRANS(vrotri_b, LSX, gvec_vv_i, MO_8, tcg_gen_gvec_rotri)
TRANS(vrotri_h, LSX, gvec_vv_i, MO_16, tcg_gen_gvec_rotri)
TRANS(vrotri_w, LSX, gvec_vv_i, MO_32, tcg_gen_gvec_rotri)
TRANS(vrotri_d, LSX, gvec_vv_i, MO_64, tcg_gen_gvec_rotri)
TRANS(xvrotr_b, LASX, gvec_xxx, MO_8, tcg_gen_gvec_rotrv)
TRANS(xvrotr_h, LASX, gvec_xxx, MO_16, tcg_gen_gvec_rotrv)
TRANS(xvrotr_w, LASX, gvec_xxx, MO_32, tcg_gen_gvec_rotrv)
TRANS(xvrotr_d, LASX, gvec_xxx, MO_64, tcg_gen_gvec_rotrv)
TRANS(xvrotri_b, LASX, gvec_xx_i, MO_8, tcg_gen_gvec_rotri)
TRANS(xvrotri_h, LASX, gvec_xx_i, MO_16, tcg_gen_gvec_rotri)
TRANS(xvrotri_w, LASX, gvec_xx_i, MO_32, tcg_gen_gvec_rotri)
TRANS(xvrotri_d, LASX, gvec_xx_i, MO_64, tcg_gen_gvec_rotri)

TRANS(vsllwil_h_b, LSX, gen_vv_i, gen_helper_vsllwil_h_b)
TRANS(vsllwil_w_h, LSX, gen_vv_i, gen_helper_vsllwil_w_h)
TRANS(vsllwil_d_w, LSX, gen_vv_i, gen_helper_vsllwil_d_w)
TRANS(vextl_q_d, LSX, gen_vv, gen_helper_vextl_q_d)
TRANS(vsllwil_hu_bu, LSX, gen_vv_i, gen_helper_vsllwil_hu_bu)
TRANS(vsllwil_wu_hu, LSX, gen_vv_i, gen_helper_vsllwil_wu_hu)
TRANS(vsllwil_du_wu, LSX, gen_vv_i, gen_helper_vsllwil_du_wu)
TRANS(vextl_qu_du, LSX, gen_vv, gen_helper_vextl_qu_du)
TRANS(xvsllwil_h_b, LASX, gen_xx_i, gen_helper_vsllwil_h_b)
TRANS(xvsllwil_w_h, LASX, gen_xx_i, gen_helper_vsllwil_w_h)
TRANS(xvsllwil_d_w, LASX, gen_xx_i, gen_helper_vsllwil_d_w)
TRANS(xvextl_q_d, LASX, gen_xx, gen_helper_vextl_q_d)
TRANS(xvsllwil_hu_bu, LASX, gen_xx_i, gen_helper_vsllwil_hu_bu)
TRANS(xvsllwil_wu_hu, LASX, gen_xx_i, gen_helper_vsllwil_wu_hu)
TRANS(xvsllwil_du_wu, LASX, gen_xx_i, gen_helper_vsllwil_du_wu)
TRANS(xvextl_qu_du, LASX, gen_xx, gen_helper_vextl_qu_du)

TRANS(vsrlr_b, LSX, gen_vvv, gen_helper_vsrlr_b)
TRANS(vsrlr_h, LSX, gen_vvv, gen_helper_vsrlr_h)
TRANS(vsrlr_w, LSX, gen_vvv, gen_helper_vsrlr_w)
TRANS(vsrlr_d, LSX, gen_vvv, gen_helper_vsrlr_d)
TRANS(vsrlri_b, LSX, gen_vv_i, gen_helper_vsrlri_b)
TRANS(vsrlri_h, LSX, gen_vv_i, gen_helper_vsrlri_h)
TRANS(vsrlri_w, LSX, gen_vv_i, gen_helper_vsrlri_w)
TRANS(vsrlri_d, LSX, gen_vv_i, gen_helper_vsrlri_d)
TRANS(xvsrlr_b, LASX, gen_xxx, gen_helper_vsrlr_b)
TRANS(xvsrlr_h, LASX, gen_xxx, gen_helper_vsrlr_h)
TRANS(xvsrlr_w, LASX, gen_xxx, gen_helper_vsrlr_w)
TRANS(xvsrlr_d, LASX, gen_xxx, gen_helper_vsrlr_d)
TRANS(xvsrlri_b, LASX, gen_xx_i, gen_helper_vsrlri_b)
TRANS(xvsrlri_h, LASX, gen_xx_i, gen_helper_vsrlri_h)
TRANS(xvsrlri_w, LASX, gen_xx_i, gen_helper_vsrlri_w)
TRANS(xvsrlri_d, LASX, gen_xx_i, gen_helper_vsrlri_d)

TRANS(vsrar_b, LSX, gen_vvv, gen_helper_vsrar_b)
TRANS(vsrar_h, LSX, gen_vvv, gen_helper_vsrar_h)
TRANS(vsrar_w, LSX, gen_vvv, gen_helper_vsrar_w)
TRANS(vsrar_d, LSX, gen_vvv, gen_helper_vsrar_d)
TRANS(vsrari_b, LSX, gen_vv_i, gen_helper_vsrari_b)
TRANS(vsrari_h, LSX, gen_vv_i, gen_helper_vsrari_h)
TRANS(vsrari_w, LSX, gen_vv_i, gen_helper_vsrari_w)
TRANS(vsrari_d, LSX, gen_vv_i, gen_helper_vsrari_d)
TRANS(xvsrar_b, LASX, gen_xxx, gen_helper_vsrar_b)
TRANS(xvsrar_h, LASX, gen_xxx, gen_helper_vsrar_h)
TRANS(xvsrar_w, LASX, gen_xxx, gen_helper_vsrar_w)
TRANS(xvsrar_d, LASX, gen_xxx, gen_helper_vsrar_d)
TRANS(xvsrari_b, LASX, gen_xx_i, gen_helper_vsrari_b)
TRANS(xvsrari_h, LASX, gen_xx_i, gen_helper_vsrari_h)
TRANS(xvsrari_w, LASX, gen_xx_i, gen_helper_vsrari_w)
TRANS(xvsrari_d, LASX, gen_xx_i, gen_helper_vsrari_d)

TRANS(vsrln_b_h, LSX, gen_vvv, gen_helper_vsrln_b_h)
TRANS(vsrln_h_w, LSX, gen_vvv, gen_helper_vsrln_h_w)
TRANS(vsrln_w_d, LSX, gen_vvv, gen_helper_vsrln_w_d)
TRANS(vsran_b_h, LSX, gen_vvv, gen_helper_vsran_b_h)
TRANS(vsran_h_w, LSX, gen_vvv, gen_helper_vsran_h_w)
TRANS(vsran_w_d, LSX, gen_vvv, gen_helper_vsran_w_d)
TRANS(xvsrln_b_h, LASX, gen_xxx, gen_helper_vsrln_b_h)
TRANS(xvsrln_h_w, LASX, gen_xxx, gen_helper_vsrln_h_w)
TRANS(xvsrln_w_d, LASX, gen_xxx, gen_helper_vsrln_w_d)
TRANS(xvsran_b_h, LASX, gen_xxx, gen_helper_vsran_b_h)
TRANS(xvsran_h_w, LASX, gen_xxx, gen_helper_vsran_h_w)
TRANS(xvsran_w_d, LASX, gen_xxx, gen_helper_vsran_w_d)

TRANS(vsrlni_b_h, LSX, gen_vv_i, gen_helper_vsrlni_b_h)
TRANS(vsrlni_h_w, LSX, gen_vv_i, gen_helper_vsrlni_h_w)
TRANS(vsrlni_w_d, LSX, gen_vv_i, gen_helper_vsrlni_w_d)
TRANS(vsrlni_d_q, LSX, gen_vv_i, gen_helper_vsrlni_d_q)
TRANS(vsrani_b_h, LSX, gen_vv_i, gen_helper_vsrani_b_h)
TRANS(vsrani_h_w, LSX, gen_vv_i, gen_helper_vsrani_h_w)
TRANS(vsrani_w_d, LSX, gen_vv_i, gen_helper_vsrani_w_d)
TRANS(vsrani_d_q, LSX, gen_vv_i, gen_helper_vsrani_d_q)
TRANS(xvsrlni_b_h, LASX, gen_xx_i, gen_helper_vsrlni_b_h)
TRANS(xvsrlni_h_w, LASX, gen_xx_i, gen_helper_vsrlni_h_w)
TRANS(xvsrlni_w_d, LASX, gen_xx_i, gen_helper_vsrlni_w_d)
TRANS(xvsrlni_d_q, LASX, gen_xx_i, gen_helper_vsrlni_d_q)
TRANS(xvsrani_b_h, LASX, gen_xx_i, gen_helper_vsrani_b_h)
TRANS(xvsrani_h_w, LASX, gen_xx_i, gen_helper_vsrani_h_w)
TRANS(xvsrani_w_d, LASX, gen_xx_i, gen_helper_vsrani_w_d)
TRANS(xvsrani_d_q, LASX, gen_xx_i, gen_helper_vsrani_d_q)

TRANS(vsrlrn_b_h, LSX, gen_vvv, gen_helper_vsrlrn_b_h)
TRANS(vsrlrn_h_w, LSX, gen_vvv, gen_helper_vsrlrn_h_w)
TRANS(vsrlrn_w_d, LSX, gen_vvv, gen_helper_vsrlrn_w_d)
TRANS(vsrarn_b_h, LSX, gen_vvv, gen_helper_vsrarn_b_h)
TRANS(vsrarn_h_w, LSX, gen_vvv, gen_helper_vsrarn_h_w)
TRANS(vsrarn_w_d, LSX, gen_vvv, gen_helper_vsrarn_w_d)
TRANS(xvsrlrn_b_h, LASX, gen_xxx, gen_helper_vsrlrn_b_h)
TRANS(xvsrlrn_h_w, LASX, gen_xxx, gen_helper_vsrlrn_h_w)
TRANS(xvsrlrn_w_d, LASX, gen_xxx, gen_helper_vsrlrn_w_d)
TRANS(xvsrarn_b_h, LASX, gen_xxx, gen_helper_vsrarn_b_h)
TRANS(xvsrarn_h_w, LASX, gen_xxx, gen_helper_vsrarn_h_w)
TRANS(xvsrarn_w_d, LASX, gen_xxx, gen_helper_vsrarn_w_d)

TRANS(vsrlrni_b_h, LSX, gen_vv_i, gen_helper_vsrlrni_b_h)
TRANS(vsrlrni_h_w, LSX, gen_vv_i, gen_helper_vsrlrni_h_w)
TRANS(vsrlrni_w_d, LSX, gen_vv_i, gen_helper_vsrlrni_w_d)
TRANS(vsrlrni_d_q, LSX, gen_vv_i, gen_helper_vsrlrni_d_q)
TRANS(vsrarni_b_h, LSX, gen_vv_i, gen_helper_vsrarni_b_h)
TRANS(vsrarni_h_w, LSX, gen_vv_i, gen_helper_vsrarni_h_w)
TRANS(vsrarni_w_d, LSX, gen_vv_i, gen_helper_vsrarni_w_d)
TRANS(vsrarni_d_q, LSX, gen_vv_i, gen_helper_vsrarni_d_q)
TRANS(xvsrlrni_b_h, LASX, gen_xx_i, gen_helper_vsrlrni_b_h)
TRANS(xvsrlrni_h_w, LASX, gen_xx_i, gen_helper_vsrlrni_h_w)
TRANS(xvsrlrni_w_d, LASX, gen_xx_i, gen_helper_vsrlrni_w_d)
TRANS(xvsrlrni_d_q, LASX, gen_xx_i, gen_helper_vsrlrni_d_q)
TRANS(xvsrarni_b_h, LASX, gen_xx_i, gen_helper_vsrarni_b_h)
TRANS(xvsrarni_h_w, LASX, gen_xx_i, gen_helper_vsrarni_h_w)
TRANS(xvsrarni_w_d, LASX, gen_xx_i, gen_helper_vsrarni_w_d)
TRANS(xvsrarni_d_q, LASX, gen_xx_i, gen_helper_vsrarni_d_q)

TRANS(vssrln_b_h, LSX, gen_vvv, gen_helper_vssrln_b_h)
TRANS(vssrln_h_w, LSX, gen_vvv, gen_helper_vssrln_h_w)
TRANS(vssrln_w_d, LSX, gen_vvv, gen_helper_vssrln_w_d)
TRANS(vssran_b_h, LSX, gen_vvv, gen_helper_vssran_b_h)
TRANS(vssran_h_w, LSX, gen_vvv, gen_helper_vssran_h_w)
TRANS(vssran_w_d, LSX, gen_vvv, gen_helper_vssran_w_d)
TRANS(vssrln_bu_h, LSX, gen_vvv, gen_helper_vssrln_bu_h)
TRANS(vssrln_hu_w, LSX, gen_vvv, gen_helper_vssrln_hu_w)
TRANS(vssrln_wu_d, LSX, gen_vvv, gen_helper_vssrln_wu_d)
TRANS(vssran_bu_h, LSX, gen_vvv, gen_helper_vssran_bu_h)
TRANS(vssran_hu_w, LSX, gen_vvv, gen_helper_vssran_hu_w)
TRANS(vssran_wu_d, LSX, gen_vvv, gen_helper_vssran_wu_d)
TRANS(xvssrln_b_h, LASX, gen_xxx, gen_helper_vssrln_b_h)
TRANS(xvssrln_h_w, LASX, gen_xxx, gen_helper_vssrln_h_w)
TRANS(xvssrln_w_d, LASX, gen_xxx, gen_helper_vssrln_w_d)
TRANS(xvssran_b_h, LASX, gen_xxx, gen_helper_vssran_b_h)
TRANS(xvssran_h_w, LASX, gen_xxx, gen_helper_vssran_h_w)
TRANS(xvssran_w_d, LASX, gen_xxx, gen_helper_vssran_w_d)
TRANS(xvssrln_bu_h, LASX, gen_xxx, gen_helper_vssrln_bu_h)
TRANS(xvssrln_hu_w, LASX, gen_xxx, gen_helper_vssrln_hu_w)
TRANS(xvssrln_wu_d, LASX, gen_xxx, gen_helper_vssrln_wu_d)
TRANS(xvssran_bu_h, LASX, gen_xxx, gen_helper_vssran_bu_h)
TRANS(xvssran_hu_w, LASX, gen_xxx, gen_helper_vssran_hu_w)
TRANS(xvssran_wu_d, LASX, gen_xxx, gen_helper_vssran_wu_d)

TRANS(vssrlni_b_h, LSX, gen_vv_i, gen_helper_vssrlni_b_h)
TRANS(vssrlni_h_w, LSX, gen_vv_i, gen_helper_vssrlni_h_w)
TRANS(vssrlni_w_d, LSX, gen_vv_i, gen_helper_vssrlni_w_d)
TRANS(vssrlni_d_q, LSX, gen_vv_i, gen_helper_vssrlni_d_q)
TRANS(vssrani_b_h, LSX, gen_vv_i, gen_helper_vssrani_b_h)
TRANS(vssrani_h_w, LSX, gen_vv_i, gen_helper_vssrani_h_w)
TRANS(vssrani_w_d, LSX, gen_vv_i, gen_helper_vssrani_w_d)
TRANS(vssrani_d_q, LSX, gen_vv_i, gen_helper_vssrani_d_q)
TRANS(vssrlni_bu_h, LSX, gen_vv_i, gen_helper_vssrlni_bu_h)
TRANS(vssrlni_hu_w, LSX, gen_vv_i, gen_helper_vssrlni_hu_w)
TRANS(vssrlni_wu_d, LSX, gen_vv_i, gen_helper_vssrlni_wu_d)
TRANS(vssrlni_du_q, LSX, gen_vv_i, gen_helper_vssrlni_du_q)
TRANS(vssrani_bu_h, LSX, gen_vv_i, gen_helper_vssrani_bu_h)
TRANS(vssrani_hu_w, LSX, gen_vv_i, gen_helper_vssrani_hu_w)
TRANS(vssrani_wu_d, LSX, gen_vv_i, gen_helper_vssrani_wu_d)
TRANS(vssrani_du_q, LSX, gen_vv_i, gen_helper_vssrani_du_q)
TRANS(xvssrlni_b_h, LASX, gen_xx_i, gen_helper_vssrlni_b_h)
TRANS(xvssrlni_h_w, LASX, gen_xx_i, gen_helper_vssrlni_h_w)
TRANS(xvssrlni_w_d, LASX, gen_xx_i, gen_helper_vssrlni_w_d)
TRANS(xvssrlni_d_q, LASX, gen_xx_i, gen_helper_vssrlni_d_q)
TRANS(xvssrani_b_h, LASX, gen_xx_i, gen_helper_vssrani_b_h)
TRANS(xvssrani_h_w, LASX, gen_xx_i, gen_helper_vssrani_h_w)
TRANS(xvssrani_w_d, LASX, gen_xx_i, gen_helper_vssrani_w_d)
TRANS(xvssrani_d_q, LASX, gen_xx_i, gen_helper_vssrani_d_q)
TRANS(xvssrlni_bu_h, LASX, gen_xx_i, gen_helper_vssrlni_bu_h)
TRANS(xvssrlni_hu_w, LASX, gen_xx_i, gen_helper_vssrlni_hu_w)
TRANS(xvssrlni_wu_d, LASX, gen_xx_i, gen_helper_vssrlni_wu_d)
TRANS(xvssrlni_du_q, LASX, gen_xx_i, gen_helper_vssrlni_du_q)
TRANS(xvssrani_bu_h, LASX, gen_xx_i, gen_helper_vssrani_bu_h)
TRANS(xvssrani_hu_w, LASX, gen_xx_i, gen_helper_vssrani_hu_w)
TRANS(xvssrani_wu_d, LASX, gen_xx_i, gen_helper_vssrani_wu_d)
TRANS(xvssrani_du_q, LASX, gen_xx_i, gen_helper_vssrani_du_q)

TRANS(vssrlrn_b_h, LSX, gen_vvv, gen_helper_vssrlrn_b_h)
TRANS(vssrlrn_h_w, LSX, gen_vvv, gen_helper_vssrlrn_h_w)
TRANS(vssrlrn_w_d, LSX, gen_vvv, gen_helper_vssrlrn_w_d)
TRANS(vssrarn_b_h, LSX, gen_vvv, gen_helper_vssrarn_b_h)
TRANS(vssrarn_h_w, LSX, gen_vvv, gen_helper_vssrarn_h_w)
TRANS(vssrarn_w_d, LSX, gen_vvv, gen_helper_vssrarn_w_d)
TRANS(vssrlrn_bu_h, LSX, gen_vvv, gen_helper_vssrlrn_bu_h)
TRANS(vssrlrn_hu_w, LSX, gen_vvv, gen_helper_vssrlrn_hu_w)
TRANS(vssrlrn_wu_d, LSX, gen_vvv, gen_helper_vssrlrn_wu_d)
TRANS(vssrarn_bu_h, LSX, gen_vvv, gen_helper_vssrarn_bu_h)
TRANS(vssrarn_hu_w, LSX, gen_vvv, gen_helper_vssrarn_hu_w)
TRANS(vssrarn_wu_d, LSX, gen_vvv, gen_helper_vssrarn_wu_d)
TRANS(xvssrlrn_b_h, LASX, gen_xxx, gen_helper_vssrlrn_b_h)
TRANS(xvssrlrn_h_w, LASX, gen_xxx, gen_helper_vssrlrn_h_w)
TRANS(xvssrlrn_w_d, LASX, gen_xxx, gen_helper_vssrlrn_w_d)
TRANS(xvssrarn_b_h, LASX, gen_xxx, gen_helper_vssrarn_b_h)
TRANS(xvssrarn_h_w, LASX, gen_xxx, gen_helper_vssrarn_h_w)
TRANS(xvssrarn_w_d, LASX, gen_xxx, gen_helper_vssrarn_w_d)
TRANS(xvssrlrn_bu_h, LASX, gen_xxx, gen_helper_vssrlrn_bu_h)
TRANS(xvssrlrn_hu_w, LASX, gen_xxx, gen_helper_vssrlrn_hu_w)
TRANS(xvssrlrn_wu_d, LASX, gen_xxx, gen_helper_vssrlrn_wu_d)
TRANS(xvssrarn_bu_h, LASX, gen_xxx, gen_helper_vssrarn_bu_h)
TRANS(xvssrarn_hu_w, LASX, gen_xxx, gen_helper_vssrarn_hu_w)
TRANS(xvssrarn_wu_d, LASX, gen_xxx, gen_helper_vssrarn_wu_d)

TRANS(vssrlrni_b_h, LSX, gen_vv_i, gen_helper_vssrlrni_b_h)
TRANS(vssrlrni_h_w, LSX, gen_vv_i, gen_helper_vssrlrni_h_w)
TRANS(vssrlrni_w_d, LSX, gen_vv_i, gen_helper_vssrlrni_w_d)
TRANS(vssrlrni_d_q, LSX, gen_vv_i, gen_helper_vssrlrni_d_q)
TRANS(vssrarni_b_h, LSX, gen_vv_i, gen_helper_vssrarni_b_h)
TRANS(vssrarni_h_w, LSX, gen_vv_i, gen_helper_vssrarni_h_w)
TRANS(vssrarni_w_d, LSX, gen_vv_i, gen_helper_vssrarni_w_d)
TRANS(vssrarni_d_q, LSX, gen_vv_i, gen_helper_vssrarni_d_q)
TRANS(vssrlrni_bu_h, LSX, gen_vv_i, gen_helper_vssrlrni_bu_h)
TRANS(vssrlrni_hu_w, LSX, gen_vv_i, gen_helper_vssrlrni_hu_w)
TRANS(vssrlrni_wu_d, LSX, gen_vv_i, gen_helper_vssrlrni_wu_d)
TRANS(vssrlrni_du_q, LSX, gen_vv_i, gen_helper_vssrlrni_du_q)
TRANS(vssrarni_bu_h, LSX, gen_vv_i, gen_helper_vssrarni_bu_h)
TRANS(vssrarni_hu_w, LSX, gen_vv_i, gen_helper_vssrarni_hu_w)
TRANS(vssrarni_wu_d, LSX, gen_vv_i, gen_helper_vssrarni_wu_d)
TRANS(vssrarni_du_q, LSX, gen_vv_i, gen_helper_vssrarni_du_q)
TRANS(xvssrlrni_b_h, LASX, gen_xx_i, gen_helper_vssrlrni_b_h)
TRANS(xvssrlrni_h_w, LASX, gen_xx_i, gen_helper_vssrlrni_h_w)
TRANS(xvssrlrni_w_d, LASX, gen_xx_i, gen_helper_vssrlrni_w_d)
TRANS(xvssrlrni_d_q, LASX, gen_xx_i, gen_helper_vssrlrni_d_q)
TRANS(xvssrarni_b_h, LASX, gen_xx_i, gen_helper_vssrarni_b_h)
TRANS(xvssrarni_h_w, LASX, gen_xx_i, gen_helper_vssrarni_h_w)
TRANS(xvssrarni_w_d, LASX, gen_xx_i, gen_helper_vssrarni_w_d)
TRANS(xvssrarni_d_q, LASX, gen_xx_i, gen_helper_vssrarni_d_q)
TRANS(xvssrlrni_bu_h, LASX, gen_xx_i, gen_helper_vssrlrni_bu_h)
TRANS(xvssrlrni_hu_w, LASX, gen_xx_i, gen_helper_vssrlrni_hu_w)
TRANS(xvssrlrni_wu_d, LASX, gen_xx_i, gen_helper_vssrlrni_wu_d)
TRANS(xvssrlrni_du_q, LASX, gen_xx_i, gen_helper_vssrlrni_du_q)
TRANS(xvssrarni_bu_h, LASX, gen_xx_i, gen_helper_vssrarni_bu_h)
TRANS(xvssrarni_hu_w, LASX, gen_xx_i, gen_helper_vssrarni_hu_w)
TRANS(xvssrarni_wu_d, LASX, gen_xx_i, gen_helper_vssrarni_wu_d)
TRANS(xvssrarni_du_q, LASX, gen_xx_i, gen_helper_vssrarni_du_q)

TRANS(vclo_b, LSX, gen_vv, gen_helper_vclo_b)
TRANS(vclo_h, LSX, gen_vv, gen_helper_vclo_h)
TRANS(vclo_w, LSX, gen_vv, gen_helper_vclo_w)
TRANS(vclo_d, LSX, gen_vv, gen_helper_vclo_d)
TRANS(vclz_b, LSX, gen_vv, gen_helper_vclz_b)
TRANS(vclz_h, LSX, gen_vv, gen_helper_vclz_h)
TRANS(vclz_w, LSX, gen_vv, gen_helper_vclz_w)
TRANS(vclz_d, LSX, gen_vv, gen_helper_vclz_d)
TRANS(xvclo_b, LASX, gen_xx, gen_helper_vclo_b)
TRANS(xvclo_h, LASX, gen_xx, gen_helper_vclo_h)
TRANS(xvclo_w, LASX, gen_xx, gen_helper_vclo_w)
TRANS(xvclo_d, LASX, gen_xx, gen_helper_vclo_d)
TRANS(xvclz_b, LASX, gen_xx, gen_helper_vclz_b)
TRANS(xvclz_h, LASX, gen_xx, gen_helper_vclz_h)
TRANS(xvclz_w, LASX, gen_xx, gen_helper_vclz_w)
TRANS(xvclz_d, LASX, gen_xx, gen_helper_vclz_d)

TRANS(vpcnt_b, LSX, gen_vv, gen_helper_vpcnt_b)
TRANS(vpcnt_h, LSX, gen_vv, gen_helper_vpcnt_h)
TRANS(vpcnt_w, LSX, gen_vv, gen_helper_vpcnt_w)
TRANS(vpcnt_d, LSX, gen_vv, gen_helper_vpcnt_d)
TRANS(xvpcnt_b, LASX, gen_xx, gen_helper_vpcnt_b)
TRANS(xvpcnt_h, LASX, gen_xx, gen_helper_vpcnt_h)
TRANS(xvpcnt_w, LASX, gen_xx, gen_helper_vpcnt_w)
TRANS(xvpcnt_d, LASX, gen_xx, gen_helper_vpcnt_d)

static void do_vbit(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                    void (*func)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec))
{
    TCGv_vec mask, lsh, t1, one;

    lsh = tcg_temp_new_vec_matching(t);
    t1 = tcg_temp_new_vec_matching(t);
    mask = tcg_constant_vec_matching(t, vece, (8 << vece) - 1);
    one = tcg_constant_vec_matching(t, vece, 1);

    tcg_gen_and_vec(vece, lsh, b, mask);
    tcg_gen_shlv_vec(vece, t1, one, lsh);
    func(vece, t, a, t1);
}

static void gen_vbitclr(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vbit(vece, t, a, b, tcg_gen_andc_vec);
}

static void gen_vbitset(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vbit(vece, t, a, b, tcg_gen_or_vec);
}

static void gen_vbitrev(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vbit(vece, t, a, b, tcg_gen_xor_vec);
}

static void do_vbitclr(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                      uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, INDEX_op_andc_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_vbitclr_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_vbitclr_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_vbitclr_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitclr,
            .fno = gen_helper_vbitclr_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vbitclr_b, LSX, gvec_vvv, MO_8, do_vbitclr)
TRANS(vbitclr_h, LSX, gvec_vvv, MO_16, do_vbitclr)
TRANS(vbitclr_w, LSX, gvec_vvv, MO_32, do_vbitclr)
TRANS(vbitclr_d, LSX, gvec_vvv, MO_64, do_vbitclr)
TRANS(xvbitclr_b, LASX, gvec_xxx, MO_8, do_vbitclr)
TRANS(xvbitclr_h, LASX, gvec_xxx, MO_16, do_vbitclr)
TRANS(xvbitclr_w, LASX, gvec_xxx, MO_32, do_vbitclr)
TRANS(xvbitclr_d, LASX, gvec_xxx, MO_64, do_vbitclr)

static void do_vbiti(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm,
                     void (*func)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec))
{
    int lsh;
    TCGv_vec t1, one;

    lsh = imm & ((8 << vece) -1);
    t1 = tcg_temp_new_vec_matching(t);
    one = tcg_constant_vec_matching(t, vece, 1);

    tcg_gen_shli_vec(vece, t1, one, lsh);
    func(vece, t, a, t1);
}

static void gen_vbitclri(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_vbiti(vece, t, a, imm, tcg_gen_andc_vec);
}

static void gen_vbitseti(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_vbiti(vece, t, a, imm, tcg_gen_or_vec);
}

static void gen_vbitrevi(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_vbiti(vece, t, a, imm, tcg_gen_xor_vec);
}

static void do_vbitclri(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_andc_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_vbitclri_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_vbitclri_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_vbitclri_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitclri,
            .fnoi = gen_helper_vbitclri_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(vbitclri_b, LSX, gvec_vv_i, MO_8, do_vbitclri)
TRANS(vbitclri_h, LSX, gvec_vv_i, MO_16, do_vbitclri)
TRANS(vbitclri_w, LSX, gvec_vv_i, MO_32, do_vbitclri)
TRANS(vbitclri_d, LSX, gvec_vv_i, MO_64, do_vbitclri)
TRANS(xvbitclri_b, LASX, gvec_xx_i, MO_8, do_vbitclri)
TRANS(xvbitclri_h, LASX, gvec_xx_i, MO_16, do_vbitclri)
TRANS(xvbitclri_w, LASX, gvec_xx_i, MO_32, do_vbitclri)
TRANS(xvbitclri_d, LASX, gvec_xx_i, MO_64, do_vbitclri)

static void do_vbitset(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_vbitset_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_vbitset_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_vbitset_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitset,
            .fno = gen_helper_vbitset_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vbitset_b, LSX, gvec_vvv, MO_8, do_vbitset)
TRANS(vbitset_h, LSX, gvec_vvv, MO_16, do_vbitset)
TRANS(vbitset_w, LSX, gvec_vvv, MO_32, do_vbitset)
TRANS(vbitset_d, LSX, gvec_vvv, MO_64, do_vbitset)
TRANS(xvbitset_b, LASX, gvec_xxx, MO_8, do_vbitset)
TRANS(xvbitset_h, LASX, gvec_xxx, MO_16, do_vbitset)
TRANS(xvbitset_w, LASX, gvec_xxx, MO_32, do_vbitset)
TRANS(xvbitset_d, LASX, gvec_xxx, MO_64, do_vbitset)

static void do_vbitseti(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_vbitseti_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_vbitseti_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_vbitseti_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitseti,
            .fnoi = gen_helper_vbitseti_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(vbitseti_b, LSX, gvec_vv_i, MO_8, do_vbitseti)
TRANS(vbitseti_h, LSX, gvec_vv_i, MO_16, do_vbitseti)
TRANS(vbitseti_w, LSX, gvec_vv_i, MO_32, do_vbitseti)
TRANS(vbitseti_d, LSX, gvec_vv_i, MO_64, do_vbitseti)
TRANS(xvbitseti_b, LASX, gvec_xx_i, MO_8, do_vbitseti)
TRANS(xvbitseti_h, LASX, gvec_xx_i, MO_16, do_vbitseti)
TRANS(xvbitseti_w, LASX, gvec_xx_i, MO_32, do_vbitseti)
TRANS(xvbitseti_d, LASX, gvec_xx_i, MO_64, do_vbitseti)

static void do_vbitrev(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                       uint32_t vk_ofs, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shlv_vec, 0
        };
    static const GVecGen3 op[4] = {
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_vbitrev_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_vbitrev_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_vbitrev_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitrev,
            .fno = gen_helper_vbitrev_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_3(vd_ofs, vj_ofs, vk_ofs, oprsz, maxsz, &op[vece]);
}

TRANS(vbitrev_b, LSX, gvec_vvv, MO_8, do_vbitrev)
TRANS(vbitrev_h, LSX, gvec_vvv, MO_16, do_vbitrev)
TRANS(vbitrev_w, LSX, gvec_vvv, MO_32, do_vbitrev)
TRANS(vbitrev_d, LSX, gvec_vvv, MO_64, do_vbitrev)
TRANS(xvbitrev_b, LASX, gvec_xxx, MO_8, do_vbitrev)
TRANS(xvbitrev_h, LASX, gvec_xxx, MO_16, do_vbitrev)
TRANS(xvbitrev_w, LASX, gvec_xxx, MO_32, do_vbitrev)
TRANS(xvbitrev_d, LASX, gvec_xxx, MO_64, do_vbitrev)

static void do_vbitrevi(unsigned vece, uint32_t vd_ofs, uint32_t vj_ofs,
                        int64_t imm, uint32_t oprsz, uint32_t maxsz)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, 0
        };
    static const GVecGen2i op[4] = {
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_vbitrevi_b,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_vbitrevi_h,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_vbitrevi_w,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vbitrevi,
            .fnoi = gen_helper_vbitrevi_d,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    tcg_gen_gvec_2i(vd_ofs, vj_ofs, oprsz, maxsz, imm, &op[vece]);
}

TRANS(vbitrevi_b, LSX, gvec_vv_i, MO_8, do_vbitrevi)
TRANS(vbitrevi_h, LSX, gvec_vv_i, MO_16, do_vbitrevi)
TRANS(vbitrevi_w, LSX, gvec_vv_i, MO_32, do_vbitrevi)
TRANS(vbitrevi_d, LSX, gvec_vv_i, MO_64, do_vbitrevi)
TRANS(xvbitrevi_b, LASX, gvec_xx_i, MO_8, do_vbitrevi)
TRANS(xvbitrevi_h, LASX, gvec_xx_i, MO_16, do_vbitrevi)
TRANS(xvbitrevi_w, LASX, gvec_xx_i, MO_32, do_vbitrevi)
TRANS(xvbitrevi_d, LASX, gvec_xx_i, MO_64, do_vbitrevi)

TRANS(vfrstp_b, LSX, gen_vvv, gen_helper_vfrstp_b)
TRANS(vfrstp_h, LSX, gen_vvv, gen_helper_vfrstp_h)
TRANS(vfrstpi_b, LSX, gen_vv_i, gen_helper_vfrstpi_b)
TRANS(vfrstpi_h, LSX, gen_vv_i, gen_helper_vfrstpi_h)
TRANS(xvfrstp_b, LASX, gen_xxx, gen_helper_vfrstp_b)
TRANS(xvfrstp_h, LASX, gen_xxx, gen_helper_vfrstp_h)
TRANS(xvfrstpi_b, LASX, gen_xx_i, gen_helper_vfrstpi_b)
TRANS(xvfrstpi_h, LASX, gen_xx_i, gen_helper_vfrstpi_h)

TRANS(vfadd_s, LSX, gen_vvv_ptr, gen_helper_vfadd_s)
TRANS(vfadd_d, LSX, gen_vvv_ptr, gen_helper_vfadd_d)
TRANS(vfsub_s, LSX, gen_vvv_ptr, gen_helper_vfsub_s)
TRANS(vfsub_d, LSX, gen_vvv_ptr, gen_helper_vfsub_d)
TRANS(vfmul_s, LSX, gen_vvv_ptr, gen_helper_vfmul_s)
TRANS(vfmul_d, LSX, gen_vvv_ptr, gen_helper_vfmul_d)
TRANS(vfdiv_s, LSX, gen_vvv_ptr, gen_helper_vfdiv_s)
TRANS(vfdiv_d, LSX, gen_vvv_ptr, gen_helper_vfdiv_d)
TRANS(xvfadd_s, LASX, gen_xxx_ptr, gen_helper_vfadd_s)
TRANS(xvfadd_d, LASX, gen_xxx_ptr, gen_helper_vfadd_d)
TRANS(xvfsub_s, LASX, gen_xxx_ptr, gen_helper_vfsub_s)
TRANS(xvfsub_d, LASX, gen_xxx_ptr, gen_helper_vfsub_d)
TRANS(xvfmul_s, LASX, gen_xxx_ptr, gen_helper_vfmul_s)
TRANS(xvfmul_d, LASX, gen_xxx_ptr, gen_helper_vfmul_d)
TRANS(xvfdiv_s, LASX, gen_xxx_ptr, gen_helper_vfdiv_s)
TRANS(xvfdiv_d, LASX, gen_xxx_ptr, gen_helper_vfdiv_d)

TRANS(vfmadd_s, LSX, gen_vvvv_ptr, gen_helper_vfmadd_s)
TRANS(vfmadd_d, LSX, gen_vvvv_ptr, gen_helper_vfmadd_d)
TRANS(vfmsub_s, LSX, gen_vvvv_ptr, gen_helper_vfmsub_s)
TRANS(vfmsub_d, LSX, gen_vvvv_ptr, gen_helper_vfmsub_d)
TRANS(vfnmadd_s, LSX, gen_vvvv_ptr, gen_helper_vfnmadd_s)
TRANS(vfnmadd_d, LSX, gen_vvvv_ptr, gen_helper_vfnmadd_d)
TRANS(vfnmsub_s, LSX, gen_vvvv_ptr, gen_helper_vfnmsub_s)
TRANS(vfnmsub_d, LSX, gen_vvvv_ptr, gen_helper_vfnmsub_d)
TRANS(xvfmadd_s, LASX, gen_xxxx_ptr, gen_helper_vfmadd_s)
TRANS(xvfmadd_d, LASX, gen_xxxx_ptr, gen_helper_vfmadd_d)
TRANS(xvfmsub_s, LASX, gen_xxxx_ptr, gen_helper_vfmsub_s)
TRANS(xvfmsub_d, LASX, gen_xxxx_ptr, gen_helper_vfmsub_d)
TRANS(xvfnmadd_s, LASX, gen_xxxx_ptr, gen_helper_vfnmadd_s)
TRANS(xvfnmadd_d, LASX, gen_xxxx_ptr, gen_helper_vfnmadd_d)
TRANS(xvfnmsub_s, LASX, gen_xxxx_ptr, gen_helper_vfnmsub_s)
TRANS(xvfnmsub_d, LASX, gen_xxxx_ptr, gen_helper_vfnmsub_d)

TRANS(vfmax_s, LSX, gen_vvv_ptr, gen_helper_vfmax_s)
TRANS(vfmax_d, LSX, gen_vvv_ptr, gen_helper_vfmax_d)
TRANS(vfmin_s, LSX, gen_vvv_ptr, gen_helper_vfmin_s)
TRANS(vfmin_d, LSX, gen_vvv_ptr, gen_helper_vfmin_d)
TRANS(xvfmax_s, LASX, gen_xxx_ptr, gen_helper_vfmax_s)
TRANS(xvfmax_d, LASX, gen_xxx_ptr, gen_helper_vfmax_d)
TRANS(xvfmin_s, LASX, gen_xxx_ptr, gen_helper_vfmin_s)
TRANS(xvfmin_d, LASX, gen_xxx_ptr, gen_helper_vfmin_d)

TRANS(vfmaxa_s, LSX, gen_vvv_ptr, gen_helper_vfmaxa_s)
TRANS(vfmaxa_d, LSX, gen_vvv_ptr, gen_helper_vfmaxa_d)
TRANS(vfmina_s, LSX, gen_vvv_ptr, gen_helper_vfmina_s)
TRANS(vfmina_d, LSX, gen_vvv_ptr, gen_helper_vfmina_d)
TRANS(xvfmaxa_s, LASX, gen_xxx_ptr, gen_helper_vfmaxa_s)
TRANS(xvfmaxa_d, LASX, gen_xxx_ptr, gen_helper_vfmaxa_d)
TRANS(xvfmina_s, LASX, gen_xxx_ptr, gen_helper_vfmina_s)
TRANS(xvfmina_d, LASX, gen_xxx_ptr, gen_helper_vfmina_d)

TRANS(vflogb_s, LSX, gen_vv_ptr, gen_helper_vflogb_s)
TRANS(vflogb_d, LSX, gen_vv_ptr, gen_helper_vflogb_d)
TRANS(xvflogb_s, LASX, gen_xx_ptr, gen_helper_vflogb_s)
TRANS(xvflogb_d, LASX, gen_xx_ptr, gen_helper_vflogb_d)

TRANS(vfclass_s, LSX, gen_vv_ptr, gen_helper_vfclass_s)
TRANS(vfclass_d, LSX, gen_vv_ptr, gen_helper_vfclass_d)
TRANS(xvfclass_s, LASX, gen_xx_ptr, gen_helper_vfclass_s)
TRANS(xvfclass_d, LASX, gen_xx_ptr, gen_helper_vfclass_d)

TRANS(vfsqrt_s, LSX, gen_vv_ptr, gen_helper_vfsqrt_s)
TRANS(vfsqrt_d, LSX, gen_vv_ptr, gen_helper_vfsqrt_d)
TRANS(vfrecip_s, LSX, gen_vv_ptr, gen_helper_vfrecip_s)
TRANS(vfrecip_d, LSX, gen_vv_ptr, gen_helper_vfrecip_d)
TRANS(vfrsqrt_s, LSX, gen_vv_ptr, gen_helper_vfrsqrt_s)
TRANS(vfrsqrt_d, LSX, gen_vv_ptr, gen_helper_vfrsqrt_d)
TRANS(xvfsqrt_s, LASX, gen_xx_ptr, gen_helper_vfsqrt_s)
TRANS(xvfsqrt_d, LASX, gen_xx_ptr, gen_helper_vfsqrt_d)
TRANS(xvfrecip_s, LASX, gen_xx_ptr, gen_helper_vfrecip_s)
TRANS(xvfrecip_d, LASX, gen_xx_ptr, gen_helper_vfrecip_d)
TRANS(xvfrsqrt_s, LASX, gen_xx_ptr, gen_helper_vfrsqrt_s)
TRANS(xvfrsqrt_d, LASX, gen_xx_ptr, gen_helper_vfrsqrt_d)

TRANS(vfcvtl_s_h, LSX, gen_vv_ptr, gen_helper_vfcvtl_s_h)
TRANS(vfcvth_s_h, LSX, gen_vv_ptr, gen_helper_vfcvth_s_h)
TRANS(vfcvtl_d_s, LSX, gen_vv_ptr, gen_helper_vfcvtl_d_s)
TRANS(vfcvth_d_s, LSX, gen_vv_ptr, gen_helper_vfcvth_d_s)
TRANS(vfcvt_h_s, LSX, gen_vvv_ptr, gen_helper_vfcvt_h_s)
TRANS(vfcvt_s_d, LSX, gen_vvv_ptr, gen_helper_vfcvt_s_d)
TRANS(xvfcvtl_s_h, LASX, gen_xx_ptr, gen_helper_vfcvtl_s_h)
TRANS(xvfcvth_s_h, LASX, gen_xx_ptr, gen_helper_vfcvth_s_h)
TRANS(xvfcvtl_d_s, LASX, gen_xx_ptr, gen_helper_vfcvtl_d_s)
TRANS(xvfcvth_d_s, LASX, gen_xx_ptr, gen_helper_vfcvth_d_s)
TRANS(xvfcvt_h_s, LASX, gen_xxx_ptr, gen_helper_vfcvt_h_s)
TRANS(xvfcvt_s_d, LASX, gen_xxx_ptr, gen_helper_vfcvt_s_d)

TRANS(vfrintrne_s, LSX, gen_vv_ptr, gen_helper_vfrintrne_s)
TRANS(vfrintrne_d, LSX, gen_vv_ptr, gen_helper_vfrintrne_d)
TRANS(vfrintrz_s, LSX, gen_vv_ptr, gen_helper_vfrintrz_s)
TRANS(vfrintrz_d, LSX, gen_vv_ptr, gen_helper_vfrintrz_d)
TRANS(vfrintrp_s, LSX, gen_vv_ptr, gen_helper_vfrintrp_s)
TRANS(vfrintrp_d, LSX, gen_vv_ptr, gen_helper_vfrintrp_d)
TRANS(vfrintrm_s, LSX, gen_vv_ptr, gen_helper_vfrintrm_s)
TRANS(vfrintrm_d, LSX, gen_vv_ptr, gen_helper_vfrintrm_d)
TRANS(vfrint_s, LSX, gen_vv_ptr, gen_helper_vfrint_s)
TRANS(vfrint_d, LSX, gen_vv_ptr, gen_helper_vfrint_d)
TRANS(xvfrintrne_s, LASX, gen_xx_ptr, gen_helper_vfrintrne_s)
TRANS(xvfrintrne_d, LASX, gen_xx_ptr, gen_helper_vfrintrne_d)
TRANS(xvfrintrz_s, LASX, gen_xx_ptr, gen_helper_vfrintrz_s)
TRANS(xvfrintrz_d, LASX, gen_xx_ptr, gen_helper_vfrintrz_d)
TRANS(xvfrintrp_s, LASX, gen_xx_ptr, gen_helper_vfrintrp_s)
TRANS(xvfrintrp_d, LASX, gen_xx_ptr, gen_helper_vfrintrp_d)
TRANS(xvfrintrm_s, LASX, gen_xx_ptr, gen_helper_vfrintrm_s)
TRANS(xvfrintrm_d, LASX, gen_xx_ptr, gen_helper_vfrintrm_d)
TRANS(xvfrint_s, LASX, gen_xx_ptr, gen_helper_vfrint_s)
TRANS(xvfrint_d, LASX, gen_xx_ptr, gen_helper_vfrint_d)

TRANS(vftintrne_w_s, LSX, gen_vv_ptr, gen_helper_vftintrne_w_s)
TRANS(vftintrne_l_d, LSX, gen_vv_ptr, gen_helper_vftintrne_l_d)
TRANS(vftintrz_w_s, LSX, gen_vv_ptr, gen_helper_vftintrz_w_s)
TRANS(vftintrz_l_d, LSX, gen_vv_ptr, gen_helper_vftintrz_l_d)
TRANS(vftintrp_w_s, LSX, gen_vv_ptr, gen_helper_vftintrp_w_s)
TRANS(vftintrp_l_d, LSX, gen_vv_ptr, gen_helper_vftintrp_l_d)
TRANS(vftintrm_w_s, LSX, gen_vv_ptr, gen_helper_vftintrm_w_s)
TRANS(vftintrm_l_d, LSX, gen_vv_ptr, gen_helper_vftintrm_l_d)
TRANS(vftint_w_s, LSX, gen_vv_ptr, gen_helper_vftint_w_s)
TRANS(vftint_l_d, LSX, gen_vv_ptr, gen_helper_vftint_l_d)
TRANS(vftintrz_wu_s, LSX, gen_vv_ptr, gen_helper_vftintrz_wu_s)
TRANS(vftintrz_lu_d, LSX, gen_vv_ptr, gen_helper_vftintrz_lu_d)
TRANS(vftint_wu_s, LSX, gen_vv_ptr, gen_helper_vftint_wu_s)
TRANS(vftint_lu_d, LSX, gen_vv_ptr, gen_helper_vftint_lu_d)
TRANS(vftintrne_w_d, LSX, gen_vvv_ptr, gen_helper_vftintrne_w_d)
TRANS(vftintrz_w_d, LSX, gen_vvv_ptr, gen_helper_vftintrz_w_d)
TRANS(vftintrp_w_d, LSX, gen_vvv_ptr, gen_helper_vftintrp_w_d)
TRANS(vftintrm_w_d, LSX, gen_vvv_ptr, gen_helper_vftintrm_w_d)
TRANS(vftint_w_d, LSX, gen_vvv_ptr, gen_helper_vftint_w_d)
TRANS(vftintrnel_l_s, LSX, gen_vv_ptr, gen_helper_vftintrnel_l_s)
TRANS(vftintrneh_l_s, LSX, gen_vv_ptr, gen_helper_vftintrneh_l_s)
TRANS(vftintrzl_l_s, LSX, gen_vv_ptr, gen_helper_vftintrzl_l_s)
TRANS(vftintrzh_l_s, LSX, gen_vv_ptr, gen_helper_vftintrzh_l_s)
TRANS(vftintrpl_l_s, LSX, gen_vv_ptr, gen_helper_vftintrpl_l_s)
TRANS(vftintrph_l_s, LSX, gen_vv_ptr, gen_helper_vftintrph_l_s)
TRANS(vftintrml_l_s, LSX, gen_vv_ptr, gen_helper_vftintrml_l_s)
TRANS(vftintrmh_l_s, LSX, gen_vv_ptr, gen_helper_vftintrmh_l_s)
TRANS(vftintl_l_s, LSX, gen_vv_ptr, gen_helper_vftintl_l_s)
TRANS(vftinth_l_s, LSX, gen_vv_ptr, gen_helper_vftinth_l_s)
TRANS(xvftintrne_w_s, LASX, gen_xx_ptr, gen_helper_vftintrne_w_s)
TRANS(xvftintrne_l_d, LASX, gen_xx_ptr, gen_helper_vftintrne_l_d)
TRANS(xvftintrz_w_s, LASX, gen_xx_ptr, gen_helper_vftintrz_w_s)
TRANS(xvftintrz_l_d, LASX, gen_xx_ptr, gen_helper_vftintrz_l_d)
TRANS(xvftintrp_w_s, LASX, gen_xx_ptr, gen_helper_vftintrp_w_s)
TRANS(xvftintrp_l_d, LASX, gen_xx_ptr, gen_helper_vftintrp_l_d)
TRANS(xvftintrm_w_s, LASX, gen_xx_ptr, gen_helper_vftintrm_w_s)
TRANS(xvftintrm_l_d, LASX, gen_xx_ptr, gen_helper_vftintrm_l_d)
TRANS(xvftint_w_s, LASX, gen_xx_ptr, gen_helper_vftint_w_s)
TRANS(xvftint_l_d, LASX, gen_xx_ptr, gen_helper_vftint_l_d)
TRANS(xvftintrz_wu_s, LASX, gen_xx_ptr, gen_helper_vftintrz_wu_s)
TRANS(xvftintrz_lu_d, LASX, gen_xx_ptr, gen_helper_vftintrz_lu_d)
TRANS(xvftint_wu_s, LASX, gen_xx_ptr, gen_helper_vftint_wu_s)
TRANS(xvftint_lu_d, LASX, gen_xx_ptr, gen_helper_vftint_lu_d)
TRANS(xvftintrne_w_d, LASX, gen_xxx_ptr, gen_helper_vftintrne_w_d)
TRANS(xvftintrz_w_d, LASX, gen_xxx_ptr, gen_helper_vftintrz_w_d)
TRANS(xvftintrp_w_d, LASX, gen_xxx_ptr, gen_helper_vftintrp_w_d)
TRANS(xvftintrm_w_d, LASX, gen_xxx_ptr, gen_helper_vftintrm_w_d)
TRANS(xvftint_w_d, LASX, gen_xxx_ptr, gen_helper_vftint_w_d)
TRANS(xvftintrnel_l_s, LASX, gen_xx_ptr, gen_helper_vftintrnel_l_s)
TRANS(xvftintrneh_l_s, LASX, gen_xx_ptr, gen_helper_vftintrneh_l_s)
TRANS(xvftintrzl_l_s, LASX, gen_xx_ptr, gen_helper_vftintrzl_l_s)
TRANS(xvftintrzh_l_s, LASX, gen_xx_ptr, gen_helper_vftintrzh_l_s)
TRANS(xvftintrpl_l_s, LASX, gen_xx_ptr, gen_helper_vftintrpl_l_s)
TRANS(xvftintrph_l_s, LASX, gen_xx_ptr, gen_helper_vftintrph_l_s)
TRANS(xvftintrml_l_s, LASX, gen_xx_ptr, gen_helper_vftintrml_l_s)
TRANS(xvftintrmh_l_s, LASX, gen_xx_ptr, gen_helper_vftintrmh_l_s)
TRANS(xvftintl_l_s, LASX, gen_xx_ptr, gen_helper_vftintl_l_s)
TRANS(xvftinth_l_s, LASX, gen_xx_ptr, gen_helper_vftinth_l_s)

TRANS(vffint_s_w, LSX, gen_vv_ptr, gen_helper_vffint_s_w)
TRANS(vffint_d_l, LSX, gen_vv_ptr, gen_helper_vffint_d_l)
TRANS(vffint_s_wu, LSX, gen_vv_ptr, gen_helper_vffint_s_wu)
TRANS(vffint_d_lu, LSX, gen_vv_ptr, gen_helper_vffint_d_lu)
TRANS(vffintl_d_w, LSX, gen_vv_ptr, gen_helper_vffintl_d_w)
TRANS(vffinth_d_w, LSX, gen_vv_ptr, gen_helper_vffinth_d_w)
TRANS(vffint_s_l, LSX, gen_vvv_ptr, gen_helper_vffint_s_l)
TRANS(xvffint_s_w, LASX, gen_xx_ptr, gen_helper_vffint_s_w)
TRANS(xvffint_d_l, LASX, gen_xx_ptr, gen_helper_vffint_d_l)
TRANS(xvffint_s_wu, LASX, gen_xx_ptr, gen_helper_vffint_s_wu)
TRANS(xvffint_d_lu, LASX, gen_xx_ptr, gen_helper_vffint_d_lu)
TRANS(xvffintl_d_w, LASX, gen_xx_ptr, gen_helper_vffintl_d_w)
TRANS(xvffinth_d_w, LASX, gen_xx_ptr, gen_helper_vffinth_d_w)
TRANS(xvffint_s_l, LASX, gen_xxx_ptr, gen_helper_vffint_s_l)

static bool do_cmp_vl(DisasContext *ctx, arg_vvv *a,
                      uint32_t oprsz, MemOp mop, TCGCond cond)
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);
    vk_ofs = vec_full_offset(a->vk);

    tcg_gen_gvec_cmp(cond, mop, vd_ofs, vj_ofs, vk_ofs, oprsz, ctx->vl / 8);
    return true;
}

static bool do_cmp(DisasContext *ctx, arg_vvv *a,
                   MemOp mop, TCGCond cond)
{
    return do_cmp_vl(ctx, a, 16, mop, cond);
}

static bool do_xcmp(DisasContext *ctx, arg_vvv *a,
                    MemOp mop, TCGCond cond)
{
    return do_cmp_vl(ctx, a, 32, mop, cond);
}

static bool do_cmpi_vl(DisasContext *ctx, arg_vv_i *a,
                       uint32_t oprsz, MemOp mop, TCGCond cond)
{
    uint32_t vd_ofs, vj_ofs;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);

    tcg_gen_gvec_cmpi(cond, mop, vd_ofs, vj_ofs, a->imm, oprsz, ctx->vl / 8);
    return true;
}

static bool do_cmpi(DisasContext *ctx, arg_vv_i *a,
                    MemOp mop, TCGCond cond)
{
    return do_cmpi_vl(ctx, a, 16, mop, cond);
}

static bool do_xcmpi(DisasContext *ctx, arg_vv_i *a,
                     MemOp mop, TCGCond cond)
{
    return do_cmpi_vl(ctx, a, 32, mop, cond);
}

TRANS(vseq_b, LSX, do_cmp, MO_8, TCG_COND_EQ)
TRANS(vseq_h, LSX, do_cmp, MO_16, TCG_COND_EQ)
TRANS(vseq_w, LSX, do_cmp, MO_32, TCG_COND_EQ)
TRANS(vseq_d, LSX, do_cmp, MO_64, TCG_COND_EQ)
TRANS(vseqi_b, LSX, do_cmpi, MO_8, TCG_COND_EQ)
TRANS(vseqi_h, LSX, do_cmpi, MO_16, TCG_COND_EQ)
TRANS(vseqi_w, LSX, do_cmpi, MO_32, TCG_COND_EQ)
TRANS(vseqi_d, LSX, do_cmpi, MO_64, TCG_COND_EQ)
TRANS(xvseq_b, LASX, do_xcmp, MO_8, TCG_COND_EQ)
TRANS(xvseq_h, LASX, do_xcmp, MO_16, TCG_COND_EQ)
TRANS(xvseq_w, LASX, do_xcmp, MO_32, TCG_COND_EQ)
TRANS(xvseq_d, LASX, do_xcmp, MO_64, TCG_COND_EQ)
TRANS(xvseqi_b, LASX, do_xcmpi, MO_8, TCG_COND_EQ)
TRANS(xvseqi_h, LASX, do_xcmpi, MO_16, TCG_COND_EQ)
TRANS(xvseqi_w, LASX, do_xcmpi, MO_32, TCG_COND_EQ)
TRANS(xvseqi_d, LASX, do_xcmpi, MO_64, TCG_COND_EQ)

TRANS(vsle_b, LSX, do_cmp, MO_8, TCG_COND_LE)
TRANS(vsle_h, LSX, do_cmp, MO_16, TCG_COND_LE)
TRANS(vsle_w, LSX, do_cmp, MO_32, TCG_COND_LE)
TRANS(vsle_d, LSX, do_cmp, MO_64, TCG_COND_LE)
TRANS(vslei_b, LSX, do_cmpi, MO_8, TCG_COND_LE)
TRANS(vslei_h, LSX, do_cmpi, MO_16, TCG_COND_LE)
TRANS(vslei_w, LSX, do_cmpi, MO_32, TCG_COND_LE)
TRANS(vslei_d, LSX, do_cmpi, MO_64, TCG_COND_LE)
TRANS(vsle_bu, LSX, do_cmp, MO_8, TCG_COND_LEU)
TRANS(vsle_hu, LSX, do_cmp, MO_16, TCG_COND_LEU)
TRANS(vsle_wu, LSX, do_cmp, MO_32, TCG_COND_LEU)
TRANS(vsle_du, LSX, do_cmp, MO_64, TCG_COND_LEU)
TRANS(vslei_bu, LSX, do_cmpi, MO_8, TCG_COND_LEU)
TRANS(vslei_hu, LSX, do_cmpi, MO_16, TCG_COND_LEU)
TRANS(vslei_wu, LSX, do_cmpi, MO_32, TCG_COND_LEU)
TRANS(vslei_du, LSX, do_cmpi, MO_64, TCG_COND_LEU)
TRANS(xvsle_b, LASX, do_xcmp, MO_8, TCG_COND_LE)
TRANS(xvsle_h, LASX, do_xcmp, MO_16, TCG_COND_LE)
TRANS(xvsle_w, LASX, do_xcmp, MO_32, TCG_COND_LE)
TRANS(xvsle_d, LASX, do_xcmp, MO_64, TCG_COND_LE)
TRANS(xvslei_b, LASX, do_xcmpi, MO_8, TCG_COND_LE)
TRANS(xvslei_h, LASX, do_xcmpi, MO_16, TCG_COND_LE)
TRANS(xvslei_w, LASX, do_xcmpi, MO_32, TCG_COND_LE)
TRANS(xvslei_d, LASX, do_xcmpi, MO_64, TCG_COND_LE)
TRANS(xvsle_bu, LASX, do_xcmp, MO_8, TCG_COND_LEU)
TRANS(xvsle_hu, LASX, do_xcmp, MO_16, TCG_COND_LEU)
TRANS(xvsle_wu, LASX, do_xcmp, MO_32, TCG_COND_LEU)
TRANS(xvsle_du, LASX, do_xcmp, MO_64, TCG_COND_LEU)
TRANS(xvslei_bu, LASX, do_xcmpi, MO_8, TCG_COND_LEU)
TRANS(xvslei_hu, LASX, do_xcmpi, MO_16, TCG_COND_LEU)
TRANS(xvslei_wu, LASX, do_xcmpi, MO_32, TCG_COND_LEU)
TRANS(xvslei_du, LASX, do_xcmpi, MO_64, TCG_COND_LEU)

TRANS(vslt_b, LSX, do_cmp, MO_8, TCG_COND_LT)
TRANS(vslt_h, LSX, do_cmp, MO_16, TCG_COND_LT)
TRANS(vslt_w, LSX, do_cmp, MO_32, TCG_COND_LT)
TRANS(vslt_d, LSX, do_cmp, MO_64, TCG_COND_LT)
TRANS(vslti_b, LSX, do_cmpi, MO_8, TCG_COND_LT)
TRANS(vslti_h, LSX, do_cmpi, MO_16, TCG_COND_LT)
TRANS(vslti_w, LSX, do_cmpi, MO_32, TCG_COND_LT)
TRANS(vslti_d, LSX, do_cmpi, MO_64, TCG_COND_LT)
TRANS(vslt_bu, LSX, do_cmp, MO_8, TCG_COND_LTU)
TRANS(vslt_hu, LSX, do_cmp, MO_16, TCG_COND_LTU)
TRANS(vslt_wu, LSX, do_cmp, MO_32, TCG_COND_LTU)
TRANS(vslt_du, LSX, do_cmp, MO_64, TCG_COND_LTU)
TRANS(vslti_bu, LSX, do_cmpi, MO_8, TCG_COND_LTU)
TRANS(vslti_hu, LSX, do_cmpi, MO_16, TCG_COND_LTU)
TRANS(vslti_wu, LSX, do_cmpi, MO_32, TCG_COND_LTU)
TRANS(vslti_du, LSX, do_cmpi, MO_64, TCG_COND_LTU)
TRANS(xvslt_b, LASX, do_xcmp, MO_8, TCG_COND_LT)
TRANS(xvslt_h, LASX, do_xcmp, MO_16, TCG_COND_LT)
TRANS(xvslt_w, LASX, do_xcmp, MO_32, TCG_COND_LT)
TRANS(xvslt_d, LASX, do_xcmp, MO_64, TCG_COND_LT)
TRANS(xvslti_b, LASX, do_xcmpi, MO_8, TCG_COND_LT)
TRANS(xvslti_h, LASX, do_xcmpi, MO_16, TCG_COND_LT)
TRANS(xvslti_w, LASX, do_xcmpi, MO_32, TCG_COND_LT)
TRANS(xvslti_d, LASX, do_xcmpi, MO_64, TCG_COND_LT)
TRANS(xvslt_bu, LASX, do_xcmp, MO_8, TCG_COND_LTU)
TRANS(xvslt_hu, LASX, do_xcmp, MO_16, TCG_COND_LTU)
TRANS(xvslt_wu, LASX, do_xcmp, MO_32, TCG_COND_LTU)
TRANS(xvslt_du, LASX, do_xcmp, MO_64, TCG_COND_LTU)
TRANS(xvslti_bu, LASX, do_xcmpi, MO_8, TCG_COND_LTU)
TRANS(xvslti_hu, LASX, do_xcmpi, MO_16, TCG_COND_LTU)
TRANS(xvslti_wu, LASX, do_xcmpi, MO_32, TCG_COND_LTU)
TRANS(xvslti_du, LASX, do_xcmpi, MO_64, TCG_COND_LTU)

static bool do_vfcmp_cond_s(DisasContext *ctx, arg_vvv_fcond *a, uint32_t sz)
{
    uint32_t flags = get_fcmp_flags(ctx, a->fcond >> 1);
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);
    TCGv_i32 oprsz = tcg_constant_i32(sz);

    if(flags == -1){
        generate_exception(ctx, EXCCODE_INE);
        return true;
    }

    if (!check_vec(ctx, sz)) {
        return true;
    }

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_s : gen_helper_vfcmp_c_s);
    fn(tcg_env, oprsz, vd, vj, vk, tcg_constant_i32(flags));

    return true;
}

static bool do_vfcmp_cond_d(DisasContext *ctx, arg_vvv_fcond *a, uint32_t sz)
{
    uint32_t flags = get_fcmp_flags(ctx, a->fcond >> 1);
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);
    TCGv_i32 oprsz = tcg_constant_i32(sz);

    if (flags == -1) {
        generate_exception(ctx, EXCCODE_INE);
        return true;
    }

    if (!check_vec(ctx, sz)) {
        return true;
    }

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_d : gen_helper_vfcmp_c_d);
    fn(tcg_env, oprsz, vd, vj, vk, tcg_constant_i32(flags));

    return true;
}

TRANS(vfcmp_cond_s, LSX, do_vfcmp_cond_s, 16)
TRANS(vfcmp_cond_d, LSX, do_vfcmp_cond_d, 16)
TRANS(xvfcmp_cond_s, LASX, do_vfcmp_cond_s, 32)
TRANS(xvfcmp_cond_d, LASX, do_vfcmp_cond_d, 32)

static bool do_vbitsel_v(DisasContext *ctx, arg_vvvv *a, uint32_t oprsz)
{
    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_bitsel(MO_64, vec_full_offset(a->vd), vec_full_offset(a->va),
                        vec_full_offset(a->vk), vec_full_offset(a->vj),
                        oprsz, ctx->vl / 8);
    return true;
}

TRANS(vbitsel_v, LSX, do_vbitsel_v, 16)
TRANS(xvbitsel_v, LASX, do_vbitsel_v, 32)

static void gen_vbitseli(unsigned vece, TCGv_vec a, TCGv_vec b, int64_t imm)
{
    tcg_gen_bitsel_vec(vece, a, a, tcg_constant_vec_matching(a, vece, imm), b);
}

static bool do_vbitseli_b(DisasContext *ctx, arg_vv_i *a, uint32_t oprsz)
{
    static const GVecGen2i op = {
       .fniv = gen_vbitseli,
       .fnoi = gen_helper_vbitseli_b,
       .vece = MO_8,
       .load_dest = true
    };

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_2i(vec_full_offset(a->vd), vec_full_offset(a->vj),
                    oprsz, ctx->vl / 8, a->imm , &op);
    return true;
}

TRANS(vbitseli_b, LSX, do_vbitseli_b, 16)
TRANS(xvbitseli_b, LASX, do_vbitseli_b, 32)

#define VSET(NAME, COND)                                                       \
static bool trans_## NAME (DisasContext *ctx, arg_cv *a)                       \
{                                                                              \
    TCGv_i64 t1, al, ah;                                                       \
                                                                               \
    al = tcg_temp_new_i64();                                                   \
    ah = tcg_temp_new_i64();                                                   \
    t1 = tcg_temp_new_i64();                                                   \
                                                                               \
    get_vreg64(ah, a->vj, 1);                                                  \
    get_vreg64(al, a->vj, 0);                                                  \
                                                                               \
    if (!avail_LSX(ctx)) {                                                     \
        return false;                                                          \
    }                                                                          \
                                                                               \
    if (!check_vec(ctx, 16)) {                                                 \
        return true;                                                           \
    }                                                                          \
                                                                               \
    tcg_gen_or_i64(t1, al, ah);                                                \
    tcg_gen_setcondi_i64(COND, t1, t1, 0);                                     \
    tcg_gen_st8_tl(t1, tcg_env, offsetof(CPULoongArchState, cf[a->cd & 0x7])); \
                                                                               \
    return true;                                                               \
}

VSET(vseteqz_v, TCG_COND_EQ)
VSET(vsetnez_v, TCG_COND_NE)

TRANS(vsetanyeqz_b, LSX, gen_cv, gen_helper_vsetanyeqz_b)
TRANS(vsetanyeqz_h, LSX, gen_cv, gen_helper_vsetanyeqz_h)
TRANS(vsetanyeqz_w, LSX, gen_cv, gen_helper_vsetanyeqz_w)
TRANS(vsetanyeqz_d, LSX, gen_cv, gen_helper_vsetanyeqz_d)
TRANS(vsetallnez_b, LSX, gen_cv, gen_helper_vsetallnez_b)
TRANS(vsetallnez_h, LSX, gen_cv, gen_helper_vsetallnez_h)
TRANS(vsetallnez_w, LSX, gen_cv, gen_helper_vsetallnez_w)
TRANS(vsetallnez_d, LSX, gen_cv, gen_helper_vsetallnez_d)

#define XVSET(NAME, COND)                                                      \
static bool trans_## NAME(DisasContext *ctx, arg_cv * a)                       \
{                                                                              \
    TCGv_i64 t1, t2, d[4];                                                     \
                                                                               \
    d[0] = tcg_temp_new_i64();                                                 \
    d[1] = tcg_temp_new_i64();                                                 \
    d[2] = tcg_temp_new_i64();                                                 \
    d[3] = tcg_temp_new_i64();                                                 \
    t1 = tcg_temp_new_i64();                                                   \
    t2 = tcg_temp_new_i64();                                                   \
                                                                               \
    get_vreg64(d[0], a->vj, 0);                                                \
    get_vreg64(d[1], a->vj, 1);                                                \
    get_vreg64(d[2], a->vj, 2);                                                \
    get_vreg64(d[3], a->vj, 3);                                                \
                                                                               \
    if (!avail_LASX(ctx)) {                                                    \
        return false;                                                          \
    }                                                                          \
                                                                               \
    if (!check_vec(ctx, 32)) {                                                 \
        return true;                                                           \
    }                                                                          \
                                                                               \
    tcg_gen_or_i64(t1, d[0], d[1]);                                            \
    tcg_gen_or_i64(t2, d[2], d[3]);                                            \
    tcg_gen_or_i64(t1, t2, t1);                                                \
    tcg_gen_setcondi_i64(COND, t1, t1, 0);                                     \
    tcg_gen_st8_tl(t1, tcg_env, offsetof(CPULoongArchState, cf[a->cd & 0x7])); \
                                                                               \
    return true;                                                               \
}

XVSET(xvseteqz_v, TCG_COND_EQ)
XVSET(xvsetnez_v, TCG_COND_NE)

TRANS(xvsetanyeqz_b, LASX, gen_cx, gen_helper_vsetanyeqz_b)
TRANS(xvsetanyeqz_h, LASX, gen_cx, gen_helper_vsetanyeqz_h)
TRANS(xvsetanyeqz_w, LASX, gen_cx, gen_helper_vsetanyeqz_w)
TRANS(xvsetanyeqz_d, LASX, gen_cx, gen_helper_vsetanyeqz_d)
TRANS(xvsetallnez_b, LASX, gen_cx, gen_helper_vsetallnez_b)
TRANS(xvsetallnez_h, LASX, gen_cx, gen_helper_vsetallnez_h)
TRANS(xvsetallnez_w, LASX, gen_cx, gen_helper_vsetallnez_w)
TRANS(xvsetallnez_d, LASX, gen_cx, gen_helper_vsetallnez_d)

static bool gen_g2v_vl(DisasContext *ctx, arg_vr_i *a, uint32_t oprsz, MemOp mop,
                       void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    func(src, tcg_env, vec_reg_offset(a->vd, a->imm, mop));

    return true;
}

static bool gen_g2v(DisasContext *ctx, arg_vr_i *a, MemOp mop,
                    void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    return gen_g2v_vl(ctx, a, 16, mop, func);
}

static bool gen_g2x(DisasContext *ctx, arg_vr_i *a, MemOp mop,
                    void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    return gen_g2v_vl(ctx, a, 32, mop, func);
}

TRANS(vinsgr2vr_b, LSX, gen_g2v, MO_8, tcg_gen_st8_i64)
TRANS(vinsgr2vr_h, LSX, gen_g2v, MO_16, tcg_gen_st16_i64)
TRANS(vinsgr2vr_w, LSX, gen_g2v, MO_32, tcg_gen_st32_i64)
TRANS(vinsgr2vr_d, LSX, gen_g2v, MO_64, tcg_gen_st_i64)
TRANS(xvinsgr2vr_w, LASX, gen_g2x, MO_32, tcg_gen_st32_i64)
TRANS(xvinsgr2vr_d, LASX, gen_g2x, MO_64, tcg_gen_st_i64)

static bool gen_v2g_vl(DisasContext *ctx, arg_rv_i *a, uint32_t oprsz, MemOp mop,
                       void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    func(dst, tcg_env, vec_reg_offset(a->vj, a->imm, mop));

    return true;
}

static bool gen_v2g(DisasContext *ctx, arg_rv_i *a, MemOp mop,
                    void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    return gen_v2g_vl(ctx, a, 16, mop, func);
}

static bool gen_x2g(DisasContext *ctx, arg_rv_i *a, MemOp mop,
                    void (*func)(TCGv, TCGv_ptr, tcg_target_long))
{
    return gen_v2g_vl(ctx, a, 32, mop, func);
}

TRANS(vpickve2gr_b, LSX, gen_v2g, MO_8, tcg_gen_ld8s_i64)
TRANS(vpickve2gr_h, LSX, gen_v2g, MO_16, tcg_gen_ld16s_i64)
TRANS(vpickve2gr_w, LSX, gen_v2g, MO_32, tcg_gen_ld32s_i64)
TRANS(vpickve2gr_d, LSX, gen_v2g, MO_64, tcg_gen_ld_i64)
TRANS(vpickve2gr_bu, LSX, gen_v2g, MO_8, tcg_gen_ld8u_i64)
TRANS(vpickve2gr_hu, LSX, gen_v2g, MO_16, tcg_gen_ld16u_i64)
TRANS(vpickve2gr_wu, LSX, gen_v2g, MO_32, tcg_gen_ld32u_i64)
TRANS(vpickve2gr_du, LSX, gen_v2g, MO_64, tcg_gen_ld_i64)
TRANS(xvpickve2gr_w, LASX, gen_x2g, MO_32, tcg_gen_ld32s_i64)
TRANS(xvpickve2gr_d, LASX, gen_x2g, MO_64, tcg_gen_ld_i64)
TRANS(xvpickve2gr_wu, LASX, gen_x2g, MO_32, tcg_gen_ld32u_i64)
TRANS(xvpickve2gr_du, LASX, gen_x2g, MO_64, tcg_gen_ld_i64)

static bool gvec_dup_vl(DisasContext *ctx, arg_vr *a,
                        uint32_t oprsz, MemOp mop)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_gvec_dup_i64(mop, vec_full_offset(a->vd),
                         oprsz, ctx->vl/8, src);
    return true;
}

static bool gvec_dup(DisasContext *ctx, arg_vr *a, MemOp mop)
{
    return gvec_dup_vl(ctx, a, 16, mop);
}

static bool gvec_dupx(DisasContext *ctx, arg_vr *a, MemOp mop)
{
    return gvec_dup_vl(ctx, a, 32, mop);
}

TRANS(vreplgr2vr_b, LSX, gvec_dup, MO_8)
TRANS(vreplgr2vr_h, LSX, gvec_dup, MO_16)
TRANS(vreplgr2vr_w, LSX, gvec_dup, MO_32)
TRANS(vreplgr2vr_d, LSX, gvec_dup, MO_64)
TRANS(xvreplgr2vr_b, LASX, gvec_dupx, MO_8)
TRANS(xvreplgr2vr_h, LASX, gvec_dupx, MO_16)
TRANS(xvreplgr2vr_w, LASX, gvec_dupx, MO_32)
TRANS(xvreplgr2vr_d, LASX, gvec_dupx, MO_64)

static bool trans_vreplvei_b(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(MO_8,vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.B((a->imm))),
                         16, ctx->vl/8);
    return true;
}

static bool trans_vreplvei_h(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(MO_16, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.H((a->imm))),
                         16, ctx->vl/8);
    return true;
}
static bool trans_vreplvei_w(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(MO_32, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.W((a->imm))),
                        16, ctx->vl/8);
    return true;
}
static bool trans_vreplvei_d(DisasContext *ctx, arg_vv_i *a)
{
    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(MO_64, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.D((a->imm))),
                         16, ctx->vl/8);
    return true;
}

static bool gen_vreplve_vl(DisasContext *ctx, arg_vvr *a,
                           uint32_t oprsz, int vece, int bit,
                           void (*func)(TCGv_i64, TCGv_ptr, tcg_target_long))
{
    int i;
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_ptr t1 = tcg_temp_new_ptr();
    TCGv_i64 t2 = tcg_temp_new_i64();

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    tcg_gen_andi_i64(t0, gpr_src(ctx, a->rk, EXT_NONE), (LSX_LEN / bit) - 1);
    tcg_gen_shli_i64(t0, t0, vece);
    if (HOST_BIG_ENDIAN) {
        tcg_gen_xori_i64(t0, t0, vece << ((LSX_LEN / bit) - 1));
    }

    tcg_gen_trunc_i64_ptr(t1, t0);
    tcg_gen_add_ptr(t1, t1, tcg_env);

    for (i = 0; i < oprsz; i += 16) {
        func(t2, t1, vec_full_offset(a->vj) + i);
        tcg_gen_gvec_dup_i64(vece, vec_full_offset(a->vd) + i, 16, 16, t2);
    }

    return true;
}

static bool gen_vreplve(DisasContext *ctx, arg_vvr *a, int vece, int bit,
                        void (*func)(TCGv_i64, TCGv_ptr, tcg_target_long))
{
    return gen_vreplve_vl(ctx, a, 16, vece, bit, func);
}

static bool gen_xvreplve(DisasContext *ctx, arg_vvr *a, int vece, int bit,
                         void (*func)(TCGv_i64, TCGv_ptr, tcg_target_long))
{
    return gen_vreplve_vl(ctx, a, 32, vece, bit, func);
}

TRANS(vreplve_b, LSX, gen_vreplve, MO_8,  8, tcg_gen_ld8u_i64)
TRANS(vreplve_h, LSX, gen_vreplve, MO_16, 16, tcg_gen_ld16u_i64)
TRANS(vreplve_w, LSX, gen_vreplve, MO_32, 32, tcg_gen_ld32u_i64)
TRANS(vreplve_d, LSX, gen_vreplve, MO_64, 64, tcg_gen_ld_i64)
TRANS(xvreplve_b, LASX, gen_xvreplve, MO_8,  8, tcg_gen_ld8u_i64)
TRANS(xvreplve_h, LASX, gen_xvreplve, MO_16, 16, tcg_gen_ld16u_i64)
TRANS(xvreplve_w, LASX, gen_xvreplve, MO_32, 32, tcg_gen_ld32u_i64)
TRANS(xvreplve_d, LASX, gen_xvreplve, MO_64, 64, tcg_gen_ld_i64)

static bool gen_xvrepl128(DisasContext *ctx, arg_vv_i *a, MemOp mop)
{
    int i;

    if (!check_vec(ctx, 32)) {
        return true;
    }

    for (i = 0; i < 32; i += 16) {
        tcg_gen_gvec_dup_mem(mop, vec_full_offset(a->vd) + i,
                             vec_reg_offset(a->vj, a->imm, mop) + i, 16, 16);

    }
    return true;
}

TRANS(xvrepl128vei_b, LASX, gen_xvrepl128, MO_8)
TRANS(xvrepl128vei_h, LASX, gen_xvrepl128, MO_16)
TRANS(xvrepl128vei_w, LASX, gen_xvrepl128, MO_32)
TRANS(xvrepl128vei_d, LASX, gen_xvrepl128, MO_64)

static bool gen_xvreplve0(DisasContext *ctx, arg_vv *a, MemOp mop)
{
    if (!check_vec(ctx, 32)) {
        return true;
    }

    tcg_gen_gvec_dup_mem(mop, vec_full_offset(a->vd),
                         vec_full_offset(a->vj), 32, 32);
    return true;
}

TRANS(xvreplve0_b, LASX, gen_xvreplve0, MO_8)
TRANS(xvreplve0_h, LASX, gen_xvreplve0, MO_16)
TRANS(xvreplve0_w, LASX, gen_xvreplve0, MO_32)
TRANS(xvreplve0_d, LASX, gen_xvreplve0, MO_64)
TRANS(xvreplve0_q, LASX, gen_xvreplve0, MO_128)

TRANS(xvinsve0_w, LASX, gen_xx_i, gen_helper_xvinsve0_w)
TRANS(xvinsve0_d, LASX, gen_xx_i, gen_helper_xvinsve0_d)

TRANS(xvpickve_w, LASX, gen_xx_i, gen_helper_xvpickve_w)
TRANS(xvpickve_d, LASX, gen_xx_i, gen_helper_xvpickve_d)

static bool do_vbsll_v(DisasContext *ctx, arg_vv_i *a, uint32_t oprsz)
{
    int i, ofs;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    for (i = 0; i < oprsz / 16; i++) {
        TCGv desthigh = tcg_temp_new_i64();
        TCGv destlow = tcg_temp_new_i64();
        TCGv high = tcg_temp_new_i64();
        TCGv low = tcg_temp_new_i64();

        get_vreg64(low, a->vj, 2 * i);

        ofs = ((a->imm) & 0xf) * 8;
        if (ofs < 64) {
            get_vreg64(high, a->vj, 2 * i + 1);
            tcg_gen_extract2_i64(desthigh, low, high, 64 - ofs);
            tcg_gen_shli_i64(destlow, low, ofs);
        } else {
            tcg_gen_shli_i64(desthigh, low, ofs - 64);
            destlow = tcg_constant_i64(0);
        }
        set_vreg64(desthigh, a->vd, 2 * i + 1);
        set_vreg64(destlow, a->vd, 2 * i);
    }

    return true;
}

static bool do_vbsrl_v(DisasContext *ctx, arg_vv_i *a, uint32_t oprsz)
{
    int i, ofs;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    for (i = 0; i < oprsz / 16; i++) {
        TCGv desthigh = tcg_temp_new_i64();
        TCGv destlow = tcg_temp_new_i64();
        TCGv high = tcg_temp_new_i64();
        TCGv low = tcg_temp_new_i64();
        get_vreg64(high, a->vj, 2 * i + 1);

        ofs = ((a->imm) & 0xf) * 8;
        if (ofs < 64) {
            get_vreg64(low, a->vj, 2 * i);
            tcg_gen_extract2_i64(destlow, low, high, ofs);
            tcg_gen_shri_i64(desthigh, high, ofs);
        } else {
            tcg_gen_shri_i64(destlow, high, ofs - 64);
            desthigh = tcg_constant_i64(0);
        }
        set_vreg64(desthigh, a->vd, 2 * i + 1);
        set_vreg64(destlow, a->vd, 2 * i);
    }

    return true;
}

TRANS(vbsll_v, LSX, do_vbsll_v, 16)
TRANS(vbsrl_v, LSX, do_vbsrl_v, 16)
TRANS(xvbsll_v, LASX, do_vbsll_v, 32)
TRANS(xvbsrl_v, LASX, do_vbsrl_v, 32)

TRANS(vpackev_b, LSX, gen_vvv, gen_helper_vpackev_b)
TRANS(vpackev_h, LSX, gen_vvv, gen_helper_vpackev_h)
TRANS(vpackev_w, LSX, gen_vvv, gen_helper_vpackev_w)
TRANS(vpackev_d, LSX, gen_vvv, gen_helper_vpackev_d)
TRANS(vpackod_b, LSX, gen_vvv, gen_helper_vpackod_b)
TRANS(vpackod_h, LSX, gen_vvv, gen_helper_vpackod_h)
TRANS(vpackod_w, LSX, gen_vvv, gen_helper_vpackod_w)
TRANS(vpackod_d, LSX, gen_vvv, gen_helper_vpackod_d)
TRANS(xvpackev_b, LASX, gen_xxx, gen_helper_vpackev_b)
TRANS(xvpackev_h, LASX, gen_xxx, gen_helper_vpackev_h)
TRANS(xvpackev_w, LASX, gen_xxx, gen_helper_vpackev_w)
TRANS(xvpackev_d, LASX, gen_xxx, gen_helper_vpackev_d)
TRANS(xvpackod_b, LASX, gen_xxx, gen_helper_vpackod_b)
TRANS(xvpackod_h, LASX, gen_xxx, gen_helper_vpackod_h)
TRANS(xvpackod_w, LASX, gen_xxx, gen_helper_vpackod_w)
TRANS(xvpackod_d, LASX, gen_xxx, gen_helper_vpackod_d)

TRANS(vpickev_b, LSX, gen_vvv, gen_helper_vpickev_b)
TRANS(vpickev_h, LSX, gen_vvv, gen_helper_vpickev_h)
TRANS(vpickev_w, LSX, gen_vvv, gen_helper_vpickev_w)
TRANS(vpickev_d, LSX, gen_vvv, gen_helper_vpickev_d)
TRANS(vpickod_b, LSX, gen_vvv, gen_helper_vpickod_b)
TRANS(vpickod_h, LSX, gen_vvv, gen_helper_vpickod_h)
TRANS(vpickod_w, LSX, gen_vvv, gen_helper_vpickod_w)
TRANS(vpickod_d, LSX, gen_vvv, gen_helper_vpickod_d)
TRANS(xvpickev_b, LASX, gen_xxx, gen_helper_vpickev_b)
TRANS(xvpickev_h, LASX, gen_xxx, gen_helper_vpickev_h)
TRANS(xvpickev_w, LASX, gen_xxx, gen_helper_vpickev_w)
TRANS(xvpickev_d, LASX, gen_xxx, gen_helper_vpickev_d)
TRANS(xvpickod_b, LASX, gen_xxx, gen_helper_vpickod_b)
TRANS(xvpickod_h, LASX, gen_xxx, gen_helper_vpickod_h)
TRANS(xvpickod_w, LASX, gen_xxx, gen_helper_vpickod_w)
TRANS(xvpickod_d, LASX, gen_xxx, gen_helper_vpickod_d)

TRANS(vilvl_b, LSX, gen_vvv, gen_helper_vilvl_b)
TRANS(vilvl_h, LSX, gen_vvv, gen_helper_vilvl_h)
TRANS(vilvl_w, LSX, gen_vvv, gen_helper_vilvl_w)
TRANS(vilvl_d, LSX, gen_vvv, gen_helper_vilvl_d)
TRANS(vilvh_b, LSX, gen_vvv, gen_helper_vilvh_b)
TRANS(vilvh_h, LSX, gen_vvv, gen_helper_vilvh_h)
TRANS(vilvh_w, LSX, gen_vvv, gen_helper_vilvh_w)
TRANS(vilvh_d, LSX, gen_vvv, gen_helper_vilvh_d)
TRANS(xvilvl_b, LASX, gen_xxx, gen_helper_vilvl_b)
TRANS(xvilvl_h, LASX, gen_xxx, gen_helper_vilvl_h)
TRANS(xvilvl_w, LASX, gen_xxx, gen_helper_vilvl_w)
TRANS(xvilvl_d, LASX, gen_xxx, gen_helper_vilvl_d)
TRANS(xvilvh_b, LASX, gen_xxx, gen_helper_vilvh_b)
TRANS(xvilvh_h, LASX, gen_xxx, gen_helper_vilvh_h)
TRANS(xvilvh_w, LASX, gen_xxx, gen_helper_vilvh_w)
TRANS(xvilvh_d, LASX, gen_xxx, gen_helper_vilvh_d)

TRANS(vshuf_b, LSX, gen_vvvv, gen_helper_vshuf_b)
TRANS(vshuf_h, LSX, gen_vvv, gen_helper_vshuf_h)
TRANS(vshuf_w, LSX, gen_vvv, gen_helper_vshuf_w)
TRANS(vshuf_d, LSX, gen_vvv, gen_helper_vshuf_d)
TRANS(xvshuf_b, LASX, gen_xxxx, gen_helper_vshuf_b)
TRANS(xvshuf_h, LASX, gen_xxx, gen_helper_vshuf_h)
TRANS(xvshuf_w, LASX, gen_xxx, gen_helper_vshuf_w)
TRANS(xvshuf_d, LASX, gen_xxx, gen_helper_vshuf_d)
TRANS(vshuf4i_b, LSX, gen_vv_i, gen_helper_vshuf4i_b)
TRANS(vshuf4i_h, LSX, gen_vv_i, gen_helper_vshuf4i_h)
TRANS(vshuf4i_w, LSX, gen_vv_i, gen_helper_vshuf4i_w)
TRANS(vshuf4i_d, LSX, gen_vv_i, gen_helper_vshuf4i_d)
TRANS(xvshuf4i_b, LASX, gen_xx_i, gen_helper_vshuf4i_b)
TRANS(xvshuf4i_h, LASX, gen_xx_i, gen_helper_vshuf4i_h)
TRANS(xvshuf4i_w, LASX, gen_xx_i, gen_helper_vshuf4i_w)
TRANS(xvshuf4i_d, LASX, gen_xx_i, gen_helper_vshuf4i_d)

TRANS(xvperm_w, LASX, gen_xxx, gen_helper_vperm_w)
TRANS(vpermi_w, LSX, gen_vv_i, gen_helper_vpermi_w)
TRANS(xvpermi_w, LASX, gen_xx_i, gen_helper_vpermi_w)
TRANS(xvpermi_d, LASX, gen_xx_i, gen_helper_vpermi_d)
TRANS(xvpermi_q, LASX, gen_xx_i, gen_helper_vpermi_q)

TRANS(vextrins_b, LSX, gen_vv_i, gen_helper_vextrins_b)
TRANS(vextrins_h, LSX, gen_vv_i, gen_helper_vextrins_h)
TRANS(vextrins_w, LSX, gen_vv_i, gen_helper_vextrins_w)
TRANS(vextrins_d, LSX, gen_vv_i, gen_helper_vextrins_d)
TRANS(xvextrins_b, LASX, gen_xx_i, gen_helper_vextrins_b)
TRANS(xvextrins_h, LASX, gen_xx_i, gen_helper_vextrins_h)
TRANS(xvextrins_w, LASX, gen_xx_i, gen_helper_vextrins_w)
TRANS(xvextrins_d, LASX, gen_xx_i, gen_helper_vextrins_d)

static bool trans_vld(DisasContext *ctx, arg_vr_i *a)
{
    TCGv addr;
    TCGv_i64 rl, rh;
    TCGv_i128 val;

    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i128();
    rl = tcg_temp_new_i64();
    rh = tcg_temp_new_i64();

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);
    tcg_gen_extr_i128_i64(rl, rh, val);
    set_vreg64(rh, a->vd, 1);
    set_vreg64(rl, a->vd, 0);

    return true;
}

static bool trans_vst(DisasContext *ctx, arg_vr_i *a)
{
    TCGv addr;
    TCGv_i128 val;
    TCGv_i64 ah, al;

    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i128();
    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();

    addr = make_address_i(ctx, addr, a->imm);

    get_vreg64(ah, a->vd, 1);
    get_vreg64(al, a->vd, 0);
    tcg_gen_concat_i64_i128(val, al, ah);
    tcg_gen_qemu_st_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);

    return true;
}

static bool trans_vldx(DisasContext *ctx, arg_vrr *a)
{
    TCGv addr, src1, src2;
    TCGv_i64 rl, rh;
    TCGv_i128 val;

    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    src1 = gpr_src(ctx, a->rj, EXT_NONE);
    src2 = gpr_src(ctx, a->rk, EXT_NONE);
    val = tcg_temp_new_i128();
    rl = tcg_temp_new_i64();
    rh = tcg_temp_new_i64();

    addr = make_address_x(ctx, src1, src2);
    tcg_gen_qemu_ld_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);
    tcg_gen_extr_i128_i64(rl, rh, val);
    set_vreg64(rh, a->vd, 1);
    set_vreg64(rl, a->vd, 0);

    return true;
}

static bool trans_vstx(DisasContext *ctx, arg_vrr *a)
{
    TCGv addr, src1, src2;
    TCGv_i64 ah, al;
    TCGv_i128 val;

    if (!avail_LSX(ctx)) {
        return false;
    }

    if (!check_vec(ctx, 16)) {
        return true;
    }

    src1 = gpr_src(ctx, a->rj, EXT_NONE);
    src2 = gpr_src(ctx, a->rk, EXT_NONE);
    val = tcg_temp_new_i128();
    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();

    addr = make_address_x(ctx, src1, src2);
    get_vreg64(ah, a->vd, 1);
    get_vreg64(al, a->vd, 0);
    tcg_gen_concat_i64_i128(val, al, ah);
    tcg_gen_qemu_st_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);

    return true;
}

static bool do_vldrepl_vl(DisasContext *ctx, arg_vr_i *a,
                          uint32_t oprsz, MemOp mop)
{
    TCGv addr;
    TCGv_i64 val;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i64();

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_i64(val, addr, ctx->mem_idx, mop);
    tcg_gen_gvec_dup_i64(mop, vec_full_offset(a->vd), oprsz, ctx->vl / 8, val);

    return true;
}

static bool do_vldrepl(DisasContext *ctx, arg_vr_i *a, MemOp mop)
{
    return do_vldrepl_vl(ctx, a, 16, mop);
}

static bool do_xvldrepl(DisasContext *ctx, arg_vr_i *a, MemOp mop)
{
    return do_vldrepl_vl(ctx, a, 32, mop);
}

TRANS(vldrepl_b, LSX, do_vldrepl, MO_8)
TRANS(vldrepl_h, LSX, do_vldrepl, MO_16)
TRANS(vldrepl_w, LSX, do_vldrepl, MO_32)
TRANS(vldrepl_d, LSX, do_vldrepl, MO_64)
TRANS(xvldrepl_b, LASX, do_xvldrepl, MO_8)
TRANS(xvldrepl_h, LASX, do_xvldrepl, MO_16)
TRANS(xvldrepl_w, LASX, do_xvldrepl, MO_32)
TRANS(xvldrepl_d, LASX, do_xvldrepl, MO_64)

static bool do_vstelm_vl(DisasContext *ctx,
                         arg_vr_ii *a, uint32_t oprsz, MemOp mop)
{
    TCGv addr;
    TCGv_i64 val;

    if (!check_vec(ctx, oprsz)) {
        return true;
    }

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i64();

    addr = make_address_i(ctx, addr, a->imm);
    tcg_gen_ld_i64(val, tcg_env, vec_reg_offset(a->vd, a->imm2, mop));
    tcg_gen_qemu_st_i64(val, addr, ctx->mem_idx, mop);
    return true;
}

static bool do_vstelm(DisasContext *ctx, arg_vr_ii *a, MemOp mop)
{
    return do_vstelm_vl(ctx, a, 16, mop);
}

static bool do_xvstelm(DisasContext *ctx, arg_vr_ii *a, MemOp mop)
{
    return do_vstelm_vl(ctx, a, 32, mop);
}

TRANS(vstelm_b, LSX, do_vstelm, MO_8)
TRANS(vstelm_h, LSX, do_vstelm, MO_16)
TRANS(vstelm_w, LSX, do_vstelm, MO_32)
TRANS(vstelm_d, LSX, do_vstelm, MO_64)
TRANS(xvstelm_b, LASX, do_xvstelm, MO_8)
TRANS(xvstelm_h, LASX, do_xvstelm, MO_16)
TRANS(xvstelm_w, LASX, do_xvstelm, MO_32)
TRANS(xvstelm_d, LASX, do_xvstelm, MO_64)

static bool gen_lasx_memory(DisasContext *ctx, arg_vr_i *a,
                            void (*func)(DisasContext *, int, TCGv))
{
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv temp = NULL;

    if (!check_vec(ctx, 32)) {
        return true;
    }

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    func(ctx, a->vd, addr);
    return true;
}

static void gen_xvld(DisasContext *ctx, int vreg, TCGv addr)
{
    int i;
    TCGv temp = tcg_temp_new();
    TCGv dest = tcg_temp_new();

    tcg_gen_qemu_ld_i64(dest, addr, ctx->mem_idx, MO_TEUQ);
    set_vreg64(dest, vreg, 0);

    for (i = 1; i < 4; i++) {
        tcg_gen_addi_tl(temp, addr, 8 * i);
        tcg_gen_qemu_ld_i64(dest, temp, ctx->mem_idx, MO_TEUQ);
        set_vreg64(dest, vreg, i);
    }
}

static void gen_xvst(DisasContext * ctx, int vreg, TCGv addr)
{
    int i;
    TCGv temp = tcg_temp_new();
    TCGv dest = tcg_temp_new();

    get_vreg64(dest, vreg, 0);
    tcg_gen_qemu_st_i64(dest, addr, ctx->mem_idx, MO_TEUQ);

    for (i = 1; i < 4; i++) {
        tcg_gen_addi_tl(temp, addr, 8 * i);
        get_vreg64(dest, vreg, i);
        tcg_gen_qemu_st_i64(dest, temp, ctx->mem_idx, MO_TEUQ);
    }
}

TRANS(xvld, LASX, gen_lasx_memory, gen_xvld)
TRANS(xvst, LASX, gen_lasx_memory, gen_xvst)

static bool gen_lasx_memoryx(DisasContext *ctx, arg_vrr *a,
                             void (*func)(DisasContext*, int, TCGv))
{
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = tcg_temp_new();

    if (!check_vec(ctx, 32)) {
        return true;
    }

    tcg_gen_add_tl(addr, src1, src2);
    func(ctx, a->vd, addr);

    return true;
}

TRANS(xvldx, LASX, gen_lasx_memoryx, gen_xvld)
TRANS(xvstx, LASX, gen_lasx_memoryx, gen_xvst)
