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
#include "dyngen-exec.h"
#include "cpu.h"
#include "exec.h"
#include "disas.h"

//#define DO_SINGLE_STEP
//#define DO_STEP_FLUSH

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

#include "gen-op.h"

typedef void (GenOpFunc)(void);

#define GEN8(func, NAME) \
static GenOpFunc *NAME ## _table [8] = {\
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,\
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,\
};\
static inline void func(int n)\
{\
    NAME ## _table[n]();\
}

#define GEN32(func, NAME) \
static GenOpFunc *NAME ## _table [32] = {\
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,\
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,\
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,\
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,\
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,\
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,\
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,\
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,\
};\
static inline void func(int n)\
{\
    NAME ## _table[n]();\
}

GEN8(gen_op_load_crf_T0, gen_op_load_crf_T0_crf)
GEN8(gen_op_load_crf_T1, gen_op_load_crf_T1_crf)
GEN8(gen_op_store_T0_crf, gen_op_store_T0_crf_crf)
GEN8(gen_op_store_T1_crf, gen_op_store_T1_crf_crf)

GEN32(gen_op_load_gpr_T0, gen_op_load_gpr_T0_gpr)
GEN32(gen_op_load_gpr_T1, gen_op_load_gpr_T1_gpr)
GEN32(gen_op_load_gpr_T2, gen_op_load_gpr_T2_gpr)

GEN32(gen_op_store_T0_gpr, gen_op_store_T0_gpr_gpr)
GEN32(gen_op_store_T1_gpr, gen_op_store_T1_gpr_gpr)
GEN32(gen_op_store_T2_gpr, gen_op_store_T2_gpr_gpr)

GEN32(gen_op_load_FT0_fpr, gen_op_load_FT0_fpr)
GEN32(gen_op_store_FT0_fpr, gen_op_store_FT0_fpr)

static uint8_t  spr_access[1024 / 2];

/* internal defines */
typedef struct DisasContext {
    struct TranslationBlock *tb;
    uint32_t *nip;
    uint32_t opcode;
    int exception;
    int retcode;
    /* Time base */
    uint32_t tb_offset;
    int supervisor;
} DisasContext;

typedef struct opc_handler_t {
    /* invalid bits */
    uint32_t inval;
    /* handler */
    void (*handler)(DisasContext *ctx);
} opc_handler_t;

#define SET_RETVAL(n)                                                         \
do {                                                                          \
    if ((n) != 0) {                                                           \
        ctx->exception = (n);                                                 \
    }                                                                         \
    return;                                                                   \
} while (0)

#define GET_RETVAL(func, __opcode)                                            \
({                                                                            \
    (func)(&ctx);                                                             \
    ctx.exception;                                                            \
})

#define GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                      \
static void gen_##name (DisasContext *ctx);                                   \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type);                              \
static void gen_##name (DisasContext *ctx)

/* Instruction types */
enum {
    PPC_INTEGER  = 0x0001, /* CPU has integer operations instructions        */
    PPC_FLOAT    = 0x0002, /* CPU has floating point operations instructions */
    PPC_FLOW     = 0x0004, /* CPU has flow control instructions              */
    PPC_MEM      = 0x0008, /* CPU has virtual memory instructions            */
    PPC_MISC     = 0x0010, /* CPU has spr/msr access instructions            */
    PPC_EXTERN   = 0x0020, /* CPU has external control instructions          */
    PPC_SEGMENT  = 0x0040, /* CPU has memory segment instructions            */
};

typedef struct opcode_t {
    unsigned char opc1, opc2, opc3;
    uint32_t type;
    opc_handler_t handler;
} opcode_t;

/* XXX: move that elsewhere */
extern FILE *logfile;
extern int loglevel;

/* XXX: shouldn't stay all alone here ! */
static int reserve = 0;

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

EXTRACT_HELPER(CRM, 12, 8);
EXTRACT_HELPER(FM, 17, 8);
EXTRACT_HELPER(SR, 16, 4);
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

#define GEN_OPCODE(name, op1, op2, op3, invl, _typ)                           \
__attribute__ ((section(".opcodes"), unused))                                 \
static opcode_t opc_##name = {                                                \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .type = _typ,                                                             \
    .handler = {                                                              \
        .inval   = invl,                                                      \
        .handler = &gen_##name,                                               \
    },                                                                        \
}

#define GEN_OPCODE_MARK(name)                                                 \
__attribute__ ((section(".opcodes"), unused))                                 \
static opcode_t opc_##name = {                                                \
    .opc1 = 0xFF,                                                             \
    .opc2 = 0xFF,                                                             \
    .opc3 = 0xFF,                                                             \
    .type = 0x00,                                                             \
    .handler = {                                                              \
        .inval   = 0x00000000,                                                \
        .handler = NULL,                                                      \
    },                                                                        \
}

/* Start opcode list */
GEN_OPCODE_MARK(start);

/* Invalid instruction */
GEN_HANDLER(invalid, 0x00, 0x00, 0x00, 0xFFFFFFFF, 0)
{
    /* Branch to next instruction to force nip update */
    gen_op_b((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_INVAL);
}

static opc_handler_t invalid_handler = {
    .inval   = 0xFFFFFFFF,
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
    SET_RETVAL(0);                                                            \
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
    SET_RETVAL(0);                                                            \
}

#define __GEN_INT_ARITH1(name, opc1, opc2, opc3)                              \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, PPC_INTEGER)                  \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0();                                                     \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}
#define __GEN_INT_ARITH1_O(name, opc1, opc2, opc3)                            \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, PPC_INTEGER)                  \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    if (Rc(ctx->opcode) != 0)                                                 \
        gen_op_set_Rc0_ov();                                                  \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
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
    SET_RETVAL(0);
}
/* addic */
GEN_HANDLER(addic, 0x0C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_addic(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}
/* addic. */
GEN_HANDLER(addic_, 0x0D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_addic(SIMM(ctx->opcode));
    gen_op_set_Rc0();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
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
    SET_RETVAL(0);
}
/* mulli */
GEN_HANDLER(mulli, 0x07, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_mulli(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}
/* subfic */
GEN_HANDLER(subfic, 0x08, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_subfic(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}

/***                           Integer comparison                          ***/
#define GEN_CMP(name, opc)                                                    \
GEN_HANDLER(name, 0x1F, 0x00, opc, 0x00400000, PPC_INTEGER)                   \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_crf(crfD(ctx->opcode));                                   \
    SET_RETVAL(0);                                                            \
}

/* cmp */
GEN_CMP(cmp, 0x00);
/* cmpi */
GEN_HANDLER(cmpi, 0x0B, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_cmpi(SIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
    SET_RETVAL(0);
}
/* cmpl */
GEN_CMP(cmpl, 0x01);
/* cmpli */
GEN_HANDLER(cmpli, 0x0A, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_cmpli(UIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
    SET_RETVAL(0);
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
    SET_RETVAL(0);                                                            \
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
    SET_RETVAL(0);                                                            \
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
    SET_RETVAL(0);
}
/* andis. */
GEN_HANDLER(andis_, 0x1D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_andi_(UIMM(ctx->opcode) << 16);
    gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    SET_RETVAL(0);
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
GEN_LOGICAL2(or, 0x0D);
/* orc & orc. */
GEN_LOGICAL2(orc, 0x0C);
/* xor & xor. */
GEN_LOGICAL2(xor, 0x09);
/* ori */
GEN_HANDLER(ori, 0x18, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

#if 0
    if (uimm == 0) {
        if (rA(ctx->opcode) != rS(ctx->opcode)) {
            gen_op_load_gpr_T0(rS(ctx->opcode));
            gen_op_store_T0_gpr(rA(ctx->opcode));
        }
    } else
#endif
    {
        gen_op_load_gpr_T0(rS(ctx->opcode));
        gen_op_ori(uimm);
        gen_op_store_T0_gpr(rA(ctx->opcode));
    }
    SET_RETVAL(0);
}
/* oris */
GEN_HANDLER(oris, 0x19, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t uimm = UIMM(ctx->opcode);

#if 0
    if (uimm == 0) {
        if (rA(ctx->opcode) != rS(ctx->opcode)) {
            gen_op_load_gpr_T0(rS(ctx->opcode));
            gen_op_store_T0_gpr(rA(ctx->opcode));
        }
    } else
#endif
    {
        gen_op_load_gpr_T0(rS(ctx->opcode));
        gen_op_ori(uimm << 16);
        gen_op_store_T0_gpr(rA(ctx->opcode));
    }
    SET_RETVAL(0);
}
/* xori */
GEN_HANDLER(xori, 0x1A, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_xori(UIMM(ctx->opcode));
    gen_op_store_T0_gpr(rA(ctx->opcode));
    SET_RETVAL(0);
}

/* xoris */
GEN_HANDLER(xoris, 0x1B, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_xori(UIMM(ctx->opcode) << 16);
    gen_op_store_T0_gpr(rA(ctx->opcode));
    SET_RETVAL(0);
}

/***                             Integer rotate                            ***/
/* rlwimi & rlwimi. */
GEN_HANDLER(rlwimi, 0x14, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_rlwimi(SH(ctx->opcode), MASK(mb, me), ~MASK(mb, me));
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    SET_RETVAL(0);
}
/* rlwinm & rlwinm. */
GEN_HANDLER(rlwinm, 0x15, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me, sh;
    
    sh = SH(ctx->opcode);
    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (loglevel > 0) {
        fprintf(logfile, "%s sh=%u mb=%u me=%u MASK=0x%08x\n",
                __func__, sh, mb, me, MASK(mb, me));
    }
    if (mb == 0) {
        if (me == 31) {
            gen_op_rotlwi(sh);
            goto store;
        } else if (me == (31 - sh)) {
            gen_op_slwi(sh);
            goto store;
        } else if (sh == 0) {
            gen_op_andi_(MASK(0, me));
            goto store;
        }
    } else if (me == 31) {
        if (sh == (32 - mb)) {
            gen_op_srwi(mb);
            goto store;
        } else if (sh == 0) {
            gen_op_andi_(MASK(mb, 31));
            goto store;
        }
    }
    gen_op_rlwinm(sh, MASK(mb, me));
store:
    if (Rc(ctx->opcode) != 0)
        gen_op_set_Rc0();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    SET_RETVAL(0);
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
    SET_RETVAL(0);
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
    SET_RETVAL(0);
}
/* srw & srw. */
__GEN_LOGICAL2(srw, 0x18, 0x10);

/***                       Floating-Point arithmetic                       ***/
/* fadd */
GEN_HANDLER(fadd, 0x3F, 0x15, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fadds */
GEN_HANDLER(fadds, 0x3B, 0x15, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fdiv */
GEN_HANDLER(fdiv, 0x3F, 0x12, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fdivs */
GEN_HANDLER(fdivs, 0x3B, 0x12, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmul */
GEN_HANDLER(fmul, 0x3F, 0x19, 0xFF, 0x0000F800, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmuls */
GEN_HANDLER(fmuls, 0x3B, 0x19, 0xFF, 0x0000F800, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fres */
GEN_HANDLER(fres, 0x3B, 0x18, 0xFF, 0x001807C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* frsqrte */
GEN_HANDLER(frsqrte, 0x3F, 0x1A, 0xFF, 0x001807C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fsel */
GEN_HANDLER(fsel, 0x3F, 0x17, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fsub */
GEN_HANDLER(fsub, 0x3F, 0x14, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fsubs */
GEN_HANDLER(fsubs, 0x3B, 0x14, 0xFF, 0x000007C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* Optional: */
/* fsqrt */
GEN_HANDLER(fsqrt, 0x3F, 0x16, 0xFF, 0x001807C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fsqrts */
GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001807C0, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd */
GEN_HANDLER(fmadd, 0x3F, 0x1D, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmadds */
GEN_HANDLER(fmadds, 0x3B, 0x1D, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmsub */
GEN_HANDLER(fmsub, 0x3F, 0x1C, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmsubs */
GEN_HANDLER(fmsubs, 0x3B, 0x1C, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fnmadd */
GEN_HANDLER(fnmadd, 0x3F, 0x1F, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fnmadds */
GEN_HANDLER(fnmadds, 0x3B, 0x1F, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fnmsub */
GEN_HANDLER(fnmsub, 0x3F, 0x1E, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fnmsubs */
GEN_HANDLER(fnmsubs, 0x3B, 0x1E, 0xFF, 0x00000000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                     Floating-Point round & convert                    ***/
/* fctiw */
GEN_HANDLER(fctiw, 0x3F, 0x0E, 0xFF, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fctiwz */
GEN_HANDLER(fctiwz, 0x3F, 0x0F, 0xFF, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* frsp */
GEN_HANDLER(frsp, 0x3F, 0x0C, 0xFF, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                         Floating-Point compare                        ***/
/* fcmpo */
GEN_HANDLER(fcmpo, 0x3F, 0x00, 0x00, 0x00600001, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fcmpu */
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                  Floating-Point status & ctrl register                ***/
/* mcrfs */
GEN_HANDLER(mcrfs, 0x3F, 0x00, 0x02, 0x0063F801, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mffs */
GEN_HANDLER(mffs, 0x3F, 0x07, 0x12, 0x001FF800, PPC_FLOAT)
{
    gen_op_load_fpscr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (Rc(ctx->opcode)) {
        /* Update CR1 */
    }
    SET_RETVAL(0);
}

/* mtfsb0 */
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mtfsb1 */
GEN_HANDLER(mtfsb1, 0x3F, 0x06, 0x01, 0x001FF800, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mtfsf */
GEN_HANDLER(mtfsf, 0x3F, 0x07, 0x16, 0x02010000, PPC_FLOAT)
{
    gen_op_load_gpr_T0(rB(ctx->opcode));
    gen_op_store_fpscr(FM(ctx->opcode));
    if (Rc(ctx->opcode)) {
        /* Update CR1 */
    }
    SET_RETVAL(0);
}

/* mtfsfi */
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006f0800, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                             Integer load                              ***/
#define GEN_ILDZ(width, opc)                                                  \
GEN_HANDLER(l##width, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)               \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_l##width##_z(simm);                                            \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_l##width (simm);                                               \
    }                                                                         \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ILDZU(width, opc)                                                 \
GEN_HANDLER(l##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)            \
{                                                                             \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode))                                   \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_l##width(SIMM(ctx->opcode));                                       \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ILDZUX(width, opc)                                                \
GEN_HANDLER(l##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_INTEGER)           \
{                                                                             \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode))                                   \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_l##width##x();                                                     \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ILDZX(width, opc2, opc3)                                          \
GEN_HANDLER(l##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_INTEGER)           \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
        gen_op_l##width##x_z();                                               \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_l##width##x();                                                 \
    }                                                                         \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ILD(width, op)                                                    \
GEN_ILDZ(width, op | 0x20)                                                    \
GEN_ILDZU(width, op | 0x21)                                                   \
GEN_ILDZUX(width, op | 0x01)                                                  \
GEN_ILDZX(width, 0x17, op | 0x00)

/* lbz lbzu lbzux lbzx */
GEN_ILD(bz, 0x02);
/* lha lhau lhaux lhax */
GEN_ILD(ha, 0x0A);
/* lhz lhzu lhzux lhzx */
GEN_ILD(hz, 0x08);
/* lwz lwzu lwzux lwzx */
GEN_ILD(wz, 0x00);

/***                              Integer store                            ***/
#define GEN_IST(width, opc)                                                   \
GEN_HANDLER(st##width, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)              \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rS(ctx->opcode));                                  \
        gen_op_st##width##_z(simm);                                           \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rS(ctx->opcode));                                  \
        gen_op_st##width(simm);                                               \
    }                                                                         \
    SET_RETVAL(0);                                                            \
}

#define GEN_ISTU(width, opc)                                                  \
GEN_HANDLER(st##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)           \
{                                                                             \
    if (rA(ctx->opcode) == 0)                                                 \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    gen_op_st##width(SIMM(ctx->opcode));                                      \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ISTUX(width, opc)                                                 \
GEN_HANDLER(st##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_INTEGER)          \
{                                                                             \
    if (rA(ctx->opcode) == 0)                                                 \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_load_gpr_T2(rS(ctx->opcode));                                      \
    gen_op_st##width##x();                                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_ISTX(width, opc2, opc3)                                           \
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_INTEGER)          \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rS(ctx->opcode));                                  \
        gen_op_st##width##x_z();                                              \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_load_gpr_T2(rS(ctx->opcode));                                  \
        gen_op_st##width##x();                                                \
    }                                                                         \
    SET_RETVAL(0);                                                            \
}

#define GEN_ISTO(width, opc)                                                  \
GEN_IST(width, opc | 0x20)                                                    \
GEN_ISTU(width, opc | 0x21)                                                   \
GEN_ISTUX(width, opc | 0x01)                                                  \
GEN_ISTX(width, 0x17, opc | 0x00)

/* stb stbu stbux stbx */
GEN_ISTO(b, 0x06);
/* sth sthu sthux sthx */
GEN_ISTO(h, 0x0C);
/* stw stwu stwux stwx */
GEN_ISTO(w, 0x04);

/***                Integer load and store with byte reverse               ***/
/* lhbrx */
GEN_ILDZX(hbr, 0x16, 0x18);
/* lwbrx */
GEN_ILDZX(wbr, 0x16, 0x10);
/* sthbrx */
GEN_ISTX(hbr, 0x16, 0x1C);
/* stwbrx */
GEN_ISTX(wbr, 0x16, 0x14);

/***                    Integer load and store multiple                    ***/
/* lmw */
GEN_HANDLER(lmw, 0x2E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
    gen_op_lmw(rD(ctx->opcode), SIMM(ctx->opcode));
    SET_RETVAL(0);
}

/* stmw */
GEN_HANDLER(stmw, 0x2F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
    gen_op_stmw(rS(ctx->opcode), SIMM(ctx->opcode));
    SET_RETVAL(0);
}

/***                    Integer load and store strings                     ***/
/* lswi */
GEN_HANDLER(lswi, 0x1F, 0x15, 0x12, 0x00000001, PPC_INTEGER)
{
    int nb = NB(ctx->opcode);
    int start = rD(ctx->opcode);
    int nr;

    if (nb == 0)
        nb = 32;
    nr = nb / 4;
    if ((start + nr) > 32) {
        /* handle wrap around r0 */
        if (rA(ctx->opcode) == 0) {
            gen_op_set_T0(0);
        } else {
            gen_op_load_gpr_T0(rA(ctx->opcode));
        }
        gen_op_lswi(start, 4 * (32 - start));
        nb -= 4 * (32 - start);
        start = 0;
    }
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
    gen_op_lswi(start, nb);
    SET_RETVAL(0);
}

/* lswx */
GEN_HANDLER(lswx, 0x1F, 0x15, 0x10, 0x00000001, PPC_INTEGER)
{
    gen_op_load_xer_bc();
    gen_op_load_gpr_T1(rB(ctx->opcode));
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T2(0);
    } else {
        gen_op_load_gpr_T2(rA(ctx->opcode));
    }
    gen_op_lswx(rD(ctx->opcode));
    SET_RETVAL(0);
}

/* stswi */
GEN_HANDLER(stswi, 0x1F, 0x15, 0x16, 0x00000001, PPC_INTEGER)
{
    int nb = NB(ctx->opcode);
    int start = rS(ctx->opcode);
    int nr;

    if (nb == 0)
        nb = 32;
    nr = nb / 4;
    if ((start + nr) > 32) {
        /* handle wrap around r0 */
        if (rA(ctx->opcode) == 0) {
            gen_op_set_T0(0);
        } else {
            gen_op_load_gpr_T0(rA(ctx->opcode));
        }
        gen_op_stswi(start, 4 * (32 - start));
        nb -= 4 * (32 - start);
        start = 0;
    }
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T0(0);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
    gen_op_stswi(start, nb);
    SET_RETVAL(0);
}

/* stswx */
GEN_HANDLER(stswx, 0x1F, 0x15, 0x14, 0x00000001, PPC_INTEGER)
{
    gen_op_load_xer_bc();
    gen_op_load_gpr_T1(rB(ctx->opcode));
    if (rA(ctx->opcode) == 0) {
        gen_op_set_T2(0);
    } else {
        gen_op_load_gpr_T2(rA(ctx->opcode));
    }
    gen_op_stswx(rS(ctx->opcode));
    SET_RETVAL(0);
}

/***                        Memory synchronisation                         ***/
/* eieio */
GEN_HANDLER(eieio, 0x1F, 0x16, 0x1A, 0x03FF0801, PPC_MEM)
{
    /* Do a branch to next instruction */
    gen_op_b((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_BRANCH);
}

/* isync */
GEN_HANDLER(isync, 0x13, 0x16, 0xFF, 0x03FF0801, PPC_MEM)
{
    /* Do a branch to next instruction */
    gen_op_b((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_BRANCH);
}

/* lwarx */
GEN_HANDLER(lwarx, 0x1F, 0x14, 0xFF, 0x00000001, PPC_MEM)
{
    reserve = 1;
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
        gen_op_lwzx_z();
        gen_op_set_reservation();
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_lwzx();
        gen_op_set_reservation();
    }
    gen_op_store_T1_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}

/* stwcx. */
GEN_HANDLER(stwcx_, 0x1F, 0x16, 0x04, 0x00000000, PPC_MEM)
{
    if (reserve == 0) {
        gen_op_reset_Rc0();
    } else {
        if (rA(ctx->opcode) == 0) {
            gen_op_load_gpr_T0(rB(ctx->opcode));
            gen_op_load_gpr_T1(rS(ctx->opcode));
            gen_op_stwx_z();
        } else {
            gen_op_load_gpr_T0(rA(ctx->opcode));
            gen_op_load_gpr_T1(rB(ctx->opcode));
            gen_op_load_gpr_T2(rS(ctx->opcode));
            gen_op_stwx();
        }
        gen_op_set_Rc0_1();
        gen_op_reset_reservation();
    }
    SET_RETVAL(0);
}

/* sync */
GEN_HANDLER(sync, 0x1F, 0x16, 0x12, 0x03FF0801, PPC_MEM)
{
    /* Do a branch to next instruction */
    gen_op_b((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_BRANCH);
}

/***                         Floating-point load                           ***/
#define GEN_LF(width, opc)                                                    \
GEN_HANDLER(lf##width, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)                \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_lf##width##_z_FT0(simm);                          \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_lf##width##_FT0(simm);                              \
    }                                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));\
    SET_RETVAL(0);                                                            \
}

#define GEN_LFU(width, opc)                                                   \
GEN_HANDLER(lf##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)             \
{                                                                             \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode))                                   \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_lf##width##_FT0(SIMM(ctx->opcode));                     \
    gen_op_store_FT0_fpr(rD(ctx->opcode));\
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_LFUX(width, opc)                                                  \
GEN_HANDLER(lf##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    if (rA(ctx->opcode) == 0 ||                                               \
        rA(ctx->opcode) == rD(ctx->opcode))                                   \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_lf##width##x_FT0();                                     \
    gen_op_store_FT0_fpr(rD(ctx->opcode));\
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_LFX(width, opc)                                                   \
GEN_HANDLER(lf##width##x, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)             \
{                                                                             \
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
        gen_op_lf##width##x_z_FT0();                               \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_lf##width##x_FT0();                                 \
    }                                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));\
    SET_RETVAL(0);                                                            \
}

#define GEN_LDF(width, opc)                                                   \
GEN_LF(width, opc | 0x20)                                                     \
GEN_LFU(width, opc | 0x21)                                                    \
GEN_LFUX(width, opc | 0x01)                                                   \
GEN_LFX(width, opc | 0x00)

/* lfd lfdu lfdux lfdx */
GEN_LDF(d, 0x12);
/* lfs lfsu lfsux lfsx */
GEN_LDF(s, 0x10);

/***                         Floating-point store                          ***/
#define GEN_STF(width, opc)                                                   \
GEN_HANDLER(stf##width, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)               \
{                                                                             \
    uint32_t simm = SIMM(ctx->opcode);                                        \
    gen_op_load_FT0_fpr(rS(ctx->opcode));\
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_stf##width##_z_FT0(simm);                         \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_stf##width##_FT0(simm);                             \
    }                                                                         \
    SET_RETVAL(0);                                                            \
}

#define GEN_STFU(width, opc)                                                  \
GEN_HANDLER(stf##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)            \
{                                                                             \
    if (rA(ctx->opcode) == 0)                                                 \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_FT0_fpr(rS(ctx->opcode));\
    gen_op_stf##width##_FT0(SIMM(ctx->opcode));                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_STFUX(width, opc)                                                 \
GEN_HANDLER(stf##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)           \
{                                                                             \
    if (rA(ctx->opcode) == 0)                                                 \
        SET_RETVAL(EXCP_INVAL);                                               \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_load_FT0_fpr(rS(ctx->opcode));\
    gen_op_stf##width##x_FT0();                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    SET_RETVAL(0);                                                            \
}

#define GEN_STFX(width, opc)                                                  \
GEN_HANDLER(stf##width##x, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    gen_op_load_FT0_fpr(rS(ctx->opcode));\
    if (rA(ctx->opcode) == 0) {                                               \
        gen_op_load_gpr_T0(rB(ctx->opcode));                                  \
        gen_op_stf##width##x_z_FT0();                              \
    } else {                                                                  \
        gen_op_load_gpr_T0(rA(ctx->opcode));                                  \
        gen_op_load_gpr_T1(rB(ctx->opcode));                                  \
        gen_op_stf##width##x_FT0();                                \
    }                                                                         \
    SET_RETVAL(0);                                                            \
}

#define GEN_STOF(width, opc)                                                  \
GEN_STF(width, opc | 0x20)                                                    \
GEN_STFU(width, opc | 0x21)                                                   \
GEN_STFUX(width, opc | 0x01)                                                  \
GEN_STFX(width, opc | 0x00)

/* stfd stfdu stfdux stfdx */
GEN_STOF(d, 0x16);
/* stfs stfsu stfsux stfsx */
GEN_STOF(s, 0x14);

/* Optional: */
/* stfiwx */
GEN_HANDLER(stfiwx, 0x1F, 0x17, 0x1E, 0x00000001, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                         Floating-point move                           ***/
/* fabs */
GEN_HANDLER(fabs, 0x3F, 0x08, 0x08, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fmr */
GEN_HANDLER(fmr, 0x3F, 0x08, 0x02, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fnabs */
GEN_HANDLER(fnabs, 0x3F, 0x08, 0x04, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* fneg */
GEN_HANDLER(fneg, 0x3F, 0x08, 0x01, 0x001F0000, PPC_FLOAT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                                Branch                                 ***/
#define GEN_BCOND(name, opc1, opc2, opc3, prologue,                           \
   bl_ctr,       b_ctr,       bl_ctrz,       b_ctrz,       b,                 \
   bl_ctr_true,  b_ctr_true,  bl_ctrz_true,  b_ctrz_true,  bl_true,  b_true,  \
   bl_ctr_false, b_ctr_false, bl_ctrz_false, b_ctrz_false, bl_false, b_false) \
GEN_HANDLER(name, opc1, opc2, opc3, 0x00000000, PPC_FLOW)                     \
{                                                                             \
    __attribute__ ((unused)) uint32_t target;                                 \
    uint32_t bo = BO(ctx->opcode);                                            \
    uint32_t bi = BI(ctx->opcode);                                            \
    uint32_t mask;                                                            \
    prologue;                                                                 \
    if ((bo & 0x4) == 0)                                                      \
        gen_op_dec_ctr();                                                     \
    if (bo & 0x10) {                                                          \
        /* No CR condition */                                                 \
        switch (bo & 0x6) {                                                   \
        case 0:                                                               \
            if (LK(ctx->opcode)) {                                            \
                bl_ctr;                                                       \
            } else {                                                          \
                b_ctr;                                                        \
            }                                                                 \
            break;                                                            \
        case 2:                                                               \
            if (LK(ctx->opcode)) {                                            \
                bl_ctrz;                                                      \
            } else {                                                          \
                b_ctrz;                                                       \
            }                                                                 \
            break;                                                            \
        case 4:                                                               \
        case 6:                                                               \
            b;                                                                \
            if (LK(ctx->opcode))                                              \
                gen_op_load_lr((uint32_t)ctx->nip);                           \
            break;                                                            \
        default:                                                              \
            printf("ERROR: %s: unhandled ba case (%d)\n", __func__, bo);      \
            SET_RETVAL(EXCP_INVAL);                                           \
            break;                                                            \
        }                                                                     \
    } else {                                                                  \
        mask = 1 << (3 - (bi & 0x03));                                        \
        gen_op_load_crf_T0(bi >> 2);                                          \
        if (bo & 0x8) {                                                       \
            switch (bo & 0x6) {                                               \
            case 0:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_ctr_true;                                              \
                } else {                                                      \
                    b_ctr_true;                                               \
                }                                                             \
                break;                                                        \
            case 2:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_ctrz_true;                                             \
                } else {                                                      \
                    b_ctrz_true;                                              \
                }                                                             \
                break;                                                        \
            case 4:                                                           \
            case 6:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_true;                                                  \
                } else {                                                      \
                    b_true;                                                   \
                }                                                             \
                break;                                                        \
            default:                                                          \
                printf("ERROR: %s: unhandled b case (%d)\n", __func__, bo);   \
                SET_RETVAL(EXCP_INVAL);                                       \
                break;                                                        \
            }                                                                 \
        } else {                                                              \
            switch (bo & 0x6) {                                               \
            case 0:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_ctr_false;                                             \
                } else {                                                      \
                    b_ctr_false;                                              \
                }                                                             \
                break;                                                        \
            case 2:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_ctrz_false;                                            \
                } else {                                                      \
                    b_ctrz_false;                                             \
                }                                                             \
                break;                                                        \
            case 4:                                                           \
            case 6:                                                           \
                if (LK(ctx->opcode)) {                                        \
                    bl_false;                                                 \
                } else {                                                      \
                    b_false;                                                  \
                }                                                             \
                break;                                                        \
            default:                                                          \
                printf("ERROR: %s: unhandled bn case (%d)\n", __func__, bo);  \
                SET_RETVAL(EXCP_INVAL);                                       \
                break;                                                        \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    SET_RETVAL(EXCP_BRANCH);                                                  \
}

/* b ba bl bla */
GEN_HANDLER(b, 0x12, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    uint32_t li = s_ext24(LI(ctx->opcode)), target;

    if (AA(ctx->opcode) == 0)
        target = (uint32_t)ctx->nip + li - 4;
    else
        target = s_ext24(LI(ctx->opcode));
    gen_op_b(target);
    if (LK(ctx->opcode))
        gen_op_load_lr((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_BRANCH);
}

/* bc bca bcl bcla */
GEN_BCOND(bc, 0x10, 0xFF, 0xFF,
          do {
              uint32_t li = s_ext16(BD(ctx->opcode));
              if (AA(ctx->opcode) == 0) {
                  target = (uint32_t)ctx->nip + li - 4;
              } else {
                  target = li;
              }
          } while (0),
          gen_op_bl_ctr((uint32_t)ctx->nip, target),
          gen_op_b_ctr((uint32_t)ctx->nip, target),
          gen_op_bl_ctrz((uint32_t)ctx->nip, target),
          gen_op_b_ctrz((uint32_t)ctx->nip, target),
          gen_op_b(target),
          gen_op_bl_ctr_true((uint32_t)ctx->nip, target, mask),
          gen_op_b_ctr_true((uint32_t)ctx->nip, target, mask),
          gen_op_bl_ctrz_true((uint32_t)ctx->nip, target, mask),
          gen_op_b_ctrz_true((uint32_t)ctx->nip, target, mask),
          gen_op_bl_true((uint32_t)ctx->nip, target, mask),
          gen_op_b_true((uint32_t)ctx->nip, target, mask),
          gen_op_bl_ctr_false((uint32_t)ctx->nip, target, mask),
          gen_op_b_ctr_false((uint32_t)ctx->nip, target, mask),
          gen_op_bl_ctrz_false((uint32_t)ctx->nip, target, mask),
          gen_op_b_ctrz_false((uint32_t)ctx->nip, target, mask),
          gen_op_bl_false((uint32_t)ctx->nip, target, mask),
          gen_op_b_false((uint32_t)ctx->nip, target, mask));

/* bcctr bcctrl */
GEN_BCOND(bcctr, 0x13, 0x10, 0x10, do { } while (0),
          gen_op_bctrl_ctr((uint32_t)ctx->nip),
          gen_op_bctr_ctr((uint32_t)ctx->nip),
          gen_op_bctrl_ctrz((uint32_t)ctx->nip),
          gen_op_bctr_ctrz((uint32_t)ctx->nip),
          gen_op_bctr(),
          gen_op_bctrl_ctr_true((uint32_t)ctx->nip, mask),
          gen_op_bctr_ctr_true((uint32_t)ctx->nip, mask),
          gen_op_bctrl_ctrz_true((uint32_t)ctx->nip, mask),
          gen_op_bctr_ctrz_true((uint32_t)ctx->nip, mask),
          gen_op_bctrl_true((uint32_t)ctx->nip, mask),
          gen_op_bctr_true((uint32_t)ctx->nip, mask),
          gen_op_bctrl_ctr_false((uint32_t)ctx->nip, mask),
          gen_op_bctr_ctr_false((uint32_t)ctx->nip, mask),
          gen_op_bctrl_ctrz_false((uint32_t)ctx->nip, mask),
          gen_op_bctr_ctrz_false((uint32_t)ctx->nip, mask),
          gen_op_bctrl_false((uint32_t)ctx->nip, mask),
          gen_op_bctr_false((uint32_t)ctx->nip, mask))

/* bclr bclrl */
GEN_BCOND(bclr, 0x13, 0x10, 0x00, do { } while (0),
          gen_op_blrl_ctr((uint32_t)ctx->nip),
          gen_op_blr_ctr((uint32_t)ctx->nip),
          gen_op_blrl_ctrz((uint32_t)ctx->nip),
          gen_op_blr_ctrz((uint32_t)ctx->nip),
          gen_op_blr(),
          gen_op_blrl_ctr_true((uint32_t)ctx->nip, mask),
          gen_op_blr_ctr_true((uint32_t)ctx->nip, mask),
          gen_op_blrl_ctrz_true((uint32_t)ctx->nip, mask),
          gen_op_blr_ctrz_true((uint32_t)ctx->nip, mask),
          gen_op_blrl_true((uint32_t)ctx->nip, mask),
          gen_op_blr_true((uint32_t)ctx->nip, mask),
          gen_op_blrl_ctr_false((uint32_t)ctx->nip, mask),
          gen_op_blr_ctr_false((uint32_t)ctx->nip, mask),
          gen_op_blrl_ctrz_false((uint32_t)ctx->nip, mask),
          gen_op_blr_ctrz_false((uint32_t)ctx->nip, mask),
          gen_op_blrl_false((uint32_t)ctx->nip, mask),
          gen_op_blr_false((uint32_t)ctx->nip, mask))

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
    SET_RETVAL(0);                                                            \
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
    SET_RETVAL(0);
}

/***                           System linkage                              ***/
/* rfi (supervisor only) */
GEN_HANDLER(rfi, 0x13, 0x12, 0xFF, 0x03FF8001, PPC_FLOW)
{
    SET_RETVAL(EXCP_INVAL);
}

/* sc */
GEN_HANDLER(sc, 0x11, 0xFF, 0xFF, 0x03FFFFFD, PPC_FLOW)
{
    gen_op_b((uint32_t)ctx->nip);
    SET_RETVAL(EXCP_SYSCALL);
}

/***                                Trap                                   ***/
/* tw */
GEN_HANDLER(tw, 0x1F, 0x04, 0xFF, 0x00000001, PPC_FLOW)
{
    SET_RETVAL(EXCP_INVAL);
}

/* twi */
GEN_HANDLER(twi, 0x03, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                          Processor control                            ***/
static inline int check_spr_access (int spr, int rw, int supervisor)
{
    uint32_t rights = spr_access[spr >> 1] >> (4 * (spr & 1));

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
    SET_RETVAL(0);
}

/* mfcr */
GEN_HANDLER(mfcr, 0x1F, 0x13, 0x00, 0x001FF801, PPC_MISC)
{
    gen_op_load_cr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}

/* mfmsr */
GEN_HANDLER(mfmsr, 0x1F, 0x13, 0x02, 0x001FF801, PPC_MISC)
{
    if (!ctx->supervisor)
        SET_RETVAL(EXCP_PRIV);
    gen_op_load_msr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    SET_RETVAL(0);
}

/* mfspr */
GEN_HANDLER(mfspr, 0x1F, 0x13, 0x0A, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

    if (check_spr_access(sprn, 0, ctx->supervisor) == 0)
        SET_RETVAL(EXCP_PRIV);
    /* XXX: make this more generic */
    switch (sprn) {
    case SPR_ENCODE(1):
        if (loglevel > 0) {
            fprintf(logfile, "LOAD XER at %p\n", ctx->nip - 1);
        }
        gen_op_load_xer();
        break;
    case SPR_ENCODE(268):
        /* We need to update the time base before reading it */
        gen_op_update_tb(ctx->tb_offset);
        ctx->tb_offset = 0;
        break;
    case SPR_ENCODE(269):
        gen_op_update_tb(ctx->tb_offset);
        ctx->tb_offset = 0;
        break;
    default:
        gen_op_load_spr(sprn);
        break;
    }
    gen_op_store_T0_gpr(rD(ctx->opcode)); //
    SET_RETVAL(0);
}

/* mftb */
GEN_HANDLER(mftb, 0x1F, 0x13, 0x0B, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

    if (check_spr_access(sprn, 0, ctx->supervisor) == 0)
        SET_RETVAL(EXCP_PRIV);
    switch (sprn) {
    case SPR_ENCODE(268):
        /* We need to update the time base before reading it */
        gen_op_update_tb(ctx->tb_offset);
        ctx->tb_offset = 0;
        break;
    case SPR_ENCODE(269):
        gen_op_update_tb(ctx->tb_offset);
        ctx->tb_offset = 0;
        break;
    default:
        SET_RETVAL(EXCP_INVAL);
        break;
    }
    SET_RETVAL(0);
}

/* mtcrf */
GEN_HANDLER(mtcrf, 0x1F, 0x10, 0x04, 0x00100801, PPC_MISC)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_cr(CRM(ctx->opcode));
    SET_RETVAL(0);
}

/* mtmsr */
GEN_HANDLER(mtmsr, 0x1F, 0x12, 0x04, 0x001FF801, PPC_MISC)
{
    if (!ctx->supervisor)
        SET_RETVAL(EXCP_PRIV);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_msr();
    /* Must stop the translation as machine state (may have) changed */
    SET_RETVAL(EXCP_MTMSR);
}

/* mtspr */
GEN_HANDLER(mtspr, 0x1F, 0x13, 0x0E, 0x00000001, PPC_MISC)
{
    uint32_t sprn = SPR(ctx->opcode);

    if (check_spr_access(sprn, 1, ctx->supervisor) == 0)
        SET_RETVAL(EXCP_PRIV);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (sprn == SPR_ENCODE(1)) {
        gen_op_store_xer();
    } else {
        gen_op_store_spr(sprn);
    }
    SET_RETVAL(0);
}

/***                         Cache management                              ***/
/* For now, all those will be implemented as nop:
 * this is valid, regarding the PowerPC specs...
 */
/* dcbf */
GEN_HANDLER(dcbf, 0x1F, 0x16, 0x17, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* dcbi (Supervisor only) */
GEN_HANDLER(dcbi, 0x1F, 0x16, 0x1F, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* dcdst */
GEN_HANDLER(dcbst, 0x1F, 0x16, 0x0E, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* dcbt */
GEN_HANDLER(dcbt, 0x1F, 0x16, 0x01, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* dcbtst */
GEN_HANDLER(dcbtst, 0x1F, 0x16, 0x02, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* dcbz */
GEN_HANDLER(dcbz, 0x1F, 0x16, 0x08, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* icbi */
GEN_HANDLER(icbi, 0x1F, 0x16, 0x1E, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/* Optional: */
/* dcba */
GEN_HANDLER(dcba, 0x1F, 0x16, 0x07, 0x03E00001, PPC_MEM)
{
    SET_RETVAL(0);
}

/***                    Segment register manipulation                      ***/
/* Supervisor only: */
/* mfsr */
GEN_HANDLER(mfsr, 0x1F, 0x13, 0x12, 0x0010F801, PPC_SEGMENT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mfsrin */
GEN_HANDLER(mfsrin, 0x1F, 0x13, 0x14, 0x0010F001, PPC_SEGMENT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mtsr */
GEN_HANDLER(mtsr, 0x1F, 0x12, 0x02, 0x0010F801, PPC_SEGMENT)
{
    SET_RETVAL(EXCP_INVAL);
}

/* mtsrin */
GEN_HANDLER(mtsrin, 0x1F, 0x12, 0x07, 0x0010F001, PPC_SEGMENT)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                      Lookaside buffer management                      ***/
/* Optional & supervisor only: */
/* tlbia */
GEN_HANDLER(tlbia, 0x1F, 0x12, 0x0B, 0x03FFFC01, PPC_MEM)
{
    SET_RETVAL(EXCP_INVAL);
}

/* tlbie */
GEN_HANDLER(tlbie, 0x1F, 0x12, 0x09, 0x03FF8001, PPC_MEM)
{
    SET_RETVAL(EXCP_INVAL);
}

/* tlbsync */
GEN_HANDLER(tlbsync, 0x1F, 0x16, 0x11, 0x03FFFC01, PPC_MEM)
{
    SET_RETVAL(EXCP_INVAL);
}

/***                              External control                         ***/
/* Optional: */
/* eciwx */
GEN_HANDLER(eciwx, 0x1F, 0x16, 0x0D, 0x00000001, PPC_EXTERN)
{
    SET_RETVAL(EXCP_INVAL);
}

/* ecowx */
GEN_HANDLER(ecowx, 0x1F, 0x16, 0x09, 0x00000001, PPC_EXTERN)
{
    SET_RETVAL(EXCP_INVAL);
}

/* End opcode list */
GEN_OPCODE_MARK(end);

/*****************************************************************************/

#include <string.h>
extern FILE *stderr;
void free (void *p);
int fflush (FILE *f);

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

static int register_direct_insn (unsigned char idx, opc_handler_t *handler)
{
    if (insert_in_table(ppc_opcodes, idx, handler) < 0) {
        fprintf(stderr, "*** ERROR: opcode %02x already assigned in main "
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
            fprintf(stderr, "*** ERROR: unable to create indirect table "
                    "idx=%02x\n", idx1);
            return -1;
        }
    } else {
        if (!is_indirect_opcode(table[idx1])) {
            fprintf(stderr, "*** ERROR: idx %02x already assigned to a direct "
                    "opcode\n", idx1);
            return -1;
        }
    }
    if (handler != NULL &&
        insert_in_table(ind_table(table[idx1]), idx2, handler) < 0) {
        fprintf(stderr, "*** ERROR: opcode %02x already assigned in "
                "opcode table %02x\n", idx2, idx1);
        return -1;
    }

    return 0;
}

static int register_ind_insn (unsigned char idx1, unsigned char idx2,
                               opc_handler_t *handler)
{
    int ret;

    ret = register_ind_in_table(ppc_opcodes, idx1, idx2, handler);

    return ret;
}

static int register_dblind_insn (unsigned char idx1, unsigned char idx2,
                                  unsigned char idx3, opc_handler_t *handler)
{
    if (register_ind_in_table(ppc_opcodes, idx1, idx2, NULL) < 0) {
        fprintf(stderr, "*** ERROR: unable to join indirect table idx "
                "[%02x-%02x]\n", idx1, idx2);
        return -1;
    }
    if (register_ind_in_table(ind_table(ppc_opcodes[idx1]), idx2, idx3,
                              handler) < 0) {
        fprintf(stderr, "*** ERROR: unable to insert opcode "
                "[%02x-%02x-%02x]\n", idx1, idx2, idx3);
        return -1;
    }

    return 0;
}

static int register_insn (opcode_t *insn)
{
    if (insn->opc2 != 0xFF) {
        if (insn->opc3 != 0xFF) {
            if (register_dblind_insn(insn->opc1, insn->opc2, insn->opc3,
                                     &insn->handler) < 0)
                return -1;
        } else {
            if (register_ind_insn(insn->opc1, insn->opc2, &insn->handler) < 0)
                return -1;
        }
    } else {
        if (register_direct_insn(insn->opc1, &insn->handler) < 0)
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

static void fix_opcode_tables (void)
{
    if (test_opcode_table(ppc_opcodes, 0x40) == 0)
        fprintf(stderr, "*** WARNING: no opcode defined !\n");
}

#define SPR_RIGHTS(rw, priv) ((2 * (priv)) + (rw))
#define SPR_UR SPR_RIGHTS(0, 0)
#define SPR_UW SPR_RIGHTS(1, 0)
#define SPR_SR SPR_RIGHTS(0, 1)
#define SPR_SW SPR_RIGHTS(1, 1)

#define spr_set_rights(spr, rights)                            \
do {                                                           \
    spr_access[(spr) >> 1] |= ((rights) << (4 * ((spr) & 1))); \
} while (0)

static void init_spr_rights (void)
{
    /* XER    (SPR 1) */
    spr_set_rights(SPR_ENCODE(1), SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* LR     (SPR 8) */
    spr_set_rights(SPR_ENCODE(8), SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* CTR    (SPR 9) */
    spr_set_rights(SPR_ENCODE(9), SPR_UR | SPR_UW | SPR_SR | SPR_SW);
    /* TBL    (SPR 268) */
    spr_set_rights(SPR_ENCODE(268), SPR_UR | SPR_SR);
    /* TBU    (SPR 269) */
    spr_set_rights(SPR_ENCODE(269), SPR_UR | SPR_SR);
    /* DSISR  (SPR 18) */
    spr_set_rights(SPR_ENCODE(18), SPR_SR | SPR_SW);
    /* DAR    (SPR 19) */
    spr_set_rights(SPR_ENCODE(19), SPR_SR | SPR_SW);
    /* DEC    (SPR 22) */
    spr_set_rights(SPR_ENCODE(22), SPR_SR | SPR_SW);
    /* SDR1   (SPR 25) */
    spr_set_rights(SPR_ENCODE(25), SPR_SR | SPR_SW);
    /* SPRG0  (SPR 272) */
    spr_set_rights(SPR_ENCODE(272), SPR_SR | SPR_SW);
    /* SPRG1  (SPR 273) */
    spr_set_rights(SPR_ENCODE(273), SPR_SR | SPR_SW);
    /* SPRG2  (SPR 274) */
    spr_set_rights(SPR_ENCODE(274), SPR_SR | SPR_SW);
    /* SPRG3  (SPR 275) */
    spr_set_rights(SPR_ENCODE(275), SPR_SR | SPR_SW);
    /* ASR    (SPR 280) */
    spr_set_rights(SPR_ENCODE(281), SPR_SR | SPR_SW);
    /* EAR    (SPR 282) */
    spr_set_rights(SPR_ENCODE(282), SPR_SR | SPR_SW);
    /* IBAT0U (SPR 528) */
    spr_set_rights(SPR_ENCODE(528), SPR_SR | SPR_SW);
    /* IBAT0L (SPR 529) */
    spr_set_rights(SPR_ENCODE(529), SPR_SR | SPR_SW);
    /* IBAT1U (SPR 530) */
    spr_set_rights(SPR_ENCODE(530), SPR_SR | SPR_SW);
    /* IBAT1L (SPR 531) */
    spr_set_rights(SPR_ENCODE(531), SPR_SR | SPR_SW);
    /* IBAT2U (SPR 532) */
    spr_set_rights(SPR_ENCODE(532), SPR_SR | SPR_SW);
    /* IBAT2L (SPR 533) */
    spr_set_rights(SPR_ENCODE(533), SPR_SR | SPR_SW);
    /* IBAT3U (SPR 534) */
    spr_set_rights(SPR_ENCODE(534), SPR_SR | SPR_SW);
    /* IBAT3L (SPR 535) */
    spr_set_rights(SPR_ENCODE(535), SPR_SR | SPR_SW);
    /* DBAT0U (SPR 536) */
    spr_set_rights(SPR_ENCODE(536), SPR_SR | SPR_SW);
    /* DBAT0L (SPR 537) */
    spr_set_rights(SPR_ENCODE(537), SPR_SR | SPR_SW);
    /* DBAT1U (SPR 538) */
    spr_set_rights(SPR_ENCODE(538), SPR_SR | SPR_SW);
    /* DBAT1L (SPR 539) */
    spr_set_rights(SPR_ENCODE(539), SPR_SR | SPR_SW);
    /* DBAT2U (SPR 540) */
    spr_set_rights(SPR_ENCODE(540), SPR_SR | SPR_SW);
    /* DBAT2L (SPR 541) */
    spr_set_rights(SPR_ENCODE(541), SPR_SR | SPR_SW);
    /* DBAT3U (SPR 542) */
    spr_set_rights(SPR_ENCODE(542), SPR_SR | SPR_SW);
    /* DBAT3L (SPR 543) */
    spr_set_rights(SPR_ENCODE(543), SPR_SR | SPR_SW);
    /* DABR   (SPR 1013) */
    spr_set_rights(SPR_ENCODE(1013), SPR_SR | SPR_SW);
    /* FPECR  (SPR 1022) */
    spr_set_rights(SPR_ENCODE(1022), SPR_SR | SPR_SW);
    /* PIR    (SPR 1023) */
    spr_set_rights(SPR_ENCODE(1023), SPR_SR | SPR_SW);
    /* PVR    (SPR 287) */
    spr_set_rights(SPR_ENCODE(287), SPR_SR);
    /* TBL    (SPR 284) */
    spr_set_rights(SPR_ENCODE(284), SPR_SW);
    /* TBU    (SPR 285) */
    spr_set_rights(SPR_ENCODE(285), SPR_SW);
}

/* PPC "main stream" common instructions */
#define PPC_COMMON  (PPC_INTEGER | PPC_FLOAT | PPC_FLOW | PPC_MEM | \
                     PPC_MISC | PPC_EXTERN | PPC_SEGMENT)

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

static ppc_def_t ppc_defs[] =
{
    /* Fallback */
    {
        .pvr      = 0x00000000,
        .pvr_mask = 0x00000000,
        .proc     = &ppc_proc_common,
    },
};

static int create_ppc_proc (unsigned long pvr)
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
        if ((opc->type & flags) != 0)
            if (register_insn(opc) < 0) {
                fprintf(stderr, "*** ERROR initializing PPC instruction "
                        "0x%02x 0x%02x 0x%02x\n", opc->opc1, opc->opc2,
                        opc->opc3);
                return -1;
            }
    }
    fix_opcode_tables();

    return 0;
}

/*****************************************************************************/
uint32_t do_load_xer (void);

void cpu_ppc_dump_state(CPUPPCState *env, FILE *f, int flags)
{
    int i;

    if (loglevel > 0) {
        fprintf(logfile, "nip=0x%08x LR=0x%08x CTR=0x%08x XER=0x%08x\n",
                env->nip, env->LR, env->CTR, do_load_xer());
        for (i = 0; i < 32; i++) {
            if ((i & 7) == 0)
                fprintf(logfile, "GPR%02d:", i);
            fprintf(logfile, " %08x", env->gpr[i]);
            if ((i & 7) == 7)
                fprintf(logfile, "\n");
        }
        fprintf(logfile, "CR: 0x");
        for (i = 0; i < 8; i++)
            fprintf(logfile, "%01x", env->crf[i]);
        fprintf(logfile, "  [");
        for (i = 0; i < 8; i++) {
            char a = '-';
            
            if (env->crf[i] & 0x08)
                a = 'L';
            else if (env->crf[i] & 0x04)
                a = 'G';
            else if (env->crf[i] & 0x02)
                a = 'E';
            fprintf(logfile, " %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
        }
        fprintf(logfile, " ] ");
        fprintf(logfile, "TB: 0x%08x %08x\n", env->spr[SPR_ENCODE(269)],
                env->spr[SPR_ENCODE(268)]);
        for (i = 0; i < 16; i++) {
            if ((i & 3) == 0)
                fprintf(logfile, "FPR%02d:", i);
            fprintf(logfile, " %016llx", env->fpr[i]);
            if ((i & 3) == 3)
                fprintf(logfile, "\n");
        }
        fflush(logfile);
    }
}

CPUPPCState *cpu_ppc_init(void)
{
    CPUPPCState *env;

    cpu_exec_init();

    env = malloc(sizeof(CPUPPCState));
    if (!env)
        return NULL;
    memset(env, 0, sizeof(CPUPPCState));
    env->PVR = 0;
    if (create_ppc_proc(0) < 0)
        return NULL;
    init_spr_rights();

    return env;
}

void cpu_ppc_close(CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    free(env);
}

int gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                    int search_pc)
{
    DisasContext ctx;
    opc_handler_t **table, *handler;
    uint32_t pc_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;
    int ret = 0;

    pc_start = tb->pc;
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    ctx.nip = (uint32_t *)pc_start;
    ctx.tb_offset = 0;
    ctx.supervisor = msr_ip;
    ctx.tb = tb;
    ctx.exception = 0;

    while (ret == 0 && gen_opc_ptr < gen_opc_end) {
        if (search_pc) {
            if (loglevel > 0)
                fprintf(logfile, "Search PC...\n");
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
                gen_opc_pc[lj] = (uint32_t)ctx.nip;
                gen_opc_instr_start[lj] = 1;
            }
        }
        ctx.opcode = __be32_to_cpu(*ctx.nip);
#ifdef DEBUG_DISAS
        if (loglevel > 0) {
            fprintf(logfile, "----------------\n");
            fprintf(logfile, "%p: translate opcode %08x\n",
                    ctx.nip, ctx.opcode);
        }
#endif
        ctx.nip++;
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
        if ((ctx.opcode & handler->inval) != 0) {
            if (loglevel > 0) {
                if (handler->handler == &gen_invalid) {
                    fprintf(logfile, "invalid/unsupported opcode: "
                            "%02x -%02x - %02x (%08x)\n", opc1(ctx.opcode),
                            opc2(ctx.opcode), opc3(ctx.opcode), ctx.opcode);
                } else {
                    fprintf(logfile, "invalid bits: %08x for opcode: "
                            "%02x -%02x - %02x (%p)\n",
                            ctx.opcode & handler->inval, opc1(ctx.opcode),
                            opc2(ctx.opcode), opc3(ctx.opcode),
                            handler->handler);
                }
            }
            ret = GET_RETVAL(gen_invalid, ctx.opcode);
        } else {
            ret = GET_RETVAL(*(handler->handler), ctx.opcode);
        }
        ctx.tb_offset++;
#if defined (DO_SINGLE_STEP)
        break;
#endif
    }
#if defined (DO_STEP_FLUSH)
        tb_flush();
#endif
    /* We need to update the time base */
    if (!search_pc)
        gen_op_update_tb(ctx.tb_offset);
    /* If we are in step-by-step mode, do a branch to the next instruction
     * so the nip will be up-to-date
     */
#if defined (DO_SINGLE_STEP)
    if (ret == 0) {
        gen_op_b((uint32_t)ctx.nip);
        ret = EXCP_BRANCH;
    }
#endif
    /* If the exeption isn't a PPC one,
     * generate it now.
     */
    if (ret != EXCP_BRANCH) {
        gen_op_set_T0(0);
        if ((ret & 0x2000) == 0)
            gen_op_raise_exception(ret);
    }
    /* TO BE FIXED: T0 hasn't got a proper value, which makes tb_add_jump
     *              do bad business and then qemu crashes !
     */
    gen_op_set_T0(0);
    /* Generate the return instruction */
    gen_op_exit_tb();
    *gen_opc_ptr = INDEX_op_end;
    if (!search_pc)
        tb->size = (uint32_t)ctx.nip - pc_start;
    else
        tb->size = 0;
//    *gen_opc_ptr = INDEX_op_end;
#ifdef DEBUG_DISAS
    if (loglevel > 0) {
        fprintf(logfile, "IN: %s\n", lookup_symbol((void *)pc_start));
	disas(logfile, (void *)pc_start, (uint32_t)ctx.nip - pc_start, 0, 0);
        fprintf(logfile, "\n");

        fprintf(logfile, "OP:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
#endif

    return 0;
}

int gen_intermediate_code(CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc(CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}
