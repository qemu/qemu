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
    return true;                                             \
}

#define TRANS_DFP_T_A_B_I32_Rc(NAME, I32FLD)                 \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rt, ra, rb;                                     \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rt = gen_fprp_ptr(a->frt);                               \
    ra = gen_fprp_ptr(a->fra);                               \
    rb = gen_fprp_ptr(a->frb);                               \
    gen_helper_##NAME(cpu_env, rt, ra, rb,                   \
                      tcg_constant_i32(a->I32FLD));          \
    if (unlikely(a->rc)) {                                   \
        gen_set_cr1_from_fpscr(ctx);                         \
    }                                                        \
    return true;                                             \
}

#define TRANS_DFP_T_B_Rc(NAME)                               \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rt, rb;                                         \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rt = gen_fprp_ptr(a->rt);                                \
    rb = gen_fprp_ptr(a->rb);                                \
    gen_helper_##NAME(cpu_env, rt, rb);                      \
    if (unlikely(a->rc)) {                                   \
        gen_set_cr1_from_fpscr(ctx);                         \
    }                                                        \
    return true;                                             \
}

#define TRANS_DFP_T_FPR_I32_Rc(NAME, FPRFLD, I32FLD)         \
static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a)   \
{                                                            \
    TCGv_ptr rt, rx;                                         \
    REQUIRE_INSNS_FLAGS2(ctx, DFP);                          \
    REQUIRE_FPU(ctx);                                        \
    rt = gen_fprp_ptr(a->rt);                                \
    rx = gen_fprp_ptr(a->FPRFLD);                            \
    gen_helper_##NAME(cpu_env, rt, rx,                       \
                      tcg_constant_i32(a->I32FLD));          \
    if (unlikely(a->rc)) {                                   \
        gen_set_cr1_from_fpscr(ctx);                         \
    }                                                        \
    return true;                                             \
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
TRANS_DFP_T_A_B_I32_Rc(DQUA, rmc)
TRANS_DFP_T_A_B_I32_Rc(DQUAQ, rmc)
TRANS_DFP_T_A_B_I32_Rc(DRRND, rmc)
TRANS_DFP_T_A_B_I32_Rc(DRRNDQ, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTX, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTXQ, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTN, r, rmc)
TRANS_DFP_T_B_U32_U32_Rc(DRINTNQ, r, rmc)
TRANS_DFP_T_B_Rc(DCTDP)
TRANS_DFP_T_B_Rc(DCTQPQ)
TRANS_DFP_T_B_Rc(DRSP)
TRANS_DFP_T_B_Rc(DRDPQ)
TRANS_DFP_T_B_Rc(DCFFIX)
TRANS_DFP_T_B_Rc(DCFFIXQ)
TRANS_DFP_T_B_Rc(DCTFIX)
TRANS_DFP_T_B_Rc(DCTFIXQ)
TRANS_DFP_T_FPR_I32_Rc(DDEDPD, rb, sp)
TRANS_DFP_T_FPR_I32_Rc(DDEDPDQ, rb, sp)
TRANS_DFP_T_FPR_I32_Rc(DENBCD, rb, s)
TRANS_DFP_T_FPR_I32_Rc(DENBCDQ, rb, s)
TRANS_DFP_T_B_Rc(DXEX)
TRANS_DFP_T_B_Rc(DXEXQ)
TRANS_DFP_T_A_B_Rc(DIEX)
TRANS_DFP_T_A_B_Rc(DIEXQ)
TRANS_DFP_T_FPR_I32_Rc(DSCLI, ra, sh)
TRANS_DFP_T_FPR_I32_Rc(DSCLIQ, ra, sh)
TRANS_DFP_T_FPR_I32_Rc(DSCRI, ra, sh)
TRANS_DFP_T_FPR_I32_Rc(DSCRIQ, ra, sh)

static bool trans_DCFFIXQQ(DisasContext *ctx, arg_DCFFIXQQ *a)
{
    TCGv_ptr rt, rb;

    REQUIRE_INSNS_FLAGS2(ctx, DFP);
    REQUIRE_FPU(ctx);
    REQUIRE_VECTOR(ctx);

    rt = gen_fprp_ptr(a->frtp);
    rb = gen_avr_ptr(a->vrb);
    gen_helper_DCFFIXQQ(cpu_env, rt, rb);

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

    return true;
}
