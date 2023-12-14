/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool gen_ll(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv t0 = make_address_i(ctx, src1, a->imm);

    tcg_gen_qemu_ld_i64(dest, t0, ctx->mem_idx, mop);
    tcg_gen_st_tl(t0, tcg_env, offsetof(CPULoongArchState, lladdr));
    tcg_gen_st_tl(dest, tcg_env, offsetof(CPULoongArchState, llval));
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_sc(DisasContext *ctx, arg_rr_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv t0 = tcg_temp_new();
    TCGv val = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_addi_tl(t0, src1, a->imm);
    tcg_gen_brcond_tl(TCG_COND_EQ, t0, cpu_lladdr, l1);
    tcg_gen_movi_tl(dest, 0);
    tcg_gen_br(done);

    gen_set_label(l1);
    tcg_gen_mov_tl(val, src2);
    /* generate cmpxchg */
    tcg_gen_atomic_cmpxchg_tl(t0, cpu_lladdr, cpu_llval,
                              val, ctx->mem_idx, mop);
    tcg_gen_setcond_tl(TCG_COND_EQ, dest, t0, cpu_llval);
    gen_set_label(done);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_am(DisasContext *ctx, arg_rrr *a,
                   void (*func)(TCGv, TCGv, TCGv, TCGArg, MemOp),
                   MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv val = gpr_src(ctx, a->rk, EXT_NONE);

    if (a->rd != 0 && (a->rj == a->rd || a->rk == a->rd)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Warning: source register overlaps destination register"
                      "in atomic insn at pc=0x" TARGET_FMT_lx "\n",
                      ctx->base.pc_next - 4);
        return false;
    }

    addr = make_address_i(ctx, addr, 0);

    func(dest, addr, val, ctx->mem_idx, mop);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

TRANS(ll_w, ALL, gen_ll, MO_TESL)
TRANS(sc_w, ALL, gen_sc, MO_TESL)
TRANS(ll_d, 64, gen_ll, MO_TEUQ)
TRANS(sc_d, 64, gen_sc, MO_TEUQ)
TRANS(amswap_w, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_TESL)
TRANS(amswap_d, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_TEUQ)
TRANS(amadd_w, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_TESL)
TRANS(amadd_d, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_TEUQ)
TRANS(amand_w, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_TESL)
TRANS(amand_d, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_TEUQ)
TRANS(amor_w, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_TESL)
TRANS(amor_d, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_TEUQ)
TRANS(amxor_w, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_TESL)
TRANS(amxor_d, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_TEUQ)
TRANS(ammax_w, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_TESL)
TRANS(ammax_d, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_TEUQ)
TRANS(ammin_w, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_TESL)
TRANS(ammin_d, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_TEUQ)
TRANS(ammax_wu, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_TESL)
TRANS(ammax_du, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_TEUQ)
TRANS(ammin_wu, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_TESL)
TRANS(ammin_du, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_TEUQ)
TRANS(amswap_db_w, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_TESL)
TRANS(amswap_db_d, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_TEUQ)
TRANS(amadd_db_w, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_TESL)
TRANS(amadd_db_d, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_TEUQ)
TRANS(amand_db_w, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_TESL)
TRANS(amand_db_d, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_TEUQ)
TRANS(amor_db_w, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_TESL)
TRANS(amor_db_d, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_TEUQ)
TRANS(amxor_db_w, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_TESL)
TRANS(amxor_db_d, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_TEUQ)
TRANS(ammax_db_w, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_TESL)
TRANS(ammax_db_d, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_TEUQ)
TRANS(ammin_db_w, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_TESL)
TRANS(ammin_db_d, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_TEUQ)
TRANS(ammax_db_wu, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_TESL)
TRANS(ammax_db_du, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_TEUQ)
TRANS(ammin_db_wu, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_TESL)
TRANS(ammin_db_du, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_TEUQ)
