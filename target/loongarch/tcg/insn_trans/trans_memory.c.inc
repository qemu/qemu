/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool gen_load(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv t1 = tcg_temp_new();
    TCGv mask = tcg_constant_tl(0x8);
    TCGv zero = tcg_constant_tl(0);
    TCGLabel *done = gen_new_label();
    TCGLabel *l1 = gen_new_label();

    addr = make_address_i(ctx, addr, a->imm);

    if (avail_SCQ(ctx) && mop == MO_LEUQ) {
        /*
         * The LL.D+LD.D may be paired with SC.Q,
         * use llval_high if llbit_scq && (addr == lladdr ^ 0x8)
         */
        tcg_gen_brcond_tl(TCG_COND_EQ, cpu_llbit_scq, zero, l1);
        tcg_gen_xor_tl(t1, addr, mask);
        tcg_gen_brcond_tl(TCG_COND_NE, cpu_lladdr, t1, l1);
        gen_set_gpr(a->rd, cpu_llval_high, EXT_NONE);
        tcg_gen_br(done);
        gen_set_label(l1);
    }

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    if (avail_SCQ(ctx) && mop == MO_LEUQ) {
        gen_set_label(done);
    }
    return true;
}

static bool gen_store(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

static bool gen_loadx(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = make_address_x(ctx, src1, src2);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_storex(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv addr = make_address_x(ctx, src1, src2);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);

    return true;
}

static bool gen_load_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_du(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_load_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_du(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_ld_tl(dest, src1, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_store_gt(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtgt_du(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool gen_store_le(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rk, EXT_NONE);

    gen_helper_asrtle_du(tcg_env, src1, src2);
    src1 = make_address_i(ctx, src1, 0);
    tcg_gen_qemu_st_tl(data, src1, ctx->mem_idx, mop);

    return true;
}

static bool trans_preld(DisasContext *ctx, arg_preld *a)
{
    return true;
}

static bool trans_preldx(DisasContext *ctx, arg_preldx * a)
{
    return true;
}

/*
 * Decode dbar hint and emit appropriate TCG memory barrier.
 *
 * The hint is a 5-bit field (0-31) encoded in the instruction.
 * For hint 0x700 (special LL/SC loop barrier), treat as full barrier.
 *
 * See LoongArch Reference Manual v1.10, Section 4.2.2 for details.
 */
static bool trans_dbar(DisasContext *ctx, arg_dbar * a)
{
    int hint = a->imm;
    TCGBar bar_flags = 0;

    /* Reserved/no-op hints: 0xf and 0x1f */
    if (hint == 0xf || hint == 0x1f) {
        return true;
    }

    /* If the CPU does not support fine-grained hints,or for the special LL/SC
     * loop barrier (0x700), emit a full barrier.
     */
    if (!avail_DBAR_HINT(ctx) || hint == 0x700) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        return true;
    }

    /*
     * Fine-grained hint decoding:
     * The hint is a 5-bit field (bits 4-0). Bit4 is reserved and currently
     * ignored/discarded. Only bits 3-0 are used for ordering control.
     * Bit3: barrier for previous read (0: true, 1: false)
     * Bit2: barrier for previous write (0: true, 1: false)
     * Bit1: barrier for succeeding read (0: true, 1: false)
     * Bit0: barrier for succeeding write (0: true, 1: false)
     *
     * For each combination, we set the corresponding TCG_MO_* flag if both
     * sides of the barrier require ordering.
     */

    bool prev_rd = !(hint & 0x08); /* bit3 */
    bool prev_wr = !(hint & 0x04); /* bit2 */
    bool succ_rd = !(hint & 0x02); /* bit1 */
    bool succ_wr = !(hint & 0x01); /* bit0 */

    if (prev_rd) {
        bar_flags |= TCG_MO_LD_LD | TCG_MO_LD_ST;
    }
    if (prev_wr) {
        bar_flags |= TCG_MO_ST_LD | TCG_MO_ST_ST;
    }
    if (succ_rd) {
        bar_flags |= TCG_MO_LD_LD | TCG_MO_ST_LD;
    }
    if (succ_wr) {
        bar_flags |= TCG_MO_ST_ST | TCG_MO_LD_ST;
    }

    if (bar_flags == 0) {
        bar_flags = TCG_MO_ALL;
    }

    tcg_gen_mb(bar_flags | TCG_BAR_SC);
    return true;
 }

static bool trans_ibar(DisasContext *ctx, arg_ibar *a)
{
    ctx->base.is_jmp = DISAS_STOP;
    return true;
}

static bool gen_ldptr(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool gen_stptr(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

TRANS(ld_b, ALL, gen_load, MO_SB)
TRANS(ld_h, ALL, gen_load, MO_LESW)
TRANS(ld_w, ALL, gen_load, MO_LESL)
TRANS(ld_d, 64, gen_load, MO_LEUQ)
TRANS(st_b, ALL, gen_store, MO_UB)
TRANS(st_h, ALL, gen_store, MO_LEUW)
TRANS(st_w, ALL, gen_store, MO_LEUL)
TRANS(st_d, 64, gen_store, MO_LEUQ)
TRANS(ld_bu, ALL, gen_load, MO_UB)
TRANS(ld_hu, ALL, gen_load, MO_LEUW)
TRANS(ld_wu, 64, gen_load, MO_LEUL)
TRANS(ldx_b, 64, gen_loadx, MO_SB)
TRANS(ldx_h, 64, gen_loadx, MO_LESW)
TRANS(ldx_w, 64, gen_loadx, MO_LESL)
TRANS(ldx_d, 64, gen_loadx, MO_LEUQ)
TRANS(stx_b, 64, gen_storex, MO_UB)
TRANS(stx_h, 64, gen_storex, MO_LEUW)
TRANS(stx_w, 64, gen_storex, MO_LEUL)
TRANS(stx_d, 64, gen_storex, MO_LEUQ)
TRANS(ldx_bu, 64, gen_loadx, MO_UB)
TRANS(ldx_hu, 64, gen_loadx, MO_LEUW)
TRANS(ldx_wu, 64, gen_loadx, MO_LEUL)
TRANS(ldptr_w, 64, gen_ldptr, MO_LESL)
TRANS(stptr_w, 64, gen_stptr, MO_LEUL)
TRANS(ldptr_d, 64, gen_ldptr, MO_LEUQ)
TRANS(stptr_d, 64, gen_stptr, MO_LEUQ)
TRANS(ldgt_b, 64, gen_load_gt, MO_SB)
TRANS(ldgt_h, 64, gen_load_gt, MO_LESW)
TRANS(ldgt_w, 64, gen_load_gt, MO_LESL)
TRANS(ldgt_d, 64, gen_load_gt, MO_LEUQ)
TRANS(ldle_b, 64, gen_load_le, MO_SB)
TRANS(ldle_h, 64, gen_load_le, MO_LESW)
TRANS(ldle_w, 64, gen_load_le, MO_LESL)
TRANS(ldle_d, 64, gen_load_le, MO_LEUQ)
TRANS(stgt_b, 64, gen_store_gt, MO_UB)
TRANS(stgt_h, 64, gen_store_gt, MO_LEUW)
TRANS(stgt_w, 64, gen_store_gt, MO_LEUL)
TRANS(stgt_d, 64, gen_store_gt, MO_LEUQ)
TRANS(stle_b, 64, gen_store_le, MO_UB)
TRANS(stle_h, 64, gen_store_le, MO_LEUW)
TRANS(stle_w, 64, gen_store_le, MO_LEUL)
TRANS(stle_d, 64, gen_store_le, MO_LEUQ)
