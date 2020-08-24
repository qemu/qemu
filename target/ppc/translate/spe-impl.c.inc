/*
 * translate-spe.c
 *
 * Freescale SPE extension translation
 */

/***                           SPE extension                               ***/
/* Register moves */

static inline void gen_evmra(DisasContext *ctx)
{

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    TCGv_i64 tmp = tcg_temp_new_i64();

    /* tmp := rA_lo + rA_hi << 32 */
    tcg_gen_concat_tl_i64(tmp, cpu_gpr[rA(ctx->opcode)],
                          cpu_gprh[rA(ctx->opcode)]);

    /* spe_acc := tmp */
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));
    tcg_temp_free_i64(tmp);

    /* rD := rA */
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
}

static inline void gen_load_gpr64(TCGv_i64 t, int reg)
{
    tcg_gen_concat_tl_i64(t, cpu_gpr[reg], cpu_gprh[reg]);
}

static inline void gen_store_gpr64(int reg, TCGv_i64 t)
{
    tcg_gen_extr_i64_tl(cpu_gpr[reg], cpu_gprh[reg], t);
}

#define GEN_SPE(name0, name1, opc2, opc3, inval0, inval1, type)         \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)                    \
{                                                                             \
    if (Rc(ctx->opcode))                                                      \
        gen_##name1(ctx);                                                     \
    else                                                                      \
        gen_##name0(ctx);                                                     \
}

/* Handler for undefined SPE opcodes */
static inline void gen_speundef(DisasContext *ctx)
{
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

/* SPE logic */
#define GEN_SPEOP_LOGIC2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],                \
           cpu_gpr[rB(ctx->opcode)]);                                         \
    tcg_op(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],              \
           cpu_gprh[rB(ctx->opcode)]);                                        \
}

GEN_SPEOP_LOGIC2(evand, tcg_gen_and_tl);
GEN_SPEOP_LOGIC2(evandc, tcg_gen_andc_tl);
GEN_SPEOP_LOGIC2(evxor, tcg_gen_xor_tl);
GEN_SPEOP_LOGIC2(evor, tcg_gen_or_tl);
GEN_SPEOP_LOGIC2(evnor, tcg_gen_nor_tl);
GEN_SPEOP_LOGIC2(eveqv, tcg_gen_eqv_tl);
GEN_SPEOP_LOGIC2(evorc, tcg_gen_orc_tl);
GEN_SPEOP_LOGIC2(evnand, tcg_gen_nand_tl);

/* SPE logic immediate */
#define GEN_SPEOP_TCG_LOGIC_IMM2(name, tcg_opi)                               \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_opi(t0, t0, rB(ctx->opcode));                                         \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gprh[rA(ctx->opcode)]);                      \
    tcg_opi(t0, t0, rB(ctx->opcode));                                         \
    tcg_gen_extu_i32_tl(cpu_gprh[rD(ctx->opcode)], t0);                       \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
}
GEN_SPEOP_TCG_LOGIC_IMM2(evslwi, tcg_gen_shli_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evsrwiu, tcg_gen_shri_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evsrwis, tcg_gen_sari_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evrlwi, tcg_gen_rotli_i32);

/* SPE arithmetic */
#define GEN_SPEOP_ARITH1(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_op(t0, t0);                                                           \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gprh[rA(ctx->opcode)]);                      \
    tcg_op(t0, t0);                                                           \
    tcg_gen_extu_i32_tl(cpu_gprh[rD(ctx->opcode)], t0);                       \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
}

GEN_SPEOP_ARITH1(evabs, tcg_gen_abs_i32);
GEN_SPEOP_ARITH1(evneg, tcg_gen_neg_i32);
GEN_SPEOP_ARITH1(evextsb, tcg_gen_ext8s_i32);
GEN_SPEOP_ARITH1(evextsh, tcg_gen_ext16s_i32);
static inline void gen_op_evrndw(TCGv_i32 ret, TCGv_i32 arg1)
{
    tcg_gen_addi_i32(ret, arg1, 0x8000);
    tcg_gen_ext16u_i32(ret, ret);
}
GEN_SPEOP_ARITH1(evrndw, gen_op_evrndw);
GEN_SPEOP_ARITH1(evcntlsw, gen_helper_cntlsw32);
GEN_SPEOP_ARITH1(evcntlzw, gen_helper_cntlzw32);

#define GEN_SPEOP_ARITH2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
    t1 = tcg_temp_new_i32();                                                  \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    tcg_op(t0, t0, t1);                                                       \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gprh[rA(ctx->opcode)]);                      \
    tcg_gen_trunc_tl_i32(t1, cpu_gprh[rB(ctx->opcode)]);                      \
    tcg_op(t0, t0, t1);                                                       \
    tcg_gen_extu_i32_tl(cpu_gprh[rD(ctx->opcode)], t0);                       \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}

static inline void gen_op_evsrwu(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();

    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_shr_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evsrwu, gen_op_evsrwu);
static inline void gen_op_evsrws(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();

    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_sar_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evsrws, gen_op_evsrws);
static inline void gen_op_evslw(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();

    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_shl_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evslw, gen_op_evslw);
static inline void gen_op_evrlw(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, arg2, 0x1F);
    tcg_gen_rotl_i32(ret, arg1, t0);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evrlw, gen_op_evrlw);
static inline void gen_evmergehi(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
}
GEN_SPEOP_ARITH2(evaddw, tcg_gen_add_i32);
static inline void gen_op_evsubf(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_sub_i32(ret, arg2, arg1);
}
GEN_SPEOP_ARITH2(evsubfw, gen_op_evsubf);

/* SPE arithmetic immediate */
#define GEN_SPEOP_ARITH_IMM2(name, tcg_op)                                    \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rB(ctx->opcode)]);                       \
    tcg_op(t0, t0, rA(ctx->opcode));                                          \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gprh[rB(ctx->opcode)]);                      \
    tcg_op(t0, t0, rA(ctx->opcode));                                          \
    tcg_gen_extu_i32_tl(cpu_gprh[rD(ctx->opcode)], t0);                       \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
}
GEN_SPEOP_ARITH_IMM2(evaddiw, tcg_gen_addi_i32);
GEN_SPEOP_ARITH_IMM2(evsubifw, tcg_gen_subi_i32);

/* SPE comparison */
#define GEN_SPEOP_COMP(name, tcg_cond)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    TCGLabel *l1 = gen_new_label();                                           \
    TCGLabel *l2 = gen_new_label();                                           \
    TCGLabel *l3 = gen_new_label();                                           \
    TCGLabel *l4 = gen_new_label();                                           \
                                                                              \
    tcg_gen_ext32s_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);    \
    tcg_gen_ext32s_tl(cpu_gpr[rB(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);    \
    tcg_gen_ext32s_tl(cpu_gprh[rA(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);  \
    tcg_gen_ext32s_tl(cpu_gprh[rB(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);  \
                                                                              \
    tcg_gen_brcond_tl(tcg_cond, cpu_gpr[rA(ctx->opcode)],                     \
                       cpu_gpr[rB(ctx->opcode)], l1);                         \
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)], 0);                          \
    tcg_gen_br(l2);                                                           \
    gen_set_label(l1);                                                        \
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)],                              \
                     CRF_CL | CRF_CH_OR_CL | CRF_CH_AND_CL);                  \
    gen_set_label(l2);                                                        \
    tcg_gen_brcond_tl(tcg_cond, cpu_gprh[rA(ctx->opcode)],                    \
                       cpu_gprh[rB(ctx->opcode)], l3);                        \
    tcg_gen_andi_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],  \
                     ~(CRF_CH | CRF_CH_AND_CL));                              \
    tcg_gen_br(l4);                                                           \
    gen_set_label(l3);                                                        \
    tcg_gen_ori_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],   \
                    CRF_CH | CRF_CH_OR_CL);                                   \
    gen_set_label(l4);                                                        \
}
GEN_SPEOP_COMP(evcmpgtu, TCG_COND_GTU);
GEN_SPEOP_COMP(evcmpgts, TCG_COND_GT);
GEN_SPEOP_COMP(evcmpltu, TCG_COND_LTU);
GEN_SPEOP_COMP(evcmplts, TCG_COND_LT);
GEN_SPEOP_COMP(evcmpeq, TCG_COND_EQ);

/* SPE misc */
static inline void gen_brinc(DisasContext *ctx)
{
    /* Note: brinc is usable even if SPE is disabled */
    gen_helper_brinc(cpu_gpr[rD(ctx->opcode)],
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
}
static inline void gen_evmergelo(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
}
static inline void gen_evmergehilo(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
}
static inline void gen_evmergelohi(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    if (rD(ctx->opcode) == rA(ctx->opcode)) {
        TCGv tmp = tcg_temp_new();
        tcg_gen_mov_tl(tmp, cpu_gpr[rA(ctx->opcode)]);
        tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
        tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], tmp);
        tcg_temp_free(tmp);
    } else {
        tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
        tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    }
}
static inline void gen_evsplati(DisasContext *ctx)
{
    uint64_t imm;
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    imm = ((int32_t)(rA(ctx->opcode) << 27)) >> 27;

    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], imm);
    tcg_gen_movi_tl(cpu_gprh[rD(ctx->opcode)], imm);
}
static inline void gen_evsplatfi(DisasContext *ctx)
{
    uint64_t imm;
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    imm = rA(ctx->opcode) << 27;

    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], imm);
    tcg_gen_movi_tl(cpu_gprh[rD(ctx->opcode)], imm);
}

static inline void gen_evsel(DisasContext *ctx)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGLabel *l3 = gen_new_label();
    TCGLabel *l4 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();

    tcg_gen_andi_i32(t0, cpu_crf[ctx->opcode & 0x07], 1 << 3);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
    gen_set_label(l2);
    tcg_gen_andi_i32(t0, cpu_crf[ctx->opcode & 0x07], 1 << 2);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l3);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l4);
    gen_set_label(l3);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    gen_set_label(l4);
    tcg_temp_free_i32(t0);
}

static void gen_evsel0(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    gen_evsel(ctx);
}

static void gen_evsel1(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    gen_evsel(ctx);
}

static void gen_evsel2(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    gen_evsel(ctx);
}

static void gen_evsel3(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    gen_evsel(ctx);
}

/* Multiply */

static inline void gen_evmwumi(DisasContext *ctx)
{
    TCGv_i64 t0, t1;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    /* t0 := rA; t1 := rB */
    tcg_gen_extu_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32u_i64(t0, t0);
    tcg_gen_extu_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_ext32u_i64(t1, t1);

    tcg_gen_mul_i64(t0, t0, t1);  /* t0 := rA * rB */

    gen_store_gpr64(rD(ctx->opcode), t0); /* rD := t0 */

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void gen_evmwumia(DisasContext *ctx)
{
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwumi(ctx);            /* rD := rA * rB */

    tmp = tcg_temp_new_i64();

    /* acc := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));
    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwumiaa(DisasContext *ctx)
{
    TCGv_i64 acc;
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwumi(ctx);           /* rD := rA * rB */

    acc = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    /* tmp := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));

    /* Load acc */
    tcg_gen_ld_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* acc := tmp + acc */
    tcg_gen_add_i64(acc, acc, tmp);

    /* Store acc */
    tcg_gen_st_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* rD := acc */
    gen_store_gpr64(rD(ctx->opcode), acc);

    tcg_temp_free_i64(acc);
    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwsmi(DisasContext *ctx)
{
    TCGv_i64 t0, t1;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    /* t0 := rA; t1 := rB */
    tcg_gen_extu_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32s_i64(t0, t0);
    tcg_gen_extu_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_ext32s_i64(t1, t1);

    tcg_gen_mul_i64(t0, t0, t1);  /* t0 := rA * rB */

    gen_store_gpr64(rD(ctx->opcode), t0); /* rD := t0 */

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void gen_evmwsmia(DisasContext *ctx)
{
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwsmi(ctx);            /* rD := rA * rB */

    tmp = tcg_temp_new_i64();

    /* acc := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));

    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwsmiaa(DisasContext *ctx)
{
    TCGv_i64 acc;
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwsmi(ctx);           /* rD := rA * rB */

    acc = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    /* tmp := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));

    /* Load acc */
    tcg_gen_ld_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* acc := tmp + acc */
    tcg_gen_add_i64(acc, acc, tmp);

    /* Store acc */
    tcg_gen_st_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* rD := acc */
    gen_store_gpr64(rD(ctx->opcode), acc);

    tcg_temp_free_i64(acc);
    tcg_temp_free_i64(tmp);
}

GEN_SPE(evaddw,      speundef,    0x00, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evaddiw,     speundef,    0x01, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evsubfw,     speundef,    0x02, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evsubifw,    speundef,    0x03, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evabs,       evneg,       0x04, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evextsb,     evextsh,     0x05, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evrndw,      evcntlzw,    0x06, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evcntlsw,    brinc,       0x07, 0x08, 0x0000F800, 0x00000000, PPC_SPE); //
GEN_SPE(evmra,       speundef,    0x02, 0x13, 0x0000F800, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(speundef,    evand,       0x08, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE); ////
GEN_SPE(evandc,      speundef,    0x09, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evxor,       evor,        0x0B, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evnor,       eveqv,       0x0C, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evmwumi,     evmwsmi,     0x0C, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwumia,    evmwsmia,    0x1C, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwumiaa,   evmwsmiaa,   0x0C, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,    evorc,       0x0D, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE); ////
GEN_SPE(evnand,      speundef,    0x0F, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evsrwu,      evsrws,      0x10, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evsrwiu,     evsrwis,     0x11, 0x08, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evslw,       speundef,    0x12, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evslwi,      speundef,    0x13, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evrlw,       evsplati,    0x14, 0x08, 0x00000000, 0x0000F800, PPC_SPE); //
GEN_SPE(evrlwi,      evsplatfi,   0x15, 0x08, 0x00000000, 0x0000F800, PPC_SPE);
GEN_SPE(evmergehi,   evmergelo,   0x16, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evmergehilo, evmergelohi, 0x17, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evcmpgtu,    evcmpgts,    0x18, 0x08, 0x00600000, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpltu,    evcmplts,    0x19, 0x08, 0x00600000, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpeq,     speundef,    0x1A, 0x08, 0x00600000, 0xFFFFFFFF, PPC_SPE); ////

/* SPE load and stores */
static inline void gen_addr_spe_imm_index(DisasContext *ctx, TCGv EA, int sh)
{
    target_ulong uimm = rB(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        tcg_gen_movi_tl(EA, uimm << sh);
    } else {
        tcg_gen_addi_tl(EA, cpu_gpr[rA(ctx->opcode)], uimm << sh);
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, EA);
        }
    }
}

static inline void gen_op_evldd(DisasContext *ctx, TCGv addr)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_qemu_ld64_i64(ctx, t0, addr);
    gen_store_gpr64(rD(ctx->opcode), t0);
    tcg_temp_free_i64(t0);
}

static inline void gen_op_evldw(DisasContext *ctx, TCGv addr)
{
    gen_qemu_ld32u(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 4);
    gen_qemu_ld32u(ctx, cpu_gpr[rD(ctx->opcode)], addr);
}

static inline void gen_op_evldh(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhesplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhousplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhossplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16s(ctx, t0, addr);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhe(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 16);
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhou(DisasContext *ctx, TCGv addr)
{
    gen_qemu_ld16u(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, cpu_gpr[rD(ctx->opcode)], addr);
}

static inline void gen_op_evlwhos(DisasContext *ctx, TCGv addr)
{
    gen_qemu_ld16s(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16s(ctx, cpu_gpr[rD(ctx->opcode)], addr);
}

static inline void gen_op_evlwwsplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld32u(ctx, t0, addr);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhsplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    tcg_gen_or_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

static inline void gen_op_evstdd(DisasContext *ctx, TCGv addr)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_load_gpr64(t0, rS(ctx->opcode));
    gen_qemu_st64_i64(ctx, t0, addr);
    tcg_temp_free_i64(t0);
}

static inline void gen_op_evstdw(DisasContext *ctx, TCGv addr)
{
    gen_qemu_st32(ctx, cpu_gprh[rS(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 4);
    gen_qemu_st32(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstdh(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gprh[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_st16(ctx, cpu_gprh[rS(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    tcg_temp_free(t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_st16(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstwhe(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gprh[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    gen_addr_add(ctx, addr, addr, 2);
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    tcg_temp_free(t0);
}

static inline void gen_op_evstwho(DisasContext *ctx, TCGv addr)
{
    gen_qemu_st16(ctx, cpu_gprh[rS(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_st16(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstwwe(DisasContext *ctx, TCGv addr)
{
    gen_qemu_st32(ctx, cpu_gprh[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstwwo(DisasContext *ctx, TCGv addr)
{
    gen_qemu_st32(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

#define GEN_SPEOP_LDST(name, opc2, sh)                                        \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    TCGv t0;                                                                  \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    t0 = tcg_temp_new();                                                      \
    if (Rc(ctx->opcode)) {                                                    \
        gen_addr_spe_imm_index(ctx, t0, sh);                                  \
    } else {                                                                  \
        gen_addr_reg_index(ctx, t0);                                          \
    }                                                                         \
    gen_op_##name(ctx, t0);                                                   \
    tcg_temp_free(t0);                                                        \
}

GEN_SPEOP_LDST(evldd, 0x00, 3);
GEN_SPEOP_LDST(evldw, 0x01, 3);
GEN_SPEOP_LDST(evldh, 0x02, 3);
GEN_SPEOP_LDST(evlhhesplat, 0x04, 1);
GEN_SPEOP_LDST(evlhhousplat, 0x06, 1);
GEN_SPEOP_LDST(evlhhossplat, 0x07, 1);
GEN_SPEOP_LDST(evlwhe, 0x08, 2);
GEN_SPEOP_LDST(evlwhou, 0x0A, 2);
GEN_SPEOP_LDST(evlwhos, 0x0B, 2);
GEN_SPEOP_LDST(evlwwsplat, 0x0C, 2);
GEN_SPEOP_LDST(evlwhsplat, 0x0E, 2);

GEN_SPEOP_LDST(evstdd, 0x10, 3);
GEN_SPEOP_LDST(evstdw, 0x11, 3);
GEN_SPEOP_LDST(evstdh, 0x12, 3);
GEN_SPEOP_LDST(evstwhe, 0x18, 2);
GEN_SPEOP_LDST(evstwho, 0x1A, 2);
GEN_SPEOP_LDST(evstwwe, 0x1C, 2);
GEN_SPEOP_LDST(evstwwo, 0x1E, 2);

/* Multiply and add - TODO */
#if 0
GEN_SPE(speundef,       evmhessf,      0x01, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);//
GEN_SPE(speundef,       evmhossf,      0x03, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumi,       evmhesmi,      0x04, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmf,      0x05, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumi,       evmhosmi,      0x06, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmf,      0x07, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfa,     0x11, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfa,     0x13, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumia,      evmhesmia,     0x14, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfa,     0x15, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumia,      evmhosmia,     0x16, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfa,     0x17, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(speundef,       evmwhssf,      0x03, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumi,       speundef,      0x04, 0x11, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evmwhumi,       evmwhsmi,      0x06, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmf,      0x07, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssf,       0x09, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmf,       0x0D, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhssfa,     0x13, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumia,      speundef,      0x14, 0x11, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evmwhumia,      evmwhsmia,     0x16, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmfa,     0x17, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfa,      0x19, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfa,      0x1D, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evadduiaaw,     evaddsiaaw,    0x00, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfusiaaw,   evsubfssiaaw,  0x01, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evaddumiaaw,    evaddsmiaaw,   0x04, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfumiaaw,   evsubfsmiaaw,  0x05, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evdivws,        evdivwu,       0x06, 0x13, 0x00000000, 0x00000000, PPC_SPE);

GEN_SPE(evmheusiaaw,    evmhessiaaw,   0x00, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfaaw,   0x01, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhousiaaw,    evmhossiaaw,   0x02, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfaaw,   0x03, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumiaaw,    evmhesmiaaw,   0x04, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfaaw,   0x05, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumiaaw,    evmhosmiaaw,   0x06, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfaaw,   0x07, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumiaa,    evmhegsmiaa,   0x14, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfaa,   0x15, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhogumiaa,    evmhogsmiaa,   0x16, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfaa,   0x17, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusiaaw,    evmwlssiaaw,   0x00, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumiaaw,    evmwlsmiaaw,   0x04, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfaa,     0x09, 0x15, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfaa,     0x0D, 0x15, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmheusianw,    evmhessianw,   0x00, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfanw,   0x01, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhousianw,    evmhossianw,   0x02, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfanw,   0x03, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumianw,    evmhesmianw,   0x04, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfanw,   0x05, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumianw,    evmhosmianw,   0x06, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfanw,   0x07, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumian,    evmhegsmian,   0x14, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfan,   0x15, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhigumian,    evmhigsmian,   0x16, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfan,   0x17, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusianw,    evmwlssianw,   0x00, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumianw,    evmwlsmianw,   0x04, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfan,     0x09, 0x17, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwumian,      evmwsmian,     0x0C, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfan,     0x0D, 0x17, 0xFFFFFFFF, 0x00000000, PPC_SPE);
#endif

/***                      SPE floating-point extension                     ***/
#define GEN_SPEFPUOP_CONV_32_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0 = tcg_temp_new_i32();                                         \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(t0, cpu_env, t0);                                       \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
    tcg_temp_free_i32(t0);                                                    \
}
#define GEN_SPEFPUOP_CONV_32_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i32 t1;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i32();                                                  \
    gen_load_gpr64(t0, rB(ctx->opcode));                                      \
    gen_helper_##name(t1, cpu_env, t0);                                       \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t1);                        \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#define GEN_SPEFPUOP_CONV_64_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0;                                                              \
    TCGv_i32 t1;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i32();                                                  \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(t0, cpu_env, t1);                                       \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#define GEN_SPEFPUOP_CONV_64_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    gen_load_gpr64(t0, rB(ctx->opcode));                                      \
    gen_helper_##name(t0, cpu_env, t0);                                       \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
}
#define GEN_SPEFPUOP_ARITH2_32_32(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0 = tcg_temp_new_i32();                                         \
    TCGv_i32 t1 = tcg_temp_new_i32();                                         \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(t0, cpu_env, t0, t1);                                   \
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);                        \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#define GEN_SPEFPUOP_ARITH2_64_64(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    gen_load_gpr64(t0, rA(ctx->opcode));                                      \
    gen_load_gpr64(t1, rB(ctx->opcode));                                      \
    gen_helper_##name(t0, cpu_env, t0, t1);                                   \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}
#define GEN_SPEFPUOP_COMP_32(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0 = tcg_temp_new_i32();                                         \
    TCGv_i32 t1 = tcg_temp_new_i32();                                         \
                                                                              \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env, t0, t1);           \
                                                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#define GEN_SPEFPUOP_COMP_64(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    gen_load_gpr64(t0, rA(ctx->opcode));                                      \
    gen_load_gpr64(t1, rB(ctx->opcode));                                      \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env, t0, t1);           \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}

/* Single precision floating-point vectors operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_64_64(evfsadd);
GEN_SPEFPUOP_ARITH2_64_64(evfssub);
GEN_SPEFPUOP_ARITH2_64_64(evfsmul);
GEN_SPEFPUOP_ARITH2_64_64(evfsdiv);
static inline void gen_evfsabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    ~0x80000000);
    tcg_gen_andi_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                    ~0x80000000);
}
static inline void gen_evfsnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                   0x80000000);
    tcg_gen_ori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                   0x80000000);
}
static inline void gen_evfsneg(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    0x80000000);
    tcg_gen_xori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                    0x80000000);
}

/* Conversion */
GEN_SPEFPUOP_CONV_64_64(evfscfui);
GEN_SPEFPUOP_CONV_64_64(evfscfsi);
GEN_SPEFPUOP_CONV_64_64(evfscfuf);
GEN_SPEFPUOP_CONV_64_64(evfscfsf);
GEN_SPEFPUOP_CONV_64_64(evfsctui);
GEN_SPEFPUOP_CONV_64_64(evfsctsi);
GEN_SPEFPUOP_CONV_64_64(evfsctuf);
GEN_SPEFPUOP_CONV_64_64(evfsctsf);
GEN_SPEFPUOP_CONV_64_64(evfsctuiz);
GEN_SPEFPUOP_CONV_64_64(evfsctsiz);

/* Comparison */
GEN_SPEFPUOP_COMP_64(evfscmpgt);
GEN_SPEFPUOP_COMP_64(evfscmplt);
GEN_SPEFPUOP_COMP_64(evfscmpeq);
GEN_SPEFPUOP_COMP_64(evfststgt);
GEN_SPEFPUOP_COMP_64(evfststlt);
GEN_SPEFPUOP_COMP_64(evfststeq);

/* Opcodes definitions */
GEN_SPE(evfsadd,   evfssub,   0x00, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(evfsabs,   evfsnabs,  0x02, 0x0A, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE); //
GEN_SPE(evfsneg,   speundef,  0x03, 0x0A, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfsmul,   evfsdiv,   0x04, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(evfscmpgt, evfscmplt, 0x06, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(evfscmpeq, speundef,  0x07, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfscfui,  evfscfsi,  0x08, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfscfuf,  evfscfsf,  0x09, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctui,  evfsctsi,  0x0A, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctuf,  evfsctsf,  0x0B, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctuiz, speundef,  0x0C, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfsctsiz, speundef,  0x0D, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfststgt, evfststlt, 0x0E, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(evfststeq, speundef,  0x0F, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //

/* Single precision floating-point operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_32_32(efsadd);
GEN_SPEFPUOP_ARITH2_32_32(efssub);
GEN_SPEFPUOP_ARITH2_32_32(efsmul);
GEN_SPEFPUOP_ARITH2_32_32(efsdiv);
static inline void gen_efsabs(DisasContext *ctx)
{
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    (target_long)~0x80000000LL);
}
static inline void gen_efsnabs(DisasContext *ctx)
{
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                   0x80000000);
}
static inline void gen_efsneg(DisasContext *ctx)
{
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    0x80000000);
}

/* Conversion */
GEN_SPEFPUOP_CONV_32_32(efscfui);
GEN_SPEFPUOP_CONV_32_32(efscfsi);
GEN_SPEFPUOP_CONV_32_32(efscfuf);
GEN_SPEFPUOP_CONV_32_32(efscfsf);
GEN_SPEFPUOP_CONV_32_32(efsctui);
GEN_SPEFPUOP_CONV_32_32(efsctsi);
GEN_SPEFPUOP_CONV_32_32(efsctuf);
GEN_SPEFPUOP_CONV_32_32(efsctsf);
GEN_SPEFPUOP_CONV_32_32(efsctuiz);
GEN_SPEFPUOP_CONV_32_32(efsctsiz);
GEN_SPEFPUOP_CONV_32_64(efscfd);

/* Comparison */
GEN_SPEFPUOP_COMP_32(efscmpgt);
GEN_SPEFPUOP_COMP_32(efscmplt);
GEN_SPEFPUOP_COMP_32(efscmpeq);
GEN_SPEFPUOP_COMP_32(efststgt);
GEN_SPEFPUOP_COMP_32(efststlt);
GEN_SPEFPUOP_COMP_32(efststeq);

/* Opcodes definitions */
GEN_SPE(efsadd,   efssub,   0x00, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(efsabs,   efsnabs,  0x02, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE); //
GEN_SPE(efsneg,   speundef, 0x03, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efsmul,   efsdiv,   0x04, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(efscmpgt, efscmplt, 0x06, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(efscmpeq, efscfd,   0x07, 0x0B, 0x00600000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efscfui,  efscfsi,  0x08, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efscfuf,  efscfsf,  0x09, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctui,  efsctsi,  0x0A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctuf,  efsctsf,  0x0B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctuiz, speundef, 0x0C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efsctsiz, speundef, 0x0D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efststgt, efststlt, 0x0E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(efststeq, speundef, 0x0F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //

/* Double precision floating-point operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_64_64(efdadd);
GEN_SPEFPUOP_ARITH2_64_64(efdsub);
GEN_SPEFPUOP_ARITH2_64_64(efdmul);
GEN_SPEFPUOP_ARITH2_64_64(efddiv);
static inline void gen_efdabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_andi_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                    ~0x80000000);
}
static inline void gen_efdnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                   0x80000000);
}
static inline void gen_efdneg(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_xori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],
                    0x80000000);
}

/* Conversion */
GEN_SPEFPUOP_CONV_64_32(efdcfui);
GEN_SPEFPUOP_CONV_64_32(efdcfsi);
GEN_SPEFPUOP_CONV_64_32(efdcfuf);
GEN_SPEFPUOP_CONV_64_32(efdcfsf);
GEN_SPEFPUOP_CONV_32_64(efdctui);
GEN_SPEFPUOP_CONV_32_64(efdctsi);
GEN_SPEFPUOP_CONV_32_64(efdctuf);
GEN_SPEFPUOP_CONV_32_64(efdctsf);
GEN_SPEFPUOP_CONV_32_64(efdctuiz);
GEN_SPEFPUOP_CONV_32_64(efdctsiz);
GEN_SPEFPUOP_CONV_64_32(efdcfs);
GEN_SPEFPUOP_CONV_64_64(efdcfuid);
GEN_SPEFPUOP_CONV_64_64(efdcfsid);
GEN_SPEFPUOP_CONV_64_64(efdctuidz);
GEN_SPEFPUOP_CONV_64_64(efdctsidz);

/* Comparison */
GEN_SPEFPUOP_COMP_64(efdcmpgt);
GEN_SPEFPUOP_COMP_64(efdcmplt);
GEN_SPEFPUOP_COMP_64(efdcmpeq);
GEN_SPEFPUOP_COMP_64(efdtstgt);
GEN_SPEFPUOP_COMP_64(efdtstlt);
GEN_SPEFPUOP_COMP_64(efdtsteq);

/* Opcodes definitions */
GEN_SPE(efdadd,    efdsub,    0x10, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfuid,  efdcfsid,  0x11, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdabs,    efdnabs,   0x12, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_DOUBLE); //
GEN_SPE(efdneg,    speundef,  0x13, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdmul,    efddiv,    0x14, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuidz, efdctsidz, 0x15, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcmpgt,  efdcmplt,  0x16, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcmpeq,  efdcfs,    0x17, 0x0B, 0x00600000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfui,   efdcfsi,   0x18, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfuf,   efdcfsf,   0x19, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctui,   efdctsi,   0x1A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuf,   efdctsf,   0x1B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuiz,  speundef,  0x1C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdctsiz,  speundef,  0x1D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdtstgt,  efdtstlt,  0x1E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE); //
GEN_SPE(efdtsteq,  speundef,  0x1F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //

#undef GEN_SPE
#undef GEN_SPEOP_LDST
