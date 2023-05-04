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
