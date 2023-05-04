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
