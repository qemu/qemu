/*
 *  PowerPC emulation for qemu: main translation routines.
 * 
 *  Copyright (c) 2003-2005 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

//#define DO_SINGLE_STEP
//#define PPC_DEBUG_DISAS

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

#include "gen-op.h"

#define GEN8(func, NAME) \
static GenOpFunc *NAME ## _table [8] = {                                      \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}

#define GEN16(func, NAME)                                                     \
static GenOpFunc *NAME ## _table [16] = {                                     \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,                                 \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}

#define GEN32(func, NAME) \
static GenOpFunc *NAME ## _table [32] = {                                     \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,                                 \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,                               \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,                               \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,                               \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,                               \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}

/* Condition register moves */
GEN8(gen_op_load_crf_T0, gen_op_load_crf_T0_crf);
GEN8(gen_op_load_crf_T1, gen_op_load_crf_T1_crf);
GEN8(gen_op_store_T0_crf, gen_op_store_T0_crf_crf);
GEN8(gen_op_store_T1_crf, gen_op_store_T1_crf_crf);

/* Floating point condition and status register moves */
GEN8(gen_op_load_fpscr_T0, gen_op_load_fpscr_T0_fpscr);
GEN8(gen_op_store_T0_fpscr, gen_op_store_T0_fpscr_fpscr);
GEN8(gen_op_clear_fpscr, gen_op_clear_fpscr_fpscr);
static GenOpFunc1 *gen_op_store_T0_fpscri_fpscr_table[8] = {
    &gen_op_store_T0_fpscri_fpscr0,
    &gen_op_store_T0_fpscri_fpscr1,
    &gen_op_store_T0_fpscri_fpscr2,
    &gen_op_store_T0_fpscri_fpscr3,
    &gen_op_store_T0_fpscri_fpscr4,
    &gen_op_store_T0_fpscri_fpscr5,
    &gen_op_store_T0_fpscri_fpscr6,
    &gen_op_store_T0_fpscri_fpscr7,
};
static inline void gen_op_store_T0_fpscri(int n, uint8_t param)
{
    (*gen_op_store_T0_fpscri_fpscr_table[n])(param);
}

/* Segment register moves */
GEN16(gen_op_load_sr, gen_op_load_sr);
GEN16(gen_op_store_sr, gen_op_store_sr);

/* General purpose registers moves */
GEN32(gen_op_load_gpr_T0, gen_op_load_gpr_T0_gpr);
GEN32(gen_op_load_gpr_T1, gen_op_load_gpr_T1_gpr);
GEN32(gen_op_load_gpr_T2, gen_op_load_gpr_T2_gpr);

GEN32(gen_op_store_T0_gpr, gen_op_store_T0_gpr_gpr);
GEN32(gen_op_store_T1_gpr, gen_op_store_T1_gpr_gpr);
GEN32(gen_op_store_T2_gpr, gen_op_store_T2_gpr_gpr);

/* floating point registers moves */
GEN32(gen_op_load_fpr_FT0, gen_op_load_fpr_FT0_fpr);
GEN32(gen_op_load_fpr_FT1, gen_op_load_fpr_FT1_fpr);
GEN32(gen_op_load_fpr_FT2, gen_op_load_fpr_FT2_fpr);
GEN32(gen_op_store_FT0_fpr, gen_op_store_FT0_fpr_fpr);
GEN32(gen_op_store_FT1_fpr, gen_op_store_FT1_fpr_fpr);
GEN32(gen_op_store_FT2_fpr, gen_op_store_FT2_fpr_fpr);

static uint8_t  spr_access[1024 / 2];

/* internal defines */
typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong nip;
    uint32_t opcode;
    uint32_t exception;
    /* Routine used to access memory */
    int mem_idx;
    /* Translation flags */
#if !defined(CONFIG_USER_ONLY)
    int supervisor;
#endif
    int fpu_enabled;
    ppc_spr_t *spr_cb; /* Needed to check rights for mfspr/mtspr */
    int singlestep_enabled;
} DisasContext;

struct opc_handler_t {
    /* invalid bits */
    uint32_t inval;
    /* instruction type */
    uint32_t type;
    /* handler */
    void (*handler)(DisasContext *ctx);
};

#define RET_EXCP(ctx, excp, error)                                            \
do {                                                                          \
    if ((ctx)->exception == EXCP_NONE) {                                      \
        gen_op_update_nip((ctx)->nip);                                        \
    }                                                                         \
    gen_op_raise_exception_err((excp), (error));                              \
    ctx->exception = (excp);                                                  \
} while (0)

#define RET_INVAL(ctx)                                                        \
RET_EXCP((ctx), EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_INVAL)

#define RET_PRIVOPC(ctx)                                                      \
RET_EXCP((ctx), EXCP_PROGRAM, EXCP_INVAL | EXCP_PRIV_OPC)

#define RET_PRIVREG(ctx)                                                      \
RET_EXCP((ctx), EXCP_PROGRAM, EXCP_INVAL | EXCP_PRIV_REG)

/* Stop translation */
static inline void RET_STOP (DisasContext *ctx)
{
    gen_op_update_nip((ctx)->nip);
    ctx->exception = EXCP_MTMSR;
}

/* No need to update nip here, as execution flow will change */
static inline void RET_CHG_FLOW (DisasContext *ctx)
{
    ctx->exception = EXCP_MTMSR;
}

#define GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                      \
static void gen_##name (DisasContext *ctx);                                   \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type);                              \
static void gen_##name (DisasContext *ctx)

typedef struct opcode_t {
    unsigned char opc1, opc2, opc3;
#if HOST_LONG_BITS == 64 /* Explicitely align to 64 bits */
    unsigned char pad[5];
#else
    unsigned char pad[1];
#endif
    opc_handler_t handler;
    const unsigned char *oname;
} opcode_t;

/***                           Instruction decoding                        ***/
#define EXTRACT_HELPER(name, shift, nb)                                       \
static inline uint32_t name (uint32_t opcode)                                 \
{                                                                             \
    return (opcode >> (shift)) & ((1 << (nb)) - 1);                           \
}

#define EXTRACT_SHELPER(name, shift, nb)                                      \
static inline int32_t name (uint32_t opcode)                                  \
{                                                                             \
    return (int16_t)((opcode >> (shift)) & ((1 << (nb)) - 1));                \
}

/* Opcode part 1 */
EXTRACT_HELPER(opc1, 26, 6);
/* Opcode part 2 */
EXTRACT_HELPER(opc2, 1, 5);
/* Opcode part 3 */
EXTRACT_HELPER(opc3, 6, 5);
/* Update Cr0 flags */
EXTRACT_HELPER(Rc, 0, 1);
/* Destination */
EXTRACT_HELPER(rD, 21, 5);
/* Source */
EXTRACT_HELPER(rS, 21, 5);
/* First operand */
EXTRACT_HELPER(rA, 16, 5);
/* Second operand */
EXTRACT_HELPER(rB, 11, 5);
/* Third operand */
EXTRACT_HELPER(rC, 6, 5);
/***                               Get CRn                                 ***/
EXTRACT_HELPER(crfD, 23, 3);
EXTRACT_HELPER(crfS, 18, 3);
EXTRACT_HELPER(crbD, 21, 5);
EXTRACT_HELPER(crbA, 16, 5);
EXTRACT_HELPER(crbB, 11, 5);
/* SPR / TBL */
EXTRACT_HELPER(_SPR, 11, 10);
static inline uint32_t SPR (uint32_t opcode)
{
    uint32_t sprn = _SPR(opcode);

    return ((sprn >> 5) & 0x1F) | ((sprn & 0x1F) << 5);
}
/***                              Get constants                            ***/
EXTRACT_HELPER(IMM, 12, 8);
/* 16 bits signed immediate value */
EXTRACT_SHELPER(SIMM, 0, 16);
/* 16 bits unsigned immediate value */
EXTRACT_HELPER(UIMM, 0, 16);
/* Bit count */
EXTRACT_HELPER(NB, 11, 5);
/* Shift count */
EXTRACT_HELPER(SH, 11, 5);
/* Mask start */
EXTRACT_HELPER(MB, 6, 5);
/* Mask end */
EXTRACT_HELPER(ME, 1, 5);
/* Trap operand */
EXTRACT_HELPER(TO, 21, 5);

EXTRACT_HELPER(CRM, 12, 8);
EXTRACT_HELPER(FM, 17, 8);
EXTRACT_HELPER(SR, 16, 4);
EXTRACT_HELPER(FPIMM, 20, 4);

/***                            Jump target decoding                       ***/
/* Displacement */
EXTRACT_SHELPER(d, 0, 16);
/* Immediate address */
static inline uint32_t LI (uint32_t opcode)
{
    return (opcode >> 0) & 0x03FFFFFC;
}

static inline uint32_t BD (uint32_t opcode)
{
    return (opcode >> 0) & 0xFFFC;
}

EXTRACT_HELPER(BO, 21, 5);
EXTRACT_HELPER(BI, 16, 5);
/* Absolute/relative address */
EXTRACT_HELPER(AA, 1, 1);
/* Link */
EXTRACT_HELPER(LK, 0, 1);

/* Create a mask between <start> and <end> bits */
static inline uint32_t MASK (uint32_t start, uint32_t end)
{
    uint32_t ret;

    ret = (((uint32_t)(-1)) >> (start)) ^ (((uint32_t)(-1) >> (end)) >> 1);
    if (start > end)
        return ~ret;

    return ret;
}

#if HOST_LONG_BITS == 64
#define OPC_ALIGN 8
#else
#define OPC_ALIGN 4
#endif
#if defined(__APPLE__)
#define OPCODES_SECTION \
    __attribute__ ((section("__TEXT,__opcodes"), unused, aligned (OPC_ALIGN) ))
#else
#define OPCODES_SECTION \
    __attribute__ ((section(".opcodes"), unused, aligned (OPC_ALIGN) ))
#endif

#define GEN_OPCODE(name, op1, op2, op3, invl, _typ)                           \
OPCODES_SECTION opcode_t opc_##name = {                                       \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval   = invl,                                                      \
        .type = _typ,                                                         \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = stringify(name),                                                 \
}

#define GEN_OPCODE_MARK(name)                                                 \
OPCODES_SECTION opcode_t opc_##name = {                                       \
    .opc1 = 0xFF,                                                             \
    .opc2 = 0xFF,                                                             \
    .opc3 = 0xFF,                                                             \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval   = 0x00000000,                                                \
        .type = 0x00,                                                         \
        .handler = NULL,                                                      \
    },                                                                        \
    .oname = stringify(name),                                                 \
}

/* Start opcode list */
GEN_OPCODE_MARK(start);

/* Invalid instruction */
GEN_HANDLER(invalid, 0x00, 0x00, 0x00, 0xFFFFFFFF, PPC_NONE)
{
    RET_INVAL(ctx);
}

static opc_handler_t invalid_handler = {
    .inval   = 0xFFFFFFFF,
    .type    = PPC_NONE,
    .handler = gen_invalid,
};

/***                           Integer arithmetic                          ***/
#define __GEN_INT_ARITH2(name, opc1, opc2, opc3, inval)                       \
GEN_HANDLER(name, opc1, opc2, opc3, inval, PPC_INTEGER)                       \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
}

#define __GEN_INT_ARITH2_O(name, opc1, opc2, opc3, inval)                     \
GEN_HANDLER(name, opc1, opc2, opc3, inval, PPC_INTEGER)                       \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
}

#define __GEN_INT_ARITH1(name, opc1, opc2, opc3)                              \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, PPC_INTEGER)                  \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
}
#define __GEN_INT_ARITH1_O(name, opc1, opc2, opc3)                            \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, PPC_INTEGER)                  \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
}

/* Two operands arithmetic functions */
#define GEN_INT_ARITH2(name, opc1, opc2, opc3)                                \
__GEN_INT_ARITH2(name, opc1, opc2, opc3, 0x00000000)                          \
__GEN_INT_ARITH2_O(name##o, opc1, opc2, opc3 | 0x10, 0x00000000)

/* Two operands arithmetic functions with no overflow allowed */
#define GEN_INT_ARITHN(name, opc1, opc2, opc3)                                \
__GEN_INT_ARITH2(name, opc1, opc2, opc3, 0x00000400)

/* One operand arithmetic functions */
#define GEN_INT_ARITH1(name, opc1, opc2, opc3)                                \
__GEN_INT_ARITH1(name, opc1, opc2, opc3)                                      \
__GEN_INT_ARITH1_O(name##o, opc1, opc2, opc3 | 0x10)

/* add    add.    addo    addo.    */
GEN_INT_ARITH2 (add,    0x1F, 0x0A, 0x08);
/* addc   addc.   addco   addco.   */
GEN_INT_ARITH2 (addc,   0x1F, 0x0A, 0x00);
/* adde   adde.   addeo   addeo.   */
GEN_INT_ARITH2 (adde,   0x1F, 0x0A, 0x04);
/* addme  addme.  addmeo  addmeo.  */
GEN_INT_ARITH1 (addme,  0x1F, 0x0A, 0x07);
/* addze  addze.  addzeo  addzeo.  */
GEN_INT_ARITH1 (addze,  0x1F, 0x0A, 0x06);
/* divw   divw.   divwo   divwo.   */
GEN_INT_ARITH2 (divw,   0x1F, 0x0B, 0x0F);
/* divwu  divwu.  divwuo  divwuo.  */
GEN_INT_ARITH2 (divwu,  0x1F, 0x0B, 0x0E);
/* mulhw  mulhw.                   */
GEN_INT_ARITHN (mulhw,  0x1F, 0x0B, 0x02);
/* mulhwu mulhwu.                  */
GEN_INT_ARITHN (mulhwu, 0x1F, 0x0B, 0x00);
/* mullw  mullw.  mullwo  mullwo.  */
GEN_INT_ARITH2 (mullw,  0x1F, 0x0B, 0x07);
/* neg    neg.    nego    nego.    */
GEN_INT_ARITH1 (neg,    0x1F, 0x08, 0x03);
/* subf   subf.   subfo   subfo.   */
GEN_INT_ARITH2 (subf,   0x1F, 0x08, 0x01);
/* subfc  subfc.  subfco  subfco.  */
GEN_INT_ARITH2 (subfc,  0x1F, 0x08, 0x00);
/* subfe  subfe.  subfeo  subfeo.  */
GEN_INT_ARITH2 (subfe,  0x1F, 0x08, 0x04);
/* subfme subfme. subfmeo subfmeo. */
GEN_INT_ARITH1 (subfme, 0x1F, 0x08, 0x07);
/* subfze subfze. subfzeo subfzeo. */
GEN_INT_ARITH1 (subfze, 0x1F, 0x08, 0x06);
/* addi */
GEN_HANDLER(addi, 0x0E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    int32_t simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(simm);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_addi(simm);
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* addic */
GEN_HANDLER(addic, 0x0C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_addic(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* addic. */
GEN_HANDLER(addic_, 0x0D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_addic(SIMM(ctx->opcode));
    gen_op_set_Rc0();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* addis */
GEN_HANDLER(addis, 0x0F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    int32_t simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(simm << 16);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_addi(simm << 16);
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* mulli */
GEN_HANDLER(mulli, 0x07, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_mulli(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* subfic */
GEN_HANDLER(subfic, 0x08, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_subfic(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/***                           Integer comparison                          ***/
#define GEN_CMP(name, opc)                                                    \
GEN_HANDLER(name, 0x1F, 0x00, opc, 0x00400000, PPC_INTEGER)                   \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_crf(crfD(ctx->opcode));                                   \
}

/* cmp */
GEN_CMP(cmp, 0x00);
/* cmpi */
GEN_HANDLER(cmpi, 0x0B, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_cmpi(SIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}
/* cmpl */
GEN_CMP(cmpl, 0x01);
/* cmpli */
GEN_HANDLER(cmpli, 0x0A, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_cmpli(UIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/***                            Integer logical                            ***/
#define __GEN_LOGICAL2(name, opc2, opc3)                                      \
GEN_HANDLER(name, 0x1F, opc2, opc3, 0x00000000, PPC_INTEGER)                  \
{                                                                             \
    gen_op_load_gpr_T0(rS(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}
#define GEN_LOGICAL2(name, opc)                                               \
__GEN_LOGICAL2(name, 0x1C, opc)

#define GEN_LOGICAL1(name, opc)                                               \
GEN_HANDLER(name, 0x1F, 0x1A, opc, 0x00000000, PPC_INTEGER)                   \
{                                                                             \
    gen_op_load_gpr_T0(rS(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

/* and & and. */
GEN_LOGICAL2(and, 0x00);
/* andc & andc. */
GEN_LOGICAL2(andc, 0x01);
/* andi. */
GEN_HANDLER(andi_, 0x1C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_andi_(UIMM(ctx->opcode));
    gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* andis. */
GEN_HANDLER(andis_, 0x1D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_andi_(UIMM(ctx->opcode) << 16);
    gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/* cntlzw */
GEN_LOGICAL1(cntlzw, 0x00);
/* eqv & eqv. */
GEN_LOGICAL2(eqv, 0x08);
/* extsb & extsb. */
GEN_LOGICAL1(extsb, 0x1D);
/* extsh & extsh. */
GEN_LOGICAL1(extsh, 0x1C);
/* nand & nand. */
GEN_LOGICAL2(nand, 0x0E);
/* nor & nor. */
GEN_LOGICAL2(nor, 0x03);

/* or & or. */
GEN_HANDLER(or, 0x1F, 0x1C, 0x0D, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    /* Optimisation for mr case */
    if (rS(ctx->opcode) != rB(ctx->opcode)) {
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_or();
    }
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/* orc & orc. */
GEN_LOGICAL2(orc, 0x0C);
/* xor & xor. */
GEN_HANDLER(xor, 0x1F, 0x1C, 0x09, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    /* Optimisation for "set to zero" case */
    if (rS(ctx->opcode) != rB(ctx->opcode)) {
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_xor();
    } else {
        gen_op_set_T0(0);
    }
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* ori */
GEN_HANDLER(ori, 0x18, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
        }
        gen_op_load_gpr_T0(rS(ctx->opcode));
    if (uimm != 0)
        gen_op_ori(uimm);
        gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* oris */
GEN_HANDLER(oris, 0x19, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
        }
        gen_op_load_gpr_T0(rS(ctx->opcode));
    if (uimm != 0)
        gen_op_ori(uimm << 16);
        gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* xori */
GEN_HANDLER(xori, 0x1A, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (uimm != 0)
    gen_op_xori(uimm);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/* xoris */
GEN_HANDLER(xoris, 0x1B, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (uimm != 0)
    gen_op_xori(uimm << 16);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/***                             Integer rotate                            ***/
/* rlwimi & rlwimi. */
GEN_HANDLER(rlwimi, 0x14, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rA(ctx->opcode));
    gen_op_rlwimi(SH(ctx->opcode), MASK(mb, me), ~MASK(mb, me));
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* rlwinm & rlwinm. */
GEN_HANDLER(rlwinm, 0x15, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me, sh;
    
    sh = SH(ctx->opcode);
    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
#if 1 // TRY
    if (sh == 0) {
        gen_op_andi_(MASK(mb, me));
        goto store;
    }
#endif
    if (mb == 0) {
        if (me == 31) {
            gen_op_rotlwi(sh);
            goto store;
#if 0
        } else if (me == (31 - sh)) {
            gen_op_slwi(sh);
            goto store;
#endif
        }
    } else if (me == 31) {
#if 0
        if (sh == (32 - mb)) {
            gen_op_srwi(mb);
            goto store;
        }
#endif
    }
    gen_op_rlwinm(sh, MASK(mb, me));
store:
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* rlwnm & rlwnm. */
GEN_HANDLER(rlwnm, 0x17, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    if (mb == 0 && me == 31) {
        gen_op_rotl();
    } else
    {
        gen_op_rlwnm(MASK(mb, me));
    }
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/***                             Integer shift                             ***/
/* slw & slw. */
__GEN_LOGICAL2(slw, 0x18, 0x00);
/* sraw & sraw. */
__GEN_LOGICAL2(sraw, 0x18, 0x18);
/* srawi & srawi. */
GEN_HANDLER(srawi, 0x1F, 0x18, 0x19, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (SH(ctx->opcode) != 0)
    gen_op_srawi(SH(ctx->opcode), MASK(32 - SH(ctx->opcode), 31));
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* srw & srw. */
__GEN_LOGICAL2(srw, 0x18, 0x10);

/***                       Floating-Point arithmetic                       ***/
#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat)                           \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x00000000, PPC_FLOAT)                   \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rC(ctx->opcode));                                     \
    gen_op_load_fpr_FT2(rB(ctx->opcode));                                     \
    gen_op_f##op();                                                           \
    if (isfloat) {                                                            \
        gen_op_frsp();                                                        \
    }                                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_ACB(name, op2)                                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0);                                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1);

#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat)                     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rB(ctx->opcode));                                     \
    gen_op_f##op();                                                           \
    if (isfloat) {                                                            \
        gen_op_frsp();                                                        \
    }                                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AB(name, op2, inval)                                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0);                               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1);

#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat)                     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rC(ctx->opcode));                                     \
    gen_op_f##op();                                                           \
    if (isfloat) {                                                            \
        gen_op_frsp();                                                        \
    }                                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AC(name, op2, inval)                                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0);                               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1);

#define GEN_FLOAT_B(name, op2, op3)                                           \
GEN_HANDLER(f##name, 0x3F, op2, op3, 0x001F0000, PPC_FLOAT)                   \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_BS(name, op1, op2)                                          \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x001F07C0, PPC_FLOAT)                   \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

/* fadd - fadds */
GEN_FLOAT_AB(add, 0x15, 0x000007C0);
/* fdiv - fdivs */
GEN_FLOAT_AB(div, 0x12, 0x000007C0);
/* fmul - fmuls */
GEN_FLOAT_AC(mul, 0x19, 0x0000F800);

/* fres */
GEN_FLOAT_BS(res, 0x3B, 0x18);

/* frsqrte */
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A);

/* fsel */
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0);
/* fsub - fsubs */
GEN_FLOAT_AB(sub, 0x14, 0x000007C0);
/* Optional: */
/* fsqrt */
GEN_HANDLER(fsqrt, 0x3F, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_OPT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_fsqrt();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_OPT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_fsqrt();
    gen_op_frsp();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd - fmadds */
GEN_FLOAT_ACB(madd, 0x1D);
/* fmsub - fmsubs */
GEN_FLOAT_ACB(msub, 0x1C);
/* fnmadd - fnmadds */
GEN_FLOAT_ACB(nmadd, 0x1F);
/* fnmsub - fnmsubs */
GEN_FLOAT_ACB(nmsub, 0x1E);

/***                     Floating-Point round & convert                    ***/
/* fctiw */
GEN_FLOAT_B(ctiw, 0x0E, 0x00);
/* fctiwz */
GEN_FLOAT_B(ctiwz, 0x0F, 0x00);
/* frsp */
GEN_FLOAT_B(rsp, 0x0C, 0x00);

/***                         Floating-Point compare                        ***/
/* fcmpo */
GEN_HANDLER(fcmpo, 0x3F, 0x00, 0x00, 0x00600001, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rA(ctx->opcode));
    gen_op_load_fpr_FT1(rB(ctx->opcode));
    gen_op_fcmpo();
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/* fcmpu */
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rA(ctx->opcode));
    gen_op_load_fpr_FT1(rB(ctx->opcode));
    gen_op_fcmpu();
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/***                         Floating-point move                           ***/
/* fabs */
GEN_FLOAT_B(abs, 0x08, 0x08);

/* fmr  - fmr. */
GEN_HANDLER(fmr, 0x3F, 0x08, 0x02, 0x001F0000, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* fnabs */
GEN_FLOAT_B(nabs, 0x08, 0x04);
/* fneg */
GEN_FLOAT_B(neg, 0x08, 0x01);

/***                  Floating-Point status & ctrl register                ***/
/* mcrfs */
GEN_HANDLER(mcrfs, 0x3F, 0x00, 0x02, 0x0063F801, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_load_fpscr_T0(crfS(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
    gen_op_clear_fpscr(crfS(ctx->opcode));
}

/* mffs */
GEN_HANDLER(mffs, 0x3F, 0x07, 0x12, 0x001FF800, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_load_fpscr();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsb0 */
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT)
{
    uint8_t crb;
    
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    crb = crbD(ctx->opcode) >> 2;
    gen_op_load_fpscr_T0(crb);
    gen_op_andi_(~(1 << (crbD(ctx->opcode) & 0x03)));
    gen_op_store_T0_fpscr(crb);
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsb1 */
GEN_HANDLER(mtfsb1, 0x3F, 0x06, 0x01, 0x001FF800, PPC_FLOAT)
{
    uint8_t crb;
    
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    crb = crbD(ctx->opcode) >> 2;
    gen_op_load_fpscr_T0(crb);
    gen_op_ori(1 << (crbD(ctx->opcode) & 0x03));
    gen_op_store_T0_fpscr(crb);
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsf */
GEN_HANDLER(mtfsf, 0x3F, 0x07, 0x16, 0x02010000, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_store_fpscr(FM(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsfi */
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006f0800, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_store_T0_fpscri(crbD(ctx->opcode) >> 2, FPIMM(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/***                             Integer load                              ***/
#define op_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_raw,                                                   \
    &gen_op_l##width##_le_raw,                                                \
};
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_raw,                                                  \
    &gen_op_st##width##_le_raw,                                               \
};
/* Byte access routine are endian safe */
#define gen_op_stb_le_raw gen_op_stb_raw
#define gen_op_lbz_le_raw gen_op_lbz_raw
#else
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_user,                                                  \
    &gen_op_l##width##_le_user,                                               \
    &gen_op_l##width##_kernel,                                                \
    &gen_op_l##width##_le_kernel,                                             \
};
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_user,                                                 \
    &gen_op_st##width##_le_user,                                              \
    &gen_op_st##width##_kernel,                                               \
    &gen_op_st##width##_le_kernel,                                            \
};
/* Byte access routine are endian safe */
#define gen_op_stb_le_user gen_op_stb_user
#define gen_op_lbz_le_user gen_op_lbz_user
#define gen_op_stb_le_kernel gen_op_stb_kernel
#define gen_op_lbz_le_kernel gen_op_lbz_kernel
#endif

#define GEN_LD(width, opc)                                                    \
GEN_HANDLER(l##width, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)               \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_set_T0(simm);                                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        if (simm != 0)                                                        \
            gen_op_addi(simm);                                                \
    }                                                                         \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
}

#define GEN_LDU(width, opc)                                                   \
GEN_HANDLER(l##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)            \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode)) {                                 \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (simm != 0)                                                            \
        gen_op_addi(simm);                                                    \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDUX(width, opc)                                                  \
GEN_HANDLER(l##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_INTEGER)           \
{                                                                             \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode)) {                                 \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_add();                                                             \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDX(width, opc2, opc3)                                            \
GEN_HANDLER(l##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_INTEGER)           \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_add();                                                         \
    }                                                                         \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
}

#define GEN_LDS(width, op)                                                    \
OP_LD_TABLE(width);                                                           \
GEN_LD(width, op | 0x20);                                                     \
GEN_LDU(width, op | 0x21);                                                    \
GEN_LDUX(width, op | 0x01);                                                   \
GEN_LDX(width, 0x17, op | 0x00)

/* lbz lbzu lbzux lbzx */
GEN_LDS(bz, 0x02);
/* lha lhau lhaux lhax */
GEN_LDS(ha, 0x0A);
/* lhz lhzu lhzux lhzx */
GEN_LDS(hz, 0x08);
/* lwz lwzu lwzux lwzx */
GEN_LDS(wz, 0x00);

/***                              Integer store                            ***/
#define GEN_ST(width, opc)                                                    \
GEN_HANDLER(st##width, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)              \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_set_T0(simm);                                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        if (simm != 0)                                                        \
            gen_op_addi(simm);                                                \
    }                                                                         \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
}

#define GEN_STU(width, opc)                                                   \
GEN_HANDLER(st##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)           \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (simm != 0)                                                            \
        gen_op_addi(simm);                                                    \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STUX(width, opc)                                                  \
GEN_HANDLER(st##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_INTEGER)          \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_add();                                                             \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STX(width, opc2, opc3)                                            \
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_INTEGER)          \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_add();                                                         \
    }                                                                         \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
}

#define GEN_STS(width, op)                                                    \
OP_ST_TABLE(width);                                                           \
GEN_ST(width, op | 0x20);                                                     \
GEN_STU(width, op | 0x21);                                                    \
GEN_STUX(width, op | 0x01);                                                   \
GEN_STX(width, 0x17, op | 0x00)

/* stb stbu stbux stbx */
GEN_STS(b, 0x06);
/* sth sthu sthux sthx */
GEN_STS(h, 0x0C);
/* stw stwu stwux stwx */
GEN_STS(w, 0x04);

/***                Integer load and store with byte reverse               ***/
/* lhbrx */
OP_LD_TABLE(hbr);
GEN_LDX(hbr, 0x16, 0x18);
/* lwbrx */
OP_LD_TABLE(wbr);
GEN_LDX(wbr, 0x16, 0x10);
/* sthbrx */
OP_ST_TABLE(hbr);
GEN_STX(hbr, 0x16, 0x1C);
/* stwbrx */
OP_ST_TABLE(wbr);
GEN_STX(wbr, 0x16, 0x14);

/***                    Integer load and store multiple                    ***/
#define op_ldstm(name, reg) (*gen_op_##name[ctx->mem_idx])(reg)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc1 *gen_op_lmw[] = {
    &gen_op_lmw_raw,
    &gen_op_lmw_le_raw,
};
static GenOpFunc1 *gen_op_stmw[] = {
    &gen_op_stmw_raw,
    &gen_op_stmw_le_raw,
};
#else
static GenOpFunc1 *gen_op_lmw[] = {
    &gen_op_lmw_user,
    &gen_op_lmw_le_user,
    &gen_op_lmw_kernel,
    &gen_op_lmw_le_kernel,
};
static GenOpFunc1 *gen_op_stmw[] = {
    &gen_op_stmw_user,
    &gen_op_stmw_le_user,
    &gen_op_stmw_kernel,
    &gen_op_stmw_le_kernel,
};
#endif

/* lmw */
GEN_HANDLER(lmw, 0x2E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    int simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(simm);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (simm != 0)
            gen_op_addi(simm);
    }
    op_ldstm(lmw, rD(ctx->opcode));
}

/* stmw */
GEN_HANDLER(stmw, 0x2F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    int simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(simm);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (simm != 0)
            gen_op_addi(simm);
    }
    op_ldstm(stmw, rS(ctx->opcode));
}

/***                    Integer load and store strings                     ***/
#define op_ldsts(name, start) (*gen_op_##name[ctx->mem_idx])(start)
#define op_ldstsx(name, rd, ra, rb) (*gen_op_##name[ctx->mem_idx])(rd, ra, rb)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc1 *gen_op_lswi[] = {
    &gen_op_lswi_raw,
    &gen_op_lswi_le_raw,
};
static GenOpFunc3 *gen_op_lswx[] = {
    &gen_op_lswx_raw,
    &gen_op_lswx_le_raw,
};
static GenOpFunc1 *gen_op_stsw[] = {
    &gen_op_stsw_raw,
    &gen_op_stsw_le_raw,
};
#else
static GenOpFunc1 *gen_op_lswi[] = {
    &gen_op_lswi_user,
    &gen_op_lswi_le_user,
    &gen_op_lswi_kernel,
    &gen_op_lswi_le_kernel,
};
static GenOpFunc3 *gen_op_lswx[] = {
    &gen_op_lswx_user,
    &gen_op_lswx_le_user,
    &gen_op_lswx_kernel,
    &gen_op_lswx_le_kernel,
};
static GenOpFunc1 *gen_op_stsw[] = {
    &gen_op_stsw_user,
    &gen_op_stsw_le_user,
    &gen_op_stsw_kernel,
    &gen_op_stsw_le_kernel,
};
#endif

/* lswi */
/* PowerPC32 specification says we must generate an exception if
 * rA is in the range of registers to be loaded.
 * In an other hand, IBM says this is valid, but rA won't be loaded.
 * For now, I'll follow the spec...
 */
GEN_HANDLER(lswi, 0x1F, 0x15, 0x12, 0x00000001, PPC_INTEGER)
{
    int nb = NB(ctx->opcode);
    int start = rD(ctx->opcode);
    int ra = rA(ctx->opcode);
    int nr;

    if (nb == 0)
        nb = 32;
    nr = nb / 4;
    if (((start + nr) > 32  && start <= ra && (start + nr - 32) > ra) ||
        ((start + nr) <= 32 && start <= ra && (start + nr) > ra)) {
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        return;
    }
    if (ra == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(ra);
    }
    gen_op_set_T1(nb);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_op_update_nip((ctx)->nip - 4); 
    op_ldsts(lswi, start);
}

/* lswx */
GEN_HANDLER(lswx, 0x1F, 0x15, 0x10, 0x00000001, PPC_INTEGER)
{
    int ra = rA(ctx->opcode);
    int rb = rB(ctx->opcode);

    if (ra == 0) {
        gen_op_load_gpr_T0(rb);
        ra = rb;
    } else {
        gen_op_load_gpr_T0(ra);
        gen_op_load_gpr_T1(rb);
        gen_op_add();
    }
    gen_op_load_xer_bc();
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_op_update_nip((ctx)->nip - 4); 
    op_ldstsx(lswx, rD(ctx->opcode), ra, rb);
}

/* stswi */
GEN_HANDLER(stswi, 0x1F, 0x15, 0x16, 0x00000001, PPC_INTEGER)
{
    int nb = NB(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
    if (nb == 0)
        nb = 32;
    gen_op_set_T1(nb);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_op_update_nip((ctx)->nip - 4); 
    op_ldsts(stsw, rS(ctx->opcode));
}

/* stswx */
GEN_HANDLER(stswx, 0x1F, 0x15, 0x14, 0x00000001, PPC_INTEGER)
{
    int ra = rA(ctx->opcode);

    if (ra == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
        ra = rB(ctx->opcode);
    } else {
        gen_op_load_gpr_T0(ra);
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    gen_op_load_xer_bc();
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_op_update_nip((ctx)->nip - 4); 
    op_ldsts(stsw, rS(ctx->opcode));
}

/***                        Memory synchronisation                         ***/
/* eieio */
GEN_HANDLER(eieio, 0x1F, 0x16, 0x1A, 0x03FF0801, PPC_MEM)
{
}

/* isync */
GEN_HANDLER(isync, 0x13, 0x16, 0xFF, 0x03FF0801, PPC_MEM)
{
}

#define op_lwarx() (*gen_op_lwarx[ctx->mem_idx])()
#define op_stwcx() (*gen_op_stwcx[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_lwarx[] = {
    &gen_op_lwarx_raw,
    &gen_op_lwarx_le_raw,
};
static GenOpFunc *gen_op_stwcx[] = {
    &gen_op_stwcx_raw,
    &gen_op_stwcx_le_raw,
};
#else
static GenOpFunc *gen_op_lwarx[] = {
    &gen_op_lwarx_user,
    &gen_op_lwarx_le_user,
    &gen_op_lwarx_kernel,
    &gen_op_lwarx_le_kernel,
};
static GenOpFunc *gen_op_stwcx[] = {
    &gen_op_stwcx_user,
    &gen_op_stwcx_le_user,
    &gen_op_stwcx_kernel,
    &gen_op_stwcx_le_kernel,
};
#endif

/* lwarx */
GEN_HANDLER(lwarx, 0x1F, 0x14, 0xFF, 0x00000001, PPC_RES)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_lwarx();
    gen_op_store_T1_gpr(rD(ctx->opcode));
}

/* stwcx. */
GEN_HANDLER(stwcx_, 0x1F, 0x16, 0x04, 0x00000000, PPC_RES)
{
        if (rA(ctx->opcode) == 0) {
            gen_op_load_gpr_T0(rB(ctx->opcode));
        } else {
            gen_op_load_gpr_T0(rA(ctx->opcode));
            gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
        }
    gen_op_load_gpr_T1(rS(ctx->opcode));
    op_stwcx();
}

/* sync */
GEN_HANDLER(sync, 0x1F, 0x16, 0x12, 0x03FF0801, PPC_MEM)
{
}

/***                         Floating-point load                           ***/
#define GEN_LDF(width, opc)                                                   \
GEN_HANDLER(l##width, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)                 \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_set_T0(simm);                                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        if (simm != 0)                                                        \
            gen_op_addi(simm);                                                \
    }                                                                         \
    op_ldst(l##width);                                                        \
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
}

#define GEN_LDUF(width, opc)                                                  \
GEN_HANDLER(l##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)              \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode)) {                                 \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (simm != 0)                                                            \
        gen_op_addi(simm);                                                    \
    op_ldst(l##width);                                                        \
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDUXF(width, opc)                                                 \
GEN_HANDLER(l##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)             \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode)) {                                 \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_add();                                                             \
    op_ldst(l##width);                                                        \
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDXF(width, opc2, opc3)                                           \
GEN_HANDLER(l##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_FLOAT)             \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_add();                                                         \
    }                                                                         \
    op_ldst(l##width);                                                        \
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
}

#define GEN_LDFS(width, op)                                                   \
OP_LD_TABLE(width);                                                           \
GEN_LDF(width, op | 0x20);                                                    \
GEN_LDUF(width, op | 0x21);                                                   \
GEN_LDUXF(width, op | 0x01);                                                  \
GEN_LDXF(width, 0x17, op | 0x00)

/* lfd lfdu lfdux lfdx */
GEN_LDFS(fd, 0x12);
/* lfs lfsu lfsux lfsx */
GEN_LDFS(fs, 0x10);

/***                         Floating-point store                          ***/
#define GEN_STF(width, opc)                                                   \
GEN_HANDLER(st##width, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)                \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_set_T0(simm);                                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        if (simm != 0)                                                        \
            gen_op_addi(simm);                                                \
    }                                                                         \
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
}

#define GEN_STUF(width, opc)                                                  \
GEN_HANDLER(st##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)             \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (simm != 0)                                                            \
        gen_op_addi(simm);                                                    \
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STUXF(width, opc)                                                 \
GEN_HANDLER(st##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_add();                                                             \
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STXF(width, opc2, opc3)                                           \
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    if (!ctx->fpu_enabled) {                                                  \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_add();                                                         \
    }                                                                         \
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
}

#define GEN_STFS(width, op)                                                   \
OP_ST_TABLE(width);                                                           \
GEN_STF(width, op | 0x20);                                                    \
GEN_STUF(width, op | 0x21);                                                   \
GEN_STUXF(width, op | 0x01);                                                  \
GEN_STXF(width, 0x17, op | 0x00)

/* stfd stfdu stfdux stfdx */
GEN_STFS(fd, 0x16);
/* stfs stfsu stfsux stfsx */
GEN_STFS(fs, 0x14);

/* Optional: */
/* stfiwx */
GEN_HANDLER(stfiwx, 0x1F, 0x17, 0x1E, 0x00000001, PPC_FLOAT)
{
    if (!ctx->fpu_enabled) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    RET_INVAL(ctx);
}

/***                                Branch                                 ***/

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        if (n == 0)
            gen_op_goto_tb0(TBPARAM(tb));
        else
            gen_op_goto_tb1(TBPARAM(tb));
        gen_op_set_T1(dest);
        gen_op_b_T1();
        gen_op_set_T0((long)tb + n);
        if (ctx->singlestep_enabled)
            gen_op_debug();
        gen_op_exit_tb();
    } else {
        gen_op_set_T1(dest);
        gen_op_b_T1();
        if (ctx->singlestep_enabled)
            gen_op_debug();
        gen_op_set_T0(0);
        gen_op_exit_tb();
    }
}

/* b ba bl bla */
GEN_HANDLER(b, 0x12, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    uint32_t li, target;

    /* sign extend LI */
    li = ((int32_t)LI(ctx->opcode) << 6) >> 6;

    if (AA(ctx->opcode) == 0)
        target = ctx->nip + li - 4;
    else
        target = li;
    if (LK(ctx->opcode)) {
        gen_op_setlr(ctx->nip);
    }
    gen_goto_tb(ctx, 0, target);
    ctx->exception = EXCP_BRANCH;
}

#define BCOND_IM  0
#define BCOND_LR  1
#define BCOND_CTR 2

static inline void gen_bcond(DisasContext *ctx, int type) 
{                                                                             
    uint32_t target = 0;
    uint32_t bo = BO(ctx->opcode);                                            
    uint32_t bi = BI(ctx->opcode);                                            
    uint32_t mask;                                                            
    uint32_t li;

    if ((bo & 0x4) == 0)
        gen_op_dec_ctr();                                                     
    switch(type) {
    case BCOND_IM:
        li = (int32_t)((int16_t)(BD(ctx->opcode)));
        if (AA(ctx->opcode) == 0) {
            target = ctx->nip + li - 4;
        } else {
            target = li;
        }
        break;
    case BCOND_CTR:
        gen_op_movl_T1_ctr();
        break;
    default:
    case BCOND_LR:
        gen_op_movl_T1_lr();
        break;
    }
    if (LK(ctx->opcode)) {                                        
        gen_op_setlr(ctx->nip);
    }
    if (bo & 0x10) {
        /* No CR condition */                                                 
        switch (bo & 0x6) {                                                   
        case 0:                                                               
            gen_op_test_ctr();
            break;
        case 2:                                                               
            gen_op_test_ctrz();
            break;                                                            
        default:
        case 4:                                                               
        case 6:                                                               
            if (type == BCOND_IM) {
                gen_goto_tb(ctx, 0, target);
            } else {
                gen_op_b_T1();
            }
            goto no_test;
        }
    } else {                                                                  
        mask = 1 << (3 - (bi & 0x03));                                        
        gen_op_load_crf_T0(bi >> 2);                                          
        if (bo & 0x8) {                                                       
            switch (bo & 0x6) {                                               
            case 0:                                                           
                gen_op_test_ctr_true(mask);
                break;                                                        
            case 2:                                                           
                gen_op_test_ctrz_true(mask);
                break;                                                        
            default:                                                          
            case 4:                                                           
            case 6:                                                           
                gen_op_test_true(mask);
                break;                                                        
            }                                                                 
        } else {                                                              
            switch (bo & 0x6) {                                               
            case 0:                                                           
                gen_op_test_ctr_false(mask);
                break;                                                        
            case 2:                                                           
                gen_op_test_ctrz_false(mask);
                break;                                                        
            default:
            case 4:                                                           
            case 6:                                                           
                gen_op_test_false(mask);
                break;                                                        
            }                                                                 
        }                                                                     
    }                                                                         
    if (type == BCOND_IM) {
        int l1 = gen_new_label();
        gen_op_jz_T0(l1);
        gen_goto_tb(ctx, 0, target);
        gen_set_label(l1);
        gen_goto_tb(ctx, 1, ctx->nip);
    } else {
        gen_op_btest_T1(ctx->nip);
    }
 no_test:
    ctx->exception = EXCP_BRANCH;                                             
}

GEN_HANDLER(bc, 0x10, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{                                                                             
    gen_bcond(ctx, BCOND_IM);
}

GEN_HANDLER(bcctr, 0x13, 0x10, 0x10, 0x00000000, PPC_FLOW)
{                                                                             
    gen_bcond(ctx, BCOND_CTR);
}

GEN_HANDLER(bclr, 0x13, 0x10, 0x00, 0x00000000, PPC_FLOW)
{                                                                             
    gen_bcond(ctx, BCOND_LR);
}

/***                      Condition register logical                       ***/
#define GEN_CRLOGIC(op, opc)                                                  \
GEN_HANDLER(cr##op, 0x13, 0x01, opc, 0x00000001, PPC_INTEGER)                 \
{                                                                             \
    gen_op_load_crf_T0(crbA(ctx->opcode) >> 2);                               \
    gen_op_getbit_T0(3 - (crbA(ctx->opcode) & 0x03));                         \
    gen_op_load_crf_T1(crbB(ctx->opcode) >> 2);                               \
    gen_op_getbit_T1(3 - (crbB(ctx->opcode) & 0x03));                         \
    gen_op_##op();                                                            \
    gen_op_load_crf_T1(crbD(ctx->opcode) >> 2);                               \
    gen_op_setcrfbit(~(1 << (3 - (crbD(ctx->opcode) & 0x03))),                \
                     3 - (crbD(ctx->opcode) & 0x03));                         \
    gen_op_store_T1_crf(crbD(ctx->opcode) >> 2);                              \
}

/* crand */
GEN_CRLOGIC(and, 0x08)
/* crandc */
GEN_CRLOGIC(andc, 0x04)
/* creqv */
GEN_CRLOGIC(eqv, 0x09)
/* crnand */
GEN_CRLOGIC(nand, 0x07)
/* crnor */
GEN_CRLOGIC(nor, 0x01)
/* cror */
GEN_CRLOGIC(or, 0x0E)
/* crorc */
GEN_CRLOGIC(orc, 0x0D)
/* crxor */
GEN_CRLOGIC(xor, 0x06)
/* mcrf */
GEN_HANDLER(mcrf, 0x13, 0x00, 0xFF, 0x00000001, PPC_INTEGER)
{
    gen_op_load_crf_T0(crfS(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/***                           System linkage                              ***/
/* rfi (supervisor only) */
GEN_HANDLER(rfi, 0x13, 0x12, 0xFF, 0x03FF8001, PPC_FLOW)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    /* Restore CPU state */
    if (!ctx->supervisor) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_rfi();
    RET_CHG_FLOW(ctx);
#endif
}

/* sc */
GEN_HANDLER(sc, 0x11, 0xFF, 0xFF, 0x03FFFFFD, PPC_FLOW)
{
#if defined(CONFIG_USER_ONLY)
    RET_EXCP(ctx, EXCP_SYSCALL_USER, 0);
#else
    RET_EXCP(ctx, EXCP_SYSCALL, 0);
#endif
}

/***                                Trap                                   ***/
/* tw */
GEN_HANDLER(tw, 0x1F, 0x04, 0xFF, 0x00000001, PPC_FLOW)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_op_update_nip(ctx->nip);
    gen_op_tw(TO(ctx->opcode));
}

/* twi */
GEN_HANDLER(twi, 0x03, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
#if 0
    printf("%s: param=0x%04x T0=0x%04x\n", __func__,
           SIMM(ctx->opcode), TO(ctx->opcode));
#endif
    gen_op_twi(SIMM(ctx->opcode), TO(ctx->opcode));
}

/***                          Processor control                            ***/
static inline int check_spr_access (int spr, int rw, int supervisor)
{
    uint32_t rights = spr_access[spr >> 1] >> (4 * (spr & 1));

#if 0
    if (spr != LR && spr != CTR) {
    if (loglevel > 0) {
        fprintf(logfile, "%s reg=%d s=%d rw=%d r=0x%02x 0x%02x\n", __func__,
                SPR_ENCODE(spr), supervisor, rw, rights,
                (rights >> ((2 * supervisor) + rw)) & 1);
    } else {
        printf("%s reg=%d s=%d rw=%d r=0x%02x 0x%02x\n", __func__,
               SPR_ENCODE(spr), supervisor, rw, rights,
               (rights >> ((2 * supervisor) + rw)) & 1);
    }
    }
#endif
    if (rights == 0)
        return -1;
    rights = rights >> (2 * supervisor);
    rights = rights >> rw;

    return rights & 1;
}

/* mcrxr */
GEN_HANDLER(mcrxr, 0x1F, 0x00, 0x10, 0x007FF801, PPC_MISC)
{
    gen_op_load_xer_cr();
    gen_op_store_T0_crf(crfD(ctx->opcode));
    gen_op_clear_xer_cr();
}

/* mfcr */
GEN_HANDLER(mfcr, 0x1F, 0x13, 0x00, 0x001FF801, PPC_MISC)
{
    gen_op_load_cr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* mfmsr */
GEN_HANDLER(mfmsr, 0x1F, 0x13, 0x02, 0x001FF801, PPC_MISC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_msr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

#if 0
#define SPR_NOACCESS ((void *)(-1))
#else
static void spr_noaccess (void *opaque, int sprn)
{
    sprn = ((sprn >> 5) & 0x1F) | ((sprn & 0x1F) << 5);
    printf("ERROR: try to access SPR %d !\n", sprn);
}
#define SPR_NOACCESS (&spr_noaccess)
#endif

/* mfspr */
static inline void gen_op_mfspr (DisasContext *ctx)
{
    void (*read_cb)(void *opaque, int sprn);
    uint32_t sprn = SPR(ctx->opcode);

#if !defined(CONFIG_USER_ONLY)
    if (ctx->supervisor)
        read_cb = ctx->spr_cb[sprn].oea_read;
    else
#endif
        read_cb = ctx->spr_cb[sprn].uea_read;
    if (read_cb != NULL) {
        if (read_cb != SPR_NOACCESS) {
            (*read_cb)(ctx, sprn);
            gen_op_store_T0_gpr(rD(ctx->opcode));
        } else {
            /* Privilege exception */
            if (loglevel) {
                fprintf(logfile, "Trying to read priviledged spr %d %03x\n",
                        sprn, sprn);
            }
            printf("Trying to read priviledged spr %d %03x\n", sprn, sprn);
        RET_PRIVREG(ctx);
        }
    } else {
        /* Not defined */
        if (loglevel) {
            fprintf(logfile, "Trying to read invalid spr %d %03x\n",
                    sprn, sprn);
        }
        printf("Trying to read invalid spr %d %03x\n", sprn, sprn);
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_SPR);
    }
}

GEN_HANDLER(mfspr, 0x1F, 0x13, 0x0A, 0x00000001, PPC_MISC)
{
    gen_op_mfspr(ctx);
    }

/* mftb */
GEN_HANDLER(mftb, 0x1F, 0x13, 0x0B, 0x00000001, PPC_TB)
{
    gen_op_mfspr(ctx);
}

/* mtcrf */
/* The mask should be 0x00100801, but Mac OS X 10.4 use an alternate form */
GEN_HANDLER(mtcrf, 0x1F, 0x10, 0x04, 0x00000801, PPC_MISC)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_cr(CRM(ctx->opcode));
}

/* mtmsr */
GEN_HANDLER(mtmsr, 0x1F, 0x12, 0x04, 0x001FF801, PPC_MISC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_update_nip((ctx)->nip);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_msr();
    /* Must stop the translation as machine state (may have) changed */
    RET_CHG_FLOW(ctx);
#endif
}

/* mtspr */
GEN_HANDLER(mtspr, 0x1F, 0x13, 0x0E, 0x00000001, PPC_MISC)
{
    void (*write_cb)(void *opaque, int sprn);
    uint32_t sprn = SPR(ctx->opcode);

#if !defined(CONFIG_USER_ONLY)
    if (ctx->supervisor)
        write_cb = ctx->spr_cb[sprn].oea_write;
    else
#endif
        write_cb = ctx->spr_cb[sprn].uea_write;
    if (write_cb != NULL) {
        if (write_cb != SPR_NOACCESS) {
            gen_op_load_gpr_T0(rS(ctx->opcode));
            (*write_cb)(ctx, sprn);
        } else {
            /* Privilege exception */
            if (loglevel) {
                fprintf(logfile, "Trying to write priviledged spr %d %03x\n",
                        sprn, sprn);
            }
            printf("Trying to write priviledged spr %d %03x\n", sprn, sprn);
        RET_PRIVREG(ctx);
    }
    } else {
        /* Not defined */
        if (loglevel) {
            fprintf(logfile, "Trying to write invalid spr %d %03x\n",
                    sprn, sprn);
        }
        printf("Trying to write invalid spr %d %03x\n", sprn, sprn);
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_SPR);
    }
}

/***                         Cache management                              ***/
/* For now, all those will be implemented as nop:
 * this is valid, regarding the PowerPC specs...
 * We just have to flush tb while invalidating instruction cache lines...
 */
/* dcbf */
GEN_HANDLER(dcbf, 0x1F, 0x16, 0x02, 0x03E00001, PPC_CACHE)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_ldst(lbz);
}

/* dcbi (Supervisor only) */
GEN_HANDLER(dcbi, 0x1F, 0x16, 0x0E, 0x03E00001, PPC_CACHE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVOPC(ctx);
        return;
    }
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_ldst(lbz);
    op_ldst(stb);
#endif
}

/* dcdst */
GEN_HANDLER(dcbst, 0x1F, 0x16, 0x01, 0x03E00001, PPC_CACHE)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_ldst(lbz);
}

/* dcbt */
GEN_HANDLER(dcbt, 0x1F, 0x16, 0x08, 0x03E00001, PPC_CACHE)
{
}

/* dcbtst */
GEN_HANDLER(dcbtst, 0x1F, 0x16, 0x07, 0x03E00001, PPC_CACHE)
{
}

/* dcbz */
#if defined(CONFIG_USER_ONLY)
#define op_dcbz() gen_op_dcbz_raw()
#else
#define op_dcbz() (*gen_op_dcbz[ctx->mem_idx])()
static GenOpFunc *gen_op_dcbz[] = {
    &gen_op_dcbz_user,
    &gen_op_dcbz_user,
    &gen_op_dcbz_kernel,
    &gen_op_dcbz_kernel,
};
#endif

GEN_HANDLER(dcbz, 0x1F, 0x16, 0x1F, 0x03E00001, PPC_CACHE)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_dcbz();
    gen_op_check_reservation();
}

/* icbi */
GEN_HANDLER(icbi, 0x1F, 0x16, 0x1E, 0x03E00001, PPC_CACHE)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    gen_op_icbi();
}

/* Optional: */
/* dcba */
GEN_HANDLER(dcba, 0x1F, 0x16, 0x17, 0x03E00001, PPC_CACHE_OPT)
{
}

/***                    Segment register manipulation                      ***/
/* Supervisor only: */
/* mfsr */
GEN_HANDLER(mfsr, 0x1F, 0x13, 0x12, 0x0010F801, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_sr(SR(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mfsrin */
GEN_HANDLER(mfsrin, 0x1F, 0x13, 0x14, 0x001F0001, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_load_srin();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mtsr */
GEN_HANDLER(mtsr, 0x1F, 0x12, 0x06, 0x0010F801, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_sr(SR(ctx->opcode));
    RET_STOP(ctx);
#endif
}

/* mtsrin */
GEN_HANDLER(mtsrin, 0x1F, 0x12, 0x07, 0x001F0001, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_store_srin();
    RET_STOP(ctx);
#endif
}

/***                      Lookaside buffer management                      ***/
/* Optional & supervisor only: */
/* tlbia */
GEN_HANDLER(tlbia, 0x1F, 0x12, 0x0B, 0x03FFFC01, PPC_MEM_TLBIA)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (!ctx->supervisor) {
        if (loglevel)
            fprintf(logfile, "%s: ! supervisor\n", __func__);
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_tlbia();
    RET_STOP(ctx);
#endif
}

/* tlbie */
GEN_HANDLER(tlbie, 0x1F, 0x12, 0x09, 0x03FF0001, PPC_MEM)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rB(ctx->opcode));
    gen_op_tlbie();
    RET_STOP(ctx);
#endif
}

/* tlbsync */
GEN_HANDLER(tlbsync, 0x1F, 0x16, 0x11, 0x03FFF801, PPC_MEM)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (!ctx->supervisor) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* This has no effect: it should ensure that all previous
     * tlbie have completed
     */
    RET_STOP(ctx);
#endif
}

/***                              External control                         ***/
/* Optional: */
#define op_eciwx() (*gen_op_eciwx[ctx->mem_idx])()
#define op_ecowx() (*gen_op_ecowx[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_eciwx[] = {
    &gen_op_eciwx_raw,
    &gen_op_eciwx_le_raw,
};
static GenOpFunc *gen_op_ecowx[] = {
    &gen_op_ecowx_raw,
    &gen_op_ecowx_le_raw,
};
#else
static GenOpFunc *gen_op_eciwx[] = {
    &gen_op_eciwx_user,
    &gen_op_eciwx_le_user,
    &gen_op_eciwx_kernel,
    &gen_op_eciwx_le_kernel,
};
static GenOpFunc *gen_op_ecowx[] = {
    &gen_op_ecowx_user,
    &gen_op_ecowx_le_user,
    &gen_op_ecowx_kernel,
    &gen_op_ecowx_le_kernel,
};
#endif

/* eciwx */
GEN_HANDLER(eciwx, 0x1F, 0x16, 0x0D, 0x00000001, PPC_EXTERN)
{
    /* Should check EAR[E] & alignment ! */
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    op_eciwx();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* ecowx */
GEN_HANDLER(ecowx, 0x1F, 0x16, 0x09, 0x00000001, PPC_EXTERN)
{
    /* Should check EAR[E] & alignment ! */
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
    gen_op_load_gpr_T2(rS(ctx->opcode));
    op_ecowx();
}

/* End opcode list */
GEN_OPCODE_MARK(end);

#include "translate_init.c"

/*****************************************************************************/
/* Misc PowerPC helpers */
void cpu_dump_state(CPUState *env, FILE *f, 
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
#if defined(TARGET_PPC64) || 1
#define FILL ""
#define REGX "%016" PRIx64
#define RGPL  4
#define RFPL  4
#else
#define FILL "        "
#define REGX "%08" PRIx64
#define RGPL  8
#define RFPL  4
#endif

    int i;

    cpu_fprintf(f, "NIP " REGX " LR " REGX " CTR " REGX "\n",
                env->nip, env->lr, env->ctr);
    cpu_fprintf(f, "MSR " REGX FILL " XER %08x      TB %08x %08x DECR %08x\n",
                do_load_msr(env), do_load_xer(env), cpu_ppc_load_tbu(env),
                cpu_ppc_load_tbl(env), cpu_ppc_load_decr(env));
        for (i = 0; i < 32; i++) {
        if ((i & (RGPL - 1)) == 0)
            cpu_fprintf(f, "GPR%02d", i);
        cpu_fprintf(f, " " REGX, env->gpr[i]);
        if ((i & (RGPL - 1)) == (RGPL - 1))
            cpu_fprintf(f, "\n");
        }
    cpu_fprintf(f, "CR ");
        for (i = 0; i < 8; i++)
        cpu_fprintf(f, "%01x", env->crf[i]);
    cpu_fprintf(f, "  [");
        for (i = 0; i < 8; i++) {
            char a = '-';
            if (env->crf[i] & 0x08)
                a = 'L';
            else if (env->crf[i] & 0x04)
                a = 'G';
            else if (env->crf[i] & 0x02)
                a = 'E';
        cpu_fprintf(f, " %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
        }
    cpu_fprintf(f, " ]             " FILL "RES " REGX "\n", env->reserve);
    for (i = 0; i < 32; i++) {
        if ((i & (RFPL - 1)) == 0)
            cpu_fprintf(f, "FPR%02d", i);
        cpu_fprintf(f, " %016" PRIx64, *((uint64_t *)&env->fpr[i]));
        if ((i & (RFPL - 1)) == (RFPL - 1))
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "SRR0 " REGX " SRR1 " REGX "         " FILL FILL FILL
                "SDR1 " REGX "\n",
                env->spr[SPR_SRR0], env->spr[SPR_SRR1], env->sdr1);

#undef REGX
#undef RGPL
#undef RFPL
#undef FILL
}

/*****************************************************************************/
int gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                    int search_pc)
{
    DisasContext ctx, *ctxp = &ctx;
    opc_handler_t **table, *handler;
    target_ulong pc_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;

    pc_start = tb->pc;
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    nb_gen_labels = 0;
    ctx.nip = pc_start;
    ctx.tb = tb;
    ctx.exception = EXCP_NONE;
    ctx.spr_cb = env->spr_cb;
#if defined(CONFIG_USER_ONLY)
    ctx.mem_idx = msr_le;
#else
    ctx.supervisor = 1 - msr_pr;
    ctx.mem_idx = ((1 - msr_pr) << 1) | msr_le;
#endif
    ctx.fpu_enabled = msr_fp;
    ctx.singlestep_enabled = env->singlestep_enabled;
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr_se = 1;
#endif
    /* Set env in case of segfault during code fetch */
    while (ctx.exception == EXCP_NONE && gen_opc_ptr < gen_opc_end) {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == ctx.nip) {
                    gen_op_update_nip(ctx.nip); 
                    gen_op_debug();
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
                gen_opc_pc[lj] = ctx.nip;
                gen_opc_instr_start[lj] = 1;
            }
        }
#if defined PPC_DEBUG_DISAS
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "----------------\n");
            fprintf(logfile, "nip=%08x super=%d ir=%d\n",
                    ctx.nip, 1 - msr_pr, msr_ir);
        }
#endif
        ctx.opcode = ldl_code(ctx.nip);
        if (msr_le) {
            ctx.opcode = ((ctx.opcode & 0xFF000000) >> 24) |
                ((ctx.opcode & 0x00FF0000) >> 8) |
                ((ctx.opcode & 0x0000FF00) << 8) |
                ((ctx.opcode & 0x000000FF) << 24);
        }
#if defined PPC_DEBUG_DISAS
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "translate opcode %08x (%02x %02x %02x) (%s)\n",
                    ctx.opcode, opc1(ctx.opcode), opc2(ctx.opcode),
                    opc3(ctx.opcode), msr_le ? "little" : "big");
        }
#endif
        ctx.nip += 4;
        table = env->opcodes;
        handler = table[opc1(ctx.opcode)];
        if (is_indirect_opcode(handler)) {
            table = ind_table(handler);
            handler = table[opc2(ctx.opcode)];
            if (is_indirect_opcode(handler)) {
                table = ind_table(handler);
                handler = table[opc3(ctx.opcode)];
            }
        }
        /* Is opcode *REALLY* valid ? */
                if (handler->handler == &gen_invalid) {
            if (loglevel > 0) {
                    fprintf(logfile, "invalid/unsupported opcode: "
                        "%02x - %02x - %02x (%08x) 0x%08x %d\n",
                            opc1(ctx.opcode), opc2(ctx.opcode),
                        opc3(ctx.opcode), ctx.opcode, ctx.nip - 4, msr_ir);
            } else {
                printf("invalid/unsupported opcode: "
                       "%02x - %02x - %02x (%08x) 0x%08x %d\n",
                       opc1(ctx.opcode), opc2(ctx.opcode),
                       opc3(ctx.opcode), ctx.opcode, ctx.nip - 4, msr_ir);
            }
                } else {
            if ((ctx.opcode & handler->inval) != 0) {
                if (loglevel > 0) {
                    fprintf(logfile, "invalid bits: %08x for opcode: "
                            "%02x -%02x - %02x (0x%08x) (0x%08x)\n",
                            ctx.opcode & handler->inval, opc1(ctx.opcode),
                            opc2(ctx.opcode), opc3(ctx.opcode),
                            ctx.opcode, ctx.nip - 4);
                } else {
                    printf("invalid bits: %08x for opcode: "
                           "%02x -%02x - %02x (0x%08x) (0x%08x)\n",
                            ctx.opcode & handler->inval, opc1(ctx.opcode),
                            opc2(ctx.opcode), opc3(ctx.opcode),
                           ctx.opcode, ctx.nip - 4);
            }
                RET_INVAL(ctxp);
                break;
            }
        }
        (*(handler->handler))(&ctx);
        /* Check trace mode exceptions */
        if ((msr_be && ctx.exception == EXCP_BRANCH) ||
            /* Check in single step trace mode
             * we need to stop except if:
             * - rfi, trap or syscall
             * - first instruction of an exception handler
             */
            (msr_se && (ctx.nip < 0x100 ||
                        ctx.nip > 0xF00 ||
                        (ctx.nip & 0xFC) != 0x04) &&
             ctx.exception != EXCP_SYSCALL &&
             ctx.exception != EXCP_SYSCALL_USER &&
             ctx.exception != EXCP_TRAP)) {
            RET_EXCP(ctxp, EXCP_TRACE, 0);
        }

        /* if we reach a page boundary or are single stepping, stop
         * generation
         */
        if (((ctx.nip & (TARGET_PAGE_SIZE - 1)) == 0) ||
            (env->singlestep_enabled)) {
            break;
    }
#if defined (DO_SINGLE_STEP)
        break;
#endif
    }
    if (ctx.exception == EXCP_NONE) {
        gen_goto_tb(&ctx, 0, ctx.nip);
    } else if (ctx.exception != EXCP_BRANCH) {
        gen_op_set_T0(0);
    }
#if 1
    /* TO BE FIXED: T0 hasn't got a proper value, which makes tb_add_jump
     *              do bad business and then qemu crashes !
     */
    gen_op_set_T0(0);
#endif
    /* Generate the return instruction */
    gen_op_exit_tb();
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
        tb->size = 0;
#if 0
        if (loglevel > 0) {
            page_dump(logfile);
        }
#endif
    } else {
        tb->size = ctx.nip - pc_start;
    }
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "---------------- excp: %04x\n", ctx.exception);
        cpu_dump_state(env, logfile, fprintf, 0);
    }
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	target_disas(logfile, pc_start, ctx.nip - pc_start, msr_le);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_OP) {
        fprintf(logfile, "OP:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
#endif
    return 0;
}

int gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}
