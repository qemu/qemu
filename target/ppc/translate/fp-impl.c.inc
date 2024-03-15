/*
 * translate-fp.c
 *
 * Standard FPU translation
 */

static inline void gen_reset_fpstatus(void)
{
    gen_helper_reset_fpstatus(tcg_env);
}

static inline void gen_compute_fprf_float64(TCGv_i64 arg)
{
    gen_helper_compute_fprf_float64(tcg_env, arg);
    gen_helper_float_check_status(tcg_env);
}

#if defined(TARGET_PPC64)
static void gen_set_cr1_from_fpscr(DisasContext *ctx)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(tmp, cpu_fpscr);
    tcg_gen_shri_i32(cpu_crf[1], tmp, 28);
}
#else
static void gen_set_cr1_from_fpscr(DisasContext *ctx)
{
    tcg_gen_shri_tl(cpu_crf[1], cpu_fpscr, 28);
}
#endif

/***                       Floating-Point arithmetic                       ***/
static bool do_helper_acb(DisasContext *ctx, arg_A *a,
                          void (*helper)(TCGv_i64, TCGv_ptr, TCGv_i64,
                                         TCGv_i64, TCGv_i64))
{
    TCGv_i64 t0, t1, t2, t3;
    REQUIRE_INSNS_FLAGS(ctx, FLOAT);
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    t3 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, a->fra);
    get_fpr(t1, a->frc);
    get_fpr(t2, a->frb);
    helper(t3, tcg_env, t0, t1, t2);
    set_fpr(a->frt, t3);
    gen_compute_fprf_float64(t3);
    if (unlikely(a->rc)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

static bool do_helper_ab(DisasContext *ctx, arg_A_tab *a,
                         void (*helper)(TCGv_i64, TCGv_ptr, TCGv_i64,
                                        TCGv_i64))
{
    TCGv_i64 t0, t1, t2;
    REQUIRE_INSNS_FLAGS(ctx, FLOAT);
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, a->fra);
    get_fpr(t1, a->frb);
    helper(t2, tcg_env, t0, t1);
    set_fpr(a->frt, t2);
    gen_compute_fprf_float64(t2);
    if (unlikely(a->rc)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

static bool do_helper_ac(DisasContext *ctx, arg_A_tac *a,
                         void (*helper)(TCGv_i64, TCGv_ptr, TCGv_i64,
                                        TCGv_i64))
{
    TCGv_i64 t0, t1, t2;
    REQUIRE_INSNS_FLAGS(ctx, FLOAT);
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, a->fra);
    get_fpr(t1, a->frc);
    helper(t2, tcg_env, t0, t1);
    set_fpr(a->frt, t2);
    gen_compute_fprf_float64(t2);
    if (unlikely(a->rc)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

#define GEN_FLOAT_B(name, op2, op3, set_fprf, type)                           \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i64 t1;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    gen_reset_fpstatus();                                                     \
    get_fpr(t0, rB(ctx->opcode));                                             \
    gen_helper_f##name(t1, tcg_env, t0);                                      \
    set_fpr(rD(ctx->opcode), t1);                                             \
    if (set_fprf) {                                                           \
        gen_helper_compute_fprf_float64(tcg_env, t1);                         \
    }                                                                         \
    gen_helper_float_check_status(tcg_env);                                   \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
}

static bool do_helper_bs(DisasContext *ctx, arg_A_tb *a,
                         void (*helper)(TCGv_i64, TCGv_ptr, TCGv_i64))
{
    TCGv_i64 t0, t1;
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, a->frb);
    helper(t1, tcg_env, t0);
    set_fpr(a->frt, t1);
    gen_compute_fprf_float64(t1);
    if (unlikely(a->rc)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

static bool trans_FSEL(DisasContext *ctx, arg_A *a)
{
    TCGv_i64 t0, t1, t2;

    REQUIRE_INSNS_FLAGS(ctx, FLOAT_FSEL);
    REQUIRE_FPU(ctx);

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();

    get_fpr(t0, a->fra);
    get_fpr(t1, a->frb);
    get_fpr(t2, a->frc);

    gen_helper_FSEL(t0, t0, t1, t2);
    set_fpr(a->frt, t0);
    if (a->rc) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

static bool do_helper_fsqrt(DisasContext *ctx, arg_A_tb *a,
                            void (*helper)(TCGv_i64, TCGv_ptr, TCGv_i64))
{
    TCGv_i64 t0, t1;

    REQUIRE_INSNS_FLAGS(ctx, FLOAT_FSQRT);
    REQUIRE_FPU(ctx);

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    gen_reset_fpstatus();
    get_fpr(t0, a->frb);
    helper(t1, tcg_env, t0);
    set_fpr(a->frt, t1);
    gen_compute_fprf_float64(t1);
    if (unlikely(a->rc != 0)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

TRANS(FADD, do_helper_ab, gen_helper_FADD);
TRANS(FADDS, do_helper_ab, gen_helper_FADDS);
TRANS(FSUB, do_helper_ab, gen_helper_FSUB);
TRANS(FSUBS, do_helper_ab, gen_helper_FSUBS);
TRANS(FDIV, do_helper_ab, gen_helper_FDIV);
TRANS(FDIVS, do_helper_ab, gen_helper_FDIVS);
TRANS(FMUL, do_helper_ac, gen_helper_FMUL);
TRANS(FMULS, do_helper_ac, gen_helper_FMULS);

TRANS(FMADD, do_helper_acb, gen_helper_FMADD);
TRANS(FMADDS, do_helper_acb, gen_helper_FMADDS);
TRANS(FMSUB, do_helper_acb, gen_helper_FMSUB);
TRANS(FMSUBS, do_helper_acb, gen_helper_FMSUBS);

TRANS(FNMADD, do_helper_acb, gen_helper_FNMADD);
TRANS(FNMADDS, do_helper_acb, gen_helper_FNMADDS);
TRANS(FNMSUB, do_helper_acb, gen_helper_FNMSUB);
TRANS(FNMSUBS, do_helper_acb, gen_helper_FNMSUBS);

TRANS_FLAGS(FLOAT_EXT, FRE, do_helper_bs, gen_helper_FRE);
TRANS_FLAGS(FLOAT_FRES, FRES, do_helper_bs, gen_helper_FRES);
TRANS_FLAGS(FLOAT_FRSQRTE, FRSQRTE, do_helper_bs, gen_helper_FRSQRTE);
TRANS_FLAGS(FLOAT_FRSQRTES, FRSQRTES, do_helper_bs, gen_helper_FRSQRTES);

TRANS(FSQRT, do_helper_fsqrt, gen_helper_FSQRT);
TRANS(FSQRTS, do_helper_fsqrt, gen_helper_FSQRTS);

/***                     Floating-Point round & convert                    ***/
/* fctiw */
GEN_FLOAT_B(ctiw, 0x0E, 0x00, 0, PPC_FLOAT);
/* fctiwu */
GEN_FLOAT_B(ctiwu, 0x0E, 0x04, 0, PPC2_FP_CVT_ISA206);
/* fctiwz */
GEN_FLOAT_B(ctiwz, 0x0F, 0x00, 0, PPC_FLOAT);
/* fctiwuz */
GEN_FLOAT_B(ctiwuz, 0x0F, 0x04, 0, PPC2_FP_CVT_ISA206);
/* frsp */
GEN_FLOAT_B(rsp, 0x0C, 0x00, 1, PPC_FLOAT);
/* fcfid */
GEN_FLOAT_B(cfid, 0x0E, 0x1A, 1, PPC2_FP_CVT_S64);
/* fcfids */
GEN_FLOAT_B(cfids, 0x0E, 0x1A, 0, PPC2_FP_CVT_ISA206);
/* fcfidu */
GEN_FLOAT_B(cfidu, 0x0E, 0x1E, 0, PPC2_FP_CVT_ISA206);
/* fcfidus */
GEN_FLOAT_B(cfidus, 0x0E, 0x1E, 0, PPC2_FP_CVT_ISA206);
/* fctid */
GEN_FLOAT_B(ctid, 0x0E, 0x19, 0, PPC2_FP_CVT_S64);
/* fctidu */
GEN_FLOAT_B(ctidu, 0x0E, 0x1D, 0, PPC2_FP_CVT_ISA206);
/* fctidz */
GEN_FLOAT_B(ctidz, 0x0F, 0x19, 0, PPC2_FP_CVT_S64);
/* fctidu */
GEN_FLOAT_B(ctiduz, 0x0F, 0x1D, 0, PPC2_FP_CVT_ISA206);

/* frin */
GEN_FLOAT_B(rin, 0x08, 0x0C, 1, PPC_FLOAT_EXT);
/* friz */
GEN_FLOAT_B(riz, 0x08, 0x0D, 1, PPC_FLOAT_EXT);
/* frip */
GEN_FLOAT_B(rip, 0x08, 0x0E, 1, PPC_FLOAT_EXT);
/* frim */
GEN_FLOAT_B(rim, 0x08, 0x0F, 1, PPC_FLOAT_EXT);

static bool trans_FTDIV(DisasContext *ctx, arg_X_bf *a)
{
    TCGv_i64 t0, t1;
    REQUIRE_INSNS_FLAGS2(ctx, FP_TST_ISA206);
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, a->ra);
    get_fpr(t1, a->rb);
    gen_helper_FTDIV(cpu_crf[a->bf], t0, t1);
    return true;
}

static bool trans_FTSQRT(DisasContext *ctx, arg_X_bf_b *a)
{
    TCGv_i64 t0;
    REQUIRE_INSNS_FLAGS2(ctx, FP_TST_ISA206);
    REQUIRE_FPU(ctx);
    t0 = tcg_temp_new_i64();
    get_fpr(t0, a->rb);
    gen_helper_FTSQRT(cpu_crf[a->bf], t0);
    return true;
}

/***                         Floating-Point compare                        ***/

/* fcmpo */
static void gen_fcmpo(DisasContext *ctx)
{
    TCGv_i32 crf;
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    crf = tcg_constant_i32(crfD(ctx->opcode));
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_fcmpo(tcg_env, t0, t1, crf);
    gen_helper_float_check_status(tcg_env);
}

/* fcmpu */
static void gen_fcmpu(DisasContext *ctx)
{
    TCGv_i32 crf;
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    crf = tcg_constant_i32(crfD(ctx->opcode));
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_fcmpu(tcg_env, t0, t1, crf);
    gen_helper_float_check_status(tcg_env);
}

/***                         Floating-point move                           ***/
/* fabs */
/* XXX: beware that fabs never checks for NaNs nor update FPSCR */
static void gen_fabs(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    tcg_gen_andi_i64(t1, t0, ~(1ULL << 63));
    set_fpr(rD(ctx->opcode), t1);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
}

/* fmr  - fmr. */
/* XXX: beware that fmr never checks for NaNs nor update FPSCR */
static void gen_fmr(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    set_fpr(rD(ctx->opcode), t0);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
}

/* fnabs */
/* XXX: beware that fnabs never checks for NaNs nor update FPSCR */
static void gen_fnabs(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    tcg_gen_ori_i64(t1, t0, 1ULL << 63);
    set_fpr(rD(ctx->opcode), t1);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
}

/* fneg */
/* XXX: beware that fneg never checks for NaNs nor update FPSCR */
static void gen_fneg(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    tcg_gen_xori_i64(t1, t0, 1ULL << 63);
    set_fpr(rD(ctx->opcode), t1);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
}

/* fcpsgn: PowerPC 2.05 specification */
/* XXX: beware that fcpsgn never checks for NaNs nor update FPSCR */
static void gen_fcpsgn(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    TCGv_i64 t2;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    tcg_gen_deposit_i64(t2, t0, t1, 0, 63);
    set_fpr(rD(ctx->opcode), t2);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
}

static void gen_fmrgew(DisasContext *ctx)
{
    TCGv_i64 b0;
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    b0 = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    tcg_gen_shri_i64(b0, t0, 32);
    get_fpr(t0, rA(ctx->opcode));
    tcg_gen_deposit_i64(t1, t0, b0, 0, 32);
    set_fpr(rD(ctx->opcode), t1);
}

static void gen_fmrgow(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    TCGv_i64 t2;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    get_fpr(t1, rA(ctx->opcode));
    tcg_gen_deposit_i64(t2, t0, t1, 32, 32);
    set_fpr(rD(ctx->opcode), t2);
}

/***                  Floating-Point status & ctrl register                ***/

/* mcrfs */
static void gen_mcrfs(DisasContext *ctx)
{
    TCGv tmp = tcg_temp_new();
    TCGv_i32 tmask;
    TCGv_i64 tnew_fpscr = tcg_temp_new_i64();
    int bfa;
    int nibble;
    int shift;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    bfa = crfS(ctx->opcode);
    nibble = 7 - bfa;
    shift = 4 * nibble;
    tcg_gen_shri_tl(tmp, cpu_fpscr, shift);
    tcg_gen_trunc_tl_i32(cpu_crf[crfD(ctx->opcode)], tmp);
    tcg_gen_andi_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],
                     0xf);
    tcg_gen_extu_tl_i64(tnew_fpscr, cpu_fpscr);
    /* Only the exception bits (including FX) should be cleared if read */
    tcg_gen_andi_i64(tnew_fpscr, tnew_fpscr,
                     ~((0xF << shift) & FP_EX_CLEAR_BITS));
    /* FEX and VX need to be updated, so don't set fpscr directly */
    tmask = tcg_constant_i32(1 << nibble);
    gen_helper_store_fpscr(tcg_env, tnew_fpscr, tmask);
}

static TCGv_i64 place_from_fpscr(int rt, uint64_t mask)
{
    TCGv_i64 fpscr = tcg_temp_new_i64();
    TCGv_i64 fpscr_masked = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(fpscr, cpu_fpscr);
    tcg_gen_andi_i64(fpscr_masked, fpscr, mask);
    set_fpr(rt, fpscr_masked);

    return fpscr;
}

static void store_fpscr_masked(TCGv_i64 fpscr, uint64_t clear_mask,
                               TCGv_i64 set_mask, uint32_t store_mask)
{
    TCGv_i64 fpscr_masked = tcg_temp_new_i64();
    TCGv_i32 st_mask = tcg_constant_i32(store_mask);

    tcg_gen_andi_i64(fpscr_masked, fpscr, ~clear_mask);
    tcg_gen_or_i64(fpscr_masked, fpscr_masked, set_mask);
    gen_helper_store_fpscr(tcg_env, fpscr_masked, st_mask);
}

static bool trans_MFFS_ISA207(DisasContext *ctx, arg_X_t_rc *a)
{
    if (!(ctx->insns_flags2 & PPC2_ISA300)) {
        /*
         * Before Power ISA v3.0, MFFS bits 11~15 were reserved, any instruction
         * with OPCD=63 and XO=583 should be decoded as MFFS.
         */
        return trans_MFFS(ctx, a);
    }
    /*
     * For Power ISA v3.0+, return false and let the pattern group
     * select the correct instruction.
     */
    return false;
}

static bool trans_MFFS(DisasContext *ctx, arg_X_t_rc *a)
{
    REQUIRE_FPU(ctx);

    gen_reset_fpstatus();
    place_from_fpscr(a->rt, UINT64_MAX);
    if (a->rc) {
        gen_set_cr1_from_fpscr(ctx);
    }
    return true;
}

static bool trans_MFFSCE(DisasContext *ctx, arg_X_t *a)
{
    TCGv_i64 fpscr;

    REQUIRE_FPU(ctx);

    gen_reset_fpstatus();
    fpscr = place_from_fpscr(a->rt, UINT64_MAX);
    store_fpscr_masked(fpscr, FP_ENABLES, tcg_constant_i64(0), 0x0003);
    return true;
}

static bool trans_MFFSCRN(DisasContext *ctx, arg_X_tb *a)
{
    TCGv_i64 t1, fpscr;

    REQUIRE_FPU(ctx);

    t1 = tcg_temp_new_i64();
    get_fpr(t1, a->rb);
    tcg_gen_andi_i64(t1, t1, FP_RN);

    gen_reset_fpstatus();
    fpscr = place_from_fpscr(a->rt, FP_DRN | FP_ENABLES | FP_NI | FP_RN);
    store_fpscr_masked(fpscr, FP_RN, t1, 0x0001);
    return true;
}

static bool trans_MFFSCDRN(DisasContext *ctx, arg_X_tb *a)
{
    TCGv_i64 t1, fpscr;

    REQUIRE_FPU(ctx);

    t1 = tcg_temp_new_i64();
    get_fpr(t1, a->rb);
    tcg_gen_andi_i64(t1, t1, FP_DRN);

    gen_reset_fpstatus();
    fpscr = place_from_fpscr(a->rt, FP_DRN | FP_ENABLES | FP_NI | FP_RN);
    store_fpscr_masked(fpscr, FP_DRN, t1, 0x0100);
    return true;
}

static bool trans_MFFSCRNI(DisasContext *ctx, arg_X_imm2 *a)
{
    TCGv_i64 t1, fpscr;

    REQUIRE_FPU(ctx);

    t1 = tcg_temp_new_i64();
    tcg_gen_movi_i64(t1, a->imm);

    gen_reset_fpstatus();
    fpscr = place_from_fpscr(a->rt, FP_DRN | FP_ENABLES | FP_NI | FP_RN);
    store_fpscr_masked(fpscr, FP_RN, t1, 0x0001);
    return true;
}

static bool trans_MFFSCDRNI(DisasContext *ctx, arg_X_imm3 *a)
{
    TCGv_i64 t1, fpscr;

    REQUIRE_FPU(ctx);

    t1 = tcg_temp_new_i64();
    tcg_gen_movi_i64(t1, (uint64_t)a->imm << FPSCR_DRN0);

    gen_reset_fpstatus();
    fpscr = place_from_fpscr(a->rt, FP_DRN | FP_ENABLES | FP_NI | FP_RN);
    store_fpscr_masked(fpscr, FP_DRN, t1, 0x0100);
    return true;
}

static bool trans_MFFSL(DisasContext *ctx, arg_X_t *a)
{
    REQUIRE_FPU(ctx);

    gen_reset_fpstatus();
    place_from_fpscr(a->rt, FP_DRN | FP_STATUS | FP_ENABLES | FP_NI | FP_RN);
    return true;
}

/* mtfsb0 */
static void gen_mtfsb0(DisasContext *ctx)
{
    uint8_t crb;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    crb = 31 - crbD(ctx->opcode);
    gen_reset_fpstatus();
    if (likely(crb != FPSCR_FEX && crb != FPSCR_VX)) {
        gen_helper_fpscr_clrbit(tcg_env, tcg_constant_i32(crb));
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
}

/* mtfsb1 */
static void gen_mtfsb1(DisasContext *ctx)
{
    uint8_t crb;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    crb = 31 - crbD(ctx->opcode);
    /* XXX: we pretend we can only do IEEE floating-point computations */
    if (likely(crb != FPSCR_FEX && crb != FPSCR_VX && crb != FPSCR_NI)) {
        gen_helper_fpscr_setbit(tcg_env, tcg_constant_i32(crb));
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a deferred exception */
    gen_helper_fpscr_check_status(tcg_env);
}

/* mtfsf */
static void gen_mtfsf(DisasContext *ctx)
{
    TCGv_i32 t0;
    TCGv_i64 t1;
    int flm, l, w;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    flm = FPFLM(ctx->opcode);
    l = FPL(ctx->opcode);
    w = FPW(ctx->opcode);
    if (unlikely(w & !(ctx->insns_flags2 & PPC2_ISA205))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }
    if (!l) {
        t0 = tcg_constant_i32(flm << (w * 8));
    } else if (ctx->insns_flags2 & PPC2_ISA205) {
        t0 = tcg_constant_i32(0xffff);
    } else {
        t0 = tcg_constant_i32(0xff);
    }
    t1 = tcg_temp_new_i64();
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_store_fpscr(tcg_env, t1, t0);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a deferred exception */
    gen_helper_fpscr_check_status(tcg_env);
}

/* mtfsfi */
static void gen_mtfsfi(DisasContext *ctx)
{
    int bf, sh, w;
    TCGv_i64 t0;
    TCGv_i32 t1;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    w = FPW(ctx->opcode);
    bf = FPBF(ctx->opcode);
    if (unlikely(w & !(ctx->insns_flags2 & PPC2_ISA205))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }
    sh = (8 * w) + 7 - bf;
    t0 = tcg_constant_i64(((uint64_t)FPIMM(ctx->opcode)) << (4 * sh));
    t1 = tcg_constant_i32(1 << sh);
    gen_helper_store_fpscr(tcg_env, t0, t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a deferred exception */
    gen_helper_fpscr_check_status(tcg_env);
}

static void gen_qemu_ld32fs(DisasContext *ctx, TCGv_i64 dest, TCGv addr)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp, addr, ctx->mem_idx, DEF_MEMOP(MO_UL));
    gen_helper_todouble(dest, tmp);
}

/* lfdepx (external PID lfdx) */
static void gen_lfdepx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    CHK_SV(ctx);
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(t0, EA, PPC_TLB_EPID_LOAD, DEF_MEMOP(MO_UQ));
    set_fpr(rD(ctx->opcode), t0);
}

/* lfdp */
static void gen_lfdp(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_imm_index(ctx, EA, 0);
    t0 = tcg_temp_new_i64();
    /*
     * We only need to swap high and low halves. gen_qemu_ld64_i64
     * does necessary 64-bit byteswap already.
     */
    if (unlikely(ctx->le_mode)) {
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode) + 1, t0);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode), t0);
    } else {
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode), t0);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode) + 1, t0);
    }
}

/* lfdpx */
static void gen_lfdpx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    t0 = tcg_temp_new_i64();
    /*
     * We only need to swap high and low halves. gen_qemu_ld64_i64
     * does necessary 64-bit byteswap already.
     */
    if (unlikely(ctx->le_mode)) {
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode) + 1, t0);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode), t0);
    } else {
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode), t0);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64_i64(ctx, t0, EA);
        set_fpr(rD(ctx->opcode) + 1, t0);
    }
}

/* lfiwax */
static void gen_lfiwax(DisasContext *ctx)
{
    TCGv EA;
    TCGv t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new();
    t1 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld32s(ctx, t0, EA);
    tcg_gen_ext_tl_i64(t1, t0);
    set_fpr(rD(ctx->opcode), t1);
}

/* lfiwzx */
static void gen_lfiwzx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld32u_i64(ctx, t0, EA);
    set_fpr(rD(ctx->opcode), t0);
}

#define GEN_STXF(name, stop, opc2, opc3, type)                                \
static void glue(gen_, name##x)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    t0 = tcg_temp_new_i64();                                                  \
    gen_addr_reg_index(ctx, EA);                                              \
    get_fpr(t0, rS(ctx->opcode));                                             \
    gen_qemu_##stop(ctx, t0, EA);                                             \
}

static void gen_qemu_st32fs(DisasContext *ctx, TCGv_i64 src, TCGv addr)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_tosingle(tmp, src);
    tcg_gen_qemu_st_i32(tmp, addr, ctx->mem_idx, DEF_MEMOP(MO_UL));
}

/* stfdepx (external PID lfdx) */
static void gen_stfdepx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    CHK_SV(ctx);
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    get_fpr(t0, rD(ctx->opcode));
    tcg_gen_qemu_st_i64(t0, EA, PPC_TLB_EPID_STORE, DEF_MEMOP(MO_UQ));
}

/* stfdp */
static void gen_stfdp(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_imm_index(ctx, EA, 0);
    /*
     * We only need to swap high and low halves. gen_qemu_st64_i64
     * does necessary 64-bit byteswap already.
     */
    if (unlikely(ctx->le_mode)) {
        get_fpr(t0, rD(ctx->opcode) + 1);
        gen_qemu_st64_i64(ctx, t0, EA);
        tcg_gen_addi_tl(EA, EA, 8);
        get_fpr(t0, rD(ctx->opcode));
        gen_qemu_st64_i64(ctx, t0, EA);
    } else {
        get_fpr(t0, rD(ctx->opcode));
        gen_qemu_st64_i64(ctx, t0, EA);
        tcg_gen_addi_tl(EA, EA, 8);
        get_fpr(t0, rD(ctx->opcode) + 1);
        gen_qemu_st64_i64(ctx, t0, EA);
    }
}

/* stfdpx */
static void gen_stfdpx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    /*
     * We only need to swap high and low halves. gen_qemu_st64_i64
     * does necessary 64-bit byteswap already.
     */
    if (unlikely(ctx->le_mode)) {
        get_fpr(t0, rD(ctx->opcode) + 1);
        gen_qemu_st64_i64(ctx, t0, EA);
        tcg_gen_addi_tl(EA, EA, 8);
        get_fpr(t0, rD(ctx->opcode));
        gen_qemu_st64_i64(ctx, t0, EA);
    } else {
        get_fpr(t0, rD(ctx->opcode));
        gen_qemu_st64_i64(ctx, t0, EA);
        tcg_gen_addi_tl(EA, EA, 8);
        get_fpr(t0, rD(ctx->opcode) + 1);
        gen_qemu_st64_i64(ctx, t0, EA);
    }
}

/* Optional: */
static inline void gen_qemu_st32fiw(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_trunc_i64_tl(t0, arg1),
    gen_qemu_st32(ctx, t0, arg2);
}
/* stfiwx */
GEN_STXF(stfiw, st32fiw, 0x17, 0x1E, PPC_FLOAT_STFIWX);

/*            Floating-point Load/Store Instructions                         */
static bool do_lsfpsd(DisasContext *ctx, int rt, int ra, TCGv displ,
                      bool update, bool store, bool single)
{
    TCGv ea;
    TCGv_i64 t0;
    REQUIRE_INSNS_FLAGS(ctx, FLOAT);
    REQUIRE_FPU(ctx);
    if (update && ra == 0) {
        gen_invalid(ctx);
        return true;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new_i64();
    ea = do_ea_calc(ctx, ra, displ);
    if (store) {
        get_fpr(t0, rt);
        if (single) {
            gen_qemu_st32fs(ctx, t0, ea);
        } else {
            gen_qemu_st64_i64(ctx, t0, ea);
        }
    } else {
        if (single) {
            gen_qemu_ld32fs(ctx, t0, ea);
        } else {
            gen_qemu_ld64_i64(ctx, t0, ea);
        }
        set_fpr(rt, t0);
    }
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[ra], ea);
    }
    return true;
}

static bool do_lsfp_D(DisasContext *ctx, arg_D *a, bool update, bool store,
                      bool single)
{
    return do_lsfpsd(ctx, a->rt, a->ra, tcg_constant_tl(a->si), update, store,
                     single);
}

static bool do_lsfp_PLS_D(DisasContext *ctx, arg_PLS_D *a, bool update,
                          bool store, bool single)
{
    arg_D d;
    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }
    return do_lsfp_D(ctx, &d, update, store, single);
}

static bool do_lsfp_X(DisasContext *ctx, arg_X *a, bool update,
                      bool store, bool single)
{
    return do_lsfpsd(ctx, a->rt, a->ra, cpu_gpr[a->rb], update, store, single);
}

TRANS(LFS, do_lsfp_D, false, false, true)
TRANS(LFSU, do_lsfp_D, true, false, true)
TRANS(LFSX, do_lsfp_X, false, false, true)
TRANS(LFSUX, do_lsfp_X, true, false, true)
TRANS(PLFS, do_lsfp_PLS_D, false, false, true)

TRANS(LFD, do_lsfp_D, false, false, false)
TRANS(LFDU, do_lsfp_D, true, false, false)
TRANS(LFDX, do_lsfp_X, false, false, false)
TRANS(LFDUX, do_lsfp_X, true, false, false)
TRANS(PLFD, do_lsfp_PLS_D, false, false, false)

TRANS(STFS, do_lsfp_D, false, true, true)
TRANS(STFSU, do_lsfp_D, true, true, true)
TRANS(STFSX, do_lsfp_X, false, true, true)
TRANS(STFSUX, do_lsfp_X, true, true, true)
TRANS(PSTFS, do_lsfp_PLS_D, false, true, true)

TRANS(STFD, do_lsfp_D, false, true, false)
TRANS(STFDU, do_lsfp_D, true, true, false)
TRANS(STFDX, do_lsfp_X, false, true, false)
TRANS(STFDUX, do_lsfp_X, true, true, false)
TRANS(PSTFD, do_lsfp_PLS_D, false, true, false)

#undef GEN_FLOAT_B

#undef GEN_LDF
#undef GEN_LDUF
#undef GEN_LDUXF
#undef GEN_LDXF
#undef GEN_LDFS

#undef GEN_STF
#undef GEN_STUF
#undef GEN_STUXF
#undef GEN_STXF
#undef GEN_STFS
