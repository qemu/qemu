/*
 *  PowerPC emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (C) 2011 Freescale Semiconductor, Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/host-utils.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#define CPU_SINGLE_STEP 0x1
#define CPU_BRANCH_STEP 0x2
#define GDBSTUB_SINGLE_STEP 0x4

/* Include definitions for instructions classes and implementations flags */
//#define PPC_DEBUG_DISAS
//#define DO_PPC_STATISTICS

#ifdef PPC_DEBUG_DISAS
#  define LOG_DISAS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif
/*****************************************************************************/
/* Code translation helpers                                                  */

/* global register indexes */
static TCGv_ptr cpu_env;
static char cpu_reg_names[10*3 + 22*4 /* GPR */
#if !defined(TARGET_PPC64)
    + 10*4 + 22*5 /* SPE GPRh */
#endif
    + 10*4 + 22*5 /* FPR */
    + 2*(10*6 + 22*7) /* AVRh, AVRl */
    + 10*5 + 22*6 /* VSR */
    + 8*5 /* CRF */];
static TCGv cpu_gpr[32];
#if !defined(TARGET_PPC64)
static TCGv cpu_gprh[32];
#endif
static TCGv_i64 cpu_fpr[32];
static TCGv_i64 cpu_avrh[32], cpu_avrl[32];
static TCGv_i64 cpu_vsr[32];
static TCGv_i32 cpu_crf[8];
static TCGv cpu_nip;
static TCGv cpu_msr;
static TCGv cpu_ctr;
static TCGv cpu_lr;
#if defined(TARGET_PPC64)
static TCGv cpu_cfar;
#endif
static TCGv cpu_xer, cpu_so, cpu_ov, cpu_ca;
static TCGv cpu_reserve;
static TCGv cpu_fpscr;
static TCGv_i32 cpu_access_type;

#include "exec/gen-icount.h"

void ppc_translate_init(void)
{
    int i;
    char* p;
    size_t cpu_reg_names_size;
    static int done_init = 0;

    if (done_init)
        return;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");

    p = cpu_reg_names;
    cpu_reg_names_size = sizeof(cpu_reg_names);

    for (i = 0; i < 8; i++) {
        snprintf(p, cpu_reg_names_size, "crf%d", i);
        cpu_crf[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                            offsetof(CPUPPCState, crf[i]), p);
        p += 5;
        cpu_reg_names_size -= 5;
    }

    for (i = 0; i < 32; i++) {
        snprintf(p, cpu_reg_names_size, "r%d", i);
        cpu_gpr[i] = tcg_global_mem_new(TCG_AREG0,
                                        offsetof(CPUPPCState, gpr[i]), p);
        p += (i < 10) ? 3 : 4;
        cpu_reg_names_size -= (i < 10) ? 3 : 4;
#if !defined(TARGET_PPC64)
        snprintf(p, cpu_reg_names_size, "r%dH", i);
        cpu_gprh[i] = tcg_global_mem_new_i32(TCG_AREG0,
                                             offsetof(CPUPPCState, gprh[i]), p);
        p += (i < 10) ? 4 : 5;
        cpu_reg_names_size -= (i < 10) ? 4 : 5;
#endif

        snprintf(p, cpu_reg_names_size, "fp%d", i);
        cpu_fpr[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                            offsetof(CPUPPCState, fpr[i]), p);
        p += (i < 10) ? 4 : 5;
        cpu_reg_names_size -= (i < 10) ? 4 : 5;

        snprintf(p, cpu_reg_names_size, "avr%dH", i);
#ifdef HOST_WORDS_BIGENDIAN
        cpu_avrh[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                             offsetof(CPUPPCState, avr[i].u64[0]), p);
#else
        cpu_avrh[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                             offsetof(CPUPPCState, avr[i].u64[1]), p);
#endif
        p += (i < 10) ? 6 : 7;
        cpu_reg_names_size -= (i < 10) ? 6 : 7;

        snprintf(p, cpu_reg_names_size, "avr%dL", i);
#ifdef HOST_WORDS_BIGENDIAN
        cpu_avrl[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                             offsetof(CPUPPCState, avr[i].u64[1]), p);
#else
        cpu_avrl[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                             offsetof(CPUPPCState, avr[i].u64[0]), p);
#endif
        p += (i < 10) ? 6 : 7;
        cpu_reg_names_size -= (i < 10) ? 6 : 7;
        snprintf(p, cpu_reg_names_size, "vsr%d", i);
        cpu_vsr[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                             offsetof(CPUPPCState, vsr[i]), p);
        p += (i < 10) ? 5 : 6;
        cpu_reg_names_size -= (i < 10) ? 5 : 6;
    }

    cpu_nip = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUPPCState, nip), "nip");

    cpu_msr = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUPPCState, msr), "msr");

    cpu_ctr = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUPPCState, ctr), "ctr");

    cpu_lr = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUPPCState, lr), "lr");

#if defined(TARGET_PPC64)
    cpu_cfar = tcg_global_mem_new(TCG_AREG0,
                                  offsetof(CPUPPCState, cfar), "cfar");
#endif

    cpu_xer = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUPPCState, xer), "xer");
    cpu_so = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUPPCState, so), "SO");
    cpu_ov = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUPPCState, ov), "OV");
    cpu_ca = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUPPCState, ca), "CA");

    cpu_reserve = tcg_global_mem_new(TCG_AREG0,
                                     offsetof(CPUPPCState, reserve_addr),
                                     "reserve_addr");

    cpu_fpscr = tcg_global_mem_new(TCG_AREG0,
                                   offsetof(CPUPPCState, fpscr), "fpscr");

    cpu_access_type = tcg_global_mem_new_i32(TCG_AREG0,
                                             offsetof(CPUPPCState, access_type), "access_type");

    done_init = 1;
}

/* internal defines */
typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong nip;
    uint32_t opcode;
    uint32_t exception;
    /* Routine used to access memory */
    int mem_idx;
    int access_type;
    /* Translation flags */
    int le_mode;
#if defined(TARGET_PPC64)
    int sf_mode;
    int has_cfar;
#endif
    int fpu_enabled;
    int altivec_enabled;
    int vsx_enabled;
    int spe_enabled;
    ppc_spr_t *spr_cb; /* Needed to check rights for mfspr/mtspr */
    int singlestep_enabled;
    uint64_t insns_flags;
    uint64_t insns_flags2;
} DisasContext;

/* True when active word size < size of target_long.  */
#ifdef TARGET_PPC64
# define NARROW_MODE(C)  (!(C)->sf_mode)
#else
# define NARROW_MODE(C)  0
#endif

struct opc_handler_t {
    /* invalid bits for instruction 1 (Rc(opcode) == 0) */
    uint32_t inval1;
    /* invalid bits for instruction 2 (Rc(opcode) == 1) */
    uint32_t inval2;
    /* instruction type */
    uint64_t type;
    /* extended instruction type */
    uint64_t type2;
    /* handler */
    void (*handler)(DisasContext *ctx);
#if defined(DO_PPC_STATISTICS) || defined(PPC_DUMP_CPU)
    const char *oname;
#endif
#if defined(DO_PPC_STATISTICS)
    uint64_t count;
#endif
};

static inline void gen_reset_fpstatus(void)
{
    gen_helper_reset_fpstatus(cpu_env);
}

static inline void gen_compute_fprf(TCGv_i64 arg, int set_fprf, int set_rc)
{
    TCGv_i32 t0 = tcg_temp_new_i32();

    if (set_fprf != 0) {
        /* This case might be optimized later */
        tcg_gen_movi_i32(t0, 1);
        gen_helper_compute_fprf(t0, cpu_env, arg, t0);
        if (unlikely(set_rc)) {
            tcg_gen_mov_i32(cpu_crf[1], t0);
        }
        gen_helper_float_check_status(cpu_env);
    } else if (unlikely(set_rc)) {
        /* We always need to compute fpcc */
        tcg_gen_movi_i32(t0, 0);
        gen_helper_compute_fprf(t0, cpu_env, arg, t0);
        tcg_gen_mov_i32(cpu_crf[1], t0);
    }

    tcg_temp_free_i32(t0);
}

static inline void gen_set_access_type(DisasContext *ctx, int access_type)
{
    if (ctx->access_type != access_type) {
        tcg_gen_movi_i32(cpu_access_type, access_type);
        ctx->access_type = access_type;
    }
}

static inline void gen_update_nip(DisasContext *ctx, target_ulong nip)
{
    if (NARROW_MODE(ctx)) {
        nip = (uint32_t)nip;
    }
    tcg_gen_movi_tl(cpu_nip, nip);
}

static inline void gen_exception_err(DisasContext *ctx, uint32_t excp, uint32_t error)
{
    TCGv_i32 t0, t1;
    if (ctx->exception == POWERPC_EXCP_NONE) {
        gen_update_nip(ctx, ctx->nip);
    }
    t0 = tcg_const_i32(excp);
    t1 = tcg_const_i32(error);
    gen_helper_raise_exception_err(cpu_env, t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    ctx->exception = (excp);
}

static inline void gen_exception(DisasContext *ctx, uint32_t excp)
{
    TCGv_i32 t0;
    if (ctx->exception == POWERPC_EXCP_NONE) {
        gen_update_nip(ctx, ctx->nip);
    }
    t0 = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, t0);
    tcg_temp_free_i32(t0);
    ctx->exception = (excp);
}

static inline void gen_debug_exception(DisasContext *ctx)
{
    TCGv_i32 t0;

    if ((ctx->exception != POWERPC_EXCP_BRANCH) &&
        (ctx->exception != POWERPC_EXCP_SYNC)) {
        gen_update_nip(ctx, ctx->nip);
    }
    t0 = tcg_const_i32(EXCP_DEBUG);
    gen_helper_raise_exception(cpu_env, t0);
    tcg_temp_free_i32(t0);
}

static inline void gen_inval_exception(DisasContext *ctx, uint32_t error)
{
    gen_exception_err(ctx, POWERPC_EXCP_PROGRAM, POWERPC_EXCP_INVAL | error);
}

/* Stop translation */
static inline void gen_stop_exception(DisasContext *ctx)
{
    gen_update_nip(ctx, ctx->nip);
    ctx->exception = POWERPC_EXCP_STOP;
}

/* No need to update nip here, as execution flow will change */
static inline void gen_sync_exception(DisasContext *ctx)
{
    ctx->exception = POWERPC_EXCP_SYNC;
}

#define GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                      \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type, PPC_NONE)

#define GEN_HANDLER_E(name, opc1, opc2, opc3, inval, type, type2)             \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type, type2)

#define GEN_HANDLER2(name, onam, opc1, opc2, opc3, inval, type)               \
GEN_OPCODE2(name, onam, opc1, opc2, opc3, inval, type, PPC_NONE)

#define GEN_HANDLER2_E(name, onam, opc1, opc2, opc3, inval, type, type2)      \
GEN_OPCODE2(name, onam, opc1, opc2, opc3, inval, type, type2)

typedef struct opcode_t {
    unsigned char opc1, opc2, opc3;
#if HOST_LONG_BITS == 64 /* Explicitly align to 64 bits */
    unsigned char pad[5];
#else
    unsigned char pad[1];
#endif
    opc_handler_t handler;
    const char *oname;
} opcode_t;

/*****************************************************************************/
/***                           Instruction decoding                        ***/
#define EXTRACT_HELPER(name, shift, nb)                                       \
static inline uint32_t name(uint32_t opcode)                                  \
{                                                                             \
    return (opcode >> (shift)) & ((1 << (nb)) - 1);                           \
}

#define EXTRACT_SHELPER(name, shift, nb)                                      \
static inline int32_t name(uint32_t opcode)                                   \
{                                                                             \
    return (int16_t)((opcode >> (shift)) & ((1 << (nb)) - 1));                \
}

#define EXTRACT_HELPER_SPLIT(name, shift1, nb1, shift2, nb2)                  \
static inline uint32_t name(uint32_t opcode)                                  \
{                                                                             \
    return (((opcode >> (shift1)) & ((1 << (nb1)) - 1)) << nb2) |             \
            ((opcode >> (shift2)) & ((1 << (nb2)) - 1));                      \
}
/* Opcode part 1 */
EXTRACT_HELPER(opc1, 26, 6);
/* Opcode part 2 */
EXTRACT_HELPER(opc2, 1, 5);
/* Opcode part 3 */
EXTRACT_HELPER(opc3, 6, 5);
/* Update Cr0 flags */
EXTRACT_HELPER(Rc, 0, 1);
/* Update Cr6 flags (Altivec) */
EXTRACT_HELPER(Rc21, 10, 1);
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
static inline uint32_t SPR(uint32_t opcode)
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
/* 5 bits signed immediate value */
EXTRACT_HELPER(SIMM5, 16, 5);
/* 5 bits signed immediate value */
EXTRACT_HELPER(UIMM5, 16, 5);
/* Bit count */
EXTRACT_HELPER(NB, 11, 5);
/* Shift count */
EXTRACT_HELPER(SH, 11, 5);
/* Vector shift count */
EXTRACT_HELPER(VSH, 6, 4);
/* Mask start */
EXTRACT_HELPER(MB, 6, 5);
/* Mask end */
EXTRACT_HELPER(ME, 1, 5);
/* Trap operand */
EXTRACT_HELPER(TO, 21, 5);

EXTRACT_HELPER(CRM, 12, 8);
EXTRACT_HELPER(SR, 16, 4);

/* mtfsf/mtfsfi */
EXTRACT_HELPER(FPBF, 23, 3);
EXTRACT_HELPER(FPIMM, 12, 4);
EXTRACT_HELPER(FPL, 25, 1);
EXTRACT_HELPER(FPFLM, 17, 8);
EXTRACT_HELPER(FPW, 16, 1);

/***                            Jump target decoding                       ***/
/* Displacement */
EXTRACT_SHELPER(d, 0, 16);
/* Immediate address */
static inline target_ulong LI(uint32_t opcode)
{
    return (opcode >> 0) & 0x03FFFFFC;
}

static inline uint32_t BD(uint32_t opcode)
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
static inline target_ulong MASK(uint32_t start, uint32_t end)
{
    target_ulong ret;

#if defined(TARGET_PPC64)
    if (likely(start == 0)) {
        ret = UINT64_MAX << (63 - end);
    } else if (likely(end == 63)) {
        ret = UINT64_MAX >> start;
    }
#else
    if (likely(start == 0)) {
        ret = UINT32_MAX << (31  - end);
    } else if (likely(end == 31)) {
        ret = UINT32_MAX >> start;
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

EXTRACT_HELPER_SPLIT(xT, 0, 1, 21, 5);
EXTRACT_HELPER_SPLIT(xS, 0, 1, 21, 5);
EXTRACT_HELPER_SPLIT(xA, 2, 1, 16, 5);
EXTRACT_HELPER_SPLIT(xB, 1, 1, 11, 5);
EXTRACT_HELPER_SPLIT(xC, 3, 1,  6, 5);
EXTRACT_HELPER(DM, 8, 2);
EXTRACT_HELPER(UIM, 16, 2);
EXTRACT_HELPER(SHW, 8, 2);
/*****************************************************************************/
/* PowerPC instructions table                                                */

#if defined(DO_PPC_STATISTICS)
#define GEN_OPCODE(name, op1, op2, op3, invl, _typ, _typ2)                    \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
        .oname = stringify(name),                                             \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#define GEN_OPCODE_DUAL(name, op1, op2, op3, invl1, invl2, _typ, _typ2)       \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl1,                                                     \
        .inval2  = invl2,                                                     \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
        .oname = stringify(name),                                             \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#define GEN_OPCODE2(name, onam, op1, op2, op3, invl, _typ, _typ2)             \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
        .oname = onam,                                                        \
    },                                                                        \
    .oname = onam,                                                            \
}
#else
#define GEN_OPCODE(name, op1, op2, op3, invl, _typ, _typ2)                    \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#define GEN_OPCODE_DUAL(name, op1, op2, op3, invl1, invl2, _typ, _typ2)       \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl1,                                                     \
        .inval2  = invl2,                                                     \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#define GEN_OPCODE2(name, onam, op1, op2, op3, invl, _typ, _typ2)             \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .pad  = { 0, },                                                           \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = onam,                                                            \
}
#endif

/* SPR load/store helpers */
static inline void gen_load_spr(TCGv t, int reg)
{
    tcg_gen_ld_tl(t, cpu_env, offsetof(CPUPPCState, spr[reg]));
}

static inline void gen_store_spr(int reg, TCGv t)
{
    tcg_gen_st_tl(t, cpu_env, offsetof(CPUPPCState, spr[reg]));
}

/* Invalid instruction */
static void gen_invalid(DisasContext *ctx)
{
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

static opc_handler_t invalid_handler = {
    .inval1  = 0xFFFFFFFF,
    .inval2  = 0xFFFFFFFF,
    .type    = PPC_NONE,
    .type2   = PPC_NONE,
    .handler = gen_invalid,
};

#if defined(TARGET_PPC64)
/* NOTE: as this time, the only use of is_user_mode() is in 64 bit code.  And */
/*       so the function is wrapped in the standard 64-bit ifdef in order to  */
/*       avoid compiler warnings in 32-bit implementations.                   */
static bool is_user_mode(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    return true;
#else
    return ctx->mem_idx == 0;
#endif
}
#endif

/***                           Integer comparison                          ***/

static inline void gen_op_cmp(TCGv arg0, TCGv arg1, int s, int crf)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(cpu_crf[crf], cpu_so);

    tcg_gen_setcond_tl((s ? TCG_COND_LT: TCG_COND_LTU), t0, arg0, arg1);
    tcg_gen_trunc_tl_i32(t1, t0);
    tcg_gen_shli_i32(t1, t1, CRF_LT);
    tcg_gen_or_i32(cpu_crf[crf], cpu_crf[crf], t1);

    tcg_gen_setcond_tl((s ? TCG_COND_GT: TCG_COND_GTU), t0, arg0, arg1);
    tcg_gen_trunc_tl_i32(t1, t0);
    tcg_gen_shli_i32(t1, t1, CRF_GT);
    tcg_gen_or_i32(cpu_crf[crf], cpu_crf[crf], t1);

    tcg_gen_setcond_tl(TCG_COND_EQ, t0, arg0, arg1);
    tcg_gen_trunc_tl_i32(t1, t0);
    tcg_gen_shli_i32(t1, t1, CRF_EQ);
    tcg_gen_or_i32(cpu_crf[crf], cpu_crf[crf], t1);

    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

static inline void gen_op_cmpi(TCGv arg0, target_ulong arg1, int s, int crf)
{
    TCGv t0 = tcg_const_tl(arg1);
    gen_op_cmp(arg0, t0, s, crf);
    tcg_temp_free(t0);
}

static inline void gen_op_cmp32(TCGv arg0, TCGv arg1, int s, int crf)
{
    TCGv t0, t1;
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    if (s) {
        tcg_gen_ext32s_tl(t0, arg0);
        tcg_gen_ext32s_tl(t1, arg1);
    } else {
        tcg_gen_ext32u_tl(t0, arg0);
        tcg_gen_ext32u_tl(t1, arg1);
    }
    gen_op_cmp(t0, t1, s, crf);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
}

static inline void gen_op_cmpi32(TCGv arg0, target_ulong arg1, int s, int crf)
{
    TCGv t0 = tcg_const_tl(arg1);
    gen_op_cmp32(arg0, t0, s, crf);
    tcg_temp_free(t0);
}

static inline void gen_set_Rc0(DisasContext *ctx, TCGv reg)
{
    if (NARROW_MODE(ctx)) {
        gen_op_cmpi32(reg, 0, 1, 0);
    } else {
        gen_op_cmpi(reg, 0, 1, 0);
    }
}

/* cmp */
static void gen_cmp(DisasContext *ctx)
{
    if ((ctx->opcode & 0x00200000) && (ctx->insns_flags & PPC_64B)) {
        gen_op_cmp(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                   1, crfD(ctx->opcode));
    } else {
        gen_op_cmp32(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                     1, crfD(ctx->opcode));
    }
}

/* cmpi */
static void gen_cmpi(DisasContext *ctx)
{
    if ((ctx->opcode & 0x00200000) && (ctx->insns_flags & PPC_64B)) {
        gen_op_cmpi(cpu_gpr[rA(ctx->opcode)], SIMM(ctx->opcode),
                    1, crfD(ctx->opcode));
    } else {
        gen_op_cmpi32(cpu_gpr[rA(ctx->opcode)], SIMM(ctx->opcode),
                      1, crfD(ctx->opcode));
    }
}

/* cmpl */
static void gen_cmpl(DisasContext *ctx)
{
    if ((ctx->opcode & 0x00200000) && (ctx->insns_flags & PPC_64B)) {
        gen_op_cmp(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                   0, crfD(ctx->opcode));
    } else {
        gen_op_cmp32(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                     0, crfD(ctx->opcode));
    }
}

/* cmpli */
static void gen_cmpli(DisasContext *ctx)
{
    if ((ctx->opcode & 0x00200000) && (ctx->insns_flags & PPC_64B)) {
        gen_op_cmpi(cpu_gpr[rA(ctx->opcode)], UIMM(ctx->opcode),
                    0, crfD(ctx->opcode));
    } else {
        gen_op_cmpi32(cpu_gpr[rA(ctx->opcode)], UIMM(ctx->opcode),
                      0, crfD(ctx->opcode));
    }
}

/* isel (PowerPC 2.03 specification) */
static void gen_isel(DisasContext *ctx)
{
    int l1, l2;
    uint32_t bi = rC(ctx->opcode);
    uint32_t mask;
    TCGv_i32 t0;

    l1 = gen_new_label();
    l2 = gen_new_label();

    mask = 1 << (3 - (bi & 0x03));
    t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_crf[bi >> 2], mask);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
    if (rA(ctx->opcode) == 0)
        tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], 0);
    else
        tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}

/* cmpb: PowerPC 2.05 specification */
static void gen_cmpb(DisasContext *ctx)
{
    gen_helper_cmpb(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                    cpu_gpr[rB(ctx->opcode)]);
}

/***                           Integer arithmetic                          ***/

static inline void gen_op_arith_compute_ov(DisasContext *ctx, TCGv arg0,
                                           TCGv arg1, TCGv arg2, int sub)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_xor_tl(cpu_ov, arg0, arg2);
    tcg_gen_xor_tl(t0, arg1, arg2);
    if (sub) {
        tcg_gen_and_tl(cpu_ov, cpu_ov, t0);
    } else {
        tcg_gen_andc_tl(cpu_ov, cpu_ov, t0);
    }
    tcg_temp_free(t0);
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32s_tl(cpu_ov, cpu_ov);
    }
    tcg_gen_shri_tl(cpu_ov, cpu_ov, TARGET_LONG_BITS - 1);
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);
}

/* Common add function */
static inline void gen_op_arith_add(DisasContext *ctx, TCGv ret, TCGv arg1,
                                    TCGv arg2, bool add_ca, bool compute_ca,
                                    bool compute_ov, bool compute_rc0)
{
    TCGv t0 = ret;

    if (compute_ca || compute_ov) {
        t0 = tcg_temp_new();
    }

    if (compute_ca) {
        if (NARROW_MODE(ctx)) {
            /* Caution: a non-obvious corner case of the spec is that we
               must produce the *entire* 64-bit addition, but produce the
               carry into bit 32.  */
            TCGv t1 = tcg_temp_new();
            tcg_gen_xor_tl(t1, arg1, arg2);        /* add without carry */
            tcg_gen_add_tl(t0, arg1, arg2);
            if (add_ca) {
                tcg_gen_add_tl(t0, t0, cpu_ca);
            }
            tcg_gen_xor_tl(cpu_ca, t0, t1);        /* bits changed w/ carry */
            tcg_temp_free(t1);
            tcg_gen_shri_tl(cpu_ca, cpu_ca, 32);   /* extract bit 32 */
            tcg_gen_andi_tl(cpu_ca, cpu_ca, 1);
        } else {
            TCGv zero = tcg_const_tl(0);
            if (add_ca) {
                tcg_gen_add2_tl(t0, cpu_ca, arg1, zero, cpu_ca, zero);
                tcg_gen_add2_tl(t0, cpu_ca, t0, cpu_ca, arg2, zero);
            } else {
                tcg_gen_add2_tl(t0, cpu_ca, arg1, zero, arg2, zero);
            }
            tcg_temp_free(zero);
        }
    } else {
        tcg_gen_add_tl(t0, arg1, arg2);
        if (add_ca) {
            tcg_gen_add_tl(t0, t0, cpu_ca);
        }
    }

    if (compute_ov) {
        gen_op_arith_compute_ov(ctx, t0, arg1, arg2, 0);
    }
    if (unlikely(compute_rc0)) {
        gen_set_Rc0(ctx, t0);
    }

    if (!TCGV_EQUAL(t0, ret)) {
        tcg_gen_mov_tl(ret, t0);
        tcg_temp_free(t0);
    }
}
/* Add functions with two operands */
#define GEN_INT_ARITH_ADD(name, opc3, add_ca, compute_ca, compute_ov)         \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)],                           \
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],      \
                     add_ca, compute_ca, compute_ov, Rc(ctx->opcode));        \
}
/* Add functions with one operand and one immediate */
#define GEN_INT_ARITH_ADD_CONST(name, opc3, const_val,                        \
                                add_ca, compute_ca, compute_ov)               \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    TCGv t0 = tcg_const_tl(const_val);                                        \
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)],                           \
                     cpu_gpr[rA(ctx->opcode)], t0,                            \
                     add_ca, compute_ca, compute_ov, Rc(ctx->opcode));        \
    tcg_temp_free(t0);                                                        \
}

/* add  add.  addo  addo. */
GEN_INT_ARITH_ADD(add, 0x08, 0, 0, 0)
GEN_INT_ARITH_ADD(addo, 0x18, 0, 0, 1)
/* addc  addc.  addco  addco. */
GEN_INT_ARITH_ADD(addc, 0x00, 0, 1, 0)
GEN_INT_ARITH_ADD(addco, 0x10, 0, 1, 1)
/* adde  adde.  addeo  addeo. */
GEN_INT_ARITH_ADD(adde, 0x04, 1, 1, 0)
GEN_INT_ARITH_ADD(addeo, 0x14, 1, 1, 1)
/* addme  addme.  addmeo  addmeo.  */
GEN_INT_ARITH_ADD_CONST(addme, 0x07, -1LL, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addmeo, 0x17, -1LL, 1, 1, 1)
/* addze  addze.  addzeo  addzeo.*/
GEN_INT_ARITH_ADD_CONST(addze, 0x06, 0, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addzeo, 0x16, 0, 1, 1, 1)
/* addi */
static void gen_addi(DisasContext *ctx)
{
    target_long simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        /* li case */
        tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], simm);
    } else {
        tcg_gen_addi_tl(cpu_gpr[rD(ctx->opcode)],
                        cpu_gpr[rA(ctx->opcode)], simm);
    }
}
/* addic  addic.*/
static inline void gen_op_addic(DisasContext *ctx, bool compute_rc0)
{
    TCGv c = tcg_const_tl(SIMM(ctx->opcode));
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                     c, 0, 1, 0, compute_rc0);
    tcg_temp_free(c);
}

static void gen_addic(DisasContext *ctx)
{
    gen_op_addic(ctx, 0);
}

static void gen_addic_(DisasContext *ctx)
{
    gen_op_addic(ctx, 1);
}

/* addis */
static void gen_addis(DisasContext *ctx)
{
    target_long simm = SIMM(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        /* lis case */
        tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], simm << 16);
    } else {
        tcg_gen_addi_tl(cpu_gpr[rD(ctx->opcode)],
                        cpu_gpr[rA(ctx->opcode)], simm << 16);
    }
}

static inline void gen_op_arith_divw(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, int sign, int compute_ov)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();
    TCGv_i32 t1 = tcg_temp_local_new_i32();

    tcg_gen_trunc_tl_i32(t0, arg1);
    tcg_gen_trunc_tl_i32(t1, arg2);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t1, 0, l1);
    if (sign) {
        int l3 = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, t1, -1, l3);
        tcg_gen_brcondi_i32(TCG_COND_EQ, t0, INT32_MIN, l1);
        gen_set_label(l3);
        tcg_gen_div_i32(t0, t0, t1);
    } else {
        tcg_gen_divu_i32(t0, t0, t1);
    }
    if (compute_ov) {
        tcg_gen_movi_tl(cpu_ov, 0);
    }
    tcg_gen_br(l2);
    gen_set_label(l1);
    if (sign) {
        tcg_gen_sari_i32(t0, t0, 31);
    } else {
        tcg_gen_movi_i32(t0, 0);
    }
    if (compute_ov) {
        tcg_gen_movi_tl(cpu_ov, 1);
        tcg_gen_movi_tl(cpu_so, 1);
    }
    gen_set_label(l2);
    tcg_gen_extu_i32_tl(ret, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, ret);
}
/* Div functions */
#define GEN_INT_ARITH_DIVW(name, opc3, sign, compute_ov)                      \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    gen_op_arith_divw(ctx, cpu_gpr[rD(ctx->opcode)],                          \
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],      \
                     sign, compute_ov);                                       \
}
/* divwu  divwu.  divwuo  divwuo.   */
GEN_INT_ARITH_DIVW(divwu, 0x0E, 0, 0);
GEN_INT_ARITH_DIVW(divwuo, 0x1E, 0, 1);
/* divw  divw.  divwo  divwo.   */
GEN_INT_ARITH_DIVW(divw, 0x0F, 1, 0);
GEN_INT_ARITH_DIVW(divwo, 0x1F, 1, 1);

/* div[wd]eu[o][.] */
#define GEN_DIVE(name, hlpr, compute_ov)                                      \
static void gen_##name(DisasContext *ctx)                                     \
{                                                                             \
    TCGv_i32 t0 = tcg_const_i32(compute_ov);                                  \
    gen_helper_##hlpr(cpu_gpr[rD(ctx->opcode)], cpu_env,                      \
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)], t0); \
    tcg_temp_free_i32(t0);                                                    \
    if (unlikely(Rc(ctx->opcode) != 0)) {                                     \
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);                           \
    }                                                                         \
}

GEN_DIVE(divweu, divweu, 0);
GEN_DIVE(divweuo, divweu, 1);
GEN_DIVE(divwe, divwe, 0);
GEN_DIVE(divweo, divwe, 1);

#if defined(TARGET_PPC64)
static inline void gen_op_arith_divd(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, int sign, int compute_ov)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();

    tcg_gen_brcondi_i64(TCG_COND_EQ, arg2, 0, l1);
    if (sign) {
        int l3 = gen_new_label();
        tcg_gen_brcondi_i64(TCG_COND_NE, arg2, -1, l3);
        tcg_gen_brcondi_i64(TCG_COND_EQ, arg1, INT64_MIN, l1);
        gen_set_label(l3);
        tcg_gen_div_i64(ret, arg1, arg2);
    } else {
        tcg_gen_divu_i64(ret, arg1, arg2);
    }
    if (compute_ov) {
        tcg_gen_movi_tl(cpu_ov, 0);
    }
    tcg_gen_br(l2);
    gen_set_label(l1);
    if (sign) {
        tcg_gen_sari_i64(ret, arg1, 63);
    } else {
        tcg_gen_movi_i64(ret, 0);
    }
    if (compute_ov) {
        tcg_gen_movi_tl(cpu_ov, 1);
        tcg_gen_movi_tl(cpu_so, 1);
    }
    gen_set_label(l2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, ret);
}
#define GEN_INT_ARITH_DIVD(name, opc3, sign, compute_ov)                      \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    gen_op_arith_divd(ctx, cpu_gpr[rD(ctx->opcode)],                          \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],     \
                      sign, compute_ov);                                      \
}
/* divwu  divwu.  divwuo  divwuo.   */
GEN_INT_ARITH_DIVD(divdu, 0x0E, 0, 0);
GEN_INT_ARITH_DIVD(divduo, 0x1E, 0, 1);
/* divw  divw.  divwo  divwo.   */
GEN_INT_ARITH_DIVD(divd, 0x0F, 1, 0);
GEN_INT_ARITH_DIVD(divdo, 0x1F, 1, 1);

GEN_DIVE(divdeu, divdeu, 0);
GEN_DIVE(divdeuo, divdeu, 1);
GEN_DIVE(divde, divde, 0);
GEN_DIVE(divdeo, divde, 1);
#endif

/* mulhw  mulhw. */
static void gen_mulhw(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_muls2_i32(t0, t1, t0, t1);
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mulhwu  mulhwu.  */
static void gen_mulhwu(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mulu2_i32(t0, t1, t0, t1);
    tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mullw  mullw. */
static void gen_mullw(DisasContext *ctx)
{
    tcg_gen_mul_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                   cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_ext32s_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mullwo  mullwo. */
static void gen_mullwo(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_muls2_i32(t0, t1, t0, t1);
    tcg_gen_ext_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);

    tcg_gen_sari_i32(t0, t0, 31);
    tcg_gen_setcond_i32(TCG_COND_NE, t0, t0, t1);
    tcg_gen_extu_i32_tl(cpu_ov, t0);
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mulli */
static void gen_mulli(DisasContext *ctx)
{
    tcg_gen_muli_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    SIMM(ctx->opcode));
}

#if defined(TARGET_PPC64)
/* mulhd  mulhd. */
static void gen_mulhd(DisasContext *ctx)
{
    TCGv lo = tcg_temp_new();
    tcg_gen_muls2_tl(lo, cpu_gpr[rD(ctx->opcode)],
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_temp_free(lo);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

/* mulhdu  mulhdu. */
static void gen_mulhdu(DisasContext *ctx)
{
    TCGv lo = tcg_temp_new();
    tcg_gen_mulu2_tl(lo, cpu_gpr[rD(ctx->opcode)],
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_temp_free(lo);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

/* mulld  mulld. */
static void gen_mulld(DisasContext *ctx)
{
    tcg_gen_mul_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                   cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mulldo  mulldo. */
static void gen_mulldo(DisasContext *ctx)
{
    gen_helper_mulldo(cpu_gpr[rD(ctx->opcode)], cpu_env,
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}
#endif

/* Common subf function */
static inline void gen_op_arith_subf(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, bool add_ca, bool compute_ca,
                                     bool compute_ov, bool compute_rc0)
{
    TCGv t0 = ret;

    if (compute_ca || compute_ov) {
        t0 = tcg_temp_new();
    }

    if (compute_ca) {
        /* dest = ~arg1 + arg2 [+ ca].  */
        if (NARROW_MODE(ctx)) {
            /* Caution: a non-obvious corner case of the spec is that we
               must produce the *entire* 64-bit addition, but produce the
               carry into bit 32.  */
            TCGv inv1 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            tcg_gen_not_tl(inv1, arg1);
            if (add_ca) {
                tcg_gen_add_tl(t0, arg2, cpu_ca);
            } else {
                tcg_gen_addi_tl(t0, arg2, 1);
            }
            tcg_gen_xor_tl(t1, arg2, inv1);         /* add without carry */
            tcg_gen_add_tl(t0, t0, inv1);
            tcg_gen_xor_tl(cpu_ca, t0, t1);         /* bits changes w/ carry */
            tcg_temp_free(t1);
            tcg_gen_shri_tl(cpu_ca, cpu_ca, 32);    /* extract bit 32 */
            tcg_gen_andi_tl(cpu_ca, cpu_ca, 1);
        } else if (add_ca) {
            TCGv zero, inv1 = tcg_temp_new();
            tcg_gen_not_tl(inv1, arg1);
            zero = tcg_const_tl(0);
            tcg_gen_add2_tl(t0, cpu_ca, arg2, zero, cpu_ca, zero);
            tcg_gen_add2_tl(t0, cpu_ca, t0, cpu_ca, inv1, zero);
            tcg_temp_free(zero);
            tcg_temp_free(inv1);
        } else {
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_ca, arg2, arg1);
            tcg_gen_sub_tl(t0, arg2, arg1);
        }
    } else if (add_ca) {
        /* Since we're ignoring carry-out, we can simplify the
           standard ~arg1 + arg2 + ca to arg2 - arg1 + ca - 1.  */
        tcg_gen_sub_tl(t0, arg2, arg1);
        tcg_gen_add_tl(t0, t0, cpu_ca);
        tcg_gen_subi_tl(t0, t0, 1);
    } else {
        tcg_gen_sub_tl(t0, arg2, arg1);
    }

    if (compute_ov) {
        gen_op_arith_compute_ov(ctx, t0, arg1, arg2, 1);
    }
    if (unlikely(compute_rc0)) {
        gen_set_Rc0(ctx, t0);
    }

    if (!TCGV_EQUAL(t0, ret)) {
        tcg_gen_mov_tl(ret, t0);
        tcg_temp_free(t0);
    }
}
/* Sub functions with Two operands functions */
#define GEN_INT_ARITH_SUBF(name, opc3, add_ca, compute_ca, compute_ov)        \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    gen_op_arith_subf(ctx, cpu_gpr[rD(ctx->opcode)],                          \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],     \
                      add_ca, compute_ca, compute_ov, Rc(ctx->opcode));       \
}
/* Sub functions with one operand and one immediate */
#define GEN_INT_ARITH_SUBF_CONST(name, opc3, const_val,                       \
                                add_ca, compute_ca, compute_ov)               \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    TCGv t0 = tcg_const_tl(const_val);                                        \
    gen_op_arith_subf(ctx, cpu_gpr[rD(ctx->opcode)],                          \
                      cpu_gpr[rA(ctx->opcode)], t0,                           \
                      add_ca, compute_ca, compute_ov, Rc(ctx->opcode));       \
    tcg_temp_free(t0);                                                        \
}
/* subf  subf.  subfo  subfo. */
GEN_INT_ARITH_SUBF(subf, 0x01, 0, 0, 0)
GEN_INT_ARITH_SUBF(subfo, 0x11, 0, 0, 1)
/* subfc  subfc.  subfco  subfco. */
GEN_INT_ARITH_SUBF(subfc, 0x00, 0, 1, 0)
GEN_INT_ARITH_SUBF(subfco, 0x10, 0, 1, 1)
/* subfe  subfe.  subfeo  subfo. */
GEN_INT_ARITH_SUBF(subfe, 0x04, 1, 1, 0)
GEN_INT_ARITH_SUBF(subfeo, 0x14, 1, 1, 1)
/* subfme  subfme.  subfmeo  subfmeo.  */
GEN_INT_ARITH_SUBF_CONST(subfme, 0x07, -1LL, 1, 1, 0)
GEN_INT_ARITH_SUBF_CONST(subfmeo, 0x17, -1LL, 1, 1, 1)
/* subfze  subfze.  subfzeo  subfzeo.*/
GEN_INT_ARITH_SUBF_CONST(subfze, 0x06, 0, 1, 1, 0)
GEN_INT_ARITH_SUBF_CONST(subfzeo, 0x16, 0, 1, 1, 1)

/* subfic */
static void gen_subfic(DisasContext *ctx)
{
    TCGv c = tcg_const_tl(SIMM(ctx->opcode));
    gen_op_arith_subf(ctx, cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                      c, 0, 1, 0, 0);
    tcg_temp_free(c);
}

/* neg neg. nego nego. */
static inline void gen_op_arith_neg(DisasContext *ctx, bool compute_ov)
{
    TCGv zero = tcg_const_tl(0);
    gen_op_arith_subf(ctx, cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                      zero, 0, 0, compute_ov, Rc(ctx->opcode));
    tcg_temp_free(zero);
}

static void gen_neg(DisasContext *ctx)
{
    gen_op_arith_neg(ctx, 0);
}

static void gen_nego(DisasContext *ctx)
{
    gen_op_arith_neg(ctx, 1);
}

/***                            Integer logical                            ***/
#define GEN_LOGICAL2(name, tcg_op, opc, type)                                 \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    tcg_op(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],                \
       cpu_gpr[rB(ctx->opcode)]);                                             \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);                           \
}

#define GEN_LOGICAL1(name, tcg_op, opc, type)                                 \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    tcg_op(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);               \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);                           \
}

/* and & and. */
GEN_LOGICAL2(and, tcg_gen_and_tl, 0x00, PPC_INTEGER);
/* andc & andc. */
GEN_LOGICAL2(andc, tcg_gen_andc_tl, 0x01, PPC_INTEGER);

/* andi. */
static void gen_andi_(DisasContext *ctx)
{
    tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], UIMM(ctx->opcode));
    gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* andis. */
static void gen_andis_(DisasContext *ctx)
{
    tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], UIMM(ctx->opcode) << 16);
    gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* cntlzw */
static void gen_cntlzw(DisasContext *ctx)
{
    gen_helper_cntlzw(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
/* eqv & eqv. */
GEN_LOGICAL2(eqv, tcg_gen_eqv_tl, 0x08, PPC_INTEGER);
/* extsb & extsb. */
GEN_LOGICAL1(extsb, tcg_gen_ext8s_tl, 0x1D, PPC_INTEGER);
/* extsh & extsh. */
GEN_LOGICAL1(extsh, tcg_gen_ext16s_tl, 0x1C, PPC_INTEGER);
/* nand & nand. */
GEN_LOGICAL2(nand, tcg_gen_nand_tl, 0x0E, PPC_INTEGER);
/* nor & nor. */
GEN_LOGICAL2(nor, tcg_gen_nor_tl, 0x03, PPC_INTEGER);

/* or & or. */
static void gen_or(DisasContext *ctx)
{
    int rs, ra, rb;

    rs = rS(ctx->opcode);
    ra = rA(ctx->opcode);
    rb = rB(ctx->opcode);
    /* Optimisation for mr. ri case */
    if (rs != ra || rs != rb) {
        if (rs != rb)
            tcg_gen_or_tl(cpu_gpr[ra], cpu_gpr[rs], cpu_gpr[rb]);
        else
            tcg_gen_mov_tl(cpu_gpr[ra], cpu_gpr[rs]);
        if (unlikely(Rc(ctx->opcode) != 0))
            gen_set_Rc0(ctx, cpu_gpr[ra]);
    } else if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rs]);
#if defined(TARGET_PPC64)
    } else {
        int prio = 0;

        switch (rs) {
        case 1:
            /* Set process priority to low */
            prio = 2;
            break;
        case 6:
            /* Set process priority to medium-low */
            prio = 3;
            break;
        case 2:
            /* Set process priority to normal */
            prio = 4;
            break;
#if !defined(CONFIG_USER_ONLY)
        case 31:
            if (ctx->mem_idx > 0) {
                /* Set process priority to very low */
                prio = 1;
            }
            break;
        case 5:
            if (ctx->mem_idx > 0) {
                /* Set process priority to medium-hight */
                prio = 5;
            }
            break;
        case 3:
            if (ctx->mem_idx > 0) {
                /* Set process priority to high */
                prio = 6;
            }
            break;
        case 7:
            if (ctx->mem_idx > 1) {
                /* Set process priority to very high */
                prio = 7;
            }
            break;
#endif
        default:
            /* nop */
            break;
        }
        if (prio) {
            TCGv t0 = tcg_temp_new();
            gen_load_spr(t0, SPR_PPR);
            tcg_gen_andi_tl(t0, t0, ~0x001C000000000000ULL);
            tcg_gen_ori_tl(t0, t0, ((uint64_t)prio) << 50);
            gen_store_spr(SPR_PPR, t0);
            tcg_temp_free(t0);
        }
#endif
    }
}
/* orc & orc. */
GEN_LOGICAL2(orc, tcg_gen_orc_tl, 0x0C, PPC_INTEGER);

/* xor & xor. */
static void gen_xor(DisasContext *ctx)
{
    /* Optimisation for "set to zero" case */
    if (rS(ctx->opcode) != rB(ctx->opcode))
        tcg_gen_xor_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    else
        tcg_gen_movi_tl(cpu_gpr[rA(ctx->opcode)], 0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* ori */
static void gen_ori(DisasContext *ctx)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        /* XXX: should handle special NOPs for POWER series */
        return;
    }
    tcg_gen_ori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], uimm);
}

/* oris */
static void gen_oris(DisasContext *ctx)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    tcg_gen_ori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], uimm << 16);
}

/* xori */
static void gen_xori(DisasContext *ctx)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    tcg_gen_xori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], uimm);
}

/* xoris */
static void gen_xoris(DisasContext *ctx)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
        /* NOP */
        return;
    }
    tcg_gen_xori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], uimm << 16);
}

/* popcntb : PowerPC 2.03 specification */
static void gen_popcntb(DisasContext *ctx)
{
    gen_helper_popcntb(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
}

static void gen_popcntw(DisasContext *ctx)
{
    gen_helper_popcntw(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
}

#if defined(TARGET_PPC64)
/* popcntd: PowerPC 2.06 specification */
static void gen_popcntd(DisasContext *ctx)
{
    gen_helper_popcntd(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
}
#endif

/* prtyw: PowerPC 2.05 specification */
static void gen_prtyw(DisasContext *ctx)
{
    TCGv ra = cpu_gpr[rA(ctx->opcode)];
    TCGv rs = cpu_gpr[rS(ctx->opcode)];
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, rs, 16);
    tcg_gen_xor_tl(ra, rs, t0);
    tcg_gen_shri_tl(t0, ra, 8);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_andi_tl(ra, ra, (target_ulong)0x100000001ULL);
    tcg_temp_free(t0);
}

#if defined(TARGET_PPC64)
/* prtyd: PowerPC 2.05 specification */
static void gen_prtyd(DisasContext *ctx)
{
    TCGv ra = cpu_gpr[rA(ctx->opcode)];
    TCGv rs = cpu_gpr[rS(ctx->opcode)];
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, rs, 32);
    tcg_gen_xor_tl(ra, rs, t0);
    tcg_gen_shri_tl(t0, ra, 16);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_shri_tl(t0, ra, 8);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_andi_tl(ra, ra, 1);
    tcg_temp_free(t0);
}
#endif

#if defined(TARGET_PPC64)
/* bpermd */
static void gen_bpermd(DisasContext *ctx)
{
    gen_helper_bpermd(cpu_gpr[rA(ctx->opcode)],
                      cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
}
#endif

#if defined(TARGET_PPC64)
/* extsw & extsw. */
GEN_LOGICAL1(extsw, tcg_gen_ext32s_tl, 0x1E, PPC_64B);

/* cntlzd */
static void gen_cntlzd(DisasContext *ctx)
{
    gen_helper_cntlzd(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
#endif

/***                             Integer rotate                            ***/

/* rlwimi & rlwimi. */
static void gen_rlwimi(DisasContext *ctx)
{
    uint32_t mb, me, sh;

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    sh = SH(ctx->opcode);
    if (likely(sh == 0 && mb == 0 && me == 31)) {
        tcg_gen_ext32u_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
    } else {
        target_ulong mask;
        TCGv t1;
        TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
        TCGv_i32 t2 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(t2, cpu_gpr[rS(ctx->opcode)]);
        tcg_gen_rotli_i32(t2, t2, sh);
        tcg_gen_extu_i32_i64(t0, t2);
        tcg_temp_free_i32(t2);
#else
        tcg_gen_rotli_i32(t0, cpu_gpr[rS(ctx->opcode)], sh);
#endif
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        mask = MASK(mb, me);
        t1 = tcg_temp_new();
        tcg_gen_andi_tl(t0, t0, mask);
        tcg_gen_andi_tl(t1, cpu_gpr[rA(ctx->opcode)], ~mask);
        tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    }
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* rlwinm & rlwinm. */
static void gen_rlwinm(DisasContext *ctx)
{
    uint32_t mb, me, sh;

    sh = SH(ctx->opcode);
    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);

    if (likely(mb == 0 && me == (31 - sh))) {
        if (likely(sh == 0)) {
            tcg_gen_ext32u_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
        } else {
            TCGv t0 = tcg_temp_new();
            tcg_gen_ext32u_tl(t0, cpu_gpr[rS(ctx->opcode)]);
            tcg_gen_shli_tl(t0, t0, sh);
            tcg_gen_ext32u_tl(cpu_gpr[rA(ctx->opcode)], t0);
            tcg_temp_free(t0);
        }
    } else if (likely(sh != 0 && me == 31 && sh == (32 - mb))) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext32u_tl(t0, cpu_gpr[rS(ctx->opcode)]);
        tcg_gen_shri_tl(t0, t0, mb);
        tcg_gen_ext32u_tl(cpu_gpr[rA(ctx->opcode)], t0);
        tcg_temp_free(t0);
    } else {
        TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(t1, cpu_gpr[rS(ctx->opcode)]);
        tcg_gen_rotli_i32(t1, t1, sh);
        tcg_gen_extu_i32_i64(t0, t1);
        tcg_temp_free_i32(t1);
#else
        tcg_gen_rotli_i32(t0, cpu_gpr[rS(ctx->opcode)], sh);
#endif
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], t0, MASK(mb, me));
        tcg_temp_free(t0);
    }
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* rlwnm & rlwnm. */
static void gen_rlwnm(DisasContext *ctx)
{
    uint32_t mb, me;
    TCGv t0;
#if defined(TARGET_PPC64)
    TCGv_i32 t1, t2;
#endif

    mb = MB(ctx->opcode);
    me = ME(ctx->opcode);
    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1f);
#if defined(TARGET_PPC64)
    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    tcg_gen_trunc_i64_i32(t1, cpu_gpr[rS(ctx->opcode)]);
    tcg_gen_trunc_i64_i32(t2, t0);
    tcg_gen_rotl_i32(t1, t1, t2);
    tcg_gen_extu_i32_i64(t0, t1);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
#else
    tcg_gen_rotl_i32(t0, cpu_gpr[rS(ctx->opcode)], t0);
#endif
    if (unlikely(mb != 0 || me != 31)) {
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], t0, MASK(mb, me));
    } else {
        tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    }
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

#if defined(TARGET_PPC64)
#define GEN_PPC64_R2(name, opc1, opc2)                                        \
static void glue(gen_, name##0)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 0);                                                       \
}                                                                             \
                                                                              \
static void glue(gen_, name##1)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 1);                                                       \
}
#define GEN_PPC64_R4(name, opc1, opc2)                                        \
static void glue(gen_, name##0)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 0, 0);                                                    \
}                                                                             \
                                                                              \
static void glue(gen_, name##1)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 0, 1);                                                    \
}                                                                             \
                                                                              \
static void glue(gen_, name##2)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 1, 0);                                                    \
}                                                                             \
                                                                              \
static void glue(gen_, name##3)(DisasContext *ctx)                            \
{                                                                             \
    gen_##name(ctx, 1, 1);                                                    \
}

static inline void gen_rldinm(DisasContext *ctx, uint32_t mb, uint32_t me,
                              uint32_t sh)
{
    if (likely(sh != 0 && mb == 0 && me == (63 - sh))) {
        tcg_gen_shli_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], sh);
    } else if (likely(sh != 0 && me == 63 && sh == (64 - mb))) {
        tcg_gen_shri_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], mb);
    } else {
        TCGv t0 = tcg_temp_new();
        tcg_gen_rotli_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
        if (likely(mb == 0 && me == 63)) {
            tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
        } else {
            tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], t0, MASK(mb, me));
        }
        tcg_temp_free(t0);
    }
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
/* rldicl - rldicl. */
static inline void gen_rldicl(DisasContext *ctx, int mbn, int shn)
{
    uint32_t sh, mb;

    sh = SH(ctx->opcode) | (shn << 5);
    mb = MB(ctx->opcode) | (mbn << 5);
    gen_rldinm(ctx, mb, 63, sh);
}
GEN_PPC64_R4(rldicl, 0x1E, 0x00);
/* rldicr - rldicr. */
static inline void gen_rldicr(DisasContext *ctx, int men, int shn)
{
    uint32_t sh, me;

    sh = SH(ctx->opcode) | (shn << 5);
    me = MB(ctx->opcode) | (men << 5);
    gen_rldinm(ctx, 0, me, sh);
}
GEN_PPC64_R4(rldicr, 0x1E, 0x02);
/* rldic - rldic. */
static inline void gen_rldic(DisasContext *ctx, int mbn, int shn)
{
    uint32_t sh, mb;

    sh = SH(ctx->opcode) | (shn << 5);
    mb = MB(ctx->opcode) | (mbn << 5);
    gen_rldinm(ctx, mb, 63 - sh, sh);
}
GEN_PPC64_R4(rldic, 0x1E, 0x04);

static inline void gen_rldnm(DisasContext *ctx, uint32_t mb, uint32_t me)
{
    TCGv t0;

    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x3f);
    tcg_gen_rotl_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    if (unlikely(mb != 0 || me != 63)) {
        tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], t0, MASK(mb, me));
    } else {
        tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    }
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* rldcl - rldcl. */
static inline void gen_rldcl(DisasContext *ctx, int mbn)
{
    uint32_t mb;

    mb = MB(ctx->opcode) | (mbn << 5);
    gen_rldnm(ctx, mb, 63);
}
GEN_PPC64_R2(rldcl, 0x1E, 0x08);
/* rldcr - rldcr. */
static inline void gen_rldcr(DisasContext *ctx, int men)
{
    uint32_t me;

    me = MB(ctx->opcode) | (men << 5);
    gen_rldnm(ctx, 0, me);
}
GEN_PPC64_R2(rldcr, 0x1E, 0x09);
/* rldimi - rldimi. */
static inline void gen_rldimi(DisasContext *ctx, int mbn, int shn)
{
    uint32_t sh, mb, me;

    sh = SH(ctx->opcode) | (shn << 5);
    mb = MB(ctx->opcode) | (mbn << 5);
    me = 63 - sh;
    if (unlikely(sh == 0 && mb == 0)) {
        tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
    } else {
        TCGv t0, t1;
        target_ulong mask;

        t0 = tcg_temp_new();
        tcg_gen_rotli_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
        t1 = tcg_temp_new();
        mask = MASK(mb, me);
        tcg_gen_andi_tl(t0, t0, mask);
        tcg_gen_andi_tl(t1, cpu_gpr[rA(ctx->opcode)], ~mask);
        tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
    }
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
GEN_PPC64_R4(rldimi, 0x1E, 0x06);
#endif

/***                             Integer shift                             ***/

/* slw & slw. */
static void gen_slw(DisasContext *ctx)
{
    TCGv t0, t1;

    t0 = tcg_temp_new();
    /* AND rS with a mask that is 0 when rB >= 0x20 */
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x3a);
    tcg_gen_sari_tl(t0, t0, 0x3f);
#else
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1a);
    tcg_gen_sari_tl(t0, t0, 0x1f);
#endif
    tcg_gen_andc_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1f);
    tcg_gen_shl_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    tcg_gen_ext32u_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sraw & sraw. */
static void gen_sraw(DisasContext *ctx)
{
    gen_helper_sraw(cpu_gpr[rA(ctx->opcode)], cpu_env,
                    cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srawi & srawi. */
static void gen_srawi(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv dst = cpu_gpr[rA(ctx->opcode)];
    TCGv src = cpu_gpr[rS(ctx->opcode)];
    if (sh == 0) {
        tcg_gen_mov_tl(dst, src);
        tcg_gen_movi_tl(cpu_ca, 0);
    } else {
        TCGv t0;
        tcg_gen_ext32s_tl(dst, src);
        tcg_gen_andi_tl(cpu_ca, dst, (1ULL << sh) - 1);
        t0 = tcg_temp_new();
        tcg_gen_sari_tl(t0, dst, TARGET_LONG_BITS - 1);
        tcg_gen_and_tl(cpu_ca, cpu_ca, t0);
        tcg_temp_free(t0);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_ca, cpu_ca, 0);
        tcg_gen_sari_tl(dst, dst, sh);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, dst);
    }
}

/* srw & srw. */
static void gen_srw(DisasContext *ctx)
{
    TCGv t0, t1;

    t0 = tcg_temp_new();
    /* AND rS with a mask that is 0 when rB >= 0x20 */
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x3a);
    tcg_gen_sari_tl(t0, t0, 0x3f);
#else
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1a);
    tcg_gen_sari_tl(t0, t0, 0x1f);
#endif
    tcg_gen_andc_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    tcg_gen_ext32u_tl(t0, t0);
    t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1f);
    tcg_gen_shr_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

#if defined(TARGET_PPC64)
/* sld & sld. */
static void gen_sld(DisasContext *ctx)
{
    TCGv t0, t1;

    t0 = tcg_temp_new();
    /* AND rS with a mask that is 0 when rB >= 0x40 */
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x39);
    tcg_gen_sari_tl(t0, t0, 0x3f);
    tcg_gen_andc_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x3f);
    tcg_gen_shl_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srad & srad. */
static void gen_srad(DisasContext *ctx)
{
    gen_helper_srad(cpu_gpr[rA(ctx->opcode)], cpu_env,
                    cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
/* sradi & sradi. */
static inline void gen_sradi(DisasContext *ctx, int n)
{
    int sh = SH(ctx->opcode) + (n << 5);
    TCGv dst = cpu_gpr[rA(ctx->opcode)];
    TCGv src = cpu_gpr[rS(ctx->opcode)];
    if (sh == 0) {
        tcg_gen_mov_tl(dst, src);
        tcg_gen_movi_tl(cpu_ca, 0);
    } else {
        TCGv t0;
        tcg_gen_andi_tl(cpu_ca, src, (1ULL << sh) - 1);
        t0 = tcg_temp_new();
        tcg_gen_sari_tl(t0, src, TARGET_LONG_BITS - 1);
        tcg_gen_and_tl(cpu_ca, cpu_ca, t0);
        tcg_temp_free(t0);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_ca, cpu_ca, 0);
        tcg_gen_sari_tl(dst, src, sh);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, dst);
    }
}

static void gen_sradi0(DisasContext *ctx)
{
    gen_sradi(ctx, 0);
}

static void gen_sradi1(DisasContext *ctx)
{
    gen_sradi(ctx, 1);
}

/* srd & srd. */
static void gen_srd(DisasContext *ctx)
{
    TCGv t0, t1;

    t0 = tcg_temp_new();
    /* AND rS with a mask that is 0 when rB >= 0x40 */
    tcg_gen_shli_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x39);
    tcg_gen_sari_tl(t0, t0, 0x3f);
    tcg_gen_andc_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x3f);
    tcg_gen_shr_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}
#endif

/***                       Floating-Point arithmetic                       ***/
#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat, set_fprf, type)           \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
    gen_reset_fpstatus();                                                     \
    gen_helper_f##op(cpu_fpr[rD(ctx->opcode)], cpu_env,                       \
                     cpu_fpr[rA(ctx->opcode)],                                \
                     cpu_fpr[rC(ctx->opcode)], cpu_fpr[rB(ctx->opcode)]);     \
    if (isfloat) {                                                            \
        gen_helper_frsp(cpu_fpr[rD(ctx->opcode)], cpu_env,                    \
                        cpu_fpr[rD(ctx->opcode)]);                            \
    }                                                                         \
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], set_fprf,                      \
                     Rc(ctx->opcode) != 0);                                   \
}

#define GEN_FLOAT_ACB(name, op2, set_fprf, type)                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0, set_fprf, type);                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1, set_fprf, type);

#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
    gen_reset_fpstatus();                                                     \
    gen_helper_f##op(cpu_fpr[rD(ctx->opcode)], cpu_env,                       \
                     cpu_fpr[rA(ctx->opcode)],                                \
                     cpu_fpr[rB(ctx->opcode)]);                               \
    if (isfloat) {                                                            \
        gen_helper_frsp(cpu_fpr[rD(ctx->opcode)], cpu_env,                    \
                        cpu_fpr[rD(ctx->opcode)]);                            \
    }                                                                         \
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)],                                \
                     set_fprf, Rc(ctx->opcode) != 0);                         \
}
#define GEN_FLOAT_AB(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0, set_fprf, type);               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1, set_fprf, type);

#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
    gen_reset_fpstatus();                                                     \
    gen_helper_f##op(cpu_fpr[rD(ctx->opcode)], cpu_env,                       \
                     cpu_fpr[rA(ctx->opcode)],                                \
                     cpu_fpr[rC(ctx->opcode)]);                               \
    if (isfloat) {                                                            \
        gen_helper_frsp(cpu_fpr[rD(ctx->opcode)], cpu_env,                    \
                        cpu_fpr[rD(ctx->opcode)]);                            \
    }                                                                         \
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)],                                \
                     set_fprf, Rc(ctx->opcode) != 0);                         \
}
#define GEN_FLOAT_AC(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0, set_fprf, type);               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1, set_fprf, type);

#define GEN_FLOAT_B(name, op2, op3, set_fprf, type)                           \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
    gen_reset_fpstatus();                                                     \
    gen_helper_f##name(cpu_fpr[rD(ctx->opcode)], cpu_env,                     \
                       cpu_fpr[rB(ctx->opcode)]);                             \
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)],                                \
                     set_fprf, Rc(ctx->opcode) != 0);                         \
}

#define GEN_FLOAT_BS(name, op1, op2, set_fprf, type)                          \
static void gen_f##name(DisasContext *ctx)                                    \
{                                                                             \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
    gen_reset_fpstatus();                                                     \
    gen_helper_f##name(cpu_fpr[rD(ctx->opcode)], cpu_env,                     \
                       cpu_fpr[rB(ctx->opcode)]);                             \
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)],                                \
                     set_fprf, Rc(ctx->opcode) != 0);                         \
}

/* fadd - fadds */
GEN_FLOAT_AB(add, 0x15, 0x000007C0, 1, PPC_FLOAT);
/* fdiv - fdivs */
GEN_FLOAT_AB(div, 0x12, 0x000007C0, 1, PPC_FLOAT);
/* fmul - fmuls */
GEN_FLOAT_AC(mul, 0x19, 0x0000F800, 1, PPC_FLOAT);

/* fre */
GEN_FLOAT_BS(re, 0x3F, 0x18, 1, PPC_FLOAT_EXT);

/* fres */
GEN_FLOAT_BS(res, 0x3B, 0x18, 1, PPC_FLOAT_FRES);

/* frsqrte */
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A, 1, PPC_FLOAT_FRSQRTE);

/* frsqrtes */
static void gen_frsqrtes(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    gen_helper_frsqrte(cpu_fpr[rD(ctx->opcode)], cpu_env,
                       cpu_fpr[rB(ctx->opcode)]);
    gen_helper_frsp(cpu_fpr[rD(ctx->opcode)], cpu_env,
                    cpu_fpr[rD(ctx->opcode)]);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 1, Rc(ctx->opcode) != 0);
}

/* fsel */
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0, 0, PPC_FLOAT_FSEL);
/* fsub - fsubs */
GEN_FLOAT_AB(sub, 0x14, 0x000007C0, 1, PPC_FLOAT);
/* Optional: */

/* fsqrt */
static void gen_fsqrt(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    gen_helper_fsqrt(cpu_fpr[rD(ctx->opcode)], cpu_env,
                     cpu_fpr[rB(ctx->opcode)]);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 1, Rc(ctx->opcode) != 0);
}

static void gen_fsqrts(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    gen_helper_fsqrt(cpu_fpr[rD(ctx->opcode)], cpu_env,
                     cpu_fpr[rB(ctx->opcode)]);
    gen_helper_frsp(cpu_fpr[rD(ctx->opcode)], cpu_env,
                    cpu_fpr[rD(ctx->opcode)]);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 1, Rc(ctx->opcode) != 0);
}

/***                     Floating-Point multiply-and-add                   ***/
/* fmadd - fmadds */
GEN_FLOAT_ACB(madd, 0x1D, 1, PPC_FLOAT);
/* fmsub - fmsubs */
GEN_FLOAT_ACB(msub, 0x1C, 1, PPC_FLOAT);
/* fnmadd - fnmadds */
GEN_FLOAT_ACB(nmadd, 0x1F, 1, PPC_FLOAT);
/* fnmsub - fnmsubs */
GEN_FLOAT_ACB(nmsub, 0x1E, 1, PPC_FLOAT);

/***                     Floating-Point round & convert                    ***/
/* fctiw */
GEN_FLOAT_B(ctiw, 0x0E, 0x00, 0, PPC_FLOAT);
/* fctiwu */
GEN_FLOAT_B(ctiwu, 0x0E, 0x04, 0, PPC2_FP_CVT_ISA206);
/* fctiwz */
GEN_FLOAT_B(ctiwz, 0x0F, 0x00, 0, PPC_FLOAT);
/* fctiwuz */
GEN_FLOAT_B(ctiwuz, 0x0F, 0x04, 0, PPC2_FP_CVT_ISA206);
/* frsp */
GEN_FLOAT_B(rsp, 0x0C, 0x00, 1, PPC_FLOAT);
#if defined(TARGET_PPC64)
/* fcfid */
GEN_FLOAT_B(cfid, 0x0E, 0x1A, 1, PPC_64B);
/* fcfids */
GEN_FLOAT_B(cfids, 0x0E, 0x1A, 0, PPC2_FP_CVT_ISA206);
/* fcfidu */
GEN_FLOAT_B(cfidu, 0x0E, 0x1E, 0, PPC2_FP_CVT_ISA206);
/* fcfidus */
GEN_FLOAT_B(cfidus, 0x0E, 0x1E, 0, PPC2_FP_CVT_ISA206);
/* fctid */
GEN_FLOAT_B(ctid, 0x0E, 0x19, 0, PPC_64B);
/* fctidu */
GEN_FLOAT_B(ctidu, 0x0E, 0x1D, 0, PPC2_FP_CVT_ISA206);
/* fctidz */
GEN_FLOAT_B(ctidz, 0x0F, 0x19, 0, PPC_64B);
/* fctidu */
GEN_FLOAT_B(ctiduz, 0x0F, 0x1D, 0, PPC2_FP_CVT_ISA206);
#endif

/* frin */
GEN_FLOAT_B(rin, 0x08, 0x0C, 1, PPC_FLOAT_EXT);
/* friz */
GEN_FLOAT_B(riz, 0x08, 0x0D, 1, PPC_FLOAT_EXT);
/* frip */
GEN_FLOAT_B(rip, 0x08, 0x0E, 1, PPC_FLOAT_EXT);
/* frim */
GEN_FLOAT_B(rim, 0x08, 0x0F, 1, PPC_FLOAT_EXT);

static void gen_ftdiv(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_helper_ftdiv(cpu_crf[crfD(ctx->opcode)], cpu_fpr[rA(ctx->opcode)],
                     cpu_fpr[rB(ctx->opcode)]);
}

static void gen_ftsqrt(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_helper_ftsqrt(cpu_crf[crfD(ctx->opcode)], cpu_fpr[rB(ctx->opcode)]);
}



/***                         Floating-Point compare                        ***/

/* fcmpo */
static void gen_fcmpo(DisasContext *ctx)
{
    TCGv_i32 crf;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    crf = tcg_const_i32(crfD(ctx->opcode));
    gen_helper_fcmpo(cpu_env, cpu_fpr[rA(ctx->opcode)],
                     cpu_fpr[rB(ctx->opcode)], crf);
    tcg_temp_free_i32(crf);
    gen_helper_float_check_status(cpu_env);
}

/* fcmpu */
static void gen_fcmpu(DisasContext *ctx)
{
    TCGv_i32 crf;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    crf = tcg_const_i32(crfD(ctx->opcode));
    gen_helper_fcmpu(cpu_env, cpu_fpr[rA(ctx->opcode)],
                     cpu_fpr[rB(ctx->opcode)], crf);
    tcg_temp_free_i32(crf);
    gen_helper_float_check_status(cpu_env);
}

/***                         Floating-point move                           ***/
/* fabs */
/* XXX: beware that fabs never checks for NaNs nor update FPSCR */
static void gen_fabs(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_andi_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rB(ctx->opcode)],
                     ~(1ULL << 63));
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

/* fmr  - fmr. */
/* XXX: beware that fmr never checks for NaNs nor update FPSCR */
static void gen_fmr(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_mov_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rB(ctx->opcode)]);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

/* fnabs */
/* XXX: beware that fnabs never checks for NaNs nor update FPSCR */
static void gen_fnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_ori_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rB(ctx->opcode)],
                    1ULL << 63);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

/* fneg */
/* XXX: beware that fneg never checks for NaNs nor update FPSCR */
static void gen_fneg(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_xori_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rB(ctx->opcode)],
                     1ULL << 63);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

/* fcpsgn: PowerPC 2.05 specification */
/* XXX: beware that fcpsgn never checks for NaNs nor update FPSCR */
static void gen_fcpsgn(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_deposit_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rA(ctx->opcode)],
                        cpu_fpr[rB(ctx->opcode)], 0, 63);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

static void gen_fmrgew(DisasContext *ctx)
{
    TCGv_i64 b0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    b0 = tcg_temp_new_i64();
    tcg_gen_shri_i64(b0, cpu_fpr[rB(ctx->opcode)], 32);
    tcg_gen_deposit_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpr[rA(ctx->opcode)],
                        b0, 0, 32);
    tcg_temp_free_i64(b0);
}

static void gen_fmrgow(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    tcg_gen_deposit_i64(cpu_fpr[rD(ctx->opcode)],
                        cpu_fpr[rB(ctx->opcode)],
                        cpu_fpr[rA(ctx->opcode)],
                        32, 32);
}

/***                  Floating-Point status & ctrl register                ***/

/* mcrfs */
static void gen_mcrfs(DisasContext *ctx)
{
    TCGv tmp = tcg_temp_new();
    int bfa;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    bfa = 4 * (7 - crfS(ctx->opcode));
    tcg_gen_shri_tl(tmp, cpu_fpscr, bfa);
    tcg_gen_trunc_tl_i32(cpu_crf[crfD(ctx->opcode)], tmp);
    tcg_temp_free(tmp);
    tcg_gen_andi_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)], 0xf);
    tcg_gen_andi_tl(cpu_fpscr, cpu_fpscr, ~(0xF << bfa));
}

/* mffs */
static void gen_mffs(DisasContext *ctx)
{
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_reset_fpstatus();
    tcg_gen_extu_tl_i64(cpu_fpr[rD(ctx->opcode)], cpu_fpscr);
    gen_compute_fprf(cpu_fpr[rD(ctx->opcode)], 0, Rc(ctx->opcode) != 0);
}

/* mtfsb0 */
static void gen_mtfsb0(DisasContext *ctx)
{
    uint8_t crb;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    crb = 31 - crbD(ctx->opcode);
    gen_reset_fpstatus();
    if (likely(crb != FPSCR_FEX && crb != FPSCR_VX)) {
        TCGv_i32 t0;
        /* NIP cannot be restored if the memory exception comes from an helper */
        gen_update_nip(ctx, ctx->nip - 4);
        t0 = tcg_const_i32(crb);
        gen_helper_fpscr_clrbit(cpu_env, t0);
        tcg_temp_free_i32(t0);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
}

/* mtfsb1 */
static void gen_mtfsb1(DisasContext *ctx)
{
    uint8_t crb;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    crb = 31 - crbD(ctx->opcode);
    gen_reset_fpstatus();
    /* XXX: we pretend we can only do IEEE floating-point computations */
    if (likely(crb != FPSCR_FEX && crb != FPSCR_VX && crb != FPSCR_NI)) {
        TCGv_i32 t0;
        /* NIP cannot be restored if the memory exception comes from an helper */
        gen_update_nip(ctx, ctx->nip - 4);
        t0 = tcg_const_i32(crb);
        gen_helper_fpscr_setbit(cpu_env, t0);
        tcg_temp_free_i32(t0);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
}

/* mtfsf */
static void gen_mtfsf(DisasContext *ctx)
{
    TCGv_i32 t0;
    int flm, l, w;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    flm = FPFLM(ctx->opcode);
    l = FPL(ctx->opcode);
    w = FPW(ctx->opcode);
    if (unlikely(w & !(ctx->insns_flags2 & PPC2_ISA205))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    if (l) {
        t0 = tcg_const_i32((ctx->insns_flags2 & PPC2_ISA205) ? 0xffff : 0xff);
    } else {
        t0 = tcg_const_i32(flm << (w * 8));
    }
    gen_helper_store_fpscr(cpu_env, cpu_fpr[rB(ctx->opcode)], t0);
    tcg_temp_free_i32(t0);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
}

/* mtfsfi */
static void gen_mtfsfi(DisasContext *ctx)
{
    int bf, sh, w;
    TCGv_i64 t0;
    TCGv_i32 t1;

    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    w = FPW(ctx->opcode);
    bf = FPBF(ctx->opcode);
    if (unlikely(w & !(ctx->insns_flags2 & PPC2_ISA205))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }
    sh = (8 * w) + 7 - bf;
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_reset_fpstatus();
    t0 = tcg_const_i64(((uint64_t)FPIMM(ctx->opcode)) << (4 * sh));
    t1 = tcg_const_i32(1 << sh);
    gen_helper_store_fpscr(cpu_env, t0, t1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        tcg_gen_trunc_tl_i32(cpu_crf[1], cpu_fpscr);
        tcg_gen_shri_i32(cpu_crf[1], cpu_crf[1], FPSCR_OX);
    }
    /* We can raise a differed exception */
    gen_helper_float_check_status(cpu_env);
}

/***                           Addressing modes                            ***/
/* Register indirect with immediate index : EA = (rA|0) + SIMM */
static inline void gen_addr_imm_index(DisasContext *ctx, TCGv EA,
                                      target_long maskl)
{
    target_long simm = SIMM(ctx->opcode);

    simm &= ~maskl;
    if (rA(ctx->opcode) == 0) {
        if (NARROW_MODE(ctx)) {
            simm = (uint32_t)simm;
        }
        tcg_gen_movi_tl(EA, simm);
    } else if (likely(simm != 0)) {
        tcg_gen_addi_tl(EA, cpu_gpr[rA(ctx->opcode)], simm);
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, EA);
        }
    } else {
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, cpu_gpr[rA(ctx->opcode)]);
        } else {
            tcg_gen_mov_tl(EA, cpu_gpr[rA(ctx->opcode)]);
        }
    }
}

static inline void gen_addr_reg_index(DisasContext *ctx, TCGv EA)
{
    if (rA(ctx->opcode) == 0) {
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, cpu_gpr[rB(ctx->opcode)]);
        } else {
            tcg_gen_mov_tl(EA, cpu_gpr[rB(ctx->opcode)]);
        }
    } else {
        tcg_gen_add_tl(EA, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, EA);
        }
    }
}

static inline void gen_addr_register(DisasContext *ctx, TCGv EA)
{
    if (rA(ctx->opcode) == 0) {
        tcg_gen_movi_tl(EA, 0);
    } else if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(EA, cpu_gpr[rA(ctx->opcode)]);
    } else {
        tcg_gen_mov_tl(EA, cpu_gpr[rA(ctx->opcode)]);
    }
}

static inline void gen_addr_add(DisasContext *ctx, TCGv ret, TCGv arg1,
                                target_long val)
{
    tcg_gen_addi_tl(ret, arg1, val);
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(ret, ret);
    }
}

static inline void gen_check_align(DisasContext *ctx, TCGv EA, int mask)
{
    int l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv_i32 t1, t2;
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    tcg_gen_andi_tl(t0, EA, mask);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    t1 = tcg_const_i32(POWERPC_EXCP_ALIGN);
    t2 = tcg_const_i32(0);
    gen_helper_raise_exception_err(cpu_env, t1, t2);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    gen_set_label(l1);
    tcg_temp_free(t0);
}

/***                             Integer load                              ***/
static inline void gen_qemu_ld8u(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld8u(arg1, arg2, ctx->mem_idx);
}

static inline void gen_qemu_ld8s(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld8s(arg1, arg2, ctx->mem_idx);
}

static inline void gen_qemu_ld16u(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld16u(arg1, arg2, ctx->mem_idx);
    if (unlikely(ctx->le_mode)) {
        tcg_gen_bswap16_tl(arg1, arg1);
    }
}

static inline void gen_qemu_ld16s(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (unlikely(ctx->le_mode)) {
        tcg_gen_qemu_ld16u(arg1, arg2, ctx->mem_idx);
        tcg_gen_bswap16_tl(arg1, arg1);
        tcg_gen_ext16s_tl(arg1, arg1);
    } else {
        tcg_gen_qemu_ld16s(arg1, arg2, ctx->mem_idx);
    }
}

static inline void gen_qemu_ld32u(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld32u(arg1, arg2, ctx->mem_idx);
    if (unlikely(ctx->le_mode)) {
        tcg_gen_bswap32_tl(arg1, arg1);
    }
}

static void gen_qemu_ld32u_i64(DisasContext *ctx, TCGv_i64 val, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    gen_qemu_ld32u(ctx, tmp, addr);
    tcg_gen_extu_tl_i64(val, tmp);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_ld32s(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (unlikely(ctx->le_mode)) {
        tcg_gen_qemu_ld32u(arg1, arg2, ctx->mem_idx);
        tcg_gen_bswap32_tl(arg1, arg1);
        tcg_gen_ext32s_tl(arg1, arg1);
    } else
        tcg_gen_qemu_ld32s(arg1, arg2, ctx->mem_idx);
}

static void gen_qemu_ld32s_i64(DisasContext *ctx, TCGv_i64 val, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    gen_qemu_ld32s(ctx, tmp, addr);
    tcg_gen_ext_tl_i64(val, tmp);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_ld64(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    tcg_gen_qemu_ld64(arg1, arg2, ctx->mem_idx);
    if (unlikely(ctx->le_mode)) {
        tcg_gen_bswap64_i64(arg1, arg1);
    }
}

static inline void gen_qemu_st8(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_st8(arg1, arg2, ctx->mem_idx);
}

static inline void gen_qemu_st16(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (unlikely(ctx->le_mode)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext16u_tl(t0, arg1);
        tcg_gen_bswap16_tl(t0, t0);
        tcg_gen_qemu_st16(t0, arg2, ctx->mem_idx);
        tcg_temp_free(t0);
    } else {
        tcg_gen_qemu_st16(arg1, arg2, ctx->mem_idx);
    }
}

static inline void gen_qemu_st32(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (unlikely(ctx->le_mode)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext32u_tl(t0, arg1);
        tcg_gen_bswap32_tl(t0, t0);
        tcg_gen_qemu_st32(t0, arg2, ctx->mem_idx);
        tcg_temp_free(t0);
    } else {
        tcg_gen_qemu_st32(arg1, arg2, ctx->mem_idx);
    }
}

static void gen_qemu_st32_i64(DisasContext *ctx, TCGv_i64 val, TCGv addr)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_trunc_i64_tl(tmp, val);
    gen_qemu_st32(ctx, tmp, addr);
    tcg_temp_free(tmp);
}

static inline void gen_qemu_st64(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    if (unlikely(ctx->le_mode)) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_bswap64_i64(t0, arg1);
        tcg_gen_qemu_st64(t0, arg2, ctx->mem_idx);
        tcg_temp_free_i64(t0);
    } else
        tcg_gen_qemu_st64(arg1, arg2, ctx->mem_idx);
}

#define GEN_LD(name, ldop, opc, type)                                         \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv EA;                                                                  \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##ldop(ctx, cpu_gpr[rD(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDU(name, ldop, opc, type)                                        \
static void glue(gen_, name##u)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(rA(ctx->opcode) == 0 ||                                      \
                 rA(ctx->opcode) == rD(ctx->opcode))) {                       \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    if (type == PPC_64B)                                                      \
        gen_addr_imm_index(ctx, EA, 0x03);                                    \
    else                                                                      \
        gen_addr_imm_index(ctx, EA, 0);                                       \
    gen_qemu_##ldop(ctx, cpu_gpr[rD(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDUX(name, ldop, opc2, opc3, type)                                \
static void glue(gen_, name##ux)(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(rA(ctx->opcode) == 0 ||                                      \
                 rA(ctx->opcode) == rD(ctx->opcode))) {                       \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, cpu_gpr[rD(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDX_E(name, ldop, opc2, opc3, type, type2)                        \
static void glue(gen_, name##x)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, cpu_gpr[rD(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}
#define GEN_LDX(name, ldop, opc2, opc3, type)                                 \
    GEN_LDX_E(name, ldop, opc2, opc3, type, PPC_NONE)

#define GEN_LDS(name, ldop, op, type)                                         \
GEN_LD(name, ldop, op | 0x20, type);                                          \
GEN_LDU(name, ldop, op | 0x21, type);                                         \
GEN_LDUX(name, ldop, 0x17, op | 0x01, type);                                  \
GEN_LDX(name, ldop, 0x17, op | 0x00, type)

/* lbz lbzu lbzux lbzx */
GEN_LDS(lbz, ld8u, 0x02, PPC_INTEGER);
/* lha lhau lhaux lhax */
GEN_LDS(lha, ld16s, 0x0A, PPC_INTEGER);
/* lhz lhzu lhzux lhzx */
GEN_LDS(lhz, ld16u, 0x08, PPC_INTEGER);
/* lwz lwzu lwzux lwzx */
GEN_LDS(lwz, ld32u, 0x00, PPC_INTEGER);
#if defined(TARGET_PPC64)
/* lwaux */
GEN_LDUX(lwa, ld32s, 0x15, 0x0B, PPC_64B);
/* lwax */
GEN_LDX(lwa, ld32s, 0x15, 0x0A, PPC_64B);
/* ldux */
GEN_LDUX(ld, ld64, 0x15, 0x01, PPC_64B);
/* ldx */
GEN_LDX(ld, ld64, 0x15, 0x00, PPC_64B);

static void gen_ld(DisasContext *ctx)
{
    TCGv EA;
    if (Rc(ctx->opcode)) {
        if (unlikely(rA(ctx->opcode) == 0 ||
                     rA(ctx->opcode) == rD(ctx->opcode))) {
            gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
            return;
        }
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_imm_index(ctx, EA, 0x03);
    if (ctx->opcode & 0x02) {
        /* lwa (lwau is undefined) */
        gen_qemu_ld32s(ctx, cpu_gpr[rD(ctx->opcode)], EA);
    } else {
        /* ld - ldu */
        gen_qemu_ld64(ctx, cpu_gpr[rD(ctx->opcode)], EA);
    }
    if (Rc(ctx->opcode))
        tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);
    tcg_temp_free(EA);
}

/* lq */
static void gen_lq(DisasContext *ctx)
{
    int ra, rd;
    TCGv EA;

    /* lq is a legal user mode instruction starting in ISA 2.07 */
    bool legal_in_user_mode = (ctx->insns_flags2 & PPC2_LSQ_ISA207) != 0;
    bool le_is_supported = (ctx->insns_flags2 & PPC2_LSQ_ISA207) != 0;

    if (!legal_in_user_mode && is_user_mode(ctx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    if (!le_is_supported && ctx->le_mode) {
        gen_exception_err(ctx, POWERPC_EXCP_ALIGN, POWERPC_EXCP_ALIGN_LE);
        return;
    }

    ra = rA(ctx->opcode);
    rd = rD(ctx->opcode);
    if (unlikely((rd & 1) || rd == ra)) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }

    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_imm_index(ctx, EA, 0x0F);

    if (unlikely(ctx->le_mode)) {
        gen_qemu_ld64(ctx, cpu_gpr[rd+1], EA);
        gen_addr_add(ctx, EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_gpr[rd], EA);
    } else {
        gen_qemu_ld64(ctx, cpu_gpr[rd], EA);
        gen_addr_add(ctx, EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_gpr[rd+1], EA);
    }
    tcg_temp_free(EA);
}
#endif

/***                              Integer store                            ***/
#define GEN_ST(name, stop, opc, type)                                         \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv EA;                                                                  \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##stop(ctx, cpu_gpr[rS(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STU(name, stop, opc, type)                                        \
static void glue(gen_, stop##u)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    if (type == PPC_64B)                                                      \
        gen_addr_imm_index(ctx, EA, 0x03);                                    \
    else                                                                      \
        gen_addr_imm_index(ctx, EA, 0);                                       \
    gen_qemu_##stop(ctx, cpu_gpr[rS(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STUX(name, stop, opc2, opc3, type)                                \
static void glue(gen_, name##ux)(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##stop(ctx, cpu_gpr[rS(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STX_E(name, stop, opc2, opc3, type, type2)                        \
static void glue(gen_, name##x)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##stop(ctx, cpu_gpr[rS(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}
#define GEN_STX(name, stop, opc2, opc3, type)                                 \
    GEN_STX_E(name, stop, opc2, opc3, type, PPC_NONE)

#define GEN_STS(name, stop, op, type)                                         \
GEN_ST(name, stop, op | 0x20, type);                                          \
GEN_STU(name, stop, op | 0x21, type);                                         \
GEN_STUX(name, stop, 0x17, op | 0x01, type);                                  \
GEN_STX(name, stop, 0x17, op | 0x00, type)

/* stb stbu stbux stbx */
GEN_STS(stb, st8, 0x06, PPC_INTEGER);
/* sth sthu sthux sthx */
GEN_STS(sth, st16, 0x0C, PPC_INTEGER);
/* stw stwu stwux stwx */
GEN_STS(stw, st32, 0x04, PPC_INTEGER);
#if defined(TARGET_PPC64)
GEN_STUX(std, st64, 0x15, 0x05, PPC_64B);
GEN_STX(std, st64, 0x15, 0x04, PPC_64B);

static void gen_std(DisasContext *ctx)
{
    int rs;
    TCGv EA;

    rs = rS(ctx->opcode);
    if ((ctx->opcode & 0x3) == 0x2) { /* stq */

        bool legal_in_user_mode = (ctx->insns_flags2 & PPC2_LSQ_ISA207) != 0;
        bool le_is_supported = (ctx->insns_flags2 & PPC2_LSQ_ISA207) != 0;

        if (!legal_in_user_mode && is_user_mode(ctx)) {
            gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
            return;
        }

        if (!le_is_supported && ctx->le_mode) {
            gen_exception_err(ctx, POWERPC_EXCP_ALIGN, POWERPC_EXCP_ALIGN_LE);
            return;
        }

        if (unlikely(rs & 1)) {
            gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
            return;
        }
        gen_set_access_type(ctx, ACCESS_INT);
        EA = tcg_temp_new();
        gen_addr_imm_index(ctx, EA, 0x03);

        if (unlikely(ctx->le_mode)) {
            gen_qemu_st64(ctx, cpu_gpr[rs+1], EA);
            gen_addr_add(ctx, EA, EA, 8);
            gen_qemu_st64(ctx, cpu_gpr[rs], EA);
        } else {
            gen_qemu_st64(ctx, cpu_gpr[rs], EA);
            gen_addr_add(ctx, EA, EA, 8);
            gen_qemu_st64(ctx, cpu_gpr[rs+1], EA);
        }
        tcg_temp_free(EA);
    } else {
        /* std / stdu*/
        if (Rc(ctx->opcode)) {
            if (unlikely(rA(ctx->opcode) == 0)) {
                gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
                return;
            }
        }
        gen_set_access_type(ctx, ACCESS_INT);
        EA = tcg_temp_new();
        gen_addr_imm_index(ctx, EA, 0x03);
        gen_qemu_st64(ctx, cpu_gpr[rs], EA);
        if (Rc(ctx->opcode))
            tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);
        tcg_temp_free(EA);
    }
}
#endif
/***                Integer load and store with byte reverse               ***/
/* lhbrx */
static inline void gen_qemu_ld16ur(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld16u(arg1, arg2, ctx->mem_idx);
    if (likely(!ctx->le_mode)) {
        tcg_gen_bswap16_tl(arg1, arg1);
    }
}
GEN_LDX(lhbr, ld16ur, 0x16, 0x18, PPC_INTEGER);

/* lwbrx */
static inline void gen_qemu_ld32ur(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld32u(arg1, arg2, ctx->mem_idx);
    if (likely(!ctx->le_mode)) {
        tcg_gen_bswap32_tl(arg1, arg1);
    }
}
GEN_LDX(lwbr, ld32ur, 0x16, 0x10, PPC_INTEGER);

#if defined(TARGET_PPC64)
/* ldbrx */
static inline void gen_qemu_ld64ur(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    tcg_gen_qemu_ld64(arg1, arg2, ctx->mem_idx);
    if (likely(!ctx->le_mode)) {
        tcg_gen_bswap64_tl(arg1, arg1);
    }
}
GEN_LDX_E(ldbr, ld64ur, 0x14, 0x10, PPC_NONE, PPC2_DBRX);
#endif  /* TARGET_PPC64 */

/* sthbrx */
static inline void gen_qemu_st16r(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (likely(!ctx->le_mode)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext16u_tl(t0, arg1);
        tcg_gen_bswap16_tl(t0, t0);
        tcg_gen_qemu_st16(t0, arg2, ctx->mem_idx);
        tcg_temp_free(t0);
    } else {
        tcg_gen_qemu_st16(arg1, arg2, ctx->mem_idx);
    }
}
GEN_STX(sthbr, st16r, 0x16, 0x1C, PPC_INTEGER);

/* stwbrx */
static inline void gen_qemu_st32r(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (likely(!ctx->le_mode)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext32u_tl(t0, arg1);
        tcg_gen_bswap32_tl(t0, t0);
        tcg_gen_qemu_st32(t0, arg2, ctx->mem_idx);
        tcg_temp_free(t0);
    } else {
        tcg_gen_qemu_st32(arg1, arg2, ctx->mem_idx);
    }
}
GEN_STX(stwbr, st32r, 0x16, 0x14, PPC_INTEGER);

#if defined(TARGET_PPC64)
/* stdbrx */
static inline void gen_qemu_st64r(DisasContext *ctx, TCGv arg1, TCGv arg2)
{
    if (likely(!ctx->le_mode)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_bswap64_tl(t0, arg1);
        tcg_gen_qemu_st64(t0, arg2, ctx->mem_idx);
        tcg_temp_free(t0);
    } else {
        tcg_gen_qemu_st64(arg1, arg2, ctx->mem_idx);
    }
}
GEN_STX_E(stdbr, st64r, 0x14, 0x14, PPC_NONE, PPC2_DBRX);
#endif  /* TARGET_PPC64 */

/***                    Integer load and store multiple                    ***/

/* lmw */
static void gen_lmw(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1;
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    t1 = tcg_const_i32(rD(ctx->opcode));
    gen_addr_imm_index(ctx, t0, 0);
    gen_helper_lmw(cpu_env, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

/* stmw */
static void gen_stmw(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1;
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    t1 = tcg_const_i32(rS(ctx->opcode));
    gen_addr_imm_index(ctx, t0, 0);
    gen_helper_stmw(cpu_env, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

/***                    Integer load and store strings                     ***/

/* lswi */
/* PowerPC32 specification says we must generate an exception if
 * rA is in the range of registers to be loaded.
 * In an other hand, IBM says this is valid, but rA won't be loaded.
 * For now, I'll follow the spec...
 */
static void gen_lswi(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1, t2;
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
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_LSWX);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    gen_addr_register(ctx, t0);
    t1 = tcg_const_i32(nb);
    t2 = tcg_const_i32(start);
    gen_helper_lsw(cpu_env, t0, t1, t2);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

/* lswx */
static void gen_lswx(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1, t2, t3;
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    t1 = tcg_const_i32(rD(ctx->opcode));
    t2 = tcg_const_i32(rA(ctx->opcode));
    t3 = tcg_const_i32(rB(ctx->opcode));
    gen_helper_lswx(cpu_env, t0, t1, t2, t3);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

/* stswi */
static void gen_stswi(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1, t2;
    int nb = NB(ctx->opcode);
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    gen_addr_register(ctx, t0);
    if (nb == 0)
        nb = 32;
    t1 = tcg_const_i32(nb);
    t2 = tcg_const_i32(rS(ctx->opcode));
    gen_helper_stsw(cpu_env, t0, t1, t2);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

/* stswx */
static void gen_stswx(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1, t2;
    gen_set_access_type(ctx, ACCESS_INT);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    t1 = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(t1, cpu_xer);
    tcg_gen_andi_i32(t1, t1, 0x7F);
    t2 = tcg_const_i32(rS(ctx->opcode));
    gen_helper_stsw(cpu_env, t0, t1, t2);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

/***                        Memory synchronisation                         ***/
/* eieio */
static void gen_eieio(DisasContext *ctx)
{
}

/* isync */
static void gen_isync(DisasContext *ctx)
{
    gen_stop_exception(ctx);
}

#define LARX(name, len, loadop)                                      \
static void gen_##name(DisasContext *ctx)                            \
{                                                                    \
    TCGv t0;                                                         \
    TCGv gpr = cpu_gpr[rD(ctx->opcode)];                             \
    gen_set_access_type(ctx, ACCESS_RES);                            \
    t0 = tcg_temp_local_new();                                       \
    gen_addr_reg_index(ctx, t0);                                     \
    if ((len) > 1) {                                                 \
        gen_check_align(ctx, t0, (len)-1);                           \
    }                                                                \
    gen_qemu_##loadop(ctx, gpr, t0);                                 \
    tcg_gen_mov_tl(cpu_reserve, t0);                                 \
    tcg_gen_st_tl(gpr, cpu_env, offsetof(CPUPPCState, reserve_val)); \
    tcg_temp_free(t0);                                               \
}

/* lwarx */
LARX(lbarx, 1, ld8u);
LARX(lharx, 2, ld16u);
LARX(lwarx, 4, ld32u);


#if defined(CONFIG_USER_ONLY)
static void gen_conditional_store(DisasContext *ctx, TCGv EA,
                                  int reg, int size)
{
    TCGv t0 = tcg_temp_new();
    uint32_t save_exception = ctx->exception;

    tcg_gen_st_tl(EA, cpu_env, offsetof(CPUPPCState, reserve_ea));
    tcg_gen_movi_tl(t0, (size << 5) | reg);
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUPPCState, reserve_info));
    tcg_temp_free(t0);
    gen_update_nip(ctx, ctx->nip-4);
    ctx->exception = POWERPC_EXCP_BRANCH;
    gen_exception(ctx, POWERPC_EXCP_STCX);
    ctx->exception = save_exception;
}
#else
static void gen_conditional_store(DisasContext *ctx, TCGv EA,
                                  int reg, int size)
{
    int l1;

    tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
    l1 = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_NE, EA, cpu_reserve, l1);
    tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], 1 << CRF_EQ);
#if defined(TARGET_PPC64)
    if (size == 8) {
        gen_qemu_st64(ctx, cpu_gpr[reg], EA);
    } else
#endif
    if (size == 4) {
        gen_qemu_st32(ctx, cpu_gpr[reg], EA);
    } else if (size == 2) {
        gen_qemu_st16(ctx, cpu_gpr[reg], EA);
#if defined(TARGET_PPC64)
    } else if (size == 16) {
        TCGv gpr1, gpr2 , EA8;
        if (unlikely(ctx->le_mode)) {
            gpr1 = cpu_gpr[reg+1];
            gpr2 = cpu_gpr[reg];
        } else {
            gpr1 = cpu_gpr[reg];
            gpr2 = cpu_gpr[reg+1];
        }
        gen_qemu_st64(ctx, gpr1, EA);
        EA8 = tcg_temp_local_new();
        gen_addr_add(ctx, EA8, EA, 8);
        gen_qemu_st64(ctx, gpr2, EA8);
        tcg_temp_free(EA8);
#endif
    } else {
        gen_qemu_st8(ctx, cpu_gpr[reg], EA);
    }
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_reserve, -1);
}
#endif

#define STCX(name, len)                                   \
static void gen_##name(DisasContext *ctx)                 \
{                                                         \
    TCGv t0;                                              \
    if (unlikely((len == 16) && (rD(ctx->opcode) & 1))) { \
        gen_inval_exception(ctx,                          \
                            POWERPC_EXCP_INVAL_INVAL);    \
        return;                                           \
    }                                                     \
    gen_set_access_type(ctx, ACCESS_RES);                 \
    t0 = tcg_temp_local_new();                            \
    gen_addr_reg_index(ctx, t0);                          \
    if (len > 1) {                                        \
        gen_check_align(ctx, t0, (len)-1);                \
    }                                                     \
    gen_conditional_store(ctx, t0, rS(ctx->opcode), len); \
    tcg_temp_free(t0);                                    \
}

STCX(stbcx_, 1);
STCX(sthcx_, 2);
STCX(stwcx_, 4);

#if defined(TARGET_PPC64)
/* ldarx */
LARX(ldarx, 8, ld64);

/* lqarx */
static void gen_lqarx(DisasContext *ctx)
{
    TCGv EA;
    int rd = rD(ctx->opcode);
    TCGv gpr1, gpr2;

    if (unlikely((rd & 1) || (rd == rA(ctx->opcode)) ||
                 (rd == rB(ctx->opcode)))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }

    gen_set_access_type(ctx, ACCESS_RES);
    EA = tcg_temp_local_new();
    gen_addr_reg_index(ctx, EA);
    gen_check_align(ctx, EA, 15);
    if (unlikely(ctx->le_mode)) {
        gpr1 = cpu_gpr[rd+1];
        gpr2 = cpu_gpr[rd];
    } else {
        gpr1 = cpu_gpr[rd];
        gpr2 = cpu_gpr[rd+1];
    }
    gen_qemu_ld64(ctx, gpr1, EA);
    tcg_gen_mov_tl(cpu_reserve, EA);

    gen_addr_add(ctx, EA, EA, 8);
    gen_qemu_ld64(ctx, gpr2, EA);

    tcg_gen_st_tl(gpr1, cpu_env, offsetof(CPUPPCState, reserve_val));
    tcg_gen_st_tl(gpr2, cpu_env, offsetof(CPUPPCState, reserve_val2));

    tcg_temp_free(EA);
}

/* stdcx. */
STCX(stdcx_, 8);
STCX(stqcx_, 16);
#endif /* defined(TARGET_PPC64) */

/* sync */
static void gen_sync(DisasContext *ctx)
{
}

/* wait */
static void gen_wait(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_st_i32(t0, cpu_env,
                   -offsetof(PowerPCCPU, env) + offsetof(CPUState, halted));
    tcg_temp_free_i32(t0);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_err(ctx, EXCP_HLT, 1);
}

/***                         Floating-point load                           ***/
#define GEN_LDF(name, ldop, opc, type)                                        \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##ldop(ctx, cpu_fpr[rD(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDUF(name, ldop, opc, type)                                       \
static void glue(gen_, name##u)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##ldop(ctx, cpu_fpr[rD(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDUXF(name, ldop, opc, type)                                      \
static void glue(gen_, name##ux)(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, cpu_fpr[rD(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDXF(name, ldop, opc2, opc3, type)                                \
static void glue(gen_, name##x)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, cpu_fpr[rD(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDFS(name, ldop, op, type)                                        \
GEN_LDF(name, ldop, op | 0x20, type);                                         \
GEN_LDUF(name, ldop, op | 0x21, type);                                        \
GEN_LDUXF(name, ldop, op | 0x01, type);                                       \
GEN_LDXF(name, ldop, 0x17, op | 0x00, type)

static inline void gen_qemu_ld32fs(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 t1 = tcg_temp_new_i32();
    gen_qemu_ld32u(ctx, t0, arg2);
    tcg_gen_trunc_tl_i32(t1, t0);
    tcg_temp_free(t0);
    gen_helper_float32_to_float64(arg1, cpu_env, t1);
    tcg_temp_free_i32(t1);
}

 /* lfd lfdu lfdux lfdx */
GEN_LDFS(lfd, ld64, 0x12, PPC_FLOAT);
 /* lfs lfsu lfsux lfsx */
GEN_LDFS(lfs, ld32fs, 0x10, PPC_FLOAT);

/* lfdp */
static void gen_lfdp(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_imm_index(ctx, EA, 0);                                           \
    if (unlikely(ctx->le_mode)) {
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
    } else {
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
    }
    tcg_temp_free(EA);
}

/* lfdpx */
static void gen_lfdpx(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    if (unlikely(ctx->le_mode)) {
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
    } else {
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_ld64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
    }
    tcg_temp_free(EA);
}

/* lfiwax */
static void gen_lfiwax(DisasContext *ctx)
{
    TCGv EA;
    TCGv t0;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld32s(ctx, t0, EA);
    tcg_gen_ext_tl_i64(cpu_fpr[rD(ctx->opcode)], t0);
    tcg_temp_free(EA);
    tcg_temp_free(t0);
}

/* lfiwzx */
static void gen_lfiwzx(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld32u_i64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
    tcg_temp_free(EA);
}
/***                         Floating-point store                          ***/
#define GEN_STF(name, stop, opc, type)                                        \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##stop(ctx, cpu_fpr[rS(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STUF(name, stop, opc, type)                                       \
static void glue(gen_, name##u)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_imm_index(ctx, EA, 0);                                           \
    gen_qemu_##stop(ctx, cpu_fpr[rS(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STUXF(name, stop, opc, type)                                      \
static void glue(gen_, name##ux)(DisasContext *ctx)                                   \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    if (unlikely(rA(ctx->opcode) == 0)) {                                     \
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);                   \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##stop(ctx, cpu_fpr[rS(ctx->opcode)], EA);                       \
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], EA);                             \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STXF(name, stop, opc2, opc3, type)                                \
static void glue(gen_, name##x)(DisasContext *ctx)                                    \
{                                                                             \
    TCGv EA;                                                                  \
    if (unlikely(!ctx->fpu_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_FPU);                                 \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_FLOAT);                                   \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##stop(ctx, cpu_fpr[rS(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_STFS(name, stop, op, type)                                        \
GEN_STF(name, stop, op | 0x20, type);                                         \
GEN_STUF(name, stop, op | 0x21, type);                                        \
GEN_STUXF(name, stop, op | 0x01, type);                                       \
GEN_STXF(name, stop, 0x17, op | 0x00, type)

static inline void gen_qemu_st32fs(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv t1 = tcg_temp_new();
    gen_helper_float64_to_float32(t0, cpu_env, arg1);
    tcg_gen_extu_i32_tl(t1, t0);
    tcg_temp_free_i32(t0);
    gen_qemu_st32(ctx, t1, arg2);
    tcg_temp_free(t1);
}

/* stfd stfdu stfdux stfdx */
GEN_STFS(stfd, st64, 0x16, PPC_FLOAT);
/* stfs stfsu stfsux stfsx */
GEN_STFS(stfs, st32fs, 0x14, PPC_FLOAT);

/* stfdp */
static void gen_stfdp(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_imm_index(ctx, EA, 0);                                           \
    if (unlikely(ctx->le_mode)) {
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
    } else {
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
    }
    tcg_temp_free(EA);
}

/* stfdpx */
static void gen_stfdpx(DisasContext *ctx)
{
    TCGv EA;
    if (unlikely(!ctx->fpu_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_FPU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_FLOAT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    if (unlikely(ctx->le_mode)) {
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
    } else {
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode)], EA);
        tcg_gen_addi_tl(EA, EA, 8);
        gen_qemu_st64(ctx, cpu_fpr[rD(ctx->opcode) + 1], EA);
    }
    tcg_temp_free(EA);
}

/* Optional: */
static inline void gen_qemu_st32fiw(DisasContext *ctx, TCGv_i64 arg1, TCGv arg2)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_trunc_i64_tl(t0, arg1),
    gen_qemu_st32(ctx, t0, arg2);
    tcg_temp_free(t0);
}
/* stfiwx */
GEN_STXF(stfiw, st32fiw, 0x17, 0x1E, PPC_FLOAT_STFIWX);

static inline void gen_update_cfar(DisasContext *ctx, target_ulong nip)
{
#if defined(TARGET_PPC64)
    if (ctx->has_cfar)
        tcg_gen_movi_tl(cpu_cfar, nip);
#endif
}

/***                                Branch                                 ***/
static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if (NARROW_MODE(ctx)) {
        dest = (uint32_t) dest;
    }
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) &&
        likely(!ctx->singlestep_enabled)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_nip, dest & ~3);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        tcg_gen_movi_tl(cpu_nip, dest & ~3);
        if (unlikely(ctx->singlestep_enabled)) {
            if ((ctx->singlestep_enabled &
                (CPU_BRANCH_STEP | CPU_SINGLE_STEP)) &&
                (ctx->exception == POWERPC_EXCP_BRANCH ||
                 ctx->exception == POWERPC_EXCP_TRACE)) {
                target_ulong tmp = ctx->nip;
                ctx->nip = dest;
                gen_exception(ctx, POWERPC_EXCP_TRACE);
                ctx->nip = tmp;
            }
            if (ctx->singlestep_enabled & GDBSTUB_SINGLE_STEP) {
                gen_debug_exception(ctx);
            }
        }
        tcg_gen_exit_tb(0);
    }
}

static inline void gen_setlr(DisasContext *ctx, target_ulong nip)
{
    if (NARROW_MODE(ctx)) {
        nip = (uint32_t)nip;
    }
    tcg_gen_movi_tl(cpu_lr, nip);
}

/* b ba bl bla */
static void gen_b(DisasContext *ctx)
{
    target_ulong li, target;

    ctx->exception = POWERPC_EXCP_BRANCH;
    /* sign extend LI */
    li = LI(ctx->opcode);
    li = (li ^ 0x02000000) - 0x02000000;
    if (likely(AA(ctx->opcode) == 0)) {
        target = ctx->nip + li - 4;
    } else {
        target = li;
    }
    if (LK(ctx->opcode)) {
        gen_setlr(ctx, ctx->nip);
    }
    gen_update_cfar(ctx, ctx->nip);
    gen_goto_tb(ctx, 0, target);
}

#define BCOND_IM  0
#define BCOND_LR  1
#define BCOND_CTR 2
#define BCOND_TAR 3

static inline void gen_bcond(DisasContext *ctx, int type)
{
    uint32_t bo = BO(ctx->opcode);
    int l1;
    TCGv target;

    ctx->exception = POWERPC_EXCP_BRANCH;
    if (type == BCOND_LR || type == BCOND_CTR || type == BCOND_TAR) {
        target = tcg_temp_local_new();
        if (type == BCOND_CTR)
            tcg_gen_mov_tl(target, cpu_ctr);
        else if (type == BCOND_TAR)
            gen_load_spr(target, SPR_TAR);
        else
            tcg_gen_mov_tl(target, cpu_lr);
    } else {
        TCGV_UNUSED(target);
    }
    if (LK(ctx->opcode))
        gen_setlr(ctx, ctx->nip);
    l1 = gen_new_label();
    if ((bo & 0x4) == 0) {
        /* Decrement and test CTR */
        TCGv temp = tcg_temp_new();
        if (unlikely(type == BCOND_CTR)) {
            gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
            return;
        }
        tcg_gen_subi_tl(cpu_ctr, cpu_ctr, 1);
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(temp, cpu_ctr);
        } else {
            tcg_gen_mov_tl(temp, cpu_ctr);
        }
        if (bo & 0x2) {
            tcg_gen_brcondi_tl(TCG_COND_NE, temp, 0, l1);
        } else {
            tcg_gen_brcondi_tl(TCG_COND_EQ, temp, 0, l1);
        }
        tcg_temp_free(temp);
    }
    if ((bo & 0x10) == 0) {
        /* Test CR */
        uint32_t bi = BI(ctx->opcode);
        uint32_t mask = 1 << (3 - (bi & 0x03));
        TCGv_i32 temp = tcg_temp_new_i32();

        if (bo & 0x8) {
            tcg_gen_andi_i32(temp, cpu_crf[bi >> 2], mask);
            tcg_gen_brcondi_i32(TCG_COND_EQ, temp, 0, l1);
        } else {
            tcg_gen_andi_i32(temp, cpu_crf[bi >> 2], mask);
            tcg_gen_brcondi_i32(TCG_COND_NE, temp, 0, l1);
        }
        tcg_temp_free_i32(temp);
    }
    gen_update_cfar(ctx, ctx->nip);
    if (type == BCOND_IM) {
        target_ulong li = (target_long)((int16_t)(BD(ctx->opcode)));
        if (likely(AA(ctx->opcode) == 0)) {
            gen_goto_tb(ctx, 0, ctx->nip + li - 4);
        } else {
            gen_goto_tb(ctx, 0, li);
        }
        gen_set_label(l1);
        gen_goto_tb(ctx, 1, ctx->nip);
    } else {
        if (NARROW_MODE(ctx)) {
            tcg_gen_andi_tl(cpu_nip, target, (uint32_t)~3);
        } else {
            tcg_gen_andi_tl(cpu_nip, target, ~3);
        }
        tcg_gen_exit_tb(0);
        gen_set_label(l1);
        gen_update_nip(ctx, ctx->nip);
        tcg_gen_exit_tb(0);
    }
}

static void gen_bc(DisasContext *ctx)
{
    gen_bcond(ctx, BCOND_IM);
}

static void gen_bcctr(DisasContext *ctx)
{
    gen_bcond(ctx, BCOND_CTR);
}

static void gen_bclr(DisasContext *ctx)
{
    gen_bcond(ctx, BCOND_LR);
}

static void gen_bctar(DisasContext *ctx)
{
    gen_bcond(ctx, BCOND_TAR);
}

/***                      Condition register logical                       ***/
#define GEN_CRLOGIC(name, tcg_op, opc)                                        \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    uint8_t bitmask;                                                          \
    int sh;                                                                   \
    TCGv_i32 t0, t1;                                                          \
    sh = (crbD(ctx->opcode) & 0x03) - (crbA(ctx->opcode) & 0x03);             \
    t0 = tcg_temp_new_i32();                                                  \
    if (sh > 0)                                                               \
        tcg_gen_shri_i32(t0, cpu_crf[crbA(ctx->opcode) >> 2], sh);            \
    else if (sh < 0)                                                          \
        tcg_gen_shli_i32(t0, cpu_crf[crbA(ctx->opcode) >> 2], -sh);           \
    else                                                                      \
        tcg_gen_mov_i32(t0, cpu_crf[crbA(ctx->opcode) >> 2]);                 \
    t1 = tcg_temp_new_i32();                                                  \
    sh = (crbD(ctx->opcode) & 0x03) - (crbB(ctx->opcode) & 0x03);             \
    if (sh > 0)                                                               \
        tcg_gen_shri_i32(t1, cpu_crf[crbB(ctx->opcode) >> 2], sh);            \
    else if (sh < 0)                                                          \
        tcg_gen_shli_i32(t1, cpu_crf[crbB(ctx->opcode) >> 2], -sh);           \
    else                                                                      \
        tcg_gen_mov_i32(t1, cpu_crf[crbB(ctx->opcode) >> 2]);                 \
    tcg_op(t0, t0, t1);                                                       \
    bitmask = 1 << (3 - (crbD(ctx->opcode) & 0x03));                          \
    tcg_gen_andi_i32(t0, t0, bitmask);                                        \
    tcg_gen_andi_i32(t1, cpu_crf[crbD(ctx->opcode) >> 2], ~bitmask);          \
    tcg_gen_or_i32(cpu_crf[crbD(ctx->opcode) >> 2], t0, t1);                  \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}

/* crand */
GEN_CRLOGIC(crand, tcg_gen_and_i32, 0x08);
/* crandc */
GEN_CRLOGIC(crandc, tcg_gen_andc_i32, 0x04);
/* creqv */
GEN_CRLOGIC(creqv, tcg_gen_eqv_i32, 0x09);
/* crnand */
GEN_CRLOGIC(crnand, tcg_gen_nand_i32, 0x07);
/* crnor */
GEN_CRLOGIC(crnor, tcg_gen_nor_i32, 0x01);
/* cror */
GEN_CRLOGIC(cror, tcg_gen_or_i32, 0x0E);
/* crorc */
GEN_CRLOGIC(crorc, tcg_gen_orc_i32, 0x0D);
/* crxor */
GEN_CRLOGIC(crxor, tcg_gen_xor_i32, 0x06);

/* mcrf */
static void gen_mcrf(DisasContext *ctx)
{
    tcg_gen_mov_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfS(ctx->opcode)]);
}

/***                           System linkage                              ***/

/* rfi (mem_idx only) */
static void gen_rfi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    /* Restore CPU state */
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_update_cfar(ctx, ctx->nip);
    gen_helper_rfi(cpu_env);
    gen_sync_exception(ctx);
#endif
}

#if defined(TARGET_PPC64)
static void gen_rfid(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    /* Restore CPU state */
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_update_cfar(ctx, ctx->nip);
    gen_helper_rfid(cpu_env);
    gen_sync_exception(ctx);
#endif
}

static void gen_hrfid(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    /* Restore CPU state */
    if (unlikely(ctx->mem_idx <= 1)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_hrfid(cpu_env);
    gen_sync_exception(ctx);
#endif
}
#endif

/* sc */
#if defined(CONFIG_USER_ONLY)
#define POWERPC_SYSCALL POWERPC_EXCP_SYSCALL_USER
#else
#define POWERPC_SYSCALL POWERPC_EXCP_SYSCALL
#endif
static void gen_sc(DisasContext *ctx)
{
    uint32_t lev;

    lev = (ctx->opcode >> 5) & 0x7F;
    gen_exception_err(ctx, POWERPC_SYSCALL, lev);
}

/***                                Trap                                   ***/

/* tw */
static void gen_tw(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_const_i32(TO(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_helper_tw(cpu_env, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                  t0);
    tcg_temp_free_i32(t0);
}

/* twi */
static void gen_twi(DisasContext *ctx)
{
    TCGv t0 = tcg_const_tl(SIMM(ctx->opcode));
    TCGv_i32 t1 = tcg_const_i32(TO(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_helper_tw(cpu_env, cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

#if defined(TARGET_PPC64)
/* td */
static void gen_td(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_const_i32(TO(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_helper_td(cpu_env, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                  t0);
    tcg_temp_free_i32(t0);
}

/* tdi */
static void gen_tdi(DisasContext *ctx)
{
    TCGv t0 = tcg_const_tl(SIMM(ctx->opcode));
    TCGv_i32 t1 = tcg_const_i32(TO(ctx->opcode));
    /* Update the nip since this might generate a trap exception */
    gen_update_nip(ctx, ctx->nip);
    gen_helper_td(cpu_env, cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}
#endif

/***                          Processor control                            ***/

static void gen_read_xer(TCGv dst)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    tcg_gen_mov_tl(dst, cpu_xer);
    tcg_gen_shli_tl(t0, cpu_so, XER_SO);
    tcg_gen_shli_tl(t1, cpu_ov, XER_OV);
    tcg_gen_shli_tl(t2, cpu_ca, XER_CA);
    tcg_gen_or_tl(t0, t0, t1);
    tcg_gen_or_tl(dst, dst, t2);
    tcg_gen_or_tl(dst, dst, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static void gen_write_xer(TCGv src)
{
    tcg_gen_andi_tl(cpu_xer, src,
                    ~((1u << XER_SO) | (1u << XER_OV) | (1u << XER_CA)));
    tcg_gen_shri_tl(cpu_so, src, XER_SO);
    tcg_gen_shri_tl(cpu_ov, src, XER_OV);
    tcg_gen_shri_tl(cpu_ca, src, XER_CA);
    tcg_gen_andi_tl(cpu_so, cpu_so, 1);
    tcg_gen_andi_tl(cpu_ov, cpu_ov, 1);
    tcg_gen_andi_tl(cpu_ca, cpu_ca, 1);
}

/* mcrxr */
static void gen_mcrxr(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 dst = cpu_crf[crfD(ctx->opcode)];

    tcg_gen_trunc_tl_i32(t0, cpu_so);
    tcg_gen_trunc_tl_i32(t1, cpu_ov);
    tcg_gen_trunc_tl_i32(dst, cpu_ca);
    tcg_gen_shri_i32(t0, t0, 2);
    tcg_gen_shri_i32(t1, t1, 1);
    tcg_gen_or_i32(dst, dst, t0);
    tcg_gen_or_i32(dst, dst, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);

    tcg_gen_movi_tl(cpu_so, 0);
    tcg_gen_movi_tl(cpu_ov, 0);
    tcg_gen_movi_tl(cpu_ca, 0);
}

/* mfcr mfocrf */
static void gen_mfcr(DisasContext *ctx)
{
    uint32_t crm, crn;

    if (likely(ctx->opcode & 0x00100000)) {
        crm = CRM(ctx->opcode);
        if (likely(crm && ((crm & (crm - 1)) == 0))) {
            crn = ctz32 (crm);
            tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], cpu_crf[7 - crn]);
            tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)],
                            cpu_gpr[rD(ctx->opcode)], crn * 4);
        }
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
        tcg_gen_mov_i32(t0, cpu_crf[0]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[1]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[2]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[3]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[4]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[5]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[6]);
        tcg_gen_shli_i32(t0, t0, 4);
        tcg_gen_or_i32(t0, t0, cpu_crf[7]);
        tcg_gen_extu_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);
        tcg_temp_free_i32(t0);
    }
}

/* mfmsr */
static void gen_mfmsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_msr);
#endif
}

static void spr_noaccess(void *opaque, int gprn, int sprn)
{
#if 0
    sprn = ((sprn >> 5) & 0x1F) | ((sprn & 0x1F) << 5);
    printf("ERROR: try to access SPR %d !\n", sprn);
#endif
}
#define SPR_NOACCESS (&spr_noaccess)

/* mfspr */
static inline void gen_op_mfspr(DisasContext *ctx)
{
    void (*read_cb)(void *opaque, int gprn, int sprn);
    uint32_t sprn = SPR(ctx->opcode);

#if !defined(CONFIG_USER_ONLY)
    if (ctx->mem_idx == 2)
        read_cb = ctx->spr_cb[sprn].hea_read;
    else if (ctx->mem_idx)
        read_cb = ctx->spr_cb[sprn].oea_read;
    else
#endif
        read_cb = ctx->spr_cb[sprn].uea_read;
    if (likely(read_cb != NULL)) {
        if (likely(read_cb != SPR_NOACCESS)) {
            (*read_cb)(ctx, rD(ctx->opcode), sprn);
        } else {
            /* Privilege exception */
            /* This is a hack to avoid warnings when running Linux:
             * this OS breaks the PowerPC virtualisation model,
             * allowing userland application to read the PVR
             */
            if (sprn != SPR_PVR) {
                qemu_log("Trying to read privileged spr %d (0x%03x) at "
                         TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
                printf("Trying to read privileged spr %d (0x%03x) at "
                       TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
            }
            gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        }
    } else {
        /* Not defined */
        qemu_log("Trying to read invalid spr %d (0x%03x) at "
                 TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
        printf("Trying to read invalid spr %d (0x%03x) at "
               TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_SPR);
    }
}

static void gen_mfspr(DisasContext *ctx)
{
    gen_op_mfspr(ctx);
}

/* mftb */
static void gen_mftb(DisasContext *ctx)
{
    gen_op_mfspr(ctx);
}

/* mtcrf mtocrf*/
static void gen_mtcrf(DisasContext *ctx)
{
    uint32_t crm, crn;

    crm = CRM(ctx->opcode);
    if (likely((ctx->opcode & 0x00100000))) {
        if (crm && ((crm & (crm - 1)) == 0)) {
            TCGv_i32 temp = tcg_temp_new_i32();
            crn = ctz32 (crm);
            tcg_gen_trunc_tl_i32(temp, cpu_gpr[rS(ctx->opcode)]);
            tcg_gen_shri_i32(temp, temp, crn * 4);
            tcg_gen_andi_i32(cpu_crf[7 - crn], temp, 0xf);
            tcg_temp_free_i32(temp);
        }
    } else {
        TCGv_i32 temp = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(temp, cpu_gpr[rS(ctx->opcode)]);
        for (crn = 0 ; crn < 8 ; crn++) {
            if (crm & (1 << crn)) {
                    tcg_gen_shri_i32(cpu_crf[7 - crn], temp, crn * 4);
                    tcg_gen_andi_i32(cpu_crf[7 - crn], cpu_crf[7 - crn], 0xf);
            }
        }
        tcg_temp_free_i32(temp);
    }
}

/* mtmsr */
#if defined(TARGET_PPC64)
static void gen_mtmsrd(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    if (ctx->opcode & 0x00010000) {
        /* Special form that does not need any synchronisation */
        TCGv t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_gpr[rS(ctx->opcode)], (1 << MSR_RI) | (1 << MSR_EE));
        tcg_gen_andi_tl(cpu_msr, cpu_msr, ~((1 << MSR_RI) | (1 << MSR_EE)));
        tcg_gen_or_tl(cpu_msr, cpu_msr, t0);
        tcg_temp_free(t0);
    } else {
        /* XXX: we need to update nip before the store
         *      if we enter power saving mode, we will exit the loop
         *      directly from ppc_store_msr
         */
        gen_update_nip(ctx, ctx->nip);
        gen_helper_store_msr(cpu_env, cpu_gpr[rS(ctx->opcode)]);
        /* Must stop the translation as machine state (may have) changed */
        /* Note that mtmsr is not always defined as context-synchronizing */
        gen_stop_exception(ctx);
    }
#endif
}
#endif

static void gen_mtmsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    if (ctx->opcode & 0x00010000) {
        /* Special form that does not need any synchronisation */
        TCGv t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_gpr[rS(ctx->opcode)], (1 << MSR_RI) | (1 << MSR_EE));
        tcg_gen_andi_tl(cpu_msr, cpu_msr, ~((1 << MSR_RI) | (1 << MSR_EE)));
        tcg_gen_or_tl(cpu_msr, cpu_msr, t0);
        tcg_temp_free(t0);
    } else {
        TCGv msr = tcg_temp_new();

        /* XXX: we need to update nip before the store
         *      if we enter power saving mode, we will exit the loop
         *      directly from ppc_store_msr
         */
        gen_update_nip(ctx, ctx->nip);
#if defined(TARGET_PPC64)
        tcg_gen_deposit_tl(msr, cpu_msr, cpu_gpr[rS(ctx->opcode)], 0, 32);
#else
        tcg_gen_mov_tl(msr, cpu_gpr[rS(ctx->opcode)]);
#endif
        gen_helper_store_msr(cpu_env, msr);
        /* Must stop the translation as machine state (may have) changed */
        /* Note that mtmsr is not always defined as context-synchronizing */
        gen_stop_exception(ctx);
    }
#endif
}

/* mtspr */
static void gen_mtspr(DisasContext *ctx)
{
    void (*write_cb)(void *opaque, int sprn, int gprn);
    uint32_t sprn = SPR(ctx->opcode);

#if !defined(CONFIG_USER_ONLY)
    if (ctx->mem_idx == 2)
        write_cb = ctx->spr_cb[sprn].hea_write;
    else if (ctx->mem_idx)
        write_cb = ctx->spr_cb[sprn].oea_write;
    else
#endif
        write_cb = ctx->spr_cb[sprn].uea_write;
    if (likely(write_cb != NULL)) {
        if (likely(write_cb != SPR_NOACCESS)) {
            (*write_cb)(ctx, sprn, rS(ctx->opcode));
        } else {
            /* Privilege exception */
            qemu_log("Trying to write privileged spr %d (0x%03x) at "
                     TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
            printf("Trying to write privileged spr %d (0x%03x) at "
                   TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
            gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        }
    } else {
        /* Not defined */
        qemu_log("Trying to write invalid spr %d (0x%03x) at "
                 TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
        printf("Trying to write invalid spr %d (0x%03x) at "
               TARGET_FMT_lx "\n", sprn, sprn, ctx->nip - 4);
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_SPR);
    }
}

/***                         Cache management                              ***/

/* dcbf */
static void gen_dcbf(DisasContext *ctx)
{
    /* XXX: specification says this is treated as a load by the MMU */
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_CACHE);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld8u(ctx, t0, t0);
    tcg_temp_free(t0);
}

/* dcbi (Supervisor only) */
static void gen_dcbi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv EA, val;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    EA = tcg_temp_new();
    gen_set_access_type(ctx, ACCESS_CACHE);
    gen_addr_reg_index(ctx, EA);
    val = tcg_temp_new();
    /* XXX: specification says this should be treated as a store by the MMU */
    gen_qemu_ld8u(ctx, val, EA);
    gen_qemu_st8(ctx, val, EA);
    tcg_temp_free(val);
    tcg_temp_free(EA);
#endif
}

/* dcdst */
static void gen_dcbst(DisasContext *ctx)
{
    /* XXX: specification say this is treated as a load by the MMU */
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_CACHE);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld8u(ctx, t0, t0);
    tcg_temp_free(t0);
}

/* dcbt */
static void gen_dcbt(DisasContext *ctx)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* dcbtst */
static void gen_dcbtst(DisasContext *ctx)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* dcbz */
static void gen_dcbz(DisasContext *ctx)
{
    TCGv tcgv_addr;
    TCGv_i32 tcgv_is_dcbzl;
    int is_dcbzl = ctx->opcode & 0x00200000 ? 1 : 0;

    gen_set_access_type(ctx, ACCESS_CACHE);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    tcgv_addr = tcg_temp_new();
    tcgv_is_dcbzl = tcg_const_i32(is_dcbzl);

    gen_addr_reg_index(ctx, tcgv_addr);
    gen_helper_dcbz(cpu_env, tcgv_addr, tcgv_is_dcbzl);

    tcg_temp_free(tcgv_addr);
    tcg_temp_free_i32(tcgv_is_dcbzl);
}

/* dst / dstt */
static void gen_dst(DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_LSWX);
    } else {
        /* interpreted as no-op */
    }
}

/* dstst /dststt */
static void gen_dstst(DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_LSWX);
    } else {
        /* interpreted as no-op */
    }

}

/* dss / dssall */
static void gen_dss(DisasContext *ctx)
{
    /* interpreted as no-op */
}

/* icbi */
static void gen_icbi(DisasContext *ctx)
{
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_CACHE);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_icbi(cpu_env, t0);
    tcg_temp_free(t0);
}

/* Optional: */
/* dcba */
static void gen_dcba(DisasContext *ctx)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a store by the MMU
     *      but does not generate any exception
     */
}

/***                    Segment register manipulation                      ***/
/* Supervisor only: */

/* mfsr */
static void gen_mfsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif
}

/* mfsrin */
static void gen_mfsrin(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 28);
    tcg_gen_andi_tl(t0, t0, 0xF);
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif
}

/* mtsr */
static void gen_mtsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif
}

/* mtsrin */
static void gen_mtsrin(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 28);
    tcg_gen_andi_tl(t0, t0, 0xF);
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rD(ctx->opcode)]);
    tcg_temp_free(t0);
#endif
}

#if defined(TARGET_PPC64)
/* Specific implementation for PowerPC 64 "bridge" emulation using SLB */

/* mfsr */
static void gen_mfsr_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif
}

/* mfsrin */
static void gen_mfsrin_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 28);
    tcg_gen_andi_tl(t0, t0, 0xF);
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif
}

/* mtsr */
static void gen_mtsr_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif
}

/* mtsrin */
static void gen_mtsrin_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 28);
    tcg_gen_andi_tl(t0, t0, 0xF);
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif
}

/* slbmte */
static void gen_slbmte(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    gen_helper_store_slb(cpu_env, cpu_gpr[rB(ctx->opcode)],
                         cpu_gpr[rS(ctx->opcode)]);
#endif
}

static void gen_slbmfee(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    gen_helper_load_slb_esid(cpu_gpr[rS(ctx->opcode)], cpu_env,
                             cpu_gpr[rB(ctx->opcode)]);
#endif
}

static void gen_slbmfev(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    gen_helper_load_slb_vsid(cpu_gpr[rS(ctx->opcode)], cpu_env,
                             cpu_gpr[rB(ctx->opcode)]);
#endif
}
#endif /* defined(TARGET_PPC64) */

/***                      Lookaside buffer management                      ***/
/* Optional & mem_idx only: */

/* tlbia */
static void gen_tlbia(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_tlbia(cpu_env);
#endif
}

/* tlbiel */
static void gen_tlbiel(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_tlbie(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

/* tlbie */
static void gen_tlbie(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    if (NARROW_MODE(ctx)) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ext32u_tl(t0, cpu_gpr[rB(ctx->opcode)]);
        gen_helper_tlbie(cpu_env, t0);
        tcg_temp_free(t0);
    } else {
        gen_helper_tlbie(cpu_env, cpu_gpr[rB(ctx->opcode)]);
    }
#endif
}

/* tlbsync */
static void gen_tlbsync(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* This has no effect: it should ensure that all previous
     * tlbie have completed
     */
    gen_stop_exception(ctx);
#endif
}

#if defined(TARGET_PPC64)
/* slbia */
static void gen_slbia(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_slbia(cpu_env);
#endif
}

/* slbie */
static void gen_slbie(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_slbie(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}
#endif

/***                              External control                         ***/
/* Optional: */

/* eciwx */
static void gen_eciwx(DisasContext *ctx)
{
    TCGv t0;
    /* Should check EAR[E] ! */
    gen_set_access_type(ctx, ACCESS_EXT);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_check_align(ctx, t0, 0x03);
    gen_qemu_ld32u(ctx, cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

/* ecowx */
static void gen_ecowx(DisasContext *ctx)
{
    TCGv t0;
    /* Should check EAR[E] ! */
    gen_set_access_type(ctx, ACCESS_EXT);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_check_align(ctx, t0, 0x03);
    gen_qemu_st32(ctx, cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
}

/* PowerPC 601 specific instructions */

/* abs - abs. */
static void gen_abs(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_GE, cpu_gpr[rA(ctx->opcode)], 0, l1);
    tcg_gen_neg_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    gen_set_label(l2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* abso - abso. */
static void gen_abso(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    int l3 = gen_new_label();
    /* Start with XER OV disabled, the most likely case */
    tcg_gen_movi_tl(cpu_ov, 0);
    tcg_gen_brcondi_tl(TCG_COND_GE, cpu_gpr[rA(ctx->opcode)], 0, l2);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_gpr[rA(ctx->opcode)], 0x80000000, l1);
    tcg_gen_movi_tl(cpu_ov, 1);
    tcg_gen_movi_tl(cpu_so, 1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_neg_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l3);
    gen_set_label(l2);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    gen_set_label(l3);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* clcs */
static void gen_clcs(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_const_i32(rA(ctx->opcode));
    gen_helper_clcs(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free_i32(t0);
    /* Rc=1 sets CR0 to an undefined state */
}

/* div - div. */
static void gen_div(DisasContext *ctx)
{
    gen_helper_div(cpu_gpr[rD(ctx->opcode)], cpu_env, cpu_gpr[rA(ctx->opcode)],
                   cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* divo - divo. */
static void gen_divo(DisasContext *ctx)
{
    gen_helper_divo(cpu_gpr[rD(ctx->opcode)], cpu_env, cpu_gpr[rA(ctx->opcode)],
                    cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* divs - divs. */
static void gen_divs(DisasContext *ctx)
{
    gen_helper_divs(cpu_gpr[rD(ctx->opcode)], cpu_env, cpu_gpr[rA(ctx->opcode)],
                    cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* divso - divso. */
static void gen_divso(DisasContext *ctx)
{
    gen_helper_divso(cpu_gpr[rD(ctx->opcode)], cpu_env,
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* doz - doz. */
static void gen_doz(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    tcg_gen_brcond_tl(TCG_COND_GE, cpu_gpr[rB(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], l1);
    tcg_gen_sub_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], 0);
    gen_set_label(l2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* dozo - dozo. */
static void gen_dozo(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    /* Start with XER OV disabled, the most likely case */
    tcg_gen_movi_tl(cpu_ov, 0);
    tcg_gen_brcond_tl(TCG_COND_GE, cpu_gpr[rB(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], l1);
    tcg_gen_sub_tl(t0, cpu_gpr[rB(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_xor_tl(t1, cpu_gpr[rB(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_xor_tl(t2, cpu_gpr[rA(ctx->opcode)], t0);
    tcg_gen_andc_tl(t1, t1, t2);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l2);
    tcg_gen_movi_tl(cpu_ov, 1);
    tcg_gen_movi_tl(cpu_so, 1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], 0);
    gen_set_label(l2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* dozi */
static void gen_dozi(DisasContext *ctx)
{
    target_long simm = SIMM(ctx->opcode);
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_LT, cpu_gpr[rA(ctx->opcode)], simm, l1);
    tcg_gen_subfi_tl(cpu_gpr[rD(ctx->opcode)], simm, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], 0);
    gen_set_label(l2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* lscbx - lscbx. */
static void gen_lscbx(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i32 t1 = tcg_const_i32(rD(ctx->opcode));
    TCGv_i32 t2 = tcg_const_i32(rA(ctx->opcode));
    TCGv_i32 t3 = tcg_const_i32(rB(ctx->opcode));

    gen_addr_reg_index(ctx, t0);
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_lscbx(t0, cpu_env, t0, t1, t2, t3);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
    tcg_gen_andi_tl(cpu_xer, cpu_xer, ~0x7F);
    tcg_gen_or_tl(cpu_xer, cpu_xer, t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, t0);
    tcg_temp_free(t0);
}

/* maskg - maskg. */
static void gen_maskg(DisasContext *ctx)
{
    int l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv t3 = tcg_temp_new();
    tcg_gen_movi_tl(t3, 0xFFFFFFFF);
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_andi_tl(t1, cpu_gpr[rS(ctx->opcode)], 0x1F);
    tcg_gen_addi_tl(t2, t0, 1);
    tcg_gen_shr_tl(t2, t3, t2);
    tcg_gen_shr_tl(t3, t3, t1);
    tcg_gen_xor_tl(cpu_gpr[rA(ctx->opcode)], t2, t3);
    tcg_gen_brcond_tl(TCG_COND_GE, t0, t1, l1);
    tcg_gen_neg_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    gen_set_label(l1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    tcg_temp_free(t3);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* maskir - maskir. */
static void gen_maskir(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_and_tl(t0, cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_andc_tl(t1, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* mul - mul. */
static void gen_mul(DisasContext *ctx)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();
    tcg_gen_extu_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_extu_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(t2, t0);
    gen_store_spr(SPR_MQ, t2);
    tcg_gen_shri_i64(t1, t0, 32);
    tcg_gen_trunc_i64_tl(cpu_gpr[rD(ctx->opcode)], t1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* mulo - mulo. */
static void gen_mulo(DisasContext *ctx)
{
    int l1 = gen_new_label();
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();
    /* Start with XER OV disabled, the most likely case */
    tcg_gen_movi_tl(cpu_ov, 0);
    tcg_gen_extu_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_extu_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(t2, t0);
    gen_store_spr(SPR_MQ, t2);
    tcg_gen_shri_i64(t1, t0, 32);
    tcg_gen_trunc_i64_tl(cpu_gpr[rD(ctx->opcode)], t1);
    tcg_gen_ext32s_i64(t1, t0);
    tcg_gen_brcond_i64(TCG_COND_EQ, t0, t1, l1);
    tcg_gen_movi_tl(cpu_ov, 1);
    tcg_gen_movi_tl(cpu_so, 1);
    gen_set_label(l1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* nabs - nabs. */
static void gen_nabs(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_GT, cpu_gpr[rA(ctx->opcode)], 0, l1);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_neg_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    gen_set_label(l2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* nabso - nabso. */
static void gen_nabso(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_GT, cpu_gpr[rA(ctx->opcode)], 0, l1);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_neg_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    gen_set_label(l2);
    /* nabs never overflows */
    tcg_gen_movi_tl(cpu_ov, 0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
}

/* rlmi - rlmi. */
static void gen_rlmi(DisasContext *ctx)
{
    uint32_t mb = MB(ctx->opcode);
    uint32_t me = ME(ctx->opcode);
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_rotl_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    tcg_gen_andi_tl(t0, t0, MASK(mb, me));
    tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], ~MASK(mb, me));
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], t0);
    tcg_temp_free(t0);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* rrib - rrib. */
static void gen_rrib(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_movi_tl(t1, 0x80000000);
    tcg_gen_shr_tl(t1, t1, t0);
    tcg_gen_shr_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    tcg_gen_and_tl(t0, t0, t1);
    tcg_gen_andc_tl(t1, cpu_gpr[rA(ctx->opcode)], t1);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sle - sle. */
static void gen_sle(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_shl_tl(t0, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_subfi_tl(t1, 32, t1);
    tcg_gen_shr_tl(t1, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_or_tl(t1, t0, t1);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    gen_store_spr(SPR_MQ, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sleq - sleq. */
static void gen_sleq(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_movi_tl(t2, 0xFFFFFFFF);
    tcg_gen_shl_tl(t2, t2, t0);
    tcg_gen_rotl_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    gen_load_spr(t1, SPR_MQ);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_and_tl(t0, t0, t2);
    tcg_gen_andc_tl(t1, t1, t2);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sliq - sliq. */
static void gen_sliq(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_shli_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
    tcg_gen_shri_tl(t1, cpu_gpr[rS(ctx->opcode)], 32 - sh);
    tcg_gen_or_tl(t1, t0, t1);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    gen_store_spr(SPR_MQ, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* slliq - slliq. */
static void gen_slliq(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_rotli_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
    gen_load_spr(t1, SPR_MQ);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_andi_tl(t0, t0,  (0xFFFFFFFFU << sh));
    tcg_gen_andi_tl(t1, t1, ~(0xFFFFFFFFU << sh));
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sllq - sllq. */
static void gen_sllq(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_local_new();
    tcg_gen_andi_tl(t2, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_movi_tl(t1, 0xFFFFFFFF);
    tcg_gen_shl_tl(t1, t1, t2);
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x20);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    gen_load_spr(t0, SPR_MQ);
    tcg_gen_and_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_shl_tl(t0, cpu_gpr[rS(ctx->opcode)], t2);
    gen_load_spr(t2, SPR_MQ);
    tcg_gen_andc_tl(t1, t2, t1);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    gen_set_label(l2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* slq - slq. */
static void gen_slq(DisasContext *ctx)
{
    int l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_shl_tl(t0, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_subfi_tl(t1, 32, t1);
    tcg_gen_shr_tl(t1, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_or_tl(t1, t0, t1);
    gen_store_spr(SPR_MQ, t1);
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x20);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
    tcg_gen_movi_tl(cpu_gpr[rA(ctx->opcode)], 0);
    gen_set_label(l1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sraiq - sraiq. */
static void gen_sraiq(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    int l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
    tcg_gen_shli_tl(t1, cpu_gpr[rS(ctx->opcode)], 32 - sh);
    tcg_gen_or_tl(t0, t0, t1);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_movi_tl(cpu_ca, 0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
    tcg_gen_brcondi_tl(TCG_COND_GE, cpu_gpr[rS(ctx->opcode)], 0, l1);
    tcg_gen_movi_tl(cpu_ca, 1);
    gen_set_label(l1);
    tcg_gen_sari_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], sh);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sraq - sraq. */
static void gen_sraq(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_local_new();
    tcg_gen_andi_tl(t2, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_shr_tl(t0, cpu_gpr[rS(ctx->opcode)], t2);
    tcg_gen_sar_tl(t1, cpu_gpr[rS(ctx->opcode)], t2);
    tcg_gen_subfi_tl(t2, 32, t2);
    tcg_gen_shl_tl(t2, cpu_gpr[rS(ctx->opcode)], t2);
    tcg_gen_or_tl(t0, t0, t2);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x20);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 0, l1);
    tcg_gen_mov_tl(t2, cpu_gpr[rS(ctx->opcode)]);
    tcg_gen_sari_tl(t1, cpu_gpr[rS(ctx->opcode)], 31);
    gen_set_label(l1);
    tcg_temp_free(t0);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t1);
    tcg_gen_movi_tl(cpu_ca, 0);
    tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l2);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t2, 0, l2);
    tcg_gen_movi_tl(cpu_ca, 1);
    gen_set_label(l2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sre - sre. */
static void gen_sre(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_shr_tl(t0, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_subfi_tl(t1, 32, t1);
    tcg_gen_shl_tl(t1, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_or_tl(t1, t0, t1);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    gen_store_spr(SPR_MQ, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srea - srea. */
static void gen_srea(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_rotr_tl(t0, cpu_gpr[rS(ctx->opcode)], t1);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_sar_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sreq */
static void gen_sreq(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_movi_tl(t1, 0xFFFFFFFF);
    tcg_gen_shr_tl(t1, t1, t0);
    tcg_gen_rotr_tl(t0, cpu_gpr[rS(ctx->opcode)], t0);
    gen_load_spr(t2, SPR_MQ);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_and_tl(t0, t0, t1);
    tcg_gen_andc_tl(t2, t2, t1);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* sriq */
static void gen_sriq(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
    tcg_gen_shli_tl(t1, cpu_gpr[rS(ctx->opcode)], 32 - sh);
    tcg_gen_or_tl(t1, t0, t1);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    gen_store_spr(SPR_MQ, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srliq */
static void gen_srliq(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_rotri_tl(t0, cpu_gpr[rS(ctx->opcode)], sh);
    gen_load_spr(t1, SPR_MQ);
    gen_store_spr(SPR_MQ, t0);
    tcg_gen_andi_tl(t0, t0,  (0xFFFFFFFFU >> sh));
    tcg_gen_andi_tl(t1, t1, ~(0xFFFFFFFFU >> sh));
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srlq */
static void gen_srlq(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_local_new();
    tcg_gen_andi_tl(t2, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_movi_tl(t1, 0xFFFFFFFF);
    tcg_gen_shr_tl(t2, t1, t2);
    tcg_gen_andi_tl(t0, cpu_gpr[rB(ctx->opcode)], 0x20);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    gen_load_spr(t0, SPR_MQ);
    tcg_gen_and_tl(cpu_gpr[rA(ctx->opcode)], t0, t2);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_shr_tl(t0, cpu_gpr[rS(ctx->opcode)], t2);
    tcg_gen_and_tl(t0, t0, t2);
    gen_load_spr(t1, SPR_MQ);
    tcg_gen_andc_tl(t1, t1, t2);
    tcg_gen_or_tl(cpu_gpr[rA(ctx->opcode)], t0, t1);
    gen_set_label(l2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* srq */
static void gen_srq(DisasContext *ctx)
{
    int l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x1F);
    tcg_gen_shr_tl(t0, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_subfi_tl(t1, 32, t1);
    tcg_gen_shl_tl(t1, cpu_gpr[rS(ctx->opcode)], t1);
    tcg_gen_or_tl(t1, t0, t1);
    gen_store_spr(SPR_MQ, t1);
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0x20);
    tcg_gen_mov_tl(cpu_gpr[rA(ctx->opcode)], t0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_movi_tl(cpu_gpr[rA(ctx->opcode)], 0);
    gen_set_label(l1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc(ctx->opcode) != 0))
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* PowerPC 602 specific instructions */

/* dsa  */
static void gen_dsa(DisasContext *ctx)
{
    /* XXX: TODO */
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

/* esa */
static void gen_esa(DisasContext *ctx)
{
    /* XXX: TODO */
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

/* mfrom */
static void gen_mfrom(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_602_mfrom(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
#endif
}

/* 602 - 603 - G2 TLB management */

/* tlbld */
static void gen_tlbld_6xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_6xx_tlbd(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

/* tlbli */
static void gen_tlbli_6xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_6xx_tlbi(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

/* 74xx TLB management */

/* tlbld */
static void gen_tlbld_74xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_74xx_tlbd(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

/* tlbli */
static void gen_tlbli_74xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_74xx_tlbi(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

/* POWER instructions not in PowerPC 601 */

/* clf */
static void gen_clf(DisasContext *ctx)
{
    /* Cache line flush: implemented as no-op */
}

/* cli */
static void gen_cli(DisasContext *ctx)
{
    /* Cache line invalidate: privileged and treated as no-op */
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
#endif
}

/* dclst */
static void gen_dclst(DisasContext *ctx)
{
    /* Data cache line store: treated as no-op */
}

static void gen_mfsri(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    tcg_gen_shri_tl(t0, t0, 28);
    tcg_gen_andi_tl(t0, t0, 0xF);
    gen_helper_load_sr(cpu_gpr[rd], cpu_env, t0);
    tcg_temp_free(t0);
    if (ra != 0 && ra != rd)
        tcg_gen_mov_tl(cpu_gpr[ra], cpu_gpr[rd]);
#endif
}

static void gen_rac(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_rac(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif
}

static void gen_rfsvc(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_helper_rfsvc(cpu_env);
    gen_sync_exception(ctx);
#endif
}

/* svc is not implemented for now */

/* POWER2 specific instructions */
/* Quad manipulation (load/store two floats at a time) */

/* lfq */
static void gen_lfq(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_ld64(ctx, cpu_fpr[rd], t0);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_ld64(ctx, cpu_fpr[(rd + 1) % 32], t0);
    tcg_temp_free(t0);
}

/* lfqu */
static void gen_lfqu(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_ld64(ctx, cpu_fpr[rd], t0);
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_ld64(ctx, cpu_fpr[(rd + 1) % 32], t1);
    if (ra != 0)
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* lfqux */
static void gen_lfqux(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    gen_set_access_type(ctx, ACCESS_FLOAT);
    TCGv t0, t1;
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld64(ctx, cpu_fpr[rd], t0);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_ld64(ctx, cpu_fpr[(rd + 1) % 32], t1);
    tcg_temp_free(t1);
    if (ra != 0)
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    tcg_temp_free(t0);
}

/* lfqx */
static void gen_lfqx(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_ld64(ctx, cpu_fpr[rd], t0);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_ld64(ctx, cpu_fpr[(rd + 1) % 32], t0);
    tcg_temp_free(t0);
}

/* stfq */
static void gen_stfq(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_st64(ctx, cpu_fpr[rd], t0);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_st64(ctx, cpu_fpr[(rd + 1) % 32], t0);
    tcg_temp_free(t0);
}

/* stfqu */
static void gen_stfqu(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_imm_index(ctx, t0, 0);
    gen_qemu_st64(ctx, cpu_fpr[rd], t0);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_st64(ctx, cpu_fpr[(rd + 1) % 32], t1);
    tcg_temp_free(t1);
    if (ra != 0)
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    tcg_temp_free(t0);
}

/* stfqux */
static void gen_stfqux(DisasContext *ctx)
{
    int ra = rA(ctx->opcode);
    int rd = rD(ctx->opcode);
    TCGv t0, t1;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_st64(ctx, cpu_fpr[rd], t0);
    t1 = tcg_temp_new();
    gen_addr_add(ctx, t1, t0, 8);
    gen_qemu_st64(ctx, cpu_fpr[(rd + 1) % 32], t1);
    tcg_temp_free(t1);
    if (ra != 0)
        tcg_gen_mov_tl(cpu_gpr[ra], t0);
    tcg_temp_free(t0);
}

/* stfqx */
static void gen_stfqx(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_FLOAT);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_qemu_st64(ctx, cpu_fpr[rd], t0);
    gen_addr_add(ctx, t0, t0, 8);
    gen_qemu_st64(ctx, cpu_fpr[(rd + 1) % 32], t0);
    tcg_temp_free(t0);
}

/* BookE specific instructions */

/* XXX: not implemented on 440 ? */
static void gen_mfapidi(DisasContext *ctx)
{
    /* XXX: TODO */
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

/* XXX: not implemented on 440 ? */
static void gen_tlbiva(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_tlbie(cpu_env, cpu_gpr[rB(ctx->opcode)]);
    tcg_temp_free(t0);
#endif
}

/* All 405 MAC instructions are translated here */
static inline void gen_405_mulladd_insn(DisasContext *ctx, int opc2, int opc3,
                                        int ra, int rb, int rt, int Rc)
{
    TCGv t0, t1;

    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();

    switch (opc3 & 0x0D) {
    case 0x05:
        /* macchw    - macchw.    - macchwo   - macchwo.   */
        /* macchws   - macchws.   - macchwso  - macchwso.  */
        /* nmacchw   - nmacchw.   - nmacchwo  - nmacchwo.  */
        /* nmacchws  - nmacchws.  - nmacchwso - nmacchwso. */
        /* mulchw - mulchw. */
        tcg_gen_ext16s_tl(t0, cpu_gpr[ra]);
        tcg_gen_sari_tl(t1, cpu_gpr[rb], 16);
        tcg_gen_ext16s_tl(t1, t1);
        break;
    case 0x04:
        /* macchwu   - macchwu.   - macchwuo  - macchwuo.  */
        /* macchwsu  - macchwsu.  - macchwsuo - macchwsuo. */
        /* mulchwu - mulchwu. */
        tcg_gen_ext16u_tl(t0, cpu_gpr[ra]);
        tcg_gen_shri_tl(t1, cpu_gpr[rb], 16);
        tcg_gen_ext16u_tl(t1, t1);
        break;
    case 0x01:
        /* machhw    - machhw.    - machhwo   - machhwo.   */
        /* machhws   - machhws.   - machhwso  - machhwso.  */
        /* nmachhw   - nmachhw.   - nmachhwo  - nmachhwo.  */
        /* nmachhws  - nmachhws.  - nmachhwso - nmachhwso. */
        /* mulhhw - mulhhw. */
        tcg_gen_sari_tl(t0, cpu_gpr[ra], 16);
        tcg_gen_ext16s_tl(t0, t0);
        tcg_gen_sari_tl(t1, cpu_gpr[rb], 16);
        tcg_gen_ext16s_tl(t1, t1);
        break;
    case 0x00:
        /* machhwu   - machhwu.   - machhwuo  - machhwuo.  */
        /* machhwsu  - machhwsu.  - machhwsuo - machhwsuo. */
        /* mulhhwu - mulhhwu. */
        tcg_gen_shri_tl(t0, cpu_gpr[ra], 16);
        tcg_gen_ext16u_tl(t0, t0);
        tcg_gen_shri_tl(t1, cpu_gpr[rb], 16);
        tcg_gen_ext16u_tl(t1, t1);
        break;
    case 0x0D:
        /* maclhw    - maclhw.    - maclhwo   - maclhwo.   */
        /* maclhws   - maclhws.   - maclhwso  - maclhwso.  */
        /* nmaclhw   - nmaclhw.   - nmaclhwo  - nmaclhwo.  */
        /* nmaclhws  - nmaclhws.  - nmaclhwso - nmaclhwso. */
        /* mullhw - mullhw. */
        tcg_gen_ext16s_tl(t0, cpu_gpr[ra]);
        tcg_gen_ext16s_tl(t1, cpu_gpr[rb]);
        break;
    case 0x0C:
        /* maclhwu   - maclhwu.   - maclhwuo  - maclhwuo.  */
        /* maclhwsu  - maclhwsu.  - maclhwsuo - maclhwsuo. */
        /* mullhwu - mullhwu. */
        tcg_gen_ext16u_tl(t0, cpu_gpr[ra]);
        tcg_gen_ext16u_tl(t1, cpu_gpr[rb]);
        break;
    }
    if (opc2 & 0x04) {
        /* (n)multiply-and-accumulate (0x0C / 0x0E) */
        tcg_gen_mul_tl(t1, t0, t1);
        if (opc2 & 0x02) {
            /* nmultiply-and-accumulate (0x0E) */
            tcg_gen_sub_tl(t0, cpu_gpr[rt], t1);
        } else {
            /* multiply-and-accumulate (0x0C) */
            tcg_gen_add_tl(t0, cpu_gpr[rt], t1);
        }

        if (opc3 & 0x12) {
            /* Check overflow and/or saturate */
            int l1 = gen_new_label();

            if (opc3 & 0x10) {
                /* Start with XER OV disabled, the most likely case */
                tcg_gen_movi_tl(cpu_ov, 0);
            }
            if (opc3 & 0x01) {
                /* Signed */
                tcg_gen_xor_tl(t1, cpu_gpr[rt], t1);
                tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
                tcg_gen_xor_tl(t1, cpu_gpr[rt], t0);
                tcg_gen_brcondi_tl(TCG_COND_LT, t1, 0, l1);
                if (opc3 & 0x02) {
                    /* Saturate */
                    tcg_gen_sari_tl(t0, cpu_gpr[rt], 31);
                    tcg_gen_xori_tl(t0, t0, 0x7fffffff);
                }
            } else {
                /* Unsigned */
                tcg_gen_brcond_tl(TCG_COND_GEU, t0, t1, l1);
                if (opc3 & 0x02) {
                    /* Saturate */
                    tcg_gen_movi_tl(t0, UINT32_MAX);
                }
            }
            if (opc3 & 0x10) {
                /* Check overflow */
                tcg_gen_movi_tl(cpu_ov, 1);
                tcg_gen_movi_tl(cpu_so, 1);
            }
            gen_set_label(l1);
            tcg_gen_mov_tl(cpu_gpr[rt], t0);
        }
    } else {
        tcg_gen_mul_tl(cpu_gpr[rt], t0, t1);
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    if (unlikely(Rc) != 0) {
        /* Update Rc0 */
        gen_set_Rc0(ctx, cpu_gpr[rt]);
    }
}

#define GEN_MAC_HANDLER(name, opc2, opc3)                                     \
static void glue(gen_, name)(DisasContext *ctx)                               \
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
static void gen_mfdcr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv dcrn;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    dcrn = tcg_const_tl(SPR(ctx->opcode));
    gen_helper_load_dcr(cpu_gpr[rD(ctx->opcode)], cpu_env, dcrn);
    tcg_temp_free(dcrn);
#endif
}

/* mtdcr */
static void gen_mtdcr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    TCGv dcrn;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    dcrn = tcg_const_tl(SPR(ctx->opcode));
    gen_helper_store_dcr(cpu_env, dcrn, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(dcrn);
#endif
}

/* mfdcrx */
/* XXX: not implemented on 440 ? */
static void gen_mfdcrx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_load_dcr(cpu_gpr[rD(ctx->opcode)], cpu_env,
                        cpu_gpr[rA(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
#endif
}

/* mtdcrx */
/* XXX: not implemented on 440 ? */
static void gen_mtdcrx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
        return;
    }
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_store_dcr(cpu_env, cpu_gpr[rA(ctx->opcode)],
                         cpu_gpr[rS(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
#endif
}

/* mfdcrux (PPC 460) : user-mode access to DCR */
static void gen_mfdcrux(DisasContext *ctx)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_load_dcr(cpu_gpr[rD(ctx->opcode)], cpu_env,
                        cpu_gpr[rA(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
}

/* mtdcrux (PPC 460) : user-mode access to DCR */
static void gen_mtdcrux(DisasContext *ctx)
{
    /* NIP cannot be restored if the memory exception comes from an helper */
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_store_dcr(cpu_env, cpu_gpr[rA(ctx->opcode)],
                         cpu_gpr[rS(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
}

/* dccci */
static void gen_dccci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* dcread */
static void gen_dcread(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv EA, val;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_set_access_type(ctx, ACCESS_CACHE);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    val = tcg_temp_new();
    gen_qemu_ld32u(ctx, val, EA);
    tcg_temp_free(val);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], EA);
    tcg_temp_free(EA);
#endif
}

/* icbt */
static void gen_icbt_40x(DisasContext *ctx)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* iccci */
static void gen_iccci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* icread */
static void gen_icread(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* interpreted as no-op */
#endif
}

/* rfci (mem_idx only) */
static void gen_rfci_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* Restore CPU state */
    gen_helper_40x_rfci(cpu_env);
    gen_sync_exception(ctx);
#endif
}

static void gen_rfci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* Restore CPU state */
    gen_helper_rfci(cpu_env);
    gen_sync_exception(ctx);
#endif
}

/* BookE specific */

/* XXX: not implemented on 440 ? */
static void gen_rfdi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* Restore CPU state */
    gen_helper_rfdi(cpu_env);
    gen_sync_exception(ctx);
#endif
}

/* XXX: not implemented on 440 ? */
static void gen_rfmci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    /* Restore CPU state */
    gen_helper_rfmci(cpu_env);
    gen_sync_exception(ctx);
#endif
}

/* TLB management - PowerPC 405 implementation */

/* tlbre */
static void gen_tlbre_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
        gen_helper_4xx_tlbre_hi(cpu_gpr[rD(ctx->opcode)], cpu_env,
                                cpu_gpr[rA(ctx->opcode)]);
        break;
    case 1:
        gen_helper_4xx_tlbre_lo(cpu_gpr[rD(ctx->opcode)], cpu_env,
                                cpu_gpr[rA(ctx->opcode)]);
        break;
    default:
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        break;
    }
#endif
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_4xx_tlbsx(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
    if (Rc(ctx->opcode)) {
        int l1 = gen_new_label();
        tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[rD(ctx->opcode)], -1, l1);
        tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], 0x02);
        gen_set_label(l1);
    }
#endif
}

/* tlbwe */
static void gen_tlbwe_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
        gen_helper_4xx_tlbwe_hi(cpu_env, cpu_gpr[rA(ctx->opcode)],
                                cpu_gpr[rS(ctx->opcode)]);
        break;
    case 1:
        gen_helper_4xx_tlbwe_lo(cpu_env, cpu_gpr[rA(ctx->opcode)],
                                cpu_gpr[rS(ctx->opcode)]);
        break;
    default:
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        break;
    }
#endif
}

/* TLB management - PowerPC 440 implementation */

/* tlbre */
static void gen_tlbre_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
    case 1:
    case 2:
        {
            TCGv_i32 t0 = tcg_const_i32(rB(ctx->opcode));
            gen_helper_440_tlbre(cpu_gpr[rD(ctx->opcode)], cpu_env,
                                 t0, cpu_gpr[rA(ctx->opcode)]);
            tcg_temp_free_i32(t0);
        }
        break;
    default:
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        break;
    }
#endif
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_440_tlbsx(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
    if (Rc(ctx->opcode)) {
        int l1 = gen_new_label();
        tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[rD(ctx->opcode)], -1, l1);
        tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], 0x02);
        gen_set_label(l1);
    }
#endif
}

/* tlbwe */
static void gen_tlbwe_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    switch (rB(ctx->opcode)) {
    case 0:
    case 1:
    case 2:
        {
            TCGv_i32 t0 = tcg_const_i32(rB(ctx->opcode));
            gen_helper_440_tlbwe(cpu_env, t0, cpu_gpr[rA(ctx->opcode)],
                                 cpu_gpr[rS(ctx->opcode)]);
            tcg_temp_free_i32(t0);
        }
        break;
    default:
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        break;
    }
#endif
}

/* TLB management - PowerPC BookE 2.06 implementation */

/* tlbre */
static void gen_tlbre_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    gen_helper_booke206_tlbre(cpu_env);
#endif
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    if (rA(ctx->opcode)) {
        t0 = tcg_temp_new();
        tcg_gen_mov_tl(t0, cpu_gpr[rD(ctx->opcode)]);
    } else {
        t0 = tcg_const_tl(0);
    }

    tcg_gen_add_tl(t0, t0, cpu_gpr[rB(ctx->opcode)]);
    gen_helper_booke206_tlbsx(cpu_env, t0);
#endif
}

/* tlbwe */
static void gen_tlbwe_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    gen_update_nip(ctx, ctx->nip - 4);
    gen_helper_booke206_tlbwe(cpu_env);
#endif
}

static void gen_tlbivax_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);

    gen_helper_booke206_tlbivax(cpu_env, t0);
#endif
}

static void gen_tlbilx_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);

    switch((ctx->opcode >> 21) & 0x3) {
    case 0:
        gen_helper_booke206_tlbilx0(cpu_env, t0);
        break;
    case 1:
        gen_helper_booke206_tlbilx1(cpu_env, t0);
        break;
    case 3:
        gen_helper_booke206_tlbilx3(cpu_env, t0);
        break;
    default:
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        break;
    }

    tcg_temp_free(t0);
#endif
}


/* wrtee */
static void gen_wrtee(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    TCGv t0;
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rD(ctx->opcode)], (1 << MSR_EE));
    tcg_gen_andi_tl(cpu_msr, cpu_msr, ~(1 << MSR_EE));
    tcg_gen_or_tl(cpu_msr, cpu_msr, t0);
    tcg_temp_free(t0);
    /* Stop translation to have a chance to raise an exception
     * if we just set msr_ee to 1
     */
    gen_stop_exception(ctx);
#endif
}

/* wrteei */
static void gen_wrteei(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(!ctx->mem_idx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }
    if (ctx->opcode & 0x00008000) {
        tcg_gen_ori_tl(cpu_msr, cpu_msr, (1 << MSR_EE));
        /* Stop translation to have a chance to raise an exception */
        gen_stop_exception(ctx);
    } else {
        tcg_gen_andi_tl(cpu_msr, cpu_msr, ~(1 << MSR_EE));
    }
#endif
}

/* PowerPC 440 specific instructions */

/* dlmzb */
static void gen_dlmzb(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_const_i32(Rc(ctx->opcode));
    gen_helper_dlmzb(cpu_gpr[rA(ctx->opcode)], cpu_env,
                     cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)], t0);
    tcg_temp_free_i32(t0);
}

/* mbar replaces eieio on 440 */
static void gen_mbar(DisasContext *ctx)
{
    /* interpreted as no-op */
}

/* msync replaces sync on 440 */
static void gen_msync_4xx(DisasContext *ctx)
{
    /* interpreted as no-op */
}

/* icbt */
static void gen_icbt_440(DisasContext *ctx)
{
    /* interpreted as no-op */
    /* XXX: specification say this is treated as a load by the MMU
     *      but does not generate any exception
     */
}

/* Embedded.Processor Control */

static void gen_msgclr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(ctx->mem_idx == 0)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    gen_helper_msgclr(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif
}

static void gen_msgsnd(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
#else
    if (unlikely(ctx->mem_idx == 0)) {
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_OPC);
        return;
    }

    gen_helper_msgsnd(cpu_gpr[rB(ctx->opcode)]);
#endif
}

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
    if (ctx->le_mode) {                                                       \
        gen_qemu_ld64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                    \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                    \
    } else {                                                                  \
        gen_qemu_ld64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                    \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_ld64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                    \
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
    if (ctx->le_mode) {                                                       \
        gen_qemu_st64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                    \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_st64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                    \
    } else {                                                                  \
        gen_qemu_st64(ctx, cpu_avrh[rD(ctx->opcode)], EA);                    \
        tcg_gen_addi_tl(EA, EA, 8);                                           \
        gen_qemu_st64(ctx, cpu_avrl[rD(ctx->opcode)], EA);                    \
    }                                                                         \
    tcg_temp_free(EA);                                                        \
}

#define GEN_VR_LVE(name, opc2, opc3)                                    \
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
        rs = gen_avr_ptr(rS(ctx->opcode));                              \
        gen_helper_lve##name(cpu_env, rs, EA);                          \
        tcg_temp_free(EA);                                              \
        tcg_temp_free_ptr(rs);                                          \
    }

#define GEN_VR_STVE(name, opc2, opc3)                                   \
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
        rs = gen_avr_ptr(rS(ctx->opcode));                              \
        gen_helper_stve##name(cpu_env, rs, EA);                         \
        tcg_temp_free(EA);                                              \
        tcg_temp_free_ptr(rs);                                          \
    }

GEN_VR_LDX(lvx, 0x07, 0x03);
/* As we don't emulate the cache, lvxl is stricly equivalent to lvx */
GEN_VR_LDX(lvxl, 0x07, 0x0B);

GEN_VR_LVE(bx, 0x07, 0x00);
GEN_VR_LVE(hx, 0x07, 0x01);
GEN_VR_LVE(wx, 0x07, 0x02);

GEN_VR_STX(svx, 0x07, 0x07);
/* As we don't emulate the cache, stvxl is stricly equivalent to stvx */
GEN_VR_STX(svxl, 0x07, 0x0F);

GEN_VR_STVE(bx, 0x07, 0x04);
GEN_VR_STVE(hx, 0x07, 0x05);
GEN_VR_STVE(wx, 0x07, 0x06);

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
    p = gen_avr_ptr(rD(ctx->opcode));
    gen_helper_mtvscr(cpu_env, p);
    tcg_temp_free_ptr(p);
}

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

GEN_VXFORM(vaddubm, 0, 0);
GEN_VXFORM(vadduhm, 0, 1);
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
GEN_VXFORM(vavguh, 1, 17);
GEN_VXFORM(vavguw, 1, 18);
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
GEN_VXFORM(vsld, 2, 23);
GEN_VXFORM(vsrb, 2, 8);
GEN_VXFORM(vsrh, 2, 9);
GEN_VXFORM(vsrw, 2, 10);
GEN_VXFORM(vsrd, 2, 27);
GEN_VXFORM(vsrab, 2, 12);
GEN_VXFORM(vsrah, 2, 13);
GEN_VXFORM(vsraw, 2, 14);
GEN_VXFORM(vsrad, 2, 15);
GEN_VXFORM(vslo, 6, 16);
GEN_VXFORM(vsro, 6, 17);
GEN_VXFORM(vaddcuw, 0, 6);
GEN_VXFORM(vsubcuw, 0, 22);
GEN_VXFORM_ENV(vaddubs, 0, 8);
GEN_VXFORM_ENV(vadduhs, 0, 9);
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
GEN_VXFORM(vrld, 2, 3);
GEN_VXFORM(vsl, 2, 7);
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
GEN_VXFORM_NOA_ENV(vrfim, 5, 8);
GEN_VXFORM_NOA_ENV(vrfin, 5, 9);
GEN_VXFORM_NOA_ENV(vrfip, 5, 10);
GEN_VXFORM_NOA_ENV(vrfiz, 5, 11);

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

GEN_VXFORM_UIMM(vspltb, 6, 8);
GEN_VXFORM_UIMM(vsplth, 6, 9);
GEN_VXFORM_UIMM(vspltw, 6, 10);
GEN_VXFORM_UIMM_ENV(vcfux, 5, 12);
GEN_VXFORM_UIMM_ENV(vcfsx, 5, 13);
GEN_VXFORM_UIMM_ENV(vctuxs, 5, 14);
GEN_VXFORM_UIMM_ENV(vctsxs, 5, 15);

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

GEN_VAFORM_PAIRED(vmsumubm, vmsummbm, 18)
GEN_VAFORM_PAIRED(vmsumuhm, vmsumuhs, 19)
GEN_VAFORM_PAIRED(vmsumshm, vmsumshs, 20)
GEN_VAFORM_PAIRED(vsel, vperm, 21)
GEN_VAFORM_PAIRED(vmaddfp, vnmsubfp, 23)

GEN_VXFORM_NOA(vclzb, 1, 28)
GEN_VXFORM_NOA(vclzh, 1, 29)
GEN_VXFORM_NOA(vclzw, 1, 30)
GEN_VXFORM_NOA(vclzd, 1, 31)
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

GEN_BCD(bcdadd)
GEN_BCD(bcdsub)

GEN_VXFORM_DUAL(vsububm, PPC_ALTIVEC, PPC_NONE, \
                bcdadd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsububs, PPC_ALTIVEC, PPC_NONE, \
                bcdadd, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubuhm, PPC_ALTIVEC, PPC_NONE, \
                bcdsub, PPC_NONE, PPC2_ALTIVEC_207)
GEN_VXFORM_DUAL(vsubuhs, PPC_ALTIVEC, PPC_NONE, \
                bcdsub, PPC_NONE, PPC2_ALTIVEC_207)

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

VSX_LOAD_SCALAR(lxsdx, ld64)
VSX_LOAD_SCALAR(lxsiwax, ld32s_i64)
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
    gen_qemu_ld64(ctx, cpu_vsrh(xT(ctx->opcode)), EA);
    tcg_gen_addi_tl(EA, EA, 8);
    gen_qemu_ld64(ctx, cpu_vsrl(xT(ctx->opcode)), EA);
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
    gen_qemu_ld64(ctx, cpu_vsrh(xT(ctx->opcode)), EA);
    tcg_gen_mov_i64(cpu_vsrl(xT(ctx->opcode)), cpu_vsrh(xT(ctx->opcode)));
    tcg_temp_free(EA);
}

static void gen_lxvw4x(DisasContext *ctx)
{
    TCGv EA;
    TCGv_i64 tmp;
    TCGv_i64 xth = cpu_vsrh(xT(ctx->opcode));
    TCGv_i64 xtl = cpu_vsrl(xT(ctx->opcode));
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    tmp = tcg_temp_new_i64();

    gen_addr_reg_index(ctx, EA);
    gen_qemu_ld32u_i64(ctx, tmp, EA);
    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_ld32u_i64(ctx, xth, EA);
    tcg_gen_deposit_i64(xth, xth, tmp, 32, 32);

    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_ld32u_i64(ctx, tmp, EA);
    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_ld32u_i64(ctx, xtl, EA);
    tcg_gen_deposit_i64(xtl, xtl, tmp, 32, 32);

    tcg_temp_free(EA);
    tcg_temp_free_i64(tmp);
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

VSX_STORE_SCALAR(stxsdx, st64)
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
    gen_qemu_st64(ctx, cpu_vsrh(xS(ctx->opcode)), EA);
    tcg_gen_addi_tl(EA, EA, 8);
    gen_qemu_st64(ctx, cpu_vsrl(xS(ctx->opcode)), EA);
    tcg_temp_free(EA);
}

static void gen_stxvw4x(DisasContext *ctx)
{
    TCGv_i64 tmp;
    TCGv EA;
    if (unlikely(!ctx->vsx_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_VSXU);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    tmp = tcg_temp_new_i64();

    tcg_gen_shri_i64(tmp, cpu_vsrh(xS(ctx->opcode)), 32);
    gen_qemu_st32_i64(ctx, tmp, EA);
    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_st32_i64(ctx, cpu_vsrh(xS(ctx->opcode)), EA);

    tcg_gen_shri_i64(tmp, cpu_vsrl(xS(ctx->opcode)), 32);
    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_st32_i64(ctx, tmp, EA);
    tcg_gen_addi_tl(EA, EA, 4);
    gen_qemu_st32_i64(ctx, cpu_vsrl(xS(ctx->opcode)), EA);

    tcg_temp_free(EA);
    tcg_temp_free_i64(tmp);
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
    /* NIP cannot be restored if the memory exception comes from an helper */ \
    gen_update_nip(ctx, ctx->nip - 4);                                        \
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
    /* NIP cannot be restored if the exception comes */       \
    /* from a helper. */                                      \
    gen_update_nip(ctx, ctx->nip - 4);                        \
                                                              \
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


/***                           SPE extension                               ***/
/* Register moves */

static inline void gen_evmra(DisasContext *ctx)
{

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

#if defined(TARGET_PPC64)
    /* rD := rA */
    tcg_gen_mov_i64(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);

    /* spe_acc := rA */
    tcg_gen_st_i64(cpu_gpr[rA(ctx->opcode)],
                   cpu_env,
                   offsetof(CPUPPCState, spe_acc));
#else
    TCGv_i64 tmp = tcg_temp_new_i64();

    /* tmp := rA_lo + rA_hi << 32 */
    tcg_gen_concat_i32_i64(tmp, cpu_gpr[rA(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);

    /* spe_acc := tmp */
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));
    tcg_temp_free_i64(tmp);

    /* rD := rA */
    tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
#endif
}

static inline void gen_load_gpr64(TCGv_i64 t, int reg)
{
#if defined(TARGET_PPC64)
    tcg_gen_mov_i64(t, cpu_gpr[reg]);
#else
    tcg_gen_concat_i32_i64(t, cpu_gpr[reg], cpu_gprh[reg]);
#endif
}

static inline void gen_store_gpr64(int reg, TCGv_i64 t)
{
#if defined(TARGET_PPC64)
    tcg_gen_mov_i64(cpu_gpr[reg], t);
#else
    TCGv_i64 tmp = tcg_temp_new_i64();
    tcg_gen_trunc_i64_i32(cpu_gpr[reg], t);
    tcg_gen_shri_i64(tmp, t, 32);
    tcg_gen_trunc_i64_i32(cpu_gprh[reg], tmp);
    tcg_temp_free_i64(tmp);
#endif
}

#define GEN_SPE(name0, name1, opc2, opc3, inval0, inval1, type)         \
static void glue(gen_, name0##_##name1)(DisasContext *ctx)                    \
{                                                                             \
    if (Rc(ctx->opcode))                                                      \
        gen_##name1(ctx);                                                     \
    else                                                                      \
        gen_##name0(ctx);                                                     \
}

/* Handler for undefined SPE opcodes */
static inline void gen_speundef(DisasContext *ctx)
{
    gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
}

/* SPE logic */
#if defined(TARGET_PPC64)
#define GEN_SPEOP_LOGIC2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],                \
           cpu_gpr[rB(ctx->opcode)]);                                         \
}
#else
#define GEN_SPEOP_LOGIC2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],                \
           cpu_gpr[rB(ctx->opcode)]);                                         \
    tcg_op(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],              \
           cpu_gprh[rB(ctx->opcode)]);                                        \
}
#endif

GEN_SPEOP_LOGIC2(evand, tcg_gen_and_tl);
GEN_SPEOP_LOGIC2(evandc, tcg_gen_andc_tl);
GEN_SPEOP_LOGIC2(evxor, tcg_gen_xor_tl);
GEN_SPEOP_LOGIC2(evor, tcg_gen_or_tl);
GEN_SPEOP_LOGIC2(evnor, tcg_gen_nor_tl);
GEN_SPEOP_LOGIC2(eveqv, tcg_gen_eqv_tl);
GEN_SPEOP_LOGIC2(evorc, tcg_gen_orc_tl);
GEN_SPEOP_LOGIC2(evnand, tcg_gen_nand_tl);

/* SPE logic immediate */
#if defined(TARGET_PPC64)
#define GEN_SPEOP_TCG_LOGIC_IMM2(name, tcg_opi)                               \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    TCGv_i32 t0 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_local_new_i32();                                   \
    TCGv_i64 t2 = tcg_temp_local_new_i64();                                   \
    tcg_gen_trunc_i64_i32(t0, cpu_gpr[rA(ctx->opcode)]);                      \
    tcg_opi(t0, t0, rB(ctx->opcode));                                         \
    tcg_gen_shri_i64(t2, cpu_gpr[rA(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t1, t2);                                            \
    tcg_temp_free_i64(t2);                                                    \
    tcg_opi(t1, t1, rB(ctx->opcode));                                         \
    tcg_gen_concat_i32_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);                 \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#else
#define GEN_SPEOP_TCG_LOGIC_IMM2(name, tcg_opi)                               \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_opi(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],               \
            rB(ctx->opcode));                                                 \
    tcg_opi(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],             \
            rB(ctx->opcode));                                                 \
}
#endif
GEN_SPEOP_TCG_LOGIC_IMM2(evslwi, tcg_gen_shli_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evsrwiu, tcg_gen_shri_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evsrwis, tcg_gen_sari_i32);
GEN_SPEOP_TCG_LOGIC_IMM2(evrlwi, tcg_gen_rotli_i32);

/* SPE arithmetic */
#if defined(TARGET_PPC64)
#define GEN_SPEOP_ARITH1(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    TCGv_i32 t0 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_local_new_i32();                                   \
    TCGv_i64 t2 = tcg_temp_local_new_i64();                                   \
    tcg_gen_trunc_i64_i32(t0, cpu_gpr[rA(ctx->opcode)]);                      \
    tcg_op(t0, t0);                                                           \
    tcg_gen_shri_i64(t2, cpu_gpr[rA(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t1, t2);                                            \
    tcg_temp_free_i64(t2);                                                    \
    tcg_op(t1, t1);                                                           \
    tcg_gen_concat_i32_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);                 \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#else
#define GEN_SPEOP_ARITH1(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);               \
    tcg_op(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);             \
}
#endif

static inline void gen_op_evabs(TCGv_i32 ret, TCGv_i32 arg1)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_GE, arg1, 0, l1);
    tcg_gen_neg_i32(ret, arg1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(ret, arg1);
    gen_set_label(l2);
}
GEN_SPEOP_ARITH1(evabs, gen_op_evabs);
GEN_SPEOP_ARITH1(evneg, tcg_gen_neg_i32);
GEN_SPEOP_ARITH1(evextsb, tcg_gen_ext8s_i32);
GEN_SPEOP_ARITH1(evextsh, tcg_gen_ext16s_i32);
static inline void gen_op_evrndw(TCGv_i32 ret, TCGv_i32 arg1)
{
    tcg_gen_addi_i32(ret, arg1, 0x8000);
    tcg_gen_ext16u_i32(ret, ret);
}
GEN_SPEOP_ARITH1(evrndw, gen_op_evrndw);
GEN_SPEOP_ARITH1(evcntlsw, gen_helper_cntlsw32);
GEN_SPEOP_ARITH1(evcntlzw, gen_helper_cntlzw32);

#if defined(TARGET_PPC64)
#define GEN_SPEOP_ARITH2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    TCGv_i32 t0 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t2 = tcg_temp_local_new_i32();                                   \
    TCGv_i64 t3 = tcg_temp_local_new_i64();                                   \
    tcg_gen_trunc_i64_i32(t0, cpu_gpr[rA(ctx->opcode)]);                      \
    tcg_gen_trunc_i64_i32(t2, cpu_gpr[rB(ctx->opcode)]);                      \
    tcg_op(t0, t0, t2);                                                       \
    tcg_gen_shri_i64(t3, cpu_gpr[rA(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t1, t3);                                            \
    tcg_gen_shri_i64(t3, cpu_gpr[rB(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t2, t3);                                            \
    tcg_temp_free_i64(t3);                                                    \
    tcg_op(t1, t1, t2);                                                       \
    tcg_temp_free_i32(t2);                                                    \
    tcg_gen_concat_i32_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);                 \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#else
#define GEN_SPEOP_ARITH2(name, tcg_op)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],                \
           cpu_gpr[rB(ctx->opcode)]);                                         \
    tcg_op(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)],              \
           cpu_gprh[rB(ctx->opcode)]);                                        \
}
#endif

static inline void gen_op_evsrwu(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0;
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new_i32();
    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_shr_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evsrwu, gen_op_evsrwu);
static inline void gen_op_evsrws(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0;
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new_i32();
    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_sar_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evsrws, gen_op_evsrws);
static inline void gen_op_evslw(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0;
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    t0 = tcg_temp_local_new_i32();
    /* No error here: 6 bits are used */
    tcg_gen_andi_i32(t0, arg2, 0x3F);
    tcg_gen_brcondi_i32(TCG_COND_GE, t0, 32, l1);
    tcg_gen_shl_i32(ret, arg1, t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_movi_i32(ret, 0);
    gen_set_label(l2);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evslw, gen_op_evslw);
static inline void gen_op_evrlw(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, arg2, 0x1F);
    tcg_gen_rotl_i32(ret, arg1, t0);
    tcg_temp_free_i32(t0);
}
GEN_SPEOP_ARITH2(evrlw, gen_op_evrlw);
static inline void gen_evmergehi(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 32);
    tcg_gen_andi_tl(t1, cpu_gpr[rA(ctx->opcode)], 0xFFFFFFFF0000000ULL);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#else
    tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
    tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
#endif
}
GEN_SPEOP_ARITH2(evaddw, tcg_gen_add_i32);
static inline void gen_op_evsubf(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2)
{
    tcg_gen_sub_i32(ret, arg2, arg1);
}
GEN_SPEOP_ARITH2(evsubfw, gen_op_evsubf);

/* SPE arithmetic immediate */
#if defined(TARGET_PPC64)
#define GEN_SPEOP_ARITH_IMM2(name, tcg_op)                                    \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    TCGv_i32 t0 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_local_new_i32();                                   \
    TCGv_i64 t2 = tcg_temp_local_new_i64();                                   \
    tcg_gen_trunc_i64_i32(t0, cpu_gpr[rB(ctx->opcode)]);                      \
    tcg_op(t0, t0, rA(ctx->opcode));                                          \
    tcg_gen_shri_i64(t2, cpu_gpr[rB(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t1, t2);                                            \
    tcg_temp_free_i64(t2);                                                    \
    tcg_op(t1, t1, rA(ctx->opcode));                                          \
    tcg_gen_concat_i32_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);                 \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#else
#define GEN_SPEOP_ARITH_IMM2(name, tcg_op)                                    \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    tcg_op(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],                \
           rA(ctx->opcode));                                                  \
    tcg_op(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)],              \
           rA(ctx->opcode));                                                  \
}
#endif
GEN_SPEOP_ARITH_IMM2(evaddiw, tcg_gen_addi_i32);
GEN_SPEOP_ARITH_IMM2(evsubifw, tcg_gen_subi_i32);

/* SPE comparison */
#if defined(TARGET_PPC64)
#define GEN_SPEOP_COMP(name, tcg_cond)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    int l1 = gen_new_label();                                                 \
    int l2 = gen_new_label();                                                 \
    int l3 = gen_new_label();                                                 \
    int l4 = gen_new_label();                                                 \
    TCGv_i32 t0 = tcg_temp_local_new_i32();                                   \
    TCGv_i32 t1 = tcg_temp_local_new_i32();                                   \
    TCGv_i64 t2 = tcg_temp_local_new_i64();                                   \
    tcg_gen_trunc_i64_i32(t0, cpu_gpr[rA(ctx->opcode)]);                      \
    tcg_gen_trunc_i64_i32(t1, cpu_gpr[rB(ctx->opcode)]);                      \
    tcg_gen_brcond_i32(tcg_cond, t0, t1, l1);                                 \
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)], 0);                          \
    tcg_gen_br(l2);                                                           \
    gen_set_label(l1);                                                        \
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)],                              \
                     CRF_CL | CRF_CH_OR_CL | CRF_CH_AND_CL);                  \
    gen_set_label(l2);                                                        \
    tcg_gen_shri_i64(t2, cpu_gpr[rA(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t0, t2);                                            \
    tcg_gen_shri_i64(t2, cpu_gpr[rB(ctx->opcode)], 32);                       \
    tcg_gen_trunc_i64_i32(t1, t2);                                            \
    tcg_temp_free_i64(t2);                                                    \
    tcg_gen_brcond_i32(tcg_cond, t0, t1, l3);                                 \
    tcg_gen_andi_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],  \
                     ~(CRF_CH | CRF_CH_AND_CL));                              \
    tcg_gen_br(l4);                                                           \
    gen_set_label(l3);                                                        \
    tcg_gen_ori_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],   \
                    CRF_CH | CRF_CH_OR_CL);                                   \
    gen_set_label(l4);                                                        \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#else
#define GEN_SPEOP_COMP(name, tcg_cond)                                        \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    int l1 = gen_new_label();                                                 \
    int l2 = gen_new_label();                                                 \
    int l3 = gen_new_label();                                                 \
    int l4 = gen_new_label();                                                 \
                                                                              \
    tcg_gen_brcond_i32(tcg_cond, cpu_gpr[rA(ctx->opcode)],                    \
                       cpu_gpr[rB(ctx->opcode)], l1);                         \
    tcg_gen_movi_tl(cpu_crf[crfD(ctx->opcode)], 0);                           \
    tcg_gen_br(l2);                                                           \
    gen_set_label(l1);                                                        \
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)],                              \
                     CRF_CL | CRF_CH_OR_CL | CRF_CH_AND_CL);                  \
    gen_set_label(l2);                                                        \
    tcg_gen_brcond_i32(tcg_cond, cpu_gprh[rA(ctx->opcode)],                   \
                       cpu_gprh[rB(ctx->opcode)], l3);                        \
    tcg_gen_andi_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],  \
                     ~(CRF_CH | CRF_CH_AND_CL));                              \
    tcg_gen_br(l4);                                                           \
    gen_set_label(l3);                                                        \
    tcg_gen_ori_i32(cpu_crf[crfD(ctx->opcode)], cpu_crf[crfD(ctx->opcode)],   \
                    CRF_CH | CRF_CH_OR_CL);                                   \
    gen_set_label(l4);                                                        \
}
#endif
GEN_SPEOP_COMP(evcmpgtu, TCG_COND_GTU);
GEN_SPEOP_COMP(evcmpgts, TCG_COND_GT);
GEN_SPEOP_COMP(evcmpltu, TCG_COND_LTU);
GEN_SPEOP_COMP(evcmplts, TCG_COND_LT);
GEN_SPEOP_COMP(evcmpeq, TCG_COND_EQ);

/* SPE misc */
static inline void gen_brinc(DisasContext *ctx)
{
    /* Note: brinc is usable even if SPE is disabled */
    gen_helper_brinc(cpu_gpr[rD(ctx->opcode)],
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
}
static inline void gen_evmergelo(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_ext32u_tl(t0, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_shli_tl(t1, cpu_gpr[rA(ctx->opcode)], 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#else
    tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
#endif
}
static inline void gen_evmergehilo(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_ext32u_tl(t0, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_andi_tl(t1, cpu_gpr[rA(ctx->opcode)], 0xFFFFFFFF0000000ULL);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#else
    tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
#endif
}
static inline void gen_evmergelohi(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rB(ctx->opcode)], 32);
    tcg_gen_shli_tl(t1, cpu_gpr[rA(ctx->opcode)], 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#else
    if (rD(ctx->opcode) == rA(ctx->opcode)) {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_mov_i32(tmp, cpu_gpr[rA(ctx->opcode)]);
        tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
        tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], tmp);
        tcg_temp_free_i32(tmp);
    } else {
        tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
        tcg_gen_mov_i32(cpu_gprh[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    }
#endif
}
static inline void gen_evsplati(DisasContext *ctx)
{
    uint64_t imm = ((int32_t)(rA(ctx->opcode) << 27)) >> 27;

#if defined(TARGET_PPC64)
    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], (imm << 32) | imm);
#else
    tcg_gen_movi_i32(cpu_gpr[rD(ctx->opcode)], imm);
    tcg_gen_movi_i32(cpu_gprh[rD(ctx->opcode)], imm);
#endif
}
static inline void gen_evsplatfi(DisasContext *ctx)
{
    uint64_t imm = rA(ctx->opcode) << 27;

#if defined(TARGET_PPC64)
    tcg_gen_movi_tl(cpu_gpr[rD(ctx->opcode)], (imm << 32) | imm);
#else
    tcg_gen_movi_i32(cpu_gpr[rD(ctx->opcode)], imm);
    tcg_gen_movi_i32(cpu_gprh[rD(ctx->opcode)], imm);
#endif
}

static inline void gen_evsel(DisasContext *ctx)
{
    int l1 = gen_new_label();
    int l2 = gen_new_label();
    int l3 = gen_new_label();
    int l4 = gen_new_label();
    TCGv_i32 t0 = tcg_temp_local_new_i32();
#if defined(TARGET_PPC64)
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_local_new();
#endif
    tcg_gen_andi_i32(t0, cpu_crf[ctx->opcode & 0x07], 1 << 3);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
#if defined(TARGET_PPC64)
    tcg_gen_andi_tl(t1, cpu_gpr[rA(ctx->opcode)], 0xFFFFFFFF00000000ULL);
#else
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)]);
#endif
    tcg_gen_br(l2);
    gen_set_label(l1);
#if defined(TARGET_PPC64)
    tcg_gen_andi_tl(t1, cpu_gpr[rB(ctx->opcode)], 0xFFFFFFFF00000000ULL);
#else
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rB(ctx->opcode)]);
#endif
    gen_set_label(l2);
    tcg_gen_andi_i32(t0, cpu_crf[ctx->opcode & 0x07], 1 << 2);
    tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l3);
#if defined(TARGET_PPC64)
    tcg_gen_ext32u_tl(t2, cpu_gpr[rA(ctx->opcode)]);
#else
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
#endif
    tcg_gen_br(l4);
    gen_set_label(l3);
#if defined(TARGET_PPC64)
    tcg_gen_ext32u_tl(t2, cpu_gpr[rB(ctx->opcode)]);
#else
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
#endif
    gen_set_label(l4);
    tcg_temp_free_i32(t0);
#if defined(TARGET_PPC64)
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], t1, t2);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
#endif
}

static void gen_evsel0(DisasContext *ctx)
{
    gen_evsel(ctx);
}

static void gen_evsel1(DisasContext *ctx)
{
    gen_evsel(ctx);
}

static void gen_evsel2(DisasContext *ctx)
{
    gen_evsel(ctx);
}

static void gen_evsel3(DisasContext *ctx)
{
    gen_evsel(ctx);
}

/* Multiply */

static inline void gen_evmwumi(DisasContext *ctx)
{
    TCGv_i64 t0, t1;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    /* t0 := rA; t1 := rB */
#if defined(TARGET_PPC64)
    tcg_gen_ext32u_tl(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32u_tl(t1, cpu_gpr[rB(ctx->opcode)]);
#else
    tcg_gen_extu_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_extu_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
#endif

    tcg_gen_mul_i64(t0, t0, t1);  /* t0 := rA * rB */

    gen_store_gpr64(rD(ctx->opcode), t0); /* rD := t0 */

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void gen_evmwumia(DisasContext *ctx)
{
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwumi(ctx);            /* rD := rA * rB */

    tmp = tcg_temp_new_i64();

    /* acc := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));
    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwumiaa(DisasContext *ctx)
{
    TCGv_i64 acc;
    TCGv_i64 tmp;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    gen_evmwumi(ctx);           /* rD := rA * rB */

    acc = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    /* tmp := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));

    /* Load acc */
    tcg_gen_ld_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* acc := tmp + acc */
    tcg_gen_add_i64(acc, acc, tmp);

    /* Store acc */
    tcg_gen_st_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* rD := acc */
    gen_store_gpr64(rD(ctx->opcode), acc);

    tcg_temp_free_i64(acc);
    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwsmi(DisasContext *ctx)
{
    TCGv_i64 t0, t1;

    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    /* t0 := rA; t1 := rB */
#if defined(TARGET_PPC64)
    tcg_gen_ext32s_tl(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32s_tl(t1, cpu_gpr[rB(ctx->opcode)]);
#else
    tcg_gen_ext_tl_i64(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext_tl_i64(t1, cpu_gpr[rB(ctx->opcode)]);
#endif

    tcg_gen_mul_i64(t0, t0, t1);  /* t0 := rA * rB */

    gen_store_gpr64(rD(ctx->opcode), t0); /* rD := t0 */

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void gen_evmwsmia(DisasContext *ctx)
{
    TCGv_i64 tmp;

    gen_evmwsmi(ctx);            /* rD := rA * rB */

    tmp = tcg_temp_new_i64();

    /* acc := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));
    tcg_gen_st_i64(tmp, cpu_env, offsetof(CPUPPCState, spe_acc));

    tcg_temp_free_i64(tmp);
}

static inline void gen_evmwsmiaa(DisasContext *ctx)
{
    TCGv_i64 acc = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    gen_evmwsmi(ctx);           /* rD := rA * rB */

    acc = tcg_temp_new_i64();
    tmp = tcg_temp_new_i64();

    /* tmp := rD */
    gen_load_gpr64(tmp, rD(ctx->opcode));

    /* Load acc */
    tcg_gen_ld_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* acc := tmp + acc */
    tcg_gen_add_i64(acc, acc, tmp);

    /* Store acc */
    tcg_gen_st_i64(acc, cpu_env, offsetof(CPUPPCState, spe_acc));

    /* rD := acc */
    gen_store_gpr64(rD(ctx->opcode), acc);

    tcg_temp_free_i64(acc);
    tcg_temp_free_i64(tmp);
}

GEN_SPE(evaddw,      speundef,    0x00, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evaddiw,     speundef,    0x01, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evsubfw,     speundef,    0x02, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evsubifw,    speundef,    0x03, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evabs,       evneg,       0x04, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evextsb,     evextsh,     0x05, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evrndw,      evcntlzw,    0x06, 0x08, 0x0000F800, 0x0000F800, PPC_SPE); ////
GEN_SPE(evcntlsw,    brinc,       0x07, 0x08, 0x0000F800, 0x00000000, PPC_SPE); //
GEN_SPE(evmra,       speundef,    0x02, 0x13, 0x0000F800, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(speundef,    evand,       0x08, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE); ////
GEN_SPE(evandc,      speundef,    0x09, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evxor,       evor,        0x0B, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evnor,       eveqv,       0x0C, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evmwumi,     evmwsmi,     0x0C, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwumia,    evmwsmia,    0x1C, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwumiaa,   evmwsmiaa,   0x0C, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,    evorc,       0x0D, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE); ////
GEN_SPE(evnand,      speundef,    0x0F, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evsrwu,      evsrws,      0x10, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evsrwiu,     evsrwis,     0x11, 0x08, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evslw,       speundef,    0x12, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE); ////
GEN_SPE(evslwi,      speundef,    0x13, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evrlw,       evsplati,    0x14, 0x08, 0x00000000, 0x0000F800, PPC_SPE); //
GEN_SPE(evrlwi,      evsplatfi,   0x15, 0x08, 0x00000000, 0x0000F800, PPC_SPE);
GEN_SPE(evmergehi,   evmergelo,   0x16, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evmergehilo, evmergelohi, 0x17, 0x08, 0x00000000, 0x00000000, PPC_SPE); ////
GEN_SPE(evcmpgtu,    evcmpgts,    0x18, 0x08, 0x00600000, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpltu,    evcmplts,    0x19, 0x08, 0x00600000, 0x00600000, PPC_SPE); ////
GEN_SPE(evcmpeq,     speundef,    0x1A, 0x08, 0x00600000, 0xFFFFFFFF, PPC_SPE); ////

/* SPE load and stores */
static inline void gen_addr_spe_imm_index(DisasContext *ctx, TCGv EA, int sh)
{
    target_ulong uimm = rB(ctx->opcode);

    if (rA(ctx->opcode) == 0) {
        tcg_gen_movi_tl(EA, uimm << sh);
    } else {
        tcg_gen_addi_tl(EA, cpu_gpr[rA(ctx->opcode)], uimm << sh);
        if (NARROW_MODE(ctx)) {
            tcg_gen_ext32u_tl(EA, EA);
        }
    }
}

static inline void gen_op_evldd(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    gen_qemu_ld64(ctx, cpu_gpr[rD(ctx->opcode)], addr);
#else
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_qemu_ld64(ctx, t0, addr);
    tcg_gen_trunc_i64_i32(cpu_gpr[rD(ctx->opcode)], t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_i32(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_temp_free_i64(t0);
#endif
}

static inline void gen_op_evldw(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld32u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 32);
    gen_addr_add(ctx, addr, addr, 4);
    gen_qemu_ld32u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
#else
    gen_qemu_ld32u(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 4);
    gen_qemu_ld32u(ctx, cpu_gpr[rD(ctx->opcode)], addr);
#endif
}

static inline void gen_op_evldh(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 48);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhesplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 48);
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhousplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, t0, addr);
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlhhossplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16s(ctx, t0, addr);
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 32);
    tcg_gen_ext32u_tl(t0, t0);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhe(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 48);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 16);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhou(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16u(ctx, cpu_gpr[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
#else
    gen_qemu_ld16u(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, cpu_gpr[rD(ctx->opcode)], addr);
#endif
}

static inline void gen_op_evlwhos(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld16s(ctx, t0, addr);
    tcg_gen_ext32u_tl(cpu_gpr[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16s(ctx, t0, addr);
    tcg_gen_shli_tl(t0, t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    tcg_temp_free(t0);
#else
    gen_qemu_ld16s(ctx, cpu_gprh[rD(ctx->opcode)], addr);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16s(ctx, cpu_gpr[rD(ctx->opcode)], addr);
#endif
}

static inline void gen_op_evlwwsplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
    gen_qemu_ld32u(ctx, t0, addr);
#if defined(TARGET_PPC64)
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    tcg_gen_mov_tl(cpu_gprh[rD(ctx->opcode)], t0);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evlwhsplat(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 48);
    tcg_gen_shli_tl(t0, t0, 32);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
    tcg_gen_shli_tl(t0, t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t0);
#else
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gprh[rD(ctx->opcode)], t0, 16);
    tcg_gen_or_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_ld16u(ctx, t0, addr);
    tcg_gen_shli_tl(cpu_gpr[rD(ctx->opcode)], t0, 16);
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gprh[rD(ctx->opcode)], t0);
#endif
    tcg_temp_free(t0);
}

static inline void gen_op_evstdd(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    gen_qemu_st64(ctx, cpu_gpr[rS(ctx->opcode)], addr);
#else
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_gpr[rS(ctx->opcode)], cpu_gprh[rS(ctx->opcode)]);
    gen_qemu_st64(ctx, t0, addr);
    tcg_temp_free_i64(t0);
#endif
}

static inline void gen_op_evstdw(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 32);
    gen_qemu_st32(ctx, t0, addr);
    tcg_temp_free(t0);
#else
    gen_qemu_st32(ctx, cpu_gprh[rS(ctx->opcode)], addr);
#endif
    gen_addr_add(ctx, addr, addr, 4);
    gen_qemu_st32(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstdh(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 48);
#else
    tcg_gen_shri_tl(t0, cpu_gprh[rS(ctx->opcode)], 16);
#endif
    gen_qemu_st16(ctx, t0, addr);
    gen_addr_add(ctx, addr, addr, 2);
#if defined(TARGET_PPC64)
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 32);
    gen_qemu_st16(ctx, t0, addr);
#else
    gen_qemu_st16(ctx, cpu_gprh[rS(ctx->opcode)], addr);
#endif
    gen_addr_add(ctx, addr, addr, 2);
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    tcg_temp_free(t0);
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_st16(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstwhe(DisasContext *ctx, TCGv addr)
{
    TCGv t0 = tcg_temp_new();
#if defined(TARGET_PPC64)
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 48);
#else
    tcg_gen_shri_tl(t0, cpu_gprh[rS(ctx->opcode)], 16);
#endif
    gen_qemu_st16(ctx, t0, addr);
    gen_addr_add(ctx, addr, addr, 2);
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 16);
    gen_qemu_st16(ctx, t0, addr);
    tcg_temp_free(t0);
}

static inline void gen_op_evstwho(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 32);
    gen_qemu_st16(ctx, t0, addr);
    tcg_temp_free(t0);
#else
    gen_qemu_st16(ctx, cpu_gprh[rS(ctx->opcode)], addr);
#endif
    gen_addr_add(ctx, addr, addr, 2);
    gen_qemu_st16(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

static inline void gen_op_evstwwe(DisasContext *ctx, TCGv addr)
{
#if defined(TARGET_PPC64)
    TCGv t0 = tcg_temp_new();
    tcg_gen_shri_tl(t0, cpu_gpr[rS(ctx->opcode)], 32);
    gen_qemu_st32(ctx, t0, addr);
    tcg_temp_free(t0);
#else
    gen_qemu_st32(ctx, cpu_gprh[rS(ctx->opcode)], addr);
#endif
}

static inline void gen_op_evstwwo(DisasContext *ctx, TCGv addr)
{
    gen_qemu_st32(ctx, cpu_gpr[rS(ctx->opcode)], addr);
}

#define GEN_SPEOP_LDST(name, opc2, sh)                                        \
static void glue(gen_, name)(DisasContext *ctx)                                       \
{                                                                             \
    TCGv t0;                                                                  \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    t0 = tcg_temp_new();                                                      \
    if (Rc(ctx->opcode)) {                                                    \
        gen_addr_spe_imm_index(ctx, t0, sh);                                  \
    } else {                                                                  \
        gen_addr_reg_index(ctx, t0);                                          \
    }                                                                         \
    gen_op_##name(ctx, t0);                                                   \
    tcg_temp_free(t0);                                                        \
}

GEN_SPEOP_LDST(evldd, 0x00, 3);
GEN_SPEOP_LDST(evldw, 0x01, 3);
GEN_SPEOP_LDST(evldh, 0x02, 3);
GEN_SPEOP_LDST(evlhhesplat, 0x04, 1);
GEN_SPEOP_LDST(evlhhousplat, 0x06, 1);
GEN_SPEOP_LDST(evlhhossplat, 0x07, 1);
GEN_SPEOP_LDST(evlwhe, 0x08, 2);
GEN_SPEOP_LDST(evlwhou, 0x0A, 2);
GEN_SPEOP_LDST(evlwhos, 0x0B, 2);
GEN_SPEOP_LDST(evlwwsplat, 0x0C, 2);
GEN_SPEOP_LDST(evlwhsplat, 0x0E, 2);

GEN_SPEOP_LDST(evstdd, 0x10, 3);
GEN_SPEOP_LDST(evstdw, 0x11, 3);
GEN_SPEOP_LDST(evstdh, 0x12, 3);
GEN_SPEOP_LDST(evstwhe, 0x18, 2);
GEN_SPEOP_LDST(evstwho, 0x1A, 2);
GEN_SPEOP_LDST(evstwwe, 0x1C, 2);
GEN_SPEOP_LDST(evstwwo, 0x1E, 2);

/* Multiply and add - TODO */
#if 0
GEN_SPE(speundef,       evmhessf,      0x01, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);//
GEN_SPE(speundef,       evmhossf,      0x03, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumi,       evmhesmi,      0x04, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmf,      0x05, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumi,       evmhosmi,      0x06, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmf,      0x07, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfa,     0x11, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfa,     0x13, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumia,      evmhesmia,     0x14, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfa,     0x15, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumia,      evmhosmia,     0x16, 0x10, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfa,     0x17, 0x10, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(speundef,       evmwhssf,      0x03, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumi,       speundef,      0x04, 0x11, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evmwhumi,       evmwhsmi,      0x06, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmf,      0x07, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssf,       0x09, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmf,       0x0D, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhssfa,     0x13, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumia,      speundef,      0x14, 0x11, 0x00000000, 0xFFFFFFFF, PPC_SPE);
GEN_SPE(evmwhumia,      evmwhsmia,     0x16, 0x11, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwhsmfa,     0x17, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfa,      0x19, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfa,      0x1D, 0x11, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evadduiaaw,     evaddsiaaw,    0x00, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfusiaaw,   evsubfssiaaw,  0x01, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evaddumiaaw,    evaddsmiaaw,   0x04, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evsubfumiaaw,   evsubfsmiaaw,  0x05, 0x13, 0x0000F800, 0x0000F800, PPC_SPE);
GEN_SPE(evdivws,        evdivwu,       0x06, 0x13, 0x00000000, 0x00000000, PPC_SPE);

GEN_SPE(evmheusiaaw,    evmhessiaaw,   0x00, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfaaw,   0x01, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhousiaaw,    evmhossiaaw,   0x02, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfaaw,   0x03, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumiaaw,    evmhesmiaaw,   0x04, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfaaw,   0x05, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumiaaw,    evmhosmiaaw,   0x06, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfaaw,   0x07, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumiaa,    evmhegsmiaa,   0x14, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfaa,   0x15, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhogumiaa,    evmhogsmiaa,   0x16, 0x14, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfaa,   0x17, 0x14, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusiaaw,    evmwlssiaaw,   0x00, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumiaaw,    evmwlsmiaaw,   0x04, 0x15, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfaa,     0x09, 0x15, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfaa,     0x0D, 0x15, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmheusianw,    evmhessianw,   0x00, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhessfanw,   0x01, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhousianw,    evmhossianw,   0x02, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhossfanw,   0x03, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmheumianw,    evmhesmianw,   0x04, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhesmfanw,   0x05, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhoumianw,    evmhosmianw,   0x06, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhosmfanw,   0x07, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhegumian,    evmhegsmian,   0x14, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhegsmfan,   0x15, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmhigumian,    evmhigsmian,   0x16, 0x16, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmhogsmfan,   0x17, 0x16, 0xFFFFFFFF, 0x00000000, PPC_SPE);

GEN_SPE(evmwlusianw,    evmwlssianw,   0x00, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(evmwlumianw,    evmwlsmianw,   0x04, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwssfan,     0x09, 0x17, 0xFFFFFFFF, 0x00000000, PPC_SPE);
GEN_SPE(evmwumian,      evmwsmian,     0x0C, 0x17, 0x00000000, 0x00000000, PPC_SPE);
GEN_SPE(speundef,       evmwsmfan,     0x0D, 0x17, 0xFFFFFFFF, 0x00000000, PPC_SPE);
#endif

/***                      SPE floating-point extension                     ***/
#if defined(TARGET_PPC64)
#define GEN_SPEFPUOP_CONV_32_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0;                                                              \
    TCGv t1;                                                                  \
    t0 = tcg_temp_new_i32();                                                  \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(t0, cpu_env, t0);                                       \
    t1 = tcg_temp_new();                                                      \
    tcg_gen_extu_i32_tl(t1, t0);                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)],       \
                    0xFFFFFFFF00000000ULL);                                   \
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t1);    \
    tcg_temp_free(t1);                                                        \
}
#define GEN_SPEFPUOP_CONV_32_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0;                                                              \
    TCGv t1;                                                                  \
    t0 = tcg_temp_new_i32();                                                  \
    gen_helper_##name(t0, cpu_env, cpu_gpr[rB(ctx->opcode)]);                 \
    t1 = tcg_temp_new();                                                      \
    tcg_gen_extu_i32_tl(t1, t0);                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)],       \
                    0xFFFFFFFF00000000ULL);                                   \
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t1);    \
    tcg_temp_free(t1);                                                        \
}
#define GEN_SPEFPUOP_CONV_64_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0 = tcg_temp_new_i32();                                         \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);                 \
    tcg_temp_free_i32(t0);                                                    \
}
#define GEN_SPEFPUOP_CONV_64_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env,                      \
                      cpu_gpr[rB(ctx->opcode)]);                              \
}
#define GEN_SPEFPUOP_ARITH2_32_32(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0, t1;                                                          \
    TCGv_i64 t2;                                                              \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
    t1 = tcg_temp_new_i32();                                                  \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(t0, cpu_env, t0, t1);                                   \
    tcg_temp_free_i32(t1);                                                    \
    t2 = tcg_temp_new();                                                      \
    tcg_gen_extu_i32_tl(t2, t0);                                              \
    tcg_temp_free_i32(t0);                                                    \
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)],       \
                    0xFFFFFFFF00000000ULL);                                   \
    tcg_gen_or_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rD(ctx->opcode)], t2);    \
    tcg_temp_free(t2);                                                        \
}
#define GEN_SPEFPUOP_ARITH2_64_64(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env,                      \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);    \
}
#define GEN_SPEFPUOP_COMP_32(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i32 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i32();                                                  \
    t1 = tcg_temp_new_i32();                                                  \
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);                       \
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);                       \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env, t0, t1);           \
    tcg_temp_free_i32(t0);                                                    \
    tcg_temp_free_i32(t1);                                                    \
}
#define GEN_SPEFPUOP_COMP_64(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env,                    \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);    \
}
#else
#define GEN_SPEFPUOP_CONV_32_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env,                      \
                      cpu_gpr[rB(ctx->opcode)]);                              \
}
#define GEN_SPEFPUOP_CONV_32_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0 = tcg_temp_new_i64();                                         \
    gen_load_gpr64(t0, rB(ctx->opcode));                                      \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);                 \
    tcg_temp_free_i64(t0);                                                    \
}
#define GEN_SPEFPUOP_CONV_64_32(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0 = tcg_temp_new_i64();                                         \
    gen_helper_##name(t0, cpu_env, cpu_gpr[rB(ctx->opcode)]);                 \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
}
#define GEN_SPEFPUOP_CONV_64_64(name)                                         \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0 = tcg_temp_new_i64();                                         \
    gen_load_gpr64(t0, rB(ctx->opcode));                                      \
    gen_helper_##name(t0, cpu_env, t0);                                       \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
}
#define GEN_SPEFPUOP_ARITH2_32_32(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_helper_##name(cpu_gpr[rD(ctx->opcode)], cpu_env,                      \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);    \
}
#define GEN_SPEFPUOP_ARITH2_64_64(name)                                       \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    gen_load_gpr64(t0, rA(ctx->opcode));                                      \
    gen_load_gpr64(t1, rB(ctx->opcode));                                      \
    gen_helper_##name(t0, cpu_env, t0, t1);                                   \
    gen_store_gpr64(rD(ctx->opcode), t0);                                     \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}
#define GEN_SPEFPUOP_COMP_32(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env,                    \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);    \
}
#define GEN_SPEFPUOP_COMP_64(name)                                            \
static inline void gen_##name(DisasContext *ctx)                              \
{                                                                             \
    TCGv_i64 t0, t1;                                                          \
    if (unlikely(!ctx->spe_enabled)) {                                        \
        gen_exception(ctx, POWERPC_EXCP_SPEU);                                \
        return;                                                               \
    }                                                                         \
    t0 = tcg_temp_new_i64();                                                  \
    t1 = tcg_temp_new_i64();                                                  \
    gen_load_gpr64(t0, rA(ctx->opcode));                                      \
    gen_load_gpr64(t1, rB(ctx->opcode));                                      \
    gen_helper_##name(cpu_crf[crfD(ctx->opcode)], cpu_env, t0, t1);           \
    tcg_temp_free_i64(t0);                                                    \
    tcg_temp_free_i64(t1);                                                    \
}
#endif

/* Single precision floating-point vectors operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_64_64(evfsadd);
GEN_SPEFPUOP_ARITH2_64_64(evfssub);
GEN_SPEFPUOP_ARITH2_64_64(evfsmul);
GEN_SPEFPUOP_ARITH2_64_64(evfsdiv);
static inline void gen_evfsabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], ~0x8000000080000000LL);
#else
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], ~0x80000000);
    tcg_gen_andi_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], ~0x80000000);
#endif
}
static inline void gen_evfsnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x8000000080000000LL);
#else
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x80000000);
    tcg_gen_ori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], 0x80000000);
#endif
}
static inline void gen_evfsneg(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x8000000080000000LL);
#else
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x80000000);
    tcg_gen_xori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], 0x80000000);
#endif
}

/* Conversion */
GEN_SPEFPUOP_CONV_64_64(evfscfui);
GEN_SPEFPUOP_CONV_64_64(evfscfsi);
GEN_SPEFPUOP_CONV_64_64(evfscfuf);
GEN_SPEFPUOP_CONV_64_64(evfscfsf);
GEN_SPEFPUOP_CONV_64_64(evfsctui);
GEN_SPEFPUOP_CONV_64_64(evfsctsi);
GEN_SPEFPUOP_CONV_64_64(evfsctuf);
GEN_SPEFPUOP_CONV_64_64(evfsctsf);
GEN_SPEFPUOP_CONV_64_64(evfsctuiz);
GEN_SPEFPUOP_CONV_64_64(evfsctsiz);

/* Comparison */
GEN_SPEFPUOP_COMP_64(evfscmpgt);
GEN_SPEFPUOP_COMP_64(evfscmplt);
GEN_SPEFPUOP_COMP_64(evfscmpeq);
GEN_SPEFPUOP_COMP_64(evfststgt);
GEN_SPEFPUOP_COMP_64(evfststlt);
GEN_SPEFPUOP_COMP_64(evfststeq);

/* Opcodes definitions */
GEN_SPE(evfsadd,   evfssub,   0x00, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(evfsabs,   evfsnabs,  0x02, 0x0A, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE); //
GEN_SPE(evfsneg,   speundef,  0x03, 0x0A, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfsmul,   evfsdiv,   0x04, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(evfscmpgt, evfscmplt, 0x06, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(evfscmpeq, speundef,  0x07, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfscfui,  evfscfsi,  0x08, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfscfuf,  evfscfsf,  0x09, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctui,  evfsctsi,  0x0A, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctuf,  evfsctsf,  0x0B, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(evfsctuiz, speundef,  0x0C, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfsctsiz, speundef,  0x0D, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(evfststgt, evfststlt, 0x0E, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(evfststeq, speundef,  0x0F, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //

/* Single precision floating-point operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_32_32(efsadd);
GEN_SPEFPUOP_ARITH2_32_32(efssub);
GEN_SPEFPUOP_ARITH2_32_32(efsmul);
GEN_SPEFPUOP_ARITH2_32_32(efsdiv);
static inline void gen_efsabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], (target_long)~0x80000000LL);
}
static inline void gen_efsnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x80000000);
}
static inline void gen_efsneg(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x80000000);
}

/* Conversion */
GEN_SPEFPUOP_CONV_32_32(efscfui);
GEN_SPEFPUOP_CONV_32_32(efscfsi);
GEN_SPEFPUOP_CONV_32_32(efscfuf);
GEN_SPEFPUOP_CONV_32_32(efscfsf);
GEN_SPEFPUOP_CONV_32_32(efsctui);
GEN_SPEFPUOP_CONV_32_32(efsctsi);
GEN_SPEFPUOP_CONV_32_32(efsctuf);
GEN_SPEFPUOP_CONV_32_32(efsctsf);
GEN_SPEFPUOP_CONV_32_32(efsctuiz);
GEN_SPEFPUOP_CONV_32_32(efsctsiz);
GEN_SPEFPUOP_CONV_32_64(efscfd);

/* Comparison */
GEN_SPEFPUOP_COMP_32(efscmpgt);
GEN_SPEFPUOP_COMP_32(efscmplt);
GEN_SPEFPUOP_COMP_32(efscmpeq);
GEN_SPEFPUOP_COMP_32(efststgt);
GEN_SPEFPUOP_COMP_32(efststlt);
GEN_SPEFPUOP_COMP_32(efststeq);

/* Opcodes definitions */
GEN_SPE(efsadd,   efssub,   0x00, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(efsabs,   efsnabs,  0x02, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE); //
GEN_SPE(efsneg,   speundef, 0x03, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efsmul,   efsdiv,   0x04, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE); //
GEN_SPE(efscmpgt, efscmplt, 0x06, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(efscmpeq, efscfd,   0x07, 0x0B, 0x00600000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efscfui,  efscfsi,  0x08, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efscfuf,  efscfsf,  0x09, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctui,  efsctsi,  0x0A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctuf,  efsctsf,  0x0B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE); //
GEN_SPE(efsctuiz, speundef, 0x0C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efsctsiz, speundef, 0x0D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE); //
GEN_SPE(efststgt, efststlt, 0x0E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE); //
GEN_SPE(efststeq, speundef, 0x0F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE); //

/* Double precision floating-point operations */
/* Arithmetic */
GEN_SPEFPUOP_ARITH2_64_64(efdadd);
GEN_SPEFPUOP_ARITH2_64_64(efdsub);
GEN_SPEFPUOP_ARITH2_64_64(efdmul);
GEN_SPEFPUOP_ARITH2_64_64(efddiv);
static inline void gen_efdabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_andi_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], ~0x8000000000000000LL);
#else
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_andi_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], ~0x80000000);
#endif
}
static inline void gen_efdnabs(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_ori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x8000000000000000LL);
#else
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], 0x80000000);
#endif
}
static inline void gen_efdneg(DisasContext *ctx)
{
    if (unlikely(!ctx->spe_enabled)) {
        gen_exception(ctx, POWERPC_EXCP_SPEU);
        return;
    }
#if defined(TARGET_PPC64)
    tcg_gen_xori_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 0x8000000000000000LL);
#else
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_xori_tl(cpu_gprh[rD(ctx->opcode)], cpu_gprh[rA(ctx->opcode)], 0x80000000);
#endif
}

/* Conversion */
GEN_SPEFPUOP_CONV_64_32(efdcfui);
GEN_SPEFPUOP_CONV_64_32(efdcfsi);
GEN_SPEFPUOP_CONV_64_32(efdcfuf);
GEN_SPEFPUOP_CONV_64_32(efdcfsf);
GEN_SPEFPUOP_CONV_32_64(efdctui);
GEN_SPEFPUOP_CONV_32_64(efdctsi);
GEN_SPEFPUOP_CONV_32_64(efdctuf);
GEN_SPEFPUOP_CONV_32_64(efdctsf);
GEN_SPEFPUOP_CONV_32_64(efdctuiz);
GEN_SPEFPUOP_CONV_32_64(efdctsiz);
GEN_SPEFPUOP_CONV_64_32(efdcfs);
GEN_SPEFPUOP_CONV_64_64(efdcfuid);
GEN_SPEFPUOP_CONV_64_64(efdcfsid);
GEN_SPEFPUOP_CONV_64_64(efdctuidz);
GEN_SPEFPUOP_CONV_64_64(efdctsidz);

/* Comparison */
GEN_SPEFPUOP_COMP_64(efdcmpgt);
GEN_SPEFPUOP_COMP_64(efdcmplt);
GEN_SPEFPUOP_COMP_64(efdcmpeq);
GEN_SPEFPUOP_COMP_64(efdtstgt);
GEN_SPEFPUOP_COMP_64(efdtstlt);
GEN_SPEFPUOP_COMP_64(efdtsteq);

/* Opcodes definitions */
GEN_SPE(efdadd,    efdsub,    0x10, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfuid,  efdcfsid,  0x11, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdabs,    efdnabs,   0x12, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_DOUBLE); //
GEN_SPE(efdneg,    speundef,  0x13, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdmul,    efddiv,    0x14, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuidz, efdctsidz, 0x15, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcmpgt,  efdcmplt,  0x16, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcmpeq,  efdcfs,    0x17, 0x0B, 0x00600000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfui,   efdcfsi,   0x18, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdcfuf,   efdcfsf,   0x19, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctui,   efdctsi,   0x1A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuf,   efdctsf,   0x1B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE); //
GEN_SPE(efdctuiz,  speundef,  0x1C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdctsiz,  speundef,  0x1D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //
GEN_SPE(efdtstgt,  efdtstlt,  0x1E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE); //
GEN_SPE(efdtsteq,  speundef,  0x1F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_DOUBLE); //

static opcode_t opcodes[] = {
GEN_HANDLER(invalid, 0x00, 0x00, 0x00, 0xFFFFFFFF, PPC_NONE),
GEN_HANDLER(cmp, 0x1F, 0x00, 0x00, 0x00400000, PPC_INTEGER),
GEN_HANDLER(cmpi, 0x0B, 0xFF, 0xFF, 0x00400000, PPC_INTEGER),
GEN_HANDLER(cmpl, 0x1F, 0x00, 0x01, 0x00400000, PPC_INTEGER),
GEN_HANDLER(cmpli, 0x0A, 0xFF, 0xFF, 0x00400000, PPC_INTEGER),
GEN_HANDLER_E(cmpb, 0x1F, 0x1C, 0x0F, 0x00000001, PPC_NONE, PPC2_ISA205),
GEN_HANDLER(isel, 0x1F, 0x0F, 0xFF, 0x00000001, PPC_ISEL),
GEN_HANDLER(addi, 0x0E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(addic, 0x0C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER2(addic_, "addic.", 0x0D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(addis, 0x0F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(mulhw, 0x1F, 0x0B, 0x02, 0x00000400, PPC_INTEGER),
GEN_HANDLER(mulhwu, 0x1F, 0x0B, 0x00, 0x00000400, PPC_INTEGER),
GEN_HANDLER(mullw, 0x1F, 0x0B, 0x07, 0x00000000, PPC_INTEGER),
GEN_HANDLER(mullwo, 0x1F, 0x0B, 0x17, 0x00000000, PPC_INTEGER),
GEN_HANDLER(mulli, 0x07, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
#if defined(TARGET_PPC64)
GEN_HANDLER(mulld, 0x1F, 0x09, 0x07, 0x00000000, PPC_64B),
#endif
GEN_HANDLER(neg, 0x1F, 0x08, 0x03, 0x0000F800, PPC_INTEGER),
GEN_HANDLER(nego, 0x1F, 0x08, 0x13, 0x0000F800, PPC_INTEGER),
GEN_HANDLER(subfic, 0x08, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER2(andi_, "andi.", 0x1C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER2(andis_, "andis.", 0x1D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(cntlzw, 0x1F, 0x1A, 0x00, 0x00000000, PPC_INTEGER),
GEN_HANDLER(or, 0x1F, 0x1C, 0x0D, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xor, 0x1F, 0x1C, 0x09, 0x00000000, PPC_INTEGER),
GEN_HANDLER(ori, 0x18, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(oris, 0x19, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xori, 0x1A, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xoris, 0x1B, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(popcntb, 0x1F, 0x03, 0x03, 0x0000F801, PPC_POPCNTB),
GEN_HANDLER(popcntw, 0x1F, 0x1A, 0x0b, 0x0000F801, PPC_POPCNTWD),
GEN_HANDLER_E(prtyw, 0x1F, 0x1A, 0x04, 0x0000F801, PPC_NONE, PPC2_ISA205),
#if defined(TARGET_PPC64)
GEN_HANDLER(popcntd, 0x1F, 0x1A, 0x0F, 0x0000F801, PPC_POPCNTWD),
GEN_HANDLER(cntlzd, 0x1F, 0x1A, 0x01, 0x00000000, PPC_64B),
GEN_HANDLER_E(prtyd, 0x1F, 0x1A, 0x05, 0x0000F801, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(bpermd, 0x1F, 0x1C, 0x07, 0x00000001, PPC_NONE, PPC2_PERM_ISA206),
#endif
GEN_HANDLER(rlwimi, 0x14, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(rlwinm, 0x15, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(rlwnm, 0x17, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(slw, 0x1F, 0x18, 0x00, 0x00000000, PPC_INTEGER),
GEN_HANDLER(sraw, 0x1F, 0x18, 0x18, 0x00000000, PPC_INTEGER),
GEN_HANDLER(srawi, 0x1F, 0x18, 0x19, 0x00000000, PPC_INTEGER),
GEN_HANDLER(srw, 0x1F, 0x18, 0x10, 0x00000000, PPC_INTEGER),
#if defined(TARGET_PPC64)
GEN_HANDLER(sld, 0x1F, 0x1B, 0x00, 0x00000000, PPC_64B),
GEN_HANDLER(srad, 0x1F, 0x1A, 0x18, 0x00000000, PPC_64B),
GEN_HANDLER2(sradi0, "sradi", 0x1F, 0x1A, 0x19, 0x00000000, PPC_64B),
GEN_HANDLER2(sradi1, "sradi", 0x1F, 0x1B, 0x19, 0x00000000, PPC_64B),
GEN_HANDLER(srd, 0x1F, 0x1B, 0x10, 0x00000000, PPC_64B),
#endif
GEN_HANDLER(frsqrtes, 0x3B, 0x1A, 0xFF, 0x001F07C0, PPC_FLOAT_FRSQRTES),
GEN_HANDLER(fsqrt, 0x3F, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_FSQRT),
GEN_HANDLER(fsqrts, 0x3B, 0x16, 0xFF, 0x001F07C0, PPC_FLOAT_FSQRT),
GEN_HANDLER(fcmpo, 0x3F, 0x00, 0x01, 0x00600001, PPC_FLOAT),
GEN_HANDLER(fcmpu, 0x3F, 0x00, 0x00, 0x00600001, PPC_FLOAT),
GEN_HANDLER(fabs, 0x3F, 0x08, 0x08, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fmr, 0x3F, 0x08, 0x02, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fnabs, 0x3F, 0x08, 0x04, 0x001F0000, PPC_FLOAT),
GEN_HANDLER(fneg, 0x3F, 0x08, 0x01, 0x001F0000, PPC_FLOAT),
GEN_HANDLER_E(fcpsgn, 0x3F, 0x08, 0x00, 0x00000000, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(fmrgew, 0x3F, 0x06, 0x1E, 0x00000001, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(fmrgow, 0x3F, 0x06, 0x1A, 0x00000001, PPC_NONE, PPC2_VSX207),
GEN_HANDLER(mcrfs, 0x3F, 0x00, 0x02, 0x0063F801, PPC_FLOAT),
GEN_HANDLER(mffs, 0x3F, 0x07, 0x12, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsb0, 0x3F, 0x06, 0x02, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsb1, 0x3F, 0x06, 0x01, 0x001FF800, PPC_FLOAT),
GEN_HANDLER(mtfsf, 0x3F, 0x07, 0x16, 0x00000000, PPC_FLOAT),
GEN_HANDLER(mtfsfi, 0x3F, 0x06, 0x04, 0x006e0800, PPC_FLOAT),
#if defined(TARGET_PPC64)
GEN_HANDLER(ld, 0x3A, 0xFF, 0xFF, 0x00000000, PPC_64B),
GEN_HANDLER(lq, 0x38, 0xFF, 0xFF, 0x00000000, PPC_64BX),
GEN_HANDLER(std, 0x3E, 0xFF, 0xFF, 0x00000000, PPC_64B),
#endif
GEN_HANDLER(lmw, 0x2E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(stmw, 0x2F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(lswi, 0x1F, 0x15, 0x12, 0x00000001, PPC_STRING),
GEN_HANDLER(lswx, 0x1F, 0x15, 0x10, 0x00000001, PPC_STRING),
GEN_HANDLER(stswi, 0x1F, 0x15, 0x16, 0x00000001, PPC_STRING),
GEN_HANDLER(stswx, 0x1F, 0x15, 0x14, 0x00000001, PPC_STRING),
GEN_HANDLER(eieio, 0x1F, 0x16, 0x1A, 0x03FFF801, PPC_MEM_EIEIO),
GEN_HANDLER(isync, 0x13, 0x16, 0x04, 0x03FFF801, PPC_MEM),
GEN_HANDLER_E(lbarx, 0x1F, 0x14, 0x01, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER_E(lharx, 0x1F, 0x14, 0x03, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER(lwarx, 0x1F, 0x14, 0x00, 0x00000000, PPC_RES),
GEN_HANDLER_E(stbcx_, 0x1F, 0x16, 0x15, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER_E(sthcx_, 0x1F, 0x16, 0x16, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER2(stwcx_, "stwcx.", 0x1F, 0x16, 0x04, 0x00000000, PPC_RES),
#if defined(TARGET_PPC64)
GEN_HANDLER(ldarx, 0x1F, 0x14, 0x02, 0x00000000, PPC_64B),
GEN_HANDLER_E(lqarx, 0x1F, 0x14, 0x08, 0, PPC_NONE, PPC2_LSQ_ISA207),
GEN_HANDLER2(stdcx_, "stdcx.", 0x1F, 0x16, 0x06, 0x00000000, PPC_64B),
GEN_HANDLER_E(stqcx_, 0x1F, 0x16, 0x05, 0, PPC_NONE, PPC2_LSQ_ISA207),
#endif
GEN_HANDLER(sync, 0x1F, 0x16, 0x12, 0x039FF801, PPC_MEM_SYNC),
GEN_HANDLER(wait, 0x1F, 0x1E, 0x01, 0x03FFF801, PPC_WAIT),
GEN_HANDLER(b, 0x12, 0xFF, 0xFF, 0x00000000, PPC_FLOW),
GEN_HANDLER(bc, 0x10, 0xFF, 0xFF, 0x00000000, PPC_FLOW),
GEN_HANDLER(bcctr, 0x13, 0x10, 0x10, 0x00000000, PPC_FLOW),
GEN_HANDLER(bclr, 0x13, 0x10, 0x00, 0x00000000, PPC_FLOW),
GEN_HANDLER_E(bctar, 0x13, 0x10, 0x11, 0, PPC_NONE, PPC2_BCTAR_ISA207),
GEN_HANDLER(mcrf, 0x13, 0x00, 0xFF, 0x00000001, PPC_INTEGER),
GEN_HANDLER(rfi, 0x13, 0x12, 0x01, 0x03FF8001, PPC_FLOW),
#if defined(TARGET_PPC64)
GEN_HANDLER(rfid, 0x13, 0x12, 0x00, 0x03FF8001, PPC_64B),
GEN_HANDLER(hrfid, 0x13, 0x12, 0x08, 0x03FF8001, PPC_64H),
#endif
GEN_HANDLER(sc, 0x11, 0xFF, 0xFF, 0x03FFF01D, PPC_FLOW),
GEN_HANDLER(tw, 0x1F, 0x04, 0x00, 0x00000001, PPC_FLOW),
GEN_HANDLER(twi, 0x03, 0xFF, 0xFF, 0x00000000, PPC_FLOW),
#if defined(TARGET_PPC64)
GEN_HANDLER(td, 0x1F, 0x04, 0x02, 0x00000001, PPC_64B),
GEN_HANDLER(tdi, 0x02, 0xFF, 0xFF, 0x00000000, PPC_64B),
#endif
GEN_HANDLER(mcrxr, 0x1F, 0x00, 0x10, 0x007FF801, PPC_MISC),
GEN_HANDLER(mfcr, 0x1F, 0x13, 0x00, 0x00000801, PPC_MISC),
GEN_HANDLER(mfmsr, 0x1F, 0x13, 0x02, 0x001FF801, PPC_MISC),
GEN_HANDLER(mfspr, 0x1F, 0x13, 0x0A, 0x00000001, PPC_MISC),
GEN_HANDLER(mftb, 0x1F, 0x13, 0x0B, 0x00000001, PPC_MFTB),
GEN_HANDLER(mtcrf, 0x1F, 0x10, 0x04, 0x00000801, PPC_MISC),
#if defined(TARGET_PPC64)
GEN_HANDLER(mtmsrd, 0x1F, 0x12, 0x05, 0x001EF801, PPC_64B),
#endif
GEN_HANDLER(mtmsr, 0x1F, 0x12, 0x04, 0x001FF801, PPC_MISC),
GEN_HANDLER(mtspr, 0x1F, 0x13, 0x0E, 0x00000001, PPC_MISC),
GEN_HANDLER(dcbf, 0x1F, 0x16, 0x02, 0x03C00001, PPC_CACHE),
GEN_HANDLER(dcbi, 0x1F, 0x16, 0x0E, 0x03E00001, PPC_CACHE),
GEN_HANDLER(dcbst, 0x1F, 0x16, 0x01, 0x03E00001, PPC_CACHE),
GEN_HANDLER(dcbt, 0x1F, 0x16, 0x08, 0x00000001, PPC_CACHE),
GEN_HANDLER(dcbtst, 0x1F, 0x16, 0x07, 0x00000001, PPC_CACHE),
GEN_HANDLER(dcbz, 0x1F, 0x16, 0x1F, 0x03C00001, PPC_CACHE_DCBZ),
GEN_HANDLER(dst, 0x1F, 0x16, 0x0A, 0x01800001, PPC_ALTIVEC),
GEN_HANDLER(dstst, 0x1F, 0x16, 0x0B, 0x02000001, PPC_ALTIVEC),
GEN_HANDLER(dss, 0x1F, 0x16, 0x19, 0x019FF801, PPC_ALTIVEC),
GEN_HANDLER(icbi, 0x1F, 0x16, 0x1E, 0x03E00001, PPC_CACHE_ICBI),
GEN_HANDLER(dcba, 0x1F, 0x16, 0x17, 0x03E00001, PPC_CACHE_DCBA),
GEN_HANDLER(mfsr, 0x1F, 0x13, 0x12, 0x0010F801, PPC_SEGMENT),
GEN_HANDLER(mfsrin, 0x1F, 0x13, 0x14, 0x001F0001, PPC_SEGMENT),
GEN_HANDLER(mtsr, 0x1F, 0x12, 0x06, 0x0010F801, PPC_SEGMENT),
GEN_HANDLER(mtsrin, 0x1F, 0x12, 0x07, 0x001F0001, PPC_SEGMENT),
#if defined(TARGET_PPC64)
GEN_HANDLER2(mfsr_64b, "mfsr", 0x1F, 0x13, 0x12, 0x0010F801, PPC_SEGMENT_64B),
GEN_HANDLER2(mfsrin_64b, "mfsrin", 0x1F, 0x13, 0x14, 0x001F0001,
             PPC_SEGMENT_64B),
GEN_HANDLER2(mtsr_64b, "mtsr", 0x1F, 0x12, 0x06, 0x0010F801, PPC_SEGMENT_64B),
GEN_HANDLER2(mtsrin_64b, "mtsrin", 0x1F, 0x12, 0x07, 0x001F0001,
             PPC_SEGMENT_64B),
GEN_HANDLER2(slbmte, "slbmte", 0x1F, 0x12, 0x0C, 0x001F0001, PPC_SEGMENT_64B),
GEN_HANDLER2(slbmfee, "slbmfee", 0x1F, 0x13, 0x1C, 0x001F0001, PPC_SEGMENT_64B),
GEN_HANDLER2(slbmfev, "slbmfev", 0x1F, 0x13, 0x1A, 0x001F0001, PPC_SEGMENT_64B),
#endif
GEN_HANDLER(tlbia, 0x1F, 0x12, 0x0B, 0x03FFFC01, PPC_MEM_TLBIA),
GEN_HANDLER(tlbiel, 0x1F, 0x12, 0x08, 0x03FF0001, PPC_MEM_TLBIE),
GEN_HANDLER(tlbie, 0x1F, 0x12, 0x09, 0x03FF0001, PPC_MEM_TLBIE),
GEN_HANDLER(tlbsync, 0x1F, 0x16, 0x11, 0x03FFF801, PPC_MEM_TLBSYNC),
#if defined(TARGET_PPC64)
GEN_HANDLER(slbia, 0x1F, 0x12, 0x0F, 0x03FFFC01, PPC_SLBI),
GEN_HANDLER(slbie, 0x1F, 0x12, 0x0D, 0x03FF0001, PPC_SLBI),
#endif
GEN_HANDLER(eciwx, 0x1F, 0x16, 0x0D, 0x00000001, PPC_EXTERN),
GEN_HANDLER(ecowx, 0x1F, 0x16, 0x09, 0x00000001, PPC_EXTERN),
GEN_HANDLER(abs, 0x1F, 0x08, 0x0B, 0x0000F800, PPC_POWER_BR),
GEN_HANDLER(abso, 0x1F, 0x08, 0x1B, 0x0000F800, PPC_POWER_BR),
GEN_HANDLER(clcs, 0x1F, 0x10, 0x13, 0x0000F800, PPC_POWER_BR),
GEN_HANDLER(div, 0x1F, 0x0B, 0x0A, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(divo, 0x1F, 0x0B, 0x1A, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(divs, 0x1F, 0x0B, 0x0B, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(divso, 0x1F, 0x0B, 0x1B, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(doz, 0x1F, 0x08, 0x08, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(dozo, 0x1F, 0x08, 0x18, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(dozi, 0x09, 0xFF, 0xFF, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(lscbx, 0x1F, 0x15, 0x08, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(maskg, 0x1F, 0x1D, 0x00, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(maskir, 0x1F, 0x1D, 0x10, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(mul, 0x1F, 0x0B, 0x03, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(mulo, 0x1F, 0x0B, 0x13, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(nabs, 0x1F, 0x08, 0x0F, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(nabso, 0x1F, 0x08, 0x1F, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(rlmi, 0x16, 0xFF, 0xFF, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(rrib, 0x1F, 0x19, 0x10, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sle, 0x1F, 0x19, 0x04, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sleq, 0x1F, 0x19, 0x06, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sliq, 0x1F, 0x18, 0x05, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(slliq, 0x1F, 0x18, 0x07, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sllq, 0x1F, 0x18, 0x06, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(slq, 0x1F, 0x18, 0x04, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sraiq, 0x1F, 0x18, 0x1D, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sraq, 0x1F, 0x18, 0x1C, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sre, 0x1F, 0x19, 0x14, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(srea, 0x1F, 0x19, 0x1C, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sreq, 0x1F, 0x19, 0x16, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(sriq, 0x1F, 0x18, 0x15, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(srliq, 0x1F, 0x18, 0x17, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(srlq, 0x1F, 0x18, 0x16, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(srq, 0x1F, 0x18, 0x14, 0x00000000, PPC_POWER_BR),
GEN_HANDLER(dsa, 0x1F, 0x14, 0x13, 0x03FFF801, PPC_602_SPEC),
GEN_HANDLER(esa, 0x1F, 0x14, 0x12, 0x03FFF801, PPC_602_SPEC),
GEN_HANDLER(mfrom, 0x1F, 0x09, 0x08, 0x03E0F801, PPC_602_SPEC),
GEN_HANDLER2(tlbld_6xx, "tlbld", 0x1F, 0x12, 0x1E, 0x03FF0001, PPC_6xx_TLB),
GEN_HANDLER2(tlbli_6xx, "tlbli", 0x1F, 0x12, 0x1F, 0x03FF0001, PPC_6xx_TLB),
GEN_HANDLER2(tlbld_74xx, "tlbld", 0x1F, 0x12, 0x1E, 0x03FF0001, PPC_74xx_TLB),
GEN_HANDLER2(tlbli_74xx, "tlbli", 0x1F, 0x12, 0x1F, 0x03FF0001, PPC_74xx_TLB),
GEN_HANDLER(clf, 0x1F, 0x16, 0x03, 0x03E00000, PPC_POWER),
GEN_HANDLER(cli, 0x1F, 0x16, 0x0F, 0x03E00000, PPC_POWER),
GEN_HANDLER(dclst, 0x1F, 0x16, 0x13, 0x03E00000, PPC_POWER),
GEN_HANDLER(mfsri, 0x1F, 0x13, 0x13, 0x00000001, PPC_POWER),
GEN_HANDLER(rac, 0x1F, 0x12, 0x19, 0x00000001, PPC_POWER),
GEN_HANDLER(rfsvc, 0x13, 0x12, 0x02, 0x03FFF0001, PPC_POWER),
GEN_HANDLER(lfq, 0x38, 0xFF, 0xFF, 0x00000003, PPC_POWER2),
GEN_HANDLER(lfqu, 0x39, 0xFF, 0xFF, 0x00000003, PPC_POWER2),
GEN_HANDLER(lfqux, 0x1F, 0x17, 0x19, 0x00000001, PPC_POWER2),
GEN_HANDLER(lfqx, 0x1F, 0x17, 0x18, 0x00000001, PPC_POWER2),
GEN_HANDLER(stfq, 0x3C, 0xFF, 0xFF, 0x00000003, PPC_POWER2),
GEN_HANDLER(stfqu, 0x3D, 0xFF, 0xFF, 0x00000003, PPC_POWER2),
GEN_HANDLER(stfqux, 0x1F, 0x17, 0x1D, 0x00000001, PPC_POWER2),
GEN_HANDLER(stfqx, 0x1F, 0x17, 0x1C, 0x00000001, PPC_POWER2),
GEN_HANDLER(mfapidi, 0x1F, 0x13, 0x08, 0x0000F801, PPC_MFAPIDI),
GEN_HANDLER(tlbiva, 0x1F, 0x12, 0x18, 0x03FFF801, PPC_TLBIVA),
GEN_HANDLER(mfdcr, 0x1F, 0x03, 0x0A, 0x00000001, PPC_DCR),
GEN_HANDLER(mtdcr, 0x1F, 0x03, 0x0E, 0x00000001, PPC_DCR),
GEN_HANDLER(mfdcrx, 0x1F, 0x03, 0x08, 0x00000000, PPC_DCRX),
GEN_HANDLER(mtdcrx, 0x1F, 0x03, 0x0C, 0x00000000, PPC_DCRX),
GEN_HANDLER(mfdcrux, 0x1F, 0x03, 0x09, 0x00000000, PPC_DCRUX),
GEN_HANDLER(mtdcrux, 0x1F, 0x03, 0x0D, 0x00000000, PPC_DCRUX),
GEN_HANDLER(dccci, 0x1F, 0x06, 0x0E, 0x03E00001, PPC_4xx_COMMON),
GEN_HANDLER(dcread, 0x1F, 0x06, 0x0F, 0x00000001, PPC_4xx_COMMON),
GEN_HANDLER2(icbt_40x, "icbt", 0x1F, 0x06, 0x08, 0x03E00001, PPC_40x_ICBT),
GEN_HANDLER(iccci, 0x1F, 0x06, 0x1E, 0x00000001, PPC_4xx_COMMON),
GEN_HANDLER(icread, 0x1F, 0x06, 0x1F, 0x03E00001, PPC_4xx_COMMON),
GEN_HANDLER2(rfci_40x, "rfci", 0x13, 0x13, 0x01, 0x03FF8001, PPC_40x_EXCP),
GEN_HANDLER_E(rfci, 0x13, 0x13, 0x01, 0x03FF8001, PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER(rfdi, 0x13, 0x07, 0x01, 0x03FF8001, PPC_RFDI),
GEN_HANDLER(rfmci, 0x13, 0x06, 0x01, 0x03FF8001, PPC_RFMCI),
GEN_HANDLER2(tlbre_40x, "tlbre", 0x1F, 0x12, 0x1D, 0x00000001, PPC_40x_TLB),
GEN_HANDLER2(tlbsx_40x, "tlbsx", 0x1F, 0x12, 0x1C, 0x00000000, PPC_40x_TLB),
GEN_HANDLER2(tlbwe_40x, "tlbwe", 0x1F, 0x12, 0x1E, 0x00000001, PPC_40x_TLB),
GEN_HANDLER2(tlbre_440, "tlbre", 0x1F, 0x12, 0x1D, 0x00000001, PPC_BOOKE),
GEN_HANDLER2(tlbsx_440, "tlbsx", 0x1F, 0x12, 0x1C, 0x00000000, PPC_BOOKE),
GEN_HANDLER2(tlbwe_440, "tlbwe", 0x1F, 0x12, 0x1E, 0x00000001, PPC_BOOKE),
GEN_HANDLER2_E(tlbre_booke206, "tlbre", 0x1F, 0x12, 0x1D, 0x00000001,
               PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER2_E(tlbsx_booke206, "tlbsx", 0x1F, 0x12, 0x1C, 0x00000000,
               PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER2_E(tlbwe_booke206, "tlbwe", 0x1F, 0x12, 0x1E, 0x00000001,
               PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER2_E(tlbivax_booke206, "tlbivax", 0x1F, 0x12, 0x18, 0x00000001,
               PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER2_E(tlbilx_booke206, "tlbilx", 0x1F, 0x12, 0x00, 0x03800001,
               PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER2_E(msgsnd, "msgsnd", 0x1F, 0x0E, 0x06, 0x03ff0001,
               PPC_NONE, PPC2_PRCNTL),
GEN_HANDLER2_E(msgclr, "msgclr", 0x1F, 0x0E, 0x07, 0x03ff0001,
               PPC_NONE, PPC2_PRCNTL),
GEN_HANDLER(wrtee, 0x1F, 0x03, 0x04, 0x000FFC01, PPC_WRTEE),
GEN_HANDLER(wrteei, 0x1F, 0x03, 0x05, 0x000E7C01, PPC_WRTEE),
GEN_HANDLER(dlmzb, 0x1F, 0x0E, 0x02, 0x00000000, PPC_440_SPEC),
GEN_HANDLER_E(mbar, 0x1F, 0x16, 0x1a, 0x001FF801,
              PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER(msync_4xx, 0x1F, 0x16, 0x12, 0x03FFF801, PPC_BOOKE),
GEN_HANDLER2_E(icbt_440, "icbt", 0x1F, 0x16, 0x00, 0x03E00001,
               PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER(lvsl, 0x1f, 0x06, 0x00, 0x00000001, PPC_ALTIVEC),
GEN_HANDLER(lvsr, 0x1f, 0x06, 0x01, 0x00000001, PPC_ALTIVEC),
GEN_HANDLER(mfvscr, 0x04, 0x2, 0x18, 0x001ff800, PPC_ALTIVEC),
GEN_HANDLER(mtvscr, 0x04, 0x2, 0x19, 0x03ff0000, PPC_ALTIVEC),
GEN_HANDLER(vmladduhm, 0x04, 0x11, 0xFF, 0x00000000, PPC_ALTIVEC),
GEN_HANDLER2(evsel0, "evsel", 0x04, 0x1c, 0x09, 0x00000000, PPC_SPE),
GEN_HANDLER2(evsel1, "evsel", 0x04, 0x1d, 0x09, 0x00000000, PPC_SPE),
GEN_HANDLER2(evsel2, "evsel", 0x04, 0x1e, 0x09, 0x00000000, PPC_SPE),
GEN_HANDLER2(evsel3, "evsel", 0x04, 0x1f, 0x09, 0x00000000, PPC_SPE),

#undef GEN_INT_ARITH_ADD
#undef GEN_INT_ARITH_ADD_CONST
#define GEN_INT_ARITH_ADD(name, opc3, add_ca, compute_ca, compute_ov)         \
GEN_HANDLER(name, 0x1F, 0x0A, opc3, 0x00000000, PPC_INTEGER),
#define GEN_INT_ARITH_ADD_CONST(name, opc3, const_val,                        \
                                add_ca, compute_ca, compute_ov)               \
GEN_HANDLER(name, 0x1F, 0x0A, opc3, 0x0000F800, PPC_INTEGER),
GEN_INT_ARITH_ADD(add, 0x08, 0, 0, 0)
GEN_INT_ARITH_ADD(addo, 0x18, 0, 0, 1)
GEN_INT_ARITH_ADD(addc, 0x00, 0, 1, 0)
GEN_INT_ARITH_ADD(addco, 0x10, 0, 1, 1)
GEN_INT_ARITH_ADD(adde, 0x04, 1, 1, 0)
GEN_INT_ARITH_ADD(addeo, 0x14, 1, 1, 1)
GEN_INT_ARITH_ADD_CONST(addme, 0x07, -1LL, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addmeo, 0x17, -1LL, 1, 1, 1)
GEN_INT_ARITH_ADD_CONST(addze, 0x06, 0, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addzeo, 0x16, 0, 1, 1, 1)

#undef GEN_INT_ARITH_DIVW
#define GEN_INT_ARITH_DIVW(name, opc3, sign, compute_ov)                      \
GEN_HANDLER(name, 0x1F, 0x0B, opc3, 0x00000000, PPC_INTEGER)
GEN_INT_ARITH_DIVW(divwu, 0x0E, 0, 0),
GEN_INT_ARITH_DIVW(divwuo, 0x1E, 0, 1),
GEN_INT_ARITH_DIVW(divw, 0x0F, 1, 0),
GEN_INT_ARITH_DIVW(divwo, 0x1F, 1, 1),
GEN_HANDLER_E(divwe, 0x1F, 0x0B, 0x0D, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divweo, 0x1F, 0x0B, 0x1D, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divweu, 0x1F, 0x0B, 0x0C, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divweuo, 0x1F, 0x0B, 0x1C, 0, PPC_NONE, PPC2_DIVE_ISA206),

#if defined(TARGET_PPC64)
#undef GEN_INT_ARITH_DIVD
#define GEN_INT_ARITH_DIVD(name, opc3, sign, compute_ov)                      \
GEN_HANDLER(name, 0x1F, 0x09, opc3, 0x00000000, PPC_64B)
GEN_INT_ARITH_DIVD(divdu, 0x0E, 0, 0),
GEN_INT_ARITH_DIVD(divduo, 0x1E, 0, 1),
GEN_INT_ARITH_DIVD(divd, 0x0F, 1, 0),
GEN_INT_ARITH_DIVD(divdo, 0x1F, 1, 1),

GEN_HANDLER_E(divdeu, 0x1F, 0x09, 0x0C, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divdeuo, 0x1F, 0x09, 0x1C, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divde, 0x1F, 0x09, 0x0D, 0, PPC_NONE, PPC2_DIVE_ISA206),
GEN_HANDLER_E(divdeo, 0x1F, 0x09, 0x1D, 0, PPC_NONE, PPC2_DIVE_ISA206),

#undef GEN_INT_ARITH_MUL_HELPER
#define GEN_INT_ARITH_MUL_HELPER(name, opc3)                                  \
GEN_HANDLER(name, 0x1F, 0x09, opc3, 0x00000000, PPC_64B)
GEN_INT_ARITH_MUL_HELPER(mulhdu, 0x00),
GEN_INT_ARITH_MUL_HELPER(mulhd, 0x02),
GEN_INT_ARITH_MUL_HELPER(mulldo, 0x17),
#endif

#undef GEN_INT_ARITH_SUBF
#undef GEN_INT_ARITH_SUBF_CONST
#define GEN_INT_ARITH_SUBF(name, opc3, add_ca, compute_ca, compute_ov)        \
GEN_HANDLER(name, 0x1F, 0x08, opc3, 0x00000000, PPC_INTEGER),
#define GEN_INT_ARITH_SUBF_CONST(name, opc3, const_val,                       \
                                add_ca, compute_ca, compute_ov)               \
GEN_HANDLER(name, 0x1F, 0x08, opc3, 0x0000F800, PPC_INTEGER),
GEN_INT_ARITH_SUBF(subf, 0x01, 0, 0, 0)
GEN_INT_ARITH_SUBF(subfo, 0x11, 0, 0, 1)
GEN_INT_ARITH_SUBF(subfc, 0x00, 0, 1, 0)
GEN_INT_ARITH_SUBF(subfco, 0x10, 0, 1, 1)
GEN_INT_ARITH_SUBF(subfe, 0x04, 1, 1, 0)
GEN_INT_ARITH_SUBF(subfeo, 0x14, 1, 1, 1)
GEN_INT_ARITH_SUBF_CONST(subfme, 0x07, -1LL, 1, 1, 0)
GEN_INT_ARITH_SUBF_CONST(subfmeo, 0x17, -1LL, 1, 1, 1)
GEN_INT_ARITH_SUBF_CONST(subfze, 0x06, 0, 1, 1, 0)
GEN_INT_ARITH_SUBF_CONST(subfzeo, 0x16, 0, 1, 1, 1)

#undef GEN_LOGICAL1
#undef GEN_LOGICAL2
#define GEN_LOGICAL2(name, tcg_op, opc, type)                                 \
GEN_HANDLER(name, 0x1F, 0x1C, opc, 0x00000000, type)
#define GEN_LOGICAL1(name, tcg_op, opc, type)                                 \
GEN_HANDLER(name, 0x1F, 0x1A, opc, 0x00000000, type)
GEN_LOGICAL2(and, tcg_gen_and_tl, 0x00, PPC_INTEGER),
GEN_LOGICAL2(andc, tcg_gen_andc_tl, 0x01, PPC_INTEGER),
GEN_LOGICAL2(eqv, tcg_gen_eqv_tl, 0x08, PPC_INTEGER),
GEN_LOGICAL1(extsb, tcg_gen_ext8s_tl, 0x1D, PPC_INTEGER),
GEN_LOGICAL1(extsh, tcg_gen_ext16s_tl, 0x1C, PPC_INTEGER),
GEN_LOGICAL2(nand, tcg_gen_nand_tl, 0x0E, PPC_INTEGER),
GEN_LOGICAL2(nor, tcg_gen_nor_tl, 0x03, PPC_INTEGER),
GEN_LOGICAL2(orc, tcg_gen_orc_tl, 0x0C, PPC_INTEGER),
#if defined(TARGET_PPC64)
GEN_LOGICAL1(extsw, tcg_gen_ext32s_tl, 0x1E, PPC_64B),
#endif

#if defined(TARGET_PPC64)
#undef GEN_PPC64_R2
#undef GEN_PPC64_R4
#define GEN_PPC64_R2(name, opc1, opc2)                                        \
GEN_HANDLER2(name##0, stringify(name), opc1, opc2, 0xFF, 0x00000000, PPC_64B),\
GEN_HANDLER2(name##1, stringify(name), opc1, opc2 | 0x10, 0xFF, 0x00000000,   \
             PPC_64B)
#define GEN_PPC64_R4(name, opc1, opc2)                                        \
GEN_HANDLER2(name##0, stringify(name), opc1, opc2, 0xFF, 0x00000000, PPC_64B),\
GEN_HANDLER2(name##1, stringify(name), opc1, opc2 | 0x01, 0xFF, 0x00000000,   \
             PPC_64B),                                                        \
GEN_HANDLER2(name##2, stringify(name), opc1, opc2 | 0x10, 0xFF, 0x00000000,   \
             PPC_64B),                                                        \
GEN_HANDLER2(name##3, stringify(name), opc1, opc2 | 0x11, 0xFF, 0x00000000,   \
             PPC_64B)
GEN_PPC64_R4(rldicl, 0x1E, 0x00),
GEN_PPC64_R4(rldicr, 0x1E, 0x02),
GEN_PPC64_R4(rldic, 0x1E, 0x04),
GEN_PPC64_R2(rldcl, 0x1E, 0x08),
GEN_PPC64_R2(rldcr, 0x1E, 0x09),
GEN_PPC64_R4(rldimi, 0x1E, 0x06),
#endif

#undef _GEN_FLOAT_ACB
#undef GEN_FLOAT_ACB
#undef _GEN_FLOAT_AB
#undef GEN_FLOAT_AB
#undef _GEN_FLOAT_AC
#undef GEN_FLOAT_AC
#undef GEN_FLOAT_B
#undef GEN_FLOAT_BS
#define _GEN_FLOAT_ACB(name, op, op1, op2, isfloat, set_fprf, type)           \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x00000000, type)
#define GEN_FLOAT_ACB(name, op2, set_fprf, type)                              \
_GEN_FLOAT_ACB(name, name, 0x3F, op2, 0, set_fprf, type),                     \
_GEN_FLOAT_ACB(name##s, name, 0x3B, op2, 1, set_fprf, type)
#define _GEN_FLOAT_AB(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, type)
#define GEN_FLOAT_AB(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AB(name, name, 0x3F, op2, inval, 0, set_fprf, type),               \
_GEN_FLOAT_AB(name##s, name, 0x3B, op2, inval, 1, set_fprf, type)
#define _GEN_FLOAT_AC(name, op, op1, op2, inval, isfloat, set_fprf, type)     \
GEN_HANDLER(f##name, op1, op2, 0xFF, inval, type)
#define GEN_FLOAT_AC(name, op2, inval, set_fprf, type)                        \
_GEN_FLOAT_AC(name, name, 0x3F, op2, inval, 0, set_fprf, type),               \
_GEN_FLOAT_AC(name##s, name, 0x3B, op2, inval, 1, set_fprf, type)
#define GEN_FLOAT_B(name, op2, op3, set_fprf, type)                           \
GEN_HANDLER(f##name, 0x3F, op2, op3, 0x001F0000, type)
#define GEN_FLOAT_BS(name, op1, op2, set_fprf, type)                          \
GEN_HANDLER(f##name, op1, op2, 0xFF, 0x001F07C0, type)

GEN_FLOAT_AB(add, 0x15, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_AB(div, 0x12, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_AC(mul, 0x19, 0x0000F800, 1, PPC_FLOAT),
GEN_FLOAT_BS(re, 0x3F, 0x18, 1, PPC_FLOAT_EXT),
GEN_FLOAT_BS(res, 0x3B, 0x18, 1, PPC_FLOAT_FRES),
GEN_FLOAT_BS(rsqrte, 0x3F, 0x1A, 1, PPC_FLOAT_FRSQRTE),
_GEN_FLOAT_ACB(sel, sel, 0x3F, 0x17, 0, 0, PPC_FLOAT_FSEL),
GEN_FLOAT_AB(sub, 0x14, 0x000007C0, 1, PPC_FLOAT),
GEN_FLOAT_ACB(madd, 0x1D, 1, PPC_FLOAT),
GEN_FLOAT_ACB(msub, 0x1C, 1, PPC_FLOAT),
GEN_FLOAT_ACB(nmadd, 0x1F, 1, PPC_FLOAT),
GEN_FLOAT_ACB(nmsub, 0x1E, 1, PPC_FLOAT),
GEN_HANDLER_E(ftdiv, 0x3F, 0x00, 0x04, 1, PPC_NONE, PPC2_FP_TST_ISA206),
GEN_HANDLER_E(ftsqrt, 0x3F, 0x00, 0x05, 1, PPC_NONE, PPC2_FP_TST_ISA206),
GEN_FLOAT_B(ctiw, 0x0E, 0x00, 0, PPC_FLOAT),
GEN_HANDLER_E(fctiwu, 0x3F, 0x0E, 0x04, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(ctiwz, 0x0F, 0x00, 0, PPC_FLOAT),
GEN_HANDLER_E(fctiwuz, 0x3F, 0x0F, 0x04, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(rsp, 0x0C, 0x00, 1, PPC_FLOAT),
#if defined(TARGET_PPC64)
GEN_FLOAT_B(cfid, 0x0E, 0x1A, 1, PPC_64B),
GEN_HANDLER_E(fcfids, 0x3B, 0x0E, 0x1A, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fcfidu, 0x3F, 0x0E, 0x1E, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(fcfidus, 0x3B, 0x0E, 0x1E, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(ctid, 0x0E, 0x19, 0, PPC_64B),
GEN_HANDLER_E(fctidu, 0x3F, 0x0E, 0x1D, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_FLOAT_B(ctidz, 0x0F, 0x19, 0, PPC_64B),
GEN_HANDLER_E(fctiduz, 0x3F, 0x0F, 0x1D, 0, PPC_NONE, PPC2_FP_CVT_ISA206),
#endif
GEN_FLOAT_B(rin, 0x08, 0x0C, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(riz, 0x08, 0x0D, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(rip, 0x08, 0x0E, 1, PPC_FLOAT_EXT),
GEN_FLOAT_B(rim, 0x08, 0x0F, 1, PPC_FLOAT_EXT),

#undef GEN_LD
#undef GEN_LDU
#undef GEN_LDUX
#undef GEN_LDX_E
#undef GEN_LDS
#define GEN_LD(name, ldop, opc, type)                                         \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDU(name, ldop, opc, type)                                        \
GEN_HANDLER(name##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDUX(name, ldop, opc2, opc3, type)                                \
GEN_HANDLER(name##ux, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_LDX_E(name, ldop, opc2, opc3, type, type2)                        \
GEN_HANDLER_E(name##x, 0x1F, opc2, opc3, 0x00000001, type, type2),
#define GEN_LDS(name, ldop, op, type)                                         \
GEN_LD(name, ldop, op | 0x20, type)                                           \
GEN_LDU(name, ldop, op | 0x21, type)                                          \
GEN_LDUX(name, ldop, 0x17, op | 0x01, type)                                   \
GEN_LDX(name, ldop, 0x17, op | 0x00, type)

GEN_LDS(lbz, ld8u, 0x02, PPC_INTEGER)
GEN_LDS(lha, ld16s, 0x0A, PPC_INTEGER)
GEN_LDS(lhz, ld16u, 0x08, PPC_INTEGER)
GEN_LDS(lwz, ld32u, 0x00, PPC_INTEGER)
#if defined(TARGET_PPC64)
GEN_LDUX(lwa, ld32s, 0x15, 0x0B, PPC_64B)
GEN_LDX(lwa, ld32s, 0x15, 0x0A, PPC_64B)
GEN_LDUX(ld, ld64, 0x15, 0x01, PPC_64B)
GEN_LDX(ld, ld64, 0x15, 0x00, PPC_64B)
GEN_LDX_E(ldbr, ld64ur, 0x14, 0x10, PPC_NONE, PPC2_DBRX)
#endif
GEN_LDX(lhbr, ld16ur, 0x16, 0x18, PPC_INTEGER)
GEN_LDX(lwbr, ld32ur, 0x16, 0x10, PPC_INTEGER)

#undef GEN_ST
#undef GEN_STU
#undef GEN_STUX
#undef GEN_STX_E
#undef GEN_STS
#define GEN_ST(name, stop, opc, type)                                         \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STU(name, stop, opc, type)                                        \
GEN_HANDLER(stop##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STUX(name, stop, opc2, opc3, type)                                \
GEN_HANDLER(name##ux, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_STX_E(name, stop, opc2, opc3, type, type2)                        \
GEN_HANDLER_E(name##x, 0x1F, opc2, opc3, 0x00000001, type, type2),
#define GEN_STS(name, stop, op, type)                                         \
GEN_ST(name, stop, op | 0x20, type)                                           \
GEN_STU(name, stop, op | 0x21, type)                                          \
GEN_STUX(name, stop, 0x17, op | 0x01, type)                                   \
GEN_STX(name, stop, 0x17, op | 0x00, type)

GEN_STS(stb, st8, 0x06, PPC_INTEGER)
GEN_STS(sth, st16, 0x0C, PPC_INTEGER)
GEN_STS(stw, st32, 0x04, PPC_INTEGER)
#if defined(TARGET_PPC64)
GEN_STUX(std, st64, 0x15, 0x05, PPC_64B)
GEN_STX(std, st64, 0x15, 0x04, PPC_64B)
GEN_STX_E(stdbr, st64r, 0x14, 0x14, PPC_NONE, PPC2_DBRX)
#endif
GEN_STX(sthbr, st16r, 0x16, 0x1C, PPC_INTEGER)
GEN_STX(stwbr, st32r, 0x16, 0x14, PPC_INTEGER)

#undef GEN_LDF
#undef GEN_LDUF
#undef GEN_LDUXF
#undef GEN_LDXF
#undef GEN_LDFS
#define GEN_LDF(name, ldop, opc, type)                                        \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDUF(name, ldop, opc, type)                                       \
GEN_HANDLER(name##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_LDUXF(name, ldop, opc, type)                                      \
GEN_HANDLER(name##ux, 0x1F, 0x17, opc, 0x00000001, type),
#define GEN_LDXF(name, ldop, opc2, opc3, type)                                \
GEN_HANDLER(name##x, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_LDFS(name, ldop, op, type)                                        \
GEN_LDF(name, ldop, op | 0x20, type)                                          \
GEN_LDUF(name, ldop, op | 0x21, type)                                         \
GEN_LDUXF(name, ldop, op | 0x01, type)                                        \
GEN_LDXF(name, ldop, 0x17, op | 0x00, type)

GEN_LDFS(lfd, ld64, 0x12, PPC_FLOAT)
GEN_LDFS(lfs, ld32fs, 0x10, PPC_FLOAT)
GEN_HANDLER_E(lfiwax, 0x1f, 0x17, 0x1a, 0x00000001, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(lfiwzx, 0x1f, 0x17, 0x1b, 0x1, PPC_NONE, PPC2_FP_CVT_ISA206),
GEN_HANDLER_E(lfdp, 0x39, 0xFF, 0xFF, 0x00200003, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(lfdpx, 0x1F, 0x17, 0x18, 0x00200001, PPC_NONE, PPC2_ISA205),

#undef GEN_STF
#undef GEN_STUF
#undef GEN_STUXF
#undef GEN_STXF
#undef GEN_STFS
#define GEN_STF(name, stop, opc, type)                                        \
GEN_HANDLER(name, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STUF(name, stop, opc, type)                                       \
GEN_HANDLER(name##u, opc, 0xFF, 0xFF, 0x00000000, type),
#define GEN_STUXF(name, stop, opc, type)                                      \
GEN_HANDLER(name##ux, 0x1F, 0x17, opc, 0x00000001, type),
#define GEN_STXF(name, stop, opc2, opc3, type)                                \
GEN_HANDLER(name##x, 0x1F, opc2, opc3, 0x00000001, type),
#define GEN_STFS(name, stop, op, type)                                        \
GEN_STF(name, stop, op | 0x20, type)                                          \
GEN_STUF(name, stop, op | 0x21, type)                                         \
GEN_STUXF(name, stop, op | 0x01, type)                                        \
GEN_STXF(name, stop, 0x17, op | 0x00, type)

GEN_STFS(stfd, st64, 0x16, PPC_FLOAT)
GEN_STFS(stfs, st32fs, 0x14, PPC_FLOAT)
GEN_STXF(stfiw, st32fiw, 0x17, 0x1E, PPC_FLOAT_STFIWX)
GEN_HANDLER_E(stfdp, 0x3D, 0xFF, 0xFF, 0x00200003, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(stfdpx, 0x1F, 0x17, 0x1C, 0x00200001, PPC_NONE, PPC2_ISA205),

#undef GEN_CRLOGIC
#define GEN_CRLOGIC(name, tcg_op, opc)                                        \
GEN_HANDLER(name, 0x13, 0x01, opc, 0x00000001, PPC_INTEGER)
GEN_CRLOGIC(crand, tcg_gen_and_i32, 0x08),
GEN_CRLOGIC(crandc, tcg_gen_andc_i32, 0x04),
GEN_CRLOGIC(creqv, tcg_gen_eqv_i32, 0x09),
GEN_CRLOGIC(crnand, tcg_gen_nand_i32, 0x07),
GEN_CRLOGIC(crnor, tcg_gen_nor_i32, 0x01),
GEN_CRLOGIC(cror, tcg_gen_or_i32, 0x0E),
GEN_CRLOGIC(crorc, tcg_gen_orc_i32, 0x0D),
GEN_CRLOGIC(crxor, tcg_gen_xor_i32, 0x06),

#undef GEN_MAC_HANDLER
#define GEN_MAC_HANDLER(name, opc2, opc3)                                     \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_405_MAC)
GEN_MAC_HANDLER(macchw, 0x0C, 0x05),
GEN_MAC_HANDLER(macchwo, 0x0C, 0x15),
GEN_MAC_HANDLER(macchws, 0x0C, 0x07),
GEN_MAC_HANDLER(macchwso, 0x0C, 0x17),
GEN_MAC_HANDLER(macchwsu, 0x0C, 0x06),
GEN_MAC_HANDLER(macchwsuo, 0x0C, 0x16),
GEN_MAC_HANDLER(macchwu, 0x0C, 0x04),
GEN_MAC_HANDLER(macchwuo, 0x0C, 0x14),
GEN_MAC_HANDLER(machhw, 0x0C, 0x01),
GEN_MAC_HANDLER(machhwo, 0x0C, 0x11),
GEN_MAC_HANDLER(machhws, 0x0C, 0x03),
GEN_MAC_HANDLER(machhwso, 0x0C, 0x13),
GEN_MAC_HANDLER(machhwsu, 0x0C, 0x02),
GEN_MAC_HANDLER(machhwsuo, 0x0C, 0x12),
GEN_MAC_HANDLER(machhwu, 0x0C, 0x00),
GEN_MAC_HANDLER(machhwuo, 0x0C, 0x10),
GEN_MAC_HANDLER(maclhw, 0x0C, 0x0D),
GEN_MAC_HANDLER(maclhwo, 0x0C, 0x1D),
GEN_MAC_HANDLER(maclhws, 0x0C, 0x0F),
GEN_MAC_HANDLER(maclhwso, 0x0C, 0x1F),
GEN_MAC_HANDLER(maclhwu, 0x0C, 0x0C),
GEN_MAC_HANDLER(maclhwuo, 0x0C, 0x1C),
GEN_MAC_HANDLER(maclhwsu, 0x0C, 0x0E),
GEN_MAC_HANDLER(maclhwsuo, 0x0C, 0x1E),
GEN_MAC_HANDLER(nmacchw, 0x0E, 0x05),
GEN_MAC_HANDLER(nmacchwo, 0x0E, 0x15),
GEN_MAC_HANDLER(nmacchws, 0x0E, 0x07),
GEN_MAC_HANDLER(nmacchwso, 0x0E, 0x17),
GEN_MAC_HANDLER(nmachhw, 0x0E, 0x01),
GEN_MAC_HANDLER(nmachhwo, 0x0E, 0x11),
GEN_MAC_HANDLER(nmachhws, 0x0E, 0x03),
GEN_MAC_HANDLER(nmachhwso, 0x0E, 0x13),
GEN_MAC_HANDLER(nmaclhw, 0x0E, 0x0D),
GEN_MAC_HANDLER(nmaclhwo, 0x0E, 0x1D),
GEN_MAC_HANDLER(nmaclhws, 0x0E, 0x0F),
GEN_MAC_HANDLER(nmaclhwso, 0x0E, 0x1F),
GEN_MAC_HANDLER(mulchw, 0x08, 0x05),
GEN_MAC_HANDLER(mulchwu, 0x08, 0x04),
GEN_MAC_HANDLER(mulhhw, 0x08, 0x01),
GEN_MAC_HANDLER(mulhhwu, 0x08, 0x00),
GEN_MAC_HANDLER(mullhw, 0x08, 0x0D),
GEN_MAC_HANDLER(mullhwu, 0x08, 0x0C),

#undef GEN_VR_LDX
#undef GEN_VR_STX
#undef GEN_VR_LVE
#undef GEN_VR_STVE
#define GEN_VR_LDX(name, opc2, opc3)                                          \
GEN_HANDLER(name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_STX(name, opc2, opc3)                                          \
GEN_HANDLER(st##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_LVE(name, opc2, opc3)                                    \
    GEN_HANDLER(lve##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
#define GEN_VR_STVE(name, opc2, opc3)                                   \
    GEN_HANDLER(stve##name, 0x1F, opc2, opc3, 0x00000001, PPC_ALTIVEC)
GEN_VR_LDX(lvx, 0x07, 0x03),
GEN_VR_LDX(lvxl, 0x07, 0x0B),
GEN_VR_LVE(bx, 0x07, 0x00),
GEN_VR_LVE(hx, 0x07, 0x01),
GEN_VR_LVE(wx, 0x07, 0x02),
GEN_VR_STX(svx, 0x07, 0x07),
GEN_VR_STX(svxl, 0x07, 0x0F),
GEN_VR_STVE(bx, 0x07, 0x04),
GEN_VR_STVE(hx, 0x07, 0x05),
GEN_VR_STVE(wx, 0x07, 0x06),

#undef GEN_VX_LOGICAL
#define GEN_VX_LOGICAL(name, tcg_op, opc2, opc3)                        \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)

#undef GEN_VX_LOGICAL_207
#define GEN_VX_LOGICAL_207(name, tcg_op, opc2, opc3) \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ALTIVEC_207)

GEN_VX_LOGICAL(vand, tcg_gen_and_i64, 2, 16),
GEN_VX_LOGICAL(vandc, tcg_gen_andc_i64, 2, 17),
GEN_VX_LOGICAL(vor, tcg_gen_or_i64, 2, 18),
GEN_VX_LOGICAL(vxor, tcg_gen_xor_i64, 2, 19),
GEN_VX_LOGICAL(vnor, tcg_gen_nor_i64, 2, 20),
GEN_VX_LOGICAL_207(veqv, tcg_gen_eqv_i64, 2, 26),
GEN_VX_LOGICAL_207(vnand, tcg_gen_nand_i64, 2, 22),
GEN_VX_LOGICAL_207(vorc, tcg_gen_orc_i64, 2, 21),

#undef GEN_VXFORM
#define GEN_VXFORM(name, opc2, opc3)                                    \
GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)

#undef GEN_VXFORM_207
#define GEN_VXFORM_207(name, opc2, opc3) \
GEN_HANDLER_E(name, 0x04, opc2, opc3, 0x00000000, PPC_NONE, PPC2_ALTIVEC_207)

#undef GEN_VXFORM_DUAL
#define GEN_VXFORM_DUAL(name0, name1, opc2, opc3, type0, type1) \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, opc3, 0x00000000, type0, type1)

#undef GEN_VXRFORM_DUAL
#define GEN_VXRFORM_DUAL(name0, name1, opc2, opc3, tp0, tp1) \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, opc3, 0x00000000, tp0, tp1), \
GEN_HANDLER_E(name0##_##name1, 0x4, opc2, (opc3 | 0x10), 0x00000000, tp0, tp1),

GEN_VXFORM(vaddubm, 0, 0),
GEN_VXFORM(vadduhm, 0, 1),
GEN_VXFORM(vadduwm, 0, 2),
GEN_VXFORM_207(vaddudm, 0, 3),
GEN_VXFORM_DUAL(vsububm, bcdadd, 0, 16, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vsubuhm, bcdsub, 0, 17, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vsubuwm, 0, 18),
GEN_VXFORM_207(vsubudm, 0, 19),
GEN_VXFORM(vmaxub, 1, 0),
GEN_VXFORM(vmaxuh, 1, 1),
GEN_VXFORM(vmaxuw, 1, 2),
GEN_VXFORM_207(vmaxud, 1, 3),
GEN_VXFORM(vmaxsb, 1, 4),
GEN_VXFORM(vmaxsh, 1, 5),
GEN_VXFORM(vmaxsw, 1, 6),
GEN_VXFORM_207(vmaxsd, 1, 7),
GEN_VXFORM(vminub, 1, 8),
GEN_VXFORM(vminuh, 1, 9),
GEN_VXFORM(vminuw, 1, 10),
GEN_VXFORM_207(vminud, 1, 11),
GEN_VXFORM(vminsb, 1, 12),
GEN_VXFORM(vminsh, 1, 13),
GEN_VXFORM(vminsw, 1, 14),
GEN_VXFORM_207(vminsd, 1, 15),
GEN_VXFORM(vavgub, 1, 16),
GEN_VXFORM(vavguh, 1, 17),
GEN_VXFORM(vavguw, 1, 18),
GEN_VXFORM(vavgsb, 1, 20),
GEN_VXFORM(vavgsh, 1, 21),
GEN_VXFORM(vavgsw, 1, 22),
GEN_VXFORM(vmrghb, 6, 0),
GEN_VXFORM(vmrghh, 6, 1),
GEN_VXFORM(vmrghw, 6, 2),
GEN_VXFORM(vmrglb, 6, 4),
GEN_VXFORM(vmrglh, 6, 5),
GEN_VXFORM(vmrglw, 6, 6),
GEN_VXFORM_207(vmrgew, 6, 30),
GEN_VXFORM_207(vmrgow, 6, 26),
GEN_VXFORM(vmuloub, 4, 0),
GEN_VXFORM(vmulouh, 4, 1),
GEN_VXFORM_DUAL(vmulouw, vmuluwm, 4, 2, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vmulosb, 4, 4),
GEN_VXFORM(vmulosh, 4, 5),
GEN_VXFORM_207(vmulosw, 4, 6),
GEN_VXFORM(vmuleub, 4, 8),
GEN_VXFORM(vmuleuh, 4, 9),
GEN_VXFORM_207(vmuleuw, 4, 10),
GEN_VXFORM(vmulesb, 4, 12),
GEN_VXFORM(vmulesh, 4, 13),
GEN_VXFORM_207(vmulesw, 4, 14),
GEN_VXFORM(vslb, 2, 4),
GEN_VXFORM(vslh, 2, 5),
GEN_VXFORM(vslw, 2, 6),
GEN_VXFORM_207(vsld, 2, 23),
GEN_VXFORM(vsrb, 2, 8),
GEN_VXFORM(vsrh, 2, 9),
GEN_VXFORM(vsrw, 2, 10),
GEN_VXFORM_207(vsrd, 2, 27),
GEN_VXFORM(vsrab, 2, 12),
GEN_VXFORM(vsrah, 2, 13),
GEN_VXFORM(vsraw, 2, 14),
GEN_VXFORM_207(vsrad, 2, 15),
GEN_VXFORM(vslo, 6, 16),
GEN_VXFORM(vsro, 6, 17),
GEN_VXFORM(vaddcuw, 0, 6),
GEN_VXFORM(vsubcuw, 0, 22),
GEN_VXFORM(vaddubs, 0, 8),
GEN_VXFORM(vadduhs, 0, 9),
GEN_VXFORM(vadduws, 0, 10),
GEN_VXFORM(vaddsbs, 0, 12),
GEN_VXFORM(vaddshs, 0, 13),
GEN_VXFORM(vaddsws, 0, 14),
GEN_VXFORM_DUAL(vsububs, bcdadd, 0, 24, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM_DUAL(vsubuhs, bcdsub, 0, 25, PPC_ALTIVEC, PPC_NONE),
GEN_VXFORM(vsubuws, 0, 26),
GEN_VXFORM(vsubsbs, 0, 28),
GEN_VXFORM(vsubshs, 0, 29),
GEN_VXFORM(vsubsws, 0, 30),
GEN_VXFORM_207(vadduqm, 0, 4),
GEN_VXFORM_207(vaddcuq, 0, 5),
GEN_VXFORM_DUAL(vaddeuqm, vaddecuq, 30, 0xFF, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_207(vsubuqm, 0, 20),
GEN_VXFORM_207(vsubcuq, 0, 21),
GEN_VXFORM_DUAL(vsubeuqm, vsubecuq, 31, 0xFF, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM(vrlb, 2, 0),
GEN_VXFORM(vrlh, 2, 1),
GEN_VXFORM(vrlw, 2, 2),
GEN_VXFORM_207(vrld, 2, 3),
GEN_VXFORM(vsl, 2, 7),
GEN_VXFORM(vsr, 2, 11),
GEN_VXFORM(vpkuhum, 7, 0),
GEN_VXFORM(vpkuwum, 7, 1),
GEN_VXFORM_207(vpkudum, 7, 17),
GEN_VXFORM(vpkuhus, 7, 2),
GEN_VXFORM(vpkuwus, 7, 3),
GEN_VXFORM_207(vpkudus, 7, 19),
GEN_VXFORM(vpkshus, 7, 4),
GEN_VXFORM(vpkswus, 7, 5),
GEN_VXFORM_207(vpksdus, 7, 21),
GEN_VXFORM(vpkshss, 7, 6),
GEN_VXFORM(vpkswss, 7, 7),
GEN_VXFORM_207(vpksdss, 7, 23),
GEN_VXFORM(vpkpx, 7, 12),
GEN_VXFORM(vsum4ubs, 4, 24),
GEN_VXFORM(vsum4sbs, 4, 28),
GEN_VXFORM(vsum4shs, 4, 25),
GEN_VXFORM(vsum2sws, 4, 26),
GEN_VXFORM(vsumsws, 4, 30),
GEN_VXFORM(vaddfp, 5, 0),
GEN_VXFORM(vsubfp, 5, 1),
GEN_VXFORM(vmaxfp, 5, 16),
GEN_VXFORM(vminfp, 5, 17),

#undef GEN_VXRFORM1
#undef GEN_VXRFORM
#define GEN_VXRFORM1(opname, name, str, opc2, opc3)                     \
    GEN_HANDLER2(name, str, 0x4, opc2, opc3, 0x00000000, PPC_ALTIVEC),
#define GEN_VXRFORM(name, opc2, opc3)                                \
    GEN_VXRFORM1(name, name, #name, opc2, opc3)                      \
    GEN_VXRFORM1(name##_dot, name##_, #name ".", opc2, (opc3 | (0x1 << 4)))
GEN_VXRFORM(vcmpequb, 3, 0)
GEN_VXRFORM(vcmpequh, 3, 1)
GEN_VXRFORM(vcmpequw, 3, 2)
GEN_VXRFORM(vcmpgtsb, 3, 12)
GEN_VXRFORM(vcmpgtsh, 3, 13)
GEN_VXRFORM(vcmpgtsw, 3, 14)
GEN_VXRFORM(vcmpgtub, 3, 8)
GEN_VXRFORM(vcmpgtuh, 3, 9)
GEN_VXRFORM(vcmpgtuw, 3, 10)
GEN_VXRFORM_DUAL(vcmpeqfp, vcmpequd, 3, 3, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM(vcmpgefp, 3, 7)
GEN_VXRFORM_DUAL(vcmpgtfp, vcmpgtud, 3, 11, PPC_ALTIVEC, PPC_NONE)
GEN_VXRFORM_DUAL(vcmpbfp, vcmpgtsd, 3, 15, PPC_ALTIVEC, PPC_NONE)

#undef GEN_VXFORM_SIMM
#define GEN_VXFORM_SIMM(name, opc2, opc3)                               \
    GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)
GEN_VXFORM_SIMM(vspltisb, 6, 12),
GEN_VXFORM_SIMM(vspltish, 6, 13),
GEN_VXFORM_SIMM(vspltisw, 6, 14),

#undef GEN_VXFORM_NOA
#define GEN_VXFORM_NOA(name, opc2, opc3)                                \
    GEN_HANDLER(name, 0x04, opc2, opc3, 0x001f0000, PPC_ALTIVEC)
GEN_VXFORM_NOA(vupkhsb, 7, 8),
GEN_VXFORM_NOA(vupkhsh, 7, 9),
GEN_VXFORM_207(vupkhsw, 7, 25),
GEN_VXFORM_NOA(vupklsb, 7, 10),
GEN_VXFORM_NOA(vupklsh, 7, 11),
GEN_VXFORM_207(vupklsw, 7, 27),
GEN_VXFORM_NOA(vupkhpx, 7, 13),
GEN_VXFORM_NOA(vupklpx, 7, 15),
GEN_VXFORM_NOA(vrefp, 5, 4),
GEN_VXFORM_NOA(vrsqrtefp, 5, 5),
GEN_VXFORM_NOA(vexptefp, 5, 6),
GEN_VXFORM_NOA(vlogefp, 5, 7),
GEN_VXFORM_NOA(vrfim, 5, 8),
GEN_VXFORM_NOA(vrfin, 5, 9),
GEN_VXFORM_NOA(vrfip, 5, 10),
GEN_VXFORM_NOA(vrfiz, 5, 11),

#undef GEN_VXFORM_UIMM
#define GEN_VXFORM_UIMM(name, opc2, opc3)                               \
    GEN_HANDLER(name, 0x04, opc2, opc3, 0x00000000, PPC_ALTIVEC)
GEN_VXFORM_UIMM(vspltb, 6, 8),
GEN_VXFORM_UIMM(vsplth, 6, 9),
GEN_VXFORM_UIMM(vspltw, 6, 10),
GEN_VXFORM_UIMM(vcfux, 5, 12),
GEN_VXFORM_UIMM(vcfsx, 5, 13),
GEN_VXFORM_UIMM(vctuxs, 5, 14),
GEN_VXFORM_UIMM(vctsxs, 5, 15),

#undef GEN_VAFORM_PAIRED
#define GEN_VAFORM_PAIRED(name0, name1, opc2)                           \
    GEN_HANDLER(name0##_##name1, 0x04, opc2, 0xFF, 0x00000000, PPC_ALTIVEC)
GEN_VAFORM_PAIRED(vmhaddshs, vmhraddshs, 16),
GEN_VAFORM_PAIRED(vmsumubm, vmsummbm, 18),
GEN_VAFORM_PAIRED(vmsumuhm, vmsumuhs, 19),
GEN_VAFORM_PAIRED(vmsumshm, vmsumshs, 20),
GEN_VAFORM_PAIRED(vsel, vperm, 21),
GEN_VAFORM_PAIRED(vmaddfp, vnmsubfp, 23),

GEN_VXFORM_DUAL(vclzb, vpopcntb, 1, 28, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzh, vpopcnth, 1, 29, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzw, vpopcntw, 1, 30, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vclzd, vpopcntd, 1, 31, PPC_NONE, PPC2_ALTIVEC_207),

GEN_VXFORM_207(vbpermq, 6, 21),
GEN_VXFORM_207(vgbbd, 6, 20),
GEN_VXFORM_207(vpmsumb, 4, 16),
GEN_VXFORM_207(vpmsumh, 4, 17),
GEN_VXFORM_207(vpmsumw, 4, 18),
GEN_VXFORM_207(vpmsumd, 4, 19),

GEN_VXFORM_207(vsbox, 4, 23),

GEN_VXFORM_DUAL(vcipher, vcipherlast, 4, 20, PPC_NONE, PPC2_ALTIVEC_207),
GEN_VXFORM_DUAL(vncipher, vncipherlast, 4, 21, PPC_NONE, PPC2_ALTIVEC_207),

GEN_VXFORM_207(vshasigmaw, 1, 26),
GEN_VXFORM_207(vshasigmad, 1, 27),

GEN_VXFORM_DUAL(vsldoi, vpermxor, 22, 0xFF, PPC_ALTIVEC, PPC_NONE),

GEN_HANDLER_E(lxsdx, 0x1F, 0x0C, 0x12, 0, PPC_NONE, PPC2_VSX),
GEN_HANDLER_E(lxsiwax, 0x1F, 0x0C, 0x02, 0, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(lxsiwzx, 0x1F, 0x0C, 0x00, 0, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(lxsspx, 0x1F, 0x0C, 0x10, 0, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(lxvd2x, 0x1F, 0x0C, 0x1A, 0, PPC_NONE, PPC2_VSX),
GEN_HANDLER_E(lxvdsx, 0x1F, 0x0C, 0x0A, 0, PPC_NONE, PPC2_VSX),
GEN_HANDLER_E(lxvw4x, 0x1F, 0x0C, 0x18, 0, PPC_NONE, PPC2_VSX),

GEN_HANDLER_E(stxsdx, 0x1F, 0xC, 0x16, 0, PPC_NONE, PPC2_VSX),
GEN_HANDLER_E(stxsiwx, 0x1F, 0xC, 0x04, 0, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(stxsspx, 0x1F, 0xC, 0x14, 0, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(stxvd2x, 0x1F, 0xC, 0x1E, 0, PPC_NONE, PPC2_VSX),
GEN_HANDLER_E(stxvw4x, 0x1F, 0xC, 0x1C, 0, PPC_NONE, PPC2_VSX),

GEN_HANDLER_E(mfvsrwz, 0x1F, 0x13, 0x03, 0x0000F800, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(mtvsrwa, 0x1F, 0x13, 0x06, 0x0000F800, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(mtvsrwz, 0x1F, 0x13, 0x07, 0x0000F800, PPC_NONE, PPC2_VSX207),
#if defined(TARGET_PPC64)
GEN_HANDLER_E(mfvsrd, 0x1F, 0x13, 0x01, 0x0000F800, PPC_NONE, PPC2_VSX207),
GEN_HANDLER_E(mtvsrd, 0x1F, 0x13, 0x05, 0x0000F800, PPC_NONE, PPC2_VSX207),
#endif

#undef GEN_XX2FORM
#define GEN_XX2FORM(name, opc2, opc3, fl2)                           \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0, opc3, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 1, opc3, 0, PPC_NONE, fl2)

#undef GEN_XX3FORM
#define GEN_XX3FORM(name, opc2, opc3, fl2)                           \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0, opc3, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 1, opc3, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 2, opc3, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 3, opc3, 0, PPC_NONE, fl2)

#undef GEN_XX3_RC_FORM
#define GEN_XX3_RC_FORM(name, opc2, opc3, fl2)                          \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x00, opc3 | 0x00, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x01, opc3 | 0x00, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x02, opc3 | 0x00, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x03, opc3 | 0x00, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x00, opc3 | 0x10, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x01, opc3 | 0x10, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x02, opc3 | 0x10, 0, PPC_NONE, fl2), \
GEN_HANDLER2_E(name, #name, 0x3C, opc2 | 0x03, opc3 | 0x10, 0, PPC_NONE, fl2)

#undef GEN_XX3FORM_DM
#define GEN_XX3FORM_DM(name, opc2, opc3) \
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x00, opc3|0x00, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x01, opc3|0x00, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x02, opc3|0x00, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x03, opc3|0x00, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x00, opc3|0x04, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x01, opc3|0x04, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x02, opc3|0x04, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x03, opc3|0x04, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x00, opc3|0x08, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x01, opc3|0x08, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x02, opc3|0x08, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x03, opc3|0x08, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x00, opc3|0x0C, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x01, opc3|0x0C, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x02, opc3|0x0C, 0, PPC_NONE, PPC2_VSX),\
GEN_HANDLER2_E(name, #name, 0x3C, opc2|0x03, opc3|0x0C, 0, PPC_NONE, PPC2_VSX)

GEN_XX2FORM(xsabsdp, 0x12, 0x15, PPC2_VSX),
GEN_XX2FORM(xsnabsdp, 0x12, 0x16, PPC2_VSX),
GEN_XX2FORM(xsnegdp, 0x12, 0x17, PPC2_VSX),
GEN_XX3FORM(xscpsgndp, 0x00, 0x16, PPC2_VSX),

GEN_XX2FORM(xvabsdp, 0x12, 0x1D, PPC2_VSX),
GEN_XX2FORM(xvnabsdp, 0x12, 0x1E, PPC2_VSX),
GEN_XX2FORM(xvnegdp, 0x12, 0x1F, PPC2_VSX),
GEN_XX3FORM(xvcpsgndp, 0x00, 0x1E, PPC2_VSX),
GEN_XX2FORM(xvabssp, 0x12, 0x19, PPC2_VSX),
GEN_XX2FORM(xvnabssp, 0x12, 0x1A, PPC2_VSX),
GEN_XX2FORM(xvnegsp, 0x12, 0x1B, PPC2_VSX),
GEN_XX3FORM(xvcpsgnsp, 0x00, 0x1A, PPC2_VSX),

GEN_XX3FORM(xsadddp, 0x00, 0x04, PPC2_VSX),
GEN_XX3FORM(xssubdp, 0x00, 0x05, PPC2_VSX),
GEN_XX3FORM(xsmuldp, 0x00, 0x06, PPC2_VSX),
GEN_XX3FORM(xsdivdp, 0x00, 0x07, PPC2_VSX),
GEN_XX2FORM(xsredp,  0x14, 0x05, PPC2_VSX),
GEN_XX2FORM(xssqrtdp,  0x16, 0x04, PPC2_VSX),
GEN_XX2FORM(xsrsqrtedp,  0x14, 0x04, PPC2_VSX),
GEN_XX3FORM(xstdivdp,  0x14, 0x07, PPC2_VSX),
GEN_XX2FORM(xstsqrtdp,  0x14, 0x06, PPC2_VSX),
GEN_XX3FORM(xsmaddadp, 0x04, 0x04, PPC2_VSX),
GEN_XX3FORM(xsmaddmdp, 0x04, 0x05, PPC2_VSX),
GEN_XX3FORM(xsmsubadp, 0x04, 0x06, PPC2_VSX),
GEN_XX3FORM(xsmsubmdp, 0x04, 0x07, PPC2_VSX),
GEN_XX3FORM(xsnmaddadp, 0x04, 0x14, PPC2_VSX),
GEN_XX3FORM(xsnmaddmdp, 0x04, 0x15, PPC2_VSX),
GEN_XX3FORM(xsnmsubadp, 0x04, 0x16, PPC2_VSX),
GEN_XX3FORM(xsnmsubmdp, 0x04, 0x17, PPC2_VSX),
GEN_XX2FORM(xscmpodp,  0x0C, 0x05, PPC2_VSX),
GEN_XX2FORM(xscmpudp,  0x0C, 0x04, PPC2_VSX),
GEN_XX3FORM(xsmaxdp, 0x00, 0x14, PPC2_VSX),
GEN_XX3FORM(xsmindp, 0x00, 0x15, PPC2_VSX),
GEN_XX2FORM(xscvdpsp, 0x12, 0x10, PPC2_VSX),
GEN_XX2FORM(xscvdpspn, 0x16, 0x10, PPC2_VSX207),
GEN_XX2FORM(xscvspdp, 0x12, 0x14, PPC2_VSX),
GEN_XX2FORM(xscvspdpn, 0x16, 0x14, PPC2_VSX207),
GEN_XX2FORM(xscvdpsxds, 0x10, 0x15, PPC2_VSX),
GEN_XX2FORM(xscvdpsxws, 0x10, 0x05, PPC2_VSX),
GEN_XX2FORM(xscvdpuxds, 0x10, 0x14, PPC2_VSX),
GEN_XX2FORM(xscvdpuxws, 0x10, 0x04, PPC2_VSX),
GEN_XX2FORM(xscvsxddp, 0x10, 0x17, PPC2_VSX),
GEN_XX2FORM(xscvuxddp, 0x10, 0x16, PPC2_VSX),
GEN_XX2FORM(xsrdpi, 0x12, 0x04, PPC2_VSX),
GEN_XX2FORM(xsrdpic, 0x16, 0x06, PPC2_VSX),
GEN_XX2FORM(xsrdpim, 0x12, 0x07, PPC2_VSX),
GEN_XX2FORM(xsrdpip, 0x12, 0x06, PPC2_VSX),
GEN_XX2FORM(xsrdpiz, 0x12, 0x05, PPC2_VSX),

GEN_XX3FORM(xsaddsp, 0x00, 0x00, PPC2_VSX207),
GEN_XX3FORM(xssubsp, 0x00, 0x01, PPC2_VSX207),
GEN_XX3FORM(xsmulsp, 0x00, 0x02, PPC2_VSX207),
GEN_XX3FORM(xsdivsp, 0x00, 0x03, PPC2_VSX207),
GEN_XX2FORM(xsresp,  0x14, 0x01, PPC2_VSX207),
GEN_XX2FORM(xsrsp, 0x12, 0x11, PPC2_VSX207),
GEN_XX2FORM(xssqrtsp,  0x16, 0x00, PPC2_VSX207),
GEN_XX2FORM(xsrsqrtesp,  0x14, 0x00, PPC2_VSX207),
GEN_XX3FORM(xsmaddasp, 0x04, 0x00, PPC2_VSX207),
GEN_XX3FORM(xsmaddmsp, 0x04, 0x01, PPC2_VSX207),
GEN_XX3FORM(xsmsubasp, 0x04, 0x02, PPC2_VSX207),
GEN_XX3FORM(xsmsubmsp, 0x04, 0x03, PPC2_VSX207),
GEN_XX3FORM(xsnmaddasp, 0x04, 0x10, PPC2_VSX207),
GEN_XX3FORM(xsnmaddmsp, 0x04, 0x11, PPC2_VSX207),
GEN_XX3FORM(xsnmsubasp, 0x04, 0x12, PPC2_VSX207),
GEN_XX3FORM(xsnmsubmsp, 0x04, 0x13, PPC2_VSX207),
GEN_XX2FORM(xscvsxdsp, 0x10, 0x13, PPC2_VSX207),
GEN_XX2FORM(xscvuxdsp, 0x10, 0x12, PPC2_VSX207),

GEN_XX3FORM(xvadddp, 0x00, 0x0C, PPC2_VSX),
GEN_XX3FORM(xvsubdp, 0x00, 0x0D, PPC2_VSX),
GEN_XX3FORM(xvmuldp, 0x00, 0x0E, PPC2_VSX),
GEN_XX3FORM(xvdivdp, 0x00, 0x0F, PPC2_VSX),
GEN_XX2FORM(xvredp,  0x14, 0x0D, PPC2_VSX),
GEN_XX2FORM(xvsqrtdp,  0x16, 0x0C, PPC2_VSX),
GEN_XX2FORM(xvrsqrtedp,  0x14, 0x0C, PPC2_VSX),
GEN_XX3FORM(xvtdivdp, 0x14, 0x0F, PPC2_VSX),
GEN_XX2FORM(xvtsqrtdp, 0x14, 0x0E, PPC2_VSX),
GEN_XX3FORM(xvmaddadp, 0x04, 0x0C, PPC2_VSX),
GEN_XX3FORM(xvmaddmdp, 0x04, 0x0D, PPC2_VSX),
GEN_XX3FORM(xvmsubadp, 0x04, 0x0E, PPC2_VSX),
GEN_XX3FORM(xvmsubmdp, 0x04, 0x0F, PPC2_VSX),
GEN_XX3FORM(xvnmaddadp, 0x04, 0x1C, PPC2_VSX),
GEN_XX3FORM(xvnmaddmdp, 0x04, 0x1D, PPC2_VSX),
GEN_XX3FORM(xvnmsubadp, 0x04, 0x1E, PPC2_VSX),
GEN_XX3FORM(xvnmsubmdp, 0x04, 0x1F, PPC2_VSX),
GEN_XX3FORM(xvmaxdp, 0x00, 0x1C, PPC2_VSX),
GEN_XX3FORM(xvmindp, 0x00, 0x1D, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpeqdp, 0x0C, 0x0C, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpgtdp, 0x0C, 0x0D, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpgedp, 0x0C, 0x0E, PPC2_VSX),
GEN_XX2FORM(xvcvdpsp, 0x12, 0x18, PPC2_VSX),
GEN_XX2FORM(xvcvdpsxds, 0x10, 0x1D, PPC2_VSX),
GEN_XX2FORM(xvcvdpsxws, 0x10, 0x0D, PPC2_VSX),
GEN_XX2FORM(xvcvdpuxds, 0x10, 0x1C, PPC2_VSX),
GEN_XX2FORM(xvcvdpuxws, 0x10, 0x0C, PPC2_VSX),
GEN_XX2FORM(xvcvsxddp, 0x10, 0x1F, PPC2_VSX),
GEN_XX2FORM(xvcvuxddp, 0x10, 0x1E, PPC2_VSX),
GEN_XX2FORM(xvcvsxwdp, 0x10, 0x0F, PPC2_VSX),
GEN_XX2FORM(xvcvuxwdp, 0x10, 0x0E, PPC2_VSX),
GEN_XX2FORM(xvrdpi, 0x12, 0x0C, PPC2_VSX),
GEN_XX2FORM(xvrdpic, 0x16, 0x0E, PPC2_VSX),
GEN_XX2FORM(xvrdpim, 0x12, 0x0F, PPC2_VSX),
GEN_XX2FORM(xvrdpip, 0x12, 0x0E, PPC2_VSX),
GEN_XX2FORM(xvrdpiz, 0x12, 0x0D, PPC2_VSX),

GEN_XX3FORM(xvaddsp, 0x00, 0x08, PPC2_VSX),
GEN_XX3FORM(xvsubsp, 0x00, 0x09, PPC2_VSX),
GEN_XX3FORM(xvmulsp, 0x00, 0x0A, PPC2_VSX),
GEN_XX3FORM(xvdivsp, 0x00, 0x0B, PPC2_VSX),
GEN_XX2FORM(xvresp, 0x14, 0x09, PPC2_VSX),
GEN_XX2FORM(xvsqrtsp, 0x16, 0x08, PPC2_VSX),
GEN_XX2FORM(xvrsqrtesp, 0x14, 0x08, PPC2_VSX),
GEN_XX3FORM(xvtdivsp, 0x14, 0x0B, PPC2_VSX),
GEN_XX2FORM(xvtsqrtsp, 0x14, 0x0A, PPC2_VSX),
GEN_XX3FORM(xvmaddasp, 0x04, 0x08, PPC2_VSX),
GEN_XX3FORM(xvmaddmsp, 0x04, 0x09, PPC2_VSX),
GEN_XX3FORM(xvmsubasp, 0x04, 0x0A, PPC2_VSX),
GEN_XX3FORM(xvmsubmsp, 0x04, 0x0B, PPC2_VSX),
GEN_XX3FORM(xvnmaddasp, 0x04, 0x18, PPC2_VSX),
GEN_XX3FORM(xvnmaddmsp, 0x04, 0x19, PPC2_VSX),
GEN_XX3FORM(xvnmsubasp, 0x04, 0x1A, PPC2_VSX),
GEN_XX3FORM(xvnmsubmsp, 0x04, 0x1B, PPC2_VSX),
GEN_XX3FORM(xvmaxsp, 0x00, 0x18, PPC2_VSX),
GEN_XX3FORM(xvminsp, 0x00, 0x19, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpeqsp, 0x0C, 0x08, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpgtsp, 0x0C, 0x09, PPC2_VSX),
GEN_XX3_RC_FORM(xvcmpgesp, 0x0C, 0x0A, PPC2_VSX),
GEN_XX2FORM(xvcvspdp, 0x12, 0x1C, PPC2_VSX),
GEN_XX2FORM(xvcvspsxds, 0x10, 0x19, PPC2_VSX),
GEN_XX2FORM(xvcvspsxws, 0x10, 0x09, PPC2_VSX),
GEN_XX2FORM(xvcvspuxds, 0x10, 0x18, PPC2_VSX),
GEN_XX2FORM(xvcvspuxws, 0x10, 0x08, PPC2_VSX),
GEN_XX2FORM(xvcvsxdsp, 0x10, 0x1B, PPC2_VSX),
GEN_XX2FORM(xvcvuxdsp, 0x10, 0x1A, PPC2_VSX),
GEN_XX2FORM(xvcvsxwsp, 0x10, 0x0B, PPC2_VSX),
GEN_XX2FORM(xvcvuxwsp, 0x10, 0x0A, PPC2_VSX),
GEN_XX2FORM(xvrspi, 0x12, 0x08, PPC2_VSX),
GEN_XX2FORM(xvrspic, 0x16, 0x0A, PPC2_VSX),
GEN_XX2FORM(xvrspim, 0x12, 0x0B, PPC2_VSX),
GEN_XX2FORM(xvrspip, 0x12, 0x0A, PPC2_VSX),
GEN_XX2FORM(xvrspiz, 0x12, 0x09, PPC2_VSX),

#undef VSX_LOGICAL
#define VSX_LOGICAL(name, opc2, opc3, fl2) \
GEN_XX3FORM(name, opc2, opc3, fl2)

VSX_LOGICAL(xxland, 0x8, 0x10, PPC2_VSX),
VSX_LOGICAL(xxlandc, 0x8, 0x11, PPC2_VSX),
VSX_LOGICAL(xxlor, 0x8, 0x12, PPC2_VSX),
VSX_LOGICAL(xxlxor, 0x8, 0x13, PPC2_VSX),
VSX_LOGICAL(xxlnor, 0x8, 0x14, PPC2_VSX),
VSX_LOGICAL(xxleqv, 0x8, 0x17, PPC2_VSX207),
VSX_LOGICAL(xxlnand, 0x8, 0x16, PPC2_VSX207),
VSX_LOGICAL(xxlorc, 0x8, 0x15, PPC2_VSX207),
GEN_XX3FORM(xxmrghw, 0x08, 0x02, PPC2_VSX),
GEN_XX3FORM(xxmrglw, 0x08, 0x06, PPC2_VSX),
GEN_XX2FORM(xxspltw, 0x08, 0x0A, PPC2_VSX),
GEN_XX3FORM_DM(xxsldwi, 0x08, 0x00),

#define GEN_XXSEL_ROW(opc3) \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x18, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x19, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1A, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1B, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1C, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1D, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1E, opc3, 0, PPC_NONE, PPC2_VSX), \
GEN_HANDLER2_E(xxsel, "xxsel", 0x3C, 0x1F, opc3, 0, PPC_NONE, PPC2_VSX), \

GEN_XXSEL_ROW(0x00)
GEN_XXSEL_ROW(0x01)
GEN_XXSEL_ROW(0x02)
GEN_XXSEL_ROW(0x03)
GEN_XXSEL_ROW(0x04)
GEN_XXSEL_ROW(0x05)
GEN_XXSEL_ROW(0x06)
GEN_XXSEL_ROW(0x07)
GEN_XXSEL_ROW(0x08)
GEN_XXSEL_ROW(0x09)
GEN_XXSEL_ROW(0x0A)
GEN_XXSEL_ROW(0x0B)
GEN_XXSEL_ROW(0x0C)
GEN_XXSEL_ROW(0x0D)
GEN_XXSEL_ROW(0x0E)
GEN_XXSEL_ROW(0x0F)
GEN_XXSEL_ROW(0x10)
GEN_XXSEL_ROW(0x11)
GEN_XXSEL_ROW(0x12)
GEN_XXSEL_ROW(0x13)
GEN_XXSEL_ROW(0x14)
GEN_XXSEL_ROW(0x15)
GEN_XXSEL_ROW(0x16)
GEN_XXSEL_ROW(0x17)
GEN_XXSEL_ROW(0x18)
GEN_XXSEL_ROW(0x19)
GEN_XXSEL_ROW(0x1A)
GEN_XXSEL_ROW(0x1B)
GEN_XXSEL_ROW(0x1C)
GEN_XXSEL_ROW(0x1D)
GEN_XXSEL_ROW(0x1E)
GEN_XXSEL_ROW(0x1F)

GEN_XX3FORM_DM(xxpermdi, 0x08, 0x01),

#undef GEN_SPE
#define GEN_SPE(name0, name1, opc2, opc3, inval0, inval1, type) \
    GEN_OPCODE_DUAL(name0##_##name1, 0x04, opc2, opc3, inval0, inval1, type, PPC_NONE)
GEN_SPE(evaddw,      speundef,    0x00, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evaddiw,     speundef,    0x01, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evsubfw,     speundef,    0x02, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evsubifw,    speundef,    0x03, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evabs,       evneg,       0x04, 0x08, 0x0000F800, 0x0000F800, PPC_SPE),
GEN_SPE(evextsb,     evextsh,     0x05, 0x08, 0x0000F800, 0x0000F800, PPC_SPE),
GEN_SPE(evrndw,      evcntlzw,    0x06, 0x08, 0x0000F800, 0x0000F800, PPC_SPE),
GEN_SPE(evcntlsw,    brinc,       0x07, 0x08, 0x0000F800, 0x00000000, PPC_SPE),
GEN_SPE(evmra,       speundef,    0x02, 0x13, 0x0000F800, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(speundef,    evand,       0x08, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE),
GEN_SPE(evandc,      speundef,    0x09, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evxor,       evor,        0x0B, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evnor,       eveqv,       0x0C, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evmwumi,     evmwsmi,     0x0C, 0x11, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evmwumia,    evmwsmia,    0x1C, 0x11, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evmwumiaa,   evmwsmiaa,   0x0C, 0x15, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(speundef,    evorc,       0x0D, 0x08, 0xFFFFFFFF, 0x00000000, PPC_SPE),
GEN_SPE(evnand,      speundef,    0x0F, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evsrwu,      evsrws,      0x10, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evsrwiu,     evsrwis,     0x11, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evslw,       speundef,    0x12, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evslwi,      speundef,    0x13, 0x08, 0x00000000, 0xFFFFFFFF, PPC_SPE),
GEN_SPE(evrlw,       evsplati,    0x14, 0x08, 0x00000000, 0x0000F800, PPC_SPE),
GEN_SPE(evrlwi,      evsplatfi,   0x15, 0x08, 0x00000000, 0x0000F800, PPC_SPE),
GEN_SPE(evmergehi,   evmergelo,   0x16, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evmergehilo, evmergelohi, 0x17, 0x08, 0x00000000, 0x00000000, PPC_SPE),
GEN_SPE(evcmpgtu,    evcmpgts,    0x18, 0x08, 0x00600000, 0x00600000, PPC_SPE),
GEN_SPE(evcmpltu,    evcmplts,    0x19, 0x08, 0x00600000, 0x00600000, PPC_SPE),
GEN_SPE(evcmpeq,     speundef,    0x1A, 0x08, 0x00600000, 0xFFFFFFFF, PPC_SPE),

GEN_SPE(evfsadd,     evfssub,     0x00, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE),
GEN_SPE(evfsabs,     evfsnabs,    0x02, 0x0A, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE),
GEN_SPE(evfsneg,     speundef,    0x03, 0x0A, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(evfsmul,     evfsdiv,     0x04, 0x0A, 0x00000000, 0x00000000, PPC_SPE_SINGLE),
GEN_SPE(evfscmpgt,   evfscmplt,   0x06, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE),
GEN_SPE(evfscmpeq,   speundef,    0x07, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(evfscfui,    evfscfsi,    0x08, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(evfscfuf,    evfscfsf,    0x09, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(evfsctui,    evfsctsi,    0x0A, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(evfsctuf,    evfsctsf,    0x0B, 0x0A, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(evfsctuiz,   speundef,    0x0C, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(evfsctsiz,   speundef,    0x0D, 0x0A, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(evfststgt,   evfststlt,   0x0E, 0x0A, 0x00600000, 0x00600000, PPC_SPE_SINGLE),
GEN_SPE(evfststeq,   speundef,    0x0F, 0x0A, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE),

GEN_SPE(efsadd,      efssub,      0x00, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE),
GEN_SPE(efsabs,      efsnabs,     0x02, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_SINGLE),
GEN_SPE(efsneg,      speundef,    0x03, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(efsmul,      efsdiv,      0x04, 0x0B, 0x00000000, 0x00000000, PPC_SPE_SINGLE),
GEN_SPE(efscmpgt,    efscmplt,    0x06, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE),
GEN_SPE(efscmpeq,    efscfd,      0x07, 0x0B, 0x00600000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(efscfui,     efscfsi,     0x08, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(efscfuf,     efscfsf,     0x09, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(efsctui,     efsctsi,     0x0A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(efsctuf,     efsctsf,     0x0B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_SINGLE),
GEN_SPE(efsctuiz,    speundef,    0x0C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(efsctsiz,    speundef,    0x0D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_SINGLE),
GEN_SPE(efststgt,    efststlt,    0x0E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_SINGLE),
GEN_SPE(efststeq,    speundef,    0x0F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_SINGLE),

GEN_SPE(efdadd,      efdsub,      0x10, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE),
GEN_SPE(efdcfuid,    efdcfsid,    0x11, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdabs,      efdnabs,     0x12, 0x0B, 0x0000F800, 0x0000F800, PPC_SPE_DOUBLE),
GEN_SPE(efdneg,      speundef,    0x13, 0x0B, 0x0000F800, 0xFFFFFFFF, PPC_SPE_DOUBLE),
GEN_SPE(efdmul,      efddiv,      0x14, 0x0B, 0x00000000, 0x00000000, PPC_SPE_DOUBLE),
GEN_SPE(efdctuidz,   efdctsidz,   0x15, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdcmpgt,    efdcmplt,    0x16, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE),
GEN_SPE(efdcmpeq,    efdcfs,      0x17, 0x0B, 0x00600000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdcfui,     efdcfsi,     0x18, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdcfuf,     efdcfsf,     0x19, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdctui,     efdctsi,     0x1A, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdctuf,     efdctsf,     0x1B, 0x0B, 0x00180000, 0x00180000, PPC_SPE_DOUBLE),
GEN_SPE(efdctuiz,    speundef,    0x1C, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE),
GEN_SPE(efdctsiz,    speundef,    0x1D, 0x0B, 0x00180000, 0xFFFFFFFF, PPC_SPE_DOUBLE),
GEN_SPE(efdtstgt,    efdtstlt,    0x1E, 0x0B, 0x00600000, 0x00600000, PPC_SPE_DOUBLE),
GEN_SPE(efdtsteq,    speundef,    0x1F, 0x0B, 0x00600000, 0xFFFFFFFF, PPC_SPE_DOUBLE),

#undef GEN_SPEOP_LDST
#define GEN_SPEOP_LDST(name, opc2, sh)                                        \
GEN_HANDLER(name, 0x04, opc2, 0x0C, 0x00000000, PPC_SPE)
GEN_SPEOP_LDST(evldd, 0x00, 3),
GEN_SPEOP_LDST(evldw, 0x01, 3),
GEN_SPEOP_LDST(evldh, 0x02, 3),
GEN_SPEOP_LDST(evlhhesplat, 0x04, 1),
GEN_SPEOP_LDST(evlhhousplat, 0x06, 1),
GEN_SPEOP_LDST(evlhhossplat, 0x07, 1),
GEN_SPEOP_LDST(evlwhe, 0x08, 2),
GEN_SPEOP_LDST(evlwhou, 0x0A, 2),
GEN_SPEOP_LDST(evlwhos, 0x0B, 2),
GEN_SPEOP_LDST(evlwwsplat, 0x0C, 2),
GEN_SPEOP_LDST(evlwhsplat, 0x0E, 2),

GEN_SPEOP_LDST(evstdd, 0x10, 3),
GEN_SPEOP_LDST(evstdw, 0x11, 3),
GEN_SPEOP_LDST(evstdh, 0x12, 3),
GEN_SPEOP_LDST(evstwhe, 0x18, 2),
GEN_SPEOP_LDST(evstwho, 0x1A, 2),
GEN_SPEOP_LDST(evstwwe, 0x1C, 2),
GEN_SPEOP_LDST(evstwwo, 0x1E, 2),
};

#include "helper_regs.h"
#include "translate_init.c"

/*****************************************************************************/
/* Misc PowerPC helpers */
void ppc_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags)
{
#define RGPL  4
#define RFPL  4

    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "NIP " TARGET_FMT_lx "   LR " TARGET_FMT_lx " CTR "
                TARGET_FMT_lx " XER " TARGET_FMT_lx "\n",
                env->nip, env->lr, env->ctr, cpu_read_xer(env));
    cpu_fprintf(f, "MSR " TARGET_FMT_lx " HID0 " TARGET_FMT_lx "  HF "
                TARGET_FMT_lx " idx %d\n", env->msr, env->spr[SPR_HID0],
                env->hflags, env->mmu_idx);
#if !defined(NO_TIMER_DUMP)
    cpu_fprintf(f, "TB %08" PRIu32 " %08" PRIu64
#if !defined(CONFIG_USER_ONLY)
                " DECR %08" PRIu32
#endif
                "\n",
                cpu_ppc_load_tbu(env), cpu_ppc_load_tbl(env)
#if !defined(CONFIG_USER_ONLY)
                , cpu_ppc_load_decr(env)
#endif
                );
#endif
    for (i = 0; i < 32; i++) {
        if ((i & (RGPL - 1)) == 0)
            cpu_fprintf(f, "GPR%02d", i);
        cpu_fprintf(f, " %016" PRIx64, ppc_dump_gpr(env, i));
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
    cpu_fprintf(f, " ]             RES " TARGET_FMT_lx "\n",
                env->reserve_addr);
    for (i = 0; i < 32; i++) {
        if ((i & (RFPL - 1)) == 0)
            cpu_fprintf(f, "FPR%02d", i);
        cpu_fprintf(f, " %016" PRIx64, *((uint64_t *)&env->fpr[i]));
        if ((i & (RFPL - 1)) == (RFPL - 1))
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "FPSCR " TARGET_FMT_lx "\n", env->fpscr);
#if !defined(CONFIG_USER_ONLY)
    cpu_fprintf(f, " SRR0 " TARGET_FMT_lx "  SRR1 " TARGET_FMT_lx
                   "    PVR " TARGET_FMT_lx " VRSAVE " TARGET_FMT_lx "\n",
                env->spr[SPR_SRR0], env->spr[SPR_SRR1],
                env->spr[SPR_PVR], env->spr[SPR_VRSAVE]);

    cpu_fprintf(f, "SPRG0 " TARGET_FMT_lx " SPRG1 " TARGET_FMT_lx
                   "  SPRG2 " TARGET_FMT_lx "  SPRG3 " TARGET_FMT_lx "\n",
                env->spr[SPR_SPRG0], env->spr[SPR_SPRG1],
                env->spr[SPR_SPRG2], env->spr[SPR_SPRG3]);

    cpu_fprintf(f, "SPRG4 " TARGET_FMT_lx " SPRG5 " TARGET_FMT_lx
                   "  SPRG6 " TARGET_FMT_lx "  SPRG7 " TARGET_FMT_lx "\n",
                env->spr[SPR_SPRG4], env->spr[SPR_SPRG5],
                env->spr[SPR_SPRG6], env->spr[SPR_SPRG7]);

    if (env->excp_model == POWERPC_EXCP_BOOKE) {
        cpu_fprintf(f, "CSRR0 " TARGET_FMT_lx " CSRR1 " TARGET_FMT_lx
                       " MCSRR0 " TARGET_FMT_lx " MCSRR1 " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_CSRR0], env->spr[SPR_BOOKE_CSRR1],
                    env->spr[SPR_BOOKE_MCSRR0], env->spr[SPR_BOOKE_MCSRR1]);

        cpu_fprintf(f, "  TCR " TARGET_FMT_lx "   TSR " TARGET_FMT_lx
                       "    ESR " TARGET_FMT_lx "   DEAR " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_TCR], env->spr[SPR_BOOKE_TSR],
                    env->spr[SPR_BOOKE_ESR], env->spr[SPR_BOOKE_DEAR]);

        cpu_fprintf(f, "  PIR " TARGET_FMT_lx " DECAR " TARGET_FMT_lx
                       "   IVPR " TARGET_FMT_lx "   EPCR " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_PIR], env->spr[SPR_BOOKE_DECAR],
                    env->spr[SPR_BOOKE_IVPR], env->spr[SPR_BOOKE_EPCR]);

        cpu_fprintf(f, " MCSR " TARGET_FMT_lx " SPRG8 " TARGET_FMT_lx
                       "    EPR " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_MCSR], env->spr[SPR_BOOKE_SPRG8],
                    env->spr[SPR_BOOKE_EPR]);

        /* FSL-specific */
        cpu_fprintf(f, " MCAR " TARGET_FMT_lx "  PID1 " TARGET_FMT_lx
                       "   PID2 " TARGET_FMT_lx "    SVR " TARGET_FMT_lx "\n",
                    env->spr[SPR_Exxx_MCAR], env->spr[SPR_BOOKE_PID1],
                    env->spr[SPR_BOOKE_PID2], env->spr[SPR_E500_SVR]);

        /*
         * IVORs are left out as they are large and do not change often --
         * they can be read with "p $ivor0", "p $ivor1", etc.
         */
    }

#if defined(TARGET_PPC64)
    if (env->flags & POWERPC_FLAG_CFAR) {
        cpu_fprintf(f, " CFAR " TARGET_FMT_lx"\n", env->cfar);
    }
#endif

    switch (env->mmu_model) {
    case POWERPC_MMU_32B:
    case POWERPC_MMU_601:
    case POWERPC_MMU_SOFT_6xx:
    case POWERPC_MMU_SOFT_74xx:
#if defined(TARGET_PPC64)
    case POWERPC_MMU_64B:
    case POWERPC_MMU_2_06:
    case POWERPC_MMU_2_06a:
    case POWERPC_MMU_2_06d:
#endif
        cpu_fprintf(f, " SDR1 " TARGET_FMT_lx "   DAR " TARGET_FMT_lx
                       "  DSISR " TARGET_FMT_lx "\n", env->spr[SPR_SDR1],
                    env->spr[SPR_DAR], env->spr[SPR_DSISR]);
        break;
    case POWERPC_MMU_BOOKE206:
        cpu_fprintf(f, " MAS0 " TARGET_FMT_lx "  MAS1 " TARGET_FMT_lx
                       "   MAS2 " TARGET_FMT_lx "   MAS3 " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_MAS0], env->spr[SPR_BOOKE_MAS1],
                    env->spr[SPR_BOOKE_MAS2], env->spr[SPR_BOOKE_MAS3]);

        cpu_fprintf(f, " MAS4 " TARGET_FMT_lx "  MAS6 " TARGET_FMT_lx
                       "   MAS7 " TARGET_FMT_lx "    PID " TARGET_FMT_lx "\n",
                    env->spr[SPR_BOOKE_MAS4], env->spr[SPR_BOOKE_MAS6],
                    env->spr[SPR_BOOKE_MAS7], env->spr[SPR_BOOKE_PID]);

        cpu_fprintf(f, "MMUCFG " TARGET_FMT_lx " TLB0CFG " TARGET_FMT_lx
                       " TLB1CFG " TARGET_FMT_lx "\n",
                    env->spr[SPR_MMUCFG], env->spr[SPR_BOOKE_TLB0CFG],
                    env->spr[SPR_BOOKE_TLB1CFG]);
        break;
    default:
        break;
    }
#endif

#undef RGPL
#undef RFPL
}

void ppc_cpu_dump_statistics(CPUState *cs, FILE*f,
                             fprintf_function cpu_fprintf, int flags)
{
#if defined(DO_PPC_STATISTICS)
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    opc_handler_t **t1, **t2, **t3, *handler;
    int op1, op2, op3;

    t1 = cpu->env.opcodes;
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
                                    "%016" PRIx64 " %" PRId64 "\n",
                                    op1, op2, op3, op1, (op3 << 5) | op2,
                                    handler->oname,
                                    handler->count, handler->count);
                    }
                } else {
                    if (handler->count == 0)
                        continue;
                    cpu_fprintf(f, "%02x %02x    (%02x %04d) %16s: "
                                "%016" PRIx64 " %" PRId64 "\n",
                                op1, op2, op1, op2, handler->oname,
                                handler->count, handler->count);
                }
            }
        } else {
            if (handler->count == 0)
                continue;
            cpu_fprintf(f, "%02x       (%02x     ) %16s: %016" PRIx64
                        " %" PRId64 "\n",
                        op1, op1, handler->oname,
                        handler->count, handler->count);
        }
    }
#endif
}

/*****************************************************************************/
static inline void gen_intermediate_code_internal(PowerPCCPU *cpu,
                                                  TranslationBlock *tb,
                                                  bool search_pc)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;
    DisasContext ctx, *ctxp = &ctx;
    opc_handler_t **table, *handler;
    target_ulong pc_start;
    uint16_t *gen_opc_end;
    CPUBreakpoint *bp;
    int j, lj = -1;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;
    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;
    ctx.nip = pc_start;
    ctx.tb = tb;
    ctx.exception = POWERPC_EXCP_NONE;
    ctx.spr_cb = env->spr_cb;
    ctx.mem_idx = env->mmu_idx;
    ctx.insns_flags = env->insns_flags;
    ctx.insns_flags2 = env->insns_flags2;
    ctx.access_type = -1;
    ctx.le_mode = env->hflags & (1 << MSR_LE) ? 1 : 0;
#if defined(TARGET_PPC64)
    ctx.sf_mode = msr_is_64bit(env, env->msr);
    ctx.has_cfar = !!(env->flags & POWERPC_FLAG_CFAR);
#endif
    ctx.fpu_enabled = msr_fp;
    if ((env->flags & POWERPC_FLAG_SPE) && msr_spe)
        ctx.spe_enabled = msr_spe;
    else
        ctx.spe_enabled = 0;
    if ((env->flags & POWERPC_FLAG_VRE) && msr_vr)
        ctx.altivec_enabled = msr_vr;
    else
        ctx.altivec_enabled = 0;
    if ((env->flags & POWERPC_FLAG_VSX) && msr_vsx) {
        ctx.vsx_enabled = msr_vsx;
    } else {
        ctx.vsx_enabled = 0;
    }
    if ((env->flags & POWERPC_FLAG_SE) && msr_se)
        ctx.singlestep_enabled = CPU_SINGLE_STEP;
    else
        ctx.singlestep_enabled = 0;
    if ((env->flags & POWERPC_FLAG_BE) && msr_be)
        ctx.singlestep_enabled |= CPU_BRANCH_STEP;
    if (unlikely(cs->singlestep_enabled)) {
        ctx.singlestep_enabled |= GDBSTUB_SINGLE_STEP;
    }
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr_se = 1;
#endif
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_tb_start();
    /* Set env in case of segfault during code fetch */
    while (ctx.exception == POWERPC_EXCP_NONE
            && tcg_ctx.gen_opc_ptr < gen_opc_end) {
        if (unlikely(!QTAILQ_EMPTY(&cs->breakpoints))) {
            QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
                if (bp->pc == ctx.nip) {
                    gen_debug_exception(ctxp);
                    break;
                }
            }
        }
        if (unlikely(search_pc)) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    tcg_ctx.gen_opc_instr_start[lj++] = 0;
            }
            tcg_ctx.gen_opc_pc[lj] = ctx.nip;
            tcg_ctx.gen_opc_instr_start[lj] = 1;
            tcg_ctx.gen_opc_icount[lj] = num_insns;
        }
        LOG_DISAS("----------------\n");
        LOG_DISAS("nip=" TARGET_FMT_lx " super=%d ir=%d\n",
                  ctx.nip, ctx.mem_idx, (int)msr_ir);
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();
        if (unlikely(ctx.le_mode)) {
            ctx.opcode = bswap32(cpu_ldl_code(env, ctx.nip));
        } else {
            ctx.opcode = cpu_ldl_code(env, ctx.nip);
        }
        LOG_DISAS("translate opcode %08x (%02x %02x %02x) (%s)\n",
                    ctx.opcode, opc1(ctx.opcode), opc2(ctx.opcode),
                    opc3(ctx.opcode), ctx.le_mode ? "little" : "big");
        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
            tcg_gen_debug_insn_start(ctx.nip);
        }
        ctx.nip += 4;
        table = env->opcodes;
        num_insns++;
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
            if (qemu_log_enabled()) {
                qemu_log("invalid/unsupported opcode: "
                         "%02x - %02x - %02x (%08x) " TARGET_FMT_lx " %d\n",
                         opc1(ctx.opcode), opc2(ctx.opcode),
                         opc3(ctx.opcode), ctx.opcode, ctx.nip - 4, (int)msr_ir);
            }
        } else {
            uint32_t inval;

            if (unlikely(handler->type & (PPC_SPE | PPC_SPE_SINGLE | PPC_SPE_DOUBLE) && Rc(ctx.opcode))) {
                inval = handler->inval2;
            } else {
                inval = handler->inval1;
            }

            if (unlikely((ctx.opcode & inval) != 0)) {
                if (qemu_log_enabled()) {
                    qemu_log("invalid bits: %08x for opcode: "
                             "%02x - %02x - %02x (%08x) " TARGET_FMT_lx "\n",
                             ctx.opcode & inval, opc1(ctx.opcode),
                             opc2(ctx.opcode), opc3(ctx.opcode),
                             ctx.opcode, ctx.nip - 4);
                }
                gen_inval_exception(ctxp, POWERPC_EXCP_INVAL_INVAL);
                break;
            }
        }
        (*(handler->handler))(&ctx);
#if defined(DO_PPC_STATISTICS)
        handler->count++;
#endif
        /* Check trace mode exceptions */
        if (unlikely(ctx.singlestep_enabled & CPU_SINGLE_STEP &&
                     (ctx.nip <= 0x100 || ctx.nip > 0xF00) &&
                     ctx.exception != POWERPC_SYSCALL &&
                     ctx.exception != POWERPC_EXCP_TRAP &&
                     ctx.exception != POWERPC_EXCP_BRANCH)) {
            gen_exception(ctxp, POWERPC_EXCP_TRACE);
        } else if (unlikely(((ctx.nip & (TARGET_PAGE_SIZE - 1)) == 0) ||
                            (cs->singlestep_enabled) ||
                            singlestep ||
                            num_insns >= max_insns)) {
            /* if we reach a page boundary or are single stepping, stop
             * generation
             */
            break;
        }
    }
    if (tb->cflags & CF_LAST_IO)
        gen_io_end();
    if (ctx.exception == POWERPC_EXCP_NONE) {
        gen_goto_tb(&ctx, 0, ctx.nip);
    } else if (ctx.exception != POWERPC_EXCP_BRANCH) {
        if (unlikely(cs->singlestep_enabled)) {
            gen_debug_exception(ctxp);
        }
        /* Generate the return instruction */
        tcg_gen_exit_tb(0);
    }
    gen_tb_end(tb, num_insns);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;
    if (unlikely(search_pc)) {
        j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
        lj++;
        while (lj <= j)
            tcg_ctx.gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = ctx.nip - pc_start;
        tb->icount = num_insns;
    }
#if defined(DEBUG_DISAS)
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        int flags;
        flags = env->bfd_mach;
        flags |= ctx.le_mode << 16;
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(env, pc_start, ctx.nip - pc_start, flags);
        qemu_log("\n");
    }
#endif
}

void gen_intermediate_code (CPUPPCState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(ppc_env_get_cpu(env), tb, false);
}

void gen_intermediate_code_pc (CPUPPCState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(ppc_env_get_cpu(env), tb, true);
}

void restore_state_to_opc(CPUPPCState *env, TranslationBlock *tb, int pc_pos)
{
    env->nip = tcg_ctx.gen_opc_pc[pc_pos];
}
