/*
 * New-style decoder for i386 instructions
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The decoder is mostly based on tables copied from the Intel SDM.  As
 * a result, most operand load and writeback is done entirely in common
 * table-driven code using the same operand type (X86_TYPE_*) and
 * size (X86_SIZE_*) codes used in the manual.
 *
 * The main difference is that the V, U and W types are extended to
 * cover MMX as well; if an instruction is like
 *
 *      por   Pq, Qq
 *  66  por   Vx, Hx, Wx
 *
 * only the second row is included and the instruction is marked as a
 * valid MMX instruction.  The MMX flag directs the decoder to rewrite
 * the V/U/H/W types to P/N/P/Q if there is no prefix, as well as changing
 * "x" to "q" if there is no prefix.
 *
 * In addition, the ss/ps/sd/pd types are sometimes mushed together as "x"
 * if the difference is expressed via prefixes.  Individual instructions
 * are separated by prefix in the generator functions.
 *
 * There are a couple cases in which instructions (e.g. MOVD) write the
 * whole XMM or MM register but are established incorrectly in the manual
 * as "d" or "q".  These have to be fixed for the decoder to work correctly.
 */

#define X86_OP_NONE { 0 },

#define X86_OP_GROUP3(op, op0_, s0_, op1_, s1_, op2_, s2_, ...) { \
    .decode = glue(decode_, op),                                  \
    .op0 = glue(X86_TYPE_, op0_),                                 \
    .s0 = glue(X86_SIZE_, s0_),                                   \
    .op1 = glue(X86_TYPE_, op1_),                                 \
    .s1 = glue(X86_SIZE_, s1_),                                   \
    .op2 = glue(X86_TYPE_, op2_),                                 \
    .s2 = glue(X86_SIZE_, s2_),                                   \
    .is_decode = true,                                            \
    ## __VA_ARGS__                                                \
}

#define X86_OP_GROUP2(op, op0, s0, op1, s1, ...)                  \
    X86_OP_GROUP3(op, op0, s0, 2op, s0, op1, s1, ## __VA_ARGS__)
#define X86_OP_GROUP0(op, ...)                                    \
    X86_OP_GROUP3(op, None, None, None, None, None, None, ## __VA_ARGS__)

#define X86_OP_ENTRY3(op, op0_, s0_, op1_, s1_, op2_, s2_, ...) { \
    .gen = glue(gen_, op),                                        \
    .op0 = glue(X86_TYPE_, op0_),                                 \
    .s0 = glue(X86_SIZE_, s0_),                                   \
    .op1 = glue(X86_TYPE_, op1_),                                 \
    .s1 = glue(X86_SIZE_, s1_),                                   \
    .op2 = glue(X86_TYPE_, op2_),                                 \
    .s2 = glue(X86_SIZE_, s2_),                                   \
    ## __VA_ARGS__                                                \
}

#define X86_OP_ENTRY4(op, op0_, s0_, op1_, s1_, op2_, s2_, ...)   \
    X86_OP_ENTRY3(op, op0_, s0_, op1_, s1_, op2_, s2_,            \
        .op3 = X86_TYPE_I, .s3 = X86_SIZE_b,                      \
        ## __VA_ARGS__)

#define X86_OP_ENTRY2(op, op0, s0, op1, s1, ...)                  \
    X86_OP_ENTRY3(op, op0, s0, 2op, s0, op1, s1, ## __VA_ARGS__)
#define X86_OP_ENTRYw(op, op0, s0, ...)                           \
    X86_OP_ENTRY3(op, op0, s0, None, None, None, None, ## __VA_ARGS__)
#define X86_OP_ENTRYr(op, op0, s0, ...)                           \
    X86_OP_ENTRY3(op, None, None, None, None, op0, s0, ## __VA_ARGS__)
#define X86_OP_ENTRY0(op, ...)                                    \
    X86_OP_ENTRY3(op, None, None, None, None, None, None, ## __VA_ARGS__)

#define cpuid(feat) .cpuid = X86_FEAT_##feat,
#define i64 .special = X86_SPECIAL_i64,
#define o64 .special = X86_SPECIAL_o64,
#define xchg .special = X86_SPECIAL_Locked,
#define mmx .special = X86_SPECIAL_MMX,
#define zext0 .special = X86_SPECIAL_ZExtOp0,
#define zext2 .special = X86_SPECIAL_ZExtOp2,
#define avx_movx .special = X86_SPECIAL_AVXExtMov,

#define vex1 .vex_class = 1,
#define vex1_rep3 .vex_class = 1, .vex_special = X86_VEX_REPScalar,
#define vex2 .vex_class = 2,
#define vex2_rep3 .vex_class = 2, .vex_special = X86_VEX_REPScalar,
#define vex3 .vex_class = 3,
#define vex4 .vex_class = 4,
#define vex4_unal .vex_class = 4, .vex_special = X86_VEX_SSEUnaligned,
#define vex4_rep5 .vex_class = 4, .vex_special = X86_VEX_REPScalar,
#define vex5 .vex_class = 5,
#define vex6 .vex_class = 6,
#define vex7 .vex_class = 7,
#define vex8 .vex_class = 8,
#define vex11 .vex_class = 11,
#define vex12 .vex_class = 12,
#define vex13 .vex_class = 13,

#define avx2_256 .vex_special = X86_VEX_AVX2_256,

#define P_00          1
#define P_66          (1 << PREFIX_DATA)
#define P_F3          (1 << PREFIX_REPZ)
#define P_F2          (1 << PREFIX_REPNZ)

#define p_00          .valid_prefix = P_00,
#define p_66          .valid_prefix = P_66,
#define p_f3          .valid_prefix = P_F3,
#define p_f2          .valid_prefix = P_F2,
#define p_00_66       .valid_prefix = P_00 | P_66,
#define p_00_f3       .valid_prefix = P_00 | P_F3,
#define p_66_f2       .valid_prefix = P_66 | P_F2,
#define p_00_66_f3    .valid_prefix = P_00 | P_66 | P_F3,
#define p_66_f3_f2    .valid_prefix = P_66 | P_F3 | P_F2,
#define p_00_66_f3_f2 .valid_prefix = P_00 | P_66 | P_F3 | P_F2,

static uint8_t get_modrm(DisasContext *s, CPUX86State *env)
{
    if (!s->has_modrm) {
        s->modrm = x86_ldub_code(env, s);
        s->has_modrm = true;
    }
    return s->modrm;
}

static inline const X86OpEntry *decode_by_prefix(DisasContext *s, const X86OpEntry entries[4])
{
    if (s->prefix & PREFIX_REPNZ) {
        return &entries[3];
    } else if (s->prefix & PREFIX_REPZ) {
        return &entries[2];
    } else if (s->prefix & PREFIX_DATA) {
        return &entries[1];
    } else {
        return &entries[0];
    }
}

static void decode_group15(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    /* only includes ldmxcsr and stmxcsr, because they have AVX variants.  */
    static const X86OpEntry group15_reg[8] = {
    };

    static const X86OpEntry group15_mem[8] = {
        [2] = X86_OP_ENTRYr(LDMXCSR,    E,d, vex5),
        [3] = X86_OP_ENTRYw(STMXCSR,    E,d, vex5),
    };

    uint8_t modrm = get_modrm(s, env);
    if ((modrm >> 6) == 3) {
        *entry = group15_reg[(modrm >> 3) & 7];
    } else {
        *entry = group15_mem[(modrm >> 3) & 7];
    }
}

static void decode_group17(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86GenFunc group17_gen[8] = {
        NULL, gen_BLSR, gen_BLSMSK, gen_BLSI,
    };
    int op = (get_modrm(s, env) >> 3) & 7;
    entry->gen = group17_gen[op];
}

static void decode_group12(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group12[8] = {
        {},
        {},
        X86_OP_ENTRY3(PSRLW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSRAW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSLLW_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group12[op];
}

static void decode_group13(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group13[8] = {
        {},
        {},
        X86_OP_ENTRY3(PSRLD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSRAD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
        X86_OP_ENTRY3(PSLLD_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        {},
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group13[op];
}

static void decode_group14(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_group14[8] = {
        /* grp14 */
        {},
        {},
        X86_OP_ENTRY3(PSRLQ_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        X86_OP_ENTRY3(PSRLDQ_i, H,x, U,x, I,b, vex7 avx2_256 p_66),
        {},
        {},
        X86_OP_ENTRY3(PSLLQ_i,  H,x, U,x, I,b, vex7 mmx avx2_256 p_00_66),
        X86_OP_ENTRY3(PSLLDQ_i, H,x, U,x, I,b, vex7 avx2_256 p_66),
    };

    int op = (get_modrm(s, env) >> 3) & 7;
    *entry = opcodes_group14[op];
}

static void decode_0F6F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F6F[4] = {
        X86_OP_ENTRY3(MOVDQ,       P,q, None,None, Q,q, vex1 mmx),  /* movq */
        X86_OP_ENTRY3(MOVDQ,       V,x, None,None, W,x, vex1),      /* movdqa */
        X86_OP_ENTRY3(MOVDQ,       V,x, None,None, W,x, vex4_unal), /* movdqu */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F6F);
}

static void decode_0F70(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry pshufw[4] = {
        X86_OP_ENTRY3(PSHUFW,  P,q, Q,q, I,b, vex4 mmx),
        X86_OP_ENTRY3(PSHUFD,  V,x, W,x, I,b, vex4 avx2_256),
        X86_OP_ENTRY3(PSHUFHW, V,x, W,x, I,b, vex4 avx2_256),
        X86_OP_ENTRY3(PSHUFLW, V,x, W,x, I,b, vex4 avx2_256),
    };

    *entry = *decode_by_prefix(s, pshufw);
}

static void decode_0F77(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (!(s->prefix & PREFIX_VEX)) {
        entry->gen = gen_EMMS;
    } else if (!s->vex_l) {
        entry->gen = gen_VZEROUPPER;
        entry->vex_class = 8;
    } else {
        entry->gen = gen_VZEROALL;
        entry->vex_class = 8;
    }
}

static void decode_0F78(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F78[4] = {
        {},
        X86_OP_ENTRY3(EXTRQ_i,       V,x, None,None, I,w,  cpuid(SSE4A)),
        {},
        X86_OP_ENTRY3(INSERTQ_i,     V,x, U,x, I,w,        cpuid(SSE4A)),
    };
    *entry = *decode_by_prefix(s, opcodes_0F78);
}

static void decode_0F79(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (s->prefix & PREFIX_REPNZ) {
        entry->gen = gen_INSERTQ_r;
    } else if (s->prefix & PREFIX_DATA) {
        entry->gen = gen_EXTRQ_r;
    } else {
        entry->gen = NULL;
    };
}

static void decode_0F7E(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F7E[4] = {
        X86_OP_ENTRY3(MOVD_from,  E,y, None,None, P,y, vex5 mmx),
        X86_OP_ENTRY3(MOVD_from,  E,y, None,None, V,y, vex5),
        X86_OP_ENTRY3(MOVQ,       V,x, None,None, W,q, vex5),  /* wrong dest Vy on SDM! */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F7E);
}

static void decode_0F7F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F7F[4] = {
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex1 mmx), /* movq */
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex1), /* movdqa */
        X86_OP_ENTRY3(MOVDQ,       W,x, None,None, V,x, vex4_unal), /* movdqu */
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F7F);
}

static void decode_0FD6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry movq[4] = {
        {},
        X86_OP_ENTRY3(MOVQ,    W,x,  None, None, V,q, vex5),
        X86_OP_ENTRY3(MOVq_dq, V,dq, None, None, N,q),
        X86_OP_ENTRY3(MOVq_dq, P,q,  None, None, U,q),
    };

    *entry = *decode_by_prefix(s, movq);
}

static const X86OpEntry opcodes_0F38_00toEF[240] = {
    [0x00] = X86_OP_ENTRY3(PSHUFB,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x01] = X86_OP_ENTRY3(PHADDW,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x02] = X86_OP_ENTRY3(PHADDD,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x03] = X86_OP_ENTRY3(PHADDSW,   V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x04] = X86_OP_ENTRY3(PMADDUBSW, V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x05] = X86_OP_ENTRY3(PHSUBW,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x06] = X86_OP_ENTRY3(PHSUBD,    V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x07] = X86_OP_ENTRY3(PHSUBSW,   V,x,  H,x,   W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

    [0x10] = X86_OP_ENTRY2(PBLENDVB,  V,x,         W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x13] = X86_OP_ENTRY2(VCVTPH2PS, V,x,         W,ph, vex11 cpuid(F16C) p_66),
    [0x14] = X86_OP_ENTRY2(BLENDVPS,  V,x,         W,x,  vex4 cpuid(SSE41) p_66),
    [0x15] = X86_OP_ENTRY2(BLENDVPD,  V,x,         W,x,  vex4 cpuid(SSE41) p_66),
    /* Listed incorrectly as type 4 */
    [0x16] = X86_OP_ENTRY3(VPERMD,    V,qq, H,qq,      W,qq,  vex6 cpuid(AVX2) p_66),
    [0x17] = X86_OP_ENTRY3(VPTEST,    None,None, V,x,  W,x,   vex4 cpuid(SSE41) p_66),

    /*
     * Source operand listed as Mq/Ux and similar in the manual; incorrectly listed
     * as 128-bit only in 2-17.
     */
    [0x20] = X86_OP_ENTRY3(VPMOVSXBW, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x21] = X86_OP_ENTRY3(VPMOVSXBD, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x22] = X86_OP_ENTRY3(VPMOVSXBQ, V,x,  None,None, W,w,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x23] = X86_OP_ENTRY3(VPMOVSXWD, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x24] = X86_OP_ENTRY3(VPMOVSXWQ, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x25] = X86_OP_ENTRY3(VPMOVSXDQ, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),

    /* Same as PMOVSX.  */
    [0x30] = X86_OP_ENTRY3(VPMOVZXBW, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x31] = X86_OP_ENTRY3(VPMOVZXBD, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x32] = X86_OP_ENTRY3(VPMOVZXBQ, V,x,  None,None, W,w,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x33] = X86_OP_ENTRY3(VPMOVZXWD, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x34] = X86_OP_ENTRY3(VPMOVZXWQ, V,x,  None,None, W,d,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x35] = X86_OP_ENTRY3(VPMOVZXDQ, V,x,  None,None, W,q,   vex5 cpuid(SSE41) avx_movx avx2_256 p_66),
    [0x36] = X86_OP_ENTRY3(VPERMD,    V,qq, H,qq,      W,qq,  vex6 cpuid(AVX2) p_66),
    [0x37] = X86_OP_ENTRY3(PCMPGTQ,   V,x,  H,x,       W,x,   vex4 cpuid(SSE42) avx2_256 p_66),

    [0x40] = X86_OP_ENTRY3(PMULLD,      V,x,  H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x41] = X86_OP_ENTRY3(VPHMINPOSUW, V,dq, None,None, W,dq, vex4 cpuid(SSE41) p_66),
    /* Listed incorrectly as type 4 */
    [0x45] = X86_OP_ENTRY3(VPSRLV,      V,x,  H,x,       W,x,  vex6 cpuid(AVX2) p_66),
    [0x46] = X86_OP_ENTRY3(VPSRAV,      V,x,  H,x,       W,x,  vex6 cpuid(AVX2) p_66),
    [0x47] = X86_OP_ENTRY3(VPSLLV,      V,x,  H,x,       W,x,  vex6 cpuid(AVX2) p_66),

    [0x90] = X86_OP_ENTRY3(VPGATHERD, V,x,  H,x,  M,d,  vex12 cpuid(AVX2) p_66), /* vpgatherdd/q */
    [0x91] = X86_OP_ENTRY3(VPGATHERQ, V,x,  H,x,  M,q,  vex12 cpuid(AVX2) p_66), /* vpgatherqd/q */
    [0x92] = X86_OP_ENTRY3(VPGATHERD, V,x,  H,x,  M,d,  vex12 cpuid(AVX2) p_66), /* vgatherdps/d */
    [0x93] = X86_OP_ENTRY3(VPGATHERQ, V,x,  H,x,  M,q,  vex12 cpuid(AVX2) p_66), /* vgatherqps/d */

    /* Should be exception type 2 but they do not have legacy SSE equivalents? */
    [0x96] = X86_OP_ENTRY3(VFMADDSUB132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x97] = X86_OP_ENTRY3(VFMSUBADD132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xa6] = X86_OP_ENTRY3(VFMADDSUB213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xa7] = X86_OP_ENTRY3(VFMSUBADD213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xb6] = X86_OP_ENTRY3(VFMADDSUB231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xb7] = X86_OP_ENTRY3(VFMSUBADD231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0x08] = X86_OP_ENTRY3(PSIGNB,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x09] = X86_OP_ENTRY3(PSIGNW,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x0a] = X86_OP_ENTRY3(PSIGND,    V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x0b] = X86_OP_ENTRY3(PMULHRSW,  V,x,        H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x0c] = X86_OP_ENTRY3(VPERMILPS, V,x,        H,x,  W,x,  vex4 cpuid(AVX) p_00_66),
    [0x0d] = X86_OP_ENTRY3(VPERMILPD, V,x,        H,x,  W,x,  vex4 cpuid(AVX) p_66),
    [0x0e] = X86_OP_ENTRY3(VTESTPS,   None,None,  V,x,  W,x,  vex4 cpuid(AVX) p_66),
    [0x0f] = X86_OP_ENTRY3(VTESTPD,   None,None,  V,x,  W,x,  vex4 cpuid(AVX) p_66),

    [0x18] = X86_OP_ENTRY3(VPBROADCASTD,   V,x,  None,None, W,d,  vex6 cpuid(AVX) p_66), /* vbroadcastss */
    [0x19] = X86_OP_ENTRY3(VPBROADCASTQ,   V,qq, None,None, W,q,  vex6 cpuid(AVX) p_66), /* vbroadcastsd */
    [0x1a] = X86_OP_ENTRY3(VBROADCASTx128, V,qq, None,None, WM,dq,vex6 cpuid(AVX) p_66),
    [0x1c] = X86_OP_ENTRY3(PABSB,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x1d] = X86_OP_ENTRY3(PABSW,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),
    [0x1e] = X86_OP_ENTRY3(PABSD,          V,x,  None,None, W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

    [0x28] = X86_OP_ENTRY3(PMULDQ,        V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x29] = X86_OP_ENTRY3(PCMPEQQ,       V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x2a] = X86_OP_ENTRY3(MOVDQ,         V,x, None,None, WM,x, vex1 cpuid(SSE41) avx2_256 p_66), /* movntdqa */
    [0x2b] = X86_OP_ENTRY3(VPACKUSDW,     V,x, H,x,       W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x2c] = X86_OP_ENTRY3(VMASKMOVPS,    V,x, H,x,       WM,x, vex6 cpuid(AVX) p_66),
    [0x2d] = X86_OP_ENTRY3(VMASKMOVPD,    V,x, H,x,       WM,x, vex6 cpuid(AVX) p_66),
    /* Incorrectly listed as Mx,Hx,Vx in the manual */
    [0x2e] = X86_OP_ENTRY3(VMASKMOVPS_st, M,x, V,x,       H,x,  vex6 cpuid(AVX) p_66),
    [0x2f] = X86_OP_ENTRY3(VMASKMOVPD_st, M,x, V,x,       H,x,  vex6 cpuid(AVX) p_66),

    [0x38] = X86_OP_ENTRY3(PMINSB,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x39] = X86_OP_ENTRY3(PMINSD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3a] = X86_OP_ENTRY3(PMINUW,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3b] = X86_OP_ENTRY3(PMINUD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3c] = X86_OP_ENTRY3(PMAXSB,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3d] = X86_OP_ENTRY3(PMAXSD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3e] = X86_OP_ENTRY3(PMAXUW,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x3f] = X86_OP_ENTRY3(PMAXUD,        V,x,  H,x, W,x,  vex4 cpuid(SSE41) avx2_256 p_66),

    [0x58] = X86_OP_ENTRY3(VPBROADCASTD,   V,x,  None,None, W,d,  vex6 cpuid(AVX2) p_66),
    [0x59] = X86_OP_ENTRY3(VPBROADCASTQ,   V,x,  None,None, W,q,  vex6 cpuid(AVX2) p_66),
    [0x5a] = X86_OP_ENTRY3(VBROADCASTx128, V,qq, None,None, WM,dq,vex6 cpuid(AVX2) p_66),

    [0x78] = X86_OP_ENTRY3(VPBROADCASTB,   V,x,  None,None, W,b,  vex6 cpuid(AVX2) p_66),
    [0x79] = X86_OP_ENTRY3(VPBROADCASTW,   V,x,  None,None, W,w,  vex6 cpuid(AVX2) p_66),

    [0x8c] = X86_OP_ENTRY3(VPMASKMOV,    V,x,  H,x, WM,x, vex6 cpuid(AVX2) p_66),
    [0x8e] = X86_OP_ENTRY3(VPMASKMOV_st, M,x,  V,x, H,x,  vex6 cpuid(AVX2) p_66),

    /* Should be exception type 2 or 3 but they do not have legacy SSE equivalents? */
    [0x98] = X86_OP_ENTRY3(VFMADD132Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x99] = X86_OP_ENTRY3(VFMADD132Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9a] = X86_OP_ENTRY3(VFMSUB132Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9b] = X86_OP_ENTRY3(VFMSUB132Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9c] = X86_OP_ENTRY3(VFNMADD132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9d] = X86_OP_ENTRY3(VFNMADD132Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9e] = X86_OP_ENTRY3(VFNMSUB132Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0x9f] = X86_OP_ENTRY3(VFNMSUB132Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xa8] = X86_OP_ENTRY3(VFMADD213Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xa9] = X86_OP_ENTRY3(VFMADD213Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xaa] = X86_OP_ENTRY3(VFMSUB213Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xab] = X86_OP_ENTRY3(VFMSUB213Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xac] = X86_OP_ENTRY3(VFNMADD213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xad] = X86_OP_ENTRY3(VFNMADD213Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xae] = X86_OP_ENTRY3(VFNMSUB213Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xaf] = X86_OP_ENTRY3(VFNMSUB213Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xb8] = X86_OP_ENTRY3(VFMADD231Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xb9] = X86_OP_ENTRY3(VFMADD231Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xba] = X86_OP_ENTRY3(VFMSUB231Px,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbb] = X86_OP_ENTRY3(VFMSUB231Sx,  V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbc] = X86_OP_ENTRY3(VFNMADD231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbd] = X86_OP_ENTRY3(VFNMADD231Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbe] = X86_OP_ENTRY3(VFNMSUB231Px, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),
    [0xbf] = X86_OP_ENTRY3(VFNMSUB231Sx, V,x,  H,x, W,x,  vex6 cpuid(FMA) p_66),

    [0xdb] = X86_OP_ENTRY3(VAESIMC,     V,dq, None,None, W,dq, vex4 cpuid(AES) p_66),
    [0xdc] = X86_OP_ENTRY3(VAESENC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xdd] = X86_OP_ENTRY3(VAESENCLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xde] = X86_OP_ENTRY3(VAESDEC,     V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
    [0xdf] = X86_OP_ENTRY3(VAESDECLAST, V,x,  H,x,       W,x,  vex4 cpuid(AES) p_66),
};

/* five rows for no prefix, 66, F3, F2, 66+F2  */
static const X86OpEntry opcodes_0F38_F0toFF[16][5] = {
    [0] = {
        X86_OP_ENTRY3(MOVBE, G,y, M,y, None,None, cpuid(MOVBE)),
        X86_OP_ENTRY3(MOVBE, G,w, M,w, None,None, cpuid(MOVBE)),
        {},
        X86_OP_ENTRY2(CRC32, G,d, E,b, cpuid(SSE42)),
        X86_OP_ENTRY2(CRC32, G,d, E,b, cpuid(SSE42)),
    },
    [1] = {
        X86_OP_ENTRY3(MOVBE, M,y, G,y, None,None, cpuid(MOVBE)),
        X86_OP_ENTRY3(MOVBE, M,w, G,w, None,None, cpuid(MOVBE)),
        {},
        X86_OP_ENTRY2(CRC32, G,d, E,y, cpuid(SSE42)),
        X86_OP_ENTRY2(CRC32, G,d, E,w, cpuid(SSE42)),
    },
    [2] = {
        X86_OP_ENTRY3(ANDN, G,y, B,y, E,y, vex13 cpuid(BMI1)),
        {},
        {},
        {},
        {},
    },
    [3] = {
        X86_OP_GROUP3(group17, B,y, E,y, None,None, vex13 cpuid(BMI1)),
        {},
        {},
        {},
        {},
    },
    [5] = {
        X86_OP_ENTRY3(BZHI, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        {},
        X86_OP_ENTRY3(PEXT, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        X86_OP_ENTRY3(PDEP, G,y, B,y, E,y, vex13 cpuid(BMI2)),
        {},
    },
    [6] = {
        {},
        X86_OP_ENTRY2(ADCX, G,y, E,y, cpuid(ADX)),
        X86_OP_ENTRY2(ADOX, G,y, E,y, cpuid(ADX)),
        X86_OP_ENTRY3(MULX, /* B,y, */ G,y, E,y, 2,y, vex13 cpuid(BMI2)),
        {},
    },
    [7] = {
        X86_OP_ENTRY3(BEXTR, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SHLX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SARX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        X86_OP_ENTRY3(SHRX, G,y, E,y, B,y, vex13 cpuid(BMI1)),
        {},
    },
};

static void decode_0F38(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    if (*b < 0xf0) {
        *entry = opcodes_0F38_00toEF[*b];
    } else {
        int row = 0;
        if (s->prefix & PREFIX_REPZ) {
            /* The REPZ (F3) prefix has priority over 66 */
            row = 2;
        } else {
            row += s->prefix & PREFIX_REPNZ ? 3 : 0;
            row += s->prefix & PREFIX_DATA ? 1 : 0;
        }
        *entry = opcodes_0F38_F0toFF[*b & 15][row];
    }
}

static void decode_VINSERTPS(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry
        vinsertps_reg = X86_OP_ENTRY4(VINSERTPS_r, V,dq, H,dq, U,dq, vex5 cpuid(SSE41) p_66),
        vinsertps_mem = X86_OP_ENTRY4(VINSERTPS_m, V,dq, H,dq, M,d,  vex5 cpuid(SSE41) p_66);

    int modrm = get_modrm(s, env);
    *entry = (modrm >> 6) == 3 ? vinsertps_reg : vinsertps_mem;
}

static const X86OpEntry opcodes_0F3A[256] = {
    /*
     * These are VEX-only, but incorrectly listed in the manual as exception type 4.
     * Also the "qq" instructions are sometimes omitted by Table 2-17, but are VEX256
     * only.
     */
    [0x00] = X86_OP_ENTRY3(VPERMQ,      V,qq, W,qq, I,b,  vex6 cpuid(AVX2) p_66),
    [0x01] = X86_OP_ENTRY3(VPERMQ,      V,qq, W,qq, I,b,  vex6 cpuid(AVX2) p_66), /* VPERMPD */
    [0x02] = X86_OP_ENTRY4(VBLENDPS,    V,x,  H,x,  W,x,  vex6 cpuid(AVX2) p_66), /* VPBLENDD */
    [0x04] = X86_OP_ENTRY3(VPERMILPS_i, V,x,  W,x,  I,b,  vex6 cpuid(AVX) p_66),
    [0x05] = X86_OP_ENTRY3(VPERMILPD_i, V,x,  W,x,  I,b,  vex6 cpuid(AVX) p_66),
    [0x06] = X86_OP_ENTRY4(VPERM2x128,  V,qq, H,qq, W,qq, vex6 cpuid(AVX) p_66),

    [0x14] = X86_OP_ENTRY3(PEXTRB,     E,b,  V,dq, I,b,  vex5 cpuid(SSE41) zext0 p_66),
    [0x15] = X86_OP_ENTRY3(PEXTRW,     E,w,  V,dq, I,b,  vex5 cpuid(SSE41) zext0 p_66),
    [0x16] = X86_OP_ENTRY3(PEXTR,      E,y,  V,dq, I,b,  vex5 cpuid(SSE41) p_66),
    [0x17] = X86_OP_ENTRY3(VEXTRACTPS, E,d,  V,dq, I,b,  vex5 cpuid(SSE41) p_66),
    [0x1d] = X86_OP_ENTRY3(VCVTPS2PH,  W,ph, V,x,  I,b,  vex11 cpuid(F16C) p_66),

    [0x20] = X86_OP_ENTRY4(PINSRB,     V,dq, H,dq, E,b,  vex5 cpuid(SSE41) zext2 p_66),
    [0x21] = X86_OP_GROUP0(VINSERTPS),
    [0x22] = X86_OP_ENTRY4(PINSR,      V,dq, H,dq, E,y,  vex5 cpuid(SSE41) p_66),

    [0x40] = X86_OP_ENTRY4(VDDPS,      V,x,  H,x,  W,x,  vex2 cpuid(SSE41) p_66),
    [0x41] = X86_OP_ENTRY4(VDDPD,      V,dq, H,dq, W,dq, vex2 cpuid(SSE41) p_66),
    [0x42] = X86_OP_ENTRY4(VMPSADBW,   V,x,  H,x,  W,x,  vex2 cpuid(SSE41) avx2_256 p_66),
    [0x44] = X86_OP_ENTRY4(PCLMULQDQ,  V,dq, H,dq, W,dq, vex4 cpuid(PCLMULQDQ) p_66),
    [0x46] = X86_OP_ENTRY4(VPERM2x128, V,qq, H,qq, W,qq, vex6 cpuid(AVX2) p_66),

    [0x60] = X86_OP_ENTRY4(PCMPESTRM,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x61] = X86_OP_ENTRY4(PCMPESTRI,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x62] = X86_OP_ENTRY4(PCMPISTRM,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),
    [0x63] = X86_OP_ENTRY4(PCMPISTRI,  None,None, V,dq, W,dq, vex4_unal cpuid(SSE42) p_66),

    [0x08] = X86_OP_ENTRY3(VROUNDPS,   V,x,  W,x,  I,b,  vex2 cpuid(SSE41) p_66),
    [0x09] = X86_OP_ENTRY3(VROUNDPD,   V,x,  W,x,  I,b,  vex2 cpuid(SSE41) p_66),
    /*
     * Not listed as four operand in the manual.  Also writes and reads 128-bits
     * from the first two operands due to the V operand picking higher entries of
     * the H operand; the "Vss,Hss,Wss" description from the manual is incorrect.
     * For other unary operations such as VSQRTSx this is hidden by the "REPScalar"
     * value of vex_special, because the table lists the operand types of VSQRTPx.
     */
    [0x0a] = X86_OP_ENTRY4(VROUNDSS,   V,x,  H,x, W,ss, vex3 cpuid(SSE41) p_66),
    [0x0b] = X86_OP_ENTRY4(VROUNDSD,   V,x,  H,x, W,sd, vex3 cpuid(SSE41) p_66),
    [0x0c] = X86_OP_ENTRY4(VBLENDPS,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) p_66),
    [0x0d] = X86_OP_ENTRY4(VBLENDPD,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) p_66),
    [0x0e] = X86_OP_ENTRY4(VPBLENDW,   V,x,  H,x,  W,x,  vex4 cpuid(SSE41) avx2_256 p_66),
    [0x0f] = X86_OP_ENTRY4(PALIGNR,    V,x,  H,x,  W,x,  vex4 cpuid(SSSE3) mmx avx2_256 p_00_66),

    [0x18] = X86_OP_ENTRY4(VINSERTx128,  V,qq, H,qq, W,qq, vex6 cpuid(AVX) p_66),
    [0x19] = X86_OP_ENTRY3(VEXTRACTx128, W,dq, V,qq, I,b,  vex6 cpuid(AVX) p_66),

    [0x38] = X86_OP_ENTRY4(VINSERTx128,  V,qq, H,qq, W,qq, vex6 cpuid(AVX2) p_66),
    [0x39] = X86_OP_ENTRY3(VEXTRACTx128, W,dq, V,qq, I,b,  vex6 cpuid(AVX2) p_66),

    /* Listed incorrectly as type 4 */
    [0x4a] = X86_OP_ENTRY4(VBLENDVPS, V,x,  H,x,  W,x,   vex6 cpuid(AVX) p_66),
    [0x4b] = X86_OP_ENTRY4(VBLENDVPD, V,x,  H,x,  W,x,   vex6 cpuid(AVX) p_66),
    [0x4c] = X86_OP_ENTRY4(VPBLENDVB, V,x,  H,x,  W,x,   vex6 cpuid(AVX) p_66 avx2_256),

    [0xdf] = X86_OP_ENTRY3(VAESKEYGEN, V,dq, W,dq, I,b,  vex4 cpuid(AES) p_66),

    [0xF0] = X86_OP_ENTRY3(RORX, G,y, E,y, I,b, vex13 cpuid(BMI2) p_f2),
};

static void decode_0F3A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    *entry = opcodes_0F3A[*b];
}

/*
 * There are some mistakes in the operands in the manual, and the load/store/register
 * cases are easiest to keep separate, so the entries for 10-17 follow simplicity and
 * efficiency of implementation rather than copying what the manual says.
 *
 * In particular:
 *
 * 1) "VMOVSS m32, xmm1" and "VMOVSD m64, xmm1" do not support VEX.vvvv != 1111b,
 * but this is not mentioned in the tables.
 *
 * 2) MOVHLPS, MOVHPS, MOVHPD, MOVLPD, MOVLPS read the high quadword of one of their
 * operands, which must therefore be dq; MOVLPD and MOVLPS also write the high
 * quadword of the V operand.
 */
static void decode_0F10(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F10_reg[4] = {
        X86_OP_ENTRY3(MOVDQ,   V,x,  None,None, W,x, vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,   V,x,  None,None, W,x, vex4_unal), /* MOVUPD */
        X86_OP_ENTRY3(VMOVSS,  V,x,  H,x,       W,x, vex4),
        X86_OP_ENTRY3(VMOVLPx, V,x,  H,x,       W,x, vex4), /* MOVSD */
    };

    static const X86OpEntry opcodes_0F10_mem[4] = {
        X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x,  vex4_unal), /* MOVUPS */
        X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x,  vex4_unal), /* MOVUPD */
        X86_OP_ENTRY3(VMOVSS_ld,  V,x,  H,x,       M,ss, vex4),
        X86_OP_ENTRY3(VMOVSD_ld,  V,x,  H,x,       M,sd, vex4),
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F10_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F10_mem);
    }
}

static void decode_0F11(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F11_reg[4] = {
        X86_OP_ENTRY3(MOVDQ,   W,x,  None,None, V,x, vex4), /* MOVPS */
        X86_OP_ENTRY3(MOVDQ,   W,x,  None,None, V,x, vex4), /* MOVPD */
        X86_OP_ENTRY3(VMOVSS,  W,x,  H,x,       V,x, vex4),
        X86_OP_ENTRY3(VMOVLPx, W,x,  H,x,       V,q, vex4), /* MOVSD */
    };

    static const X86OpEntry opcodes_0F11_mem[4] = {
        X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex4), /* MOVPS */
        X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex4), /* MOVPD */
        X86_OP_ENTRY3(VMOVSS_st,  M,ss, None,None, V,x, vex4),
        X86_OP_ENTRY3(VMOVLPx_st, M,sd, None,None, V,x, vex4), /* MOVSD */
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F11_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F11_mem);
    }
}

static void decode_0F12(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F12_mem[4] = {
        /*
         * Use dq for operand for compatibility with gen_MOVSD and
         * to allow VEX128 only.
         */
        X86_OP_ENTRY3(VMOVLPx_ld, V,dq, H,dq,      M,q, vex4), /* MOVLPS */
        X86_OP_ENTRY3(VMOVLPx_ld, V,dq, H,dq,      M,q, vex4), /* MOVLPD */
        X86_OP_ENTRY3(VMOVSLDUP,  V,x,  None,None, W,x, vex4 cpuid(SSE3)),
        X86_OP_ENTRY3(VMOVDDUP,   V,x,  None,None, WM,q, vex4 cpuid(SSE3)), /* qq if VEX.256 */
    };
    static const X86OpEntry opcodes_0F12_reg[4] = {
        X86_OP_ENTRY3(VMOVHLPS,  V,dq, H,dq,       U,dq, vex4),
        X86_OP_ENTRY3(VMOVLPx,   W,x,  H,x,        U,q,  vex4), /* MOVLPD */
        X86_OP_ENTRY3(VMOVSLDUP, V,x,  None,None,  U,x,  vex4 cpuid(SSE3)),
        X86_OP_ENTRY3(VMOVDDUP,  V,x,  None,None,  U,x,  vex4 cpuid(SSE3)),
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F12_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F12_mem);
        if ((s->prefix & PREFIX_REPNZ) && s->vex_l) {
            entry->s2 = X86_SIZE_qq;
        }
    }
}

static void decode_0F16(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F16_mem[4] = {
        /*
         * Operand 1 technically only reads the low 64 bits, but uses dq so that
         * it is easier to check for op0 == op1 in an endianness-neutral manner.
         */
        X86_OP_ENTRY3(VMOVHPx_ld, V,dq, H,dq,      M,q, vex4), /* MOVHPS */
        X86_OP_ENTRY3(VMOVHPx_ld, V,dq, H,dq,      M,q, vex4), /* MOVHPD */
        X86_OP_ENTRY3(VMOVSHDUP,  V,x,  None,None, W,x, vex4 cpuid(SSE3)),
        {},
    };
    static const X86OpEntry opcodes_0F16_reg[4] = {
        /* Same as above, operand 1 could be Hq if it wasn't for big-endian.  */
        X86_OP_ENTRY3(VMOVLHPS,  V,dq, H,dq,      U,q, vex4),
        X86_OP_ENTRY3(VMOVHPx,   V,x,  H,x,       U,x, vex4), /* MOVHPD */
        X86_OP_ENTRY3(VMOVSHDUP, V,x,  None,None, U,x, vex4 cpuid(SSE3)),
        {},
    };

    if ((get_modrm(s, env) >> 6) == 3) {
        *entry = *decode_by_prefix(s, opcodes_0F16_reg);
    } else {
        *entry = *decode_by_prefix(s, opcodes_0F16_mem);
    }
}

static void decode_0F2A(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2A[4] = {
        X86_OP_ENTRY3(CVTPI2Px,  V,x,  None,None, Q,q),
        X86_OP_ENTRY3(CVTPI2Px,  V,x,  None,None, Q,q),
        X86_OP_ENTRY3(VCVTSI2Sx, V,x,  H,x, E,y,        vex3),
        X86_OP_ENTRY3(VCVTSI2Sx, V,x,  H,x, E,y,        vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2A);
}

static void decode_0F2B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2B[4] = {
        X86_OP_ENTRY3(MOVDQ,      M,x,  None,None, V,x, vex4), /* MOVNTPS */
        X86_OP_ENTRY3(MOVDQ,      M,x,  None,None, V,x, vex4), /* MOVNTPD */
        X86_OP_ENTRY3(VMOVSS_st,  M,ss, None,None, V,x, vex4 cpuid(SSE4A)), /* MOVNTSS */
        X86_OP_ENTRY3(VMOVLPx_st, M,sd, None,None, V,x, vex4 cpuid(SSE4A)), /* MOVNTSD */
    };

    *entry = *decode_by_prefix(s, opcodes_0F2B);
}

static void decode_0F2C(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2C[4] = {
        /* Listed as ps/pd in the manual, but CVTTPS2PI only reads 64-bit.  */
        X86_OP_ENTRY3(CVTTPx2PI,  P,q,  None,None, W,q),
        X86_OP_ENTRY3(CVTTPx2PI,  P,q,  None,None, W,dq),
        X86_OP_ENTRY3(VCVTTSx2SI, G,y,  None,None, W,ss, vex3),
        X86_OP_ENTRY3(VCVTTSx2SI, G,y,  None,None, W,sd, vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2C);
}

static void decode_0F2D(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F2D[4] = {
        /* Listed as ps/pd in the manual, but CVTPS2PI only reads 64-bit.  */
        X86_OP_ENTRY3(CVTPx2PI,  P,q,  None,None, W,q),
        X86_OP_ENTRY3(CVTPx2PI,  P,q,  None,None, W,dq),
        X86_OP_ENTRY3(VCVTSx2SI, G,y,  None,None, W,ss, vex3),
        X86_OP_ENTRY3(VCVTSx2SI, G,y,  None,None, W,sd, vex3),
    };
    *entry = *decode_by_prefix(s, opcodes_0F2D);
}

static void decode_sse_unary(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    if (!(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ))) {
        entry->op1 = X86_TYPE_None;
        entry->s1 = X86_SIZE_None;
    }
    switch (*b) {
    case 0x51: entry->gen = gen_VSQRT; break;
    case 0x52: entry->gen = gen_VRSQRT; break;
    case 0x53: entry->gen = gen_VRCP; break;
    case 0x5A: entry->gen = gen_VCVTfp2fp; break;
    }
}

static void decode_0F5B(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0F5B[4] = {
        X86_OP_ENTRY2(VCVTDQ2PS,   V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTPS2DQ,   V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTTPS2DQ,  V,x, W,x,      vex2),
        {},
    };
    *entry = *decode_by_prefix(s, opcodes_0F5B);
}

static void decode_0FE6(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    static const X86OpEntry opcodes_0FE6[4] = {
        {},
        X86_OP_ENTRY2(VCVTTPD2DQ,  V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTDQ2PD,   V,x, W,x,      vex2),
        X86_OP_ENTRY2(VCVTPD2DQ,   V,x, W,x,      vex2),
    };
    *entry = *decode_by_prefix(s, opcodes_0FE6);
}

static const X86OpEntry opcodes_0F[256] = {
    [0x0E] = X86_OP_ENTRY0(EMMS,                              cpuid(3DNOW)), /* femms */
    /*
     * 3DNow!'s opcode byte comes *after* modrm and displacements, making it
     * more like an Ib operand.  Dispatch to the right helper in a single gen_*
     * function.
     */
    [0x0F] = X86_OP_ENTRY3(3dnow,       P,q, Q,q, I,b,        cpuid(3DNOW)),

    [0x10] = X86_OP_GROUP0(0F10),
    [0x11] = X86_OP_GROUP0(0F11),
    [0x12] = X86_OP_GROUP0(0F12),
    [0x13] = X86_OP_ENTRY3(VMOVLPx_st,  M,q, None,None, V,q,  vex4 p_00_66),
    [0x14] = X86_OP_ENTRY3(VUNPCKLPx,   V,x, H,x, W,x,        vex4 p_00_66),
    [0x15] = X86_OP_ENTRY3(VUNPCKHPx,   V,x, H,x, W,x,        vex4 p_00_66),
    [0x16] = X86_OP_GROUP0(0F16),
    /* Incorrectly listed as Mq,Vq in the manual */
    [0x17] = X86_OP_ENTRY3(VMOVHPx_st,  M,q, None,None, V,dq, vex4 p_00_66),

    [0x50] = X86_OP_ENTRY3(MOVMSK,     G,y, None,None, U,x, vex7 p_00_66),
    [0x51] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x52] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex4_rep5 p_00_f3),
    [0x53] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex4_rep5 p_00_f3),
    [0x54] = X86_OP_ENTRY3(PAND,       V,x, H,x, W,x,  vex4 p_00_66), /* vand */
    [0x55] = X86_OP_ENTRY3(PANDN,      V,x, H,x, W,x,  vex4 p_00_66), /* vandn */
    [0x56] = X86_OP_ENTRY3(POR,        V,x, H,x, W,x,  vex4 p_00_66), /* vor */
    [0x57] = X86_OP_ENTRY3(PXOR,       V,x, H,x, W,x,  vex4 p_00_66), /* vxor */

    [0x60] = X86_OP_ENTRY3(PUNPCKLBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x61] = X86_OP_ENTRY3(PUNPCKLWD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x62] = X86_OP_ENTRY3(PUNPCKLDQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x63] = X86_OP_ENTRY3(PACKSSWB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x64] = X86_OP_ENTRY3(PCMPGTB,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x65] = X86_OP_ENTRY3(PCMPGTW,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x66] = X86_OP_ENTRY3(PCMPGTD,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x67] = X86_OP_ENTRY3(PACKUSWB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0x70] = X86_OP_GROUP0(0F70),
    [0x71] = X86_OP_GROUP0(group12),
    [0x72] = X86_OP_GROUP0(group13),
    [0x73] = X86_OP_GROUP0(group14),
    [0x74] = X86_OP_ENTRY3(PCMPEQB,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x75] = X86_OP_ENTRY3(PCMPEQW,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x76] = X86_OP_ENTRY3(PCMPEQD,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x77] = X86_OP_GROUP0(0F77),

    [0x28] = X86_OP_ENTRY3(MOVDQ,      V,x,  None,None, W,x, vex1 p_00_66), /* MOVAPS */
    [0x29] = X86_OP_ENTRY3(MOVDQ,      W,x,  None,None, V,x, vex1 p_00_66), /* MOVAPS */
    [0x2A] = X86_OP_GROUP0(0F2A),
    [0x2B] = X86_OP_GROUP0(0F2B),
    [0x2C] = X86_OP_GROUP0(0F2C),
    [0x2D] = X86_OP_GROUP0(0F2D),
    [0x2E] = X86_OP_ENTRY3(VUCOMI,     None,None, V,x, W,x,  vex4 p_00_66),
    [0x2F] = X86_OP_ENTRY3(VCOMI,      None,None, V,x, W,x,  vex4 p_00_66),

    [0x38] = X86_OP_GROUP0(0F38),
    [0x3a] = X86_OP_GROUP0(0F3A),

    [0x58] = X86_OP_ENTRY3(VADD,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x59] = X86_OP_ENTRY3(VMUL,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5a] = X86_OP_GROUP3(sse_unary,  V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5b] = X86_OP_GROUP0(0F5B),
    [0x5c] = X86_OP_ENTRY3(VSUB,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5d] = X86_OP_ENTRY3(VMIN,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5e] = X86_OP_ENTRY3(VDIV,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),
    [0x5f] = X86_OP_ENTRY3(VMAX,       V,x, H,x, W,x, vex2_rep3 p_00_66_f3_f2),

    [0x68] = X86_OP_ENTRY3(PUNPCKHBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x69] = X86_OP_ENTRY3(PUNPCKHWD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6a] = X86_OP_ENTRY3(PUNPCKHDQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6b] = X86_OP_ENTRY3(PACKSSDW,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0x6c] = X86_OP_ENTRY3(PUNPCKLQDQ, V,x, H,x, W,x,  vex4 p_66 avx2_256),
    [0x6d] = X86_OP_ENTRY3(PUNPCKHQDQ, V,x, H,x, W,x,  vex4 p_66 avx2_256),
    [0x6e] = X86_OP_ENTRY3(MOVD_to,    V,x, None,None, E,y, vex5 mmx p_00_66),  /* wrong dest Vy on SDM! */
    [0x6f] = X86_OP_GROUP0(0F6F),

    [0x78] = X86_OP_GROUP0(0F78),
    [0x79] = X86_OP_GROUP2(0F79,       V,x, U,x,       cpuid(SSE4A)),
    [0x7c] = X86_OP_ENTRY3(VHADD,      V,x, H,x, W,x,  vex2 cpuid(SSE3) p_66_f2),
    [0x7d] = X86_OP_ENTRY3(VHSUB,      V,x, H,x, W,x,  vex2 cpuid(SSE3) p_66_f2),
    [0x7e] = X86_OP_GROUP0(0F7E),
    [0x7f] = X86_OP_GROUP0(0F7F),

    [0xae] = X86_OP_GROUP0(group15),

    [0xc2] = X86_OP_ENTRY4(VCMP,       V,x, H,x, W,x,       vex2_rep3 p_00_66_f3_f2),
    [0xc4] = X86_OP_ENTRY4(PINSRW,     V,dq,H,dq,E,w,       vex5 mmx p_00_66),
    [0xc5] = X86_OP_ENTRY3(PEXTRW,     G,d, U,dq,I,b,       vex5 mmx p_00_66),
    [0xc6] = X86_OP_ENTRY4(VSHUF,      V,x, H,x, W,x,       vex4 p_00_66),

    [0xd0] = X86_OP_ENTRY3(VADDSUB,   V,x, H,x, W,x,        vex2 cpuid(SSE3) p_66_f2),
    [0xd1] = X86_OP_ENTRY3(PSRLW_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd2] = X86_OP_ENTRY3(PSRLD_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd3] = X86_OP_ENTRY3(PSRLQ_r,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd4] = X86_OP_ENTRY3(PADDQ,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd5] = X86_OP_ENTRY3(PMULLW,    V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xd6] = X86_OP_GROUP0(0FD6),
    [0xd7] = X86_OP_ENTRY3(PMOVMSKB,  G,d, None,None, U,x,  vex7 mmx avx2_256 p_00_66),

    [0xe0] = X86_OP_ENTRY3(PAVGB,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe1] = X86_OP_ENTRY3(PSRAW_r,   V,x, H,x, W,x,        vex7 mmx avx2_256 p_00_66),
    [0xe2] = X86_OP_ENTRY3(PSRAD_r,   V,x, H,x, W,x,        vex7 mmx avx2_256 p_00_66),
    [0xe3] = X86_OP_ENTRY3(PAVGW,     V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe4] = X86_OP_ENTRY3(PMULHUW,   V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe5] = X86_OP_ENTRY3(PMULHW,    V,x, H,x, W,x,        vex4 mmx avx2_256 p_00_66),
    [0xe6] = X86_OP_GROUP0(0FE6),
    [0xe7] = X86_OP_ENTRY3(MOVDQ,     W,x, None,None, V,x,  vex1 mmx p_00_66), /* MOVNTQ/MOVNTDQ */

    [0xf0] = X86_OP_ENTRY3(MOVDQ,    V,x, None,None, WM,x,  vex4_unal cpuid(SSE3) p_f2), /* LDDQU */
    [0xf1] = X86_OP_ENTRY3(PSLLW_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf2] = X86_OP_ENTRY3(PSLLD_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf3] = X86_OP_ENTRY3(PSLLQ_r,  V,x, H,x, W,x,         vex7 mmx avx2_256 p_00_66),
    [0xf4] = X86_OP_ENTRY3(PMULUDQ,  V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf5] = X86_OP_ENTRY3(PMADDWD,  V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf6] = X86_OP_ENTRY3(PSADBW,   V,x, H,x, W,x,         vex4 mmx avx2_256 p_00_66),
    [0xf7] = X86_OP_ENTRY3(MASKMOV,  None,None, V,dq, U,dq, vex4_unal avx2_256 mmx p_00_66),

    /* Incorrectly missing from 2-17 */
    [0xd8] = X86_OP_ENTRY3(PSUBUSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xd9] = X86_OP_ENTRY3(PSUBUSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xda] = X86_OP_ENTRY3(PMINUB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdb] = X86_OP_ENTRY3(PAND,     V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdc] = X86_OP_ENTRY3(PADDUSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdd] = X86_OP_ENTRY3(PADDUSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xde] = X86_OP_ENTRY3(PMAXUB,   V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xdf] = X86_OP_ENTRY3(PANDN,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0xe8] = X86_OP_ENTRY3(PSUBSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xe9] = X86_OP_ENTRY3(PSUBSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xea] = X86_OP_ENTRY3(PMINSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xeb] = X86_OP_ENTRY3(POR,     V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xec] = X86_OP_ENTRY3(PADDSB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xed] = X86_OP_ENTRY3(PADDSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xee] = X86_OP_ENTRY3(PMAXSW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xef] = X86_OP_ENTRY3(PXOR,    V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),

    [0xf8] = X86_OP_ENTRY3(PSUBB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xf9] = X86_OP_ENTRY3(PSUBW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfa] = X86_OP_ENTRY3(PSUBD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfb] = X86_OP_ENTRY3(PSUBQ,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfc] = X86_OP_ENTRY3(PADDB,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfd] = X86_OP_ENTRY3(PADDW,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    [0xfe] = X86_OP_ENTRY3(PADDD,  V,x, H,x, W,x,  vex4 mmx avx2_256 p_00_66),
    /* 0xff = UD0 */
};

static void do_decode_0F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *entry = opcodes_0F[*b];
}

static void decode_0F(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *b = x86_ldub_code(env, s);
    do_decode_0F(s, env, entry, b);
}

static const X86OpEntry opcodes_root[256] = {
    [0x0F] = X86_OP_GROUP0(0F),
};

#undef mmx
#undef vex1
#undef vex2
#undef vex3
#undef vex4
#undef vex4_unal
#undef vex5
#undef vex6
#undef vex7
#undef vex8
#undef vex11
#undef vex12
#undef vex13

/*
 * Decode the fixed part of the opcode and place the last
 * in b.
 */
static void decode_root(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b)
{
    *entry = opcodes_root[*b];
}


static int decode_modrm(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                        X86DecodedOp *op, X86OpType type)
{
    int modrm = get_modrm(s, env);
    if ((modrm >> 6) == 3) {
        if (s->prefix & PREFIX_LOCK) {
            decode->e.gen = gen_illegal;
            return 0xff;
        }
        op->n = (modrm & 7);
        if (type != X86_TYPE_Q && type != X86_TYPE_N) {
            op->n |= REX_B(s);
        }
    } else {
        op->has_ea = true;
        op->n = -1;
        decode->mem = gen_lea_modrm_0(env, s, get_modrm(s, env));
    }
    return modrm;
}

static bool decode_op_size(DisasContext *s, X86OpEntry *e, X86OpSize size, MemOp *ot)
{
    switch (size) {
    case X86_SIZE_b:  /* byte */
        *ot = MO_8;
        return true;

    case X86_SIZE_d:  /* 32-bit */
    case X86_SIZE_ss: /* SSE/AVX scalar single precision */
        *ot = MO_32;
        return true;

    case X86_SIZE_p:  /* Far pointer, return offset size */
    case X86_SIZE_s:  /* Descriptor, return offset size */
    case X86_SIZE_v:  /* 16/32/64-bit, based on operand size */
        *ot = s->dflag;
        return true;

    case X86_SIZE_pi: /* MMX */
    case X86_SIZE_q:  /* 64-bit */
    case X86_SIZE_sd: /* SSE/AVX scalar double precision */
        *ot = MO_64;
        return true;

    case X86_SIZE_w:  /* 16-bit */
        *ot = MO_16;
        return true;

    case X86_SIZE_y:  /* 32/64-bit, based on operand size */
        *ot = s->dflag == MO_16 ? MO_32 : s->dflag;
        return true;

    case X86_SIZE_z:  /* 16-bit for 16-bit operand size, else 32-bit */
        *ot = s->dflag == MO_16 ? MO_16 : MO_32;
        return true;

    case X86_SIZE_dq: /* SSE/AVX 128-bit */
        if (e->special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            *ot = MO_64;
            return true;
        }
        if (s->vex_l && e->s0 != X86_SIZE_qq && e->s1 != X86_SIZE_qq) {
            return false;
        }
        *ot = MO_128;
        return true;

    case X86_SIZE_qq: /* AVX 256-bit */
        if (!s->vex_l) {
            return false;
        }
        *ot = MO_256;
        return true;

    case X86_SIZE_x:  /* 128/256-bit, based on operand size */
        if (e->special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            *ot = MO_64;
            return true;
        }
        /* fall through */
    case X86_SIZE_ps: /* SSE/AVX packed single precision */
    case X86_SIZE_pd: /* SSE/AVX packed double precision */
        *ot = s->vex_l ? MO_256 : MO_128;
        return true;

    case X86_SIZE_ph: /* SSE/AVX packed half precision */
        *ot = s->vex_l ? MO_128 : MO_64;
        return true;

    case X86_SIZE_d64:  /* Default to 64-bit in 64-bit mode */
        *ot = CODE64(s) && s->dflag == MO_32 ? MO_64 : s->dflag;
        return true;

    case X86_SIZE_f64:  /* Ignore size override prefix in 64-bit mode */
        *ot = CODE64(s) ? MO_64 : s->dflag;
        return true;

    default:
        *ot = -1;
        return true;
    }
}

static bool decode_op(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                      X86DecodedOp *op, X86OpType type, int b)
{
    int modrm;

    switch (type) {
    case X86_TYPE_None:  /* Implicit or absent */
    case X86_TYPE_A:  /* Implicit */
    case X86_TYPE_F:  /* EFLAGS/RFLAGS */
        break;

    case X86_TYPE_B:  /* VEX.vvvv selects a GPR */
        op->unit = X86_OP_INT;
        op->n = s->vex_v;
        break;

    case X86_TYPE_C:  /* REG in the modrm byte selects a control register */
        op->unit = X86_OP_CR;
        goto get_reg;

    case X86_TYPE_D:  /* REG in the modrm byte selects a debug register */
        op->unit = X86_OP_DR;
        goto get_reg;

    case X86_TYPE_G:  /* REG in the modrm byte selects a GPR */
        op->unit = X86_OP_INT;
        goto get_reg;

    case X86_TYPE_S:  /* reg selects a segment register */
        op->unit = X86_OP_SEG;
        goto get_reg;

    case X86_TYPE_P:
        op->unit = X86_OP_MMX;
        goto get_reg;

    case X86_TYPE_V:  /* reg in the modrm byte selects an XMM/YMM register */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
    get_reg:
        op->n = ((get_modrm(s, env) >> 3) & 7) | REX_R(s);
        break;

    case X86_TYPE_E:  /* ALU modrm operand */
        op->unit = X86_OP_INT;
        goto get_modrm;

    case X86_TYPE_Q:  /* MMX modrm operand */
        op->unit = X86_OP_MMX;
        goto get_modrm;

    case X86_TYPE_W:  /* XMM/YMM modrm operand */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
        goto get_modrm;

    case X86_TYPE_N:  /* R/M in the modrm byte selects an MMX register */
        op->unit = X86_OP_MMX;
        goto get_modrm_reg;

    case X86_TYPE_U:  /* R/M in the modrm byte selects an XMM/YMM register */
        if (decode->e.special == X86_SPECIAL_MMX &&
            !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
            op->unit = X86_OP_MMX;
        } else {
            op->unit = X86_OP_SSE;
        }
        goto get_modrm_reg;

    case X86_TYPE_R:  /* R/M in the modrm byte selects a register */
        op->unit = X86_OP_INT;
    get_modrm_reg:
        modrm = get_modrm(s, env);
        if ((modrm >> 6) != 3) {
            return false;
        }
        goto get_modrm;

    case X86_TYPE_WM:  /* modrm byte selects an XMM/YMM memory operand */
        op->unit = X86_OP_SSE;
        /* fall through */
    case X86_TYPE_M:  /* modrm byte selects a memory operand */
        modrm = get_modrm(s, env);
        if ((modrm >> 6) == 3) {
            return false;
        }
    get_modrm:
        decode_modrm(s, env, decode, op, type);
        break;

    case X86_TYPE_O:  /* Absolute address encoded in the instruction */
        op->unit = X86_OP_INT;
        op->has_ea = true;
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_DS,
            .base = -1,
            .index = -1,
            .disp = insn_get_addr(env, s, s->aflag)
        };
        break;

    case X86_TYPE_H:  /* For AVX, VEX.vvvv selects an XMM/YMM register */
        if ((s->prefix & PREFIX_VEX)) {
            op->unit = X86_OP_SSE;
            op->n = s->vex_v;
            break;
        }
        if (op == &decode->op[0]) {
            /* shifts place the destination in VEX.vvvv, use modrm */
            return decode_op(s, env, decode, op, decode->e.op1, b);
        } else {
            return decode_op(s, env, decode, op, decode->e.op0, b);
        }

    case X86_TYPE_I:  /* Immediate */
        op->unit = X86_OP_IMM;
        decode->immediate = insn_get_signed(env, s, op->ot);
        break;

    case X86_TYPE_J:  /* Relative offset for a jump */
        op->unit = X86_OP_IMM;
        decode->immediate = insn_get_signed(env, s, op->ot);
        decode->immediate += s->pc - s->cs_base;
        if (s->dflag == MO_16) {
            decode->immediate &= 0xffff;
        } else if (!CODE64(s)) {
            decode->immediate &= 0xffffffffu;
        }
        break;

    case X86_TYPE_L:  /* The upper 4 bits of the immediate select a 128-bit register */
        op->n = insn_get(env, s, op->ot) >> 4;
        break;

    case X86_TYPE_X:  /* string source */
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_DS,
            .base = R_ESI,
            .index = -1,
        };
        break;

    case X86_TYPE_Y:  /* string destination */
        op->n = -1;
        decode->mem = (AddressParts) {
            .def_seg = R_ES,
            .base = R_EDI,
            .index = -1,
        };
        break;

    case X86_TYPE_2op:
        *op = decode->op[0];
        break;

    case X86_TYPE_LoBits:
        op->n = (b & 7) | REX_B(s);
        op->unit = X86_OP_INT;
        break;

    case X86_TYPE_0 ... X86_TYPE_7:
        op->n = type - X86_TYPE_0;
        op->unit = X86_OP_INT;
        break;

    case X86_TYPE_ES ... X86_TYPE_GS:
        op->n = type - X86_TYPE_ES;
        op->unit = X86_OP_SEG;
        break;
    }

    return true;
}

static bool validate_sse_prefix(DisasContext *s, X86OpEntry *e)
{
    uint16_t sse_prefixes;

    if (!e->valid_prefix) {
        return true;
    }
    if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
        /* In SSE instructions, 0xF3 and 0xF2 cancel 0x66.  */
        s->prefix &= ~PREFIX_DATA;
    }

    /* Now, either zero or one bit is set in sse_prefixes.  */
    sse_prefixes = s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA);
    return e->valid_prefix & (1 << sse_prefixes);
}

static bool decode_insn(DisasContext *s, CPUX86State *env, X86DecodeFunc decode_func,
                        X86DecodedInsn *decode)
{
    X86OpEntry *e = &decode->e;

    decode_func(s, env, e, &decode->b);
    while (e->is_decode) {
        e->is_decode = false;
        e->decode(s, env, e, &decode->b);
    }

    if (!validate_sse_prefix(s, e)) {
        return false;
    }

    /* First compute size of operands in order to initialize s->rip_offset.  */
    if (e->op0 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s0, &decode->op[0].ot)) {
            return false;
        }
        if (e->op0 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[0].ot;
        }
    }
    if (e->op1 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s1, &decode->op[1].ot)) {
            return false;
        }
        if (e->op1 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[1].ot;
        }
    }
    if (e->op2 != X86_TYPE_None) {
        if (!decode_op_size(s, e, e->s2, &decode->op[2].ot)) {
            return false;
        }
        if (e->op2 == X86_TYPE_I) {
            s->rip_offset += 1 << decode->op[2].ot;
        }
    }
    if (e->op3 != X86_TYPE_None) {
        /*
         * A couple instructions actually use the extra immediate byte for an Lx
         * register operand; those are handled in the gen_* functions as one off.
         */
        assert(e->op3 == X86_TYPE_I && e->s3 == X86_SIZE_b);
        s->rip_offset += 1;
    }

    if (e->op0 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[0], e->op0, decode->b)) {
        return false;
    }

    if (e->op1 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[1], e->op1, decode->b)) {
        return false;
    }

    if (e->op2 != X86_TYPE_None &&
        !decode_op(s, env, decode, &decode->op[2], e->op2, decode->b)) {
        return false;
    }

    if (e->op3 != X86_TYPE_None) {
        decode->immediate = insn_get_signed(env, s, MO_8);
    }

    return true;
}

static bool has_cpuid_feature(DisasContext *s, X86CPUIDFeature cpuid)
{
    switch (cpuid) {
    case X86_FEAT_None:
        return true;
    case X86_FEAT_F16C:
        return (s->cpuid_ext_features & CPUID_EXT_F16C);
    case X86_FEAT_FMA:
        return (s->cpuid_ext_features & CPUID_EXT_FMA);
    case X86_FEAT_MOVBE:
        return (s->cpuid_ext_features & CPUID_EXT_MOVBE);
    case X86_FEAT_PCLMULQDQ:
        return (s->cpuid_ext_features & CPUID_EXT_PCLMULQDQ);
    case X86_FEAT_SSE:
        return (s->cpuid_ext_features & CPUID_SSE);
    case X86_FEAT_SSE2:
        return (s->cpuid_ext_features & CPUID_SSE2);
    case X86_FEAT_SSE3:
        return (s->cpuid_ext_features & CPUID_EXT_SSE3);
    case X86_FEAT_SSSE3:
        return (s->cpuid_ext_features & CPUID_EXT_SSSE3);
    case X86_FEAT_SSE41:
        return (s->cpuid_ext_features & CPUID_EXT_SSE41);
    case X86_FEAT_SSE42:
        return (s->cpuid_ext_features & CPUID_EXT_SSE42);
    case X86_FEAT_AES:
        if (!(s->cpuid_ext_features & CPUID_EXT_AES)) {
            return false;
        } else if (!(s->prefix & PREFIX_VEX)) {
            return true;
        } else if (!(s->cpuid_ext_features & CPUID_EXT_AVX)) {
            return false;
        } else {
            return !s->vex_l || (s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_VAES);
        }

    case X86_FEAT_AVX:
        return (s->cpuid_ext_features & CPUID_EXT_AVX);

    case X86_FEAT_3DNOW:
        return (s->cpuid_ext2_features & CPUID_EXT2_3DNOW);
    case X86_FEAT_SSE4A:
        return (s->cpuid_ext3_features & CPUID_EXT3_SSE4A);

    case X86_FEAT_ADX:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_ADX);
    case X86_FEAT_BMI1:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_BMI1);
    case X86_FEAT_BMI2:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_BMI2);
    case X86_FEAT_AVX2:
        return (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_AVX2);
    }
    g_assert_not_reached();
}

static bool validate_vex(DisasContext *s, X86DecodedInsn *decode)
{
    X86OpEntry *e = &decode->e;

    switch (e->vex_special) {
    case X86_VEX_REPScalar:
        /*
         * Instructions which differ between 00/66 and F2/F3 in the
         * exception classification and the size of the memory operand.
         */
        assert(e->vex_class == 1 || e->vex_class == 2 || e->vex_class == 4);
        if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
            e->vex_class = e->vex_class < 4 ? 3 : 5;
            if (s->vex_l) {
                goto illegal;
            }
            assert(decode->e.s2 == X86_SIZE_x);
            if (decode->op[2].has_ea) {
                decode->op[2].ot = s->prefix & PREFIX_REPZ ? MO_32 : MO_64;
            }
        }
        break;

    case X86_VEX_SSEUnaligned:
        /* handled in sse_needs_alignment.  */
        break;

    case X86_VEX_AVX2_256:
        if ((s->prefix & PREFIX_VEX) && s->vex_l && !has_cpuid_feature(s, X86_FEAT_AVX2)) {
            goto illegal;
        }
    }

    /* TODO: instructions that require VEX.W=0 (Table 2-16) */

    switch (e->vex_class) {
    case 0:
        if (s->prefix & PREFIX_VEX) {
            goto illegal;
        }
        return true;
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 7:
        if (s->prefix & PREFIX_VEX) {
            if (!(s->flags & HF_AVX_EN_MASK)) {
                goto illegal;
            }
        } else if (e->special != X86_SPECIAL_MMX ||
                   (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) {
            if (!(s->flags & HF_OSFXSR_MASK)) {
                goto illegal;
            }
        }
        break;
    case 12:
        /* Must have a VSIB byte and no address prefix.  */
        assert(s->has_modrm);
        if ((s->modrm & 7) != 4 || s->aflag == MO_16) {
            goto illegal;
        }

        /* Check no overlap between registers.  */
        if (!decode->op[0].has_ea &&
            (decode->op[0].n == decode->mem.index || decode->op[0].n == decode->op[1].n)) {
            goto illegal;
        }
        assert(!decode->op[1].has_ea);
        if (decode->op[1].n == decode->mem.index) {
            goto illegal;
        }
        if (!decode->op[2].has_ea &&
            (decode->op[2].n == decode->mem.index || decode->op[2].n == decode->op[1].n)) {
            goto illegal;
        }
        /* fall through */
    case 6:
    case 11:
        if (!(s->prefix & PREFIX_VEX)) {
            goto illegal;
        }
        if (!(s->flags & HF_AVX_EN_MASK)) {
            goto illegal;
        }
        break;
    case 8:
        /* Non-VEX case handled in decode_0F77.  */
        assert(s->prefix & PREFIX_VEX);
        if (!(s->flags & HF_AVX_EN_MASK)) {
            goto illegal;
        }
        break;
    case 13:
        if (!(s->prefix & PREFIX_VEX)) {
            goto illegal;
        }
        if (s->vex_l) {
            goto illegal;
        }
        /* All integer instructions use VEX.vvvv, so exit.  */
        return true;
    }

    if (s->vex_v != 0 &&
        e->op0 != X86_TYPE_H && e->op0 != X86_TYPE_B &&
        e->op1 != X86_TYPE_H && e->op1 != X86_TYPE_B &&
        e->op2 != X86_TYPE_H && e->op2 != X86_TYPE_B) {
        goto illegal;
    }

    if (s->flags & HF_TS_MASK) {
        goto nm_exception;
    }
    if (s->flags & HF_EM_MASK) {
        goto illegal;
    }
    return true;

nm_exception:
    gen_NM_exception(s);
    return false;
illegal:
    gen_illegal_opcode(s);
    return false;
}

/*
 * Convert one instruction. s->base.is_jmp is set if the translation must
 * be stopped.
 */
static void disas_insn_new(DisasContext *s, CPUState *cpu, int b)
{
    CPUX86State *env = cpu->env_ptr;
    bool first = true;
    X86DecodedInsn decode;
    X86DecodeFunc decode_func = decode_root;

    s->has_modrm = false;

 next_byte:
    if (first) {
        first = false;
    } else {
        b = x86_ldub_code(env, s);
    }
    /* Collect prefixes.  */
    switch (b) {
    case 0xf3:
        s->prefix |= PREFIX_REPZ;
        s->prefix &= ~PREFIX_REPNZ;
        goto next_byte;
    case 0xf2:
        s->prefix |= PREFIX_REPNZ;
        s->prefix &= ~PREFIX_REPZ;
        goto next_byte;
    case 0xf0:
        s->prefix |= PREFIX_LOCK;
        goto next_byte;
    case 0x2e:
        s->override = R_CS;
        goto next_byte;
    case 0x36:
        s->override = R_SS;
        goto next_byte;
    case 0x3e:
        s->override = R_DS;
        goto next_byte;
    case 0x26:
        s->override = R_ES;
        goto next_byte;
    case 0x64:
        s->override = R_FS;
        goto next_byte;
    case 0x65:
        s->override = R_GS;
        goto next_byte;
    case 0x66:
        s->prefix |= PREFIX_DATA;
        goto next_byte;
    case 0x67:
        s->prefix |= PREFIX_ADR;
        goto next_byte;
#ifdef TARGET_X86_64
    case 0x40 ... 0x4f:
        if (CODE64(s)) {
            /* REX prefix */
            s->prefix |= PREFIX_REX;
            s->vex_w = (b >> 3) & 1;
            s->rex_r = (b & 0x4) << 1;
            s->rex_x = (b & 0x2) << 2;
            s->rex_b = (b & 0x1) << 3;
            goto next_byte;
        }
        break;
#endif
    case 0xc5: /* 2-byte VEX */
    case 0xc4: /* 3-byte VEX */
        /*
         * VEX prefixes cannot be used except in 32-bit mode.
         * Otherwise the instruction is LES or LDS.
         */
        if (CODE32(s) && !VM86(s)) {
            static const int pp_prefix[4] = {
                0, PREFIX_DATA, PREFIX_REPZ, PREFIX_REPNZ
            };
            int vex3, vex2 = x86_ldub_code(env, s);

            if (!CODE64(s) && (vex2 & 0xc0) != 0xc0) {
                /*
                 * 4.1.4.6: In 32-bit mode, bits [7:6] must be 11b,
                 * otherwise the instruction is LES or LDS.
                 */
                s->pc--; /* rewind the advance_pc() x86_ldub_code() did */
                break;
            }

            /* 4.1.1-4.1.3: No preceding lock, 66, f2, f3, or rex prefixes. */
            if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ
                             | PREFIX_LOCK | PREFIX_DATA | PREFIX_REX)) {
                goto illegal_op;
            }
#ifdef TARGET_X86_64
            s->rex_r = (~vex2 >> 4) & 8;
#endif
            if (b == 0xc5) {
                /* 2-byte VEX prefix: RVVVVlpp, implied 0f leading opcode byte */
                vex3 = vex2;
                decode_func = decode_0F;
            } else {
                /* 3-byte VEX prefix: RXBmmmmm wVVVVlpp */
                vex3 = x86_ldub_code(env, s);
#ifdef TARGET_X86_64
                s->rex_x = (~vex2 >> 3) & 8;
                s->rex_b = (~vex2 >> 2) & 8;
#endif
                s->vex_w = (vex3 >> 7) & 1;
                switch (vex2 & 0x1f) {
                case 0x01: /* Implied 0f leading opcode bytes.  */
                    decode_func = decode_0F;
                    break;
                case 0x02: /* Implied 0f 38 leading opcode bytes.  */
                    decode_func = decode_0F38;
                    break;
                case 0x03: /* Implied 0f 3a leading opcode bytes.  */
                    decode_func = decode_0F3A;
                    break;
                default:   /* Reserved for future use.  */
                    goto unknown_op;
                }
            }
            s->vex_v = (~vex3 >> 3) & 0xf;
            s->vex_l = (vex3 >> 2) & 1;
            s->prefix |= pp_prefix[vex3 & 3] | PREFIX_VEX;
        }
        break;
    default:
        if (b >= 0x100) {
            b -= 0x100;
            decode_func = do_decode_0F;
        }
        break;
    }

    /* Post-process prefixes.  */
    if (CODE64(s)) {
        /*
         * In 64-bit mode, the default data size is 32-bit.  Select 64-bit
         * data with rex_w, and 16-bit data with 0x66; rex_w takes precedence
         * over 0x66 if both are present.
         */
        s->dflag = (REX_W(s) ? MO_64 : s->prefix & PREFIX_DATA ? MO_16 : MO_32);
        /* In 64-bit mode, 0x67 selects 32-bit addressing.  */
        s->aflag = (s->prefix & PREFIX_ADR ? MO_32 : MO_64);
    } else {
        /* In 16/32-bit mode, 0x66 selects the opposite data size.  */
        if (CODE32(s) ^ ((s->prefix & PREFIX_DATA) != 0)) {
            s->dflag = MO_32;
        } else {
            s->dflag = MO_16;
        }
        /* In 16/32-bit mode, 0x67 selects the opposite addressing.  */
        if (CODE32(s) ^ ((s->prefix & PREFIX_ADR) != 0)) {
            s->aflag = MO_32;
        }  else {
            s->aflag = MO_16;
        }
    }

    memset(&decode, 0, sizeof(decode));
    decode.b = b;
    if (!decode_insn(s, env, decode_func, &decode)) {
        goto illegal_op;
    }
    if (!decode.e.gen) {
        goto unknown_op;
    }

    if (!has_cpuid_feature(s, decode.e.cpuid)) {
        goto illegal_op;
    }

    switch (decode.e.special) {
    case X86_SPECIAL_None:
        break;

    case X86_SPECIAL_Locked:
        if (decode.op[0].has_ea) {
            s->prefix |= PREFIX_LOCK;
        }
        break;

    case X86_SPECIAL_ProtMode:
        if (!PE(s) || VM86(s)) {
            goto illegal_op;
        }
        break;

    case X86_SPECIAL_i64:
        if (CODE64(s)) {
            goto illegal_op;
        }
        break;
    case X86_SPECIAL_o64:
        if (!CODE64(s)) {
            goto illegal_op;
        }
        break;

    case X86_SPECIAL_ZExtOp0:
        assert(decode.op[0].unit == X86_OP_INT);
        if (!decode.op[0].has_ea) {
            decode.op[0].ot = MO_32;
        }
        break;

    case X86_SPECIAL_ZExtOp2:
        assert(decode.op[2].unit == X86_OP_INT);
        if (!decode.op[2].has_ea) {
            decode.op[2].ot = MO_32;
        }
        break;

    case X86_SPECIAL_AVXExtMov:
        if (!decode.op[2].has_ea) {
            decode.op[2].ot = s->vex_l ? MO_256 : MO_128;
        } else if (s->vex_l) {
            decode.op[2].ot++;
        }
        break;

    case X86_SPECIAL_MMX:
        if (!(s->prefix & (PREFIX_REPZ | PREFIX_REPNZ | PREFIX_DATA))) {
            gen_helper_enter_mmx(cpu_env);
        }
        break;
    }

    if (!validate_vex(s, &decode)) {
        return;
    }
    if (decode.op[0].has_ea || decode.op[1].has_ea || decode.op[2].has_ea) {
        gen_load_ea(s, &decode.mem, decode.e.vex_class == 12);
    }
    if (s->prefix & PREFIX_LOCK) {
        if (decode.op[0].unit != X86_OP_INT || !decode.op[0].has_ea) {
            goto illegal_op;
        }
        gen_load(s, &decode, 2, s->T1);
        decode.e.gen(s, env, &decode);
    } else {
        if (decode.op[0].unit == X86_OP_MMX) {
            compute_mmx_offset(&decode.op[0]);
        } else if (decode.op[0].unit == X86_OP_SSE) {
            compute_xmm_offset(&decode.op[0]);
        }
        gen_load(s, &decode, 1, s->T0);
        gen_load(s, &decode, 2, s->T1);
        decode.e.gen(s, env, &decode);
        gen_writeback(s, &decode, 0, s->T0);
    }
    return;
 illegal_op:
    gen_illegal_opcode(s);
    return;
 unknown_op:
    gen_unknown_opcode(env, s);
}
