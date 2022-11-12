/*
 *  PowerPC emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright (C) 2011 Freescale Semiconductor, Inc.
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "internal.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"
#include "qemu/atomic128.h"
#include "spr_common.h"
#include "power8-pmu.h"

#include "qemu/qemu-print.h"
#include "qapi/error.h"

#define CPU_SINGLE_STEP 0x1
#define CPU_BRANCH_STEP 0x2

/* Include definitions for instructions classes and implementations flags */
/* #define PPC_DEBUG_DISAS */

#ifdef PPC_DEBUG_DISAS
#  define LOG_DISAS(...) qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif
/*****************************************************************************/
/* Code translation helpers                                                  */

/* global register indexes */
static char cpu_reg_names[10 * 3 + 22 * 4   /* GPR */
                          + 10 * 4 + 22 * 5 /* SPE GPRh */
                          + 8 * 5           /* CRF */];
static TCGv cpu_gpr[32];
static TCGv cpu_gprh[32];
static TCGv_i32 cpu_crf[8];
static TCGv cpu_nip;
static TCGv cpu_msr;
static TCGv cpu_ctr;
static TCGv cpu_lr;
#if defined(TARGET_PPC64)
static TCGv cpu_cfar;
#endif
static TCGv cpu_xer, cpu_so, cpu_ov, cpu_ca, cpu_ov32, cpu_ca32;
static TCGv cpu_reserve;
static TCGv cpu_reserve_val;
static TCGv cpu_reserve_val2;
static TCGv cpu_fpscr;
static TCGv_i32 cpu_access_type;

#include "exec/gen-icount.h"

void ppc_translate_init(void)
{
    int i;
    char *p;
    size_t cpu_reg_names_size;

    p = cpu_reg_names;
    cpu_reg_names_size = sizeof(cpu_reg_names);

    for (i = 0; i < 8; i++) {
        snprintf(p, cpu_reg_names_size, "crf%d", i);
        cpu_crf[i] = tcg_global_mem_new_i32(cpu_env,
                                            offsetof(CPUPPCState, crf[i]), p);
        p += 5;
        cpu_reg_names_size -= 5;
    }

    for (i = 0; i < 32; i++) {
        snprintf(p, cpu_reg_names_size, "r%d", i);
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
                                        offsetof(CPUPPCState, gpr[i]), p);
        p += (i < 10) ? 3 : 4;
        cpu_reg_names_size -= (i < 10) ? 3 : 4;
        snprintf(p, cpu_reg_names_size, "r%dH", i);
        cpu_gprh[i] = tcg_global_mem_new(cpu_env,
                                         offsetof(CPUPPCState, gprh[i]), p);
        p += (i < 10) ? 4 : 5;
        cpu_reg_names_size -= (i < 10) ? 4 : 5;
    }

    cpu_nip = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUPPCState, nip), "nip");

    cpu_msr = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUPPCState, msr), "msr");

    cpu_ctr = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUPPCState, ctr), "ctr");

    cpu_lr = tcg_global_mem_new(cpu_env,
                                offsetof(CPUPPCState, lr), "lr");

#if defined(TARGET_PPC64)
    cpu_cfar = tcg_global_mem_new(cpu_env,
                                  offsetof(CPUPPCState, cfar), "cfar");
#endif

    cpu_xer = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUPPCState, xer), "xer");
    cpu_so = tcg_global_mem_new(cpu_env,
                                offsetof(CPUPPCState, so), "SO");
    cpu_ov = tcg_global_mem_new(cpu_env,
                                offsetof(CPUPPCState, ov), "OV");
    cpu_ca = tcg_global_mem_new(cpu_env,
                                offsetof(CPUPPCState, ca), "CA");
    cpu_ov32 = tcg_global_mem_new(cpu_env,
                                  offsetof(CPUPPCState, ov32), "OV32");
    cpu_ca32 = tcg_global_mem_new(cpu_env,
                                  offsetof(CPUPPCState, ca32), "CA32");

    cpu_reserve = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUPPCState, reserve_addr),
                                     "reserve_addr");
    cpu_reserve_val = tcg_global_mem_new(cpu_env,
                                         offsetof(CPUPPCState, reserve_val),
                                         "reserve_val");
    cpu_reserve_val2 = tcg_global_mem_new(cpu_env,
                                          offsetof(CPUPPCState, reserve_val2),
                                          "reserve_val2");

    cpu_fpscr = tcg_global_mem_new(cpu_env,
                                   offsetof(CPUPPCState, fpscr), "fpscr");

    cpu_access_type = tcg_global_mem_new_i32(cpu_env,
                                             offsetof(CPUPPCState, access_type),
                                             "access_type");
}

/* internal defines */
struct DisasContext {
    DisasContextBase base;
    target_ulong cia;  /* current instruction address */
    uint32_t opcode;
    /* Routine used to access memory */
    bool pr, hv, dr, le_mode;
    bool lazy_tlb_flush;
    bool need_access_type;
    int mem_idx;
    int access_type;
    /* Translation flags */
    MemOp default_tcg_memop_mask;
#if defined(TARGET_PPC64)
    bool sf_mode;
    bool has_cfar;
#endif
    bool fpu_enabled;
    bool altivec_enabled;
    bool vsx_enabled;
    bool spe_enabled;
    bool tm_enabled;
    bool gtse;
    bool hr;
    bool mmcr0_pmcc0;
    bool mmcr0_pmcc1;
    bool mmcr0_pmcjce;
    bool pmc_other;
    bool pmu_insn_cnt;
    ppc_spr_t *spr_cb; /* Needed to check rights for mfspr/mtspr */
    int singlestep_enabled;
    uint32_t flags;
    uint64_t insns_flags;
    uint64_t insns_flags2;
};

#define DISAS_EXIT         DISAS_TARGET_0  /* exit to main loop, pc updated */
#define DISAS_EXIT_UPDATE  DISAS_TARGET_1  /* exit to main loop, pc stale */
#define DISAS_CHAIN        DISAS_TARGET_2  /* lookup next tb, pc updated */
#define DISAS_CHAIN_UPDATE DISAS_TARGET_3  /* lookup next tb, pc stale */

/* Return true iff byteswap is needed in a scalar memop */
static inline bool need_byteswap(const DisasContext *ctx)
{
#if TARGET_BIG_ENDIAN
     return ctx->le_mode;
#else
     return !ctx->le_mode;
#endif
}

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
};

/* SPR load/store helpers */
static inline void gen_load_spr(TCGv t, int reg)
{
    tcg_gen_ld_tl(t, cpu_env, offsetof(CPUPPCState, spr[reg]));
}

static inline void gen_store_spr(int reg, TCGv t)
{
    tcg_gen_st_tl(t, cpu_env, offsetof(CPUPPCState, spr[reg]));
}

static inline void gen_set_access_type(DisasContext *ctx, int access_type)
{
    if (ctx->need_access_type && ctx->access_type != access_type) {
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

static void gen_exception_err(DisasContext *ctx, uint32_t excp, uint32_t error)
{
    TCGv_i32 t0, t1;

    /*
     * These are all synchronous exceptions, we set the PC back to the
     * faulting instruction
     */
    gen_update_nip(ctx, ctx->cia);
    t0 = tcg_const_i32(excp);
    t1 = tcg_const_i32(error);
    gen_helper_raise_exception_err(cpu_env, t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception(DisasContext *ctx, uint32_t excp)
{
    TCGv_i32 t0;

    /*
     * These are all synchronous exceptions, we set the PC back to the
     * faulting instruction
     */
    gen_update_nip(ctx, ctx->cia);
    t0 = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, t0);
    tcg_temp_free_i32(t0);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_nip(DisasContext *ctx, uint32_t excp,
                              target_ulong nip)
{
    TCGv_i32 t0;

    gen_update_nip(ctx, nip);
    t0 = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, t0);
    tcg_temp_free_i32(t0);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_icount_io_start(DisasContext *ctx)
{
    if (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
        /*
         * An I/O instruction must be last in the TB.
         * Chain to the next TB, and let the code from gen_tb_start
         * decide if we need to return to the main loop.
         * Doing this first also allows this value to be overridden.
         */
        ctx->base.is_jmp = DISAS_TOO_MANY;
    }
}

#if !defined(CONFIG_USER_ONLY)
static void gen_ppc_maybe_interrupt(DisasContext *ctx)
{
    gen_icount_io_start(ctx);
    gen_helper_ppc_maybe_interrupt(cpu_env);
}
#endif

/*
 * Tells the caller what is the appropriate exception to generate and prepares
 * SPR registers for this exception.
 *
 * The exception can be either POWERPC_EXCP_TRACE (on most PowerPCs) or
 * POWERPC_EXCP_DEBUG (on BookE).
 */
static uint32_t gen_prep_dbgex(DisasContext *ctx)
{
    if (ctx->flags & POWERPC_FLAG_DE) {
        target_ulong dbsr = 0;
        if (ctx->singlestep_enabled & CPU_SINGLE_STEP) {
            dbsr = DBCR0_ICMP;
        } else {
            /* Must have been branch */
            dbsr = DBCR0_BRT;
        }
        TCGv t0 = tcg_temp_new();
        gen_load_spr(t0, SPR_BOOKE_DBSR);
        tcg_gen_ori_tl(t0, t0, dbsr);
        gen_store_spr(SPR_BOOKE_DBSR, t0);
        tcg_temp_free(t0);
        return POWERPC_EXCP_DEBUG;
    } else {
        return POWERPC_EXCP_TRACE;
    }
}

static void gen_debug_exception(DisasContext *ctx)
{
    gen_helper_raise_exception(cpu_env, tcg_constant_i32(gen_prep_dbgex(ctx)));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static inline void gen_inval_exception(DisasContext *ctx, uint32_t error)
{
    /* Will be converted to program check if needed */
    gen_exception_err(ctx, POWERPC_EXCP_HV_EMU, POWERPC_EXCP_INVAL | error);
}

static inline void gen_priv_exception(DisasContext *ctx, uint32_t error)
{
    gen_exception_err(ctx, POWERPC_EXCP_PROGRAM, POWERPC_EXCP_PRIV | error);
}

static inline void gen_hvpriv_exception(DisasContext *ctx, uint32_t error)
{
    /* Will be converted to program check if needed */
    gen_exception_err(ctx, POWERPC_EXCP_HV_EMU, POWERPC_EXCP_PRIV | error);
}

/*****************************************************************************/
/* SPR READ/WRITE CALLBACKS */

void spr_noaccess(DisasContext *ctx, int gprn, int sprn)
{
#if 0
    sprn = ((sprn >> 5) & 0x1F) | ((sprn & 0x1F) << 5);
    printf("ERROR: try to access SPR %d !\n", sprn);
#endif
}

/* #define PPC_DUMP_SPR_ACCESSES */

/*
 * Generic callbacks:
 * do nothing but store/retrieve spr value
 */
static void spr_load_dump_spr(int sprn)
{
#ifdef PPC_DUMP_SPR_ACCESSES
    TCGv_i32 t0 = tcg_const_i32(sprn);
    gen_helper_load_dump_spr(cpu_env, t0);
    tcg_temp_free_i32(t0);
#endif
}

void spr_read_generic(DisasContext *ctx, int gprn, int sprn)
{
    gen_load_spr(cpu_gpr[gprn], sprn);
    spr_load_dump_spr(sprn);
}

static void spr_store_dump_spr(int sprn)
{
#ifdef PPC_DUMP_SPR_ACCESSES
    TCGv_i32 t0 = tcg_const_i32(sprn);
    gen_helper_store_dump_spr(cpu_env, t0);
    tcg_temp_free_i32(t0);
#endif
}

void spr_write_generic(DisasContext *ctx, int sprn, int gprn)
{
    gen_store_spr(sprn, cpu_gpr[gprn]);
    spr_store_dump_spr(sprn);
}

void spr_write_CTRL(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);

    /*
     * SPR_CTRL writes must force a new translation block,
     * allowing the PMU to calculate the run latch events with
     * more accuracy.
     */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
}

#if !defined(CONFIG_USER_ONLY)
void spr_write_generic32(DisasContext *ctx, int sprn, int gprn)
{
#ifdef TARGET_PPC64
    TCGv t0 = tcg_temp_new();
    tcg_gen_ext32u_tl(t0, cpu_gpr[gprn]);
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
    spr_store_dump_spr(sprn);
#else
    spr_write_generic(ctx, sprn, gprn);
#endif
}

void spr_write_clear(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    gen_load_spr(t0, sprn);
    tcg_gen_neg_tl(t1, cpu_gpr[gprn]);
    tcg_gen_and_tl(t0, t0, t1);
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

void spr_access_nop(DisasContext *ctx, int sprn, int gprn)
{
}

#endif

/* SPR common to all PowerPC */
/* XER */
void spr_read_xer(DisasContext *ctx, int gprn, int sprn)
{
    TCGv dst = cpu_gpr[gprn];
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
    if (is_isa300(ctx)) {
        tcg_gen_shli_tl(t0, cpu_ov32, XER_OV32);
        tcg_gen_or_tl(dst, dst, t0);
        tcg_gen_shli_tl(t0, cpu_ca32, XER_CA32);
        tcg_gen_or_tl(dst, dst, t0);
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

void spr_write_xer(DisasContext *ctx, int sprn, int gprn)
{
    TCGv src = cpu_gpr[gprn];
    /* Write all flags, while reading back check for isa300 */
    tcg_gen_andi_tl(cpu_xer, src,
                    ~((1u << XER_SO) |
                      (1u << XER_OV) | (1u << XER_OV32) |
                      (1u << XER_CA) | (1u << XER_CA32)));
    tcg_gen_extract_tl(cpu_ov32, src, XER_OV32, 1);
    tcg_gen_extract_tl(cpu_ca32, src, XER_CA32, 1);
    tcg_gen_extract_tl(cpu_so, src, XER_SO, 1);
    tcg_gen_extract_tl(cpu_ov, src, XER_OV, 1);
    tcg_gen_extract_tl(cpu_ca, src, XER_CA, 1);
}

/* LR */
void spr_read_lr(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_lr);
}

void spr_write_lr(DisasContext *ctx, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_lr, cpu_gpr[gprn]);
}

/* CFAR */
#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
void spr_read_cfar(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_cfar);
}

void spr_write_cfar(DisasContext *ctx, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_cfar, cpu_gpr[gprn]);
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */

/* CTR */
void spr_read_ctr(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_ctr);
}

void spr_write_ctr(DisasContext *ctx, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_ctr, cpu_gpr[gprn]);
}

/* User read access to SPR */
/* USPRx */
/* UMMCRx */
/* UPMCx */
/* USIA */
/* UDECR */
void spr_read_ureg(DisasContext *ctx, int gprn, int sprn)
{
    gen_load_spr(cpu_gpr[gprn], sprn + 0x10);
}

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
void spr_write_ureg(DisasContext *ctx, int sprn, int gprn)
{
    gen_store_spr(sprn + 0x10, cpu_gpr[gprn]);
}
#endif

/* SPR common to all non-embedded PowerPC */
/* DECR */
#if !defined(CONFIG_USER_ONLY)
void spr_read_decr(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_decr(cpu_gpr[gprn], cpu_env);
}

void spr_write_decr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_decr(cpu_env, cpu_gpr[gprn]);
}
#endif

/* SPR common to all non-embedded PowerPC, except 601 */
/* Time base */
void spr_read_tbl(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_tbl(cpu_gpr[gprn], cpu_env);
}

void spr_read_tbu(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_tbu(cpu_gpr[gprn], cpu_env);
}

void spr_read_atbl(DisasContext *ctx, int gprn, int sprn)
{
    gen_helper_load_atbl(cpu_gpr[gprn], cpu_env);
}

void spr_read_atbu(DisasContext *ctx, int gprn, int sprn)
{
    gen_helper_load_atbu(cpu_gpr[gprn], cpu_env);
}

#if !defined(CONFIG_USER_ONLY)
void spr_write_tbl(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_tbl(cpu_env, cpu_gpr[gprn]);
}

void spr_write_tbu(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_tbu(cpu_env, cpu_gpr[gprn]);
}

void spr_write_atbl(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_atbl(cpu_env, cpu_gpr[gprn]);
}

void spr_write_atbu(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_atbu(cpu_env, cpu_gpr[gprn]);
}

#if defined(TARGET_PPC64)
void spr_read_purr(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_purr(cpu_gpr[gprn], cpu_env);
}

void spr_write_purr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_purr(cpu_env, cpu_gpr[gprn]);
}

/* HDECR */
void spr_read_hdecr(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_hdecr(cpu_gpr[gprn], cpu_env);
}

void spr_write_hdecr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_hdecr(cpu_env, cpu_gpr[gprn]);
}

void spr_read_vtb(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_vtb(cpu_gpr[gprn], cpu_env);
}

void spr_write_vtb(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_vtb(cpu_env, cpu_gpr[gprn]);
}

void spr_write_tbu40(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_tbu40(cpu_env, cpu_gpr[gprn]);
}

#endif
#endif

#if !defined(CONFIG_USER_ONLY)
/* IBAT0U...IBAT0U */
/* IBAT0L...IBAT7L */
void spr_read_ibat(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env,
                  offsetof(CPUPPCState,
                           IBAT[sprn & 1][(sprn - SPR_IBAT0U) / 2]));
}

void spr_read_ibat_h(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env,
                  offsetof(CPUPPCState,
                           IBAT[sprn & 1][((sprn - SPR_IBAT4U) / 2) + 4]));
}

void spr_write_ibatu(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0U) / 2);
    gen_helper_store_ibatu(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_ibatu_h(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_IBAT4U) / 2) + 4);
    gen_helper_store_ibatu(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_ibatl(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0L) / 2);
    gen_helper_store_ibatl(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_ibatl_h(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_IBAT4L) / 2) + 4);
    gen_helper_store_ibatl(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

/* DBAT0U...DBAT7U */
/* DBAT0L...DBAT7L */
void spr_read_dbat(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env,
                  offsetof(CPUPPCState,
                           DBAT[sprn & 1][(sprn - SPR_DBAT0U) / 2]));
}

void spr_read_dbat_h(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env,
                  offsetof(CPUPPCState,
                           DBAT[sprn & 1][((sprn - SPR_DBAT4U) / 2) + 4]));
}

void spr_write_dbatu(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_DBAT0U) / 2);
    gen_helper_store_dbatu(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_dbatu_h(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_DBAT4U) / 2) + 4);
    gen_helper_store_dbatu(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_dbatl(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_DBAT0L) / 2);
    gen_helper_store_dbatl(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

void spr_write_dbatl_h(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_DBAT4L) / 2) + 4);
    gen_helper_store_dbatl(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

/* SDR1 */
void spr_write_sdr1(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_sdr1(cpu_env, cpu_gpr[gprn]);
}

#if defined(TARGET_PPC64)
/* 64 bits PowerPC specific SPRs */
/* PIDR */
void spr_write_pidr(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_pidr(cpu_env, cpu_gpr[gprn]);
}

void spr_write_lpidr(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_lpidr(cpu_env, cpu_gpr[gprn]);
}

void spr_read_hior(DisasContext *ctx, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUPPCState, excp_prefix));
}

void spr_write_hior(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], 0x3FFFFF00000ULL);
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUPPCState, excp_prefix));
    tcg_temp_free(t0);
}
void spr_write_ptcr(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_ptcr(cpu_env, cpu_gpr[gprn]);
}

void spr_write_pcr(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_pcr(cpu_env, cpu_gpr[gprn]);
}

/* DPDES */
void spr_read_dpdes(DisasContext *ctx, int gprn, int sprn)
{
    gen_helper_load_dpdes(cpu_gpr[gprn], cpu_env);
}

void spr_write_dpdes(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_dpdes(cpu_env, cpu_gpr[gprn]);
}
#endif
#endif

/* PowerPC 40x specific registers */
#if !defined(CONFIG_USER_ONLY)
void spr_read_40x_pit(DisasContext *ctx, int gprn, int sprn)
{
    gen_icount_io_start(ctx);
    gen_helper_load_40x_pit(cpu_gpr[gprn], cpu_env);
}

void spr_write_40x_pit(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_40x_pit(cpu_env, cpu_gpr[gprn]);
}

void spr_write_40x_dbcr0(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_store_spr(sprn, cpu_gpr[gprn]);
    gen_helper_store_40x_dbcr0(cpu_env, cpu_gpr[gprn]);
    /* We must stop translation as we may have rebooted */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
}

void spr_write_40x_sler(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_40x_sler(cpu_env, cpu_gpr[gprn]);
}

void spr_write_40x_tcr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_40x_tcr(cpu_env, cpu_gpr[gprn]);
}

void spr_write_40x_tsr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_40x_tsr(cpu_env, cpu_gpr[gprn]);
}

void spr_write_40x_pid(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], 0xFF);
    gen_helper_store_40x_pid(cpu_env, t0);
    tcg_temp_free(t0);
}

void spr_write_booke_tcr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_booke_tcr(cpu_env, cpu_gpr[gprn]);
}

void spr_write_booke_tsr(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_booke_tsr(cpu_env, cpu_gpr[gprn]);
}
#endif

/* PIR */
#if !defined(CONFIG_USER_ONLY)
void spr_write_pir(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], 0xF);
    gen_store_spr(SPR_PIR, t0);
    tcg_temp_free(t0);
}
#endif

/* SPE specific registers */
void spr_read_spefscr(DisasContext *ctx, int gprn, int sprn)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_ld_i32(t0, cpu_env, offsetof(CPUPPCState, spe_fscr));
    tcg_gen_extu_i32_tl(cpu_gpr[gprn], t0);
    tcg_temp_free_i32(t0);
}

void spr_write_spefscr(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[gprn]);
    tcg_gen_st_i32(t0, cpu_env, offsetof(CPUPPCState, spe_fscr));
    tcg_temp_free_i32(t0);
}

#if !defined(CONFIG_USER_ONLY)
/* Callback used to write the exception vector base */
void spr_write_excp_prefix(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUPPCState, ivpr_mask));
    tcg_gen_and_tl(t0, t0, cpu_gpr[gprn]);
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUPPCState, excp_prefix));
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
}

void spr_write_excp_vector(DisasContext *ctx, int sprn, int gprn)
{
    int sprn_offs;

    if (sprn >= SPR_BOOKE_IVOR0 && sprn <= SPR_BOOKE_IVOR15) {
        sprn_offs = sprn - SPR_BOOKE_IVOR0;
    } else if (sprn >= SPR_BOOKE_IVOR32 && sprn <= SPR_BOOKE_IVOR37) {
        sprn_offs = sprn - SPR_BOOKE_IVOR32 + 32;
    } else if (sprn >= SPR_BOOKE_IVOR38 && sprn <= SPR_BOOKE_IVOR42) {
        sprn_offs = sprn - SPR_BOOKE_IVOR38 + 38;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "Trying to write an unknown exception"
                      " vector 0x%03x\n", sprn);
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }

    TCGv t0 = tcg_temp_new();
    tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUPPCState, ivor_mask));
    tcg_gen_and_tl(t0, t0, cpu_gpr[gprn]);
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUPPCState, excp_vectors[sprn_offs]));
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
}
#endif

#ifdef TARGET_PPC64
#ifndef CONFIG_USER_ONLY
void spr_write_amr(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    /*
     * Note, the HV=1 PR=0 case is handled earlier by simply using
     * spr_write_generic for HV mode in the SPR table
     */

    /* Build insertion mask into t1 based on context */
    if (ctx->pr) {
        gen_load_spr(t1, SPR_UAMOR);
    } else {
        gen_load_spr(t1, SPR_AMOR);
    }

    /* Mask new bits into t2 */
    tcg_gen_and_tl(t2, t1, cpu_gpr[gprn]);

    /* Load AMR and clear new bits in t0 */
    gen_load_spr(t0, SPR_AMR);
    tcg_gen_andc_tl(t0, t0, t1);

    /* Or'in new bits and write it out */
    tcg_gen_or_tl(t0, t0, t2);
    gen_store_spr(SPR_AMR, t0);
    spr_store_dump_spr(SPR_AMR);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

void spr_write_uamor(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    /*
     * Note, the HV=1 case is handled earlier by simply using
     * spr_write_generic for HV mode in the SPR table
     */

    /* Build insertion mask into t1 based on context */
    gen_load_spr(t1, SPR_AMOR);

    /* Mask new bits into t2 */
    tcg_gen_and_tl(t2, t1, cpu_gpr[gprn]);

    /* Load AMR and clear new bits in t0 */
    gen_load_spr(t0, SPR_UAMOR);
    tcg_gen_andc_tl(t0, t0, t1);

    /* Or'in new bits and write it out */
    tcg_gen_or_tl(t0, t0, t2);
    gen_store_spr(SPR_UAMOR, t0);
    spr_store_dump_spr(SPR_UAMOR);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

void spr_write_iamr(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    /*
     * Note, the HV=1 case is handled earlier by simply using
     * spr_write_generic for HV mode in the SPR table
     */

    /* Build insertion mask into t1 based on context */
    gen_load_spr(t1, SPR_AMOR);

    /* Mask new bits into t2 */
    tcg_gen_and_tl(t2, t1, cpu_gpr[gprn]);

    /* Load AMR and clear new bits in t0 */
    gen_load_spr(t0, SPR_IAMR);
    tcg_gen_andc_tl(t0, t0, t1);

    /* Or'in new bits and write it out */
    tcg_gen_or_tl(t0, t0, t2);
    gen_store_spr(SPR_IAMR, t0);
    spr_store_dump_spr(SPR_IAMR);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}
#endif
#endif

#ifndef CONFIG_USER_ONLY
void spr_read_thrm(DisasContext *ctx, int gprn, int sprn)
{
    gen_helper_fixup_thrm(cpu_env);
    gen_load_spr(cpu_gpr[gprn], sprn);
    spr_load_dump_spr(sprn);
}
#endif /* !CONFIG_USER_ONLY */

#if !defined(CONFIG_USER_ONLY)
void spr_write_e500_l1csr0(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_gpr[gprn], L1CSR0_DCE | L1CSR0_CPE);
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
}

void spr_write_e500_l1csr1(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_gpr[gprn], L1CSR1_ICE | L1CSR1_CPE);
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
}

void spr_write_e500_l2csr0(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_gpr[gprn],
                    ~(E500_L2CSR0_L2FI | E500_L2CSR0_L2FL | E500_L2CSR0_L2LFC));
    gen_store_spr(sprn, t0);
    tcg_temp_free(t0);
}

void spr_write_booke206_mmucsr0(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_booke206_tlbflush(cpu_env, cpu_gpr[gprn]);
}

void spr_write_booke_pid(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(sprn);
    gen_helper_booke_setpid(cpu_env, t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}
void spr_write_eplc(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_booke_set_eplc(cpu_env, cpu_gpr[gprn]);
}
void spr_write_epsc(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_booke_set_epsc(cpu_env, cpu_gpr[gprn]);
}

#endif

#if !defined(CONFIG_USER_ONLY)
void spr_write_mas73(DisasContext *ctx, int sprn, int gprn)
{
    TCGv val = tcg_temp_new();
    tcg_gen_ext32u_tl(val, cpu_gpr[gprn]);
    gen_store_spr(SPR_BOOKE_MAS3, val);
    tcg_gen_shri_tl(val, cpu_gpr[gprn], 32);
    gen_store_spr(SPR_BOOKE_MAS7, val);
    tcg_temp_free(val);
}

void spr_read_mas73(DisasContext *ctx, int gprn, int sprn)
{
    TCGv mas7 = tcg_temp_new();
    TCGv mas3 = tcg_temp_new();
    gen_load_spr(mas7, SPR_BOOKE_MAS7);
    tcg_gen_shli_tl(mas7, mas7, 32);
    gen_load_spr(mas3, SPR_BOOKE_MAS3);
    tcg_gen_or_tl(cpu_gpr[gprn], mas3, mas7);
    tcg_temp_free(mas3);
    tcg_temp_free(mas7);
}

#endif

#ifdef TARGET_PPC64
static void gen_fscr_facility_check(DisasContext *ctx, int facility_sprn,
                                    int bit, int sprn, int cause)
{
    TCGv_i32 t1 = tcg_const_i32(bit);
    TCGv_i32 t2 = tcg_const_i32(sprn);
    TCGv_i32 t3 = tcg_const_i32(cause);

    gen_helper_fscr_facility_check(cpu_env, t1, t2, t3);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

static void gen_msr_facility_check(DisasContext *ctx, int facility_sprn,
                                   int bit, int sprn, int cause)
{
    TCGv_i32 t1 = tcg_const_i32(bit);
    TCGv_i32 t2 = tcg_const_i32(sprn);
    TCGv_i32 t3 = tcg_const_i32(cause);

    gen_helper_msr_facility_check(cpu_env, t1, t2, t3);

    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t1);
}

void spr_read_prev_upper32(DisasContext *ctx, int gprn, int sprn)
{
    TCGv spr_up = tcg_temp_new();
    TCGv spr = tcg_temp_new();

    gen_load_spr(spr, sprn - 1);
    tcg_gen_shri_tl(spr_up, spr, 32);
    tcg_gen_ext32u_tl(cpu_gpr[gprn], spr_up);

    tcg_temp_free(spr);
    tcg_temp_free(spr_up);
}

void spr_write_prev_upper32(DisasContext *ctx, int sprn, int gprn)
{
    TCGv spr = tcg_temp_new();

    gen_load_spr(spr, sprn - 1);
    tcg_gen_deposit_tl(spr, spr, cpu_gpr[gprn], 32, 32);
    gen_store_spr(sprn - 1, spr);

    tcg_temp_free(spr);
}

#if !defined(CONFIG_USER_ONLY)
void spr_write_hmer(DisasContext *ctx, int sprn, int gprn)
{
    TCGv hmer = tcg_temp_new();

    gen_load_spr(hmer, sprn);
    tcg_gen_and_tl(hmer, cpu_gpr[gprn], hmer);
    gen_store_spr(sprn, hmer);
    spr_store_dump_spr(sprn);
    tcg_temp_free(hmer);
}

void spr_write_lpcr(DisasContext *ctx, int sprn, int gprn)
{
    gen_helper_store_lpcr(cpu_env, cpu_gpr[gprn]);
}
#endif /* !defined(CONFIG_USER_ONLY) */

void spr_read_tar(DisasContext *ctx, int gprn, int sprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_TAR, sprn, FSCR_IC_TAR);
    spr_read_generic(ctx, gprn, sprn);
}

void spr_write_tar(DisasContext *ctx, int sprn, int gprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_TAR, sprn, FSCR_IC_TAR);
    spr_write_generic(ctx, sprn, gprn);
}

void spr_read_tm(DisasContext *ctx, int gprn, int sprn)
{
    gen_msr_facility_check(ctx, SPR_FSCR, MSR_TM, sprn, FSCR_IC_TM);
    spr_read_generic(ctx, gprn, sprn);
}

void spr_write_tm(DisasContext *ctx, int sprn, int gprn)
{
    gen_msr_facility_check(ctx, SPR_FSCR, MSR_TM, sprn, FSCR_IC_TM);
    spr_write_generic(ctx, sprn, gprn);
}

void spr_read_tm_upper32(DisasContext *ctx, int gprn, int sprn)
{
    gen_msr_facility_check(ctx, SPR_FSCR, MSR_TM, sprn, FSCR_IC_TM);
    spr_read_prev_upper32(ctx, gprn, sprn);
}

void spr_write_tm_upper32(DisasContext *ctx, int sprn, int gprn)
{
    gen_msr_facility_check(ctx, SPR_FSCR, MSR_TM, sprn, FSCR_IC_TM);
    spr_write_prev_upper32(ctx, sprn, gprn);
}

void spr_read_ebb(DisasContext *ctx, int gprn, int sprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_EBB, sprn, FSCR_IC_EBB);
    spr_read_generic(ctx, gprn, sprn);
}

void spr_write_ebb(DisasContext *ctx, int sprn, int gprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_EBB, sprn, FSCR_IC_EBB);
    spr_write_generic(ctx, sprn, gprn);
}

void spr_read_ebb_upper32(DisasContext *ctx, int gprn, int sprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_EBB, sprn, FSCR_IC_EBB);
    spr_read_prev_upper32(ctx, gprn, sprn);
}

void spr_write_ebb_upper32(DisasContext *ctx, int sprn, int gprn)
{
    gen_fscr_facility_check(ctx, SPR_FSCR, FSCR_EBB, sprn, FSCR_IC_EBB);
    spr_write_prev_upper32(ctx, sprn, gprn);
}

void spr_read_dexcr_ureg(DisasContext *ctx, int gprn, int sprn)
{
    TCGv t0 = tcg_temp_new();

    /*
     * Access to the (H)DEXCR in problem state is done using separated
     * SPR indexes which are 16 below the SPR indexes which have full
     * access to the (H)DEXCR in privileged state. Problem state can
     * only read bits 32:63, bits 0:31 return 0.
     *
     * See section 9.3.1-9.3.2 of PowerISA v3.1B
     */

    gen_load_spr(t0, sprn + 16);
    tcg_gen_ext32u_tl(cpu_gpr[gprn], t0);

    tcg_temp_free(t0);
}
#endif

#define GEN_HANDLER(name, opc1, opc2, opc3, inval, type)                      \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type, PPC_NONE)

#define GEN_HANDLER_E(name, opc1, opc2, opc3, inval, type, type2)             \
GEN_OPCODE(name, opc1, opc2, opc3, inval, type, type2)

#define GEN_HANDLER2(name, onam, opc1, opc2, opc3, inval, type)               \
GEN_OPCODE2(name, onam, opc1, opc2, opc3, inval, type, PPC_NONE)

#define GEN_HANDLER2_E(name, onam, opc1, opc2, opc3, inval, type, type2)      \
GEN_OPCODE2(name, onam, opc1, opc2, opc3, inval, type, type2)

#define GEN_HANDLER_E_2(name, opc1, opc2, opc3, opc4, inval, type, type2)     \
GEN_OPCODE3(name, opc1, opc2, opc3, opc4, inval, type, type2)

#define GEN_HANDLER2_E_2(name, onam, opc1, opc2, opc3, opc4, inval, typ, typ2) \
GEN_OPCODE4(name, onam, opc1, opc2, opc3, opc4, inval, typ, typ2)

typedef struct opcode_t {
    unsigned char opc1, opc2, opc3, opc4;
#if HOST_LONG_BITS == 64 /* Explicitly align to 64 bits */
    unsigned char pad[4];
#endif
    opc_handler_t handler;
    const char *oname;
} opcode_t;

static void gen_priv_opc(DisasContext *ctx)
{
    gen_priv_exception(ctx, POWERPC_EXCP_PRIV_OPC);
}

/* Helpers for priv. check */
#define GEN_PRIV(CTX)              \
    do {                           \
        gen_priv_opc(CTX); return; \
    } while (0)

#if defined(CONFIG_USER_ONLY)
#define CHK_HV(CTX) GEN_PRIV(CTX)
#define CHK_SV(CTX) GEN_PRIV(CTX)
#define CHK_HVRM(CTX) GEN_PRIV(CTX)
#else
#define CHK_HV(CTX)                         \
    do {                                    \
        if (unlikely(ctx->pr || !ctx->hv)) {\
            GEN_PRIV(CTX);                  \
        }                                   \
    } while (0)
#define CHK_SV(CTX)              \
    do {                         \
        if (unlikely(ctx->pr)) { \
            GEN_PRIV(CTX);       \
        }                        \
    } while (0)
#define CHK_HVRM(CTX)                                   \
    do {                                                \
        if (unlikely(ctx->pr || !ctx->hv || ctx->dr)) { \
            GEN_PRIV(CTX);                              \
        }                                               \
    } while (0)
#endif

#define CHK_NONE(CTX)

/*****************************************************************************/
/* PowerPC instructions table                                                */

#define GEN_OPCODE(name, op1, op2, op3, invl, _typ, _typ2)                    \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .opc4 = 0xff,                                                             \
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
    .opc4 = 0xff,                                                             \
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
    .opc4 = 0xff,                                                             \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = onam,                                                            \
}
#define GEN_OPCODE3(name, op1, op2, op3, op4, invl, _typ, _typ2)              \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .opc4 = op4,                                                              \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = stringify(name),                                                 \
}
#define GEN_OPCODE4(name, onam, op1, op2, op3, op4, invl, _typ, _typ2)        \
{                                                                             \
    .opc1 = op1,                                                              \
    .opc2 = op2,                                                              \
    .opc3 = op3,                                                              \
    .opc4 = op4,                                                              \
    .handler = {                                                              \
        .inval1  = invl,                                                      \
        .type = _typ,                                                         \
        .type2 = _typ2,                                                       \
        .handler = &gen_##name,                                               \
    },                                                                        \
    .oname = onam,                                                            \
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

/***                           Integer comparison                          ***/

static inline void gen_op_cmp(TCGv arg0, TCGv arg1, int s, int crf)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_movi_tl(t0, CRF_EQ);
    tcg_gen_movi_tl(t1, CRF_LT);
    tcg_gen_movcond_tl((s ? TCG_COND_LT : TCG_COND_LTU),
                       t0, arg0, arg1, t1, t0);
    tcg_gen_movi_tl(t1, CRF_GT);
    tcg_gen_movcond_tl((s ? TCG_COND_GT : TCG_COND_GTU),
                       t0, arg0, arg1, t1, t0);

    tcg_gen_trunc_tl_i32(t, t0);
    tcg_gen_trunc_tl_i32(cpu_crf[crf], cpu_so);
    tcg_gen_or_i32(cpu_crf[crf], cpu_crf[crf], t);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free_i32(t);
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

/* cmprb - range comparison: isupper, isaplha, islower*/
static void gen_cmprb(DisasContext *ctx)
{
    TCGv_i32 src1 = tcg_temp_new_i32();
    TCGv_i32 src2 = tcg_temp_new_i32();
    TCGv_i32 src2lo = tcg_temp_new_i32();
    TCGv_i32 src2hi = tcg_temp_new_i32();
    TCGv_i32 crf = cpu_crf[crfD(ctx->opcode)];

    tcg_gen_trunc_tl_i32(src1, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_trunc_tl_i32(src2, cpu_gpr[rB(ctx->opcode)]);

    tcg_gen_andi_i32(src1, src1, 0xFF);
    tcg_gen_ext8u_i32(src2lo, src2);
    tcg_gen_shri_i32(src2, src2, 8);
    tcg_gen_ext8u_i32(src2hi, src2);

    tcg_gen_setcond_i32(TCG_COND_LEU, src2lo, src2lo, src1);
    tcg_gen_setcond_i32(TCG_COND_LEU, src2hi, src1, src2hi);
    tcg_gen_and_i32(crf, src2lo, src2hi);

    if (ctx->opcode & 0x00200000) {
        tcg_gen_shri_i32(src2, src2, 8);
        tcg_gen_ext8u_i32(src2lo, src2);
        tcg_gen_shri_i32(src2, src2, 8);
        tcg_gen_ext8u_i32(src2hi, src2);
        tcg_gen_setcond_i32(TCG_COND_LEU, src2lo, src2lo, src1);
        tcg_gen_setcond_i32(TCG_COND_LEU, src2hi, src1, src2hi);
        tcg_gen_and_i32(src2lo, src2lo, src2hi);
        tcg_gen_or_i32(crf, crf, src2lo);
    }
    tcg_gen_shli_i32(crf, crf, CRF_GT_BIT);
    tcg_temp_free_i32(src1);
    tcg_temp_free_i32(src2);
    tcg_temp_free_i32(src2lo);
    tcg_temp_free_i32(src2hi);
}

#if defined(TARGET_PPC64)
/* cmpeqb */
static void gen_cmpeqb(DisasContext *ctx)
{
    gen_helper_cmpeqb(cpu_crf[crfD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                      cpu_gpr[rB(ctx->opcode)]);
}
#endif

/* isel (PowerPC 2.03 specification) */
static void gen_isel(DisasContext *ctx)
{
    uint32_t bi = rC(ctx->opcode);
    uint32_t mask = 0x08 >> (bi & 0x03);
    TCGv t0 = tcg_temp_new();
    TCGv zr;

    tcg_gen_extu_i32_tl(t0, cpu_crf[bi >> 2]);
    tcg_gen_andi_tl(t0, t0, mask);

    zr = tcg_const_tl(0);
    tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[rD(ctx->opcode)], t0, zr,
                       rA(ctx->opcode) ? cpu_gpr[rA(ctx->opcode)] : zr,
                       cpu_gpr[rB(ctx->opcode)]);
    tcg_temp_free(zr);
    tcg_temp_free(t0);
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
        tcg_gen_extract_tl(cpu_ov, cpu_ov, 31, 1);
        if (is_isa300(ctx)) {
            tcg_gen_mov_tl(cpu_ov32, cpu_ov);
        }
    } else {
        if (is_isa300(ctx)) {
            tcg_gen_extract_tl(cpu_ov32, cpu_ov, 31, 1);
        }
        tcg_gen_extract_tl(cpu_ov, cpu_ov, TARGET_LONG_BITS - 1, 1);
    }
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);
}

static inline void gen_op_arith_compute_ca32(DisasContext *ctx,
                                             TCGv res, TCGv arg0, TCGv arg1,
                                             TCGv ca32, int sub)
{
    TCGv t0;

    if (!is_isa300(ctx)) {
        return;
    }

    t0 = tcg_temp_new();
    if (sub) {
        tcg_gen_eqv_tl(t0, arg0, arg1);
    } else {
        tcg_gen_xor_tl(t0, arg0, arg1);
    }
    tcg_gen_xor_tl(t0, t0, res);
    tcg_gen_extract_tl(ca32, t0, 32, 1);
    tcg_temp_free(t0);
}

/* Common add function */
static inline void gen_op_arith_add(DisasContext *ctx, TCGv ret, TCGv arg1,
                                    TCGv arg2, TCGv ca, TCGv ca32,
                                    bool add_ca, bool compute_ca,
                                    bool compute_ov, bool compute_rc0)
{
    TCGv t0 = ret;

    if (compute_ca || compute_ov) {
        t0 = tcg_temp_new();
    }

    if (compute_ca) {
        if (NARROW_MODE(ctx)) {
            /*
             * Caution: a non-obvious corner case of the spec is that
             * we must produce the *entire* 64-bit addition, but
             * produce the carry into bit 32.
             */
            TCGv t1 = tcg_temp_new();
            tcg_gen_xor_tl(t1, arg1, arg2);        /* add without carry */
            tcg_gen_add_tl(t0, arg1, arg2);
            if (add_ca) {
                tcg_gen_add_tl(t0, t0, ca);
            }
            tcg_gen_xor_tl(ca, t0, t1);        /* bits changed w/ carry */
            tcg_temp_free(t1);
            tcg_gen_extract_tl(ca, ca, 32, 1);
            if (is_isa300(ctx)) {
                tcg_gen_mov_tl(ca32, ca);
            }
        } else {
            TCGv zero = tcg_const_tl(0);
            if (add_ca) {
                tcg_gen_add2_tl(t0, ca, arg1, zero, ca, zero);
                tcg_gen_add2_tl(t0, ca, t0, ca, arg2, zero);
            } else {
                tcg_gen_add2_tl(t0, ca, arg1, zero, arg2, zero);
            }
            gen_op_arith_compute_ca32(ctx, t0, arg1, arg2, ca32, 0);
            tcg_temp_free(zero);
        }
    } else {
        tcg_gen_add_tl(t0, arg1, arg2);
        if (add_ca) {
            tcg_gen_add_tl(t0, t0, ca);
        }
    }

    if (compute_ov) {
        gen_op_arith_compute_ov(ctx, t0, arg1, arg2, 0);
    }
    if (unlikely(compute_rc0)) {
        gen_set_Rc0(ctx, t0);
    }

    if (t0 != ret) {
        tcg_gen_mov_tl(ret, t0);
        tcg_temp_free(t0);
    }
}
/* Add functions with two operands */
#define GEN_INT_ARITH_ADD(name, opc3, ca, add_ca, compute_ca, compute_ov)     \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)],                           \
                     cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],      \
                     ca, glue(ca, 32),                                        \
                     add_ca, compute_ca, compute_ov, Rc(ctx->opcode));        \
}
/* Add functions with one operand and one immediate */
#define GEN_INT_ARITH_ADD_CONST(name, opc3, const_val, ca,                    \
                                add_ca, compute_ca, compute_ov)               \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    TCGv t0 = tcg_const_tl(const_val);                                        \
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)],                           \
                     cpu_gpr[rA(ctx->opcode)], t0,                            \
                     ca, glue(ca, 32),                                        \
                     add_ca, compute_ca, compute_ov, Rc(ctx->opcode));        \
    tcg_temp_free(t0);                                                        \
}

/* add  add.  addo  addo. */
GEN_INT_ARITH_ADD(add, 0x08, cpu_ca, 0, 0, 0)
GEN_INT_ARITH_ADD(addo, 0x18, cpu_ca, 0, 0, 1)
/* addc  addc.  addco  addco. */
GEN_INT_ARITH_ADD(addc, 0x00, cpu_ca, 0, 1, 0)
GEN_INT_ARITH_ADD(addco, 0x10, cpu_ca, 0, 1, 1)
/* adde  adde.  addeo  addeo. */
GEN_INT_ARITH_ADD(adde, 0x04, cpu_ca, 1, 1, 0)
GEN_INT_ARITH_ADD(addeo, 0x14, cpu_ca, 1, 1, 1)
/* addme  addme.  addmeo  addmeo.  */
GEN_INT_ARITH_ADD_CONST(addme, 0x07, -1LL, cpu_ca, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addmeo, 0x17, -1LL, cpu_ca, 1, 1, 1)
/* addex */
GEN_INT_ARITH_ADD(addex, 0x05, cpu_ov, 1, 1, 0);
/* addze  addze.  addzeo  addzeo.*/
GEN_INT_ARITH_ADD_CONST(addze, 0x06, 0, cpu_ca, 1, 1, 0)
GEN_INT_ARITH_ADD_CONST(addzeo, 0x16, 0, cpu_ca, 1, 1, 1)
/* addic  addic.*/
static inline void gen_op_addic(DisasContext *ctx, bool compute_rc0)
{
    TCGv c = tcg_const_tl(SIMM(ctx->opcode));
    gen_op_arith_add(ctx, cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                     c, cpu_ca, cpu_ca32, 0, 1, 0, compute_rc0);
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

static inline void gen_op_arith_divw(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, int sign, int compute_ov)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, arg1);
    tcg_gen_trunc_tl_i32(t1, arg2);
    if (sign) {
        tcg_gen_setcondi_i32(TCG_COND_EQ, t2, t0, INT_MIN);
        tcg_gen_setcondi_i32(TCG_COND_EQ, t3, t1, -1);
        tcg_gen_and_i32(t2, t2, t3);
        tcg_gen_setcondi_i32(TCG_COND_EQ, t3, t1, 0);
        tcg_gen_or_i32(t2, t2, t3);
        tcg_gen_movi_i32(t3, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_div_i32(t3, t0, t1);
        tcg_gen_extu_i32_tl(ret, t3);
    } else {
        tcg_gen_setcondi_i32(TCG_COND_EQ, t2, t1, 0);
        tcg_gen_movi_i32(t3, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_divu_i32(t3, t0, t1);
        tcg_gen_extu_i32_tl(ret, t3);
    }
    if (compute_ov) {
        tcg_gen_extu_i32_tl(cpu_ov, t2);
        if (is_isa300(ctx)) {
            tcg_gen_extu_i32_tl(cpu_ov32, t2);
        }
        tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);
    }
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);

    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, ret);
    }
}
/* Div functions */
#define GEN_INT_ARITH_DIVW(name, opc3, sign, compute_ov)                      \
static void glue(gen_, name)(DisasContext *ctx)                               \
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
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();

    tcg_gen_mov_i64(t0, arg1);
    tcg_gen_mov_i64(t1, arg2);
    if (sign) {
        tcg_gen_setcondi_i64(TCG_COND_EQ, t2, t0, INT64_MIN);
        tcg_gen_setcondi_i64(TCG_COND_EQ, t3, t1, -1);
        tcg_gen_and_i64(t2, t2, t3);
        tcg_gen_setcondi_i64(TCG_COND_EQ, t3, t1, 0);
        tcg_gen_or_i64(t2, t2, t3);
        tcg_gen_movi_i64(t3, 0);
        tcg_gen_movcond_i64(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_div_i64(ret, t0, t1);
    } else {
        tcg_gen_setcondi_i64(TCG_COND_EQ, t2, t1, 0);
        tcg_gen_movi_i64(t3, 0);
        tcg_gen_movcond_i64(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_divu_i64(ret, t0, t1);
    }
    if (compute_ov) {
        tcg_gen_mov_tl(cpu_ov, t2);
        if (is_isa300(ctx)) {
            tcg_gen_mov_tl(cpu_ov32, t2);
        }
        tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);

    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, ret);
    }
}

#define GEN_INT_ARITH_DIVD(name, opc3, sign, compute_ov)                      \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    gen_op_arith_divd(ctx, cpu_gpr[rD(ctx->opcode)],                          \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],     \
                      sign, compute_ov);                                      \
}
/* divdu  divdu.  divduo  divduo.   */
GEN_INT_ARITH_DIVD(divdu, 0x0E, 0, 0);
GEN_INT_ARITH_DIVD(divduo, 0x1E, 0, 1);
/* divd  divd.  divdo  divdo.   */
GEN_INT_ARITH_DIVD(divd, 0x0F, 1, 0);
GEN_INT_ARITH_DIVD(divdo, 0x1F, 1, 1);

GEN_DIVE(divdeu, divdeu, 0);
GEN_DIVE(divdeuo, divdeu, 1);
GEN_DIVE(divde, divde, 0);
GEN_DIVE(divdeo, divde, 1);
#endif

static inline void gen_op_arith_modw(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, int sign)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, arg1);
    tcg_gen_trunc_tl_i32(t1, arg2);
    if (sign) {
        TCGv_i32 t2 = tcg_temp_new_i32();
        TCGv_i32 t3 = tcg_temp_new_i32();
        tcg_gen_setcondi_i32(TCG_COND_EQ, t2, t0, INT_MIN);
        tcg_gen_setcondi_i32(TCG_COND_EQ, t3, t1, -1);
        tcg_gen_and_i32(t2, t2, t3);
        tcg_gen_setcondi_i32(TCG_COND_EQ, t3, t1, 0);
        tcg_gen_or_i32(t2, t2, t3);
        tcg_gen_movi_i32(t3, 0);
        tcg_gen_movcond_i32(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_rem_i32(t3, t0, t1);
        tcg_gen_ext_i32_tl(ret, t3);
        tcg_temp_free_i32(t2);
        tcg_temp_free_i32(t3);
    } else {
        TCGv_i32 t2 = tcg_const_i32(1);
        TCGv_i32 t3 = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_EQ, t1, t1, t3, t2, t1);
        tcg_gen_remu_i32(t3, t0, t1);
        tcg_gen_extu_i32_tl(ret, t3);
        tcg_temp_free_i32(t2);
        tcg_temp_free_i32(t3);
    }
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

#define GEN_INT_ARITH_MODW(name, opc3, sign)                                \
static void glue(gen_, name)(DisasContext *ctx)                             \
{                                                                           \
    gen_op_arith_modw(ctx, cpu_gpr[rD(ctx->opcode)],                        \
                      cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],   \
                      sign);                                                \
}

GEN_INT_ARITH_MODW(moduw, 0x08, 0);
GEN_INT_ARITH_MODW(modsw, 0x18, 1);

#if defined(TARGET_PPC64)
static inline void gen_op_arith_modd(DisasContext *ctx, TCGv ret, TCGv arg1,
                                     TCGv arg2, int sign)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_mov_i64(t0, arg1);
    tcg_gen_mov_i64(t1, arg2);
    if (sign) {
        TCGv_i64 t2 = tcg_temp_new_i64();
        TCGv_i64 t3 = tcg_temp_new_i64();
        tcg_gen_setcondi_i64(TCG_COND_EQ, t2, t0, INT64_MIN);
        tcg_gen_setcondi_i64(TCG_COND_EQ, t3, t1, -1);
        tcg_gen_and_i64(t2, t2, t3);
        tcg_gen_setcondi_i64(TCG_COND_EQ, t3, t1, 0);
        tcg_gen_or_i64(t2, t2, t3);
        tcg_gen_movi_i64(t3, 0);
        tcg_gen_movcond_i64(TCG_COND_NE, t1, t2, t3, t2, t1);
        tcg_gen_rem_i64(ret, t0, t1);
        tcg_temp_free_i64(t2);
        tcg_temp_free_i64(t3);
    } else {
        TCGv_i64 t2 = tcg_const_i64(1);
        TCGv_i64 t3 = tcg_const_i64(0);
        tcg_gen_movcond_i64(TCG_COND_EQ, t1, t1, t3, t2, t1);
        tcg_gen_remu_i64(ret, t0, t1);
        tcg_temp_free_i64(t2);
        tcg_temp_free_i64(t3);
    }
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

#define GEN_INT_ARITH_MODD(name, opc3, sign)                            \
static void glue(gen_, name)(DisasContext *ctx)                           \
{                                                                         \
  gen_op_arith_modd(ctx, cpu_gpr[rD(ctx->opcode)],                        \
                    cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],   \
                    sign);                                                \
}

GEN_INT_ARITH_MODD(modud, 0x08, 0);
GEN_INT_ARITH_MODD(modsd, 0x18, 1);
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

/* mullw  mullw. */
static void gen_mullw(DisasContext *ctx)
{
#if defined(TARGET_PPC64)
    TCGv_i64 t0, t1;
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    tcg_gen_ext32s_tl(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_ext32s_tl(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mul_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
#else
    tcg_gen_mul_i32(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)],
                    cpu_gpr[rB(ctx->opcode)]);
#endif
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

/* mullwo  mullwo. */
static void gen_mullwo(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, cpu_gpr[rA(ctx->opcode)]);
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_muls2_i32(t0, t1, t0, t1);
#if defined(TARGET_PPC64)
    tcg_gen_concat_i32_i64(cpu_gpr[rD(ctx->opcode)], t0, t1);
#else
    tcg_gen_mov_i32(cpu_gpr[rD(ctx->opcode)], t0);
#endif

    tcg_gen_sari_i32(t0, t0, 31);
    tcg_gen_setcond_i32(TCG_COND_NE, t0, t0, t1);
    tcg_gen_extu_i32_tl(cpu_ov, t0);
    if (is_isa300(ctx)) {
        tcg_gen_mov_tl(cpu_ov32, cpu_ov);
    }
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

/* mulldo  mulldo. */
static void gen_mulldo(DisasContext *ctx)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_muls2_i64(t0, t1, cpu_gpr[rA(ctx->opcode)],
                      cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_mov_i64(cpu_gpr[rD(ctx->opcode)], t0);

    tcg_gen_sari_i64(t0, t0, 63);
    tcg_gen_setcond_i64(TCG_COND_NE, cpu_ov, t0, t1);
    if (is_isa300(ctx)) {
        tcg_gen_mov_tl(cpu_ov32, cpu_ov);
    }
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);

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
            /*
             * Caution: a non-obvious corner case of the spec is that
             * we must produce the *entire* 64-bit addition, but
             * produce the carry into bit 32.
             */
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
            tcg_temp_free(inv1);
            tcg_gen_xor_tl(cpu_ca, t0, t1);         /* bits changes w/ carry */
            tcg_temp_free(t1);
            tcg_gen_extract_tl(cpu_ca, cpu_ca, 32, 1);
            if (is_isa300(ctx)) {
                tcg_gen_mov_tl(cpu_ca32, cpu_ca);
            }
        } else if (add_ca) {
            TCGv zero, inv1 = tcg_temp_new();
            tcg_gen_not_tl(inv1, arg1);
            zero = tcg_const_tl(0);
            tcg_gen_add2_tl(t0, cpu_ca, arg2, zero, cpu_ca, zero);
            tcg_gen_add2_tl(t0, cpu_ca, t0, cpu_ca, inv1, zero);
            gen_op_arith_compute_ca32(ctx, t0, inv1, arg2, cpu_ca32, 0);
            tcg_temp_free(zero);
            tcg_temp_free(inv1);
        } else {
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_ca, arg2, arg1);
            tcg_gen_sub_tl(t0, arg2, arg1);
            gen_op_arith_compute_ca32(ctx, t0, arg1, arg2, cpu_ca32, 1);
        }
    } else if (add_ca) {
        /*
         * Since we're ignoring carry-out, we can simplify the
         * standard ~arg1 + arg2 + ca to arg2 - arg1 + ca - 1.
         */
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

    if (t0 != ret) {
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
    tcg_gen_neg_tl(cpu_gpr[rD(ctx->opcode)], cpu_gpr[rA(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode))) {
        gen_set_Rc0(ctx, cpu_gpr[rD(ctx->opcode)]);
    }
}

static void gen_nego(DisasContext *ctx)
{
    gen_op_arith_neg(ctx, 1);
}

/***                            Integer logical                            ***/
#define GEN_LOGICAL2(name, tcg_op, opc, type)                                 \
static void glue(gen_, name)(DisasContext *ctx)                               \
{                                                                             \
    tcg_op(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],                \
       cpu_gpr[rB(ctx->opcode)]);                                             \
    if (unlikely(Rc(ctx->opcode) != 0))                                       \
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);                           \
}

#define GEN_LOGICAL1(name, tcg_op, opc, type)                                 \
static void glue(gen_, name)(DisasContext *ctx)                               \
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
    tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                    UIMM(ctx->opcode));
    gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* andis. */
static void gen_andis_(DisasContext *ctx)
{
    tcg_gen_andi_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                    UIMM(ctx->opcode) << 16);
    gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
}

/* cntlzw */
static void gen_cntlzw(DisasContext *ctx)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t, cpu_gpr[rS(ctx->opcode)]);
    tcg_gen_clzi_i32(t, t, 32);
    tcg_gen_extu_i32_tl(cpu_gpr[rA(ctx->opcode)], t);
    tcg_temp_free_i32(t);

    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* cnttzw */
static void gen_cnttzw(DisasContext *ctx)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t, cpu_gpr[rS(ctx->opcode)]);
    tcg_gen_ctzi_i32(t, t, 32);
    tcg_gen_extu_i32_tl(cpu_gpr[rA(ctx->opcode)], t);
    tcg_temp_free_i32(t);

    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
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

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
static void gen_pause(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_const_i32(0);
    tcg_gen_st_i32(t0, cpu_env,
                   -offsetof(PowerPCCPU, env) + offsetof(CPUState, halted));
    tcg_temp_free_i32(t0);

    /* Stop translation, this gives other CPUs a chance to run */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
}
#endif /* defined(TARGET_PPC64) */

/* or & or. */
static void gen_or(DisasContext *ctx)
{
    int rs, ra, rb;

    rs = rS(ctx->opcode);
    ra = rA(ctx->opcode);
    rb = rB(ctx->opcode);
    /* Optimisation for mr. ri case */
    if (rs != ra || rs != rb) {
        if (rs != rb) {
            tcg_gen_or_tl(cpu_gpr[ra], cpu_gpr[rs], cpu_gpr[rb]);
        } else {
            tcg_gen_mov_tl(cpu_gpr[ra], cpu_gpr[rs]);
        }
        if (unlikely(Rc(ctx->opcode) != 0)) {
            gen_set_Rc0(ctx, cpu_gpr[ra]);
        }
    } else if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rs]);
#if defined(TARGET_PPC64)
    } else if (rs != 0) { /* 0 is nop */
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
            if (!ctx->pr) {
                /* Set process priority to very low */
                prio = 1;
            }
            break;
        case 5:
            if (!ctx->pr) {
                /* Set process priority to medium-hight */
                prio = 5;
            }
            break;
        case 3:
            if (!ctx->pr) {
                /* Set process priority to high */
                prio = 6;
            }
            break;
        case 7:
            if (ctx->hv && !ctx->pr) {
                /* Set process priority to very high */
                prio = 7;
            }
            break;
#endif
        default:
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
#if !defined(CONFIG_USER_ONLY)
        /*
         * Pause out of TCG otherwise spin loops with smt_low eat too
         * much CPU and the kernel hangs.  This applies to all
         * encodings other than no-op, e.g., miso(rs=26), yield(27),
         * mdoio(29), mdoom(30), and all currently undefined.
         */
        gen_pause(ctx);
#endif
#endif
    }
}
/* orc & orc. */
GEN_LOGICAL2(orc, tcg_gen_orc_tl, 0x0C, PPC_INTEGER);

/* xor & xor. */
static void gen_xor(DisasContext *ctx)
{
    /* Optimisation for "set to zero" case */
    if (rS(ctx->opcode) != rB(ctx->opcode)) {
        tcg_gen_xor_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                       cpu_gpr[rB(ctx->opcode)]);
    } else {
        tcg_gen_movi_tl(cpu_gpr[rA(ctx->opcode)], 0);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* ori */
static void gen_ori(DisasContext *ctx)
{
    target_ulong uimm = UIMM(ctx->opcode);

    if (rS(ctx->opcode) == rA(ctx->opcode) && uimm == 0) {
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
    tcg_gen_ori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                   uimm << 16);
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
    tcg_gen_xori_tl(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)],
                    uimm << 16);
}

/* popcntb : PowerPC 2.03 specification */
static void gen_popcntb(DisasContext *ctx)
{
    gen_helper_popcntb(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
}

static void gen_popcntw(DisasContext *ctx)
{
#if defined(TARGET_PPC64)
    gen_helper_popcntw(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
#else
    tcg_gen_ctpop_i32(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
#endif
}

#if defined(TARGET_PPC64)
/* popcntd: PowerPC 2.06 specification */
static void gen_popcntd(DisasContext *ctx)
{
    tcg_gen_ctpop_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
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
    tcg_gen_clzi_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], 64);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* cnttzd */
static void gen_cnttzd(DisasContext *ctx)
{
    tcg_gen_ctzi_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)], 64);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* darn */
static void gen_darn(DisasContext *ctx)
{
    int l = L(ctx->opcode);

    if (l > 2) {
        tcg_gen_movi_i64(cpu_gpr[rD(ctx->opcode)], -1);
    } else {
        gen_icount_io_start(ctx);
        if (l == 0) {
            gen_helper_darn32(cpu_gpr[rD(ctx->opcode)]);
        } else {
            /* Return 64-bit random for both CRN and RRN */
            gen_helper_darn64(cpu_gpr[rD(ctx->opcode)]);
        }
    }
}
#endif

/***                             Integer rotate                            ***/

/* rlwimi & rlwimi. */
static void gen_rlwimi(DisasContext *ctx)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    uint32_t sh = SH(ctx->opcode);
    uint32_t mb = MB(ctx->opcode);
    uint32_t me = ME(ctx->opcode);

    if (sh == (31 - me) && mb <= me) {
        tcg_gen_deposit_tl(t_ra, t_ra, t_rs, sh, me - mb + 1);
    } else {
        target_ulong mask;
        bool mask_in_32b = true;
        TCGv t1;

#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        mask = MASK(mb, me);

#if defined(TARGET_PPC64)
        if (mask > 0xffffffffu) {
            mask_in_32b = false;
        }
#endif
        t1 = tcg_temp_new();
        if (mask_in_32b) {
            TCGv_i32 t0 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t0, t_rs);
            tcg_gen_rotli_i32(t0, t0, sh);
            tcg_gen_extu_i32_tl(t1, t0);
            tcg_temp_free_i32(t0);
        } else {
#if defined(TARGET_PPC64)
            tcg_gen_deposit_i64(t1, t_rs, t_rs, 32, 32);
            tcg_gen_rotli_i64(t1, t1, sh);
#else
            g_assert_not_reached();
#endif
        }

        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_andi_tl(t_ra, t_ra, ~mask);
        tcg_gen_or_tl(t_ra, t_ra, t1);
        tcg_temp_free(t1);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
}

/* rlwinm & rlwinm. */
static void gen_rlwinm(DisasContext *ctx)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    int sh = SH(ctx->opcode);
    int mb = MB(ctx->opcode);
    int me = ME(ctx->opcode);
    int len = me - mb + 1;
    int rsh = (32 - sh) & 31;

    if (sh != 0 && len > 0 && me == (31 - sh)) {
        tcg_gen_deposit_z_tl(t_ra, t_rs, sh, len);
    } else if (me == 31 && rsh + len <= 32) {
        tcg_gen_extract_tl(t_ra, t_rs, rsh, len);
    } else {
        target_ulong mask;
        bool mask_in_32b = true;
#if defined(TARGET_PPC64)
        mb += 32;
        me += 32;
#endif
        mask = MASK(mb, me);
#if defined(TARGET_PPC64)
        if (mask > 0xffffffffu) {
            mask_in_32b = false;
        }
#endif
        if (mask_in_32b) {
            if (sh == 0) {
                tcg_gen_andi_tl(t_ra, t_rs, mask);
            } else {
                TCGv_i32 t0 = tcg_temp_new_i32();
                tcg_gen_trunc_tl_i32(t0, t_rs);
                tcg_gen_rotli_i32(t0, t0, sh);
                tcg_gen_andi_i32(t0, t0, mask);
                tcg_gen_extu_i32_tl(t_ra, t0);
                tcg_temp_free_i32(t0);
            }
        } else {
#if defined(TARGET_PPC64)
            tcg_gen_deposit_i64(t_ra, t_rs, t_rs, 32, 32);
            tcg_gen_rotli_i64(t_ra, t_ra, sh);
            tcg_gen_andi_i64(t_ra, t_ra, mask);
#else
            g_assert_not_reached();
#endif
        }
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
}

/* rlwnm & rlwnm. */
static void gen_rlwnm(DisasContext *ctx)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    TCGv t_rb = cpu_gpr[rB(ctx->opcode)];
    uint32_t mb = MB(ctx->opcode);
    uint32_t me = ME(ctx->opcode);
    target_ulong mask;
    bool mask_in_32b = true;

#if defined(TARGET_PPC64)
    mb += 32;
    me += 32;
#endif
    mask = MASK(mb, me);

#if defined(TARGET_PPC64)
    if (mask > 0xffffffffu) {
        mask_in_32b = false;
    }
#endif
    if (mask_in_32b) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(t0, t_rb);
        tcg_gen_trunc_tl_i32(t1, t_rs);
        tcg_gen_andi_i32(t0, t0, 0x1f);
        tcg_gen_rotl_i32(t1, t1, t0);
        tcg_gen_extu_i32_tl(t_ra, t1);
        tcg_temp_free_i32(t0);
        tcg_temp_free_i32(t1);
    } else {
#if defined(TARGET_PPC64)
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_andi_i64(t0, t_rb, 0x1f);
        tcg_gen_deposit_i64(t_ra, t_rs, t_rs, 32, 32);
        tcg_gen_rotl_i64(t_ra, t_ra, t0);
        tcg_temp_free_i64(t0);
#else
        g_assert_not_reached();
#endif
    }

    tcg_gen_andi_tl(t_ra, t_ra, mask);

    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
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

static void gen_rldinm(DisasContext *ctx, int mb, int me, int sh)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    int len = me - mb + 1;
    int rsh = (64 - sh) & 63;

    if (sh != 0 && len > 0 && me == (63 - sh)) {
        tcg_gen_deposit_z_tl(t_ra, t_rs, sh, len);
    } else if (me == 63 && rsh + len <= 64) {
        tcg_gen_extract_tl(t_ra, t_rs, rsh, len);
    } else {
        tcg_gen_rotli_tl(t_ra, t_rs, sh);
        tcg_gen_andi_tl(t_ra, t_ra, MASK(mb, me));
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
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

static void gen_rldnm(DisasContext *ctx, int mb, int me)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    TCGv t_rb = cpu_gpr[rB(ctx->opcode)];
    TCGv t0;

    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, t_rb, 0x3f);
    tcg_gen_rotl_tl(t_ra, t_rs, t0);
    tcg_temp_free(t0);

    tcg_gen_andi_tl(t_ra, t_ra, MASK(mb, me));
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
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
static void gen_rldimi(DisasContext *ctx, int mbn, int shn)
{
    TCGv t_ra = cpu_gpr[rA(ctx->opcode)];
    TCGv t_rs = cpu_gpr[rS(ctx->opcode)];
    uint32_t sh = SH(ctx->opcode) | (shn << 5);
    uint32_t mb = MB(ctx->opcode) | (mbn << 5);
    uint32_t me = 63 - sh;

    if (mb <= me) {
        tcg_gen_deposit_tl(t_ra, t_ra, t_rs, sh, me - mb + 1);
    } else {
        target_ulong mask = MASK(mb, me);
        TCGv t1 = tcg_temp_new();

        tcg_gen_rotli_tl(t1, t_rs, sh);
        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_andi_tl(t_ra, t_ra, ~mask);
        tcg_gen_or_tl(t_ra, t_ra, t1);
        tcg_temp_free(t1);
    }
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, t_ra);
    }
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* sraw & sraw. */
static void gen_sraw(DisasContext *ctx)
{
    gen_helper_sraw(cpu_gpr[rA(ctx->opcode)], cpu_env,
                    cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* srawi & srawi. */
static void gen_srawi(DisasContext *ctx)
{
    int sh = SH(ctx->opcode);
    TCGv dst = cpu_gpr[rA(ctx->opcode)];
    TCGv src = cpu_gpr[rS(ctx->opcode)];
    if (sh == 0) {
        tcg_gen_ext32s_tl(dst, src);
        tcg_gen_movi_tl(cpu_ca, 0);
        if (is_isa300(ctx)) {
            tcg_gen_movi_tl(cpu_ca32, 0);
        }
    } else {
        TCGv t0;
        tcg_gen_ext32s_tl(dst, src);
        tcg_gen_andi_tl(cpu_ca, dst, (1ULL << sh) - 1);
        t0 = tcg_temp_new();
        tcg_gen_sari_tl(t0, dst, TARGET_LONG_BITS - 1);
        tcg_gen_and_tl(cpu_ca, cpu_ca, t0);
        tcg_temp_free(t0);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_ca, cpu_ca, 0);
        if (is_isa300(ctx)) {
            tcg_gen_mov_tl(cpu_ca32, cpu_ca);
        }
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}

/* srad & srad. */
static void gen_srad(DisasContext *ctx)
{
    gen_helper_srad(cpu_gpr[rA(ctx->opcode)], cpu_env,
                    cpu_gpr[rS(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
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
        if (is_isa300(ctx)) {
            tcg_gen_movi_tl(cpu_ca32, 0);
        }
    } else {
        TCGv t0;
        tcg_gen_andi_tl(cpu_ca, src, (1ULL << sh) - 1);
        t0 = tcg_temp_new();
        tcg_gen_sari_tl(t0, src, TARGET_LONG_BITS - 1);
        tcg_gen_and_tl(cpu_ca, cpu_ca, t0);
        tcg_temp_free(t0);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_ca, cpu_ca, 0);
        if (is_isa300(ctx)) {
            tcg_gen_mov_tl(cpu_ca32, cpu_ca);
        }
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

/* extswsli & extswsli. */
static inline void gen_extswsli(DisasContext *ctx, int n)
{
    int sh = SH(ctx->opcode) + (n << 5);
    TCGv dst = cpu_gpr[rA(ctx->opcode)];
    TCGv src = cpu_gpr[rS(ctx->opcode)];

    tcg_gen_ext32s_tl(dst, src);
    tcg_gen_shli_tl(dst, dst, sh);
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, dst);
    }
}

static void gen_extswsli0(DisasContext *ctx)
{
    gen_extswsli(ctx, 0);
}

static void gen_extswsli1(DisasContext *ctx)
{
    gen_extswsli(ctx, 1);
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
    if (unlikely(Rc(ctx->opcode) != 0)) {
        gen_set_Rc0(ctx, cpu_gpr[rA(ctx->opcode)]);
    }
}
#endif

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

static inline void gen_align_no_le(DisasContext *ctx)
{
    gen_exception_err(ctx, POWERPC_EXCP_ALIGN,
                      (ctx->opcode & 0x03FF0000) | POWERPC_EXCP_ALIGN_LE);
}

static TCGv do_ea_calc(DisasContext *ctx, int ra, TCGv displ)
{
    TCGv ea = tcg_temp_new();
    if (ra) {
        tcg_gen_add_tl(ea, cpu_gpr[ra], displ);
    } else {
        tcg_gen_mov_tl(ea, displ);
    }
    if (NARROW_MODE(ctx)) {
        tcg_gen_ext32u_tl(ea, ea);
    }
    return ea;
}

/***                             Integer load                              ***/
#define DEF_MEMOP(op) ((op) | ctx->default_tcg_memop_mask)
#define BSWAP_MEMOP(op) ((op) | (ctx->default_tcg_memop_mask ^ MO_BSWAP))

#define GEN_QEMU_LOAD_TL(ldop, op)                                      \
static void glue(gen_qemu_, ldop)(DisasContext *ctx,                    \
                                  TCGv val,                             \
                                  TCGv addr)                            \
{                                                                       \
    tcg_gen_qemu_ld_tl(val, addr, ctx->mem_idx, op);                    \
}

GEN_QEMU_LOAD_TL(ld8u,  DEF_MEMOP(MO_UB))
GEN_QEMU_LOAD_TL(ld16u, DEF_MEMOP(MO_UW))
GEN_QEMU_LOAD_TL(ld16s, DEF_MEMOP(MO_SW))
GEN_QEMU_LOAD_TL(ld32u, DEF_MEMOP(MO_UL))
GEN_QEMU_LOAD_TL(ld32s, DEF_MEMOP(MO_SL))

GEN_QEMU_LOAD_TL(ld16ur, BSWAP_MEMOP(MO_UW))
GEN_QEMU_LOAD_TL(ld32ur, BSWAP_MEMOP(MO_UL))

#define GEN_QEMU_LOAD_64(ldop, op)                                  \
static void glue(gen_qemu_, glue(ldop, _i64))(DisasContext *ctx,    \
                                             TCGv_i64 val,          \
                                             TCGv addr)             \
{                                                                   \
    tcg_gen_qemu_ld_i64(val, addr, ctx->mem_idx, op);               \
}

GEN_QEMU_LOAD_64(ld8u,  DEF_MEMOP(MO_UB))
GEN_QEMU_LOAD_64(ld16u, DEF_MEMOP(MO_UW))
GEN_QEMU_LOAD_64(ld32u, DEF_MEMOP(MO_UL))
GEN_QEMU_LOAD_64(ld32s, DEF_MEMOP(MO_SL))
GEN_QEMU_LOAD_64(ld64,  DEF_MEMOP(MO_UQ))

#if defined(TARGET_PPC64)
GEN_QEMU_LOAD_64(ld64ur, BSWAP_MEMOP(MO_UQ))
#endif

#define GEN_QEMU_STORE_TL(stop, op)                                     \
static void glue(gen_qemu_, stop)(DisasContext *ctx,                    \
                                  TCGv val,                             \
                                  TCGv addr)                            \
{                                                                       \
    tcg_gen_qemu_st_tl(val, addr, ctx->mem_idx, op);                    \
}

#if defined(TARGET_PPC64) || !defined(CONFIG_USER_ONLY)
GEN_QEMU_STORE_TL(st8,  DEF_MEMOP(MO_UB))
#endif
GEN_QEMU_STORE_TL(st16, DEF_MEMOP(MO_UW))
GEN_QEMU_STORE_TL(st32, DEF_MEMOP(MO_UL))

GEN_QEMU_STORE_TL(st16r, BSWAP_MEMOP(MO_UW))
GEN_QEMU_STORE_TL(st32r, BSWAP_MEMOP(MO_UL))

#define GEN_QEMU_STORE_64(stop, op)                               \
static void glue(gen_qemu_, glue(stop, _i64))(DisasContext *ctx,  \
                                              TCGv_i64 val,       \
                                              TCGv addr)          \
{                                                                 \
    tcg_gen_qemu_st_i64(val, addr, ctx->mem_idx, op);             \
}

GEN_QEMU_STORE_64(st8,  DEF_MEMOP(MO_UB))
GEN_QEMU_STORE_64(st16, DEF_MEMOP(MO_UW))
GEN_QEMU_STORE_64(st32, DEF_MEMOP(MO_UL))
GEN_QEMU_STORE_64(st64, DEF_MEMOP(MO_UQ))

#if defined(TARGET_PPC64)
GEN_QEMU_STORE_64(st64r, BSWAP_MEMOP(MO_UQ))
#endif

#define GEN_LDX_E(name, ldop, opc2, opc3, type, type2, chk)                   \
static void glue(gen_, name##x)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    chk(ctx);                                                                 \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##ldop(ctx, cpu_gpr[rD(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}

#define GEN_LDX(name, ldop, opc2, opc3, type)                                 \
    GEN_LDX_E(name, ldop, opc2, opc3, type, PPC_NONE, CHK_NONE)

#define GEN_LDX_HVRM(name, ldop, opc2, opc3, type)                            \
    GEN_LDX_E(name, ldop, opc2, opc3, type, PPC_NONE, CHK_HVRM)

#define GEN_LDEPX(name, ldop, opc2, opc3)                                     \
static void glue(gen_, name##epx)(DisasContext *ctx)                          \
{                                                                             \
    TCGv EA;                                                                  \
    CHK_SV(ctx);                                                              \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_qemu_ld_tl(cpu_gpr[rD(ctx->opcode)], EA, PPC_TLB_EPID_LOAD, ldop);\
    tcg_temp_free(EA);                                                        \
}

GEN_LDEPX(lb, DEF_MEMOP(MO_UB), 0x1F, 0x02)
GEN_LDEPX(lh, DEF_MEMOP(MO_UW), 0x1F, 0x08)
GEN_LDEPX(lw, DEF_MEMOP(MO_UL), 0x1F, 0x00)
#if defined(TARGET_PPC64)
GEN_LDEPX(ld, DEF_MEMOP(MO_UQ), 0x1D, 0x00)
#endif

#if defined(TARGET_PPC64)
/* CI load/store variants */
GEN_LDX_HVRM(ldcix, ld64_i64, 0x15, 0x1b, PPC_CILDST)
GEN_LDX_HVRM(lwzcix, ld32u, 0x15, 0x15, PPC_CILDST)
GEN_LDX_HVRM(lhzcix, ld16u, 0x15, 0x19, PPC_CILDST)
GEN_LDX_HVRM(lbzcix, ld8u, 0x15, 0x1a, PPC_CILDST)
#endif

/***                              Integer store                            ***/
#define GEN_STX_E(name, stop, opc2, opc3, type, type2, chk)                   \
static void glue(gen_, name##x)(DisasContext *ctx)                            \
{                                                                             \
    TCGv EA;                                                                  \
    chk(ctx);                                                                 \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    gen_qemu_##stop(ctx, cpu_gpr[rS(ctx->opcode)], EA);                       \
    tcg_temp_free(EA);                                                        \
}
#define GEN_STX(name, stop, opc2, opc3, type)                                 \
    GEN_STX_E(name, stop, opc2, opc3, type, PPC_NONE, CHK_NONE)

#define GEN_STX_HVRM(name, stop, opc2, opc3, type)                            \
    GEN_STX_E(name, stop, opc2, opc3, type, PPC_NONE, CHK_HVRM)

#define GEN_STEPX(name, stop, opc2, opc3)                                     \
static void glue(gen_, name##epx)(DisasContext *ctx)                          \
{                                                                             \
    TCGv EA;                                                                  \
    CHK_SV(ctx);                                                              \
    gen_set_access_type(ctx, ACCESS_INT);                                     \
    EA = tcg_temp_new();                                                      \
    gen_addr_reg_index(ctx, EA);                                              \
    tcg_gen_qemu_st_tl(                                                       \
        cpu_gpr[rD(ctx->opcode)], EA, PPC_TLB_EPID_STORE, stop);              \
    tcg_temp_free(EA);                                                        \
}

GEN_STEPX(stb, DEF_MEMOP(MO_UB), 0x1F, 0x06)
GEN_STEPX(sth, DEF_MEMOP(MO_UW), 0x1F, 0x0C)
GEN_STEPX(stw, DEF_MEMOP(MO_UL), 0x1F, 0x04)
#if defined(TARGET_PPC64)
GEN_STEPX(std, DEF_MEMOP(MO_UQ), 0x1d, 0x04)
#endif

#if defined(TARGET_PPC64)
GEN_STX_HVRM(stdcix, st64_i64, 0x15, 0x1f, PPC_CILDST)
GEN_STX_HVRM(stwcix, st32, 0x15, 0x1c, PPC_CILDST)
GEN_STX_HVRM(sthcix, st16, 0x15, 0x1d, PPC_CILDST)
GEN_STX_HVRM(stbcix, st8, 0x15, 0x1e, PPC_CILDST)
#endif
/***                Integer load and store with byte reverse               ***/

/* lhbrx */
GEN_LDX(lhbr, ld16ur, 0x16, 0x18, PPC_INTEGER);

/* lwbrx */
GEN_LDX(lwbr, ld32ur, 0x16, 0x10, PPC_INTEGER);

#if defined(TARGET_PPC64)
/* ldbrx */
GEN_LDX_E(ldbr, ld64ur_i64, 0x14, 0x10, PPC_NONE, PPC2_DBRX, CHK_NONE);
/* stdbrx */
GEN_STX_E(stdbr, st64r_i64, 0x14, 0x14, PPC_NONE, PPC2_DBRX, CHK_NONE);
#endif  /* TARGET_PPC64 */

/* sthbrx */
GEN_STX(sthbr, st16r, 0x16, 0x1C, PPC_INTEGER);
/* stwbrx */
GEN_STX(stwbr, st32r, 0x16, 0x14, PPC_INTEGER);

/***                    Integer load and store multiple                    ***/

/* lmw */
static void gen_lmw(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1;

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
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

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    t0 = tcg_temp_new();
    t1 = tcg_const_i32(rS(ctx->opcode));
    gen_addr_imm_index(ctx, t0, 0);
    gen_helper_stmw(cpu_env, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

/***                    Integer load and store strings                     ***/

/* lswi */
/*
 * PowerPC32 specification says we must generate an exception if rA is
 * in the range of registers to be loaded.  In an other hand, IBM says
 * this is valid, but rA won't be loaded.  For now, I'll follow the
 * spec...
 */
static void gen_lswi(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1, t2;
    int nb = NB(ctx->opcode);
    int start = rD(ctx->opcode);
    int ra = rA(ctx->opcode);
    int nr;

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    if (nb == 0) {
        nb = 32;
    }
    nr = DIV_ROUND_UP(nb, 4);
    if (unlikely(lsw_reg_in_range(start, nr, ra))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_LSWX);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
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

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
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

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
    t0 = tcg_temp_new();
    gen_addr_register(ctx, t0);
    if (nb == 0) {
        nb = 32;
    }
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

    if (ctx->le_mode) {
        gen_align_no_le(ctx);
        return;
    }
    gen_set_access_type(ctx, ACCESS_INT);
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
    TCGBar bar = TCG_MO_ALL;

    /*
     * eieio has complex semanitcs. It provides memory ordering between
     * operations in the set:
     * - loads from CI memory.
     * - stores to CI memory.
     * - stores to WT memory.
     *
     * It separately also orders memory for operations in the set:
     * - stores to cacheble memory.
     *
     * It also serializes instructions:
     * - dcbt and dcbst.
     *
     * It separately serializes:
     * - tlbie and tlbsync.
     *
     * And separately serializes:
     * - slbieg, slbiag, and slbsync.
     *
     * The end result is that CI memory ordering requires TCG_MO_ALL
     * and it is not possible to special-case more relaxed ordering for
     * cacheable accesses. TCG_BAR_SC is required to provide this
     * serialization.
     */

    /*
     * POWER9 has a eieio instruction variant using bit 6 as a hint to
     * tell the CPU it is a store-forwarding barrier.
     */
    if (ctx->opcode & 0x2000000) {
        /*
         * ISA says that "Reserved fields in instructions are ignored
         * by the processor". So ignore the bit 6 on non-POWER9 CPU but
         * as this is not an instruction software should be using,
         * complain to the user.
         */
        if (!(ctx->insns_flags2 & PPC2_ISA300)) {
            qemu_log_mask(LOG_GUEST_ERROR, "invalid eieio using bit 6 at @"
                          TARGET_FMT_lx "\n", ctx->cia);
        } else {
            bar = TCG_MO_ST_LD;
        }
    }

    tcg_gen_mb(bar | TCG_BAR_SC);
}

#if !defined(CONFIG_USER_ONLY)
static inline void gen_check_tlb_flush(DisasContext *ctx, bool global)
{
    TCGv_i32 t;
    TCGLabel *l;

    if (!ctx->lazy_tlb_flush) {
        return;
    }
    l = gen_new_label();
    t = tcg_temp_new_i32();
    tcg_gen_ld_i32(t, cpu_env, offsetof(CPUPPCState, tlb_need_flush));
    tcg_gen_brcondi_i32(TCG_COND_EQ, t, 0, l);
    if (global) {
        gen_helper_check_tlb_flush_global(cpu_env);
    } else {
        gen_helper_check_tlb_flush_local(cpu_env);
    }
    gen_set_label(l);
    tcg_temp_free_i32(t);
}
#else
static inline void gen_check_tlb_flush(DisasContext *ctx, bool global) { }
#endif

/* isync */
static void gen_isync(DisasContext *ctx)
{
    /*
     * We need to check for a pending TLB flush. This can only happen in
     * kernel mode however so check MSR_PR
     */
    if (!ctx->pr) {
        gen_check_tlb_flush(ctx, false);
    }
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
}

#define MEMOP_GET_SIZE(x)  (1 << ((x) & MO_SIZE))

static void gen_load_locked(DisasContext *ctx, MemOp memop)
{
    TCGv gpr = cpu_gpr[rD(ctx->opcode)];
    TCGv t0 = tcg_temp_new();

    gen_set_access_type(ctx, ACCESS_RES);
    gen_addr_reg_index(ctx, t0);
    tcg_gen_qemu_ld_tl(gpr, t0, ctx->mem_idx, memop | MO_ALIGN);
    tcg_gen_mov_tl(cpu_reserve, t0);
    tcg_gen_mov_tl(cpu_reserve_val, gpr);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    tcg_temp_free(t0);
}

#define LARX(name, memop)                  \
static void gen_##name(DisasContext *ctx)  \
{                                          \
    gen_load_locked(ctx, memop);           \
}

/* lwarx */
LARX(lbarx, DEF_MEMOP(MO_UB))
LARX(lharx, DEF_MEMOP(MO_UW))
LARX(lwarx, DEF_MEMOP(MO_UL))

static void gen_fetch_inc_conditional(DisasContext *ctx, MemOp memop,
                                      TCGv EA, TCGCond cond, int addend)
{
    TCGv t = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv u = tcg_temp_new();

    tcg_gen_qemu_ld_tl(t, EA, ctx->mem_idx, memop);
    tcg_gen_addi_tl(t2, EA, MEMOP_GET_SIZE(memop));
    tcg_gen_qemu_ld_tl(t2, t2, ctx->mem_idx, memop);
    tcg_gen_addi_tl(u, t, addend);

    /* E.g. for fetch and increment bounded... */
    /* mem(EA,s) = (t != t2 ? u = t + 1 : t) */
    tcg_gen_movcond_tl(cond, u, t, t2, u, t);
    tcg_gen_qemu_st_tl(u, EA, ctx->mem_idx, memop);

    /* RT = (t != t2 ? t : u = 1<<(s*8-1)) */
    tcg_gen_movi_tl(u, 1 << (MEMOP_GET_SIZE(memop) * 8 - 1));
    tcg_gen_movcond_tl(cond, cpu_gpr[rD(ctx->opcode)], t, t2, t, u);

    tcg_temp_free(t);
    tcg_temp_free(t2);
    tcg_temp_free(u);
}

static void gen_ld_atomic(DisasContext *ctx, MemOp memop)
{
    uint32_t gpr_FC = FC(ctx->opcode);
    TCGv EA = tcg_temp_new();
    int rt = rD(ctx->opcode);
    bool need_serial;
    TCGv src, dst;

    gen_addr_register(ctx, EA);
    dst = cpu_gpr[rt];
    src = cpu_gpr[(rt + 1) & 31];

    need_serial = false;
    memop |= MO_ALIGN;
    switch (gpr_FC) {
    case 0: /* Fetch and add */
        tcg_gen_atomic_fetch_add_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 1: /* Fetch and xor */
        tcg_gen_atomic_fetch_xor_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 2: /* Fetch and or */
        tcg_gen_atomic_fetch_or_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 3: /* Fetch and 'and' */
        tcg_gen_atomic_fetch_and_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 4:  /* Fetch and max unsigned */
        tcg_gen_atomic_fetch_umax_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 5:  /* Fetch and max signed */
        tcg_gen_atomic_fetch_smax_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 6:  /* Fetch and min unsigned */
        tcg_gen_atomic_fetch_umin_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 7:  /* Fetch and min signed */
        tcg_gen_atomic_fetch_smin_tl(dst, EA, src, ctx->mem_idx, memop);
        break;
    case 8: /* Swap */
        tcg_gen_atomic_xchg_tl(dst, EA, src, ctx->mem_idx, memop);
        break;

    case 16: /* Compare and swap not equal */
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            need_serial = true;
        } else {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();

            tcg_gen_qemu_ld_tl(t0, EA, ctx->mem_idx, memop);
            if ((memop & MO_SIZE) == MO_64 || TARGET_LONG_BITS == 32) {
                tcg_gen_mov_tl(t1, src);
            } else {
                tcg_gen_ext32u_tl(t1, src);
            }
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t0, t1,
                               cpu_gpr[(rt + 2) & 31], t0);
            tcg_gen_qemu_st_tl(t1, EA, ctx->mem_idx, memop);
            tcg_gen_mov_tl(dst, t0);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;

    case 24: /* Fetch and increment bounded */
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            need_serial = true;
        } else {
            gen_fetch_inc_conditional(ctx, memop, EA, TCG_COND_NE, 1);
        }
        break;
    case 25: /* Fetch and increment equal */
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            need_serial = true;
        } else {
            gen_fetch_inc_conditional(ctx, memop, EA, TCG_COND_EQ, 1);
        }
        break;
    case 28: /* Fetch and decrement bounded */
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            need_serial = true;
        } else {
            gen_fetch_inc_conditional(ctx, memop, EA, TCG_COND_NE, -1);
        }
        break;

    default:
        /* invoke data storage error handler */
        gen_exception_err(ctx, POWERPC_EXCP_DSI, POWERPC_EXCP_INVAL);
    }
    tcg_temp_free(EA);

    if (need_serial) {
        /* Restart with exclusive lock.  */
        gen_helper_exit_atomic(cpu_env);
        ctx->base.is_jmp = DISAS_NORETURN;
    }
}

static void gen_lwat(DisasContext *ctx)
{
    gen_ld_atomic(ctx, DEF_MEMOP(MO_UL));
}

#ifdef TARGET_PPC64
static void gen_ldat(DisasContext *ctx)
{
    gen_ld_atomic(ctx, DEF_MEMOP(MO_UQ));
}
#endif

static void gen_st_atomic(DisasContext *ctx, MemOp memop)
{
    uint32_t gpr_FC = FC(ctx->opcode);
    TCGv EA = tcg_temp_new();
    TCGv src, discard;

    gen_addr_register(ctx, EA);
    src = cpu_gpr[rD(ctx->opcode)];
    discard = tcg_temp_new();

    memop |= MO_ALIGN;
    switch (gpr_FC) {
    case 0: /* add and Store */
        tcg_gen_atomic_add_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 1: /* xor and Store */
        tcg_gen_atomic_xor_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 2: /* Or and Store */
        tcg_gen_atomic_or_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 3: /* 'and' and Store */
        tcg_gen_atomic_and_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 4:  /* Store max unsigned */
        tcg_gen_atomic_umax_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 5:  /* Store max signed */
        tcg_gen_atomic_smax_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 6:  /* Store min unsigned */
        tcg_gen_atomic_umin_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 7:  /* Store min signed */
        tcg_gen_atomic_smin_fetch_tl(discard, EA, src, ctx->mem_idx, memop);
        break;
    case 24: /* Store twin  */
        if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
            /* Restart with exclusive lock.  */
            gen_helper_exit_atomic(cpu_env);
            ctx->base.is_jmp = DISAS_NORETURN;
        } else {
            TCGv t = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGv s = tcg_temp_new();
            TCGv s2 = tcg_temp_new();
            TCGv ea_plus_s = tcg_temp_new();

            tcg_gen_qemu_ld_tl(t, EA, ctx->mem_idx, memop);
            tcg_gen_addi_tl(ea_plus_s, EA, MEMOP_GET_SIZE(memop));
            tcg_gen_qemu_ld_tl(t2, ea_plus_s, ctx->mem_idx, memop);
            tcg_gen_movcond_tl(TCG_COND_EQ, s, t, t2, src, t);
            tcg_gen_movcond_tl(TCG_COND_EQ, s2, t, t2, src, t2);
            tcg_gen_qemu_st_tl(s, EA, ctx->mem_idx, memop);
            tcg_gen_qemu_st_tl(s2, ea_plus_s, ctx->mem_idx, memop);

            tcg_temp_free(ea_plus_s);
            tcg_temp_free(s2);
            tcg_temp_free(s);
            tcg_temp_free(t2);
            tcg_temp_free(t);
        }
        break;
    default:
        /* invoke data storage error handler */
        gen_exception_err(ctx, POWERPC_EXCP_DSI, POWERPC_EXCP_INVAL);
    }
    tcg_temp_free(discard);
    tcg_temp_free(EA);
}

static void gen_stwat(DisasContext *ctx)
{
    gen_st_atomic(ctx, DEF_MEMOP(MO_UL));
}

#ifdef TARGET_PPC64
static void gen_stdat(DisasContext *ctx)
{
    gen_st_atomic(ctx, DEF_MEMOP(MO_UQ));
}
#endif

static void gen_conditional_store(DisasContext *ctx, MemOp memop)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGv t0 = tcg_temp_new();
    int reg = rS(ctx->opcode);

    gen_set_access_type(ctx, ACCESS_RES);
    gen_addr_reg_index(ctx, t0);
    tcg_gen_brcond_tl(TCG_COND_NE, t0, cpu_reserve, l1);
    tcg_temp_free(t0);

    t0 = tcg_temp_new();
    tcg_gen_atomic_cmpxchg_tl(t0, cpu_reserve, cpu_reserve_val,
                              cpu_gpr[reg], ctx->mem_idx,
                              DEF_MEMOP(memop) | MO_ALIGN);
    tcg_gen_setcond_tl(TCG_COND_EQ, t0, t0, cpu_reserve_val);
    tcg_gen_shli_tl(t0, t0, CRF_EQ_BIT);
    tcg_gen_or_tl(t0, t0, cpu_so);
    tcg_gen_trunc_tl_i32(cpu_crf[0], t0);
    tcg_temp_free(t0);
    tcg_gen_br(l2);

    gen_set_label(l1);

    /*
     * Address mismatch implies failure.  But we still need to provide
     * the memory barrier semantics of the instruction.
     */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);

    gen_set_label(l2);
    tcg_gen_movi_tl(cpu_reserve, -1);
}

#define STCX(name, memop)                  \
static void gen_##name(DisasContext *ctx)  \
{                                          \
    gen_conditional_store(ctx, memop);     \
}

STCX(stbcx_, DEF_MEMOP(MO_UB))
STCX(sthcx_, DEF_MEMOP(MO_UW))
STCX(stwcx_, DEF_MEMOP(MO_UL))

#if defined(TARGET_PPC64)
/* ldarx */
LARX(ldarx, DEF_MEMOP(MO_UQ))
/* stdcx. */
STCX(stdcx_, DEF_MEMOP(MO_UQ))

/* lqarx */
static void gen_lqarx(DisasContext *ctx)
{
    int rd = rD(ctx->opcode);
    TCGv EA, hi, lo;

    if (unlikely((rd & 1) || (rd == rA(ctx->opcode)) ||
                 (rd == rB(ctx->opcode)))) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }

    gen_set_access_type(ctx, ACCESS_RES);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);

    /* Note that the low part is always in RD+1, even in LE mode.  */
    lo = cpu_gpr[rd + 1];
    hi = cpu_gpr[rd];

    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        if (HAVE_ATOMIC128) {
            TCGv_i32 oi = tcg_temp_new_i32();
            if (ctx->le_mode) {
                tcg_gen_movi_i32(oi, make_memop_idx(MO_LE | MO_128 | MO_ALIGN,
                                                    ctx->mem_idx));
                gen_helper_lq_le_parallel(lo, cpu_env, EA, oi);
            } else {
                tcg_gen_movi_i32(oi, make_memop_idx(MO_BE | MO_128 | MO_ALIGN,
                                                    ctx->mem_idx));
                gen_helper_lq_be_parallel(lo, cpu_env, EA, oi);
            }
            tcg_temp_free_i32(oi);
            tcg_gen_ld_i64(hi, cpu_env, offsetof(CPUPPCState, retxh));
        } else {
            /* Restart with exclusive lock.  */
            gen_helper_exit_atomic(cpu_env);
            ctx->base.is_jmp = DISAS_NORETURN;
            tcg_temp_free(EA);
            return;
        }
    } else if (ctx->le_mode) {
        tcg_gen_qemu_ld_i64(lo, EA, ctx->mem_idx, MO_LEUQ | MO_ALIGN_16);
        tcg_gen_mov_tl(cpu_reserve, EA);
        gen_addr_add(ctx, EA, EA, 8);
        tcg_gen_qemu_ld_i64(hi, EA, ctx->mem_idx, MO_LEUQ);
    } else {
        tcg_gen_qemu_ld_i64(hi, EA, ctx->mem_idx, MO_BEUQ | MO_ALIGN_16);
        tcg_gen_mov_tl(cpu_reserve, EA);
        gen_addr_add(ctx, EA, EA, 8);
        tcg_gen_qemu_ld_i64(lo, EA, ctx->mem_idx, MO_BEUQ);
    }
    tcg_temp_free(EA);

    tcg_gen_st_tl(hi, cpu_env, offsetof(CPUPPCState, reserve_val));
    tcg_gen_st_tl(lo, cpu_env, offsetof(CPUPPCState, reserve_val2));
}

/* stqcx. */
static void gen_stqcx_(DisasContext *ctx)
{
    TCGLabel *lab_fail, *lab_over;
    int rs = rS(ctx->opcode);
    TCGv EA, t0, t1;
    TCGv_i128 cmp, val;

    if (unlikely(rs & 1)) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }

    lab_fail = gen_new_label();
    lab_over = gen_new_label();

    gen_set_access_type(ctx, ACCESS_RES);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);

    tcg_gen_brcond_tl(TCG_COND_NE, EA, cpu_reserve, lab_fail);
    tcg_temp_free(EA);

    cmp = tcg_temp_new_i128();
    val = tcg_temp_new_i128();

    tcg_gen_concat_i64_i128(cmp, cpu_reserve_val2, cpu_reserve_val);

    /* Note that the low part is always in RS+1, even in LE mode.  */
    tcg_gen_concat_i64_i128(val, cpu_gpr[rs + 1], cpu_gpr[rs]);

    tcg_gen_atomic_cmpxchg_i128(val, cpu_reserve, cmp, val, ctx->mem_idx,
                                DEF_MEMOP(MO_128 | MO_ALIGN));
    tcg_temp_free_i128(cmp);

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    tcg_gen_extr_i128_i64(t1, t0, val);
    tcg_temp_free_i128(val);

    tcg_gen_xor_tl(t1, t1, cpu_reserve_val2);
    tcg_gen_xor_tl(t0, t0, cpu_reserve_val);
    tcg_gen_or_tl(t0, t0, t1);
    tcg_temp_free(t1);

    tcg_gen_setcondi_tl(TCG_COND_EQ, t0, t0, 0);
    tcg_gen_shli_tl(t0, t0, CRF_EQ_BIT);
    tcg_gen_or_tl(t0, t0, cpu_so);
    tcg_gen_trunc_tl_i32(cpu_crf[0], t0);
    tcg_temp_free(t0);

    tcg_gen_br(lab_over);
    gen_set_label(lab_fail);

    /*
     * Address mismatch implies failure.  But we still need to provide
     * the memory barrier semantics of the instruction.
     */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);

    gen_set_label(lab_over);
    tcg_gen_movi_tl(cpu_reserve, -1);
}
#endif /* defined(TARGET_PPC64) */

/* sync */
static void gen_sync(DisasContext *ctx)
{
    TCGBar bar = TCG_MO_ALL;
    uint32_t l = (ctx->opcode >> 21) & 3;

    if ((l == 1) && (ctx->insns_flags2 & PPC2_MEM_LWSYNC)) {
        bar = TCG_MO_LD_LD | TCG_MO_LD_ST | TCG_MO_ST_ST;
    }

    /*
     * We may need to check for a pending TLB flush.
     *
     * We do this on ptesync (l == 2) on ppc64 and any sync pn ppc32.
     *
     * Additionally, this can only happen in kernel mode however so
     * check MSR_PR as well.
     */
    if (((l == 2) || !(ctx->insns_flags & PPC_64B)) && !ctx->pr) {
        gen_check_tlb_flush(ctx, true);
    }

    tcg_gen_mb(bar | TCG_BAR_SC);
}

/* wait */
static void gen_wait(DisasContext *ctx)
{
    uint32_t wc;

    if (ctx->insns_flags & PPC_WAIT) {
        /* v2.03-v2.07 define an older incompatible 'wait' encoding. */

        if (ctx->insns_flags2 & PPC2_PM_ISA206) {
            /* v2.06 introduced the WC field. WC > 0 may be treated as no-op. */
            wc = WC(ctx->opcode);
        } else {
            wc = 0;
        }

    } else if (ctx->insns_flags2 & PPC2_ISA300) {
        /* v3.0 defines a new 'wait' encoding. */
        wc = WC(ctx->opcode);
        if (ctx->insns_flags2 & PPC2_ISA310) {
            uint32_t pl = PL(ctx->opcode);

            /* WC 1,2 may be treated as no-op. WC 3 is reserved. */
            if (wc == 3) {
                gen_invalid(ctx);
                return;
            }

            /* PL 1-3 are reserved. If WC=2 then the insn is treated as noop. */
            if (pl > 0 && wc != 2) {
                gen_invalid(ctx);
                return;
            }

        } else { /* ISA300 */
            /* WC 1-3 are reserved */
            if (wc > 0) {
                gen_invalid(ctx);
                return;
            }
        }

    } else {
        warn_report("wait instruction decoded with wrong ISA flags.");
        gen_invalid(ctx);
        return;
    }

    /*
     * wait without WC field or with WC=0 waits for an exception / interrupt
     * to occur.
     */
    if (wc == 0) {
        TCGv_i32 t0 = tcg_const_i32(1);
        tcg_gen_st_i32(t0, cpu_env,
                       -offsetof(PowerPCCPU, env) + offsetof(CPUState, halted));
        tcg_temp_free_i32(t0);
        /* Stop translation, as the CPU is supposed to sleep from now */
        gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
    }

    /*
     * Other wait types must not just wait until an exception occurs because
     * ignoring their other wake-up conditions could cause a hang.
     *
     * For v2.06 and 2.07, wc=1,2,3 are architected but may be implemented as
     * no-ops.
     *
     * wc=1 and wc=3 explicitly allow the instruction to be treated as a no-op.
     *
     * wc=2 waits for an implementation-specific condition, such could be
     * always true, so it can be implemented as a no-op.
     *
     * For v3.1, wc=1,2 are architected but may be implemented as no-ops.
     *
     * wc=1 (waitrsv) waits for an exception or a reservation to be lost.
     * Reservation-loss may have implementation-specific conditions, so it
     * can be implemented as a no-op.
     *
     * wc=2 waits for an exception or an amount of time to pass. This
     * amount is implementation-specific so it can be implemented as a
     * no-op.
     *
     * ISA v3.1 allows for execution to resume "in the rare case of
     * an implementation-dependent event", so in any case software must
     * not depend on the architected resumption condition to become
     * true, so no-op implementations should be architecturally correct
     * (if suboptimal).
     */
}

#if defined(TARGET_PPC64)
static void gen_doze(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv_i32 t;

    CHK_HV(ctx);
    t = tcg_const_i32(PPC_PM_DOZE);
    gen_helper_pminsn(cpu_env, t);
    tcg_temp_free_i32(t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_nap(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv_i32 t;

    CHK_HV(ctx);
    t = tcg_const_i32(PPC_PM_NAP);
    gen_helper_pminsn(cpu_env, t);
    tcg_temp_free_i32(t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_stop(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv_i32 t;

    CHK_HV(ctx);
    t = tcg_const_i32(PPC_PM_STOP);
    gen_helper_pminsn(cpu_env, t);
    tcg_temp_free_i32(t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_sleep(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv_i32 t;

    CHK_HV(ctx);
    t = tcg_const_i32(PPC_PM_SLEEP);
    gen_helper_pminsn(cpu_env, t);
    tcg_temp_free_i32(t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_rvwinkle(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv_i32 t;

    CHK_HV(ctx);
    t = tcg_const_i32(PPC_PM_RVWINKLE);
    gen_helper_pminsn(cpu_env, t);
    tcg_temp_free_i32(t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#endif /* defined(CONFIG_USER_ONLY) */
}
#endif /* #if defined(TARGET_PPC64) */

static inline void gen_update_cfar(DisasContext *ctx, target_ulong nip)
{
#if defined(TARGET_PPC64)
    if (ctx->has_cfar) {
        tcg_gen_movi_tl(cpu_cfar, nip);
    }
#endif
}

#if defined(TARGET_PPC64)
static void pmu_count_insns(DisasContext *ctx)
{
    /*
     * Do not bother calling the helper if the PMU isn't counting
     * instructions.
     */
    if (!ctx->pmu_insn_cnt) {
        return;
    }

 #if !defined(CONFIG_USER_ONLY)
    TCGLabel *l;
    TCGv t0;

    /*
     * The PMU insns_inc() helper stops the internal PMU timer if a
     * counter overflows happens. In that case, if the guest is
     * running with icount and we do not handle it beforehand,
     * the helper can trigger a 'bad icount read'.
     */
    gen_icount_io_start(ctx);

    /* Avoid helper calls when only PMC5-6 are enabled. */
    if (!ctx->pmc_other) {
        l = gen_new_label();
        t0 = tcg_temp_new();

        gen_load_spr(t0, SPR_POWER_PMC5);
        tcg_gen_addi_tl(t0, t0, ctx->base.num_insns);
        gen_store_spr(SPR_POWER_PMC5, t0);
        /* Check for overflow, if it's enabled */
        if (ctx->mmcr0_pmcjce) {
            tcg_gen_brcondi_tl(TCG_COND_LT, t0, PMC_COUNTER_NEGATIVE_VAL, l);
            gen_helper_handle_pmc5_overflow(cpu_env);
        }

        gen_set_label(l);
        tcg_temp_free(t0);
    } else {
        gen_helper_insns_inc(cpu_env, tcg_constant_i32(ctx->base.num_insns));
    }
  #else
    /*
     * User mode can read (but not write) PMC5 and start/stop
     * the PMU via MMCR0_FC. In this case just increment
     * PMC5 with base.num_insns.
     */
    TCGv t0 = tcg_temp_new();

    gen_load_spr(t0, SPR_POWER_PMC5);
    tcg_gen_addi_tl(t0, t0, ctx->base.num_insns);
    gen_store_spr(SPR_POWER_PMC5, t0);

    tcg_temp_free(t0);
  #endif /* #if !defined(CONFIG_USER_ONLY) */
}
#else
static void pmu_count_insns(DisasContext *ctx)
{
    return;
}
#endif /* #if defined(TARGET_PPC64) */

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    return translator_use_goto_tb(&ctx->base, dest);
}

static void gen_lookup_and_goto_ptr(DisasContext *ctx)
{
    if (unlikely(ctx->singlestep_enabled)) {
        gen_debug_exception(ctx);
    } else {
        /*
         * tcg_gen_lookup_and_goto_ptr will exit the TB if
         * CF_NO_GOTO_PTR is set. Count insns now.
         */
        if (ctx->base.tb->flags & CF_NO_GOTO_PTR) {
            pmu_count_insns(ctx);
        }

        tcg_gen_lookup_and_goto_ptr();
    }
}

/***                                Branch                                 ***/
static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (NARROW_MODE(ctx)) {
        dest = (uint32_t) dest;
    }
    if (use_goto_tb(ctx, dest)) {
        pmu_count_insns(ctx);
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_nip, dest & ~3);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_nip, dest & ~3);
        gen_lookup_and_goto_ptr(ctx);
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

    /* sign extend LI */
    li = LI(ctx->opcode);
    li = (li ^ 0x02000000) - 0x02000000;
    if (likely(AA(ctx->opcode) == 0)) {
        target = ctx->cia + li;
    } else {
        target = li;
    }
    if (LK(ctx->opcode)) {
        gen_setlr(ctx, ctx->base.pc_next);
    }
    gen_update_cfar(ctx, ctx->cia);
    gen_goto_tb(ctx, 0, target);
    ctx->base.is_jmp = DISAS_NORETURN;
}

#define BCOND_IM  0
#define BCOND_LR  1
#define BCOND_CTR 2
#define BCOND_TAR 3

static void gen_bcond(DisasContext *ctx, int type)
{
    uint32_t bo = BO(ctx->opcode);
    TCGLabel *l1;
    TCGv target;

    if (type == BCOND_LR || type == BCOND_CTR || type == BCOND_TAR) {
        target = tcg_temp_local_new();
        if (type == BCOND_CTR) {
            tcg_gen_mov_tl(target, cpu_ctr);
        } else if (type == BCOND_TAR) {
            gen_load_spr(target, SPR_TAR);
        } else {
            tcg_gen_mov_tl(target, cpu_lr);
        }
    } else {
        target = NULL;
    }
    if (LK(ctx->opcode)) {
        gen_setlr(ctx, ctx->base.pc_next);
    }
    l1 = gen_new_label();
    if ((bo & 0x4) == 0) {
        /* Decrement and test CTR */
        TCGv temp = tcg_temp_new();

        if (type == BCOND_CTR) {
            /*
             * All ISAs up to v3 describe this form of bcctr as invalid but
             * some processors, ie. 64-bit server processors compliant with
             * arch 2.x, do implement a "test and decrement" logic instead,
             * as described in their respective UMs. This logic involves CTR
             * to act as both the branch target and a counter, which makes
             * it basically useless and thus never used in real code.
             *
             * This form was hence chosen to trigger extra micro-architectural
             * side-effect on real HW needed for the Spectre v2 workaround.
             * It is up to guests that implement such workaround, ie. linux, to
             * use this form in a way it just triggers the side-effect without
             * doing anything else harmful.
             */
            if (unlikely(!is_book3s_arch2x(ctx))) {
                gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
                tcg_temp_free(temp);
                tcg_temp_free(target);
                return;
            }

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
            tcg_gen_subi_tl(cpu_ctr, cpu_ctr, 1);
        } else {
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
        }
        tcg_temp_free(temp);
    }
    if ((bo & 0x10) == 0) {
        /* Test CR */
        uint32_t bi = BI(ctx->opcode);
        uint32_t mask = 0x08 >> (bi & 0x03);
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
    gen_update_cfar(ctx, ctx->cia);
    if (type == BCOND_IM) {
        target_ulong li = (target_long)((int16_t)(BD(ctx->opcode)));
        if (likely(AA(ctx->opcode) == 0)) {
            gen_goto_tb(ctx, 0, ctx->cia + li);
        } else {
            gen_goto_tb(ctx, 0, li);
        }
    } else {
        if (NARROW_MODE(ctx)) {
            tcg_gen_andi_tl(cpu_nip, target, (uint32_t)~3);
        } else {
            tcg_gen_andi_tl(cpu_nip, target, ~3);
        }
        gen_lookup_and_goto_ptr(ctx);
        tcg_temp_free(target);
    }
    if ((bo & 0x14) != 0x14) {
        /* fallthrough case */
        gen_set_label(l1);
        gen_goto_tb(ctx, 1, ctx->base.pc_next);
    }
    ctx->base.is_jmp = DISAS_NORETURN;
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
static void glue(gen_, name)(DisasContext *ctx)                               \
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
    bitmask = 0x08 >> (crbD(ctx->opcode) & 0x03);                             \
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

/* rfi (supervisor only) */
static void gen_rfi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    /*
     * This instruction doesn't exist anymore on 64-bit server
     * processors compliant with arch 2.x
     */
    if (is_book3s_arch2x(ctx)) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
        return;
    }
    /* Restore CPU state */
    CHK_SV(ctx);
    gen_icount_io_start(ctx);
    gen_update_cfar(ctx, ctx->cia);
    gen_helper_rfi(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif
}

#if defined(TARGET_PPC64)
static void gen_rfid(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    /* Restore CPU state */
    CHK_SV(ctx);
    gen_icount_io_start(ctx);
    gen_update_cfar(ctx, ctx->cia);
    gen_helper_rfid(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif
}

#if !defined(CONFIG_USER_ONLY)
static void gen_rfscv(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    /* Restore CPU state */
    CHK_SV(ctx);
    gen_icount_io_start(ctx);
    gen_update_cfar(ctx, ctx->cia);
    gen_helper_rfscv(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif
}
#endif

static void gen_hrfid(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    /* Restore CPU state */
    CHK_HV(ctx);
    gen_helper_hrfid(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif
}
#endif

/* sc */
#if defined(CONFIG_USER_ONLY)
#define POWERPC_SYSCALL POWERPC_EXCP_SYSCALL_USER
#else
#define POWERPC_SYSCALL POWERPC_EXCP_SYSCALL
#define POWERPC_SYSCALL_VECTORED POWERPC_EXCP_SYSCALL_VECTORED
#endif
static void gen_sc(DisasContext *ctx)
{
    uint32_t lev;

    lev = (ctx->opcode >> 5) & 0x7F;
    gen_exception_err(ctx, POWERPC_SYSCALL, lev);
}

#if defined(TARGET_PPC64)
#if !defined(CONFIG_USER_ONLY)
static void gen_scv(DisasContext *ctx)
{
    uint32_t lev = (ctx->opcode >> 5) & 0x7F;

    /* Set the PC back to the faulting instruction. */
    gen_update_nip(ctx, ctx->cia);
    gen_helper_scv(cpu_env, tcg_constant_i32(lev));

    ctx->base.is_jmp = DISAS_NORETURN;
}
#endif
#endif

/***                                Trap                                   ***/

/* Check for unconditional traps (always or never) */
static bool check_unconditional_trap(DisasContext *ctx)
{
    /* Trap never */
    if (TO(ctx->opcode) == 0) {
        return true;
    }
    /* Trap always */
    if (TO(ctx->opcode) == 31) {
        gen_exception_err(ctx, POWERPC_EXCP_PROGRAM, POWERPC_EXCP_TRAP);
        return true;
    }
    return false;
}

/* tw */
static void gen_tw(DisasContext *ctx)
{
    TCGv_i32 t0;

    if (check_unconditional_trap(ctx)) {
        return;
    }
    t0 = tcg_const_i32(TO(ctx->opcode));
    gen_helper_tw(cpu_env, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                  t0);
    tcg_temp_free_i32(t0);
}

/* twi */
static void gen_twi(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1;

    if (check_unconditional_trap(ctx)) {
        return;
    }
    t0 = tcg_const_tl(SIMM(ctx->opcode));
    t1 = tcg_const_i32(TO(ctx->opcode));
    gen_helper_tw(cpu_env, cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}

#if defined(TARGET_PPC64)
/* td */
static void gen_td(DisasContext *ctx)
{
    TCGv_i32 t0;

    if (check_unconditional_trap(ctx)) {
        return;
    }
    t0 = tcg_const_i32(TO(ctx->opcode));
    gen_helper_td(cpu_env, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)],
                  t0);
    tcg_temp_free_i32(t0);
}

/* tdi */
static void gen_tdi(DisasContext *ctx)
{
    TCGv t0;
    TCGv_i32 t1;

    if (check_unconditional_trap(ctx)) {
        return;
    }
    t0 = tcg_const_tl(SIMM(ctx->opcode));
    t1 = tcg_const_i32(TO(ctx->opcode));
    gen_helper_td(cpu_env, cpu_gpr[rA(ctx->opcode)], t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free_i32(t1);
}
#endif

/***                          Processor control                            ***/

/* mcrxr */
static void gen_mcrxr(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 dst = cpu_crf[crfD(ctx->opcode)];

    tcg_gen_trunc_tl_i32(t0, cpu_so);
    tcg_gen_trunc_tl_i32(t1, cpu_ov);
    tcg_gen_trunc_tl_i32(dst, cpu_ca);
    tcg_gen_shli_i32(t0, t0, 3);
    tcg_gen_shli_i32(t1, t1, 2);
    tcg_gen_shli_i32(dst, dst, 1);
    tcg_gen_or_i32(dst, dst, t0);
    tcg_gen_or_i32(dst, dst, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);

    tcg_gen_movi_tl(cpu_so, 0);
    tcg_gen_movi_tl(cpu_ov, 0);
    tcg_gen_movi_tl(cpu_ca, 0);
}

#ifdef TARGET_PPC64
/* mcrxrx */
static void gen_mcrxrx(DisasContext *ctx)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i32 dst = cpu_crf[crfD(ctx->opcode)];

    /* copy OV and OV32 */
    tcg_gen_shli_tl(t0, cpu_ov, 1);
    tcg_gen_or_tl(t0, t0, cpu_ov32);
    tcg_gen_shli_tl(t0, t0, 2);
    /* copy CA and CA32 */
    tcg_gen_shli_tl(t1, cpu_ca, 1);
    tcg_gen_or_tl(t1, t1, cpu_ca32);
    tcg_gen_or_tl(t0, t0, t1);
    tcg_gen_trunc_tl_i32(dst, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}
#endif

/* mfcr mfocrf */
static void gen_mfcr(DisasContext *ctx)
{
    uint32_t crm, crn;

    if (likely(ctx->opcode & 0x00100000)) {
        crm = CRM(ctx->opcode);
        if (likely(crm && ((crm & (crm - 1)) == 0))) {
            crn = ctz32(crm);
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
    CHK_SV(ctx);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], cpu_msr);
}

/* mfspr */
static inline void gen_op_mfspr(DisasContext *ctx)
{
    void (*read_cb)(DisasContext *ctx, int gprn, int sprn);
    uint32_t sprn = SPR(ctx->opcode);

#if defined(CONFIG_USER_ONLY)
    read_cb = ctx->spr_cb[sprn].uea_read;
#else
    if (ctx->pr) {
        read_cb = ctx->spr_cb[sprn].uea_read;
    } else if (ctx->hv) {
        read_cb = ctx->spr_cb[sprn].hea_read;
    } else {
        read_cb = ctx->spr_cb[sprn].oea_read;
    }
#endif
    if (likely(read_cb != NULL)) {
        if (likely(read_cb != SPR_NOACCESS)) {
            (*read_cb)(ctx, rD(ctx->opcode), sprn);
        } else {
            /* Privilege exception */
            /*
             * This is a hack to avoid warnings when running Linux:
             * this OS breaks the PowerPC virtualisation model,
             * allowing userland application to read the PVR
             */
            if (sprn != SPR_PVR) {
                qemu_log_mask(LOG_GUEST_ERROR, "Trying to read privileged spr "
                              "%d (0x%03x) at " TARGET_FMT_lx "\n", sprn, sprn,
                              ctx->cia);
            }
            gen_priv_exception(ctx, POWERPC_EXCP_PRIV_REG);
        }
    } else {
        /* ISA 2.07 defines these as no-ops */
        if ((ctx->insns_flags2 & PPC2_ISA207S) &&
            (sprn >= 808 && sprn <= 811)) {
            /* This is a nop */
            return;
        }
        /* Not defined */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Trying to read invalid spr %d (0x%03x) at "
                      TARGET_FMT_lx "\n", sprn, sprn, ctx->cia);

        /*
         * The behaviour depends on MSR:PR and SPR# bit 0x10, it can
         * generate a priv, a hv emu or a no-op
         */
        if (sprn & 0x10) {
            if (ctx->pr) {
                gen_priv_exception(ctx, POWERPC_EXCP_PRIV_REG);
            }
        } else {
            if (ctx->pr || sprn == 0 || sprn == 4 || sprn == 5 || sprn == 6) {
                gen_hvpriv_exception(ctx, POWERPC_EXCP_PRIV_REG);
            }
        }
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
            crn = ctz32(crm);
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
    if (unlikely(!is_book3s_arch2x(ctx))) {
        gen_invalid(ctx);
        return;
    }

    CHK_SV(ctx);

#if !defined(CONFIG_USER_ONLY)
    TCGv t0, t1;
    target_ulong mask;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_icount_io_start(ctx);

    if (ctx->opcode & 0x00010000) {
        /* L=1 form only updates EE and RI */
        mask = (1ULL << MSR_RI) | (1ULL << MSR_EE);
    } else {
        /* mtmsrd does not alter HV, S, ME, or LE */
        mask = ~((1ULL << MSR_LE) | (1ULL << MSR_ME) | (1ULL << MSR_S) |
                 (1ULL << MSR_HV));
        /*
         * XXX: we need to update nip before the store if we enter
         *      power saving mode, we will exit the loop directly from
         *      ppc_store_msr
         */
        gen_update_nip(ctx, ctx->base.pc_next);
    }

    tcg_gen_andi_tl(t0, cpu_gpr[rS(ctx->opcode)], mask);
    tcg_gen_andi_tl(t1, cpu_msr, ~mask);
    tcg_gen_or_tl(t0, t0, t1);

    gen_helper_store_msr(cpu_env, t0);

    /* Must stop the translation as machine state (may have) changed */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;

    tcg_temp_free(t0);
    tcg_temp_free(t1);
#endif /* !defined(CONFIG_USER_ONLY) */
}
#endif /* defined(TARGET_PPC64) */

static void gen_mtmsr(DisasContext *ctx)
{
    CHK_SV(ctx);

#if !defined(CONFIG_USER_ONLY)
    TCGv t0, t1;
    target_ulong mask = 0xFFFFFFFF;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_icount_io_start(ctx);
    if (ctx->opcode & 0x00010000) {
        /* L=1 form only updates EE and RI */
        mask &= (1ULL << MSR_RI) | (1ULL << MSR_EE);
    } else {
        /* mtmsr does not alter S, ME, or LE */
        mask &= ~((1ULL << MSR_LE) | (1ULL << MSR_ME) | (1ULL << MSR_S));

        /*
         * XXX: we need to update nip before the store if we enter
         *      power saving mode, we will exit the loop directly from
         *      ppc_store_msr
         */
        gen_update_nip(ctx, ctx->base.pc_next);
    }

    tcg_gen_andi_tl(t0, cpu_gpr[rS(ctx->opcode)], mask);
    tcg_gen_andi_tl(t1, cpu_msr, ~mask);
    tcg_gen_or_tl(t0, t0, t1);

    gen_helper_store_msr(cpu_env, t0);

    /* Must stop the translation as machine state (may have) changed */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;

    tcg_temp_free(t0);
    tcg_temp_free(t1);
#endif
}

/* mtspr */
static void gen_mtspr(DisasContext *ctx)
{
    void (*write_cb)(DisasContext *ctx, int sprn, int gprn);
    uint32_t sprn = SPR(ctx->opcode);

#if defined(CONFIG_USER_ONLY)
    write_cb = ctx->spr_cb[sprn].uea_write;
#else
    if (ctx->pr) {
        write_cb = ctx->spr_cb[sprn].uea_write;
    } else if (ctx->hv) {
        write_cb = ctx->spr_cb[sprn].hea_write;
    } else {
        write_cb = ctx->spr_cb[sprn].oea_write;
    }
#endif
    if (likely(write_cb != NULL)) {
        if (likely(write_cb != SPR_NOACCESS)) {
            (*write_cb)(ctx, sprn, rS(ctx->opcode));
        } else {
            /* Privilege exception */
            qemu_log_mask(LOG_GUEST_ERROR, "Trying to write privileged spr "
                          "%d (0x%03x) at " TARGET_FMT_lx "\n", sprn, sprn,
                          ctx->cia);
            gen_priv_exception(ctx, POWERPC_EXCP_PRIV_REG);
        }
    } else {
        /* ISA 2.07 defines these as no-ops */
        if ((ctx->insns_flags2 & PPC2_ISA207S) &&
            (sprn >= 808 && sprn <= 811)) {
            /* This is a nop */
            return;
        }

        /* Not defined */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Trying to write invalid spr %d (0x%03x) at "
                      TARGET_FMT_lx "\n", sprn, sprn, ctx->cia);


        /*
         * The behaviour depends on MSR:PR and SPR# bit 0x10, it can
         * generate a priv, a hv emu or a no-op
         */
        if (sprn & 0x10) {
            if (ctx->pr) {
                gen_priv_exception(ctx, POWERPC_EXCP_PRIV_REG);
            }
        } else {
            if (ctx->pr || sprn == 0) {
                gen_hvpriv_exception(ctx, POWERPC_EXCP_PRIV_REG);
            }
        }
    }
}

#if defined(TARGET_PPC64)
/* setb */
static void gen_setb(DisasContext *ctx)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t8 = tcg_constant_i32(8);
    TCGv_i32 tm1 = tcg_constant_i32(-1);
    int crf = crfS(ctx->opcode);

    tcg_gen_setcondi_i32(TCG_COND_GEU, t0, cpu_crf[crf], 4);
    tcg_gen_movcond_i32(TCG_COND_GEU, t0, cpu_crf[crf], t8, tm1, t0);
    tcg_gen_ext_i32_tl(cpu_gpr[rD(ctx->opcode)], t0);

    tcg_temp_free_i32(t0);
}
#endif

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

/* dcbfep (external PID dcbf) */
static void gen_dcbfep(DisasContext *ctx)
{
    /* XXX: specification says this is treated as a load by the MMU */
    TCGv t0;
    CHK_SV(ctx);
    gen_set_access_type(ctx, ACCESS_CACHE);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    tcg_gen_qemu_ld_tl(t0, t0, PPC_TLB_EPID_LOAD, DEF_MEMOP(MO_UB));
    tcg_temp_free(t0);
}

/* dcbi (Supervisor only) */
static void gen_dcbi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv EA, val;

    CHK_SV(ctx);
    EA = tcg_temp_new();
    gen_set_access_type(ctx, ACCESS_CACHE);
    gen_addr_reg_index(ctx, EA);
    val = tcg_temp_new();
    /* XXX: specification says this should be treated as a store by the MMU */
    gen_qemu_ld8u(ctx, val, EA);
    gen_qemu_st8(ctx, val, EA);
    tcg_temp_free(val);
    tcg_temp_free(EA);
#endif /* defined(CONFIG_USER_ONLY) */
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

/* dcbstep (dcbstep External PID version) */
static void gen_dcbstep(DisasContext *ctx)
{
    /* XXX: specification say this is treated as a load by the MMU */
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_CACHE);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    tcg_gen_qemu_ld_tl(t0, t0, PPC_TLB_EPID_LOAD, DEF_MEMOP(MO_UB));
    tcg_temp_free(t0);
}

/* dcbt */
static void gen_dcbt(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

/* dcbtep */
static void gen_dcbtep(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

/* dcbtst */
static void gen_dcbtst(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

/* dcbtstep */
static void gen_dcbtstep(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

/* dcbtls */
static void gen_dcbtls(DisasContext *ctx)
{
    /* Always fails locking the cache */
    TCGv t0 = tcg_temp_new();
    gen_load_spr(t0, SPR_Exxx_L1CSR0);
    tcg_gen_ori_tl(t0, t0, L1CSR0_CUL);
    gen_store_spr(SPR_Exxx_L1CSR0, t0);
    tcg_temp_free(t0);
}

/* dcbz */
static void gen_dcbz(DisasContext *ctx)
{
    TCGv tcgv_addr;
    TCGv_i32 tcgv_op;

    gen_set_access_type(ctx, ACCESS_CACHE);
    tcgv_addr = tcg_temp_new();
    tcgv_op = tcg_const_i32(ctx->opcode & 0x03FF000);
    gen_addr_reg_index(ctx, tcgv_addr);
    gen_helper_dcbz(cpu_env, tcgv_addr, tcgv_op);
    tcg_temp_free(tcgv_addr);
    tcg_temp_free_i32(tcgv_op);
}

/* dcbzep */
static void gen_dcbzep(DisasContext *ctx)
{
    TCGv tcgv_addr;
    TCGv_i32 tcgv_op;

    gen_set_access_type(ctx, ACCESS_CACHE);
    tcgv_addr = tcg_temp_new();
    tcgv_op = tcg_const_i32(ctx->opcode & 0x03FF000);
    gen_addr_reg_index(ctx, tcgv_addr);
    gen_helper_dcbzep(cpu_env, tcgv_addr, tcgv_op);
    tcg_temp_free(tcgv_addr);
    tcg_temp_free_i32(tcgv_op);
}

/* dst / dstt */
static void gen_dst(DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
    } else {
        /* interpreted as no-op */
    }
}

/* dstst /dststt */
static void gen_dstst(DisasContext *ctx)
{
    if (rA(ctx->opcode) == 0) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
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
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_icbi(cpu_env, t0);
    tcg_temp_free(t0);
}

/* icbiep */
static void gen_icbiep(DisasContext *ctx)
{
    TCGv t0;
    gen_set_access_type(ctx, ACCESS_CACHE);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_icbiep(cpu_env, t0);
    tcg_temp_free(t0);
}

/* Optional: */
/* dcba */
static void gen_dcba(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a store by the MMU
     *      but does not generate any exception
     */
}

/***                    Segment register manipulation                      ***/
/* Supervisor only: */

/* mfsr */
static void gen_mfsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mfsrin */
static void gen_mfsrin(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    tcg_gen_extract_tl(t0, cpu_gpr[rB(ctx->opcode)], 28, 4);
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtsr */
static void gen_mtsr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtsrin */
static void gen_mtsrin(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;
    CHK_SV(ctx);

    t0 = tcg_temp_new();
    tcg_gen_extract_tl(t0, cpu_gpr[rB(ctx->opcode)], 28, 4);
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rD(ctx->opcode)]);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

#if defined(TARGET_PPC64)
/* Specific implementation for PowerPC 64 "bridge" emulation using SLB */

/* mfsr */
static void gen_mfsr_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mfsrin */
static void gen_mfsrin_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    tcg_gen_extract_tl(t0, cpu_gpr[rB(ctx->opcode)], 28, 4);
    gen_helper_load_sr(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtsr */
static void gen_mtsr_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_const_tl(SR(ctx->opcode));
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtsrin */
static void gen_mtsrin_64b(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    tcg_gen_extract_tl(t0, cpu_gpr[rB(ctx->opcode)], 28, 4);
    gen_helper_store_sr(cpu_env, t0, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

#endif /* defined(TARGET_PPC64) */

/***                      Lookaside buffer management                      ***/
/* Optional & supervisor only: */

/* tlbia */
static void gen_tlbia(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_HV(ctx);

    gen_helper_tlbia(cpu_env);
#endif  /* defined(CONFIG_USER_ONLY) */
}

/* tlbsync */
static void gen_tlbsync(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else

    if (ctx->gtse) {
        CHK_SV(ctx); /* If gtse is set then tlbsync is supervisor privileged */
    } else {
        CHK_HV(ctx); /* Else hypervisor privileged */
    }

    /* BookS does both ptesync and tlbsync make tlbsync a nop for server */
    if (ctx->insns_flags & PPC_BOOKE) {
        gen_check_tlb_flush(ctx, true);
    }
#endif /* defined(CONFIG_USER_ONLY) */
}

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
    tcg_gen_qemu_ld_tl(cpu_gpr[rD(ctx->opcode)], t0, ctx->mem_idx,
                       DEF_MEMOP(MO_UL | MO_ALIGN));
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
    tcg_gen_qemu_st_tl(cpu_gpr[rD(ctx->opcode)], t0, ctx->mem_idx,
                       DEF_MEMOP(MO_UL | MO_ALIGN));
    tcg_temp_free(t0);
}

/* 602 - 603 - G2 TLB management */

/* tlbld */
static void gen_tlbld_6xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    gen_helper_6xx_tlbd(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbli */
static void gen_tlbli_6xx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    gen_helper_6xx_tlbi(cpu_env, cpu_gpr[rB(ctx->opcode)]);
#endif /* defined(CONFIG_USER_ONLY) */
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
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_tlbiva(cpu_env, cpu_gpr[rB(ctx->opcode)]);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
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
            TCGLabel *l1 = gen_new_label();

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
    GEN_PRIV(ctx);
#else
    TCGv dcrn;

    CHK_SV(ctx);
    dcrn = tcg_const_tl(SPR(ctx->opcode));
    gen_helper_load_dcr(cpu_gpr[rD(ctx->opcode)], cpu_env, dcrn);
    tcg_temp_free(dcrn);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtdcr */
static void gen_mtdcr(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv dcrn;

    CHK_SV(ctx);
    dcrn = tcg_const_tl(SPR(ctx->opcode));
    gen_helper_store_dcr(cpu_env, dcrn, cpu_gpr[rS(ctx->opcode)]);
    tcg_temp_free(dcrn);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mfdcrx */
/* XXX: not implemented on 440 ? */
static void gen_mfdcrx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    gen_helper_load_dcr(cpu_gpr[rD(ctx->opcode)], cpu_env,
                        cpu_gpr[rA(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
#endif /* defined(CONFIG_USER_ONLY) */
}

/* mtdcrx */
/* XXX: not implemented on 440 ? */
static void gen_mtdcrx(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    gen_helper_store_dcr(cpu_env, cpu_gpr[rA(ctx->opcode)],
                         cpu_gpr[rS(ctx->opcode)]);
    /* Note: Rc update flag set leads to undefined state of Rc0 */
#endif /* defined(CONFIG_USER_ONLY) */
}

/* dccci */
static void gen_dccci(DisasContext *ctx)
{
    CHK_SV(ctx);
    /* interpreted as no-op */
}

/* dcread */
static void gen_dcread(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv EA, val;

    CHK_SV(ctx);
    gen_set_access_type(ctx, ACCESS_CACHE);
    EA = tcg_temp_new();
    gen_addr_reg_index(ctx, EA);
    val = tcg_temp_new();
    gen_qemu_ld32u(ctx, val, EA);
    tcg_temp_free(val);
    tcg_gen_mov_tl(cpu_gpr[rD(ctx->opcode)], EA);
    tcg_temp_free(EA);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* icbt */
static void gen_icbt_40x(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

/* iccci */
static void gen_iccci(DisasContext *ctx)
{
    CHK_SV(ctx);
    /* interpreted as no-op */
}

/* icread */
static void gen_icread(DisasContext *ctx)
{
    CHK_SV(ctx);
    /* interpreted as no-op */
}

/* rfci (supervisor only) */
static void gen_rfci_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    /* Restore CPU state */
    gen_helper_40x_rfci(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_rfci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    /* Restore CPU state */
    gen_helper_rfci(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif /* defined(CONFIG_USER_ONLY) */
}

/* BookE specific */

/* XXX: not implemented on 440 ? */
static void gen_rfdi(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    /* Restore CPU state */
    gen_helper_rfdi(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif /* defined(CONFIG_USER_ONLY) */
}

/* XXX: not implemented on 440 ? */
static void gen_rfmci(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    /* Restore CPU state */
    gen_helper_rfmci(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
#endif /* defined(CONFIG_USER_ONLY) */
}

/* TLB management - PowerPC 405 implementation */

/* tlbre */
static void gen_tlbre_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
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
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_4xx_tlbsx(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
    if (Rc(ctx->opcode)) {
        TCGLabel *l1 = gen_new_label();
        tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[rD(ctx->opcode)], -1, l1);
        tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], 0x02);
        gen_set_label(l1);
    }
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbwe */
static void gen_tlbwe_40x(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);

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
#endif /* defined(CONFIG_USER_ONLY) */
}

/* TLB management - PowerPC 440 implementation */

/* tlbre */
static void gen_tlbre_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);

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
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_440_tlbsx(cpu_gpr[rD(ctx->opcode)], cpu_env, t0);
    tcg_temp_free(t0);
    if (Rc(ctx->opcode)) {
        TCGLabel *l1 = gen_new_label();
        tcg_gen_trunc_tl_i32(cpu_crf[0], cpu_so);
        tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[rD(ctx->opcode)], -1, l1);
        tcg_gen_ori_i32(cpu_crf[0], cpu_crf[0], 0x02);
        gen_set_label(l1);
    }
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbwe */
static void gen_tlbwe_440(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
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
#endif /* defined(CONFIG_USER_ONLY) */
}

/* TLB management - PowerPC BookE 2.06 implementation */

/* tlbre */
static void gen_tlbre_booke206(DisasContext *ctx)
{
 #if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
   CHK_SV(ctx);
    gen_helper_booke206_tlbre(cpu_env);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbsx - tlbsx. */
static void gen_tlbsx_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    if (rA(ctx->opcode)) {
        t0 = tcg_temp_new();
        tcg_gen_mov_tl(t0, cpu_gpr[rD(ctx->opcode)]);
    } else {
        t0 = tcg_const_tl(0);
    }

    tcg_gen_add_tl(t0, t0, cpu_gpr[rB(ctx->opcode)]);
    gen_helper_booke206_tlbsx(cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

/* tlbwe */
static void gen_tlbwe_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    gen_helper_booke206_tlbwe(cpu_env);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_tlbivax_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);
    gen_helper_booke206_tlbivax(cpu_env, t0);
    tcg_temp_free(t0);
#endif /* defined(CONFIG_USER_ONLY) */
}

static void gen_tlbilx_booke206(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    gen_addr_reg_index(ctx, t0);

    switch ((ctx->opcode >> 21) & 0x3) {
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
#endif /* defined(CONFIG_USER_ONLY) */
}

/* wrtee */
static void gen_wrtee(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    TCGv t0;

    CHK_SV(ctx);
    t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[rD(ctx->opcode)], (1 << MSR_EE));
    tcg_gen_andi_tl(cpu_msr, cpu_msr, ~(1 << MSR_EE));
    tcg_gen_or_tl(cpu_msr, cpu_msr, t0);
    gen_ppc_maybe_interrupt(ctx);
    tcg_temp_free(t0);
    /*
     * Stop translation to have a chance to raise an exception if we
     * just set msr_ee to 1
     */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
#endif /* defined(CONFIG_USER_ONLY) */
}

/* wrteei */
static void gen_wrteei(DisasContext *ctx)
{
#if defined(CONFIG_USER_ONLY)
    GEN_PRIV(ctx);
#else
    CHK_SV(ctx);
    if (ctx->opcode & 0x00008000) {
        tcg_gen_ori_tl(cpu_msr, cpu_msr, (1 << MSR_EE));
        gen_ppc_maybe_interrupt(ctx);
        /* Stop translation to have a chance to raise an exception */
        ctx->base.is_jmp = DISAS_EXIT_UPDATE;
    } else {
        tcg_gen_andi_tl(cpu_msr, cpu_msr, ~(1 << MSR_EE));
    }
#endif /* defined(CONFIG_USER_ONLY) */
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
    /* Only e500 seems to treat reserved bits as invalid */
    if ((ctx->insns_flags2 & PPC2_BOOKE206) &&
        (ctx->opcode & 0x03FFF801)) {
        gen_inval_exception(ctx, POWERPC_EXCP_INVAL_INVAL);
    }
    /* otherwise interpreted as no-op */
}

/* icbt */
static void gen_icbt_440(DisasContext *ctx)
{
    /*
     * interpreted as no-op
     * XXX: specification say this is treated as a load by the MMU but
     *      does not generate any exception
     */
}

#if defined(TARGET_PPC64)
static void gen_maddld(DisasContext *ctx)
{
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_mul_i64(t1, cpu_gpr[rA(ctx->opcode)], cpu_gpr[rB(ctx->opcode)]);
    tcg_gen_add_i64(cpu_gpr[rD(ctx->opcode)], t1, cpu_gpr[rC(ctx->opcode)]);
    tcg_temp_free_i64(t1);
}

/* maddhd maddhdu */
static void gen_maddhd_maddhdu(DisasContext *ctx)
{
    TCGv_i64 lo = tcg_temp_new_i64();
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    if (Rc(ctx->opcode)) {
        tcg_gen_mulu2_i64(lo, hi, cpu_gpr[rA(ctx->opcode)],
                          cpu_gpr[rB(ctx->opcode)]);
        tcg_gen_movi_i64(t1, 0);
    } else {
        tcg_gen_muls2_i64(lo, hi, cpu_gpr[rA(ctx->opcode)],
                          cpu_gpr[rB(ctx->opcode)]);
        tcg_gen_sari_i64(t1, cpu_gpr[rC(ctx->opcode)], 63);
    }
    tcg_gen_add2_i64(t1, cpu_gpr[rD(ctx->opcode)], lo, hi,
                     cpu_gpr[rC(ctx->opcode)], t1);
    tcg_temp_free_i64(lo);
    tcg_temp_free_i64(hi);
    tcg_temp_free_i64(t1);
}
#endif /* defined(TARGET_PPC64) */

static void gen_tbegin(DisasContext *ctx)
{
    if (unlikely(!ctx->tm_enabled)) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_TM);
        return;
    }
    gen_helper_tbegin(cpu_env);
}

#define GEN_TM_NOOP(name)                                      \
static inline void gen_##name(DisasContext *ctx)               \
{                                                              \
    if (unlikely(!ctx->tm_enabled)) {                          \
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_TM);   \
        return;                                                \
    }                                                          \
    /*                                                         \
     * Because tbegin always fails in QEMU, these user         \
     * space instructions all have a simple implementation:    \
     *                                                         \
     *     CR[0] = 0b0 || MSR[TS] || 0b0                       \
     *           = 0b0 || 0b00    || 0b0                       \
     */                                                        \
    tcg_gen_movi_i32(cpu_crf[0], 0);                           \
}

GEN_TM_NOOP(tend);
GEN_TM_NOOP(tabort);
GEN_TM_NOOP(tabortwc);
GEN_TM_NOOP(tabortwci);
GEN_TM_NOOP(tabortdc);
GEN_TM_NOOP(tabortdci);
GEN_TM_NOOP(tsr);

static inline void gen_cp_abort(DisasContext *ctx)
{
    /* Do Nothing */
}

#define GEN_CP_PASTE_NOOP(name)                           \
static inline void gen_##name(DisasContext *ctx)          \
{                                                         \
    /*                                                    \
     * Generate invalid exception until we have an        \
     * implementation of the copy paste facility          \
     */                                                   \
    gen_invalid(ctx);                                     \
}

GEN_CP_PASTE_NOOP(copy)
GEN_CP_PASTE_NOOP(paste)

static void gen_tcheck(DisasContext *ctx)
{
    if (unlikely(!ctx->tm_enabled)) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_TM);
        return;
    }
    /*
     * Because tbegin always fails, the tcheck implementation is
     * simple:
     *
     * CR[CRF] = TDOOMED || MSR[TS] || 0b0
     *         = 0b1 || 0b00 || 0b0
     */
    tcg_gen_movi_i32(cpu_crf[crfD(ctx->opcode)], 0x8);
}

#if defined(CONFIG_USER_ONLY)
#define GEN_TM_PRIV_NOOP(name)                                 \
static inline void gen_##name(DisasContext *ctx)               \
{                                                              \
    gen_priv_opc(ctx);                                         \
}

#else

#define GEN_TM_PRIV_NOOP(name)                                 \
static inline void gen_##name(DisasContext *ctx)               \
{                                                              \
    CHK_SV(ctx);                                               \
    if (unlikely(!ctx->tm_enabled)) {                          \
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_TM);   \
        return;                                                \
    }                                                          \
    /*                                                         \
     * Because tbegin always fails, the implementation is      \
     * simple:                                                 \
     *                                                         \
     *   CR[0] = 0b0 || MSR[TS] || 0b0                         \
     *         = 0b0 || 0b00 | 0b0                             \
     */                                                        \
    tcg_gen_movi_i32(cpu_crf[0], 0);                           \
}

#endif

GEN_TM_PRIV_NOOP(treclaim);
GEN_TM_PRIV_NOOP(trechkpt);

static inline void get_fpr(TCGv_i64 dst, int regno)
{
    tcg_gen_ld_i64(dst, cpu_env, fpr_offset(regno));
}

static inline void set_fpr(int regno, TCGv_i64 src)
{
    tcg_gen_st_i64(src, cpu_env, fpr_offset(regno));
    /*
     * Before PowerISA v3.1 the result of doubleword 1 of the VSR
     * corresponding to the target FPR was undefined. However,
     * most (if not all) real hardware were setting the result to 0.
     * Starting at ISA v3.1, the result for doubleword 1 is now defined
     * to be 0.
     */
    tcg_gen_st_i64(tcg_constant_i64(0), cpu_env, vsr64_offset(regno, false));
}

static inline void get_avr64(TCGv_i64 dst, int regno, bool high)
{
    tcg_gen_ld_i64(dst, cpu_env, avr64_offset(regno, high));
}

static inline void set_avr64(int regno, TCGv_i64 src, bool high)
{
    tcg_gen_st_i64(src, cpu_env, avr64_offset(regno, high));
}

/*
 * Helpers for decodetree used by !function for decoding arguments.
 */
static int times_2(DisasContext *ctx, int x)
{
    return x * 2;
}

static int times_4(DisasContext *ctx, int x)
{
    return x * 4;
}

static int times_16(DisasContext *ctx, int x)
{
    return x * 16;
}

static int64_t dw_compose_ea(DisasContext *ctx, int x)
{
    return deposit64(0xfffffffffffffe00, 3, 6, x);
}

/*
 * Helpers for trans_* functions to check for specific insns flags.
 * Use token pasting to ensure that we use the proper flag with the
 * proper variable.
 */
#define REQUIRE_INSNS_FLAGS(CTX, NAME) \
    do {                                                \
        if (((CTX)->insns_flags & PPC_##NAME) == 0) {   \
            return false;                               \
        }                                               \
    } while (0)

#define REQUIRE_INSNS_FLAGS2(CTX, NAME) \
    do {                                                \
        if (((CTX)->insns_flags2 & PPC2_##NAME) == 0) { \
            return false;                               \
        }                                               \
    } while (0)

/* Then special-case the check for 64-bit so that we elide code for ppc32. */
#if TARGET_LONG_BITS == 32
# define REQUIRE_64BIT(CTX)  return false
#else
# define REQUIRE_64BIT(CTX)  REQUIRE_INSNS_FLAGS(CTX, 64B)
#endif

#define REQUIRE_VECTOR(CTX)                             \
    do {                                                \
        if (unlikely(!(CTX)->altivec_enabled)) {        \
            gen_exception((CTX), POWERPC_EXCP_VPU);     \
            return true;                                \
        }                                               \
    } while (0)

#define REQUIRE_VSX(CTX)                                \
    do {                                                \
        if (unlikely(!(CTX)->vsx_enabled)) {            \
            gen_exception((CTX), POWERPC_EXCP_VSXU);    \
            return true;                                \
        }                                               \
    } while (0)

#define REQUIRE_FPU(ctx)                                \
    do {                                                \
        if (unlikely(!(ctx)->fpu_enabled)) {            \
            gen_exception((ctx), POWERPC_EXCP_FPU);     \
            return true;                                \
        }                                               \
    } while (0)

#if !defined(CONFIG_USER_ONLY)
#define REQUIRE_SV(CTX)             \
    do {                            \
        if (unlikely((CTX)->pr)) {  \
            gen_priv_opc(CTX);      \
            return true;            \
        }                           \
    } while (0)

#define REQUIRE_HV(CTX)                             \
    do {                                            \
        if (unlikely((CTX)->pr || !(CTX)->hv)) {    \
            gen_priv_opc(CTX);                      \
            return true;                            \
        }                                           \
    } while (0)
#else
#define REQUIRE_SV(CTX) do { gen_priv_opc(CTX); return true; } while (0)
#define REQUIRE_HV(CTX) do { gen_priv_opc(CTX); return true; } while (0)
#endif

/*
 * Helpers for implementing sets of trans_* functions.
 * Defer the implementation of NAME to FUNC, with optional extra arguments.
 */
#define TRANS(NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    { return FUNC(ctx, a, __VA_ARGS__); }
#define TRANS_FLAGS(FLAGS, NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    {                                                          \
        REQUIRE_INSNS_FLAGS(ctx, FLAGS);                       \
        return FUNC(ctx, a, __VA_ARGS__);                      \
    }
#define TRANS_FLAGS2(FLAGS2, NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    {                                                          \
        REQUIRE_INSNS_FLAGS2(ctx, FLAGS2);                     \
        return FUNC(ctx, a, __VA_ARGS__);                      \
    }

#define TRANS64(NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    { REQUIRE_64BIT(ctx); return FUNC(ctx, a, __VA_ARGS__); }
#define TRANS64_FLAGS2(FLAGS2, NAME, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME *a) \
    {                                                          \
        REQUIRE_64BIT(ctx);                                    \
        REQUIRE_INSNS_FLAGS2(ctx, FLAGS2);                     \
        return FUNC(ctx, a, __VA_ARGS__);                      \
    }

/* TODO: More TRANS* helpers for extra insn_flags checks. */


#include "decode-insn32.c.inc"
#include "decode-insn64.c.inc"
#include "power8-pmu-regs.c.inc"

/*
 * Incorporate CIA into the constant when R=1.
 * Validate that when R=1, RA=0.
 */
static bool resolve_PLS_D(DisasContext *ctx, arg_D *d, arg_PLS_D *a)
{
    d->rt = a->rt;
    d->ra = a->ra;
    d->si = a->si;
    if (a->r) {
        if (unlikely(a->ra != 0)) {
            gen_invalid(ctx);
            return false;
        }
        d->si += ctx->cia;
    }
    return true;
}

#include "translate/fixedpoint-impl.c.inc"

#include "translate/fp-impl.c.inc"

#include "translate/vmx-impl.c.inc"

#include "translate/vsx-impl.c.inc"

#include "translate/dfp-impl.c.inc"

#include "translate/spe-impl.c.inc"

#include "translate/branch-impl.c.inc"

#include "translate/processor-ctrl-impl.c.inc"

#include "translate/storage-ctrl-impl.c.inc"

/* Handles lfdp */
static void gen_dform39(DisasContext *ctx)
{
    if ((ctx->opcode & 0x3) == 0) {
        if (ctx->insns_flags2 & PPC2_ISA205) {
            return gen_lfdp(ctx);
        }
    }
    return gen_invalid(ctx);
}

/* Handles stfdp */
static void gen_dform3D(DisasContext *ctx)
{
    if ((ctx->opcode & 3) == 0) { /* DS-FORM */
        /* stfdp */
        if (ctx->insns_flags2 & PPC2_ISA205) {
            return gen_stfdp(ctx);
        }
    }
    return gen_invalid(ctx);
}

#if defined(TARGET_PPC64)
/* brd */
static void gen_brd(DisasContext *ctx)
{
    tcg_gen_bswap64_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
}

/* brw */
static void gen_brw(DisasContext *ctx)
{
    tcg_gen_bswap64_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rS(ctx->opcode)]);
    tcg_gen_rotli_i64(cpu_gpr[rA(ctx->opcode)], cpu_gpr[rA(ctx->opcode)], 32);

}

/* brh */
static void gen_brh(DisasContext *ctx)
{
    TCGv_i64 mask = tcg_constant_i64(0x00ff00ff00ff00ffull);
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_shri_i64(t1, cpu_gpr[rS(ctx->opcode)], 8);
    tcg_gen_and_i64(t2, t1, mask);
    tcg_gen_and_i64(t1, cpu_gpr[rS(ctx->opcode)], mask);
    tcg_gen_shli_i64(t1, t1, 8);
    tcg_gen_or_i64(cpu_gpr[rA(ctx->opcode)], t1, t2);

    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}
#endif

static opcode_t opcodes[] = {
#if defined(TARGET_PPC64)
GEN_HANDLER_E(brd, 0x1F, 0x1B, 0x05, 0x0000F801, PPC_NONE, PPC2_ISA310),
GEN_HANDLER_E(brw, 0x1F, 0x1B, 0x04, 0x0000F801, PPC_NONE, PPC2_ISA310),
GEN_HANDLER_E(brh, 0x1F, 0x1B, 0x06, 0x0000F801, PPC_NONE, PPC2_ISA310),
#endif
GEN_HANDLER(invalid, 0x00, 0x00, 0x00, 0xFFFFFFFF, PPC_NONE),
#if defined(TARGET_PPC64)
GEN_HANDLER_E(cmpeqb, 0x1F, 0x00, 0x07, 0x00600000, PPC_NONE, PPC2_ISA300),
#endif
GEN_HANDLER_E(cmpb, 0x1F, 0x1C, 0x0F, 0x00000001, PPC_NONE, PPC2_ISA205),
GEN_HANDLER_E(cmprb, 0x1F, 0x00, 0x06, 0x00400001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER(isel, 0x1F, 0x0F, 0xFF, 0x00000001, PPC_ISEL),
GEN_HANDLER(addic, 0x0C, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER2(addic_, "addic.", 0x0D, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
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
GEN_HANDLER_E(cnttzw, 0x1F, 0x1A, 0x10, 0x00000000, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(copy, 0x1F, 0x06, 0x18, 0x03C00001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(cp_abort, 0x1F, 0x06, 0x1A, 0x03FFF801, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(paste, 0x1F, 0x06, 0x1C, 0x03C00000, PPC_NONE, PPC2_ISA300),
GEN_HANDLER(or, 0x1F, 0x1C, 0x0D, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xor, 0x1F, 0x1C, 0x09, 0x00000000, PPC_INTEGER),
GEN_HANDLER(ori, 0x18, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(oris, 0x19, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xori, 0x1A, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(xoris, 0x1B, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(popcntb, 0x1F, 0x1A, 0x03, 0x0000F801, PPC_POPCNTB),
GEN_HANDLER(popcntw, 0x1F, 0x1A, 0x0b, 0x0000F801, PPC_POPCNTWD),
GEN_HANDLER_E(prtyw, 0x1F, 0x1A, 0x04, 0x0000F801, PPC_NONE, PPC2_ISA205),
#if defined(TARGET_PPC64)
GEN_HANDLER(popcntd, 0x1F, 0x1A, 0x0F, 0x0000F801, PPC_POPCNTWD),
GEN_HANDLER(cntlzd, 0x1F, 0x1A, 0x01, 0x00000000, PPC_64B),
GEN_HANDLER_E(cnttzd, 0x1F, 0x1A, 0x11, 0x00000000, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(darn, 0x1F, 0x13, 0x17, 0x001CF801, PPC_NONE, PPC2_ISA300),
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
GEN_HANDLER2_E(extswsli0, "extswsli", 0x1F, 0x1A, 0x1B, 0x00000000,
               PPC_NONE, PPC2_ISA300),
GEN_HANDLER2_E(extswsli1, "extswsli", 0x1F, 0x1B, 0x1B, 0x00000000,
               PPC_NONE, PPC2_ISA300),
#endif
/* handles lfdp, lxsd, lxssp */
GEN_HANDLER_E(dform39, 0x39, 0xFF, 0xFF, 0x00000000, PPC_NONE, PPC2_ISA205),
/* handles stfdp, stxsd, stxssp */
GEN_HANDLER_E(dform3D, 0x3D, 0xFF, 0xFF, 0x00000000, PPC_NONE, PPC2_ISA205),
GEN_HANDLER(lmw, 0x2E, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(stmw, 0x2F, 0xFF, 0xFF, 0x00000000, PPC_INTEGER),
GEN_HANDLER(lswi, 0x1F, 0x15, 0x12, 0x00000001, PPC_STRING),
GEN_HANDLER(lswx, 0x1F, 0x15, 0x10, 0x00000001, PPC_STRING),
GEN_HANDLER(stswi, 0x1F, 0x15, 0x16, 0x00000001, PPC_STRING),
GEN_HANDLER(stswx, 0x1F, 0x15, 0x14, 0x00000001, PPC_STRING),
GEN_HANDLER(eieio, 0x1F, 0x16, 0x1A, 0x01FFF801, PPC_MEM_EIEIO),
GEN_HANDLER(isync, 0x13, 0x16, 0x04, 0x03FFF801, PPC_MEM),
GEN_HANDLER_E(lbarx, 0x1F, 0x14, 0x01, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER_E(lharx, 0x1F, 0x14, 0x03, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER(lwarx, 0x1F, 0x14, 0x00, 0x00000000, PPC_RES),
GEN_HANDLER_E(lwat, 0x1F, 0x06, 0x12, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(stwat, 0x1F, 0x06, 0x16, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(stbcx_, 0x1F, 0x16, 0x15, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER_E(sthcx_, 0x1F, 0x16, 0x16, 0, PPC_NONE, PPC2_ATOMIC_ISA206),
GEN_HANDLER2(stwcx_, "stwcx.", 0x1F, 0x16, 0x04, 0x00000000, PPC_RES),
#if defined(TARGET_PPC64)
GEN_HANDLER_E(ldat, 0x1F, 0x06, 0x13, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(stdat, 0x1F, 0x06, 0x17, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER(ldarx, 0x1F, 0x14, 0x02, 0x00000000, PPC_64B),
GEN_HANDLER_E(lqarx, 0x1F, 0x14, 0x08, 0, PPC_NONE, PPC2_LSQ_ISA207),
GEN_HANDLER2(stdcx_, "stdcx.", 0x1F, 0x16, 0x06, 0x00000000, PPC_64B),
GEN_HANDLER_E(stqcx_, 0x1F, 0x16, 0x05, 0, PPC_NONE, PPC2_LSQ_ISA207),
#endif
GEN_HANDLER(sync, 0x1F, 0x16, 0x12, 0x039FF801, PPC_MEM_SYNC),
/* ISA v3.0 changed the extended opcode from 62 to 30 */
GEN_HANDLER(wait, 0x1F, 0x1E, 0x01, 0x039FF801, PPC_WAIT),
GEN_HANDLER_E(wait, 0x1F, 0x1E, 0x00, 0x039CF801, PPC_NONE, PPC2_ISA300),
GEN_HANDLER(b, 0x12, 0xFF, 0xFF, 0x00000000, PPC_FLOW),
GEN_HANDLER(bc, 0x10, 0xFF, 0xFF, 0x00000000, PPC_FLOW),
GEN_HANDLER(bcctr, 0x13, 0x10, 0x10, 0x00000000, PPC_FLOW),
GEN_HANDLER(bclr, 0x13, 0x10, 0x00, 0x00000000, PPC_FLOW),
GEN_HANDLER_E(bctar, 0x13, 0x10, 0x11, 0x0000E000, PPC_NONE, PPC2_BCTAR_ISA207),
GEN_HANDLER(mcrf, 0x13, 0x00, 0xFF, 0x00000001, PPC_INTEGER),
GEN_HANDLER(rfi, 0x13, 0x12, 0x01, 0x03FF8001, PPC_FLOW),
#if defined(TARGET_PPC64)
GEN_HANDLER(rfid, 0x13, 0x12, 0x00, 0x03FF8001, PPC_64B),
#if !defined(CONFIG_USER_ONLY)
/* Top bit of opc2 corresponds with low bit of LEV, so use two handlers */
GEN_HANDLER_E(scv, 0x11, 0x10, 0xFF, 0x03FFF01E, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(scv, 0x11, 0x00, 0xFF, 0x03FFF01E, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(rfscv, 0x13, 0x12, 0x02, 0x03FF8001, PPC_NONE, PPC2_ISA300),
#endif
GEN_HANDLER_E(stop, 0x13, 0x12, 0x0b, 0x03FFF801, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(doze, 0x13, 0x12, 0x0c, 0x03FFF801, PPC_NONE, PPC2_PM_ISA206),
GEN_HANDLER_E(nap, 0x13, 0x12, 0x0d, 0x03FFF801, PPC_NONE, PPC2_PM_ISA206),
GEN_HANDLER_E(sleep, 0x13, 0x12, 0x0e, 0x03FFF801, PPC_NONE, PPC2_PM_ISA206),
GEN_HANDLER_E(rvwinkle, 0x13, 0x12, 0x0f, 0x03FFF801, PPC_NONE, PPC2_PM_ISA206),
GEN_HANDLER(hrfid, 0x13, 0x12, 0x08, 0x03FF8001, PPC_64H),
#endif
/* Top bit of opc2 corresponds with low bit of LEV, so use two handlers */
GEN_HANDLER(sc, 0x11, 0x11, 0xFF, 0x03FFF01D, PPC_FLOW),
GEN_HANDLER(sc, 0x11, 0x01, 0xFF, 0x03FFF01D, PPC_FLOW),
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
GEN_HANDLER_E(setb, 0x1F, 0x00, 0x04, 0x0003F801, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(mcrxrx, 0x1F, 0x00, 0x12, 0x007FF801, PPC_NONE, PPC2_ISA300),
#endif
GEN_HANDLER(mtmsr, 0x1F, 0x12, 0x04, 0x001EF801, PPC_MISC),
GEN_HANDLER(mtspr, 0x1F, 0x13, 0x0E, 0x00000000, PPC_MISC),
GEN_HANDLER(dcbf, 0x1F, 0x16, 0x02, 0x03C00001, PPC_CACHE),
GEN_HANDLER_E(dcbfep, 0x1F, 0x1F, 0x03, 0x03C00001, PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER(dcbi, 0x1F, 0x16, 0x0E, 0x03E00001, PPC_CACHE),
GEN_HANDLER(dcbst, 0x1F, 0x16, 0x01, 0x03E00001, PPC_CACHE),
GEN_HANDLER_E(dcbstep, 0x1F, 0x1F, 0x01, 0x03E00001, PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER(dcbt, 0x1F, 0x16, 0x08, 0x00000001, PPC_CACHE),
GEN_HANDLER_E(dcbtep, 0x1F, 0x1F, 0x09, 0x00000001, PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER(dcbtst, 0x1F, 0x16, 0x07, 0x00000001, PPC_CACHE),
GEN_HANDLER_E(dcbtstep, 0x1F, 0x1F, 0x07, 0x00000001, PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER_E(dcbtls, 0x1F, 0x06, 0x05, 0x02000001, PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER(dcbz, 0x1F, 0x16, 0x1F, 0x03C00001, PPC_CACHE_DCBZ),
GEN_HANDLER_E(dcbzep, 0x1F, 0x1F, 0x1F, 0x03C00001, PPC_NONE, PPC2_BOOKE206),
GEN_HANDLER(dst, 0x1F, 0x16, 0x0A, 0x01800001, PPC_ALTIVEC),
GEN_HANDLER(dstst, 0x1F, 0x16, 0x0B, 0x01800001, PPC_ALTIVEC),
GEN_HANDLER(dss, 0x1F, 0x16, 0x19, 0x019FF801, PPC_ALTIVEC),
GEN_HANDLER(icbi, 0x1F, 0x16, 0x1E, 0x03E00001, PPC_CACHE_ICBI),
GEN_HANDLER_E(icbiep, 0x1F, 0x1F, 0x1E, 0x03E00001, PPC_NONE, PPC2_BOOKE206),
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
#endif
GEN_HANDLER(tlbia, 0x1F, 0x12, 0x0B, 0x03FFFC01, PPC_MEM_TLBIA),
/*
 * XXX Those instructions will need to be handled differently for
 * different ISA versions
 */
GEN_HANDLER(tlbsync, 0x1F, 0x16, 0x11, 0x03FFF801, PPC_MEM_TLBSYNC),
GEN_HANDLER(eciwx, 0x1F, 0x16, 0x0D, 0x00000001, PPC_EXTERN),
GEN_HANDLER(ecowx, 0x1F, 0x16, 0x09, 0x00000001, PPC_EXTERN),
GEN_HANDLER2(tlbld_6xx, "tlbld", 0x1F, 0x12, 0x1E, 0x03FF0001, PPC_6xx_TLB),
GEN_HANDLER2(tlbli_6xx, "tlbli", 0x1F, 0x12, 0x1F, 0x03FF0001, PPC_6xx_TLB),
GEN_HANDLER(mfapidi, 0x1F, 0x13, 0x08, 0x0000F801, PPC_MFAPIDI),
GEN_HANDLER(tlbiva, 0x1F, 0x12, 0x18, 0x03FFF801, PPC_TLBIVA),
GEN_HANDLER(mfdcr, 0x1F, 0x03, 0x0A, 0x00000001, PPC_DCR),
GEN_HANDLER(mtdcr, 0x1F, 0x03, 0x0E, 0x00000001, PPC_DCR),
GEN_HANDLER(mfdcrx, 0x1F, 0x03, 0x08, 0x00000000, PPC_DCRX),
GEN_HANDLER(mtdcrx, 0x1F, 0x03, 0x0C, 0x00000000, PPC_DCRX),
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
GEN_HANDLER(wrtee, 0x1F, 0x03, 0x04, 0x000FFC01, PPC_WRTEE),
GEN_HANDLER(wrteei, 0x1F, 0x03, 0x05, 0x000E7C01, PPC_WRTEE),
GEN_HANDLER(dlmzb, 0x1F, 0x0E, 0x02, 0x00000000, PPC_440_SPEC),
GEN_HANDLER_E(mbar, 0x1F, 0x16, 0x1a, 0x001FF801,
              PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER(msync_4xx, 0x1F, 0x16, 0x12, 0x039FF801, PPC_BOOKE),
GEN_HANDLER2_E(icbt_440, "icbt", 0x1F, 0x16, 0x00, 0x03E00001,
               PPC_BOOKE, PPC2_BOOKE206),
GEN_HANDLER2(icbt_440, "icbt", 0x1F, 0x06, 0x08, 0x03E00001,
             PPC_440_SPEC),
GEN_HANDLER(lvsl, 0x1f, 0x06, 0x00, 0x00000001, PPC_ALTIVEC),
GEN_HANDLER(lvsr, 0x1f, 0x06, 0x01, 0x00000001, PPC_ALTIVEC),
GEN_HANDLER(mfvscr, 0x04, 0x2, 0x18, 0x001ff800, PPC_ALTIVEC),
GEN_HANDLER(mtvscr, 0x04, 0x2, 0x19, 0x03ff0000, PPC_ALTIVEC),
#if defined(TARGET_PPC64)
GEN_HANDLER_E(maddhd_maddhdu, 0x04, 0x18, 0xFF, 0x00000000, PPC_NONE,
              PPC2_ISA300),
GEN_HANDLER_E(maddld, 0x04, 0x19, 0xFF, 0x00000000, PPC_NONE, PPC2_ISA300),
#endif

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
GEN_HANDLER_E(addex, 0x1F, 0x0A, 0x05, 0x00000000, PPC_NONE, PPC2_ISA300),
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
GEN_HANDLER_E(modsw, 0x1F, 0x0B, 0x18, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(moduw, 0x1F, 0x0B, 0x08, 0x00000001, PPC_NONE, PPC2_ISA300),

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
GEN_HANDLER_E(modsd, 0x1F, 0x09, 0x18, 0x00000001, PPC_NONE, PPC2_ISA300),
GEN_HANDLER_E(modud, 0x1F, 0x09, 0x08, 0x00000001, PPC_NONE, PPC2_ISA300),

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

#undef GEN_LDX_E
#define GEN_LDX_E(name, ldop, opc2, opc3, type, type2, chk)                   \
GEN_HANDLER_E(name##x, 0x1F, opc2, opc3, 0x00000001, type, type2),

#if defined(TARGET_PPC64)
GEN_LDX_E(ldbr, ld64ur_i64, 0x14, 0x10, PPC_NONE, PPC2_DBRX, CHK_NONE)

/* HV/P7 and later only */
GEN_LDX_HVRM(ldcix, ld64_i64, 0x15, 0x1b, PPC_CILDST)
GEN_LDX_HVRM(lwzcix, ld32u, 0x15, 0x18, PPC_CILDST)
GEN_LDX_HVRM(lhzcix, ld16u, 0x15, 0x19, PPC_CILDST)
GEN_LDX_HVRM(lbzcix, ld8u, 0x15, 0x1a, PPC_CILDST)
#endif
GEN_LDX(lhbr, ld16ur, 0x16, 0x18, PPC_INTEGER)
GEN_LDX(lwbr, ld32ur, 0x16, 0x10, PPC_INTEGER)

/* External PID based load */
#undef GEN_LDEPX
#define GEN_LDEPX(name, ldop, opc2, opc3)                                     \
GEN_HANDLER_E(name##epx, 0x1F, opc2, opc3,                                    \
              0x00000001, PPC_NONE, PPC2_BOOKE206),

GEN_LDEPX(lb, DEF_MEMOP(MO_UB), 0x1F, 0x02)
GEN_LDEPX(lh, DEF_MEMOP(MO_UW), 0x1F, 0x08)
GEN_LDEPX(lw, DEF_MEMOP(MO_UL), 0x1F, 0x00)
#if defined(TARGET_PPC64)
GEN_LDEPX(ld, DEF_MEMOP(MO_UQ), 0x1D, 0x00)
#endif

#undef GEN_STX_E
#define GEN_STX_E(name, stop, opc2, opc3, type, type2, chk)                   \
GEN_HANDLER_E(name##x, 0x1F, opc2, opc3, 0x00000000, type, type2),

#if defined(TARGET_PPC64)
GEN_STX_E(stdbr, st64r_i64, 0x14, 0x14, PPC_NONE, PPC2_DBRX, CHK_NONE)
GEN_STX_HVRM(stdcix, st64_i64, 0x15, 0x1f, PPC_CILDST)
GEN_STX_HVRM(stwcix, st32, 0x15, 0x1c, PPC_CILDST)
GEN_STX_HVRM(sthcix, st16, 0x15, 0x1d, PPC_CILDST)
GEN_STX_HVRM(stbcix, st8, 0x15, 0x1e, PPC_CILDST)
#endif
GEN_STX(sthbr, st16r, 0x16, 0x1C, PPC_INTEGER)
GEN_STX(stwbr, st32r, 0x16, 0x14, PPC_INTEGER)

#undef GEN_STEPX
#define GEN_STEPX(name, ldop, opc2, opc3)                                     \
GEN_HANDLER_E(name##epx, 0x1F, opc2, opc3,                                    \
              0x00000001, PPC_NONE, PPC2_BOOKE206),

GEN_STEPX(stb, DEF_MEMOP(MO_UB), 0x1F, 0x06)
GEN_STEPX(sth, DEF_MEMOP(MO_UW), 0x1F, 0x0C)
GEN_STEPX(stw, DEF_MEMOP(MO_UL), 0x1F, 0x04)
#if defined(TARGET_PPC64)
GEN_STEPX(std, DEF_MEMOP(MO_UQ), 0x1D, 0x04)
#endif

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

GEN_HANDLER2_E(tbegin, "tbegin", 0x1F, 0x0E, 0x14, 0x01DFF800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tend,   "tend",   0x1F, 0x0E, 0x15, 0x01FFF800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tabort, "tabort", 0x1F, 0x0E, 0x1C, 0x03E0F800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tabortwc, "tabortwc", 0x1F, 0x0E, 0x18, 0x00000000, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tabortwci, "tabortwci", 0x1F, 0x0E, 0x1A, 0x00000000, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tabortdc, "tabortdc", 0x1F, 0x0E, 0x19, 0x00000000, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tabortdci, "tabortdci", 0x1F, 0x0E, 0x1B, 0x00000000, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tsr, "tsr", 0x1F, 0x0E, 0x17, 0x03DFF800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(tcheck, "tcheck", 0x1F, 0x0E, 0x16, 0x007FF800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(treclaim, "treclaim", 0x1F, 0x0E, 0x1D, 0x03E0F800, \
               PPC_NONE, PPC2_TM),
GEN_HANDLER2_E(trechkpt, "trechkpt", 0x1F, 0x0E, 0x1F, 0x03FFF800, \
               PPC_NONE, PPC2_TM),

#include "translate/fp-ops.c.inc"

#include "translate/vmx-ops.c.inc"

#include "translate/vsx-ops.c.inc"

#include "translate/spe-ops.c.inc"
};

/*****************************************************************************/
/* Opcode types */
enum {
    PPC_DIRECT   = 0, /* Opcode routine        */
    PPC_INDIRECT = 1, /* Indirect opcode table */
};

#define PPC_OPCODE_MASK 0x3

static inline int is_indirect_opcode(void *handler)
{
    return ((uintptr_t)handler & PPC_OPCODE_MASK) == PPC_INDIRECT;
}

static inline opc_handler_t **ind_table(void *handler)
{
    return (opc_handler_t **)((uintptr_t)handler & ~PPC_OPCODE_MASK);
}

/* Instruction table creation */
/* Opcodes tables creation */
static void fill_new_table(opc_handler_t **table, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        table[i] = &invalid_handler;
    }
}

static int create_new_table(opc_handler_t **table, unsigned char idx)
{
    opc_handler_t **tmp;

    tmp = g_new(opc_handler_t *, PPC_CPU_INDIRECT_OPCODES_LEN);
    fill_new_table(tmp, PPC_CPU_INDIRECT_OPCODES_LEN);
    table[idx] = (opc_handler_t *)((uintptr_t)tmp | PPC_INDIRECT);

    return 0;
}

static int insert_in_table(opc_handler_t **table, unsigned char idx,
                            opc_handler_t *handler)
{
    if (table[idx] != &invalid_handler) {
        return -1;
    }
    table[idx] = handler;

    return 0;
}

static int register_direct_insn(opc_handler_t **ppc_opcodes,
                                unsigned char idx, opc_handler_t *handler)
{
    if (insert_in_table(ppc_opcodes, idx, handler) < 0) {
        printf("*** ERROR: opcode %02x already assigned in main "
               "opcode table\n", idx);
        return -1;
    }

    return 0;
}

static int register_ind_in_table(opc_handler_t **table,
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

static int register_ind_insn(opc_handler_t **ppc_opcodes,
                             unsigned char idx1, unsigned char idx2,
                             opc_handler_t *handler)
{
    return register_ind_in_table(ppc_opcodes, idx1, idx2, handler);
}

static int register_dblind_insn(opc_handler_t **ppc_opcodes,
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

static int register_trplind_insn(opc_handler_t **ppc_opcodes,
                                 unsigned char idx1, unsigned char idx2,
                                 unsigned char idx3, unsigned char idx4,
                                 opc_handler_t *handler)
{
    opc_handler_t **table;

    if (register_ind_in_table(ppc_opcodes, idx1, idx2, NULL) < 0) {
        printf("*** ERROR: unable to join indirect table idx "
               "[%02x-%02x]\n", idx1, idx2);
        return -1;
    }
    table = ind_table(ppc_opcodes[idx1]);
    if (register_ind_in_table(table, idx2, idx3, NULL) < 0) {
        printf("*** ERROR: unable to join 2nd-level indirect table idx "
               "[%02x-%02x-%02x]\n", idx1, idx2, idx3);
        return -1;
    }
    table = ind_table(table[idx2]);
    if (register_ind_in_table(table, idx3, idx4, handler) < 0) {
        printf("*** ERROR: unable to insert opcode "
               "[%02x-%02x-%02x-%02x]\n", idx1, idx2, idx3, idx4);
        return -1;
    }
    return 0;
}
static int register_insn(opc_handler_t **ppc_opcodes, opcode_t *insn)
{
    if (insn->opc2 != 0xFF) {
        if (insn->opc3 != 0xFF) {
            if (insn->opc4 != 0xFF) {
                if (register_trplind_insn(ppc_opcodes, insn->opc1, insn->opc2,
                                          insn->opc3, insn->opc4,
                                          &insn->handler) < 0) {
                    return -1;
                }
            } else {
                if (register_dblind_insn(ppc_opcodes, insn->opc1, insn->opc2,
                                         insn->opc3, &insn->handler) < 0) {
                    return -1;
                }
            }
        } else {
            if (register_ind_insn(ppc_opcodes, insn->opc1,
                                  insn->opc2, &insn->handler) < 0) {
                return -1;
            }
        }
    } else {
        if (register_direct_insn(ppc_opcodes, insn->opc1, &insn->handler) < 0) {
            return -1;
        }
    }

    return 0;
}

static int test_opcode_table(opc_handler_t **table, int len)
{
    int i, count, tmp;

    for (i = 0, count = 0; i < len; i++) {
        /* Consistency fixup */
        if (table[i] == NULL) {
            table[i] = &invalid_handler;
        }
        if (table[i] != &invalid_handler) {
            if (is_indirect_opcode(table[i])) {
                tmp = test_opcode_table(ind_table(table[i]),
                    PPC_CPU_INDIRECT_OPCODES_LEN);
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

static void fix_opcode_tables(opc_handler_t **ppc_opcodes)
{
    if (test_opcode_table(ppc_opcodes, PPC_CPU_OPCODES_LEN) == 0) {
        printf("*** WARNING: no opcode defined !\n");
    }
}

/*****************************************************************************/
void create_ppc_opcodes(PowerPCCPU *cpu, Error **errp)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    opcode_t *opc;

    fill_new_table(cpu->opcodes, PPC_CPU_OPCODES_LEN);
    for (opc = opcodes; opc < &opcodes[ARRAY_SIZE(opcodes)]; opc++) {
        if (((opc->handler.type & pcc->insns_flags) != 0) ||
            ((opc->handler.type2 & pcc->insns_flags2) != 0)) {
            if (register_insn(cpu->opcodes, opc) < 0) {
                error_setg(errp, "ERROR initializing PowerPC instruction "
                           "0x%02x 0x%02x 0x%02x", opc->opc1, opc->opc2,
                           opc->opc3);
                return;
            }
        }
    }
    fix_opcode_tables(cpu->opcodes);
    fflush(stdout);
    fflush(stderr);
}

void destroy_ppc_opcodes(PowerPCCPU *cpu)
{
    opc_handler_t **table, **table_2;
    int i, j, k;

    for (i = 0; i < PPC_CPU_OPCODES_LEN; i++) {
        if (cpu->opcodes[i] == &invalid_handler) {
            continue;
        }
        if (is_indirect_opcode(cpu->opcodes[i])) {
            table = ind_table(cpu->opcodes[i]);
            for (j = 0; j < PPC_CPU_INDIRECT_OPCODES_LEN; j++) {
                if (table[j] == &invalid_handler) {
                    continue;
                }
                if (is_indirect_opcode(table[j])) {
                    table_2 = ind_table(table[j]);
                    for (k = 0; k < PPC_CPU_INDIRECT_OPCODES_LEN; k++) {
                        if (table_2[k] != &invalid_handler &&
                            is_indirect_opcode(table_2[k])) {
                            g_free((opc_handler_t *)((uintptr_t)table_2[k] &
                                                     ~PPC_INDIRECT));
                        }
                    }
                    g_free((opc_handler_t *)((uintptr_t)table[j] &
                                             ~PPC_INDIRECT));
                }
            }
            g_free((opc_handler_t *)((uintptr_t)cpu->opcodes[i] &
                ~PPC_INDIRECT));
        }
    }
}

int ppc_fixup_cpu(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    /*
     * TCG doesn't (yet) emulate some groups of instructions that are
     * implemented on some otherwise supported CPUs (e.g. VSX and
     * decimal floating point instructions on POWER7).  We remove
     * unsupported instruction groups from the cpu state's instruction
     * masks and hope the guest can cope.  For at least the pseries
     * machine, the unavailability of these instructions can be
     * advertised to the guest via the device tree.
     */
    if ((env->insns_flags & ~PPC_TCG_INSNS)
        || (env->insns_flags2 & ~PPC_TCG_INSNS2)) {
        warn_report("Disabling some instructions which are not "
                    "emulated by TCG (0x%" PRIx64 ", 0x%" PRIx64 ")",
                    env->insns_flags & ~PPC_TCG_INSNS,
                    env->insns_flags2 & ~PPC_TCG_INSNS2);
    }
    env->insns_flags &= PPC_TCG_INSNS;
    env->insns_flags2 &= PPC_TCG_INSNS2;
    return 0;
}

static bool decode_legacy(PowerPCCPU *cpu, DisasContext *ctx, uint32_t insn)
{
    opc_handler_t **table, *handler;
    uint32_t inval;

    ctx->opcode = insn;

    LOG_DISAS("translate opcode %08x (%02x %02x %02x %02x) (%s)\n",
              insn, opc1(insn), opc2(insn), opc3(insn), opc4(insn),
              ctx->le_mode ? "little" : "big");

    table = cpu->opcodes;
    handler = table[opc1(insn)];
    if (is_indirect_opcode(handler)) {
        table = ind_table(handler);
        handler = table[opc2(insn)];
        if (is_indirect_opcode(handler)) {
            table = ind_table(handler);
            handler = table[opc3(insn)];
            if (is_indirect_opcode(handler)) {
                table = ind_table(handler);
                handler = table[opc4(insn)];
            }
        }
    }

    /* Is opcode *REALLY* valid ? */
    if (unlikely(handler->handler == &gen_invalid)) {
        qemu_log_mask(LOG_GUEST_ERROR, "invalid/unsupported opcode: "
                      "%02x - %02x - %02x - %02x (%08x) "
                      TARGET_FMT_lx "\n",
                      opc1(insn), opc2(insn), opc3(insn), opc4(insn),
                      insn, ctx->cia);
        return false;
    }

    if (unlikely(handler->type & (PPC_SPE | PPC_SPE_SINGLE | PPC_SPE_DOUBLE)
                 && Rc(insn))) {
        inval = handler->inval2;
    } else {
        inval = handler->inval1;
    }

    if (unlikely((insn & inval) != 0)) {
        qemu_log_mask(LOG_GUEST_ERROR, "invalid bits: %08x for opcode: "
                      "%02x - %02x - %02x - %02x (%08x) "
                      TARGET_FMT_lx "\n", insn & inval,
                      opc1(insn), opc2(insn), opc3(insn), opc4(insn),
                      insn, ctx->cia);
        return false;
    }

    handler->handler(ctx);
    return true;
}

static void ppc_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUPPCState *env = cs->env_ptr;
    uint32_t hflags = ctx->base.tb->flags;

    ctx->spr_cb = env->spr_cb;
    ctx->pr = (hflags >> HFLAGS_PR) & 1;
    ctx->mem_idx = (hflags >> HFLAGS_DMMU_IDX) & 7;
    ctx->dr = (hflags >> HFLAGS_DR) & 1;
    ctx->hv = (hflags >> HFLAGS_HV) & 1;
    ctx->insns_flags = env->insns_flags;
    ctx->insns_flags2 = env->insns_flags2;
    ctx->access_type = -1;
    ctx->need_access_type = !mmu_is_64bit(env->mmu_model);
    ctx->le_mode = (hflags >> HFLAGS_LE) & 1;
    ctx->default_tcg_memop_mask = ctx->le_mode ? MO_LE : MO_BE;
    ctx->flags = env->flags;
#if defined(TARGET_PPC64)
    ctx->sf_mode = (hflags >> HFLAGS_64) & 1;
    ctx->has_cfar = !!(env->flags & POWERPC_FLAG_CFAR);
#endif
    ctx->lazy_tlb_flush = env->mmu_model == POWERPC_MMU_32B
        || env->mmu_model & POWERPC_MMU_64;

    ctx->fpu_enabled = (hflags >> HFLAGS_FP) & 1;
    ctx->spe_enabled = (hflags >> HFLAGS_SPE) & 1;
    ctx->altivec_enabled = (hflags >> HFLAGS_VR) & 1;
    ctx->vsx_enabled = (hflags >> HFLAGS_VSX) & 1;
    ctx->tm_enabled = (hflags >> HFLAGS_TM) & 1;
    ctx->gtse = (hflags >> HFLAGS_GTSE) & 1;
    ctx->hr = (hflags >> HFLAGS_HR) & 1;
    ctx->mmcr0_pmcc0 = (hflags >> HFLAGS_PMCC0) & 1;
    ctx->mmcr0_pmcc1 = (hflags >> HFLAGS_PMCC1) & 1;
    ctx->mmcr0_pmcjce = (hflags >> HFLAGS_PMCJCE) & 1;
    ctx->pmc_other = (hflags >> HFLAGS_PMC_OTHER) & 1;
    ctx->pmu_insn_cnt = (hflags >> HFLAGS_INSN_CNT) & 1;

    ctx->singlestep_enabled = 0;
    if ((hflags >> HFLAGS_SE) & 1) {
        ctx->singlestep_enabled |= CPU_SINGLE_STEP;
        ctx->base.max_insns = 1;
    }
    if ((hflags >> HFLAGS_BE) & 1) {
        ctx->singlestep_enabled |= CPU_BRANCH_STEP;
    }
}

static void ppc_tr_tb_start(DisasContextBase *db, CPUState *cs)
{
}

static void ppc_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    tcg_gen_insn_start(dcbase->pc_next);
}

static bool is_prefix_insn(DisasContext *ctx, uint32_t insn)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    return opc1(insn) == 1;
}

static void ppc_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = cs->env_ptr;
    target_ulong pc;
    uint32_t insn;
    bool ok;

    LOG_DISAS("----------------\n");
    LOG_DISAS("nip=" TARGET_FMT_lx " super=%d ir=%d\n",
              ctx->base.pc_next, ctx->mem_idx, (int)msr_ir);

    ctx->cia = pc = ctx->base.pc_next;
    insn = translator_ldl_swap(env, dcbase, pc, need_byteswap(ctx));
    ctx->base.pc_next = pc += 4;

    if (!is_prefix_insn(ctx, insn)) {
        ok = (decode_insn32(ctx, insn) ||
              decode_legacy(cpu, ctx, insn));
    } else if ((pc & 63) == 0) {
        /*
         * Power v3.1, section 1.9 Exceptions:
         * attempt to execute a prefixed instruction that crosses a
         * 64-byte address boundary (system alignment error).
         */
        gen_exception_err(ctx, POWERPC_EXCP_ALIGN, POWERPC_EXCP_ALIGN_INSN);
        ok = true;
    } else {
        uint32_t insn2 = translator_ldl_swap(env, dcbase, pc,
                                             need_byteswap(ctx));
        ctx->base.pc_next = pc += 4;
        ok = decode_insn64(ctx, deposit64(insn2, 32, 32, insn));
    }
    if (!ok) {
        gen_invalid(ctx);
    }

    /* End the TB when crossing a page boundary. */
    if (ctx->base.is_jmp == DISAS_NEXT && !(pc & ~TARGET_PAGE_MASK)) {
        ctx->base.is_jmp = DISAS_TOO_MANY;
    }

    translator_loop_temp_check(&ctx->base);
}

static void ppc_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    DisasJumpType is_jmp = ctx->base.is_jmp;
    target_ulong nip = ctx->base.pc_next;

    if (is_jmp == DISAS_NORETURN) {
        /* We have already exited the TB. */
        return;
    }

    /* Honor single stepping. */
    if (unlikely(ctx->singlestep_enabled & CPU_SINGLE_STEP)
        && (nip <= 0x100 || nip > 0xf00)) {
        switch (is_jmp) {
        case DISAS_TOO_MANY:
        case DISAS_EXIT_UPDATE:
        case DISAS_CHAIN_UPDATE:
            gen_update_nip(ctx, nip);
            break;
        case DISAS_EXIT:
        case DISAS_CHAIN:
            break;
        default:
            g_assert_not_reached();
        }

        gen_debug_exception(ctx);
        return;
    }

    switch (is_jmp) {
    case DISAS_TOO_MANY:
        if (use_goto_tb(ctx, nip)) {
            pmu_count_insns(ctx);
            tcg_gen_goto_tb(0);
            gen_update_nip(ctx, nip);
            tcg_gen_exit_tb(ctx->base.tb, 0);
            break;
        }
        /* fall through */
    case DISAS_CHAIN_UPDATE:
        gen_update_nip(ctx, nip);
        /* fall through */
    case DISAS_CHAIN:
        /*
         * tcg_gen_lookup_and_goto_ptr will exit the TB if
         * CF_NO_GOTO_PTR is set. Count insns now.
         */
        if (ctx->base.tb->flags & CF_NO_GOTO_PTR) {
            pmu_count_insns(ctx);
        }

        tcg_gen_lookup_and_goto_ptr();
        break;

    case DISAS_EXIT_UPDATE:
        gen_update_nip(ctx, nip);
        /* fall through */
    case DISAS_EXIT:
        pmu_count_insns(ctx);
        tcg_gen_exit_tb(NULL, 0);
        break;

    default:
        g_assert_not_reached();
    }
}

static void ppc_tr_disas_log(const DisasContextBase *dcbase,
                             CPUState *cs, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cs, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps ppc_tr_ops = {
    .init_disas_context = ppc_tr_init_disas_context,
    .tb_start           = ppc_tr_tb_start,
    .insn_start         = ppc_tr_insn_start,
    .translate_insn     = ppc_tr_translate_insn,
    .tb_stop            = ppc_tr_tb_stop,
    .disas_log          = ppc_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc, &ppc_tr_ops, &ctx.base);
}
