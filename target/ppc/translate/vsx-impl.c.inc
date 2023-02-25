/***                           VSX extension                               ***/

static inline void get_cpu_vsr(TCGv_i64 dst, int n, bool high)
{
    tcg_gen_ld_i64(dst, cpu_env, vsr64_offset(n, high));
}

static inline void set_cpu_vsr(int n, TCGv_i64 src, bool high)
{
    tcg_gen_st_i64(src, cpu_env, vsr64_offset(n, high));
}

static inline TCGv_ptr gen_vsr_ptr(int reg)
{
    TCGv_ptr r = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(r, cpu_env, vsr_full_offset(reg));
    return r;
}

static inline TCGv_ptr gen_acc_ptr(int reg)
{
    TCGv_ptr r = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(r, cpu_env, acc_full_offset(reg));
    return r;
}

#define VSX_LOAD_SCALAR(name, operation)                      \
static void gen_##name(DisasContext *ctx)                     \
{                                                             \
    TCGv EA;                                                  \
    TCGv_i64 t0;                                              \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    t0 = tcg_temp_new_i64();                                  \
    gen_set_access_type(ctx, ACCESS_INT);                     \
    EA = tcg_temp_new();                                      \
    gen_addr_reg_index(ctx, EA);                              \
    gen_qemu_##operation(ctx, t0, EA);                        \
    set_cpu_vsr(xT(ctx->opcode), t0, true);                   \
    /* NOTE: cpu_vsrl is undefined */                         \
}

VSX_LOAD_SCALAR(lxsdx, ld64_i64)
VSX_LOAD_SCALAR(lxsiwax, ld32s_i64)
VSX_LOAD_SCALAR(lxsibzx, ld8u_i64)
VSX_LOAD_SCALAR(lxsihzx, ld16u_i64)
VSX_LOAD_SCALAR(lxsiwzx, ld32u_i64)
VSX_LOAD_SCALAR(lxsspx, ld32fs)

static void gen_lxvd2x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    t0 = tcg_temp_new_i64();
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld64_i64(ctx, t0, EA);
    set_cpu_vsr(xT(ctx->opcode), t0, true);
    tcg_gen_addi_tl(EA, EA, 8);
    gen_qemu_ld64_i64(ctx, t0, EA);
    set_cpu_vsr(xT(ctx->opcode), t0, false);
}

static void gen_lxvw4x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xth;
    TCGv_i64 xtl;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();

    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();

    gen_addr_reg_index(ctx, EA);
    if (ctx->le_mode) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        tcg_gen_qemu_ld_i64(t0, EA, ctx->mem_idx, MO_LEUQ);
        tcg_gen_shri_i64(t1, t0, 32);
        tcg_gen_deposit_i64(xth, t1, t0, 32, 32);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_ld_i64(t0, EA, ctx->mem_idx, MO_LEUQ);
        tcg_gen_shri_i64(t1, t0, 32);
        tcg_gen_deposit_i64(xtl, t1, t0, 32, 32);
    } else {
        tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEUQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEUQ);
    }
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_lxvwsx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i32 data;

    if (xT(ctx->opcode) < 32) {
        if (unlikely(!ctx->vsx_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VSXU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }

    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();

    gen_addr_reg_index(ctx, EA);

    data = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(data, EA, ctx->mem_idx, DEF_MEMOP(MO_UL));
    tcg_gen_gvec_dup_i32(MO_UL, vsr_full_offset(xT(ctx->opcode)), 16, 16, data);
}

static void gen_lxvdsx(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 data;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }

    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();

    gen_addr_reg_index(ctx, EA);

    data = tcg_temp_new_i64();
    tcg_gen_qemu_ld_i64(data, EA, ctx->mem_idx, DEF_MEMOP(MO_UQ));
    tcg_gen_gvec_dup_i64(MO_UQ, vsr_full_offset(xT(ctx->opcode)), 16, 16, data);
}

static void gen_bswap16x8(TCGv_i64 outh, TCGv_i64 outl,
                          TCGv_i64 inh, TCGv_i64 inl)
{
    TCGv_i64 mask = tcg_const_i64(0x00FF00FF00FF00FF);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    /* outh = ((inh & mask) << 8) | ((inh >> 8) & mask) */
    tcg_gen_and_i64(t0, inh, mask);
    tcg_gen_shli_i64(t0, t0, 8);
    tcg_gen_shri_i64(t1, inh, 8);
    tcg_gen_and_i64(t1, t1, mask);
    tcg_gen_or_i64(outh, t0, t1);

    /* outl = ((inl & mask) << 8) | ((inl >> 8) & mask) */
    tcg_gen_and_i64(t0, inl, mask);
    tcg_gen_shli_i64(t0, t0, 8);
    tcg_gen_shri_i64(t1, inl, 8);
    tcg_gen_and_i64(t1, t1, mask);
    tcg_gen_or_i64(outl, t0, t1);
}

static void gen_bswap32x4(TCGv_i64 outh, TCGv_i64 outl,
                          TCGv_i64 inh, TCGv_i64 inl)
{
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 lo = tcg_temp_new_i64();

    tcg_gen_bswap64_i64(hi, inh);
    tcg_gen_bswap64_i64(lo, inl);
    tcg_gen_shri_i64(outh, hi, 32);
    tcg_gen_deposit_i64(outh, outh, hi, 32, 32);
    tcg_gen_shri_i64(outl, lo, 32);
    tcg_gen_deposit_i64(outl, outl, lo, 32, 32);
}

static void gen_lxvh8x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xth;
    TCGv_i64 xtl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    gen_set_access_type(ctx, ACCESS_INT);

    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEUQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEUQ);
    if (ctx->le_mode) {
        gen_bswap16x8(xth, xtl, xth, xtl);
    }
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_lxvb16x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xth;
    TCGv_i64 xtl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEUQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEUQ);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

#ifdef TARGET_PPC64
#define VSX_VECTOR_LOAD_STORE_LENGTH(name)                         \
static void gen_##name(DisasContext *ctx)                          \
{                                                                  \
    TCGv EA;                                                       \
    TCGv_ptr xt;                                                   \
                                                                   \
    if (xT(ctx->opcode) < 32) {                                    \
        if (unlikely(!ctx->vsx_enabled)) {                         \
            gen_exception(ctx, POWERPC_EXCP_VSXU);                 \
            return;                                                \
        }                                                          \
    } else {                                                       \
        if (unlikely(!ctx->altivec_enabled)) {                     \
            gen_exception(ctx, POWERPC_EXCP_VPU);                  \
            return;                                                \
        }                                                          \
    }                                                              \
    EA = tcg_temp_new();                                           \
    xt = gen_vsr_ptr(xT(ctx->opcode));                             \
    gen_set_access_type(ctx, ACCESS_INT);                          \
    gen_addr_register(ctx, EA);                                    \
    gen_helper_##name(cpu_env, EA, xt, cpu_gpr[rB(ctx->opcode)]);  \
}

VSX_VECTOR_LOAD_STORE_LENGTH(lxvl)
VSX_VECTOR_LOAD_STORE_LENGTH(lxvll)
VSX_VECTOR_LOAD_STORE_LENGTH(stxvl)
VSX_VECTOR_LOAD_STORE_LENGTH(stxvll)
#endif

#define VSX_STORE_SCALAR(name, operation)                     \
static void gen_##name(DisasContext *ctx)                     \
{                                                             \
    TCGv EA;                                                  \
    TCGv_i64 t0;                                              \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    t0 = tcg_temp_new_i64();                                  \
    gen_set_access_type(ctx, ACCESS_INT);                     \
    EA = tcg_temp_new();                                      \
    gen_addr_reg_index(ctx, EA);                              \
    get_cpu_vsr(t0, xS(ctx->opcode), true);                   \
    gen_qemu_##operation(ctx, t0, EA);                        \
}

VSX_STORE_SCALAR(stxsdx, st64_i64)

VSX_STORE_SCALAR(stxsibx, st8_i64)
VSX_STORE_SCALAR(stxsihx, st16_i64)
VSX_STORE_SCALAR(stxsiwx, st32_i64)
VSX_STORE_SCALAR(stxsspx, st32fs)

static void gen_stxvd2x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 t0;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    t0 = tcg_temp_new_i64();
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    get_cpu_vsr(t0, xS(ctx->opcode), true);
    gen_qemu_st64_i64(ctx, t0, EA);
    tcg_gen_addi_tl(EA, EA, 8);
    get_cpu_vsr(t0, xS(ctx->opcode), false);
    gen_qemu_st64_i64(ctx, t0, EA);
}

static void gen_stxvw4x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xsh;
    TCGv_i64 xsl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xsh = tcg_temp_new_i64();
    xsl = tcg_temp_new_i64();
    get_cpu_vsr(xsh, xS(ctx->opcode), true);
    get_cpu_vsr(xsl, xS(ctx->opcode), false);
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    if (ctx->le_mode) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        tcg_gen_shri_i64(t0, xsh, 32);
        tcg_gen_deposit_i64(t1, t0, xsh, 32, 32);
        tcg_gen_qemu_st_i64(t1, EA, ctx->mem_idx, MO_LEUQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_shri_i64(t0, xsl, 32);
        tcg_gen_deposit_i64(t1, t0, xsl, 32, 32);
        tcg_gen_qemu_st_i64(t1, EA, ctx->mem_idx, MO_LEUQ);
    } else {
        tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEUQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEUQ);
    }
}

static void gen_stxvh8x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xsh;
    TCGv_i64 xsl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xsh = tcg_temp_new_i64();
    xsl = tcg_temp_new_i64();
    get_cpu_vsr(xsh, xS(ctx->opcode), true);
    get_cpu_vsr(xsl, xS(ctx->opcode), false);
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    if (ctx->le_mode) {
        TCGv_i64 outh = tcg_temp_new_i64();
        TCGv_i64 outl = tcg_temp_new_i64();

        gen_bswap16x8(outh, outl, xsh, xsl);
        tcg_gen_qemu_st_i64(outh, EA, ctx->mem_idx, MO_BEUQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(outl, EA, ctx->mem_idx, MO_BEUQ);
    } else {
        tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEUQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEUQ);
    }
}

static void gen_stxvb16x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xsh;
    TCGv_i64 xsl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xsh = tcg_temp_new_i64();
    xsl = tcg_temp_new_i64();
    get_cpu_vsr(xsh, xS(ctx->opcode), true);
    get_cpu_vsr(xsl, xS(ctx->opcode), false);
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEUQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEUQ);
}

static void gen_mfvsrwz(DisasContext *ctx)
{
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->fpu_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_FPU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 xsh = tcg_temp_new_i64();
    get_cpu_vsr(xsh, xS(ctx->opcode), true);
    tcg_gen_ext32u_i64(tmp, xsh);
    tcg_gen_trunc_i64_tl(cpu_gpr[rA(ctx->opcode)], tmp);
}

static void gen_mtvsrwa(DisasContext *ctx)
{
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->fpu_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_FPU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 xsh = tcg_temp_new_i64();
    tcg_gen_extu_tl_i64(tmp, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32s_i64(xsh, tmp);
    set_cpu_vsr(xT(ctx->opcode), xsh, true);
}

static void gen_mtvsrwz(DisasContext *ctx)
{
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->fpu_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_FPU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 xsh = tcg_temp_new_i64();
    tcg_gen_extu_tl_i64(tmp, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32u_i64(xsh, tmp);
    set_cpu_vsr(xT(ctx->opcode), xsh, true);
}

#if defined(TARGET_PPC64)
static void gen_mfvsrd(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->fpu_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_FPU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    t0 = tcg_temp_new_i64();
    get_cpu_vsr(t0, xS(ctx->opcode), true);
    tcg_gen_mov_i64(cpu_gpr[rA(ctx->opcode)], t0);
}

static void gen_mtvsrd(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->fpu_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_FPU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    t0 = tcg_temp_new_i64();
    tcg_gen_mov_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    set_cpu_vsr(xT(ctx->opcode), t0, true);
}

static void gen_mfvsrld(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->vsx_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VSXU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }
    t0 = tcg_temp_new_i64();
    get_cpu_vsr(t0, xS(ctx->opcode), false);
    tcg_gen_mov_i64(cpu_gpr[rA(ctx->opcode)], t0);
}

static void gen_mtvsrdd(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (xT(ctx->opcode) < 32) {
        if (unlikely(!ctx->vsx_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VSXU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }

    t0 = tcg_temp_new_i64();
    if (!rA(ctx->opcode)) {
        tcg_gen_movi_i64(t0, 0);
    } else {
        tcg_gen_mov_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    }
    set_cpu_vsr(xT(ctx->opcode), t0, true);

    tcg_gen_mov_i64(t0, cpu_gpr[rB(ctx->opcode)]);
    set_cpu_vsr(xT(ctx->opcode), t0, false);
}

static void gen_mtvsrws(DisasContext *ctx)
{
    TCGv_i64 t0;
    if (xT(ctx->opcode) < 32) {
        if (unlikely(!ctx->vsx_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VSXU);
            return;
        }
    } else {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    }

    t0 = tcg_temp_new_i64();
    tcg_gen_deposit_i64(t0, cpu_gpr[rA(ctx->opcode)],
                        cpu_gpr[rA(ctx->opcode)], 32, 32);
    set_cpu_vsr(xT(ctx->opcode), t0, false);
    set_cpu_vsr(xT(ctx->opcode), t0, true);
}

#endif

#define OP_ABS 1
#define OP_NABS 2
#define OP_NEG 3
#define OP_CPSGN 4
#define SGN_MASK_DP  0x8000000000000000ull
#define SGN_MASK_SP 0x8000000080000000ull
#define EXP_MASK_DP  0x7FF0000000000000ull
#define EXP_MASK_SP 0x7F8000007F800000ull
#define FRC_MASK_DP (~(SGN_MASK_DP | EXP_MASK_DP))
#define FRC_MASK_SP (~(SGN_MASK_SP | EXP_MASK_SP))

#define VSX_SCALAR_MOVE(name, op, sgn_mask)                       \
static void glue(gen_, name)(DisasContext *ctx)                   \
    {                                                             \
        TCGv_i64 xb, sgm;                                         \
        if (unlikely(!ctx->vsx_enabled)) {                        \
            gen_exception(ctx, POWERPC_EXCP_VSXU);                \
            return;                                               \
        }                                                         \
        xb = tcg_temp_new_i64();                                  \
        sgm = tcg_temp_new_i64();                                 \
        get_cpu_vsr(xb, xB(ctx->opcode), true);                   \
        tcg_gen_movi_i64(sgm, sgn_mask);                          \
        switch (op) {                                             \
            case OP_ABS: {                                        \
                tcg_gen_andc_i64(xb, xb, sgm);                    \
                break;                                            \
            }                                                     \
            case OP_NABS: {                                       \
                tcg_gen_or_i64(xb, xb, sgm);                      \
                break;                                            \
            }                                                     \
            case OP_NEG: {                                        \
                tcg_gen_xor_i64(xb, xb, sgm);                     \
                break;                                            \
            }                                                     \
            case OP_CPSGN: {                                      \
                TCGv_i64 xa = tcg_temp_new_i64();                 \
                get_cpu_vsr(xa, xA(ctx->opcode), true);           \
                tcg_gen_and_i64(xa, xa, sgm);                     \
                tcg_gen_andc_i64(xb, xb, sgm);                    \
                tcg_gen_or_i64(xb, xb, xa);                       \
                break;                                            \
            }                                                     \
        }                                                         \
        set_cpu_vsr(xT(ctx->opcode), xb, true);                   \
        set_cpu_vsr(xT(ctx->opcode), tcg_constant_i64(0), false); \
    }

VSX_SCALAR_MOVE(xsabsdp, OP_ABS, SGN_MASK_DP)
VSX_SCALAR_MOVE(xsnabsdp, OP_NABS, SGN_MASK_DP)
VSX_SCALAR_MOVE(xsnegdp, OP_NEG, SGN_MASK_DP)
VSX_SCALAR_MOVE(xscpsgndp, OP_CPSGN, SGN_MASK_DP)

#define VSX_SCALAR_MOVE_QP(name, op, sgn_mask)                    \
static void glue(gen_, name)(DisasContext *ctx)                   \
{                                                                 \
    int xa;                                                       \
    int xt = rD(ctx->opcode) + 32;                                \
    int xb = rB(ctx->opcode) + 32;                                \
    TCGv_i64 xah, xbh, xbl, sgm, tmp;                             \
                                                                  \
    if (unlikely(!ctx->vsx_enabled)) {                            \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                    \
        return;                                                   \
    }                                                             \
    xbh = tcg_temp_new_i64();                                     \
    xbl = tcg_temp_new_i64();                                     \
    sgm = tcg_temp_new_i64();                                     \
    tmp = tcg_temp_new_i64();                                     \
    get_cpu_vsr(xbh, xb, true);                                   \
    get_cpu_vsr(xbl, xb, false);                                  \
    tcg_gen_movi_i64(sgm, sgn_mask);                              \
    switch (op) {                                                 \
    case OP_ABS:                                                  \
        tcg_gen_andc_i64(xbh, xbh, sgm);                          \
        break;                                                    \
    case OP_NABS:                                                 \
        tcg_gen_or_i64(xbh, xbh, sgm);                            \
        break;                                                    \
    case OP_NEG:                                                  \
        tcg_gen_xor_i64(xbh, xbh, sgm);                           \
        break;                                                    \
    case OP_CPSGN:                                                \
        xah = tcg_temp_new_i64();                                 \
        xa = rA(ctx->opcode) + 32;                                \
        get_cpu_vsr(tmp, xa, true);                               \
        tcg_gen_and_i64(xah, tmp, sgm);                           \
        tcg_gen_andc_i64(xbh, xbh, sgm);                          \
        tcg_gen_or_i64(xbh, xbh, xah);                            \
        break;                                                    \
    }                                                             \
    set_cpu_vsr(xt, xbh, true);                                   \
    set_cpu_vsr(xt, xbl, false);                                  \
}

VSX_SCALAR_MOVE_QP(xsabsqp, OP_ABS, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xsnabsqp, OP_NABS, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xsnegqp, OP_NEG, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xscpsgnqp, OP_CPSGN, SGN_MASK_DP)

#define TCG_OP_IMM_i64(FUNC, OP, IMM)                           \
    static void FUNC(TCGv_i64 t, TCGv_i64 b)                    \
    {                                                           \
        OP(t, b, IMM);                                          \
    }

TCG_OP_IMM_i64(do_xvabssp_i64, tcg_gen_andi_i64, ~SGN_MASK_SP)
TCG_OP_IMM_i64(do_xvnabssp_i64, tcg_gen_ori_i64, SGN_MASK_SP)
TCG_OP_IMM_i64(do_xvnegsp_i64, tcg_gen_xori_i64, SGN_MASK_SP)
TCG_OP_IMM_i64(do_xvabsdp_i64, tcg_gen_andi_i64, ~SGN_MASK_DP)
TCG_OP_IMM_i64(do_xvnabsdp_i64, tcg_gen_ori_i64, SGN_MASK_DP)
TCG_OP_IMM_i64(do_xvnegdp_i64, tcg_gen_xori_i64, SGN_MASK_DP)
#undef TCG_OP_IMM_i64

static void xv_msb_op1(unsigned vece, TCGv_vec t, TCGv_vec b,
                 void (*tcg_gen_op_vec)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec))
{
    uint64_t msb = (vece == MO_32) ? SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_op_vec(vece, t, b, tcg_constant_vec_matching(t, vece, msb));
}

static void do_xvabs_vec(unsigned vece, TCGv_vec t, TCGv_vec b)
{
    xv_msb_op1(vece, t, b, tcg_gen_andc_vec);
}

static void do_xvnabs_vec(unsigned vece, TCGv_vec t, TCGv_vec b)
{
    xv_msb_op1(vece, t, b, tcg_gen_or_vec);
}

static void do_xvneg_vec(unsigned vece, TCGv_vec t, TCGv_vec b)
{
    xv_msb_op1(vece, t, b, tcg_gen_xor_vec);
}

static bool do_vsx_msb_op(DisasContext *ctx, arg_XX2 *a, unsigned vece,
                          void (*vec)(unsigned, TCGv_vec, TCGv_vec),
                          void (*i64)(TCGv_i64, TCGv_i64))
{
    static const TCGOpcode vecop_list[] = {
        0
    };

    const GVecGen2 op = {
       .fni8 = i64,
       .fniv = vec,
       .opt_opc = vecop_list,
       .vece = vece
    };

    REQUIRE_INSNS_FLAGS2(ctx, VSX);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_2(vsr_full_offset(a->xt), vsr_full_offset(a->xb),
                   16, 16, &op);

    return true;
}

TRANS(XVABSDP, do_vsx_msb_op, MO_64, do_xvabs_vec, do_xvabsdp_i64)
TRANS(XVNABSDP, do_vsx_msb_op, MO_64, do_xvnabs_vec, do_xvnabsdp_i64)
TRANS(XVNEGDP, do_vsx_msb_op, MO_64, do_xvneg_vec, do_xvnegdp_i64)
TRANS(XVABSSP, do_vsx_msb_op, MO_32, do_xvabs_vec, do_xvabssp_i64)
TRANS(XVNABSSP, do_vsx_msb_op, MO_32, do_xvnabs_vec, do_xvnabssp_i64)
TRANS(XVNEGSP, do_vsx_msb_op, MO_32, do_xvneg_vec, do_xvnegsp_i64)

static void do_xvcpsgndp_i64(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_andi_i64(a, a, SGN_MASK_DP);
    tcg_gen_andi_i64(b, b, ~SGN_MASK_DP);
    tcg_gen_or_i64(t, a, b);
}

static void do_xvcpsgnsp_i64(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)
{
    tcg_gen_andi_i64(a, a, SGN_MASK_SP);
    tcg_gen_andi_i64(b, b, ~SGN_MASK_SP);
    tcg_gen_or_i64(t, a, b);
}

static void do_xvcpsgn_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    uint64_t msb = (vece == MO_32) ? SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_bitsel_vec(vece, t, tcg_constant_vec_matching(t, vece, msb), a, b);
}

static bool do_xvcpsgn(DisasContext *ctx, arg_XX3 *a, unsigned vece)
{
    static const TCGOpcode vecop_list[] = {
        0
    };

    static const GVecGen3 op[] = {
        {
            .fni8 = do_xvcpsgnsp_i64,
            .fniv = do_xvcpsgn_vec,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = do_xvcpsgndp_i64,
            .fniv = do_xvcpsgn_vec,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    REQUIRE_INSNS_FLAGS2(ctx, VSX);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_3(vsr_full_offset(a->xt), vsr_full_offset(a->xa),
                   vsr_full_offset(a->xb), 16, 16, &op[vece - MO_32]);

    return true;
}

TRANS(XVCPSGNSP, do_xvcpsgn, MO_32)
TRANS(XVCPSGNDP, do_xvcpsgn, MO_64)

#define VSX_CMP(name, op1, op2, inval, type)                                  \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 ignored;                                                         \
    TCGv_ptr xt, xa, xb;                                                      \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    xt = gen_vsr_ptr(xT(ctx->opcode));                                        \
    xa = gen_vsr_ptr(xA(ctx->opcode));                                        \
    xb = gen_vsr_ptr(xB(ctx->opcode));                                        \
    if ((ctx->opcode >> (31 - 21)) & 1) {                                     \
        gen_helper_##name(cpu_crf[6], cpu_env, xt, xa, xb);                   \
    } else {                                                                  \
        ignored = tcg_temp_new_i32();                                         \
        gen_helper_##name(ignored, cpu_env, xt, xa, xb);                      \
    }                                                                         \
}

VSX_CMP(xvcmpeqdp, 0x0C, 0x0C, 0, PPC2_VSX)
VSX_CMP(xvcmpgedp, 0x0C, 0x0E, 0, PPC2_VSX)
VSX_CMP(xvcmpgtdp, 0x0C, 0x0D, 0, PPC2_VSX)
VSX_CMP(xvcmpnedp, 0x0C, 0x0F, 0, PPC2_ISA300)
VSX_CMP(xvcmpeqsp, 0x0C, 0x08, 0, PPC2_VSX)
VSX_CMP(xvcmpgesp, 0x0C, 0x0A, 0, PPC2_VSX)
VSX_CMP(xvcmpgtsp, 0x0C, 0x09, 0, PPC2_VSX)
VSX_CMP(xvcmpnesp, 0x0C, 0x0B, 0, PPC2_VSX)

static bool trans_XSCVQPDP(DisasContext *ctx, arg_X_tb_rc *a)
{
    TCGv_i32 ro;
    TCGv_ptr xt, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    ro = tcg_const_i32(a->rc);

    xt = gen_avr_ptr(a->rt);
    xb = gen_avr_ptr(a->rb);
    gen_helper_XSCVQPDP(cpu_env, ro, xt, xb);
    return true;
}

static bool do_helper_env_X_tb(DisasContext *ctx, arg_X_tb *a,
                               void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr xt, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    xt = gen_avr_ptr(a->rt);
    xb = gen_avr_ptr(a->rb);
    gen_helper(cpu_env, xt, xb);
    return true;
}

TRANS(XSCVUQQP, do_helper_env_X_tb, gen_helper_XSCVUQQP)
TRANS(XSCVSQQP, do_helper_env_X_tb, gen_helper_XSCVSQQP)
TRANS(XSCVQPUQZ, do_helper_env_X_tb, gen_helper_XSCVQPUQZ)
TRANS(XSCVQPSQZ, do_helper_env_X_tb, gen_helper_XSCVQPSQZ)

#define GEN_VSX_HELPER_2(name, op1, op2, inval, type)                         \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    gen_helper_##name(cpu_env, opc);                                          \
}

#define GEN_VSX_HELPER_X3(name, op1, op2, inval, type)                        \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_ptr xt, xa, xb;                                                      \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    xt = gen_vsr_ptr(xT(ctx->opcode));                                        \
    xa = gen_vsr_ptr(xA(ctx->opcode));                                        \
    xb = gen_vsr_ptr(xB(ctx->opcode));                                        \
    gen_helper_##name(cpu_env, xt, xa, xb);                                   \
}

#define GEN_VSX_HELPER_X2(name, op1, op2, inval, type)                        \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_ptr xt, xb;                                                          \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    xt = gen_vsr_ptr(xT(ctx->opcode));                                        \
    xb = gen_vsr_ptr(xB(ctx->opcode));                                        \
    gen_helper_##name(cpu_env, xt, xb);                                       \
}

#define GEN_VSX_HELPER_X2_AB(name, op1, op2, inval, type)                     \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    TCGv_ptr xa, xb;                                                          \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    xa = gen_vsr_ptr(xA(ctx->opcode));                                        \
    xb = gen_vsr_ptr(xB(ctx->opcode));                                        \
    gen_helper_##name(cpu_env, opc, xa, xb);                                  \
}

#define GEN_VSX_HELPER_X1(name, op1, op2, inval, type)                        \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    TCGv_ptr xb;                                                              \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    xb = gen_vsr_ptr(xB(ctx->opcode));                                        \
    gen_helper_##name(cpu_env, opc, xb);                                      \
}

#define GEN_VSX_HELPER_R3(name, op1, op2, inval, type)                        \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    TCGv_ptr xt, xa, xb;                                                      \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    xt = gen_vsr_ptr(rD(ctx->opcode) + 32);                                   \
    xa = gen_vsr_ptr(rA(ctx->opcode) + 32);                                   \
    xb = gen_vsr_ptr(rB(ctx->opcode) + 32);                                   \
    gen_helper_##name(cpu_env, opc, xt, xa, xb);                              \
}

#define GEN_VSX_HELPER_R2(name, op1, op2, inval, type)                        \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    TCGv_ptr xt, xb;                                                          \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    xt = gen_vsr_ptr(rD(ctx->opcode) + 32);                                   \
    xb = gen_vsr_ptr(rB(ctx->opcode) + 32);                                   \
    gen_helper_##name(cpu_env, opc, xt, xb);                                  \
}

#define GEN_VSX_HELPER_R2_AB(name, op1, op2, inval, type)                     \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 opc;                                                             \
    TCGv_ptr xa, xb;                                                          \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    opc = tcg_const_i32(ctx->opcode);                                         \
    xa = gen_vsr_ptr(rA(ctx->opcode) + 32);                                   \
    xb = gen_vsr_ptr(rB(ctx->opcode) + 32);                                   \
    gen_helper_##name(cpu_env, opc, xa, xb);                                  \
}

#define GEN_VSX_HELPER_XT_XB_ENV(name, op1, op2, inval, type) \
static void gen_##name(DisasContext *ctx)                     \
{                                                             \
    TCGv_i64 t0;                                              \
    TCGv_i64 t1;                                              \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    t0 = tcg_temp_new_i64();                                  \
    t1 = tcg_temp_new_i64();                                  \
    get_cpu_vsr(t0, xB(ctx->opcode), true);                   \
    gen_helper_##name(t1, cpu_env, t0);                       \
    set_cpu_vsr(xT(ctx->opcode), t1, true);                   \
    set_cpu_vsr(xT(ctx->opcode), tcg_constant_i64(0), false); \
}

GEN_VSX_HELPER_X3(xsadddp, 0x00, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_R3(xsaddqp, 0x04, 0x00, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xssubdp, 0x00, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xsmuldp, 0x00, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_R3(xsmulqp, 0x04, 0x01, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xsdivdp, 0x00, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_R3(xsdivqp, 0x04, 0x11, 0, PPC2_ISA300)
GEN_VSX_HELPER_X2(xsredp, 0x14, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xssqrtdp, 0x16, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrsqrtedp, 0x14, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_X2_AB(xstdivdp, 0x14, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_X1(xstsqrtdp, 0x14, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_X2_AB(xscmpexpdp, 0x0C, 0x07, 0, PPC2_ISA300)
GEN_VSX_HELPER_R2_AB(xscmpexpqp, 0x04, 0x05, 0, PPC2_ISA300)
GEN_VSX_HELPER_X2_AB(xscmpodp, 0x0C, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_X2_AB(xscmpudp, 0x0C, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_R2_AB(xscmpoqp, 0x04, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_R2_AB(xscmpuqp, 0x04, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xsmaxdp, 0x00, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xsmindp, 0x00, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xscvdphp, 0x16, 0x15, 0x11, PPC2_ISA300)
GEN_VSX_HELPER_X2(xscvdpsp, 0x12, 0x10, 0, PPC2_VSX)
GEN_VSX_HELPER_R2(xscvdpqp, 0x04, 0x1A, 0x16, PPC2_ISA300)
GEN_VSX_HELPER_XT_XB_ENV(xscvdpspn, 0x16, 0x10, 0, PPC2_VSX207)
GEN_VSX_HELPER_R2(xscvqpsdz, 0x04, 0x1A, 0x19, PPC2_ISA300)
GEN_VSX_HELPER_R2(xscvqpswz, 0x04, 0x1A, 0x09, PPC2_ISA300)
GEN_VSX_HELPER_R2(xscvqpudz, 0x04, 0x1A, 0x11, PPC2_ISA300)
GEN_VSX_HELPER_R2(xscvqpuwz, 0x04, 0x1A, 0x01, PPC2_ISA300)
GEN_VSX_HELPER_X2(xscvhpdp, 0x16, 0x15, 0x10, PPC2_ISA300)
GEN_VSX_HELPER_R2(xscvsdqp, 0x04, 0x1A, 0x0A, PPC2_ISA300)
GEN_VSX_HELPER_X2(xscvspdp, 0x12, 0x14, 0, PPC2_VSX)

/* test if +Inf */
static void gen_is_pos_inf(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t exp_msk = (vece == MO_32) ? (uint32_t)EXP_MASK_SP : EXP_MASK_DP;
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, exp_msk));
}

/* test if -Inf */
static void gen_is_neg_inf(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t exp_msk = (vece == MO_32) ? (uint32_t)EXP_MASK_SP : EXP_MASK_DP;
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, sgn_msk | exp_msk));
}

/* test if +Inf or -Inf */
static void gen_is_any_inf(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t exp_msk = (vece == MO_32) ? (uint32_t)EXP_MASK_SP : EXP_MASK_DP;
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_andc_vec(vece, b, b, tcg_constant_vec_matching(t, vece, sgn_msk));
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, exp_msk));
}

/* test if +0 */
static void gen_is_pos_zero(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, 0));
}

/* test if -0 */
static void gen_is_neg_zero(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, sgn_msk));
}

/* test if +0 or -0 */
static void gen_is_any_zero(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_andc_vec(vece, b, b, tcg_constant_vec_matching(t, vece, sgn_msk));
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t, b,
                    tcg_constant_vec_matching(t, vece, 0));
}

/* test if +Denormal */
static void gen_is_pos_denormal(unsigned vece, TCGv_vec t,
                                TCGv_vec b, int64_t v)
{
    uint64_t frc_msk = (vece == MO_32) ? (uint32_t)FRC_MASK_SP : FRC_MASK_DP;
    tcg_gen_cmp_vec(TCG_COND_LEU, vece, t, b,
                    tcg_constant_vec_matching(t, vece, frc_msk));
    tcg_gen_cmp_vec(TCG_COND_NE, vece, b, b,
                    tcg_constant_vec_matching(t, vece, 0));
    tcg_gen_and_vec(vece, t, t, b);
}

/* test if -Denormal */
static void gen_is_neg_denormal(unsigned vece, TCGv_vec t,
                                TCGv_vec b, int64_t v)
{
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    uint64_t frc_msk = (vece == MO_32) ? (uint32_t)FRC_MASK_SP : FRC_MASK_DP;
    tcg_gen_cmp_vec(TCG_COND_LEU, vece, t, b,
                    tcg_constant_vec_matching(t, vece, sgn_msk | frc_msk));
    tcg_gen_cmp_vec(TCG_COND_GTU, vece, b, b,
                    tcg_constant_vec_matching(t, vece, sgn_msk));
    tcg_gen_and_vec(vece, t, t, b);
}

/* test if +Denormal or -Denormal */
static void gen_is_any_denormal(unsigned vece, TCGv_vec t,
                                TCGv_vec b, int64_t v)
{
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    uint64_t frc_msk = (vece == MO_32) ? (uint32_t)FRC_MASK_SP : FRC_MASK_DP;
    tcg_gen_andc_vec(vece, b, b, tcg_constant_vec_matching(t, vece, sgn_msk));
    tcg_gen_cmp_vec(TCG_COND_LE, vece, t, b,
                    tcg_constant_vec_matching(t, vece, frc_msk));
    tcg_gen_cmp_vec(TCG_COND_NE, vece, b, b,
                    tcg_constant_vec_matching(t, vece, 0));
    tcg_gen_and_vec(vece, t, t, b);
}

/* test if NaN */
static void gen_is_nan(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t v)
{
    uint64_t exp_msk = (vece == MO_32) ? (uint32_t)EXP_MASK_SP : EXP_MASK_DP;
    uint64_t sgn_msk = (vece == MO_32) ? (uint32_t)SGN_MASK_SP : SGN_MASK_DP;
    tcg_gen_and_vec(vece, b, b, tcg_constant_vec_matching(t, vece, ~sgn_msk));
    tcg_gen_cmp_vec(TCG_COND_GT, vece, t, b,
                    tcg_constant_vec_matching(t, vece, exp_msk));
}

static bool do_xvtstdc(DisasContext *ctx, arg_XX2_uim *a, unsigned vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_cmp_vec, 0
    };

    GVecGen2i op = {
        .fnoi = (vece == MO_32) ? gen_helper_XVTSTDCSP : gen_helper_XVTSTDCDP,
        .vece = vece,
        .opt_opc = vecop_list
    };

    REQUIRE_VSX(ctx);

    switch (a->uim) {
    case 0:
        set_cpu_vsr(a->xt, tcg_constant_i64(0), true);
        set_cpu_vsr(a->xt, tcg_constant_i64(0), false);
        return true;
    case ((1 << 0) | (1 << 1)):
        /* test if +Denormal or -Denormal */
        op.fniv = gen_is_any_denormal;
        break;
    case (1 << 0):
        /* test if -Denormal */
        op.fniv = gen_is_neg_denormal;
        break;
    case (1 << 1):
        /* test if +Denormal */
        op.fniv = gen_is_pos_denormal;
        break;
    case ((1 << 2) | (1 << 3)):
        /* test if +0 or -0 */
        op.fniv = gen_is_any_zero;
        break;
    case (1 << 2):
        /* test if -0 */
        op.fniv = gen_is_neg_zero;
        break;
    case (1 << 3):
        /* test if +0 */
        op.fniv = gen_is_pos_zero;
        break;
    case ((1 << 4) | (1 << 5)):
        /* test if +Inf or -Inf */
        op.fniv = gen_is_any_inf;
        break;
    case (1 << 4):
        /* test if -Inf */
        op.fniv = gen_is_neg_inf;
        break;
    case (1 << 5):
        /* test if +Inf */
        op.fniv = gen_is_pos_inf;
        break;
    case (1 << 6):
        /* test if NaN */
        op.fniv = gen_is_nan;
        break;
    }
    tcg_gen_gvec_2i(vsr_full_offset(a->xt), vsr_full_offset(a->xb),
                    16, 16, a->uim, &op);

    return true;
}

TRANS_FLAGS2(VSX, XVTSTDCSP, do_xvtstdc, MO_32)
TRANS_FLAGS2(VSX, XVTSTDCDP, do_xvtstdc, MO_64)

static bool do_XX2_bf_uim(DisasContext *ctx, arg_XX2_bf_uim *a, bool vsr,
                     void (*gen_helper)(TCGv_env, TCGv_i32, TCGv_i32, TCGv_ptr))
{
    TCGv_ptr xb;

    REQUIRE_VSX(ctx);
    xb = vsr ? gen_vsr_ptr(a->xb) : gen_avr_ptr(a->xb);
    gen_helper(cpu_env, tcg_constant_i32(a->bf), tcg_constant_i32(a->uim), xb);
    return true;
}

TRANS_FLAGS2(ISA300, XSTSTDCSP, do_XX2_bf_uim, true, gen_helper_XSTSTDCSP)
TRANS_FLAGS2(ISA300, XSTSTDCDP, do_XX2_bf_uim, true, gen_helper_XSTSTDCDP)
TRANS_FLAGS2(ISA300, XSTSTDCQP, do_XX2_bf_uim, false, gen_helper_XSTSTDCQP)

bool trans_XSCVSPDPN(DisasContext *ctx, arg_XX2 *a)
{
    TCGv_i64 tmp;

    REQUIRE_INSNS_FLAGS2(ctx, VSX207);
    REQUIRE_VSX(ctx);

    tmp = tcg_temp_new_i64();
    get_cpu_vsr(tmp, a->xb, true);

    gen_helper_XSCVSPDPN(tmp, tmp);

    set_cpu_vsr(a->xt, tmp, true);
    set_cpu_vsr(a->xt, tcg_constant_i64(0), false);
    return true;
}

GEN_VSX_HELPER_X2(xscvdpsxds, 0x10, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xscvdpsxws, 0x10, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xscvdpuxds, 0x10, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xscvdpuxws, 0x10, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xscvsxddp, 0x10, 0x17, 0, PPC2_VSX)
GEN_VSX_HELPER_R2(xscvudqp, 0x04, 0x1A, 0x02, PPC2_ISA300)
GEN_VSX_HELPER_X2(xscvuxddp, 0x10, 0x16, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrdpi, 0x12, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrdpic, 0x16, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrdpim, 0x12, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrdpip, 0x12, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xsrdpiz, 0x12, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_XT_XB_ENV(xsrsp, 0x12, 0x11, 0, PPC2_VSX207)
GEN_VSX_HELPER_R2(xsrqpi, 0x05, 0x00, 0, PPC2_ISA300)
GEN_VSX_HELPER_R2(xsrqpxp, 0x05, 0x01, 0, PPC2_ISA300)
GEN_VSX_HELPER_R2(xssqrtqp, 0x04, 0x19, 0x1B, PPC2_ISA300)
GEN_VSX_HELPER_R3(xssubqp, 0x04, 0x10, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xsaddsp, 0x00, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_X3(xssubsp, 0x00, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_X3(xsmulsp, 0x00, 0x02, 0, PPC2_VSX207)
GEN_VSX_HELPER_X3(xsdivsp, 0x00, 0x03, 0, PPC2_VSX207)
GEN_VSX_HELPER_X2(xsresp, 0x14, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_X2(xssqrtsp, 0x16, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_X2(xsrsqrtesp, 0x14, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_X2(xscvsxdsp, 0x10, 0x13, 0, PPC2_VSX207)
GEN_VSX_HELPER_X2(xscvuxdsp, 0x10, 0x12, 0, PPC2_VSX207)

GEN_VSX_HELPER_X3(xvadddp, 0x00, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvsubdp, 0x00, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvmuldp, 0x00, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvdivdp, 0x00, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvredp, 0x14, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvsqrtdp, 0x16, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrsqrtedp, 0x14, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2_AB(xvtdivdp, 0x14, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_X1(xvtsqrtdp, 0x14, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvmaxdp, 0x00, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvmindp, 0x00, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvdpsp, 0x12, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvdpsxds, 0x10, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvdpsxws, 0x10, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvdpuxds, 0x10, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvdpuxws, 0x10, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvsxddp, 0x10, 0x1F, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvuxddp, 0x10, 0x1E, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvsxwdp, 0x10, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvuxwdp, 0x10, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrdpi, 0x12, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrdpic, 0x16, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrdpim, 0x12, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrdpip, 0x12, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrdpiz, 0x12, 0x0D, 0, PPC2_VSX)

GEN_VSX_HELPER_X3(xvaddsp, 0x00, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvsubsp, 0x00, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvmulsp, 0x00, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvdivsp, 0x00, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvresp, 0x14, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvsqrtsp, 0x16, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrsqrtesp, 0x14, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_X2_AB(xvtdivsp, 0x14, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_X1(xvtsqrtsp, 0x14, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvmaxsp, 0x00, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xvminsp, 0x00, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvspdp, 0x12, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvhpsp, 0x16, 0x1D, 0x18, PPC2_ISA300)
GEN_VSX_HELPER_X2(xvcvsphp, 0x16, 0x1D, 0x19, PPC2_ISA300)
GEN_VSX_HELPER_X2(xvcvspsxds, 0x10, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvspsxws, 0x10, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvspuxds, 0x10, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvspuxws, 0x10, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvsxdsp, 0x10, 0x1B, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvuxdsp, 0x10, 0x1A, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvsxwsp, 0x10, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvcvuxwsp, 0x10, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrspi, 0x12, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrspic, 0x16, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrspim, 0x12, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrspip, 0x12, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_X2(xvrspiz, 0x12, 0x09, 0, PPC2_VSX)

static bool trans_XXPERM(DisasContext *ctx, arg_XX3 *a)
{
    TCGv_ptr xt, xa, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    gen_helper_VPERM(xt, xa, xt, xb);
    return true;
}

static bool trans_XXPERMR(DisasContext *ctx, arg_XX3 *a)
{
    TCGv_ptr xt, xa, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    gen_helper_VPERMR(xt, xa, xt, xb);
    return true;
}

static bool trans_XXPERMDI(DisasContext *ctx, arg_XX3_dm *a)
{
    TCGv_i64 t0, t1;

    REQUIRE_INSNS_FLAGS2(ctx, VSX);
    REQUIRE_VSX(ctx);

    t0 = tcg_temp_new_i64();

    if (unlikely(a->xt == a->xa || a->xt == a->xb)) {
        t1 = tcg_temp_new_i64();

        get_cpu_vsr(t0, a->xa, (a->dm & 2) == 0);
        get_cpu_vsr(t1, a->xb, (a->dm & 1) == 0);

        set_cpu_vsr(a->xt, t0, true);
        set_cpu_vsr(a->xt, t1, false);
    } else {
        get_cpu_vsr(t0, a->xa, (a->dm & 2) == 0);
        set_cpu_vsr(a->xt, t0, true);

        get_cpu_vsr(t0, a->xb, (a->dm & 1) == 0);
        set_cpu_vsr(a->xt, t0, false);
    }
    return true;
}

static bool trans_XXPERMX(DisasContext *ctx, arg_8RR_XX4_uim3 *a)
{
    TCGv_ptr xt, xa, xb, xc;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);
    xc = gen_vsr_ptr(a->xc);

    gen_helper_XXPERMX(xt, xa, xb, xc, tcg_constant_tl(a->uim3));
    return true;
}

typedef void (*xxgenpcv_genfn)(TCGv_ptr, TCGv_ptr);

static bool do_xxgenpcv(DisasContext *ctx, arg_X_imm5 *a,
                        const xxgenpcv_genfn fn[4])
{
    TCGv_ptr xt, vrb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    if (a->imm & ~0x3) {
        gen_invalid(ctx);
        return true;
    }

    xt = gen_vsr_ptr(a->xt);
    vrb = gen_avr_ptr(a->vrb);

    fn[a->imm](xt, vrb);
    return true;
}

#define XXGENPCV(NAME) \
    static bool trans_##NAME(DisasContext *ctx, arg_X_imm5 *a)  \
    {                                                           \
        static const xxgenpcv_genfn fn[4] = {                   \
            gen_helper_##NAME##_be_exp,                         \
            gen_helper_##NAME##_be_comp,                        \
            gen_helper_##NAME##_le_exp,                         \
            gen_helper_##NAME##_le_comp,                        \
        };                                                      \
        return do_xxgenpcv(ctx, a, fn);                         \
    }

XXGENPCV(XXGENPCVBM)
XXGENPCV(XXGENPCVHM)
XXGENPCV(XXGENPCVWM)
XXGENPCV(XXGENPCVDM)
#undef XXGENPCV

static bool do_xsmadd(DisasContext *ctx, int tgt, int src1, int src2, int src3,
        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr t, s1, s2, s3;

    t = gen_vsr_ptr(tgt);
    s1 = gen_vsr_ptr(src1);
    s2 = gen_vsr_ptr(src2);
    s3 = gen_vsr_ptr(src3);

    gen_helper(cpu_env, t, s1, s2, s3);
    return true;
}

static bool do_xsmadd_XX3(DisasContext *ctx, arg_XX3 *a, bool type_a,
        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    REQUIRE_VSX(ctx);

    if (type_a) {
        return do_xsmadd(ctx, a->xt, a->xa, a->xt, a->xb, gen_helper);
    }
    return do_xsmadd(ctx, a->xt, a->xa, a->xb, a->xt, gen_helper);
}

TRANS_FLAGS2(VSX, XSMADDADP, do_xsmadd_XX3, true, gen_helper_XSMADDDP)
TRANS_FLAGS2(VSX, XSMADDMDP, do_xsmadd_XX3, false, gen_helper_XSMADDDP)
TRANS_FLAGS2(VSX, XSMSUBADP, do_xsmadd_XX3, true, gen_helper_XSMSUBDP)
TRANS_FLAGS2(VSX, XSMSUBMDP, do_xsmadd_XX3, false, gen_helper_XSMSUBDP)
TRANS_FLAGS2(VSX, XSNMADDADP, do_xsmadd_XX3, true, gen_helper_XSNMADDDP)
TRANS_FLAGS2(VSX, XSNMADDMDP, do_xsmadd_XX3, false, gen_helper_XSNMADDDP)
TRANS_FLAGS2(VSX, XSNMSUBADP, do_xsmadd_XX3, true, gen_helper_XSNMSUBDP)
TRANS_FLAGS2(VSX, XSNMSUBMDP, do_xsmadd_XX3, false, gen_helper_XSNMSUBDP)
TRANS_FLAGS2(VSX207, XSMADDASP, do_xsmadd_XX3, true, gen_helper_XSMADDSP)
TRANS_FLAGS2(VSX207, XSMADDMSP, do_xsmadd_XX3, false, gen_helper_XSMADDSP)
TRANS_FLAGS2(VSX207, XSMSUBASP, do_xsmadd_XX3, true, gen_helper_XSMSUBSP)
TRANS_FLAGS2(VSX207, XSMSUBMSP, do_xsmadd_XX3, false, gen_helper_XSMSUBSP)
TRANS_FLAGS2(VSX207, XSNMADDASP, do_xsmadd_XX3, true, gen_helper_XSNMADDSP)
TRANS_FLAGS2(VSX207, XSNMADDMSP, do_xsmadd_XX3, false, gen_helper_XSNMADDSP)
TRANS_FLAGS2(VSX207, XSNMSUBASP, do_xsmadd_XX3, true, gen_helper_XSNMSUBSP)
TRANS_FLAGS2(VSX207, XSNMSUBMSP, do_xsmadd_XX3, false, gen_helper_XSNMSUBSP)

static bool do_xsmadd_X(DisasContext *ctx, arg_X_rc *a,
        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr),
        void (*gen_helper_ro)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    int vrt, vra, vrb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    vrt = a->rt + 32;
    vra = a->ra + 32;
    vrb = a->rb + 32;

    if (a->rc) {
        return do_xsmadd(ctx, vrt, vra, vrt, vrb, gen_helper_ro);
    }

    return do_xsmadd(ctx, vrt, vra, vrt, vrb, gen_helper);
}

TRANS(XSMADDQP, do_xsmadd_X, gen_helper_XSMADDQP, gen_helper_XSMADDQPO)
TRANS(XSMSUBQP, do_xsmadd_X, gen_helper_XSMSUBQP, gen_helper_XSMSUBQPO)
TRANS(XSNMADDQP, do_xsmadd_X, gen_helper_XSNMADDQP, gen_helper_XSNMADDQPO)
TRANS(XSNMSUBQP, do_xsmadd_X, gen_helper_XSNMSUBQP, gen_helper_XSNMSUBQPO)

#define GEN_VSX_HELPER_VSX_MADD(name, op1, aop, mop, inval, type)             \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_ptr xt, s1, s2, s3;                                                  \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    xt = gen_vsr_ptr(xT(ctx->opcode));                                        \
    s1 = gen_vsr_ptr(xA(ctx->opcode));                                        \
    if (ctx->opcode & PPC_BIT32(25)) {                                        \
        /*                                                                    \
         * AxT + B                                                            \
         */                                                                   \
        s2 = gen_vsr_ptr(xB(ctx->opcode));                                    \
        s3 = gen_vsr_ptr(xT(ctx->opcode));                                    \
    } else {                                                                  \
        /*                                                                    \
         * AxB + T                                                            \
         */                                                                   \
        s2 = gen_vsr_ptr(xT(ctx->opcode));                                    \
        s3 = gen_vsr_ptr(xB(ctx->opcode));                                    \
    }                                                                         \
    gen_helper_##name(cpu_env, xt, s1, s2, s3);                               \
}

GEN_VSX_HELPER_VSX_MADD(xvmadddp, 0x04, 0x0C, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvmsubdp, 0x04, 0x0E, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvnmadddp, 0x04, 0x1C, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvnmsubdp, 0x04, 0x1E, 0x1F, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvmaddsp, 0x04, 0x08, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvmsubsp, 0x04, 0x0A, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvnmaddsp, 0x04, 0x18, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xvnmsubsp, 0x04, 0x1A, 0x1B, 0, PPC2_VSX)

static void gen_xxbrd(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    tcg_gen_bswap64_i64(xth, xbh);
    tcg_gen_bswap64_i64(xtl, xbl);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_xxbrh(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    gen_bswap16x8(xth, xtl, xbh, xbl);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_xxbrq(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;
    TCGv_i64 t0;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);
    t0 = tcg_temp_new_i64();

    tcg_gen_bswap64_i64(t0, xbl);
    tcg_gen_bswap64_i64(xtl, xbh);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
    tcg_gen_mov_i64(xth, t0);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
}

static void gen_xxbrw(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    gen_bswap32x4(xth, xtl, xbh, xbl);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

#define VSX_LOGICAL(name, vece, tcg_op)                              \
static void glue(gen_, name)(DisasContext *ctx)                      \
    {                                                                \
        if (unlikely(!ctx->vsx_enabled)) {                           \
            gen_exception(ctx, POWERPC_EXCP_VSXU);                   \
            return;                                                  \
        }                                                            \
        tcg_op(vece, vsr_full_offset(xT(ctx->opcode)),               \
               vsr_full_offset(xA(ctx->opcode)),                     \
               vsr_full_offset(xB(ctx->opcode)), 16, 16);            \
    }

VSX_LOGICAL(xxland, MO_64, tcg_gen_gvec_and)
VSX_LOGICAL(xxlandc, MO_64, tcg_gen_gvec_andc)
VSX_LOGICAL(xxlor, MO_64, tcg_gen_gvec_or)
VSX_LOGICAL(xxlxor, MO_64, tcg_gen_gvec_xor)
VSX_LOGICAL(xxlnor, MO_64, tcg_gen_gvec_nor)
VSX_LOGICAL(xxleqv, MO_64, tcg_gen_gvec_eqv)
VSX_LOGICAL(xxlnand, MO_64, tcg_gen_gvec_nand)
VSX_LOGICAL(xxlorc, MO_64, tcg_gen_gvec_orc)

#define VSX_XXMRG(name, high)                               \
static void glue(gen_, name)(DisasContext *ctx)             \
    {                                                       \
        TCGv_i64 a0, a1, b0, b1, tmp;                       \
        if (unlikely(!ctx->vsx_enabled)) {                  \
            gen_exception(ctx, POWERPC_EXCP_VSXU);          \
            return;                                         \
        }                                                   \
        a0 = tcg_temp_new_i64();                            \
        a1 = tcg_temp_new_i64();                            \
        b0 = tcg_temp_new_i64();                            \
        b1 = tcg_temp_new_i64();                            \
        tmp = tcg_temp_new_i64();                           \
        get_cpu_vsr(a0, xA(ctx->opcode), high);             \
        get_cpu_vsr(a1, xA(ctx->opcode), high);             \
        get_cpu_vsr(b0, xB(ctx->opcode), high);             \
        get_cpu_vsr(b1, xB(ctx->opcode), high);             \
        tcg_gen_shri_i64(a0, a0, 32);                       \
        tcg_gen_shri_i64(b0, b0, 32);                       \
        tcg_gen_deposit_i64(tmp, b0, a0, 32, 32);           \
        set_cpu_vsr(xT(ctx->opcode), tmp, true);            \
        tcg_gen_deposit_i64(tmp, b1, a1, 32, 32);           \
        set_cpu_vsr(xT(ctx->opcode), tmp, false);           \
    }

VSX_XXMRG(xxmrghw, 1)
VSX_XXMRG(xxmrglw, 0)

static bool trans_XXSEL(DisasContext *ctx, arg_XX4 *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, VSX);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_bitsel(MO_64, vsr_full_offset(a->xt), vsr_full_offset(a->xc),
                        vsr_full_offset(a->xb), vsr_full_offset(a->xa), 16, 16);

    return true;
}

static bool trans_XXSPLTW(DisasContext *ctx, arg_XX2_uim *a)
{
    int tofs, bofs;

    REQUIRE_VSX(ctx);

    tofs = vsr_full_offset(a->xt);
    bofs = vsr_full_offset(a->xb);
    bofs += a->uim << MO_32;
#if !HOST_BIG_ENDIAN
    bofs ^= 8 | 4;
#endif

    tcg_gen_gvec_dup_mem(MO_32, tofs, bofs, 16, 16);
    return true;
}

#define pattern(x) (((x) & 0xff) * (~(uint64_t)0 / 0xff))

static bool trans_XXSPLTIB(DisasContext *ctx, arg_X_imm8 *a)
{
    if (a->xt < 32) {
        REQUIRE_VSX(ctx);
    } else {
        REQUIRE_VECTOR(ctx);
    }
    tcg_gen_gvec_dup_imm(MO_8, vsr_full_offset(a->xt), 16, 16, a->imm);
    return true;
}

static bool trans_XXSPLTIW(DisasContext *ctx, arg_8RR_D *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_dup_imm(MO_32, vsr_full_offset(a->xt), 16, 16, a->si);

    return true;
}

static bool trans_XXSPLTIDP(DisasContext *ctx, arg_8RR_D *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_dup_imm(MO_64, vsr_full_offset(a->xt), 16, 16,
                         helper_todouble(a->si));
    return true;
}

static bool trans_XXSPLTI32DX(DisasContext *ctx, arg_8RR_D_IX *a)
{
    TCGv_i32 imm;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    imm = tcg_constant_i32(a->si);

    tcg_gen_st_i32(imm, cpu_env,
        offsetof(CPUPPCState, vsr[a->xt].VsrW(0 + a->ix)));
    tcg_gen_st_i32(imm, cpu_env,
        offsetof(CPUPPCState, vsr[a->xt].VsrW(2 + a->ix)));

    return true;
}

static bool trans_LXVKQ(DisasContext *ctx, arg_X_uim5 *a)
{
    static const uint64_t values[32] = {
        0, /* Unspecified */
        0x3FFF000000000000llu, /* QP +1.0 */
        0x4000000000000000llu, /* QP +2.0 */
        0x4000800000000000llu, /* QP +3.0 */
        0x4001000000000000llu, /* QP +4.0 */
        0x4001400000000000llu, /* QP +5.0 */
        0x4001800000000000llu, /* QP +6.0 */
        0x4001C00000000000llu, /* QP +7.0 */
        0x7FFF000000000000llu, /* QP +Inf */
        0x7FFF800000000000llu, /* QP dQNaN */
        0, /* Unspecified */
        0, /* Unspecified */
        0, /* Unspecified */
        0, /* Unspecified */
        0, /* Unspecified */
        0, /* Unspecified */
        0x8000000000000000llu, /* QP -0.0 */
        0xBFFF000000000000llu, /* QP -1.0 */
        0xC000000000000000llu, /* QP -2.0 */
        0xC000800000000000llu, /* QP -3.0 */
        0xC001000000000000llu, /* QP -4.0 */
        0xC001400000000000llu, /* QP -5.0 */
        0xC001800000000000llu, /* QP -6.0 */
        0xC001C00000000000llu, /* QP -7.0 */
        0xFFFF000000000000llu, /* QP -Inf */
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    if (values[a->uim]) {
        set_cpu_vsr(a->xt, tcg_constant_i64(0x0), false);
        set_cpu_vsr(a->xt, tcg_constant_i64(values[a->uim]), true);
    } else {
        gen_invalid(ctx);
    }

    return true;
}

static bool trans_XVTLSBB(DisasContext *ctx, arg_XX2_bf_xb *a)
{
    TCGv_i64 xb, t0, t1, all_true, all_false, mask, zero;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    xb = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    all_true = tcg_temp_new_i64();
    all_false = tcg_temp_new_i64();
    mask = tcg_constant_i64(dup_const(MO_8, 1));
    zero = tcg_constant_i64(0);

    get_cpu_vsr(xb, a->xb, true);
    tcg_gen_and_i64(t0, mask, xb);
    get_cpu_vsr(xb, a->xb, false);
    tcg_gen_and_i64(t1, mask, xb);

    tcg_gen_or_i64(all_false, t0, t1);
    tcg_gen_and_i64(all_true, t0, t1);

    tcg_gen_setcond_i64(TCG_COND_EQ, all_false, all_false, zero);
    tcg_gen_shli_i64(all_false, all_false, 1);
    tcg_gen_setcond_i64(TCG_COND_EQ, all_true, all_true, mask);
    tcg_gen_shli_i64(all_true, all_true, 3);

    tcg_gen_or_i64(t0, all_false, all_true);
    tcg_gen_extrl_i64_i32(cpu_crf[a->bf], t0);
    return true;
}

static void gen_xxsldwi(DisasContext *ctx)
{
    TCGv_i64 xth, xtl;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();

    switch (SHW(ctx->opcode)) {
        case 0: {
            get_cpu_vsr(xth, xA(ctx->opcode), true);
            get_cpu_vsr(xtl, xA(ctx->opcode), false);
            break;
        }
        case 1: {
            TCGv_i64 t0 = tcg_temp_new_i64();
            get_cpu_vsr(xth, xA(ctx->opcode), true);
            tcg_gen_shli_i64(xth, xth, 32);
            get_cpu_vsr(t0, xA(ctx->opcode), false);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xth, xth, t0);
            get_cpu_vsr(xtl, xA(ctx->opcode), false);
            tcg_gen_shli_i64(xtl, xtl, 32);
            get_cpu_vsr(t0, xB(ctx->opcode), true);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xtl, xtl, t0);
            break;
        }
        case 2: {
            get_cpu_vsr(xth, xA(ctx->opcode), false);
            get_cpu_vsr(xtl, xB(ctx->opcode), true);
            break;
        }
        case 3: {
            TCGv_i64 t0 = tcg_temp_new_i64();
            get_cpu_vsr(xth, xA(ctx->opcode), false);
            tcg_gen_shli_i64(xth, xth, 32);
            get_cpu_vsr(t0, xB(ctx->opcode), true);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xth, xth, t0);
            get_cpu_vsr(xtl, xB(ctx->opcode), true);
            tcg_gen_shli_i64(xtl, xtl, 32);
            get_cpu_vsr(t0, xB(ctx->opcode), false);
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xtl, xtl, t0);
            break;
        }
    }

    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static bool do_vsx_extract_insert(DisasContext *ctx, arg_XX2_uim *a,
    void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i32))
{
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_ptr xt, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    /*
     * uim > 15 out of bound and for
     * uim > 12 handle as per hardware in helper
     */
    if (a->uim > 15) {
        set_cpu_vsr(a->xt, zero, true);
        set_cpu_vsr(a->xt, zero, false);
    } else {
        xt = gen_vsr_ptr(a->xt);
        xb = gen_vsr_ptr(a->xb);
        gen_helper(xt, xb, tcg_constant_i32(a->uim));
    }
    return true;
}

TRANS(XXEXTRACTUW, do_vsx_extract_insert, gen_helper_XXEXTRACTUW)
TRANS(XXINSERTW, do_vsx_extract_insert, gen_helper_XXINSERTW)

#ifdef TARGET_PPC64
static void gen_xsxexpdp(DisasContext *ctx)
{
    TCGv rt = cpu_gpr[rD(ctx->opcode)];
    TCGv_i64 t0;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    t0 = tcg_temp_new_i64();
    get_cpu_vsr(t0, xB(ctx->opcode), true);
    tcg_gen_extract_i64(rt, t0, 52, 11);
}

static void gen_xsxexpqp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    get_cpu_vsr(xbh, rB(ctx->opcode) + 32, true);

    tcg_gen_extract_i64(xth, xbh, 48, 15);
    set_cpu_vsr(rD(ctx->opcode) + 32, xth, true);
    tcg_gen_movi_i64(xtl, 0);
    set_cpu_vsr(rD(ctx->opcode) + 32, xtl, false);
}

static void gen_xsiexpdp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv ra = cpu_gpr[rA(ctx->opcode)];
    TCGv rb = cpu_gpr[rB(ctx->opcode)];
    TCGv_i64 t0;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    t0 = tcg_temp_new_i64();
    xth = tcg_temp_new_i64();
    tcg_gen_andi_i64(xth, ra, 0x800FFFFFFFFFFFFF);
    tcg_gen_andi_i64(t0, rb, 0x7FF);
    tcg_gen_shli_i64(t0, t0, 52);
    tcg_gen_or_i64(xth, xth, t0);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), tcg_constant_i64(0), false);
}

static void gen_xsiexpqp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xah;
    TCGv_i64 xal;
    TCGv_i64 xbh;
    TCGv_i64 t0;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xah = tcg_temp_new_i64();
    xal = tcg_temp_new_i64();
    get_cpu_vsr(xah, rA(ctx->opcode) + 32, true);
    get_cpu_vsr(xal, rA(ctx->opcode) + 32, false);
    xbh = tcg_temp_new_i64();
    get_cpu_vsr(xbh, rB(ctx->opcode) + 32, true);
    t0 = tcg_temp_new_i64();

    tcg_gen_andi_i64(xth, xah, 0x8000FFFFFFFFFFFF);
    tcg_gen_andi_i64(t0, xbh, 0x7FFF);
    tcg_gen_shli_i64(t0, t0, 48);
    tcg_gen_or_i64(xth, xth, t0);
    set_cpu_vsr(rD(ctx->opcode) + 32, xth, true);
    tcg_gen_mov_i64(xtl, xal);
    set_cpu_vsr(rD(ctx->opcode) + 32, xtl, false);
}

static void gen_xsxsigdp(DisasContext *ctx)
{
    TCGv rt = cpu_gpr[rD(ctx->opcode)];
    TCGv_i64 t0, t1, zr, nan, exp;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    exp = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    zr = tcg_const_i64(0);
    nan = tcg_const_i64(2047);

    get_cpu_vsr(t1, xB(ctx->opcode), true);
    tcg_gen_extract_i64(exp, t1, 52, 11);
    tcg_gen_movi_i64(t0, 0x0010000000000000);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, zr, zr, t0);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, nan, zr, t0);
    get_cpu_vsr(t1, xB(ctx->opcode), true);
    tcg_gen_deposit_i64(rt, t0, t1, 0, 52);
}

static void gen_xsxsigqp(DisasContext *ctx)
{
    TCGv_i64 t0, zr, nan, exp;
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, rB(ctx->opcode) + 32, true);
    get_cpu_vsr(xbl, rB(ctx->opcode) + 32, false);
    exp = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    zr = tcg_const_i64(0);
    nan = tcg_const_i64(32767);

    tcg_gen_extract_i64(exp, xbh, 48, 15);
    tcg_gen_movi_i64(t0, 0x0001000000000000);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, zr, zr, t0);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, nan, zr, t0);
    tcg_gen_deposit_i64(xth, t0, xbh, 0, 48);
    set_cpu_vsr(rD(ctx->opcode) + 32, xth, true);
    tcg_gen_mov_i64(xtl, xbl);
    set_cpu_vsr(rD(ctx->opcode) + 32, xtl, false);
}
#endif

static void gen_xviexpsp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xah;
    TCGv_i64 xal;
    TCGv_i64 xbh;
    TCGv_i64 xbl;
    TCGv_i64 t0;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xah = tcg_temp_new_i64();
    xal = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xah, xA(ctx->opcode), true);
    get_cpu_vsr(xal, xA(ctx->opcode), false);
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);
    t0 = tcg_temp_new_i64();

    tcg_gen_andi_i64(xth, xah, 0x807FFFFF807FFFFF);
    tcg_gen_andi_i64(t0, xbh, 0xFF000000FF);
    tcg_gen_shli_i64(t0, t0, 23);
    tcg_gen_or_i64(xth, xth, t0);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    tcg_gen_andi_i64(xtl, xal, 0x807FFFFF807FFFFF);
    tcg_gen_andi_i64(t0, xbl, 0xFF000000FF);
    tcg_gen_shli_i64(t0, t0, 23);
    tcg_gen_or_i64(xtl, xtl, t0);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_xviexpdp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xah;
    TCGv_i64 xal;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xah = tcg_temp_new_i64();
    xal = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xah, xA(ctx->opcode), true);
    get_cpu_vsr(xal, xA(ctx->opcode), false);
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    tcg_gen_deposit_i64(xth, xah, xbh, 52, 11);
    set_cpu_vsr(xT(ctx->opcode), xth, true);

    tcg_gen_deposit_i64(xtl, xal, xbl, 52, 11);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_xvxexpsp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    tcg_gen_shri_i64(xth, xbh, 23);
    tcg_gen_andi_i64(xth, xth, 0xFF000000FF);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    tcg_gen_shri_i64(xtl, xbl, 23);
    tcg_gen_andi_i64(xtl, xtl, 0xFF000000FF);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static void gen_xvxexpdp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);

    tcg_gen_extract_i64(xth, xbh, 52, 11);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    tcg_gen_extract_i64(xtl, xbl, 52, 11);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static bool trans_XVXSIGSP(DisasContext *ctx, arg_XX2 *a)
{
    TCGv_ptr t, b;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    t = gen_vsr_ptr(a->xt);
    b = gen_vsr_ptr(a->xb);

    gen_helper_XVXSIGSP(t, b);
    return true;
}

static void gen_xvxsigdp(DisasContext *ctx)
{
    TCGv_i64 xth;
    TCGv_i64 xtl;
    TCGv_i64 xbh;
    TCGv_i64 xbl;
    TCGv_i64 t0, zr, nan, exp;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    xth = tcg_temp_new_i64();
    xtl = tcg_temp_new_i64();
    xbh = tcg_temp_new_i64();
    xbl = tcg_temp_new_i64();
    get_cpu_vsr(xbh, xB(ctx->opcode), true);
    get_cpu_vsr(xbl, xB(ctx->opcode), false);
    exp = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    zr = tcg_const_i64(0);
    nan = tcg_const_i64(2047);

    tcg_gen_extract_i64(exp, xbh, 52, 11);
    tcg_gen_movi_i64(t0, 0x0010000000000000);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, zr, zr, t0);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, nan, zr, t0);
    tcg_gen_deposit_i64(xth, t0, xbh, 0, 52);
    set_cpu_vsr(xT(ctx->opcode), xth, true);

    tcg_gen_extract_i64(exp, xbl, 52, 11);
    tcg_gen_movi_i64(t0, 0x0010000000000000);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, zr, zr, t0);
    tcg_gen_movcond_i64(TCG_COND_EQ, t0, exp, nan, zr, t0);
    tcg_gen_deposit_i64(xtl, t0, xbl, 0, 52);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
}

static bool do_lstxv(DisasContext *ctx, int ra, TCGv displ,
                     int rt, bool store, bool paired)
{
    TCGv ea;
    TCGv_i64 xt;
    MemOp mop;
    int rt1, rt2;

    xt = tcg_temp_new_i64();

    mop = DEF_MEMOP(MO_UQ);

    gen_set_access_type(ctx, ACCESS_INT);
    ea = do_ea_calc(ctx, ra, displ);

    if (paired && ctx->le_mode) {
        rt1 = rt + 1;
        rt2 = rt;
    } else {
        rt1 = rt;
        rt2 = rt + 1;
    }

    if (store) {
        get_cpu_vsr(xt, rt1, !ctx->le_mode);
        tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
        gen_addr_add(ctx, ea, ea, 8);
        get_cpu_vsr(xt, rt1, ctx->le_mode);
        tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
        if (paired) {
            gen_addr_add(ctx, ea, ea, 8);
            get_cpu_vsr(xt, rt2, !ctx->le_mode);
            tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
            gen_addr_add(ctx, ea, ea, 8);
            get_cpu_vsr(xt, rt2, ctx->le_mode);
            tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
        }
    } else {
        tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
        set_cpu_vsr(rt1, xt, !ctx->le_mode);
        gen_addr_add(ctx, ea, ea, 8);
        tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
        set_cpu_vsr(rt1, xt, ctx->le_mode);
        if (paired) {
            gen_addr_add(ctx, ea, ea, 8);
            tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
            set_cpu_vsr(rt2, xt, !ctx->le_mode);
            gen_addr_add(ctx, ea, ea, 8);
            tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
            set_cpu_vsr(rt2, xt, ctx->le_mode);
        }
    }
    return true;
}

static bool do_lstxv_D(DisasContext *ctx, arg_D *a, bool store, bool paired)
{
    if (paired || a->rt >= 32) {
        REQUIRE_VSX(ctx);
    } else {
        REQUIRE_VECTOR(ctx);
    }

    return do_lstxv(ctx, a->ra, tcg_constant_tl(a->si), a->rt, store, paired);
}

static bool do_lstxv_PLS_D(DisasContext *ctx, arg_PLS_D *a,
                           bool store, bool paired)
{
    arg_D d;
    REQUIRE_VSX(ctx);

    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }

    return do_lstxv(ctx, d.ra, tcg_constant_tl(d.si), d.rt, store, paired);
}

static bool do_lstxv_X(DisasContext *ctx, arg_X *a, bool store, bool paired)
{
    if (paired || a->rt >= 32) {
        REQUIRE_VSX(ctx);
    } else {
        REQUIRE_VECTOR(ctx);
    }

    return do_lstxv(ctx, a->ra, cpu_gpr[a->rb], a->rt, store, paired);
}

static bool do_lstxsd(DisasContext *ctx, int rt, int ra, TCGv displ, bool store)
{
    TCGv ea;
    TCGv_i64 xt;
    MemOp mop;

    if (store) {
        REQUIRE_VECTOR(ctx);
    } else {
        REQUIRE_VSX(ctx);
    }

    xt = tcg_temp_new_i64();
    mop = DEF_MEMOP(MO_UQ);

    gen_set_access_type(ctx, ACCESS_INT);
    ea = do_ea_calc(ctx, ra, displ);

    if (store) {
        get_cpu_vsr(xt, rt + 32, true);
        tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
        set_cpu_vsr(rt + 32, xt, true);
        set_cpu_vsr(rt + 32, tcg_constant_i64(0), false);
    }
    return true;
}

static bool do_lstxsd_DS(DisasContext *ctx, arg_D *a, bool store)
{
    return do_lstxsd(ctx, a->rt, a->ra, tcg_constant_tl(a->si), store);
}

static bool do_plstxsd_PLS_D(DisasContext *ctx, arg_PLS_D *a, bool store)
{
    arg_D d;

    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }

    return do_lstxsd(ctx, d.rt, d.ra, tcg_constant_tl(d.si), store);
}

static bool do_lstxssp(DisasContext *ctx, int rt, int ra, TCGv displ, bool store)
{
    TCGv ea;
    TCGv_i64 xt;

    REQUIRE_VECTOR(ctx);

    xt = tcg_temp_new_i64();

    gen_set_access_type(ctx, ACCESS_INT);
    ea = do_ea_calc(ctx, ra, displ);

    if (store) {
        get_cpu_vsr(xt, rt + 32, true);
        gen_qemu_st32fs(ctx, xt, ea);
    } else {
        gen_qemu_ld32fs(ctx, xt, ea);
        set_cpu_vsr(rt + 32, xt, true);
        set_cpu_vsr(rt + 32, tcg_constant_i64(0), false);
    }
    return true;
}

static bool do_lstxssp_DS(DisasContext *ctx, arg_D *a, bool store)
{
    return do_lstxssp(ctx, a->rt, a->ra, tcg_constant_tl(a->si), store);
}

static bool do_plstxssp_PLS_D(DisasContext *ctx, arg_PLS_D *a, bool store)
{
    arg_D d;

    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }

    return do_lstxssp(ctx, d.rt, d.ra, tcg_constant_tl(d.si), store);
}

TRANS_FLAGS2(ISA300, LXSD, do_lstxsd_DS, false)
TRANS_FLAGS2(ISA300, STXSD, do_lstxsd_DS, true)
TRANS_FLAGS2(ISA300, LXSSP, do_lstxssp_DS, false)
TRANS_FLAGS2(ISA300, STXSSP, do_lstxssp_DS, true)
TRANS_FLAGS2(ISA300, STXV, do_lstxv_D, true, false)
TRANS_FLAGS2(ISA300, LXV, do_lstxv_D, false, false)
TRANS_FLAGS2(ISA310, STXVP, do_lstxv_D, true, true)
TRANS_FLAGS2(ISA310, LXVP, do_lstxv_D, false, true)
TRANS_FLAGS2(ISA300, STXVX, do_lstxv_X, true, false)
TRANS_FLAGS2(ISA300, LXVX, do_lstxv_X, false, false)
TRANS_FLAGS2(ISA310, STXVPX, do_lstxv_X, true, true)
TRANS_FLAGS2(ISA310, LXVPX, do_lstxv_X, false, true)
TRANS64_FLAGS2(ISA310, PLXSD, do_plstxsd_PLS_D, false)
TRANS64_FLAGS2(ISA310, PSTXSD, do_plstxsd_PLS_D, true)
TRANS64_FLAGS2(ISA310, PLXSSP, do_plstxssp_PLS_D, false)
TRANS64_FLAGS2(ISA310, PSTXSSP, do_plstxssp_PLS_D, true)
TRANS64_FLAGS2(ISA310, PSTXV, do_lstxv_PLS_D, true, false)
TRANS64_FLAGS2(ISA310, PLXV, do_lstxv_PLS_D, false, false)
TRANS64_FLAGS2(ISA310, PSTXVP, do_lstxv_PLS_D, true, true)
TRANS64_FLAGS2(ISA310, PLXVP, do_lstxv_PLS_D, false, true)

static bool do_lstrm(DisasContext *ctx, arg_X *a, MemOp mop, bool store)
{
    TCGv ea;
    TCGv_i64 xt;

    REQUIRE_VSX(ctx);

    xt = tcg_temp_new_i64();

    gen_set_access_type(ctx, ACCESS_INT);
    ea = do_ea_calc(ctx, a->ra , cpu_gpr[a->rb]);

    if (store) {
        get_cpu_vsr(xt, a->rt, false);
        tcg_gen_qemu_st_i64(xt, ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_i64(xt, ea, ctx->mem_idx, mop);
        set_cpu_vsr(a->rt, xt, false);
        set_cpu_vsr(a->rt, tcg_constant_i64(0), true);
    }
    return true;
}

TRANS_FLAGS2(ISA310, LXVRBX, do_lstrm, DEF_MEMOP(MO_UB), false)
TRANS_FLAGS2(ISA310, LXVRHX, do_lstrm, DEF_MEMOP(MO_UW), false)
TRANS_FLAGS2(ISA310, LXVRWX, do_lstrm, DEF_MEMOP(MO_UL), false)
TRANS_FLAGS2(ISA310, LXVRDX, do_lstrm, DEF_MEMOP(MO_UQ), false)
TRANS_FLAGS2(ISA310, STXVRBX, do_lstrm, DEF_MEMOP(MO_UB), true)
TRANS_FLAGS2(ISA310, STXVRHX, do_lstrm, DEF_MEMOP(MO_UW), true)
TRANS_FLAGS2(ISA310, STXVRWX, do_lstrm, DEF_MEMOP(MO_UL), true)
TRANS_FLAGS2(ISA310, STXVRDX, do_lstrm, DEF_MEMOP(MO_UQ), true)

static void gen_xxeval_i64(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b, TCGv_i64 c,
                           int64_t imm)
{
    /*
     * Instead of processing imm bit-by-bit, we'll skip the computation of
     * conjunctions whose corresponding bit is unset.
     */
    int bit;
    TCGv_i64 conj, disj;

    conj = tcg_temp_new_i64();
    disj = tcg_const_i64(0);

    /* Iterate over set bits from the least to the most significant bit */
    while (imm) {
        /*
         * Get the next bit to be processed with ctz64. Invert the result of
         * ctz64 to match the indexing used by PowerISA.
         */
        bit = 7 - ctz64(imm);
        if (bit & 0x4) {
            tcg_gen_mov_i64(conj, a);
        } else {
            tcg_gen_not_i64(conj, a);
        }
        if (bit & 0x2) {
            tcg_gen_and_i64(conj, conj, b);
        } else {
            tcg_gen_andc_i64(conj, conj, b);
        }
        if (bit & 0x1) {
            tcg_gen_and_i64(conj, conj, c);
        } else {
            tcg_gen_andc_i64(conj, conj, c);
        }
        tcg_gen_or_i64(disj, disj, conj);

        /* Unset the least significant bit that is set */
        imm &= imm - 1;
    }

    tcg_gen_mov_i64(t, disj);
}

static void gen_xxeval_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                           TCGv_vec c, int64_t imm)
{
    /*
     * Instead of processing imm bit-by-bit, we'll skip the computation of
     * conjunctions whose corresponding bit is unset.
     */
    int bit;
    TCGv_vec disj, conj;

    disj = tcg_const_zeros_vec_matching(t);
    conj = tcg_temp_new_vec_matching(t);

    /* Iterate over set bits from the least to the most significant bit */
    while (imm) {
        /*
         * Get the next bit to be processed with ctz64. Invert the result of
         * ctz64 to match the indexing used by PowerISA.
         */
        bit = 7 - ctz64(imm);
        if (bit & 0x4) {
            tcg_gen_mov_vec(conj, a);
        } else {
            tcg_gen_not_vec(vece, conj, a);
        }
        if (bit & 0x2) {
            tcg_gen_and_vec(vece, conj, conj, b);
        } else {
            tcg_gen_andc_vec(vece, conj, conj, b);
        }
        if (bit & 0x1) {
            tcg_gen_and_vec(vece, conj, conj, c);
        } else {
            tcg_gen_andc_vec(vece, conj, conj, c);
        }
        tcg_gen_or_vec(vece, disj, disj, conj);

        /* Unset the least significant bit that is set */
        imm &= imm - 1;
    }

    tcg_gen_mov_vec(t, disj);
}

static bool trans_XXEVAL(DisasContext *ctx, arg_8RR_XX4_imm *a)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_andc_vec, 0
    };
    static const GVecGen4i op = {
        .fniv = gen_xxeval_vec,
        .fno = gen_helper_XXEVAL,
        .fni8 = gen_xxeval_i64,
        .opt_opc = vecop_list,
        .vece = MO_64
    };
    int xt = vsr_full_offset(a->xt), xa = vsr_full_offset(a->xa),
        xb = vsr_full_offset(a->xb), xc = vsr_full_offset(a->xc);

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    /* Equivalent functions that can be implemented with a single gen_gvec */
    switch (a->imm) {
    case 0b00000000: /* true */
        set_cpu_vsr(a->xt, tcg_constant_i64(0), true);
        set_cpu_vsr(a->xt, tcg_constant_i64(0), false);
        break;
    case 0b00000011: /* and(B,A) */
        tcg_gen_gvec_and(MO_64, xt, xb, xa, 16, 16);
        break;
    case 0b00000101: /* and(C,A) */
        tcg_gen_gvec_and(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b00001111: /* A */
        tcg_gen_gvec_mov(MO_64, xt, xa, 16, 16);
        break;
    case 0b00010001: /* and(C,B) */
        tcg_gen_gvec_and(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b00011011: /* C?B:A */
        tcg_gen_gvec_bitsel(MO_64, xt, xc, xb, xa, 16, 16);
        break;
    case 0b00011101: /* B?C:A */
        tcg_gen_gvec_bitsel(MO_64, xt, xb, xc, xa, 16, 16);
        break;
    case 0b00100111: /* C?A:B */
        tcg_gen_gvec_bitsel(MO_64, xt, xc, xa, xb, 16, 16);
        break;
    case 0b00110011: /* B */
        tcg_gen_gvec_mov(MO_64, xt, xb, 16, 16);
        break;
    case 0b00110101: /* A?C:B */
        tcg_gen_gvec_bitsel(MO_64, xt, xa, xc, xb, 16, 16);
        break;
    case 0b00111100: /* xor(B,A) */
        tcg_gen_gvec_xor(MO_64, xt, xb, xa, 16, 16);
        break;
    case 0b00111111: /* or(B,A) */
        tcg_gen_gvec_or(MO_64, xt, xb, xa, 16, 16);
        break;
    case 0b01000111: /* B?A:C */
        tcg_gen_gvec_bitsel(MO_64, xt, xb, xa, xc, 16, 16);
        break;
    case 0b01010011: /* A?B:C */
        tcg_gen_gvec_bitsel(MO_64, xt, xa, xb, xc, 16, 16);
        break;
    case 0b01010101: /* C */
        tcg_gen_gvec_mov(MO_64, xt, xc, 16, 16);
        break;
    case 0b01011010: /* xor(C,A) */
        tcg_gen_gvec_xor(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b01011111: /* or(C,A) */
        tcg_gen_gvec_or(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b01100110: /* xor(C,B) */
        tcg_gen_gvec_xor(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b01110111: /* or(C,B) */
        tcg_gen_gvec_or(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b10001000: /* nor(C,B) */
        tcg_gen_gvec_nor(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b10011001: /* eqv(C,B) */
        tcg_gen_gvec_eqv(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b10100000: /* nor(C,A) */
        tcg_gen_gvec_nor(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b10100101: /* eqv(C,A) */
        tcg_gen_gvec_eqv(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b10101010: /* not(C) */
        tcg_gen_gvec_not(MO_64, xt, xc, 16, 16);
        break;
    case 0b11000000: /* nor(B,A) */
        tcg_gen_gvec_nor(MO_64, xt,  xb, xa, 16, 16);
        break;
    case 0b11000011: /* eqv(B,A) */
        tcg_gen_gvec_eqv(MO_64, xt,  xb, xa, 16, 16);
        break;
    case 0b11001100: /* not(B) */
        tcg_gen_gvec_not(MO_64, xt, xb, 16, 16);
        break;
    case 0b11101110: /* nand(C,B) */
        tcg_gen_gvec_nand(MO_64, xt, xc, xb, 16, 16);
        break;
    case 0b11110000: /* not(A) */
        tcg_gen_gvec_not(MO_64, xt, xa, 16, 16);
        break;
    case 0b11111010: /* nand(C,A) */
        tcg_gen_gvec_nand(MO_64, xt, xc, xa, 16, 16);
        break;
    case 0b11111100: /* nand(B,A) */
        tcg_gen_gvec_nand(MO_64, xt, xb, xa, 16, 16);
        break;
    case 0b11111111: /* true */
        set_cpu_vsr(a->xt, tcg_constant_i64(-1), true);
        set_cpu_vsr(a->xt, tcg_constant_i64(-1), false);
        break;
    default:
        /* Fallback to compute all conjunctions/disjunctions */
        tcg_gen_gvec_4i(xt, xa, xb, xc, 16, 16, a->imm, &op);
    }

    return true;
}

static void gen_xxblendv_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                             TCGv_vec c)
{
    TCGv_vec tmp = tcg_temp_new_vec_matching(c);
    tcg_gen_sari_vec(vece, tmp, c, (8 << vece) - 1);
    tcg_gen_bitsel_vec(vece, t, tmp, b, a);
}

static bool do_xxblendv(DisasContext *ctx, arg_8RR_XX4 *a, unsigned vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_sari_vec, 0
    };
    static const GVecGen4 ops[4] = {
        {
            .fniv = gen_xxblendv_vec,
            .fno = gen_helper_XXBLENDVB,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_xxblendv_vec,
            .fno = gen_helper_XXBLENDVH,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_xxblendv_vec,
            .fno = gen_helper_XXBLENDVW,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_xxblendv_vec,
            .fno = gen_helper_XXBLENDVD,
            .opt_opc = vecop_list,
            .vece = MO_64
        }
    };

    REQUIRE_VSX(ctx);

    tcg_gen_gvec_4(vsr_full_offset(a->xt), vsr_full_offset(a->xa),
                   vsr_full_offset(a->xb), vsr_full_offset(a->xc),
                   16, 16, &ops[vece]);

    return true;
}

TRANS(XXBLENDVB, do_xxblendv, MO_8)
TRANS(XXBLENDVH, do_xxblendv, MO_16)
TRANS(XXBLENDVW, do_xxblendv, MO_32)
TRANS(XXBLENDVD, do_xxblendv, MO_64)

static bool do_helper_XX3(DisasContext *ctx, arg_XX3 *a,
    void (*helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr xt, xa, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    helper(cpu_env, xt, xa, xb);
    return true;
}

TRANS(XSCMPEQDP, do_helper_XX3, gen_helper_XSCMPEQDP)
TRANS(XSCMPGEDP, do_helper_XX3, gen_helper_XSCMPGEDP)
TRANS(XSCMPGTDP, do_helper_XX3, gen_helper_XSCMPGTDP)
TRANS(XSMAXCDP, do_helper_XX3, gen_helper_XSMAXCDP)
TRANS(XSMINCDP, do_helper_XX3, gen_helper_XSMINCDP)
TRANS(XSMAXJDP, do_helper_XX3, gen_helper_XSMAXJDP)
TRANS(XSMINJDP, do_helper_XX3, gen_helper_XSMINJDP)

static bool do_helper_X(arg_X *a,
    void (*helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr rt, ra, rb;

    rt = gen_avr_ptr(a->rt);
    ra = gen_avr_ptr(a->ra);
    rb = gen_avr_ptr(a->rb);

    helper(cpu_env, rt, ra, rb);
    return true;
}

static bool do_xscmpqp(DisasContext *ctx, arg_X *a,
    void (*helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    return do_helper_X(a, helper);
}

TRANS(XSCMPEQQP, do_xscmpqp, gen_helper_XSCMPEQQP)
TRANS(XSCMPGEQP, do_xscmpqp, gen_helper_XSCMPGEQP)
TRANS(XSCMPGTQP, do_xscmpqp, gen_helper_XSCMPGTQP)
TRANS(XSMAXCQP, do_xscmpqp, gen_helper_XSMAXCQP)
TRANS(XSMINCQP, do_xscmpqp, gen_helper_XSMINCQP)

static bool trans_XVCVSPBF16(DisasContext *ctx, arg_XX2 *a)
{
    TCGv_ptr xt, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xb = gen_vsr_ptr(a->xb);

    gen_helper_XVCVSPBF16(cpu_env, xt, xb);
    return true;
}

static bool trans_XVCVBF16SPN(DisasContext *ctx, arg_XX2 *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_shli(MO_32, vsr_full_offset(a->xt), vsr_full_offset(a->xb),
                      16, 16, 16);

    return true;
}

    /*
     *  The PowerISA 3.1 mentions that for the current version of the
     *  architecture, "the hardware implementation provides the effect of
     *  ACC[i] and VSRs 4*i to 4*i + 3 logically containing the same data"
     *  and "The Accumulators introduce no new logical state at this time"
     *  (page 501). For now it seems unnecessary to create new structures,
     *  so ACC[i] is the same as VSRs 4*i to 4*i+3 and therefore
     *  move to and from accumulators are no-ops.
     */
static bool trans_XXMFACC(DisasContext *ctx, arg_X_a *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);
    return true;
}

static bool trans_XXMTACC(DisasContext *ctx, arg_X_a *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);
    return true;
}

static bool trans_XXSETACCZ(DisasContext *ctx, arg_X_a *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);
    tcg_gen_gvec_dup_imm(MO_64, acc_full_offset(a->ra), 64, 64, 0);
    return true;
}

static bool do_ger(DisasContext *ctx, arg_MMIRR_XX3 *a,
    void (*helper)(TCGv_env, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_i32))
{
    uint32_t mask;
    TCGv_ptr xt, xa, xb;
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);
    if (unlikely((a->xa / 4 == a->xt) || (a->xb / 4 == a->xt))) {
        gen_invalid(ctx);
        return true;
    }

    xt = gen_acc_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    mask = ger_pack_masks(a->pmsk, a->ymsk, a->xmsk);
    helper(cpu_env, xa, xb, xt, tcg_constant_i32(mask));
    return true;
}

TRANS(XVI4GER8, do_ger, gen_helper_XVI4GER8)
TRANS(XVI4GER8PP, do_ger,  gen_helper_XVI4GER8PP)
TRANS(XVI8GER4, do_ger, gen_helper_XVI8GER4)
TRANS(XVI8GER4PP, do_ger,  gen_helper_XVI8GER4PP)
TRANS(XVI8GER4SPP, do_ger, gen_helper_XVI8GER4SPP)
TRANS(XVI16GER2, do_ger, gen_helper_XVI16GER2)
TRANS(XVI16GER2PP, do_ger, gen_helper_XVI16GER2PP)
TRANS(XVI16GER2S, do_ger, gen_helper_XVI16GER2S)
TRANS(XVI16GER2SPP, do_ger, gen_helper_XVI16GER2SPP)

TRANS64(PMXVI4GER8, do_ger, gen_helper_XVI4GER8)
TRANS64(PMXVI4GER8PP, do_ger, gen_helper_XVI4GER8PP)
TRANS64(PMXVI8GER4, do_ger, gen_helper_XVI8GER4)
TRANS64(PMXVI8GER4PP, do_ger, gen_helper_XVI8GER4PP)
TRANS64(PMXVI8GER4SPP, do_ger, gen_helper_XVI8GER4SPP)
TRANS64(PMXVI16GER2, do_ger, gen_helper_XVI16GER2)
TRANS64(PMXVI16GER2PP, do_ger, gen_helper_XVI16GER2PP)
TRANS64(PMXVI16GER2S, do_ger, gen_helper_XVI16GER2S)
TRANS64(PMXVI16GER2SPP, do_ger, gen_helper_XVI16GER2SPP)

TRANS(XVBF16GER2, do_ger, gen_helper_XVBF16GER2)
TRANS(XVBF16GER2PP, do_ger, gen_helper_XVBF16GER2PP)
TRANS(XVBF16GER2PN, do_ger, gen_helper_XVBF16GER2PN)
TRANS(XVBF16GER2NP, do_ger, gen_helper_XVBF16GER2NP)
TRANS(XVBF16GER2NN, do_ger, gen_helper_XVBF16GER2NN)

TRANS(XVF16GER2, do_ger, gen_helper_XVF16GER2)
TRANS(XVF16GER2PP, do_ger, gen_helper_XVF16GER2PP)
TRANS(XVF16GER2PN, do_ger, gen_helper_XVF16GER2PN)
TRANS(XVF16GER2NP, do_ger, gen_helper_XVF16GER2NP)
TRANS(XVF16GER2NN, do_ger, gen_helper_XVF16GER2NN)

TRANS(XVF32GER, do_ger, gen_helper_XVF32GER)
TRANS(XVF32GERPP, do_ger, gen_helper_XVF32GERPP)
TRANS(XVF32GERPN, do_ger, gen_helper_XVF32GERPN)
TRANS(XVF32GERNP, do_ger, gen_helper_XVF32GERNP)
TRANS(XVF32GERNN, do_ger, gen_helper_XVF32GERNN)

TRANS(XVF64GER, do_ger, gen_helper_XVF64GER)
TRANS(XVF64GERPP, do_ger, gen_helper_XVF64GERPP)
TRANS(XVF64GERPN, do_ger, gen_helper_XVF64GERPN)
TRANS(XVF64GERNP, do_ger, gen_helper_XVF64GERNP)
TRANS(XVF64GERNN, do_ger, gen_helper_XVF64GERNN)

TRANS64(PMXVBF16GER2, do_ger, gen_helper_XVBF16GER2)
TRANS64(PMXVBF16GER2PP, do_ger, gen_helper_XVBF16GER2PP)
TRANS64(PMXVBF16GER2PN, do_ger, gen_helper_XVBF16GER2PN)
TRANS64(PMXVBF16GER2NP, do_ger, gen_helper_XVBF16GER2NP)
TRANS64(PMXVBF16GER2NN, do_ger, gen_helper_XVBF16GER2NN)

TRANS64(PMXVF16GER2, do_ger, gen_helper_XVF16GER2)
TRANS64(PMXVF16GER2PP, do_ger, gen_helper_XVF16GER2PP)
TRANS64(PMXVF16GER2PN, do_ger, gen_helper_XVF16GER2PN)
TRANS64(PMXVF16GER2NP, do_ger, gen_helper_XVF16GER2NP)
TRANS64(PMXVF16GER2NN, do_ger, gen_helper_XVF16GER2NN)

TRANS64(PMXVF32GER, do_ger, gen_helper_XVF32GER)
TRANS64(PMXVF32GERPP, do_ger, gen_helper_XVF32GERPP)
TRANS64(PMXVF32GERPN, do_ger, gen_helper_XVF32GERPN)
TRANS64(PMXVF32GERNP, do_ger, gen_helper_XVF32GERNP)
TRANS64(PMXVF32GERNN, do_ger, gen_helper_XVF32GERNN)

TRANS64(PMXVF64GER, do_ger, gen_helper_XVF64GER)
TRANS64(PMXVF64GERPP, do_ger, gen_helper_XVF64GERPP)
TRANS64(PMXVF64GERPN, do_ger, gen_helper_XVF64GERPN)
TRANS64(PMXVF64GERNP, do_ger, gen_helper_XVF64GERNP)
TRANS64(PMXVF64GERNN, do_ger, gen_helper_XVF64GERNN)

#undef GEN_XX2FORM
#undef GEN_XX3FORM
#undef GEN_XX2IFORM
#undef GEN_XX3_RC_FORM
#undef GEN_XX3FORM_DM
#undef VSX_LOGICAL
