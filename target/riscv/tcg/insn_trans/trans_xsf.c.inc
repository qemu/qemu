/*
 * RISC-V translation routines for the SiFive vendor extensions (xsf*)
 *
 * Copyright (c) 2023 SiFive, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


/*
 * SiFive Xsfvqmaccdod/Xsfvqmaccqoq custom int8 matrix-multiply extensions
 */
static bool sf_int8_matmul_check(DisasContext *s, arg_rmrr *a)
{
    return require_rvv(s) &&
           vext_check_isa_ill(s) &&
           s->vstart_eq_zero &&
           (s->cfg_ptr->vlenb >= 32) &&
           (s->sew == MO_8) &&
           (a->vm == 1);
}

static bool sf_int8_matmul_4x8x4_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * vd  has EMUL=2*LMUL
     * vs2 has EMUL=LMUL
     * vs1 has EMUL=1
     * vd must not overlap vs1
     */
    return sf_int8_matmul_check(s, a) &&
           (s->cfg_ptr->ext_xsfvqmaccqoq) &&
           (s->lmul <= 2) &&
           require_align(a->rd, s->lmul + 1) &&
           require_align(a->rs2, s->lmul) &&
           require_align(a->rs1, 0) &&
           require_noover(a->rd, s->lmul + 1, a->rs2, s->lmul) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul + 1, 0), a->rs1, 1);
}

static bool sf_int8_matmul_2x8x2_check(DisasContext *s, arg_rmrr *a)
{
    /*
     * vd  has EMUL=LMUL
     * vs2 has EMUL=LMUL
     * vs1 has EMUL=1
     * vd must not overlap vs1
     */
    return sf_int8_matmul_check(s, a) &&
           (s->cfg_ptr->ext_xsfvqmaccdod) &&
           require_align(a->rd, s->lmul) &&
           require_align(a->rs2, s->lmul) &&
           require_align(a->rs1, 0) &&
           !is_overlapped(a->rd, 1 << MAX(s->lmul, 0), a->rs1, 1);
}

static bool sf_int8_matmul_op(DisasContext *s, arg_rmrr *a, uint8_t seq)
{
    static gen_helper_gvec_3_ptr * const fns[8] = {
        gen_helper_sf_vqmaccu_4x8x4, gen_helper_sf_vqmacc_4x8x4,
        gen_helper_sf_vqmaccus_4x8x4, gen_helper_sf_vqmaccsu_4x8x4,
        gen_helper_sf_vqmaccu_2x8x2, gen_helper_sf_vqmacc_2x8x2,
        gen_helper_sf_vqmaccus_2x8x2, gen_helper_sf_vqmaccsu_2x8x2,
    };

    /*
     * The helper raises an illegal-instruction exception when vl is not a
     * multiple of the tile size; save the opcode so mtval/stval report the
     * faulting instruction if that exception is thrown.
     */
    decode_save_opc(s, 0);

    tcg_gen_gvec_3_ptr(vreg_ofs(s, a->rd), vreg_ofs(s, a->rs1),
                       vreg_ofs(s, a->rs2), tcg_env,
                       s->cfg_ptr->vlenb, s->cfg_ptr->vlenb, 0, fns[seq]);

    finalize_rvv_inst(s);

    return true;
}

#define GEN_SF_INT8_MATMUL_TRANS(NAME, CHECK, SEQ)       \
static bool trans_##NAME(DisasContext *s, arg_rmrr *a)   \
{                                                        \
    if (CHECK(s, a)) {                                   \
        return sf_int8_matmul_op(s, a, SEQ);             \
    }                                                    \
    return false;                                        \
}

GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccu_4x8x4,  sf_int8_matmul_4x8x4_check, 0)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmacc_4x8x4,   sf_int8_matmul_4x8x4_check, 1)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccus_4x8x4, sf_int8_matmul_4x8x4_check, 2)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccsu_4x8x4, sf_int8_matmul_4x8x4_check, 3)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccu_2x8x2,  sf_int8_matmul_2x8x2_check, 4)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmacc_2x8x2,   sf_int8_matmul_2x8x2_check, 5)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccus_2x8x2, sf_int8_matmul_2x8x2_check, 6)
GEN_SF_INT8_MATMUL_TRANS(sf_vqmaccsu_2x8x2, sf_int8_matmul_2x8x2_check, 7)
