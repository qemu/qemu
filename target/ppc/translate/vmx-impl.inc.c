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
    tcg_gen_addi_ptr(r, cpu_env, avr_full_offset(reg));
    return r;
}

#define GEN_VR_LDX(name, opc2, opc3)                                          \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 avr;                                                             \
    if (unlikely(!ctx->altivec_enabled)) {                                    \
        gen_exception(ctx, POWERPC_EXCP_VPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    avr = tcg_temp_new_i64();                                                 \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_andi_tl(EA, EA, ~0xf);                                            \
    /*                                                                        \
     * We only need to swap high and low halves. gen_qemu_ld64_i64            \
     * does necessary 64-bit byteswap already.                                \
     */                                                                       \
    if (ctx->le_mode) {                                                       \
        gen_qemu_ld64_i64(ctx, avr, EA);                                      \
        set_avr64(rD(ctx->opcode), avr, false);                               \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64_i64(ctx, avr, EA);                                      \
        set_avr64(rD(ctx->opcode), avr, true);                                \
    } else {                                                                  \
        gen_qemu_ld64_i64(ctx, avr, EA);                                      \
        set_avr64(rD(ctx->opcode), avr, true);                                \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64_i64(ctx, avr, EA);                                      \
        set_avr64(rD(ctx->opcode), avr, false);                               \
    }                                                                         \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(avr);                                                   \
}

#define GEN_VR_STX(name, opc2, opc3)                                          \
static void gen_st##name(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    TCGv_i64 avr;                                                             \
    if (unlikely(!ctx->altivec_enabled)) {                                    \
        gen_exception(ctx, POWERPC_EXCP_VPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    avr = tcg_temp_new_i64();                                                 \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_andi_tl(EA, EA, ~0xf);                                            \
    /*                                                                        \
     * We only need to swap high and low halves. gen_qemu_st64_i64            \
     * does necessary 64-bit byteswap already.                                \
     */                                                                       \
    if (ctx->le_mode) {                                                       \
        get_avr64(avr, rD(ctx->opcode), false);                               \
        gen_qemu_st64_i64(ctx, avr, EA);                                      \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        get_avr64(avr, rD(ctx->opcode), true);                                \
        gen_qemu_st64_i64(ctx, avr, EA);                                      \
    } else {                                                                  \
        get_avr64(avr, rD(ctx->opcode), true);                                \
        gen_qemu_st64_i64(ctx, avr, EA);                                      \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        get_avr64(avr, rD(ctx->opcode), false);                               \
        gen_qemu_st64_i64(ctx, avr, EA);                                      \
    }                                                                         \
    tcg_temp_free(EA);                                                        \
    tcg_temp_free_i64(avr);                                                   \
}

#define GEN_VR_LVE(name, opc2, opc3, size)                              \
static void gen_lve##name(DisasContext *ctx)                            \
    {                                                                   \
        TCGv EA;                                                        \
        TCGv_ptr rs;                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        gen_set_access_type(ctx, ACCESS_INT);                           \
        EA = tcg_temp_new();                                            \
        gen_addr_reg_index(ctx, EA);                                    \
        if (size > 1) {                                                 \
            tcg_gen_andi_tl(EA, EA, ~(size - 1));                       \
        }                                                               \
        rs = gen_avr_ptr(rS(ctx->opcode));                              \
        gen_helper_lve##name(cpu_env, rs, EA);                          \
        tcg_temp_free(EA);                                              \
        tcg_temp_free_ptr(rs);                                          \
    }

#define GEN_VR_STVE(name, opc2, opc3, size)                             \
static void gen_stve##name(DisasContext *ctx)                           \
    {                                                                   \
        TCGv EA;                                                        \
        TCGv_ptr rs;                                                    \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        gen_set_access_type(ctx, ACCESS_INT);                           \
        EA = tcg_temp_new();                                            \
        gen_addr_reg_index(ctx, EA);                                    \
        if (size > 1) {                                                 \
            tcg_gen_andi_tl(EA, EA, ~(size - 1));                       \
        }                                                               \
        rs = gen_avr_ptr(rS(ctx->opcode));                              \
        gen_helper_stve##name(cpu_env, rs, EA);                         \
        tcg_temp_free(EA);                                              \
        tcg_temp_free_ptr(rs);                                          \
    }

GEN_VR_LDX(lvx, 0x07, 0x03);
/* As we don't emulate the cache, lvxl is stricly equivalent to lvx */
GEN_VR_LDX(lvxl, 0x07, 0x0B);

GEN_VR_LVE(bx, 0x07, 0x00, 1);
GEN_VR_LVE(hx, 0x07, 0x01, 2);
GEN_VR_LVE(wx, 0x07, 0x02, 4);

GEN_VR_STX(svx, 0x07, 0x07);
/* As we don't emulate the cache, stvxl is stricly equivalent to stvx */
GEN_VR_STX(svxl, 0x07, 0x0F);

GEN_VR_STVE(bx, 0x07, 0x04, 1);
GEN_VR_STVE(hx, 0x07, 0x05, 2);
GEN_VR_STVE(wx, 0x07, 0x06, 4);

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
    gen_helper_mfvscr(t, cpu_env);
    tcg_gen_extu_i32_i64(avr, t);
    set_avr64(rD(ctx->opcode), avr, false);
    tcg_temp_free_i32(t);
    tcg_temp_free_i64(avr);
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
#ifdef HOST_WORDS_BIGENDIAN
    bofs += 3 * 4;
#endif

    tcg_gen_ld_i32(val, cpu_env, bofs);
    gen_helper_mtvscr(cpu_env, val);
    tcg_temp_free_i32(val);
}

#define GEN_VX_VMUL10(name, add_cin, ret_carry)                         \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_i64 t0;                                                        \
    TCGv_i64 t1;                                                        \
    TCGv_i64 t2;                                                        \
    TCGv_i64 avr;                                                       \
    TCGv_i64 ten, z;                                                    \
                                                                        \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
                                                                        \
    t0 = tcg_temp_new_i64();                                            \
    t1 = tcg_temp_new_i64();                                            \
    t2 = tcg_temp_new_i64();                                            \
    avr = tcg_temp_new_i64();                                           \
    ten = tcg_const_i64(10);                                            \
    z = tcg_const_i64(0);                                               \
                                                                        \
    if (add_cin) {                                                      \
        get_avr64(avr, rA(ctx->opcode), false);                         \
        tcg_gen_mulu2_i64(t0, t1, avr, ten);                            \
        get_avr64(avr, rB(ctx->opcode), false);                         \
        tcg_gen_andi_i64(t2, avr, 0xF);                                 \
        tcg_gen_add2_i64(avr, t2, t0, t1, t2, z);                       \
        set_avr64(rD(ctx->opcode), avr, false);                         \
    } else {                                                            \
        get_avr64(avr, rA(ctx->opcode), false);                         \
        tcg_gen_mulu2_i64(avr, t2, avr, ten);                           \
        set_avr64(rD(ctx->opcode), avr, false);                         \
    }                                                                   \
                                                                        \
    if (ret_carry) {                                                    \
        get_avr64(avr, rA(ctx->opcode), true);                          \
        tcg_gen_mulu2_i64(t0, t1, avr, ten);                            \
        tcg_gen_add2_i64(t0, avr, t0, t1, t2, z);                       \
        set_avr64(rD(ctx->opcode), avr, false);                         \
        set_avr64(rD(ctx->opcode), z, true);                            \
    } else {                                                            \
        get_avr64(avr, rA(ctx->opcode), true);                          \
        tcg_gen_mul_i64(t0, avr, ten);                                  \
        tcg_gen_add_i64(avr, t0, t2);                                   \
        set_avr64(rD(ctx->opcode), avr, true);                          \
    }                                                                   \
                                                                        \
    tcg_temp_free_i64(t0);                                              \
    tcg_temp_free_i64(t1);                                              \
    tcg_temp_free_i64(t2);                                              \
    tcg_temp_free_i64(avr);                                             \
    tcg_temp_free_i64(ten);                                             \
    tcg_temp_free_i64(z);                                               \
}                                                                       \

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

/* Logical operations */
GEN_VXFORM_V(vand, MO_64, tcg_gen_gvec_and, 2, 16);
GEN_VXFORM_V(vandc, MO_64, tcg_gen_gvec_andc, 2, 17);
GEN_VXFORM_V(vor, MO_64, tcg_gen_gvec_or, 2, 18);
GEN_VXFORM_V(vxor, MO_64, tcg_gen_gvec_xor, 2, 19);
GEN_VXFORM_V(vnor, MO_64, tcg_gen_gvec_nor, 2, 20);
GEN_VXFORM_V(veqv, MO_64, tcg_gen_gvec_eqv, 2, 26);
GEN_VXFORM_V(vnand, MO_64, tcg_gen_gvec_nand, 2, 22);
GEN_VXFORM_V(vorc, MO_64, tcg_gen_gvec_orc, 2, 21);

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
    tcg_temp_free_ptr(ra);                                              \
    tcg_temp_free_ptr(rb);                                              \
    tcg_temp_free_ptr(rd);                                              \
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
    gen_helper_##name(cpu_env, rd, ra, rb);                             \
    tcg_temp_free_ptr(ra);                                              \
    tcg_temp_free_ptr(rb);                                              \
    tcg_temp_free_ptr(rd);                                              \
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
    tcg_temp_free_ptr(ra);                                              \
    tcg_temp_free_ptr(rb);                                              \
    tcg_temp_free_ptr(rc);                                              \
    tcg_temp_free_ptr(rd);                                              \
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
    tcg_temp_free_ptr(rb);                                              \
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
GEN_VXFORM_V(vmaxub, MO_8, tcg_gen_gvec_umax, 1, 0);
GEN_VXFORM_V(vmaxuh, MO_16, tcg_gen_gvec_umax, 1, 1);
GEN_VXFORM_V(vmaxuw, MO_32, tcg_gen_gvec_umax, 1, 2);
GEN_VXFORM_V(vmaxud, MO_64, tcg_gen_gvec_umax, 1, 3);
GEN_VXFORM_V(vmaxsb, MO_8, tcg_gen_gvec_smax, 1, 4);
GEN_VXFORM_V(vmaxsh, MO_16, tcg_gen_gvec_smax, 1, 5);
GEN_VXFORM_V(vmaxsw, MO_32, tcg_gen_gvec_smax, 1, 6);
GEN_VXFORM_V(vmaxsd, MO_64, tcg_gen_gvec_smax, 1, 7);
GEN_VXFORM_V(vminub, MO_8, tcg_gen_gvec_umin, 1, 8);
GEN_VXFORM_V(vminuh, MO_16, tcg_gen_gvec_umin, 1, 9);
GEN_VXFORM_V(vminuw, MO_32, tcg_gen_gvec_umin, 1, 10);
GEN_VXFORM_V(vminud, MO_64, tcg_gen_gvec_umin, 1, 11);
GEN_VXFORM_V(vminsb, MO_8, tcg_gen_gvec_smin, 1, 12);
GEN_VXFORM_V(vminsh, MO_16, tcg_gen_gvec_smin, 1, 13);
GEN_VXFORM_V(vminsw, MO_32, tcg_gen_gvec_smin, 1, 14);
GEN_VXFORM_V(vminsd, MO_64, tcg_gen_gvec_smin, 1, 15);
GEN_VXFORM(vavgub, 1, 16);
GEN_VXFORM(vabsdub, 1, 16);
GEN_VXFORM_DUAL(vavgub, PPC_ALTIVEC, PPC_NONE, \
                vabsdub, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vavguh, 1, 17);
GEN_VXFORM(vabsduh, 1, 17);
GEN_VXFORM_DUAL(vavguh, PPC_ALTIVEC, PPC_NONE, \
                vabsduh, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vavguw, 1, 18);
GEN_VXFORM(vabsduw, 1, 18);
GEN_VXFORM_DUAL(vavguw, PPC_ALTIVEC, PPC_NONE, \
                vabsduw, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vavgsb, 1, 20);
GEN_VXFORM(vavgsh, 1, 21);
GEN_VXFORM(vavgsw, 1, 22);
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

    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(avr);
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

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(avr);
}

/*
 * lvsl VRT,RA,RB - Load Vector for Shift Left
 *
 * Let the EA be the sum (rA|0)+(rB). Let sh=EA[28–31].
 * Let X be the 32-byte value 0x00 || 0x01 || 0x02 || ... || 0x1E || 0x1F.
 * Bytes sh:sh+15 of X are placed into vD.
 */
static void trans_lvsl(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    TCGv_i64 result = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv EA = tcg_temp_new();

    /* Get sh(from description) by anding EA with 0xf. */
    gen_addr_reg_index(ctx, EA);
    tcg_gen_extu_tl_i64(sh, EA);
    tcg_gen_andi_i64(sh, sh, 0xfULL);

    /*
     * Create bytes sh:sh+7 of X(from description) and place them in
     * higher doubleword of vD.
     */
    tcg_gen_muli_i64(sh, sh, 0x0101010101010101ULL);
    tcg_gen_addi_i64(result, sh, 0x0001020304050607ull);
    set_avr64(VT, result, true);
    /*
     * Create bytes sh+8:sh+15 of X(from description) and place them in
     * lower doubleword of vD.
     */
    tcg_gen_addi_i64(result, sh, 0x08090a0b0c0d0e0fULL);
    set_avr64(VT, result, false);

    tcg_temp_free_i64(result);
    tcg_temp_free_i64(sh);
    tcg_temp_free(EA);
}

/*
 * lvsr VRT,RA,RB - Load Vector for Shift Right
 *
 * Let the EA be the sum (rA|0)+(rB). Let sh=EA[28–31].
 * Let X be the 32-byte value 0x00 || 0x01 || 0x02 || ... || 0x1E || 0x1F.
 * Bytes (16-sh):(31-sh) of X are placed into vD.
 */
static void trans_lvsr(DisasContext *ctx)
{
    int VT = rD(ctx->opcode);
    TCGv_i64 result = tcg_temp_new_i64();
    TCGv_i64 sh = tcg_temp_new_i64();
    TCGv EA = tcg_temp_new();


    /* Get sh(from description) by anding EA with 0xf. */
    gen_addr_reg_index(ctx, EA);
    tcg_gen_extu_tl_i64(sh, EA);
    tcg_gen_andi_i64(sh, sh, 0xfULL);

    /*
     * Create bytes (16-sh):(23-sh) of X(from description) and place them in
     * higher doubleword of vD.
     */
    tcg_gen_muli_i64(sh, sh, 0x0101010101010101ULL);
    tcg_gen_subfi_i64(result, 0x1011121314151617ULL, sh);
    set_avr64(VT, result, true);
    /*
     * Create bytes (24-sh):(32-sh) of X(from description) and place them in
     * lower doubleword of vD.
     */
    tcg_gen_subfi_i64(result, 0x18191a1b1c1d1e1fULL, sh);
    set_avr64(VT, result, false);

    tcg_temp_free_i64(result);
    tcg_temp_free_i64(sh);
    tcg_temp_free(EA);
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

    tcg_temp_free_i64(avr);
    tcg_temp_free_i64(sh);
    tcg_temp_free_i64(carry);
    tcg_temp_free_i64(tmp);
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

    tcg_temp_free_i64(avr);
    tcg_temp_free_i64(sh);
    tcg_temp_free_i64(carry);
    tcg_temp_free_i64(tmp);
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

    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(tcg_mask);
    tcg_temp_free_i64(result[0]);
    tcg_temp_free_i64(result[1]);
    tcg_temp_free_i64(avr[0]);
    tcg_temp_free_i64(avr[1]);
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
        tcg_gen_ld_i32(tmp, cpu_env,
            offsetof(CPUPPCState, vsr[32 + VB].u64[0]) + i * 4);
        tcg_gen_clzi_i32(tmp, tmp, 32);
        tcg_gen_st_i32(tmp, cpu_env,
            offsetof(CPUPPCState, vsr[32 + VT].u64[0]) + i * 4);
    }

    tcg_temp_free_i32(tmp);
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

    tcg_temp_free_i64(avr);
}

GEN_VXFORM(vmuloub, 4, 0);
GEN_VXFORM(vmulouh, 4, 1);
GEN_VXFORM(vmulouw, 4, 2);
GEN_VXFORM(vmuluwm, 4, 2);
GEN_VXFORM_DUAL(vmulouw, PPC_ALTIVEC, PPC_NONE,
                vmuluwm, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM(vmulosb, 4, 4);
GEN_VXFORM(vmulosh, 4, 5);
GEN_VXFORM(vmulosw, 4, 6);
GEN_VXFORM(vmuleub, 4, 8);
GEN_VXFORM(vmuleuh, 4, 9);
GEN_VXFORM(vmuleuw, 4, 10);
GEN_VXFORM(vmulesb, 4, 12);
GEN_VXFORM(vmulesh, 4, 13);
GEN_VXFORM(vmulesw, 4, 14);
GEN_VXFORM_V(vslb, MO_8, tcg_gen_gvec_shlv, 2, 4);
GEN_VXFORM_V(vslh, MO_16, tcg_gen_gvec_shlv, 2, 5);
GEN_VXFORM_V(vslw, MO_32, tcg_gen_gvec_shlv, 2, 6);
GEN_VXFORM(vrlwnm, 2, 6);
GEN_VXFORM_DUAL(vslw, PPC_ALTIVEC, PPC_NONE, \
                vrlwnm, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_V(vsld, MO_64, tcg_gen_gvec_shlv, 2, 23);
GEN_VXFORM_V(vsrb, MO_8, tcg_gen_gvec_shrv, 2, 8);
GEN_VXFORM_V(vsrh, MO_16, tcg_gen_gvec_shrv, 2, 9);
GEN_VXFORM_V(vsrw, MO_32, tcg_gen_gvec_shrv, 2, 10);
GEN_VXFORM_V(vsrd, MO_64, tcg_gen_gvec_shrv, 2, 27);
GEN_VXFORM_V(vsrab, MO_8, tcg_gen_gvec_sarv, 2, 12);
GEN_VXFORM_V(vsrah, MO_16, tcg_gen_gvec_sarv, 2, 13);
GEN_VXFORM_V(vsraw, MO_32, tcg_gen_gvec_sarv, 2, 14);
GEN_VXFORM_V(vsrad, MO_64, tcg_gen_gvec_sarv, 2, 15);
GEN_VXFORM(vsrv, 2, 28);
GEN_VXFORM(vslv, 2, 29);
GEN_VXFORM(vslo, 6, 16);
GEN_VXFORM(vsro, 6, 17);
GEN_VXFORM(vaddcuw, 0, 6);
GEN_VXFORM(vsubcuw, 0, 22);

#define GEN_VXFORM_SAT(NAME, VECE, NORM, SAT, OPC2, OPC3)               \
static void glue(glue(gen_, NAME), _vec)(unsigned vece, TCGv_vec t,     \
                                         TCGv_vec sat, TCGv_vec a,      \
                                         TCGv_vec b)                    \
{                                                                       \
    TCGv_vec x = tcg_temp_new_vec_matching(t);                          \
    glue(glue(tcg_gen_, NORM), _vec)(VECE, x, a, b);                    \
    glue(glue(tcg_gen_, SAT), _vec)(VECE, t, a, b);                     \
    tcg_gen_cmp_vec(TCG_COND_NE, VECE, x, x, t);                        \
    tcg_gen_or_vec(VECE, sat, sat, x);                                  \
    tcg_temp_free_vec(x);                                               \
}                                                                       \
static void glue(gen_, NAME)(DisasContext *ctx)                         \
{                                                                       \
    static const TCGOpcode vecop_list[] = {                             \
        glue(glue(INDEX_op_, NORM), _vec),                              \
        glue(glue(INDEX_op_, SAT), _vec),                               \
        INDEX_op_cmp_vec, 0                                             \
    };                                                                  \
    static const GVecGen4 g = {                                         \
        .fniv = glue(glue(gen_, NAME), _vec),                           \
        .fno = glue(gen_helper_, NAME),                                 \
        .opt_opc = vecop_list,                                          \
        .write_aofs = true,                                             \
        .vece = VECE,                                                   \
    };                                                                  \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    tcg_gen_gvec_4(avr_full_offset(rD(ctx->opcode)),                    \
                   offsetof(CPUPPCState, vscr_sat),                     \
                   avr_full_offset(rA(ctx->opcode)),                    \
                   avr_full_offset(rB(ctx->opcode)),                    \
                   16, 16, &g);                                         \
}

GEN_VXFORM_SAT(vaddubs, MO_8, add, usadd, 0, 8);
GEN_VXFORM_DUAL_EXT(vaddubs, PPC_ALTIVEC, PPC_NONE, 0,       \
                    vmul10uq, PPC_NONE, PPC2_ISA300, 0x0000F800)
GEN_VXFORM_SAT(vadduhs, MO_16, add, usadd, 0, 9);
GEN_VXFORM_DUAL(vadduhs, PPC_ALTIVEC, PPC_NONE, \
                vmul10euq, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_SAT(vadduws, MO_32, add, usadd, 0, 10);
GEN_VXFORM_SAT(vaddsbs, MO_8, add, ssadd, 0, 12);
GEN_VXFORM_SAT(vaddshs, MO_16, add, ssadd, 0, 13);
GEN_VXFORM_SAT(vaddsws, MO_32, add, ssadd, 0, 14);
GEN_VXFORM_SAT(vsububs, MO_8, sub, ussub, 0, 24);
GEN_VXFORM_SAT(vsubuhs, MO_16, sub, ussub, 0, 25);
GEN_VXFORM_SAT(vsubuws, MO_32, sub, ussub, 0, 26);
GEN_VXFORM_SAT(vsubsbs, MO_8, sub, sssub, 0, 28);
GEN_VXFORM_SAT(vsubshs, MO_16, sub, sssub, 0, 29);
GEN_VXFORM_SAT(vsubsws, MO_32, sub, sssub, 0, 30);
GEN_VXFORM(vadduqm, 0, 4);
GEN_VXFORM(vaddcuq, 0, 5);
GEN_VXFORM3(vaddeuqm, 30, 0);
GEN_VXFORM3(vaddecuq, 30, 0);
GEN_VXFORM_DUAL(vaddeuqm, PPC_NONE, PPC2_ALTIVEC_207, \
            vaddecuq, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM(vsubuqm, 0, 20);
GEN_VXFORM(vsubcuq, 0, 21);
GEN_VXFORM3(vsubeuqm, 31, 0);
GEN_VXFORM3(vsubecuq, 31, 0);
GEN_VXFORM_DUAL(vsubeuqm, PPC_NONE, PPC2_ALTIVEC_207, \
            vsubecuq, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM(vrlb, 2, 0);
GEN_VXFORM(vrlh, 2, 1);
GEN_VXFORM(vrlw, 2, 2);
GEN_VXFORM(vrlwmi, 2, 2);
GEN_VXFORM_DUAL(vrlw, PPC_ALTIVEC, PPC_NONE, \
                vrlwmi, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vrld, 2, 3);
GEN_VXFORM(vrldmi, 2, 3);
GEN_VXFORM_DUAL(vrld, PPC_NONE, PPC2_ALTIVEC_207, \
                vrldmi, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_TRANS(vsl, 2, 7);
GEN_VXFORM(vrldnm, 2, 7);
GEN_VXFORM_DUAL(vsl, PPC_ALTIVEC, PPC_NONE, \
                vrldnm, PPC_NONE, PPC2_ISA300)
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
GEN_VXFORM_TRANS(lvsl, 6, 31)
GEN_VXFORM_TRANS(lvsr, 6, 32)
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
        gen_helper_##opname(cpu_env, rd, ra, rb);                       \
        tcg_temp_free_ptr(ra);                                          \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
    }

#define GEN_VXRFORM(name, opc2, opc3)                                \
    GEN_VXRFORM1(name, name, #name, opc2, opc3)                      \
    GEN_VXRFORM1(name##_dot, name##_, #name ".", opc2, (opc3 | (0x1 << 4)))

/*
 * Support for Altivec instructions that use bit 31 (Rc) as an opcode
 * bit but also use bit 21 as an actual Rc bit.  In general, thse pairs
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

GEN_VXRFORM(vcmpequb, 3, 0)
GEN_VXRFORM(vcmpequh, 3, 1)
GEN_VXRFORM(vcmpequw, 3, 2)
GEN_VXRFORM(vcmpequd, 3, 3)
GEN_VXRFORM(vcmpnezb, 3, 4)
GEN_VXRFORM(vcmpnezh, 3, 5)
GEN_VXRFORM(vcmpnezw, 3, 6)
GEN_VXRFORM(vcmpgtsb, 3, 12)
GEN_VXRFORM(vcmpgtsh, 3, 13)
GEN_VXRFORM(vcmpgtsw, 3, 14)
GEN_VXRFORM(vcmpgtsd, 3, 15)
GEN_VXRFORM(vcmpgtub, 3, 8)
GEN_VXRFORM(vcmpgtuh, 3, 9)
GEN_VXRFORM(vcmpgtuw, 3, 10)
GEN_VXRFORM(vcmpgtud, 3, 11)
GEN_VXRFORM(vcmpeqfp, 3, 3)
GEN_VXRFORM(vcmpgefp, 3, 7)
GEN_VXRFORM(vcmpgtfp, 3, 11)
GEN_VXRFORM(vcmpbfp, 3, 15)
GEN_VXRFORM(vcmpneb, 3, 0)
GEN_VXRFORM(vcmpneh, 3, 1)
GEN_VXRFORM(vcmpnew, 3, 2)

GEN_VXRFORM_DUAL(vcmpequb, PPC_ALTIVEC, PPC_NONE, \
                 vcmpneb, PPC_NONE, PPC2_ISA300)
GEN_VXRFORM_DUAL(vcmpequh, PPC_ALTIVEC, PPC_NONE, \
                 vcmpneh, PPC_NONE, PPC2_ISA300)
GEN_VXRFORM_DUAL(vcmpequw, PPC_ALTIVEC, PPC_NONE, \
                 vcmpnew, PPC_NONE, PPC2_ISA300)
GEN_VXRFORM_DUAL(vcmpeqfp, PPC_ALTIVEC, PPC_NONE, \
                 vcmpequd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXRFORM_DUAL(vcmpbfp, PPC_ALTIVEC, PPC_NONE, \
                 vcmpgtsd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXRFORM_DUAL(vcmpgtfp, PPC_ALTIVEC, PPC_NONE, \
                 vcmpgtud, PPC_NONE, PPC2_ALTIVEC_207)

#define GEN_VXFORM_DUPI(name, tcg_op, opc2, opc3)                       \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        int simm;                                                       \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        simm = SIMM5(ctx->opcode);                                      \
        tcg_op(avr_full_offset(rD(ctx->opcode)), 16, 16, simm);         \
    }

GEN_VXFORM_DUPI(vspltisb, tcg_gen_gvec_dup8i, 6, 12);
GEN_VXFORM_DUPI(vspltish, tcg_gen_gvec_dup16i, 6, 13);
GEN_VXFORM_DUPI(vspltisw, tcg_gen_gvec_dup32i, 6, 14);

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
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
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
        gen_helper_##name(cpu_env, rd, rb);                             \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
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
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
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
        tcg_temp_free_ptr(rb);                                          \
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
GEN_VXFORM_NOA(vprtybw, 1, 24);
GEN_VXFORM_NOA(vprtybd, 1, 24);
GEN_VXFORM_NOA(vprtybq, 1, 24);

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
#ifndef HOST_WORDS_BIGENDIAN
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
        uimm = tcg_const_i32(UIMM5(ctx->opcode));                       \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(cpu_env, rd, rb, uimm);                       \
        tcg_temp_free_i32(uimm);                                        \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
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
        tcg_temp_free_i32(t0);                                          \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
    }

GEN_VXFORM_VSPLT(vspltb, MO_8, 6, 8);
GEN_VXFORM_VSPLT(vsplth, MO_16, 6, 9);
GEN_VXFORM_VSPLT(vspltw, MO_32, 6, 10);
GEN_VXFORM_UIMM_SPLAT(vextractub, 6, 8, 15);
GEN_VXFORM_UIMM_SPLAT(vextractuh, 6, 9, 14);
GEN_VXFORM_UIMM_SPLAT(vextractuw, 6, 10, 12);
GEN_VXFORM_UIMM_SPLAT(vextractd, 6, 11, 8);
GEN_VXFORM_UIMM_SPLAT(vinsertb, 6, 12, 15);
GEN_VXFORM_UIMM_SPLAT(vinserth, 6, 13, 14);
GEN_VXFORM_UIMM_SPLAT(vinsertw, 6, 14, 12);
GEN_VXFORM_UIMM_SPLAT(vinsertd, 6, 15, 8);
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
GEN_VXFORM_DUAL(vspltisb, PPC_ALTIVEC, PPC_NONE,
                vinsertb, PPC_NONE, PPC2_ISA300);
GEN_VXFORM_DUAL(vspltish, PPC_ALTIVEC, PPC_NONE,
                vinserth, PPC_NONE, PPC2_ISA300);
GEN_VXFORM_DUAL(vspltisw, PPC_ALTIVEC, PPC_NONE,
                vinsertw, PPC_NONE, PPC2_ISA300);

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
    sh = tcg_const_i32(VSH(ctx->opcode));
    gen_helper_vsldoi(rd, ra, rb, sh);
    tcg_temp_free_ptr(ra);
    tcg_temp_free_ptr(rb);
    tcg_temp_free_ptr(rd);
    tcg_temp_free_i32(sh);
}

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
            gen_helper_##name1(cpu_env, rd, ra, rb, rc);                \
        } else {                                                        \
            gen_helper_##name0(cpu_env, rd, ra, rb, rc);                \
        }                                                               \
        tcg_temp_free_ptr(ra);                                          \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rc);                                          \
        tcg_temp_free_ptr(rd);                                          \
    }

GEN_VAFORM_PAIRED(vmhaddshs, vmhraddshs, 16)

static void gen_vmladduhm(DisasContext *ctx)
{
    TCGv_ptr ra, rb, rc, rd;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    ra = gen_avr_ptr(rA(ctx->opcode));
    rb = gen_avr_ptr(rB(ctx->opcode));
    rc = gen_avr_ptr(rC(ctx->opcode));
    rd = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_vmladduhm(rd, ra, rb, rc);
    tcg_temp_free_ptr(ra);
    tcg_temp_free_ptr(rb);
    tcg_temp_free_ptr(rc);
    tcg_temp_free_ptr(rd);
}

static void gen_vpermr(DisasContext *ctx)
{
    TCGv_ptr ra, rb, rc, rd;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    ra = gen_avr_ptr(rA(ctx->opcode));
    rb = gen_avr_ptr(rB(ctx->opcode));
    rc = gen_avr_ptr(rC(ctx->opcode));
    rd = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_vpermr(cpu_env, rd, ra, rb, rc);
    tcg_temp_free_ptr(ra);
    tcg_temp_free_ptr(rb);
    tcg_temp_free_ptr(rc);
    tcg_temp_free_ptr(rd);
}

GEN_VAFORM_PAIRED(vmsumubm, vmsummbm, 18)
GEN_VAFORM_PAIRED(vmsumuhm, vmsumuhs, 19)
GEN_VAFORM_PAIRED(vmsumshm, vmsumshs, 20)
GEN_VAFORM_PAIRED(vsel, vperm, 21)
GEN_VAFORM_PAIRED(vmaddfp, vnmsubfp, 23)

GEN_VXFORM_NOA(vclzb, 1, 28)
GEN_VXFORM_NOA(vclzh, 1, 29)
GEN_VXFORM_TRANS(vclzw, 1, 30)
GEN_VXFORM_TRANS(vclzd, 1, 31)
GEN_VXFORM_NOA_2(vnegw, 1, 24, 6)
GEN_VXFORM_NOA_2(vnegd, 1, 24, 7)
GEN_VXFORM_NOA_2(vextsb2w, 1, 24, 16)
GEN_VXFORM_NOA_2(vextsh2w, 1, 24, 17)
GEN_VXFORM_NOA_2(vextsb2d, 1, 24, 24)
GEN_VXFORM_NOA_2(vextsh2d, 1, 24, 25)
GEN_VXFORM_NOA_2(vextsw2d, 1, 24, 26)
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
GEN_VXFORM(vpmsumd, 4, 19)

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
    ps = tcg_const_i32((ctx->opcode & 0x200) != 0); \
                                                    \
    gen_helper_##op(cpu_crf[6], rd, ra, rb, ps);    \
                                                    \
    tcg_temp_free_ptr(ra);                          \
    tcg_temp_free_ptr(rb);                          \
    tcg_temp_free_ptr(rd);                          \
    tcg_temp_free_i32(ps);                          \
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
    ps = tcg_const_i32((ctx->opcode & 0x200) != 0); \
                                                    \
    gen_helper_##op(cpu_crf[6], rd, rb, ps);        \
                                                    \
    tcg_temp_free_ptr(rb);                          \
    tcg_temp_free_ptr(rd);                          \
    tcg_temp_free_i32(ps);                          \
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


GEN_VXFORM_DUAL(vsubcuw, PPC_ALTIVEC, PPC_NONE, \
                xpnd04_1, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubsws, PPC_ALTIVEC, PPC_NONE, \
                xpnd04_2, PPC_NONE, PPC2_ISA300)

GEN_VXFORM_DUAL(vsububm, PPC_ALTIVEC, PPC_NONE, \
                bcdadd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsububs, PPC_ALTIVEC, PPC_NONE, \
                bcdadd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubuhm, PPC_ALTIVEC, PPC_NONE, \
                bcdsub, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubuhs, PPC_ALTIVEC, PPC_NONE, \
                bcdsub, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vaddshs, PPC_ALTIVEC, PPC_NONE, \
                bcdcpsgn, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubudm, PPC2_ALTIVEC_207, PPC_NONE, \
                bcds, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubuwm, PPC_ALTIVEC, PPC_NONE, \
                bcdus, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubsbs, PPC_ALTIVEC, PPC_NONE, \
                bcdtrunc, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubuqm, PPC2_ALTIVEC_207, PPC_NONE, \
                bcdtrunc, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_DUAL(vsubcuq, PPC2_ALTIVEC_207, PPC_NONE, \
                bcdutrunc, PPC_NONE, PPC2_ISA300)


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
    tcg_temp_free_ptr(ra);
    tcg_temp_free_ptr(rd);
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
    st_six = tcg_const_i32(rB(ctx->opcode));  \
    gen_helper_##op(rd, ra, st_six);          \
    tcg_temp_free_ptr(ra);                    \
    tcg_temp_free_ptr(rd);                    \
    tcg_temp_free_i32(st_six);                \
}

VSHASIGMA(vshasigmaw)
VSHASIGMA(vshasigmad)

GEN_VXFORM3(vpermxor, 22, 0xFF)
GEN_VXFORM_DUAL(vsldoi, PPC_ALTIVEC, PPC_NONE,
                vpermxor, PPC_NONE, PPC2_ALTIVEC_207)

#undef GEN_VR_LDX
#undef GEN_VR_STX
#undef GEN_VR_LVE
#undef GEN_VR_STVE

#undef GEN_VX_LOGICAL
#undef GEN_VX_LOGICAL_207
#undef GEN_VXFORM
#undef GEN_VXFORM_207
#undef GEN_VXFORM_DUAL
#undef GEN_VXRFORM_DUAL
#undef GEN_VXRFORM1
#undef GEN_VXRFORM
#undef GEN_VXFORM_DUPI
#undef GEN_VXFORM_NOA
#undef GEN_VXFORM_UIMM
#undef GEN_VAFORM_PAIRED

#undef GEN_BCD2
