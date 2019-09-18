/*
 * translate-fp.c
 *
 * Standard FPU translation
 */

static inline void gen_reset_fpstatus(void)
{
    gen_helper_reset_fpstatus(cpu_env);
}

static inline void gen_compute_fprf_float64(TCGv_i64 arg)
{
    gen_helper_compute_fprf_float64(cpu_env, arg);
    gen_helper_float_check_status(cpu_env);
}

#if defined(TARGET_PPC64)
static void gen_set_cr1_from_fpscr(DisasContext *ctx)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(tmp, cpu_fpscr);
    tcg_gen_shri_i32(cpu_crf[1], tmp, 28);
    tcg_temp_free_i32(tmp);
}
#else
static void gen_set_cr1_from_fpscr(DisasContext *ctx)
{
    tcg_gen_shri_tl(cpu_crf[1], cpu_fpscr, 28);
}
#endif

/***                       Floating-Point arithmetic                       ***/
#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat, set_fprf, type)           \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i64 t1;                                                              \
    TCGv_i64 t2;                                                              \
    TCGv_i64 t3;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    t2 = tcg_temp_new_i64();                                                  \
    t3 = tcg_temp_new_i64();                                                  \
    gen_reset_fpstatus();                                                     \
    get_fpr(t0, rA(ctx->opcode));                                             \
    get_fpr(t1, rC(ctx->opcode));                                             \
    get_fpr(t2, rB(ctx->opcode));                                             \
    gen_helper_f##op(t3, cpu_env, t0, t1, t2);                                \
    if (isfloat) {                                                            \
        gen_helper_frsp(t3, cpu_env, t3);                                     \
    }                                                                         \
    set_fpr(rD(ctx->opcode), t3);                                             \
    if (set_fprf) {                                                           \
        gen_compute_fprf_float64(t3);                                         \
    }                                                                         \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
    tcg_temp_free_i64(t2);                                                    \
    tcg_temp_free_i64(t3);                                                    \
}

#define GEN_FLOAT_ACB(name, op2, set_fprf, type)                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0, set_fprf, type);                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1, set_fprf, type);

#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i64 t1;                                                              \
    TCGv_i64 t2;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    t2 = tcg_temp_new_i64();                                                  \
    gen_reset_fpstatus();                                                     \
    get_fpr(t0, rA(ctx->opcode));                                             \
    get_fpr(t1, rB(ctx->opcode));                                             \
    gen_helper_f##op(t2, cpu_env, t0, t1);                                    \
    if (isfloat) {                                                            \
        gen_helper_frsp(t2, cpu_env, t2);                                     \
    }                                                                         \
    set_fpr(rD(ctx->opcode), t2);                                             \
    if (set_fprf) {                                                           \
        gen_compute_fprf_float64(t2);                                         \
    }                                                                         \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
    tcg_temp_free_i64(t2);                                                    \
}
#define GEN_FLOAT_AB(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0, set_fprf, type);               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1, set_fprf, type);

#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i64 t1;                                                              \
    TCGv_i64 t2;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    t2 = tcg_temp_new_i64();                                                  \
    gen_reset_fpstatus();                                                     \
    get_fpr(t0, rA(ctx->opcode));                                             \
    get_fpr(t1, rC(ctx->opcode));                                             \
    gen_helper_f##op(t2, cpu_env, t0, t1);                                    \
    if (isfloat) {                                                            \
        gen_helper_frsp(t2, cpu_env, t2);                                     \
    }                                                                         \
    set_fpr(rD(ctx->opcode), t2);                                             \
    if (set_fprf) {                                                           \
        gen_compute_fprf_float64(t2);                                         \
    }                                                                         \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
    tcg_temp_free_i64(t2);                                                    \
}
#define GEN_FLOAT_AC(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0, set_fprf, type);               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1, set_fprf, type);

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
    gen_helper_f##name(t1, cpu_env, t0);                                      \
    set_fpr(rD(ctx->opcode), t1);                                             \
    if (set_fprf) {                                                           \
        gen_compute_fprf_float64(t1);                                         \
    }                                                                         \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}

#define GEN_FLOAT_BS(name, op1, op2, set_fprf, type)                          \
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
    gen_helper_f##name(t1, cpu_env, t0);                                      \
    set_fpr(rD(ctx->opcode), t1);                                             \
    if (set_fprf) {                                                           \
        gen_compute_fprf_float64(t1);                                         \
    }                                                                         \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_cr1_from_fpscr(ctx);                                          \
    }                                                                         \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}

/* fadd - fadds */
GEN_FLOAT_AB(add, 0x15, 0x000007C0, 1, PPC_FLOAT);
/* fdiv - fdivs */
GEN_FLOAT_AB(div, 0x12, 0x000007C0, 1, PPC_FLOAT);
/* fmul - fmuls */
GEN_FLOAT_AC(mul, 0x19, 0x0000F800, 1, PPC_FLOAT);

/* fre */
GEN_FLOAT_BS(re, 0x3F, 0x18, 1, PPC_FLOAT_EXT);

/* fres */
GEN_FLOAT_BS(res, 0x3B, 0x18, 1, PPC_FLOAT_FRES);

/* frsqrte */
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A, 1, PPC_FLOAT_FRSQRTE);

/* frsqrtes */
static void gen_frsqrtes(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, rB(ctx->opcode));
    gen_helper_frsqrte(t1, cpu_env, t0);
    gen_helper_frsp(t1, cpu_env, t1);
    set_fpr(rD(ctx->opcode), t1);
    gen_compute_fprf_float64(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* fsel */
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0, 0, PPC_FLOAT_FSEL);
/* fsub - fsubs */
GEN_FLOAT_AB(sub, 0x14, 0x000007C0, 1, PPC_FLOAT);
/* Optional: */

/* fsqrt */
static void gen_fsqrt(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, rB(ctx->opcode));
    gen_helper_fsqrt(t1, cpu_env, t0);
    set_fpr(rD(ctx->opcode), t1);
    gen_compute_fprf_float64(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static void gen_fsqrts(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    get_fpr(t0, rB(ctx->opcode));
    gen_helper_fsqrt(t1, cpu_env, t0);
    gen_helper_frsp(t1, cpu_env, t1);
    set_fpr(rD(ctx->opcode), t1);
    gen_compute_fprf_float64(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_cr1_from_fpscr(ctx);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd - fmadds */
GEN_FLOAT_ACB(madd, 0x1D, 1, PPC_FLOAT);
/* fmsub - fmsubs */
GEN_FLOAT_ACB(msub, 0x1C, 1, PPC_FLOAT);
/* fnmadd - fnmadds */
GEN_FLOAT_ACB(nmadd, 0x1F, 1, PPC_FLOAT);
/* fnmsub - fnmsubs */
GEN_FLOAT_ACB(nmsub, 0x1E, 1, PPC_FLOAT);

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

static void gen_ftdiv(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_ftdiv(cpu_crf[crfD(ctx->opcode)], t0, t1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static void gen_ftsqrt(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    get_fpr(t0, rB(ctx->opcode));
    gen_helper_ftsqrt(cpu_crf[crfD(ctx->opcode)], t0);
    tcg_temp_free_i64(t0);
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
    crf = tcg_const_i32(crfD(ctx->opcode));
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_fcmpo(cpu_env, t0, t1, crf);
    tcg_temp_free_i32(crf);
    gen_helper_float_check_status(cpu_env);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    crf = tcg_const_i32(crfD(ctx->opcode));
    get_fpr(t0, rA(ctx->opcode));
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_fcmpu(cpu_env, t0, t1, crf);
    tcg_temp_free_i32(crf);
    gen_helper_float_check_status(cpu_env);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free_i64(t0);
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
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
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
    tcg_temp_free_i64(b0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
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
    tcg_temp_free(tmp);
    tcg_gen_extu_tl_i64(tnew_fpscr, cpu_fpscr);
    /* Only the exception bits (including FX) should be cleared if read */
    tcg_gen_andi_i64(tnew_fpscr, tnew_fpscr,
                     ~((0xF << shift) & FP_EX_CLEAR_BITS));
    /* FEX and VX need to be updated, so don't set fpscr directly */
    tmask = tcg_const_i32(1 << nibble);
    gen_helper_store_fpscr(cpu_env, tnew_fpscr, tmask);
    tcg_temp_free_i32(tmask);
    tcg_temp_free_i64(tnew_fpscr);
}

/* mffs */
static void gen_mffs(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    tcg_gen_extu_tl_i64(t0, cpu_fpscr);
    set_fpr(rD(ctx->opcode), t0);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_cr1_from_fpscr(ctx);
    }
    tcg_temp_free_i64(t0);
}

/* mffsl */
static void gen_mffsl(DisasContext *ctx)
{
    TCGv_i64 t0;

    if (unlikely(!(ctx->insns_flags2 & PPC2_ISA300))) {
        return gen_mffs(ctx);
    }

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    t0 = tcg_temp_new_i64();
    gen_reset_fpstatus();
    tcg_gen_extu_tl_i64(t0, cpu_fpscr);
    /* Mask everything except mode, status, and enables.  */
    tcg_gen_andi_i64(t0, t0, FP_DRN | FP_STATUS | FP_ENABLES | FP_RN);
    set_fpr(rD(ctx->opcode), t0);
    tcg_temp_free_i64(t0);
}

/* mffsce */
static void gen_mffsce(DisasContext *ctx)
{
    TCGv_i64 t0;
    TCGv_i32 mask;

    if (unlikely(!(ctx->insns_flags2 & PPC2_ISA300))) {
        return gen_mffs(ctx);
    }

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }

    t0 = tcg_temp_new_i64();

    gen_reset_fpstatus();
    tcg_gen_extu_tl_i64(t0, cpu_fpscr);
    set_fpr(rD(ctx->opcode), t0);

    /* Clear exception enable bits in the FPSCR.  */
    tcg_gen_andi_i64(t0, t0, ~FP_ENABLES);
    mask = tcg_const_i32(0x0003);
    gen_helper_store_fpscr(cpu_env, t0, mask);

    tcg_temp_free_i32(mask);
    tcg_temp_free_i64(t0);
}

static void gen_helper_mffscrn(DisasContext *ctx, TCGv_i64 t1)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i32 mask = tcg_const_i32(0x0001);

    gen_reset_fpstatus();
    tcg_gen_extu_tl_i64(t0, cpu_fpscr);
    tcg_gen_andi_i64(t0, t0, FP_DRN | FP_ENABLES | FP_RN);
    set_fpr(rD(ctx->opcode), t0);

    /* Mask FPSCR value to clear RN.  */
    tcg_gen_andi_i64(t0, t0, ~FP_RN);

    /* Merge RN into FPSCR value.  */
    tcg_gen_or_i64(t0, t0, t1);

    gen_helper_store_fpscr(cpu_env, t0, mask);

    tcg_temp_free_i32(mask);
    tcg_temp_free_i64(t0);
}

/* mffscrn */
static void gen_mffscrn(DisasContext *ctx)
{
    TCGv_i64 t1;

    if (unlikely(!(ctx->insns_flags2 & PPC2_ISA300))) {
        return gen_mffs(ctx);
    }

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }

    t1 = tcg_temp_new_i64();
    get_fpr(t1, rB(ctx->opcode));
    /* Mask FRB to get just RN.  */
    tcg_gen_andi_i64(t1, t1, FP_RN);

    gen_helper_mffscrn(ctx, t1);

    tcg_temp_free_i64(t1);
}

/* mffscrni */
static void gen_mffscrni(DisasContext *ctx)
{
    TCGv_i64 t1;

    if (unlikely(!(ctx->insns_flags2 & PPC2_ISA300))) {
        return gen_mffs(ctx);
    }

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }

    t1 = tcg_const_i64((uint64_t)RM(ctx->opcode));

    gen_helper_mffscrn(ctx, t1);

    tcg_temp_free_i64(t1);
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
        TCGv_i32 t0;
        t0 = tcg_const_i32(crb);
        gen_helper_fpscr_clrbit(cpu_env, t0);
        tcg_temp_free_i32(t0);
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
    gen_reset_fpstatus();
    /* XXX: we pretend we can only do IEEE floating-point computations */
    if (likely(crb != FPSCR_FEX && crb != FPSCR_VX && crb != FPSCR_NI)) {
        TCGv_i32 t0;
        t0 = tcg_const_i32(crb);
        gen_helper_fpscr_setbit(cpu_env, t0);
        tcg_temp_free_i32(t0);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
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
    gen_reset_fpstatus();
    if (l) {
        t0 = tcg_const_i32((ctx->insns_flags2 & PPC2_ISA205) ? 0xffff : 0xff);
    } else {
        t0 = tcg_const_i32(flm << (w * 8));
    }
    t1 = tcg_temp_new_i64();
    get_fpr(t1, rB(ctx->opcode));
    gen_helper_store_fpscr(cpu_env, t1, t0);
    tcg_temp_free_i32(t0);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
    tcg_temp_free_i64(t1);
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
    gen_reset_fpstatus();
    t0 = tcg_const_i64(((uint64_t)FPIMM(ctx->opcode)) << (4 * sh));
    t1 = tcg_const_i32(1 << sh);
    gen_helper_store_fpscr(cpu_env, t0, t1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
}

/***                         Floating-point load                           ***/
#define GEN_LDF(name, ldop, opc, type)                                        \
static void glue(gen_, name)(DisasContext *ctx)                               \
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
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##ldop(ctx, t0, EA);                                             \
    set_fpr(rD(ctx->opcode), t0);                                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_LDUF(name, ldop, opc, type)                                       \
static void glue(gen_, name##u)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    t0 = tcg_temp_new_i64();                                                  \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##ldop(ctx, t0, EA);                                             \
    set_fpr(rD(ctx->opcode), t0);                                             \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_LDUXF(name, ldop, opc, type)                                      \
static void glue(gen_, name##ux)(DisasContext *ctx)                           \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, t0, EA);                                             \
    set_fpr(rD(ctx->opcode), t0);                                             \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_LDXF(name, ldop, opc2, opc3, type)                                \
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
    gen_qemu_##ldop(ctx, t0, EA);                                             \
    set_fpr(rD(ctx->opcode), t0);                                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_LDFS(name, ldop, op, type)                                        \
GEN_LDF(name, ldop, op | 0x20, type);                                         \
GEN_LDUF(name, ldop, op | 0x21, type);                                        \
GEN_LDUXF(name, ldop, op | 0x01, type);                                       \
GEN_LDXF(name, ldop, 0x17, op | 0x00, type)

static void gen_qemu_ld32fs(DisasContext *ctx, TCGv_i64 dest, TCGv addr)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(tmp, addr, ctx->mem_idx, DEF_MEMOP(MO_UL));
    gen_helper_todouble(dest, tmp);
    tcg_temp_free_i32(tmp);
}

 /* lfd lfdu lfdux lfdx */
GEN_LDFS(lfd, ld64_i64, 0x12, PPC_FLOAT);
 /* lfs lfsu lfsux lfsx */
GEN_LDFS(lfs, ld32fs, 0x10, PPC_FLOAT);

/* lfdepx (external PID lfdx) */
static void gen_lfdepx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    CHK_SV;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(t0, EA, PPC_TLB_EPID_LOAD, DEF_MEMOP(MO_Q));
    set_fpr(rD(ctx->opcode), t0);
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
    tcg_temp_free(EA);
    tcg_temp_free(t0);
    tcg_temp_free_i64(t1);
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
}
/***                         Floating-point store                          ***/
#define GEN_STF(name, stop, opc, type)                                        \
static void glue(gen_, name)(DisasContext *ctx)                               \
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
    gen_addr_imm_index(ctx, EA, 0);                                           \
    get_fpr(t0, rS(ctx->opcode));                                             \
    gen_qemu_##stop(ctx, t0, EA);                                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_STUF(name, stop, opc, type)                                       \
static void glue(gen_, name##u)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    t0 = tcg_temp_new_i64();                                                  \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    get_fpr(t0, rS(ctx->opcode));                                             \
    gen_qemu_##stop(ctx, t0, EA);                                             \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_STUXF(name, stop, opc, type)                                      \
static void glue(gen_, name##ux)(DisasContext *ctx)                           \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    t0 = tcg_temp_new_i64();                                                  \
    gen_addr_reg_index(ctx, EA);                                              \
    get_fpr(t0, rS(ctx->opcode));                                             \
    gen_qemu_##stop(ctx, t0, EA);                                             \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
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
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(t0);                                                    \
}

#define GEN_STFS(name, stop, op, type)                                        \
GEN_STF(name, stop, op | 0x20, type);                                         \
GEN_STUF(name, stop, op | 0x21, type);                                        \
GEN_STUXF(name, stop, op | 0x01, type);                                       \
GEN_STXF(name, stop, 0x17, op | 0x00, type)

static void gen_qemu_st32fs(DisasContext *ctx, TCGv_i64 src, TCGv addr)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_tosingle(tmp, src);
    tcg_gen_qemu_st_i32(tmp, addr, ctx->mem_idx, DEF_MEMOP(MO_UL));
    tcg_temp_free_i32(tmp);
}

/* stfd stfdu stfdux stfdx */
GEN_STFS(stfd, st64_i64, 0x16, PPC_FLOAT);
/* stfs stfsu stfsux stfsx */
GEN_STFS(stfs, st32fs, 0x14, PPC_FLOAT);

/* stfdepx (external PID lfdx) */
static void gen_stfdepx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    CHK_SV;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, EA);
    get_fpr(t0, rD(ctx->opcode));
    tcg_gen_qemu_st_i64(t0, EA, PPC_TLB_EPID_STORE, DEF_MEMOP(MO_Q));
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
}

/* Optional: */
static inline void gen_qemu_st32fiw(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_trunc_i64_tl(t0, arg1),
    gen_qemu_st32(ctx, t0, arg2);
    tcg_temp_free(t0);
}
/* stfiwx */
GEN_STXF(stfiw, st32fiw, 0x17, 0x1E, PPC_FLOAT_STFIWX);

/* POWER2 specific instructions */
/* Quad manipulation (load/store two floats at a time) */

/* lfq */
static void gen_lfq(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    TCGv_i64 t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    t1 = tcg_temp_new_i64();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_ld64_i64(ctx, t1, t0);
    set_fpr(rd, t1);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_ld64_i64(ctx, t1, t0);
    set_fpr((rd + 1) % 32, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i64(t1);
}

/* lfqu */
static void gen_lfqu(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    TCGv_i64 t2;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    t2 = tcg_temp_new_i64();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_ld64_i64(ctx, t2, t0);
    set_fpr(rd, t2);
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_ld64_i64(ctx, t2, t1);
    set_fpr((rd + 1) % 32, t2);
    if (ra != 0) {
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free_i64(t2);
}

/* lfqux */
static void gen_lfqux(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    gen_set_access_type(ctx, ACCESS_FLOAT);
    TCGv t0, t1;
    TCGv_i64 t2;
    t2 = tcg_temp_new_i64();
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld64_i64(ctx, t2, t0);
    set_fpr(rd, t2);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_ld64_i64(ctx, t2, t1);
    set_fpr((rd + 1) % 32, t2);
    tcg_temp_free(t1);
    if (ra != 0) {
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    }
    tcg_temp_free(t0);
    tcg_temp_free_i64(t2);
}

/* lfqx */
static void gen_lfqx(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    TCGv_i64 t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    t1 = tcg_temp_new_i64();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld64_i64(ctx, t1, t0);
    set_fpr(rd, t1);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_ld64_i64(ctx, t1, t0);
    set_fpr((rd + 1) % 32, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i64(t1);
}

/* stfq */
static void gen_stfq(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    TCGv_i64 t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    t1 = tcg_temp_new_i64();
    gen_addr_imm_index(ctx, t0, 0);
    get_fpr(t1, rd);
    gen_qemu_st64_i64(ctx, t1, t0);
    gen_addr_add(ctx, t0, t0, 8);
    get_fpr(t1, (rd + 1) % 32);
    gen_qemu_st64_i64(ctx, t1, t0);
    tcg_temp_free(t0);
    tcg_temp_free_i64(t1);
}

/* stfqu */
static void gen_stfqu(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    TCGv_i64 t2;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t2 = tcg_temp_new_i64();
    t0 = tcg_temp_new();
    gen_addr_imm_index(ctx, t0, 0);
    get_fpr(t2, rd);
    gen_qemu_st64_i64(ctx, t2, t0);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    get_fpr(t2, (rd + 1) % 32);
    gen_qemu_st64_i64(ctx, t2, t1);
    tcg_temp_free(t1);
    if (ra != 0) {
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    }
    tcg_temp_free(t0);
    tcg_temp_free_i64(t2);
}

/* stfqux */
static void gen_stfqux(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    TCGv_i64 t2;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t2 = tcg_temp_new_i64();
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    get_fpr(t2, rd);
    gen_qemu_st64_i64(ctx, t2, t0);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    get_fpr(t2, (rd + 1) % 32);
    gen_qemu_st64_i64(ctx, t2, t1);
    tcg_temp_free(t1);
    if (ra != 0) {
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    }
    tcg_temp_free(t0);
    tcg_temp_free_i64(t2);
}

/* stfqx */
static void gen_stfqx(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    TCGv_i64 t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t1 = tcg_temp_new_i64();
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    get_fpr(t1, rd);
    gen_qemu_st64_i64(ctx, t1, t0);
    gen_addr_add(ctx, t0, t0, 8);
    get_fpr(t1, (rd + 1) % 32);
    gen_qemu_st64_i64(ctx, t1, t0);
    tcg_temp_free(t0);
    tcg_temp_free_i64(t1);
}

#undef _GEN_FLOAT_ACB
#undef GEN_FLOAT_ACB
#undef _GEN_FLOAT_AB
#undef GEN_FLOAT_AB
#undef _GEN_FLOAT_AC
#undef GEN_FLOAT_AC
#undef GEN_FLOAT_B
#undef GEN_FLOAT_BS

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
