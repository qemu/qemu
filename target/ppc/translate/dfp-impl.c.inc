/*** Decimal Floating Point ***/

static inline TCGv_ptr gen_fprp_ptr(int reg)
{
    TCGv_ptr r = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(r, cpu_env, offsetof(CPUPPCState, vsr[reg].u64[0]));
    return r;
}

#define TRANS_DFP_T_A_B_Rc(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rt, ra, rb;                                     \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rt = gen_fprp_ptr(a->rt);                                \
    ra = gen_fprp_ptr(a->ra);                                \
    rb = gen_fprp_ptr(a->rb);                                \
    gen_helper_##NAME(cpu_env, rt, ra, rb);                  \
    if (unlikely(a->rc)) {                                   \
        gen_set_cr1_from_fpscr(ctx);                         \
    }                                                        \
    tcg_temp_free_ptr(rt);                                   \
    tcg_temp_free_ptr(ra);                                   \
    tcg_temp_free_ptr(rb);                                   \
    return true;                                             \
}

#define TRANS_DFP_BF_A_B(NAME)                               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr ra, rb;                                         \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    ra = gen_fprp_ptr(a->ra);                                \
    rb = gen_fprp_ptr(a->rb);                                \
    gen_helper_##NAME(cpu_crf[a->bf],                        \
                      cpu_env, ra, rb);                      \
    tcg_temp_free_ptr(ra);                                   \
    tcg_temp_free_ptr(rb);                                   \
    return true;                                             \
}

#define TRANS_DFP_BF_I_B(NAME)                               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rb;                                             \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rb = gen_fprp_ptr(a->rb);                                \
    gen_helper_##NAME(cpu_crf[a->bf],                        \
                      cpu_env, tcg_constant_i32(a->uim), rb);\
    tcg_temp_free_ptr(rb);                                   \
    return true;                                             \
}

#define TRANS_DFP_BF_A_DCM(NAME)                             \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr ra;                                             \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    ra = gen_fprp_ptr(a->fra);                               \
    gen_helper_##NAME(cpu_crf[a->bf],                        \
                      cpu_env, ra, tcg_constant_i32(a->dm)); \
    tcg_temp_free_ptr(ra);                                   \
    return true;                                             \
}

#define TRANS_DFP_T_B_U32_U32_Rc(NAME, U32F1, U32F2)         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rt, rb;                                         \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rt = gen_fprp_ptr(a->frt);                               \
    rb = gen_fprp_ptr(a->frb);                               \
    gen_helper_##NAME(cpu_env, rt, rb,                       \
                      tcg_constant_i32(a->U32F1),            \
                      tcg_constant_i32(a->U32F2));           \
    if (unlikely(a->rc)) {                                   \
        gen_set_cr1_from_fpscr(ctx);                         \
    }                                                        \
    tcg_temp_free_ptr(rt);                                   \
    tcg_temp_free_ptr(rb);                                   \
    return true;                                             \
}

#define GEN_DFP_T_A_B_I32_Rc(name, i32fld)       \
static void gen_##name(DisasContext *ctx)        \
{                                                \
    TCGv_ptr rt, ra, rb;                         \
    TCGv_i32 i32;                                \
    if (unlikely(!ctx->fpu_enabled)) {           \
        gen_exception(ctx, POWERPC_EXCP_FPU);    \
        return;                                  \
    }                                            \
    rt = gen_fprp_ptr(rD(ctx->opcode));          \
    ra = gen_fprp_ptr(rA(ctx->opcode));          \
    rb = gen_fprp_ptr(rB(ctx->opcode));          \
    i32 = tcg_const_i32(i32fld(ctx->opcode));    \
    gen_helper_##name(cpu_env, rt, ra, rb, i32); \
    if (unlikely(Rc(ctx->opcode) != 0)) {        \
        gen_set_cr1_from_fpscr(ctx);             \
    }                                            \
    tcg_temp_free_ptr(rt);                       \
    tcg_temp_free_ptr(rb);                       \
    tcg_temp_free_ptr(ra);                       \
    tcg_temp_free_i32(i32);                      \
    }

#define GEN_DFP_T_B_Rc(name)                     \
static void gen_##name(DisasContext *ctx)        \
{                                                \
    TCGv_ptr rt, rb;                             \
    if (unlikely(!ctx->fpu_enabled)) {           \
        gen_exception(ctx, POWERPC_EXCP_FPU);    \
        return;                                  \
    }                                            \
    rt = gen_fprp_ptr(rD(ctx->opcode));          \
    rb = gen_fprp_ptr(rB(ctx->opcode));          \
    gen_helper_##name(cpu_env, rt, rb);          \
    if (unlikely(Rc(ctx->opcode) != 0)) {        \
        gen_set_cr1_from_fpscr(ctx);             \
    }                                            \
    tcg_temp_free_ptr(rt);                       \
    tcg_temp_free_ptr(rb);                       \
    }

#define GEN_DFP_T_FPR_I32_Rc(name, fprfld, i32fld) \
static void gen_##name(DisasContext *ctx)          \
{                                                  \
    TCGv_ptr rt, rs;                               \
    TCGv_i32 i32;                                  \
    if (unlikely(!ctx->fpu_enabled)) {             \
        gen_exception(ctx, POWERPC_EXCP_FPU);      \
        return;                                    \
    }                                              \
    rt = gen_fprp_ptr(rD(ctx->opcode));            \
    rs = gen_fprp_ptr(fprfld(ctx->opcode));        \
    i32 = tcg_const_i32(i32fld(ctx->opcode));      \
    gen_helper_##name(cpu_env, rt, rs, i32);       \
    if (unlikely(Rc(ctx->opcode) != 0)) {          \
        gen_set_cr1_from_fpscr(ctx);               \
    }                                              \
    tcg_temp_free_ptr(rt);                         \
    tcg_temp_free_ptr(rs);                         \
    tcg_temp_free_i32(i32);                        \
}

TRANS_DFP_T_A_B_Rc(DADD)
TRANS_DFP_T_A_B_Rc(DADDQ)
TRANS_DFP_T_A_B_Rc(DSUB)
TRANS_DFP_T_A_B_Rc(DSUBQ)
TRANS_DFP_T_A_B_Rc(DMUL)
TRANS_DFP_T_A_B_Rc(DMULQ)
TRANS_DFP_T_A_B_Rc(DDIV)
TRANS_DFP_T_A_B_Rc(DDIVQ)
TRANS_DFP_BF_A_B(DCMPU)
TRANS_DFP_BF_A_B(DCMPUQ)
TRANS_DFP_BF_A_B(DCMPO)
TRANS_DFP_BF_A_B(DCMPOQ)
TRANS_DFP_BF_A_DCM(DTSTDC)
TRANS_DFP_BF_A_DCM(DTSTDCQ)
TRANS_DFP_BF_A_DCM(DTSTDG)
TRANS_DFP_BF_A_DCM(DTSTDGQ)
TRANS_DFP_BF_A_B(DTSTEX)
TRANS_DFP_BF_A_B(DTSTEXQ)
TRANS_DFP_BF_A_B(DTSTSF)
TRANS_DFP_BF_A_B(DTSTSFQ)
TRANS_DFP_BF_I_B(DTSTSFI)
TRANS_DFP_BF_I_B(DTSTSFIQ)
TRANS_DFP_T_B_U32_U32_Rc(DQUAI, te, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DQUAIQ, te, rmc)
GEN_DFP_T_A_B_I32_Rc(dqua, RMC)
GEN_DFP_T_A_B_I32_Rc(dquaq, RMC)
GEN_DFP_T_A_B_I32_Rc(drrnd, RMC)
GEN_DFP_T_A_B_I32_Rc(drrndq, RMC)
TRANS_DFP_T_B_U32_U32_Rc(DRINTX, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTXQ, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTN, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTNQ, r, rmc)
GEN_DFP_T_B_Rc(dctdp)
GEN_DFP_T_B_Rc(dctqpq)
GEN_DFP_T_B_Rc(drsp)
GEN_DFP_T_B_Rc(drdpq)
GEN_DFP_T_B_Rc(dcffix)
GEN_DFP_T_B_Rc(dcffixq)
GEN_DFP_T_B_Rc(dctfix)
GEN_DFP_T_B_Rc(dctfixq)
GEN_DFP_T_FPR_I32_Rc(ddedpd, rB, SP)
GEN_DFP_T_FPR_I32_Rc(ddedpdq, rB, SP)
GEN_DFP_T_FPR_I32_Rc(denbcd, rB, SP)
GEN_DFP_T_FPR_I32_Rc(denbcdq, rB, SP)
GEN_DFP_T_B_Rc(dxex)
GEN_DFP_T_B_Rc(dxexq)
TRANS_DFP_T_A_B_Rc(DIEX)
TRANS_DFP_T_A_B_Rc(DIEXQ)
GEN_DFP_T_FPR_I32_Rc(dscli, rA, DCM)
GEN_DFP_T_FPR_I32_Rc(dscliq, rA, DCM)
GEN_DFP_T_FPR_I32_Rc(dscri, rA, DCM)
GEN_DFP_T_FPR_I32_Rc(dscriq, rA, DCM)

#undef GEN_DFP_T_A_B_I32_Rc
#undef GEN_DFP_T_B_Rc
#undef GEN_DFP_T_FPR_I32_Rc

static bool trans_DCFFIXQQ(DisasContext *ctx, arg_DCFFIXQQ *a)
{
    TCGv_ptr rt, rb;

    REQUIRE_INSNS_FLAGS2(ctx, DFP);
    REQUIRE_FPU(ctx);
    REQUIRE_VECTOR(ctx);

    rt = gen_fprp_ptr(a->frtp);
    rb = gen_avr_ptr(a->vrb);
    gen_helper_DCFFIXQQ(cpu_env, rt, rb);
    tcg_temp_free_ptr(rt);
    tcg_temp_free_ptr(rb);

    return true;
}

static bool trans_DCTFIXQQ(DisasContext *ctx, arg_DCTFIXQQ *a)
{
    TCGv_ptr rt, rb;

    REQUIRE_INSNS_FLAGS2(ctx, DFP);
    REQUIRE_FPU(ctx);
    REQUIRE_VECTOR(ctx);

    rt = gen_avr_ptr(a->vrt);
    rb = gen_fprp_ptr(a->frbp);
    gen_helper_DCTFIXQQ(cpu_env, rt, rb);
    tcg_temp_free_ptr(rt);
    tcg_temp_free_ptr(rb);

    return true;
}
