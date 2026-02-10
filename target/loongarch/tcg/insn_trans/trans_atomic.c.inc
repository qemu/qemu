/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

static bool gen_ll(DisasContext *ctx, arg_rr_i *a, MemOp mop, bool acq)
{
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv t0 = make_address_i(ctx, src1, a->imm);
    TCGv_i128 t16 = tcg_temp_new_i128();
    TCGv mask = tcg_constant_tl(0xf);
    TCGv one = tcg_constant_tl(1);
    TCGv zero = tcg_constant_tl(0);
    TCGLabel *l1 = gen_new_label();
    TCGLabel *done = gen_new_label();

    if (avail_SCQ(ctx) && mop == MO_LEUQ) {
        /*
         * The LL.D+LD.D may be paired with SC.Q,
         * load 128-bit if aligned: (t0 & 0xf) == 0
         */
        tcg_gen_and_tl(t1, t0, mask);
        tcg_gen_brcond_tl(TCG_COND_EQ, t1, zero, l1);
        /* fallthrough if not aligned to 16B */
    }

    tcg_gen_qemu_ld_i64(t1, t0, ctx->mem_idx, mop | MO_ALIGN);
    tcg_gen_st_tl(t0, tcg_env, offsetof(CPULoongArchState, lladdr));
    tcg_gen_st_tl(t1, tcg_env, offsetof(CPULoongArchState, llval));
    gen_set_gpr(a->rd, t1, EXT_NONE);

    if (avail_SCQ(ctx) && mop == MO_LEUQ) {
        tcg_gen_br(done);

        gen_set_label(l1);

        /* Load 16B data and save into llval/llval_high */
        tcg_gen_qemu_ld_i128(t16, t0, ctx->mem_idx, MO_128 | MO_ALIGN);
        tcg_gen_st_tl(t0, tcg_env, offsetof(CPULoongArchState, lladdr));
        tcg_gen_extr_i128_i64(t1, t2, t16);
        tcg_gen_st_tl(t1, tcg_env, offsetof(CPULoongArchState, llval));
        tcg_gen_st_tl(t2, tcg_env, offsetof(CPULoongArchState, llval_high));
        tcg_gen_st_tl(one, tcg_env, offsetof(CPULoongArchState, llbit_scq));
        gen_set_gpr(a->rd, t1, EXT_NONE);

        gen_set_label(done);
    }

    if (acq) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }

    return true;
}

static bool gen_sc(DisasContext *ctx, arg_rr_i *a, MemOp mop, bool rel)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv t0 = tcg_temp_new();
    TCGv val = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    TCGLabel *l1 = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_addi_tl(t0, src1, a->imm);

    if (rel) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }

    if (avail_SCQ(ctx)) {
        tcg_gen_st_tl(zero, tcg_env, offsetof(CPULoongArchState, llbit_scq));
    }

    tcg_gen_brcond_tl(TCG_COND_EQ, t0, cpu_lladdr, l1);
    tcg_gen_movi_tl(dest, 0);
    tcg_gen_br(done);

    gen_set_label(l1);
    tcg_gen_mov_tl(val, src2);
    /* generate cmpxchg */
    tcg_gen_atomic_cmpxchg_tl(t0, cpu_lladdr, cpu_llval,
                              val, ctx->mem_idx, mop | MO_ALIGN);
    tcg_gen_setcond_tl(TCG_COND_EQ, dest, t0, cpu_llval);
    gen_set_label(done);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_sc_q(DisasContext *ctx, arg_rrr *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv src2 = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv src3 = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv_i128 t16 = tcg_temp_new_i128();
    TCGv_i128 c16 = tcg_temp_new_i128();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    TCGLabel *l1 = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_st_tl(zero, tcg_env, offsetof(CPULoongArchState, llbit_scq));
    tcg_gen_brcond_tl(TCG_COND_EQ, src1, cpu_lladdr, l1);
    tcg_gen_movi_tl(dest, 0);
    tcg_gen_br(done);

    gen_set_label(l1);
    tcg_gen_concat_i64_i128(t16, src2, src3);
    tcg_gen_concat_i64_i128(c16, cpu_llval,
                            cpu_llval_high);

    /* generate cmpxchg */
    tcg_gen_atomic_cmpxchg_i128(t16, cpu_lladdr, c16,
                              t16, ctx->mem_idx, mop | MO_ALIGN);

    /* check if success */
    tcg_gen_extr_i128_i64(t1, t2, t16);
    tcg_gen_xor_i64(t1, t1, cpu_llval);
    tcg_gen_xor_i64(t2, t2, cpu_llval_high);
    tcg_gen_or_i64(t1, t1, t2);
    tcg_gen_setcondi_i64(TCG_COND_EQ, dest, t1, 0);
    gen_set_label(done);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

static bool gen_cas(DisasContext *ctx, arg_rrr *a,
                    void (*func)(TCGv, TCGv, TCGv, TCGv, TCGArg, MemOp),
                    MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv val = gpr_src(ctx, a->rk, EXT_NONE);
    TCGv old = gpr_src(ctx, a->rd, EXT_NONE);

    addr = make_address_i(ctx, addr, 0);

    func(dest, addr, old, val, ctx->mem_idx, mop | MO_ALIGN);
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
                      "in atomic insn at pc=0x%" VADDR_PRIx "\n",
                      ctx->base.pc_next - 4);
        return false;
    }

    addr = make_address_i(ctx, addr, 0);

    func(dest, addr, val, ctx->mem_idx, mop | MO_ALIGN);
    gen_set_gpr(a->rd, dest, EXT_NONE);

    return true;
}

TRANS(ll_w, ALL, gen_ll, MO_LESL, false)
TRANS(sc_w, ALL, gen_sc, MO_LESL, false)
TRANS(ll_d, 64, gen_ll, MO_LEUQ, false)
TRANS(sc_d, 64, gen_sc, MO_LEUQ, false)
TRANS(sc_q, 64, gen_sc_q, MO_128)
TRANS(llacq_w, LLACQ_SCREL, gen_ll, MO_LESL, true)
TRANS(screl_w, LLACQ_SCREL, gen_sc, MO_LESL, true)
TRANS(llacq_d, LLACQ_SCREL_64, gen_ll, MO_LEUQ, true)
TRANS(screl_d, LLACQ_SCREL_64, gen_sc, MO_LEUQ, true)
TRANS(amcas_b, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_SB)
TRANS(amcas_h, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LESW)
TRANS(amcas_w, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LESL)
TRANS(amcas_d, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LEUQ)
TRANS(amcas_db_b, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_SB)
TRANS(amcas_db_h, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LESW)
TRANS(amcas_db_w, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LESL)
TRANS(amcas_db_d, LAMCAS, gen_cas, tcg_gen_atomic_cmpxchg_tl, MO_LEUQ)
TRANS(amswap_b, LAM_BH, gen_am, tcg_gen_atomic_xchg_tl, MO_SB)
TRANS(amswap_h, LAM_BH, gen_am, tcg_gen_atomic_xchg_tl, MO_LESW)
TRANS(amadd_b, LAM_BH, gen_am, tcg_gen_atomic_fetch_add_tl, MO_SB)
TRANS(amadd_h, LAM_BH, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LESW)
TRANS(amswap_db_b, LAM_BH, gen_am, tcg_gen_atomic_xchg_tl, MO_SB)
TRANS(amswap_db_h, LAM_BH, gen_am, tcg_gen_atomic_xchg_tl, MO_LESW)
TRANS(amadd_db_b, LAM_BH, gen_am, tcg_gen_atomic_fetch_add_tl, MO_SB)
TRANS(amadd_db_h, LAM_BH, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LESW)
TRANS(amswap_w, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_LESL)
TRANS64(amswap_d, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_LEUQ)
TRANS(amadd_w, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LESL)
TRANS64(amadd_d, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LEUQ)
TRANS(amand_w, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_LESL)
TRANS64(amand_d, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_LEUQ)
TRANS(amor_w, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_LESL)
TRANS64(amor_d, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_LEUQ)
TRANS(amxor_w, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_LESL)
TRANS64(amxor_d, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_LEUQ)
TRANS(ammax_w, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_LESL)
TRANS64(ammax_d, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_LEUQ)
TRANS(ammin_w, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_LESL)
TRANS64(ammin_d, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_LEUQ)
TRANS(ammax_wu, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_LESL)
TRANS64(ammax_du, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_LEUQ)
TRANS(ammin_wu, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_LESL)
TRANS64(ammin_du, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_LEUQ)
TRANS(amswap_db_w, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_LESL)
TRANS64(amswap_db_d, LAM, gen_am, tcg_gen_atomic_xchg_tl, MO_LEUQ)
TRANS(amadd_db_w, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LESL)
TRANS64(amadd_db_d, LAM, gen_am, tcg_gen_atomic_fetch_add_tl, MO_LEUQ)
TRANS(amand_db_w, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_LESL)
TRANS64(amand_db_d, LAM, gen_am, tcg_gen_atomic_fetch_and_tl, MO_LEUQ)
TRANS(amor_db_w, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_LESL)
TRANS64(amor_db_d, LAM, gen_am, tcg_gen_atomic_fetch_or_tl, MO_LEUQ)
TRANS(amxor_db_w, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_LESL)
TRANS64(amxor_db_d, LAM, gen_am, tcg_gen_atomic_fetch_xor_tl, MO_LEUQ)
TRANS(ammax_db_w, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_LESL)
TRANS64(ammax_db_d, LAM, gen_am, tcg_gen_atomic_fetch_smax_tl, MO_LEUQ)
TRANS(ammin_db_w, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_LESL)
TRANS64(ammin_db_d, LAM, gen_am, tcg_gen_atomic_fetch_smin_tl, MO_LEUQ)
TRANS(ammax_db_wu, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_LESL)
TRANS64(ammax_db_du, LAM, gen_am, tcg_gen_atomic_fetch_umax_tl, MO_LEUQ)
TRANS(ammin_db_wu, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_LESL)
TRANS64(ammin_db_du, LAM, gen_am, tcg_gen_atomic_fetch_umin_tl, MO_LEUQ)
