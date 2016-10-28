/***                           VSX extension                               ***/

static inline TCGv_i64 cpu_vsrh(int n)
{
    if (n < 32) {
        return cpu_fpr[n];
    } else {
        return cpu_avrh[n-32];
    }
}

static inline TCGv_i64 cpu_vsrl(int n)
{
    if (n < 32) {
        return cpu_vsr[n];
    } else {
        return cpu_avrl[n-32];
    }
}

#define VSX_LOAD_SCALAR(name, operation)                      \
static void gen_##name(DisasContext *ctx)                     \
{                                                             \
    TCGv EA;                                                  \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                     \
    EA = tcg_temp_new();                                      \
    gen_addr_reg_index(ctx, EA);                              \
    gen_qemu_##operation(ctx, cpu_vsrh(xT(ctx->opcode)), EA); \
    /* NOTE: cpu_vsrl is undefined */                         \
    tcg_temp_free(EA);                                        \
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
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld64_i64(ctx, cpu_vsrh(xT(ctx->opcode)), EA);
    tcg_gen_addi_tl(EA, EA, 8);
    gen_qemu_ld64_i64(ctx, cpu_vsrl(xT(ctx->opcode)), EA);
    tcg_temp_free(EA);
}

static void gen_lxvdsx(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld64_i64(ctx, cpu_vsrh(xT(ctx->opcode)), EA);
    tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_vsrh(xT(ctx->opcode)));
    tcg_temp_free(EA);
}

static void gen_lxvw4x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
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
    tcg_temp_free(EA);
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
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEQ);
    if (ctx->le_mode) {
        gen_bswap16x8(xth, xtl, xth, xtl);
    }
    tcg_temp_free(EA);
}

static void gen_lxvb16x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_ld_i64(xth, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_ld_i64(xtl, EA, ctx->mem_idx, MO_BEQ);
    tcg_temp_free(EA);
}

#define VSX_STORE_SCALAR(name, operation)                     \
static void gen_##name(DisasContext *ctx)                     \
{                                                             \
    TCGv EA;                                                  \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                     \
    EA = tcg_temp_new();                                      \
    gen_addr_reg_index(ctx, EA);                              \
    gen_qemu_##operation(ctx, cpu_vsrh(xS(ctx->opcode)), EA); \
    tcg_temp_free(EA);                                        \
}

VSX_STORE_SCALAR(stxsdx, st64_i64)

VSX_STORE_SCALAR(stxsibx, st8_i64)
VSX_STORE_SCALAR(stxsihx, st16_i64)
VSX_STORE_SCALAR(stxsiwx, st32_i64)
VSX_STORE_SCALAR(stxsspx, st32fs)

static void gen_stxvd2x(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_st64_i64(ctx, cpu_vsrh(xS(ctx->opcode)), EA);
    tcg_gen_addi_tl(EA, EA, 8);
    gen_qemu_st64_i64(ctx, cpu_vsrl(xS(ctx->opcode)), EA);
    tcg_temp_free(EA);
}

static void gen_stxvw4x(DisasContext *ctx)
{
    TCGv_i64 xsh = cpu_vsrh(xS(ctx->opcode));
    TCGv_i64 xsl = cpu_vsrl(xS(ctx->opcode));
    TCGv EA;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
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
}

static void gen_stxvh8x(DisasContext *ctx)
{
    TCGv_i64 xsh = cpu_vsrh(xS(ctx->opcode));
    TCGv_i64 xsl = cpu_vsrl(xS(ctx->opcode));
    TCGv EA;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
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
}

static void gen_stxvb16x(DisasContext *ctx)
{
    TCGv_i64 xsh = cpu_vsrh(xS(ctx->opcode));
    TCGv_i64 xsl = cpu_vsrl(xS(ctx->opcode));
    TCGv EA;

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tcg_gen_qemu_st_i64(xsh, EA, ctx->mem_idx, MO_BEQ);
    tcg_gen_addi_tl(EA, EA, 8);
    tcg_gen_qemu_st_i64(xsl, EA, ctx->mem_idx, MO_BEQ);
    tcg_temp_free(EA);
}

#define MV_VSRW(name, tcgop1, tcgop2, target, source)           \
static void gen_##name(DisasContext *ctx)                       \
{                                                               \
    if (xS(ctx->opcode) < 32) {                                 \
        if (unlikely(!ctx->fpu_enabled)) {                      \
            gen_exception(ctx, POWERPC_EXCP_FPU);               \
            return;                                             \
        }                                                       \
    } else {                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                  \
            gen_exception(ctx, POWERPC_EXCP_VPU);               \
            return;                                             \
        }                                                       \
    }                                                           \
    TCGv_i64 tmp = tcg_temp_new_i64();                          \
    tcg_gen_##tcgop1(tmp, source);                              \
    tcg_gen_##tcgop2(target, tmp);                              \
    tcg_temp_free_i64(tmp);                                     \
}


MV_VSRW(mfvsrwz, ext32u_i64, trunc_i64_tl, cpu_gpr[rA(ctx->opcode)], \
        cpu_vsrh(xS(ctx->opcode)))
MV_VSRW(mtvsrwa, extu_tl_i64, ext32s_i64, cpu_vsrh(xT(ctx->opcode)), \
        cpu_gpr[rA(ctx->opcode)])
MV_VSRW(mtvsrwz, extu_tl_i64, ext32u_i64, cpu_vsrh(xT(ctx->opcode)), \
        cpu_gpr[rA(ctx->opcode)])

#if defined(TARGET_PPC64)
#define MV_VSRD(name, target, source)                           \
static void gen_##name(DisasContext *ctx)                       \
{                                                               \
    if (xS(ctx->opcode) < 32) {                                 \
        if (unlikely(!ctx->fpu_enabled)) {                      \
            gen_exception(ctx, POWERPC_EXCP_FPU);               \
            return;                                             \
        }                                                       \
    } else {                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                  \
            gen_exception(ctx, POWERPC_EXCP_VPU);               \
            return;                                             \
        }                                                       \
    }                                                           \
    tcg_gen_mov_i64(target, source);                            \
}

MV_VSRD(mfvsrd, cpu_gpr[rA(ctx->opcode)], cpu_vsrh(xS(ctx->opcode)))
MV_VSRD(mtvsrd, cpu_vsrh(xT(ctx->opcode)), cpu_gpr[rA(ctx->opcode)])

static void gen_mfvsrld(DisasContext *ctx)
{
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

    tcg_gen_mov_i64(cpu_gpr[rA(ctx->opcode)], cpu_vsrl(xS(ctx->opcode)));
}

static void gen_mtvsrdd(DisasContext *ctx)
{
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

    if (!rA(ctx->opcode)) {
        tcg_gen_movi_i64(cpu_vsrh(xT(ctx->opcode)), 0);
    } else {
        tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), cpu_gpr[rA(ctx->opcode)]);
    }

    tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_gpr[rB(ctx->opcode)]);
}

static void gen_mtvsrws(DisasContext *ctx)
{
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

    tcg_gen_deposit_i64(cpu_vsrl(xT(ctx->opcode)), cpu_gpr[rA(ctx->opcode)],
                        cpu_gpr[rA(ctx->opcode)], 32, 32);
    tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), cpu_vsrl(xT(ctx->opcode)));
}

#endif

static void gen_xxpermdi(DisasContext *ctx)
{
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }

    if (unlikely((xT(ctx->opcode) == xA(ctx->opcode)) ||
                 (xT(ctx->opcode) == xB(ctx->opcode)))) {
        TCGv_i64 xh, xl;

        xh = tcg_temp_new_i64();
        xl = tcg_temp_new_i64();

        if ((DM(ctx->opcode) & 2) == 0) {
            tcg_gen_mov_i64(xh, cpu_vsrh(xA(ctx->opcode)));
        } else {
            tcg_gen_mov_i64(xh, cpu_vsrl(xA(ctx->opcode)));
        }
        if ((DM(ctx->opcode) & 1) == 0) {
            tcg_gen_mov_i64(xl, cpu_vsrh(xB(ctx->opcode)));
        } else {
            tcg_gen_mov_i64(xl, cpu_vsrl(xB(ctx->opcode)));
        }

        tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), xh);
        tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), xl);

        tcg_temp_free_i64(xh);
        tcg_temp_free_i64(xl);
    } else {
        if ((DM(ctx->opcode) & 2) == 0) {
            tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), cpu_vsrh(xA(ctx->opcode)));
        } else {
            tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), cpu_vsrl(xA(ctx->opcode)));
        }
        if ((DM(ctx->opcode) & 1) == 0) {
            tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_vsrh(xB(ctx->opcode)));
        } else {
            tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_vsrl(xB(ctx->opcode)));
        }
    }
}

#define OP_ABS 1
#define OP_NABS 2
#define OP_NEG 3
#define OP_CPSGN 4
#define SGN_MASK_DP  0x8000000000000000ull
#define SGN_MASK_SP 0x8000000080000000ull

#define VSX_SCALAR_MOVE(name, op, sgn_mask)                       \
static void glue(gen_, name)(DisasContext * ctx)                  \
    {                                                             \
        TCGv_i64 xb, sgm;                                         \
        if (unlikely(!ctx->vsx_enabled)) {                        \
            gen_exception(ctx, POWERPC_EXCP_VSXU);                \
            return;                                               \
        }                                                         \
        xb = tcg_temp_new_i64();                                  \
        sgm = tcg_temp_new_i64();                                 \
        tcg_gen_mov_i64(xb, cpu_vsrh(xB(ctx->opcode)));           \
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
                tcg_gen_mov_i64(xa, cpu_vsrh(xA(ctx->opcode)));   \
                tcg_gen_and_i64(xa, xa, sgm);                     \
                tcg_gen_andc_i64(xb, xb, sgm);                    \
                tcg_gen_or_i64(xb, xb, xa);                       \
                tcg_temp_free_i64(xa);                            \
                break;                                            \
            }                                                     \
        }                                                         \
        tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), xb);           \
        tcg_temp_free_i64(xb);                                    \
        tcg_temp_free_i64(sgm);                                   \
    }

VSX_SCALAR_MOVE(xsabsdp, OP_ABS, SGN_MASK_DP)
VSX_SCALAR_MOVE(xsnabsdp, OP_NABS, SGN_MASK_DP)
VSX_SCALAR_MOVE(xsnegdp, OP_NEG, SGN_MASK_DP)
VSX_SCALAR_MOVE(xscpsgndp, OP_CPSGN, SGN_MASK_DP)

#define VSX_VECTOR_MOVE(name, op, sgn_mask)                      \
static void glue(gen_, name)(DisasContext * ctx)                 \
    {                                                            \
        TCGv_i64 xbh, xbl, sgm;                                  \
        if (unlikely(!ctx->vsx_enabled)) {                       \
            gen_exception(ctx, POWERPC_EXCP_VSXU);               \
            return;                                              \
        }                                                        \
        xbh = tcg_temp_new_i64();                                \
        xbl = tcg_temp_new_i64();                                \
        sgm = tcg_temp_new_i64();                                \
        tcg_gen_mov_i64(xbh, cpu_vsrh(xB(ctx->opcode)));         \
        tcg_gen_mov_i64(xbl, cpu_vsrl(xB(ctx->opcode)));         \
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
                tcg_gen_mov_i64(xah, cpu_vsrh(xA(ctx->opcode))); \
                tcg_gen_mov_i64(xal, cpu_vsrl(xA(ctx->opcode))); \
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
        tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), xbh);         \
        tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), xbl);         \
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

#define GEN_VSX_HELPER_2(name, op1, op2, inval, type)                         \
static void gen_##name(DisasContext * ctx)                                    \
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

#define GEN_VSX_HELPER_XT_XB_ENV(name, op1, op2, inval, type) \
static void gen_##name(DisasContext * ctx)                    \
{                                                             \
    if (unlikely(!ctx->vsx_enabled)) {                        \
        gen_exception(ctx, POWERPC_EXCP_VSXU);                \
        return;                                               \
    }                                                         \
    gen_helper_##name(cpu_vsrh(xT(ctx->opcode)), cpu_env,     \
                      cpu_vsrh(xB(ctx->opcode)));             \
}

GEN_VSX_HELPER_2(xsadddp, 0x00, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xssubdp, 0x00, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmuldp, 0x00, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsdivdp, 0x00, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsredp, 0x14, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xssqrtdp, 0x16, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrsqrtedp, 0x14, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xstdivdp, 0x14, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xstsqrtdp, 0x14, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmaddadp, 0x04, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmaddmdp, 0x04, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmsubadp, 0x04, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmsubmdp, 0x04, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsnmaddadp, 0x04, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsnmaddmdp, 0x04, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsnmsubadp, 0x04, 0x16, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsnmsubmdp, 0x04, 0x17, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscmpeqdp, 0x0C, 0x00, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xscmpgtdp, 0x0C, 0x01, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xscmpgedp, 0x0C, 0x02, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xscmpnedp, 0x0C, 0x03, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xscmpodp, 0x0C, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscmpudp, 0x0C, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmaxdp, 0x00, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsmindp, 0x00, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvdpsp, 0x12, 0x10, 0, PPC2_VSX)
GEN_VSX_HELPER_XT_XB_ENV(xscvdpspn, 0x16, 0x10, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xscvspdp, 0x12, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_XT_XB_ENV(xscvspdpn, 0x16, 0x14, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xscvdpsxds, 0x10, 0x15, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvdpsxws, 0x10, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvdpuxds, 0x10, 0x14, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvdpuxws, 0x10, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvsxddp, 0x10, 0x17, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xscvuxddp, 0x10, 0x16, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrdpi, 0x12, 0x04, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrdpic, 0x16, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrdpim, 0x12, 0x07, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrdpip, 0x12, 0x06, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xsrdpiz, 0x12, 0x05, 0, PPC2_VSX)
GEN_VSX_HELPER_XT_XB_ENV(xsrsp, 0x12, 0x11, 0, PPC2_VSX207)

GEN_VSX_HELPER_2(xsaddsp, 0x00, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xssubsp, 0x00, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsmulsp, 0x00, 0x02, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsdivsp, 0x00, 0x03, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsresp, 0x14, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xssqrtsp, 0x16, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsrsqrtesp, 0x14, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsmaddasp, 0x04, 0x00, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsmaddmsp, 0x04, 0x01, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsmsubasp, 0x04, 0x02, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsmsubmsp, 0x04, 0x03, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsnmaddasp, 0x04, 0x10, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsnmaddmsp, 0x04, 0x11, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsnmsubasp, 0x04, 0x12, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xsnmsubmsp, 0x04, 0x13, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xscvsxdsp, 0x10, 0x13, 0, PPC2_VSX207)
GEN_VSX_HELPER_2(xscvuxdsp, 0x10, 0x12, 0, PPC2_VSX207)

GEN_VSX_HELPER_2(xvadddp, 0x00, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvsubdp, 0x00, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmuldp, 0x00, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvdivdp, 0x00, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvredp, 0x14, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvsqrtdp, 0x16, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrsqrtedp, 0x14, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvtdivdp, 0x14, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvtsqrtdp, 0x14, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaddadp, 0x04, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaddmdp, 0x04, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmsubadp, 0x04, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmsubmdp, 0x04, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmaddadp, 0x04, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmaddmdp, 0x04, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmsubadp, 0x04, 0x1E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmsubmdp, 0x04, 0x1F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaxdp, 0x00, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmindp, 0x00, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpeqdp, 0x0C, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpgtdp, 0x0C, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpgedp, 0x0C, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpnedp, 0x0C, 0x0F, 0, PPC2_ISA300)
GEN_VSX_HELPER_2(xvcvdpsp, 0x12, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvdpsxds, 0x10, 0x1D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvdpsxws, 0x10, 0x0D, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvdpuxds, 0x10, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvdpuxws, 0x10, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvsxddp, 0x10, 0x1F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvuxddp, 0x10, 0x1E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvsxwdp, 0x10, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvuxwdp, 0x10, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrdpi, 0x12, 0x0C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrdpic, 0x16, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrdpim, 0x12, 0x0F, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrdpip, 0x12, 0x0E, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrdpiz, 0x12, 0x0D, 0, PPC2_VSX)

GEN_VSX_HELPER_2(xvaddsp, 0x00, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvsubsp, 0x00, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmulsp, 0x00, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvdivsp, 0x00, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvresp, 0x14, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvsqrtsp, 0x16, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrsqrtesp, 0x14, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvtdivsp, 0x14, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvtsqrtsp, 0x14, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaddasp, 0x04, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaddmsp, 0x04, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmsubasp, 0x04, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmsubmsp, 0x04, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmaddasp, 0x04, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmaddmsp, 0x04, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmsubasp, 0x04, 0x1A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvnmsubmsp, 0x04, 0x1B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvmaxsp, 0x00, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvminsp, 0x00, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpeqsp, 0x0C, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpgtsp, 0x0C, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpgesp, 0x0C, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcmpnesp, 0x0C, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvspdp, 0x12, 0x1C, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvspsxds, 0x10, 0x19, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvspsxws, 0x10, 0x09, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvspuxds, 0x10, 0x18, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvspuxws, 0x10, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvsxdsp, 0x10, 0x1B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvuxdsp, 0x10, 0x1A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvsxwsp, 0x10, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvcvuxwsp, 0x10, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrspi, 0x12, 0x08, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrspic, 0x16, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrspim, 0x12, 0x0B, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrspip, 0x12, 0x0A, 0, PPC2_VSX)
GEN_VSX_HELPER_2(xvrspiz, 0x12, 0x09, 0, PPC2_VSX)

static void gen_xxbrd(DisasContext *ctx)
{
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    TCGv_i64 xbh = cpu_vsrh(xB(ctx->opcode));
    TCGv_i64 xbl = cpu_vsrl(xB(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    tcg_gen_bswap64_i64(xth, xbh);
    tcg_gen_bswap64_i64(xtl, xbl);
}

static void gen_xxbrh(DisasContext *ctx)
{
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    TCGv_i64 xbh = cpu_vsrh(xB(ctx->opcode));
    TCGv_i64 xbl = cpu_vsrl(xB(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_bswap16x8(xth, xtl, xbh, xbl);
}

static void gen_xxbrq(DisasContext *ctx)
{
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    TCGv_i64 xbh = cpu_vsrh(xB(ctx->opcode));
    TCGv_i64 xbl = cpu_vsrl(xB(ctx->opcode));
    TCGv_i64 t0 = tcg_temp_new_i64();

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    tcg_gen_bswap64_i64(t0, xbl);
    tcg_gen_bswap64_i64(xtl, xbh);
    tcg_gen_mov_i64(xth, t0);
    tcg_temp_free_i64(t0);
}

static void gen_xxbrw(DisasContext *ctx)
{
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    TCGv_i64 xbh = cpu_vsrh(xB(ctx->opcode));
    TCGv_i64 xbl = cpu_vsrl(xB(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_bswap32x4(xth, xtl, xbh, xbl);
}

#define VSX_LOGICAL(name, tcg_op)                                    \
static void glue(gen_, name)(DisasContext * ctx)                     \
    {                                                                \
        if (unlikely(!ctx->vsx_enabled)) {                           \
            gen_exception(ctx, POWERPC_EXCP_VSXU);                   \
            return;                                                  \
        }                                                            \
        tcg_op(cpu_vsrh(xT(ctx->opcode)), cpu_vsrh(xA(ctx->opcode)), \
            cpu_vsrh(xB(ctx->opcode)));                              \
        tcg_op(cpu_vsrl(xT(ctx->opcode)), cpu_vsrl(xA(ctx->opcode)), \
            cpu_vsrl(xB(ctx->opcode)));                              \
    }

VSX_LOGICAL(xxland, tcg_gen_and_i64)
VSX_LOGICAL(xxlandc, tcg_gen_andc_i64)
VSX_LOGICAL(xxlor, tcg_gen_or_i64)
VSX_LOGICAL(xxlxor, tcg_gen_xor_i64)
VSX_LOGICAL(xxlnor, tcg_gen_nor_i64)
VSX_LOGICAL(xxleqv, tcg_gen_eqv_i64)
VSX_LOGICAL(xxlnand, tcg_gen_nand_i64)
VSX_LOGICAL(xxlorc, tcg_gen_orc_i64)

#define VSX_XXMRG(name, high)                               \
static void glue(gen_, name)(DisasContext * ctx)            \
    {                                                       \
        TCGv_i64 a0, a1, b0, b1;                            \
        if (unlikely(!ctx->vsx_enabled)) {                  \
            gen_exception(ctx, POWERPC_EXCP_VSXU);          \
            return;                                         \
        }                                                   \
        a0 = tcg_temp_new_i64();                            \
        a1 = tcg_temp_new_i64();                            \
        b0 = tcg_temp_new_i64();                            \
        b1 = tcg_temp_new_i64();                            \
        if (high) {                                         \
            tcg_gen_mov_i64(a0, cpu_vsrh(xA(ctx->opcode))); \
            tcg_gen_mov_i64(a1, cpu_vsrh(xA(ctx->opcode))); \
            tcg_gen_mov_i64(b0, cpu_vsrh(xB(ctx->opcode))); \
            tcg_gen_mov_i64(b1, cpu_vsrh(xB(ctx->opcode))); \
        } else {                                            \
            tcg_gen_mov_i64(a0, cpu_vsrl(xA(ctx->opcode))); \
            tcg_gen_mov_i64(a1, cpu_vsrl(xA(ctx->opcode))); \
            tcg_gen_mov_i64(b0, cpu_vsrl(xB(ctx->opcode))); \
            tcg_gen_mov_i64(b1, cpu_vsrl(xB(ctx->opcode))); \
        }                                                   \
        tcg_gen_shri_i64(a0, a0, 32);                       \
        tcg_gen_shri_i64(b0, b0, 32);                       \
        tcg_gen_deposit_i64(cpu_vsrh(xT(ctx->opcode)),      \
                            b0, a0, 32, 32);                \
        tcg_gen_deposit_i64(cpu_vsrl(xT(ctx->opcode)),      \
                            b1, a1, 32, 32);                \
        tcg_temp_free_i64(a0);                              \
        tcg_temp_free_i64(a1);                              \
        tcg_temp_free_i64(b0);                              \
        tcg_temp_free_i64(b1);                              \
    }

VSX_XXMRG(xxmrghw, 1)
VSX_XXMRG(xxmrglw, 0)

static void gen_xxsel(DisasContext * ctx)
{
    TCGv_i64 a, b, c;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    a = tcg_temp_new_i64();
    b = tcg_temp_new_i64();
    c = tcg_temp_new_i64();

    tcg_gen_mov_i64(a, cpu_vsrh(xA(ctx->opcode)));
    tcg_gen_mov_i64(b, cpu_vsrh(xB(ctx->opcode)));
    tcg_gen_mov_i64(c, cpu_vsrh(xC(ctx->opcode)));

    tcg_gen_and_i64(b, b, c);
    tcg_gen_andc_i64(a, a, c);
    tcg_gen_or_i64(cpu_vsrh(xT(ctx->opcode)), a, b);

    tcg_gen_mov_i64(a, cpu_vsrl(xA(ctx->opcode)));
    tcg_gen_mov_i64(b, cpu_vsrl(xB(ctx->opcode)));
    tcg_gen_mov_i64(c, cpu_vsrl(xC(ctx->opcode)));

    tcg_gen_and_i64(b, b, c);
    tcg_gen_andc_i64(a, a, c);
    tcg_gen_or_i64(cpu_vsrl(xT(ctx->opcode)), a, b);

    tcg_temp_free_i64(a);
    tcg_temp_free_i64(b);
    tcg_temp_free_i64(c);
}

static void gen_xxspltw(DisasContext *ctx)
{
    TCGv_i64 b, b2;
    TCGv_i64 vsr = (UIM(ctx->opcode) & 2) ?
                   cpu_vsrl(xB(ctx->opcode)) :
                   cpu_vsrh(xB(ctx->opcode));

    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }

    b = tcg_temp_new_i64();
    b2 = tcg_temp_new_i64();

    if (UIM(ctx->opcode) & 1) {
        tcg_gen_ext32u_i64(b, vsr);
    } else {
        tcg_gen_shri_i64(b, vsr, 32);
    }

    tcg_gen_shli_i64(b2, b, 32);
    tcg_gen_or_i64(cpu_vsrh(xT(ctx->opcode)), b, b2);
    tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_vsrh(xT(ctx->opcode)));

    tcg_temp_free_i64(b);
    tcg_temp_free_i64(b2);
}

#define pattern(x) (((x) & 0xff) * (~(uint64_t)0 / 0xff))

static void gen_xxspltib(DisasContext *ctx)
{
    unsigned char uim8 = IMM8(ctx->opcode);
    if (xS(ctx->opcode) < 32) {
        if (unlikely(!ctx->altivec_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VPU);
            return;
        }
    } else {
        if (unlikely(!ctx->vsx_enabled)) {
            gen_exception(ctx, POWERPC_EXCP_VSXU);
            return;
        }
    }
    tcg_gen_movi_i64(cpu_vsrh(xT(ctx->opcode)), pattern(uim8));
    tcg_gen_movi_i64(cpu_vsrl(xT(ctx->opcode)), pattern(uim8));
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
            tcg_gen_mov_i64(xth, cpu_vsrh(xA(ctx->opcode)));
            tcg_gen_mov_i64(xtl, cpu_vsrl(xA(ctx->opcode)));
            break;
        }
        case 1: {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_mov_i64(xth, cpu_vsrh(xA(ctx->opcode)));
            tcg_gen_shli_i64(xth, xth, 32);
            tcg_gen_mov_i64(t0, cpu_vsrl(xA(ctx->opcode)));
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xth, xth, t0);
            tcg_gen_mov_i64(xtl, cpu_vsrl(xA(ctx->opcode)));
            tcg_gen_shli_i64(xtl, xtl, 32);
            tcg_gen_mov_i64(t0, cpu_vsrh(xB(ctx->opcode)));
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xtl, xtl, t0);
            tcg_temp_free_i64(t0);
            break;
        }
        case 2: {
            tcg_gen_mov_i64(xth, cpu_vsrl(xA(ctx->opcode)));
            tcg_gen_mov_i64(xtl, cpu_vsrh(xB(ctx->opcode)));
            break;
        }
        case 3: {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_mov_i64(xth, cpu_vsrl(xA(ctx->opcode)));
            tcg_gen_shli_i64(xth, xth, 32);
            tcg_gen_mov_i64(t0, cpu_vsrh(xB(ctx->opcode)));
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xth, xth, t0);
            tcg_gen_mov_i64(xtl, cpu_vsrh(xB(ctx->opcode)));
            tcg_gen_shli_i64(xtl, xtl, 32);
            tcg_gen_mov_i64(t0, cpu_vsrl(xB(ctx->opcode)));
            tcg_gen_shri_i64(t0, t0, 32);
            tcg_gen_or_i64(xtl, xtl, t0);
            tcg_temp_free_i64(t0);
            break;
        }
    }

    tcg_gen_mov_i64(cpu_vsrh(xT(ctx->opcode)), xth);
    tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), xtl);

    tcg_temp_free_i64(xth);
    tcg_temp_free_i64(xtl);
}

#undef GEN_XX2FORM
#undef GEN_XX3FORM
#undef GEN_XX2IFORM
#undef GEN_XX3_RC_FORM
#undef GEN_XX3FORM_DM
#undef VSX_LOGICAL
