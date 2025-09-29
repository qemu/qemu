/*
 * IBM PPE Instructions
 *
 * Copyright (c) 2025, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */


static bool vdr_is_valid(uint32_t vdr)
{
    const uint32_t valid_bitmap = 0xf00003ff;
    return !!((1ul << (vdr & 0x1f)) & valid_bitmap);
}

static bool ppe_gpr_is_valid(uint32_t reg)
{
    const uint32_t valid_bitmap = 0xf00027ff;
    return !!((1ul << (reg & 0x1f)) & valid_bitmap);
}

#define CHECK_VDR(CTX, VDR)                             \
    do {                                                \
        if (unlikely(!vdr_is_valid(VDR))) {             \
            gen_invalid(CTX);                           \
            return true;                                \
        }                                               \
    } while (0)

#define CHECK_PPE_GPR(CTX, REG)                         \
    do {                                                \
        if (unlikely(!ppe_gpr_is_valid(REG))) {         \
            gen_invalid(CTX);                           \
            return true;                                \
        }                                               \
    } while (0)

#define VDR_PAIR_REG(VDR) (((VDR) + 1) & 0x1f)

#define CHECK_PPE_LEVEL(CTX, LVL)                       \
    do {                                                \
        if (unlikely(!((CTX)->insns_flags2 & (LVL)))) { \
            gen_invalid(CTX);                           \
            return true;                                \
        }                                               \
    } while (0)

static bool trans_LCXU(DisasContext *ctx, arg_LCXU *a)
{
    int i;
    TCGv base, EA;
    TCGv lo, hi;
    TCGv_i64 t8;
    const uint8_t vd_list[] = {9, 7, 5, 3, 0};

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_PPE_GPR(ctx, a->rt);

    if (unlikely((a->rt != a->ra) || (a->ra == 0) || (a->si < 0xB))) {
        gen_invalid(ctx);
        return true;
    }

    EA = tcg_temp_new();
    base = tcg_temp_new();

    tcg_gen_addi_tl(base, cpu_gpr[a->ra], a->si * 8);
    gen_store_spr(SPR_PPE42_EDR, base);

    t8 = tcg_temp_new_i64();

    tcg_gen_addi_tl(EA, base, -8);
    tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    tcg_gen_extr_i64_tl(cpu_gpr[31], cpu_gpr[30], t8);

    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    tcg_gen_extr_i64_tl(cpu_gpr[29], cpu_gpr[28], t8);

    lo = tcg_temp_new();
    hi = tcg_temp_new();

    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    tcg_gen_extr_i64_tl(lo, hi, t8);
    gen_store_spr(SPR_SRR0, hi);
    gen_store_spr(SPR_SRR1, lo);

    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    tcg_gen_extr_i64_tl(lo, hi, t8);
    gen_set_xer(ctx, hi);
    tcg_gen_mov_tl(cpu_ctr, lo);

    for (i = 0; i < sizeof(vd_list); i++) {
        int vd = vd_list[i];
        tcg_gen_addi_tl(EA, EA, -8);
        tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
        tcg_gen_extr_i64_tl(cpu_gpr[VDR_PAIR_REG(vd)], cpu_gpr[vd], t8);
    }

    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    tcg_gen_extr_i64_tl(lo, hi, t8);
    tcg_gen_shri_tl(hi, hi, 28);
    tcg_gen_trunc_tl_i32(cpu_crf[0], hi);
    gen_store_spr(SPR_SPRG0, lo);

    tcg_gen_addi_tl(EA, base, 4);
    tcg_gen_qemu_ld_tl(cpu_lr, EA, ctx->mem_idx, DEF_MEMOP(MO_32) | MO_ALIGN);
    tcg_gen_mov_tl(cpu_gpr[a->ra], base);
    return true;
}

static bool trans_LSKU(DisasContext *ctx, arg_LSKU *a)
{
    int64_t n;
    TCGv base, EA;
    TCGv lo, hi;
    TCGv_i64 t8;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }

    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_PPE_GPR(ctx, a->rt);

    if (unlikely((a->rt != a->ra) || (a->ra == 0) ||
                 (a->si & PPC_BIT(0)) || (a->si == 0))) {
        gen_invalid(ctx);
        return true;
    }

    EA = tcg_temp_new();
    base = tcg_temp_new();
    gen_addr_register(ctx, base);


    tcg_gen_addi_tl(base, base, a->si * 8);
    gen_store_spr(SPR_PPE42_EDR, base);

    n = a->si - 1;
    t8 = tcg_temp_new_i64();
    if (n > 0) {
        tcg_gen_addi_tl(EA, base, -8);
        tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
        hi = cpu_gpr[30];
        lo = cpu_gpr[31];
        tcg_gen_extr_i64_tl(lo, hi, t8);
    }
    if (n > 1) {
        tcg_gen_addi_tl(EA, base, -16);
        tcg_gen_qemu_ld_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
        hi = cpu_gpr[28];
        lo = cpu_gpr[29];
        tcg_gen_extr_i64_tl(lo, hi, t8);
    }
    tcg_gen_addi_tl(EA, base, 4);
    tcg_gen_qemu_ld_tl(cpu_lr, EA, ctx->mem_idx, DEF_MEMOP(MO_32) | MO_ALIGN);
    tcg_gen_mov_tl(cpu_gpr[a->ra], base);
    return true;
}

static bool trans_STCXU(DisasContext *ctx, arg_STCXU *a)
{
    TCGv EA;
    TCGv lo, hi;
    TCGv_i64 t8;
    int i;
    const uint8_t vd_list[] = {9, 7, 5, 3, 0};

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }

    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_PPE_GPR(ctx, a->rt);

    if (unlikely((a->rt != a->ra) || (a->ra == 0) || !(a->si & PPC_BIT(0)))) {
        gen_invalid(ctx);
        return true;
    }

    EA = tcg_temp_new();
    tcg_gen_addi_tl(EA, cpu_gpr[a->ra], 4);
    tcg_gen_qemu_st_tl(cpu_lr, EA, ctx->mem_idx, DEF_MEMOP(MO_32) | MO_ALIGN);

    gen_store_spr(SPR_PPE42_EDR, cpu_gpr[a->ra]);

    t8 = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(t8, cpu_gpr[31], cpu_gpr[30]);
    tcg_gen_addi_tl(EA, cpu_gpr[a->ra], -8);
    tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);

    tcg_gen_concat_tl_i64(t8, cpu_gpr[29], cpu_gpr[28]);
    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);

    lo = tcg_temp_new();
    hi = tcg_temp_new();

    gen_load_spr(hi, SPR_SRR0);
    gen_load_spr(lo, SPR_SRR1);
    tcg_gen_concat_tl_i64(t8, lo, hi);
    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);

    gen_get_xer(ctx, hi);
    tcg_gen_mov_tl(lo, cpu_ctr);
    tcg_gen_concat_tl_i64(t8, lo, hi);
    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);

    for (i = 0; i < sizeof(vd_list); i++) {
        int vd = vd_list[i];
        tcg_gen_concat_tl_i64(t8, cpu_gpr[VDR_PAIR_REG(vd)], cpu_gpr[vd]);
        tcg_gen_addi_tl(EA, EA, -8);
        tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    }

    gen_load_spr(lo, SPR_SPRG0);
    tcg_gen_extu_i32_tl(hi, cpu_crf[0]);
    tcg_gen_shli_tl(hi, hi, 28);
    tcg_gen_concat_tl_i64(t8, lo, hi);
    tcg_gen_addi_tl(EA, EA, -8);
    tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);

    tcg_gen_addi_tl(EA, cpu_gpr[a->ra], a->si * 8);
    tcg_gen_qemu_st_tl(cpu_gpr[a->rt], EA, ctx->mem_idx, DEF_MEMOP(MO_32) |
                                                          MO_ALIGN);
    tcg_gen_mov_tl(cpu_gpr[a->ra], EA);
    return true;
}

static bool trans_STSKU(DisasContext *ctx, arg_STSKU *a)
{
    int64_t n;
    TCGv base, EA;
    TCGv lo, hi;
    TCGv_i64 t8;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }

    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_PPE_GPR(ctx, a->rt);

    if (unlikely((a->rt != a->ra) || (a->ra == 0) || !(a->si & PPC_BIT(0)))) {
        gen_invalid(ctx);
        return true;
    }

    EA = tcg_temp_new();
    base = tcg_temp_new();
    gen_addr_register(ctx, base);
    tcg_gen_addi_tl(EA, base, 4);
    tcg_gen_qemu_st_tl(cpu_lr, EA, ctx->mem_idx, DEF_MEMOP(MO_32) | MO_ALIGN);

    gen_store_spr(SPR_PPE42_EDR, base);

    n = ~(a->si);

    t8 = tcg_temp_new_i64();
    if (n > 0) {
        hi = cpu_gpr[30];
        lo = cpu_gpr[31];
        tcg_gen_concat_tl_i64(t8, lo, hi);
        tcg_gen_addi_tl(EA, base, -8);
        tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    }
    if (n > 1) {
        hi = cpu_gpr[28];
        lo = cpu_gpr[29];
        tcg_gen_concat_tl_i64(t8, lo, hi);
        tcg_gen_addi_tl(EA, base, -16);
        tcg_gen_qemu_st_i64(t8, EA, ctx->mem_idx, DEF_MEMOP(MO_64) | MO_ALIGN);
    }

    tcg_gen_addi_tl(EA, base, a->si * 8);
    tcg_gen_qemu_st_tl(cpu_gpr[a->rt], EA, ctx->mem_idx, DEF_MEMOP(MO_32) |
                                                          MO_ALIGN);
    tcg_gen_mov_tl(cpu_gpr[a->ra], EA);
    return true;
}

static bool do_ppe_ldst(DisasContext *ctx, int rt, int ra, TCGv disp,
                        bool update, bool store)
{
    TCGv ea;
    int rt_lo;
    TCGv_i64 t8;

    CHECK_VDR(ctx, rt);
    CHECK_PPE_GPR(ctx, ra);
    rt_lo = VDR_PAIR_REG(rt);
    if (update && (ra == 0 || (!store && ((ra == rt) || (ra == rt_lo))))) {
        gen_invalid(ctx);
        return true;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    ea = do_ea_calc(ctx, ra, disp);
    t8 = tcg_temp_new_i64();
    if (store) {
        tcg_gen_concat_tl_i64(t8, cpu_gpr[rt_lo], cpu_gpr[rt]);
        tcg_gen_qemu_st_i64(t8, ea, ctx->mem_idx, DEF_MEMOP(MO_64));
    } else {
        tcg_gen_qemu_ld_i64(t8, ea, ctx->mem_idx, DEF_MEMOP(MO_64));
        tcg_gen_extr_i64_tl(cpu_gpr[rt_lo], cpu_gpr[rt], t8);
    }
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[ra], ea);
    }
    return true;
}

static bool do_ppe_ldst_D(DisasContext *ctx, arg_D *a, bool update, bool store)
{
    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    return do_ppe_ldst(ctx, a->rt, a->ra, tcg_constant_tl(a->si), update,
                       store);
}

static bool do_ppe_ldst_X(DisasContext *ctx, arg_X *a, bool store)
{
    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_GPR(ctx, a->rb);
    return do_ppe_ldst(ctx, a->rt, a->ra, cpu_gpr[a->rb], false, store);
}

TRANS(LVD,   do_ppe_ldst_D, false, false)
TRANS(LVDU,  do_ppe_ldst_D, true,  false)
TRANS(STVD,  do_ppe_ldst_D, false, true)
TRANS(STVDU, do_ppe_ldst_D, true,  true)
TRANS(LVDX,  do_ppe_ldst_X, false)
TRANS(STVDX, do_ppe_ldst_X, true)


static bool do_fcb(DisasContext *ctx, TCGv ra_val, TCGv rb_val, int bix,
                          int32_t bdx, bool s, bool px, bool lk)
{
    TCGCond cond;
    uint32_t mask;
    TCGLabel *no_branch;
    target_ulong dest;

    /* Update CR0 */
    gen_op_cmp32(ra_val, rb_val, s, 0);

    if (lk) {
        gen_setlr(ctx, ctx->base.pc_next);
    }


    mask = PPC_BIT32(28 + bix);
    cond = (px) ? TCG_COND_TSTEQ : TCG_COND_TSTNE;
    no_branch = gen_new_label();
    dest = ctx->cia + bdx;

    /* Do the branch if CR0[bix] == PX */
    tcg_gen_brcondi_i32(cond, cpu_crf[0], mask, no_branch);
    gen_goto_tb(ctx, 0, dest);
    gen_set_label(no_branch);
    gen_goto_tb(ctx, 1, ctx->base.pc_next);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool do_cmp_branch(DisasContext *ctx, arg_FCB_bix *a, bool s,
                          bool rb_is_gpr)
{
    TCGv old_ra;
    TCGv rb_val;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_GPR(ctx, a->ra);
    if (rb_is_gpr) {
        CHECK_PPE_GPR(ctx, a->rb);
        rb_val = cpu_gpr[a->rb];
    } else {
        rb_val = tcg_constant_tl(a->rb);
    }
    if (a->bix == 3) {
        old_ra = tcg_temp_new();
        tcg_gen_mov_tl(old_ra, cpu_gpr[a->ra]);
        tcg_gen_sub_tl(cpu_gpr[a->ra], cpu_gpr[a->ra], rb_val);
        return do_fcb(ctx, old_ra, rb_val, 2,
                      a->bdx, s, a->px, a->lk);
    } else {
        return do_fcb(ctx, cpu_gpr[a->ra], rb_val, a->bix,
                      a->bdx, s, a->px, a->lk);
    }
}

TRANS(CMPWBC, do_cmp_branch, true, true)
TRANS(CMPLWBC, do_cmp_branch, false, true)
TRANS(CMPWIBC, do_cmp_branch, true, false)

static bool do_mask_branch(DisasContext *ctx, arg_FCB * a, bool invert,
                           bool update, bool rb_is_gpr)
{
    TCGv r;
    TCGv mask, shift;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_GPR(ctx, a->ra);
    if (rb_is_gpr) {
        CHECK_PPE_GPR(ctx, a->rb);
        mask = tcg_temp_new();
        shift = tcg_temp_new();
        tcg_gen_andi_tl(shift, cpu_gpr[a->rb], 0x1f);
        tcg_gen_shr_tl(mask, tcg_constant_tl(0x80000000), shift);
    } else {
        mask = tcg_constant_tl(PPC_BIT32(a->rb));
    }
    if (invert) {
        tcg_gen_not_tl(mask, mask);
    }

    /* apply mask to ra */
    r = tcg_temp_new();
    tcg_gen_and_tl(r, cpu_gpr[a->ra], mask);
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[a->ra], r);
    }
    return do_fcb(ctx, r, tcg_constant_tl(0), 2,
                  a->bdx, false, a->px, a->lk);
}

TRANS(BNBWI,    do_mask_branch, false, false, false)
TRANS(BNBW,     do_mask_branch, false, false, true)
TRANS(CLRBWIBC, do_mask_branch, true,  true,  false)
TRANS(CLRBWBC,  do_mask_branch, true,  true,  true)

static void gen_set_Rc0_i64(DisasContext *ctx, TCGv_i64 reg)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_movi_i64(t0, CRF_EQ);
    tcg_gen_movi_i64(t1, CRF_LT);
    tcg_gen_movcond_i64(TCG_COND_LT, t0, reg, tcg_constant_i64(0), t1, t0);
    tcg_gen_movi_i64(t1, CRF_GT);
    tcg_gen_movcond_i64(TCG_COND_GT, t0, reg, tcg_constant_i64(0), t1, t0);
    tcg_gen_extrl_i64_i32(t, t0);
    tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
    tcg_gen_or_i32(cpu_crf[0], cpu_crf[0], t);
}

static bool do_shift64(DisasContext *ctx, arg_X_rc *a, bool left)
{
    int rt_lo, ra_lo;
    TCGv_i64 t0, t8;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_VDR(ctx, a->rt);
    CHECK_VDR(ctx, a->ra);
    CHECK_PPE_GPR(ctx, a->rb);
    rt_lo = VDR_PAIR_REG(a->rt);
    ra_lo = VDR_PAIR_REG(a->ra);
    t8 = tcg_temp_new_i64();

    /* AND rt with a mask that is 0 when rb >= 0x40 */
    t0 = tcg_temp_new_i64();
    tcg_gen_extu_tl_i64(t0, cpu_gpr[a->rb]);
    tcg_gen_shli_i64(t0, t0, 0x39);
    tcg_gen_sari_i64(t0, t0, 0x3f);

    /* form 64bit value from two 32bit regs */
    tcg_gen_concat_tl_i64(t8, cpu_gpr[rt_lo], cpu_gpr[a->rt]);

    /* apply mask */
    tcg_gen_andc_i64(t8, t8, t0);

    /* do the shift */
    tcg_gen_extu_tl_i64(t0, cpu_gpr[a->rb]);
    tcg_gen_andi_i64(t0, t0, 0x3f);
    if (left) {
        tcg_gen_shl_i64(t8, t8, t0);
    } else {
        tcg_gen_shr_i64(t8, t8, t0);
    }

    /* split the 64bit word back into two 32bit regs */
    tcg_gen_extr_i64_tl(cpu_gpr[ra_lo], cpu_gpr[a->ra], t8);

    /* update CR0 if requested */
    if (unlikely(a->rc != 0)) {
        gen_set_Rc0_i64(ctx, t8);
    }
    return true;
}

TRANS(SRVD, do_shift64, false)
TRANS(SLVD, do_shift64, true)

static bool trans_DCBQ(DisasContext *ctx, arg_DCBQ * a)
{
    if (unlikely(!is_ppe(ctx))) {
        return false;
    }

    CHECK_PPE_GPR(ctx, a->rt);
    CHECK_PPE_GPR(ctx, a->ra);
    CHECK_PPE_GPR(ctx, a->rb);

    /* No cache exists, so just set RT to 0 */
    tcg_gen_movi_tl(cpu_gpr[a->rt], 0);
    return true;
}

static bool trans_RLDIMI(DisasContext *ctx, arg_RLDIMI *a)
{
    TCGv_i64 t_rs, t_ra;
    int ra_lo, rs_lo;
    uint32_t sh = a->sh;
    uint32_t mb = a->mb;
    uint32_t me = 63 - sh;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_VDR(ctx, a->rs);
    CHECK_VDR(ctx, a->ra);

    rs_lo = VDR_PAIR_REG(a->rs);
    ra_lo = VDR_PAIR_REG(a->ra);

    t_rs = tcg_temp_new_i64();
    t_ra = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(t_rs, cpu_gpr[rs_lo], cpu_gpr[a->rs]);
    tcg_gen_concat_tl_i64(t_ra, cpu_gpr[ra_lo], cpu_gpr[a->ra]);

    if (mb <= me) {
        tcg_gen_deposit_i64(t_ra, t_ra, t_rs, sh, me - mb + 1);
    } else {
        uint64_t mask = mask_u64(mb, me);
        TCGv_i64 t1 = tcg_temp_new_i64();

        tcg_gen_rotli_i64(t1, t_rs, sh);
        tcg_gen_andi_i64(t1, t1, mask);
        tcg_gen_andi_i64(t_ra, t_ra, ~mask);
        tcg_gen_or_i64(t_ra, t_ra, t1);
    }

    tcg_gen_extr_i64_tl(cpu_gpr[ra_lo], cpu_gpr[a->ra], t_ra);

    if (unlikely(a->rc != 0)) {
        gen_set_Rc0_i64(ctx, t_ra);
    }
    return true;
}


static bool gen_rldinm_i64(DisasContext *ctx, arg_MD *a, int mb, int me, int sh)
{
    int len = me - mb + 1;
    int rsh = (64 - sh) & 63;
    int ra_lo, rs_lo;
    TCGv_i64 t8;

    if (unlikely(!is_ppe(ctx))) {
        return false;
    }
    CHECK_PPE_LEVEL(ctx, PPC2_PPE42X);
    CHECK_VDR(ctx, a->rs);
    CHECK_VDR(ctx, a->ra);

    rs_lo = VDR_PAIR_REG(a->rs);
    ra_lo = VDR_PAIR_REG(a->ra);
    t8 = tcg_temp_new_i64();
    tcg_gen_concat_tl_i64(t8, cpu_gpr[rs_lo], cpu_gpr[a->rs]);
    if (sh != 0 && len > 0 && me == (63 - sh)) {
        tcg_gen_deposit_z_i64(t8, t8, sh, len);
    } else if (me == 63 && rsh + len <= 64) {
        tcg_gen_extract_i64(t8, t8, rsh, len);
    } else {
        tcg_gen_rotli_i64(t8, t8, sh);
        tcg_gen_andi_i64(t8, t8, mask_u64(mb, me));
    }
    tcg_gen_extr_i64_tl(cpu_gpr[ra_lo], cpu_gpr[a->ra], t8);
    if (unlikely(a->rc != 0)) {
        gen_set_Rc0_i64(ctx, t8);
    }
    return true;
}

TRANS(RLDICL, gen_rldinm_i64, a->mb, 63, a->sh)
TRANS(RLDICR, gen_rldinm_i64, 0, a->mb, a->sh)
