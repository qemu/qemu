/*
 *  PPC emulation for qemu: main translation routines.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
    uint32_t nip;
    uint32_t opcode;
    uint32_t exception;
    /* Execution mode */
#if !defined(CONFIG_USER_ONLY)
    int supervisor;
#endif
    /* Routine used to access memory */
    int mem_idx;
} DisasContext;

typedef struct opc_handler_t {
    /* invalid bits */
    uint32_t inval;
    /* instruction type */
    uint32_t type;
    /* handler */
    void (*handler)(DisasContext *ctx);
} opc_handler_t;

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

#define RET_MTMSR(ctx)                                                        \
RET_EXCP((ctx), EXCP_MTMSR, 0)

#define GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                      \
static void gen_##name (DisasContext *ctx);                                   \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type);                              \
static void gen_##name (DisasContext *ctx)

typedef struct opcode_t {
    unsigned char opc1, opc2, opc3;
    opc_handler_t handler;
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
    return s_ext16((opcode >> (shift)) & ((1 << (nb)) - 1));                  \
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
EXTRACT_HELPER(SPR, 11, 10);
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

#if defined(__APPLE__)
#define OPCODES_SECTION \
    __attribute__ ((section("__TEXT,__opcodes"), unused, aligned (8) ))
#else
#define OPCODES_SECTION \
    __attribute__ ((section(".opcodes"), unused, aligned (8) ))
#endif

#define GEN_OPCODE(name, op1, op2, op3, invl, _typ)                           \
OPCODES_SECTION static opcode_t opc_##name = {                                \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .handler = {                                                              \
        .inval   = invl,                                                      \
        .type = _typ,                                                         \
        .handler = &gen_##name,                                               \
    },                                                                        \
}

#define GEN_OPCODE_MARK(name)                                                 \
OPCODES_SECTION static opcode_t opc_##name = {                                \
    .opc1 = 0xFF,                                                             \
    .opc2 = 0xFF,                                                             \
    .opc3 = 0xFF,                                                             \
    .handler = {                                                              \
        .inval   = 0x00000000,                                                \
        .type = 0x00,                                                         \
        .handler = NULL,                                                      \
    },                                                                        \
}

/* Start opcode list */
GEN_OPCODE_MARK(start);

/* Invalid instruction */
GEN_HANDLER(invalid, 0x00, 0x00, 0x00, 0xFFFFFFFF, PPC_NONE)
{
    RET_INVAL(ctx);
}

/* Special opcode to stop emulation */
GEN_HANDLER(stop, 0x06, 0x00, 0xFF, 0x03FFFFC1, PPC_COMMON)
{
    RET_EXCP(ctx, EXCP_HLT, 0);
}

/* Special opcode to call open-firmware */
GEN_HANDLER(of_enter, 0x06, 0x01, 0xFF, 0x03FFFFC1, PPC_COMMON)
{
    RET_EXCP(ctx, EXCP_OFCALL, 0);
}

/* Special opcode to call RTAS */
GEN_HANDLER(rtas_enter, 0x06, 0x02, 0xFF, 0x03FFFFC1, PPC_COMMON)
{
    printf("RTAS entry point !\n");
    RET_EXCP(ctx, EXCP_RTASCALL, 0);
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
        gen_op_set_Rc0_ov();                                                  \
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
        gen_op_set_Rc0_ov();                                                  \
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
    gen_op_srawi(SH(ctx->opcode), MASK(32 - SH(ctx->opcode), 31));
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* srw & srw. */
__GEN_LOGICAL2(srw, 0x18, 0x10);

/***                       Floating-Point arithmetic                       ***/
#define _GEN_FLOAT_ACB(name, op1, op2)                                        \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x00000000, PPC_FLOAT)                   \
{                                                                             \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rC(ctx->opcode));                                     \
    gen_op_load_fpr_FT2(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_ACB(name, op2)                                              \
_GEN_FLOAT_ACB(name, 0x3F, op2);                                              \
_GEN_FLOAT_ACB(name##s, 0x3B, op2);

#define _GEN_FLOAT_AB(name, op1, op2, inval)                                  \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AB(name, op2, inval)                                        \
_GEN_FLOAT_AB(name, 0x3F, op2, inval);                                        \
_GEN_FLOAT_AB(name##s, 0x3B, op2, inval);

#define _GEN_FLOAT_AC(name, op1, op2, inval)                                  \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rA(ctx->opcode));                                     \
    gen_op_load_fpr_FT1(rC(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AC(name, op2, inval)                                        \
_GEN_FLOAT_AC(name, 0x3F, op2, inval);                                        \
_GEN_FLOAT_AC(name##s, 0x3B, op2, inval);

#define GEN_FLOAT_B(name, op2, op3)                                           \
GEN_HANDLER(f##name, 0x3F, op2, op3, 0x001F0000, PPC_FLOAT)                   \
{                                                                             \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_BS(name, op2)                                               \
GEN_HANDLER(f##name, 0x3F, op2, 0xFF, 0x001F07C0, PPC_FLOAT)                  \
{                                                                             \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (Rc(ctx->opcode))                                                      \
        gen_op_set_Rc1();                                                     \
}

/* fadd - fadds */
GEN_FLOAT_AB(add, 0x15, 0x000007C0);
/* fdiv */
GEN_FLOAT_AB(div, 0x12, 0x000007C0);
/* fmul */
GEN_FLOAT_AC(mul, 0x19, 0x0000F800);

/* fres */
GEN_FLOAT_BS(res, 0x18);

/* frsqrte */
GEN_FLOAT_BS(rsqrte, 0x1A);

/* fsel */
_GEN_FLOAT_ACB(sel, 0x3F, 0x17);
/* fsub */
GEN_FLOAT_AB(sub, 0x14, 0x000007C0);
/* Optional: */
/* fsqrt */
GEN_FLOAT_BS(sqrt, 0x16);

GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_OPT)
{
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_fsqrts();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd */
GEN_FLOAT_ACB(madd, 0x1D);
/* fmsub */
GEN_FLOAT_ACB(msub, 0x1C);
/* fnmadd */
GEN_FLOAT_ACB(nmadd, 0x1F);
/* fnmsub */
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
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rA(ctx->opcode));
    gen_op_load_fpr_FT1(rB(ctx->opcode));
    gen_op_fcmpo();
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/* fcmpu */
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT)
{
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
    gen_op_load_fpscr_T0(crfS(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
    gen_op_clear_fpscr(crfS(ctx->opcode));
}

/* mffs */
GEN_HANDLER(mffs, 0x3F, 0x07, 0x12, 0x001FF800, PPC_FLOAT)
{
    gen_op_load_fpscr();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsb0 */
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT)
{
    uint8_t crb;
    
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
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_store_fpscr(FM(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/* mtfsfi */
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006f0800, PPC_FLOAT)
{
    gen_op_store_T0_fpscri(crbD(ctx->opcode) >> 2, FPIMM(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_set_Rc1();
}

/***                             Integer load                              ***/
#if defined(CONFIG_USER_ONLY)
#define op_ldst(name)        gen_op_##name##_raw()
#define OP_LD_TABLE(width)
#define OP_ST_TABLE(width)
#else
#define op_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_user,                                                  \
    &gen_op_l##width##_kernel,                                                \
}
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_user,                                                 \
    &gen_op_st##width##_kernel,                                               \
}
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
#if defined(CONFIG_USER_ONLY)
#define op_ldstm(name, reg) gen_op_##name##_raw(reg)
#else
#define op_ldstm(name, reg) (*gen_op_##name[ctx->mem_idx])(reg)
static GenOpFunc1 *gen_op_lmw[] = {
    &gen_op_lmw_user,
    &gen_op_lmw_kernel,
};
static GenOpFunc1 *gen_op_stmw[] = {
    &gen_op_stmw_user,
    &gen_op_stmw_kernel,
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
#if defined(CONFIG_USER_ONLY)
#define op_ldsts(name, start) gen_op_##name##_raw(start)
#define op_ldstsx(name, rd, ra, rb) gen_op_##name##_raw(rd, ra, rb)
#else
#define op_ldsts(name, start) (*gen_op_##name[ctx->mem_idx])(start)
#define op_ldstsx(name, rd, ra, rb) (*gen_op_##name[ctx->mem_idx])(rd, ra, rb)
static GenOpFunc1 *gen_op_lswi[] = {
    &gen_op_lswi_user,
    &gen_op_lswi_kernel,
};
static GenOpFunc3 *gen_op_lswx[] = {
    &gen_op_lswx_user,
    &gen_op_lswx_kernel,
};
static GenOpFunc1 *gen_op_stsw[] = {
    &gen_op_stsw_user,
    &gen_op_stsw_kernel,
};
#endif

/* lswi */
/* PPC32 specification says we must generate an exception if
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

/* lwarx */
#if defined(CONFIG_USER_ONLY)
#define op_lwarx() gen_op_lwarx_raw()
#define op_stwcx() gen_op_stwcx_raw()
#else
#define op_lwarx() (*gen_op_lwarx[ctx->mem_idx])()
static GenOpFunc *gen_op_lwarx[] = {
    &gen_op_lwarx_user,
    &gen_op_lwarx_kernel,
};
#define op_stwcx() (*gen_op_stwcx[ctx->mem_idx])()
static GenOpFunc *gen_op_stwcx[] = {
    &gen_op_stwcx_user,
    &gen_op_stwcx_kernel,
};
#endif

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
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
}

#define GEN_LDUF(width, opc)                                                  \
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
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDUXF(width, opc)                                                 \
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
    gen_op_store_FT1_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDXF(width, opc2, opc3)                                           \
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
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
}

#define GEN_STUF(width, opc)                                                  \
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
    gen_op_load_fpr_FT1(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STUXF(width, opc)                                                 \
GEN_HANDLER(st##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_INTEGER)          \
{                                                                             \
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
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_INTEGER)          \
{                                                                             \
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
    RET_INVAL(ctx);
}

/***                                Branch                                 ***/

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
    gen_op_b((long)ctx->tb, target);
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
        li = s_ext16(BD(ctx->opcode));
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
                gen_op_b((long)ctx->tb, target);
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
        gen_op_btest((long)ctx->tb, target, ctx->nip);
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
    RET_EXCP(ctx, EXCP_RFI, 0);
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

/* mfspr */
GEN_HANDLER(mfspr, 0x1F, 0x13, 0x0A, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

#if defined(CONFIG_USER_ONLY)
    switch (check_spr_access(sprn, 0, 0))
#else
    switch (check_spr_access(sprn, 0, ctx->supervisor))
#endif
    {
    case -1:
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_SPR);
        return;
    case 0:
        RET_PRIVREG(ctx);
        return;
    default:
        break;
        }
    switch (sprn) {
    case XER:
        gen_op_load_xer();
        break;
    case LR:
        gen_op_load_lr();
        break;
    case CTR:
        gen_op_load_ctr();
        break;
    case IBAT0U:
        gen_op_load_ibat(0, 0);
        break;
    case IBAT1U:
        gen_op_load_ibat(0, 1);
        break;
    case IBAT2U:
        gen_op_load_ibat(0, 2);
        break;
    case IBAT3U:
        gen_op_load_ibat(0, 3);
        break;
    case IBAT4U:
        gen_op_load_ibat(0, 4);
        break;
    case IBAT5U:
        gen_op_load_ibat(0, 5);
        break;
    case IBAT6U:
        gen_op_load_ibat(0, 6);
        break;
    case IBAT7U:
        gen_op_load_ibat(0, 7);
        break;
    case IBAT0L:
        gen_op_load_ibat(1, 0);
        break;
    case IBAT1L:
        gen_op_load_ibat(1, 1);
        break;
    case IBAT2L:
        gen_op_load_ibat(1, 2);
        break;
    case IBAT3L:
        gen_op_load_ibat(1, 3);
        break;
    case IBAT4L:
        gen_op_load_ibat(1, 4);
        break;
    case IBAT5L:
        gen_op_load_ibat(1, 5);
        break;
    case IBAT6L:
        gen_op_load_ibat(1, 6);
        break;
    case IBAT7L:
        gen_op_load_ibat(1, 7);
        break;
    case DBAT0U:
        gen_op_load_dbat(0, 0);
        break;
    case DBAT1U:
        gen_op_load_dbat(0, 1);
        break;
    case DBAT2U:
        gen_op_load_dbat(0, 2);
        break;
    case DBAT3U:
        gen_op_load_dbat(0, 3);
        break;
    case DBAT4U:
        gen_op_load_dbat(0, 4);
        break;
    case DBAT5U:
        gen_op_load_dbat(0, 5);
        break;
    case DBAT6U:
        gen_op_load_dbat(0, 6);
        break;
    case DBAT7U:
        gen_op_load_dbat(0, 7);
        break;
    case DBAT0L:
        gen_op_load_dbat(1, 0);
        break;
    case DBAT1L:
        gen_op_load_dbat(1, 1);
        break;
    case DBAT2L:
        gen_op_load_dbat(1, 2);
        break;
    case DBAT3L:
        gen_op_load_dbat(1, 3);
        break;
    case DBAT4L:
        gen_op_load_dbat(1, 4);
        break;
    case DBAT5L:
        gen_op_load_dbat(1, 5);
        break;
    case DBAT6L:
        gen_op_load_dbat(1, 6);
        break;
    case DBAT7L:
        gen_op_load_dbat(1, 7);
        break;
    case SDR1:
        gen_op_load_sdr1();
        break;
    case V_TBL:
        gen_op_load_tbl();
        break;
    case V_TBU:
        gen_op_load_tbu();
        break;
    case DECR:
        gen_op_load_decr();
        break;
    default:
        gen_op_load_spr(sprn);
        break;
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* mftb */
GEN_HANDLER(mftb, 0x1F, 0x13, 0x0B, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

        /* We need to update the time base before reading it */
    switch (sprn) {
    case V_TBL:
        gen_op_load_tbl();
        break;
    case V_TBU:
        gen_op_load_tbu();
        break;
    default:
        RET_INVAL(ctx);
        return;
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* mtcrf */
GEN_HANDLER(mtcrf, 0x1F, 0x10, 0x04, 0x00100801, PPC_MISC)
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
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_msr();
    /* Must stop the translation as machine state (may have) changed */
    RET_MTMSR(ctx);
#endif
}

/* mtspr */
GEN_HANDLER(mtspr, 0x1F, 0x13, 0x0E, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

#if 0
    if (loglevel > 0) {
        fprintf(logfile, "MTSPR %d src=%d (%d)\n", SPR_ENCODE(sprn),
                rS(ctx->opcode), sprn);
    }
#endif
#if defined(CONFIG_USER_ONLY)
    switch (check_spr_access(sprn, 1, 0))
#else
    switch (check_spr_access(sprn, 1, ctx->supervisor))
#endif
    {
    case -1:
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_SPR);
        break;
    case 0:
        RET_PRIVREG(ctx);
        break;
    default:
        break;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    switch (sprn) {
    case XER:
        gen_op_store_xer();
        break;
    case LR:
        gen_op_store_lr();
        break;
    case CTR:
        gen_op_store_ctr();
        break;
    case IBAT0U:
        gen_op_store_ibat(0, 0);
        RET_MTMSR(ctx);
        break;
    case IBAT1U:
        gen_op_store_ibat(0, 1);
        RET_MTMSR(ctx);
        break;
    case IBAT2U:
        gen_op_store_ibat(0, 2);
        RET_MTMSR(ctx);
        break;
    case IBAT3U:
        gen_op_store_ibat(0, 3);
        RET_MTMSR(ctx);
        break;
    case IBAT4U:
        gen_op_store_ibat(0, 4);
        RET_MTMSR(ctx);
        break;
    case IBAT5U:
        gen_op_store_ibat(0, 5);
        RET_MTMSR(ctx);
        break;
    case IBAT6U:
        gen_op_store_ibat(0, 6);
        RET_MTMSR(ctx);
        break;
    case IBAT7U:
        gen_op_store_ibat(0, 7);
        RET_MTMSR(ctx);
        break;
    case IBAT0L:
        gen_op_store_ibat(1, 0);
        RET_MTMSR(ctx);
        break;
    case IBAT1L:
        gen_op_store_ibat(1, 1);
        RET_MTMSR(ctx);
        break;
    case IBAT2L:
        gen_op_store_ibat(1, 2);
        RET_MTMSR(ctx);
        break;
    case IBAT3L:
        gen_op_store_ibat(1, 3);
        RET_MTMSR(ctx);
        break;
    case IBAT4L:
        gen_op_store_ibat(1, 4);
        RET_MTMSR(ctx);
        break;
    case IBAT5L:
        gen_op_store_ibat(1, 5);
        RET_MTMSR(ctx);
        break;
    case IBAT6L:
        gen_op_store_ibat(1, 6);
        RET_MTMSR(ctx);
        break;
    case IBAT7L:
        gen_op_store_ibat(1, 7);
        RET_MTMSR(ctx);
        break;
    case DBAT0U:
        gen_op_store_dbat(0, 0);
        RET_MTMSR(ctx);
        break;
    case DBAT1U:
        gen_op_store_dbat(0, 1);
        RET_MTMSR(ctx);
        break;
    case DBAT2U:
        gen_op_store_dbat(0, 2);
        RET_MTMSR(ctx);
        break;
    case DBAT3U:
        gen_op_store_dbat(0, 3);
        RET_MTMSR(ctx);
        break;
    case DBAT4U:
        gen_op_store_dbat(0, 4);
        RET_MTMSR(ctx);
        break;
    case DBAT5U:
        gen_op_store_dbat(0, 5);
        RET_MTMSR(ctx);
        break;
    case DBAT6U:
        gen_op_store_dbat(0, 6);
        RET_MTMSR(ctx);
        break;
    case DBAT7U:
        gen_op_store_dbat(0, 7);
        RET_MTMSR(ctx);
        break;
    case DBAT0L:
        gen_op_store_dbat(1, 0);
        RET_MTMSR(ctx);
        break;
    case DBAT1L:
        gen_op_store_dbat(1, 1);
        RET_MTMSR(ctx);
        break;
    case DBAT2L:
        gen_op_store_dbat(1, 2);
        RET_MTMSR(ctx);
        break;
    case DBAT3L:
        gen_op_store_dbat(1, 3);
        RET_MTMSR(ctx);
        break;
    case DBAT4L:
        gen_op_store_dbat(1, 4);
        RET_MTMSR(ctx);
        break;
    case DBAT5L:
        gen_op_store_dbat(1, 5);
        RET_MTMSR(ctx);
        break;
    case DBAT6L:
        gen_op_store_dbat(1, 6);
        RET_MTMSR(ctx);
        break;
    case DBAT7L:
        gen_op_store_dbat(1, 7);
        RET_MTMSR(ctx);
        break;
    case SDR1:
        gen_op_store_sdr1();
        RET_MTMSR(ctx);
        break;
    case O_TBL:
        gen_op_store_tbl();
        break;
    case O_TBU:
        gen_op_store_tbu();
        break;
    case DECR:
        gen_op_store_decr();
        break;
#if 0
    case HID0:
        gen_op_store_hid0();
        break;
#endif
    default:
        gen_op_store_spr(sprn);
        break;
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
GEN_HANDLER(dcba, 0x1F, 0x16, 0x07, 0x03E00001, PPC_CACHE_OPT)
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
#endif
}

/***                      Lookaside buffer management                      ***/
/* Optional & supervisor only: */
/* tlbia */
GEN_HANDLER(tlbia, 0x1F, 0x12, 0x0B, 0x03FFFC01, PPC_MEM_OPT)
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
    RET_MTMSR(ctx);
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
    RET_MTMSR(ctx);
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
    RET_MTMSR(ctx);
#endif
}

/***                              External control                         ***/
/* Optional: */
/* eciwx */
#if defined(CONFIG_USER_ONLY)
#define op_eciwx() gen_op_eciwx_raw()
#define op_ecowx() gen_op_ecowx_raw()
#else
#define op_eciwx() (*gen_op_eciwx[ctx->mem_idx])()
#define op_ecowx() (*gen_op_ecowx[ctx->mem_idx])()
static GenOpFunc *gen_op_eciwx[] = {
    &gen_op_eciwx_user,
    &gen_op_eciwx_kernel,
};
static GenOpFunc *gen_op_ecowx[] = {
    &gen_op_ecowx_user,
    &gen_op_ecowx_kernel,
};
#endif

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

/*****************************************************************************/
#include <stdlib.h>
#include <string.h>

int fflush (FILE *stream);

/* Main ppc opcodes table:
 * at init, all opcodes are invalids
 */
static opc_handler_t *ppc_opcodes[0x40];

/* Opcode types */
enum {
    PPC_DIRECT   = 0, /* Opcode routine        */
    PPC_INDIRECT = 1, /* Indirect opcode table */
};

static inline int is_indirect_opcode (void *handler)
{
    return ((unsigned long)handler & 0x03) == PPC_INDIRECT;
}

static inline opc_handler_t **ind_table(void *handler)
{
    return (opc_handler_t **)((unsigned long)handler & ~3);
}

/* Instruction table creation */
/* Opcodes tables creation */
static void fill_new_table (opc_handler_t **table, int len)
{
    int i;

    for (i = 0; i < len; i++)
        table[i] = &invalid_handler;
}

static int create_new_table (opc_handler_t **table, unsigned char idx)
{
    opc_handler_t **tmp;

    tmp = malloc(0x20 * sizeof(opc_handler_t));
    if (tmp == NULL)
        return -1;
    fill_new_table(tmp, 0x20);
    table[idx] = (opc_handler_t *)((unsigned long)tmp | PPC_INDIRECT);

    return 0;
}

static int insert_in_table (opc_handler_t **table, unsigned char idx,
                            opc_handler_t *handler)
{
    if (table[idx] != &invalid_handler)
        return -1;
    table[idx] = handler;

    return 0;
}

static int register_direct_insn (opc_handler_t **ppc_opcodes,
                                 unsigned char idx, opc_handler_t *handler)
{
    if (insert_in_table(ppc_opcodes, idx, handler) < 0) {
        printf("*** ERROR: opcode %02x already assigned in main "
                "opcode table\n", idx);
        return -1;
    }

    return 0;
}

static int register_ind_in_table (opc_handler_t **table,
                                  unsigned char idx1, unsigned char idx2,
                                  opc_handler_t *handler)
{
    if (table[idx1] == &invalid_handler) {
        if (create_new_table(table, idx1) < 0) {
            printf("*** ERROR: unable to create indirect table "
                    "idx=%02x\n", idx1);
            return -1;
        }
    } else {
        if (!is_indirect_opcode(table[idx1])) {
            printf("*** ERROR: idx %02x already assigned to a direct "
                    "opcode\n", idx1);
            return -1;
        }
    }
    if (handler != NULL &&
        insert_in_table(ind_table(table[idx1]), idx2, handler) < 0) {
        printf("*** ERROR: opcode %02x already assigned in "
                "opcode table %02x\n", idx2, idx1);
        return -1;
    }

    return 0;
}

static int register_ind_insn (opc_handler_t **ppc_opcodes,
                              unsigned char idx1, unsigned char idx2,
                               opc_handler_t *handler)
{
    int ret;

    ret = register_ind_in_table(ppc_opcodes, idx1, idx2, handler);

    return ret;
}

static int register_dblind_insn (opc_handler_t **ppc_opcodes, 
                                 unsigned char idx1, unsigned char idx2,
                                  unsigned char idx3, opc_handler_t *handler)
{
    if (register_ind_in_table(ppc_opcodes, idx1, idx2, NULL) < 0) {
        printf("*** ERROR: unable to join indirect table idx "
                "[%02x-%02x]\n", idx1, idx2);
        return -1;
    }
    if (register_ind_in_table(ind_table(ppc_opcodes[idx1]), idx2, idx3,
                              handler) < 0) {
        printf("*** ERROR: unable to insert opcode "
                "[%02x-%02x-%02x]\n", idx1, idx2, idx3);
        return -1;
    }

    return 0;
}

static int register_insn (opc_handler_t **ppc_opcodes, opcode_t *insn)
{
    if (insn->opc2 != 0xFF) {
        if (insn->opc3 != 0xFF) {
            if (register_dblind_insn(ppc_opcodes, insn->opc1, insn->opc2,
                                     insn->opc3, &insn->handler) < 0)
                return -1;
        } else {
            if (register_ind_insn(ppc_opcodes, insn->opc1,
                                  insn->opc2, &insn->handler) < 0)
                return -1;
        }
    } else {
        if (register_direct_insn(ppc_opcodes, insn->opc1, &insn->handler) < 0)
            return -1;
    }

    return 0;
}

static int test_opcode_table (opc_handler_t **table, int len)
{
    int i, count, tmp;

    for (i = 0, count = 0; i < len; i++) {
        /* Consistency fixup */
        if (table[i] == NULL)
            table[i] = &invalid_handler;
        if (table[i] != &invalid_handler) {
            if (is_indirect_opcode(table[i])) {
                tmp = test_opcode_table(ind_table(table[i]), 0x20);
                if (tmp == 0) {
                    free(table[i]);
                    table[i] = &invalid_handler;
                } else {
                    count++;
                }
            } else {
                count++;
            }
        }
    }

    return count;
}

static void fix_opcode_tables (opc_handler_t **ppc_opcodes)
{
    if (test_opcode_table(ppc_opcodes, 0x40) == 0)
        printf("*** WARNING: no opcode defined !\n");
}

#define SPR_RIGHTS(rw, priv) (1 << ((2 * (priv)) + (rw)))
#define SPR_UR SPR_RIGHTS(0, 0)
#define SPR_UW SPR_RIGHTS(1, 0)
#define SPR_SR SPR_RIGHTS(0, 1)
#define SPR_SW SPR_RIGHTS(1, 1)

#define spr_set_rights(spr, rights)                            \
do {                                                           \
    spr_access[(spr) >> 1] |= ((rights) << (4 * ((spr) & 1))); \
} while (0)

static void init_spr_rights (uint32_t pvr)
{
    /* XER    (SPR 1) */
    spr_set_rights(XER,    SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* LR     (SPR 8) */
    spr_set_rights(LR,     SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* CTR    (SPR 9) */
    spr_set_rights(CTR,    SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* TBL    (SPR 268) */
    spr_set_rights(V_TBL,  SPR_UR | SPR_SR);
    /* TBU    (SPR 269) */
    spr_set_rights(V_TBU,  SPR_UR | SPR_SR);
    /* DSISR  (SPR 18) */
    spr_set_rights(DSISR,  SPR_SR | SPR_SW);
    /* DAR    (SPR 19) */
    spr_set_rights(DAR,    SPR_SR | SPR_SW);
    /* DEC    (SPR 22) */
    spr_set_rights(DECR,   SPR_SR | SPR_SW);
    /* SDR1   (SPR 25) */
    spr_set_rights(SDR1,   SPR_SR | SPR_SW);
    /* SRR0   (SPR 26) */
    spr_set_rights(SRR0,   SPR_SR | SPR_SW);
    /* SRR1   (SPR 27) */
    spr_set_rights(SRR1,   SPR_SR | SPR_SW);
    /* SPRG0  (SPR 272) */
    spr_set_rights(SPRG0,  SPR_SR | SPR_SW);
    /* SPRG1  (SPR 273) */
    spr_set_rights(SPRG1,  SPR_SR | SPR_SW);
    /* SPRG2  (SPR 274) */
    spr_set_rights(SPRG2,  SPR_SR | SPR_SW);
    /* SPRG3  (SPR 275) */
    spr_set_rights(SPRG3,  SPR_SR | SPR_SW);
    /* ASR    (SPR 280) */
    spr_set_rights(ASR,    SPR_SR | SPR_SW);
    /* EAR    (SPR 282) */
    spr_set_rights(EAR,    SPR_SR | SPR_SW);
    /* TBL    (SPR 284) */
    spr_set_rights(O_TBL,  SPR_SW);
    /* TBU    (SPR 285) */
    spr_set_rights(O_TBU,  SPR_SW);
    /* PVR    (SPR 287) */
    spr_set_rights(PVR,    SPR_SR);
    /* IBAT0U (SPR 528) */
    spr_set_rights(IBAT0U, SPR_SR | SPR_SW);
    /* IBAT0L (SPR 529) */
    spr_set_rights(IBAT0L, SPR_SR | SPR_SW);
    /* IBAT1U (SPR 530) */
    spr_set_rights(IBAT1U, SPR_SR | SPR_SW);
    /* IBAT1L (SPR 531) */
    spr_set_rights(IBAT1L, SPR_SR | SPR_SW);
    /* IBAT2U (SPR 532) */
    spr_set_rights(IBAT2U, SPR_SR | SPR_SW);
    /* IBAT2L (SPR 533) */
    spr_set_rights(IBAT2L, SPR_SR | SPR_SW);
    /* IBAT3U (SPR 534) */
    spr_set_rights(IBAT3U, SPR_SR | SPR_SW);
    /* IBAT3L (SPR 535) */
    spr_set_rights(IBAT3L, SPR_SR | SPR_SW);
    /* DBAT0U (SPR 536) */
    spr_set_rights(DBAT0U, SPR_SR | SPR_SW);
    /* DBAT0L (SPR 537) */
    spr_set_rights(DBAT0L, SPR_SR | SPR_SW);
    /* DBAT1U (SPR 538) */
    spr_set_rights(DBAT1U, SPR_SR | SPR_SW);
    /* DBAT1L (SPR 539) */
    spr_set_rights(DBAT1L, SPR_SR | SPR_SW);
    /* DBAT2U (SPR 540) */
    spr_set_rights(DBAT2U, SPR_SR | SPR_SW);
    /* DBAT2L (SPR 541) */
    spr_set_rights(DBAT2L, SPR_SR | SPR_SW);
    /* DBAT3U (SPR 542) */
    spr_set_rights(DBAT3U, SPR_SR | SPR_SW);
    /* DBAT3L (SPR 543) */
    spr_set_rights(DBAT3L, SPR_SR | SPR_SW);
    /* FPECR  (SPR 1022) */
    spr_set_rights(FPECR,  SPR_SR | SPR_SW);
    /* Special registers for PPC 604 */
    if ((pvr & 0xFFFF0000) == 0x00040000) {
        /* IABR */
        spr_set_rights(IABR ,  SPR_SR | SPR_SW);
        /* DABR   (SPR 1013) */
        spr_set_rights(DABR,   SPR_SR | SPR_SW);
        /* HID0 */
        spr_set_rights(HID0,   SPR_SR | SPR_SW);
        /* PIR */
    spr_set_rights(PIR,    SPR_SR | SPR_SW);
        /* PMC1 */
        spr_set_rights(PMC1,   SPR_SR | SPR_SW);
        /* PMC2 */
        spr_set_rights(PMC2,   SPR_SR | SPR_SW);
        /* MMCR0 */
        spr_set_rights(MMCR0,  SPR_SR | SPR_SW);
        /* SIA */
        spr_set_rights(SIA,    SPR_SR | SPR_SW);
        /* SDA */
        spr_set_rights(SDA,    SPR_SR | SPR_SW);
    }
    /* Special registers for MPC740/745/750/755 (aka G3) & IBM 750 */
    if ((pvr & 0xFFFF0000) == 0x00080000 ||
        (pvr & 0xFFFF0000) == 0x70000000) {
        /* HID0 */
        spr_set_rights(HID0,   SPR_SR | SPR_SW);
        /* HID1 */
        spr_set_rights(HID1,   SPR_SR | SPR_SW);
        /* IABR */
        spr_set_rights(IABR,   SPR_SR | SPR_SW);
        /* ICTC */
        spr_set_rights(ICTC,   SPR_SR | SPR_SW);
        /* L2CR */
        spr_set_rights(L2CR,   SPR_SR | SPR_SW);
        /* MMCR0 */
        spr_set_rights(MMCR0,  SPR_SR | SPR_SW);
        /* MMCR1 */
        spr_set_rights(MMCR1,  SPR_SR | SPR_SW);
        /* PMC1 */
        spr_set_rights(PMC1,   SPR_SR | SPR_SW);
        /* PMC2 */
        spr_set_rights(PMC2,   SPR_SR | SPR_SW);
        /* PMC3 */
        spr_set_rights(PMC3,   SPR_SR | SPR_SW);
        /* PMC4 */
        spr_set_rights(PMC4,   SPR_SR | SPR_SW);
        /* SIA */
        spr_set_rights(SIA,    SPR_SR | SPR_SW);
        /* SDA */
        spr_set_rights(SDA,    SPR_SR | SPR_SW);
        /* THRM1 */
        spr_set_rights(THRM1,  SPR_SR | SPR_SW);
        /* THRM2 */
        spr_set_rights(THRM2,  SPR_SR | SPR_SW);
        /* THRM3 */
        spr_set_rights(THRM3,  SPR_SR | SPR_SW);
        /* UMMCR0 */
        spr_set_rights(UMMCR0, SPR_UR | SPR_UW);
        /* UMMCR1 */
        spr_set_rights(UMMCR1, SPR_UR | SPR_UW);
        /* UPMC1 */
        spr_set_rights(UPMC1,  SPR_UR | SPR_UW);
        /* UPMC2 */
        spr_set_rights(UPMC2,  SPR_UR | SPR_UW);
        /* UPMC3 */
        spr_set_rights(UPMC3,  SPR_UR | SPR_UW);
        /* UPMC4 */
        spr_set_rights(UPMC4,  SPR_UR | SPR_UW);
        /* USIA */
        spr_set_rights(USIA,   SPR_UR | SPR_UW);
    }
    /* MPC755 has special registers */
    if (pvr == 0x00083100) {
        /* SPRG4 */
        spr_set_rights(SPRG4, SPR_SR | SPR_SW);
        /* SPRG5 */
        spr_set_rights(SPRG5, SPR_SR | SPR_SW);
        /* SPRG6 */
        spr_set_rights(SPRG6, SPR_SR | SPR_SW);
        /* SPRG7 */
        spr_set_rights(SPRG7, SPR_SR | SPR_SW);
        /* IBAT4U */
        spr_set_rights(IBAT4U, SPR_SR | SPR_SW);
        /* IBAT4L */
        spr_set_rights(IBAT4L, SPR_SR | SPR_SW);
        /* IBAT5U */
        spr_set_rights(IBAT5U, SPR_SR | SPR_SW);
        /* IBAT5L */
        spr_set_rights(IBAT5L, SPR_SR | SPR_SW);
        /* IBAT6U */
        spr_set_rights(IBAT6U, SPR_SR | SPR_SW);
        /* IBAT6L */
        spr_set_rights(IBAT6L, SPR_SR | SPR_SW);
        /* IBAT7U */
        spr_set_rights(IBAT7U, SPR_SR | SPR_SW);
        /* IBAT7L */
        spr_set_rights(IBAT7L, SPR_SR | SPR_SW);
        /* DBAT4U */
        spr_set_rights(DBAT4U, SPR_SR | SPR_SW);
        /* DBAT4L */
        spr_set_rights(DBAT4L, SPR_SR | SPR_SW);
        /* DBAT5U */
        spr_set_rights(DBAT5U, SPR_SR | SPR_SW);
        /* DBAT5L */
        spr_set_rights(DBAT5L, SPR_SR | SPR_SW);
        /* DBAT6U */
        spr_set_rights(DBAT6U, SPR_SR | SPR_SW);
        /* DBAT6L */
        spr_set_rights(DBAT6L, SPR_SR | SPR_SW);
        /* DBAT7U */
        spr_set_rights(DBAT7U, SPR_SR | SPR_SW);
        /* DBAT7L */
        spr_set_rights(DBAT7L, SPR_SR | SPR_SW);
        /* DMISS */
        spr_set_rights(DMISS,  SPR_SR | SPR_SW);
        /* DCMP */
        spr_set_rights(DCMP,   SPR_SR | SPR_SW);
        /* DHASH1 */
        spr_set_rights(DHASH1, SPR_SR | SPR_SW);
        /* DHASH2 */
        spr_set_rights(DHASH2, SPR_SR | SPR_SW);
        /* IMISS */
        spr_set_rights(IMISS,  SPR_SR | SPR_SW);
        /* ICMP */
        spr_set_rights(ICMP,   SPR_SR | SPR_SW);
        /* RPA */
        spr_set_rights(RPA,    SPR_SR | SPR_SW);
        /* HID2 */
        spr_set_rights(HID2,   SPR_SR | SPR_SW);
        /* L2PM */
        spr_set_rights(L2PM,   SPR_SR | SPR_SW);
    }
}

/*****************************************************************************/
/* PPC "main stream" common instructions (no optional ones) */

typedef struct ppc_proc_t {
    int flags;
    void *specific;
} ppc_proc_t;

typedef struct ppc_def_t {
    unsigned long pvr;
    unsigned long pvr_mask;
    ppc_proc_t *proc;
} ppc_def_t;

static ppc_proc_t ppc_proc_common = {
    .flags    = PPC_COMMON,
    .specific = NULL,
};

static ppc_proc_t ppc_proc_G3 = {
    .flags    = PPC_750,
    .specific = NULL,
};

static ppc_def_t ppc_defs[] =
{
    /* MPC740/745/750/755 (G3) */
    {
        .pvr      = 0x00080000,
        .pvr_mask = 0xFFFF0000,
        .proc     = &ppc_proc_G3,
    },
    /* IBM 750FX (G3 embedded) */
    {
        .pvr      = 0x70000000,
        .pvr_mask = 0xFFFF0000,
        .proc     = &ppc_proc_G3,
    },
    /* Fallback (generic PPC) */
    {
        .pvr      = 0x00000000,
        .pvr_mask = 0x00000000,
        .proc     = &ppc_proc_common,
    },
};

static int create_ppc_proc (opc_handler_t **ppc_opcodes, unsigned long pvr)
{
    opcode_t *opc;
    int i, flags;

    fill_new_table(ppc_opcodes, 0x40);
    for (i = 0; ; i++) {
        if ((ppc_defs[i].pvr & ppc_defs[i].pvr_mask) ==
            (pvr & ppc_defs[i].pvr_mask)) {
            flags = ppc_defs[i].proc->flags;
            break;
        }
    }
    
    for (opc = &opc_start + 1; opc != &opc_end; opc++) {
        if ((opc->handler.type & flags) != 0)
            if (register_insn(ppc_opcodes, opc) < 0) {
                printf("*** ERROR initializing PPC instruction "
                        "0x%02x 0x%02x 0x%02x\n", opc->opc1, opc->opc2,
                        opc->opc3);
                return -1;
            }
    }
    fix_opcode_tables(ppc_opcodes);

    return 0;
}


/*****************************************************************************/
/* Misc PPC helpers */

void cpu_ppc_dump_state(CPUPPCState *env, FILE *f, int flags)
{
    int i;

    fprintf(f, "nip=0x%08x LR=0x%08x CTR=0x%08x XER=0x%08x "
            "MSR=0x%08x\n", env->nip, env->lr, env->ctr,
            _load_xer(env), _load_msr(env));
        for (i = 0; i < 32; i++) {
            if ((i & 7) == 0)
            fprintf(f, "GPR%02d:", i);
        fprintf(f, " %08x", env->gpr[i]);
            if ((i & 7) == 7)
            fprintf(f, "\n");
        }
    fprintf(f, "CR: 0x");
        for (i = 0; i < 8; i++)
        fprintf(f, "%01x", env->crf[i]);
    fprintf(f, "  [");
        for (i = 0; i < 8; i++) {
            char a = '-';
            if (env->crf[i] & 0x08)
                a = 'L';
            else if (env->crf[i] & 0x04)
                a = 'G';
            else if (env->crf[i] & 0x02)
                a = 'E';
        fprintf(f, " %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
        }
    fprintf(f, " ] ");
    fprintf(f, "TB: 0x%08x %08x\n", cpu_ppc_load_tbu(env),
            cpu_ppc_load_tbl(env));
        for (i = 0; i < 16; i++) {
            if ((i & 3) == 0)
            fprintf(f, "FPR%02d:", i);
        fprintf(f, " %016llx", *((uint64_t *)&env->fpr[i]));
            if ((i & 3) == 3)
            fprintf(f, "\n");
    }
    fprintf(f, "SRR0 0x%08x SRR1 0x%08x DECR=0x%08x\n",
            env->spr[SRR0], env->spr[SRR1], cpu_ppc_load_decr(env));
    fprintf(f, "reservation 0x%08x\n", env->reserve);
    fflush(f);
}

#if !defined(CONFIG_USER_ONLY) && defined (USE_OPENFIRMWARE)
int setup_machine (CPUPPCState *env, uint32_t mid);
#endif

CPUPPCState *cpu_ppc_init(void)
{
    CPUPPCState *env;

    cpu_exec_init();

    env = qemu_mallocz(sizeof(CPUPPCState));
    if (!env)
        return NULL;
#if !defined(CONFIG_USER_ONLY) && defined (USE_OPEN_FIRMWARE)
    setup_machine(env, 0);
#else
//    env->spr[PVR] = 0; /* Basic PPC */
    env->spr[PVR] = 0x00080100; /* G3 CPU */
//    env->spr[PVR] = 0x00083100; /* MPC755 (G3 embedded) */
//    env->spr[PVR] = 0x00070100; /* IBM 750FX */
#endif
    tlb_flush(env, 1);
#if defined (DO_SINGLE_STEP)
    /* Single step trace mode */
    msr_se = 1;
#endif
    msr_fp = 1; /* Allow floating point exceptions */
    msr_me = 1; /* Allow machine check exceptions  */
#if defined(CONFIG_USER_ONLY)
    msr_pr = 1;
    cpu_ppc_register(env, 0x00080000);
#else
    env->nip = 0xFFFFFFFC;
#endif
    cpu_single_env = env;
    return env;
}

int cpu_ppc_register (CPUPPCState *env, uint32_t pvr)
{
    env->spr[PVR] = pvr;
    if (create_ppc_proc(ppc_opcodes, env->spr[PVR]) < 0)
        return -1;
    init_spr_rights(env->spr[PVR]);

    return 0;
}

void cpu_ppc_close(CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    free(env);
}

/*****************************************************************************/
int gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                    int search_pc)
{
    DisasContext ctx, *ctxp = &ctx;
    opc_handler_t **table, *handler;
    uint32_t pc_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;

    pc_start = tb->pc;
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    ctx.nip = pc_start;
    ctx.tb = tb;
    ctx.exception = EXCP_NONE;
#if defined(CONFIG_USER_ONLY)
    ctx.mem_idx = 0;
#else
    ctx.supervisor = 1 - msr_pr;
    ctx.mem_idx = (1 - msr_pr);
#endif
#if defined (DO_SINGLE_STEP)
    /* Single step trace mode */
    msr_se = 1;
#endif
    /* Set env in case of segfault during code fetch */
    while (ctx.exception == EXCP_NONE && gen_opc_ptr < gen_opc_end) {
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
        ctx.opcode = ldl_code((void *)ctx.nip);
#if defined PPC_DEBUG_DISAS
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "translate opcode %08x (%02x %02x %02x)\n",
                    ctx.opcode, opc1(ctx.opcode), opc2(ctx.opcode),
                    opc3(ctx.opcode));
        }
#endif
        ctx.nip += 4;
        table = ppc_opcodes;
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
             ctx.exception != EXCP_SYSCALL && ctx.exception != EXCP_RFI &&
             ctx.exception != EXCP_TRAP)) {
            RET_EXCP(ctxp, EXCP_TRACE, 0);
        }
        /* if we reach a page boundary, stop generation */
        if ((ctx.nip & (TARGET_PAGE_SIZE - 1)) == 0) {
            RET_EXCP(ctxp, EXCP_BRANCH, 0);
    }
    }
    if (ctx.exception == EXCP_NONE) {
        gen_op_b((unsigned long)ctx.tb, ctx.nip);
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
        cpu_ppc_dump_state(env, logfile, 0);
    }
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol((void *)pc_start));
	disas(logfile, (void *)pc_start, ctx.nip - pc_start, 0, 0);
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
