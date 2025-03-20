/*
 * translate/vmx-impl.c
 *
 * Altivec/VMX translation
 */

/***                      Altivec vector extension                         ***/
/* Altivec registers moves */

static inline TCGv_ptr gen_avr_ptr(int reg)
{
    TCGv_ptr r = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(r, tcg_env, avr_full_offset(reg));
    return r;
}

static inline void get_avr64(TCGv_i64 dst, int regno, bool high)
{
    tcg_gen_ld_i64(dst, tcg_env, avr64_offset(regno, high));
}

static inline void set_avr64(int regno, TCGv_i64 src, bool high)
{
    tcg_gen_st_i64(src, tcg_env, avr64_offset(regno, high));
}

static inline void get_avr_full(TCGv_i128 dst, int regno)
{
    tcg_gen_ld_i128(dst, tcg_env, avr_full_offset(regno));
}

static inline void set_avr_full(int regno, TCGv_i128 src)
{
    tcg_gen_st_i128(src, tcg_env, avr_full_offset(regno));
}

static bool trans_LVX(DisasContext *ctx, arg_X *a)
{
    TCGv EA;
    TCGv_i128 avr;
    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);
    gen_set_access_type(ctx, ACCESS_INT);
    avr = tcg_temp_new_i128();
    EA = do_ea_calc(ctx, a->ra, cpu_gpr[a->rb]);
    tcg_gen_andi_tl(EA, EA, ~0xf);
    tcg_gen_qemu_ld_i128(avr, EA, ctx->mem_idx,
                         DEF_MEMOP(MO_128 | MO_ATOM_IFALIGN_PAIR));
    set_avr_full(a->rt, avr);
    return true;
}

/* As we don't emulate the cache, lvxl is strictly equivalent to lvx */
QEMU_FLATTEN
static bool trans_LVXL(DisasContext *ctx, arg_LVXL *a)
{
    return trans_LVX(ctx, a);
}

static bool trans_STVX(DisasContext *ctx, arg_STVX *a)
{
    TCGv EA;
    TCGv_i128 avr;
    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);
    gen_set_access_type(ctx, ACCESS_INT);
    avr = tcg_temp_new_i128();
    EA = do_ea_calc(ctx, a->ra, cpu_gpr[a->rb]);
    tcg_gen_andi_tl(EA, EA, ~0xf);
    get_avr_full(avr, a->rt);
    tcg_gen_qemu_st_i128(avr, EA, ctx->mem_idx,
                         DEF_MEMOP(MO_128 | MO_ATOM_IFALIGN_PAIR));
    return true;
}

/* As we don't emulate the cache, stvxl is strictly equivalent to stvx */
QEMU_FLATTEN
static bool trans_STVXL(DisasContext *ctx, arg_STVXL *a)
{
    return trans_STVX(ctx, a);
}

static bool do_ldst_ve_X(DisasContext *ctx, arg_X *a, int size,
                   void (*helper)(TCGv_env, TCGv_ptr, TCGv))
{
    TCGv EA;
    TCGv_ptr vrt;
    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);
    gen_set_access_type(ctx, ACCESS_INT);
    EA = do_ea_calc(ctx, a->ra, cpu_gpr[a->rb]);
    if (size > 1) {
        tcg_gen_andi_tl(EA, EA, ~(size - 1));
    }
    vrt = gen_avr_ptr(a->rt);
    helper(tcg_env, vrt, EA);
    return true;
}

TRANS(LVEBX, do_ldst_ve_X, 1, gen_helper_LVEBX);
TRANS(LVEHX, do_ldst_ve_X, 2, gen_helper_LVEHX);
TRANS(LVEWX, do_ldst_ve_X, 4, gen_helper_LVEWX);

TRANS(STVEBX, do_ldst_ve_X, 1, gen_helper_STVEBX);
TRANS(STVEHX, do_ldst_ve_X, 2, gen_helper_STVEHX);
TRANS(STVEWX, do_ldst_ve_X, 4, gen_helper_STVEWX);

static void gen_mfvscr(DisasContext *ctx)
{
    TCGv_i32 t;
    TCGv_i64 avr;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    avr = tcg_temp_new_i64();
    tcg_gen_movi_i64(avr, 0);
    set_avr64(rD(ctx->opcode), avr, true);
    t = tcg_temp_new_i32();
    gen_helper_mfvscr(t, tcg_env);
    tcg_gen_extu_i32_i64(avr, t);
    set_avr64(rD(ctx->opcode), avr, false);
}

static void gen_mtvscr(DisasContext *ctx)
{
    TCGv_i32 val;
    int bofs;

    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }

    val = tcg_temp_new_i32();
    bofs = avr_full_offset(rB(ctx->opcode));
#if HOST_BIG_ENDIAN
    bofs += 3 * 4;
#endif

    tcg_gen_ld_i32(val, tcg_env, bofs);
    gen_helper_mtvscr(tcg_env, val);
}

static void gen_vx_vmul10(DisasContext *ctx, bool add_cin, bool ret_carry)
{
    TCGv_i64 t0;
    TCGv_i64 t1;
    TCGv_i64 t2;
    TCGv_i64 avr;
    TCGv_i64 ten, z;

    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    avr = tcg_temp_new_i64();
    ten = tcg_constant_i64(10);
    z = tcg_constant_i64(0);

    if (add_cin) {
        get_avr64(avr, rA(ctx->opcode), false);
        tcg_gen_mulu2_i64(t0, t1, avr, ten);
        get_avr64(avr, rB(ctx->opcode), false);
        tcg_gen_andi_i64(t2, avr, 0xF);
        tcg_gen_add2_i64(avr, t2, t0, t1, t2, z);
        set_avr64(rD(ctx->opcode), avr, false);
    } else {
        get_avr64(avr, rA(ctx->opcode), false);
        tcg_gen_mulu2_i64(avr, t2, avr, ten);
        set_avr64(rD(ctx->opcode), avr, false);
    }

    if (ret_carry) {
        get_avr64(avr, rA(ctx->opcode), true);
        tcg_gen_mulu2_i64(t0, t1, avr, ten);
        tcg_gen_add2_i64(t0, avr, t0, t1, t2, z);
        set_avr64(rD(ctx->opcode), avr, false);
        set_avr64(rD(ctx->opcode), z, true);
    } else {
        get_avr64(avr, rA(ctx->opcode), true);
        tcg_gen_mul_i64(t0, avr, ten);
        tcg_gen_add_i64(avr, t0, t2);
        set_avr64(rD(ctx->opcode), avr, true);
    }
}

#define GEN_VX_VMUL10(name, add_cin, ret_carry)                         \
    static void glue(gen_, name)(DisasContext *ctx)                     \
    { gen_vx_vmul10(ctx, add_cin, ret_carry); }

GEN_VX_VMUL10(vmul10uq, 0, 0);
GEN_VX_VMUL10(vmul10euq, 1, 0);
GEN_VX_VMUL10(vmul10cuq, 0, 1);
GEN_VX_VMUL10(vmul10ecuq, 1, 1);

#define GEN_VXFORM_V(name, vece, tcg_op, opc2, opc3)                    \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
                                                                        \
    tcg_op(vece,                                                        \
           avr_full_offset(rD(ctx->opcode)),                            \
           avr_full_offset(rA(ctx->opcode)),                            \
           avr_full_offset(rB(ctx->opcode)),                            \
           16, 16);                                                     \
}

#define GEN_VXFORM(name, opc2, opc3)                                    \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_ptr ra, rb, rd;                                                \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    ra = gen_avr_ptr(rA(ctx->opcode));                                  \
    rb = gen_avr_ptr(rB(ctx->opcode));                                  \
    rd = gen_avr_ptr(rD(ctx->opcode));                                  \
    gen_helper_##name(rd, ra, rb);                                      \
}

#define GEN_VXFORM_TRANS(name, opc2, opc3)                              \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    trans_##name(ctx);                                                  \
}

#define GEN_VXFORM_ENV(name, opc2, opc3)                                \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_ptr ra, rb, rd;                                                \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    ra = gen_avr_ptr(rA(ctx->opcode));                                  \
    rb = gen_avr_ptr(rB(ctx->opcode));                                  \
    rd = gen_avr_ptr(rD(ctx->opcode));                                  \
    gen_helper_##name(tcg_env, rd, ra, rb);                             \
}

#define GEN_VXFORM3(name, opc2, opc3)                                   \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_ptr ra, rb, rc, rd;                                            \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    ra = gen_avr_ptr(rA(ctx->opcode));                                  \
    rb = gen_avr_ptr(rB(ctx->opcode));                                  \
    rc = gen_avr_ptr(rC(ctx->opcode));                                  \
    rd = gen_avr_ptr(rD(ctx->opcode));                                  \
    gen_helper_##name(rd, ra, rb, rc);                                  \
}

/*
 * Support for Altivec instruction pairs that use bit 31 (Rc) as
 * an opcode bit.  In general, these pairs come from different
 * versions of the ISA, so we must also support a pair of flags for
 * each instruction.
 */
#define GEN_VXFORM_DUAL(name0, flg0, flg2_0, name1, flg1, flg2_1)          \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)             \
{                                                                      \
    if ((Rc(ctx->opcode) == 0) &&                                      \
        ((ctx->insns_flags & flg0) || (ctx->insns_flags2 & flg2_0))) { \
        gen_##name0(ctx);                                              \
    } else if ((Rc(ctx->opcode) == 1) &&                               \
        ((ctx->insns_flags & flg1) || (ctx->insns_flags2 & flg2_1))) { \
        gen_##name1(ctx);                                              \
    } else {                                                           \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);            \
    }                                                                  \
}

/*
 * We use this macro if one instruction is realized with direct
 * translation, and second one with helper.
 */
#define GEN_VXFORM_TRANS_DUAL(name0, flg0, flg2_0, name1, flg1, flg2_1)\
static void glue(gen_, name0##_##name1)(DisasContext *ctx)             \
{                                                                      \
    if ((Rc(ctx->opcode) == 0) &&                                      \
        ((ctx->insns_flags & flg0) || (ctx->insns_flags2 & flg2_0))) { \
        if (unlikely(!ctx->altivec_enabled)) {                         \
            gen_exception(ctx, POWERPC_EXCP_VPU);                      \
            return;                                                    \
        }                                                              \
        trans_##name0(ctx);                                            \
    } else if ((Rc(ctx->opcode) == 1) &&                               \
        ((ctx->insns_flags & flg1) || (ctx->insns_flags2 & flg2_1))) { \
        gen_##name1(ctx);                                              \
    } else {                                                           \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);            \
    }                                                                  \
}

/* Adds support to provide invalid mask */
#define GEN_VXFORM_DUAL_EXT(name0, flg0, flg2_0, inval0,                \
                            name1, flg1, flg2_1, inval1)                \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)              \
{                                                                       \
    if ((Rc(ctx->opcode) == 0) &&                                       \
        ((ctx->insns_flags & flg0) || (ctx->insns_flags2 & flg2_0)) &&  \
        !(ctx->opcode & inval0)) {                                      \
        gen_##name0(ctx);                                               \
    } else if ((Rc(ctx->opcode) == 1) &&                                \
               ((ctx->insns_flags & flg1) || (ctx->insns_flags2 & flg2_1)) && \
               !(ctx->opcode & inval1)) {                               \
        gen_##name1(ctx);                                               \
    } else {                                                            \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);             \
    }                                                                   \
}

#define GEN_VXFORM_HETRO(name, opc2, opc3)                              \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_ptr rb;                                                        \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    rb = gen_avr_ptr(rB(ctx->opcode));                                  \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], rb); \
}

GEN_VXFORM_V(vaddubm, MO_8, tcg_gen_gvec_add, 0, 0);
GEN_VXFORM_DUAL_EXT(vaddubm, PPC_ALTIVEC, PPC_NONE, 0,       \
                    vmul10cuq, PPC_NONE, PPC2_ISA300, 0x0000F800)
GEN_VXFORM_V(vadduhm, MO_16, tcg_gen_gvec_add, 0, 1);
GEN_VXFORM_DUAL(vadduhm, PPC_ALTIVEC, PPC_NONE,  \
                vmul10ecuq, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_V(vadduwm, MO_32, tcg_gen_gvec_add, 0, 2);
GEN_VXFORM_V(vaddudm, MO_64, tcg_gen_gvec_add, 0, 3);
GEN_VXFORM_V(vsububm, MO_8, tcg_gen_gvec_sub, 0, 16);
GEN_VXFORM_V(vsubuhm, MO_16, tcg_gen_gvec_sub, 0, 17);
GEN_VXFORM_V(vsubuwm, MO_32, tcg_gen_gvec_sub, 0, 18);
GEN_VXFORM_V(vsubudm, MO_64, tcg_gen_gvec_sub, 0, 19);
GEN_VXFORM(vmrghb, 6, 0);
GEN_VXFORM(vmrghh, 6, 1);
GEN_VXFORM(vmrghw, 6, 2);
GEN_VXFORM(vmrglb, 6, 4);
GEN_VXFORM(vmrglh, 6, 5);
GEN_VXFORM(vmrglw, 6, 6);

static void trans_vmrgew(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VA = rA(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGv_i64 avr = tcg_temp_new_i64();

    get_avr64(avr, VB, true);
    tcg_gen_shri_i64(tmp, avr, 32);
    get_avr64(avr, VA, true);
    tcg_gen_deposit_i64(avr, avr, tmp, 0, 32);
    set_avr64(VT, avr, true);

    get_avr64(avr, VB, false);
    tcg_gen_shri_i64(tmp, avr, 32);
    get_avr64(avr, VA, false);
    tcg_gen_deposit_i64(avr, avr, tmp, 0, 32);
    set_avr64(VT, avr, false);
}

static void trans_vmrgow(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VA = rA(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 avr = tcg_temp_new_i64();

    get_avr64(t0, VB, true);
    get_avr64(t1, VA, true);
    tcg_gen_deposit_i64(avr, t0, t1, 32, 32);
    set_avr64(VT, avr, true);

    get_avr64(t0, VB, false);
    get_avr64(t1, VA, false);
    tcg_gen_deposit_i64(avr, t0, t1, 32, 32);
    set_avr64(VT, avr, false);
}

/*
 * lvsl VRT,RA,RB - Load Vector for Shift Left
 *
 * Let the EA be the sum (rA|0)+(rB). Let sh=EA[28–31].
 * Let X be the 32-byte value 0x00 || 0x01 || 0x02 || ... || 0x1E || 0x1F.
 * Bytes sh:sh+15 of X are placed into vD.
 */
static bool trans_LVSL(DisasContext *ctx, arg_LVSL *a)
{
    TCGv_i64 result = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv EA = tcg_temp_new();

    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);

    /* Get sh(from description) by anding EA with 0xf. */
    EA = do_ea_calc(ctx, a->ra, cpu_gpr[a->rb]);
    tcg_gen_extu_tl_i64(sh, EA);
    tcg_gen_andi_i64(sh, sh, 0xfULL);

    /*
     * Create bytes sh:sh+7 of X(from description) and place them in
     * higher doubleword of vD.
     */
    tcg_gen_muli_i64(sh, sh, 0x0101010101010101ULL);
    tcg_gen_addi_i64(result, sh, 0x0001020304050607ull);
    set_avr64(a->rt, result, true);
    /*
     * Create bytes sh+8:sh+15 of X(from description) and place them in
     * lower doubleword of vD.
     */
    tcg_gen_addi_i64(result, sh, 0x08090a0b0c0d0e0fULL);
    set_avr64(a->rt, result, false);
    return true;
}

/*
 * lvsr VRT,RA,RB - Load Vector for Shift Right
 *
 * Let the EA be the sum (rA|0)+(rB). Let sh=EA[28–31].
 * Let X be the 32-byte value 0x00 || 0x01 || 0x02 || ... || 0x1E || 0x1F.
 * Bytes (16-sh):(31-sh) of X are placed into vD.
 */
static bool trans_LVSR(DisasContext *ctx, arg_LVSR *a)
{
    TCGv_i64 result = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv EA = tcg_temp_new();

    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);

    /* Get sh(from description) by anding EA with 0xf. */
    EA = do_ea_calc(ctx, a->ra, cpu_gpr[a->rb]);
    tcg_gen_extu_tl_i64(sh, EA);
    tcg_gen_andi_i64(sh, sh, 0xfULL);

    /*
     * Create bytes (16-sh):(23-sh) of X(from description) and place them in
     * higher doubleword of vD.
     */
    tcg_gen_muli_i64(sh, sh, 0x0101010101010101ULL);
    tcg_gen_subfi_i64(result, 0x1011121314151617ULL, sh);
    set_avr64(a->rt, result, true);
    /*
     * Create bytes (24-sh):(32-sh) of X(from description) and place them in
     * lower doubleword of vD.
     */
    tcg_gen_subfi_i64(result, 0x18191a1b1c1d1e1fULL, sh);
    set_avr64(a->rt, result, false);
    return true;
}

/*
 * vsl VRT,VRA,VRB - Vector Shift Left
 *
 * Shifting left 128 bit value of vA by value specified in bits 125-127 of vB.
 * Lowest 3 bits in each byte element of register vB must be identical or
 * result is undefined.
 */
static void trans_vsl(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VA = rA(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 avr = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv_i64 carry = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* Place bits 125-127 of vB in 'sh'. */
    get_avr64(avr, VB, false);
    tcg_gen_andi_i64(sh, avr, 0x07ULL);

    /*
     * Save highest 'sh' bits of lower doubleword element of vA in variable
     * 'carry' and perform shift on lower doubleword.
     */
    get_avr64(avr, VA, false);
    tcg_gen_subfi_i64(tmp, 32, sh);
    tcg_gen_shri_i64(carry, avr, 32);
    tcg_gen_shr_i64(carry, carry, tmp);
    tcg_gen_shl_i64(avr, avr, sh);
    set_avr64(VT, avr, false);

    /*
     * Perform shift on higher doubleword element of vA and replace lowest
     * 'sh' bits with 'carry'.
     */
    get_avr64(avr, VA, true);
    tcg_gen_shl_i64(avr, avr, sh);
    tcg_gen_or_i64(avr, avr, carry);
    set_avr64(VT, avr, true);
}

/*
 * vsr VRT,VRA,VRB - Vector Shift Right
 *
 * Shifting right 128 bit value of vA by value specified in bits 125-127 of vB.
 * Lowest 3 bits in each byte element of register vB must be identical or
 * result is undefined.
 */
static void trans_vsr(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VA = rA(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 avr = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv_i64 carry = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* Place bits 125-127 of vB in 'sh'. */
    get_avr64(avr, VB, false);
    tcg_gen_andi_i64(sh, avr, 0x07ULL);

    /*
     * Save lowest 'sh' bits of higher doubleword element of vA in variable
     * 'carry' and perform shift on higher doubleword.
     */
    get_avr64(avr, VA, true);
    tcg_gen_subfi_i64(tmp, 32, sh);
    tcg_gen_shli_i64(carry, avr, 32);
    tcg_gen_shl_i64(carry, carry, tmp);
    tcg_gen_shr_i64(avr, avr, sh);
    set_avr64(VT, avr, true);
    /*
     * Perform shift on lower doubleword element of vA and replace highest
     * 'sh' bits with 'carry'.
     */
    get_avr64(avr, VA, false);
    tcg_gen_shr_i64(avr, avr, sh);
    tcg_gen_or_i64(avr, avr, carry);
    set_avr64(VT, avr, false);
}

/*
 * vgbbd VRT,VRB - Vector Gather Bits by Bytes by Doubleword
 *
 * All ith bits (i in range 1 to 8) of each byte of doubleword element in source
 * register are concatenated and placed into ith byte of appropriate doubleword
 * element in destination register.
 *
 * Following solution is done for both doubleword elements of source register
 * in parallel, in order to reduce the number of instructions needed(that's why
 * arrays are used):
 * First, both doubleword elements of source register vB are placed in
 * appropriate element of array avr. Bits are gathered in 2x8 iterations(2 for
 * loops). In first iteration bit 1 of byte 1, bit 2 of byte 2,... bit 8 of
 * byte 8 are in their final spots so avr[i], i={0,1} can be and-ed with
 * tcg_mask. For every following iteration, both avr[i] and tcg_mask variables
 * have to be shifted right for 7 and 8 places, respectively, in order to get
 * bit 1 of byte 2, bit 2 of byte 3.. bit 7 of byte 8 in their final spots so
 * shifted avr values(saved in tmp) can be and-ed with new value of tcg_mask...
 * After first 8 iteration(first loop), all the first bits are in their final
 * places, all second bits but second bit from eight byte are in their places...
 * only 1 eight bit from eight byte is in it's place). In second loop we do all
 * operations symmetrically, in order to get other half of bits in their final
 * spots. Results for first and second doubleword elements are saved in
 * result[0] and result[1] respectively. In the end those results are saved in
 * appropriate doubleword element of destination register vD.
 */
static void trans_vgbbd(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 tmp = tcg_temp_new_i64();
    uint64_t mask = 0x8040201008040201ULL;
    int i, j;

    TCGv_i64 result[2];
    result[0] = tcg_temp_new_i64();
    result[1] = tcg_temp_new_i64();
    TCGv_i64 avr[2];
    avr[0] = tcg_temp_new_i64();
    avr[1] = tcg_temp_new_i64();
    TCGv_i64 tcg_mask = tcg_temp_new_i64();

    tcg_gen_movi_i64(tcg_mask, mask);
    for (j = 0; j < 2; j++) {
        get_avr64(avr[j], VB, j);
        tcg_gen_and_i64(result[j], avr[j], tcg_mask);
    }
    for (i = 1; i < 8; i++) {
        tcg_gen_movi_i64(tcg_mask, mask >> (i * 8));
        for (j = 0; j < 2; j++) {
            tcg_gen_shri_i64(tmp, avr[j], i * 7);
            tcg_gen_and_i64(tmp, tmp, tcg_mask);
            tcg_gen_or_i64(result[j], result[j], tmp);
        }
    }
    for (i = 1; i < 8; i++) {
        tcg_gen_movi_i64(tcg_mask, mask << (i * 8));
        for (j = 0; j < 2; j++) {
            tcg_gen_shli_i64(tmp, avr[j], i * 7);
            tcg_gen_and_i64(tmp, tmp, tcg_mask);
            tcg_gen_or_i64(result[j], result[j], tmp);
        }
    }
    for (j = 0; j < 2; j++) {
        set_avr64(VT, result[j], j);
    }
}

/*
 * vclzw VRT,VRB - Vector Count Leading Zeros Word
 *
 * Counting the number of leading zero bits of each word element in source
 * register and placing result in appropriate word element of destination
 * register.
 */
static void trans_vclzw(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i32 tmp = tcg_temp_new_i32();
    int i;

    /* Perform count for every word element using tcg_gen_clzi_i32. */
    for (i = 0; i < 4; i++) {
        tcg_gen_ld_i32(tmp, tcg_env,
            offsetof(CPUPPCState, vsr[32 + VB].u64[0]) + i * 4);
        tcg_gen_clzi_i32(tmp, tmp, 32);
        tcg_gen_st_i32(tmp, tcg_env,
            offsetof(CPUPPCState, vsr[32 + VT].u64[0]) + i * 4);
    }
}

/*
 * vclzd VRT,VRB - Vector Count Leading Zeros Doubleword
 *
 * Counting the number of leading zero bits of each doubleword element in source
 * register and placing result in appropriate doubleword element of destination
 * register.
 */
static void trans_vclzd(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    int VB = rB(ctx->opcode);
    TCGv_i64 avr = tcg_temp_new_i64();

    /* high doubleword */
    get_avr64(avr, VB, true);
    tcg_gen_clzi_i64(avr, avr, 64);
    set_avr64(VT, avr, true);

    /* low doubleword */
    get_avr64(avr, VB, false);
    tcg_gen_clzi_i64(avr, avr, 64);
    set_avr64(VT, avr, false);
}

GEN_VXFORM_V(vmuluwm, MO_32, tcg_gen_gvec_mul, 4, 2);
GEN_VXFORM(vsrv, 2, 28);
GEN_VXFORM(vslv, 2, 29);
GEN_VXFORM(vslo, 6, 16);
GEN_VXFORM(vsro, 6, 17);

static bool do_vector_gvec3_VX(DisasContext *ctx, arg_VX *a, int vece,
                               void (*gen_gvec)(unsigned, uint32_t, uint32_t,
                                                uint32_t, uint32_t, uint32_t))
{
    REQUIRE_VECTOR(ctx);

    gen_gvec(vece, avr_full_offset(a->vrt), avr_full_offset(a->vra),
             avr_full_offset(a->vrb), 16, 16);

    return true;
}

TRANS_FLAGS(ALTIVEC, VSLB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_shlv);
TRANS_FLAGS(ALTIVEC, VSLH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_shlv);
TRANS_FLAGS(ALTIVEC, VSLW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_shlv);
TRANS_FLAGS2(ALTIVEC_207, VSLD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_shlv);

TRANS_FLAGS(ALTIVEC, VSRB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_shrv);
TRANS_FLAGS(ALTIVEC, VSRH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_shrv);
TRANS_FLAGS(ALTIVEC, VSRW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_shrv);
TRANS_FLAGS2(ALTIVEC_207, VSRD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_shrv);

TRANS_FLAGS(ALTIVEC, VSRAB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_sarv);
TRANS_FLAGS(ALTIVEC, VSRAH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_sarv);
TRANS_FLAGS(ALTIVEC, VSRAW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_sarv);
TRANS_FLAGS2(ALTIVEC_207, VSRAD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_sarv);

TRANS_FLAGS(ALTIVEC, VRLB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_rotlv)
TRANS_FLAGS(ALTIVEC, VRLH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_rotlv)
TRANS_FLAGS(ALTIVEC, VRLW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_rotlv)
TRANS_FLAGS2(ALTIVEC_207, VRLD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_rotlv)

/* Logical operations */
TRANS_FLAGS(ALTIVEC, VAND, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_and);
TRANS_FLAGS(ALTIVEC, VANDC, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_andc);
TRANS_FLAGS(ALTIVEC, VOR, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_or);
TRANS_FLAGS(ALTIVEC, VXOR, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_xor);
TRANS_FLAGS(ALTIVEC, VNOR, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_nor);
TRANS_FLAGS2(ALTIVEC_207, VEQV, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_eqv);
TRANS_FLAGS2(ALTIVEC_207, VNAND, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_nand);
TRANS_FLAGS2(ALTIVEC_207, VORC, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_orc);

/* Integer Max/Min operations */
TRANS_FLAGS(ALTIVEC, VMAXUB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_umax);
TRANS_FLAGS(ALTIVEC, VMAXUH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_umax);
TRANS_FLAGS(ALTIVEC, VMAXUW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_umax);
TRANS_FLAGS2(ALTIVEC_207, VMAXUD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_umax);

TRANS_FLAGS(ALTIVEC, VMAXSB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_smax);
TRANS_FLAGS(ALTIVEC, VMAXSH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_smax);
TRANS_FLAGS(ALTIVEC, VMAXSW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_smax);
TRANS_FLAGS2(ALTIVEC_207, VMAXSD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_smax);

TRANS_FLAGS(ALTIVEC, VMINUB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_umin);
TRANS_FLAGS(ALTIVEC, VMINUH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_umin);
TRANS_FLAGS(ALTIVEC, VMINUW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_umin);
TRANS_FLAGS2(ALTIVEC_207, VMINUD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_umin);

TRANS_FLAGS(ALTIVEC, VMINSB, do_vector_gvec3_VX, MO_8, tcg_gen_gvec_smin);
TRANS_FLAGS(ALTIVEC, VMINSH, do_vector_gvec3_VX, MO_16, tcg_gen_gvec_smin);
TRANS_FLAGS(ALTIVEC, VMINSW, do_vector_gvec3_VX, MO_32, tcg_gen_gvec_smin);
TRANS_FLAGS2(ALTIVEC_207, VMINSD, do_vector_gvec3_VX, MO_64, tcg_gen_gvec_smin);

static TCGv_vec do_vrl_mask_vec(unsigned vece, TCGv_vec vrb)
{
    TCGv_vec t0 = tcg_temp_new_vec_matching(vrb),
             t1 = tcg_temp_new_vec_matching(vrb),
             t2 = tcg_temp_new_vec_matching(vrb),
             ones = tcg_constant_vec_matching(vrb, vece, -1);

    /* Extract b and e */
    tcg_gen_dupi_vec(vece, t2, (8 << vece) - 1);

    tcg_gen_shri_vec(vece, t0, vrb, 16);
    tcg_gen_and_vec(vece, t0, t0, t2);

    tcg_gen_shri_vec(vece, t1, vrb, 8);
    tcg_gen_and_vec(vece, t1, t1, t2);

    /* Compare b and e to negate the mask where begin > end */
    tcg_gen_cmp_vec(TCG_COND_GT, vece, t2, t0, t1);

    /* Create the mask with (~0 >> b) ^ ((~0 >> e) >> 1) */
    tcg_gen_shrv_vec(vece, t0, ones, t0);
    tcg_gen_shrv_vec(vece, t1, ones, t1);
    tcg_gen_shri_vec(vece, t1, t1, 1);
    tcg_gen_xor_vec(vece, t0, t0, t1);

    /* negate the mask */
    tcg_gen_xor_vec(vece, t0, t0, t2);

    return t0;
}

static void gen_vrlnm_vec(unsigned vece, TCGv_vec vrt, TCGv_vec vra,
                          TCGv_vec vrb)
{
    TCGv_vec mask, n = tcg_temp_new_vec_matching(vrt);

    /* Create the mask */
    mask = do_vrl_mask_vec(vece, vrb);

    /* Extract n */
    tcg_gen_dupi_vec(vece, n, (8 << vece) - 1);
    tcg_gen_and_vec(vece, n, vrb, n);

    /* Rotate and mask */
    tcg_gen_rotlv_vec(vece, vrt, vra, n);
    tcg_gen_and_vec(vece, vrt, vrt, mask);
}

static bool do_vrlnm(DisasContext *ctx, arg_VX *a, int vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_cmp_vec, INDEX_op_rotlv_vec, INDEX_op_sari_vec,
        INDEX_op_shli_vec, INDEX_op_shri_vec, INDEX_op_shrv_vec, 0
    };
    static const GVecGen3 ops[2] = {
        {
            .fniv = gen_vrlnm_vec,
            .fno = gen_helper_VRLWNM,
            .opt_opc = vecop_list,
            .load_dest = true,
            .vece = MO_32
        },
        {
            .fniv = gen_vrlnm_vec,
            .fno = gen_helper_VRLDNM,
            .opt_opc = vecop_list,
            .load_dest = true,
            .vece = MO_64
        }
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &ops[vece - 2]);

    return true;
}

TRANS(VRLWNM, do_vrlnm, MO_32)
TRANS(VRLDNM, do_vrlnm, MO_64)

static void gen_vrlmi_vec(unsigned vece, TCGv_vec vrt, TCGv_vec vra,
                          TCGv_vec vrb)
{
    TCGv_vec mask, n = tcg_temp_new_vec_matching(vrt),
             tmp = tcg_temp_new_vec_matching(vrt);

    /* Create the mask */
    mask = do_vrl_mask_vec(vece, vrb);

    /* Extract n */
    tcg_gen_dupi_vec(vece, n, (8 << vece) - 1);
    tcg_gen_and_vec(vece, n, vrb, n);

    /* Rotate and insert */
    tcg_gen_rotlv_vec(vece, tmp, vra, n);
    tcg_gen_bitsel_vec(vece, vrt, mask, tmp, vrt);
}

static bool do_vrlmi(DisasContext *ctx, arg_VX *a, int vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_cmp_vec, INDEX_op_rotlv_vec, INDEX_op_sari_vec,
        INDEX_op_shli_vec, INDEX_op_shri_vec, INDEX_op_shrv_vec, 0
    };
    static const GVecGen3 ops[2] = {
        {
            .fniv = gen_vrlmi_vec,
            .fno = gen_helper_VRLWMI,
            .opt_opc = vecop_list,
            .load_dest = true,
            .vece = MO_32
        },
        {
            .fniv = gen_vrlnm_vec,
            .fno = gen_helper_VRLDMI,
            .opt_opc = vecop_list,
            .load_dest = true,
            .vece = MO_64
        }
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VSX(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &ops[vece - 2]);

    return true;
}

TRANS(VRLWMI, do_vrlmi, MO_32)
TRANS(VRLDMI, do_vrlmi, MO_64)

static bool do_vector_shift_quad(DisasContext *ctx, arg_VX *a, bool right,
                                 bool alg)
{
    TCGv_i64 hi, lo, t0, t1, n, zero = tcg_constant_i64(0);

    REQUIRE_VECTOR(ctx);

    n = tcg_temp_new_i64();
    hi = tcg_temp_new_i64();
    lo = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();

    get_avr64(lo, a->vra, false);
    get_avr64(hi, a->vra, true);

    get_avr64(n, a->vrb, true);

    tcg_gen_andi_i64(t0, n, 64);
    if (right) {
        tcg_gen_movcond_i64(TCG_COND_NE, lo, t0, zero, hi, lo);
        if (alg) {
            t1 = tcg_temp_new_i64();
            tcg_gen_sari_i64(t1, lo, 63);
        } else {
            t1 = zero;
        }
        tcg_gen_movcond_i64(TCG_COND_NE, hi, t0, zero, t1, hi);
    } else {
        tcg_gen_movcond_i64(TCG_COND_NE, hi, t0, zero, lo, hi);
        tcg_gen_movcond_i64(TCG_COND_NE, lo, t0, zero, zero, lo);
    }
    tcg_gen_andi_i64(n, n, 0x3F);

    if (right) {
        if (alg) {
            tcg_gen_sar_i64(t0, hi, n);
        } else {
            tcg_gen_shr_i64(t0, hi, n);
        }
    } else {
        tcg_gen_shl_i64(t0, lo, n);
    }
    set_avr64(a->vrt, t0, right);

    if (right) {
        tcg_gen_shr_i64(lo, lo, n);
    } else {
        tcg_gen_shl_i64(hi, hi, n);
    }
    tcg_gen_xori_i64(n, n, 63);
    if (right) {
        tcg_gen_shl_i64(hi, hi, n);
        tcg_gen_shli_i64(hi, hi, 1);
    } else {
        tcg_gen_shr_i64(lo, lo, n);
        tcg_gen_shri_i64(lo, lo, 1);
    }
    tcg_gen_or_i64(hi, hi, lo);
    set_avr64(a->vrt, hi, !right);
    return true;
}

TRANS_FLAGS2(ISA310, VSLQ, do_vector_shift_quad, false, false);
TRANS_FLAGS2(ISA310, VSRQ, do_vector_shift_quad, true, false);
TRANS_FLAGS2(ISA310, VSRAQ, do_vector_shift_quad, true, true);

static void do_vrlq_mask(TCGv_i64 mh, TCGv_i64 ml, TCGv_i64 b, TCGv_i64 e)
{
    TCGv_i64 th, tl, t0, t1, zero = tcg_constant_i64(0),
             ones = tcg_constant_i64(-1);

    th = tcg_temp_new_i64();
    tl = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    /* m = ~0 >> b */
    tcg_gen_andi_i64(t0, b, 64);
    tcg_gen_movcond_i64(TCG_COND_NE, t1, t0, zero, zero, ones);
    tcg_gen_andi_i64(t0, b, 0x3F);
    tcg_gen_shr_i64(mh, t1, t0);
    tcg_gen_shr_i64(ml, ones, t0);
    tcg_gen_xori_i64(t0, t0, 63);
    tcg_gen_shl_i64(t1, t1, t0);
    tcg_gen_shli_i64(t1, t1, 1);
    tcg_gen_or_i64(ml, t1, ml);

    /* t = ~0 >> e */
    tcg_gen_andi_i64(t0, e, 64);
    tcg_gen_movcond_i64(TCG_COND_NE, t1, t0, zero, zero, ones);
    tcg_gen_andi_i64(t0, e, 0x3F);
    tcg_gen_shr_i64(th, t1, t0);
    tcg_gen_shr_i64(tl, ones, t0);
    tcg_gen_xori_i64(t0, t0, 63);
    tcg_gen_shl_i64(t1, t1, t0);
    tcg_gen_shli_i64(t1, t1, 1);
    tcg_gen_or_i64(tl, t1, tl);

    /* t = t >> 1 */
    tcg_gen_extract2_i64(tl, tl, th, 1);
    tcg_gen_shri_i64(th, th, 1);

    /* m = m ^ t */
    tcg_gen_xor_i64(mh, mh, th);
    tcg_gen_xor_i64(ml, ml, tl);

    /* Negate the mask if begin > end */
    tcg_gen_movcond_i64(TCG_COND_GT, t0, b, e, ones, zero);

    tcg_gen_xor_i64(mh, mh, t0);
    tcg_gen_xor_i64(ml, ml, t0);
}

static bool do_vector_rotl_quad(DisasContext *ctx, arg_VX *a, bool mask,
                                bool insert)
{
    TCGv_i64 ah, al, vrb, n, t0, t1, zero = tcg_constant_i64(0);

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    ah = tcg_temp_new_i64();
    al = tcg_temp_new_i64();
    vrb = tcg_temp_new_i64();
    n = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    get_avr64(ah, a->vra, true);
    get_avr64(al, a->vra, false);
    get_avr64(vrb, a->vrb, true);

    tcg_gen_mov_i64(t0, ah);
    tcg_gen_andi_i64(t1, vrb, 64);
    tcg_gen_movcond_i64(TCG_COND_NE, ah, t1, zero, al, ah);
    tcg_gen_movcond_i64(TCG_COND_NE, al, t1, zero, t0, al);
    tcg_gen_andi_i64(n, vrb, 0x3F);

    tcg_gen_shl_i64(t0, ah, n);
    tcg_gen_shl_i64(t1, al, n);

    tcg_gen_xori_i64(n, n, 63);

    tcg_gen_shr_i64(al, al, n);
    tcg_gen_shri_i64(al, al, 1);
    tcg_gen_or_i64(t0, al, t0);

    tcg_gen_shr_i64(ah, ah, n);
    tcg_gen_shri_i64(ah, ah, 1);
    tcg_gen_or_i64(t1, ah, t1);

    if (mask || insert) {
        tcg_gen_extract_i64(n, vrb, 8, 7);
        tcg_gen_extract_i64(vrb, vrb, 16, 7);

        do_vrlq_mask(ah, al, vrb, n);

        tcg_gen_and_i64(t0, t0, ah);
        tcg_gen_and_i64(t1, t1, al);

        if (insert) {
            get_avr64(n, a->vrt, true);
            get_avr64(vrb, a->vrt, false);
            tcg_gen_andc_i64(n, n, ah);
            tcg_gen_andc_i64(vrb, vrb, al);
            tcg_gen_or_i64(t0, t0, n);
            tcg_gen_or_i64(t1, t1, vrb);
        }
    }

    set_avr64(a->vrt, t0, true);
    set_avr64(a->vrt, t1, false);
    return true;
}

TRANS(VRLQ, do_vector_rotl_quad, false, false)
TRANS(VRLQNM, do_vector_rotl_quad, true, false)
TRANS(VRLQMI, do_vector_rotl_quad, false, true)

GEN_VXFORM_TRANS(vsl, 2, 7);
GEN_VXFORM_TRANS(vsr, 2, 11);
GEN_VXFORM_ENV(vpkuhum, 7, 0);
GEN_VXFORM_ENV(vpkuwum, 7, 1);
GEN_VXFORM_ENV(vpkudum, 7, 17);
GEN_VXFORM_ENV(vpkuhus, 7, 2);
GEN_VXFORM_ENV(vpkuwus, 7, 3);
GEN_VXFORM_ENV(vpkudus, 7, 19);
GEN_VXFORM_ENV(vpkshus, 7, 4);
GEN_VXFORM_ENV(vpkswus, 7, 5);
GEN_VXFORM_ENV(vpksdus, 7, 21);
GEN_VXFORM_ENV(vpkshss, 7, 6);
GEN_VXFORM_ENV(vpkswss, 7, 7);
GEN_VXFORM_ENV(vpksdss, 7, 23);
GEN_VXFORM(vpkpx, 7, 12);
GEN_VXFORM_ENV(vsum4ubs, 4, 24);
GEN_VXFORM_ENV(vsum4sbs, 4, 28);
GEN_VXFORM_ENV(vsum4shs, 4, 25);
GEN_VXFORM_ENV(vsum2sws, 4, 26);
GEN_VXFORM_ENV(vsumsws, 4, 30);
GEN_VXFORM_ENV(vaddfp, 5, 0);
GEN_VXFORM_ENV(vsubfp, 5, 1);
GEN_VXFORM_ENV(vmaxfp, 5, 16);
GEN_VXFORM_ENV(vminfp, 5, 17);
GEN_VXFORM_HETRO(vextublx, 6, 24)
GEN_VXFORM_HETRO(vextuhlx, 6, 25)
GEN_VXFORM_HETRO(vextuwlx, 6, 26)
GEN_VXFORM_TRANS_DUAL(vmrgow, PPC_NONE, PPC2_ALTIVEC_207,
                vextuwlx, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_HETRO(vextubrx, 6, 28)
GEN_VXFORM_HETRO(vextuhrx, 6, 29)
GEN_VXFORM_HETRO(vextuwrx, 6, 30)
GEN_VXFORM_TRANS_DUAL(vmrgew, PPC_NONE, PPC2_ALTIVEC_207,
                vextuwrx, PPC_NONE, PPC2_ISA300)

#define GEN_VXRFORM1(opname, name, str, opc2, opc3)                     \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr ra, rb, rd;                                            \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        ra = gen_avr_ptr(rA(ctx->opcode));                              \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##opname(tcg_env, rd, ra, rb);                       \
    }

#define GEN_VXRFORM(name, opc2, opc3)                                \
    GEN_VXRFORM1(name, name, #name, opc2, opc3)                      \
    GEN_VXRFORM1(name##_dot, name##_, #name ".", opc2, (opc3 | (0x1 << 4)))

/*
 * Support for Altivec instructions that use bit 31 (Rc) as an opcode
 * bit but also use bit 21 as an actual Rc bit.  In general, these pairs
 * come from different versions of the ISA, so we must also support a
 * pair of flags for each instruction.
 */
#define GEN_VXRFORM_DUAL(name0, flg0, flg2_0, name1, flg1, flg2_1)     \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)             \
{                                                                      \
    if ((Rc(ctx->opcode) == 0) &&                                      \
        ((ctx->insns_flags & flg0) || (ctx->insns_flags2 & flg2_0))) { \
        if (Rc21(ctx->opcode) == 0) {                                  \
            gen_##name0(ctx);                                          \
        } else {                                                       \
            gen_##name0##_(ctx);                                       \
        }                                                              \
    } else if ((Rc(ctx->opcode) == 1) &&                               \
        ((ctx->insns_flags & flg1) || (ctx->insns_flags2 & flg2_1))) { \
        if (Rc21(ctx->opcode) == 0) {                                  \
            gen_##name1(ctx);                                          \
        } else {                                                       \
            gen_##name1##_(ctx);                                       \
        }                                                              \
    } else {                                                           \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);            \
    }                                                                  \
}

static void do_vcmp_rc(int vrt)
{
    TCGv_i64 tmp, set, clr;

    tmp = tcg_temp_new_i64();
    set = tcg_temp_new_i64();
    clr = tcg_temp_new_i64();

    get_avr64(tmp, vrt, true);
    tcg_gen_mov_i64(set, tmp);
    get_avr64(tmp, vrt, false);
    tcg_gen_or_i64(clr, set, tmp);
    tcg_gen_and_i64(set, set, tmp);

    tcg_gen_setcondi_i64(TCG_COND_EQ, clr, clr, 0);
    tcg_gen_shli_i64(clr, clr, 1);

    tcg_gen_setcondi_i64(TCG_COND_EQ, set, set, -1);
    tcg_gen_shli_i64(set, set, 3);

    tcg_gen_or_i64(tmp, set, clr);
    tcg_gen_extrl_i64_i32(cpu_crf[6], tmp);
}

static bool do_vcmp(DisasContext *ctx, arg_VC *a, TCGCond cond, int vece)
{
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_cmp(cond, vece, avr_full_offset(a->vrt),
                     avr_full_offset(a->vra), avr_full_offset(a->vrb), 16, 16);

    if (a->rc) {
        do_vcmp_rc(a->vrt);
    }

    return true;
}

TRANS_FLAGS(ALTIVEC, VCMPEQUB, do_vcmp, TCG_COND_EQ, MO_8)
TRANS_FLAGS(ALTIVEC, VCMPEQUH, do_vcmp, TCG_COND_EQ, MO_16)
TRANS_FLAGS(ALTIVEC, VCMPEQUW, do_vcmp, TCG_COND_EQ, MO_32)
TRANS_FLAGS2(ALTIVEC_207, VCMPEQUD, do_vcmp, TCG_COND_EQ, MO_64)

TRANS_FLAGS(ALTIVEC, VCMPGTSB, do_vcmp, TCG_COND_GT, MO_8)
TRANS_FLAGS(ALTIVEC, VCMPGTSH, do_vcmp, TCG_COND_GT, MO_16)
TRANS_FLAGS(ALTIVEC, VCMPGTSW, do_vcmp, TCG_COND_GT, MO_32)
TRANS_FLAGS2(ALTIVEC_207, VCMPGTSD, do_vcmp, TCG_COND_GT, MO_64)
TRANS_FLAGS(ALTIVEC, VCMPGTUB, do_vcmp, TCG_COND_GTU, MO_8)
TRANS_FLAGS(ALTIVEC, VCMPGTUH, do_vcmp, TCG_COND_GTU, MO_16)
TRANS_FLAGS(ALTIVEC, VCMPGTUW, do_vcmp, TCG_COND_GTU, MO_32)
TRANS_FLAGS2(ALTIVEC_207, VCMPGTUD, do_vcmp, TCG_COND_GTU, MO_64)

TRANS_FLAGS2(ISA300, VCMPNEB, do_vcmp, TCG_COND_NE, MO_8)
TRANS_FLAGS2(ISA300, VCMPNEH, do_vcmp, TCG_COND_NE, MO_16)
TRANS_FLAGS2(ISA300, VCMPNEW, do_vcmp, TCG_COND_NE, MO_32)

static void gen_vcmpnez_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    TCGv_vec t0, t1, zero;

    t0 = tcg_temp_new_vec_matching(t);
    t1 = tcg_temp_new_vec_matching(t);
    zero = tcg_constant_vec_matching(t, vece, 0);

    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t0, a, zero);
    tcg_gen_cmp_vec(TCG_COND_EQ, vece, t1, b, zero);
    tcg_gen_cmp_vec(TCG_COND_NE, vece, t, a, b);

    tcg_gen_or_vec(vece, t, t, t0);
    tcg_gen_or_vec(vece, t, t, t1);
}

static bool do_vcmpnez(DisasContext *ctx, arg_VC *a, int vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_cmp_vec, 0
    };
    static const GVecGen3 ops[3] = {
        {
            .fniv = gen_vcmpnez_vec,
            .fno = gen_helper_VCMPNEZB,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vcmpnez_vec,
            .fno = gen_helper_VCMPNEZH,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vcmpnez_vec,
            .fno = gen_helper_VCMPNEZW,
            .opt_opc = vecop_list,
            .vece = MO_32
        }
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &ops[vece]);

    if (a->rc) {
        do_vcmp_rc(a->vrt);
    }

    return true;
}

TRANS(VCMPNEZB, do_vcmpnez, MO_8)
TRANS(VCMPNEZH, do_vcmpnez, MO_16)
TRANS(VCMPNEZW, do_vcmpnez, MO_32)

static bool trans_VCMPEQUQ(DisasContext *ctx, arg_VC *a)
{
    TCGv_i64 t0, t1, t2;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();

    get_avr64(t0, a->vra, true);
    get_avr64(t1, a->vrb, true);
    tcg_gen_xor_i64(t2, t0, t1);

    get_avr64(t0, a->vra, false);
    get_avr64(t1, a->vrb, false);
    tcg_gen_xor_i64(t1, t0, t1);

    tcg_gen_or_i64(t1, t1, t2);
    tcg_gen_negsetcond_i64(TCG_COND_EQ, t1, t1, tcg_constant_i64(0));

    set_avr64(a->vrt, t1, true);
    set_avr64(a->vrt, t1, false);

    if (a->rc) {
        tcg_gen_extrl_i64_i32(cpu_crf[6], t1);
        tcg_gen_andi_i32(cpu_crf[6], cpu_crf[6], 0xa);
        tcg_gen_xori_i32(cpu_crf[6], cpu_crf[6], 0x2);
    }
    return true;
}

static bool do_vcmpgtq(DisasContext *ctx, arg_VC *a, bool sign)
{
    TCGv_i64 t0, t1, t2;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();

    get_avr64(t0, a->vra, false);
    get_avr64(t1, a->vrb, false);
    tcg_gen_negsetcond_i64(TCG_COND_GTU, t2, t0, t1);

    get_avr64(t0, a->vra, true);
    get_avr64(t1, a->vrb, true);
    tcg_gen_movcond_i64(TCG_COND_EQ, t2, t0, t1, t2, tcg_constant_i64(0));
    tcg_gen_negsetcond_i64(sign ? TCG_COND_GT : TCG_COND_GTU, t1, t0, t1);

    tcg_gen_or_i64(t1, t1, t2);

    set_avr64(a->vrt, t1, true);
    set_avr64(a->vrt, t1, false);

    if (a->rc) {
        tcg_gen_extrl_i64_i32(cpu_crf[6], t1);
        tcg_gen_andi_i32(cpu_crf[6], cpu_crf[6], 0xa);
        tcg_gen_xori_i32(cpu_crf[6], cpu_crf[6], 0x2);
    }
    return true;
}

TRANS(VCMPGTSQ, do_vcmpgtq, true)
TRANS(VCMPGTUQ, do_vcmpgtq, false)

static bool do_vcmpq(DisasContext *ctx, arg_VX_bf *a, bool sign)
{
    TCGv_i64 vra, vrb;
    TCGLabel *gt, *lt, *done;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    vra = tcg_temp_new_i64();
    vrb = tcg_temp_new_i64();
    gt = gen_new_label();
    lt = gen_new_label();
    done = gen_new_label();

    get_avr64(vra, a->vra, true);
    get_avr64(vrb, a->vrb, true);
    tcg_gen_brcond_i64((sign ? TCG_COND_GT : TCG_COND_GTU), vra, vrb, gt);
    tcg_gen_brcond_i64((sign ? TCG_COND_LT : TCG_COND_LTU), vra, vrb, lt);

    get_avr64(vra, a->vra, false);
    get_avr64(vrb, a->vrb, false);
    tcg_gen_brcond_i64(TCG_COND_GTU, vra, vrb, gt);
    tcg_gen_brcond_i64(TCG_COND_LTU, vra, vrb, lt);

    tcg_gen_movi_i32(cpu_crf[a->bf], CRF_EQ);
    tcg_gen_br(done);

    gen_set_label(gt);
    tcg_gen_movi_i32(cpu_crf[a->bf], CRF_GT);
    tcg_gen_br(done);

    gen_set_label(lt);
    tcg_gen_movi_i32(cpu_crf[a->bf], CRF_LT);
    tcg_gen_br(done);

    gen_set_label(done);
    return true;
}

TRANS(VCMPSQ, do_vcmpq, true)
TRANS(VCMPUQ, do_vcmpq, false)

GEN_VXRFORM(vcmpeqfp, 3, 3)
GEN_VXRFORM(vcmpgefp, 3, 7)
GEN_VXRFORM(vcmpgtfp, 3, 11)
GEN_VXRFORM(vcmpbfp, 3, 15)

static void gen_vsplti(DisasContext *ctx, int vece)
{
    int simm;

    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }

    simm = SIMM5(ctx->opcode);
    tcg_gen_gvec_dup_imm(vece, avr_full_offset(rD(ctx->opcode)), 16, 16, simm);
}

#define GEN_VXFORM_VSPLTI(name, vece, opc2, opc3) \
static void glue(gen_, name)(DisasContext *ctx) { gen_vsplti(ctx, vece); }

GEN_VXFORM_VSPLTI(vspltisb, MO_8, 6, 12);
GEN_VXFORM_VSPLTI(vspltish, MO_16, 6, 13);
GEN_VXFORM_VSPLTI(vspltisw, MO_32, 6, 14);

#define GEN_VXFORM_NOA(name, opc2, opc3)                                \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(rd, rb);                                      \
    }

#define GEN_VXFORM_NOA_ENV(name, opc2, opc3)                            \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
                                                                        \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(tcg_env, rd, rb);                             \
    }

#define GEN_VXFORM_NOA_2(name, opc2, opc3, opc4)                        \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(rd, rb);                                      \
    }

#define GEN_VXFORM_NOA_3(name, opc2, opc3, opc4)                        \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb;                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        gen_helper_##name(cpu_gpr[rD(ctx->opcode)], rb);                \
    }
GEN_VXFORM_NOA(vupkhsb, 7, 8);
GEN_VXFORM_NOA(vupkhsh, 7, 9);
GEN_VXFORM_NOA(vupkhsw, 7, 25);
GEN_VXFORM_NOA(vupklsb, 7, 10);
GEN_VXFORM_NOA(vupklsh, 7, 11);
GEN_VXFORM_NOA(vupklsw, 7, 27);
GEN_VXFORM_NOA(vupkhpx, 7, 13);
GEN_VXFORM_NOA(vupklpx, 7, 15);
GEN_VXFORM_NOA_ENV(vrefp, 5, 4);
GEN_VXFORM_NOA_ENV(vrsqrtefp, 5, 5);
GEN_VXFORM_NOA_ENV(vexptefp, 5, 6);
GEN_VXFORM_NOA_ENV(vlogefp, 5, 7);
GEN_VXFORM_NOA_ENV(vrfim, 5, 11);
GEN_VXFORM_NOA_ENV(vrfin, 5, 8);
GEN_VXFORM_NOA_ENV(vrfip, 5, 10);
GEN_VXFORM_NOA_ENV(vrfiz, 5, 9);

static void gen_vprtyb_vec(unsigned vece, TCGv_vec t, TCGv_vec b)
{
    int i;
    TCGv_vec tmp = tcg_temp_new_vec_matching(b);
    /* MO_32 is 2, so 2 iterations for MO_32 and 3 for MO_64 */
    for (i = 0; i < vece; i++) {
        tcg_gen_shri_vec(vece, tmp, b, (4 << (vece - i)));
        tcg_gen_xor_vec(vece, b, tmp, b);
    }
    tcg_gen_and_vec(vece, t, b, tcg_constant_vec_matching(t, vece, 1));
}

/* vprtybw */
static void gen_vprtyb_i32(TCGv_i32 t, TCGv_i32 b)
{
    tcg_gen_ctpop_i32(t, b);
    tcg_gen_and_i32(t, t, tcg_constant_i32(1));
}

/* vprtybd */
static void gen_vprtyb_i64(TCGv_i64 t, TCGv_i64 b)
{
    tcg_gen_ctpop_i64(t, b);
    tcg_gen_and_i64(t, t, tcg_constant_i64(1));
}

static bool do_vx_vprtyb(DisasContext *ctx, arg_VX_tb *a, unsigned vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shri_vec, 0
    };

    static const GVecGen2 op[] = {
        {
            .fniv = gen_vprtyb_vec,
            .fni4 = gen_vprtyb_i32,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_vprtyb_vec,
            .fni8 = gen_vprtyb_i64,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
        {
            .fno = gen_helper_VPRTYBQ,
            .vece = MO_128
        },
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_2(avr_full_offset(a->vrt), avr_full_offset(a->vrb),
                   16, 16, &op[vece - MO_32]);

    return true;
}

TRANS(VPRTYBW, do_vx_vprtyb, MO_32)
TRANS(VPRTYBD, do_vx_vprtyb, MO_64)
TRANS(VPRTYBQ, do_vx_vprtyb, MO_128)

static void gen_vsplt(DisasContext *ctx, int vece)
{
    int uimm, dofs, bofs;

    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }

    uimm = UIMM5(ctx->opcode);
    bofs = avr_full_offset(rB(ctx->opcode));
    dofs = avr_full_offset(rD(ctx->opcode));

    /* Experimental testing shows that hardware masks the immediate.  */
    bofs += (uimm << vece) & 15;
#if !HOST_BIG_ENDIAN
    bofs ^= 15;
    bofs &= ~((1 << vece) - 1);
#endif

    tcg_gen_gvec_dup_mem(vece, dofs, bofs, 16, 16);
}

#define GEN_VXFORM_VSPLT(name, vece, opc2, opc3) \
static void glue(gen_, name)(DisasContext *ctx) { gen_vsplt(ctx, vece); }

#define GEN_VXFORM_UIMM_ENV(name, opc2, opc3)                           \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        TCGv_i32 uimm;                                                  \
                                                                        \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        uimm = tcg_constant_i32(UIMM5(ctx->opcode));                    \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(tcg_env, rd, rb, uimm);                       \
    }

#define GEN_VXFORM_UIMM_SPLAT(name, opc2, opc3, splat_max)              \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        uint8_t uimm = UIMM4(ctx->opcode);                              \
        TCGv_i32 t0;                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        if (uimm > splat_max) {                                         \
            uimm = 0;                                                   \
        }                                                               \
        t0 = tcg_temp_new_i32();                                        \
        tcg_gen_movi_i32(t0, uimm);                                     \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(rd, rb, t0);                                  \
    }

GEN_VXFORM_VSPLT(vspltb, MO_8, 6, 8);
GEN_VXFORM_VSPLT(vsplth, MO_16, 6, 9);
GEN_VXFORM_VSPLT(vspltw, MO_32, 6, 10);
GEN_VXFORM_UIMM_SPLAT(vextractub, 6, 8, 15);
GEN_VXFORM_UIMM_SPLAT(vextractuh, 6, 9, 14);
GEN_VXFORM_UIMM_SPLAT(vextractuw, 6, 10, 12);
GEN_VXFORM_UIMM_SPLAT(vextractd, 6, 11, 8);
GEN_VXFORM_UIMM_ENV(vcfux, 5, 12);
GEN_VXFORM_UIMM_ENV(vcfsx, 5, 13);
GEN_VXFORM_UIMM_ENV(vctuxs, 5, 14);
GEN_VXFORM_UIMM_ENV(vctsxs, 5, 15);
GEN_VXFORM_DUAL(vspltb, PPC_ALTIVEC, PPC_NONE,
                vextractub, PPC_NONE, PPC2_ISA300);
GEN_VXFORM_DUAL(vsplth, PPC_ALTIVEC, PPC_NONE,
                vextractuh, PPC_NONE, PPC2_ISA300);
GEN_VXFORM_DUAL(vspltw, PPC_ALTIVEC, PPC_NONE,
                vextractuw, PPC_NONE, PPC2_ISA300);

static bool trans_VGNB(DisasContext *ctx, arg_VX_n *a)
{
    /*
     * Similar to do_vextractm, we'll use a sequence of mask-shift-or operations
     * to gather the bits. The masks can be created with
     *
     * uint64_t mask(uint64_t n, uint64_t step)
     * {
     *     uint64_t p = ((1UL << (1UL << step)) - 1UL) << ((n - 1UL) << step),
     *                  plen = n << step, m = 0;
     *     for(int i = 0; i < 64/plen; i++) {
     *         m |= p;
     *         m = ror64(m, plen);
     *     }
     *     p >>= plen * DIV_ROUND_UP(64, plen) - 64;
     *     return m | p;
     * }
     *
     * But since there are few values of N, we'll use a lookup table to avoid
     * these calculations at runtime.
     */
    static const uint64_t mask[6][5] = {
        {
            0xAAAAAAAAAAAAAAAAULL, 0xccccccccccccccccULL, 0xf0f0f0f0f0f0f0f0ULL,
            0xff00ff00ff00ff00ULL, 0xffff0000ffff0000ULL
        },
        {
            0x9249249249249249ULL, 0xC30C30C30C30C30CULL, 0xF00F00F00F00F00FULL,
            0xFF0000FF0000FF00ULL, 0xFFFF00000000FFFFULL
        },
        {
            /* For N >= 4, some mask operations can be elided */
            0x8888888888888888ULL, 0, 0xf000f000f000f000ULL, 0,
            0xFFFF000000000000ULL
        },
        {
            0x8421084210842108ULL, 0, 0xF0000F0000F0000FULL, 0, 0
        },
        {
            0x8208208208208208ULL, 0, 0xF00000F00000F000ULL, 0, 0
        },
        {
            0x8102040810204081ULL, 0, 0xF000000F000000F0ULL, 0, 0
        }
    };
    uint64_t m;
    int i, sh, nbits = DIV_ROUND_UP(64, a->n);
    TCGv_i64 hi, lo, t0, t1;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    if (a->n < 2) {
        /*
         * "N can be any value between 2 and 7, inclusive." Otherwise, the
         * result is undefined, so we don't need to change RT. Also, N > 7 is
         * impossible since the immediate field is 3 bits only.
         */
        return true;
    }

    hi = tcg_temp_new_i64();
    lo = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    get_avr64(hi, a->vrb, true);
    get_avr64(lo, a->vrb, false);

    /* Align the lower doubleword so we can use the same mask */
    tcg_gen_shli_i64(lo, lo, a->n * nbits - 64);

    /*
     * Starting from the most significant bit, gather every Nth bit with a
     * sequence of mask-shift-or operation. E.g.: for N=3
     * AxxBxxCxxDxxExxFxxGxxHxxIxxJxxKxxLxxMxxNxxOxxPxxQxxRxxSxxTxxUxxV
     *     & rep(0b100)
     * A..B..C..D..E..F..G..H..I..J..K..L..M..N..O..P..Q..R..S..T..U..V
     *     << 2
     * .B..C..D..E..F..G..H..I..J..K..L..M..N..O..P..Q..R..S..T..U..V..
     *     |
     * AB.BC.CD.DE.EF.FG.GH.HI.IJ.JK.KL.LM.MN.NO.OP.PQ.QR.RS.ST.TU.UV.V
     *  & rep(0b110000)
     * AB....CD....EF....GH....IJ....KL....MN....OP....QR....ST....UV..
     *     << 4
     * ..CD....EF....GH....IJ....KL....MN....OP....QR....ST....UV......
     *     |
     * ABCD..CDEF..EFGH..GHIJ..IJKL..KLMN..MNOP..OPQR..QRST..STUV..UV..
     *     & rep(0b111100000000)
     * ABCD........EFGH........IJKL........MNOP........QRST........UV..
     *     << 8
     * ....EFGH........IJKL........MNOP........QRST........UV..........
     *     |
     * ABCDEFGH....EFGHIJKL....IJKLMNOP....MNOPQRST....QRSTUV......UV..
     *  & rep(0b111111110000000000000000)
     * ABCDEFGH................IJKLMNOP................QRSTUV..........
     *     << 16
     * ........IJKLMNOP................QRSTUV..........................
     *     |
     * ABCDEFGHIJKLMNOP........IJKLMNOPQRSTUV..........QRSTUV..........
     *     & rep(0b111111111111111100000000000000000000000000000000)
     * ABCDEFGHIJKLMNOP................................QRSTUV..........
     *     << 32
     * ................QRSTUV..........................................
     *     |
     * ABCDEFGHIJKLMNOPQRSTUV..........................QRSTUV..........
     */
    for (i = 0, sh = a->n - 1; i < 5; i++, sh <<= 1) {
        m = mask[a->n - 2][i];
        if (m) {
            tcg_gen_andi_i64(hi, hi, m);
            tcg_gen_andi_i64(lo, lo, m);
        }
        if (sh < 64) {
            tcg_gen_shli_i64(t0, hi, sh);
            tcg_gen_shli_i64(t1, lo, sh);
            tcg_gen_or_i64(hi, t0, hi);
            tcg_gen_or_i64(lo, t1, lo);
        }
    }

    tcg_gen_andi_i64(hi, hi, ~(~0ULL >> nbits));
    tcg_gen_andi_i64(lo, lo, ~(~0ULL >> nbits));
    tcg_gen_shri_i64(lo, lo, nbits);
    tcg_gen_or_i64(hi, hi, lo);
    tcg_gen_trunc_i64_tl(cpu_gpr[a->rt], hi);
    return true;
}

static bool do_vextdx(DisasContext *ctx, arg_VA *a, int size, bool right,
               void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv))
{
    TCGv_ptr vrt, vra, vrb;
    TCGv rc;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    vrt = gen_avr_ptr(a->vrt);
    vra = gen_avr_ptr(a->vra);
    vrb = gen_avr_ptr(a->vrb);
    rc = tcg_temp_new();

    tcg_gen_andi_tl(rc, cpu_gpr[a->rc], 0x1F);
    if (right) {
        tcg_gen_subfi_tl(rc, 32 - size, rc);
    }
    gen_helper(tcg_env, vrt, vra, vrb, rc);
    return true;
}

TRANS(VEXTDUBVLX, do_vextdx, 1, false, gen_helper_VEXTDUBVLX)
TRANS(VEXTDUHVLX, do_vextdx, 2, false, gen_helper_VEXTDUHVLX)
TRANS(VEXTDUWVLX, do_vextdx, 4, false, gen_helper_VEXTDUWVLX)
TRANS(VEXTDDVLX, do_vextdx, 8, false, gen_helper_VEXTDDVLX)

TRANS(VEXTDUBVRX, do_vextdx, 1, true, gen_helper_VEXTDUBVLX)
TRANS(VEXTDUHVRX, do_vextdx, 2, true, gen_helper_VEXTDUHVLX)
TRANS(VEXTDUWVRX, do_vextdx, 4, true, gen_helper_VEXTDUWVLX)
TRANS(VEXTDDVRX, do_vextdx, 8, true, gen_helper_VEXTDDVLX)

static bool do_vinsx(DisasContext *ctx, int vrt, int size, bool right, TCGv ra,
            TCGv_i64 rb, void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_ptr t;
    TCGv idx;

    t = gen_avr_ptr(vrt);
    idx = tcg_temp_new();

    tcg_gen_andi_tl(idx, ra, 0xF);
    if (right) {
        tcg_gen_subfi_tl(idx, 16 - size, idx);
    }

    gen_helper(tcg_env, t, rb, idx);
    return true;
}

static bool do_vinsvx(DisasContext *ctx, int vrt, int size, bool right, TCGv ra,
                int vrb, void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 val;

    val = tcg_temp_new_i64();
    get_avr64(val, vrb, true);
    return do_vinsx(ctx, vrt, size, right, ra, val, gen_helper);
}

static bool do_vinsx_VX(DisasContext *ctx, arg_VX *a, int size, bool right,
                        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 val;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    val = tcg_temp_new_i64();
    tcg_gen_extu_tl_i64(val, cpu_gpr[a->vrb]);

    return do_vinsx(ctx, a->vrt, size, right, cpu_gpr[a->vra], val, gen_helper);
}

static bool do_vinsvx_VX(DisasContext *ctx, arg_VX *a, int size, bool right,
                        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    return do_vinsvx(ctx, a->vrt, size, right, cpu_gpr[a->vra], a->vrb,
                     gen_helper);
}

static bool do_vins_VX_uim4(DisasContext *ctx, arg_VX_uim4 *a, int size,
                        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    TCGv_i64 val;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    if (a->uim > (16 - size)) {
        /*
         * PowerISA v3.1 says that the resulting value is undefined in this
         * case, so just log a guest error and leave VRT unchanged. The
         * real hardware would do a partial insert, e.g. if VRT is zeroed and
         * RB is 0x12345678, executing "vinsw VRT,RB,14" results in
         * VRT = 0x0000...00001234, but we don't bother to reproduce this
         * behavior as software shouldn't rely on it.
         */
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid index for VINS* at"
            " 0x" TARGET_FMT_lx ", UIM = %d > %d\n", ctx->cia, a->uim,
            16 - size);
        return true;
    }

    val = tcg_temp_new_i64();
    tcg_gen_extu_tl_i64(val, cpu_gpr[a->vrb]);

    return do_vinsx(ctx, a->vrt, size, false, tcg_constant_tl(a->uim), val,
                    gen_helper);
}

static bool do_vinsert_VX_uim4(DisasContext *ctx, arg_VX_uim4 *a, int size,
                        void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_i64, TCGv))
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    if (a->uim > (16 - size)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid index for VINSERT* at"
            " 0x" TARGET_FMT_lx ", UIM = %d > %d\n", ctx->cia, a->uim,
            16 - size);
        return true;
    }

    return do_vinsvx(ctx, a->vrt, size, false, tcg_constant_tl(a->uim), a->vrb,
                     gen_helper);
}

TRANS(VINSBLX, do_vinsx_VX, 1, false, gen_helper_VINSBLX)
TRANS(VINSHLX, do_vinsx_VX, 2, false, gen_helper_VINSHLX)
TRANS(VINSWLX, do_vinsx_VX, 4, false, gen_helper_VINSWLX)
TRANS(VINSDLX, do_vinsx_VX, 8, false, gen_helper_VINSDLX)

TRANS(VINSBRX, do_vinsx_VX, 1, true, gen_helper_VINSBLX)
TRANS(VINSHRX, do_vinsx_VX, 2, true, gen_helper_VINSHLX)
TRANS(VINSWRX, do_vinsx_VX, 4, true, gen_helper_VINSWLX)
TRANS(VINSDRX, do_vinsx_VX, 8, true, gen_helper_VINSDLX)

TRANS(VINSW, do_vins_VX_uim4, 4, gen_helper_VINSWLX)
TRANS(VINSD, do_vins_VX_uim4, 8, gen_helper_VINSDLX)

TRANS(VINSBVLX, do_vinsvx_VX, 1, false, gen_helper_VINSBLX)
TRANS(VINSHVLX, do_vinsvx_VX, 2, false, gen_helper_VINSHLX)
TRANS(VINSWVLX, do_vinsvx_VX, 4, false, gen_helper_VINSWLX)

TRANS(VINSBVRX, do_vinsvx_VX, 1, true, gen_helper_VINSBLX)
TRANS(VINSHVRX, do_vinsvx_VX, 2, true, gen_helper_VINSHLX)
TRANS(VINSWVRX, do_vinsvx_VX, 4, true, gen_helper_VINSWLX)

TRANS(VINSERTB, do_vinsert_VX_uim4, 1, gen_helper_VINSBLX)
TRANS(VINSERTH, do_vinsert_VX_uim4, 2, gen_helper_VINSHLX)
TRANS(VINSERTW, do_vinsert_VX_uim4, 4, gen_helper_VINSWLX)
TRANS(VINSERTD, do_vinsert_VX_uim4, 8, gen_helper_VINSDLX)

static void gen_vsldoi(DisasContext *ctx)
{
    TCGv_ptr ra, rb, rd;
    TCGv_i32 sh;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    ra = gen_avr_ptr(rA(ctx->opcode));
    rb = gen_avr_ptr(rB(ctx->opcode));
    rd = gen_avr_ptr(rD(ctx->opcode));
    sh = tcg_constant_i32(VSH(ctx->opcode));
    gen_helper_vsldoi(rd, ra, rb, sh);
}

static bool trans_VSLDBI(DisasContext *ctx, arg_VN *a)
{
    TCGv_i64 t0, t1, t2;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    get_avr64(t0, a->vra, true);
    get_avr64(t1, a->vra, false);

    if (a->sh != 0) {
        t2 = tcg_temp_new_i64();

        get_avr64(t2, a->vrb, true);

        tcg_gen_extract2_i64(t0, t1, t0, 64 - a->sh);
        tcg_gen_extract2_i64(t1, t2, t1, 64 - a->sh);
    }

    set_avr64(a->vrt, t0, true);
    set_avr64(a->vrt, t1, false);
    return true;
}

static bool trans_VSRDBI(DisasContext *ctx, arg_VN *a)
{
    TCGv_i64 t2, t1, t0;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    get_avr64(t0, a->vrb, false);
    get_avr64(t1, a->vrb, true);

    if (a->sh != 0) {
        t2 = tcg_temp_new_i64();

        get_avr64(t2, a->vra, false);

        tcg_gen_extract2_i64(t0, t0, t1, a->sh);
        tcg_gen_extract2_i64(t1, t1, t2, a->sh);
    }

    set_avr64(a->vrt, t0, false);
    set_avr64(a->vrt, t1, true);
    return true;
}

static bool do_vexpand(DisasContext *ctx, arg_VX_tb *a, unsigned vece)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_sari(vece, avr_full_offset(a->vrt), avr_full_offset(a->vrb),
                      (8 << vece) - 1, 16, 16);

    return true;
}

TRANS(VEXPANDBM, do_vexpand, MO_8)
TRANS(VEXPANDHM, do_vexpand, MO_16)
TRANS(VEXPANDWM, do_vexpand, MO_32)
TRANS(VEXPANDDM, do_vexpand, MO_64)

static bool trans_VEXPANDQM(DisasContext *ctx, arg_VX_tb *a)
{
    TCGv_i64 tmp;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tmp = tcg_temp_new_i64();

    get_avr64(tmp, a->vrb, true);
    tcg_gen_sari_i64(tmp, tmp, 63);
    set_avr64(a->vrt, tmp, false);
    set_avr64(a->vrt, tmp, true);
    return true;
}

static bool do_vextractm(DisasContext *ctx, arg_VX_tb *a, unsigned vece)
{
    const uint64_t elem_width = 8 << vece, elem_count_half = 8 >> vece,
                   mask = dup_const(vece, 1ULL << (elem_width - 1));
    uint64_t i, j;
    TCGv_i64 lo, hi, t0, t1;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    hi = tcg_temp_new_i64();
    lo = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    get_avr64(lo, a->vrb, false);
    get_avr64(hi, a->vrb, true);

    tcg_gen_andi_i64(lo, lo, mask);
    tcg_gen_andi_i64(hi, hi, mask);

    /*
     * Gather the most significant bit of each element in the highest element
     * element. E.g. for bytes:
     * aXXXXXXXbXXXXXXXcXXXXXXXdXXXXXXXeXXXXXXXfXXXXXXXgXXXXXXXhXXXXXXX
     *     & dup(1 << (elem_width - 1))
     * a0000000b0000000c0000000d0000000e0000000f0000000g0000000h0000000
     *     << 32 - 4
     * 0000e0000000f0000000g0000000h00000000000000000000000000000000000
     *     |
     * a000e000b000f000c000g000d000h000e0000000f0000000g0000000h0000000
     *     << 16 - 2
     * 00c000g000d000h000e0000000f0000000g0000000h000000000000000000000
     *     |
     * a0c0e0g0b0d0f0h0c0e0g000d0f0h000e0g00000f0h00000g0000000h0000000
     *     << 8 - 1
     * 0b0d0f0h0c0e0g000d0f0h000e0g00000f0h00000g0000000h00000000000000
     *     |
     * abcdefghbcdefgh0cdefgh00defgh000efgh0000fgh00000gh000000h0000000
     */
    for (i = elem_count_half / 2, j = 32; i > 0; i >>= 1, j >>= 1) {
        tcg_gen_shli_i64(t0, hi, j - i);
        tcg_gen_shli_i64(t1, lo, j - i);
        tcg_gen_or_i64(hi, hi, t0);
        tcg_gen_or_i64(lo, lo, t1);
    }

    tcg_gen_shri_i64(hi, hi, 64 - elem_count_half);
    tcg_gen_extract2_i64(lo, lo, hi, 64 - elem_count_half);
    tcg_gen_trunc_i64_tl(cpu_gpr[a->vrt], lo);
    return true;
}

TRANS(VEXTRACTBM, do_vextractm, MO_8)
TRANS(VEXTRACTHM, do_vextractm, MO_16)
TRANS(VEXTRACTWM, do_vextractm, MO_32)
TRANS(VEXTRACTDM, do_vextractm, MO_64)

static bool trans_VEXTRACTQM(DisasContext *ctx, arg_VX_tb *a)
{
    TCGv_i64 tmp;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tmp = tcg_temp_new_i64();

    get_avr64(tmp, a->vrb, true);
    tcg_gen_shri_i64(tmp, tmp, 63);
    tcg_gen_trunc_i64_tl(cpu_gpr[a->vrt], tmp);
    return true;
}

static bool do_mtvsrm(DisasContext *ctx, arg_VX_tb *a, unsigned vece)
{
    const uint64_t elem_width = 8 << vece, elem_count_half = 8 >> vece;
    uint64_t c;
    int i, j;
    TCGv_i64 hi, lo, t0, t1;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    hi = tcg_temp_new_i64();
    lo = tcg_temp_new_i64();
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_gpr[a->vrb]);
    tcg_gen_extract_i64(hi, t0, elem_count_half, elem_count_half);
    tcg_gen_extract_i64(lo, t0, 0, elem_count_half);

    /*
     * Spread the bits into their respective elements.
     * E.g. for bytes:
     * 00000000000000000000000000000000000000000000000000000000abcdefgh
     *   << 32 - 4
     * 0000000000000000000000000000abcdefgh0000000000000000000000000000
     *   |
     * 0000000000000000000000000000abcdefgh00000000000000000000abcdefgh
     *   << 16 - 2
     * 00000000000000abcdefgh00000000000000000000abcdefgh00000000000000
     *   |
     * 00000000000000abcdefgh000000abcdefgh000000abcdefgh000000abcdefgh
     *   << 8 - 1
     * 0000000abcdefgh000000abcdefgh000000abcdefgh000000abcdefgh0000000
     *   |
     * 0000000abcdefgXbcdefgXbcdefgXbcdefgXbcdefgXbcdefgXbcdefgXbcdefgh
     *   & dup(1)
     * 0000000a0000000b0000000c0000000d0000000e0000000f0000000g0000000h
     *   * 0xff
     * aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh
     */
    for (i = elem_count_half / 2, j = 32; i > 0; i >>= 1, j >>= 1) {
        tcg_gen_shli_i64(t0, hi, j - i);
        tcg_gen_shli_i64(t1, lo, j - i);
        tcg_gen_or_i64(hi, hi, t0);
        tcg_gen_or_i64(lo, lo, t1);
    }

    c = dup_const(vece, 1);
    tcg_gen_andi_i64(hi, hi, c);
    tcg_gen_andi_i64(lo, lo, c);

    c = MAKE_64BIT_MASK(0, elem_width);
    tcg_gen_muli_i64(hi, hi, c);
    tcg_gen_muli_i64(lo, lo, c);

    set_avr64(a->vrt, lo, false);
    set_avr64(a->vrt, hi, true);
    return true;
}

TRANS(MTVSRBM, do_mtvsrm, MO_8)
TRANS(MTVSRHM, do_mtvsrm, MO_16)
TRANS(MTVSRWM, do_mtvsrm, MO_32)
TRANS(MTVSRDM, do_mtvsrm, MO_64)

static bool trans_MTVSRQM(DisasContext *ctx, arg_VX_tb *a)
{
    TCGv_i64 tmp;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tmp = tcg_temp_new_i64();

    tcg_gen_ext_tl_i64(tmp, cpu_gpr[a->vrb]);
    tcg_gen_sextract_i64(tmp, tmp, 0, 1);
    set_avr64(a->vrt, tmp, false);
    set_avr64(a->vrt, tmp, true);
    return true;
}

static bool trans_MTVSRBMI(DisasContext *ctx, arg_DX_b *a)
{
    const uint64_t mask = dup_const(MO_8, 1);
    uint64_t hi, lo;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    hi = extract16(a->b, 8, 8);
    lo = extract16(a->b, 0, 8);

    for (int i = 4, j = 32; i > 0; i >>= 1, j >>= 1) {
        hi |= hi << (j - i);
        lo |= lo << (j - i);
    }

    hi = (hi & mask) * 0xFF;
    lo = (lo & mask) * 0xFF;

    set_avr64(a->vrt, tcg_constant_i64(hi), true);
    set_avr64(a->vrt, tcg_constant_i64(lo), false);

    return true;
}

static bool do_vcntmb(DisasContext *ctx, arg_VX_mp *a, int vece)
{
    TCGv_i64 r[2], mask;

    r[0] = tcg_temp_new_i64();
    r[1] = tcg_temp_new_i64();
    mask = tcg_constant_i64(dup_const(vece, 1ULL << ((8 << vece) - 1)));

    for (int i = 0; i < 2; i++) {
        get_avr64(r[i], a->vrb, i);
        if (a->mp) {
            tcg_gen_and_i64(r[i], mask, r[i]);
        } else {
            tcg_gen_andc_i64(r[i], mask, r[i]);
        }
        tcg_gen_ctpop_i64(r[i], r[i]);
    }

    tcg_gen_add_i64(r[0], r[0], r[1]);
    tcg_gen_shli_i64(r[0], r[0], TARGET_LONG_BITS - 8 + vece);
    tcg_gen_trunc_i64_tl(cpu_gpr[a->rt], r[0]);
    return true;
}

TRANS(VCNTMBB, do_vcntmb, MO_8)
TRANS(VCNTMBH, do_vcntmb, MO_16)
TRANS(VCNTMBW, do_vcntmb, MO_32)
TRANS(VCNTMBD, do_vcntmb, MO_64)

static bool do_vstri(DisasContext *ctx, arg_VX_tb_rc *a,
                     void (*gen_helper)(TCGv_i32, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr vrt, vrb;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    vrt = gen_avr_ptr(a->vrt);
    vrb = gen_avr_ptr(a->vrb);

    if (a->rc) {
        gen_helper(cpu_crf[6], vrt, vrb);
    } else {
        TCGv_i32 discard = tcg_temp_new_i32();
        gen_helper(discard, vrt, vrb);
    }
    return true;
}

TRANS(VSTRIBL, do_vstri, gen_helper_VSTRIBL)
TRANS(VSTRIBR, do_vstri, gen_helper_VSTRIBR)
TRANS(VSTRIHL, do_vstri, gen_helper_VSTRIHL)
TRANS(VSTRIHR, do_vstri, gen_helper_VSTRIHR)

static bool do_vclrb(DisasContext *ctx, arg_VX *a, bool right)
{
    TCGv_i64 rb, mh, ml, tmp,
             ones = tcg_constant_i64(-1),
             zero = tcg_constant_i64(0);

    rb = tcg_temp_new_i64();
    mh = tcg_temp_new_i64();
    ml = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(rb, cpu_gpr[a->vrb]);
    tcg_gen_andi_i64(tmp, rb, 7);
    tcg_gen_shli_i64(tmp, tmp, 3);
    if (right) {
        tcg_gen_shr_i64(tmp, ones, tmp);
    } else {
        tcg_gen_shl_i64(tmp, ones, tmp);
    }
    tcg_gen_not_i64(tmp, tmp);

    if (right) {
        tcg_gen_movcond_i64(TCG_COND_LTU, mh, rb, tcg_constant_i64(8),
                            tmp, ones);
        tcg_gen_movcond_i64(TCG_COND_LTU, ml, rb, tcg_constant_i64(8),
                            zero, tmp);
        tcg_gen_movcond_i64(TCG_COND_LTU, ml, rb, tcg_constant_i64(16),
                            ml, ones);
    } else {
        tcg_gen_movcond_i64(TCG_COND_LTU, ml, rb, tcg_constant_i64(8),
                            tmp, ones);
        tcg_gen_movcond_i64(TCG_COND_LTU, mh, rb, tcg_constant_i64(8),
                            zero, tmp);
        tcg_gen_movcond_i64(TCG_COND_LTU, mh, rb, tcg_constant_i64(16),
                            mh, ones);
    }

    get_avr64(tmp, a->vra, true);
    tcg_gen_and_i64(tmp, tmp, mh);
    set_avr64(a->vrt, tmp, true);

    get_avr64(tmp, a->vra, false);
    tcg_gen_and_i64(tmp, tmp, ml);
    set_avr64(a->vrt, tmp, false);
    return true;
}

TRANS(VCLRLB, do_vclrb, false)
TRANS(VCLRRB, do_vclrb, true)

#define GEN_VAFORM_PAIRED(name0, name1, opc2)                           \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)              \
    {                                                                   \
        TCGv_ptr ra, rb, rc, rd;                                        \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        ra = gen_avr_ptr(rA(ctx->opcode));                              \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rc = gen_avr_ptr(rC(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        if (Rc(ctx->opcode)) {                                          \
            gen_helper_##name1(tcg_env, rd, ra, rb, rc);                \
        } else {                                                        \
            gen_helper_##name0(tcg_env, rd, ra, rb, rc);                \
        }                                                               \
    }

GEN_VAFORM_PAIRED(vmaddfp, vnmsubfp, 23)

static bool do_va_helper(DisasContext *ctx, arg_VA *a,
    void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr vrt, vra, vrb, vrc;
    REQUIRE_VECTOR(ctx);

    vrt = gen_avr_ptr(a->vrt);
    vra = gen_avr_ptr(a->vra);
    vrb = gen_avr_ptr(a->vrb);
    vrc = gen_avr_ptr(a->rc);
    gen_helper(vrt, vra, vrb, vrc);
    return true;
}

TRANS_FLAGS2(ALTIVEC_207, VADDECUQ, do_va_helper, gen_helper_VADDECUQ)
TRANS_FLAGS2(ALTIVEC_207, VADDEUQM, do_va_helper, gen_helper_VADDEUQM)

TRANS_FLAGS2(ALTIVEC_207, VSUBEUQM, do_va_helper, gen_helper_VSUBEUQM)
TRANS_FLAGS2(ALTIVEC_207, VSUBECUQ, do_va_helper, gen_helper_VSUBECUQ)

TRANS_FLAGS(ALTIVEC, VPERM, do_va_helper, gen_helper_VPERM)
TRANS_FLAGS2(ISA300, VPERMR, do_va_helper, gen_helper_VPERMR)

static void gen_vmladduhm_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                              TCGv_vec c)
{
    tcg_gen_mul_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, t, t, c);
}

static bool trans_VMLADDUHM(DisasContext *ctx, arg_VA *a)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_add_vec, INDEX_op_mul_vec, 0
    };

    static const GVecGen4 op = {
        .fno = gen_helper_VMLADDUHM,
        .fniv = gen_vmladduhm_vec,
        .opt_opc = vecop_list,
        .vece = MO_16
    };

    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_4(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), avr_full_offset(a->rc),
                   16, 16, &op);

    return true;
}

static bool trans_VSEL(DisasContext *ctx, arg_VA *a)
{
    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_bitsel(MO_64, avr_full_offset(a->vrt), avr_full_offset(a->rc),
                        avr_full_offset(a->vrb), avr_full_offset(a->vra),
                        16, 16);

    return true;
}

TRANS_FLAGS(ALTIVEC, VMSUMUBM, do_va_helper, gen_helper_VMSUMUBM)
TRANS_FLAGS(ALTIVEC, VMSUMMBM, do_va_helper, gen_helper_VMSUMMBM)
TRANS_FLAGS(ALTIVEC, VMSUMSHM, do_va_helper, gen_helper_VMSUMSHM)
TRANS_FLAGS(ALTIVEC, VMSUMUHM, do_va_helper, gen_helper_VMSUMUHM)

static bool do_va_env_helper(DisasContext *ctx, arg_VA *a,
    void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr vrt, vra, vrb, vrc;
    REQUIRE_VECTOR(ctx);

    vrt = gen_avr_ptr(a->vrt);
    vra = gen_avr_ptr(a->vra);
    vrb = gen_avr_ptr(a->vrb);
    vrc = gen_avr_ptr(a->rc);
    gen_helper(tcg_env, vrt, vra, vrb, vrc);
    return true;
}

TRANS_FLAGS(ALTIVEC, VMSUMUHS, do_va_env_helper, gen_helper_VMSUMUHS)
TRANS_FLAGS(ALTIVEC, VMSUMSHS, do_va_env_helper, gen_helper_VMSUMSHS)

TRANS_FLAGS(ALTIVEC, VMHADDSHS, do_va_env_helper, gen_helper_VMHADDSHS)
TRANS_FLAGS(ALTIVEC, VMHRADDSHS, do_va_env_helper, gen_helper_VMHRADDSHS)

GEN_VXFORM_NOA(vclzb, 1, 28)
GEN_VXFORM_NOA(vclzh, 1, 29)
GEN_VXFORM_TRANS(vclzw, 1, 30)
GEN_VXFORM_TRANS(vclzd, 1, 31)

static bool do_vneg(DisasContext *ctx, arg_VX_tb *a, unsigned vece)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_neg(vece, avr_full_offset(a->vrt), avr_full_offset(a->vrb),
                     16, 16);
    return true;
}

TRANS(VNEGW, do_vneg, MO_32)
TRANS(VNEGD, do_vneg, MO_64)

static void gen_vexts_i64(TCGv_i64 t, TCGv_i64 b, int64_t s)
{
    tcg_gen_sextract_i64(t, b, 0, 64 - s);
}

static void gen_vexts_i32(TCGv_i32 t, TCGv_i32 b, int32_t s)
{
    tcg_gen_sextract_i32(t, b, 0, 32 - s);
}

static void gen_vexts_vec(unsigned vece, TCGv_vec t, TCGv_vec b, int64_t s)
{
    tcg_gen_shli_vec(vece, t, b, s);
    tcg_gen_sari_vec(vece, t, t, s);
}

static bool do_vexts(DisasContext *ctx, arg_VX_tb *a, unsigned vece, int64_t s)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_shli_vec, INDEX_op_sari_vec, 0
    };

    static const GVecGen2i op[2] = {
        {
            .fni4 = gen_vexts_i32,
            .fniv = gen_vexts_vec,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fni8 = gen_vexts_i64,
            .fniv = gen_vexts_vec,
            .opt_opc = vecop_list,
            .vece = MO_64
        },
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_2i(avr_full_offset(a->vrt), avr_full_offset(a->vrb),
                    16, 16, s, &op[vece - MO_32]);

    return true;
}

TRANS(VEXTSB2W, do_vexts, MO_32, 24);
TRANS(VEXTSH2W, do_vexts, MO_32, 16);
TRANS(VEXTSB2D, do_vexts, MO_64, 56);
TRANS(VEXTSH2D, do_vexts, MO_64, 48);
TRANS(VEXTSW2D, do_vexts, MO_64, 32);

static bool trans_VEXTSD2Q(DisasContext *ctx, arg_VX_tb *a)
{
    TCGv_i64 tmp;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tmp = tcg_temp_new_i64();

    get_avr64(tmp, a->vrb, false);
    set_avr64(a->vrt, tmp, false);
    tcg_gen_sari_i64(tmp, tmp, 63);
    set_avr64(a->vrt, tmp, true);
    return true;
}

GEN_VXFORM_NOA_2(vctzb, 1, 24, 28)
GEN_VXFORM_NOA_2(vctzh, 1, 24, 29)
GEN_VXFORM_NOA_2(vctzw, 1, 24, 30)
GEN_VXFORM_NOA_2(vctzd, 1, 24, 31)
GEN_VXFORM_NOA_3(vclzlsbb, 1, 24, 0)
GEN_VXFORM_NOA_3(vctzlsbb, 1, 24, 1)
GEN_VXFORM_NOA(vpopcntb, 1, 28)
GEN_VXFORM_NOA(vpopcnth, 1, 29)
GEN_VXFORM_NOA(vpopcntw, 1, 30)
GEN_VXFORM_NOA(vpopcntd, 1, 31)
GEN_VXFORM_DUAL(vclzb, PPC_NONE, PPC2_ALTIVEC_207, \
                vpopcntb, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vclzh, PPC_NONE, PPC2_ALTIVEC_207, \
                vpopcnth, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vclzw, PPC_NONE, PPC2_ALTIVEC_207, \
                vpopcntw, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vclzd, PPC_NONE, PPC2_ALTIVEC_207, \
                vpopcntd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM(vbpermd, 6, 23);
GEN_VXFORM(vbpermq, 6, 21);
GEN_VXFORM_TRANS(vgbbd, 6, 20);
GEN_VXFORM(vpmsumb, 4, 16)
GEN_VXFORM(vpmsumh, 4, 17)
GEN_VXFORM(vpmsumw, 4, 18)

#define GEN_BCD(op)                                 \
static void gen_##op(DisasContext *ctx)             \
{                                                   \
    TCGv_ptr ra, rb, rd;                            \
    TCGv_i32 ps;                                    \
                                                    \
    if (unlikely(!ctx->altivec_enabled)) {          \
        gen_exception(ctx, POWERPC_EXCP_VPU);       \
        return;                                     \
    }                                               \
                                                    \
    ra = gen_avr_ptr(rA(ctx->opcode));              \
    rb = gen_avr_ptr(rB(ctx->opcode));              \
    rd = gen_avr_ptr(rD(ctx->opcode));              \
                                                    \
    ps = tcg_constant_i32((ctx->opcode & 0x200) != 0); \
                                                    \
    gen_helper_##op(cpu_crf[6], rd, ra, rb, ps);    \
}

#define GEN_BCD2(op)                                \
static void gen_##op(DisasContext *ctx)             \
{                                                   \
    TCGv_ptr rd, rb;                                \
    TCGv_i32 ps;                                    \
                                                    \
    if (unlikely(!ctx->altivec_enabled)) {          \
        gen_exception(ctx, POWERPC_EXCP_VPU);       \
        return;                                     \
    }                                               \
                                                    \
    rb = gen_avr_ptr(rB(ctx->opcode));              \
    rd = gen_avr_ptr(rD(ctx->opcode));              \
                                                    \
    ps = tcg_constant_i32((ctx->opcode & 0x200) != 0); \
                                                    \
    gen_helper_##op(cpu_crf[6], rd, rb, ps);        \
}

GEN_BCD(bcdadd)
GEN_BCD(bcdsub)
GEN_BCD2(bcdcfn)
GEN_BCD2(bcdctn)
GEN_BCD2(bcdcfz)
GEN_BCD2(bcdctz)
GEN_BCD2(bcdcfsq)
GEN_BCD2(bcdctsq)
GEN_BCD2(bcdsetsgn)
GEN_BCD(bcdcpsgn);
GEN_BCD(bcds);
GEN_BCD(bcdus);
GEN_BCD(bcdsr);
GEN_BCD(bcdtrunc);
GEN_BCD(bcdutrunc);

static void gen_xpnd04_1(DisasContext *ctx)
{
    switch (opc4(ctx->opcode)) {
    case 0:
        gen_bcdctsq(ctx);
        break;
    case 2:
        gen_bcdcfsq(ctx);
        break;
    case 4:
        gen_bcdctz(ctx);
        break;
    case 5:
        gen_bcdctn(ctx);
        break;
    case 6:
        gen_bcdcfz(ctx);
        break;
    case 7:
        gen_bcdcfn(ctx);
        break;
    case 31:
        gen_bcdsetsgn(ctx);
        break;
    default:
        gen_invalid(ctx);
        break;
    }
}

static void gen_xpnd04_2(DisasContext *ctx)
{
    switch (opc4(ctx->opcode)) {
    case 0:
        gen_bcdctsq(ctx);
        break;
    case 2:
        gen_bcdcfsq(ctx);
        break;
    case 4:
        gen_bcdctz(ctx);
        break;
    case 6:
        gen_bcdcfz(ctx);
        break;
    case 7:
        gen_bcdcfn(ctx);
        break;
    case 31:
        gen_bcdsetsgn(ctx);
        break;
    default:
        gen_invalid(ctx);
        break;
    }
}

GEN_VXFORM_DUAL(vsububm, PPC_ALTIVEC, PPC_NONE, \
                bcdadd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubuhm, PPC_ALTIVEC, PPC_NONE, \
                bcdsub, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubudm, PPC2_ALTIVEC_207, PPC_NONE, \
                bcds, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubuwm, PPC_ALTIVEC, PPC_NONE, \
                bcdus, PPC_NONE, PPC2_ISA300)

static void gen_vsbox(DisasContext *ctx)
{
    TCGv_ptr ra, rd;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    ra = gen_avr_ptr(rA(ctx->opcode));
    rd = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_vsbox(rd, ra);
}

GEN_VXFORM(vcipher, 4, 20)
GEN_VXFORM(vcipherlast, 4, 20)
GEN_VXFORM(vncipher, 4, 21)
GEN_VXFORM(vncipherlast, 4, 21)

GEN_VXFORM_DUAL(vcipher, PPC_NONE, PPC2_ALTIVEC_207,
                vcipherlast, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vncipher, PPC_NONE, PPC2_ALTIVEC_207,
                vncipherlast, PPC_NONE, PPC2_ALTIVEC_207)

#define VSHASIGMA(op)                         \
static void gen_##op(DisasContext *ctx)       \
{                                             \
    TCGv_ptr ra, rd;                          \
    TCGv_i32 st_six;                          \
    if (unlikely(!ctx->altivec_enabled)) {    \
        gen_exception(ctx, POWERPC_EXCP_VPU); \
        return;                               \
    }                                         \
    ra = gen_avr_ptr(rA(ctx->opcode));        \
    rd = gen_avr_ptr(rD(ctx->opcode));        \
    st_six = tcg_constant_i32(rB(ctx->opcode));  \
    gen_helper_##op(rd, ra, st_six);          \
}

VSHASIGMA(vshasigmaw)
VSHASIGMA(vshasigmad)

GEN_VXFORM3(vpermxor, 22, 0xFF)
GEN_VXFORM_DUAL(vsldoi, PPC_ALTIVEC, PPC_NONE,
                vpermxor, PPC_NONE, PPC2_ALTIVEC_207)

static bool trans_VCFUGED(DisasContext *ctx, arg_VX *a)
{
    static const GVecGen3 g = {
        .fni8 = gen_helper_CFUGED,
        .vece = MO_64,
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &g);

    return true;
}

static bool trans_VCLZDM(DisasContext *ctx, arg_VX *a)
{
    static const GVecGen3i g = {
        .fni8 = do_cntzdm,
        .vece = MO_64,
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3i(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                    avr_full_offset(a->vrb), 16, 16, false, &g);

    return true;
}

static bool trans_VCTZDM(DisasContext *ctx, arg_VX *a)
{
    static const GVecGen3i g = {
        .fni8 = do_cntzdm,
        .vece = MO_64,
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3i(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                    avr_full_offset(a->vrb), 16, 16, true, &g);

    return true;
}

static bool trans_VPDEPD(DisasContext *ctx, arg_VX *a)
{
    static const GVecGen3 g = {
        .fni8 = gen_helper_PDEPD,
        .vece = MO_64,
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &g);

    return true;
}

static bool trans_VPEXTD(DisasContext *ctx, arg_VX *a)
{
    static const GVecGen3 g = {
        .fni8 = gen_helper_PEXTD,
        .vece = MO_64,
    };

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &g);

    return true;
}

static bool trans_VMSUMUDM(DisasContext *ctx, arg_VA *a)
{
    TCGv_i64 rl, rh, src1, src2;
    int dw;

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_VECTOR(ctx);

    rh = tcg_temp_new_i64();
    rl = tcg_temp_new_i64();
    src1 = tcg_temp_new_i64();
    src2 = tcg_temp_new_i64();

    get_avr64(rl, a->rc, false);
    get_avr64(rh, a->rc, true);

    for (dw = 0; dw < 2; dw++) {
        get_avr64(src1, a->vra, dw);
        get_avr64(src2, a->vrb, dw);
        tcg_gen_mulu2_i64(src1, src2, src1, src2);
        tcg_gen_add2_i64(rl, rh, rl, rh, src1, src2);
    }

    set_avr64(a->vrt, rl, false);
    set_avr64(a->vrt, rh, true);
    return true;
}

static bool trans_VMSUMCUD(DisasContext *ctx, arg_VA *a)
{
    TCGv_i64 tmp0, tmp1, prod1h, prod1l, prod0h, prod0l, zero;

    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tmp0 = tcg_temp_new_i64();
    tmp1 = tcg_temp_new_i64();
    prod1h = tcg_temp_new_i64();
    prod1l = tcg_temp_new_i64();
    prod0h = tcg_temp_new_i64();
    prod0l = tcg_temp_new_i64();
    zero = tcg_constant_i64(0);

    /* prod1 = vsr[vra+32].dw[1] * vsr[vrb+32].dw[1] */
    get_avr64(tmp0, a->vra, false);
    get_avr64(tmp1, a->vrb, false);
    tcg_gen_mulu2_i64(prod1l, prod1h, tmp0, tmp1);

    /* prod0 = vsr[vra+32].dw[0] * vsr[vrb+32].dw[0] */
    get_avr64(tmp0, a->vra, true);
    get_avr64(tmp1, a->vrb, true);
    tcg_gen_mulu2_i64(prod0l, prod0h, tmp0, tmp1);

    /* Sum lower 64-bits elements */
    get_avr64(tmp1, a->rc, false);
    tcg_gen_add2_i64(tmp1, tmp0, tmp1, zero, prod1l, zero);
    tcg_gen_add2_i64(tmp1, tmp0, tmp1, tmp0, prod0l, zero);

    /*
     * Discard lower 64-bits, leaving the carry into bit 64.
     * Then sum the higher 64-bit elements.
     */
    get_avr64(tmp1, a->rc, true);
    tcg_gen_add2_i64(tmp1, tmp0, tmp0, zero, tmp1, zero);
    tcg_gen_add2_i64(tmp1, tmp0, tmp1, tmp0, prod1h, zero);
    tcg_gen_add2_i64(tmp1, tmp0, tmp1, tmp0, prod0h, zero);

    /* Discard 64 more bits to complete the CHOP128(temp >> 128) */
    set_avr64(a->vrt, tmp0, false);
    set_avr64(a->vrt, zero, true);
    return true;
}

static bool do_vx_helper(DisasContext *ctx, arg_VX *a,
                         void (*gen_helper)(TCGv_ptr, TCGv_ptr, TCGv_ptr))
{
    TCGv_ptr ra, rb, rd;
    REQUIRE_VECTOR(ctx);

    ra = gen_avr_ptr(a->vra);
    rb = gen_avr_ptr(a->vrb);
    rd = gen_avr_ptr(a->vrt);
    gen_helper(rd, ra, rb);
    return true;
}

TRANS_FLAGS2(ALTIVEC_207, VADDCUQ, do_vx_helper, gen_helper_VADDCUQ)
TRANS_FLAGS2(ALTIVEC_207, VADDUQM, do_vx_helper, gen_helper_VADDUQM)

TRANS_FLAGS2(ALTIVEC_207, VPMSUMD, do_vx_helper, gen_helper_VPMSUMD)

TRANS_FLAGS2(ALTIVEC_207, VSUBCUQ, do_vx_helper, gen_helper_VSUBCUQ)
TRANS_FLAGS2(ALTIVEC_207, VSUBUQM, do_vx_helper, gen_helper_VSUBUQM)

static void gen_VADDCUW_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_not_vec(vece, a, a);
    tcg_gen_cmp_vec(TCG_COND_LTU, vece, t, a, b);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(t, vece, 1));
}

static void gen_VADDCUW_i32(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_not_i32(a, a);
    tcg_gen_setcond_i32(TCG_COND_LTU, t, a, b);
}

static void gen_VSUBCUW_vec(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_cmp_vec(TCG_COND_GEU, vece, t, a, b);
    tcg_gen_and_vec(vece, t, t, tcg_constant_vec_matching(t, vece, 1));
}

static void gen_VSUBCUW_i32(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    tcg_gen_setcond_i32(TCG_COND_GEU, t, a, b);
}

static bool do_vx_vaddsubcuw(DisasContext *ctx, arg_VX *a, int add)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_cmp_vec, 0
    };

    static const GVecGen3 op[] = {
        {
            .fniv = gen_VSUBCUW_vec,
            .fni4 = gen_VSUBCUW_i32,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
        {
            .fniv = gen_VADDCUW_vec,
            .fni4 = gen_VADDCUW_i32,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
    };

    REQUIRE_INSNS_FLAGS(ctx, ALTIVEC);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &op[add]);

    return true;
}

TRANS(VSUBCUW, do_vx_vaddsubcuw, 0)
TRANS(VADDCUW, do_vx_vaddsubcuw, 1)

/* Integer Add/Sub Saturate Instructions */
static inline void do_vadd_vsub_sat
(
    unsigned vece, TCGv_vec t, TCGv_vec qc, TCGv_vec a, TCGv_vec b,
    void (*norm_op)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec),
    void (*sat_op)(unsigned, TCGv_vec, TCGv_vec, TCGv_vec))
{
    TCGv_vec x = tcg_temp_new_vec_matching(t);
    norm_op(vece, x, a, b);
    sat_op(vece, t, a, b);
    tcg_gen_xor_vec(vece, x, x, t);
    tcg_gen_or_vec(vece, qc, qc, x);
}

static void gen_vadd_sat_u(unsigned vece, TCGv_vec t, TCGv_vec sat,
                           TCGv_vec a, TCGv_vec b)
{
    do_vadd_vsub_sat(vece, t, sat, a, b, tcg_gen_add_vec, tcg_gen_usadd_vec);
}

static void gen_vadd_sat_s(unsigned vece, TCGv_vec t, TCGv_vec sat,
                           TCGv_vec a, TCGv_vec b)
{
    do_vadd_vsub_sat(vece, t, sat, a, b, tcg_gen_add_vec, tcg_gen_ssadd_vec);
}

static void gen_vsub_sat_u(unsigned vece, TCGv_vec t, TCGv_vec sat,
                           TCGv_vec a, TCGv_vec b)
{
    do_vadd_vsub_sat(vece, t, sat, a, b, tcg_gen_sub_vec, tcg_gen_ussub_vec);
}

static void gen_vsub_sat_s(unsigned vece, TCGv_vec t, TCGv_vec sat,
                           TCGv_vec a, TCGv_vec b)
{
    do_vadd_vsub_sat(vece, t, sat, a, b, tcg_gen_sub_vec, tcg_gen_sssub_vec);
}

/*
 * Signed/Unsigned add/sub helper ops for byte/halfword/word
 * GVecGen4 struct variants.
 */
static const TCGOpcode vecop_list_sub_u[] = {
    INDEX_op_sub_vec, INDEX_op_ussub_vec, 0
};
static const TCGOpcode vecop_list_sub_s[] = {
    INDEX_op_sub_vec, INDEX_op_sssub_vec, 0
};
static const TCGOpcode vecop_list_add_u[] = {
    INDEX_op_add_vec, INDEX_op_usadd_vec, 0
};
static const TCGOpcode vecop_list_add_s[] = {
    INDEX_op_add_vec, INDEX_op_ssadd_vec, 0
};

static const GVecGen4 op_vsububs = {
    .fniv = gen_vsub_sat_u,
    .fno = gen_helper_VSUBUBS,
    .opt_opc = vecop_list_sub_u,
    .write_aofs = true,
    .vece = MO_8
};

static const GVecGen4 op_vaddubs = {
    .fniv = gen_vadd_sat_u,
    .fno = gen_helper_VADDUBS,
    .opt_opc = vecop_list_add_u,
    .write_aofs = true,
    .vece = MO_8
};

static const GVecGen4 op_vsubuhs = {
    .fniv = gen_vsub_sat_u,
    .fno = gen_helper_VSUBUHS,
    .opt_opc = vecop_list_sub_u,
    .write_aofs = true,
    .vece = MO_16
};

static const GVecGen4 op_vadduhs = {
    .fniv = gen_vadd_sat_u,
    .fno = gen_helper_VADDUHS,
    .opt_opc = vecop_list_add_u,
    .write_aofs = true,
    .vece = MO_16
};

static const GVecGen4 op_vsubuws = {
    .fniv = gen_vsub_sat_u,
    .fno = gen_helper_VSUBUWS,
    .opt_opc = vecop_list_sub_u,
    .write_aofs = true,
    .vece = MO_32
};

static const GVecGen4 op_vadduws = {
    .fniv = gen_vadd_sat_u,
    .fno = gen_helper_VADDUWS,
    .opt_opc = vecop_list_add_u,
    .write_aofs = true,
    .vece = MO_32
};

static const GVecGen4 op_vsubsbs = {
    .fniv = gen_vsub_sat_s,
    .fno = gen_helper_VSUBSBS,
    .opt_opc = vecop_list_sub_s,
    .write_aofs = true,
    .vece = MO_8
};

static const GVecGen4 op_vaddsbs = {
    .fniv = gen_vadd_sat_s,
    .fno = gen_helper_VADDSBS,
    .opt_opc = vecop_list_add_s,
    .write_aofs = true,
    .vece = MO_8
};

static const GVecGen4 op_vsubshs = {
    .fniv = gen_vsub_sat_s,
    .fno = gen_helper_VSUBSHS,
    .opt_opc = vecop_list_sub_s,
    .write_aofs = true,
    .vece = MO_16
};

static const GVecGen4 op_vaddshs = {
    .fniv = gen_vadd_sat_s,
    .fno = gen_helper_VADDSHS,
    .opt_opc = vecop_list_add_s,
    .write_aofs = true,
    .vece = MO_16
};

static const GVecGen4 op_vsubsws = {
    .fniv = gen_vsub_sat_s,
    .fno = gen_helper_VSUBSWS,
    .opt_opc = vecop_list_sub_s,
    .write_aofs = true,
    .vece = MO_32
};

static const GVecGen4 op_vaddsws = {
    .fniv = gen_vadd_sat_s,
    .fno = gen_helper_VADDSWS,
    .opt_opc = vecop_list_add_s,
    .write_aofs = true,
    .vece = MO_32
};

static bool do_vx_vadd_vsub_sat(DisasContext *ctx, arg_VX *a, const GVecGen4 *op)
{
    REQUIRE_VECTOR(ctx);
    tcg_gen_gvec_4(avr_full_offset(a->vrt), offsetof(CPUPPCState, vscr_sat),
                   avr_full_offset(a->vra), avr_full_offset(a->vrb),
                   16, 16, op);

    return true;
}

TRANS_FLAGS(ALTIVEC, VSUBUBS, do_vx_vadd_vsub_sat, &op_vsububs)
TRANS_FLAGS(ALTIVEC, VSUBUHS, do_vx_vadd_vsub_sat, &op_vsubuhs)
TRANS_FLAGS(ALTIVEC, VSUBUWS, do_vx_vadd_vsub_sat, &op_vsubuws)
TRANS_FLAGS(ALTIVEC, VSUBSBS, do_vx_vadd_vsub_sat, &op_vsubsbs)
TRANS_FLAGS(ALTIVEC, VSUBSHS, do_vx_vadd_vsub_sat, &op_vsubshs)
TRANS_FLAGS(ALTIVEC, VSUBSWS, do_vx_vadd_vsub_sat, &op_vsubsws)
TRANS_FLAGS(ALTIVEC, VADDUBS, do_vx_vadd_vsub_sat, &op_vaddubs)
TRANS_FLAGS(ALTIVEC, VADDUHS, do_vx_vadd_vsub_sat, &op_vadduhs)
TRANS_FLAGS(ALTIVEC, VADDUWS, do_vx_vadd_vsub_sat, &op_vadduws)
TRANS_FLAGS(ALTIVEC, VADDSBS, do_vx_vadd_vsub_sat, &op_vaddsbs)
TRANS_FLAGS(ALTIVEC, VADDSHS, do_vx_vadd_vsub_sat, &op_vaddshs)
TRANS_FLAGS(ALTIVEC, VADDSWS, do_vx_vadd_vsub_sat, &op_vaddsws)

static bool do_vx_vmuleo(DisasContext *ctx, arg_VX *a, bool even,
                         void (*gen_mul)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 vra, vrb, vrt0, vrt1;
    REQUIRE_VECTOR(ctx);

    vra = tcg_temp_new_i64();
    vrb = tcg_temp_new_i64();
    vrt0 = tcg_temp_new_i64();
    vrt1 = tcg_temp_new_i64();

    get_avr64(vra, a->vra, even);
    get_avr64(vrb, a->vrb, even);
    gen_mul(vrt0, vrt1, vra, vrb);
    set_avr64(a->vrt, vrt0, false);
    set_avr64(a->vrt, vrt1, true);
    return true;
}

static bool trans_VMULLD(DisasContext *ctx, arg_VX *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_mul(MO_64, avr_full_offset(a->vrt), avr_full_offset(a->vra),
                     avr_full_offset(a->vrb), 16, 16);

    return true;
}

TRANS_FLAGS(ALTIVEC, VMULESB, do_vx_helper, gen_helper_VMULESB)
TRANS_FLAGS(ALTIVEC, VMULOSB, do_vx_helper, gen_helper_VMULOSB)
TRANS_FLAGS(ALTIVEC, VMULEUB, do_vx_helper, gen_helper_VMULEUB)
TRANS_FLAGS(ALTIVEC, VMULOUB, do_vx_helper, gen_helper_VMULOUB)
TRANS_FLAGS(ALTIVEC, VMULESH, do_vx_helper, gen_helper_VMULESH)
TRANS_FLAGS(ALTIVEC, VMULOSH, do_vx_helper, gen_helper_VMULOSH)
TRANS_FLAGS(ALTIVEC, VMULEUH, do_vx_helper, gen_helper_VMULEUH)
TRANS_FLAGS(ALTIVEC, VMULOUH, do_vx_helper, gen_helper_VMULOUH)
TRANS_FLAGS2(ALTIVEC_207, VMULESW, do_vx_helper, gen_helper_VMULESW)
TRANS_FLAGS2(ALTIVEC_207, VMULOSW, do_vx_helper, gen_helper_VMULOSW)
TRANS_FLAGS2(ALTIVEC_207, VMULEUW, do_vx_helper, gen_helper_VMULEUW)
TRANS_FLAGS2(ALTIVEC_207, VMULOUW, do_vx_helper, gen_helper_VMULOUW)
TRANS_FLAGS2(ISA310, VMULESD, do_vx_vmuleo, true , tcg_gen_muls2_i64)
TRANS_FLAGS2(ISA310, VMULOSD, do_vx_vmuleo, false, tcg_gen_muls2_i64)
TRANS_FLAGS2(ISA310, VMULEUD, do_vx_vmuleo, true , tcg_gen_mulu2_i64)
TRANS_FLAGS2(ISA310, VMULOUD, do_vx_vmuleo, false, tcg_gen_mulu2_i64)

static void do_vx_vmulhw_i64(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b, bool sign)
{
    TCGv_i64 hh, lh, temp;

    hh = tcg_temp_new_i64();
    lh = tcg_temp_new_i64();
    temp = tcg_temp_new_i64();

    if (sign) {
        tcg_gen_ext32s_i64(lh, a);
        tcg_gen_ext32s_i64(temp, b);
    } else {
        tcg_gen_ext32u_i64(lh, a);
        tcg_gen_ext32u_i64(temp, b);
    }
    tcg_gen_mul_i64(lh, lh, temp);

    if (sign) {
        tcg_gen_sari_i64(hh, a, 32);
        tcg_gen_sari_i64(temp, b, 32);
    } else {
        tcg_gen_shri_i64(hh, a, 32);
        tcg_gen_shri_i64(temp, b, 32);
    }
    tcg_gen_mul_i64(hh, hh, temp);

    tcg_gen_shri_i64(lh, lh, 32);
    tcg_gen_deposit_i64(t, hh, lh, 0, 32);
}

static void do_vx_vmulhd_i64(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b, bool sign)
{
    TCGv_i64 tlow;

    tlow  = tcg_temp_new_i64();
    if (sign) {
        tcg_gen_muls2_i64(tlow, t, a, b);
    } else {
        tcg_gen_mulu2_i64(tlow, t, a, b);
    }
}

static bool do_vx_mulh(DisasContext *ctx, arg_VX *a, bool sign,
                       void (*func)(TCGv_i64, TCGv_i64, TCGv_i64, bool))
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    REQUIRE_VECTOR(ctx);

    TCGv_i64 vra, vrb, vrt;
    int i;

    vra = tcg_temp_new_i64();
    vrb = tcg_temp_new_i64();
    vrt = tcg_temp_new_i64();

    for (i = 0; i < 2; i++) {
        get_avr64(vra, a->vra, i);
        get_avr64(vrb, a->vrb, i);
        get_avr64(vrt, a->vrt, i);

        func(vrt, vra, vrb, sign);

        set_avr64(a->vrt, vrt, i);
    }
    return true;
}

TRANS(VMULHSW, do_vx_mulh, true , do_vx_vmulhw_i64)
TRANS(VMULHSD, do_vx_mulh, true , do_vx_vmulhd_i64)
TRANS(VMULHUW, do_vx_mulh, false, do_vx_vmulhw_i64)
TRANS(VMULHUD, do_vx_mulh, false, do_vx_vmulhd_i64)

static void do_vavg(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b,
                    void (*gen_shr_vec)(unsigned, TCGv_vec, TCGv_vec, int64_t))
{
    TCGv_vec tmp = tcg_temp_new_vec_matching(t);
    tcg_gen_or_vec(vece, tmp, a, b);
    tcg_gen_and_vec(vece, tmp, tmp, tcg_constant_vec_matching(t, vece, 1));
    gen_shr_vec(vece, a, a, 1);
    gen_shr_vec(vece, b, b, 1);
    tcg_gen_add_vec(vece, t, a, b);
    tcg_gen_add_vec(vece, t, t, tmp);
}

QEMU_FLATTEN
static void gen_vavgu(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_shri_vec);
}

QEMU_FLATTEN
static void gen_vavgs(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    do_vavg(vece, t, a, b, tcg_gen_sari_vec);
}

static bool do_vx_vavg(DisasContext *ctx, arg_VX *a, int sign, int vece)
{
    static const TCGOpcode vecop_list_s[] = {
        INDEX_op_add_vec, INDEX_op_sari_vec, 0
    };
    static const TCGOpcode vecop_list_u[] = {
        INDEX_op_add_vec, INDEX_op_shri_vec, 0
    };

    static const GVecGen3 op[2][3] = {
        {
            {
                .fniv = gen_vavgu,
                .fno = gen_helper_VAVGUB,
                .opt_opc = vecop_list_u,
                .vece = MO_8
            },
            {
                .fniv = gen_vavgu,
                .fno = gen_helper_VAVGUH,
                .opt_opc = vecop_list_u,
                .vece = MO_16
            },
            {
                .fniv = gen_vavgu,
                .fno = gen_helper_VAVGUW,
                .opt_opc = vecop_list_u,
                .vece = MO_32
            },
        },
        {
            {
                .fniv = gen_vavgs,
                .fno = gen_helper_VAVGSB,
                .opt_opc = vecop_list_s,
                .vece = MO_8
            },
            {
                .fniv = gen_vavgs,
                .fno = gen_helper_VAVGSH,
                .opt_opc = vecop_list_s,
                .vece = MO_16
            },
            {
                .fniv = gen_vavgs,
                .fno = gen_helper_VAVGSW,
                .opt_opc = vecop_list_s,
                .vece = MO_32
            },
        },
    };

    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &op[sign][vece]);


    return true;
}


TRANS_FLAGS(ALTIVEC, VAVGSB, do_vx_vavg, 1, MO_8)
TRANS_FLAGS(ALTIVEC, VAVGSH, do_vx_vavg, 1, MO_16)
TRANS_FLAGS(ALTIVEC, VAVGSW, do_vx_vavg, 1, MO_32)
TRANS_FLAGS(ALTIVEC, VAVGUB, do_vx_vavg, 0, MO_8)
TRANS_FLAGS(ALTIVEC, VAVGUH, do_vx_vavg, 0, MO_16)
TRANS_FLAGS(ALTIVEC, VAVGUW, do_vx_vavg, 0, MO_32)

static void gen_vabsdu(unsigned vece, TCGv_vec t, TCGv_vec a, TCGv_vec b)
{
    tcg_gen_umax_vec(vece, t, a, b);
    tcg_gen_umin_vec(vece, a, a, b);
    tcg_gen_sub_vec(vece, t, t, a);
}

static bool do_vabsdu(DisasContext *ctx, arg_VX *a, const int vece)
{
    static const TCGOpcode vecop_list[] = {
        INDEX_op_umax_vec, INDEX_op_umin_vec, INDEX_op_sub_vec, 0
    };

    static const GVecGen3 op[] = {
        {
            .fniv = gen_vabsdu,
            .fno = gen_helper_VABSDUB,
            .opt_opc = vecop_list,
            .vece = MO_8
        },
        {
            .fniv = gen_vabsdu,
            .fno = gen_helper_VABSDUH,
            .opt_opc = vecop_list,
            .vece = MO_16
        },
        {
            .fniv = gen_vabsdu,
            .fno = gen_helper_VABSDUW,
            .opt_opc = vecop_list,
            .vece = MO_32
        },
    };

    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &op[vece]);

    return true;
}

TRANS_FLAGS2(ISA300, VABSDUB, do_vabsdu, MO_8)
TRANS_FLAGS2(ISA300, VABSDUH, do_vabsdu, MO_16)
TRANS_FLAGS2(ISA300, VABSDUW, do_vabsdu, MO_32)

static bool do_vdiv_vmod(DisasContext *ctx, arg_VX *a, const int vece,
                         void (*func_32)(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b),
                         void (*func_64)(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b))
{
    const GVecGen3 op = {
        .fni4 = func_32,
        .fni8 = func_64,
        .vece = vece
    };

    REQUIRE_VECTOR(ctx);

    tcg_gen_gvec_3(avr_full_offset(a->vrt), avr_full_offset(a->vra),
                   avr_full_offset(a->vrb), 16, 16, &op);

    return true;
}

#define DIVU32(NAME, DIV)                                               \
static void NAME(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)                    \
{                                                                       \
    TCGv_i32 zero = tcg_constant_i32(0);                                \
    TCGv_i32 one = tcg_constant_i32(1);                                 \
    tcg_gen_movcond_i32(TCG_COND_EQ, b, b, zero, one, b);               \
    DIV(t, a, b);                                                       \
}

#define DIVS32(NAME, DIV)                                               \
static void NAME(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)                    \
{                                                                       \
    TCGv_i32 t0 = tcg_temp_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_new_i32();                                   \
    tcg_gen_setcondi_i32(TCG_COND_EQ, t0, a, INT32_MIN);                \
    tcg_gen_setcondi_i32(TCG_COND_EQ, t1, b, -1);                       \
    tcg_gen_and_i32(t0, t0, t1);                                        \
    tcg_gen_setcondi_i32(TCG_COND_EQ, t1, b, 0);                        \
    tcg_gen_or_i32(t0, t0, t1);                                         \
    tcg_gen_movi_i32(t1, 0);                                            \
    tcg_gen_movcond_i32(TCG_COND_NE, b, t0, t1, t0, b);                 \
    DIV(t, a, b);                                                       \
}

#define DIVU64(NAME, DIV)                                               \
static void NAME(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)                    \
{                                                                       \
    TCGv_i64 zero = tcg_constant_i64(0);                                \
    TCGv_i64 one = tcg_constant_i64(1);                                 \
    tcg_gen_movcond_i64(TCG_COND_EQ, b, b, zero, one, b);               \
    DIV(t, a, b);                                                       \
}

#define DIVS64(NAME, DIV)                                               \
static void NAME(TCGv_i64 t, TCGv_i64 a, TCGv_i64 b)                    \
{                                                                       \
    TCGv_i64 t0 = tcg_temp_new_i64();                                   \
    TCGv_i64 t1 = tcg_temp_new_i64();                                   \
    tcg_gen_setcondi_i64(TCG_COND_EQ, t0, a, INT64_MIN);                \
    tcg_gen_setcondi_i64(TCG_COND_EQ, t1, b, -1);                       \
    tcg_gen_and_i64(t0, t0, t1);                                        \
    tcg_gen_setcondi_i64(TCG_COND_EQ, t1, b, 0);                        \
    tcg_gen_or_i64(t0, t0, t1);                                         \
    tcg_gen_movi_i64(t1, 0);                                            \
    tcg_gen_movcond_i64(TCG_COND_NE, b, t0, t1, t0, b);                 \
    DIV(t, a, b);                                                       \
}

DIVS32(do_divsw, tcg_gen_div_i32)
DIVU32(do_divuw, tcg_gen_divu_i32)
DIVS64(do_divsd, tcg_gen_div_i64)
DIVU64(do_divud, tcg_gen_divu_i64)

TRANS_FLAGS2(ISA310, VDIVSW, do_vdiv_vmod, MO_32, do_divsw, NULL)
TRANS_FLAGS2(ISA310, VDIVUW, do_vdiv_vmod, MO_32, do_divuw, NULL)
TRANS_FLAGS2(ISA310, VDIVSD, do_vdiv_vmod, MO_64, NULL, do_divsd)
TRANS_FLAGS2(ISA310, VDIVUD, do_vdiv_vmod, MO_64, NULL, do_divud)
TRANS_FLAGS2(ISA310, VDIVSQ, do_vx_helper, gen_helper_VDIVSQ)
TRANS_FLAGS2(ISA310, VDIVUQ, do_vx_helper, gen_helper_VDIVUQ)

static void do_dives_i32(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i64 val1, val2;

    val1 = tcg_temp_new_i64();
    val2 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(val1, a);
    tcg_gen_ext_i32_i64(val2, b);

    /* (a << 32)/b */
    tcg_gen_shli_i64(val1, val1, 32);
    tcg_gen_div_i64(val1, val1, val2);

    /* if quotient doesn't fit in 32 bits the result is undefined */
    tcg_gen_extrl_i64_i32(t, val1);
}

static void do_diveu_i32(TCGv_i32 t, TCGv_i32 a, TCGv_i32 b)
{
    TCGv_i64 val1, val2;

    val1 = tcg_temp_new_i64();
    val2 = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(val1, a);
    tcg_gen_extu_i32_i64(val2, b);

    /* (a << 32)/b */
    tcg_gen_shli_i64(val1, val1, 32);
    tcg_gen_divu_i64(val1, val1, val2);

    /* if quotient doesn't fit in 32 bits the result is undefined */
    tcg_gen_extrl_i64_i32(t, val1);
}

DIVS32(do_divesw, do_dives_i32)
DIVU32(do_diveuw, do_diveu_i32)

DIVS32(do_modsw, tcg_gen_rem_i32)
DIVU32(do_moduw, tcg_gen_remu_i32)
DIVS64(do_modsd, tcg_gen_rem_i64)
DIVU64(do_modud, tcg_gen_remu_i64)

TRANS_FLAGS2(ISA310, VDIVESW, do_vdiv_vmod, MO_32, do_divesw, NULL)
TRANS_FLAGS2(ISA310, VDIVEUW, do_vdiv_vmod, MO_32, do_diveuw, NULL)
TRANS_FLAGS2(ISA310, VDIVESD, do_vx_helper, gen_helper_VDIVESD)
TRANS_FLAGS2(ISA310, VDIVEUD, do_vx_helper, gen_helper_VDIVEUD)
TRANS_FLAGS2(ISA310, VDIVESQ, do_vx_helper, gen_helper_VDIVESQ)
TRANS_FLAGS2(ISA310, VDIVEUQ, do_vx_helper, gen_helper_VDIVEUQ)

TRANS_FLAGS2(ISA310, VMODSW, do_vdiv_vmod, MO_32, do_modsw , NULL)
TRANS_FLAGS2(ISA310, VMODUW, do_vdiv_vmod, MO_32, do_moduw, NULL)
TRANS_FLAGS2(ISA310, VMODSD, do_vdiv_vmod, MO_64, NULL, do_modsd)
TRANS_FLAGS2(ISA310, VMODUD, do_vdiv_vmod, MO_64, NULL, do_modud)
TRANS_FLAGS2(ISA310, VMODSQ, do_vx_helper, gen_helper_VMODSQ)
TRANS_FLAGS2(ISA310, VMODUQ, do_vx_helper, gen_helper_VMODUQ)

#undef DIVS32
#undef DIVU32
#undef DIVS64
#undef DIVU64

#undef GEN_VXFORM
#undef GEN_VXFORM_207
#undef GEN_VXFORM_DUAL
#undef GEN_VXRFORM_DUAL
#undef GEN_VXRFORM1
#undef GEN_VXRFORM
#undef GEN_VXFORM_VSPLTI
#undef GEN_VXFORM_NOA
#undef GEN_VXFORM_UIMM
#undef GEN_VAFORM_PAIRED

#undef GEN_BCD2
