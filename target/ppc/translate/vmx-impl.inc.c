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
    tcg_gen_addi_ptr(r, cpu_env, offsetof(CPUPPCState, avr[reg]));
    return r;
}

#define GEN_VR_LDX(name, opc2, opc3)                                          \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->altivec_enabled)) {                                    \
        gen_exception(ctx, POWERPC_EXCP_VPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_andi_tl(EA, EA, ~0xf);                                            \
    /* We only need to swap high and low halves. gen_qemu_ld64_i64 does       \
       necessary 64-bit byteswap already. */                                  \
    if (ctx->le_mode) {                                                       \
        gen_qemu_ld64_i64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64_i64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                \
    } else {                                                                  \
        gen_qemu_ld64_i64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64_i64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                \
    }                                                                         \
    tcg_temp_free(EA);                                                        \
}

#define GEN_VR_STX(name, opc2, opc3)                                          \
static void gen_st##name(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->altivec_enabled)) {                                    \
        gen_exception(ctx, POWERPC_EXCP_VPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_andi_tl(EA, EA, ~0xf);                                            \
    /* We only need to swap high and low halves. gen_qemu_st64_i64 does       \
       necessary 64-bit byteswap already. */                                  \
    if (ctx->le_mode) {                                                       \
        gen_qemu_st64_i64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_st64_i64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                \
    } else {                                                                  \
        gen_qemu_st64_i64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_st64_i64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                \
    }                                                                         \
    tcg_temp_free(EA);                                                        \
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

static void gen_lvsl(DisasContext *ctx)
{
    TCGv_ptr rd;
    TCGv EA;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    rd = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_lvsl(rd, EA);
    tcg_temp_free(EA);
    tcg_temp_free_ptr(rd);
}

static void gen_lvsr(DisasContext *ctx)
{
    TCGv_ptr rd;
    TCGv EA;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    rd = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_lvsr(rd, EA);
    tcg_temp_free(EA);
    tcg_temp_free_ptr(rd);
}

static void gen_mfvscr(DisasContext *ctx)
{
    TCGv_i32 t;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    tcg_gen_movi_i64(cpu_avrh[rD(ctx->opcode)], 0);
    t = tcg_temp_new_i32();
    tcg_gen_ld_i32(t, cpu_env, offsetof(CPUPPCState, vscr));
    tcg_gen_extu_i32_i64(cpu_avrl[rD(ctx->opcode)], t);
    tcg_temp_free_i32(t);
}

static void gen_mtvscr(DisasContext *ctx)
{
    TCGv_ptr p;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    p = gen_avr_ptr(rB(ctx->opcode));
    gen_helper_mtvscr(cpu_env, p);
    tcg_temp_free_ptr(p);
}

#define GEN_VX_VMUL10(name, add_cin, ret_carry)                         \
static void glue(gen_, name)(DisasContext *ctx)                         \
{                                                                       \
    TCGv_i64 t0 = tcg_temp_new_i64();                                   \
    TCGv_i64 t1 = tcg_temp_new_i64();                                   \
    TCGv_i64 t2 = tcg_temp_new_i64();                                   \
    TCGv_i64 ten, z;                                                    \
                                                                        \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
                                                                        \
    ten = tcg_const_i64(10);                                            \
    z = tcg_const_i64(0);                                               \
                                                                        \
    if (add_cin) {                                                      \
        tcg_gen_mulu2_i64(t0, t1, cpu_avrl[rA(ctx->opcode)], ten);      \
        tcg_gen_andi_i64(t2, cpu_avrl[rB(ctx->opcode)], 0xF);           \
        tcg_gen_add2_i64(cpu_avrl[rD(ctx->opcode)], t2, t0, t1, t2, z); \
    } else {                                                            \
        tcg_gen_mulu2_i64(cpu_avrl[rD(ctx->opcode)], t2,                \
                          cpu_avrl[rA(ctx->opcode)], ten);              \
    }                                                                   \
                                                                        \
    if (ret_carry) {                                                    \
        tcg_gen_mulu2_i64(t0, t1, cpu_avrh[rA(ctx->opcode)], ten);      \
        tcg_gen_add2_i64(t0, cpu_avrl[rD(ctx->opcode)], t0, t1, t2, z); \
        tcg_gen_movi_i64(cpu_avrh[rD(ctx->opcode)], 0);                 \
    } else {                                                            \
        tcg_gen_mul_i64(t0, cpu_avrh[rA(ctx->opcode)], ten);            \
        tcg_gen_add_i64(cpu_avrh[rD(ctx->opcode)], t0, t2);             \
    }                                                                   \
                                                                        \
    tcg_temp_free_i64(t0);                                              \
    tcg_temp_free_i64(t1);                                              \
    tcg_temp_free_i64(t2);                                              \
    tcg_temp_free_i64(ten);                                             \
    tcg_temp_free_i64(z);                                               \
}                                                                       \

GEN_VX_VMUL10(vmul10uq, 0, 0);
GEN_VX_VMUL10(vmul10euq, 1, 0);
GEN_VX_VMUL10(vmul10cuq, 0, 1);
GEN_VX_VMUL10(vmul10ecuq, 1, 1);

/* Logical operations */
#define GEN_VX_LOGICAL(name, tcg_op, opc2, opc3)                        \
static void glue(gen_, name)(DisasContext *ctx)                                 \
{                                                                       \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    tcg_op(cpu_avrh[rD(ctx->opcode)], cpu_avrh[rA(ctx->opcode)], cpu_avrh[rB(ctx->opcode)]); \
    tcg_op(cpu_avrl[rD(ctx->opcode)], cpu_avrl[rA(ctx->opcode)], cpu_avrl[rB(ctx->opcode)]); \
}

GEN_VX_LOGICAL(vand, tcg_gen_and_i64, 2, 16);
GEN_VX_LOGICAL(vandc, tcg_gen_andc_i64, 2, 17);
GEN_VX_LOGICAL(vor, tcg_gen_or_i64, 2, 18);
GEN_VX_LOGICAL(vxor, tcg_gen_xor_i64, 2, 19);
GEN_VX_LOGICAL(vnor, tcg_gen_nor_i64, 2, 20);
GEN_VX_LOGICAL(veqv, tcg_gen_eqv_i64, 2, 26);
GEN_VX_LOGICAL(vnand, tcg_gen_nand_i64, 2, 22);
GEN_VX_LOGICAL(vorc, tcg_gen_orc_i64, 2, 21);

#define GEN_VXFORM(name, opc2, opc3)                                    \
static void glue(gen_, name)(DisasContext *ctx)                                 \
{                                                                       \
    TCGv_ptr ra, rb, rd;                                                \
    if (unlikely(!ctx->altivec_enabled)) {                              \
        gen_exception(ctx, POWERPC_EXCP_VPU);                           \
        return;                                                         \
    }                                                                   \
    ra = gen_avr_ptr(rA(ctx->opcode));                                  \
    rb = gen_avr_ptr(rB(ctx->opcode));                                  \
    rd = gen_avr_ptr(rD(ctx->opcode));                                  \
    gen_helper_##name (rd, ra, rb);                                     \
    tcg_temp_free_ptr(ra);                                              \
    tcg_temp_free_ptr(rb);                                              \
    tcg_temp_free_ptr(rd);                                              \
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

GEN_VXFORM(vaddubm, 0, 0);
GEN_VXFORM_DUAL_EXT(vaddubm, PPC_ALTIVEC, PPC_NONE, 0,       \
                    vmul10cuq, PPC_NONE, PPC2_ISA300, 0x0000F800)
GEN_VXFORM(vadduhm, 0, 1);
GEN_VXFORM_DUAL(vadduhm, PPC_ALTIVEC, PPC_NONE,  \
                vmul10ecuq, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vadduwm, 0, 2);
GEN_VXFORM(vaddudm, 0, 3);
GEN_VXFORM(vsububm, 0, 16);
GEN_VXFORM(vsubuhm, 0, 17);
GEN_VXFORM(vsubuwm, 0, 18);
GEN_VXFORM(vsubudm, 0, 19);
GEN_VXFORM(vmaxub, 1, 0);
GEN_VXFORM(vmaxuh, 1, 1);
GEN_VXFORM(vmaxuw, 1, 2);
GEN_VXFORM(vmaxud, 1, 3);
GEN_VXFORM(vmaxsb, 1, 4);
GEN_VXFORM(vmaxsh, 1, 5);
GEN_VXFORM(vmaxsw, 1, 6);
GEN_VXFORM(vmaxsd, 1, 7);
GEN_VXFORM(vminub, 1, 8);
GEN_VXFORM(vminuh, 1, 9);
GEN_VXFORM(vminuw, 1, 10);
GEN_VXFORM(vminud, 1, 11);
GEN_VXFORM(vminsb, 1, 12);
GEN_VXFORM(vminsh, 1, 13);
GEN_VXFORM(vminsw, 1, 14);
GEN_VXFORM(vminsd, 1, 15);
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

static void gen_vmrgew(DisasContext *ctx)
{
    TCGv_i64 tmp;
    int VT, VA, VB;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    VT = rD(ctx->opcode);
    VA = rA(ctx->opcode);
    VB = rB(ctx->opcode);
    tmp = tcg_temp_new_i64();
    tcg_gen_shri_i64(tmp, cpu_avrh[VB], 32);
    tcg_gen_deposit_i64(cpu_avrh[VT], cpu_avrh[VA], tmp, 0, 32);
    tcg_gen_shri_i64(tmp, cpu_avrl[VB], 32);
    tcg_gen_deposit_i64(cpu_avrl[VT], cpu_avrl[VA], tmp, 0, 32);
    tcg_temp_free_i64(tmp);
}

static void gen_vmrgow(DisasContext *ctx)
{
    int VT, VA, VB;
    if (unlikely(!ctx->altivec_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VPU);
        return;
    }
    VT = rD(ctx->opcode);
    VA = rA(ctx->opcode);
    VB = rB(ctx->opcode);

    tcg_gen_deposit_i64(cpu_avrh[VT], cpu_avrh[VB], cpu_avrh[VA], 32, 32);
    tcg_gen_deposit_i64(cpu_avrl[VT], cpu_avrl[VB], cpu_avrl[VA], 32, 32);
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
GEN_VXFORM(vslb, 2, 4);
GEN_VXFORM(vslh, 2, 5);
GEN_VXFORM(vslw, 2, 6);
GEN_VXFORM(vrlwnm, 2, 6);
GEN_VXFORM_DUAL(vslw, PPC_ALTIVEC, PPC_NONE, \
                vrlwnm, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vsld, 2, 23);
GEN_VXFORM(vsrb, 2, 8);
GEN_VXFORM(vsrh, 2, 9);
GEN_VXFORM(vsrw, 2, 10);
GEN_VXFORM(vsrd, 2, 27);
GEN_VXFORM(vsrab, 2, 12);
GEN_VXFORM(vsrah, 2, 13);
GEN_VXFORM(vsraw, 2, 14);
GEN_VXFORM(vsrad, 2, 15);
GEN_VXFORM(vsrv, 2, 28);
GEN_VXFORM(vslv, 2, 29);
GEN_VXFORM(vslo, 6, 16);
GEN_VXFORM(vsro, 6, 17);
GEN_VXFORM(vaddcuw, 0, 6);
GEN_VXFORM(vsubcuw, 0, 22);
GEN_VXFORM_ENV(vaddubs, 0, 8);
GEN_VXFORM_DUAL_EXT(vaddubs, PPC_ALTIVEC, PPC_NONE, 0,       \
                    vmul10uq, PPC_NONE, PPC2_ISA300, 0x0000F800)
GEN_VXFORM_ENV(vadduhs, 0, 9);
GEN_VXFORM_DUAL(vadduhs, PPC_ALTIVEC, PPC_NONE, \
                vmul10euq, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_ENV(vadduws, 0, 10);
GEN_VXFORM_ENV(vaddsbs, 0, 12);
GEN_VXFORM_ENV(vaddshs, 0, 13);
GEN_VXFORM_ENV(vaddsws, 0, 14);
GEN_VXFORM_ENV(vsububs, 0, 24);
GEN_VXFORM_ENV(vsubuhs, 0, 25);
GEN_VXFORM_ENV(vsubuws, 0, 26);
GEN_VXFORM_ENV(vsubsbs, 0, 28);
GEN_VXFORM_ENV(vsubshs, 0, 29);
GEN_VXFORM_ENV(vsubsws, 0, 30);
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
GEN_VXFORM(vsl, 2, 7);
GEN_VXFORM(vrldnm, 2, 7);
GEN_VXFORM_DUAL(vsl, PPC_ALTIVEC, PPC_NONE, \
                vrldnm, PPC_NONE, PPC2_ISA300)
GEN_VXFORM(vsr, 2, 11);
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
GEN_VXFORM_DUAL(vmrgow, PPC_NONE, PPC2_ALTIVEC_207,
                vextuwlx, PPC_NONE, PPC2_ISA300)
GEN_VXFORM_HETRO(vextubrx, 6, 28)
GEN_VXFORM_HETRO(vextuhrx, 6, 29)
GEN_VXFORM_HETRO(vextuwrx, 6, 30)
GEN_VXFORM_DUAL(vmrgew, PPC_NONE, PPC2_ALTIVEC_207, \
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

#define GEN_VXFORM_SIMM(name, opc2, opc3)                               \
static void glue(gen_, name)(DisasContext *ctx)                         \
    {                                                                   \
        TCGv_ptr rd;                                                    \
        TCGv_i32 simm;                                                  \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        simm = tcg_const_i32(SIMM5(ctx->opcode));                       \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name (rd, simm);                                   \
        tcg_temp_free_i32(simm);                                        \
        tcg_temp_free_ptr(rd);                                          \
    }

GEN_VXFORM_SIMM(vspltisb, 6, 12);
GEN_VXFORM_SIMM(vspltish, 6, 13);
GEN_VXFORM_SIMM(vspltisw, 6, 14);

#define GEN_VXFORM_NOA(name, opc2, opc3)                                \
static void glue(gen_, name)(DisasContext *ctx)                                 \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name (rd, rb);                                     \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                         \
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

#define GEN_VXFORM_SIMM(name, opc2, opc3)                               \
static void glue(gen_, name)(DisasContext *ctx)                                 \
    {                                                                   \
        TCGv_ptr rd;                                                    \
        TCGv_i32 simm;                                                  \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        simm = tcg_const_i32(SIMM5(ctx->opcode));                       \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name (rd, simm);                                   \
        tcg_temp_free_i32(simm);                                        \
        tcg_temp_free_ptr(rd);                                          \
    }

#define GEN_VXFORM_UIMM(name, opc2, opc3)                               \
static void glue(gen_, name)(DisasContext *ctx)                                 \
    {                                                                   \
        TCGv_ptr rb, rd;                                                \
        TCGv_i32 uimm;                                                  \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        uimm = tcg_const_i32(UIMM5(ctx->opcode));                       \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name (rd, rb, uimm);                               \
        tcg_temp_free_i32(uimm);                                        \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
    }

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
        TCGv_i32 t0 = tcg_temp_new_i32();                               \
        if (unlikely(!ctx->altivec_enabled)) {                          \
            gen_exception(ctx, POWERPC_EXCP_VPU);                       \
            return;                                                     \
        }                                                               \
        if (uimm > splat_max) {                                         \
            uimm = 0;                                                   \
        }                                                               \
        tcg_gen_movi_i32(t0, uimm);                                     \
        rb = gen_avr_ptr(rB(ctx->opcode));                              \
        rd = gen_avr_ptr(rD(ctx->opcode));                              \
        gen_helper_##name(rd, rb, t0);                                  \
        tcg_temp_free_i32(t0);                                          \
        tcg_temp_free_ptr(rb);                                          \
        tcg_temp_free_ptr(rd);                                          \
    }

GEN_VXFORM_UIMM(vspltb, 6, 8);
GEN_VXFORM_UIMM(vsplth, 6, 9);
GEN_VXFORM_UIMM(vspltw, 6, 10);
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
    gen_helper_vsldoi (rd, ra, rb, sh);
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
GEN_VXFORM_NOA(vclzw, 1, 30)
GEN_VXFORM_NOA(vclzd, 1, 31)
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
GEN_VXFORM_NOA(vgbbd, 6, 20);
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
#undef GEN_VXFORM_SIMM
#undef GEN_VXFORM_NOA
#undef GEN_VXFORM_UIMM
#undef GEN_VAFORM_PAIRED

#undef GEN_BCD2
