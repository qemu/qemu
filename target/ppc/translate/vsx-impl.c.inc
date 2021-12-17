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
    tcg_temp_free(EA);                                        \
    tcg_temp_free_i64(t0);                                    \
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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

        tcg_gen_qemu_ld_i64(t0, EA, ctx->mem_idx, MO_LEQ);
        tcg_gen_shri_i64(t1, t0, 32);
        tcg_gen_deposit_i64(xth, t1, t0, 32, 32);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_ld_i64(t0, EA, ctx->mem_idx, MO_LEQ);
        tcg_gen_shri_i64(t1, t0, 32);
        tcg_gen_deposit_i64(xtl, t1, t0, 32, 32);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    } else {
        tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEQ);
    }
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
    tcg_temp_free(EA);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
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

    tcg_temp_free(EA);
    tcg_temp_free_i32(data);
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
    tcg_gen_qemu_ld_i64(data, EA, ctx->mem_idx, DEF_MEMOP(MO_Q));
    tcg_gen_gvec_dup_i64(MO_Q, vsr_full_offset(xT(ctx->opcode)), 16, 16, data);

    tcg_temp_free(EA);
    tcg_temp_free_i64(data);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(mask);
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

    tcg_temp_free_i64(hi);
    tcg_temp_free_i64(lo);
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
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEQ);
    if (ctx->le_mode) {
        gen_bswap16x8(xth, xtl, xth, xtl);
    }
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
    tcg_temp_free(EA);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
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
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEQ);
    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);
    tcg_temp_free(EA);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
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
    tcg_temp_free(EA);                                             \
    tcg_temp_free_ptr(xt);                                         \
}

VSX_VECTOR_LOAD_STORE_LENGTH(lxvl)
VSX_VECTOR_LOAD_STORE_LENGTH(lxvll)
VSX_VECTOR_LOAD_STORE_LENGTH(stxvl)
VSX_VECTOR_LOAD_STORE_LENGTH(stxvll)
#endif

#define VSX_LOAD_SCALAR_DS(name, operation)                       \
static void gen_##name(DisasContext *ctx)                         \
{                                                                 \
    TCGv EA;                                                      \
    TCGv_i64 xth;                                                 \
                                                                  \
    if (unlikely(!ctx->altivec_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VPU);                     \
        return;                                                   \
    }                                                             \
    xth = tcg_temp_new_i64();                                     \
    gen_set_access_type(ctx, ACCESS_INT);                         \
    EA = tcg_temp_new();                                          \
    gen_addr_imm_index(ctx, EA, 0x03);                            \
    gen_qemu_##operation(ctx, xth, EA);                           \
    set_cpu_vsr(rD(ctx->opcode) + 32, xth, true);                 \
    /* NOTE: cpu_vsrl is undefined */                             \
    tcg_temp_free(EA);                                            \
    tcg_temp_free_i64(xth);                                       \
}

VSX_LOAD_SCALAR_DS(lxsd, ld64_i64)
VSX_LOAD_SCALAR_DS(lxssp, ld32fs)

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
    tcg_temp_free(EA);                                        \
    tcg_temp_free_i64(t0);                                    \
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
    tcg_temp_free(EA);
    tcg_temp_free_i64(t0);
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
        tcg_gen_qemu_st_i64(t1, EA, ctx->mem_idx, MO_LEQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_shri_i64(t0, xsl, 32);
        tcg_gen_deposit_i64(t1, t0, xsl, 32, 32);
        tcg_gen_qemu_st_i64(t1, EA, ctx->mem_idx, MO_LEQ);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i64(t1);
    } else {
        tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEQ);
    }
    tcg_temp_free(EA);
    tcg_temp_free_i64(xsh);
    tcg_temp_free_i64(xsl);
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
        tcg_gen_qemu_st_i64(outh, EA, ctx->mem_idx, MO_BEQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(outl, EA, ctx->mem_idx, MO_BEQ);
        tcg_temp_free_i64(outh);
        tcg_temp_free_i64(outl);
    } else {
        tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEQ);
        tcg_gen_addi_tl(EA, EA, 8);
        tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEQ);
    }
    tcg_temp_free(EA);
    tcg_temp_free_i64(xsh);
    tcg_temp_free_i64(xsl);
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
    tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEQ);
    tcg_temp_free(EA);
    tcg_temp_free_i64(xsh);
    tcg_temp_free_i64(xsl);
}

#define VSX_STORE_SCALAR_DS(name, operation)                      \
static void gen_##name(DisasContext *ctx)                         \
{                                                                 \
    TCGv EA;                                                      \
    TCGv_i64 xth;                                                 \
                                                                  \
    if (unlikely(!ctx->altivec_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VPU);                     \
        return;                                                   \
    }                                                             \
    xth = tcg_temp_new_i64();                                     \
    get_cpu_vsr(xth, rD(ctx->opcode) + 32, true);                 \
    gen_set_access_type(ctx, ACCESS_INT);                         \
    EA = tcg_temp_new();                                          \
    gen_addr_imm_index(ctx, EA, 0x03);                            \
    gen_qemu_##operation(ctx, xth, EA);                           \
    /* NOTE: cpu_vsrl is undefined */                             \
    tcg_temp_free(EA);                                            \
    tcg_temp_free_i64(xth);                                       \
}

VSX_STORE_SCALAR_DS(stxsd, st64_i64)
VSX_STORE_SCALAR_DS(stxssp, st32fs)

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
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(xsh);
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
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(xsh);
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
    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(xsh);
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
    tcg_temp_free_i64(t0);
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
    tcg_temp_free_i64(t0);
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
    tcg_temp_free_i64(t0);
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
    tcg_temp_free_i64(t0);
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
    tcg_temp_free_i64(t0);
}

#endif

static void gen_xxpermdi(DisasContext *ctx)
{
    TCGv_i64 xh, xl;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }

    xh = tcg_temp_new_i64();
    xl = tcg_temp_new_i64();

    if (unlikely((xT(ctx->opcode) == xA(ctx->opcode)) ||
                 (xT(ctx->opcode) == xB(ctx->opcode)))) {
        get_cpu_vsr(xh, xA(ctx->opcode), (DM(ctx->opcode) & 2) == 0);
        get_cpu_vsr(xl, xB(ctx->opcode), (DM(ctx->opcode) & 1) == 0);

        set_cpu_vsr(xT(ctx->opcode), xh, true);
        set_cpu_vsr(xT(ctx->opcode), xl, false);
    } else {
        if ((DM(ctx->opcode) & 2) == 0) {
            get_cpu_vsr(xh, xA(ctx->opcode), true);
            set_cpu_vsr(xT(ctx->opcode), xh, true);
        } else {
            get_cpu_vsr(xh, xA(ctx->opcode), false);
            set_cpu_vsr(xT(ctx->opcode), xh, true);
        }
        if ((DM(ctx->opcode) & 1) == 0) {
            get_cpu_vsr(xl, xB(ctx->opcode), true);
            set_cpu_vsr(xT(ctx->opcode), xl, false);
        } else {
            get_cpu_vsr(xl, xB(ctx->opcode), false);
            set_cpu_vsr(xT(ctx->opcode), xl, false);
        }
    }
    tcg_temp_free_i64(xh);
    tcg_temp_free_i64(xl);
}

#define OP_ABS 1
#define OP_NABS 2
#define OP_NEG 3
#define OP_CPSGN 4
#define SGN_MASK_DP  0x8000000000000000ull
#define SGN_MASK_SP 0x8000000080000000ull

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
                tcg_temp_free_i64(xa);                            \
                break;                                            \
            }                                                     \
        }                                                         \
        set_cpu_vsr(xT(ctx->opcode), xb, true);                   \
        tcg_temp_free_i64(xb);                                    \
        tcg_temp_free_i64(sgm);                                   \
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
        tcg_temp_free_i64(xah);                                   \
        break;                                                    \
    }                                                             \
    set_cpu_vsr(xt, xbh, true);                                   \
    set_cpu_vsr(xt, xbl, false);                                  \
    tcg_temp_free_i64(xbl);                                       \
    tcg_temp_free_i64(xbh);                                       \
    tcg_temp_free_i64(sgm);                                       \
    tcg_temp_free_i64(tmp);                                       \
}

VSX_SCALAR_MOVE_QP(xsabsqp, OP_ABS, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xsnabsqp, OP_NABS, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xsnegqp, OP_NEG, SGN_MASK_DP)
VSX_SCALAR_MOVE_QP(xscpsgnqp, OP_CPSGN, SGN_MASK_DP)

#define VSX_VECTOR_MOVE(name, op, sgn_mask)                      \
static void glue(gen_, name)(DisasContext *ctx)                  \
    {                                                            \
        TCGv_i64 xbh, xbl, sgm;                                  \
        if (unlikely(!ctx->vsx_enabled)) {                       \
            gen_exception(ctx, POWERPC_EXCP_VSXU);               \
            return;                                              \
        }                                                        \
        xbh = tcg_temp_new_i64();                                \
        xbl = tcg_temp_new_i64();                                \
        sgm = tcg_temp_new_i64();                                \
        get_cpu_vsr(xbh, xB(ctx->opcode), true);                 \
        get_cpu_vsr(xbl, xB(ctx->opcode), false);                \
        tcg_gen_movi_i64(sgm, sgn_mask);                         \
        switch (op) {                                            \
            case OP_ABS: {                                       \
                tcg_gen_andc_i64(xbh, xbh, sgm);                 \
                tcg_gen_andc_i64(xbl, xbl, sgm);                 \
                break;                                           \
            }                                                    \
            case OP_NABS: {                                      \
                tcg_gen_or_i64(xbh, xbh, sgm);                   \
                tcg_gen_or_i64(xbl, xbl, sgm);                   \
                break;                                           \
            }                                                    \
            case OP_NEG: {                                       \
                tcg_gen_xor_i64(xbh, xbh, sgm);                  \
                tcg_gen_xor_i64(xbl, xbl, sgm);                  \
                break;                                           \
            }                                                    \
            case OP_CPSGN: {                                     \
                TCGv_i64 xah = tcg_temp_new_i64();               \
                TCGv_i64 xal = tcg_temp_new_i64();               \
                get_cpu_vsr(xah, xA(ctx->opcode), true);         \
                get_cpu_vsr(xal, xA(ctx->opcode), false);        \
                tcg_gen_and_i64(xah, xah, sgm);                  \
                tcg_gen_and_i64(xal, xal, sgm);                  \
                tcg_gen_andc_i64(xbh, xbh, sgm);                 \
                tcg_gen_andc_i64(xbl, xbl, sgm);                 \
                tcg_gen_or_i64(xbh, xbh, xah);                   \
                tcg_gen_or_i64(xbl, xbl, xal);                   \
                tcg_temp_free_i64(xah);                          \
                tcg_temp_free_i64(xal);                          \
                break;                                           \
            }                                                    \
        }                                                        \
        set_cpu_vsr(xT(ctx->opcode), xbh, true);                 \
        set_cpu_vsr(xT(ctx->opcode), xbl, false);                \
        tcg_temp_free_i64(xbh);                                  \
        tcg_temp_free_i64(xbl);                                  \
        tcg_temp_free_i64(sgm);                                  \
    }

VSX_VECTOR_MOVE(xvabsdp, OP_ABS, SGN_MASK_DP)
VSX_VECTOR_MOVE(xvnabsdp, OP_NABS, SGN_MASK_DP)
VSX_VECTOR_MOVE(xvnegdp, OP_NEG, SGN_MASK_DP)
VSX_VECTOR_MOVE(xvcpsgndp, OP_CPSGN, SGN_MASK_DP)
VSX_VECTOR_MOVE(xvabssp, OP_ABS, SGN_MASK_SP)
VSX_VECTOR_MOVE(xvnabssp, OP_NABS, SGN_MASK_SP)
VSX_VECTOR_MOVE(xvnegsp, OP_NEG, SGN_MASK_SP)
VSX_VECTOR_MOVE(xvcpsgnsp, OP_CPSGN, SGN_MASK_SP)

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
        tcg_temp_free_i32(ignored);                                           \
    }                                                                         \
    gen_helper_float_check_status(cpu_env);                                   \
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(ro);
    tcg_temp_free_ptr(xt);
    tcg_temp_free_ptr(xb);

    return true;
}

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
    tcg_temp_free_i32(opc);                                                   \
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
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(opc);                                                   \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(opc);                                                   \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(opc);                                                   \
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(opc);                                                   \
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i32(opc);                                                   \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(xb);                                                    \
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
    tcg_temp_free_i64(t0);                                    \
    tcg_temp_free_i64(t1);                                    \
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
GEN_VSX_HELPER_X3(xscmpeqdp, 0x0C, 0x00, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xscmpgtdp, 0x0C, 0x01, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xscmpgedp, 0x0C, 0x02, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xscmpnedp, 0x0C, 0x03, 0, PPC2_ISA300)
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
GEN_VSX_HELPER_XT_XB_ENV(xscvspdpn, 0x16, 0x14, 0, PPC2_VSX207)
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
GEN_VSX_HELPER_X1(xststdcsp, 0x14, 0x12, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xststdcdp, 0x14, 0x16, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xststdcqp, 0x04, 0x16, 0, PPC2_ISA300)

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
GEN_VSX_HELPER_2(xvtstdcsp, 0x14, 0x1A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvtstdcdp, 0x14, 0x1E, 0, PPC2_VSX)
GEN_VSX_HELPER_X3(xxperm, 0x08, 0x03, 0, PPC2_ISA300)
GEN_VSX_HELPER_X3(xxpermr, 0x08, 0x07, 0, PPC2_ISA300)

#define GEN_VSX_HELPER_VSX_MADD(name, op1, aop, mop, inval, type)             \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_ptr xt, xa, b, c;                                                    \
    if (unlikely(!ctx->vsx_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                                \
        return;                                                               \
    }                                                                         \
    xt = gen_vsr_ptr(xT(ctx->opcode));                                        \
    xa = gen_vsr_ptr(xA(ctx->opcode));                                        \
    if (ctx->opcode & PPC_BIT32(25)) {                                        \
        /*                                                                    \
         * AxT + B                                                            \
         */                                                                   \
        b = gen_vsr_ptr(xT(ctx->opcode));                                     \
        c = gen_vsr_ptr(xB(ctx->opcode));                                     \
    } else {                                                                  \
        /*                                                                    \
         * AxB + T                                                            \
         */                                                                   \
        b = gen_vsr_ptr(xB(ctx->opcode));                                     \
        c = gen_vsr_ptr(xT(ctx->opcode));                                     \
    }                                                                         \
    gen_helper_##name(cpu_env, xt, xa, b, c);                                 \
    tcg_temp_free_ptr(xt);                                                    \
    tcg_temp_free_ptr(xa);                                                    \
    tcg_temp_free_ptr(b);                                                     \
    tcg_temp_free_ptr(c);                                                     \
}

GEN_VSX_HELPER_VSX_MADD(xsmadddp, 0x04, 0x04, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xsmsubdp, 0x04, 0x06, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xsnmadddp, 0x04, 0x14, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xsnmsubdp, 0x04, 0x16, 0x17, 0, PPC2_VSX)
GEN_VSX_HELPER_VSX_MADD(xsmaddsp, 0x04, 0x00, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_VSX_MADD(xsmsubsp, 0x04, 0x02, 0x03, 0, PPC2_VSX207)
GEN_VSX_HELPER_VSX_MADD(xsnmaddsp, 0x04, 0x10, 0x11, 0, PPC2_VSX207)
GEN_VSX_HELPER_VSX_MADD(xsnmsubsp, 0x04, 0x12, 0x13, 0, PPC2_VSX207)
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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
        tcg_temp_free_i64(a0);                              \
        tcg_temp_free_i64(a1);                              \
        tcg_temp_free_i64(b0);                              \
        tcg_temp_free_i64(b1);                              \
        tcg_temp_free_i64(tmp);                             \
    }

VSX_XXMRG(xxmrghw, 1)
VSX_XXMRG(xxmrglw, 0)

static void gen_xxsel(DisasContext *ctx)
{
    int rt = xT(ctx->opcode);
    int ra = xA(ctx->opcode);
    int rb = xB(ctx->opcode);
    int rc = xC(ctx->opcode);

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    tcg_gen_gvec_bitsel(MO_64, vsr_full_offset(rt), vsr_full_offset(rc),
                        vsr_full_offset(rb), vsr_full_offset(ra), 16, 16);
}

static bool trans_XXSPLTW(DisasContext *ctx, arg_XX2 *a)
{
    int tofs, bofs;

    REQUIRE_VSX(ctx);

    tofs = vsr_full_offset(a->xt);
    bofs = vsr_full_offset(a->xb);
    bofs += a->uim << MO_32;
#ifndef HOST_WORDS_BIG_ENDIAN
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
            tcg_temp_free_i64(t0);
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
            tcg_temp_free_i64(t0);
            break;
        }
    }

    set_cpu_vsr(xT(ctx->opcode), xth, true);
    set_cpu_vsr(xT(ctx->opcode), xtl, false);

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
}

#define VSX_EXTRACT_INSERT(name)                                \
static void gen_##name(DisasContext *ctx)                       \
{                                                               \
    TCGv_ptr xt, xb;                                            \
    TCGv_i32 t0;                                                \
    TCGv_i64 t1;                                                \
    uint8_t uimm = UIMM4(ctx->opcode);                          \
                                                                \
    if (unlikely(!ctx->vsx_enabled)) {                          \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                  \
        return;                                                 \
    }                                                           \
    xt = gen_vsr_ptr(xT(ctx->opcode));                          \
    xb = gen_vsr_ptr(xB(ctx->opcode));                          \
    t0 = tcg_temp_new_i32();                                    \
    t1 = tcg_temp_new_i64();                                    \
    /*                                                          \
     * uimm > 15 out of bound and for                           \
     * uimm > 12 handle as per hardware in helper               \
     */                                                         \
    if (uimm > 15) {                                            \
        tcg_gen_movi_i64(t1, 0);                                \
        set_cpu_vsr(xT(ctx->opcode), t1, true);                 \
        set_cpu_vsr(xT(ctx->opcode), t1, false);                \
        return;                                                 \
    }                                                           \
    tcg_gen_movi_i32(t0, uimm);                                 \
    gen_helper_##name(cpu_env, xt, xb, t0);                     \
    tcg_temp_free_ptr(xb);                                      \
    tcg_temp_free_ptr(xt);                                      \
    tcg_temp_free_i32(t0);                                      \
    tcg_temp_free_i64(t1);                                      \
}

VSX_EXTRACT_INSERT(xxextractuw)
VSX_EXTRACT_INSERT(xxinsertw)

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
    tcg_temp_free_i64(t0);
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

    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
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
    /* dword[1] is undefined */
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(xth);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xah);
    tcg_temp_free_i64(xal);
    tcg_temp_free_i64(xbh);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(exp);
    tcg_temp_free_i64(zr);
    tcg_temp_free_i64(nan);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(exp);
    tcg_temp_free_i64(zr);
    tcg_temp_free_i64(nan);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xah);
    tcg_temp_free_i64(xal);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xah);
    tcg_temp_free_i64(xal);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
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

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
}

GEN_VSX_HELPER_X2(xvxsigsp, 0x00, 0x04, 0, PPC2_ISA300)

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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(exp);
    tcg_temp_free_i64(zr);
    tcg_temp_free_i64(nan);
    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
    tcg_temp_free_i64(xbh);
    tcg_temp_free_i64(xbl);
}

static bool do_lstxv(DisasContext *ctx, int ra, TCGv displ,
                     int rt, bool store, bool paired)
{
    TCGv ea;
    TCGv_i64 xt;
    MemOp mop;
    int rt1, rt2;

    xt = tcg_temp_new_i64();

    mop = DEF_MEMOP(MO_Q);

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

    tcg_temp_free(ea);
    tcg_temp_free_i64(xt);
    return true;
}

static bool do_lstxv_D(DisasContext *ctx, arg_D *a, bool store, bool paired)
{
    if (paired) {
        REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    } else {
        REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    }

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
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VSX(ctx);

    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }

    return do_lstxv(ctx, d.ra, tcg_constant_tl(d.si), d.rt, store, paired);
}

static bool do_lstxv_X(DisasContext *ctx, arg_X *a, bool store, bool paired)
{
    if (paired) {
        REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    } else {
        REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    }

    if (paired || a->rt >= 32) {
        REQUIRE_VSX(ctx);
    } else {
        REQUIRE_VECTOR(ctx);
    }

    return do_lstxv(ctx, a->ra, cpu_gpr[a->rb], a->rt, store, paired);
}

TRANS(STXV, do_lstxv_D, true, false)
TRANS(LXV, do_lstxv_D, false, false)
TRANS(STXVP, do_lstxv_D, true, true)
TRANS(LXVP, do_lstxv_D, false, true)
TRANS(STXVX, do_lstxv_X, true, false)
TRANS(LXVX, do_lstxv_X, false, false)
TRANS(STXVPX, do_lstxv_X, true, true)
TRANS(LXVPX, do_lstxv_X, false, true)
TRANS64(PSTXV, do_lstxv_PLS_D, true, false)
TRANS64(PLXV, do_lstxv_PLS_D, false, false)
TRANS64(PSTXVP, do_lstxv_PLS_D, true, true)
TRANS64(PLXVP, do_lstxv_PLS_D, false, true)

static void gen_xxblendv_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                             TCGv_vec c)
{
    TCGv_vec tmp = tcg_temp_new_vec_matching(c);
    tcg_gen_sari_vec(vece, tmp, c, (8 << vece) - 1);
    tcg_gen_bitsel_vec(vece, t, tmp, b, a);
    tcg_temp_free_vec(tmp);
}

static bool do_xxblendv(DisasContext *ctx, arg_XX4 *a, unsigned vece)
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

static bool do_xsmaxmincjdp(DisasContext *ctx, arg_XX3 *a,
                            void (*helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr xt, xa, xb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    xt = gen_vsr_ptr(a->xt);
    xa = gen_vsr_ptr(a->xa);
    xb = gen_vsr_ptr(a->xb);

    helper(cpu_env, xt, xa, xb);

    tcg_temp_free_ptr(xt);
    tcg_temp_free_ptr(xa);
    tcg_temp_free_ptr(xb);

    return true;
}

TRANS(XSMAXCDP, do_xsmaxmincjdp, gen_helper_xsmaxcdp)
TRANS(XSMINCDP, do_xsmaxmincjdp, gen_helper_xsmincdp)
TRANS(XSMAXJDP, do_xsmaxmincjdp, gen_helper_xsmaxjdp)
TRANS(XSMINJDP, do_xsmaxmincjdp, gen_helper_xsminjdp)

#undef GEN_XX2FORM
#undef GEN_XX3FORM
#undef GEN_XX2IFORM
#undef GEN_XX3_RC_FORM
#undef GEN_XX3FORM_DM
#undef VSX_LOGICAL
