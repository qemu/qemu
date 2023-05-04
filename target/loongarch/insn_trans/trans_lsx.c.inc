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
