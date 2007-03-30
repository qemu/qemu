/*
 *  PowerPC emulation for qemu: main translation routines.
 * 
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
//#define DO_PPC_STATISTICS

#if defined(USE_DIRECT_JUMP)
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

static inline void gen_set_T0 (target_ulong val)
{
#if defined(TARGET_PPC64)
    if (val >> 32)
        gen_op_set_T0_64(val >> 32, val);
    else
#endif
        gen_op_set_T0(val);
}

static inline void gen_set_T1 (target_ulong val)
{
#if defined(TARGET_PPC64)
    if (val >> 32)
        gen_op_set_T1_64(val >> 32, val);
    else
#endif
        gen_op_set_T1(val);
}

#define GEN8(func, NAME)                                                      \
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

#define GEN32(func, NAME)                                                     \
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
static inline void gen_op_store_T0_fpscri(int n, uint8_t param)
{
    gen_op_set_T0(param);
    gen_op_store_T0_fpscr(n);
}

/* General purpose registers moves */
GEN32(gen_op_load_gpr_T0, gen_op_load_gpr_T0_gpr);
GEN32(gen_op_load_gpr_T1, gen_op_load_gpr_T1_gpr);
GEN32(gen_op_load_gpr_T2, gen_op_load_gpr_T2_gpr);

GEN32(gen_op_store_T0_gpr, gen_op_store_T0_gpr_gpr);
GEN32(gen_op_store_T1_gpr, gen_op_store_T1_gpr_gpr);
#if 0 // unused
GEN32(gen_op_store_T2_gpr, gen_op_store_T2_gpr_gpr);
#endif

/* floating point registers moves */
GEN32(gen_op_load_fpr_FT0, gen_op_load_fpr_FT0_fpr);
GEN32(gen_op_load_fpr_FT1, gen_op_load_fpr_FT1_fpr);
GEN32(gen_op_load_fpr_FT2, gen_op_load_fpr_FT2_fpr);
GEN32(gen_op_store_FT0_fpr, gen_op_store_FT0_fpr_fpr);
GEN32(gen_op_store_FT1_fpr, gen_op_store_FT1_fpr_fpr);
#if 0 // unused
GEN32(gen_op_store_FT2_fpr, gen_op_store_FT2_fpr_fpr);
#endif

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
#if defined(TARGET_PPC64)
    int sf_mode;
#endif
    int fpu_enabled;
#if defined(TARGET_PPCSPE)
    int spe_enabled;
#endif
    ppc_spr_t *spr_cb; /* Needed to check rights for mfspr/mtspr */
    int singlestep_enabled;
} DisasContext;

struct opc_handler_t {
    /* invalid bits */
    uint32_t inval;
    /* instruction type */
    uint64_t type;
    /* handler */
    void (*handler)(DisasContext *ctx);
#if defined(DO_PPC_STATISTICS)
    const unsigned char *oname;
    uint64_t count;
#endif
};

static inline void gen_set_Rc0 (DisasContext *ctx)
{
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_cmpi_64(0);
    else
#endif
        gen_op_cmpi(0);
    gen_op_set_Rc0();
}

static inline void gen_update_nip (DisasContext *ctx, target_ulong nip)
{
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_update_nip_64(nip >> 32, nip);
    else
#endif
        gen_op_update_nip(nip);
}

#define RET_EXCP(ctx, excp, error)                                            \
do {                                                                          \
    if ((ctx)->exception == EXCP_NONE) {                                      \
        gen_update_nip(ctx, (ctx)->nip);                                      \
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
    gen_update_nip(ctx, ctx->nip);
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
static inline target_ulong LI (uint32_t opcode)
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
static inline target_ulong MASK (uint32_t start, uint32_t end)
{
    target_ulong ret;

#if defined(TARGET_PPC64)
    if (likely(start == 0)) {
        ret = (uint64_t)(-1ULL) << (63 - end);
    } else if (likely(end == 63)) {
        ret = (uint64_t)(-1ULL) >> start;
    }
#else
    if (likely(start == 0)) {
        ret = (uint32_t)(-1ULL) << (31  - end);
    } else if (likely(end == 31)) {
        ret = (uint32_t)(-1ULL) >> start;
    }
#endif
    else {
        ret = (((target_ulong)(-1ULL)) >> (start)) ^
            (((target_ulong)(-1ULL) >> (end)) >> 1);
        if (unlikely(start > end))
            return ~ret;
    }

    return ret;
}

#if HOST_LONG_BITS == 64
#define OPC_ALIGN 8
#else
#define OPC_ALIGN 4
#endif
#if defined(__APPLE__)
#define OPCODES_SECTION                                                       \
    __attribute__ ((section("__TEXT,__opcodes"), unused, aligned (OPC_ALIGN) ))
#else
#define OPCODES_SECTION                                                       \
    __attribute__ ((section(".opcodes"), unused, aligned (OPC_ALIGN) ))
#endif

#if defined(DO_PPC_STATISTICS)
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
        .oname = stringify(name),                                             \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#else
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
#endif

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
#define __GEN_INT_ARITH2(name, opc1, opc2, opc3, inval, type)                 \
GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                              \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

#define __GEN_INT_ARITH2_O(name, opc1, opc2, opc3, inval, type)               \
GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                              \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

#define __GEN_INT_ARITH1(name, opc1, opc2, opc3, type)                        \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, type)                         \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}
#define __GEN_INT_ARITH1_O(name, opc1, opc2, opc3, type)                      \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, type)                         \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

/* Two operands arithmetic functions */
#define GEN_INT_ARITH2(name, opc1, opc2, opc3, type)                          \
__GEN_INT_ARITH2(name, opc1, opc2, opc3, 0x00000000, type)                    \
__GEN_INT_ARITH2_O(name##o, opc1, opc2, opc3 | 0x10, 0x00000000, type)

/* Two operands arithmetic functions with no overflow allowed */
#define GEN_INT_ARITHN(name, opc1, opc2, opc3, type)                          \
__GEN_INT_ARITH2(name, opc1, opc2, opc3, 0x00000400, type)

/* One operand arithmetic functions */
#define GEN_INT_ARITH1(name, opc1, opc2, opc3, type)                          \
__GEN_INT_ARITH1(name, opc1, opc2, opc3, type)                                \
__GEN_INT_ARITH1_O(name##o, opc1, opc2, opc3 | 0x10, type)

#if defined(TARGET_PPC64)
#define __GEN_INT_ARITH2_64(name, opc1, opc2, opc3, inval, type)              \
GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                              \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    if (ctx->sf_mode)                                                         \
        gen_op_##name##_64();                                                 \
    else                                                                      \
        gen_op_##name();                                                      \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

#define __GEN_INT_ARITH2_O_64(name, opc1, opc2, opc3, inval, type)            \
GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                              \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    if (ctx->sf_mode)                                                         \
        gen_op_##name##_64();                                                 \
    else                                                                      \
        gen_op_##name();                                                      \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

#define __GEN_INT_ARITH1_64(name, opc1, opc2, opc3, type)                     \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, type)                         \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (ctx->sf_mode)                                                         \
        gen_op_##name##_64();                                                 \
    else                                                                      \
        gen_op_##name();                                                      \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}
#define __GEN_INT_ARITH1_O_64(name, opc1, opc2, opc3, type)                   \
GEN_HANDLER(name, opc1, opc2, opc3, 0x0000F800, type)                         \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    if (ctx->sf_mode)                                                         \
        gen_op_##name##_64();                                                 \
    else                                                                      \
        gen_op_##name();                                                      \
    gen_op_store_T0_gpr(rD(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

/* Two operands arithmetic functions */
#define GEN_INT_ARITH2_64(name, opc1, opc2, opc3, type)                       \
__GEN_INT_ARITH2_64(name, opc1, opc2, opc3, 0x00000000, type)                 \
__GEN_INT_ARITH2_O_64(name##o, opc1, opc2, opc3 | 0x10, 0x00000000, type)

/* Two operands arithmetic functions with no overflow allowed */
#define GEN_INT_ARITHN_64(name, opc1, opc2, opc3, type)                       \
__GEN_INT_ARITH2_64(name, opc1, opc2, opc3, 0x00000400, type)

/* One operand arithmetic functions */
#define GEN_INT_ARITH1_64(name, opc1, opc2, opc3, type)                       \
__GEN_INT_ARITH1_64(name, opc1, opc2, opc3, type)                             \
__GEN_INT_ARITH1_O_64(name##o, opc1, opc2, opc3 | 0x10, type)
#else
#define GEN_INT_ARITH2_64 GEN_INT_ARITH2
#define GEN_INT_ARITHN_64 GEN_INT_ARITHN
#define GEN_INT_ARITH1_64 GEN_INT_ARITH1
#endif

/* add    add.    addo    addo.    */
static inline void gen_op_addo (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addo();
}
#if defined(TARGET_PPC64)
#define gen_op_add_64 gen_op_add
static inline void gen_op_addo_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addo_64();
}
#endif
GEN_INT_ARITH2_64 (add,    0x1F, 0x0A, 0x08, PPC_INTEGER);
/* addc   addc.   addco   addco.   */
static inline void gen_op_addc (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addc();
}
static inline void gen_op_addco (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addc();
    gen_op_check_addo();
}
#if defined(TARGET_PPC64)
static inline void gen_op_addc_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addc_64();
}
static inline void gen_op_addco_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_add();
    gen_op_check_addc_64();
    gen_op_check_addo_64();
}
#endif
GEN_INT_ARITH2_64 (addc,   0x1F, 0x0A, 0x00, PPC_INTEGER);
/* adde   adde.   addeo   addeo.   */
static inline void gen_op_addeo (void)
{
    gen_op_move_T2_T0();
    gen_op_adde();
    gen_op_check_addo();
}
#if defined(TARGET_PPC64)
static inline void gen_op_addeo_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_adde_64();
    gen_op_check_addo_64();
}
#endif
GEN_INT_ARITH2_64 (adde,   0x1F, 0x0A, 0x04, PPC_INTEGER);
/* addme  addme.  addmeo  addmeo.  */
static inline void gen_op_addme (void)
{
    gen_op_move_T1_T0();
    gen_op_add_me();
}
#if defined(TARGET_PPC64)
static inline void gen_op_addme_64 (void)
{
    gen_op_move_T1_T0();
    gen_op_add_me_64();
}
#endif
GEN_INT_ARITH1_64 (addme,  0x1F, 0x0A, 0x07, PPC_INTEGER);
/* addze  addze.  addzeo  addzeo.  */
static inline void gen_op_addze (void)
{
    gen_op_move_T2_T0();
    gen_op_add_ze();
    gen_op_check_addc();
}
static inline void gen_op_addzeo (void)
{
    gen_op_move_T2_T0();
    gen_op_add_ze();
    gen_op_check_addc();
    gen_op_check_addo();
}
#if defined(TARGET_PPC64)
static inline void gen_op_addze_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_add_ze();
    gen_op_check_addc_64();
}
static inline void gen_op_addzeo_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_add_ze();
    gen_op_check_addc_64();
    gen_op_check_addo_64();
}
#endif
GEN_INT_ARITH1_64 (addze,  0x1F, 0x0A, 0x06, PPC_INTEGER);
/* divw   divw.   divwo   divwo.   */
GEN_INT_ARITH2 (divw,   0x1F, 0x0B, 0x0F, PPC_INTEGER);
/* divwu  divwu.  divwuo  divwuo.  */
GEN_INT_ARITH2 (divwu,  0x1F, 0x0B, 0x0E, PPC_INTEGER);
/* mulhw  mulhw.                   */
GEN_INT_ARITHN (mulhw,  0x1F, 0x0B, 0x02, PPC_INTEGER);
/* mulhwu mulhwu.                  */
GEN_INT_ARITHN (mulhwu, 0x1F, 0x0B, 0x00, PPC_INTEGER);
/* mullw  mullw.  mullwo  mullwo.  */
GEN_INT_ARITH2 (mullw,  0x1F, 0x0B, 0x07, PPC_INTEGER);
/* neg    neg.    nego    nego.    */
GEN_INT_ARITH1_64 (neg,    0x1F, 0x08, 0x03, PPC_INTEGER);
/* subf   subf.   subfo   subfo.   */
static inline void gen_op_subfo (void)
{
    gen_op_move_T2_T0();
    gen_op_subf();
    gen_op_check_subfo();
}
#if defined(TARGET_PPC64)
#define gen_op_subf_64 gen_op_subf
static inline void gen_op_subfo_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_subf();
    gen_op_check_subfo_64();
}
#endif
GEN_INT_ARITH2_64 (subf,   0x1F, 0x08, 0x01, PPC_INTEGER);
/* subfc  subfc.  subfco  subfco.  */
static inline void gen_op_subfc (void)
{
    gen_op_subf();
    gen_op_check_subfc();
}
static inline void gen_op_subfco (void)
{
    gen_op_move_T2_T0();
    gen_op_subf();
    gen_op_check_subfc();
    gen_op_check_subfo();
}
#if defined(TARGET_PPC64)
static inline void gen_op_subfc_64 (void)
{
    gen_op_subf();
    gen_op_check_subfc_64();
}
static inline void gen_op_subfco_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_subf();
    gen_op_check_subfc_64();
    gen_op_check_subfo_64();
}
#endif
GEN_INT_ARITH2_64 (subfc,  0x1F, 0x08, 0x00, PPC_INTEGER);
/* subfe  subfe.  subfeo  subfeo.  */
static inline void gen_op_subfeo (void)
{
    gen_op_move_T2_T0();
    gen_op_subfe();
    gen_op_check_subfo();
}
#if defined(TARGET_PPC64)
#define gen_op_subfe_64 gen_op_subfe
static inline void gen_op_subfeo_64 (void)
{
    gen_op_move_T2_T0();
    gen_op_subfe_64();
    gen_op_check_subfo_64();
}
#endif
GEN_INT_ARITH2_64 (subfe,  0x1F, 0x08, 0x04, PPC_INTEGER);
/* subfme subfme. subfmeo subfmeo. */
GEN_INT_ARITH1_64 (subfme, 0x1F, 0x08, 0x07, PPC_INTEGER);
/* subfze subfze. subfzeo subfzeo. */
GEN_INT_ARITH1_64 (subfze, 0x1F, 0x08, 0x06, PPC_INTEGER);
/* addi */
GEN_HANDLER(addi, 0x0E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_long simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        /* li case */
        gen_set_T0(simm);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (likely(simm != 0))
            gen_op_addi(simm);
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* addic */
GEN_HANDLER(addic, 0x0C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_long simm = SIMM(ctx->opcode);

    gen_op_load_gpr_T0(rA(ctx->opcode));
    if (likely(simm != 0)) {
        gen_op_move_T2_T0();
        gen_op_addi(simm);
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_check_addc_64();
        else
#endif
            gen_op_check_addc();
    } else {
        gen_op_clear_xer_ca();
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}
/* addic. */
GEN_HANDLER(addic_, 0x0D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_long simm = SIMM(ctx->opcode);

    gen_op_load_gpr_T0(rA(ctx->opcode));
    if (likely(simm != 0)) {
        gen_op_move_T2_T0();
        gen_op_addi(simm);
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_check_addc_64();
        else
#endif
            gen_op_check_addc();
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
    gen_set_Rc0(ctx);
}
/* addis */
GEN_HANDLER(addis, 0x0F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_long simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        /* lis case */
        gen_set_T0(simm << 16);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (likely(simm != 0))
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
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_subfic_64(SIMM(ctx->opcode));
    else
#endif
        gen_op_subfic(SIMM(ctx->opcode));
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

#if defined(TARGET_PPC64)
/* mulhd  mulhd.                   */
GEN_INT_ARITHN (mulhd,  0x1F, 0x09, 0x02, PPC_INTEGER);
/* mulhdu mulhdu.                  */
GEN_INT_ARITHN (mulhdu, 0x1F, 0x09, 0x00, PPC_INTEGER);
/* mulld  mulld.  mulldo  mulldo.  */
GEN_INT_ARITH2 (mulld,  0x1F, 0x09, 0x07, PPC_INTEGER);
/* divd   divd.   divdo   divdo.   */
GEN_INT_ARITH2 (divd,   0x1F, 0x09, 0x0F, PPC_INTEGER);
/* divdu  divdu.  divduo  divduo.  */
GEN_INT_ARITH2 (divdu,  0x1F, 0x09, 0x0E, PPC_INTEGER);
#endif

/***                           Integer comparison                          ***/
#if defined(TARGET_PPC64)
#define GEN_CMP(name, opc, type)                                              \
GEN_HANDLER(name, 0x1F, 0x00, opc, 0x00400000, type)                          \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    if (ctx->sf_mode)                                                         \
        gen_op_##name##_64();                                                 \
    else                                                                      \
        gen_op_##name();                                                      \
    gen_op_store_T0_crf(crfD(ctx->opcode));                                   \
}
#else
#define GEN_CMP(name, opc, type)                                              \
GEN_HANDLER(name, 0x1F, 0x00, opc, 0x00400000, type)                          \
{                                                                             \
    gen_op_load_gpr_T0(rA(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_crf(crfD(ctx->opcode));                                   \
}
#endif

/* cmp */
GEN_CMP(cmp, 0x00, PPC_INTEGER);
/* cmpi */
GEN_HANDLER(cmpi, 0x0B, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_cmpi_64(SIMM(ctx->opcode));
    else
#endif
        gen_op_cmpi(SIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}
/* cmpl */
GEN_CMP(cmpl, 0x01, PPC_INTEGER);
/* cmpli */
GEN_HANDLER(cmpli, 0x0A, 0xFF, 0xFF, 0x00400000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_cmpli_64(UIMM(ctx->opcode));
    else
#endif
        gen_op_cmpli(UIMM(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/* isel (PowerPC 2.03 specification) */
GEN_HANDLER(isel, 0x1F, 0x0F, 0x00, 0x00000001, PPC_203)
{
    uint32_t bi = rC(ctx->opcode);
    uint32_t mask;

    if (rA(ctx->opcode) == 0) {
        gen_set_T0(0);
    } else {
        gen_op_load_gpr_T1(rA(ctx->opcode));
    }
    gen_op_load_gpr_T2(rB(ctx->opcode));
    mask = 1 << (3 - (bi & 0x03));
    gen_op_load_crf_T0(bi >> 2);
    gen_op_test_true(mask);
    gen_op_isel();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/***                            Integer logical                            ***/
#define __GEN_LOGICAL2(name, opc2, opc3, type)                                \
GEN_HANDLER(name, 0x1F, opc2, opc3, 0x00000000, type)                         \
{                                                                             \
    gen_op_load_gpr_T0(rS(ctx->opcode));                                      \
    gen_op_load_gpr_T1(rB(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}
#define GEN_LOGICAL2(name, opc, type)                                         \
__GEN_LOGICAL2(name, 0x1C, opc, type)

#define GEN_LOGICAL1(name, opc, type)                                         \
GEN_HANDLER(name, 0x1F, 0x1A, opc, 0x00000000, type)                          \
{                                                                             \
    gen_op_load_gpr_T0(rS(ctx->opcode));                                      \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx);                                                     \
}

/* and & and. */
GEN_LOGICAL2(and, 0x00, PPC_INTEGER);
/* andc & andc. */
GEN_LOGICAL2(andc, 0x01, PPC_INTEGER);
/* andi. */
GEN_HANDLER(andi_, 0x1C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_andi_T0(UIMM(ctx->opcode));
    gen_op_store_T0_gpr(rA(ctx->opcode));
    gen_set_Rc0(ctx);
}
/* andis. */
GEN_HANDLER(andis_, 0x1D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_andi_T0(UIMM(ctx->opcode) << 16);
    gen_op_store_T0_gpr(rA(ctx->opcode));
    gen_set_Rc0(ctx);
}

/* cntlzw */
GEN_LOGICAL1(cntlzw, 0x00, PPC_INTEGER);
/* eqv & eqv. */
GEN_LOGICAL2(eqv, 0x08, PPC_INTEGER);
/* extsb & extsb. */
GEN_LOGICAL1(extsb, 0x1D, PPC_INTEGER);
/* extsh & extsh. */
GEN_LOGICAL1(extsh, 0x1C, PPC_INTEGER);
/* nand & nand. */
GEN_LOGICAL2(nand, 0x0E, PPC_INTEGER);
/* nor & nor. */
GEN_LOGICAL2(nor, 0x03, PPC_INTEGER);

/* or & or. */
GEN_HANDLER(or, 0x1F, 0x1C, 0x0D, 0x00000000, PPC_INTEGER)
{
    int rs, ra, rb;

    rs = rS(ctx->opcode);
    ra = rA(ctx->opcode);
    rb = rB(ctx->opcode);
    /* Optimisation for mr. ri case */
    if (rs != ra || rs != rb) {
        gen_op_load_gpr_T0(rs);
        if (rs != rb) {
            gen_op_load_gpr_T1(rb);
            gen_op_or();
        }
        gen_op_store_T0_gpr(ra);
        if (unlikely(Rc(ctx->opcode) != 0))
            gen_set_Rc0(ctx);
    } else if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_op_load_gpr_T0(rs);
        gen_set_Rc0(ctx);
    }
}

/* orc & orc. */
GEN_LOGICAL2(orc, 0x0C, PPC_INTEGER);
/* xor & xor. */
GEN_HANDLER(xor, 0x1F, 0x1C, 0x09, 0x00000000, PPC_INTEGER)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    /* Optimisation for "set to zero" case */
    if (rS(ctx->opcode) != rB(ctx->opcode)) {
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_xor();
    } else {
        gen_op_reset_T0();
    }
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
/* ori */
GEN_HANDLER(ori, 0x18, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        /* XXX: should handle special NOPs for POWER series */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(uimm != 0))
        gen_op_ori(uimm);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* oris */
GEN_HANDLER(oris, 0x19, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(uimm != 0))
        gen_op_ori(uimm << 16);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}
/* xori */
GEN_HANDLER(xori, 0x1A, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(uimm != 0))
        gen_op_xori(uimm);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/* xoris */
GEN_HANDLER(xoris, 0x1B, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(uimm != 0))
        gen_op_xori(uimm << 16);
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

/* popcntb : PowerPC 2.03 specification */
GEN_HANDLER(popcntb, 0x1F, 0x03, 0x03, 0x0000F801, PPC_203)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_popcntb_64();
    else
#endif
        gen_op_popcntb();
    gen_op_store_T0_gpr(rA(ctx->opcode));
}

#if defined(TARGET_PPC64)
/* extsw & extsw. */
GEN_LOGICAL1(extsw, 0x1E, PPC_64B);
/* cntlzd */
GEN_LOGICAL1(cntlzd, 0x01, PPC_64B);
#endif

/***                             Integer rotate                            ***/
/* rlwimi & rlwimi. */
GEN_HANDLER(rlwimi, 0x14, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    target_ulong mask;
    uint32_t mb, me, sh;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    sh = SH(ctx->opcode);
    if (likely(sh == 0)) {
        if (likely(mb == 0 && me == 31)) {
            gen_op_load_gpr_T0(rS(ctx->opcode));
            goto do_store;
        } else if (likely(mb == 31 && me == 0)) {
            gen_op_load_gpr_T0(rA(ctx->opcode));
            goto do_store;
        }
        gen_op_load_gpr_T0(rS(ctx->opcode));
        gen_op_load_gpr_T1(rA(ctx->opcode));
        goto do_mask;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rA(ctx->opcode));
    gen_op_rotli32_T0(SH(ctx->opcode));
 do_mask:
#if defined(TARGET_PPC64)
    mb += 32;
    me += 32;
#endif
    mask = MASK(mb, me);
    gen_op_andi_T0(mask);
    gen_op_andi_T1(~mask);
    gen_op_or();
 do_store:
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
/* rlwinm & rlwinm. */
GEN_HANDLER(rlwinm, 0x15, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me, sh;
    
    sh = SH(ctx->opcode);
    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(sh == 0)) {
        goto do_mask;
    }
    if (likely(mb == 0)) {
        if (likely(me == 31)) {
            gen_op_rotli32_T0(sh);
            goto do_store;
        } else if (likely(me == (31 - sh))) {
            gen_op_sli_T0(sh);
            goto do_store;
        }
    } else if (likely(me == 31)) {
        if (likely(sh == (32 - mb))) {
            gen_op_srli_T0(mb);
            goto do_store;
        }
    }
    gen_op_rotli32_T0(sh);
 do_mask:
#if defined(TARGET_PPC64)
    mb += 32;
    me += 32;
#endif
    gen_op_andi_T0(MASK(mb, me));
 do_store:
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
/* rlwnm & rlwnm. */
GEN_HANDLER(rlwnm, 0x17, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    uint32_t mb, me;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_rotl32_T0_T1();
    if (unlikely(mb != 0 || me != 31)) {
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        gen_op_andi_T0(MASK(mb, me));
    }
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

#if defined(TARGET_PPC64)
#define GEN_PPC64_R2(name, opc1, opc2)                                        \
GEN_HANDLER(name##0, opc1, opc2, 0xFF, 0x00000000, PPC_64B)                   \
{                                                                             \
    gen_##name(ctx, 0);                                                       \
}                                                                             \
GEN_HANDLER(name##1, opc1, opc2 | 0x10, 0xFF, 0x00000000, PPC_64B)            \
{                                                                             \
    gen_##name(ctx, 1);                                                       \
}
#define GEN_PPC64_R4(name, opc1, opc2)                                        \
GEN_HANDLER(name##0, opc1, opc2, 0xFF, 0x00000000, PPC_64B)                   \
{                                                                             \
    gen_##name(ctx, 0, 0);                                                    \
}                                                                             \
GEN_HANDLER(name##1, opc1, opc2 | 0x01, 0xFF, 0x00000000, PPC_64B)            \
{                                                                             \
    gen_##name(ctx, 0, 1);                                                    \
}                                                                             \
GEN_HANDLER(name##2, opc1, opc2 | 0x10, 0xFF, 0x00000000, PPC_64B)            \
{                                                                             \
    gen_##name(ctx, 1, 0);                                                    \
}                                                                             \
GEN_HANDLER(name##3, opc1, opc2 | 0x11, 0xFF, 0x00000000, PPC_64B)            \
{                                                                             \
    gen_##name(ctx, 1, 1);                                                    \
}

static inline void gen_rldinm (DisasContext *ctx, uint32_t mb, uint32_t me,
                               uint32_t sh)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (likely(sh == 0)) {
        goto do_mask;
    }
    if (likely(mb == 0)) {
        if (likely(me == 63)) {
            gen_op_rotli32_T0(sh);
            goto do_store;
        } else if (likely(me == (63 - sh))) {
            gen_op_sli_T0(sh);
            goto do_store;
        }
    } else if (likely(me == 63)) {
        if (likely(sh == (64 - mb))) {
            gen_op_srli_T0(mb);
            goto do_store;
        }
    }
    gen_op_rotli64_T0(sh);
 do_mask:
    gen_op_andi_T0(MASK(mb, me));
 do_store:
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
/* rldicl - rldicl. */
static inline void gen_rldicl (DisasContext *ctx, int mbn, int shn)
{
    uint32_t sh, mb;

    sh = SH(ctx->opcode) | (1 << shn);
    mb = (MB(ctx->opcode) << 1) | mbn;
    gen_rldinm(ctx, mb, 63, sh);
}
GEN_PPC64_R4(rldicl, 0x1E, 0x00);
/* rldicr - rldicr. */
static inline void gen_rldicr (DisasContext *ctx, int men, int shn)
{
    uint32_t sh, me;

    sh = SH(ctx->opcode) | (1 << shn);
    me = (MB(ctx->opcode) << 1) | men;
    gen_rldinm(ctx, 0, me, sh);
}
GEN_PPC64_R4(rldicr, 0x1E, 0x02);
/* rldic - rldic. */
static inline void gen_rldic (DisasContext *ctx, int mbn, int shn)
{
    uint32_t sh, mb;

    sh = SH(ctx->opcode) | (1 << shn);
    mb = (MB(ctx->opcode) << 1) | mbn;
    gen_rldinm(ctx, mb, 63 - sh, sh);
}
GEN_PPC64_R4(rldic, 0x1E, 0x04);

static inline void gen_rldnm (DisasContext *ctx, uint32_t mb, uint32_t me)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_rotl64_T0_T1();
    if (unlikely(mb != 0 || me != 63)) {
        gen_op_andi_T0(MASK(mb, me));
    }
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* rldcl - rldcl. */
static inline void gen_rldcl (DisasContext *ctx, int mbn)
{
    uint32_t mb;

    mb = (MB(ctx->opcode) << 1) | mbn;
    gen_rldnm(ctx, mb, 63);
}
GEN_PPC64_R2(rldcl, 0x1E, 0x08)
/* rldcr - rldcr. */
static inline void gen_rldcr (DisasContext *ctx, int men)
{
    uint32_t me;

    me = (MB(ctx->opcode) << 1) | men;
    gen_rldnm(ctx, 0, me);
}
GEN_PPC64_R2(rldcr, 0x1E, 0x09)
/* rldimi - rldimi. */
static inline void gen_rldimi (DisasContext *ctx, int mbn, int shn)
{
    uint64_t mask;
    uint32_t sh, mb;

    sh = SH(ctx->opcode) | (1 << shn);
    mb = (MB(ctx->opcode) << 1) | mbn;
    if (likely(sh == 0)) {
        if (likely(mb == 0)) {
            gen_op_load_gpr_T0(rS(ctx->opcode));
            goto do_store;
        } else if (likely(mb == 63)) {
            gen_op_load_gpr_T0(rA(ctx->opcode));
            goto do_store;
        }
        gen_op_load_gpr_T0(rS(ctx->opcode));
        gen_op_load_gpr_T1(rA(ctx->opcode));
        goto do_mask;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rA(ctx->opcode));
    gen_op_rotli64_T0(SH(ctx->opcode));
 do_mask:
    mask = MASK(mb, 63 - sh);
    gen_op_andi_T0(mask);
    gen_op_andi_T1(~mask);
    gen_op_or();
 do_store:
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
GEN_PPC64_R4(rldimi, 0x1E, 0x06)
#endif

/***                             Integer shift                             ***/
/* slw & slw. */
__GEN_LOGICAL2(slw, 0x18, 0x00, PPC_INTEGER);
/* sraw & sraw. */
__GEN_LOGICAL2(sraw, 0x18, 0x18, PPC_INTEGER);
/* srawi & srawi. */
GEN_HANDLER(srawi, 0x1F, 0x18, 0x19, 0x00000000, PPC_INTEGER)
{
    int mb, me;
    gen_op_load_gpr_T0(rS(ctx->opcode));
    if (SH(ctx->opcode) != 0) {
        gen_op_move_T1_T0();
        mb = 32 - SH(ctx->opcode);
        me = 31;
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        gen_op_srawi(SH(ctx->opcode), MASK(mb, me));
    }
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
/* srw & srw. */
__GEN_LOGICAL2(srw, 0x18, 0x10, PPC_INTEGER);

#if defined(TARGET_PPC64)
/* sld & sld. */
__GEN_LOGICAL2(sld, 0x1B, 0x00, PPC_64B);
/* srad & srad. */
__GEN_LOGICAL2(srad, 0x1A, 0x18, PPC_64B);
/* sradi & sradi. */
static inline void gen_sradi (DisasContext *ctx, int n)
{
    uint64_t mask;
    int sh, mb, me;

    gen_op_load_gpr_T0(rS(ctx->opcode));
    sh = SH(ctx->opcode) + (n << 5);
    if (sh != 0) {
        gen_op_move_T1_T0();
        mb = 64 - SH(ctx->opcode);
        me = 63;
        mask = MASK(mb, me);
        gen_op_sradi(sh, mask >> 32, mask);
    }
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}
GEN_HANDLER(sradi0, 0x1F, 0x1A, 0x19, 0x00000000, PPC_64B)
{
    gen_sradi(ctx, 0);
}
GEN_HANDLER(sradi1, 0x1F, 0x1B, 0x19, 0x00000000, PPC_64B)
{
    gen_sradi(ctx, 1);
}
/* srd & srd. */
__GEN_LOGICAL2(srd, 0x1B, 0x10, PPC_64B);
#endif

/***                       Floating-Point arithmetic                       ***/
#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat)                           \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x00000000, PPC_FLOAT)                   \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
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
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_ACB(name, op2)                                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0);                                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1);

#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat)                     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
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
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AB(name, op2, inval)                                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0);                               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1);

#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat)                     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, PPC_FLOAT)                        \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
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
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_op_set_Rc1();                                                     \
}
#define GEN_FLOAT_AC(name, op2, inval)                                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0);                               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1);

#define GEN_FLOAT_B(name, op2, op3)                                           \
GEN_HANDLER(f##name, 0x3F, op2, op3, 0x001F0000, PPC_FLOAT)                   \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_op_set_Rc1();                                                     \
}

#define GEN_FLOAT_BS(name, op1, op2)                                          \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x001F07C0, PPC_FLOAT)                   \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_op_reset_scrfx();                                                     \
    gen_op_load_fpr_FT0(rB(ctx->opcode));                                     \
    gen_op_f##name();                                                         \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_op_set_Rc1();                                                     \
}

/* fadd - fadds */
GEN_FLOAT_AB(add, 0x15, 0x000007C0);
/* fdiv - fdivs */
GEN_FLOAT_AB(div, 0x12, 0x000007C0);
/* fmul - fmuls */
GEN_FLOAT_AC(mul, 0x19, 0x0000F800);

/* fres */ /* XXX: not in 601 */
GEN_FLOAT_BS(res, 0x3B, 0x18);

/* frsqrte */ /* XXX: not in 601 */
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A);

/* fsel */ /* XXX: not in 601 */
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0);
/* fsub - fsubs */
GEN_FLOAT_AB(sub, 0x14, 0x000007C0);
/* Optional: */
/* fsqrt */
GEN_HANDLER(fsqrt, 0x3F, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_OPT)
{
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_fsqrt();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_OPT)
{
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_fsqrt();
    gen_op_frsp();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
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
#if defined(TARGET_PPC64)
/* fcfid */
GEN_FLOAT_B(cfid, 0x0E, 0x1A);
/* fctid */
GEN_FLOAT_B(ctid, 0x0E, 0x19);
/* fctidz */
GEN_FLOAT_B(ctidz, 0x0F, 0x19);
#endif

/***                         Floating-Point compare                        ***/
/* fcmpo */
GEN_HANDLER(fcmpo, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT)
{
    if (unlikely(!ctx->fpu_enabled)) {
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
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x00, 0x00600001, PPC_FLOAT)
{
    if (unlikely(!ctx->fpu_enabled)) {
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
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_reset_scrfx();
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
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
    if (unlikely(!ctx->fpu_enabled)) {
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
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_load_fpscr();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

/* mtfsb0 */
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT)
{
    uint8_t crb;
    
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    crb = crbD(ctx->opcode) >> 2;
    gen_op_load_fpscr_T0(crb);
    gen_op_andi_T0(~(1 << (crbD(ctx->opcode) & 0x03)));
    gen_op_store_T0_fpscr(crb);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

/* mtfsb1 */
GEN_HANDLER(mtfsb1, 0x3F, 0x06, 0x01, 0x001FF800, PPC_FLOAT)
{
    uint8_t crb;
    
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    crb = crbD(ctx->opcode) >> 2;
    gen_op_load_fpscr_T0(crb);
    gen_op_ori(1 << (crbD(ctx->opcode) & 0x03));
    gen_op_store_T0_fpscr(crb);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

/* mtfsf */
GEN_HANDLER(mtfsf, 0x3F, 0x07, 0x16, 0x02010000, PPC_FLOAT)
{
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_load_fpr_FT0(rB(ctx->opcode));
    gen_op_store_fpscr(FM(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

/* mtfsfi */
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006f0800, PPC_FLOAT)
{
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_op_store_T0_fpscri(crbD(ctx->opcode) >> 2, FPIMM(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_op_set_Rc1();
}

/***                           Addressing modes                            ***/
/* Register indirect with immediate index : EA = (rA|0) + SIMM */
static inline void gen_addr_imm_index (DisasContext *ctx)
{
    target_long simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_set_T0(simm);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (likely(simm != 0))
            gen_op_addi(simm);
    }
}

static inline void gen_addr_reg_index (DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_load_gpr_T0(rB(ctx->opcode));
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rB(ctx->opcode));
        gen_op_add();
    }
}

static inline void gen_addr_register (DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_op_reset_T0();
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
    }
}

/***                             Integer load                              ***/
#define op_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
#if defined(TARGET_PPC64)
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_raw,                                                   \
    &gen_op_l##width##_le_raw,                                                \
    &gen_op_l##width##_64_raw,                                                \
    &gen_op_l##width##_le_64_raw,                                             \
};
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_raw,                                                  \
    &gen_op_st##width##_le_raw,                                               \
    &gen_op_st##width##_64_raw,                                               \
    &gen_op_st##width##_le_64_raw,                                            \
};
/* Byte access routine are endian safe */
#define gen_op_stb_le_64_raw gen_op_stb_64_raw
#define gen_op_lbz_le_64_raw gen_op_lbz_64_raw
#else
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
#endif
/* Byte access routine are endian safe */
#define gen_op_stb_le_raw gen_op_stb_raw
#define gen_op_lbz_le_raw gen_op_lbz_raw
#else
#if defined(TARGET_PPC64)
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_user,                                                  \
    &gen_op_l##width##_le_user,                                               \
    &gen_op_l##width##_kernel,                                                \
    &gen_op_l##width##_le_kernel,                                             \
    &gen_op_l##width##_64_user,                                               \
    &gen_op_l##width##_le_64_user,                                            \
    &gen_op_l##width##_64_kernel,                                             \
    &gen_op_l##width##_le_64_kernel,                                          \
};
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_st##width[] = {                                      \
    &gen_op_st##width##_user,                                                 \
    &gen_op_st##width##_le_user,                                              \
    &gen_op_st##width##_kernel,                                               \
    &gen_op_st##width##_le_kernel,                                            \
    &gen_op_st##width##_64_user,                                              \
    &gen_op_st##width##_le_64_user,                                           \
    &gen_op_st##width##_64_kernel,                                            \
    &gen_op_st##width##_le_64_kernel,                                         \
};
/* Byte access routine are endian safe */
#define gen_op_stb_le_64_user gen_op_stb_64_user
#define gen_op_lbz_le_64_user gen_op_lbz_64_user
#define gen_op_stb_le_64_kernel gen_op_stb_64_kernel
#define gen_op_lbz_le_64_kernel gen_op_lbz_64_kernel
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
#endif
/* Byte access routine are endian safe */
#define gen_op_stb_le_user gen_op_stb_user
#define gen_op_lbz_le_user gen_op_lbz_user
#define gen_op_stb_le_kernel gen_op_stb_kernel
#define gen_op_lbz_le_kernel gen_op_lbz_kernel
#endif

#define GEN_LD(width, opc, type)                                              \
GEN_HANDLER(l##width, opc, 0xFF, 0xFF, 0x00000000, type)                      \
{                                                                             \
    gen_addr_imm_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
}

#define GEN_LDU(width, opc, type)                                             \
GEN_HANDLER(l##width##u, opc, 0xFF, 0xFF, 0x00000000, type)                   \
{                                                                             \
    if (unlikely(rA(ctx->opcode) == 0 ||                                      \
                 rA(ctx->opcode) == rD(ctx->opcode))) {                       \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDUX(width, opc2, opc3, type)                                     \
GEN_HANDLER(l##width##ux, 0x1F, opc2, opc3, 0x00000001, type)                 \
{                                                                             \
    if (unlikely(rA(ctx->opcode) == 0 ||                                      \
                 rA(ctx->opcode) == rD(ctx->opcode))) {                       \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDX(width, opc2, opc3, type)                                      \
GEN_HANDLER(l##width##x, 0x1F, opc2, opc3, 0x00000001, type)                  \
{                                                                             \
    gen_addr_reg_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_T1_gpr(rD(ctx->opcode));                                     \
}

#define GEN_LDS(width, op, type)                                              \
OP_LD_TABLE(width);                                                           \
GEN_LD(width, op | 0x20, type);                                               \
GEN_LDU(width, op | 0x21, type);                                              \
GEN_LDUX(width, 0x17, op | 0x01, type);                                       \
GEN_LDX(width, 0x17, op | 0x00, type)

/* lbz lbzu lbzux lbzx */
GEN_LDS(bz, 0x02, PPC_INTEGER);
/* lha lhau lhaux lhax */
GEN_LDS(ha, 0x0A, PPC_INTEGER);
/* lhz lhzu lhzux lhzx */
GEN_LDS(hz, 0x08, PPC_INTEGER);
/* lwz lwzu lwzux lwzx */
GEN_LDS(wz, 0x00, PPC_INTEGER);
#if defined(TARGET_PPC64)
OP_LD_TABLE(wa);
OP_LD_TABLE(d);
/* lwaux */
GEN_LDUX(wa, 0x15, 0x0B, PPC_64B);
/* lwax */
GEN_LDX(wa, 0x15, 0x0A, PPC_64B);
/* ldux */
GEN_LDUX(d, 0x15, 0x01, PPC_64B);
/* ldx */
GEN_LDX(d, 0x15, 0x00, PPC_64B);
GEN_HANDLER(ld, 0x3A, 0xFF, 0xFF, 0x00000000, PPC_64B)
{
    if (Rc(ctx->opcode)) {
        if (unlikely(rA(ctx->opcode) == 0 ||
                     rA(ctx->opcode) == rD(ctx->opcode))) {
            RET_INVAL(ctx);
            return;
        }
    }
    gen_addr_imm_index(ctx);
    if (ctx->opcode & 0x02) {
        /* lwa (lwau is undefined) */
        op_ldst(lwa);
    } else {
        /* ld - ldu */
        op_ldst(ld);
    }
    gen_op_store_T1_gpr(rD(ctx->opcode));
    if (Rc(ctx->opcode))
        gen_op_store_T0_gpr(rA(ctx->opcode));
}
#endif

/***                              Integer store                            ***/
#define GEN_ST(width, opc, type)                                              \
GEN_HANDLER(st##width, opc, 0xFF, 0xFF, 0x00000000, type)                     \
{                                                                             \
    gen_addr_imm_index(ctx);                                                  \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
}

#define GEN_STU(width, opc, type)                                             \
GEN_HANDLER(st##width##u, opc, 0xFF, 0xFF, 0x00000000, type)                  \
{                                                                             \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STUX(width, opc2, opc3, type)                                     \
GEN_HANDLER(st##width##ux, 0x1F, opc2, opc3, 0x00000001, type)                \
{                                                                             \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STX(width, opc2, opc3, type)                                      \
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, type)                 \
{                                                                             \
    gen_addr_reg_index(ctx);                                                  \
    gen_op_load_gpr_T1(rS(ctx->opcode));                                      \
    op_ldst(st##width);                                                       \
}

#define GEN_STS(width, op, type)                                              \
OP_ST_TABLE(width);                                                           \
GEN_ST(width, op | 0x20, type);                                               \
GEN_STU(width, op | 0x21, type);                                              \
GEN_STUX(width, 0x17, op | 0x01, type);                                       \
GEN_STX(width, 0x17, op | 0x00, type)

/* stb stbu stbux stbx */
GEN_STS(b, 0x06, PPC_INTEGER);
/* sth sthu sthux sthx */
GEN_STS(h, 0x0C, PPC_INTEGER);
/* stw stwu stwux stwx */
GEN_STS(w, 0x04, PPC_INTEGER);
#if defined(TARGET_PPC64)
OP_ST_TABLE(d);
GEN_STUX(d, 0x15, 0x05, PPC_64B);
GEN_STX(d, 0x15, 0x04, PPC_64B);
GEN_HANDLER(std, 0x3E, 0xFF, 0xFF, 0x00000002, PPC_64B)
{
    if (Rc(ctx->opcode)) {
        if (unlikely(rA(ctx->opcode) == 0)) {
            RET_INVAL(ctx);
            return;
        }
    }
    gen_addr_imm_index(ctx);
    gen_op_load_gpr_T1(rS(ctx->opcode));
    op_ldst(std);
    if (Rc(ctx->opcode))
        gen_op_store_T0_gpr(rA(ctx->opcode));
}
#endif
/***                Integer load and store with byte reverse               ***/
/* lhbrx */
OP_LD_TABLE(hbr);
GEN_LDX(hbr, 0x16, 0x18, PPC_INTEGER);
/* lwbrx */
OP_LD_TABLE(wbr);
GEN_LDX(wbr, 0x16, 0x10, PPC_INTEGER);
/* sthbrx */
OP_ST_TABLE(hbr);
GEN_STX(hbr, 0x16, 0x1C, PPC_INTEGER);
/* stwbrx */
OP_ST_TABLE(wbr);
GEN_STX(wbr, 0x16, 0x14, PPC_INTEGER);

/***                    Integer load and store multiple                    ***/
#define op_ldstm(name, reg) (*gen_op_##name[ctx->mem_idx])(reg)
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc1 *gen_op_lmw[] = {
    &gen_op_lmw_raw,
    &gen_op_lmw_le_raw,
    &gen_op_lmw_64_raw,
    &gen_op_lmw_le_64_raw,
};
static GenOpFunc1 *gen_op_stmw[] = {
    &gen_op_stmw_64_raw,
    &gen_op_stmw_le_64_raw,
};
#else
static GenOpFunc1 *gen_op_lmw[] = {
    &gen_op_lmw_user,
    &gen_op_lmw_le_user,
    &gen_op_lmw_kernel,
    &gen_op_lmw_le_kernel,
    &gen_op_lmw_64_user,
    &gen_op_lmw_le_64_user,
    &gen_op_lmw_64_kernel,
    &gen_op_lmw_le_64_kernel,
};
static GenOpFunc1 *gen_op_stmw[] = {
    &gen_op_stmw_user,
    &gen_op_stmw_le_user,
    &gen_op_stmw_kernel,
    &gen_op_stmw_le_kernel,
    &gen_op_stmw_64_user,
    &gen_op_stmw_le_64_user,
    &gen_op_stmw_64_kernel,
    &gen_op_stmw_le_64_kernel,
};
#endif
#else
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
#endif

/* lmw */
GEN_HANDLER(lmw, 0x2E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    op_ldstm(lmw, rD(ctx->opcode));
}

/* stmw */
GEN_HANDLER(stmw, 0x2F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    op_ldstm(stmw, rS(ctx->opcode));
}

/***                    Integer load and store strings                     ***/
#define op_ldsts(name, start) (*gen_op_##name[ctx->mem_idx])(start)
#define op_ldstsx(name, rd, ra, rb) (*gen_op_##name[ctx->mem_idx])(rd, ra, rb)
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc1 *gen_op_lswi[] = {
    &gen_op_lswi_raw,
    &gen_op_lswi_le_raw,
    &gen_op_lswi_64_raw,
    &gen_op_lswi_le_64_raw,
};
static GenOpFunc3 *gen_op_lswx[] = {
    &gen_op_lswx_raw,
    &gen_op_lswx_le_raw,
    &gen_op_lswx_64_raw,
    &gen_op_lswx_le_64_raw,
};
static GenOpFunc1 *gen_op_stsw[] = {
    &gen_op_stsw_raw,
    &gen_op_stsw_le_raw,
    &gen_op_stsw_64_raw,
    &gen_op_stsw_le_64_raw,
};
#else
static GenOpFunc1 *gen_op_lswi[] = {
    &gen_op_lswi_user,
    &gen_op_lswi_le_user,
    &gen_op_lswi_kernel,
    &gen_op_lswi_le_kernel,
    &gen_op_lswi_64_user,
    &gen_op_lswi_le_64_user,
    &gen_op_lswi_64_kernel,
    &gen_op_lswi_le_64_kernel,
};
static GenOpFunc3 *gen_op_lswx[] = {
    &gen_op_lswx_user,
    &gen_op_lswx_le_user,
    &gen_op_lswx_kernel,
    &gen_op_lswx_le_kernel,
    &gen_op_lswx_64_user,
    &gen_op_lswx_le_64_user,
    &gen_op_lswx_64_kernel,
    &gen_op_lswx_le_64_kernel,
};
static GenOpFunc1 *gen_op_stsw[] = {
    &gen_op_stsw_user,
    &gen_op_stsw_le_user,
    &gen_op_stsw_kernel,
    &gen_op_stsw_le_kernel,
    &gen_op_stsw_64_user,
    &gen_op_stsw_le_64_user,
    &gen_op_stsw_64_kernel,
    &gen_op_stsw_le_64_kernel,
};
#endif
#else
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
    if (unlikely(((start + nr) > 32  &&
                  start <= ra && (start + nr - 32) > ra) ||
                 ((start + nr) <= 32 && start <= ra && (start + nr) > ra))) {
        RET_EXCP(ctx, EXCP_PROGRAM, EXCP_INVAL | EXCP_INVAL_LSWX);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_register(ctx);
    gen_op_set_T1(nb);
    op_ldsts(lswi, start);
}

/* lswx */
GEN_HANDLER(lswx, 0x1F, 0x15, 0x10, 0x00000001, PPC_INTEGER)
{
    int ra = rA(ctx->opcode);
    int rb = rB(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    if (ra == 0) {
        ra = rb;
    }
    gen_op_load_xer_bc();
    op_ldstsx(lswx, rD(ctx->opcode), ra, rb);
}

/* stswi */
GEN_HANDLER(stswi, 0x1F, 0x15, 0x16, 0x00000001, PPC_INTEGER)
{
    int nb = NB(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_register(ctx);
    if (nb == 0)
        nb = 32;
    gen_op_set_T1(nb);
    op_ldsts(stsw, rS(ctx->opcode));
}

/* stswx */
GEN_HANDLER(stswx, 0x1F, 0x15, 0x14, 0x00000001, PPC_INTEGER)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4); 
    gen_addr_reg_index(ctx);
    gen_op_load_xer_bc();
    op_ldsts(stsw, rS(ctx->opcode));
}

/***                        Memory synchronisation                         ***/
/* eieio */
GEN_HANDLER(eieio, 0x1F, 0x16, 0x1A, 0x03FF0801, PPC_MEM_EIEIO)
{
}

/* isync */
GEN_HANDLER(isync, 0x13, 0x16, 0x04, 0x03FF0801, PPC_MEM)
{
}

#define op_lwarx() (*gen_op_lwarx[ctx->mem_idx])()
#define op_stwcx() (*gen_op_stwcx[ctx->mem_idx])()
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_lwarx[] = {
    &gen_op_lwarx_raw,
    &gen_op_lwarx_le_raw,
    &gen_op_lwarx_64_raw,
    &gen_op_lwarx_le_64_raw,
};
static GenOpFunc *gen_op_stwcx[] = {
    &gen_op_stwcx_raw,
    &gen_op_stwcx_le_raw,
    &gen_op_stwcx_64_raw,
    &gen_op_stwcx_le_64_raw,
};
#else
static GenOpFunc *gen_op_lwarx[] = {
    &gen_op_lwarx_user,
    &gen_op_lwarx_le_user,
    &gen_op_lwarx_kernel,
    &gen_op_lwarx_le_kernel,
    &gen_op_lwarx_64_user,
    &gen_op_lwarx_le_64_user,
    &gen_op_lwarx_64_kernel,
    &gen_op_lwarx_le_64_kernel,
};
static GenOpFunc *gen_op_stwcx[] = {
    &gen_op_stwcx_user,
    &gen_op_stwcx_le_user,
    &gen_op_stwcx_kernel,
    &gen_op_stwcx_le_kernel,
    &gen_op_stwcx_64_user,
    &gen_op_stwcx_le_64_user,
    &gen_op_stwcx_64_kernel,
    &gen_op_stwcx_le_64_kernel,
};
#endif
#else
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
#endif

/* lwarx */
GEN_HANDLER(lwarx, 0x1F, 0x14, 0x00, 0x00000001, PPC_RES)
{
    gen_addr_reg_index(ctx);
    op_lwarx();
    gen_op_store_T1_gpr(rD(ctx->opcode));
}

/* stwcx. */
GEN_HANDLER(stwcx_, 0x1F, 0x16, 0x04, 0x00000000, PPC_RES)
{
    gen_addr_reg_index(ctx);
    gen_op_load_gpr_T1(rS(ctx->opcode));
    op_stwcx();
}

#if defined(TARGET_PPC64)
#define op_ldarx() (*gen_op_ldarx[ctx->mem_idx])()
#define op_stdcx() (*gen_op_stdcx[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_ldarx[] = {
    &gen_op_ldarx_raw,
    &gen_op_ldarx_le_raw,
    &gen_op_ldarx_64_raw,
    &gen_op_ldarx_le_64_raw,
};
static GenOpFunc *gen_op_stdcx[] = {
    &gen_op_stdcx_raw,
    &gen_op_stdcx_le_raw,
    &gen_op_stdcx_64_raw,
    &gen_op_stdcx_le_64_raw,
};
#else
static GenOpFunc *gen_op_ldarx[] = {
    &gen_op_ldarx_user,
    &gen_op_ldarx_le_user,
    &gen_op_ldarx_kernel,
    &gen_op_ldarx_le_kernel,
    &gen_op_ldarx_64_user,
    &gen_op_ldarx_le_64_user,
    &gen_op_ldarx_64_kernel,
    &gen_op_ldarx_le_64_kernel,
};
static GenOpFunc *gen_op_stdcx[] = {
    &gen_op_stdcx_user,
    &gen_op_stdcx_le_user,
    &gen_op_stdcx_kernel,
    &gen_op_stdcx_le_kernel,
    &gen_op_stdcx_64_user,
    &gen_op_stdcx_le_64_user,
    &gen_op_stdcx_64_kernel,
    &gen_op_stdcx_le_64_kernel,
};
#endif

/* ldarx */
GEN_HANDLER(ldarx, 0x1F, 0x14, 0x02, 0x00000001, PPC_RES)
{
    gen_addr_reg_index(ctx);
    op_ldarx();
    gen_op_store_T1_gpr(rD(ctx->opcode));
}

/* stdcx. */
GEN_HANDLER(stdcx_, 0x1F, 0x16, 0x06, 0x00000000, PPC_RES)
{
    gen_addr_reg_index(ctx);
    gen_op_load_gpr_T1(rS(ctx->opcode));
    op_stdcx();
}
#endif /* defined(TARGET_PPC64) */

/* sync */
GEN_HANDLER(sync, 0x1F, 0x16, 0x12, 0x03FF0801, PPC_MEM_SYNC)
{
}

/***                         Floating-point load                           ***/
#define GEN_LDF(width, opc)                                                   \
GEN_HANDLER(l##width, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)                 \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
}

#define GEN_LDUF(width, opc)                                                  \
GEN_HANDLER(l##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)              \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDUXF(width, opc)                                                 \
GEN_HANDLER(l##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)             \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_LDXF(width, opc2, opc3)                                           \
GEN_HANDLER(l##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_FLOAT)             \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    op_ldst(l##width);                                                        \
    gen_op_store_FT0_fpr(rD(ctx->opcode));                                    \
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
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    gen_op_load_fpr_FT0(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
}

#define GEN_STUF(width, opc)                                                  \
GEN_HANDLER(st##width##u, opc, 0xFF, 0xFF, 0x00000000, PPC_FLOAT)             \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_imm_index(ctx);                                                  \
    gen_op_load_fpr_FT0(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STUXF(width, opc)                                                 \
GEN_HANDLER(st##width##ux, 0x1F, 0x17, opc, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        RET_INVAL(ctx);                                                       \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    gen_op_load_fpr_FT0(rS(ctx->opcode));                                     \
    op_ldst(st##width);                                                       \
    gen_op_store_T0_gpr(rA(ctx->opcode));                                     \
}

#define GEN_STXF(width, opc2, opc3)                                           \
GEN_HANDLER(st##width##x, 0x1F, opc2, opc3, 0x00000001, PPC_FLOAT)            \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_FP, 0);                                         \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    gen_op_load_fpr_FT0(rS(ctx->opcode));                                     \
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
    if (unlikely(!ctx->fpu_enabled)) {
        RET_EXCP(ctx, EXCP_NO_FP, 0);
        return;
    }
    gen_addr_reg_index(ctx);
    /* XXX: TODO: memcpy low order 32 bits of FRP(rs) into memory */
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
        gen_set_T1(dest);
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_b_T1_64();
        else
#endif
            gen_op_b_T1();
        gen_op_set_T0((long)tb + n);
        if (ctx->singlestep_enabled)
            gen_op_debug();
        gen_op_exit_tb();
    } else {
        gen_set_T1(dest);
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_b_T1_64();
        else
#endif
            gen_op_b_T1();
        gen_op_reset_T0();
        if (ctx->singlestep_enabled)
            gen_op_debug();
        gen_op_exit_tb();
    }
}

/* b ba bl bla */
GEN_HANDLER(b, 0x12, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    target_ulong li, target;

    /* sign extend LI */
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        li = ((int64_t)LI(ctx->opcode) << 38) >> 38;
    else
#endif
        li = ((int32_t)LI(ctx->opcode) << 6) >> 6;
    if (likely(AA(ctx->opcode) == 0))
        target = ctx->nip + li - 4;
    else
        target = li;
    if (LK(ctx->opcode)) {
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_setlr_64(ctx->nip >> 32, ctx->nip);
        else
#endif
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
    target_ulong target = 0;
    target_ulong li;
    uint32_t bo = BO(ctx->opcode);
    uint32_t bi = BI(ctx->opcode);
    uint32_t mask;

    if ((bo & 0x4) == 0)
        gen_op_dec_ctr();
    switch(type) {
    case BCOND_IM:
        li = (target_long)((int16_t)(BD(ctx->opcode)));
        if (likely(AA(ctx->opcode) == 0)) {
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
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_setlr_64(ctx->nip >> 32, ctx->nip);
        else
#endif
            gen_op_setlr(ctx->nip);
    }
    if (bo & 0x10) {
        /* No CR condition */
        switch (bo & 0x6) {
        case 0:
#if defined(TARGET_PPC64)
            if (ctx->sf_mode)
                gen_op_test_ctr_64();
            else
#endif
                gen_op_test_ctr();
            break;
        case 2:
#if defined(TARGET_PPC64)
            if (ctx->sf_mode)
                gen_op_test_ctrz_64();
            else
#endif
                gen_op_test_ctrz();
            break;
        default:
        case 4:
        case 6:
            if (type == BCOND_IM) {
                gen_goto_tb(ctx, 0, target);
            } else {
#if defined(TARGET_PPC64)
                if (ctx->sf_mode)
                    gen_op_b_T1_64();
                else
#endif
                    gen_op_b_T1();
                gen_op_reset_T0();
            }
            goto no_test;
        }
    } else {
        mask = 1 << (3 - (bi & 0x03));
        gen_op_load_crf_T0(bi >> 2);
        if (bo & 0x8) {
            switch (bo & 0x6) {
            case 0:
#if defined(TARGET_PPC64)
                if (ctx->sf_mode)
                    gen_op_test_ctr_true_64(mask);
                else
#endif
                    gen_op_test_ctr_true(mask);
                break;
            case 2:
#if defined(TARGET_PPC64)
                if (ctx->sf_mode)
                    gen_op_test_ctrz_true_64(mask);
                else
#endif
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
#if defined(TARGET_PPC64)
                if (ctx->sf_mode)
                    gen_op_test_ctr_false_64(mask);
                else
#endif
                    gen_op_test_ctr_false(mask);
                break;                           
            case 2:
#if defined(TARGET_PPC64)
                if (ctx->sf_mode)
                    gen_op_test_ctrz_false_64(mask);
                else
#endif
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
#if defined(TARGET_PPC64)
        if (ctx->sf_mode)
            gen_op_btest_T1_64(ctx->nip >> 32, ctx->nip);
        else
#endif
            gen_op_btest_T1(ctx->nip);
        gen_op_reset_T0();
    }
 no_test:
    if (ctx->singlestep_enabled)
        gen_op_debug();
    gen_op_exit_tb();
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
GEN_CRLOGIC(and, 0x08);
/* crandc */
GEN_CRLOGIC(andc, 0x04);
/* creqv */
GEN_CRLOGIC(eqv, 0x09);
/* crnand */
GEN_CRLOGIC(nand, 0x07);
/* crnor */
GEN_CRLOGIC(nor, 0x01);
/* cror */
GEN_CRLOGIC(or, 0x0E);
/* crorc */
GEN_CRLOGIC(orc, 0x0D);
/* crxor */
GEN_CRLOGIC(xor, 0x06);
/* mcrf */
GEN_HANDLER(mcrf, 0x13, 0x00, 0xFF, 0x00000001, PPC_INTEGER)
{
    gen_op_load_crf_T0(crfS(ctx->opcode));
    gen_op_store_T0_crf(crfD(ctx->opcode));
}

/***                           System linkage                              ***/
/* rfi (supervisor only) */
GEN_HANDLER(rfi, 0x13, 0x12, 0x01, 0x03FF8001, PPC_FLOW)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    /* Restore CPU state */
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_rfi();
    RET_CHG_FLOW(ctx);
#endif
}

#if defined(TARGET_PPC64)
GEN_HANDLER(rfid, 0x13, 0x12, 0x00, 0x03FF8001, PPC_FLOW)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    /* Restore CPU state */
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_rfid();
    RET_CHG_FLOW(ctx);
#endif
}
#endif

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
GEN_HANDLER(tw, 0x1F, 0x04, 0x00, 0x00000001, PPC_FLOW)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_op_tw(TO(ctx->opcode));
}

/* twi */
GEN_HANDLER(twi, 0x03, 0xFF, 0xFF, 0x00000000, PPC_FLOW)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_set_T1(SIMM(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_op_tw(TO(ctx->opcode));
}

#if defined(TARGET_PPC64)
/* td */
GEN_HANDLER(td, 0x1F, 0x04, 0x02, 0x00000001, PPC_64B)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_op_td(TO(ctx->opcode));
}

/* tdi */
GEN_HANDLER(tdi, 0x02, 0xFF, 0xFF, 0x00000000, PPC_64B)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_set_T1(SIMM(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_op_td(TO(ctx->opcode));
}
#endif

/***                          Processor control                            ***/
/* mcrxr */
GEN_HANDLER(mcrxr, 0x1F, 0x00, 0x10, 0x007FF801, PPC_MISC)
{
    gen_op_load_xer_cr();
    gen_op_store_T0_crf(crfD(ctx->opcode));
    gen_op_clear_xer_ov();
    gen_op_clear_xer_ca();
}

/* mfcr */
GEN_HANDLER(mfcr, 0x1F, 0x13, 0x00, 0x00000801, PPC_MISC)
{
    uint32_t crm, crn;
    
    if (likely(ctx->opcode & 0x00100000)) {
        crm = CRM(ctx->opcode);
        if (likely((crm ^ (crm - 1)) == 0)) {
            crn = ffs(crm);
            gen_op_load_cro(7 - crn);
        }
    } else {
        gen_op_load_cr();
    }
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* mfmsr */
GEN_HANDLER(mfmsr, 0x1F, 0x13, 0x02, 0x001FF801, PPC_MISC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
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
    if (likely(read_cb != NULL)) {
        if (likely(read_cb != SPR_NOACCESS)) {
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
GEN_HANDLER(mtcrf, 0x1F, 0x10, 0x04, 0x00000801, PPC_MISC)
{
    uint32_t crm, crn;
    
    gen_op_load_gpr_T0(rS(ctx->opcode));
    crm = CRM(ctx->opcode);
    if (likely((ctx->opcode & 0x00100000) || (crm ^ (crm - 1)) == 0)) {
        crn = ffs(crm);
        gen_op_srli_T0(crn * 4);
        gen_op_andi_T0(0xF);
        gen_op_store_cro(7 - crn);
    } else {
        gen_op_store_cr(crm);
    }
}

/* mtmsr */
#if defined(TARGET_PPC64)
GEN_HANDLER(mtmsrd, 0x1F, 0x12, 0x05, 0x001FF801, PPC_MISC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_update_nip(ctx, ctx->nip);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_store_msr();
    /* Must stop the translation as machine state (may have) changed */
    RET_CHG_FLOW(ctx);
#endif
}
#endif

GEN_HANDLER(mtmsr, 0x1F, 0x12, 0x04, 0x001FF801, PPC_MISC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_update_nip(ctx, ctx->nip);
    gen_op_load_gpr_T0(rS(ctx->opcode));
#if defined(TARGET_PPC64)
    if (!ctx->sf_mode)
        gen_op_store_msr_32();
    else
#endif
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
    if (likely(write_cb != NULL)) {
        if (likely(write_cb != SPR_NOACCESS)) {
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
    gen_addr_reg_index(ctx);
    op_ldst(lbz);
}

/* dcbi (Supervisor only) */
GEN_HANDLER(dcbi, 0x1F, 0x16, 0x0E, 0x03E00001, PPC_CACHE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_addr_reg_index(ctx);
    /* XXX: specification says this should be treated as a store by the MMU */
    //op_ldst(lbz);
    op_ldst(stb);
#endif
}

/* dcdst */
GEN_HANDLER(dcbst, 0x1F, 0x16, 0x01, 0x03E00001, PPC_CACHE)
{
    /* XXX: specification say this is treated as a load by the MMU */
    gen_addr_reg_index(ctx);
    op_ldst(lbz);
}

/* dcbt */
GEN_HANDLER(dcbt, 0x1F, 0x16, 0x08, 0x03E00001, PPC_CACHE)
{
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* dcbtst */
GEN_HANDLER(dcbtst, 0x1F, 0x16, 0x07, 0x03E00001, PPC_CACHE)
{
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* dcbz */
#define op_dcbz() (*gen_op_dcbz[ctx->mem_idx])()
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_dcbz[] = {
    &gen_op_dcbz_raw,
    &gen_op_dcbz_raw,
    &gen_op_dcbz_64_raw,
    &gen_op_dcbz_64_raw,
};
#else
static GenOpFunc *gen_op_dcbz[] = {
    &gen_op_dcbz_user,
    &gen_op_dcbz_user,
    &gen_op_dcbz_kernel,
    &gen_op_dcbz_kernel,
    &gen_op_dcbz_64_user,
    &gen_op_dcbz_64_user,
    &gen_op_dcbz_64_kernel,
    &gen_op_dcbz_64_kernel,
};
#endif
#else
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_dcbz[] = {
    &gen_op_dcbz_raw,
    &gen_op_dcbz_raw,
};
#else
static GenOpFunc *gen_op_dcbz[] = {
    &gen_op_dcbz_user,
    &gen_op_dcbz_user,
    &gen_op_dcbz_kernel,
    &gen_op_dcbz_kernel,
};
#endif
#endif

GEN_HANDLER(dcbz, 0x1F, 0x16, 0x1F, 0x03E00001, PPC_CACHE)
{
    gen_addr_reg_index(ctx);
    op_dcbz();
    gen_op_check_reservation();
}

/* icbi */
#define op_icbi() (*gen_op_icbi[ctx->mem_idx])()
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_icbi[] = {
    &gen_op_icbi_raw,
    &gen_op_icbi_raw,
    &gen_op_icbi_64_raw,
    &gen_op_icbi_64_raw,
};
#else
static GenOpFunc *gen_op_icbi[] = {
    &gen_op_icbi_user,
    &gen_op_icbi_user,
    &gen_op_icbi_kernel,
    &gen_op_icbi_kernel,
    &gen_op_icbi_64_user,
    &gen_op_icbi_64_user,
    &gen_op_icbi_64_kernel,
    &gen_op_icbi_64_kernel,
};
#endif
#else
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_icbi[] = {
    &gen_op_icbi_raw,
    &gen_op_icbi_raw,
};
#else
static GenOpFunc *gen_op_icbi[] = {
    &gen_op_icbi_user,
    &gen_op_icbi_user,
    &gen_op_icbi_kernel,
    &gen_op_icbi_kernel,
};
#endif
#endif
GEN_HANDLER(icbi, 0x1F, 0x16, 0x1E, 0x03E00001, PPC_CACHE)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    op_icbi();
    RET_STOP(ctx);
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
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_set_T1(SR(ctx->opcode));
    gen_op_load_sr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mfsrin */
GEN_HANDLER(mfsrin, 0x1F, 0x13, 0x14, 0x001F0001, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_srli_T1(28);
    gen_op_load_sr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mtsr */
GEN_HANDLER(mtsr, 0x1F, 0x12, 0x06, 0x0010F801, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_set_T1(SR(ctx->opcode));
    gen_op_store_sr();
    RET_STOP(ctx);
#endif
}

/* mtsrin */
GEN_HANDLER(mtsrin, 0x1F, 0x12, 0x07, 0x001F0001, PPC_SEGMENT)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_srli_T1(28);
    gen_op_store_sr();
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
    if (unlikely(!ctx->supervisor)) {
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
GEN_HANDLER(tlbie, 0x1F, 0x12, 0x09, 0x03FF0001, PPC_MEM_TLBIE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rB(ctx->opcode));
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_tlbie_64();
    else
#endif
        gen_op_tlbie();
    RET_STOP(ctx);
#endif
}

/* tlbsync */
GEN_HANDLER(tlbsync, 0x1F, 0x16, 0x11, 0x03FFF801, PPC_MEM_TLBSYNC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* This has no effect: it should ensure that all previous
     * tlbie have completed
     */
    RET_STOP(ctx);
#endif
}

#if defined(TARGET_PPC64)
/* slbia */
GEN_HANDLER(slbia, 0x1F, 0x12, 0x0F, 0x03FFFC01, PPC_SLBI)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        if (loglevel)
            fprintf(logfile, "%s: ! supervisor\n", __func__);
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_slbia();
    RET_STOP(ctx);
#endif
}

/* slbie */
GEN_HANDLER(slbie, 0x1F, 0x12, 0x0D, 0x03FF0001, PPC_SLBI)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rB(ctx->opcode));
    gen_op_slbie();
    RET_STOP(ctx);
#endif
}
#endif

/***                              External control                         ***/
/* Optional: */
#define op_eciwx() (*gen_op_eciwx[ctx->mem_idx])()
#define op_ecowx() (*gen_op_ecowx[ctx->mem_idx])()
#if defined(TARGET_PPC64)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_eciwx[] = {
    &gen_op_eciwx_raw,
    &gen_op_eciwx_le_raw,
    &gen_op_eciwx_64_raw,
    &gen_op_eciwx_le_64_raw,
};
static GenOpFunc *gen_op_ecowx[] = {
    &gen_op_ecowx_raw,
    &gen_op_ecowx_le_raw,
    &gen_op_ecowx_64_raw,
    &gen_op_ecowx_le_64_raw,
};
#else
static GenOpFunc *gen_op_eciwx[] = {
    &gen_op_eciwx_user,
    &gen_op_eciwx_le_user,
    &gen_op_eciwx_kernel,
    &gen_op_eciwx_le_kernel,
    &gen_op_eciwx_64_user,
    &gen_op_eciwx_le_64_user,
    &gen_op_eciwx_64_kernel,
    &gen_op_eciwx_le_64_kernel,
};
static GenOpFunc *gen_op_ecowx[] = {
    &gen_op_ecowx_user,
    &gen_op_ecowx_le_user,
    &gen_op_ecowx_kernel,
    &gen_op_ecowx_le_kernel,
    &gen_op_ecowx_64_user,
    &gen_op_ecowx_le_64_user,
    &gen_op_ecowx_64_kernel,
    &gen_op_ecowx_le_64_kernel,
};
#endif
#else
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
#endif

/* eciwx */
GEN_HANDLER(eciwx, 0x1F, 0x16, 0x0D, 0x00000001, PPC_EXTERN)
{
    /* Should check EAR[E] & alignment ! */
    gen_addr_reg_index(ctx);
    op_eciwx();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* ecowx */
GEN_HANDLER(ecowx, 0x1F, 0x16, 0x09, 0x00000001, PPC_EXTERN)
{
    /* Should check EAR[E] & alignment ! */
    gen_addr_reg_index(ctx);
    gen_op_load_gpr_T1(rS(ctx->opcode));
    op_ecowx();
}

/* PowerPC 601 specific instructions */
/* abs - abs. */
GEN_HANDLER(abs, 0x1F, 0x08, 0x0B, 0x0000F800, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_POWER_abs();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* abso - abso. */
GEN_HANDLER(abso, 0x1F, 0x08, 0x1B, 0x0000F800, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_POWER_abso();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* clcs */
GEN_HANDLER(clcs, 0x1F, 0x10, 0x13, 0x0000F800, PPC_POWER_BR) /* 601 ? */
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_POWER_clcs();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* div - div. */
GEN_HANDLER(div, 0x1F, 0x0B, 0x0A, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_div();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* divo - divo. */
GEN_HANDLER(divo, 0x1F, 0x0B, 0x1A, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_divo();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* divs - divs. */
GEN_HANDLER(divs, 0x1F, 0x0B, 0x0B, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_divs();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* divso - divso. */
GEN_HANDLER(divso, 0x1F, 0x0B, 0x1B, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_divso();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* doz - doz. */
GEN_HANDLER(doz, 0x1F, 0x08, 0x08, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_doz();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* dozo - dozo. */
GEN_HANDLER(dozo, 0x1F, 0x08, 0x18, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_dozo();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* dozi */
GEN_HANDLER(dozi, 0x09, 0xFF, 0xFF, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_set_T1(SIMM(ctx->opcode));
    gen_op_POWER_doz();
    gen_op_store_T0_gpr(rD(ctx->opcode));
}

/* As lscbx load from memory byte after byte, it's always endian safe */
#define op_POWER_lscbx(start, ra, rb) \
(*gen_op_POWER_lscbx[ctx->mem_idx])(start, ra, rb)
#if defined(CONFIG_USER_ONLY)
static GenOpFunc3 *gen_op_POWER_lscbx[] = {
    &gen_op_POWER_lscbx_raw,
    &gen_op_POWER_lscbx_raw,
};
#else
static GenOpFunc3 *gen_op_POWER_lscbx[] = {
    &gen_op_POWER_lscbx_user,
    &gen_op_POWER_lscbx_user,
    &gen_op_POWER_lscbx_kernel,
    &gen_op_POWER_lscbx_kernel,
};
#endif

/* lscbx - lscbx. */
GEN_HANDLER(lscbx, 0x1F, 0x15, 0x08, 0x00000000, PPC_POWER_BR)
{
    int ra = rA(ctx->opcode);
    int rb = rB(ctx->opcode);

    gen_addr_reg_index(ctx);
    if (ra == 0) {
        ra = rb;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_op_load_xer_bc();
    gen_op_load_xer_cmp();
    op_POWER_lscbx(rD(ctx->opcode), ra, rb);
    gen_op_store_xer_bc();
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* maskg - maskg. */
GEN_HANDLER(maskg, 0x1F, 0x1D, 0x00, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_maskg();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* maskir - maskir. */
GEN_HANDLER(maskir, 0x1F, 0x1D, 0x10, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rS(ctx->opcode));
    gen_op_load_gpr_T2(rB(ctx->opcode));
    gen_op_POWER_maskir();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* mul - mul. */
GEN_HANDLER(mul, 0x1F, 0x0B, 0x03, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_mul();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* mulo - mulo. */
GEN_HANDLER(mulo, 0x1F, 0x0B, 0x13, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_mulo();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* nabs - nabs. */
GEN_HANDLER(nabs, 0x1F, 0x08, 0x0F, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_POWER_nabs();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* nabso - nabso. */
GEN_HANDLER(nabso, 0x1F, 0x08, 0x1F, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_POWER_nabso();
    gen_op_store_T0_gpr(rD(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* rlmi - rlmi. */
GEN_HANDLER(rlmi, 0x16, 0xFF, 0xFF, 0x00000000, PPC_POWER_BR)
{
    uint32_t mb, me;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rA(ctx->opcode));
    gen_op_load_gpr_T2(rB(ctx->opcode));
    gen_op_POWER_rlmi(MASK(mb, me), ~MASK(mb, me));
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* rrib - rrib. */
GEN_HANDLER(rrib, 0x1F, 0x19, 0x10, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rA(ctx->opcode));
    gen_op_load_gpr_T2(rB(ctx->opcode));
    gen_op_POWER_rrib();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sle - sle. */
GEN_HANDLER(sle, 0x1F, 0x19, 0x04, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sle();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sleq - sleq. */
GEN_HANDLER(sleq, 0x1F, 0x19, 0x06, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sleq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sliq - sliq. */
GEN_HANDLER(sliq, 0x1F, 0x18, 0x05, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_set_T1(SH(ctx->opcode));
    gen_op_POWER_sle();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* slliq - slliq. */
GEN_HANDLER(slliq, 0x1F, 0x18, 0x07, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_set_T1(SH(ctx->opcode));
    gen_op_POWER_sleq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sllq - sllq. */
GEN_HANDLER(sllq, 0x1F, 0x18, 0x06, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sllq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* slq - slq. */
GEN_HANDLER(slq, 0x1F, 0x18, 0x04, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_slq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sraiq - sraiq. */
GEN_HANDLER(sraiq, 0x1F, 0x18, 0x1D, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_set_T1(SH(ctx->opcode));
    gen_op_POWER_sraq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sraq - sraq. */
GEN_HANDLER(sraq, 0x1F, 0x18, 0x1C, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sraq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sre - sre. */
GEN_HANDLER(sre, 0x1F, 0x19, 0x14, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sre();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* srea - srea. */
GEN_HANDLER(srea, 0x1F, 0x19, 0x1C, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_srea();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sreq */
GEN_HANDLER(sreq, 0x1F, 0x19, 0x16, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_sreq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* sriq */
GEN_HANDLER(sriq, 0x1F, 0x18, 0x15, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_set_T1(SH(ctx->opcode));
    gen_op_POWER_srq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* srliq */
GEN_HANDLER(srliq, 0x1F, 0x18, 0x17, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_set_T1(SH(ctx->opcode));
    gen_op_POWER_srlq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* srlq */
GEN_HANDLER(srlq, 0x1F, 0x18, 0x16, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_srlq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* srq */
GEN_HANDLER(srq, 0x1F, 0x18, 0x14, 0x00000000, PPC_POWER_BR)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_POWER_srq();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx);
}

/* PowerPC 602 specific instructions */
/* dsa  */
GEN_HANDLER(dsa, 0x1F, 0x14, 0x13, 0x03FFF801, PPC_602_SPEC)
{
    /* XXX: TODO */
    RET_INVAL(ctx);
}

/* esa */
GEN_HANDLER(esa, 0x1F, 0x14, 0x12, 0x03FFF801, PPC_602_SPEC)
{
    /* XXX: TODO */
    RET_INVAL(ctx);
}

/* mfrom */
GEN_HANDLER(mfrom, 0x1F, 0x09, 0x08, 0x03E0F801, PPC_602_SPEC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_602_mfrom();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* 602 - 603 - G2 TLB management */
/* tlbld */
GEN_HANDLER(tlbld, 0x1F, 0x12, 0x1E, 0x03FF0001, PPC_6xx_TLB)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rB(ctx->opcode));
    gen_op_6xx_tlbld();
    RET_STOP(ctx);
#endif
}

/* tlbli */
GEN_HANDLER(tlbli, 0x1F, 0x12, 0x1F, 0x03FF0001, PPC_6xx_TLB)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rB(ctx->opcode));
    gen_op_6xx_tlbli();
    RET_STOP(ctx);
#endif
}

/* POWER instructions not in PowerPC 601 */
/* clf */
GEN_HANDLER(clf, 0x1F, 0x16, 0x03, 0x03E00000, PPC_POWER)
{
    /* Cache line flush: implemented as no-op */
}

/* cli */
GEN_HANDLER(cli, 0x1F, 0x16, 0x0F, 0x03E00000, PPC_POWER)
{
    /* Cache line invalidate: priviledged and treated as no-op */
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
#endif
}

/* dclst */
GEN_HANDLER(dclst, 0x1F, 0x16, 0x13, 0x03E00000, PPC_POWER)
{
    /* Data cache line store: treated as no-op */
}

GEN_HANDLER(mfsri, 0x1F, 0x13, 0x13, 0x00000001, PPC_POWER)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);

    gen_addr_reg_index(ctx);
    gen_op_POWER_mfsri();
    gen_op_store_T0_gpr(rd);
    if (ra != 0 && ra != rd)
        gen_op_store_T1_gpr(ra);
#endif
}

GEN_HANDLER(rac, 0x1F, 0x12, 0x19, 0x00000001, PPC_POWER)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_addr_reg_index(ctx);
    gen_op_POWER_rac();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

GEN_HANDLER(rfsvc, 0x13, 0x12, 0x02, 0x03FFF0001, PPC_POWER)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_POWER_rfsvc();
    RET_CHG_FLOW(ctx);
#endif
}

/* svc is not implemented for now */

/* POWER2 specific instructions */
/* Quad manipulation (load/store two floats at a time) */
#define op_POWER2_lfq() (*gen_op_POWER2_lfq[ctx->mem_idx])()
#define op_POWER2_stfq() (*gen_op_POWER2_stfq[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
static GenOpFunc *gen_op_POWER2_lfq[] = {
    &gen_op_POWER2_lfq_le_raw,
    &gen_op_POWER2_lfq_raw,
};
static GenOpFunc *gen_op_POWER2_stfq[] = {
    &gen_op_POWER2_stfq_le_raw,
    &gen_op_POWER2_stfq_raw,
};
#else
static GenOpFunc *gen_op_POWER2_lfq[] = {
    &gen_op_POWER2_lfq_le_user,
    &gen_op_POWER2_lfq_user,
    &gen_op_POWER2_lfq_le_kernel,
    &gen_op_POWER2_lfq_kernel,
};
static GenOpFunc *gen_op_POWER2_stfq[] = {
    &gen_op_POWER2_stfq_le_user,
    &gen_op_POWER2_stfq_user,
    &gen_op_POWER2_stfq_le_kernel,
    &gen_op_POWER2_stfq_kernel,
};
#endif

/* lfq */
GEN_HANDLER(lfq, 0x38, 0xFF, 0xFF, 0x00000003, PPC_POWER2)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    op_POWER2_lfq();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    gen_op_store_FT1_fpr(rD(ctx->opcode) + 1);
}

/* lfqu */
GEN_HANDLER(lfqu, 0x39, 0xFF, 0xFF, 0x00000003, PPC_POWER2)
{
    int ra = rA(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    op_POWER2_lfq();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    gen_op_store_FT1_fpr(rD(ctx->opcode) + 1);
    if (ra != 0)
        gen_op_store_T0_gpr(ra);
}

/* lfqux */
GEN_HANDLER(lfqux, 0x1F, 0x17, 0x19, 0x00000001, PPC_POWER2)
{
    int ra = rA(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    op_POWER2_lfq();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    gen_op_store_FT1_fpr(rD(ctx->opcode) + 1);
    if (ra != 0)
        gen_op_store_T0_gpr(ra);
}

/* lfqx */
GEN_HANDLER(lfqx, 0x1F, 0x17, 0x18, 0x00000001, PPC_POWER2)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    op_POWER2_lfq();
    gen_op_store_FT0_fpr(rD(ctx->opcode));
    gen_op_store_FT1_fpr(rD(ctx->opcode) + 1);
}

/* stfq */
GEN_HANDLER(stfq, 0x3C, 0xFF, 0xFF, 0x00000003, PPC_POWER2)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    gen_op_load_fpr_FT0(rS(ctx->opcode));
    gen_op_load_fpr_FT1(rS(ctx->opcode) + 1);
    op_POWER2_stfq();
}

/* stfqu */
GEN_HANDLER(stfqu, 0x3D, 0xFF, 0xFF, 0x00000003, PPC_POWER2)
{
    int ra = rA(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_imm_index(ctx);
    gen_op_load_fpr_FT0(rS(ctx->opcode));
    gen_op_load_fpr_FT1(rS(ctx->opcode) + 1);
    op_POWER2_stfq();
    if (ra != 0)
        gen_op_store_T0_gpr(ra);
}

/* stfqux */
GEN_HANDLER(stfqux, 0x1F, 0x17, 0x1D, 0x00000001, PPC_POWER2)
{
    int ra = rA(ctx->opcode);

    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    gen_op_load_fpr_FT0(rS(ctx->opcode));
    gen_op_load_fpr_FT1(rS(ctx->opcode) + 1);
    op_POWER2_stfq();
    if (ra != 0)
        gen_op_store_T0_gpr(ra);
}

/* stfqx */
GEN_HANDLER(stfqx, 0x1F, 0x17, 0x1C, 0x00000001, PPC_POWER2)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_addr_reg_index(ctx);
    gen_op_load_fpr_FT0(rS(ctx->opcode));
    gen_op_load_fpr_FT1(rS(ctx->opcode) + 1);
    op_POWER2_stfq();
}

/* BookE specific instructions */
GEN_HANDLER(mfapidi, 0x1F, 0x13, 0x08, 0x0000F801, PPC_BOOKE)
{
    /* XXX: TODO */
    RET_INVAL(ctx);
}

GEN_HANDLER(tlbiva, 0x1F, 0x12, 0x18, 0x03FFF801, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_addr_reg_index(ctx);
    /* Use the same micro-ops as for tlbie */
#if defined(TARGET_PPC64)
    if (ctx->sf_mode)
        gen_op_tlbie_64();
    else
#endif
        gen_op_tlbie();
    RET_STOP(ctx);
#endif
}

/* All 405 MAC instructions are translated here */
static inline void gen_405_mulladd_insn (DisasContext *ctx, int opc2, int opc3,
                                         int ra, int rb, int rt, int Rc)
{
    gen_op_load_gpr_T0(ra);
    gen_op_load_gpr_T1(rb);
    switch (opc3 & 0x0D) {
    case 0x05:
        /* macchw    - macchw.    - macchwo   - macchwo.   */
        /* macchws   - macchws.   - macchwso  - macchwso.  */
        /* nmacchw   - nmacchw.   - nmacchwo  - nmacchwo.  */
        /* nmacchws  - nmacchws.  - nmacchwso - nmacchwso. */
        /* mulchw - mulchw. */
        gen_op_405_mulchw();
        break;
    case 0x04:
        /* macchwu   - macchwu.   - macchwuo  - macchwuo.  */
        /* macchwsu  - macchwsu.  - macchwsuo - macchwsuo. */
        /* mulchwu - mulchwu. */
        gen_op_405_mulchwu();
        break;
    case 0x01:
        /* machhw    - machhw.    - machhwo   - machhwo.   */
        /* machhws   - machhws.   - machhwso  - machhwso.  */
        /* nmachhw   - nmachhw.   - nmachhwo  - nmachhwo.  */
        /* nmachhws  - nmachhws.  - nmachhwso - nmachhwso. */
        /* mulhhw - mulhhw. */
        gen_op_405_mulhhw();
        break;
    case 0x00:
        /* machhwu   - machhwu.   - machhwuo  - machhwuo.  */
        /* machhwsu  - machhwsu.  - machhwsuo - machhwsuo. */
        /* mulhhwu - mulhhwu. */
        gen_op_405_mulhhwu();
        break;
    case 0x0D:
        /* maclhw    - maclhw.    - maclhwo   - maclhwo.   */
        /* maclhws   - maclhws.   - maclhwso  - maclhwso.  */
        /* nmaclhw   - nmaclhw.   - nmaclhwo  - nmaclhwo.  */
        /* nmaclhws  - nmaclhws.  - nmaclhwso - nmaclhwso. */
        /* mullhw - mullhw. */
        gen_op_405_mullhw();
        break;
    case 0x0C:
        /* maclhwu   - maclhwu.   - maclhwuo  - maclhwuo.  */
        /* maclhwsu  - maclhwsu.  - maclhwsuo - maclhwsuo. */
        /* mullhwu - mullhwu. */
        gen_op_405_mullhwu();
        break;
    }
    if (opc2 & 0x02) {
        /* nmultiply-and-accumulate (0x0E) */
        gen_op_neg();
    }
    if (opc2 & 0x04) {
        /* (n)multiply-and-accumulate (0x0C - 0x0E) */
        gen_op_load_gpr_T2(rt);
        gen_op_move_T1_T0();
        gen_op_405_add_T0_T2();
    }
    if (opc3 & 0x10) {
        /* Check overflow */
        if (opc3 & 0x01)
            gen_op_405_check_ov();
        else
            gen_op_405_check_ovu();
    }
    if (opc3 & 0x02) {
        /* Saturate */
        if (opc3 & 0x01)
            gen_op_405_check_sat();
        else
            gen_op_405_check_satu();
    }
    gen_op_store_T0_gpr(rt);
    if (unlikely(Rc) != 0) {
        /* Update Rc0 */
        gen_set_Rc0(ctx);
    }
}

#define GEN_MAC_HANDLER(name, opc2, opc3)                                     \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_405_MAC)                  \
{                                                                             \
    gen_405_mulladd_insn(ctx, opc2, opc3, rA(ctx->opcode), rB(ctx->opcode),   \
                         rD(ctx->opcode), Rc(ctx->opcode));                   \
}

/* macchw    - macchw.    */
GEN_MAC_HANDLER(macchw, 0x0C, 0x05);
/* macchwo   - macchwo.   */
GEN_MAC_HANDLER(macchwo, 0x0C, 0x15);
/* macchws   - macchws.   */
GEN_MAC_HANDLER(macchws, 0x0C, 0x07);
/* macchwso  - macchwso.  */
GEN_MAC_HANDLER(macchwso, 0x0C, 0x17);
/* macchwsu  - macchwsu.  */
GEN_MAC_HANDLER(macchwsu, 0x0C, 0x06);
/* macchwsuo - macchwsuo. */
GEN_MAC_HANDLER(macchwsuo, 0x0C, 0x16);
/* macchwu   - macchwu.   */
GEN_MAC_HANDLER(macchwu, 0x0C, 0x04);
/* macchwuo  - macchwuo.  */
GEN_MAC_HANDLER(macchwuo, 0x0C, 0x14);
/* machhw    - machhw.    */
GEN_MAC_HANDLER(machhw, 0x0C, 0x01);
/* machhwo   - machhwo.   */
GEN_MAC_HANDLER(machhwo, 0x0C, 0x11);
/* machhws   - machhws.   */
GEN_MAC_HANDLER(machhws, 0x0C, 0x03);
/* machhwso  - machhwso.  */
GEN_MAC_HANDLER(machhwso, 0x0C, 0x13);
/* machhwsu  - machhwsu.  */
GEN_MAC_HANDLER(machhwsu, 0x0C, 0x02);
/* machhwsuo - machhwsuo. */
GEN_MAC_HANDLER(machhwsuo, 0x0C, 0x12);
/* machhwu   - machhwu.   */
GEN_MAC_HANDLER(machhwu, 0x0C, 0x00);
/* machhwuo  - machhwuo.  */
GEN_MAC_HANDLER(machhwuo, 0x0C, 0x10);
/* maclhw    - maclhw.    */
GEN_MAC_HANDLER(maclhw, 0x0C, 0x0D);
/* maclhwo   - maclhwo.   */
GEN_MAC_HANDLER(maclhwo, 0x0C, 0x1D);
/* maclhws   - maclhws.   */
GEN_MAC_HANDLER(maclhws, 0x0C, 0x0F);
/* maclhwso  - maclhwso.  */
GEN_MAC_HANDLER(maclhwso, 0x0C, 0x1F);
/* maclhwu   - maclhwu.   */
GEN_MAC_HANDLER(maclhwu, 0x0C, 0x0C);
/* maclhwuo  - maclhwuo.  */
GEN_MAC_HANDLER(maclhwuo, 0x0C, 0x1C);
/* maclhwsu  - maclhwsu.  */
GEN_MAC_HANDLER(maclhwsu, 0x0C, 0x0E);
/* maclhwsuo - maclhwsuo. */
GEN_MAC_HANDLER(maclhwsuo, 0x0C, 0x1E);
/* nmacchw   - nmacchw.   */
GEN_MAC_HANDLER(nmacchw, 0x0E, 0x05);
/* nmacchwo  - nmacchwo.  */
GEN_MAC_HANDLER(nmacchwo, 0x0E, 0x15);
/* nmacchws  - nmacchws.  */
GEN_MAC_HANDLER(nmacchws, 0x0E, 0x07);
/* nmacchwso - nmacchwso. */
GEN_MAC_HANDLER(nmacchwso, 0x0E, 0x17);
/* nmachhw   - nmachhw.   */
GEN_MAC_HANDLER(nmachhw, 0x0E, 0x01);
/* nmachhwo  - nmachhwo.  */
GEN_MAC_HANDLER(nmachhwo, 0x0E, 0x11);
/* nmachhws  - nmachhws.  */
GEN_MAC_HANDLER(nmachhws, 0x0E, 0x03);
/* nmachhwso - nmachhwso. */
GEN_MAC_HANDLER(nmachhwso, 0x0E, 0x13);
/* nmaclhw   - nmaclhw.   */
GEN_MAC_HANDLER(nmaclhw, 0x0E, 0x0D);
/* nmaclhwo  - nmaclhwo.  */
GEN_MAC_HANDLER(nmaclhwo, 0x0E, 0x1D);
/* nmaclhws  - nmaclhws.  */
GEN_MAC_HANDLER(nmaclhws, 0x0E, 0x0F);
/* nmaclhwso - nmaclhwso. */
GEN_MAC_HANDLER(nmaclhwso, 0x0E, 0x1F);

/* mulchw  - mulchw.  */
GEN_MAC_HANDLER(mulchw, 0x08, 0x05);
/* mulchwu - mulchwu. */
GEN_MAC_HANDLER(mulchwu, 0x08, 0x04);
/* mulhhw  - mulhhw.  */
GEN_MAC_HANDLER(mulhhw, 0x08, 0x01);
/* mulhhwu - mulhhwu. */
GEN_MAC_HANDLER(mulhhwu, 0x08, 0x00);
/* mullhw  - mullhw.  */
GEN_MAC_HANDLER(mullhw, 0x08, 0x0D);
/* mullhwu - mullhwu. */
GEN_MAC_HANDLER(mullhwu, 0x08, 0x0C);

/* mfdcr */
GEN_HANDLER(mfdcr, 0x1F, 0x03, 0x0A, 0x00000001, PPC_EMB_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    uint32_t dcrn = SPR(ctx->opcode);

    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_set_T0(dcrn);
    gen_op_load_dcr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mtdcr */
GEN_HANDLER(mtdcr, 0x1F, 0x03, 0x0E, 0x00000001, PPC_EMB_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    uint32_t dcrn = SPR(ctx->opcode);

    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_set_T0(dcrn);
    gen_op_load_gpr_T1(rS(ctx->opcode));
    gen_op_store_dcr();
#endif
}

/* mfdcrx */
GEN_HANDLER(mfdcrx, 0x1F, 0x03, 0x08, 0x00000001, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_dcr();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* mtdcrx */
GEN_HANDLER(mtdcrx, 0x1F, 0x03, 0x0C, 0x00000001, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVREG(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVREG(ctx);
        return;
    }
    gen_op_load_gpr_T0(rA(ctx->opcode));
    gen_op_load_gpr_T1(rS(ctx->opcode));
    gen_op_store_dcr();
#endif
}

/* dccci */
GEN_HANDLER(dccci, 0x1F, 0x06, 0x0E, 0x03E00001, PPC_4xx_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* dcread */
GEN_HANDLER(dcread, 0x1F, 0x06, 0x0F, 0x00000001, PPC_4xx_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_addr_reg_index(ctx);
    op_ldst(lwz);
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* icbt */
GEN_HANDLER(icbt_40x, 0x1F, 0x06, 0x08, 0x03E00001, PPC_40x_SPEC)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* iccci */
GEN_HANDLER(iccci, 0x1F, 0x06, 0x1E, 0x00000001, PPC_4xx_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* icread */
GEN_HANDLER(icread, 0x1F, 0x06, 0x1F, 0x03E00001, PPC_4xx_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* rfci (supervisor only) */
GEN_HANDLER(rfci_40x, 0x13, 0x13, 0x01, 0x03FF8001, PPC_40x_EXCP)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* Restore CPU state */
    gen_op_40x_rfci();
    RET_CHG_FLOW(ctx);
#endif
}

GEN_HANDLER(rfci, 0x13, 0x13, 0x01, 0x03FF8001, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* Restore CPU state */
    gen_op_rfci();
    RET_CHG_FLOW(ctx);
#endif
}

/* BookE specific */
GEN_HANDLER(rfdi, 0x13, 0x07, 0x01, 0x03FF8001, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* Restore CPU state */
    gen_op_rfdi();
    RET_CHG_FLOW(ctx);
#endif
}

GEN_HANDLER(rfmci, 0x13, 0x06, 0x01, 0x03FF8001, PPC_BOOKE)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    /* Restore CPU state */
    gen_op_rfmci();
    RET_CHG_FLOW(ctx);
#endif
}
/* TLB management - PowerPC 405 implementation */
/* tlbre */
GEN_HANDLER(tlbre, 0x1F, 0x12, 0x1D, 0x00000001, PPC_40x_SPEC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_4xx_tlbre_hi();
        gen_op_store_T0_gpr(rD(ctx->opcode));
        break;
    case 1:
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_4xx_tlbre_lo();
        gen_op_store_T0_gpr(rD(ctx->opcode));
        break;
    default:
        RET_INVAL(ctx);
        break;
    }
#endif
}

/* tlbsx - tlbsx. */
GEN_HANDLER(tlbsx, 0x1F, 0x12, 0x1C, 0x00000000, PPC_40x_SPEC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_addr_reg_index(ctx);
    if (Rc(ctx->opcode))
        gen_op_4xx_tlbsx_();
    else
        gen_op_4xx_tlbsx();
    gen_op_store_T0_gpr(rD(ctx->opcode));
#endif
}

/* tlbwe */
GEN_HANDLER(tlbwe, 0x1F, 0x12, 0x1E, 0x00000001, PPC_40x_SPEC)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rS(ctx->opcode));
        gen_op_4xx_tlbwe_hi();
        break;
    case 1:
        gen_op_load_gpr_T0(rA(ctx->opcode));
        gen_op_load_gpr_T1(rS(ctx->opcode));
        gen_op_4xx_tlbwe_lo();
        break;
    default:
        RET_INVAL(ctx);
        break;
    }
#endif
}

/* wrtee */
GEN_HANDLER(wrtee, 0x1F, 0x03, 0x04, 0x000FFC01, PPC_EMB_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_load_gpr_T0(rD(ctx->opcode));
    gen_op_wrte();
    RET_EXCP(ctx, EXCP_MTMSR, 0);
#endif
}

/* wrteei */
GEN_HANDLER(wrteei, 0x1F, 0x03, 0x05, 0x000EFC01, PPC_EMB_COMMON)
{
#if defined(CONFIG_USER_ONLY)
    RET_PRIVOPC(ctx);
#else
    if (unlikely(!ctx->supervisor)) {
        RET_PRIVOPC(ctx);
        return;
    }
    gen_op_set_T0(ctx->opcode & 0x00010000);
    gen_op_wrte();
    RET_EXCP(ctx, EXCP_MTMSR, 0);
#endif
}

/* PPC 440 specific instructions */
/* dlmzb */
GEN_HANDLER(dlmzb, 0x1F, 0x0E, 0x02, 0x00000000, PPC_440_SPEC)
{
    gen_op_load_gpr_T0(rS(ctx->opcode));
    gen_op_load_gpr_T1(rB(ctx->opcode));
    gen_op_440_dlmzb();
    gen_op_store_T0_gpr(rA(ctx->opcode));
    gen_op_store_xer_bc();
    if (Rc(ctx->opcode)) {
        gen_op_440_dlmzb_update_Rc();
        gen_op_store_T0_crf(0);
    }
}

/* mbar replaces eieio on 440 */
GEN_HANDLER(mbar, 0x1F, 0x16, 0x13, 0x001FF801, PPC_BOOKE)
{
    /* interpreted as no-op */
}

/* msync replaces sync on 440 */
GEN_HANDLER(msync, 0x1F, 0x16, 0x12, 0x03FF0801, PPC_BOOKE)
{
    /* interpreted as no-op */
}

/* icbt */
GEN_HANDLER(icbt_440, 0x1F, 0x16, 0x00, 0x03E00001, PPC_BOOKE)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

#if defined(TARGET_PPCSPE)
/***                           SPE extension                               ***/

/* Register moves */
GEN32(gen_op_load_gpr64_T0, gen_op_load_gpr64_T0_gpr);
GEN32(gen_op_load_gpr64_T1, gen_op_load_gpr64_T1_gpr);
#if 0 // unused
GEN32(gen_op_load_gpr64_T2, gen_op_load_gpr64_T2_gpr);
#endif

GEN32(gen_op_store_T0_gpr64, gen_op_store_T0_gpr64_gpr);
GEN32(gen_op_store_T1_gpr64, gen_op_store_T1_gpr64_gpr);
#if 0 // unused
GEN32(gen_op_store_T2_gpr64, gen_op_store_T2_gpr64_gpr);
#endif

#define GEN_SPE(name0, name1, opc2, opc3, inval, type)                        \
GEN_HANDLER(name0##_##name1, 0x04, opc2, opc3, inval, type)                   \
{                                                                             \
    if (Rc(ctx->opcode))                                                      \
        gen_##name1(ctx);                                                     \
    else                                                                      \
        gen_##name0(ctx);                                                     \
}

/* Handler for undefined SPE opcodes */
static inline void gen_speundef (DisasContext *ctx)
{
    RET_INVAL(ctx);
}

/* SPE load and stores */
static inline void gen_addr_spe_imm_index (DisasContext *ctx, int sh)
{
    target_long simm = rB(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        gen_set_T0(simm << sh);
    } else {
        gen_op_load_gpr_T0(rA(ctx->opcode));
        if (likely(simm != 0))
            gen_op_addi(simm << sh);
    }
}

#define op_spe_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#if defined(CONFIG_USER_ONLY)
#if defined(TARGET_PPC64)
#define OP_SPE_LD_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_l##name[] = {                                    \
    &gen_op_spe_l##name##_raw,                                                \
    &gen_op_spe_l##name##_le_raw,                                             \
    &gen_op_spe_l##name##_64_raw,                                             \
    &gen_op_spe_l##name##_le_64_raw,                                          \
};
#define OP_SPE_ST_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_st##name[] = {                                   \
    &gen_op_spe_st##name##_raw,                                               \
    &gen_op_spe_st##name##_le_raw,                                            \
    &gen_op_spe_st##name##_64_raw,                                            \
    &gen_op_spe_st##name##_le_64_raw,                                         \
};
#else /* defined(TARGET_PPC64) */
#define OP_SPE_LD_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_l##name[] = {                                    \
    &gen_op_spe_l##name##_raw,                                                \
    &gen_op_spe_l##name##_le_raw,                                             \
};
#define OP_SPE_ST_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_st##name[] = {                                   \
    &gen_op_spe_st##name##_raw,                                               \
    &gen_op_spe_st##name##_le_raw,                                            \
};
#endif /* defined(TARGET_PPC64) */
#else /* defined(CONFIG_USER_ONLY) */
#if defined(TARGET_PPC64)
#define OP_SPE_LD_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_l##name[] = {                                    \
    &gen_op_spe_l##name##_user,                                               \
    &gen_op_spe_l##name##_le_user,                                            \
    &gen_op_spe_l##name##_kernel,                                             \
    &gen_op_spe_l##name##_le_kernel,                                          \
    &gen_op_spe_l##name##_64_user,                                            \
    &gen_op_spe_l##name##_le_64_user,                                         \
    &gen_op_spe_l##name##_64_kernel,                                          \
    &gen_op_spe_l##name##_le_64_kernel,                                       \
};
#define OP_SPE_ST_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_st##name[] = {                                   \
    &gen_op_spe_st##name##_user,                                              \
    &gen_op_spe_st##name##_le_user,                                           \
    &gen_op_spe_st##name##_kernel,                                            \
    &gen_op_spe_st##name##_le_kernel,                                         \
    &gen_op_spe_st##name##_64_user,                                           \
    &gen_op_spe_st##name##_le_64_user,                                        \
    &gen_op_spe_st##name##_64_kernel,                                         \
    &gen_op_spe_st##name##_le_64_kernel,                                      \
};
#else /* defined(TARGET_PPC64) */
#define OP_SPE_LD_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_l##name[] = {                                    \
    &gen_op_spe_l##name##_user,                                               \
    &gen_op_spe_l##name##_le_user,                                            \
    &gen_op_spe_l##name##_kernel,                                             \
    &gen_op_spe_l##name##_le_kernel,                                          \
};
#define OP_SPE_ST_TABLE(name)                                                 \
static GenOpFunc *gen_op_spe_st##name[] = {                                   \
    &gen_op_spe_st##name##_user,                                              \
    &gen_op_spe_st##name##_le_user,                                           \
    &gen_op_spe_st##name##_kernel,                                            \
    &gen_op_spe_st##name##_le_kernel,                                         \
};
#endif /* defined(TARGET_PPC64) */
#endif /* defined(CONFIG_USER_ONLY) */

#define GEN_SPE_LD(name, sh)                                                  \
static inline void gen_evl##name (DisasContext *ctx)                          \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_addr_spe_imm_index(ctx, sh);                                          \
    op_spe_ldst(spe_l##name);                                                 \
    gen_op_store_T1_gpr64(rD(ctx->opcode));                                   \
}

#define GEN_SPE_LDX(name)                                                     \
static inline void gen_evl##name##x (DisasContext *ctx)                       \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    op_spe_ldst(spe_l##name);                                                 \
    gen_op_store_T1_gpr64(rD(ctx->opcode));                                   \
}

#define GEN_SPEOP_LD(name, sh)                                                \
OP_SPE_LD_TABLE(name);                                                        \
GEN_SPE_LD(name, sh);                                                         \
GEN_SPE_LDX(name)

#define GEN_SPE_ST(name, sh)                                                  \
static inline void gen_evst##name (DisasContext *ctx)                         \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_addr_spe_imm_index(ctx, sh);                                          \
    gen_op_load_gpr64_T1(rS(ctx->opcode));                                    \
    op_spe_ldst(spe_st##name);                                                \
}

#define GEN_SPE_STX(name)                                                     \
static inline void gen_evst##name##x (DisasContext *ctx)                      \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_addr_reg_index(ctx);                                                  \
    gen_op_load_gpr64_T1(rS(ctx->opcode));                                    \
    op_spe_ldst(spe_st##name);                                                \
}

#define GEN_SPEOP_ST(name, sh)                                                \
OP_SPE_ST_TABLE(name);                                                        \
GEN_SPE_ST(name, sh);                                                         \
GEN_SPE_STX(name)

#define GEN_SPEOP_LDST(name, sh)                                              \
GEN_SPEOP_LD(name, sh);                                                       \
GEN_SPEOP_ST(name, sh)

/* SPE arithmetic and logic */
#define GEN_SPEOP_ARITH2(name)                                                \
static inline void gen_##name (DisasContext *ctx)                             \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr64_T0(rA(ctx->opcode));                                    \
    gen_op_load_gpr64_T1(rB(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr64(rD(ctx->opcode));                                   \
}

#define GEN_SPEOP_ARITH1(name)                                                \
static inline void gen_##name (DisasContext *ctx)                             \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr64_T0(rA(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr64(rD(ctx->opcode));                                   \
}

#define GEN_SPEOP_COMP(name)                                                  \
static inline void gen_##name (DisasContext *ctx)                             \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr64_T0(rA(ctx->opcode));                                    \
    gen_op_load_gpr64_T1(rB(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_crf(crfD(ctx->opcode));                                   \
}

/* Logical */
GEN_SPEOP_ARITH2(evand);
GEN_SPEOP_ARITH2(evandc);
GEN_SPEOP_ARITH2(evxor);
GEN_SPEOP_ARITH2(evor);
GEN_SPEOP_ARITH2(evnor);
GEN_SPEOP_ARITH2(eveqv);
GEN_SPEOP_ARITH2(evorc);
GEN_SPEOP_ARITH2(evnand);
GEN_SPEOP_ARITH2(evsrwu);
GEN_SPEOP_ARITH2(evsrws);
GEN_SPEOP_ARITH2(evslw);
GEN_SPEOP_ARITH2(evrlw);
GEN_SPEOP_ARITH2(evmergehi);
GEN_SPEOP_ARITH2(evmergelo);
GEN_SPEOP_ARITH2(evmergehilo);
GEN_SPEOP_ARITH2(evmergelohi);

/* Arithmetic */
GEN_SPEOP_ARITH2(evaddw);
GEN_SPEOP_ARITH2(evsubfw);
GEN_SPEOP_ARITH1(evabs);
GEN_SPEOP_ARITH1(evneg);
GEN_SPEOP_ARITH1(evextsb);
GEN_SPEOP_ARITH1(evextsh);
GEN_SPEOP_ARITH1(evrndw);
GEN_SPEOP_ARITH1(evcntlzw);
GEN_SPEOP_ARITH1(evcntlsw);
static inline void gen_brinc (DisasContext *ctx)
{
    /* Note: brinc is usable even if SPE is disabled */
    gen_op_load_gpr64_T0(rA(ctx->opcode));
    gen_op_load_gpr64_T1(rB(ctx->opcode));
    gen_op_brinc();
    gen_op_store_T0_gpr64(rD(ctx->opcode));
}

#define GEN_SPEOP_ARITH_IMM2(name)                                            \
static inline void gen_##name##i (DisasContext *ctx)                          \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr64_T0(rB(ctx->opcode));                                    \
    gen_op_splatwi_T1_64(rA(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr64(rD(ctx->opcode));                                   \
}

#define GEN_SPEOP_LOGIC_IMM2(name)                                            \
static inline void gen_##name##i (DisasContext *ctx)                          \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        RET_EXCP(ctx, EXCP_NO_SPE, 0);                                        \
        return;                                                               \
    }                                                                         \
    gen_op_load_gpr64_T0(rA(ctx->opcode));                                    \
    gen_op_splatwi_T1_64(rB(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr64(rD(ctx->opcode));                                   \
}

GEN_SPEOP_ARITH_IMM2(evaddw);
#define gen_evaddiw gen_evaddwi
GEN_SPEOP_ARITH_IMM2(evsubfw);
#define gen_evsubifw gen_evsubfwi
GEN_SPEOP_LOGIC_IMM2(evslw);
GEN_SPEOP_LOGIC_IMM2(evsrwu);
#define gen_evsrwis gen_evsrwsi
GEN_SPEOP_LOGIC_IMM2(evsrws);
#define gen_evsrwiu gen_evsrwui
GEN_SPEOP_LOGIC_IMM2(evrlw);

static inline void gen_evsplati (DisasContext *ctx)
{
    int32_t imm = (int32_t)(rA(ctx->opcode) << 27) >> 27;

    gen_op_splatwi_T0_64(imm);
    gen_op_store_T0_gpr64(rD(ctx->opcode));
}

static inline void gen_evsplatfi (DisasContext *ctx)
{
    uint32_t imm = rA(ctx->opcode) << 27;

    gen_op_splatwi_T0_64(imm);
    gen_op_store_T0_gpr64(rD(ctx->opcode));
}

/* Comparison */
GEN_SPEOP_COMP(evcmpgtu);
GEN_SPEOP_COMP(evcmpgts);
GEN_SPEOP_COMP(evcmpltu);
GEN_SPEOP_COMP(evcmplts);
GEN_SPEOP_COMP(evcmpeq);

GEN_SPE(evaddw,         speundef,      0x00, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evaddiw,        speundef,      0x01, 0x08, 0x00000000, PPC_SPE);
GEN_SPE(evsubfw,        speundef,      0x02, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evsubifw,       speundef,      0x03, 0x08, 0x00000000, PPC_SPE);
GEN_SPE(evabs,          evneg,         0x04, 0x08, 0x0000F800, PPC_SPE); ////
GEN_SPE(evextsb,        evextsh,       0x05, 0x08, 0x0000F800, PPC_SPE); ////
GEN_SPE(evrndw,         evcntlzw,      0x06, 0x08, 0x0000F800, PPC_SPE); ////
GEN_SPE(evcntlsw,       brinc,         0x07, 0x08, 0x00000000, PPC_SPE); //
GEN_SPE(speundef,       evand,         0x08, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evandc,         speundef,      0x09, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evxor,          evor,          0x0B, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evnor,          eveqv,         0x0C, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(speundef,       evorc,         0x0D, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evnand,         speundef,      0x0F, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evsrwu,         evsrws,        0x10, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evsrwiu,        evsrwis,       0x11, 0x08, 0x00000000, PPC_SPE);
GEN_SPE(evslw,          speundef,      0x12, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evslwi,         speundef,      0x13, 0x08, 0x00000000, PPC_SPE);
GEN_SPE(evrlw,          evsplati,      0x14, 0x08, 0x00000000, PPC_SPE); //
GEN_SPE(evrlwi,         evsplatfi,     0x15, 0x08, 0x00000000, PPC_SPE);
GEN_SPE(evmergehi,      evmergelo,     0x16, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evmergehilo,    evmergelohi,   0x17, 0x08, 0x00000000, PPC_SPE); ////
GEN_SPE(evcmpgtu,       evcmpgts,      0x18, 0x08, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpltu,       evcmplts,      0x19, 0x08, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpeq,        speundef,      0x1A, 0x08, 0x00600000, PPC_SPE); ////

static inline void gen_evsel (DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        RET_EXCP(ctx, EXCP_NO_SPE, 0);
        return;
    }
    gen_op_load_crf_T0(ctx->opcode & 0x7);
    gen_op_load_gpr64_T0(rA(ctx->opcode));
    gen_op_load_gpr64_T1(rB(ctx->opcode));
    gen_op_evsel();
    gen_op_store_T0_gpr64(rD(ctx->opcode));
}

GEN_HANDLER(evsel0, 0x04, 0x1c, 0x09, 0x00000000, PPC_SPE)
{
    gen_evsel(ctx);
}
GEN_HANDLER(evsel1, 0x04, 0x1d, 0x09, 0x00000000, PPC_SPE)
{
    gen_evsel(ctx);
}
GEN_HANDLER(evsel2, 0x04, 0x1e, 0x09, 0x00000000, PPC_SPE)
{
    gen_evsel(ctx);
}
GEN_HANDLER(evsel3, 0x04, 0x1f, 0x09, 0x00000000, PPC_SPE)
{
    gen_evsel(ctx);
}

/* Load and stores */
#if defined(TARGET_PPC64)
/* In that case, we already have 64 bits load & stores
 * so, spe_ldd is equivalent to ld and spe_std is equivalent to std
 */
#if defined(CONFIG_USER_ONLY)
#define gen_op_spe_ldd_raw gen_op_ld_raw
#define gen_op_spe_ldd_64_raw gen_op_ld_64_raw
#define gen_op_spe_ldd_le_raw gen_op_ld_le_raw
#define gen_op_spe_ldd_le_64_raw gen_op_ld_le_64_raw
#define gen_op_spe_stdd_raw gen_op_ld_raw
#define gen_op_spe_stdd_64_raw gen_op_std_64_raw
#define gen_op_spe_stdd_le_raw gen_op_std_le_raw
#define gen_op_spe_stdd_le_64_raw gen_op_std_le_64_raw
#else /* defined(CONFIG_USER_ONLY) */
#define gen_op_spe_ldd_kernel gen_op_ld_kernel
#define gen_op_spe_ldd_64_kernel gen_op_ld_64_kernel
#define gen_op_spe_ldd_le_kernel gen_op_ld_kernel
#define gen_op_spe_ldd_le_64_kernel gen_op_ld_64_kernel
#define gen_op_spe_ldd_user gen_op_ld_user
#define gen_op_spe_ldd_64_user gen_op_ld_64_user
#define gen_op_spe_ldd_le_user gen_op_ld_le_user
#define gen_op_spe_ldd_le_64_user gen_op_ld_le_64_user
#define gen_op_spe_stdd_kernel gen_op_std_kernel
#define gen_op_spe_stdd_64_kernel gen_op_std_64_kernel
#define gen_op_spe_stdd_le_kernel gen_op_std_kernel
#define gen_op_spe_stdd_le_64_kernel gen_op_std_64_kernel
#define gen_op_spe_stdd_user gen_op_std_user
#define gen_op_spe_stdd_64_user gen_op_std_64_user
#define gen_op_spe_stdd_le_user gen_op_std_le_user
#define gen_op_spe_stdd_le_64_user gen_op_std_le_64_user
#endif /* defined(CONFIG_USER_ONLY) */
#endif /* defined(TARGET_PPC64) */
GEN_SPEOP_LDST(dd, 3);
GEN_SPEOP_LDST(dw, 3);
GEN_SPEOP_LDST(dh, 3);
GEN_SPEOP_LDST(whe, 2);
GEN_SPEOP_LD(whou, 2);
GEN_SPEOP_LD(whos, 2);
GEN_SPEOP_ST(who, 2);

#if defined(TARGET_PPC64)
/* In that case, spe_stwwo is equivalent to stw */
#if defined(CONFIG_USER_ONLY)
#define gen_op_spe_stwwo_raw gen_op_stw_raw
#define gen_op_spe_stwwo_le_raw gen_op_stw_le_raw
#define gen_op_spe_stwwo_64_raw gen_op_stw_64_raw
#define gen_op_spe_stwwo_le_64_raw gen_op_stw_le_64_raw
#else
#define gen_op_spe_stwwo_user gen_op_stw_user
#define gen_op_spe_stwwo_le_user gen_op_stw_le_user
#define gen_op_spe_stwwo_64_user gen_op_stw_64_user
#define gen_op_spe_stwwo_le_64_user gen_op_stw_le_64_user
#define gen_op_spe_stwwo_kernel gen_op_stw_kernel
#define gen_op_spe_stwwo_le_kernel gen_op_stw_le_kernel
#define gen_op_spe_stwwo_64_kernel gen_op_stw_64_kernel
#define gen_op_spe_stwwo_le_64_kernel gen_op_stw_le_64_kernel
#endif
#endif
#define _GEN_OP_SPE_STWWE(suffix)                                             \
static inline void gen_op_spe_stwwe_##suffix (void)                           \
{                                                                             \
    gen_op_srli32_T1_64();                                                    \
    gen_op_spe_stwwo_##suffix();                                              \
}
#define _GEN_OP_SPE_STWWE_LE(suffix)                                          \
static inline void gen_op_spe_stwwe_le_##suffix (void)                        \
{                                                                             \
    gen_op_srli32_T1_64();                                                    \
    gen_op_spe_stwwo_le_##suffix();                                           \
}
#if defined(TARGET_PPC64)
#define GEN_OP_SPE_STWWE(suffix)                                              \
_GEN_OP_SPE_STWWE(suffix);                                                    \
_GEN_OP_SPE_STWWE_LE(suffix);                                                 \
static inline void gen_op_spe_stwwe_64_##suffix (void)                        \
{                                                                             \
    gen_op_srli32_T1_64();                                                    \
    gen_op_spe_stwwo_64_##suffix();                                           \
}                                                                             \
static inline void gen_op_spe_stwwe_le_64_##suffix (void)                     \
{                                                                             \
    gen_op_srli32_T1_64();                                                    \
    gen_op_spe_stwwo_le_64_##suffix();                                        \
}
#else
#define GEN_OP_SPE_STWWE(suffix)                                              \
_GEN_OP_SPE_STWWE(suffix);                                                    \
_GEN_OP_SPE_STWWE_LE(suffix)
#endif
#if defined(CONFIG_USER_ONLY)
GEN_OP_SPE_STWWE(raw);
#else /* defined(CONFIG_USER_ONLY) */
GEN_OP_SPE_STWWE(kernel);
GEN_OP_SPE_STWWE(user);
#endif /* defined(CONFIG_USER_ONLY) */
GEN_SPEOP_ST(wwe, 2);
GEN_SPEOP_ST(wwo, 2);

#define GEN_SPE_LDSPLAT(name, op, suffix)                                     \
static inline void gen_op_spe_l##name##_##suffix (void)                       \
{                                                                             \
    gen_op_##op##_##suffix();                                                 \
    gen_op_splatw_T1_64();                                                    \
}

#define GEN_OP_SPE_LHE(suffix)                                                \
static inline void gen_op_spe_lhe_##suffix (void)                             \
{                                                                             \
    gen_op_spe_lh_##suffix();                                                 \
    gen_op_sli16_T1_64();                                                     \
}

#define GEN_OP_SPE_LHX(suffix)                                                \
static inline void gen_op_spe_lhx_##suffix (void)                             \
{                                                                             \
    gen_op_spe_lh_##suffix();                                                 \
    gen_op_extsh_T1_64();                                                     \
}

#if defined(CONFIG_USER_ONLY)
GEN_OP_SPE_LHE(raw);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, raw);
GEN_OP_SPE_LHE(le_raw);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_raw);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, raw);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_raw);
GEN_OP_SPE_LHX(raw);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, raw);
GEN_OP_SPE_LHX(le_raw);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_raw);
#if defined(TARGET_PPC64)
GEN_OP_SPE_LHE(64_raw);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, 64_raw);
GEN_OP_SPE_LHE(le_64_raw);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_64_raw);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, 64_raw);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_64_raw);
GEN_OP_SPE_LHX(64_raw);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, 64_raw);
GEN_OP_SPE_LHX(le_64_raw);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_64_raw);
#endif
#else
GEN_OP_SPE_LHE(kernel);
GEN_OP_SPE_LHE(user);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, kernel);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, user);
GEN_OP_SPE_LHE(le_kernel);
GEN_OP_SPE_LHE(le_user);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_kernel);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_user);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, kernel);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, user);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_kernel);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_user);
GEN_OP_SPE_LHX(kernel);
GEN_OP_SPE_LHX(user);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, kernel);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, user);
GEN_OP_SPE_LHX(le_kernel);
GEN_OP_SPE_LHX(le_user);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_kernel);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_user);
#if defined(TARGET_PPC64)
GEN_OP_SPE_LHE(64_kernel);
GEN_OP_SPE_LHE(64_user);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, 64_kernel);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, 64_user);
GEN_OP_SPE_LHE(le_64_kernel);
GEN_OP_SPE_LHE(le_64_user);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_64_kernel);
GEN_SPE_LDSPLAT(hhesplat, spe_lhe, le_64_user);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, 64_kernel);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, 64_user);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_64_kernel);
GEN_SPE_LDSPLAT(hhousplat, spe_lh, le_64_user);
GEN_OP_SPE_LHX(64_kernel);
GEN_OP_SPE_LHX(64_user);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, 64_kernel);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, 64_user);
GEN_OP_SPE_LHX(le_64_kernel);
GEN_OP_SPE_LHX(le_64_user);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_64_kernel);
GEN_SPE_LDSPLAT(hhossplat, spe_lhx, le_64_user);
#endif
#endif
GEN_SPEOP_LD(hhesplat, 1);
GEN_SPEOP_LD(hhousplat, 1);
GEN_SPEOP_LD(hhossplat, 1);
GEN_SPEOP_LD(wwsplat, 2);
GEN_SPEOP_LD(whsplat, 2);

GEN_SPE(evlddx,         evldd,         0x00, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evldwx,         evldw,         0x01, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evldhx,         evldh,         0x02, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlhhesplatx,   evlhhesplat,   0x04, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlhhousplatx,  evlhhousplat,  0x06, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlhhossplatx,  evlhhossplat,  0x07, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlwhex,        evlwhe,        0x08, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlwhoux,       evlwhou,       0x0A, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlwhosx,       evlwhos,       0x0B, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlwwsplatx,    evlwwsplat,    0x0C, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evlwhsplatx,    evlwhsplat,    0x0E, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstddx,        evstdd,        0x10, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstdwx,        evstdw,        0x11, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstdhx,        evstdh,        0x12, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstwhex,       evstwhe,       0x18, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstwhox,       evstwho,       0x1A, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstwwex,       evstwwe,       0x1C, 0x0C, 0x00000000, PPC_SPE); //
GEN_SPE(evstwwox,       evstwwo,       0x1E, 0x0C, 0x00000000, PPC_SPE); //

/* Multiply and add - TODO */
#if 0
GEN_SPE(speundef,       evmhessf,      0x01, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossf,      0x03, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(evmheumi,       evmhesmi,      0x04, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmf,      0x05, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumi,       evmhosmi,      0x06, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmf,      0x07, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfa,     0x11, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfa,     0x13, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(evmheumia,      evmhesmia,     0x14, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfa,     0x15, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumia,      evmhosmia,     0x16, 0x10, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfa,     0x17, 0x10, 0x00000000, PPC_SPE);

GEN_SPE(speundef,       evmwhssf,      0x03, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumi,       speundef,      0x04, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwhumi,       evmwhsmi,      0x06, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmf,      0x07, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssf,       0x09, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwumi,        evmwsmi,       0x0C, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmf,       0x0D, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhssfa,     0x13, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumia,      speundef,      0x14, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwhumia,      evmwhsmia,     0x16, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmfa,     0x17, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfa,      0x19, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(evmwumia,       evmwsmia,      0x1C, 0x11, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfa,      0x1D, 0x11, 0x00000000, PPC_SPE);

GEN_SPE(evadduiaaw,     evaddsiaaw,    0x00, 0x13, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfusiaaw,   evsubfssiaaw,  0x01, 0x13, 0x0000F800, PPC_SPE);
GEN_SPE(evaddumiaaw,    evaddsmiaaw,   0x04, 0x13, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfumiaaw,   evsubfsmiaaw,  0x05, 0x13, 0x0000F800, PPC_SPE);
GEN_SPE(evdivws,        evdivwu,       0x06, 0x13, 0x00000000, PPC_SPE);
GEN_SPE(evmra,          speundef,      0x07, 0x13, 0x0000F800, PPC_SPE);

GEN_SPE(evmheusiaaw,    evmhessiaaw,   0x00, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfaaw,   0x01, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(evmhousiaaw,    evmhossiaaw,   0x02, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfaaw,   0x03, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(evmheumiaaw,    evmhesmiaaw,   0x04, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfaaw,   0x05, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumiaaw,    evmhosmiaaw,   0x06, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfaaw,   0x07, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumiaa,    evmhegsmiaa,   0x14, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfaa,   0x15, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(evmhogumiaa,    evmhogsmiaa,   0x16, 0x14, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfaa,   0x17, 0x14, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusiaaw,    evmwlssiaaw,   0x00, 0x15, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumiaaw,    evmwlsmiaaw,   0x04, 0x15, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfaa,     0x09, 0x15, 0x00000000, PPC_SPE);
GEN_SPE(evmwumiaa,      evmwsmiaa,     0x0C, 0x15, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfaa,     0x0D, 0x15, 0x00000000, PPC_SPE);

GEN_SPE(evmheusianw,    evmhessianw,   0x00, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfanw,   0x01, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(evmhousianw,    evmhossianw,   0x02, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfanw,   0x03, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(evmheumianw,    evmhesmianw,   0x04, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfanw,   0x05, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumianw,    evmhosmianw,   0x06, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfanw,   0x07, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumian,    evmhegsmian,   0x14, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfan,   0x15, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(evmhigumian,    evmhigsmian,   0x16, 0x16, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfan,   0x17, 0x16, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusianw,    evmwlssianw,   0x00, 0x17, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumianw,    evmwlsmianw,   0x04, 0x17, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfan,     0x09, 0x17, 0x00000000, PPC_SPE);
GEN_SPE(evmwumian,      evmwsmian,     0x0C, 0x17, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfan,     0x0D, 0x17, 0x00000000, PPC_SPE);
#endif

/***                      SPE floating-point extension                     ***/
#define GEN_SPEFPUOP_CONV(name)                                               \
static inline void gen_##name (DisasContext *ctx)                             \
{                                                                             \
    gen_op_load_gpr64_T0(rB(ctx->opcode));                                    \
    gen_op_##name();                                                          \
    gen_op_store_T0_gpr64(rD(ctx->opcode));                                   \
}

/* Single precision floating-point vectors operations */
/* Arithmetic */
GEN_SPEOP_ARITH2(evfsadd);
GEN_SPEOP_ARITH2(evfssub);
GEN_SPEOP_ARITH2(evfsmul);
GEN_SPEOP_ARITH2(evfsdiv);
GEN_SPEOP_ARITH1(evfsabs);
GEN_SPEOP_ARITH1(evfsnabs);
GEN_SPEOP_ARITH1(evfsneg);
/* Conversion */
GEN_SPEFPUOP_CONV(evfscfui);
GEN_SPEFPUOP_CONV(evfscfsi);
GEN_SPEFPUOP_CONV(evfscfuf);
GEN_SPEFPUOP_CONV(evfscfsf);
GEN_SPEFPUOP_CONV(evfsctui);
GEN_SPEFPUOP_CONV(evfsctsi);
GEN_SPEFPUOP_CONV(evfsctuf);
GEN_SPEFPUOP_CONV(evfsctsf);
GEN_SPEFPUOP_CONV(evfsctuiz);
GEN_SPEFPUOP_CONV(evfsctsiz);
/* Comparison */
GEN_SPEOP_COMP(evfscmpgt);
GEN_SPEOP_COMP(evfscmplt);
GEN_SPEOP_COMP(evfscmpeq);
GEN_SPEOP_COMP(evfststgt);
GEN_SPEOP_COMP(evfststlt);
GEN_SPEOP_COMP(evfststeq);

/* Opcodes definitions */
GEN_SPE(evfsadd,        evfssub,       0x00, 0x0A, 0x00000000, PPC_SPEFPU); //
GEN_SPE(evfsabs,        evfsnabs,      0x02, 0x0A, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(evfsneg,        speundef,      0x03, 0x0A, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(evfsmul,        evfsdiv,       0x04, 0x0A, 0x00000000, PPC_SPEFPU); //
GEN_SPE(evfscmpgt,      evfscmplt,     0x06, 0x0A, 0x00600000, PPC_SPEFPU); //
GEN_SPE(evfscmpeq,      speundef,      0x07, 0x0A, 0x00600000, PPC_SPEFPU); //
GEN_SPE(evfscfui,       evfscfsi,      0x08, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfscfuf,       evfscfsf,      0x09, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfsctui,       evfsctsi,      0x0A, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfsctuf,       evfsctsf,      0x0B, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfsctuiz,      speundef,      0x0C, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfsctsiz,      speundef,      0x0D, 0x0A, 0x00180000, PPC_SPEFPU); //
GEN_SPE(evfststgt,      evfststlt,     0x0E, 0x0A, 0x00600000, PPC_SPEFPU); //
GEN_SPE(evfststeq,      speundef,      0x0F, 0x0A, 0x00600000, PPC_SPEFPU); //

/* Single precision floating-point operations */
/* Arithmetic */
GEN_SPEOP_ARITH2(efsadd);
GEN_SPEOP_ARITH2(efssub);
GEN_SPEOP_ARITH2(efsmul);
GEN_SPEOP_ARITH2(efsdiv);
GEN_SPEOP_ARITH1(efsabs);
GEN_SPEOP_ARITH1(efsnabs);
GEN_SPEOP_ARITH1(efsneg);
/* Conversion */
GEN_SPEFPUOP_CONV(efscfui);
GEN_SPEFPUOP_CONV(efscfsi);
GEN_SPEFPUOP_CONV(efscfuf);
GEN_SPEFPUOP_CONV(efscfsf);
GEN_SPEFPUOP_CONV(efsctui);
GEN_SPEFPUOP_CONV(efsctsi);
GEN_SPEFPUOP_CONV(efsctuf);
GEN_SPEFPUOP_CONV(efsctsf);
GEN_SPEFPUOP_CONV(efsctuiz);
GEN_SPEFPUOP_CONV(efsctsiz);
GEN_SPEFPUOP_CONV(efscfd);
/* Comparison */
GEN_SPEOP_COMP(efscmpgt);
GEN_SPEOP_COMP(efscmplt);
GEN_SPEOP_COMP(efscmpeq);
GEN_SPEOP_COMP(efststgt);
GEN_SPEOP_COMP(efststlt);
GEN_SPEOP_COMP(efststeq);

/* Opcodes definitions */
GEN_SPE(efsadd,         efssub,        0x00, 0x0A, 0x00000000, PPC_SPEFPU); //
GEN_SPE(efsabs,         efsnabs,       0x02, 0x0B, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(efsneg,         speundef,      0x03, 0x0B, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(efsmul,         efsdiv,        0x04, 0x0B, 0x00000000, PPC_SPEFPU); //
GEN_SPE(efscmpgt,       efscmplt,      0x06, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efscmpeq,       efscfd,        0x07, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efscfui,        efscfsi,       0x08, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efscfuf,        efscfsf,       0x09, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efsctui,        efsctsi,       0x0A, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efsctuf,        efsctsf,       0x0B, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efsctuiz,       efsctsiz,      0x0C, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efststgt,       efststlt,      0x0E, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efststeq,       speundef,      0x0F, 0x0B, 0x00600000, PPC_SPEFPU); //

/* Double precision floating-point operations */
/* Arithmetic */
GEN_SPEOP_ARITH2(efdadd);
GEN_SPEOP_ARITH2(efdsub);
GEN_SPEOP_ARITH2(efdmul);
GEN_SPEOP_ARITH2(efddiv);
GEN_SPEOP_ARITH1(efdabs);
GEN_SPEOP_ARITH1(efdnabs);
GEN_SPEOP_ARITH1(efdneg);
/* Conversion */

GEN_SPEFPUOP_CONV(efdcfui);
GEN_SPEFPUOP_CONV(efdcfsi);
GEN_SPEFPUOP_CONV(efdcfuf);
GEN_SPEFPUOP_CONV(efdcfsf);
GEN_SPEFPUOP_CONV(efdctui);
GEN_SPEFPUOP_CONV(efdctsi);
GEN_SPEFPUOP_CONV(efdctuf);
GEN_SPEFPUOP_CONV(efdctsf);
GEN_SPEFPUOP_CONV(efdctuiz);
GEN_SPEFPUOP_CONV(efdctsiz);
GEN_SPEFPUOP_CONV(efdcfs);
GEN_SPEFPUOP_CONV(efdcfuid);
GEN_SPEFPUOP_CONV(efdcfsid);
GEN_SPEFPUOP_CONV(efdctuidz);
GEN_SPEFPUOP_CONV(efdctsidz);
/* Comparison */
GEN_SPEOP_COMP(efdcmpgt);
GEN_SPEOP_COMP(efdcmplt);
GEN_SPEOP_COMP(efdcmpeq);
GEN_SPEOP_COMP(efdtstgt);
GEN_SPEOP_COMP(efdtstlt);
GEN_SPEOP_COMP(efdtsteq);

/* Opcodes definitions */
GEN_SPE(efdadd,         efdsub,        0x10, 0x0B, 0x00000000, PPC_SPEFPU); //
GEN_SPE(efdcfuid,       efdcfsid,      0x11, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdabs,         efdnabs,       0x12, 0x0B, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(efdneg,         speundef,      0x13, 0x0B, 0x0000F800, PPC_SPEFPU); //
GEN_SPE(efdmul,         efddiv,        0x14, 0x0B, 0x00000000, PPC_SPEFPU); //
GEN_SPE(efdctuidz,      efdctsidz,     0x15, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdcmpgt,       efdcmplt,      0x16, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efdcmpeq,       efdcfs,        0x17, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efdcfui,        efdcfsi,       0x18, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdcfuf,        efdcfsf,       0x19, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdctui,        efdctsi,       0x1A, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdctuf,        efdctsf,       0x1B, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdctuiz,       speundef,      0x1C, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdctsiz,       speundef,      0x1D, 0x0B, 0x00180000, PPC_SPEFPU); //
GEN_SPE(efdtstgt,       efdtstlt,      0x1E, 0x0B, 0x00600000, PPC_SPEFPU); //
GEN_SPE(efdtsteq,       speundef,      0x1F, 0x0B, 0x00600000, PPC_SPEFPU); //
#endif

/* End opcode list */
GEN_OPCODE_MARK(end);

#include "translate_init.c"

/*****************************************************************************/
/* Misc PowerPC helpers */
static inline uint32_t load_xer (CPUState *env)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

void cpu_dump_state(CPUState *env, FILE *f, 
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
#if defined(TARGET_PPC64) || 1
#define FILL ""
#define RGPL  4
#define RFPL  4
#else
#define FILL "        "
#define RGPL  8
#define RFPL  4
#endif

    int i;

    cpu_fprintf(f, "NIP " ADDRX " LR " ADDRX " CTR " ADDRX "\n",
                env->nip, env->lr, env->ctr);
    cpu_fprintf(f, "MSR " REGX FILL " XER %08x      "
#if !defined(NO_TIMER_DUMP)
                "TB %08x %08x "
#if !defined(CONFIG_USER_ONLY)
                "DECR %08x"
#endif
#endif
                "\n",
                do_load_msr(env), load_xer(env)
#if !defined(NO_TIMER_DUMP)
                , cpu_ppc_load_tbu(env), cpu_ppc_load_tbl(env)
#if !defined(CONFIG_USER_ONLY)
                , cpu_ppc_load_decr(env)
#endif
#endif
                );
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

#undef RGPL
#undef RFPL
#undef FILL
}

void cpu_dump_statistics (CPUState *env, FILE*f,
                          int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                          int flags)
{
#if defined(DO_PPC_STATISTICS)
    opc_handler_t **t1, **t2, **t3, *handler;
    int op1, op2, op3;

    t1 = env->opcodes;
    for (op1 = 0; op1 < 64; op1++) {
        handler = t1[op1];
        if (is_indirect_opcode(handler)) {
            t2 = ind_table(handler);
            for (op2 = 0; op2 < 32; op2++) {
                handler = t2[op2];
                if (is_indirect_opcode(handler)) {
                    t3 = ind_table(handler);
                    for (op3 = 0; op3 < 32; op3++) {
                        handler = t3[op3];
                        if (handler->count == 0)
                            continue;
                        cpu_fprintf(f, "%02x %02x %02x (%02x %04d) %16s: "
                                    "%016llx %lld\n",
                                    op1, op2, op3, op1, (op3 << 5) | op2,
                                    handler->oname,
                                    handler->count, handler->count);
                    }
                } else {
                    if (handler->count == 0)
                        continue;
                    cpu_fprintf(f, "%02x %02x    (%02x %04d) %16s: "
                                "%016llx %lld\n",
                                op1, op2, op1, op2, handler->oname,
                                handler->count, handler->count);
                }
            }
        } else {
            if (handler->count == 0)
                continue;
            cpu_fprintf(f, "%02x       (%02x     ) %16s: %016llx %lld\n",
                        op1, op1, handler->oname,
                        handler->count, handler->count);
        }
    }
#endif
}

/*****************************************************************************/
static inline int gen_intermediate_code_internal (CPUState *env,
                                                  TranslationBlock *tb,
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
#if defined(TARGET_PPC64)
    ctx.mem_idx |= msr_sf << 1;
#endif
#else
    ctx.supervisor = 1 - msr_pr;
    ctx.mem_idx = ((1 - msr_pr) << 1) | msr_le;
#if defined(TARGET_PPC64)
    ctx.mem_idx |= msr_sf << 2;
#endif
#endif
#if defined(TARGET_PPC64)
    ctx.sf_mode = msr_sf;
#endif
    ctx.fpu_enabled = msr_fp;
#if defined(TARGET_PPCSPE)
    ctx.spe_enabled = msr_spe;
#endif
    ctx.singlestep_enabled = env->singlestep_enabled;
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr_se = 1;
#endif
    /* Set env in case of segfault during code fetch */
    while (ctx.exception == EXCP_NONE && gen_opc_ptr < gen_opc_end) {
        if (unlikely(env->nb_breakpoints > 0)) {
            for (j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == ctx.nip) {
                    gen_update_nip(&ctx, ctx.nip); 
                    gen_op_debug();
                    break;
                }
            }
        }
        if (unlikely(search_pc)) {
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
            fprintf(logfile, "nip=" ADDRX " super=%d ir=%d\n",
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
        if (unlikely(handler->handler == &gen_invalid)) {
            if (loglevel > 0) {
                fprintf(logfile, "invalid/unsupported opcode: "
                        "%02x - %02x - %02x (%08x) 0x" ADDRX " %d\n",
                        opc1(ctx.opcode), opc2(ctx.opcode),
                        opc3(ctx.opcode), ctx.opcode, ctx.nip - 4, msr_ir);
            } else {
                printf("invalid/unsupported opcode: "
                       "%02x - %02x - %02x (%08x) 0x" ADDRX " %d\n",
                       opc1(ctx.opcode), opc2(ctx.opcode),
                       opc3(ctx.opcode), ctx.opcode, ctx.nip - 4, msr_ir);
            }
        } else {
            if (unlikely((ctx.opcode & handler->inval) != 0)) {
                if (loglevel > 0) {
                    fprintf(logfile, "invalid bits: %08x for opcode: "
                            "%02x -%02x - %02x (%08x) 0x" ADDRX "\n",
                            ctx.opcode & handler->inval, opc1(ctx.opcode),
                            opc2(ctx.opcode), opc3(ctx.opcode),
                            ctx.opcode, ctx.nip - 4);
                } else {
                    printf("invalid bits: %08x for opcode: "
                           "%02x -%02x - %02x (%08x) 0x" ADDRX "\n",
                           ctx.opcode & handler->inval, opc1(ctx.opcode),
                           opc2(ctx.opcode), opc3(ctx.opcode),
                           ctx.opcode, ctx.nip - 4);
                }
                RET_INVAL(ctxp);
                break;
            }
        }
        (*(handler->handler))(&ctx);
#if defined(DO_PPC_STATISTICS)
        handler->count++;
#endif
        /* Check trace mode exceptions */
        if (unlikely((msr_be && ctx.exception == EXCP_BRANCH) ||
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
                      ctx.exception != EXCP_TRAP))) {
            RET_EXCP(ctxp, EXCP_TRACE, 0);
        }
        /* if we reach a page boundary or are single stepping, stop
         * generation
         */
        if (unlikely(((ctx.nip & (TARGET_PAGE_SIZE - 1)) == 0) ||
                     (env->singlestep_enabled))) {
            break;
        }
#if defined (DO_SINGLE_STEP)
        break;
#endif
    }
    if (ctx.exception == EXCP_NONE) {
        gen_goto_tb(&ctx, 0, ctx.nip);
    } else if (ctx.exception != EXCP_BRANCH) {
        gen_op_reset_T0();
        /* Generate the return instruction */
        gen_op_exit_tb();
    }
    *gen_opc_ptr = INDEX_op_end;
    if (unlikely(search_pc)) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
        tb->size = 0;
    } else {
        tb->size = ctx.nip - pc_start;
    }
#if defined(DEBUG_DISAS)
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "---------------- excp: %04x\n", ctx.exception);
        cpu_dump_state(env, logfile, fprintf, 0);
    }
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        int flags;
        flags = msr_le;
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, ctx.nip - pc_start, flags);
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
