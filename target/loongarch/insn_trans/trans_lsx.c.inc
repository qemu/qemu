/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LSX translate functions
 * Copyright (c) 2022-2023 Loongson Technology Corporation Limited
 */

#ifndef CONFIG_USER_ONLY
#define CHECK_SXE do { \
    if ((ctx->base.tb->flags & HW_FLAGS_EUEN_SXE) == 0) { \
        generate_exception(ctx, EXCCODE_SXD); \
        return true; \
    } \
} while (0)
#else
#define CHECK_SXE
#endif

static bool gen_vvvv(DisasContext *ctx, arg_vvvv *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32,
                                  TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);
    TCGv_i32 va = tcg_constant_i32(a->va);

    CHECK_SXE;
    func(cpu_env, vd, vj, vk, va);
    return true;
}

static bool gen_vvv(DisasContext *ctx, arg_vvv *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    CHECK_SXE;

    func(cpu_env, vd, vj, vk);
    return true;
}

static bool gen_vv(DisasContext *ctx, arg_vv *a,
                   void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);

    CHECK_SXE;
    func(cpu_env, vd, vj);
    return true;
}

static bool gen_vv_i(DisasContext *ctx, arg_vv_i *a,
                     void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 imm = tcg_constant_i32(a->imm);

    CHECK_SXE;
    func(cpu_env, vd, vj, imm);
    return true;
}

static bool gen_cv(DisasContext *ctx, arg_cv *a,
                    void (*func)(TCGv_ptr, TCGv_i32, TCGv_i32))
{
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 cd = tcg_constant_i32(a->cd);

    CHECK_SXE;
    func(cpu_env, cd, vj);
    return true;
}

static bool gvec_vvv(DisasContext *ctx, arg_vvv *a, MemOp mop,
                     void (*func)(unsigned, uint32_t, uint32_t,
                                  uint32_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);
    vk_ofs = vec_full_offset(a->vk);

    func(mop, vd_ofs, vj_ofs, vk_ofs, 16, ctx->vl/8);
    return true;
}

static bool gvec_vv(DisasContext *ctx, arg_vv *a, MemOp mop,
                    void (*func)(unsigned, uint32_t, uint32_t,
                                 uint32_t, uint32_t))
{
    uint32_t vd_ofs, vj_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);

    func(mop, vd_ofs, vj_ofs, 16, ctx->vl/8);
    return true;
}

static bool gvec_vv_i(DisasContext *ctx, arg_vv_i *a, MemOp mop,
                      void (*func)(unsigned, uint32_t, uint32_t,
                                   int64_t, uint32_t, uint32_t))
{
    uint32_t vd_ofs, vj_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);

    func(mop, vd_ofs, vj_ofs, a->imm , 16, ctx->vl/8);
    return true;
}

static bool gvec_subi(DisasContext *ctx, arg_vv_i *a, MemOp mop)
{
    uint32_t vd_ofs, vj_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);

    tcg_gen_gvec_addi(mop, vd_ofs, vj_ofs, -a->imm, 16, ctx->vl/8);
    return true;
}

TRANS(vadd_b, gvec_vvv, MO_8, tcg_gen_gvec_add)
TRANS(vadd_h, gvec_vvv, MO_16, tcg_gen_gvec_add)
TRANS(vadd_w, gvec_vvv, MO_32, tcg_gen_gvec_add)
TRANS(vadd_d, gvec_vvv, MO_64, tcg_gen_gvec_add)

#define VADDSUB_Q(NAME)                                        \
static bool trans_v## NAME ##_q(DisasContext *ctx, arg_vvv *a) \
{                                                              \
    TCGv_i64 rh, rl, ah, al, bh, bl;                           \
                                                               \
    CHECK_SXE;                                                 \
                                                               \
    rh = tcg_temp_new_i64();                                   \
    rl = tcg_temp_new_i64();                                   \
    ah = tcg_temp_new_i64();                                   \
    al = tcg_temp_new_i64();                                   \
    bh = tcg_temp_new_i64();                                   \
    bl = tcg_temp_new_i64();                                   \
                                                               \
    get_vreg64(ah, a->vj, 1);                                  \
    get_vreg64(al, a->vj, 0);                                  \
    get_vreg64(bh, a->vk, 1);                                  \
    get_vreg64(bl, a->vk, 0);                                  \
                                                               \
    tcg_gen_## NAME ##2_i64(rl, rh, al, ah, bl, bh);           \
                                                               \
    set_vreg64(rh, a->vd, 1);                                  \
    set_vreg64(rl, a->vd, 0);                                  \
                                                               \
    return true;                                               \
}

VADDSUB_Q(add)
VADDSUB_Q(sub)

TRANS(vsub_b, gvec_vvv, MO_8, tcg_gen_gvec_sub)
TRANS(vsub_h, gvec_vvv, MO_16, tcg_gen_gvec_sub)
TRANS(vsub_w, gvec_vvv, MO_32, tcg_gen_gvec_sub)
TRANS(vsub_d, gvec_vvv, MO_64, tcg_gen_gvec_sub)

TRANS(vaddi_bu, gvec_vv_i, MO_8, tcg_gen_gvec_addi)
TRANS(vaddi_hu, gvec_vv_i, MO_16, tcg_gen_gvec_addi)
TRANS(vaddi_wu, gvec_vv_i, MO_32, tcg_gen_gvec_addi)
TRANS(vaddi_du, gvec_vv_i, MO_64, tcg_gen_gvec_addi)
TRANS(vsubi_bu, gvec_subi, MO_8)
TRANS(vsubi_hu, gvec_subi, MO_16)
TRANS(vsubi_wu, gvec_subi, MO_32)
TRANS(vsubi_du, gvec_subi, MO_64)

TRANS(vneg_b, gvec_vv, MO_8, tcg_gen_gvec_neg)
TRANS(vneg_h, gvec_vv, MO_16, tcg_gen_gvec_neg)
TRANS(vneg_w, gvec_vv, MO_32, tcg_gen_gvec_neg)
TRANS(vneg_d, gvec_vv, MO_64, tcg_gen_gvec_neg)

TRANS(vsadd_b, gvec_vvv, MO_8, tcg_gen_gvec_ssadd)
TRANS(vsadd_h, gvec_vvv, MO_16, tcg_gen_gvec_ssadd)
TRANS(vsadd_w, gvec_vvv, MO_32, tcg_gen_gvec_ssadd)
TRANS(vsadd_d, gvec_vvv, MO_64, tcg_gen_gvec_ssadd)
TRANS(vsadd_bu, gvec_vvv, MO_8, tcg_gen_gvec_usadd)
TRANS(vsadd_hu, gvec_vvv, MO_16, tcg_gen_gvec_usadd)
TRANS(vsadd_wu, gvec_vvv, MO_32, tcg_gen_gvec_usadd)
TRANS(vsadd_du, gvec_vvv, MO_64, tcg_gen_gvec_usadd)
TRANS(vssub_b, gvec_vvv, MO_8, tcg_gen_gvec_sssub)
TRANS(vssub_h, gvec_vvv, MO_16, tcg_gen_gvec_sssub)
TRANS(vssub_w, gvec_vvv, MO_32, tcg_gen_gvec_sssub)
TRANS(vssub_d, gvec_vvv, MO_64, tcg_gen_gvec_sssub)
TRANS(vssub_bu, gvec_vvv, MO_8, tcg_gen_gvec_ussub)
TRANS(vssub_hu, gvec_vvv, MO_16, tcg_gen_gvec_ussub)
TRANS(vssub_wu, gvec_vvv, MO_32, tcg_gen_gvec_ussub)
TRANS(vssub_du, gvec_vvv, MO_64, tcg_gen_gvec_ussub)

TRANS(vhaddw_h_b, gen_vvv, gen_helper_vhaddw_h_b)
TRANS(vhaddw_w_h, gen_vvv, gen_helper_vhaddw_w_h)
TRANS(vhaddw_d_w, gen_vvv, gen_helper_vhaddw_d_w)
TRANS(vhaddw_q_d, gen_vvv, gen_helper_vhaddw_q_d)
TRANS(vhaddw_hu_bu, gen_vvv, gen_helper_vhaddw_hu_bu)
TRANS(vhaddw_wu_hu, gen_vvv, gen_helper_vhaddw_wu_hu)
TRANS(vhaddw_du_wu, gen_vvv, gen_helper_vhaddw_du_wu)
TRANS(vhaddw_qu_du, gen_vvv, gen_helper_vhaddw_qu_du)
TRANS(vhsubw_h_b, gen_vvv, gen_helper_vhsubw_h_b)
TRANS(vhsubw_w_h, gen_vvv, gen_helper_vhsubw_w_h)
TRANS(vhsubw_d_w, gen_vvv, gen_helper_vhsubw_d_w)
TRANS(vhsubw_q_d, gen_vvv, gen_helper_vhsubw_q_d)
TRANS(vhsubw_hu_bu, gen_vvv, gen_helper_vhsubw_hu_bu)
TRANS(vhsubw_wu_hu, gen_vvv, gen_helper_vhsubw_wu_hu)
TRANS(vhsubw_du_wu, gen_vvv, gen_helper_vhsubw_du_wu)
TRANS(vhsubw_qu_du, gen_vvv, gen_helper_vhsubw_qu_du)

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

TRANS(vaddwev_h_b, gvec_vvv, MO_8, do_vaddwev_s)
TRANS(vaddwev_w_h, gvec_vvv, MO_16, do_vaddwev_s)
TRANS(vaddwev_d_w, gvec_vvv, MO_32, do_vaddwev_s)
TRANS(vaddwev_q_d, gvec_vvv, MO_64, do_vaddwev_s)

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

TRANS(vaddwod_h_b, gvec_vvv, MO_8, do_vaddwod_s)
TRANS(vaddwod_w_h, gvec_vvv, MO_16, do_vaddwod_s)
TRANS(vaddwod_d_w, gvec_vvv, MO_32, do_vaddwod_s)
TRANS(vaddwod_q_d, gvec_vvv, MO_64, do_vaddwod_s)

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

TRANS(vsubwev_h_b, gvec_vvv, MO_8, do_vsubwev_s)
TRANS(vsubwev_w_h, gvec_vvv, MO_16, do_vsubwev_s)
TRANS(vsubwev_d_w, gvec_vvv, MO_32, do_vsubwev_s)
TRANS(vsubwev_q_d, gvec_vvv, MO_64, do_vsubwev_s)

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

TRANS(vsubwod_h_b, gvec_vvv, MO_8, do_vsubwod_s)
TRANS(vsubwod_w_h, gvec_vvv, MO_16, do_vsubwod_s)
TRANS(vsubwod_d_w, gvec_vvv, MO_32, do_vsubwod_s)
TRANS(vsubwod_q_d, gvec_vvv, MO_64, do_vsubwod_s)

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

TRANS(vaddwev_h_bu, gvec_vvv, MO_8, do_vaddwev_u)
TRANS(vaddwev_w_hu, gvec_vvv, MO_16, do_vaddwev_u)
TRANS(vaddwev_d_wu, gvec_vvv, MO_32, do_vaddwev_u)
TRANS(vaddwev_q_du, gvec_vvv, MO_64, do_vaddwev_u)

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

TRANS(vaddwod_h_bu, gvec_vvv, MO_8, do_vaddwod_u)
TRANS(vaddwod_w_hu, gvec_vvv, MO_16, do_vaddwod_u)
TRANS(vaddwod_d_wu, gvec_vvv, MO_32, do_vaddwod_u)
TRANS(vaddwod_q_du, gvec_vvv, MO_64, do_vaddwod_u)

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

TRANS(vsubwev_h_bu, gvec_vvv, MO_8, do_vsubwev_u)
TRANS(vsubwev_w_hu, gvec_vvv, MO_16, do_vsubwev_u)
TRANS(vsubwev_d_wu, gvec_vvv, MO_32, do_vsubwev_u)
TRANS(vsubwev_q_du, gvec_vvv, MO_64, do_vsubwev_u)

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

TRANS(vsubwod_h_bu, gvec_vvv, MO_8, do_vsubwod_u)
TRANS(vsubwod_w_hu, gvec_vvv, MO_16, do_vsubwod_u)
TRANS(vsubwod_d_wu, gvec_vvv, MO_32, do_vsubwod_u)
TRANS(vsubwod_q_du, gvec_vvv, MO_64, do_vsubwod_u)

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

TRANS(vaddwev_h_bu_b, gvec_vvv, MO_8, do_vaddwev_u_s)
TRANS(vaddwev_w_hu_h, gvec_vvv, MO_16, do_vaddwev_u_s)
TRANS(vaddwev_d_wu_w, gvec_vvv, MO_32, do_vaddwev_u_s)
TRANS(vaddwev_q_du_d, gvec_vvv, MO_64, do_vaddwev_u_s)

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

TRANS(vaddwod_h_bu_b, gvec_vvv, MO_8, do_vaddwod_u_s)
TRANS(vaddwod_w_hu_h, gvec_vvv, MO_16, do_vaddwod_u_s)
TRANS(vaddwod_d_wu_w, gvec_vvv, MO_32, do_vaddwod_u_s)
TRANS(vaddwod_q_du_d, gvec_vvv, MO_64, do_vaddwod_u_s)

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

TRANS(vavg_b, gvec_vvv, MO_8, do_vavg_s)
TRANS(vavg_h, gvec_vvv, MO_16, do_vavg_s)
TRANS(vavg_w, gvec_vvv, MO_32, do_vavg_s)
TRANS(vavg_d, gvec_vvv, MO_64, do_vavg_s)
TRANS(vavg_bu, gvec_vvv, MO_8, do_vavg_u)
TRANS(vavg_hu, gvec_vvv, MO_16, do_vavg_u)
TRANS(vavg_wu, gvec_vvv, MO_32, do_vavg_u)
TRANS(vavg_du, gvec_vvv, MO_64, do_vavg_u)

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

TRANS(vavgr_b, gvec_vvv, MO_8, do_vavgr_s)
TRANS(vavgr_h, gvec_vvv, MO_16, do_vavgr_s)
TRANS(vavgr_w, gvec_vvv, MO_32, do_vavgr_s)
TRANS(vavgr_d, gvec_vvv, MO_64, do_vavgr_s)
TRANS(vavgr_bu, gvec_vvv, MO_8, do_vavgr_u)
TRANS(vavgr_hu, gvec_vvv, MO_16, do_vavgr_u)
TRANS(vavgr_wu, gvec_vvv, MO_32, do_vavgr_u)
TRANS(vavgr_du, gvec_vvv, MO_64, do_vavgr_u)

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

TRANS(vabsd_b, gvec_vvv, MO_8, do_vabsd_s)
TRANS(vabsd_h, gvec_vvv, MO_16, do_vabsd_s)
TRANS(vabsd_w, gvec_vvv, MO_32, do_vabsd_s)
TRANS(vabsd_d, gvec_vvv, MO_64, do_vabsd_s)
TRANS(vabsd_bu, gvec_vvv, MO_8, do_vabsd_u)
TRANS(vabsd_hu, gvec_vvv, MO_16, do_vabsd_u)
TRANS(vabsd_wu, gvec_vvv, MO_32, do_vabsd_u)
TRANS(vabsd_du, gvec_vvv, MO_64, do_vabsd_u)

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

TRANS(vadda_b, gvec_vvv, MO_8, do_vadda)
TRANS(vadda_h, gvec_vvv, MO_16, do_vadda)
TRANS(vadda_w, gvec_vvv, MO_32, do_vadda)
TRANS(vadda_d, gvec_vvv, MO_64, do_vadda)

TRANS(vmax_b, gvec_vvv, MO_8, tcg_gen_gvec_smax)
TRANS(vmax_h, gvec_vvv, MO_16, tcg_gen_gvec_smax)
TRANS(vmax_w, gvec_vvv, MO_32, tcg_gen_gvec_smax)
TRANS(vmax_d, gvec_vvv, MO_64, tcg_gen_gvec_smax)
TRANS(vmax_bu, gvec_vvv, MO_8, tcg_gen_gvec_umax)
TRANS(vmax_hu, gvec_vvv, MO_16, tcg_gen_gvec_umax)
TRANS(vmax_wu, gvec_vvv, MO_32, tcg_gen_gvec_umax)
TRANS(vmax_du, gvec_vvv, MO_64, tcg_gen_gvec_umax)

TRANS(vmin_b, gvec_vvv, MO_8, tcg_gen_gvec_smin)
TRANS(vmin_h, gvec_vvv, MO_16, tcg_gen_gvec_smin)
TRANS(vmin_w, gvec_vvv, MO_32, tcg_gen_gvec_smin)
TRANS(vmin_d, gvec_vvv, MO_64, tcg_gen_gvec_smin)
TRANS(vmin_bu, gvec_vvv, MO_8, tcg_gen_gvec_umin)
TRANS(vmin_hu, gvec_vvv, MO_16, tcg_gen_gvec_umin)
TRANS(vmin_wu, gvec_vvv, MO_32, tcg_gen_gvec_umin)
TRANS(vmin_du, gvec_vvv, MO_64, tcg_gen_gvec_umin)

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

TRANS(vmini_b, gvec_vv_i, MO_8, do_vmini_s)
TRANS(vmini_h, gvec_vv_i, MO_16, do_vmini_s)
TRANS(vmini_w, gvec_vv_i, MO_32, do_vmini_s)
TRANS(vmini_d, gvec_vv_i, MO_64, do_vmini_s)
TRANS(vmini_bu, gvec_vv_i, MO_8, do_vmini_u)
TRANS(vmini_hu, gvec_vv_i, MO_16, do_vmini_u)
TRANS(vmini_wu, gvec_vv_i, MO_32, do_vmini_u)
TRANS(vmini_du, gvec_vv_i, MO_64, do_vmini_u)

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

TRANS(vmaxi_b, gvec_vv_i, MO_8, do_vmaxi_s)
TRANS(vmaxi_h, gvec_vv_i, MO_16, do_vmaxi_s)
TRANS(vmaxi_w, gvec_vv_i, MO_32, do_vmaxi_s)
TRANS(vmaxi_d, gvec_vv_i, MO_64, do_vmaxi_s)
TRANS(vmaxi_bu, gvec_vv_i, MO_8, do_vmaxi_u)
TRANS(vmaxi_hu, gvec_vv_i, MO_16, do_vmaxi_u)
TRANS(vmaxi_wu, gvec_vv_i, MO_32, do_vmaxi_u)
TRANS(vmaxi_du, gvec_vv_i, MO_64, do_vmaxi_u)

TRANS(vmul_b, gvec_vvv, MO_8, tcg_gen_gvec_mul)
TRANS(vmul_h, gvec_vvv, MO_16, tcg_gen_gvec_mul)
TRANS(vmul_w, gvec_vvv, MO_32, tcg_gen_gvec_mul)
TRANS(vmul_d, gvec_vvv, MO_64, tcg_gen_gvec_mul)

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

TRANS(vmuh_b, gvec_vvv, MO_8, do_vmuh_s)
TRANS(vmuh_h, gvec_vvv, MO_16, do_vmuh_s)
TRANS(vmuh_w, gvec_vvv, MO_32, do_vmuh_s)
TRANS(vmuh_d, gvec_vvv, MO_64, do_vmuh_s)

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

TRANS(vmuh_bu, gvec_vvv, MO_8,  do_vmuh_u)
TRANS(vmuh_hu, gvec_vvv, MO_16, do_vmuh_u)
TRANS(vmuh_wu, gvec_vvv, MO_32, do_vmuh_u)
TRANS(vmuh_du, gvec_vvv, MO_64, do_vmuh_u)

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

TRANS(vmulwev_h_b, gvec_vvv, MO_8, do_vmulwev_s)
TRANS(vmulwev_w_h, gvec_vvv, MO_16, do_vmulwev_s)
TRANS(vmulwev_d_w, gvec_vvv, MO_32, do_vmulwev_s)

static void tcg_gen_mulus2_i64(TCGv_i64 rl, TCGv_i64 rh,
                               TCGv_i64 arg1, TCGv_i64 arg2)
{
    tcg_gen_mulsu2_i64(rl, rh, arg2, arg1);
}

#define VMUL_Q(NAME, FN, idx1, idx2)                      \
static bool trans_## NAME (DisasContext *ctx, arg_vvv *a) \
{                                                         \
    TCGv_i64 rh, rl, arg1, arg2;                          \
                                                          \
    rh = tcg_temp_new_i64();                              \
    rl = tcg_temp_new_i64();                              \
    arg1 = tcg_temp_new_i64();                            \
    arg2 = tcg_temp_new_i64();                            \
                                                          \
    get_vreg64(arg1, a->vj, idx1);                        \
    get_vreg64(arg2, a->vk, idx2);                        \
                                                          \
    tcg_gen_## FN ##_i64(rl, rh, arg1, arg2);             \
                                                          \
    set_vreg64(rh, a->vd, 1);                             \
    set_vreg64(rl, a->vd, 0);                             \
                                                          \
    return true;                                          \
}

VMUL_Q(vmulwev_q_d, muls2, 0, 0)
VMUL_Q(vmulwod_q_d, muls2, 1, 1)
VMUL_Q(vmulwev_q_du, mulu2, 0, 0)
VMUL_Q(vmulwod_q_du, mulu2, 1, 1)
VMUL_Q(vmulwev_q_du_d, mulus2, 0, 0)
VMUL_Q(vmulwod_q_du_d, mulus2, 1, 1)

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

TRANS(vmulwod_h_b, gvec_vvv, MO_8, do_vmulwod_s)
TRANS(vmulwod_w_h, gvec_vvv, MO_16, do_vmulwod_s)
TRANS(vmulwod_d_w, gvec_vvv, MO_32, do_vmulwod_s)

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

TRANS(vmulwev_h_bu, gvec_vvv, MO_8, do_vmulwev_u)
TRANS(vmulwev_w_hu, gvec_vvv, MO_16, do_vmulwev_u)
TRANS(vmulwev_d_wu, gvec_vvv, MO_32, do_vmulwev_u)

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

TRANS(vmulwod_h_bu, gvec_vvv, MO_8, do_vmulwod_u)
TRANS(vmulwod_w_hu, gvec_vvv, MO_16, do_vmulwod_u)
TRANS(vmulwod_d_wu, gvec_vvv, MO_32, do_vmulwod_u)

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

TRANS(vmulwev_h_bu_b, gvec_vvv, MO_8, do_vmulwev_u_s)
TRANS(vmulwev_w_hu_h, gvec_vvv, MO_16, do_vmulwev_u_s)
TRANS(vmulwev_d_wu_w, gvec_vvv, MO_32, do_vmulwev_u_s)

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

TRANS(vmulwod_h_bu_b, gvec_vvv, MO_8, do_vmulwod_u_s)
TRANS(vmulwod_w_hu_h, gvec_vvv, MO_16, do_vmulwod_u_s)
TRANS(vmulwod_d_wu_w, gvec_vvv, MO_32, do_vmulwod_u_s)

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

TRANS(vmadd_b, gvec_vvv, MO_8, do_vmadd)
TRANS(vmadd_h, gvec_vvv, MO_16, do_vmadd)
TRANS(vmadd_w, gvec_vvv, MO_32, do_vmadd)
TRANS(vmadd_d, gvec_vvv, MO_64, do_vmadd)

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

TRANS(vmsub_b, gvec_vvv, MO_8, do_vmsub)
TRANS(vmsub_h, gvec_vvv, MO_16, do_vmsub)
TRANS(vmsub_w, gvec_vvv, MO_32, do_vmsub)
TRANS(vmsub_d, gvec_vvv, MO_64, do_vmsub)

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

TRANS(vmaddwev_h_b, gvec_vvv, MO_8, do_vmaddwev_s)
TRANS(vmaddwev_w_h, gvec_vvv, MO_16, do_vmaddwev_s)
TRANS(vmaddwev_d_w, gvec_vvv, MO_32, do_vmaddwev_s)

#define VMADD_Q(NAME, FN, idx1, idx2)                     \
static bool trans_## NAME (DisasContext *ctx, arg_vvv *a) \
{                                                         \
    TCGv_i64 rh, rl, arg1, arg2, th, tl;                  \
                                                          \
    rh = tcg_temp_new_i64();                              \
    rl = tcg_temp_new_i64();                              \
    arg1 = tcg_temp_new_i64();                            \
    arg2 = tcg_temp_new_i64();                            \
    th = tcg_temp_new_i64();                              \
    tl = tcg_temp_new_i64();                              \
                                                          \
    get_vreg64(arg1, a->vj, idx1);                        \
    get_vreg64(arg2, a->vk, idx2);                        \
    get_vreg64(rh, a->vd, 1);                             \
    get_vreg64(rl, a->vd, 0);                             \
                                                          \
    tcg_gen_## FN ##_i64(tl, th, arg1, arg2);             \
    tcg_gen_add2_i64(rl, rh, rl, rh, tl, th);             \
                                                          \
    set_vreg64(rh, a->vd, 1);                             \
    set_vreg64(rl, a->vd, 0);                             \
                                                          \
    return true;                                          \
}

VMADD_Q(vmaddwev_q_d, muls2, 0, 0)
VMADD_Q(vmaddwod_q_d, muls2, 1, 1)
VMADD_Q(vmaddwev_q_du, mulu2, 0, 0)
VMADD_Q(vmaddwod_q_du, mulu2, 1, 1)
VMADD_Q(vmaddwev_q_du_d, mulus2, 0, 0)
VMADD_Q(vmaddwod_q_du_d, mulus2, 1, 1)

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

TRANS(vmaddwod_h_b, gvec_vvv, MO_8, do_vmaddwod_s)
TRANS(vmaddwod_w_h, gvec_vvv, MO_16, do_vmaddwod_s)
TRANS(vmaddwod_d_w, gvec_vvv, MO_32, do_vmaddwod_s)

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

TRANS(vmaddwev_h_bu, gvec_vvv, MO_8, do_vmaddwev_u)
TRANS(vmaddwev_w_hu, gvec_vvv, MO_16, do_vmaddwev_u)
TRANS(vmaddwev_d_wu, gvec_vvv, MO_32, do_vmaddwev_u)

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

TRANS(vmaddwod_h_bu, gvec_vvv, MO_8, do_vmaddwod_u)
TRANS(vmaddwod_w_hu, gvec_vvv, MO_16, do_vmaddwod_u)
TRANS(vmaddwod_d_wu, gvec_vvv, MO_32, do_vmaddwod_u)

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

TRANS(vmaddwev_h_bu_b, gvec_vvv, MO_8, do_vmaddwev_u_s)
TRANS(vmaddwev_w_hu_h, gvec_vvv, MO_16, do_vmaddwev_u_s)
TRANS(vmaddwev_d_wu_w, gvec_vvv, MO_32, do_vmaddwev_u_s)

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

TRANS(vmaddwod_h_bu_b, gvec_vvv, MO_8, do_vmaddwod_u_s)
TRANS(vmaddwod_w_hu_h, gvec_vvv, MO_16, do_vmaddwod_u_s)
TRANS(vmaddwod_d_wu_w, gvec_vvv, MO_32, do_vmaddwod_u_s)

TRANS(vdiv_b, gen_vvv, gen_helper_vdiv_b)
TRANS(vdiv_h, gen_vvv, gen_helper_vdiv_h)
TRANS(vdiv_w, gen_vvv, gen_helper_vdiv_w)
TRANS(vdiv_d, gen_vvv, gen_helper_vdiv_d)
TRANS(vdiv_bu, gen_vvv, gen_helper_vdiv_bu)
TRANS(vdiv_hu, gen_vvv, gen_helper_vdiv_hu)
TRANS(vdiv_wu, gen_vvv, gen_helper_vdiv_wu)
TRANS(vdiv_du, gen_vvv, gen_helper_vdiv_du)
TRANS(vmod_b, gen_vvv, gen_helper_vmod_b)
TRANS(vmod_h, gen_vvv, gen_helper_vmod_h)
TRANS(vmod_w, gen_vvv, gen_helper_vmod_w)
TRANS(vmod_d, gen_vvv, gen_helper_vmod_d)
TRANS(vmod_bu, gen_vvv, gen_helper_vmod_bu)
TRANS(vmod_hu, gen_vvv, gen_helper_vmod_hu)
TRANS(vmod_wu, gen_vvv, gen_helper_vmod_wu)
TRANS(vmod_du, gen_vvv, gen_helper_vmod_du)

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

TRANS(vsat_b, gvec_vv_i, MO_8, do_vsat_s)
TRANS(vsat_h, gvec_vv_i, MO_16, do_vsat_s)
TRANS(vsat_w, gvec_vv_i, MO_32, do_vsat_s)
TRANS(vsat_d, gvec_vv_i, MO_64, do_vsat_s)

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

TRANS(vsat_bu, gvec_vv_i, MO_8, do_vsat_u)
TRANS(vsat_hu, gvec_vv_i, MO_16, do_vsat_u)
TRANS(vsat_wu, gvec_vv_i, MO_32, do_vsat_u)
TRANS(vsat_du, gvec_vv_i, MO_64, do_vsat_u)

TRANS(vexth_h_b, gen_vv, gen_helper_vexth_h_b)
TRANS(vexth_w_h, gen_vv, gen_helper_vexth_w_h)
TRANS(vexth_d_w, gen_vv, gen_helper_vexth_d_w)
TRANS(vexth_q_d, gen_vv, gen_helper_vexth_q_d)
TRANS(vexth_hu_bu, gen_vv, gen_helper_vexth_hu_bu)
TRANS(vexth_wu_hu, gen_vv, gen_helper_vexth_wu_hu)
TRANS(vexth_du_wu, gen_vv, gen_helper_vexth_du_wu)
TRANS(vexth_qu_du, gen_vv, gen_helper_vexth_qu_du)

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

TRANS(vsigncov_b, gvec_vvv, MO_8, do_vsigncov)
TRANS(vsigncov_h, gvec_vvv, MO_16, do_vsigncov)
TRANS(vsigncov_w, gvec_vvv, MO_32, do_vsigncov)
TRANS(vsigncov_d, gvec_vvv, MO_64, do_vsigncov)

TRANS(vmskltz_b, gen_vv, gen_helper_vmskltz_b)
TRANS(vmskltz_h, gen_vv, gen_helper_vmskltz_h)
TRANS(vmskltz_w, gen_vv, gen_helper_vmskltz_w)
TRANS(vmskltz_d, gen_vv, gen_helper_vmskltz_d)
TRANS(vmskgez_b, gen_vv, gen_helper_vmskgez_b)
TRANS(vmsknz_b, gen_vv, gen_helper_vmsknz_b)

#define EXPAND_BYTE(bit)  ((uint64_t)(bit ? 0xff : 0))

static uint64_t vldi_get_value(DisasContext *ctx, uint32_t imm)
{
    int mode;
    uint64_t data, t;

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
        data = (t << 24) | (t << 8);
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
        generate_exception(ctx, EXCCODE_INE);
        g_assert_not_reached();
    }
    return data;
}

static bool trans_vldi(DisasContext *ctx, arg_vldi *a)
{
    int sel, vece;
    uint64_t value;
    CHECK_SXE;

    sel = (a->imm >> 12) & 0x1;

    if (sel) {
        value = vldi_get_value(ctx, a->imm);
        vece = MO_64;
    } else {
        value = ((int32_t)(a->imm << 22)) >> 22;
        vece = (a->imm >> 10) & 0x3;
    }

    tcg_gen_gvec_dup_i64(vece, vec_full_offset(a->vd), 16, ctx->vl/8,
                         tcg_constant_i64(value));
    return true;
}

TRANS(vand_v, gvec_vvv, MO_64, tcg_gen_gvec_and)
TRANS(vor_v, gvec_vvv, MO_64, tcg_gen_gvec_or)
TRANS(vxor_v, gvec_vvv, MO_64, tcg_gen_gvec_xor)
TRANS(vnor_v, gvec_vvv, MO_64, tcg_gen_gvec_nor)

static bool trans_vandn_v(DisasContext *ctx, arg_vvv *a)
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);
    vk_ofs = vec_full_offset(a->vk);

    tcg_gen_gvec_andc(MO_64, vd_ofs, vk_ofs, vj_ofs, 16, ctx->vl/8);
    return true;
}
TRANS(vorn_v, gvec_vvv, MO_64, tcg_gen_gvec_orc)
TRANS(vandi_b, gvec_vv_i, MO_8, tcg_gen_gvec_andi)
TRANS(vori_b, gvec_vv_i, MO_8, tcg_gen_gvec_ori)
TRANS(vxori_b, gvec_vv_i, MO_8, tcg_gen_gvec_xori)

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

TRANS(vnori_b, gvec_vv_i, MO_8, do_vnori_b)

TRANS(vsll_b, gvec_vvv, MO_8, tcg_gen_gvec_shlv)
TRANS(vsll_h, gvec_vvv, MO_16, tcg_gen_gvec_shlv)
TRANS(vsll_w, gvec_vvv, MO_32, tcg_gen_gvec_shlv)
TRANS(vsll_d, gvec_vvv, MO_64, tcg_gen_gvec_shlv)
TRANS(vslli_b, gvec_vv_i, MO_8, tcg_gen_gvec_shli)
TRANS(vslli_h, gvec_vv_i, MO_16, tcg_gen_gvec_shli)
TRANS(vslli_w, gvec_vv_i, MO_32, tcg_gen_gvec_shli)
TRANS(vslli_d, gvec_vv_i, MO_64, tcg_gen_gvec_shli)

TRANS(vsrl_b, gvec_vvv, MO_8, tcg_gen_gvec_shrv)
TRANS(vsrl_h, gvec_vvv, MO_16, tcg_gen_gvec_shrv)
TRANS(vsrl_w, gvec_vvv, MO_32, tcg_gen_gvec_shrv)
TRANS(vsrl_d, gvec_vvv, MO_64, tcg_gen_gvec_shrv)
TRANS(vsrli_b, gvec_vv_i, MO_8, tcg_gen_gvec_shri)
TRANS(vsrli_h, gvec_vv_i, MO_16, tcg_gen_gvec_shri)
TRANS(vsrli_w, gvec_vv_i, MO_32, tcg_gen_gvec_shri)
TRANS(vsrli_d, gvec_vv_i, MO_64, tcg_gen_gvec_shri)

TRANS(vsra_b, gvec_vvv, MO_8, tcg_gen_gvec_sarv)
TRANS(vsra_h, gvec_vvv, MO_16, tcg_gen_gvec_sarv)
TRANS(vsra_w, gvec_vvv, MO_32, tcg_gen_gvec_sarv)
TRANS(vsra_d, gvec_vvv, MO_64, tcg_gen_gvec_sarv)
TRANS(vsrai_b, gvec_vv_i, MO_8, tcg_gen_gvec_sari)
TRANS(vsrai_h, gvec_vv_i, MO_16, tcg_gen_gvec_sari)
TRANS(vsrai_w, gvec_vv_i, MO_32, tcg_gen_gvec_sari)
TRANS(vsrai_d, gvec_vv_i, MO_64, tcg_gen_gvec_sari)

TRANS(vrotr_b, gvec_vvv, MO_8, tcg_gen_gvec_rotrv)
TRANS(vrotr_h, gvec_vvv, MO_16, tcg_gen_gvec_rotrv)
TRANS(vrotr_w, gvec_vvv, MO_32, tcg_gen_gvec_rotrv)
TRANS(vrotr_d, gvec_vvv, MO_64, tcg_gen_gvec_rotrv)
TRANS(vrotri_b, gvec_vv_i, MO_8, tcg_gen_gvec_rotri)
TRANS(vrotri_h, gvec_vv_i, MO_16, tcg_gen_gvec_rotri)
TRANS(vrotri_w, gvec_vv_i, MO_32, tcg_gen_gvec_rotri)
TRANS(vrotri_d, gvec_vv_i, MO_64, tcg_gen_gvec_rotri)

TRANS(vsllwil_h_b, gen_vv_i, gen_helper_vsllwil_h_b)
TRANS(vsllwil_w_h, gen_vv_i, gen_helper_vsllwil_w_h)
TRANS(vsllwil_d_w, gen_vv_i, gen_helper_vsllwil_d_w)
TRANS(vextl_q_d, gen_vv, gen_helper_vextl_q_d)
TRANS(vsllwil_hu_bu, gen_vv_i, gen_helper_vsllwil_hu_bu)
TRANS(vsllwil_wu_hu, gen_vv_i, gen_helper_vsllwil_wu_hu)
TRANS(vsllwil_du_wu, gen_vv_i, gen_helper_vsllwil_du_wu)
TRANS(vextl_qu_du, gen_vv, gen_helper_vextl_qu_du)

TRANS(vsrlr_b, gen_vvv, gen_helper_vsrlr_b)
TRANS(vsrlr_h, gen_vvv, gen_helper_vsrlr_h)
TRANS(vsrlr_w, gen_vvv, gen_helper_vsrlr_w)
TRANS(vsrlr_d, gen_vvv, gen_helper_vsrlr_d)
TRANS(vsrlri_b, gen_vv_i, gen_helper_vsrlri_b)
TRANS(vsrlri_h, gen_vv_i, gen_helper_vsrlri_h)
TRANS(vsrlri_w, gen_vv_i, gen_helper_vsrlri_w)
TRANS(vsrlri_d, gen_vv_i, gen_helper_vsrlri_d)

TRANS(vsrar_b, gen_vvv, gen_helper_vsrar_b)
TRANS(vsrar_h, gen_vvv, gen_helper_vsrar_h)
TRANS(vsrar_w, gen_vvv, gen_helper_vsrar_w)
TRANS(vsrar_d, gen_vvv, gen_helper_vsrar_d)
TRANS(vsrari_b, gen_vv_i, gen_helper_vsrari_b)
TRANS(vsrari_h, gen_vv_i, gen_helper_vsrari_h)
TRANS(vsrari_w, gen_vv_i, gen_helper_vsrari_w)
TRANS(vsrari_d, gen_vv_i, gen_helper_vsrari_d)

TRANS(vsrln_b_h, gen_vvv, gen_helper_vsrln_b_h)
TRANS(vsrln_h_w, gen_vvv, gen_helper_vsrln_h_w)
TRANS(vsrln_w_d, gen_vvv, gen_helper_vsrln_w_d)
TRANS(vsran_b_h, gen_vvv, gen_helper_vsran_b_h)
TRANS(vsran_h_w, gen_vvv, gen_helper_vsran_h_w)
TRANS(vsran_w_d, gen_vvv, gen_helper_vsran_w_d)

TRANS(vsrlni_b_h, gen_vv_i, gen_helper_vsrlni_b_h)
TRANS(vsrlni_h_w, gen_vv_i, gen_helper_vsrlni_h_w)
TRANS(vsrlni_w_d, gen_vv_i, gen_helper_vsrlni_w_d)
TRANS(vsrlni_d_q, gen_vv_i, gen_helper_vsrlni_d_q)
TRANS(vsrani_b_h, gen_vv_i, gen_helper_vsrani_b_h)
TRANS(vsrani_h_w, gen_vv_i, gen_helper_vsrani_h_w)
TRANS(vsrani_w_d, gen_vv_i, gen_helper_vsrani_w_d)
TRANS(vsrani_d_q, gen_vv_i, gen_helper_vsrani_d_q)

TRANS(vsrlrn_b_h, gen_vvv, gen_helper_vsrlrn_b_h)
TRANS(vsrlrn_h_w, gen_vvv, gen_helper_vsrlrn_h_w)
TRANS(vsrlrn_w_d, gen_vvv, gen_helper_vsrlrn_w_d)
TRANS(vsrarn_b_h, gen_vvv, gen_helper_vsrarn_b_h)
TRANS(vsrarn_h_w, gen_vvv, gen_helper_vsrarn_h_w)
TRANS(vsrarn_w_d, gen_vvv, gen_helper_vsrarn_w_d)

TRANS(vsrlrni_b_h, gen_vv_i, gen_helper_vsrlrni_b_h)
TRANS(vsrlrni_h_w, gen_vv_i, gen_helper_vsrlrni_h_w)
TRANS(vsrlrni_w_d, gen_vv_i, gen_helper_vsrlrni_w_d)
TRANS(vsrlrni_d_q, gen_vv_i, gen_helper_vsrlrni_d_q)
TRANS(vsrarni_b_h, gen_vv_i, gen_helper_vsrarni_b_h)
TRANS(vsrarni_h_w, gen_vv_i, gen_helper_vsrarni_h_w)
TRANS(vsrarni_w_d, gen_vv_i, gen_helper_vsrarni_w_d)
TRANS(vsrarni_d_q, gen_vv_i, gen_helper_vsrarni_d_q)

TRANS(vssrln_b_h, gen_vvv, gen_helper_vssrln_b_h)
TRANS(vssrln_h_w, gen_vvv, gen_helper_vssrln_h_w)
TRANS(vssrln_w_d, gen_vvv, gen_helper_vssrln_w_d)
TRANS(vssran_b_h, gen_vvv, gen_helper_vssran_b_h)
TRANS(vssran_h_w, gen_vvv, gen_helper_vssran_h_w)
TRANS(vssran_w_d, gen_vvv, gen_helper_vssran_w_d)
TRANS(vssrln_bu_h, gen_vvv, gen_helper_vssrln_bu_h)
TRANS(vssrln_hu_w, gen_vvv, gen_helper_vssrln_hu_w)
TRANS(vssrln_wu_d, gen_vvv, gen_helper_vssrln_wu_d)
TRANS(vssran_bu_h, gen_vvv, gen_helper_vssran_bu_h)
TRANS(vssran_hu_w, gen_vvv, gen_helper_vssran_hu_w)
TRANS(vssran_wu_d, gen_vvv, gen_helper_vssran_wu_d)

TRANS(vssrlni_b_h, gen_vv_i, gen_helper_vssrlni_b_h)
TRANS(vssrlni_h_w, gen_vv_i, gen_helper_vssrlni_h_w)
TRANS(vssrlni_w_d, gen_vv_i, gen_helper_vssrlni_w_d)
TRANS(vssrlni_d_q, gen_vv_i, gen_helper_vssrlni_d_q)
TRANS(vssrani_b_h, gen_vv_i, gen_helper_vssrani_b_h)
TRANS(vssrani_h_w, gen_vv_i, gen_helper_vssrani_h_w)
TRANS(vssrani_w_d, gen_vv_i, gen_helper_vssrani_w_d)
TRANS(vssrani_d_q, gen_vv_i, gen_helper_vssrani_d_q)
TRANS(vssrlni_bu_h, gen_vv_i, gen_helper_vssrlni_bu_h)
TRANS(vssrlni_hu_w, gen_vv_i, gen_helper_vssrlni_hu_w)
TRANS(vssrlni_wu_d, gen_vv_i, gen_helper_vssrlni_wu_d)
TRANS(vssrlni_du_q, gen_vv_i, gen_helper_vssrlni_du_q)
TRANS(vssrani_bu_h, gen_vv_i, gen_helper_vssrani_bu_h)
TRANS(vssrani_hu_w, gen_vv_i, gen_helper_vssrani_hu_w)
TRANS(vssrani_wu_d, gen_vv_i, gen_helper_vssrani_wu_d)
TRANS(vssrani_du_q, gen_vv_i, gen_helper_vssrani_du_q)

TRANS(vssrlrn_b_h, gen_vvv, gen_helper_vssrlrn_b_h)
TRANS(vssrlrn_h_w, gen_vvv, gen_helper_vssrlrn_h_w)
TRANS(vssrlrn_w_d, gen_vvv, gen_helper_vssrlrn_w_d)
TRANS(vssrarn_b_h, gen_vvv, gen_helper_vssrarn_b_h)
TRANS(vssrarn_h_w, gen_vvv, gen_helper_vssrarn_h_w)
TRANS(vssrarn_w_d, gen_vvv, gen_helper_vssrarn_w_d)
TRANS(vssrlrn_bu_h, gen_vvv, gen_helper_vssrlrn_bu_h)
TRANS(vssrlrn_hu_w, gen_vvv, gen_helper_vssrlrn_hu_w)
TRANS(vssrlrn_wu_d, gen_vvv, gen_helper_vssrlrn_wu_d)
TRANS(vssrarn_bu_h, gen_vvv, gen_helper_vssrarn_bu_h)
TRANS(vssrarn_hu_w, gen_vvv, gen_helper_vssrarn_hu_w)
TRANS(vssrarn_wu_d, gen_vvv, gen_helper_vssrarn_wu_d)

TRANS(vssrlrni_b_h, gen_vv_i, gen_helper_vssrlrni_b_h)
TRANS(vssrlrni_h_w, gen_vv_i, gen_helper_vssrlrni_h_w)
TRANS(vssrlrni_w_d, gen_vv_i, gen_helper_vssrlrni_w_d)
TRANS(vssrlrni_d_q, gen_vv_i, gen_helper_vssrlrni_d_q)
TRANS(vssrarni_b_h, gen_vv_i, gen_helper_vssrarni_b_h)
TRANS(vssrarni_h_w, gen_vv_i, gen_helper_vssrarni_h_w)
TRANS(vssrarni_w_d, gen_vv_i, gen_helper_vssrarni_w_d)
TRANS(vssrarni_d_q, gen_vv_i, gen_helper_vssrarni_d_q)
TRANS(vssrlrni_bu_h, gen_vv_i, gen_helper_vssrlrni_bu_h)
TRANS(vssrlrni_hu_w, gen_vv_i, gen_helper_vssrlrni_hu_w)
TRANS(vssrlrni_wu_d, gen_vv_i, gen_helper_vssrlrni_wu_d)
TRANS(vssrlrni_du_q, gen_vv_i, gen_helper_vssrlrni_du_q)
TRANS(vssrarni_bu_h, gen_vv_i, gen_helper_vssrarni_bu_h)
TRANS(vssrarni_hu_w, gen_vv_i, gen_helper_vssrarni_hu_w)
TRANS(vssrarni_wu_d, gen_vv_i, gen_helper_vssrarni_wu_d)
TRANS(vssrarni_du_q, gen_vv_i, gen_helper_vssrarni_du_q)

TRANS(vclo_b, gen_vv, gen_helper_vclo_b)
TRANS(vclo_h, gen_vv, gen_helper_vclo_h)
TRANS(vclo_w, gen_vv, gen_helper_vclo_w)
TRANS(vclo_d, gen_vv, gen_helper_vclo_d)
TRANS(vclz_b, gen_vv, gen_helper_vclz_b)
TRANS(vclz_h, gen_vv, gen_helper_vclz_h)
TRANS(vclz_w, gen_vv, gen_helper_vclz_w)
TRANS(vclz_d, gen_vv, gen_helper_vclz_d)

TRANS(vpcnt_b, gen_vv, gen_helper_vpcnt_b)
TRANS(vpcnt_h, gen_vv, gen_helper_vpcnt_h)
TRANS(vpcnt_w, gen_vv, gen_helper_vpcnt_w)
TRANS(vpcnt_d, gen_vv, gen_helper_vpcnt_d)

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

TRANS(vbitclr_b, gvec_vvv, MO_8, do_vbitclr)
TRANS(vbitclr_h, gvec_vvv, MO_16, do_vbitclr)
TRANS(vbitclr_w, gvec_vvv, MO_32, do_vbitclr)
TRANS(vbitclr_d, gvec_vvv, MO_64, do_vbitclr)

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

TRANS(vbitclri_b, gvec_vv_i, MO_8, do_vbitclri)
TRANS(vbitclri_h, gvec_vv_i, MO_16, do_vbitclri)
TRANS(vbitclri_w, gvec_vv_i, MO_32, do_vbitclri)
TRANS(vbitclri_d, gvec_vv_i, MO_64, do_vbitclri)

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

TRANS(vbitset_b, gvec_vvv, MO_8, do_vbitset)
TRANS(vbitset_h, gvec_vvv, MO_16, do_vbitset)
TRANS(vbitset_w, gvec_vvv, MO_32, do_vbitset)
TRANS(vbitset_d, gvec_vvv, MO_64, do_vbitset)

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

TRANS(vbitseti_b, gvec_vv_i, MO_8, do_vbitseti)
TRANS(vbitseti_h, gvec_vv_i, MO_16, do_vbitseti)
TRANS(vbitseti_w, gvec_vv_i, MO_32, do_vbitseti)
TRANS(vbitseti_d, gvec_vv_i, MO_64, do_vbitseti)

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

TRANS(vbitrev_b, gvec_vvv, MO_8, do_vbitrev)
TRANS(vbitrev_h, gvec_vvv, MO_16, do_vbitrev)
TRANS(vbitrev_w, gvec_vvv, MO_32, do_vbitrev)
TRANS(vbitrev_d, gvec_vvv, MO_64, do_vbitrev)

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

TRANS(vbitrevi_b, gvec_vv_i, MO_8, do_vbitrevi)
TRANS(vbitrevi_h, gvec_vv_i, MO_16, do_vbitrevi)
TRANS(vbitrevi_w, gvec_vv_i, MO_32, do_vbitrevi)
TRANS(vbitrevi_d, gvec_vv_i, MO_64, do_vbitrevi)

TRANS(vfrstp_b, gen_vvv, gen_helper_vfrstp_b)
TRANS(vfrstp_h, gen_vvv, gen_helper_vfrstp_h)
TRANS(vfrstpi_b, gen_vv_i, gen_helper_vfrstpi_b)
TRANS(vfrstpi_h, gen_vv_i, gen_helper_vfrstpi_h)

TRANS(vfadd_s, gen_vvv, gen_helper_vfadd_s)
TRANS(vfadd_d, gen_vvv, gen_helper_vfadd_d)
TRANS(vfsub_s, gen_vvv, gen_helper_vfsub_s)
TRANS(vfsub_d, gen_vvv, gen_helper_vfsub_d)
TRANS(vfmul_s, gen_vvv, gen_helper_vfmul_s)
TRANS(vfmul_d, gen_vvv, gen_helper_vfmul_d)
TRANS(vfdiv_s, gen_vvv, gen_helper_vfdiv_s)
TRANS(vfdiv_d, gen_vvv, gen_helper_vfdiv_d)

TRANS(vfmadd_s, gen_vvvv, gen_helper_vfmadd_s)
TRANS(vfmadd_d, gen_vvvv, gen_helper_vfmadd_d)
TRANS(vfmsub_s, gen_vvvv, gen_helper_vfmsub_s)
TRANS(vfmsub_d, gen_vvvv, gen_helper_vfmsub_d)
TRANS(vfnmadd_s, gen_vvvv, gen_helper_vfnmadd_s)
TRANS(vfnmadd_d, gen_vvvv, gen_helper_vfnmadd_d)
TRANS(vfnmsub_s, gen_vvvv, gen_helper_vfnmsub_s)
TRANS(vfnmsub_d, gen_vvvv, gen_helper_vfnmsub_d)

TRANS(vfmax_s, gen_vvv, gen_helper_vfmax_s)
TRANS(vfmax_d, gen_vvv, gen_helper_vfmax_d)
TRANS(vfmin_s, gen_vvv, gen_helper_vfmin_s)
TRANS(vfmin_d, gen_vvv, gen_helper_vfmin_d)

TRANS(vfmaxa_s, gen_vvv, gen_helper_vfmaxa_s)
TRANS(vfmaxa_d, gen_vvv, gen_helper_vfmaxa_d)
TRANS(vfmina_s, gen_vvv, gen_helper_vfmina_s)
TRANS(vfmina_d, gen_vvv, gen_helper_vfmina_d)

TRANS(vflogb_s, gen_vv, gen_helper_vflogb_s)
TRANS(vflogb_d, gen_vv, gen_helper_vflogb_d)

TRANS(vfclass_s, gen_vv, gen_helper_vfclass_s)
TRANS(vfclass_d, gen_vv, gen_helper_vfclass_d)

TRANS(vfsqrt_s, gen_vv, gen_helper_vfsqrt_s)
TRANS(vfsqrt_d, gen_vv, gen_helper_vfsqrt_d)
TRANS(vfrecip_s, gen_vv, gen_helper_vfrecip_s)
TRANS(vfrecip_d, gen_vv, gen_helper_vfrecip_d)
TRANS(vfrsqrt_s, gen_vv, gen_helper_vfrsqrt_s)
TRANS(vfrsqrt_d, gen_vv, gen_helper_vfrsqrt_d)

TRANS(vfcvtl_s_h, gen_vv, gen_helper_vfcvtl_s_h)
TRANS(vfcvth_s_h, gen_vv, gen_helper_vfcvth_s_h)
TRANS(vfcvtl_d_s, gen_vv, gen_helper_vfcvtl_d_s)
TRANS(vfcvth_d_s, gen_vv, gen_helper_vfcvth_d_s)
TRANS(vfcvt_h_s, gen_vvv, gen_helper_vfcvt_h_s)
TRANS(vfcvt_s_d, gen_vvv, gen_helper_vfcvt_s_d)

TRANS(vfrintrne_s, gen_vv, gen_helper_vfrintrne_s)
TRANS(vfrintrne_d, gen_vv, gen_helper_vfrintrne_d)
TRANS(vfrintrz_s, gen_vv, gen_helper_vfrintrz_s)
TRANS(vfrintrz_d, gen_vv, gen_helper_vfrintrz_d)
TRANS(vfrintrp_s, gen_vv, gen_helper_vfrintrp_s)
TRANS(vfrintrp_d, gen_vv, gen_helper_vfrintrp_d)
TRANS(vfrintrm_s, gen_vv, gen_helper_vfrintrm_s)
TRANS(vfrintrm_d, gen_vv, gen_helper_vfrintrm_d)
TRANS(vfrint_s, gen_vv, gen_helper_vfrint_s)
TRANS(vfrint_d, gen_vv, gen_helper_vfrint_d)

TRANS(vftintrne_w_s, gen_vv, gen_helper_vftintrne_w_s)
TRANS(vftintrne_l_d, gen_vv, gen_helper_vftintrne_l_d)
TRANS(vftintrz_w_s, gen_vv, gen_helper_vftintrz_w_s)
TRANS(vftintrz_l_d, gen_vv, gen_helper_vftintrz_l_d)
TRANS(vftintrp_w_s, gen_vv, gen_helper_vftintrp_w_s)
TRANS(vftintrp_l_d, gen_vv, gen_helper_vftintrp_l_d)
TRANS(vftintrm_w_s, gen_vv, gen_helper_vftintrm_w_s)
TRANS(vftintrm_l_d, gen_vv, gen_helper_vftintrm_l_d)
TRANS(vftint_w_s, gen_vv, gen_helper_vftint_w_s)
TRANS(vftint_l_d, gen_vv, gen_helper_vftint_l_d)
TRANS(vftintrz_wu_s, gen_vv, gen_helper_vftintrz_wu_s)
TRANS(vftintrz_lu_d, gen_vv, gen_helper_vftintrz_lu_d)
TRANS(vftint_wu_s, gen_vv, gen_helper_vftint_wu_s)
TRANS(vftint_lu_d, gen_vv, gen_helper_vftint_lu_d)
TRANS(vftintrne_w_d, gen_vvv, gen_helper_vftintrne_w_d)
TRANS(vftintrz_w_d, gen_vvv, gen_helper_vftintrz_w_d)
TRANS(vftintrp_w_d, gen_vvv, gen_helper_vftintrp_w_d)
TRANS(vftintrm_w_d, gen_vvv, gen_helper_vftintrm_w_d)
TRANS(vftint_w_d, gen_vvv, gen_helper_vftint_w_d)
TRANS(vftintrnel_l_s, gen_vv, gen_helper_vftintrnel_l_s)
TRANS(vftintrneh_l_s, gen_vv, gen_helper_vftintrneh_l_s)
TRANS(vftintrzl_l_s, gen_vv, gen_helper_vftintrzl_l_s)
TRANS(vftintrzh_l_s, gen_vv, gen_helper_vftintrzh_l_s)
TRANS(vftintrpl_l_s, gen_vv, gen_helper_vftintrpl_l_s)
TRANS(vftintrph_l_s, gen_vv, gen_helper_vftintrph_l_s)
TRANS(vftintrml_l_s, gen_vv, gen_helper_vftintrml_l_s)
TRANS(vftintrmh_l_s, gen_vv, gen_helper_vftintrmh_l_s)
TRANS(vftintl_l_s, gen_vv, gen_helper_vftintl_l_s)
TRANS(vftinth_l_s, gen_vv, gen_helper_vftinth_l_s)

TRANS(vffint_s_w, gen_vv, gen_helper_vffint_s_w)
TRANS(vffint_d_l, gen_vv, gen_helper_vffint_d_l)
TRANS(vffint_s_wu, gen_vv, gen_helper_vffint_s_wu)
TRANS(vffint_d_lu, gen_vv, gen_helper_vffint_d_lu)
TRANS(vffintl_d_w, gen_vv, gen_helper_vffintl_d_w)
TRANS(vffinth_d_w, gen_vv, gen_helper_vffinth_d_w)
TRANS(vffint_s_l, gen_vvv, gen_helper_vffint_s_l)

static bool do_cmp(DisasContext *ctx, arg_vvv *a, MemOp mop, TCGCond cond)
{
    uint32_t vd_ofs, vj_ofs, vk_ofs;

    CHECK_SXE;

    vd_ofs = vec_full_offset(a->vd);
    vj_ofs = vec_full_offset(a->vj);
    vk_ofs = vec_full_offset(a->vk);

    tcg_gen_gvec_cmp(cond, mop, vd_ofs, vj_ofs, vk_ofs, 16, ctx->vl/8);
    return true;
}

static void do_cmpi_vec(TCGCond cond,
                        unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    tcg_gen_cmp_vec(cond, vece, t, a, tcg_constant_vec_matching(t, vece, imm));
}

static void gen_vseqi_s_vec(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_cmpi_vec(TCG_COND_EQ, vece, t, a, imm);
}

static void gen_vslei_s_vec(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_cmpi_vec(TCG_COND_LE, vece, t, a, imm);
}

static void gen_vslti_s_vec(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_cmpi_vec(TCG_COND_LT, vece, t, a, imm);
}

static void gen_vslei_u_vec(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_cmpi_vec(TCG_COND_LEU, vece, t, a, imm);
}

static void gen_vslti_u_vec(unsigned vece, TCGv_vec t, TCGv_vec a, int64_t imm)
{
    do_cmpi_vec(TCG_COND_LTU, vece, t, a, imm);
}

#define DO_CMPI_S(NAME)                                                \
static bool do_## NAME ##_s(DisasContext *ctx, arg_vv_i *a, MemOp mop) \
{                                                                      \
    uint32_t vd_ofs, vj_ofs;                                           \
                                                                       \
    CHECK_SXE;                                                         \
                                                                       \
    static const TCGOpcode vecop_list[] = {                            \
        INDEX_op_cmp_vec, 0                                            \
    };                                                                 \
    static const GVecGen2i op[4] = {                                   \
        {                                                              \
            .fniv = gen_## NAME ##_s_vec,                              \
            .fnoi = gen_helper_## NAME ##_b,                           \
            .opt_opc = vecop_list,                                     \
            .vece = MO_8                                               \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_s_vec,                              \
            .fnoi = gen_helper_## NAME ##_h,                           \
            .opt_opc = vecop_list,                                     \
            .vece = MO_16                                              \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_s_vec,                              \
            .fnoi = gen_helper_## NAME ##_w,                           \
            .opt_opc = vecop_list,                                     \
            .vece = MO_32                                              \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_s_vec,                              \
            .fnoi = gen_helper_## NAME ##_d,                           \
            .opt_opc = vecop_list,                                     \
            .vece = MO_64                                              \
        }                                                              \
    };                                                                 \
                                                                       \
    vd_ofs = vec_full_offset(a->vd);                                   \
    vj_ofs = vec_full_offset(a->vj);                                   \
                                                                       \
    tcg_gen_gvec_2i(vd_ofs, vj_ofs, 16, ctx->vl/8, a->imm, &op[mop]);  \
                                                                       \
    return true;                                                       \
}

DO_CMPI_S(vseqi)
DO_CMPI_S(vslei)
DO_CMPI_S(vslti)

#define DO_CMPI_U(NAME)                                                \
static bool do_## NAME ##_u(DisasContext *ctx, arg_vv_i *a, MemOp mop) \
{                                                                      \
    uint32_t vd_ofs, vj_ofs;                                           \
                                                                       \
    CHECK_SXE;                                                         \
                                                                       \
    static const TCGOpcode vecop_list[] = {                            \
        INDEX_op_cmp_vec, 0                                            \
    };                                                                 \
    static const GVecGen2i op[4] = {                                   \
        {                                                              \
            .fniv = gen_## NAME ##_u_vec,                              \
            .fnoi = gen_helper_## NAME ##_bu,                          \
            .opt_opc = vecop_list,                                     \
            .vece = MO_8                                               \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_u_vec,                              \
            .fnoi = gen_helper_## NAME ##_hu,                          \
            .opt_opc = vecop_list,                                     \
            .vece = MO_16                                              \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_u_vec,                              \
            .fnoi = gen_helper_## NAME ##_wu,                          \
            .opt_opc = vecop_list,                                     \
            .vece = MO_32                                              \
        },                                                             \
        {                                                              \
            .fniv = gen_## NAME ##_u_vec,                              \
            .fnoi = gen_helper_## NAME ##_du,                          \
            .opt_opc = vecop_list,                                     \
            .vece = MO_64                                              \
        }                                                              \
    };                                                                 \
                                                                       \
    vd_ofs = vec_full_offset(a->vd);                                   \
    vj_ofs = vec_full_offset(a->vj);                                   \
                                                                       \
    tcg_gen_gvec_2i(vd_ofs, vj_ofs, 16, ctx->vl/8, a->imm, &op[mop]);  \
                                                                       \
    return true;                                                       \
}

DO_CMPI_U(vslei)
DO_CMPI_U(vslti)

TRANS(vseq_b, do_cmp, MO_8, TCG_COND_EQ)
TRANS(vseq_h, do_cmp, MO_16, TCG_COND_EQ)
TRANS(vseq_w, do_cmp, MO_32, TCG_COND_EQ)
TRANS(vseq_d, do_cmp, MO_64, TCG_COND_EQ)
TRANS(vseqi_b, do_vseqi_s, MO_8)
TRANS(vseqi_h, do_vseqi_s, MO_16)
TRANS(vseqi_w, do_vseqi_s, MO_32)
TRANS(vseqi_d, do_vseqi_s, MO_64)

TRANS(vsle_b, do_cmp, MO_8, TCG_COND_LE)
TRANS(vsle_h, do_cmp, MO_16, TCG_COND_LE)
TRANS(vsle_w, do_cmp, MO_32, TCG_COND_LE)
TRANS(vsle_d, do_cmp, MO_64, TCG_COND_LE)
TRANS(vslei_b, do_vslei_s, MO_8)
TRANS(vslei_h, do_vslei_s, MO_16)
TRANS(vslei_w, do_vslei_s, MO_32)
TRANS(vslei_d, do_vslei_s, MO_64)
TRANS(vsle_bu, do_cmp, MO_8, TCG_COND_LEU)
TRANS(vsle_hu, do_cmp, MO_16, TCG_COND_LEU)
TRANS(vsle_wu, do_cmp, MO_32, TCG_COND_LEU)
TRANS(vsle_du, do_cmp, MO_64, TCG_COND_LEU)
TRANS(vslei_bu, do_vslei_u, MO_8)
TRANS(vslei_hu, do_vslei_u, MO_16)
TRANS(vslei_wu, do_vslei_u, MO_32)
TRANS(vslei_du, do_vslei_u, MO_64)

TRANS(vslt_b, do_cmp, MO_8, TCG_COND_LT)
TRANS(vslt_h, do_cmp, MO_16, TCG_COND_LT)
TRANS(vslt_w, do_cmp, MO_32, TCG_COND_LT)
TRANS(vslt_d, do_cmp, MO_64, TCG_COND_LT)
TRANS(vslti_b, do_vslti_s, MO_8)
TRANS(vslti_h, do_vslti_s, MO_16)
TRANS(vslti_w, do_vslti_s, MO_32)
TRANS(vslti_d, do_vslti_s, MO_64)
TRANS(vslt_bu, do_cmp, MO_8, TCG_COND_LTU)
TRANS(vslt_hu, do_cmp, MO_16, TCG_COND_LTU)
TRANS(vslt_wu, do_cmp, MO_32, TCG_COND_LTU)
TRANS(vslt_du, do_cmp, MO_64, TCG_COND_LTU)
TRANS(vslti_bu, do_vslti_u, MO_8)
TRANS(vslti_hu, do_vslti_u, MO_16)
TRANS(vslti_wu, do_vslti_u, MO_32)
TRANS(vslti_du, do_vslti_u, MO_64)

static bool trans_vfcmp_cond_s(DisasContext *ctx, arg_vvv_fcond *a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    CHECK_SXE;

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_s : gen_helper_vfcmp_c_s);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, vd, vj, vk,  tcg_constant_i32(flags));

    return true;
}

static bool trans_vfcmp_cond_d(DisasContext *ctx, arg_vvv_fcond *a)
{
    uint32_t flags;
    void (*fn)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32, TCGv_i32);
    TCGv_i32 vd = tcg_constant_i32(a->vd);
    TCGv_i32 vj = tcg_constant_i32(a->vj);
    TCGv_i32 vk = tcg_constant_i32(a->vk);

    fn = (a->fcond & 1 ? gen_helper_vfcmp_s_d : gen_helper_vfcmp_c_d);
    flags = get_fcmp_flags(a->fcond >> 1);
    fn(cpu_env, vd, vj, vk, tcg_constant_i32(flags));

    return true;
}

static bool trans_vbitsel_v(DisasContext *ctx, arg_vvvv *a)
{
    CHECK_SXE;

    tcg_gen_gvec_bitsel(MO_64, vec_full_offset(a->vd), vec_full_offset(a->va),
                        vec_full_offset(a->vk), vec_full_offset(a->vj),
                        16, ctx->vl/8);
    return true;
}

static void gen_vbitseli(unsigned vece, TCGv_vec a, TCGv_vec b, int64_t imm)
{
    tcg_gen_bitsel_vec(vece, a, a, tcg_constant_vec_matching(a, vece, imm), b);
}

static bool trans_vbitseli_b(DisasContext *ctx, arg_vv_i *a)
{
    static const GVecGen2i op = {
       .fniv = gen_vbitseli,
       .fnoi = gen_helper_vbitseli_b,
       .vece = MO_8,
       .load_dest = true
    };

    CHECK_SXE;

    tcg_gen_gvec_2i(vec_full_offset(a->vd), vec_full_offset(a->vj),
                    16, ctx->vl/8, a->imm, &op);
    return true;
}

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
    CHECK_SXE;                                                                 \
    tcg_gen_or_i64(t1, al, ah);                                                \
    tcg_gen_setcondi_i64(COND, t1, t1, 0);                                     \
    tcg_gen_st8_tl(t1, cpu_env, offsetof(CPULoongArchState, cf[a->cd & 0x7])); \
                                                                               \
    return true;                                                               \
}

VSET(vseteqz_v, TCG_COND_EQ)
VSET(vsetnez_v, TCG_COND_NE)

TRANS(vsetanyeqz_b, gen_cv, gen_helper_vsetanyeqz_b)
TRANS(vsetanyeqz_h, gen_cv, gen_helper_vsetanyeqz_h)
TRANS(vsetanyeqz_w, gen_cv, gen_helper_vsetanyeqz_w)
TRANS(vsetanyeqz_d, gen_cv, gen_helper_vsetanyeqz_d)
TRANS(vsetallnez_b, gen_cv, gen_helper_vsetallnez_b)
TRANS(vsetallnez_h, gen_cv, gen_helper_vsetallnez_h)
TRANS(vsetallnez_w, gen_cv, gen_helper_vsetallnez_w)
TRANS(vsetallnez_d, gen_cv, gen_helper_vsetallnez_d)

static bool trans_vinsgr2vr_b(DisasContext *ctx, arg_vr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_SXE;
    tcg_gen_st8_i64(src, cpu_env,
                    offsetof(CPULoongArchState, fpr[a->vd].vreg.B(a->imm)));
    return true;
}

static bool trans_vinsgr2vr_h(DisasContext *ctx, arg_vr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_SXE;
    tcg_gen_st16_i64(src, cpu_env,
                    offsetof(CPULoongArchState, fpr[a->vd].vreg.H(a->imm)));
    return true;
}

static bool trans_vinsgr2vr_w(DisasContext *ctx, arg_vr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_SXE;
    tcg_gen_st32_i64(src, cpu_env,
                     offsetof(CPULoongArchState, fpr[a->vd].vreg.W(a->imm)));
    return true;
}

static bool trans_vinsgr2vr_d(DisasContext *ctx, arg_vr_i *a)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_SXE;
    tcg_gen_st_i64(src, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->vd].vreg.D(a->imm)));
    return true;
}

static bool trans_vpickve2gr_b(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld8s_i64(dst, cpu_env,
                     offsetof(CPULoongArchState, fpr[a->vj].vreg.B(a->imm)));
    return true;
}

static bool trans_vpickve2gr_h(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld16s_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->vj].vreg.H(a->imm)));
    return true;
}

static bool trans_vpickve2gr_w(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld32s_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->vj].vreg.W(a->imm)));
    return true;
}

static bool trans_vpickve2gr_d(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld_i64(dst, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->vj].vreg.D(a->imm)));
    return true;
}

static bool trans_vpickve2gr_bu(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld8u_i64(dst, cpu_env,
                     offsetof(CPULoongArchState, fpr[a->vj].vreg.B(a->imm)));
    return true;
}

static bool trans_vpickve2gr_hu(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld16u_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->vj].vreg.H(a->imm)));
    return true;
}

static bool trans_vpickve2gr_wu(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld32u_i64(dst, cpu_env,
                      offsetof(CPULoongArchState, fpr[a->vj].vreg.W(a->imm)));
    return true;
}

static bool trans_vpickve2gr_du(DisasContext *ctx, arg_rv_i *a)
{
    TCGv dst = gpr_dst(ctx, a->rd, EXT_NONE);
    CHECK_SXE;
    tcg_gen_ld_i64(dst, cpu_env,
                   offsetof(CPULoongArchState, fpr[a->vj].vreg.D(a->imm)));
    return true;
}

static bool gvec_dup(DisasContext *ctx, arg_vr *a, MemOp mop)
{
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    CHECK_SXE;

    tcg_gen_gvec_dup_i64(mop, vec_full_offset(a->vd),
                         16, ctx->vl/8, src);
    return true;
}

TRANS(vreplgr2vr_b, gvec_dup, MO_8)
TRANS(vreplgr2vr_h, gvec_dup, MO_16)
TRANS(vreplgr2vr_w, gvec_dup, MO_32)
TRANS(vreplgr2vr_d, gvec_dup, MO_64)

static bool trans_vreplvei_b(DisasContext *ctx, arg_vv_i *a)
{
    CHECK_SXE;
    tcg_gen_gvec_dup_mem(MO_8,vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.B((a->imm))),
                         16, ctx->vl/8);
    return true;
}

static bool trans_vreplvei_h(DisasContext *ctx, arg_vv_i *a)
{
    CHECK_SXE;
    tcg_gen_gvec_dup_mem(MO_16, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.H((a->imm))),
                         16, ctx->vl/8);
    return true;
}
static bool trans_vreplvei_w(DisasContext *ctx, arg_vv_i *a)
{
    CHECK_SXE;
    tcg_gen_gvec_dup_mem(MO_32, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.W((a->imm))),
                        16, ctx->vl/8);
    return true;
}
static bool trans_vreplvei_d(DisasContext *ctx, arg_vv_i *a)
{
    CHECK_SXE;
    tcg_gen_gvec_dup_mem(MO_64, vec_full_offset(a->vd),
                         offsetof(CPULoongArchState,
                                  fpr[a->vj].vreg.D((a->imm))),
                         16, ctx->vl/8);
    return true;
}

static bool gen_vreplve(DisasContext *ctx, arg_vvr *a, int vece, int bit,
                        void (*func)(TCGv_i64, TCGv_ptr, tcg_target_long))
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_ptr t1 = tcg_temp_new_ptr();
    TCGv_i64 t2 = tcg_temp_new_i64();

    CHECK_SXE;

    tcg_gen_andi_i64(t0, gpr_src(ctx, a->rk, EXT_NONE), (LSX_LEN/bit) -1);
    tcg_gen_shli_i64(t0, t0, vece);
    if (HOST_BIG_ENDIAN) {
        tcg_gen_xori_i64(t0, t0, vece << ((LSX_LEN/bit) -1));
    }

    tcg_gen_trunc_i64_ptr(t1, t0);
    tcg_gen_add_ptr(t1, t1, cpu_env);
    func(t2, t1, vec_full_offset(a->vj));
    tcg_gen_gvec_dup_i64(vece, vec_full_offset(a->vd), 16, ctx->vl/8, t2);

    return true;
}

TRANS(vreplve_b, gen_vreplve, MO_8,  8, tcg_gen_ld8u_i64)
TRANS(vreplve_h, gen_vreplve, MO_16, 16, tcg_gen_ld16u_i64)
TRANS(vreplve_w, gen_vreplve, MO_32, 32, tcg_gen_ld32u_i64)
TRANS(vreplve_d, gen_vreplve, MO_64, 64, tcg_gen_ld_i64)

static bool trans_vbsll_v(DisasContext *ctx, arg_vv_i *a)
{
    int ofs;
    TCGv_i64 desthigh, destlow, high, low;

    CHECK_SXE;

    desthigh = tcg_temp_new_i64();
    destlow = tcg_temp_new_i64();
    high = tcg_temp_new_i64();
    low = tcg_temp_new_i64();

    get_vreg64(low, a->vj, 0);

    ofs = ((a->imm) & 0xf) * 8;
    if (ofs < 64) {
        get_vreg64(high, a->vj, 1);
        tcg_gen_extract2_i64(desthigh, low, high, 64 - ofs);
        tcg_gen_shli_i64(destlow, low, ofs);
    } else {
        tcg_gen_shli_i64(desthigh, low, ofs - 64);
        destlow = tcg_constant_i64(0);
    }

    set_vreg64(desthigh, a->vd, 1);
    set_vreg64(destlow, a->vd, 0);

    return true;
}

static bool trans_vbsrl_v(DisasContext *ctx, arg_vv_i *a)
{
    TCGv_i64 desthigh, destlow, high, low;
    int ofs;

    CHECK_SXE;

    desthigh = tcg_temp_new_i64();
    destlow = tcg_temp_new_i64();
    high = tcg_temp_new_i64();
    low = tcg_temp_new_i64();

    get_vreg64(high, a->vj, 1);

    ofs = ((a->imm) & 0xf) * 8;
    if (ofs < 64) {
        get_vreg64(low, a->vj, 0);
        tcg_gen_extract2_i64(destlow, low, high, ofs);
        tcg_gen_shri_i64(desthigh, high, ofs);
    } else {
        tcg_gen_shri_i64(destlow, high, ofs - 64);
        desthigh = tcg_constant_i64(0);
    }

    set_vreg64(desthigh, a->vd, 1);
    set_vreg64(destlow, a->vd, 0);

    return true;
}

TRANS(vpackev_b, gen_vvv, gen_helper_vpackev_b)
TRANS(vpackev_h, gen_vvv, gen_helper_vpackev_h)
TRANS(vpackev_w, gen_vvv, gen_helper_vpackev_w)
TRANS(vpackev_d, gen_vvv, gen_helper_vpackev_d)
TRANS(vpackod_b, gen_vvv, gen_helper_vpackod_b)
TRANS(vpackod_h, gen_vvv, gen_helper_vpackod_h)
TRANS(vpackod_w, gen_vvv, gen_helper_vpackod_w)
TRANS(vpackod_d, gen_vvv, gen_helper_vpackod_d)

TRANS(vpickev_b, gen_vvv, gen_helper_vpickev_b)
TRANS(vpickev_h, gen_vvv, gen_helper_vpickev_h)
TRANS(vpickev_w, gen_vvv, gen_helper_vpickev_w)
TRANS(vpickev_d, gen_vvv, gen_helper_vpickev_d)
TRANS(vpickod_b, gen_vvv, gen_helper_vpickod_b)
TRANS(vpickod_h, gen_vvv, gen_helper_vpickod_h)
TRANS(vpickod_w, gen_vvv, gen_helper_vpickod_w)
TRANS(vpickod_d, gen_vvv, gen_helper_vpickod_d)

TRANS(vilvl_b, gen_vvv, gen_helper_vilvl_b)
TRANS(vilvl_h, gen_vvv, gen_helper_vilvl_h)
TRANS(vilvl_w, gen_vvv, gen_helper_vilvl_w)
TRANS(vilvl_d, gen_vvv, gen_helper_vilvl_d)
TRANS(vilvh_b, gen_vvv, gen_helper_vilvh_b)
TRANS(vilvh_h, gen_vvv, gen_helper_vilvh_h)
TRANS(vilvh_w, gen_vvv, gen_helper_vilvh_w)
TRANS(vilvh_d, gen_vvv, gen_helper_vilvh_d)

TRANS(vshuf_b, gen_vvvv, gen_helper_vshuf_b)
TRANS(vshuf_h, gen_vvv, gen_helper_vshuf_h)
TRANS(vshuf_w, gen_vvv, gen_helper_vshuf_w)
TRANS(vshuf_d, gen_vvv, gen_helper_vshuf_d)
TRANS(vshuf4i_b, gen_vv_i, gen_helper_vshuf4i_b)
TRANS(vshuf4i_h, gen_vv_i, gen_helper_vshuf4i_h)
TRANS(vshuf4i_w, gen_vv_i, gen_helper_vshuf4i_w)
TRANS(vshuf4i_d, gen_vv_i, gen_helper_vshuf4i_d)

TRANS(vpermi_w, gen_vv_i, gen_helper_vpermi_w)

TRANS(vextrins_b, gen_vv_i, gen_helper_vextrins_b)
TRANS(vextrins_h, gen_vv_i, gen_helper_vextrins_h)
TRANS(vextrins_w, gen_vv_i, gen_helper_vextrins_w)
TRANS(vextrins_d, gen_vv_i, gen_helper_vextrins_d)

static bool trans_vld(DisasContext *ctx, arg_vr_i *a)
{
    TCGv addr, temp;
    TCGv_i64 rl, rh;
    TCGv_i128 val;

    CHECK_SXE;

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i128();
    rl = tcg_temp_new_i64();
    rh = tcg_temp_new_i64();

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

    tcg_gen_qemu_ld_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);
    tcg_gen_extr_i128_i64(rl, rh, val);
    set_vreg64(rh, a->vd, 1);
    set_vreg64(rl, a->vd, 0);

    return true;
}

static bool trans_vst(DisasContext *ctx, arg_vr_i *a)
{
    TCGv addr, temp;
    TCGv_i128 val;
    TCGv_i64 ah, al;

    CHECK_SXE;

    addr = gpr_src(ctx, a->rj, EXT_NONE);
    val = tcg_temp_new_i128();
    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();

    if (a->imm) {
        temp = tcg_temp_new();
        tcg_gen_addi_tl(temp, addr, a->imm);
        addr = temp;
    }

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

    CHECK_SXE;

    addr = tcg_temp_new();
    src1 = gpr_src(ctx, a->rj, EXT_NONE);
    src2 = gpr_src(ctx, a->rk, EXT_NONE);
    val = tcg_temp_new_i128();
    rl = tcg_temp_new_i64();
    rh = tcg_temp_new_i64();

    tcg_gen_add_tl(addr, src1, src2);
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

    CHECK_SXE;

    addr = tcg_temp_new();
    src1 = gpr_src(ctx, a->rj, EXT_NONE);
    src2 = gpr_src(ctx, a->rk, EXT_NONE);
    val = tcg_temp_new_i128();
    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();

    tcg_gen_add_tl(addr, src1, src2);
    get_vreg64(ah, a->vd, 1);
    get_vreg64(al, a->vd, 0);
    tcg_gen_concat_i64_i128(val, al, ah);
    tcg_gen_qemu_st_i128(val, addr, ctx->mem_idx, MO_128 | MO_TE);

    return true;
}

#define VLDREPL(NAME, MO)                                                 \
static bool trans_## NAME (DisasContext *ctx, arg_vr_i *a)                \
{                                                                         \
    TCGv addr, temp;                                                      \
    TCGv_i64 val;                                                         \
                                                                          \
    CHECK_SXE;                                                            \
                                                                          \
    addr = gpr_src(ctx, a->rj, EXT_NONE);                                 \
    val = tcg_temp_new_i64();                                             \
                                                                          \
    if (a->imm) {                                                         \
        temp = tcg_temp_new();                                            \
        tcg_gen_addi_tl(temp, addr, a->imm);                              \
        addr = temp;                                                      \
    }                                                                     \
                                                                          \
    tcg_gen_qemu_ld_i64(val, addr, ctx->mem_idx, MO);                     \
    tcg_gen_gvec_dup_i64(MO, vec_full_offset(a->vd), 16, ctx->vl/8, val); \
                                                                          \
    return true;                                                          \
}

VLDREPL(vldrepl_b, MO_8)
VLDREPL(vldrepl_h, MO_16)
VLDREPL(vldrepl_w, MO_32)
VLDREPL(vldrepl_d, MO_64)

#define VSTELM(NAME, MO, E)                                                  \
static bool trans_## NAME (DisasContext *ctx, arg_vr_ii *a)                  \
{                                                                            \
    TCGv addr, temp;                                                         \
    TCGv_i64 val;                                                            \
                                                                             \
    CHECK_SXE;                                                               \
                                                                             \
    addr = gpr_src(ctx, a->rj, EXT_NONE);                                    \
    val = tcg_temp_new_i64();                                                \
                                                                             \
    if (a->imm) {                                                            \
        temp = tcg_temp_new();                                               \
        tcg_gen_addi_tl(temp, addr, a->imm);                                 \
        addr = temp;                                                         \
    }                                                                        \
                                                                             \
    tcg_gen_ld_i64(val, cpu_env,                                             \
                   offsetof(CPULoongArchState, fpr[a->vd].vreg.E(a->imm2))); \
    tcg_gen_qemu_st_i64(val, addr, ctx->mem_idx, MO);                        \
                                                                             \
    return true;                                                             \
}

VSTELM(vstelm_b, MO_8, B)
VSTELM(vstelm_h, MO_16, H)
VSTELM(vstelm_w, MO_32, W)
VSTELM(vstelm_d, MO_64, D)
