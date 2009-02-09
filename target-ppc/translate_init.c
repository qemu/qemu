/*
 *  PowerPC CPU initialization for qemu.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

/* A lot of PowerPC definition have been included here.
 * Most of them are not usable for now but have been kept
 * inside "#if defined(TODO) ... #endif" statements to make tests easier.
 */

#include "dis-asm.h"
#include "host-utils.h"
#include "gdbstub.h"

//#define PPC_DUMP_CPU
//#define PPC_DEBUG_SPR
//#define PPC_DUMP_SPR_ACCESSES
#if defined(CONFIG_USER_ONLY)
#define TODO_USER_ONLY 1
#endif

struct ppc_def_t {
    const char *name;
    uint32_t pvr;
    uint32_t svr;
    uint64_t insns_flags;
    uint64_t msr_mask;
    powerpc_mmu_t   mmu_model;
    powerpc_excp_t  excp_model;
    powerpc_input_t bus_model;
    uint32_t flags;
    int bfd_mach;
    void (*init_proc)(CPUPPCState *env);
    int  (*check_pow)(CPUPPCState *env);
};

/* For user-mode emulation, we don't emulate any IRQ controller */
#if defined(CONFIG_USER_ONLY)
#define PPC_IRQ_INIT_FN(name)                                                 \
static inline void glue(glue(ppc, name),_irq_init) (CPUPPCState *env)         \
{                                                                             \
}
#else
#define PPC_IRQ_INIT_FN(name)                                                 \
void glue(glue(ppc, name),_irq_init) (CPUPPCState *env);
#endif

PPC_IRQ_INIT_FN(40x);
PPC_IRQ_INIT_FN(6xx);
PPC_IRQ_INIT_FN(970);

/* Generic callbacks:
 * do nothing but store/retrieve spr value
 */
static void spr_read_generic (void *opaque, int gprn, int sprn)
{
    gen_load_spr(cpu_gpr[gprn], sprn);
#ifdef PPC_DUMP_SPR_ACCESSES
    {
        TCGv t0 = tcg_const_i32(sprn);
        gen_helper_load_dump_spr(t0);
        tcg_temp_free_i32(t0);
    }
#endif
}

static void spr_write_generic (void *opaque, int sprn, int gprn)
{
    gen_store_spr(sprn, cpu_gpr[gprn]);
#ifdef PPC_DUMP_SPR_ACCESSES
    {
        TCGv t0 = tcg_const_i32(sprn);
        gen_helper_store_dump_spr(t0);
        tcg_temp_free_i32(t0);
    }
#endif
}

#if !defined(CONFIG_USER_ONLY)
static void spr_write_clear (void *opaque, int sprn, int gprn)
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
#endif

/* SPR common to all PowerPC */
/* XER */
static void spr_read_xer (void *opaque, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_xer);
}

static void spr_write_xer (void *opaque, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_xer, cpu_gpr[gprn]);
}

/* LR */
static void spr_read_lr (void *opaque, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_lr);
}

static void spr_write_lr (void *opaque, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_lr, cpu_gpr[gprn]);
}

/* CTR */
static void spr_read_ctr (void *opaque, int gprn, int sprn)
{
    tcg_gen_mov_tl(cpu_gpr[gprn], cpu_ctr);
}

static void spr_write_ctr (void *opaque, int sprn, int gprn)
{
    tcg_gen_mov_tl(cpu_ctr, cpu_gpr[gprn]);
}

/* User read access to SPR */
/* USPRx */
/* UMMCRx */
/* UPMCx */
/* USIA */
/* UDECR */
static void spr_read_ureg (void *opaque, int gprn, int sprn)
{
    gen_load_spr(cpu_gpr[gprn], sprn + 0x10);
}

/* SPR common to all non-embedded PowerPC */
/* DECR */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_decr (void *opaque, int gprn, int sprn)
{
    gen_helper_load_decr(cpu_gpr[gprn]);
}

static void spr_write_decr (void *opaque, int sprn, int gprn)
{
    gen_helper_store_decr(cpu_gpr[gprn]);
}
#endif

/* SPR common to all non-embedded PowerPC, except 601 */
/* Time base */
static void spr_read_tbl (void *opaque, int gprn, int sprn)
{
    gen_helper_load_tbl(cpu_gpr[gprn]);
}

static void spr_read_tbu (void *opaque, int gprn, int sprn)
{
    gen_helper_load_tbu(cpu_gpr[gprn]);
}

__attribute__ (( unused ))
static void spr_read_atbl (void *opaque, int gprn, int sprn)
{
    gen_helper_load_atbl(cpu_gpr[gprn]);
}

__attribute__ (( unused ))
static void spr_read_atbu (void *opaque, int gprn, int sprn)
{
    gen_helper_load_atbu(cpu_gpr[gprn]);
}

#if !defined(CONFIG_USER_ONLY)
static void spr_write_tbl (void *opaque, int sprn, int gprn)
{
    gen_helper_store_tbl(cpu_gpr[gprn]);
}

static void spr_write_tbu (void *opaque, int sprn, int gprn)
{
    gen_helper_store_tbu(cpu_gpr[gprn]);
}

__attribute__ (( unused ))
static void spr_write_atbl (void *opaque, int sprn, int gprn)
{
    gen_helper_store_atbl(cpu_gpr[gprn]);
}

__attribute__ (( unused ))
static void spr_write_atbu (void *opaque, int sprn, int gprn)
{
    gen_helper_store_atbu(cpu_gpr[gprn]);
}
#endif

#if !defined(CONFIG_USER_ONLY)
/* IBAT0U...IBAT0U */
/* IBAT0L...IBAT7L */
static void spr_read_ibat (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, IBAT[sprn & 1][(sprn - SPR_IBAT0U) / 2]));
}

static void spr_read_ibat_h (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, IBAT[sprn & 1][(sprn - SPR_IBAT4U) / 2]));
}

static void spr_write_ibatu (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0U) / 2);
    gen_helper_store_ibatu(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_ibatu_h (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT4U) / 2);
    gen_helper_store_ibatu(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_ibatl (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0L) / 2);
    gen_helper_store_ibatl(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_ibatl_h (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT4L) / 2);
    gen_helper_store_ibatl(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

/* DBAT0U...DBAT7U */
/* DBAT0L...DBAT7L */
static void spr_read_dbat (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, DBAT[sprn & 1][(sprn - SPR_DBAT0U) / 2]));
}

static void spr_read_dbat_h (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, DBAT[sprn & 1][((sprn - SPR_DBAT4U) / 2) + 4]));
}

static void spr_write_dbatu (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_DBAT0U) / 2);
    gen_helper_store_dbatu(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_dbatu_h (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_DBAT4U) / 2) + 4);
    gen_helper_store_dbatu(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_dbatl (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_DBAT0L) / 2);
    gen_helper_store_dbatl(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_dbatl_h (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(((sprn - SPR_DBAT4L) / 2) + 4);
    gen_helper_store_dbatl(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

/* SDR1 */
static void spr_read_sdr1 (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, sdr1));
}

static void spr_write_sdr1 (void *opaque, int sprn, int gprn)
{
    gen_helper_store_sdr1(cpu_gpr[gprn]);
}

/* 64 bits PowerPC specific SPRs */
/* ASR */
#if defined(TARGET_PPC64)
static void spr_read_asr (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, asr));
}

static void spr_write_asr (void *opaque, int sprn, int gprn)
{
    gen_helper_store_asr(cpu_gpr[gprn]);
}
#endif
#endif

/* PowerPC 601 specific registers */
/* RTC */
static void spr_read_601_rtcl (void *opaque, int gprn, int sprn)
{
    gen_helper_load_601_rtcl(cpu_gpr[gprn]);
}

static void spr_read_601_rtcu (void *opaque, int gprn, int sprn)
{
    gen_helper_load_601_rtcu(cpu_gpr[gprn]);
}

#if !defined(CONFIG_USER_ONLY)
static void spr_write_601_rtcu (void *opaque, int sprn, int gprn)
{
    gen_helper_store_601_rtcu(cpu_gpr[gprn]);
}

static void spr_write_601_rtcl (void *opaque, int sprn, int gprn)
{
    gen_helper_store_601_rtcl(cpu_gpr[gprn]);
}

static void spr_write_hid0_601 (void *opaque, int sprn, int gprn)
{
    DisasContext *ctx = opaque;

    gen_helper_store_hid0_601(cpu_gpr[gprn]);
    /* Must stop the translation as endianness may have changed */
    gen_stop_exception(ctx);
}
#endif

/* Unified bats */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_601_ubat (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, IBAT[sprn & 1][(sprn - SPR_IBAT0U) / 2]));
}

static void spr_write_601_ubatu (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0U) / 2);
    gen_helper_store_601_batl(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_601_ubatl (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32((sprn - SPR_IBAT0U) / 2);
    gen_helper_store_601_batu(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}
#endif

/* PowerPC 40x specific registers */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_40x_pit (void *opaque, int gprn, int sprn)
{
    gen_helper_load_40x_pit(cpu_gpr[gprn]);
}

static void spr_write_40x_pit (void *opaque, int sprn, int gprn)
{
    gen_helper_store_40x_pit(cpu_gpr[gprn]);
}

static void spr_write_40x_dbcr0 (void *opaque, int sprn, int gprn)
{
    DisasContext *ctx = opaque;

    gen_helper_store_40x_dbcr0(cpu_gpr[gprn]);
    /* We must stop translation as we may have rebooted */
    gen_stop_exception(ctx);
}

static void spr_write_40x_sler (void *opaque, int sprn, int gprn)
{
    gen_helper_store_40x_sler(cpu_gpr[gprn]);
}

static void spr_write_booke_tcr (void *opaque, int sprn, int gprn)
{
    gen_helper_store_booke_tcr(cpu_gpr[gprn]);
}

static void spr_write_booke_tsr (void *opaque, int sprn, int gprn)
{
    gen_helper_store_booke_tsr(cpu_gpr[gprn]);
}
#endif

/* PowerPC 403 specific registers */
/* PBL1 / PBU1 / PBL2 / PBU2 */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_403_pbr (void *opaque, int gprn, int sprn)
{
    tcg_gen_ld_tl(cpu_gpr[gprn], cpu_env, offsetof(CPUState, pb[sprn - SPR_403_PBL1]));
}

static void spr_write_403_pbr (void *opaque, int sprn, int gprn)
{
    TCGv_i32 t0 = tcg_const_i32(sprn - SPR_403_PBL1);
    gen_helper_store_403_pbr(t0, cpu_gpr[gprn]);
    tcg_temp_free_i32(t0);
}

static void spr_write_pir (void *opaque, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], 0xF);
    gen_store_spr(SPR_PIR, t0);
    tcg_temp_free(t0);
}
#endif

#if !defined(CONFIG_USER_ONLY)
/* Callback used to write the exception vector base */
static void spr_write_excp_prefix (void *opaque, int sprn, int gprn)
{
    TCGv t0 = tcg_temp_new();
    tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, ivpr_mask));
    tcg_gen_and_tl(t0, t0, cpu_gpr[gprn]);
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, excp_prefix));
    gen_store_spr(sprn, t0);
}

static void spr_write_excp_vector (void *opaque, int sprn, int gprn)
{
    DisasContext *ctx = opaque;

    if (sprn >= SPR_BOOKE_IVOR0 && sprn <= SPR_BOOKE_IVOR15) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, ivor_mask));
        tcg_gen_and_tl(t0, t0, cpu_gpr[gprn]);
        tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, excp_vectors[sprn - SPR_BOOKE_IVOR0]));
        gen_store_spr(sprn, t0);
        tcg_temp_free(t0);
    } else if (sprn >= SPR_BOOKE_IVOR32 && sprn <= SPR_BOOKE_IVOR37) {
        TCGv t0 = tcg_temp_new();
        tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, ivor_mask));
        tcg_gen_and_tl(t0, t0, cpu_gpr[gprn]);
        tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, excp_vectors[sprn - SPR_BOOKE_IVOR32 + 32]));
        gen_store_spr(sprn, t0);
        tcg_temp_free(t0);
    } else {
        printf("Trying to write an unknown exception vector %d %03x\n",
               sprn, sprn);
        gen_inval_exception(ctx, POWERPC_EXCP_PRIV_REG);
    }
}
#endif

static inline void vscr_init (CPUPPCState *env, uint32_t val)
{
    env->vscr = val;
    /* Altivec always uses round-to-nearest */
    set_float_rounding_mode(float_round_nearest_even, &env->vec_status);
    set_flush_to_zero(vscr_nj, &env->vec_status);
}

#if defined(CONFIG_USER_ONLY)
#define spr_register(env, num, name, uea_read, uea_write,                     \
                     oea_read, oea_write, initial_value)                      \
do {                                                                          \
     _spr_register(env, num, name, uea_read, uea_write, initial_value);       \
} while (0)
static inline void _spr_register (CPUPPCState *env, int num,
                                  const char *name,
                                  void (*uea_read)(void *opaque, int gprn, int sprn),
                                  void (*uea_write)(void *opaque, int sprn, int gprn),
                                  target_ulong initial_value)
#else
static inline void spr_register (CPUPPCState *env, int num,
                                 const char *name,
                                 void (*uea_read)(void *opaque, int gprn, int sprn),
                                 void (*uea_write)(void *opaque, int sprn, int gprn),
                                 void (*oea_read)(void *opaque, int gprn, int sprn),
                                 void (*oea_write)(void *opaque, int sprn, int gprn),
                                 target_ulong initial_value)
#endif
{
    ppc_spr_t *spr;

    spr = &env->spr_cb[num];
    if (spr->name != NULL ||env-> spr[num] != 0x00000000 ||
#if !defined(CONFIG_USER_ONLY)
        spr->oea_read != NULL || spr->oea_write != NULL ||
#endif
        spr->uea_read != NULL || spr->uea_write != NULL) {
        printf("Error: Trying to register SPR %d (%03x) twice !\n", num, num);
        exit(1);
    }
#if defined(PPC_DEBUG_SPR)
    printf("*** register spr %d (%03x) %s val " ADDRX "\n", num, num, name,
           initial_value);
#endif
    spr->name = name;
    spr->uea_read = uea_read;
    spr->uea_write = uea_write;
#if !defined(CONFIG_USER_ONLY)
    spr->oea_read = oea_read;
    spr->oea_write = oea_write;
#endif
    env->spr[num] = initial_value;
}

/* Generic PowerPC SPRs */
static void gen_spr_generic (CPUPPCState *env)
{
    /* Integer processing */
    spr_register(env, SPR_XER, "XER",
                 &spr_read_xer, &spr_write_xer,
                 &spr_read_xer, &spr_write_xer,
                 0x00000000);
    /* Branch contol */
    spr_register(env, SPR_LR, "LR",
                 &spr_read_lr, &spr_write_lr,
                 &spr_read_lr, &spr_write_lr,
                 0x00000000);
    spr_register(env, SPR_CTR, "CTR",
                 &spr_read_ctr, &spr_write_ctr,
                 &spr_read_ctr, &spr_write_ctr,
                 0x00000000);
    /* Interrupt processing */
    spr_register(env, SPR_SRR0, "SRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SRR1, "SRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Processor control */
    spr_register(env, SPR_SPRG0, "SPRG0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG1, "SPRG1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG2, "SPRG2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG3, "SPRG3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR common to all non-embedded PowerPC, including 601 */
static void gen_spr_ne_601 (CPUPPCState *env)
{
    /* Exception processing */
    spr_register(env, SPR_DSISR, "DSISR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_DAR, "DAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);
    /* Memory management */
    spr_register(env, SPR_SDR1, "SDR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_sdr1, &spr_write_sdr1,
                 0x00000000);
}

/* BATs 0-3 */
static void gen_low_BATs (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_IBAT0U, "IBAT0U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT0L, "IBAT0L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT1U, "IBAT1U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT1L, "IBAT1L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT2U, "IBAT2U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT2L, "IBAT2L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT3U, "IBAT3U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT3L, "IBAT3L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_DBAT0U, "DBAT0U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT0L, "DBAT0L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT1U, "DBAT1U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT1L, "DBAT1L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT2U, "DBAT2U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT2L, "DBAT2L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT3U, "DBAT3U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT3L, "DBAT3L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    env->nb_BATs += 4;
#endif
}

/* BATs 4-7 */
static void gen_high_BATs (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_IBAT4U, "IBAT4U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT4L, "IBAT4L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT5U, "IBAT5U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT5L, "IBAT5L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT6U, "IBAT6U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT6L, "IBAT6L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT7U, "IBAT7U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT7L, "IBAT7L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT4U, "DBAT4U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT4L, "DBAT4L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT5U, "DBAT5U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT5L, "DBAT5L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT6U, "DBAT6U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT6L, "DBAT6L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT7U, "DBAT7U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT7L, "DBAT7L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    env->nb_BATs += 4;
#endif
}

/* Generic PowerPC time base */
static void gen_tbl (CPUPPCState *env)
{
    spr_register(env, SPR_VTBL,  "TBL",
                 &spr_read_tbl, SPR_NOACCESS,
                 &spr_read_tbl, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_TBL,   "TBL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_tbl,
                 0x00000000);
    spr_register(env, SPR_VTBU,  "TBU",
                 &spr_read_tbu, SPR_NOACCESS,
                 &spr_read_tbu, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_TBU,   "TBU",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_tbu,
                 0x00000000);
}

/* Softare table search registers */
static void gen_6xx_7xx_soft_tlb (CPUPPCState *env, int nb_tlbs, int nb_ways)
{
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = nb_tlbs;
    env->nb_ways = nb_ways;
    env->id_tlbs = 1;
    spr_register(env, SPR_DMISS, "DMISS",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_DCMP, "DCMP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_HASH1, "HASH1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_HASH2, "HASH2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_IMISS, "IMISS",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_ICMP, "ICMP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_RPA, "RPA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#endif
}

/* SPR common to MPC755 and G2 */
static void gen_spr_G2_755 (CPUPPCState *env)
{
    /* SGPRs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR common to all 7xx PowerPC implementations */
static void gen_spr_7xx (CPUPPCState *env)
{
    /* Breakpoints */
    /* XXX : not implemented */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Cache management */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTC, "ICTC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Performance monitors */
    /* XXX : not implemented */
    spr_register(env, SPR_MMCR0, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MMCR1, "MMCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC1, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC2, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC3, "PMC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC4, "PMC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UMMCR0, "UMMCR0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UMMCR1, "UMMCR1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC1, "UPMC1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC2, "UPMC2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC3, "UPMC3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC4, "UPMC4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_USIAR, "USIAR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_spr_thrm (CPUPPCState *env)
{
    /* Thermal management */
    /* XXX : not implemented */
    spr_register(env, SPR_THRM1, "THRM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_THRM2, "THRM2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_THRM3, "THRM3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 604 implementation */
static void gen_spr_604 (CPUPPCState *env)
{
    /* Processor identification */
    spr_register(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* Breakpoints */
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Performance counters */
    /* XXX : not implemented */
    spr_register(env, SPR_MMCR0, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC1, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC2, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SDA, "SDA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 603 implementation */
static void gen_spr_603 (CPUPPCState *env)
{
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC G2 implementation */
static void gen_spr_G2 (CPUPPCState *env)
{
    /* Memory base address */
    /* MBAR */
    /* XXX : not implemented */
    spr_register(env, SPR_MBAR, "MBAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Exception processing */
    spr_register(env, SPR_BOOKE_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_CSRR1, "CSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Breakpoints */
    /* XXX : not implemented */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DABR2, "DABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IABR2, "IABR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IBCR, "IBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DBCR, "DBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 602 implementation */
static void gen_spr_602 (CPUPPCState *env)
{
    /* ESA registers */
    /* XXX : not implemented */
    spr_register(env, SPR_SER, "SER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SEBR, "SEBR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_ESASRR, "ESASRR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Floating point status */
    /* XXX : not implemented */
    spr_register(env, SPR_SP, "SP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_LT, "LT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Watchdog timer */
    /* XXX : not implemented */
    spr_register(env, SPR_TCR, "TCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Interrupt base */
    spr_register(env, SPR_IBR, "IBR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 601 implementation */
static void gen_spr_601 (CPUPPCState *env)
{
    /* Multiplication/division register */
    /* MQ */
    spr_register(env, SPR_MQ, "MQ",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* RTC registers */
    spr_register(env, SPR_601_RTCU, "RTCU",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_601_rtcu,
                 0x00000000);
    spr_register(env, SPR_601_VRTCU, "RTCU",
                 &spr_read_601_rtcu, SPR_NOACCESS,
                 &spr_read_601_rtcu, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_601_RTCL, "RTCL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_601_rtcl,
                 0x00000000);
    spr_register(env, SPR_601_VRTCL, "RTCL",
                 &spr_read_601_rtcl, SPR_NOACCESS,
                 &spr_read_601_rtcl, SPR_NOACCESS,
                 0x00000000);
    /* Timer */
#if 0 /* ? */
    spr_register(env, SPR_601_UDECR, "UDECR",
                 &spr_read_decr, SPR_NOACCESS,
                 &spr_read_decr, SPR_NOACCESS,
                 0x00000000);
#endif
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_IBAT0U, "IBAT0U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatu,
                 0x00000000);
    spr_register(env, SPR_IBAT0L, "IBAT0L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatl,
                 0x00000000);
    spr_register(env, SPR_IBAT1U, "IBAT1U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatu,
                 0x00000000);
    spr_register(env, SPR_IBAT1L, "IBAT1L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatl,
                 0x00000000);
    spr_register(env, SPR_IBAT2U, "IBAT2U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatu,
                 0x00000000);
    spr_register(env, SPR_IBAT2L, "IBAT2L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatl,
                 0x00000000);
    spr_register(env, SPR_IBAT3U, "IBAT3U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatu,
                 0x00000000);
    spr_register(env, SPR_IBAT3L, "IBAT3L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_601_ubat, &spr_write_601_ubatl,
                 0x00000000);
    env->nb_BATs = 4;
#endif
}

static void gen_spr_74xx (CPUPPCState *env)
{
    /* Processor identification */
    spr_register(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MMCR2, "MMCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UMMCR2, "UMMCR2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX: not implemented */
    spr_register(env, SPR_BAMR, "BAMR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MSSCR0, "MSSCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Altivec */
    spr_register(env, SPR_VRSAVE, "VRSAVE",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Not strictly an SPR */
    vscr_init(env, 0x00010000);
}

static void gen_l3_ctrl (CPUPPCState *env)
{
    /* L3CR */
    /* XXX : not implemented */
    spr_register(env, SPR_L3CR, "L3CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR0, "L3ITCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3PM */
    /* XXX : not implemented */
    spr_register(env, SPR_L3PM, "L3PM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_74xx_soft_tlb (CPUPPCState *env, int nb_tlbs, int nb_ways)
{
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = nb_tlbs;
    env->nb_ways = nb_ways;
    env->id_tlbs = 1;
    /* XXX : not implemented */
    spr_register(env, SPR_PTEHI, "PTEHI",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PTELO, "PTELO",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_TLBMISS, "TLBMISS",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#endif
}

static void gen_spr_usprgh (CPUPPCState *env)
{
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
}

/* PowerPC BookE SPR */
static void gen_spr_BookE (CPUPPCState *env, uint64_t ivor_mask)
{
    const char *ivor_names[64] = {
        "IVOR0",  "IVOR1",  "IVOR2",  "IVOR3",
        "IVOR4",  "IVOR5",  "IVOR6",  "IVOR7",
        "IVOR8",  "IVOR9",  "IVOR10", "IVOR11",
        "IVOR12", "IVOR13", "IVOR14", "IVOR15",
        "IVOR16", "IVOR17", "IVOR18", "IVOR19",
        "IVOR20", "IVOR21", "IVOR22", "IVOR23",
        "IVOR24", "IVOR25", "IVOR26", "IVOR27",
        "IVOR28", "IVOR29", "IVOR30", "IVOR31",
        "IVOR32", "IVOR33", "IVOR34", "IVOR35",
        "IVOR36", "IVOR37", "IVOR38", "IVOR39",
        "IVOR40", "IVOR41", "IVOR42", "IVOR43",
        "IVOR44", "IVOR45", "IVOR46", "IVOR47",
        "IVOR48", "IVOR49", "IVOR50", "IVOR51",
        "IVOR52", "IVOR53", "IVOR54", "IVOR55",
        "IVOR56", "IVOR57", "IVOR58", "IVOR59",
        "IVOR60", "IVOR61", "IVOR62", "IVOR63",
    };
#define SPR_BOOKE_IVORxx (-1)
    int ivor_sprn[64] = {
        SPR_BOOKE_IVOR0,  SPR_BOOKE_IVOR1,  SPR_BOOKE_IVOR2,  SPR_BOOKE_IVOR3,
        SPR_BOOKE_IVOR4,  SPR_BOOKE_IVOR5,  SPR_BOOKE_IVOR6,  SPR_BOOKE_IVOR7,
        SPR_BOOKE_IVOR8,  SPR_BOOKE_IVOR9,  SPR_BOOKE_IVOR10, SPR_BOOKE_IVOR11,
        SPR_BOOKE_IVOR12, SPR_BOOKE_IVOR13, SPR_BOOKE_IVOR14, SPR_BOOKE_IVOR15,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVOR32, SPR_BOOKE_IVOR33, SPR_BOOKE_IVOR34, SPR_BOOKE_IVOR35,
        SPR_BOOKE_IVOR36, SPR_BOOKE_IVOR37, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
        SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx, SPR_BOOKE_IVORxx,
    };
    int i;

    /* Interrupt processing */
    spr_register(env, SPR_BOOKE_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_CSRR1, "CSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Debug */
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DAC1, "DAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DBCR1, "DBCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DBCR2, "DBCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DEAR, "DEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_ESR, "ESR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVPR, "IVPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_excp_prefix,
                 0x00000000);
    /* Exception vectors */
    for (i = 0; i < 64; i++) {
        if (ivor_mask & (1ULL << i)) {
            if (ivor_sprn[i] == SPR_BOOKE_IVORxx) {
                fprintf(stderr, "ERROR: IVOR %d SPR is not defined\n", i);
                exit(1);
            }
            spr_register(env, ivor_sprn[i], ivor_names[i],
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, &spr_write_excp_vector,
                         0x00000000);
        }
    }
    spr_register(env, SPR_BOOKE_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_TCR, "TCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tcr,
                 0x00000000);
    spr_register(env, SPR_BOOKE_TSR, "TSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tsr,
                 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DECAR, "DECAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_USPRG0, "USPRG0",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* FSL storage control registers */
static void gen_spr_BookE_FSL (CPUPPCState *env, uint32_t mas_mask)
{
#if !defined(CONFIG_USER_ONLY)
    const char *mas_names[8] = {
        "MAS0", "MAS1", "MAS2", "MAS3", "MAS4", "MAS5", "MAS6", "MAS7",
    };
    int mas_sprn[8] = {
        SPR_BOOKE_MAS0, SPR_BOOKE_MAS1, SPR_BOOKE_MAS2, SPR_BOOKE_MAS3,
        SPR_BOOKE_MAS4, SPR_BOOKE_MAS5, SPR_BOOKE_MAS6, SPR_BOOKE_MAS7,
    };
    int i;

    /* TLB assist registers */
    /* XXX : not implemented */
    for (i = 0; i < 8; i++) {
        if (mas_mask & (1 << i)) {
            spr_register(env, mas_sprn[i], mas_names[i],
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, &spr_write_generic,
                         0x00000000);
        }
    }
    if (env->nb_pids > 1) {
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_PID1, "PID1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
    }
    if (env->nb_pids > 2) {
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_PID2, "PID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
    }
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000); /* TOFIX */
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    switch (env->nb_ways) {
    case 4:
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_TLB3CFG, "TLB3CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000000); /* TOFIX */
        /* Fallthru */
    case 3:
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_TLB2CFG, "TLB2CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000000); /* TOFIX */
        /* Fallthru */
    case 2:
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_TLB1CFG, "TLB1CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000000); /* TOFIX */
        /* Fallthru */
    case 1:
        /* XXX : not implemented */
        spr_register(env, SPR_BOOKE_TLB0CFG, "TLB0CFG",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, SPR_NOACCESS,
                     0x00000000); /* TOFIX */
        /* Fallthru */
    case 0:
    default:
        break;
    }
#endif
}

/* SPR specific to PowerPC 440 implementation */
static void gen_spr_440 (CPUPPCState *env)
{
    /* Cache control */
    /* XXX : not implemented */
    spr_register(env, SPR_440_DNV0, "DNV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DNV1, "DNV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DNV2, "DNV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DNV3, "DNV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DTV0, "DTV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DTV1, "DTV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DTV2, "DTV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DTV3, "DTV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DVLIM, "DVLIM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_INV0, "INV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_INV1, "INV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_INV2, "INV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_INV3, "INV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ITV0, "ITV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ITV1, "ITV1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ITV2, "ITV2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ITV3, "ITV3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_IVLIM, "IVLIM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Cache debug */
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DCDBTRH, "DCDBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DCDBTRL, "DCDBTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_ICDBTRH, "ICDBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_ICDBTRL, "ICDBTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DBDR, "DBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Processor control */
    spr_register(env, SPR_4xx_CCR0, "CCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_440_RSTCFG, "RSTCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Storage control */
    spr_register(env, SPR_440_MMUCR, "MMUCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR shared between PowerPC 40x implementations */
static void gen_spr_40x (CPUPPCState *env)
{
    /* Cache */
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_40x_DCCR, "DCCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_40x_ICCR, "ICCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_BOOKE_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Exception */
    spr_register(env, SPR_40x_DEAR, "DEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_ESR, "ESR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_EVPR, "EVPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_excp_prefix,
                 0x00000000);
    spr_register(env, SPR_40x_SRR2, "SRR2",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_SRR3, "SRR3",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Timers */
    spr_register(env, SPR_40x_PIT, "PIT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_40x_pit, &spr_write_40x_pit,
                 0x00000000);
    spr_register(env, SPR_40x_TCR, "TCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tcr,
                 0x00000000);
    spr_register(env, SPR_40x_TSR, "TSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_booke_tsr,
                 0x00000000);
}

/* SPR specific to PowerPC 405 implementation */
static void gen_spr_405 (CPUPPCState *env)
{
    /* MMU */
    spr_register(env, SPR_40x_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_4xx_CCR0, "CCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00700000);
    /* Debug interface */
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_dbcr0,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_DBCR1, "DBCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 /* Last reset was system reset */
                 0x00000300);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC1, "DAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Storage control */
    /* XXX: TODO: not implemented */
    spr_register(env, SPR_405_SLER, "SLER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_sler,
                 0x00000000);
    spr_register(env, SPR_40x_ZPR, "ZPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_405_SU0R, "SU0R",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* SPRG */
    spr_register(env, SPR_USPRG0, "USPRG0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 spr_read_generic, &spr_write_generic,
                 0x00000000);
    gen_spr_usprgh(env);
}

/* SPR shared between PowerPC 401 & 403 implementations */
static void gen_spr_401_403 (CPUPPCState *env)
{
    /* Time base */
    spr_register(env, SPR_403_VTBL,  "TBL",
                 &spr_read_tbl, SPR_NOACCESS,
                 &spr_read_tbl, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_403_TBL,   "TBL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_tbl,
                 0x00000000);
    spr_register(env, SPR_403_VTBU,  "TBU",
                 &spr_read_tbu, SPR_NOACCESS,
                 &spr_read_tbu, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_403_TBU,   "TBU",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_tbu,
                 0x00000000);
    /* Debug */
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_403_CDBCR, "CDBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 401 implementation */
static void gen_spr_401 (CPUPPCState *env)
{
    /* Debug interface */
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBCR0, "DBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_dbcr0,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 /* Last reset was system reset */
                 0x00000300);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC1, "DAC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC1, "IAC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Storage control */
    /* XXX: TODO: not implemented */
    spr_register(env, SPR_405_SLER, "SLER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_sler,
                 0x00000000);
    /* not emulated, as Qemu never does speculative access */
    spr_register(env, SPR_40x_SGR, "SGR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFFFFFFF);
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_40x_DCWR, "DCWR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_spr_401x2 (CPUPPCState *env)
{
    gen_spr_401(env);
    spr_register(env, SPR_40x_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_ZPR, "ZPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 403 implementation */
static void gen_spr_403 (CPUPPCState *env)
{
    /* Debug interface */
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_40x_dbcr0,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 /* Last reset was system reset */
                 0x00000300);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC1, "DAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_spr_403_real (CPUPPCState *env)
{
    spr_register(env, SPR_403_PBL1,  "PBL1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_403_pbr, &spr_write_403_pbr,
                 0x00000000);
    spr_register(env, SPR_403_PBU1,  "PBU1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_403_pbr, &spr_write_403_pbr,
                 0x00000000);
    spr_register(env, SPR_403_PBL2,  "PBL2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_403_pbr, &spr_write_403_pbr,
                 0x00000000);
    spr_register(env, SPR_403_PBU2,  "PBU2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_403_pbr, &spr_write_403_pbr,
                 0x00000000);
}

static void gen_spr_403_mmu (CPUPPCState *env)
{
    /* MMU */
    spr_register(env, SPR_40x_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_ZPR, "ZPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC compression coprocessor extension */
static void gen_spr_compress (CPUPPCState *env)
{
    /* XXX : not implemented */
    spr_register(env, SPR_401_SKR, "SKR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

#if defined (TARGET_PPC64)
/* SPR specific to PowerPC 620 */
static void gen_spr_620 (CPUPPCState *env)
{
    /* Processor identification */
    spr_register(env, SPR_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    spr_register(env, SPR_ASR, "ASR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_asr, &spr_write_asr,
                 0x00000000);
    /* Breakpoints */
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DABR, "DABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SIAR, "SIAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_SDA, "SDA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMC1R, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_620_PMC1W, "PMC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                  SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMC2R, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_620_PMC2W, "PMC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                  SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_MMCR0R, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_620_MMCR0W, "MMCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                  SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#if 0 // XXX: check this
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR0, "PMR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR1, "PMR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR2, "PMR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR3, "PMR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR4, "PMR4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR5, "PMR5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR6, "PMR6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR7, "PMR7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR8, "PMR8",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMR9, "PMR9",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRA, "PMR10",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRB, "PMR11",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRC, "PMR12",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRD, "PMR13",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRE, "PMR14",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_PMRF, "PMR15",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#endif
    /* XXX : not implemented */
    spr_register(env, SPR_620_BUSCSR, "BUSCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_620_L2SR, "L2SR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}
#endif /* defined (TARGET_PPC64) */

static void gen_spr_5xx_8xx (CPUPPCState *env)
{
    /* Exception processing */
    spr_register(env, SPR_DSISR, "DSISR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_DAR, "DAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_EIE, "EIE",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_EID, "EID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_NRI, "NRI",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPA, "CMPA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPB, "CMPB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPC, "CMPC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPD, "CMPD",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_ECR, "ECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_DER, "DER",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_COUNTA, "COUNTA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_COUNTB, "COUNTB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPE, "CMPE",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPF, "CMPF",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPG, "CMPG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_CMPH, "CMPH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_LCTRL1, "LCTRL1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_LCTRL2, "LCTRL2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_BAR, "BAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_DPDR, "DPDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_IMMR, "IMMR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_spr_5xx (CPUPPCState *env)
{
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_GRA, "MI_GRA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_GRA, "L2U_GRA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RPCU_BBCMCR, "L2U_BBCMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_MCR, "L2U_MCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RBA0, "MI_RBA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RBA1, "MI_RBA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RBA2, "MI_RBA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RBA3, "MI_RBA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RBA0, "L2U_RBA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RBA1, "L2U_RBA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RBA2, "L2U_RBA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RBA3, "L2U_RBA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RA0, "MI_RA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RA1, "MI_RA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RA2, "MI_RA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_MI_RA3, "MI_RA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RA0, "L2U_RA0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RA1, "L2U_RA1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RA2, "L2U_RA2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_L2U_RA3, "L2U_RA3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_RCPU_FPECR, "FPECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

static void gen_spr_8xx (CPUPPCState *env)
{
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_IC_CST, "IC_CST",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_IC_ADR, "IC_ADR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_IC_DAT, "IC_DAT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_DC_CST, "DC_CST",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_DC_ADR, "DC_ADR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_DC_DAT, "DC_DAT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_CTR, "MI_CTR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_AP, "MI_AP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_EPN, "MI_EPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_TWC, "MI_TWC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_RPN, "MI_RPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_DBCAM, "MI_DBCAM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_DBRAM0, "MI_DBRAM0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MI_DBRAM1, "MI_DBRAM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_CTR, "MD_CTR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_CASID, "MD_CASID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_AP, "MD_AP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_EPN, "MD_EPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_TWB, "MD_TWB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_TWC, "MD_TWC",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_RPN, "MD_RPN",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_TW, "MD_TW",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_DBCAM, "MD_DBCAM",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_DBRAM0, "MD_DBRAM0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_MPC_MD_DBRAM1, "MD_DBRAM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

// XXX: TODO
/*
 * AMR     => SPR 29 (Power 2.04)
 * CTRL    => SPR 136 (Power 2.04)
 * CTRL    => SPR 152 (Power 2.04)
 * SCOMC   => SPR 276 (64 bits ?)
 * SCOMD   => SPR 277 (64 bits ?)
 * TBU40   => SPR 286 (Power 2.04 hypv)
 * HSPRG0  => SPR 304 (Power 2.04 hypv)
 * HSPRG1  => SPR 305 (Power 2.04 hypv)
 * HDSISR  => SPR 306 (Power 2.04 hypv)
 * HDAR    => SPR 307 (Power 2.04 hypv)
 * PURR    => SPR 309 (Power 2.04 hypv)
 * HDEC    => SPR 310 (Power 2.04 hypv)
 * HIOR    => SPR 311 (hypv)
 * RMOR    => SPR 312 (970)
 * HRMOR   => SPR 313 (Power 2.04 hypv)
 * HSRR0   => SPR 314 (Power 2.04 hypv)
 * HSRR1   => SPR 315 (Power 2.04 hypv)
 * LPCR    => SPR 316 (970)
 * LPIDR   => SPR 317 (970)
 * SPEFSCR => SPR 512 (Power 2.04 emb)
 * EPR     => SPR 702 (Power 2.04 emb)
 * perf    => 768-783 (Power 2.04)
 * perf    => 784-799 (Power 2.04)
 * PPR     => SPR 896 (Power 2.04)
 * EPLC    => SPR 947 (Power 2.04 emb)
 * EPSC    => SPR 948 (Power 2.04 emb)
 * DABRX   => 1015    (Power 2.04 hypv)
 * FPECR   => SPR 1022 (?)
 * ... and more (thermal management, performance counters, ...)
 */

/*****************************************************************************/
/* Exception vectors models                                                  */
static void init_excp_4xx_real (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_PIT]      = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00001010;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00001020;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00002000;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_4xx_softmmu (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_PIT]      = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00001010;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00001020;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00002000;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_MPC5xx (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_FPA]      = 0x00000E00;
    env->excp_vectors[POWERPC_EXCP_EMUL]     = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_MEXTBR]   = 0x00001E00;
    env->excp_vectors[POWERPC_EXCP_NMEXTBR]  = 0x00001F00;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_MPC8xx (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_FPA]      = 0x00000E00;
    env->excp_vectors[POWERPC_EXCP_EMUL]     = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_ITLBE]    = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_DTLBE]    = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_DABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001C00;
    env->excp_vectors[POWERPC_EXCP_MEXTBR]   = 0x00001E00;
    env->excp_vectors[POWERPC_EXCP_NMEXTBR]  = 0x00001F00;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFF0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_G2 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000A00;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_e200 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000FFC;
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_APU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_SPEU]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EFPDI]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EFPRI]    = 0x00000000;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFF7UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_BookE (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_CRITICAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_APU]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_FIT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DTLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_ITLB]     = 0x00000000;
    env->excp_vectors[POWERPC_EXCP_DEBUG]    = 0x00000000;
    env->excp_prefix = 0x00000000UL;
    env->ivor_mask = 0x0000FFE0UL;
    env->ivpr_mask = 0xFFFF0000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_601 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_IO]       = 0x00000A00;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_RUNM]     = 0x00002000;
    env->excp_prefix = 0xFFF00000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0x00000100UL;
#endif
}

static void init_excp_602 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    /* XXX: exception prefix has a special behavior on 602 */
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_WDT]      = 0x00001500;
    env->excp_vectors[POWERPC_EXCP_EMUL]     = 0x00001600;
    env->excp_prefix = 0xFFF00000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_603 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_604 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

#if defined(TARGET_PPC64)
static void init_excp_620 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_prefix = 0xFFF00000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0x0000000000000100ULL;
#endif
}
#endif /* defined(TARGET_PPC64) */

static void init_excp_7x0 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_750cl (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_750cx (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

/* XXX: Check if this is correct */
static void init_excp_7x5 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_7400 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001600;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001700;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

static void init_excp_7450 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IFTLB]    = 0x00001000;
    env->excp_vectors[POWERPC_EXCP_DLTLB]    = 0x00001100;
    env->excp_vectors[POWERPC_EXCP_DSTLB]    = 0x00001200;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_SMI]      = 0x00001400;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001600;
    env->excp_prefix = 0x00000000UL;
    /* Hardware reset vector */
    env->hreset_vector = 0xFFFFFFFCUL;
#endif
}

#if defined (TARGET_PPC64)
static void init_excp_970 (CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    env->excp_vectors[POWERPC_EXCP_RESET]    = 0x00000100;
    env->excp_vectors[POWERPC_EXCP_MCHECK]   = 0x00000200;
    env->excp_vectors[POWERPC_EXCP_DSI]      = 0x00000300;
    env->excp_vectors[POWERPC_EXCP_DSEG]     = 0x00000380;
    env->excp_vectors[POWERPC_EXCP_ISI]      = 0x00000400;
    env->excp_vectors[POWERPC_EXCP_ISEG]     = 0x00000480;
    env->excp_vectors[POWERPC_EXCP_EXTERNAL] = 0x00000500;
    env->excp_vectors[POWERPC_EXCP_ALIGN]    = 0x00000600;
    env->excp_vectors[POWERPC_EXCP_PROGRAM]  = 0x00000700;
    env->excp_vectors[POWERPC_EXCP_FPU]      = 0x00000800;
    env->excp_vectors[POWERPC_EXCP_DECR]     = 0x00000900;
    env->excp_vectors[POWERPC_EXCP_HDECR]    = 0x00000980;
    env->excp_vectors[POWERPC_EXCP_SYSCALL]  = 0x00000C00;
    env->excp_vectors[POWERPC_EXCP_TRACE]    = 0x00000D00;
    env->excp_vectors[POWERPC_EXCP_PERFM]    = 0x00000F00;
    env->excp_vectors[POWERPC_EXCP_VPU]      = 0x00000F20;
    env->excp_vectors[POWERPC_EXCP_IABR]     = 0x00001300;
    env->excp_vectors[POWERPC_EXCP_MAINT]    = 0x00001600;
    env->excp_vectors[POWERPC_EXCP_VPUA]     = 0x00001700;
    env->excp_vectors[POWERPC_EXCP_THERM]    = 0x00001800;
    env->excp_prefix   = 0x00000000FFF00000ULL;
    /* Hardware reset vector */
    env->hreset_vector = 0x0000000000000100ULL;
#endif
}
#endif

/*****************************************************************************/
/* Power management enable checks                                            */
static int check_pow_none (CPUPPCState *env)
{
    return 0;
}

static int check_pow_nocheck (CPUPPCState *env)
{
    return 1;
}

static int check_pow_hid0 (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00E00000)
        return 1;

    return 0;
}

static int check_pow_hid0_74xx (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00600000)
        return 1;

    return 0;
}

/*****************************************************************************/
/* PowerPC implementations definitions                                       */

/* PowerPC 401                                                               */
#define POWERPC_INSNS_401    (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_WRTEE | PPC_DCR |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ |                                \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_401     (0x00000000000FD201ULL)
#define POWERPC_MMU_401      (POWERPC_MMU_REAL)
#define POWERPC_EXCP_401     (POWERPC_EXCP_40x)
#define POWERPC_INPUT_401    (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_401     (bfd_mach_ppc_403)
#define POWERPC_FLAG_401     (POWERPC_FLAG_CE | POWERPC_FLAG_DE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_401        check_pow_nocheck

static void init_proc_401 (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_401(env);
    init_excp_4xx_real(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 401x2                                                             */
#define POWERPC_INSNS_401x2  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC | \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_401x2   (0x00000000001FD231ULL)
#define POWERPC_MMU_401x2    (POWERPC_MMU_SOFT_4xx_Z)
#define POWERPC_EXCP_401x2   (POWERPC_EXCP_40x)
#define POWERPC_INPUT_401x2  (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_401x2   (bfd_mach_ppc_403)
#define POWERPC_FLAG_401x2   (POWERPC_FLAG_CE | POWERPC_FLAG_DE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_401x2      check_pow_nocheck

static void init_proc_401x2 (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_401x2(env);
    gen_spr_compress(env);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 401x3                                                             */
#define POWERPC_INSNS_401x3  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC | \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_401x3   (0x00000000001FD631ULL)
#define POWERPC_MMU_401x3    (POWERPC_MMU_SOFT_4xx_Z)
#define POWERPC_EXCP_401x3   (POWERPC_EXCP_40x)
#define POWERPC_INPUT_401x3  (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_401x3   (bfd_mach_ppc_403)
#define POWERPC_FLAG_401x3   (POWERPC_FLAG_CE | POWERPC_FLAG_DE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_401x3      check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_401x3 (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_401(env);
    gen_spr_401x2(env);
    gen_spr_compress(env);
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* IOP480                                                                    */
#define POWERPC_INSNS_IOP480 (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI |  PPC_40x_ICBT |    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC | \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_IOP480  (0x00000000001FD231ULL)
#define POWERPC_MMU_IOP480   (POWERPC_MMU_SOFT_4xx_Z)
#define POWERPC_EXCP_IOP480  (POWERPC_EXCP_40x)
#define POWERPC_INPUT_IOP480 (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_IOP480  (bfd_mach_ppc_403)
#define POWERPC_FLAG_IOP480  (POWERPC_FLAG_CE | POWERPC_FLAG_DE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_IOP480     check_pow_nocheck

static void init_proc_IOP480 (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_401x2(env);
    gen_spr_compress(env);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 403                                                               */
#define POWERPC_INSNS_403    (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ |                                \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_403     (0x000000000007D00DULL)
#define POWERPC_MMU_403      (POWERPC_MMU_REAL)
#define POWERPC_EXCP_403     (POWERPC_EXCP_40x)
#define POWERPC_INPUT_403    (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_403     (bfd_mach_ppc_403)
#define POWERPC_FLAG_403     (POWERPC_FLAG_CE | POWERPC_FLAG_PX |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_403        check_pow_nocheck

static void init_proc_403 (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_403(env);
    gen_spr_403_real(env);
    init_excp_4xx_real(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 403 GCX                                                           */
#define POWERPC_INSNS_403GCX (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ |                                \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC | \
                              PPC_4xx_COMMON | PPC_40x_EXCP)
#define POWERPC_MSRM_403GCX  (0x000000000007D00DULL)
#define POWERPC_MMU_403GCX   (POWERPC_MMU_SOFT_4xx_Z)
#define POWERPC_EXCP_403GCX  (POWERPC_EXCP_40x)
#define POWERPC_INPUT_403GCX (PPC_FLAGS_INPUT_401)
#define POWERPC_BFDM_403GCX  (bfd_mach_ppc_403)
#define POWERPC_FLAG_403GCX  (POWERPC_FLAG_CE | POWERPC_FLAG_PX |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_403GCX     check_pow_nocheck

static void init_proc_403GCX (CPUPPCState *env)
{
    gen_spr_40x(env);
    gen_spr_401_403(env);
    gen_spr_403(env);
    gen_spr_403_real(env);
    gen_spr_403_mmu(env);
    /* Bus access control */
    /* not emulated, as Qemu never does speculative access */
    spr_register(env, SPR_40x_SGR, "SGR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFFFFFFF);
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_40x_DCWR, "DCWR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 405                                                               */
#define POWERPC_INSNS_405    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_40x_ICBT |     \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_40x_TLB | PPC_MEM_TLBIA | PPC_MEM_TLBSYNC | \
                              PPC_4xx_COMMON | PPC_405_MAC | PPC_40x_EXCP)
#define POWERPC_MSRM_405     (0x000000000006E630ULL)
#define POWERPC_MMU_405      (POWERPC_MMU_SOFT_4xx)
#define POWERPC_EXCP_405     (POWERPC_EXCP_40x)
#define POWERPC_INPUT_405    (PPC_FLAGS_INPUT_405)
#define POWERPC_BFDM_405     (bfd_mach_ppc_403)
#define POWERPC_FLAG_405     (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_405        check_pow_nocheck

static void init_proc_405 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_40x(env);
    gen_spr_405(env);
    /* Bus access control */
    /* not emulated, as Qemu never does speculative access */
    spr_register(env, SPR_40x_SGR, "SGR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFFFFFFF);
    /* not emulated, as Qemu do not emulate caches */
    spr_register(env, SPR_40x_DCWR, "DCWR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_4xx_softmmu(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc40x_irq_init(env);
}

/* PowerPC 440 EP                                                            */
#define POWERPC_INSNS_440EP  (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE | PPC_RFMCI |               \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC |                               \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_440EP   (0x000000000006D630ULL)
#define POWERPC_MMU_440EP    (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_440EP   (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_440EP  (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_440EP   (bfd_mach_ppc_403)
#define POWERPC_FLAG_440EP   (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_440EP      check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_440EP (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* PowerPC 440 GP                                                            */
#define POWERPC_INSNS_440GP  (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_DCRX | PPC_WRTEE | PPC_MFAPIDI |  \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC | PPC_TLBIVA |                  \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_440GP   (0x000000000006FF30ULL)
#define POWERPC_MMU_440GP    (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_440GP   (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_440GP  (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_440GP   (bfd_mach_ppc_403)
#define POWERPC_FLAG_440GP   (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_440GP      check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_440GP (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* PowerPC 440x4                                                             */
#define POWERPC_INSNS_440x4  (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE |                           \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC |                               \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_440x4   (0x000000000006FF30ULL)
#define POWERPC_MMU_440x4    (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_440x4   (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_440x4  (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_440x4   (bfd_mach_ppc_403)
#define POWERPC_FLAG_440x4   (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_440x4      check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_440x4 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* PowerPC 440x5                                                             */
#define POWERPC_INSNS_440x5  (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_WRTEE | PPC_RFMCI |               \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC |                               \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_440x5   (0x000000000006FF30ULL)
#define POWERPC_MMU_440x5    (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_440x5   (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_440x5  (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_440x5   (bfd_mach_ppc_403)
#define POWERPC_FLAG_440x5   (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |           \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_440x5      check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_440x5 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* PowerPC 460 (guessed)                                                     */
#define POWERPC_INSNS_460    (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_DCR | PPC_DCRX  | PPC_DCRUX |               \
                              PPC_WRTEE | PPC_MFAPIDI |                       \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC | PPC_TLBIVA |                  \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_460     (0x000000000006FF30ULL)
#define POWERPC_MMU_460      (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_460     (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_460    (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_460     (bfd_mach_ppc_403)
#define POWERPC_FLAG_460     (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_460        check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_460 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DCRIPR, "SPR_DCRIPR",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* PowerPC 460F (guessed)                                                    */
#define POWERPC_INSNS_460F   (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_FLOAT | PPC_FLOAT_FRES | PPC_FLOAT_FSEL |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_DCR | PPC_DCRX | PPC_DCRUX |                \
                              PPC_WRTEE | PPC_MFAPIDI |                       \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC | PPC_TLBIVA |                  \
                              PPC_BOOKE | PPC_4xx_COMMON | PPC_405_MAC |      \
                              PPC_440_SPEC)
#define POWERPC_MSRM_460     (0x000000000006FF30ULL)
#define POWERPC_MMU_460F     (POWERPC_MMU_BOOKE)
#define POWERPC_EXCP_460F    (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_460F   (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_460F    (bfd_mach_ppc_403)
#define POWERPC_FLAG_460F    (POWERPC_FLAG_CE | POWERPC_FLAG_DWE |            \
                              POWERPC_FLAG_DE | POWERPC_FLAG_BUS_CLK)
#define check_pow_460F       check_pow_nocheck

__attribute__ (( unused ))
static void init_proc_460F (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000000000FFFFULL);
    gen_spr_440(env);
    gen_spr_usprgh(env);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC1, "DVC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_DVC2, "DVC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_CCR1, "CCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_DCRIPR, "SPR_DCRIPR",
                 &spr_read_generic, &spr_write_generic,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_BookE(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* Freescale 5xx cores (aka RCPU) */
#define POWERPC_INSNS_MPC5xx (PPC_INSNS_BASE | PPC_STRING |                   \
                              PPC_MEM_EIEIO | PPC_MEM_SYNC |                  \
                              PPC_CACHE_ICBI | PPC_FLOAT | PPC_FLOAT_STFIWX | \
                              PPC_MFTB)
#define POWERPC_MSRM_MPC5xx  (0x000000000001FF43ULL)
#define POWERPC_MMU_MPC5xx   (POWERPC_MMU_REAL)
#define POWERPC_EXCP_MPC5xx  (POWERPC_EXCP_603)
#define POWERPC_INPUT_MPC5xx (PPC_FLAGS_INPUT_RCPU)
#define POWERPC_BFDM_MPC5xx  (bfd_mach_ppc_505)
#define POWERPC_FLAG_MPC5xx  (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_MPC5xx     check_pow_none

__attribute__ (( unused ))
static void init_proc_MPC5xx (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_5xx_8xx(env);
    gen_spr_5xx(env);
    init_excp_MPC5xx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* Freescale 8xx cores (aka PowerQUICC) */
#define POWERPC_INSNS_MPC8xx (PPC_INSNS_BASE | PPC_STRING  |                  \
                              PPC_MEM_EIEIO | PPC_MEM_SYNC |                  \
                              PPC_CACHE_ICBI | PPC_MFTB)
#define POWERPC_MSRM_MPC8xx  (0x000000000001F673ULL)
#define POWERPC_MMU_MPC8xx   (POWERPC_MMU_MPC8xx)
#define POWERPC_EXCP_MPC8xx  (POWERPC_EXCP_603)
#define POWERPC_INPUT_MPC8xx (PPC_FLAGS_INPUT_RCPU)
#define POWERPC_BFDM_MPC8xx  (bfd_mach_ppc_860)
#define POWERPC_FLAG_MPC8xx  (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_MPC8xx     check_pow_none

__attribute__ (( unused ))
static void init_proc_MPC8xx (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_5xx_8xx(env);
    gen_spr_8xx(env);
    init_excp_MPC8xx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* Freescale 82xx cores (aka PowerQUICC-II)                                  */
/* PowerPC G2                                                                */
#define POWERPC_INSNS_G2     (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_G2      (0x000000000006FFF2ULL)
#define POWERPC_MMU_G2       (POWERPC_MMU_SOFT_6xx)
//#define POWERPC_EXCP_G2      (POWERPC_EXCP_G2)
#define POWERPC_INPUT_G2     (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_G2      (bfd_mach_ppc_ec603e)
#define POWERPC_FLAG_G2      (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_G2         check_pow_hid0

static void init_proc_G2 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_G2_755(env);
    gen_spr_G2(env);
    /* Time base */
    gen_tbl(env);
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation register */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_G2(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC G2LE                                                              */
#define POWERPC_INSNS_G2LE   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_G2LE    (0x000000000007FFF3ULL)
#define POWERPC_MMU_G2LE     (POWERPC_MMU_SOFT_6xx)
#define POWERPC_EXCP_G2LE    (POWERPC_EXCP_G2)
#define POWERPC_INPUT_G2LE   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_G2LE    (bfd_mach_ppc_ec603e)
#define POWERPC_FLAG_G2LE    (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_G2LE       check_pow_hid0

static void init_proc_G2LE (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_G2_755(env);
    gen_spr_G2(env);
    /* Time base */
    gen_tbl(env);
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation register */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_G2(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* e200 core                                                                 */
/* XXX: unimplemented instructions:
 * dcblc
 * dcbtlst
 * dcbtstls
 * icblc
 * icbtls
 * tlbivax
 * all SPE multiply-accumulate instructions
 */
#define POWERPC_INSNS_e200   (PPC_INSNS_BASE | PPC_ISEL |                     \
                              PPC_SPE | PPC_SPE_SINGLE |                      \
                              PPC_WRTEE | PPC_RFDI |                          \
                              PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI |   \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |               \
                              PPC_MEM_TLBSYNC | PPC_TLBIVAX |                 \
                              PPC_BOOKE)
#define POWERPC_MSRM_e200    (0x000000000606FF30ULL)
#define POWERPC_MMU_e200     (POWERPC_MMU_BOOKE_FSL)
#define POWERPC_EXCP_e200    (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_e200   (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_e200    (bfd_mach_ppc_860)
#define POWERPC_FLAG_e200    (POWERPC_FLAG_SPE | POWERPC_FLAG_CE |            \
                              POWERPC_FLAG_UBLE | POWERPC_FLAG_DE |           \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_e200       check_pow_hid0

__attribute__ (( unused ))
static void init_proc_e200 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x000000070000FFFFULL);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_SPEFSCR, "SPEFSCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_spr_BookE_FSL(env, 0x0000005D);
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_ALTCTXCR, "ALTCTXCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_BUCSR, "BUCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_CTXCR, "CTXCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_DBCNT, "DBCNT",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_DBCR3, "DBCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1CFG0, "L1CFG0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1CSR0, "L1CSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1FINV0, "L1FINV0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_TLB0CFG, "TLB0CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_TLB1CFG, "TLB1CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC3, "IAC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_IAC4, "IAC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DSRR0, "DSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DSRR1, "DSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_e200(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* e300 core                                                                 */
#define POWERPC_INSNS_e300   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_e300    (0x000000000007FFF3ULL)
#define POWERPC_MMU_e300     (POWERPC_MMU_SOFT_6xx)
#define POWERPC_EXCP_e300    (POWERPC_EXCP_603)
#define POWERPC_INPUT_e300   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_e300    (bfd_mach_ppc_603)
#define POWERPC_FLAG_e300    (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_e300       check_pow_hid0

__attribute__ (( unused ))
static void init_proc_e300 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_603(env);
    /* Time base */
    gen_tbl(env);
    /* hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_603(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* e500 core                                                               */
#define POWERPC_INSNS_e500   (PPC_INSNS_BASE | PPC_ISEL |             \
                              PPC_SPE | PPC_SPE_SINGLE | PPC_SPE_DOUBLE |   \
                              PPC_WRTEE | PPC_RFDI |                  \
                              PPC_CACHE | PPC_CACHE_LOCK | PPC_CACHE_ICBI | \
                              PPC_CACHE_DCBZ | PPC_CACHE_DCBA |       \
                              PPC_MEM_TLBSYNC | PPC_TLBIVAX |         \
                              PPC_BOOKE)
#define POWERPC_MSRM_e500    (0x000000000606FF30ULL)
#define POWERPC_MMU_e500     (POWERPC_MMU_BOOKE_FSL)
#define POWERPC_EXCP_e500    (POWERPC_EXCP_BOOKE)
#define POWERPC_INPUT_e500   (PPC_FLAGS_INPUT_BookE)
#define POWERPC_BFDM_e500    (bfd_mach_ppc_860)
#define POWERPC_FLAG_e500    (POWERPC_FLAG_SPE | POWERPC_FLAG_CE |            \
                              POWERPC_FLAG_UBLE | POWERPC_FLAG_DE |           \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_e500       check_pow_hid0

__attribute__ (( unused ))
static void init_proc_e500 (CPUPPCState *env)
{
    /* Time base */
    gen_tbl(env);
    gen_spr_BookE(env, 0x0000000F0000FD7FULL);
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_SPEFSCR, "SPEFSCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
#if !defined(CONFIG_USER_ONLY)
    env->nb_pids = 3;
#endif
    gen_spr_BookE_FSL(env, 0x0000005F);
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_BBEAR, "BBEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_BBTAR, "BBTAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_MCAR, "MCAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_MCSR, "MCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_NPIDR, "NPIDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_BUCSR, "BUCSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1CFG0, "L1CFG0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1CSR0, "L1CSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_Exxx_L1CSR1, "L1CSR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_TLB0CFG, "TLB0CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_BOOKE_TLB1CFG, "TLB1CFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR0, "MCSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_MCSRR1, "MCSRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = 64;
    env->nb_ways = 1;
    env->id_tlbs = 0;
#endif
    init_excp_e200(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* XXX: TODO: allocate internal IRQ controller */
}

/* Non-embedded PowerPC                                                      */

/* POWER : same as 601, without mfmsr, mfsr                                  */
#if defined(TODO)
#define POWERPC_INSNS_POWER  (XXX_TODO)
/* POWER RSC (from RAD6000) */
#define POWERPC_MSRM_POWER   (0x00000000FEF0ULL)
#endif /* TODO */

/* PowerPC 601                                                               */
#define POWERPC_INSNS_601    (PPC_INSNS_BASE | PPC_STRING | PPC_POWER_BR |    \
                              PPC_FLOAT |                                     \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO | PPC_MEM_TLBIE |  \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_601     (0x000000000000FD70ULL)
#define POWERPC_MSRR_601     (0x0000000000001040ULL)
//#define POWERPC_MMU_601      (POWERPC_MMU_601)
//#define POWERPC_EXCP_601     (POWERPC_EXCP_601)
#define POWERPC_INPUT_601    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_601     (bfd_mach_ppc_601)
#define POWERPC_FLAG_601     (POWERPC_FLAG_SE | POWERPC_FLAG_RTC_CLK)
#define check_pow_601        check_pow_none

static void init_proc_601 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_601(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_hid0_601,
                 0x80010080);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_601_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_601_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    init_excp_601(env);
    /* XXX: beware that dcache line size is 64 
     *      but dcbz uses 32 bytes "sectors"
     * XXX: this breaks clcs instruction !
     */
    env->dcache_line_size = 32;
    env->icache_line_size = 64;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 601v                                                              */
#define POWERPC_INSNS_601v   (PPC_INSNS_BASE | PPC_STRING | PPC_POWER_BR |    \
                              PPC_FLOAT |                                     \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO | PPC_MEM_TLBIE |  \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_601v    (0x000000000000FD70ULL)
#define POWERPC_MSRR_601v    (0x0000000000001040ULL)
#define POWERPC_MMU_601v     (POWERPC_MMU_601)
#define POWERPC_EXCP_601v    (POWERPC_EXCP_601)
#define POWERPC_INPUT_601v   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_601v    (bfd_mach_ppc_601)
#define POWERPC_FLAG_601v    (POWERPC_FLAG_SE | POWERPC_FLAG_RTC_CLK)
#define check_pow_601v       check_pow_none

static void init_proc_601v (CPUPPCState *env)
{
    init_proc_601(env);
    /* XXX : not implemented */
    spr_register(env, SPR_601_HID15, "HID15",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* PowerPC 602                                                               */
#define POWERPC_INSNS_602    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_6xx_TLB | PPC_MEM_TLBSYNC | \
                              PPC_SEGMENT | PPC_602_SPEC)
#define POWERPC_MSRM_602     (0x0000000000C7FF73ULL)
/* XXX: 602 MMU is quite specific. Should add a special case */
#define POWERPC_MMU_602      (POWERPC_MMU_SOFT_6xx)
//#define POWERPC_EXCP_602     (POWERPC_EXCP_602)
#define POWERPC_INPUT_602    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_602     (bfd_mach_ppc_602)
#define POWERPC_FLAG_602     (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_602        check_pow_hid0

static void init_proc_602 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_602(env);
    /* Time base */
    gen_tbl(env);
    /* hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_602(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 603                                                               */
#define POWERPC_INSNS_603    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_603     (0x000000000007FF73ULL)
#define POWERPC_MMU_603      (POWERPC_MMU_SOFT_6xx)
//#define POWERPC_EXCP_603     (POWERPC_EXCP_603)
#define POWERPC_INPUT_603    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_603     (bfd_mach_ppc_603)
#define POWERPC_FLAG_603     (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_603        check_pow_hid0

static void init_proc_603 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_603(env);
    /* Time base */
    gen_tbl(env);
    /* hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_603(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 603e                                                              */
#define POWERPC_INSNS_603E   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_603E    (0x000000000007FF73ULL)
#define POWERPC_MMU_603E     (POWERPC_MMU_SOFT_6xx)
//#define POWERPC_EXCP_603E    (POWERPC_EXCP_603E)
#define POWERPC_INPUT_603E   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_603E    (bfd_mach_ppc_ec603e)
#define POWERPC_FLAG_603E    (POWERPC_FLAG_TGPR | POWERPC_FLAG_SE |           \
                              POWERPC_FLAG_BE | POWERPC_FLAG_BUS_CLK)
#define check_pow_603E       check_pow_hid0

static void init_proc_603E (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_603(env);
    /* Time base */
    gen_tbl(env);
    /* hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_IABR, "IABR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_603(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 604                                                               */
#define POWERPC_INSNS_604    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_604     (0x000000000005FF77ULL)
#define POWERPC_MMU_604      (POWERPC_MMU_32B)
//#define POWERPC_EXCP_604     (POWERPC_EXCP_604)
#define POWERPC_INPUT_604    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_604     (bfd_mach_ppc_604)
#define POWERPC_FLAG_604     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_604        check_pow_nocheck

static void init_proc_604 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_604(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_604(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 604E                                                              */
#define POWERPC_INSNS_604E   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_604E    (0x000000000005FF77ULL)
#define POWERPC_MMU_604E     (POWERPC_MMU_32B)
#define POWERPC_EXCP_604E    (POWERPC_EXCP_604)
#define POWERPC_INPUT_604E   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_604E    (bfd_mach_ppc_604)
#define POWERPC_FLAG_604E    (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_604E       check_pow_nocheck

static void init_proc_604E (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_604(env);
    /* XXX : not implemented */
    spr_register(env, SPR_MMCR1, "MMCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC3, "PMC3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC4, "PMC4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_604(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 740                                                               */
#define POWERPC_INSNS_740    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_740     (0x000000000005FF77ULL)
#define POWERPC_MMU_740      (POWERPC_MMU_32B)
#define POWERPC_EXCP_740     (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_740    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_740     (bfd_mach_ppc_750)
#define POWERPC_FLAG_740     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_740        check_pow_hid0

static void init_proc_740 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 750                                                               */
#define POWERPC_INSNS_750    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_750     (0x000000000005FF77ULL)
#define POWERPC_MMU_750      (POWERPC_MMU_32B)
#define POWERPC_EXCP_750     (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_750    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_750     (bfd_mach_ppc_750)
#define POWERPC_FLAG_750     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_750        check_pow_hid0

static void init_proc_750 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    /* XXX: high BATs are also present but are known to be bugged on
     *      die version 1.x
     */
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 750 CL                                                            */
/* XXX: not implemented:
 * cache lock instructions:
 * dcbz_l
 * floating point paired instructions
 * psq_lux
 * psq_lx
 * psq_stux
 * psq_stx
 * ps_abs
 * ps_add
 * ps_cmpo0
 * ps_cmpo1
 * ps_cmpu0
 * ps_cmpu1
 * ps_div
 * ps_madd
 * ps_madds0
 * ps_madds1
 * ps_merge00
 * ps_merge01
 * ps_merge10
 * ps_merge11
 * ps_mr
 * ps_msub
 * ps_mul
 * ps_muls0
 * ps_muls1
 * ps_nabs
 * ps_neg
 * ps_nmadd
 * ps_nmsub
 * ps_res
 * ps_rsqrte
 * ps_sel
 * ps_sub
 * ps_sum0
 * ps_sum1
 */
#define POWERPC_INSNS_750cl  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_750cl   (0x000000000005FF77ULL)
#define POWERPC_MMU_750cl    (POWERPC_MMU_32B)
#define POWERPC_EXCP_750cl   (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_750cl  (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_750cl   (bfd_mach_ppc_750)
#define POWERPC_FLAG_750cl   (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_750cl      check_pow_hid0

static void init_proc_750cl (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    /* Those registers are fake on 750CL */
    spr_register(env, SPR_THRM1, "THRM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_THRM2, "THRM2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_THRM3, "THRM3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX: not implemented */
    spr_register(env, SPR_750_TDCL, "TDCL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_TDCH, "TDCH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* DMA */
    /* XXX : not implemented */
    spr_register(env, SPR_750_WPAR, "WPAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_DMAL, "DMAL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_750_DMAU, "DMAU",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750CL_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750CL_HID4, "HID4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Quantization registers */
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR0, "GQR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR1, "GQR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR2, "GQR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR3, "GQR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR4, "GQR4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR5, "GQR5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR6, "GQR6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750_GQR7, "GQR7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    /* PowerPC 750cl has 8 DBATs and 8 IBATs */
    gen_high_BATs(env);
    init_excp_750cl(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 750CX                                                             */
#define POWERPC_INSNS_750cx  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_750cx   (0x000000000005FF77ULL)
#define POWERPC_MMU_750cx    (POWERPC_MMU_32B)
#define POWERPC_EXCP_750cx   (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_750cx  (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_750cx   (bfd_mach_ppc_750)
#define POWERPC_FLAG_750cx   (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_750cx      check_pow_hid0

static void init_proc_750cx (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* This register is not implemented but is present for compatibility */
    spr_register(env, SPR_SDA, "SDA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    /* PowerPC 750cx has 8 DBATs and 8 IBATs */
    gen_high_BATs(env);
    init_excp_750cx(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 750FX                                                             */
#define POWERPC_INSNS_750fx  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT  | PPC_EXTERN)
#define POWERPC_MSRM_750fx   (0x000000000005FF77ULL)
#define POWERPC_MMU_750fx    (POWERPC_MMU_32B)
#define POWERPC_EXCP_750fx   (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_750fx  (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_750fx   (bfd_mach_ppc_750)
#define POWERPC_FLAG_750fx   (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_750fx      check_pow_hid0

static void init_proc_750fx (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* XXX : not implemented */
    spr_register(env, SPR_750_THRM4, "THRM4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
    gen_high_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 750GX                                                             */
#define POWERPC_INSNS_750gx  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT  | PPC_EXTERN)
#define POWERPC_MSRM_750gx   (0x000000000005FF77ULL)
#define POWERPC_MMU_750gx    (POWERPC_MMU_32B)
#define POWERPC_EXCP_750gx   (POWERPC_EXCP_7x0)
#define POWERPC_INPUT_750gx  (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_750gx   (bfd_mach_ppc_750)
#define POWERPC_FLAG_750gx   (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_750gx      check_pow_hid0

static void init_proc_750gx (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* XXX : not implemented (XXX: different from 750fx) */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* XXX : not implemented */
    spr_register(env, SPR_750_THRM4, "THRM4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Hardware implementation registers */
    /* XXX : not implemented (XXX: different from 750fx) */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented (XXX: different from 750fx) */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
    gen_high_BATs(env);
    init_excp_7x0(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 745                                                               */
#define POWERPC_INSNS_745    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_745     (0x000000000005FF77ULL)
#define POWERPC_MMU_745      (POWERPC_MMU_SOFT_6xx)
#define POWERPC_EXCP_745     (POWERPC_EXCP_7x5)
#define POWERPC_INPUT_745    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_745     (bfd_mach_ppc_750)
#define POWERPC_FLAG_745     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_745        check_pow_hid0

static void init_proc_745 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    gen_spr_G2_755(env);
    /* Time base */
    gen_tbl(env);
    /* Thermal management */
    gen_spr_thrm(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_7x5(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 755                                                               */
#define POWERPC_INSNS_755    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FRSQRTE | PPC_FLOAT_STFIWX |          \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC | PPC_6xx_TLB | \
                              PPC_SEGMENT | PPC_EXTERN)
#define POWERPC_MSRM_755     (0x000000000005FF77ULL)
#define POWERPC_MMU_755      (POWERPC_MMU_SOFT_6xx)
#define POWERPC_EXCP_755     (POWERPC_EXCP_7x5)
#define POWERPC_INPUT_755    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_755     (bfd_mach_ppc_750)
#define POWERPC_FLAG_755     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |             \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_755        check_pow_hid0

static void init_proc_755 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    gen_spr_G2_755(env);
    /* Time base */
    gen_tbl(env);
    /* L2 cache control */
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_L2PMCR, "L2PMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Thermal management */
    gen_spr_thrm(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_6xx_7xx_soft_tlb(env, 64, 2);
    init_excp_7x5(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7400 (aka G4)                                                     */
#define POWERPC_INSNS_7400   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA |                                 \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7400    (0x000000000205FF77ULL)
#define POWERPC_MMU_7400     (POWERPC_MMU_32B)
#define POWERPC_EXCP_7400    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7400   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7400    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7400    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7400       check_pow_hid0_74xx

static void init_proc_7400 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX: this seems not implemented on all revisions. */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSCR1, "MSSCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Thermal management */
    gen_spr_thrm(env);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_7400(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7410 (aka G4)                                                     */
#define POWERPC_INSNS_7410   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA |                                 \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7410    (0x000000000205FF77ULL)
#define POWERPC_MMU_7410     (POWERPC_MMU_32B)
#define POWERPC_EXCP_7410    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7410   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7410    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7410    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7410       check_pow_hid0_74xx

static void init_proc_7410 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Thermal management */
    gen_spr_thrm(env);
    /* L2PMCR */
    /* XXX : not implemented */
    spr_register(env, SPR_L2PMCR, "L2PMCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* LDSTDB */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTDB, "LDSTDB",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_7400(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7440 (aka G4)                                                     */
#define POWERPC_INSNS_7440   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA | PPC_74xx_TLB |                  \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7440    (0x000000000205FF77ULL)
#define POWERPC_MMU_7440     (POWERPC_MMU_SOFT_74xx)
#define POWERPC_EXCP_7440    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7440   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7440    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7440    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7440       check_pow_hid0_74xx

__attribute__ (( unused ))
static void init_proc_7440 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* XXX : not implemented */
    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* LDSTCR */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    /* XXX : not implemented */
    spr_register(env, SPR_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_74xx_soft_tlb(env, 128, 2);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7450 (aka G4)                                                     */
#define POWERPC_INSNS_7450   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA | PPC_74xx_TLB |                  \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7450    (0x000000000205FF77ULL)
#define POWERPC_MMU_7450     (POWERPC_MMU_SOFT_74xx)
#define POWERPC_EXCP_7450    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7450   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7450    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7450    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7450       check_pow_hid0_74xx

__attribute__ (( unused ))
static void init_proc_7450 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* Level 3 cache control */
    gen_l3_ctrl(env);
    /* L3ITCR1 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR1, "L3ITCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR2 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR2, "L3ITCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR3 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR3, "L3ITCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3OHCR */
    /* XXX : not implemented */
    spr_register(env, SPR_L3OHCR, "L3OHCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UBAMR, "UBAMR",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* LDSTCR */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    /* XXX : not implemented */
    spr_register(env, SPR_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_74xx_soft_tlb(env, 128, 2);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7445 (aka G4)                                                     */
#define POWERPC_INSNS_7445   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA | PPC_74xx_TLB |                  \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7445    (0x000000000205FF77ULL)
#define POWERPC_MMU_7445     (POWERPC_MMU_SOFT_74xx)
#define POWERPC_EXCP_7445    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7445   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7445    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7445    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7445       check_pow_hid0_74xx

__attribute__ (( unused ))
static void init_proc_7445 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* LDSTCR */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    /* XXX : not implemented */
    spr_register(env, SPR_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_74xx_soft_tlb(env, 128, 2);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7455 (aka G4)                                                     */
#define POWERPC_INSNS_7455   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA | PPC_74xx_TLB |                  \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7455    (0x000000000205FF77ULL)
#define POWERPC_MMU_7455     (POWERPC_MMU_SOFT_74xx)
#define POWERPC_EXCP_7455    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7455   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7455    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7455    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7455       check_pow_hid0_74xx

__attribute__ (( unused ))
static void init_proc_7455 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* Level 3 cache control */
    gen_l3_ctrl(env);
    /* LDSTCR */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    /* XXX : not implemented */
    spr_register(env, SPR_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_74xx_soft_tlb(env, 128, 2);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

/* PowerPC 7457 (aka G4)                                                     */
#define POWERPC_INSNS_7457   (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI |                    \
                              PPC_CACHE_DCBA | PPC_CACHE_DCBZ |               \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_MEM_TLBIA | PPC_74xx_TLB |                  \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_ALTIVEC)
#define POWERPC_MSRM_7457    (0x000000000205FF77ULL)
#define POWERPC_MMU_7457     (POWERPC_MMU_SOFT_74xx)
#define POWERPC_EXCP_7457    (POWERPC_EXCP_74xx)
#define POWERPC_INPUT_7457   (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_7457    (bfd_mach_ppc_7400)
#define POWERPC_FLAG_7457    (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)
#define check_pow_7457       check_pow_hid0_74xx

__attribute__ (( unused ))
static void init_proc_7457 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* 74xx specific SPR */
    gen_spr_74xx(env);
    /* Level 3 cache control */
    gen_l3_ctrl(env);
    /* L3ITCR1 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR1, "L3ITCR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR2 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR2, "L3ITCR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3ITCR3 */
    /* XXX : not implemented */
    spr_register(env, SPR_L3ITCR3, "L3ITCR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* L3OHCR */
    /* XXX : not implemented */
    spr_register(env, SPR_L3OHCR, "L3OHCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* LDSTCR */
    /* XXX : not implemented */
    spr_register(env, SPR_LDSTCR, "LDSTCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* ICTRL */
    /* XXX : not implemented */
    spr_register(env, SPR_ICTRL, "ICTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MSSSR0 */
    /* XXX : not implemented */
    spr_register(env, SPR_MSSSR0, "MSSSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* PMC */
    /* XXX : not implemented */
    spr_register(env, SPR_PMC5, "PMC5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC5, "UPMC5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_PMC6, "PMC6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_UPMC6, "UPMC6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* SPRGs */
    spr_register(env, SPR_SPRG4, "SPRG4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    gen_high_BATs(env);
    gen_74xx_soft_tlb(env, 128, 2);
    init_excp_7450(env);
    env->dcache_line_size = 32;
    env->icache_line_size = 32;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}

#if defined (TARGET_PPC64)
/* PowerPC 970                                                               */
#define POWERPC_INSNS_970    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZT |  \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_64B | PPC_ALTIVEC |                         \
                              PPC_SEGMENT_64B | PPC_SLBI)
#define POWERPC_MSRM_970     (0x900000000204FF36ULL)
#define POWERPC_MMU_970      (POWERPC_MMU_64B)
//#define POWERPC_EXCP_970     (POWERPC_EXCP_970)
#define POWERPC_INPUT_970    (PPC_FLAGS_INPUT_970)
#define POWERPC_BFDM_970     (bfd_mach_ppc64)
#define POWERPC_FLAG_970     (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)

#if defined(CONFIG_USER_ONLY)
#define POWERPC970_HID5_INIT 0x00000080
#else
#define POWERPC970_HID5_INIT 0x00000000
#endif

static int check_pow_970 (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00600000)
        return 1;

    return 0;
}

static void init_proc_970 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x60000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_970_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 POWERPC970_HID5_INIT);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    /* XXX: not correct */
    gen_low_BATs(env);
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000); /* TOFIX */
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    spr_register(env, SPR_HIOR, "SPR_HIOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFF00000); /* XXX: This is a hack */
#if !defined(CONFIG_USER_ONLY)
    env->slb_nr = 32;
#endif
    init_excp_970(env);
    env->dcache_line_size = 128;
    env->icache_line_size = 128;
    /* Allocate hardware IRQ controller */
    ppc970_irq_init(env);
    /* Can't find information on what this should be on reset.  This
     * value is the one used by 74xx processors. */
    vscr_init(env, 0x00010000);
}

/* PowerPC 970FX (aka G5)                                                    */
#define POWERPC_INSNS_970FX  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZT |  \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_64B | PPC_ALTIVEC |                         \
                              PPC_SEGMENT_64B | PPC_SLBI)
#define POWERPC_MSRM_970FX   (0x800000000204FF36ULL)
#define POWERPC_MMU_970FX    (POWERPC_MMU_64B)
#define POWERPC_EXCP_970FX   (POWERPC_EXCP_970)
#define POWERPC_INPUT_970FX  (PPC_FLAGS_INPUT_970)
#define POWERPC_BFDM_970FX   (bfd_mach_ppc64)
#define POWERPC_FLAG_970FX   (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)

static int check_pow_970FX (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00600000)
        return 1;

    return 0;
}

static void init_proc_970FX (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x60000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_970_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 POWERPC970_HID5_INIT);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    /* XXX: not correct */
    gen_low_BATs(env);
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000); /* TOFIX */
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    spr_register(env, SPR_HIOR, "SPR_HIOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFF00000); /* XXX: This is a hack */
#if !defined(CONFIG_USER_ONLY)
    env->slb_nr = 32;
#endif
    init_excp_970(env);
    env->dcache_line_size = 128;
    env->icache_line_size = 128;
    /* Allocate hardware IRQ controller */
    ppc970_irq_init(env);
    /* Can't find information on what this should be on reset.  This
     * value is the one used by 74xx processors. */
    vscr_init(env, 0x00010000);
}

/* PowerPC 970 GX                                                            */
#define POWERPC_INSNS_970GX  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZT |  \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_64B | PPC_ALTIVEC |                         \
                              PPC_SEGMENT_64B | PPC_SLBI)
#define POWERPC_MSRM_970GX   (0x800000000204FF36ULL)
#define POWERPC_MMU_970GX    (POWERPC_MMU_64B)
#define POWERPC_EXCP_970GX   (POWERPC_EXCP_970)
#define POWERPC_INPUT_970GX  (PPC_FLAGS_INPUT_970)
#define POWERPC_BFDM_970GX   (bfd_mach_ppc64)
#define POWERPC_FLAG_970GX   (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)

static int check_pow_970GX (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x00600000)
        return 1;

    return 0;
}

static void init_proc_970GX (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x60000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_970_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 POWERPC970_HID5_INIT);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    /* XXX: not correct */
    gen_low_BATs(env);
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000); /* TOFIX */
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    spr_register(env, SPR_HIOR, "SPR_HIOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFF00000); /* XXX: This is a hack */
#if !defined(CONFIG_USER_ONLY)
    env->slb_nr = 32;
#endif
    init_excp_970(env);
    env->dcache_line_size = 128;
    env->icache_line_size = 128;
    /* Allocate hardware IRQ controller */
    ppc970_irq_init(env);
    /* Can't find information on what this should be on reset.  This
     * value is the one used by 74xx processors. */
    vscr_init(env, 0x00010000);
}

/* PowerPC 970 MP                                                            */
#define POWERPC_INSNS_970MP  (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZT |  \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_64B | PPC_ALTIVEC |                         \
                              PPC_SEGMENT_64B | PPC_SLBI)
#define POWERPC_MSRM_970MP   (0x900000000204FF36ULL)
#define POWERPC_MMU_970MP    (POWERPC_MMU_64B)
#define POWERPC_EXCP_970MP   (POWERPC_EXCP_970)
#define POWERPC_INPUT_970MP  (PPC_FLAGS_INPUT_970)
#define POWERPC_BFDM_970MP   (bfd_mach_ppc64)
#define POWERPC_FLAG_970MP   (POWERPC_FLAG_VRE | POWERPC_FLAG_SE |            \
                              POWERPC_FLAG_BE | POWERPC_FLAG_PMM |            \
                              POWERPC_FLAG_BUS_CLK)

static int check_pow_970MP (CPUPPCState *env)
{
    if (env->spr[SPR_HID0] & 0x01C00000)
        return 1;

    return 0;
}

static void init_proc_970MP (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_7xx(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_clear,
                 0x60000000);
    /* XXX : not implemented */
    spr_register(env, SPR_HID1, "HID1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_750FX_HID2, "HID2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_970_HID5, "HID5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 POWERPC970_HID5_INIT);
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    /* XXX: not correct */
    gen_low_BATs(env);
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCFG, "MMUCFG",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000); /* TOFIX */
    /* XXX : not implemented */
    spr_register(env, SPR_MMUCSR0, "MMUCSR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000); /* TOFIX */
    spr_register(env, SPR_HIOR, "SPR_HIOR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFF00000); /* XXX: This is a hack */
#if !defined(CONFIG_USER_ONLY)
    env->slb_nr = 32;
#endif
    init_excp_970(env);
    env->dcache_line_size = 128;
    env->icache_line_size = 128;
    /* Allocate hardware IRQ controller */
    ppc970_irq_init(env);
    /* Can't find information on what this should be on reset.  This
     * value is the one used by 74xx processors. */
    vscr_init(env, 0x00010000);
}

/* PowerPC 620                                                               */
#define POWERPC_INSNS_620    (PPC_INSNS_BASE | PPC_STRING | PPC_MFTB |        \
                              PPC_FLOAT | PPC_FLOAT_FSEL | PPC_FLOAT_FRES |   \
                              PPC_FLOAT_FSQRT | PPC_FLOAT_FRSQRTE |           \
                              PPC_FLOAT_STFIWX |                              \
                              PPC_CACHE | PPC_CACHE_ICBI | PPC_CACHE_DCBZ |   \
                              PPC_MEM_SYNC | PPC_MEM_EIEIO |                  \
                              PPC_MEM_TLBIE | PPC_MEM_TLBSYNC |               \
                              PPC_SEGMENT | PPC_EXTERN |                      \
                              PPC_64B | PPC_SLBI)
#define POWERPC_MSRM_620     (0x800000000005FF77ULL)
//#define POWERPC_MMU_620      (POWERPC_MMU_620)
#define POWERPC_EXCP_620     (POWERPC_EXCP_970)
#define POWERPC_INPUT_620    (PPC_FLAGS_INPUT_6xx)
#define POWERPC_BFDM_620     (bfd_mach_ppc64)
#define POWERPC_FLAG_620     (POWERPC_FLAG_SE | POWERPC_FLAG_BE |            \
                              POWERPC_FLAG_PMM | POWERPC_FLAG_BUS_CLK)
#define check_pow_620        check_pow_nocheck /* Check this */

__attribute__ (( unused ))
static void init_proc_620 (CPUPPCState *env)
{
    gen_spr_ne_601(env);
    gen_spr_620(env);
    /* Time base */
    gen_tbl(env);
    /* Hardware implementation registers */
    /* XXX : not implemented */
    spr_register(env, SPR_HID0, "HID0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Memory management */
    gen_low_BATs(env);
    init_excp_620(env);
    env->dcache_line_size = 64;
    env->icache_line_size = 64;
    /* Allocate hardware IRQ controller */
    ppc6xx_irq_init(env);
}
#endif /* defined (TARGET_PPC64) */

/* Default 32 bits PowerPC target will be 604 */
#define CPU_POWERPC_PPC32     CPU_POWERPC_604
#define POWERPC_INSNS_PPC32   POWERPC_INSNS_604
#define POWERPC_MSRM_PPC32    POWERPC_MSRM_604
#define POWERPC_MMU_PPC32     POWERPC_MMU_604
#define POWERPC_EXCP_PPC32    POWERPC_EXCP_604
#define POWERPC_INPUT_PPC32   POWERPC_INPUT_604
#define POWERPC_BFDM_PPC32    POWERPC_BFDM_604
#define POWERPC_FLAG_PPC32    POWERPC_FLAG_604
#define check_pow_PPC32       check_pow_604
#define init_proc_PPC32       init_proc_604

/* Default 64 bits PowerPC target will be 970 FX */
#define CPU_POWERPC_PPC64     CPU_POWERPC_970FX
#define POWERPC_INSNS_PPC64   POWERPC_INSNS_970FX
#define POWERPC_MSRM_PPC64    POWERPC_MSRM_970FX
#define POWERPC_MMU_PPC64     POWERPC_MMU_970FX
#define POWERPC_EXCP_PPC64    POWERPC_EXCP_970FX
#define POWERPC_INPUT_PPC64   POWERPC_INPUT_970FX
#define POWERPC_BFDM_PPC64    POWERPC_BFDM_970FX
#define POWERPC_FLAG_PPC64    POWERPC_FLAG_970FX
#define check_pow_PPC64       check_pow_970FX
#define init_proc_PPC64       init_proc_970FX

/* Default PowerPC target will be PowerPC 32 */
#if defined (TARGET_PPC64) && 0 // XXX: TODO
#define CPU_POWERPC_DEFAULT   CPU_POWERPC_PPC64
#define POWERPC_INSNS_DEFAULT POWERPC_INSNS_PPC64
#define POWERPC_MSRM_DEFAULT  POWERPC_MSRM_PPC64
#define POWERPC_MMU_DEFAULT   POWERPC_MMU_PPC64
#define POWERPC_EXCP_DEFAULT  POWERPC_EXCP_PPC64
#define POWERPC_INPUT_DEFAULT POWERPC_INPUT_PPC64
#define POWERPC_BFDM_DEFAULT  POWERPC_BFDM_PPC64
#define POWERPC_FLAG_DEFAULT  POWERPC_FLAG_PPC64
#define check_pow_DEFAULT     check_pow_PPC64
#define init_proc_DEFAULT     init_proc_PPC64
#else
#define CPU_POWERPC_DEFAULT   CPU_POWERPC_PPC32
#define POWERPC_INSNS_DEFAULT POWERPC_INSNS_PPC32
#define POWERPC_MSRM_DEFAULT  POWERPC_MSRM_PPC32
#define POWERPC_MMU_DEFAULT   POWERPC_MMU_PPC32
#define POWERPC_EXCP_DEFAULT  POWERPC_EXCP_PPC32
#define POWERPC_INPUT_DEFAULT POWERPC_INPUT_PPC32
#define POWERPC_BFDM_DEFAULT  POWERPC_BFDM_PPC32
#define POWERPC_FLAG_DEFAULT  POWERPC_FLAG_PPC32
#define check_pow_DEFAULT     check_pow_PPC32
#define init_proc_DEFAULT     init_proc_PPC32
#endif

/*****************************************************************************/
/* PVR definitions for most known PowerPC                                    */
enum {
    /* PowerPC 401 family */
    /* Generic PowerPC 401 */
#define CPU_POWERPC_401              CPU_POWERPC_401G2
    /* PowerPC 401 cores */
    CPU_POWERPC_401A1              = 0x00210000,
    CPU_POWERPC_401B2              = 0x00220000,
#if 0
    CPU_POWERPC_401B3              = xxx,
#endif
    CPU_POWERPC_401C2              = 0x00230000,
    CPU_POWERPC_401D2              = 0x00240000,
    CPU_POWERPC_401E2              = 0x00250000,
    CPU_POWERPC_401F2              = 0x00260000,
    CPU_POWERPC_401G2              = 0x00270000,
    /* PowerPC 401 microcontrolers */
#if 0
    CPU_POWERPC_401GF              = xxx,
#endif
#define CPU_POWERPC_IOP480           CPU_POWERPC_401B2
    /* IBM Processor for Network Resources */
    CPU_POWERPC_COBRA              = 0x10100000, /* XXX: 405 ? */
#if 0
    CPU_POWERPC_XIPCHIP            = xxx,
#endif
    /* PowerPC 403 family */
    /* Generic PowerPC 403 */
#define CPU_POWERPC_403              CPU_POWERPC_403GC
    /* PowerPC 403 microcontrollers */
    CPU_POWERPC_403GA              = 0x00200011,
    CPU_POWERPC_403GB              = 0x00200100,
    CPU_POWERPC_403GC              = 0x00200200,
    CPU_POWERPC_403GCX             = 0x00201400,
#if 0
    CPU_POWERPC_403GP              = xxx,
#endif
    /* PowerPC 405 family */
    /* Generic PowerPC 405 */
#define CPU_POWERPC_405              CPU_POWERPC_405D4
    /* PowerPC 405 cores */
#if 0
    CPU_POWERPC_405A3              = xxx,
#endif
#if 0
    CPU_POWERPC_405A4              = xxx,
#endif
#if 0
    CPU_POWERPC_405B3              = xxx,
#endif
#if 0
    CPU_POWERPC_405B4              = xxx,
#endif
#if 0
    CPU_POWERPC_405C3              = xxx,
#endif
#if 0
    CPU_POWERPC_405C4              = xxx,
#endif
    CPU_POWERPC_405D2              = 0x20010000,
#if 0
    CPU_POWERPC_405D3              = xxx,
#endif
    CPU_POWERPC_405D4              = 0x41810000,
#if 0
    CPU_POWERPC_405D5              = xxx,
#endif
#if 0
    CPU_POWERPC_405E4              = xxx,
#endif
#if 0
    CPU_POWERPC_405F4              = xxx,
#endif
#if 0
    CPU_POWERPC_405F5              = xxx,
#endif
#if 0
    CPU_POWERPC_405F6              = xxx,
#endif
    /* PowerPC 405 microcontrolers */
    /* XXX: missing 0x200108a0 */
#define CPU_POWERPC_405CR            CPU_POWERPC_405CRc
    CPU_POWERPC_405CRa             = 0x40110041,
    CPU_POWERPC_405CRb             = 0x401100C5,
    CPU_POWERPC_405CRc             = 0x40110145,
    CPU_POWERPC_405EP              = 0x51210950,
#if 0
    CPU_POWERPC_405EXr             = xxx,
#endif
    CPU_POWERPC_405EZ              = 0x41511460, /* 0x51210950 ? */
#if 0
    CPU_POWERPC_405FX              = xxx,
#endif
#define CPU_POWERPC_405GP            CPU_POWERPC_405GPd
    CPU_POWERPC_405GPa             = 0x40110000,
    CPU_POWERPC_405GPb             = 0x40110040,
    CPU_POWERPC_405GPc             = 0x40110082,
    CPU_POWERPC_405GPd             = 0x401100C4,
#define CPU_POWERPC_405GPe           CPU_POWERPC_405CRc
    CPU_POWERPC_405GPR             = 0x50910951,
#if 0
    CPU_POWERPC_405H               = xxx,
#endif
#if 0
    CPU_POWERPC_405L               = xxx,
#endif
    CPU_POWERPC_405LP              = 0x41F10000,
#if 0
    CPU_POWERPC_405PM              = xxx,
#endif
#if 0
    CPU_POWERPC_405PS              = xxx,
#endif
#if 0
    CPU_POWERPC_405S               = xxx,
#endif
    /* IBM network processors */
    CPU_POWERPC_NPE405H            = 0x414100C0,
    CPU_POWERPC_NPE405H2           = 0x41410140,
    CPU_POWERPC_NPE405L            = 0x416100C0,
    CPU_POWERPC_NPE4GS3            = 0x40B10000,
#if 0
    CPU_POWERPC_NPCxx1             = xxx,
#endif
#if 0
    CPU_POWERPC_NPR161             = xxx,
#endif
#if 0
    CPU_POWERPC_LC77700            = xxx,
#endif
    /* IBM STBxxx (PowerPC 401/403/405 core based microcontrollers) */
#if 0
    CPU_POWERPC_STB01000           = xxx,
#endif
#if 0
    CPU_POWERPC_STB01010           = xxx,
#endif
#if 0
    CPU_POWERPC_STB0210            = xxx, /* 401B3 */
#endif
    CPU_POWERPC_STB03              = 0x40310000, /* 0x40130000 ? */
#if 0
    CPU_POWERPC_STB043             = xxx,
#endif
#if 0
    CPU_POWERPC_STB045             = xxx,
#endif
    CPU_POWERPC_STB04              = 0x41810000,
    CPU_POWERPC_STB25              = 0x51510950,
#if 0
    CPU_POWERPC_STB130             = xxx,
#endif
    /* Xilinx cores */
    CPU_POWERPC_X2VP4              = 0x20010820,
#define CPU_POWERPC_X2VP7            CPU_POWERPC_X2VP4
    CPU_POWERPC_X2VP20             = 0x20010860,
#define CPU_POWERPC_X2VP50           CPU_POWERPC_X2VP20
#if 0
    CPU_POWERPC_ZL10310            = xxx,
#endif
#if 0
    CPU_POWERPC_ZL10311            = xxx,
#endif
#if 0
    CPU_POWERPC_ZL10320            = xxx,
#endif
#if 0
    CPU_POWERPC_ZL10321            = xxx,
#endif
    /* PowerPC 440 family */
    /* Generic PowerPC 440 */
#define CPU_POWERPC_440              CPU_POWERPC_440GXf
    /* PowerPC 440 cores */
#if 0
    CPU_POWERPC_440A4              = xxx,
#endif
#if 0
    CPU_POWERPC_440A5              = xxx,
#endif
#if 0
    CPU_POWERPC_440B4              = xxx,
#endif
#if 0
    CPU_POWERPC_440F5              = xxx,
#endif
#if 0
    CPU_POWERPC_440G5              = xxx,
#endif
#if 0
    CPU_POWERPC_440H4              = xxx,
#endif
#if 0
    CPU_POWERPC_440H6              = xxx,
#endif
    /* PowerPC 440 microcontrolers */
#define CPU_POWERPC_440EP            CPU_POWERPC_440EPb
    CPU_POWERPC_440EPa             = 0x42221850,
    CPU_POWERPC_440EPb             = 0x422218D3,
#define CPU_POWERPC_440GP            CPU_POWERPC_440GPc
    CPU_POWERPC_440GPb             = 0x40120440,
    CPU_POWERPC_440GPc             = 0x40120481,
#define CPU_POWERPC_440GR            CPU_POWERPC_440GRa
#define CPU_POWERPC_440GRa           CPU_POWERPC_440EPb
    CPU_POWERPC_440GRX             = 0x200008D0,
#define CPU_POWERPC_440EPX           CPU_POWERPC_440GRX
#define CPU_POWERPC_440GX            CPU_POWERPC_440GXf
    CPU_POWERPC_440GXa             = 0x51B21850,
    CPU_POWERPC_440GXb             = 0x51B21851,
    CPU_POWERPC_440GXc             = 0x51B21892,
    CPU_POWERPC_440GXf             = 0x51B21894,
#if 0
    CPU_POWERPC_440S               = xxx,
#endif
    CPU_POWERPC_440SP              = 0x53221850,
    CPU_POWERPC_440SP2             = 0x53221891,
    CPU_POWERPC_440SPE             = 0x53421890,
    /* PowerPC 460 family */
#if 0
    /* Generic PowerPC 464 */
#define CPU_POWERPC_464              CPU_POWERPC_464H90
#endif
    /* PowerPC 464 microcontrolers */
#if 0
    CPU_POWERPC_464H90             = xxx,
#endif
#if 0
    CPU_POWERPC_464H90FP           = xxx,
#endif
    /* Freescale embedded PowerPC cores */
    /* PowerPC MPC 5xx cores (aka RCPU) */
    CPU_POWERPC_MPC5xx             = 0x00020020,
#define CPU_POWERPC_MGT560           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC509           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC533           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC534           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC555           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC556           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC560           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC561           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC562           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC563           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC564           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC565           CPU_POWERPC_MPC5xx
#define CPU_POWERPC_MPC566           CPU_POWERPC_MPC5xx
    /* PowerPC MPC 8xx cores (aka PowerQUICC) */
    CPU_POWERPC_MPC8xx             = 0x00500000,
#define CPU_POWERPC_MGT823           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC821           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC823           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC850           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC852T          CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC855T          CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC857           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC859           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC860           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC862           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC866           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC870           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC875           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC880           CPU_POWERPC_MPC8xx
#define CPU_POWERPC_MPC885           CPU_POWERPC_MPC8xx
    /* G2 cores (aka PowerQUICC-II) */
    CPU_POWERPC_G2                 = 0x00810011,
    CPU_POWERPC_G2H4               = 0x80811010,
    CPU_POWERPC_G2gp               = 0x80821010,
    CPU_POWERPC_G2ls               = 0x90810010,
    CPU_POWERPC_MPC603             = 0x00810100,
    CPU_POWERPC_G2_HIP3            = 0x00810101,
    CPU_POWERPC_G2_HIP4            = 0x80811014,
    /*   G2_LE core (aka PowerQUICC-II) */
    CPU_POWERPC_G2LE               = 0x80820010,
    CPU_POWERPC_G2LEgp             = 0x80822010,
    CPU_POWERPC_G2LEls             = 0xA0822010,
    CPU_POWERPC_G2LEgp1            = 0x80822011,
    CPU_POWERPC_G2LEgp3            = 0x80822013,
    /* MPC52xx microcontrollers  */
    /* XXX: MPC 5121 ? */
#define CPU_POWERPC_MPC52xx          CPU_POWERPC_MPC5200
#define CPU_POWERPC_MPC5200          CPU_POWERPC_MPC5200_v12
#define CPU_POWERPC_MPC5200_v10      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200_v11      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200_v12      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200B         CPU_POWERPC_MPC5200B_v21
#define CPU_POWERPC_MPC5200B_v20     CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200B_v21     CPU_POWERPC_G2LEgp1
    /* MPC82xx microcontrollers */
#define CPU_POWERPC_MPC82xx          CPU_POWERPC_MPC8280
#define CPU_POWERPC_MPC8240          CPU_POWERPC_MPC603
#define CPU_POWERPC_MPC8241          CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8245          CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8247          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8248          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8250          CPU_POWERPC_MPC8250_HiP4
#define CPU_POWERPC_MPC8250_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8250_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8255          CPU_POWERPC_MPC8255_HiP4
#define CPU_POWERPC_MPC8255_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8255_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8260          CPU_POWERPC_MPC8260_HiP4
#define CPU_POWERPC_MPC8260_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8260_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8264          CPU_POWERPC_MPC8264_HiP4
#define CPU_POWERPC_MPC8264_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8264_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8265          CPU_POWERPC_MPC8265_HiP4
#define CPU_POWERPC_MPC8265_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8265_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8266          CPU_POWERPC_MPC8266_HiP4
#define CPU_POWERPC_MPC8266_HiP3     CPU_POWERPC_G2_HIP3
#define CPU_POWERPC_MPC8266_HiP4     CPU_POWERPC_G2_HIP4
#define CPU_POWERPC_MPC8270          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8271          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8272          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8275          CPU_POWERPC_G2LEgp3
#define CPU_POWERPC_MPC8280          CPU_POWERPC_G2LEgp3
    /* e200 family */
    /* e200 cores */
#define CPU_POWERPC_e200             CPU_POWERPC_e200z6
#if 0
    CPU_POWERPC_e200z0             = xxx,
#endif
#if 0
    CPU_POWERPC_e200z1             = xxx,
#endif
#if 0 /* ? */
    CPU_POWERPC_e200z3             = 0x81120000,
#endif
    CPU_POWERPC_e200z5             = 0x81000000,
    CPU_POWERPC_e200z6             = 0x81120000,
    /* MPC55xx microcontrollers */
#define CPU_POWERPC_MPC55xx          CPU_POWERPC_MPC5567
#if 0
#define CPU_POWERPC_MPC5514E         CPU_POWERPC_MPC5514E_v1
#define CPU_POWERPC_MPC5514E_v0      CPU_POWERPC_e200z0
#define CPU_POWERPC_MPC5514E_v1      CPU_POWERPC_e200z1
#define CPU_POWERPC_MPC5514G         CPU_POWERPC_MPC5514G_v1
#define CPU_POWERPC_MPC5514G_v0      CPU_POWERPC_e200z0
#define CPU_POWERPC_MPC5514G_v1      CPU_POWERPC_e200z1
#define CPU_POWERPC_MPC5515S         CPU_POWERPC_e200z1
#define CPU_POWERPC_MPC5516E         CPU_POWERPC_MPC5516E_v1
#define CPU_POWERPC_MPC5516E_v0      CPU_POWERPC_e200z0
#define CPU_POWERPC_MPC5516E_v1      CPU_POWERPC_e200z1
#define CPU_POWERPC_MPC5516G         CPU_POWERPC_MPC5516G_v1
#define CPU_POWERPC_MPC5516G_v0      CPU_POWERPC_e200z0
#define CPU_POWERPC_MPC5516G_v1      CPU_POWERPC_e200z1
#define CPU_POWERPC_MPC5516S         CPU_POWERPC_e200z1
#endif
#if 0
#define CPU_POWERPC_MPC5533          CPU_POWERPC_e200z3
#define CPU_POWERPC_MPC5534          CPU_POWERPC_e200z3
#endif
#define CPU_POWERPC_MPC5553          CPU_POWERPC_e200z6
#define CPU_POWERPC_MPC5554          CPU_POWERPC_e200z6
#define CPU_POWERPC_MPC5561          CPU_POWERPC_e200z6
#define CPU_POWERPC_MPC5565          CPU_POWERPC_e200z6
#define CPU_POWERPC_MPC5566          CPU_POWERPC_e200z6
#define CPU_POWERPC_MPC5567          CPU_POWERPC_e200z6
    /* e300 family */
    /* e300 cores */
#define CPU_POWERPC_e300             CPU_POWERPC_e300c3
    CPU_POWERPC_e300c1             = 0x00830010,
    CPU_POWERPC_e300c2             = 0x00840010,
    CPU_POWERPC_e300c3             = 0x00850010,
    CPU_POWERPC_e300c4             = 0x00860010,
    /* MPC83xx microcontrollers */
#define CPU_POWERPC_MPC8313          CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8313E         CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8314          CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8314E         CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8315          CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8315E         CPU_POWERPC_e300c3
#define CPU_POWERPC_MPC8321          CPU_POWERPC_e300c2
#define CPU_POWERPC_MPC8321E         CPU_POWERPC_e300c2
#define CPU_POWERPC_MPC8323          CPU_POWERPC_e300c2
#define CPU_POWERPC_MPC8323E         CPU_POWERPC_e300c2
#define CPU_POWERPC_MPC8343A         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8343EA        CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347A         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347AT        CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347AP        CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347EA        CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347EAT       CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8347EAP       CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8349          CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8349A         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8349E         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8349EA        CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8358E         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8360E         CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC8377          CPU_POWERPC_e300c4
#define CPU_POWERPC_MPC8377E         CPU_POWERPC_e300c4
#define CPU_POWERPC_MPC8378          CPU_POWERPC_e300c4
#define CPU_POWERPC_MPC8378E         CPU_POWERPC_e300c4
#define CPU_POWERPC_MPC8379          CPU_POWERPC_e300c4
#define CPU_POWERPC_MPC8379E         CPU_POWERPC_e300c4
    /* e500 family */
    /* e500 cores  */
#define CPU_POWERPC_e500             CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_e500v2           CPU_POWERPC_e500v2_v22
    CPU_POWERPC_e500_v10           = 0x80200010,
    CPU_POWERPC_e500_v20           = 0x80200020,
    CPU_POWERPC_e500v2_v10         = 0x80210010,
    CPU_POWERPC_e500v2_v11         = 0x80210011,
    CPU_POWERPC_e500v2_v20         = 0x80210020,
    CPU_POWERPC_e500v2_v21         = 0x80210021,
    CPU_POWERPC_e500v2_v22         = 0x80210022,
    CPU_POWERPC_e500v2_v30         = 0x80210030,
    /* MPC85xx microcontrollers */
#define CPU_POWERPC_MPC8533          CPU_POWERPC_MPC8533_v11
#define CPU_POWERPC_MPC8533_v10      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8533_v11      CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8533E         CPU_POWERPC_MPC8533E_v11
#define CPU_POWERPC_MPC8533E_v10     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8533E_v11     CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8540          CPU_POWERPC_MPC8540_v21
#define CPU_POWERPC_MPC8540_v10      CPU_POWERPC_e500_v10
#define CPU_POWERPC_MPC8540_v20      CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8540_v21      CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8541          CPU_POWERPC_MPC8541_v11
#define CPU_POWERPC_MPC8541_v10      CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8541_v11      CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8541E         CPU_POWERPC_MPC8541E_v11
#define CPU_POWERPC_MPC8541E_v10     CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8541E_v11     CPU_POWERPC_e500_v20
#define CPU_POWERPC_MPC8543          CPU_POWERPC_MPC8543_v21
#define CPU_POWERPC_MPC8543_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8543_v11      CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8543_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8543_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8543E         CPU_POWERPC_MPC8543E_v21
#define CPU_POWERPC_MPC8543E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8543E_v11     CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8543E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8543E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8544          CPU_POWERPC_MPC8544_v11
#define CPU_POWERPC_MPC8544_v10      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8544_v11      CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8544E_v11     CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8544E         CPU_POWERPC_MPC8544E_v11
#define CPU_POWERPC_MPC8544E_v10     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8545          CPU_POWERPC_MPC8545_v21
#define CPU_POWERPC_MPC8545_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8545_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8545_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8545E         CPU_POWERPC_MPC8545E_v21
#define CPU_POWERPC_MPC8545E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8545E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8545E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8547E         CPU_POWERPC_MPC8545E_v21
#define CPU_POWERPC_MPC8547E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8547E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8547E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8548          CPU_POWERPC_MPC8548_v21
#define CPU_POWERPC_MPC8548_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8548_v11      CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8548_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8548_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8548E         CPU_POWERPC_MPC8548E_v21
#define CPU_POWERPC_MPC8548E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8548E_v11     CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8548E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8548E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8555          CPU_POWERPC_MPC8555_v11
#define CPU_POWERPC_MPC8555_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8555_v11      CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8555E         CPU_POWERPC_MPC8555E_v11
#define CPU_POWERPC_MPC8555E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8555E_v11     CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8560          CPU_POWERPC_MPC8560_v21
#define CPU_POWERPC_MPC8560_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8560_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8560_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8567          CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8567E         CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8568          CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8568E         CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8572          CPU_POWERPC_e500v2_v30
#define CPU_POWERPC_MPC8572E         CPU_POWERPC_e500v2_v30
    /* e600 family */
    /* e600 cores */
    CPU_POWERPC_e600               = 0x80040010,
    /* MPC86xx microcontrollers */
#define CPU_POWERPC_MPC8610          CPU_POWERPC_e600
#define CPU_POWERPC_MPC8641          CPU_POWERPC_e600
#define CPU_POWERPC_MPC8641D         CPU_POWERPC_e600
    /* PowerPC 6xx cores */
#define CPU_POWERPC_601              CPU_POWERPC_601_v2
    CPU_POWERPC_601_v0             = 0x00010001,
    CPU_POWERPC_601_v1             = 0x00010001,
#define CPU_POWERPC_601v             CPU_POWERPC_601_v2
    CPU_POWERPC_601_v2             = 0x00010002,
    CPU_POWERPC_602                = 0x00050100,
    CPU_POWERPC_603                = 0x00030100,
#define CPU_POWERPC_603E             CPU_POWERPC_603E_v41
    CPU_POWERPC_603E_v11           = 0x00060101,
    CPU_POWERPC_603E_v12           = 0x00060102,
    CPU_POWERPC_603E_v13           = 0x00060103,
    CPU_POWERPC_603E_v14           = 0x00060104,
    CPU_POWERPC_603E_v22           = 0x00060202,
    CPU_POWERPC_603E_v3            = 0x00060300,
    CPU_POWERPC_603E_v4            = 0x00060400,
    CPU_POWERPC_603E_v41           = 0x00060401,
    CPU_POWERPC_603E7t             = 0x00071201,
    CPU_POWERPC_603E7v             = 0x00070100,
    CPU_POWERPC_603E7v1            = 0x00070101,
    CPU_POWERPC_603E7v2            = 0x00070201,
    CPU_POWERPC_603E7              = 0x00070200,
    CPU_POWERPC_603P               = 0x00070000,
#define CPU_POWERPC_603R             CPU_POWERPC_603E7t
    /* XXX: missing 0x00040303 (604) */
    CPU_POWERPC_604                = 0x00040103,
#define CPU_POWERPC_604E             CPU_POWERPC_604E_v24
    /* XXX: missing 0x00091203 */
    /* XXX: missing 0x00092110 */
    /* XXX: missing 0x00092120 */
    CPU_POWERPC_604E_v10           = 0x00090100,
    CPU_POWERPC_604E_v22           = 0x00090202,
    CPU_POWERPC_604E_v24           = 0x00090204,
    /* XXX: missing 0x000a0100 */
    /* XXX: missing 0x00093102 */
    CPU_POWERPC_604R               = 0x000a0101,
#if 0
    CPU_POWERPC_604EV              = xxx, /* XXX: same as 604R ? */
#endif
    /* PowerPC 740/750 cores (aka G3) */
    /* XXX: missing 0x00084202 */
#define CPU_POWERPC_7x0              CPU_POWERPC_7x0_v31
    CPU_POWERPC_7x0_v10            = 0x00080100,
    CPU_POWERPC_7x0_v20            = 0x00080200,
    CPU_POWERPC_7x0_v21            = 0x00080201,
    CPU_POWERPC_7x0_v22            = 0x00080202,
    CPU_POWERPC_7x0_v30            = 0x00080300,
    CPU_POWERPC_7x0_v31            = 0x00080301,
    CPU_POWERPC_740E               = 0x00080100,
    CPU_POWERPC_750E               = 0x00080200,
    CPU_POWERPC_7x0P               = 0x10080000,
    /* XXX: missing 0x00087010 (CL ?) */
#define CPU_POWERPC_750CL            CPU_POWERPC_750CL_v20
    CPU_POWERPC_750CL_v10          = 0x00087200,
    CPU_POWERPC_750CL_v20          = 0x00087210, /* aka rev E */
#define CPU_POWERPC_750CX            CPU_POWERPC_750CX_v22
    CPU_POWERPC_750CX_v10          = 0x00082100,
    CPU_POWERPC_750CX_v20          = 0x00082200,
    CPU_POWERPC_750CX_v21          = 0x00082201,
    CPU_POWERPC_750CX_v22          = 0x00082202,
#define CPU_POWERPC_750CXE           CPU_POWERPC_750CXE_v31b
    CPU_POWERPC_750CXE_v21         = 0x00082211,
    CPU_POWERPC_750CXE_v22         = 0x00082212,
    CPU_POWERPC_750CXE_v23         = 0x00082213,
    CPU_POWERPC_750CXE_v24         = 0x00082214,
    CPU_POWERPC_750CXE_v24b        = 0x00083214,
    CPU_POWERPC_750CXE_v30         = 0x00082310,
    CPU_POWERPC_750CXE_v31         = 0x00082311,
    CPU_POWERPC_750CXE_v31b        = 0x00083311,
    CPU_POWERPC_750CXR             = 0x00083410,
    CPU_POWERPC_750FL              = 0x70000203,
#define CPU_POWERPC_750FX            CPU_POWERPC_750FX_v23
    CPU_POWERPC_750FX_v10          = 0x70000100,
    CPU_POWERPC_750FX_v20          = 0x70000200,
    CPU_POWERPC_750FX_v21          = 0x70000201,
    CPU_POWERPC_750FX_v22          = 0x70000202,
    CPU_POWERPC_750FX_v23          = 0x70000203,
    CPU_POWERPC_750GL              = 0x70020102,
#define CPU_POWERPC_750GX            CPU_POWERPC_750GX_v12
    CPU_POWERPC_750GX_v10          = 0x70020100,
    CPU_POWERPC_750GX_v11          = 0x70020101,
    CPU_POWERPC_750GX_v12          = 0x70020102,
#define CPU_POWERPC_750L             CPU_POWERPC_750L_v32 /* Aka LoneStar */
    CPU_POWERPC_750L_v20           = 0x00088200,
    CPU_POWERPC_750L_v21           = 0x00088201,
    CPU_POWERPC_750L_v22           = 0x00088202,
    CPU_POWERPC_750L_v30           = 0x00088300,
    CPU_POWERPC_750L_v32           = 0x00088302,
    /* PowerPC 745/755 cores */
#define CPU_POWERPC_7x5              CPU_POWERPC_7x5_v28
    CPU_POWERPC_7x5_v10            = 0x00083100,
    CPU_POWERPC_7x5_v11            = 0x00083101,
    CPU_POWERPC_7x5_v20            = 0x00083200,
    CPU_POWERPC_7x5_v21            = 0x00083201,
    CPU_POWERPC_7x5_v22            = 0x00083202, /* aka D */
    CPU_POWERPC_7x5_v23            = 0x00083203, /* aka E */
    CPU_POWERPC_7x5_v24            = 0x00083204,
    CPU_POWERPC_7x5_v25            = 0x00083205,
    CPU_POWERPC_7x5_v26            = 0x00083206,
    CPU_POWERPC_7x5_v27            = 0x00083207,
    CPU_POWERPC_7x5_v28            = 0x00083208,
#if 0
    CPU_POWERPC_7x5P               = xxx,
#endif
    /* PowerPC 74xx cores (aka G4) */
    /* XXX: missing 0x000C1101 */
#define CPU_POWERPC_7400             CPU_POWERPC_7400_v29
    CPU_POWERPC_7400_v10           = 0x000C0100,
    CPU_POWERPC_7400_v11           = 0x000C0101,
    CPU_POWERPC_7400_v20           = 0x000C0200,
    CPU_POWERPC_7400_v21           = 0x000C0201,
    CPU_POWERPC_7400_v22           = 0x000C0202,
    CPU_POWERPC_7400_v26           = 0x000C0206,
    CPU_POWERPC_7400_v27           = 0x000C0207,
    CPU_POWERPC_7400_v28           = 0x000C0208,
    CPU_POWERPC_7400_v29           = 0x000C0209,
#define CPU_POWERPC_7410             CPU_POWERPC_7410_v14
    CPU_POWERPC_7410_v10           = 0x800C1100,
    CPU_POWERPC_7410_v11           = 0x800C1101,
    CPU_POWERPC_7410_v12           = 0x800C1102, /* aka C */
    CPU_POWERPC_7410_v13           = 0x800C1103, /* aka D */
    CPU_POWERPC_7410_v14           = 0x800C1104, /* aka E */
#define CPU_POWERPC_7448             CPU_POWERPC_7448_v21
    CPU_POWERPC_7448_v10           = 0x80040100,
    CPU_POWERPC_7448_v11           = 0x80040101,
    CPU_POWERPC_7448_v20           = 0x80040200,
    CPU_POWERPC_7448_v21           = 0x80040201,
#define CPU_POWERPC_7450             CPU_POWERPC_7450_v21
    CPU_POWERPC_7450_v10           = 0x80000100,
    CPU_POWERPC_7450_v11           = 0x80000101,
    CPU_POWERPC_7450_v12           = 0x80000102,
    CPU_POWERPC_7450_v20           = 0x80000200, /* aka A, B, C, D: 2.04 */
    CPU_POWERPC_7450_v21           = 0x80000201, /* aka E */
#define CPU_POWERPC_74x1             CPU_POWERPC_74x1_v23
    CPU_POWERPC_74x1_v23           = 0x80000203, /* aka G: 2.3 */
    /* XXX: this entry might be a bug in some documentation */
    CPU_POWERPC_74x1_v210          = 0x80000210, /* aka G: 2.3 ? */
#define CPU_POWERPC_74x5             CPU_POWERPC_74x5_v32
    CPU_POWERPC_74x5_v10           = 0x80010100,
    /* XXX: missing 0x80010200 */
    CPU_POWERPC_74x5_v21           = 0x80010201, /* aka C: 2.1 */
    CPU_POWERPC_74x5_v32           = 0x80010302,
    CPU_POWERPC_74x5_v33           = 0x80010303, /* aka F: 3.3 */
    CPU_POWERPC_74x5_v34           = 0x80010304, /* aka G: 3.4 */
#define CPU_POWERPC_74x7             CPU_POWERPC_74x7_v12
    CPU_POWERPC_74x7_v10           = 0x80020100, /* aka A: 1.0 */
    CPU_POWERPC_74x7_v11           = 0x80020101, /* aka B: 1.1 */
    CPU_POWERPC_74x7_v12           = 0x80020102, /* aka C: 1.2 */
#define CPU_POWERPC_74x7A            CPU_POWERPC_74x7A_v12
    CPU_POWERPC_74x7A_v10          = 0x80030100, /* aka A: 1.0 */
    CPU_POWERPC_74x7A_v11          = 0x80030101, /* aka B: 1.1 */
    CPU_POWERPC_74x7A_v12          = 0x80030102, /* aka C: 1.2 */
    /* 64 bits PowerPC */
#if defined(TARGET_PPC64)
    CPU_POWERPC_620                = 0x00140000,
    CPU_POWERPC_630                = 0x00400000,
    CPU_POWERPC_631                = 0x00410104,
    CPU_POWERPC_POWER4             = 0x00350000,
    CPU_POWERPC_POWER4P            = 0x00380000,
     /* XXX: missing 0x003A0201 */
    CPU_POWERPC_POWER5             = 0x003A0203,
#define CPU_POWERPC_POWER5GR         CPU_POWERPC_POWER5
    CPU_POWERPC_POWER5P            = 0x003B0000,
#define CPU_POWERPC_POWER5GS         CPU_POWERPC_POWER5P
    CPU_POWERPC_POWER6             = 0x003E0000,
    CPU_POWERPC_POWER6_5           = 0x0F000001, /* POWER6 in POWER5 mode */
    CPU_POWERPC_POWER6A            = 0x0F000002,
    CPU_POWERPC_970                = 0x00390202,
#define CPU_POWERPC_970FX            CPU_POWERPC_970FX_v31
    CPU_POWERPC_970FX_v10          = 0x00391100,
    CPU_POWERPC_970FX_v20          = 0x003C0200,
    CPU_POWERPC_970FX_v21          = 0x003C0201,
    CPU_POWERPC_970FX_v30          = 0x003C0300,
    CPU_POWERPC_970FX_v31          = 0x003C0301,
    CPU_POWERPC_970GX              = 0x00450000,
#define CPU_POWERPC_970MP            CPU_POWERPC_970MP_v11
    CPU_POWERPC_970MP_v10          = 0x00440100,
    CPU_POWERPC_970MP_v11          = 0x00440101,
#define CPU_POWERPC_CELL             CPU_POWERPC_CELL_v32
    CPU_POWERPC_CELL_v10           = 0x00700100,
    CPU_POWERPC_CELL_v20           = 0x00700400,
    CPU_POWERPC_CELL_v30           = 0x00700500,
    CPU_POWERPC_CELL_v31           = 0x00700501,
#define CPU_POWERPC_CELL_v32         CPU_POWERPC_CELL_v31
    CPU_POWERPC_RS64               = 0x00330000,
    CPU_POWERPC_RS64II             = 0x00340000,
    CPU_POWERPC_RS64III            = 0x00360000,
    CPU_POWERPC_RS64IV             = 0x00370000,
#endif /* defined(TARGET_PPC64) */
    /* Original POWER */
    /* XXX: should be POWER (RIOS), RSC3308, RSC4608,
     * POWER2 (RIOS2) & RSC2 (P2SC) here
     */
#if 0
    CPU_POWER                      = xxx, /* 0x20000 ? 0x30000 for RSC ? */
#endif
#if 0
    CPU_POWER2                     = xxx, /* 0x40000 ? */
#endif
    /* PA Semi core */
    CPU_POWERPC_PA6T               = 0x00900000,
};

/* System version register (used on MPC 8xxx)                                */
enum {
    POWERPC_SVR_NONE               = 0x00000000,
#define POWERPC_SVR_52xx             POWERPC_SVR_5200
#define POWERPC_SVR_5200             POWERPC_SVR_5200_v12
    POWERPC_SVR_5200_v10           = 0x80110010,
    POWERPC_SVR_5200_v11           = 0x80110011,
    POWERPC_SVR_5200_v12           = 0x80110012,
#define POWERPC_SVR_5200B            POWERPC_SVR_5200B_v21
    POWERPC_SVR_5200B_v20          = 0x80110020,
    POWERPC_SVR_5200B_v21          = 0x80110021,
#define POWERPC_SVR_55xx             POWERPC_SVR_5567
#if 0
    POWERPC_SVR_5533               = xxx,
#endif
#if 0
    POWERPC_SVR_5534               = xxx,
#endif
#if 0
    POWERPC_SVR_5553               = xxx,
#endif
#if 0
    POWERPC_SVR_5554               = xxx,
#endif
#if 0
    POWERPC_SVR_5561               = xxx,
#endif
#if 0
    POWERPC_SVR_5565               = xxx,
#endif
#if 0
    POWERPC_SVR_5566               = xxx,
#endif
#if 0
    POWERPC_SVR_5567               = xxx,
#endif
#if 0
    POWERPC_SVR_8313               = xxx,
#endif
#if 0
    POWERPC_SVR_8313E              = xxx,
#endif
#if 0
    POWERPC_SVR_8314               = xxx,
#endif
#if 0
    POWERPC_SVR_8314E              = xxx,
#endif
#if 0
    POWERPC_SVR_8315               = xxx,
#endif
#if 0
    POWERPC_SVR_8315E              = xxx,
#endif
#if 0
    POWERPC_SVR_8321               = xxx,
#endif
#if 0
    POWERPC_SVR_8321E              = xxx,
#endif
#if 0
    POWERPC_SVR_8323               = xxx,
#endif
#if 0
    POWERPC_SVR_8323E              = xxx,
#endif
    POWERPC_SVR_8343A              = 0x80570030,
    POWERPC_SVR_8343EA             = 0x80560030,
#define POWERPC_SVR_8347A            POWERPC_SVR_8347AT
    POWERPC_SVR_8347AP             = 0x80550030, /* PBGA package */
    POWERPC_SVR_8347AT             = 0x80530030, /* TBGA package */
#define POWERPC_SVR_8347EA            POWERPC_SVR_8347EAT
    POWERPC_SVR_8347EAP            = 0x80540030, /* PBGA package */
    POWERPC_SVR_8347EAT            = 0x80520030, /* TBGA package */
    POWERPC_SVR_8349               = 0x80510010,
    POWERPC_SVR_8349A              = 0x80510030,
    POWERPC_SVR_8349E              = 0x80500010,
    POWERPC_SVR_8349EA             = 0x80500030,
#if 0
    POWERPC_SVR_8358E              = xxx,
#endif
#if 0
    POWERPC_SVR_8360E              = xxx,
#endif
#define POWERPC_SVR_E500             0x40000000
    POWERPC_SVR_8377               = 0x80C70010 | POWERPC_SVR_E500,
    POWERPC_SVR_8377E              = 0x80C60010 | POWERPC_SVR_E500,
    POWERPC_SVR_8378               = 0x80C50010 | POWERPC_SVR_E500,
    POWERPC_SVR_8378E              = 0x80C40010 | POWERPC_SVR_E500,
    POWERPC_SVR_8379               = 0x80C30010 | POWERPC_SVR_E500,
    POWERPC_SVR_8379E              = 0x80C00010 | POWERPC_SVR_E500,
#define POWERPC_SVR_8533             POWERPC_SVR_8533_v11
    POWERPC_SVR_8533_v10           = 0x80340010 | POWERPC_SVR_E500,
    POWERPC_SVR_8533_v11           = 0x80340011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8533E            POWERPC_SVR_8533E_v11
    POWERPC_SVR_8533E_v10          = 0x803C0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8533E_v11          = 0x803C0011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8540             POWERPC_SVR_8540_v21
    POWERPC_SVR_8540_v10           = 0x80300010 | POWERPC_SVR_E500,
    POWERPC_SVR_8540_v20           = 0x80300020 | POWERPC_SVR_E500,
    POWERPC_SVR_8540_v21           = 0x80300021 | POWERPC_SVR_E500,
#define POWERPC_SVR_8541             POWERPC_SVR_8541_v11
    POWERPC_SVR_8541_v10           = 0x80720010 | POWERPC_SVR_E500,
    POWERPC_SVR_8541_v11           = 0x80720011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8541E            POWERPC_SVR_8541E_v11
    POWERPC_SVR_8541E_v10          = 0x807A0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8541E_v11          = 0x807A0011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8543             POWERPC_SVR_8543_v21
    POWERPC_SVR_8543_v10           = 0x80320010 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v11           = 0x80320011 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v20           = 0x80320020 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v21           = 0x80320021 | POWERPC_SVR_E500,
#define POWERPC_SVR_8543E            POWERPC_SVR_8543E_v21
    POWERPC_SVR_8543E_v10          = 0x803A0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v11          = 0x803A0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v20          = 0x803A0020 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v21          = 0x803A0021 | POWERPC_SVR_E500,
#define POWERPC_SVR_8544             POWERPC_SVR_8544_v11
    POWERPC_SVR_8544_v10           = 0x80340110 | POWERPC_SVR_E500,
    POWERPC_SVR_8544_v11           = 0x80340111 | POWERPC_SVR_E500,
#define POWERPC_SVR_8544E            POWERPC_SVR_8544E_v11
    POWERPC_SVR_8544E_v10          = 0x803C0110 | POWERPC_SVR_E500,
    POWERPC_SVR_8544E_v11          = 0x803C0111 | POWERPC_SVR_E500,
#define POWERPC_SVR_8545             POWERPC_SVR_8545_v21
    POWERPC_SVR_8545_v20           = 0x80310220 | POWERPC_SVR_E500,
    POWERPC_SVR_8545_v21           = 0x80310221 | POWERPC_SVR_E500,
#define POWERPC_SVR_8545E            POWERPC_SVR_8545E_v21
    POWERPC_SVR_8545E_v20          = 0x80390220 | POWERPC_SVR_E500,
    POWERPC_SVR_8545E_v21          = 0x80390221 | POWERPC_SVR_E500,
#define POWERPC_SVR_8547E            POWERPC_SVR_8547E_v21
    POWERPC_SVR_8547E_v20          = 0x80390120 | POWERPC_SVR_E500,
    POWERPC_SVR_8547E_v21          = 0x80390121 | POWERPC_SVR_E500,
#define POWERPC_SVR_8548             POWERPC_SVR_8548_v21
    POWERPC_SVR_8548_v10           = 0x80310010 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v11           = 0x80310011 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v20           = 0x80310020 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v21           = 0x80310021 | POWERPC_SVR_E500,
#define POWERPC_SVR_8548E            POWERPC_SVR_8548E_v21
    POWERPC_SVR_8548E_v10          = 0x80390010 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v11          = 0x80390011 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v20          = 0x80390020 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v21          = 0x80390021 | POWERPC_SVR_E500,
#define POWERPC_SVR_8555             POWERPC_SVR_8555_v11
    POWERPC_SVR_8555_v10           = 0x80710010 | POWERPC_SVR_E500,
    POWERPC_SVR_8555_v11           = 0x80710011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8555E            POWERPC_SVR_8555_v11
    POWERPC_SVR_8555E_v10          = 0x80790010 | POWERPC_SVR_E500,
    POWERPC_SVR_8555E_v11          = 0x80790011 | POWERPC_SVR_E500,
#define POWERPC_SVR_8560             POWERPC_SVR_8560_v21
    POWERPC_SVR_8560_v10           = 0x80700010 | POWERPC_SVR_E500,
    POWERPC_SVR_8560_v20           = 0x80700020 | POWERPC_SVR_E500,
    POWERPC_SVR_8560_v21           = 0x80700021 | POWERPC_SVR_E500,
    POWERPC_SVR_8567               = 0x80750111 | POWERPC_SVR_E500,
    POWERPC_SVR_8567E              = 0x807D0111 | POWERPC_SVR_E500,
    POWERPC_SVR_8568               = 0x80750011 | POWERPC_SVR_E500,
    POWERPC_SVR_8568E              = 0x807D0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8572               = 0x80E00010 | POWERPC_SVR_E500,
    POWERPC_SVR_8572E              = 0x80E80010 | POWERPC_SVR_E500,
#if 0
    POWERPC_SVR_8610               = xxx,
#endif
    POWERPC_SVR_8641               = 0x80900021,
    POWERPC_SVR_8641D              = 0x80900121,
};

/*****************************************************************************/
/* PowerPC CPU definitions                                                   */
#define POWERPC_DEF_SVR(_name, _pvr, _svr, _type)                             \
    {                                                                         \
        .name        = _name,                                                 \
        .pvr         = _pvr,                                                  \
        .svr         = _svr,                                                  \
        .insns_flags = glue(POWERPC_INSNS_,_type),                            \
        .msr_mask    = glue(POWERPC_MSRM_,_type),                             \
        .mmu_model   = glue(POWERPC_MMU_,_type),                              \
        .excp_model  = glue(POWERPC_EXCP_,_type),                             \
        .bus_model   = glue(POWERPC_INPUT_,_type),                            \
        .bfd_mach    = glue(POWERPC_BFDM_,_type),                             \
        .flags       = glue(POWERPC_FLAG_,_type),                             \
        .init_proc   = &glue(init_proc_,_type),                               \
        .check_pow   = &glue(check_pow_,_type),                               \
    }
#define POWERPC_DEF(_name, _pvr, _type)                                       \
POWERPC_DEF_SVR(_name, _pvr, POWERPC_SVR_NONE, _type)

static const ppc_def_t ppc_defs[] = {
    /* Embedded PowerPC                                                      */
    /* PowerPC 401 family                                                    */
    /* Generic PowerPC 401 */
    POWERPC_DEF("401",           CPU_POWERPC_401,                    401),
    /* PowerPC 401 cores                                                     */
    /* PowerPC 401A1 */
    POWERPC_DEF("401A1",         CPU_POWERPC_401A1,                  401),
    /* PowerPC 401B2                                                         */
    POWERPC_DEF("401B2",         CPU_POWERPC_401B2,                  401x2),
#if defined (TODO)
    /* PowerPC 401B3                                                         */
    POWERPC_DEF("401B3",         CPU_POWERPC_401B3,                  401x3),
#endif
    /* PowerPC 401C2                                                         */
    POWERPC_DEF("401C2",         CPU_POWERPC_401C2,                  401x2),
    /* PowerPC 401D2                                                         */
    POWERPC_DEF("401D2",         CPU_POWERPC_401D2,                  401x2),
    /* PowerPC 401E2                                                         */
    POWERPC_DEF("401E2",         CPU_POWERPC_401E2,                  401x2),
    /* PowerPC 401F2                                                         */
    POWERPC_DEF("401F2",         CPU_POWERPC_401F2,                  401x2),
    /* PowerPC 401G2                                                         */
    /* XXX: to be checked */
    POWERPC_DEF("401G2",         CPU_POWERPC_401G2,                  401x2),
    /* PowerPC 401 microcontrolers                                           */
#if defined (TODO)
    /* PowerPC 401GF                                                         */
    POWERPC_DEF("401GF",         CPU_POWERPC_401GF,                  401),
#endif
    /* IOP480 (401 microcontroler)                                           */
    POWERPC_DEF("IOP480",        CPU_POWERPC_IOP480,                 IOP480),
    /* IBM Processor for Network Resources                                   */
    POWERPC_DEF("Cobra",         CPU_POWERPC_COBRA,                  401),
#if defined (TODO)
    POWERPC_DEF("Xipchip",       CPU_POWERPC_XIPCHIP,                401),
#endif
    /* PowerPC 403 family                                                    */
    /* Generic PowerPC 403                                                   */
    POWERPC_DEF("403",           CPU_POWERPC_403,                    403),
    /* PowerPC 403 microcontrolers                                           */
    /* PowerPC 403 GA                                                        */
    POWERPC_DEF("403GA",         CPU_POWERPC_403GA,                  403),
    /* PowerPC 403 GB                                                        */
    POWERPC_DEF("403GB",         CPU_POWERPC_403GB,                  403),
    /* PowerPC 403 GC                                                        */
    POWERPC_DEF("403GC",         CPU_POWERPC_403GC,                  403),
    /* PowerPC 403 GCX                                                       */
    POWERPC_DEF("403GCX",        CPU_POWERPC_403GCX,                 403GCX),
#if defined (TODO)
    /* PowerPC 403 GP                                                        */
    POWERPC_DEF("403GP",         CPU_POWERPC_403GP,                  403),
#endif
    /* PowerPC 405 family                                                    */
    /* Generic PowerPC 405                                                   */
    POWERPC_DEF("405",           CPU_POWERPC_405,                    405),
    /* PowerPC 405 cores                                                     */
#if defined (TODO)
    /* PowerPC 405 A3                                                        */
    POWERPC_DEF("405A3",         CPU_POWERPC_405A3,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 A4                                                        */
    POWERPC_DEF("405A4",         CPU_POWERPC_405A4,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 B3                                                        */
    POWERPC_DEF("405B3",         CPU_POWERPC_405B3,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 B4                                                        */
    POWERPC_DEF("405B4",         CPU_POWERPC_405B4,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 C3                                                        */
    POWERPC_DEF("405C3",         CPU_POWERPC_405C3,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 C4                                                        */
    POWERPC_DEF("405C4",         CPU_POWERPC_405C4,                  405),
#endif
    /* PowerPC 405 D2                                                        */
    POWERPC_DEF("405D2",         CPU_POWERPC_405D2,                  405),
#if defined (TODO)
    /* PowerPC 405 D3                                                        */
    POWERPC_DEF("405D3",         CPU_POWERPC_405D3,                  405),
#endif
    /* PowerPC 405 D4                                                        */
    POWERPC_DEF("405D4",         CPU_POWERPC_405D4,                  405),
#if defined (TODO)
    /* PowerPC 405 D5                                                        */
    POWERPC_DEF("405D5",         CPU_POWERPC_405D5,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 E4                                                        */
    POWERPC_DEF("405E4",         CPU_POWERPC_405E4,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 F4                                                        */
    POWERPC_DEF("405F4",         CPU_POWERPC_405F4,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 F5                                                        */
    POWERPC_DEF("405F5",         CPU_POWERPC_405F5,                  405),
#endif
#if defined (TODO)
    /* PowerPC 405 F6                                                        */
    POWERPC_DEF("405F6",         CPU_POWERPC_405F6,                  405),
#endif
    /* PowerPC 405 microcontrolers                                           */
    /* PowerPC 405 CR                                                        */
    POWERPC_DEF("405CR",         CPU_POWERPC_405CR,                  405),
    /* PowerPC 405 CRa                                                       */
    POWERPC_DEF("405CRa",        CPU_POWERPC_405CRa,                 405),
    /* PowerPC 405 CRb                                                       */
    POWERPC_DEF("405CRb",        CPU_POWERPC_405CRb,                 405),
    /* PowerPC 405 CRc                                                       */
    POWERPC_DEF("405CRc",        CPU_POWERPC_405CRc,                 405),
    /* PowerPC 405 EP                                                        */
    POWERPC_DEF("405EP",         CPU_POWERPC_405EP,                  405),
#if defined(TODO)
    /* PowerPC 405 EXr                                                       */
    POWERPC_DEF("405EXr",        CPU_POWERPC_405EXr,                 405),
#endif
    /* PowerPC 405 EZ                                                        */
    POWERPC_DEF("405EZ",         CPU_POWERPC_405EZ,                  405),
#if defined(TODO)
    /* PowerPC 405 FX                                                        */
    POWERPC_DEF("405FX",         CPU_POWERPC_405FX,                  405),
#endif
    /* PowerPC 405 GP                                                        */
    POWERPC_DEF("405GP",         CPU_POWERPC_405GP,                  405),
    /* PowerPC 405 GPa                                                       */
    POWERPC_DEF("405GPa",        CPU_POWERPC_405GPa,                 405),
    /* PowerPC 405 GPb                                                       */
    POWERPC_DEF("405GPb",        CPU_POWERPC_405GPb,                 405),
    /* PowerPC 405 GPc                                                       */
    POWERPC_DEF("405GPc",        CPU_POWERPC_405GPc,                 405),
    /* PowerPC 405 GPd                                                       */
    POWERPC_DEF("405GPd",        CPU_POWERPC_405GPd,                 405),
    /* PowerPC 405 GPe                                                       */
    POWERPC_DEF("405GPe",        CPU_POWERPC_405GPe,                 405),
    /* PowerPC 405 GPR                                                       */
    POWERPC_DEF("405GPR",        CPU_POWERPC_405GPR,                 405),
#if defined(TODO)
    /* PowerPC 405 H                                                         */
    POWERPC_DEF("405H",          CPU_POWERPC_405H,                   405),
#endif
#if defined(TODO)
    /* PowerPC 405 L                                                         */
    POWERPC_DEF("405L",          CPU_POWERPC_405L,                   405),
#endif
    /* PowerPC 405 LP                                                        */
    POWERPC_DEF("405LP",         CPU_POWERPC_405LP,                  405),
#if defined(TODO)
    /* PowerPC 405 PM                                                        */
    POWERPC_DEF("405PM",         CPU_POWERPC_405PM,                  405),
#endif
#if defined(TODO)
    /* PowerPC 405 PS                                                        */
    POWERPC_DEF("405PS",         CPU_POWERPC_405PS,                  405),
#endif
#if defined(TODO)
    /* PowerPC 405 S                                                         */
    POWERPC_DEF("405S",          CPU_POWERPC_405S,                   405),
#endif
    /* Npe405 H                                                              */
    POWERPC_DEF("Npe405H",       CPU_POWERPC_NPE405H,                405),
    /* Npe405 H2                                                             */
    POWERPC_DEF("Npe405H2",      CPU_POWERPC_NPE405H2,               405),
    /* Npe405 L                                                              */
    POWERPC_DEF("Npe405L",       CPU_POWERPC_NPE405L,                405),
    /* Npe4GS3                                                               */
    POWERPC_DEF("Npe4GS3",       CPU_POWERPC_NPE4GS3,                405),
#if defined (TODO)
    POWERPC_DEF("Npcxx1",        CPU_POWERPC_NPCxx1,                 405),
#endif
#if defined (TODO)
    POWERPC_DEF("Npr161",        CPU_POWERPC_NPR161,                 405),
#endif
#if defined (TODO)
    /* PowerPC LC77700 (Sanyo)                                               */
    POWERPC_DEF("LC77700",       CPU_POWERPC_LC77700,                405),
#endif
    /* PowerPC 401/403/405 based set-top-box microcontrolers                 */
#if defined (TODO)
    /* STB010000                                                             */
    POWERPC_DEF("STB01000",      CPU_POWERPC_STB01000,               401x2),
#endif
#if defined (TODO)
    /* STB01010                                                              */
    POWERPC_DEF("STB01010",      CPU_POWERPC_STB01010,               401x2),
#endif
#if defined (TODO)
    /* STB0210                                                               */
    POWERPC_DEF("STB0210",       CPU_POWERPC_STB0210,                401x3),
#endif
    /* STB03xx                                                               */
    POWERPC_DEF("STB03",         CPU_POWERPC_STB03,                  405),
#if defined (TODO)
    /* STB043x                                                               */
    POWERPC_DEF("STB043",        CPU_POWERPC_STB043,                 405),
#endif
#if defined (TODO)
    /* STB045x                                                               */
    POWERPC_DEF("STB045",        CPU_POWERPC_STB045,                 405),
#endif
    /* STB04xx                                                               */
    POWERPC_DEF("STB04",         CPU_POWERPC_STB04,                  405),
    /* STB25xx                                                               */
    POWERPC_DEF("STB25",         CPU_POWERPC_STB25,                  405),
#if defined (TODO)
    /* STB130                                                                */
    POWERPC_DEF("STB130",        CPU_POWERPC_STB130,                 405),
#endif
    /* Xilinx PowerPC 405 cores                                              */
    POWERPC_DEF("x2vp4",         CPU_POWERPC_X2VP4,                  405),
    POWERPC_DEF("x2vp7",         CPU_POWERPC_X2VP7,                  405),
    POWERPC_DEF("x2vp20",        CPU_POWERPC_X2VP20,                 405),
    POWERPC_DEF("x2vp50",        CPU_POWERPC_X2VP50,                 405),
#if defined (TODO)
    /* Zarlink ZL10310                                                       */
    POWERPC_DEF("zl10310",       CPU_POWERPC_ZL10310,                405),
#endif
#if defined (TODO)
    /* Zarlink ZL10311                                                       */
    POWERPC_DEF("zl10311",       CPU_POWERPC_ZL10311,                405),
#endif
#if defined (TODO)
    /* Zarlink ZL10320                                                       */
    POWERPC_DEF("zl10320",       CPU_POWERPC_ZL10320,                405),
#endif
#if defined (TODO)
    /* Zarlink ZL10321                                                       */
    POWERPC_DEF("zl10321",       CPU_POWERPC_ZL10321,                405),
#endif
    /* PowerPC 440 family                                                    */
#if defined(TODO_USER_ONLY)
    /* Generic PowerPC 440                                                   */
    POWERPC_DEF("440",           CPU_POWERPC_440,                    440GP),
#endif
    /* PowerPC 440 cores                                                     */
#if defined (TODO)
    /* PowerPC 440 A4                                                        */
    POWERPC_DEF("440A4",         CPU_POWERPC_440A4,                  440x4),
#endif
#if defined (TODO)
    /* PowerPC 440 A5                                                        */
    POWERPC_DEF("440A5",         CPU_POWERPC_440A5,                  440x5),
#endif
#if defined (TODO)
    /* PowerPC 440 B4                                                        */
    POWERPC_DEF("440B4",         CPU_POWERPC_440B4,                  440x4),
#endif
#if defined (TODO)
    /* PowerPC 440 G4                                                        */
    POWERPC_DEF("440G4",         CPU_POWERPC_440G4,                  440x4),
#endif
#if defined (TODO)
    /* PowerPC 440 F5                                                        */
    POWERPC_DEF("440F5",         CPU_POWERPC_440F5,                  440x5),
#endif
#if defined (TODO)
    /* PowerPC 440 G5                                                        */
    POWERPC_DEF("440G5",         CPU_POWERPC_440G5,                  440x5),
#endif
#if defined (TODO)
    /* PowerPC 440H4                                                         */
    POWERPC_DEF("440H4",         CPU_POWERPC_440H4,                  440x4),
#endif
#if defined (TODO)
    /* PowerPC 440H6                                                         */
    POWERPC_DEF("440H6",         CPU_POWERPC_440H6,                  440Gx5),
#endif
    /* PowerPC 440 microcontrolers                                           */
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 EP                                                        */
    POWERPC_DEF("440EP",         CPU_POWERPC_440EP,                  440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 EPa                                                       */
    POWERPC_DEF("440EPa",        CPU_POWERPC_440EPa,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 EPb                                                       */
    POWERPC_DEF("440EPb",        CPU_POWERPC_440EPb,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 EPX                                                       */
    POWERPC_DEF("440EPX",        CPU_POWERPC_440EPX,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GP                                                        */
    POWERPC_DEF("440GP",         CPU_POWERPC_440GP,                  440GP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GPb                                                       */
    POWERPC_DEF("440GPb",        CPU_POWERPC_440GPb,                 440GP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GPc                                                       */
    POWERPC_DEF("440GPc",        CPU_POWERPC_440GPc,                 440GP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GR                                                        */
    POWERPC_DEF("440GR",         CPU_POWERPC_440GR,                  440x5),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GRa                                                       */
    POWERPC_DEF("440GRa",        CPU_POWERPC_440GRa,                 440x5),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GRX                                                       */
    POWERPC_DEF("440GRX",        CPU_POWERPC_440GRX,                 440x5),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GX                                                        */
    POWERPC_DEF("440GX",         CPU_POWERPC_440GX,                  440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GXa                                                       */
    POWERPC_DEF("440GXa",        CPU_POWERPC_440GXa,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GXb                                                       */
    POWERPC_DEF("440GXb",        CPU_POWERPC_440GXb,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GXc                                                       */
    POWERPC_DEF("440GXc",        CPU_POWERPC_440GXc,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 GXf                                                       */
    POWERPC_DEF("440GXf",        CPU_POWERPC_440GXf,                 440EP),
#endif
#if defined(TODO)
    /* PowerPC 440 S                                                         */
    POWERPC_DEF("440S",          CPU_POWERPC_440S,                   440),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 SP                                                        */
    POWERPC_DEF("440SP",         CPU_POWERPC_440SP,                  440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 SP2                                                       */
    POWERPC_DEF("440SP2",        CPU_POWERPC_440SP2,                 440EP),
#endif
#if defined(TODO_USER_ONLY)
    /* PowerPC 440 SPE                                                       */
    POWERPC_DEF("440SPE",        CPU_POWERPC_440SPE,                 440EP),
#endif
    /* PowerPC 460 family                                                    */
#if defined (TODO)
    /* Generic PowerPC 464                                                   */
    POWERPC_DEF("464",           CPU_POWERPC_464,                    460),
#endif
    /* PowerPC 464 microcontrolers                                           */
#if defined (TODO)
    /* PowerPC 464H90                                                        */
    POWERPC_DEF("464H90",        CPU_POWERPC_464H90,                 460),
#endif
#if defined (TODO)
    /* PowerPC 464H90F                                                       */
    POWERPC_DEF("464H90F",       CPU_POWERPC_464H90F,                460F),
#endif
    /* Freescale embedded PowerPC cores                                      */
    /* MPC5xx family (aka RCPU)                                              */
#if defined(TODO_USER_ONLY)
    /* Generic MPC5xx core                                                   */
    POWERPC_DEF("MPC5xx",        CPU_POWERPC_MPC5xx,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* Codename for MPC5xx core                                              */
    POWERPC_DEF("RCPU",          CPU_POWERPC_MPC5xx,                 MPC5xx),
#endif
    /* MPC5xx microcontrollers                                               */
#if defined(TODO_USER_ONLY)
    /* MGT560                                                                */
    POWERPC_DEF("MGT560",        CPU_POWERPC_MGT560,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC509                                                                */
    POWERPC_DEF("MPC509",        CPU_POWERPC_MPC509,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC533                                                                */
    POWERPC_DEF("MPC533",        CPU_POWERPC_MPC533,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC534                                                                */
    POWERPC_DEF("MPC534",        CPU_POWERPC_MPC534,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC555                                                                */
    POWERPC_DEF("MPC555",        CPU_POWERPC_MPC555,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC556                                                                */
    POWERPC_DEF("MPC556",        CPU_POWERPC_MPC556,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC560                                                                */
    POWERPC_DEF("MPC560",        CPU_POWERPC_MPC560,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC561                                                                */
    POWERPC_DEF("MPC561",        CPU_POWERPC_MPC561,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC562                                                                */
    POWERPC_DEF("MPC562",        CPU_POWERPC_MPC562,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC563                                                                */
    POWERPC_DEF("MPC563",        CPU_POWERPC_MPC563,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC564                                                                */
    POWERPC_DEF("MPC564",        CPU_POWERPC_MPC564,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC565                                                                */
    POWERPC_DEF("MPC565",        CPU_POWERPC_MPC565,                 MPC5xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC566                                                                */
    POWERPC_DEF("MPC566",        CPU_POWERPC_MPC566,                 MPC5xx),
#endif
    /* MPC8xx family (aka PowerQUICC)                                        */
#if defined(TODO_USER_ONLY)
    /* Generic MPC8xx core                                                   */
    POWERPC_DEF("MPC8xx",        CPU_POWERPC_MPC8xx,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* Codename for MPC8xx core                                              */
    POWERPC_DEF("PowerQUICC",    CPU_POWERPC_MPC8xx,                 MPC8xx),
#endif
    /* MPC8xx microcontrollers                                               */
#if defined(TODO_USER_ONLY)
    /* MGT823                                                                */
    POWERPC_DEF("MGT823",        CPU_POWERPC_MGT823,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC821                                                                */
    POWERPC_DEF("MPC821",        CPU_POWERPC_MPC821,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC823                                                                */
    POWERPC_DEF("MPC823",        CPU_POWERPC_MPC823,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC850                                                                */
    POWERPC_DEF("MPC850",        CPU_POWERPC_MPC850,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC852T                                                               */
    POWERPC_DEF("MPC852T",       CPU_POWERPC_MPC852T,                MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC855T                                                               */
    POWERPC_DEF("MPC855T",       CPU_POWERPC_MPC855T,                MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC857                                                                */
    POWERPC_DEF("MPC857",        CPU_POWERPC_MPC857,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC859                                                                */
    POWERPC_DEF("MPC859",        CPU_POWERPC_MPC859,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC860                                                                */
    POWERPC_DEF("MPC860",        CPU_POWERPC_MPC860,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC862                                                                */
    POWERPC_DEF("MPC862",        CPU_POWERPC_MPC862,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC866                                                                */
    POWERPC_DEF("MPC866",        CPU_POWERPC_MPC866,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC870                                                                */
    POWERPC_DEF("MPC870",        CPU_POWERPC_MPC870,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC875                                                                */
    POWERPC_DEF("MPC875",        CPU_POWERPC_MPC875,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC880                                                                */
    POWERPC_DEF("MPC880",        CPU_POWERPC_MPC880,                 MPC8xx),
#endif
#if defined(TODO_USER_ONLY)
    /* MPC885                                                                */
    POWERPC_DEF("MPC885",        CPU_POWERPC_MPC885,                 MPC8xx),
#endif
    /* MPC82xx family (aka PowerQUICC-II)                                    */
    /* Generic MPC52xx core                                                  */
    POWERPC_DEF_SVR("MPC52xx",
                    CPU_POWERPC_MPC52xx,      POWERPC_SVR_52xx,      G2LE),
    /* Generic MPC82xx core                                                  */
    POWERPC_DEF("MPC82xx",       CPU_POWERPC_MPC82xx,                G2),
    /* Codename for MPC82xx                                                  */
    POWERPC_DEF("PowerQUICC-II", CPU_POWERPC_MPC82xx,                G2),
    /* PowerPC G2 core                                                       */
    POWERPC_DEF("G2",            CPU_POWERPC_G2,                     G2),
    /* PowerPC G2 H4 core                                                    */
    POWERPC_DEF("G2H4",          CPU_POWERPC_G2H4,                   G2),
    /* PowerPC G2 GP core                                                    */
    POWERPC_DEF("G2GP",          CPU_POWERPC_G2gp,                   G2),
    /* PowerPC G2 LS core                                                    */
    POWERPC_DEF("G2LS",          CPU_POWERPC_G2ls,                   G2),
    /* PowerPC G2 HiP3 core                                                  */
    POWERPC_DEF("G2HiP3",        CPU_POWERPC_G2_HIP3,                G2),
    /* PowerPC G2 HiP4 core                                                  */
    POWERPC_DEF("G2HiP4",        CPU_POWERPC_G2_HIP4,                G2),
    /* PowerPC MPC603 core                                                   */
    POWERPC_DEF("MPC603",        CPU_POWERPC_MPC603,                 603E),
    /* PowerPC G2le core (same as G2 plus little-endian mode support)        */
    POWERPC_DEF("G2le",          CPU_POWERPC_G2LE,                   G2LE),
    /* PowerPC G2LE GP core                                                  */
    POWERPC_DEF("G2leGP",        CPU_POWERPC_G2LEgp,                 G2LE),
    /* PowerPC G2LE LS core                                                  */
    POWERPC_DEF("G2leLS",        CPU_POWERPC_G2LEls,                 G2LE),
    /* PowerPC G2LE GP1 core                                                 */
    POWERPC_DEF("G2leGP1",       CPU_POWERPC_G2LEgp1,                G2LE),
    /* PowerPC G2LE GP3 core                                                 */
    POWERPC_DEF("G2leGP3",       CPU_POWERPC_G2LEgp1,                G2LE),
    /* PowerPC MPC603 microcontrollers                                       */
    /* MPC8240                                                               */
    POWERPC_DEF("MPC8240",       CPU_POWERPC_MPC8240,                603E),
    /* PowerPC G2 microcontrollers                                           */
#if defined(TODO)
    /* MPC5121                                                               */
    POWERPC_DEF_SVR("MPC5121",
                    CPU_POWERPC_MPC5121,      POWERPC_SVR_5121,      G2LE),
#endif
    /* MPC5200                                                               */
    POWERPC_DEF_SVR("MPC5200",
                    CPU_POWERPC_MPC5200,      POWERPC_SVR_5200,      G2LE),
    /* MPC5200 v1.0                                                          */
    POWERPC_DEF_SVR("MPC5200_v10",
                    CPU_POWERPC_MPC5200_v10,  POWERPC_SVR_5200_v10,  G2LE),
    /* MPC5200 v1.1                                                          */
    POWERPC_DEF_SVR("MPC5200_v11",
                    CPU_POWERPC_MPC5200_v11,  POWERPC_SVR_5200_v11,  G2LE),
    /* MPC5200 v1.2                                                          */
    POWERPC_DEF_SVR("MPC5200_v12",
                    CPU_POWERPC_MPC5200_v12,  POWERPC_SVR_5200_v12,  G2LE),
    /* MPC5200B                                                              */
    POWERPC_DEF_SVR("MPC5200B",
                    CPU_POWERPC_MPC5200B,     POWERPC_SVR_5200B,     G2LE),
    /* MPC5200B v2.0                                                         */
    POWERPC_DEF_SVR("MPC5200B_v20",
                    CPU_POWERPC_MPC5200B_v20, POWERPC_SVR_5200B_v20, G2LE),
    /* MPC5200B v2.1                                                         */
    POWERPC_DEF_SVR("MPC5200B_v21",
                    CPU_POWERPC_MPC5200B_v21, POWERPC_SVR_5200B_v21, G2LE),
    /* MPC8241                                                               */
    POWERPC_DEF("MPC8241",       CPU_POWERPC_MPC8241,                G2),
    /* MPC8245                                                               */
    POWERPC_DEF("MPC8245",       CPU_POWERPC_MPC8245,                G2),
    /* MPC8247                                                               */
    POWERPC_DEF("MPC8247",       CPU_POWERPC_MPC8247,                G2LE),
    /* MPC8248                                                               */
    POWERPC_DEF("MPC8248",       CPU_POWERPC_MPC8248,                G2LE),
    /* MPC8250                                                               */
    POWERPC_DEF("MPC8250",       CPU_POWERPC_MPC8250,                G2),
    /* MPC8250 HiP3                                                          */
    POWERPC_DEF("MPC8250_HiP3",  CPU_POWERPC_MPC8250_HiP3,           G2),
    /* MPC8250 HiP4                                                          */
    POWERPC_DEF("MPC8250_HiP4",  CPU_POWERPC_MPC8250_HiP4,           G2),
    /* MPC8255                                                               */
    POWERPC_DEF("MPC8255",       CPU_POWERPC_MPC8255,                G2),
    /* MPC8255 HiP3                                                          */
    POWERPC_DEF("MPC8255_HiP3",  CPU_POWERPC_MPC8255_HiP3,           G2),
    /* MPC8255 HiP4                                                          */
    POWERPC_DEF("MPC8255_HiP4",  CPU_POWERPC_MPC8255_HiP4,           G2),
    /* MPC8260                                                               */
    POWERPC_DEF("MPC8260",       CPU_POWERPC_MPC8260,                G2),
    /* MPC8260 HiP3                                                          */
    POWERPC_DEF("MPC8260_HiP3",  CPU_POWERPC_MPC8260_HiP3,           G2),
    /* MPC8260 HiP4                                                          */
    POWERPC_DEF("MPC8260_HiP4",  CPU_POWERPC_MPC8260_HiP4,           G2),
    /* MPC8264                                                               */
    POWERPC_DEF("MPC8264",       CPU_POWERPC_MPC8264,                G2),
    /* MPC8264 HiP3                                                          */
    POWERPC_DEF("MPC8264_HiP3",  CPU_POWERPC_MPC8264_HiP3,           G2),
    /* MPC8264 HiP4                                                          */
    POWERPC_DEF("MPC8264_HiP4",  CPU_POWERPC_MPC8264_HiP4,           G2),
    /* MPC8265                                                               */
    POWERPC_DEF("MPC8265",       CPU_POWERPC_MPC8265,                G2),
    /* MPC8265 HiP3                                                          */
    POWERPC_DEF("MPC8265_HiP3",  CPU_POWERPC_MPC8265_HiP3,           G2),
    /* MPC8265 HiP4                                                          */
    POWERPC_DEF("MPC8265_HiP4",  CPU_POWERPC_MPC8265_HiP4,           G2),
    /* MPC8266                                                               */
    POWERPC_DEF("MPC8266",       CPU_POWERPC_MPC8266,                G2),
    /* MPC8266 HiP3                                                          */
    POWERPC_DEF("MPC8266_HiP3",  CPU_POWERPC_MPC8266_HiP3,           G2),
    /* MPC8266 HiP4                                                          */
    POWERPC_DEF("MPC8266_HiP4",  CPU_POWERPC_MPC8266_HiP4,           G2),
    /* MPC8270                                                               */
    POWERPC_DEF("MPC8270",       CPU_POWERPC_MPC8270,                G2LE),
    /* MPC8271                                                               */
    POWERPC_DEF("MPC8271",       CPU_POWERPC_MPC8271,                G2LE),
    /* MPC8272                                                               */
    POWERPC_DEF("MPC8272",       CPU_POWERPC_MPC8272,                G2LE),
    /* MPC8275                                                               */
    POWERPC_DEF("MPC8275",       CPU_POWERPC_MPC8275,                G2LE),
    /* MPC8280                                                               */
    POWERPC_DEF("MPC8280",       CPU_POWERPC_MPC8280,                G2LE),
    /* e200 family                                                           */
    /* Generic PowerPC e200 core                                             */
    POWERPC_DEF("e200",          CPU_POWERPC_e200,                   e200),
    /* Generic MPC55xx core                                                  */
#if defined (TODO)
    POWERPC_DEF_SVR("MPC55xx",
                    CPU_POWERPC_MPC55xx,      POWERPC_SVR_55xx,      e200),
#endif
#if defined (TODO)
    /* PowerPC e200z0 core                                                   */
    POWERPC_DEF("e200z0",        CPU_POWERPC_e200z0,                 e200),
#endif
#if defined (TODO)
    /* PowerPC e200z1 core                                                   */
    POWERPC_DEF("e200z1",        CPU_POWERPC_e200z1,                 e200),
#endif
#if defined (TODO)
    /* PowerPC e200z3 core                                                   */
    POWERPC_DEF("e200z3",        CPU_POWERPC_e200z3,                 e200),
#endif
    /* PowerPC e200z5 core                                                   */
    POWERPC_DEF("e200z5",        CPU_POWERPC_e200z5,                 e200),
    /* PowerPC e200z6 core                                                   */
    POWERPC_DEF("e200z6",        CPU_POWERPC_e200z6,                 e200),
    /* PowerPC e200 microcontrollers                                         */
#if defined (TODO)
    /* MPC5514E                                                              */
    POWERPC_DEF_SVR("MPC5514E",
                    CPU_POWERPC_MPC5514E,     POWERPC_SVR_5514E,     e200),
#endif
#if defined (TODO)
    /* MPC5514E v0                                                           */
    POWERPC_DEF_SVR("MPC5514E_v0",
                    CPU_POWERPC_MPC5514E_v0,  POWERPC_SVR_5514E_v0,  e200),
#endif
#if defined (TODO)
    /* MPC5514E v1                                                           */
    POWERPC_DEF_SVR("MPC5514E_v1",
                    CPU_POWERPC_MPC5514E_v1,  POWERPC_SVR_5514E_v1,  e200),
#endif
#if defined (TODO)
    /* MPC5514G                                                              */
    POWERPC_DEF_SVR("MPC5514G",
                    CPU_POWERPC_MPC5514G,     POWERPC_SVR_5514G,     e200),
#endif
#if defined (TODO)
    /* MPC5514G v0                                                           */
    POWERPC_DEF_SVR("MPC5514G_v0",
                    CPU_POWERPC_MPC5514G_v0,  POWERPC_SVR_5514G_v0,  e200),
#endif
#if defined (TODO)
    /* MPC5514G v1                                                           */
    POWERPC_DEF_SVR("MPC5514G_v1",
                    CPU_POWERPC_MPC5514G_v1,  POWERPC_SVR_5514G_v1,  e200),
#endif
#if defined (TODO)
    /* MPC5515S                                                              */
    POWERPC_DEF_SVR("MPC5515S",
                    CPU_POWERPC_MPC5515S,     POWERPC_SVR_5515S,     e200),
#endif
#if defined (TODO)
    /* MPC5516E                                                              */
    POWERPC_DEF_SVR("MPC5516E",
                    CPU_POWERPC_MPC5516E,     POWERPC_SVR_5516E,     e200),
#endif
#if defined (TODO)
    /* MPC5516E v0                                                           */
    POWERPC_DEF_SVR("MPC5516E_v0",
                    CPU_POWERPC_MPC5516E_v0,  POWERPC_SVR_5516E_v0,  e200),
#endif
#if defined (TODO)
    /* MPC5516E v1                                                           */
    POWERPC_DEF_SVR("MPC5516E_v1",
                    CPU_POWERPC_MPC5516E_v1,  POWERPC_SVR_5516E_v1,  e200),
#endif
#if defined (TODO)
    /* MPC5516G                                                              */
    POWERPC_DEF_SVR("MPC5516G",
                    CPU_POWERPC_MPC5516G,     POWERPC_SVR_5516G,     e200),
#endif
#if defined (TODO)
    /* MPC5516G v0                                                           */
    POWERPC_DEF_SVR("MPC5516G_v0",
                    CPU_POWERPC_MPC5516G_v0,  POWERPC_SVR_5516G_v0,  e200),
#endif
#if defined (TODO)
    /* MPC5516G v1                                                           */
    POWERPC_DEF_SVR("MPC5516G_v1",
                    CPU_POWERPC_MPC5516G_v1,  POWERPC_SVR_5516G_v1,  e200),
#endif
#if defined (TODO)
    /* MPC5516S                                                              */
    POWERPC_DEF_SVR("MPC5516S",
                    CPU_POWERPC_MPC5516S,     POWERPC_SVR_5516S,     e200),
#endif
#if defined (TODO)
    /* MPC5533                                                               */
    POWERPC_DEF_SVR("MPC5533",
                    CPU_POWERPC_MPC5533,      POWERPC_SVR_5533,      e200),
#endif
#if defined (TODO)
    /* MPC5534                                                               */
    POWERPC_DEF_SVR("MPC5534",
                    CPU_POWERPC_MPC5534,      POWERPC_SVR_5534,      e200),
#endif
#if defined (TODO)
    /* MPC5553                                                               */
    POWERPC_DEF_SVR("MPC5553",
                    CPU_POWERPC_MPC5553,      POWERPC_SVR_5553,      e200),
#endif
#if defined (TODO)
    /* MPC5554                                                               */
    POWERPC_DEF_SVR("MPC5554",
                    CPU_POWERPC_MPC5554,      POWERPC_SVR_5554,      e200),
#endif
#if defined (TODO)
    /* MPC5561                                                               */
    POWERPC_DEF_SVR("MPC5561",
                    CPU_POWERPC_MPC5561,      POWERPC_SVR_5561,      e200),
#endif
#if defined (TODO)
    /* MPC5565                                                               */
    POWERPC_DEF_SVR("MPC5565",
                    CPU_POWERPC_MPC5565,      POWERPC_SVR_5565,      e200),
#endif
#if defined (TODO)
    /* MPC5566                                                               */
    POWERPC_DEF_SVR("MPC5566",
                    CPU_POWERPC_MPC5566,      POWERPC_SVR_5566,      e200),
#endif
#if defined (TODO)
    /* MPC5567                                                               */
    POWERPC_DEF_SVR("MPC5567",
                    CPU_POWERPC_MPC5567,      POWERPC_SVR_5567,      e200),
#endif
    /* e300 family                                                           */
    /* Generic PowerPC e300 core                                             */
    POWERPC_DEF("e300",          CPU_POWERPC_e300,                   e300),
    /* PowerPC e300c1 core                                                   */
    POWERPC_DEF("e300c1",        CPU_POWERPC_e300c1,                 e300),
    /* PowerPC e300c2 core                                                   */
    POWERPC_DEF("e300c2",        CPU_POWERPC_e300c2,                 e300),
    /* PowerPC e300c3 core                                                   */
    POWERPC_DEF("e300c3",        CPU_POWERPC_e300c3,                 e300),
    /* PowerPC e300c4 core                                                   */
    POWERPC_DEF("e300c4",        CPU_POWERPC_e300c4,                 e300),
    /* PowerPC e300 microcontrollers                                         */
#if defined (TODO)
    /* MPC8313                                                               */
    POWERPC_DEF_SVR("MPC8313",
                    CPU_POWERPC_MPC8313,      POWERPC_SVR_8313,      e300),
#endif
#if defined (TODO)
    /* MPC8313E                                                              */
    POWERPC_DEF_SVR("MPC8313E",
                    CPU_POWERPC_MPC8313E,     POWERPC_SVR_8313E,     e300),
#endif
#if defined (TODO)
    /* MPC8314                                                               */
    POWERPC_DEF_SVR("MPC8314",
                    CPU_POWERPC_MPC8314,      POWERPC_SVR_8314,      e300),
#endif
#if defined (TODO)
    /* MPC8314E                                                              */
    POWERPC_DEF_SVR("MPC8314E",
                    CPU_POWERPC_MPC8314E,     POWERPC_SVR_8314E,     e300),
#endif
#if defined (TODO)
    /* MPC8315                                                               */
    POWERPC_DEF_SVR("MPC8315",
                    CPU_POWERPC_MPC8315,      POWERPC_SVR_8315,      e300),
#endif
#if defined (TODO)
    /* MPC8315E                                                              */
    POWERPC_DEF_SVR("MPC8315E",
                    CPU_POWERPC_MPC8315E,     POWERPC_SVR_8315E,     e300),
#endif
#if defined (TODO)
    /* MPC8321                                                               */
    POWERPC_DEF_SVR("MPC8321",
                    CPU_POWERPC_MPC8321,      POWERPC_SVR_8321,      e300),
#endif
#if defined (TODO)
    /* MPC8321E                                                              */
    POWERPC_DEF_SVR("MPC8321E",
                    CPU_POWERPC_MPC8321E,     POWERPC_SVR_8321E,     e300),
#endif
#if defined (TODO)
    /* MPC8323                                                               */
    POWERPC_DEF_SVR("MPC8323",
                    CPU_POWERPC_MPC8323,      POWERPC_SVR_8323,      e300),
#endif
#if defined (TODO)
    /* MPC8323E                                                              */
    POWERPC_DEF_SVR("MPC8323E",
                    CPU_POWERPC_MPC8323E,     POWERPC_SVR_8323E,     e300),
#endif
    /* MPC8343A                                                              */
    POWERPC_DEF_SVR("MPC8343A",
                    CPU_POWERPC_MPC8343A,     POWERPC_SVR_8343A,     e300),
    /* MPC8343EA                                                             */
    POWERPC_DEF_SVR("MPC8343EA",
                    CPU_POWERPC_MPC8343EA,    POWERPC_SVR_8343EA,    e300),
    /* MPC8347A                                                              */
    POWERPC_DEF_SVR("MPC8347A",
                    CPU_POWERPC_MPC8347A,     POWERPC_SVR_8347A,     e300),
    /* MPC8347AT                                                             */
    POWERPC_DEF_SVR("MPC8347AT",
                    CPU_POWERPC_MPC8347AT,    POWERPC_SVR_8347AT,    e300),
    /* MPC8347AP                                                             */
    POWERPC_DEF_SVR("MPC8347AP",
                    CPU_POWERPC_MPC8347AP,    POWERPC_SVR_8347AP,    e300),
    /* MPC8347EA                                                             */
    POWERPC_DEF_SVR("MPC8347EA",
                    CPU_POWERPC_MPC8347EA,    POWERPC_SVR_8347EA,    e300),
    /* MPC8347EAT                                                            */
    POWERPC_DEF_SVR("MPC8347EAT",
                    CPU_POWERPC_MPC8347EAT,   POWERPC_SVR_8347EAT,   e300),
    /* MPC8343EAP                                                            */
    POWERPC_DEF_SVR("MPC8347EAP",
                    CPU_POWERPC_MPC8347EAP,   POWERPC_SVR_8347EAP,   e300),
    /* MPC8349                                                               */
    POWERPC_DEF_SVR("MPC8349",
                    CPU_POWERPC_MPC8349,      POWERPC_SVR_8349,      e300),
    /* MPC8349A                                                              */
    POWERPC_DEF_SVR("MPC8349A",
                    CPU_POWERPC_MPC8349A,     POWERPC_SVR_8349A,     e300),
    /* MPC8349E                                                              */
    POWERPC_DEF_SVR("MPC8349E",
                    CPU_POWERPC_MPC8349E,     POWERPC_SVR_8349E,     e300),
    /* MPC8349EA                                                             */
    POWERPC_DEF_SVR("MPC8349EA",
                    CPU_POWERPC_MPC8349EA,    POWERPC_SVR_8349EA,    e300),
#if defined (TODO)
    /* MPC8358E                                                              */
    POWERPC_DEF_SVR("MPC8358E",
                    CPU_POWERPC_MPC8358E,     POWERPC_SVR_8358E,     e300),
#endif
#if defined (TODO)
    /* MPC8360E                                                              */
    POWERPC_DEF_SVR("MPC8360E",
                    CPU_POWERPC_MPC8360E,     POWERPC_SVR_8360E,     e300),
#endif
    /* MPC8377                                                               */
    POWERPC_DEF_SVR("MPC8377",
                    CPU_POWERPC_MPC8377,      POWERPC_SVR_8377,      e300),
    /* MPC8377E                                                              */
    POWERPC_DEF_SVR("MPC8377E",
                    CPU_POWERPC_MPC8377E,     POWERPC_SVR_8377E,     e300),
    /* MPC8378                                                               */
    POWERPC_DEF_SVR("MPC8378",
                    CPU_POWERPC_MPC8378,      POWERPC_SVR_8378,      e300),
    /* MPC8378E                                                              */
    POWERPC_DEF_SVR("MPC8378E",
                    CPU_POWERPC_MPC8378E,     POWERPC_SVR_8378E,     e300),
    /* MPC8379                                                               */
    POWERPC_DEF_SVR("MPC8379",
                    CPU_POWERPC_MPC8379,      POWERPC_SVR_8379,      e300),
    /* MPC8379E                                                              */
    POWERPC_DEF_SVR("MPC8379E",
                    CPU_POWERPC_MPC8379E,     POWERPC_SVR_8379E,     e300),
    /* e500 family                                                           */
    /* PowerPC e500 core                                                     */
    POWERPC_DEF("e500",          CPU_POWERPC_e500,                   e500),
    /* PowerPC e500 v1.0 core                                                */
    POWERPC_DEF("e500_v10",      CPU_POWERPC_e500_v10,               e500),
    /* PowerPC e500 v2.0 core                                                */
    POWERPC_DEF("e500_v20",      CPU_POWERPC_e500_v20,               e500),
    /* PowerPC e500v2 core                                                   */
    POWERPC_DEF("e500v2",        CPU_POWERPC_e500v2,                 e500),
    /* PowerPC e500v2 v1.0 core                                              */
    POWERPC_DEF("e500v2_v10",    CPU_POWERPC_e500v2_v10,             e500),
    /* PowerPC e500v2 v2.0 core                                              */
    POWERPC_DEF("e500v2_v20",    CPU_POWERPC_e500v2_v20,             e500),
    /* PowerPC e500v2 v2.1 core                                              */
    POWERPC_DEF("e500v2_v21",    CPU_POWERPC_e500v2_v21,             e500),
    /* PowerPC e500v2 v2.2 core                                              */
    POWERPC_DEF("e500v2_v22",    CPU_POWERPC_e500v2_v22,             e500),
    /* PowerPC e500v2 v3.0 core                                              */
    POWERPC_DEF("e500v2_v30",    CPU_POWERPC_e500v2_v30,             e500),
    /* PowerPC e500 microcontrollers                                         */
    /* MPC8533                                                               */
    POWERPC_DEF_SVR("MPC8533",
                    CPU_POWERPC_MPC8533,      POWERPC_SVR_8533,      e500),
    /* MPC8533 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8533_v10",
                    CPU_POWERPC_MPC8533_v10,  POWERPC_SVR_8533_v10,  e500),
    /* MPC8533 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8533_v11",
                    CPU_POWERPC_MPC8533_v11,  POWERPC_SVR_8533_v11,  e500),
    /* MPC8533E                                                              */
    POWERPC_DEF_SVR("MPC8533E",
                    CPU_POWERPC_MPC8533E,     POWERPC_SVR_8533E,     e500),
    /* MPC8533E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8533E_v10",
                    CPU_POWERPC_MPC8533E_v10, POWERPC_SVR_8533E_v10, e500),
    POWERPC_DEF_SVR("MPC8533E_v11",
                    CPU_POWERPC_MPC8533E_v11, POWERPC_SVR_8533E_v11, e500),
    /* MPC8540                                                               */
    POWERPC_DEF_SVR("MPC8540",
                    CPU_POWERPC_MPC8540,      POWERPC_SVR_8540,      e500),
    /* MPC8540 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8540_v10",
                    CPU_POWERPC_MPC8540_v10,  POWERPC_SVR_8540_v10,  e500),
    /* MPC8540 v2.0                                                          */
    POWERPC_DEF_SVR("MPC8540_v20",
                    CPU_POWERPC_MPC8540_v20,  POWERPC_SVR_8540_v20,  e500),
    /* MPC8540 v2.1                                                          */
    POWERPC_DEF_SVR("MPC8540_v21",
                    CPU_POWERPC_MPC8540_v21,  POWERPC_SVR_8540_v21,  e500),
    /* MPC8541                                                               */
    POWERPC_DEF_SVR("MPC8541",
                    CPU_POWERPC_MPC8541,      POWERPC_SVR_8541,      e500),
    /* MPC8541 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8541_v10",
                    CPU_POWERPC_MPC8541_v10,  POWERPC_SVR_8541_v10,  e500),
    /* MPC8541 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8541_v11",
                    CPU_POWERPC_MPC8541_v11,  POWERPC_SVR_8541_v11,  e500),
    /* MPC8541E                                                              */
    POWERPC_DEF_SVR("MPC8541E",
                    CPU_POWERPC_MPC8541E,     POWERPC_SVR_8541E,     e500),
    /* MPC8541E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8541E_v10",
                    CPU_POWERPC_MPC8541E_v10, POWERPC_SVR_8541E_v10, e500),
    /* MPC8541E v1.1                                                         */
    POWERPC_DEF_SVR("MPC8541E_v11",
                    CPU_POWERPC_MPC8541E_v11, POWERPC_SVR_8541E_v11, e500),
    /* MPC8543                                                               */
    POWERPC_DEF_SVR("MPC8543",
                    CPU_POWERPC_MPC8543,      POWERPC_SVR_8543,      e500),
    /* MPC8543 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8543_v10",
                    CPU_POWERPC_MPC8543_v10,  POWERPC_SVR_8543_v10,  e500),
    /* MPC8543 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8543_v11",
                    CPU_POWERPC_MPC8543_v11,  POWERPC_SVR_8543_v11,  e500),
    /* MPC8543 v2.0                                                          */
    POWERPC_DEF_SVR("MPC8543_v20",
                    CPU_POWERPC_MPC8543_v20,  POWERPC_SVR_8543_v20,  e500),
    /* MPC8543 v2.1                                                          */
    POWERPC_DEF_SVR("MPC8543_v21",
                    CPU_POWERPC_MPC8543_v21,  POWERPC_SVR_8543_v21,  e500),
    /* MPC8543E                                                              */
    POWERPC_DEF_SVR("MPC8543E",
                    CPU_POWERPC_MPC8543E,     POWERPC_SVR_8543E,     e500),
    /* MPC8543E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8543E_v10",
                    CPU_POWERPC_MPC8543E_v10, POWERPC_SVR_8543E_v10, e500),
    /* MPC8543E v1.1                                                         */
    POWERPC_DEF_SVR("MPC8543E_v11",
                    CPU_POWERPC_MPC8543E_v11, POWERPC_SVR_8543E_v11, e500),
    /* MPC8543E v2.0                                                         */
    POWERPC_DEF_SVR("MPC8543E_v20",
                    CPU_POWERPC_MPC8543E_v20, POWERPC_SVR_8543E_v20, e500),
    /* MPC8543E v2.1                                                         */
    POWERPC_DEF_SVR("MPC8543E_v21",
                    CPU_POWERPC_MPC8543E_v21, POWERPC_SVR_8543E_v21, e500),
    /* MPC8544                                                               */
    POWERPC_DEF_SVR("MPC8544",
                    CPU_POWERPC_MPC8544,      POWERPC_SVR_8544,      e500),
    /* MPC8544 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8544_v10",
                    CPU_POWERPC_MPC8544_v10,  POWERPC_SVR_8544_v10,  e500),
    /* MPC8544 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8544_v11",
                    CPU_POWERPC_MPC8544_v11,  POWERPC_SVR_8544_v11,  e500),
    /* MPC8544E                                                              */
    POWERPC_DEF_SVR("MPC8544E",
                    CPU_POWERPC_MPC8544E,     POWERPC_SVR_8544E,     e500),
    /* MPC8544E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8544E_v10",
                    CPU_POWERPC_MPC8544E_v10, POWERPC_SVR_8544E_v10, e500),
    /* MPC8544E v1.1                                                         */
    POWERPC_DEF_SVR("MPC8544E_v11",
                    CPU_POWERPC_MPC8544E_v11, POWERPC_SVR_8544E_v11, e500),
    /* MPC8545                                                               */
    POWERPC_DEF_SVR("MPC8545",
                    CPU_POWERPC_MPC8545,      POWERPC_SVR_8545,      e500),
    /* MPC8545 v2.0                                                          */
    POWERPC_DEF_SVR("MPC8545_v20",
                    CPU_POWERPC_MPC8545_v20,  POWERPC_SVR_8545_v20,  e500),
    /* MPC8545 v2.1                                                          */
    POWERPC_DEF_SVR("MPC8545_v21",
                    CPU_POWERPC_MPC8545_v21,  POWERPC_SVR_8545_v21,  e500),
    /* MPC8545E                                                              */
    POWERPC_DEF_SVR("MPC8545E",
                    CPU_POWERPC_MPC8545E,     POWERPC_SVR_8545E,     e500),
    /* MPC8545E v2.0                                                         */
    POWERPC_DEF_SVR("MPC8545E_v20",
                    CPU_POWERPC_MPC8545E_v20, POWERPC_SVR_8545E_v20, e500),
    /* MPC8545E v2.1                                                         */
    POWERPC_DEF_SVR("MPC8545E_v21",
                    CPU_POWERPC_MPC8545E_v21, POWERPC_SVR_8545E_v21, e500),
    /* MPC8547E                                                              */
    POWERPC_DEF_SVR("MPC8547E",
                    CPU_POWERPC_MPC8547E,     POWERPC_SVR_8547E,     e500),
    /* MPC8547E v2.0                                                         */
    POWERPC_DEF_SVR("MPC8547E_v20",
                    CPU_POWERPC_MPC8547E_v20, POWERPC_SVR_8547E_v20, e500),
    /* MPC8547E v2.1                                                         */
    POWERPC_DEF_SVR("MPC8547E_v21",
                    CPU_POWERPC_MPC8547E_v21, POWERPC_SVR_8547E_v21, e500),
    /* MPC8548                                                               */
    POWERPC_DEF_SVR("MPC8548",
                    CPU_POWERPC_MPC8548,      POWERPC_SVR_8548,      e500),
    /* MPC8548 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8548_v10",
                    CPU_POWERPC_MPC8548_v10,  POWERPC_SVR_8548_v10,  e500),
    /* MPC8548 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8548_v11",
                    CPU_POWERPC_MPC8548_v11,  POWERPC_SVR_8548_v11,  e500),
    /* MPC8548 v2.0                                                          */
    POWERPC_DEF_SVR("MPC8548_v20",
                    CPU_POWERPC_MPC8548_v20,  POWERPC_SVR_8548_v20,  e500),
    /* MPC8548 v2.1                                                          */
    POWERPC_DEF_SVR("MPC8548_v21",
                    CPU_POWERPC_MPC8548_v21,  POWERPC_SVR_8548_v21,  e500),
    /* MPC8548E                                                              */
    POWERPC_DEF_SVR("MPC8548E",
                    CPU_POWERPC_MPC8548E,     POWERPC_SVR_8548E,     e500),
    /* MPC8548E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8548E_v10",
                    CPU_POWERPC_MPC8548E_v10, POWERPC_SVR_8548E_v10, e500),
    /* MPC8548E v1.1                                                         */
    POWERPC_DEF_SVR("MPC8548E_v11",
                    CPU_POWERPC_MPC8548E_v11, POWERPC_SVR_8548E_v11, e500),
    /* MPC8548E v2.0                                                         */
    POWERPC_DEF_SVR("MPC8548E_v20",
                    CPU_POWERPC_MPC8548E_v20, POWERPC_SVR_8548E_v20, e500),
    /* MPC8548E v2.1                                                         */
    POWERPC_DEF_SVR("MPC8548E_v21",
                    CPU_POWERPC_MPC8548E_v21, POWERPC_SVR_8548E_v21, e500),
    /* MPC8555                                                               */
    POWERPC_DEF_SVR("MPC8555",
                    CPU_POWERPC_MPC8555,      POWERPC_SVR_8555,      e500),
    /* MPC8555 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8555_v10",
                    CPU_POWERPC_MPC8555_v10,  POWERPC_SVR_8555_v10,  e500),
    /* MPC8555 v1.1                                                          */
    POWERPC_DEF_SVR("MPC8555_v11",
                    CPU_POWERPC_MPC8555_v11,  POWERPC_SVR_8555_v11,  e500),
    /* MPC8555E                                                              */
    POWERPC_DEF_SVR("MPC8555E",
                    CPU_POWERPC_MPC8555E,     POWERPC_SVR_8555E,     e500),
    /* MPC8555E v1.0                                                         */
    POWERPC_DEF_SVR("MPC8555E_v10",
                    CPU_POWERPC_MPC8555E_v10, POWERPC_SVR_8555E_v10, e500),
    /* MPC8555E v1.1                                                         */
    POWERPC_DEF_SVR("MPC8555E_v11",
                    CPU_POWERPC_MPC8555E_v11, POWERPC_SVR_8555E_v11, e500),
    /* MPC8560                                                               */
    POWERPC_DEF_SVR("MPC8560",
                    CPU_POWERPC_MPC8560,      POWERPC_SVR_8560,      e500),
    /* MPC8560 v1.0                                                          */
    POWERPC_DEF_SVR("MPC8560_v10",
                    CPU_POWERPC_MPC8560_v10,  POWERPC_SVR_8560_v10,  e500),
    /* MPC8560 v2.0                                                          */
    POWERPC_DEF_SVR("MPC8560_v20",
                    CPU_POWERPC_MPC8560_v20,  POWERPC_SVR_8560_v20,  e500),
    /* MPC8560 v2.1                                                          */
    POWERPC_DEF_SVR("MPC8560_v21",
                    CPU_POWERPC_MPC8560_v21,  POWERPC_SVR_8560_v21,  e500),
    /* MPC8567                                                               */
    POWERPC_DEF_SVR("MPC8567",
                    CPU_POWERPC_MPC8567,      POWERPC_SVR_8567,      e500),
    /* MPC8567E                                                              */
    POWERPC_DEF_SVR("MPC8567E",
                    CPU_POWERPC_MPC8567E,     POWERPC_SVR_8567E,     e500),
    /* MPC8568                                                               */
    POWERPC_DEF_SVR("MPC8568",
                    CPU_POWERPC_MPC8568,      POWERPC_SVR_8568,      e500),
    /* MPC8568E                                                              */
    POWERPC_DEF_SVR("MPC8568E",
                    CPU_POWERPC_MPC8568E,     POWERPC_SVR_8568E,     e500),
    /* MPC8572                                                               */
    POWERPC_DEF_SVR("MPC8572",
                    CPU_POWERPC_MPC8572,      POWERPC_SVR_8572,      e500),
    /* MPC8572E                                                              */
    POWERPC_DEF_SVR("MPC8572E",
                    CPU_POWERPC_MPC8572E,     POWERPC_SVR_8572E,     e500),
    /* e600 family                                                           */
    /* PowerPC e600 core                                                     */
    POWERPC_DEF("e600",          CPU_POWERPC_e600,                   7400),
    /* PowerPC e600 microcontrollers                                         */
#if defined (TODO)
    /* MPC8610                                                               */
    POWERPC_DEF_SVR("MPC8610",
                    CPU_POWERPC_MPC8610,      POWERPC_SVR_8610,      7400),
#endif
    /* MPC8641                                                               */
    POWERPC_DEF_SVR("MPC8641",
                    CPU_POWERPC_MPC8641,      POWERPC_SVR_8641,      7400),
    /* MPC8641D                                                              */
    POWERPC_DEF_SVR("MPC8641D",
                    CPU_POWERPC_MPC8641D,     POWERPC_SVR_8641D,     7400),
    /* 32 bits "classic" PowerPC                                             */
    /* PowerPC 6xx family                                                    */
    /* PowerPC 601                                                           */
    POWERPC_DEF("601",           CPU_POWERPC_601,                    601v),
    /* PowerPC 601v0                                                         */
    POWERPC_DEF("601_v0",        CPU_POWERPC_601_v0,                 601),
    /* PowerPC 601v1                                                         */
    POWERPC_DEF("601_v1",        CPU_POWERPC_601_v1,                 601),
    /* PowerPC 601v                                                          */
    POWERPC_DEF("601v",          CPU_POWERPC_601v,                   601v),
    /* PowerPC 601v2                                                         */
    POWERPC_DEF("601_v2",        CPU_POWERPC_601_v2,                 601v),
    /* PowerPC 602                                                           */
    POWERPC_DEF("602",           CPU_POWERPC_602,                    602),
    /* PowerPC 603                                                           */
    POWERPC_DEF("603",           CPU_POWERPC_603,                    603),
    /* Code name for PowerPC 603                                             */
    POWERPC_DEF("Vanilla",       CPU_POWERPC_603,                    603),
    /* PowerPC 603e (aka PID6)                                               */
    POWERPC_DEF("603e",          CPU_POWERPC_603E,                   603E),
    /* Code name for PowerPC 603e                                            */
    POWERPC_DEF("Stretch",       CPU_POWERPC_603E,                   603E),
    /* PowerPC 603e v1.1                                                     */
    POWERPC_DEF("603e_v1.1",     CPU_POWERPC_603E_v11,               603E),
    /* PowerPC 603e v1.2                                                     */
    POWERPC_DEF("603e_v1.2",     CPU_POWERPC_603E_v12,               603E),
    /* PowerPC 603e v1.3                                                     */
    POWERPC_DEF("603e_v1.3",     CPU_POWERPC_603E_v13,               603E),
    /* PowerPC 603e v1.4                                                     */
    POWERPC_DEF("603e_v1.4",     CPU_POWERPC_603E_v14,               603E),
    /* PowerPC 603e v2.2                                                     */
    POWERPC_DEF("603e_v2.2",     CPU_POWERPC_603E_v22,               603E),
    /* PowerPC 603e v3                                                       */
    POWERPC_DEF("603e_v3",       CPU_POWERPC_603E_v3,                603E),
    /* PowerPC 603e v4                                                       */
    POWERPC_DEF("603e_v4",       CPU_POWERPC_603E_v4,                603E),
    /* PowerPC 603e v4.1                                                     */
    POWERPC_DEF("603e_v4.1",     CPU_POWERPC_603E_v41,               603E),
    /* PowerPC 603e (aka PID7)                                               */
    POWERPC_DEF("603e7",         CPU_POWERPC_603E7,                  603E),
    /* PowerPC 603e7t                                                        */
    POWERPC_DEF("603e7t",        CPU_POWERPC_603E7t,                 603E),
    /* PowerPC 603e7v                                                        */
    POWERPC_DEF("603e7v",        CPU_POWERPC_603E7v,                 603E),
    /* Code name for PowerPC 603ev                                           */
    POWERPC_DEF("Vaillant",      CPU_POWERPC_603E7v,                 603E),
    /* PowerPC 603e7v1                                                       */
    POWERPC_DEF("603e7v1",       CPU_POWERPC_603E7v1,                603E),
    /* PowerPC 603e7v2                                                       */
    POWERPC_DEF("603e7v2",       CPU_POWERPC_603E7v2,                603E),
    /* PowerPC 603p (aka PID7v)                                              */
    POWERPC_DEF("603p",          CPU_POWERPC_603P,                   603E),
    /* PowerPC 603r (aka PID7t)                                              */
    POWERPC_DEF("603r",          CPU_POWERPC_603R,                   603E),
    /* Code name for PowerPC 603r                                            */
    POWERPC_DEF("Goldeneye",     CPU_POWERPC_603R,                   603E),
    /* PowerPC 604                                                           */
    POWERPC_DEF("604",           CPU_POWERPC_604,                    604),
    /* PowerPC 604e (aka PID9)                                               */
    POWERPC_DEF("604e",          CPU_POWERPC_604E,                   604E),
    /* Code name for PowerPC 604e                                            */
    POWERPC_DEF("Sirocco",       CPU_POWERPC_604E,                   604E),
    /* PowerPC 604e v1.0                                                     */
    POWERPC_DEF("604e_v1.0",     CPU_POWERPC_604E_v10,               604E),
    /* PowerPC 604e v2.2                                                     */
    POWERPC_DEF("604e_v2.2",     CPU_POWERPC_604E_v22,               604E),
    /* PowerPC 604e v2.4                                                     */
    POWERPC_DEF("604e_v2.4",     CPU_POWERPC_604E_v24,               604E),
    /* PowerPC 604r (aka PIDA)                                               */
    POWERPC_DEF("604r",          CPU_POWERPC_604R,                   604E),
    /* Code name for PowerPC 604r                                            */
    POWERPC_DEF("Mach5",         CPU_POWERPC_604R,                   604E),
#if defined(TODO)
    /* PowerPC 604ev                                                         */
    POWERPC_DEF("604ev",         CPU_POWERPC_604EV,                  604E),
#endif
    /* PowerPC 7xx family                                                    */
    /* Generic PowerPC 740 (G3)                                              */
    POWERPC_DEF("740",           CPU_POWERPC_7x0,                    740),
    /* Code name for PowerPC 740                                             */
    POWERPC_DEF("Arthur",        CPU_POWERPC_7x0,                    740),
    /* Generic PowerPC 750 (G3)                                              */
    POWERPC_DEF("750",           CPU_POWERPC_7x0,                    750),
    /* Code name for PowerPC 750                                             */
    POWERPC_DEF("Typhoon",       CPU_POWERPC_7x0,                    750),
    /* PowerPC 740/750 is also known as G3                                   */
    POWERPC_DEF("G3",            CPU_POWERPC_7x0,                    750),
    /* PowerPC 740 v1.0 (G3)                                                 */
    POWERPC_DEF("740_v1.0",      CPU_POWERPC_7x0_v10,                740),
    /* PowerPC 750 v1.0 (G3)                                                 */
    POWERPC_DEF("750_v1.0",      CPU_POWERPC_7x0_v10,                750),
    /* PowerPC 740 v2.0 (G3)                                                 */
    POWERPC_DEF("740_v2.0",      CPU_POWERPC_7x0_v20,                740),
    /* PowerPC 750 v2.0 (G3)                                                 */
    POWERPC_DEF("750_v2.0",      CPU_POWERPC_7x0_v20,                750),
    /* PowerPC 740 v2.1 (G3)                                                 */
    POWERPC_DEF("740_v2.1",      CPU_POWERPC_7x0_v21,                740),
    /* PowerPC 750 v2.1 (G3)                                                 */
    POWERPC_DEF("750_v2.1",      CPU_POWERPC_7x0_v21,                750),
    /* PowerPC 740 v2.2 (G3)                                                 */
    POWERPC_DEF("740_v2.2",      CPU_POWERPC_7x0_v22,                740),
    /* PowerPC 750 v2.2 (G3)                                                 */
    POWERPC_DEF("750_v2.2",      CPU_POWERPC_7x0_v22,                750),
    /* PowerPC 740 v3.0 (G3)                                                 */
    POWERPC_DEF("740_v3.0",      CPU_POWERPC_7x0_v30,                740),
    /* PowerPC 750 v3.0 (G3)                                                 */
    POWERPC_DEF("750_v3.0",      CPU_POWERPC_7x0_v30,                750),
    /* PowerPC 740 v3.1 (G3)                                                 */
    POWERPC_DEF("740_v3.1",      CPU_POWERPC_7x0_v31,                740),
    /* PowerPC 750 v3.1 (G3)                                                 */
    POWERPC_DEF("750_v3.1",      CPU_POWERPC_7x0_v31,                750),
    /* PowerPC 740E (G3)                                                     */
    POWERPC_DEF("740e",          CPU_POWERPC_740E,                   740),
    /* PowerPC 750E (G3)                                                     */
    POWERPC_DEF("750e",          CPU_POWERPC_750E,                   750),
    /* PowerPC 740P (G3)                                                     */
    POWERPC_DEF("740p",          CPU_POWERPC_7x0P,                   740),
    /* PowerPC 750P (G3)                                                     */
    POWERPC_DEF("750p",          CPU_POWERPC_7x0P,                   750),
    /* Code name for PowerPC 740P/750P (G3)                                  */
    POWERPC_DEF("Conan/Doyle",   CPU_POWERPC_7x0P,                   750),
    /* PowerPC 750CL (G3 embedded)                                           */
    POWERPC_DEF("750cl",         CPU_POWERPC_750CL,                  750cl),
    /* PowerPC 750CL v1.0                                                    */
    POWERPC_DEF("750cl_v1.0",    CPU_POWERPC_750CL_v10,              750cl),
    /* PowerPC 750CL v2.0                                                    */
    POWERPC_DEF("750cl_v2.0",    CPU_POWERPC_750CL_v20,              750cl),
    /* PowerPC 750CX (G3 embedded)                                           */
    POWERPC_DEF("750cx",         CPU_POWERPC_750CX,                  750cx),
    /* PowerPC 750CX v1.0 (G3 embedded)                                      */
    POWERPC_DEF("750cx_v1.0",    CPU_POWERPC_750CX_v10,              750cx),
    /* PowerPC 750CX v2.1 (G3 embedded)                                      */
    POWERPC_DEF("750cx_v2.0",    CPU_POWERPC_750CX_v20,              750cx),
    /* PowerPC 750CX v2.1 (G3 embedded)                                      */
    POWERPC_DEF("750cx_v2.1",    CPU_POWERPC_750CX_v21,              750cx),
    /* PowerPC 750CX v2.2 (G3 embedded)                                      */
    POWERPC_DEF("750cx_v2.2",    CPU_POWERPC_750CX_v22,              750cx),
    /* PowerPC 750CXe (G3 embedded)                                          */
    POWERPC_DEF("750cxe",        CPU_POWERPC_750CXE,                 750cx),
    /* PowerPC 750CXe v2.1 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v2.1",   CPU_POWERPC_750CXE_v21,             750cx),
    /* PowerPC 750CXe v2.2 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v2.2",   CPU_POWERPC_750CXE_v22,             750cx),
    /* PowerPC 750CXe v2.3 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v2.3",   CPU_POWERPC_750CXE_v23,             750cx),
    /* PowerPC 750CXe v2.4 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v2.4",   CPU_POWERPC_750CXE_v24,             750cx),
    /* PowerPC 750CXe v2.4b (G3 embedded)                                    */
    POWERPC_DEF("750cxe_v2.4b",  CPU_POWERPC_750CXE_v24b,            750cx),
    /* PowerPC 750CXe v3.0 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v3.0",   CPU_POWERPC_750CXE_v30,             750cx),
    /* PowerPC 750CXe v3.1 (G3 embedded)                                     */
    POWERPC_DEF("750cxe_v3.1",   CPU_POWERPC_750CXE_v31,             750cx),
    /* PowerPC 750CXe v3.1b (G3 embedded)                                    */
    POWERPC_DEF("750cxe_v3.1b",  CPU_POWERPC_750CXE_v31b,            750cx),
    /* PowerPC 750CXr (G3 embedded)                                          */
    POWERPC_DEF("750cxr",        CPU_POWERPC_750CXR,                 750cx),
    /* PowerPC 750FL (G3 embedded)                                           */
    POWERPC_DEF("750fl",         CPU_POWERPC_750FL,                  750fx),
    /* PowerPC 750FX (G3 embedded)                                           */
    POWERPC_DEF("750fx",         CPU_POWERPC_750FX,                  750fx),
    /* PowerPC 750FX v1.0 (G3 embedded)                                      */
    POWERPC_DEF("750fx_v1.0",    CPU_POWERPC_750FX_v10,              750fx),
    /* PowerPC 750FX v2.0 (G3 embedded)                                      */
    POWERPC_DEF("750fx_v2.0",    CPU_POWERPC_750FX_v20,              750fx),
    /* PowerPC 750FX v2.1 (G3 embedded)                                      */
    POWERPC_DEF("750fx_v2.1",    CPU_POWERPC_750FX_v21,              750fx),
    /* PowerPC 750FX v2.2 (G3 embedded)                                      */
    POWERPC_DEF("750fx_v2.2",    CPU_POWERPC_750FX_v22,              750fx),
    /* PowerPC 750FX v2.3 (G3 embedded)                                      */
    POWERPC_DEF("750fx_v2.3",    CPU_POWERPC_750FX_v23,              750fx),
    /* PowerPC 750GL (G3 embedded)                                           */
    POWERPC_DEF("750gl",         CPU_POWERPC_750GL,                  750gx),
    /* PowerPC 750GX (G3 embedded)                                           */
    POWERPC_DEF("750gx",         CPU_POWERPC_750GX,                  750gx),
    /* PowerPC 750GX v1.0 (G3 embedded)                                      */
    POWERPC_DEF("750gx_v1.0",    CPU_POWERPC_750GX_v10,              750gx),
    /* PowerPC 750GX v1.1 (G3 embedded)                                      */
    POWERPC_DEF("750gx_v1.1",    CPU_POWERPC_750GX_v11,              750gx),
    /* PowerPC 750GX v1.2 (G3 embedded)                                      */
    POWERPC_DEF("750gx_v1.2",    CPU_POWERPC_750GX_v12,              750gx),
    /* PowerPC 750L (G3 embedded)                                            */
    POWERPC_DEF("750l",          CPU_POWERPC_750L,                   750),
    /* Code name for PowerPC 750L (G3 embedded)                              */
    POWERPC_DEF("LoneStar",      CPU_POWERPC_750L,                   750),
    /* PowerPC 750L v2.0 (G3 embedded)                                       */
    POWERPC_DEF("750l_v2.0",     CPU_POWERPC_750L_v20,               750),
    /* PowerPC 750L v2.1 (G3 embedded)                                       */
    POWERPC_DEF("750l_v2.1",     CPU_POWERPC_750L_v21,               750),
    /* PowerPC 750L v2.2 (G3 embedded)                                       */
    POWERPC_DEF("750l_v2.2",     CPU_POWERPC_750L_v22,               750),
    /* PowerPC 750L v3.0 (G3 embedded)                                       */
    POWERPC_DEF("750l_v3.0",     CPU_POWERPC_750L_v30,               750),
    /* PowerPC 750L v3.2 (G3 embedded)                                       */
    POWERPC_DEF("750l_v3.2",     CPU_POWERPC_750L_v32,               750),
    /* Generic PowerPC 745                                                   */
    POWERPC_DEF("745",           CPU_POWERPC_7x5,                    745),
    /* Generic PowerPC 755                                                   */
    POWERPC_DEF("755",           CPU_POWERPC_7x5,                    755),
    /* Code name for PowerPC 745/755                                         */
    POWERPC_DEF("Goldfinger",    CPU_POWERPC_7x5,                    755),
    /* PowerPC 745 v1.0                                                      */
    POWERPC_DEF("745_v1.0",      CPU_POWERPC_7x5_v10,                745),
    /* PowerPC 755 v1.0                                                      */
    POWERPC_DEF("755_v1.0",      CPU_POWERPC_7x5_v10,                755),
    /* PowerPC 745 v1.1                                                      */
    POWERPC_DEF("745_v1.1",      CPU_POWERPC_7x5_v11,                745),
    /* PowerPC 755 v1.1                                                      */
    POWERPC_DEF("755_v1.1",      CPU_POWERPC_7x5_v11,                755),
    /* PowerPC 745 v2.0                                                      */
    POWERPC_DEF("745_v2.0",      CPU_POWERPC_7x5_v20,                745),
    /* PowerPC 755 v2.0                                                      */
    POWERPC_DEF("755_v2.0",      CPU_POWERPC_7x5_v20,                755),
    /* PowerPC 745 v2.1                                                      */
    POWERPC_DEF("745_v2.1",      CPU_POWERPC_7x5_v21,                745),
    /* PowerPC 755 v2.1                                                      */
    POWERPC_DEF("755_v2.1",      CPU_POWERPC_7x5_v21,                755),
    /* PowerPC 745 v2.2                                                      */
    POWERPC_DEF("745_v2.2",      CPU_POWERPC_7x5_v22,                745),
    /* PowerPC 755 v2.2                                                      */
    POWERPC_DEF("755_v2.2",      CPU_POWERPC_7x5_v22,                755),
    /* PowerPC 745 v2.3                                                      */
    POWERPC_DEF("745_v2.3",      CPU_POWERPC_7x5_v23,                745),
    /* PowerPC 755 v2.3                                                      */
    POWERPC_DEF("755_v2.3",      CPU_POWERPC_7x5_v23,                755),
    /* PowerPC 745 v2.4                                                      */
    POWERPC_DEF("745_v2.4",      CPU_POWERPC_7x5_v24,                745),
    /* PowerPC 755 v2.4                                                      */
    POWERPC_DEF("755_v2.4",      CPU_POWERPC_7x5_v24,                755),
    /* PowerPC 745 v2.5                                                      */
    POWERPC_DEF("745_v2.5",      CPU_POWERPC_7x5_v25,                745),
    /* PowerPC 755 v2.5                                                      */
    POWERPC_DEF("755_v2.5",      CPU_POWERPC_7x5_v25,                755),
    /* PowerPC 745 v2.6                                                      */
    POWERPC_DEF("745_v2.6",      CPU_POWERPC_7x5_v26,                745),
    /* PowerPC 755 v2.6                                                      */
    POWERPC_DEF("755_v2.6",      CPU_POWERPC_7x5_v26,                755),
    /* PowerPC 745 v2.7                                                      */
    POWERPC_DEF("745_v2.7",      CPU_POWERPC_7x5_v27,                745),
    /* PowerPC 755 v2.7                                                      */
    POWERPC_DEF("755_v2.7",      CPU_POWERPC_7x5_v27,                755),
    /* PowerPC 745 v2.8                                                      */
    POWERPC_DEF("745_v2.8",      CPU_POWERPC_7x5_v28,                745),
    /* PowerPC 755 v2.8                                                      */
    POWERPC_DEF("755_v2.8",      CPU_POWERPC_7x5_v28,                755),
#if defined (TODO)
    /* PowerPC 745P (G3)                                                     */
    POWERPC_DEF("745p",          CPU_POWERPC_7x5P,                   745),
    /* PowerPC 755P (G3)                                                     */
    POWERPC_DEF("755p",          CPU_POWERPC_7x5P,                   755),
#endif
    /* PowerPC 74xx family                                                   */
    /* PowerPC 7400 (G4)                                                     */
    POWERPC_DEF("7400",          CPU_POWERPC_7400,                   7400),
    /* Code name for PowerPC 7400                                            */
    POWERPC_DEF("Max",           CPU_POWERPC_7400,                   7400),
    /* PowerPC 74xx is also well known as G4                                 */
    POWERPC_DEF("G4",            CPU_POWERPC_7400,                   7400),
    /* PowerPC 7400 v1.0 (G4)                                                */
    POWERPC_DEF("7400_v1.0",     CPU_POWERPC_7400_v10,               7400),
    /* PowerPC 7400 v1.1 (G4)                                                */
    POWERPC_DEF("7400_v1.1",     CPU_POWERPC_7400_v11,               7400),
    /* PowerPC 7400 v2.0 (G4)                                                */
    POWERPC_DEF("7400_v2.0",     CPU_POWERPC_7400_v20,               7400),
    /* PowerPC 7400 v2.1 (G4)                                                */
    POWERPC_DEF("7400_v2.1",     CPU_POWERPC_7400_v21,               7400),
    /* PowerPC 7400 v2.2 (G4)                                                */
    POWERPC_DEF("7400_v2.2",     CPU_POWERPC_7400_v22,               7400),
    /* PowerPC 7400 v2.6 (G4)                                                */
    POWERPC_DEF("7400_v2.6",     CPU_POWERPC_7400_v26,               7400),
    /* PowerPC 7400 v2.7 (G4)                                                */
    POWERPC_DEF("7400_v2.7",     CPU_POWERPC_7400_v27,               7400),
    /* PowerPC 7400 v2.8 (G4)                                                */
    POWERPC_DEF("7400_v2.8",     CPU_POWERPC_7400_v28,               7400),
    /* PowerPC 7400 v2.9 (G4)                                                */
    POWERPC_DEF("7400_v2.9",     CPU_POWERPC_7400_v29,               7400),
    /* PowerPC 7410 (G4)                                                     */
    POWERPC_DEF("7410",          CPU_POWERPC_7410,                   7410),
    /* Code name for PowerPC 7410                                            */
    POWERPC_DEF("Nitro",         CPU_POWERPC_7410,                   7410),
    /* PowerPC 7410 v1.0 (G4)                                                */
    POWERPC_DEF("7410_v1.0",     CPU_POWERPC_7410_v10,               7410),
    /* PowerPC 7410 v1.1 (G4)                                                */
    POWERPC_DEF("7410_v1.1",     CPU_POWERPC_7410_v11,               7410),
    /* PowerPC 7410 v1.2 (G4)                                                */
    POWERPC_DEF("7410_v1.2",     CPU_POWERPC_7410_v12,               7410),
    /* PowerPC 7410 v1.3 (G4)                                                */
    POWERPC_DEF("7410_v1.3",     CPU_POWERPC_7410_v13,               7410),
    /* PowerPC 7410 v1.4 (G4)                                                */
    POWERPC_DEF("7410_v1.4",     CPU_POWERPC_7410_v14,               7410),
    /* PowerPC 7448 (G4)                                                     */
    POWERPC_DEF("7448",          CPU_POWERPC_7448,                   7400),
    /* PowerPC 7448 v1.0 (G4)                                                */
    POWERPC_DEF("7448_v1.0",     CPU_POWERPC_7448_v10,               7400),
    /* PowerPC 7448 v1.1 (G4)                                                */
    POWERPC_DEF("7448_v1.1",     CPU_POWERPC_7448_v11,               7400),
    /* PowerPC 7448 v2.0 (G4)                                                */
    POWERPC_DEF("7448_v2.0",     CPU_POWERPC_7448_v20,               7400),
    /* PowerPC 7448 v2.1 (G4)                                                */
    POWERPC_DEF("7448_v2.1",     CPU_POWERPC_7448_v21,               7400),
    /* PowerPC 7450 (G4)                                                     */
    POWERPC_DEF("7450",          CPU_POWERPC_7450,                   7450),
    /* Code name for PowerPC 7450                                            */
    POWERPC_DEF("Vger",          CPU_POWERPC_7450,                   7450),
    /* PowerPC 7450 v1.0 (G4)                                                */
    POWERPC_DEF("7450_v1.0",     CPU_POWERPC_7450_v10,               7450),
    /* PowerPC 7450 v1.1 (G4)                                                */
    POWERPC_DEF("7450_v1.1",     CPU_POWERPC_7450_v11,               7450),
    /* PowerPC 7450 v1.2 (G4)                                                */
    POWERPC_DEF("7450_v1.2",     CPU_POWERPC_7450_v12,               7450),
    /* PowerPC 7450 v2.0 (G4)                                                */
    POWERPC_DEF("7450_v2.0",     CPU_POWERPC_7450_v20,               7450),
    /* PowerPC 7450 v2.1 (G4)                                                */
    POWERPC_DEF("7450_v2.1",     CPU_POWERPC_7450_v21,               7450),
    /* PowerPC 7441 (G4)                                                     */
    POWERPC_DEF("7441",          CPU_POWERPC_74x1,                   7440),
    /* PowerPC 7451 (G4)                                                     */
    POWERPC_DEF("7451",          CPU_POWERPC_74x1,                   7450),
    /* PowerPC 7441 v2.1 (G4)                                                */
    POWERPC_DEF("7441_v2.1",     CPU_POWERPC_7450_v21,               7440),
    /* PowerPC 7441 v2.3 (G4)                                                */
    POWERPC_DEF("7441_v2.3",     CPU_POWERPC_74x1_v23,               7440),
    /* PowerPC 7451 v2.3 (G4)                                                */
    POWERPC_DEF("7451_v2.3",     CPU_POWERPC_74x1_v23,               7450),
    /* PowerPC 7441 v2.10 (G4)                                                */
    POWERPC_DEF("7441_v2.10",    CPU_POWERPC_74x1_v210,              7440),
    /* PowerPC 7451 v2.10 (G4)                                               */
    POWERPC_DEF("7451_v2.10",    CPU_POWERPC_74x1_v210,              7450),
    /* PowerPC 7445 (G4)                                                     */
    POWERPC_DEF("7445",          CPU_POWERPC_74x5,                   7445),
    /* PowerPC 7455 (G4)                                                     */
    POWERPC_DEF("7455",          CPU_POWERPC_74x5,                   7455),
    /* Code name for PowerPC 7445/7455                                       */
    POWERPC_DEF("Apollo6",       CPU_POWERPC_74x5,                   7455),
    /* PowerPC 7445 v1.0 (G4)                                                */
    POWERPC_DEF("7445_v1.0",     CPU_POWERPC_74x5_v10,               7445),
    /* PowerPC 7455 v1.0 (G4)                                                */
    POWERPC_DEF("7455_v1.0",     CPU_POWERPC_74x5_v10,               7455),
    /* PowerPC 7445 v2.1 (G4)                                                */
    POWERPC_DEF("7445_v2.1",     CPU_POWERPC_74x5_v21,               7445),
    /* PowerPC 7455 v2.1 (G4)                                                */
    POWERPC_DEF("7455_v2.1",     CPU_POWERPC_74x5_v21,               7455),
    /* PowerPC 7445 v3.2 (G4)                                                */
    POWERPC_DEF("7445_v3.2",     CPU_POWERPC_74x5_v32,               7445),
    /* PowerPC 7455 v3.2 (G4)                                                */
    POWERPC_DEF("7455_v3.2",     CPU_POWERPC_74x5_v32,               7455),
    /* PowerPC 7445 v3.3 (G4)                                                */
    POWERPC_DEF("7445_v3.3",     CPU_POWERPC_74x5_v33,               7445),
    /* PowerPC 7455 v3.3 (G4)                                                */
    POWERPC_DEF("7455_v3.3",     CPU_POWERPC_74x5_v33,               7455),
    /* PowerPC 7445 v3.4 (G4)                                                */
    POWERPC_DEF("7445_v3.4",     CPU_POWERPC_74x5_v34,               7445),
    /* PowerPC 7455 v3.4 (G4)                                                */
    POWERPC_DEF("7455_v3.4",     CPU_POWERPC_74x5_v34,               7455),
    /* PowerPC 7447 (G4)                                                     */
    POWERPC_DEF("7447",          CPU_POWERPC_74x7,                   7445),
    /* PowerPC 7457 (G4)                                                     */
    POWERPC_DEF("7457",          CPU_POWERPC_74x7,                   7455),
    /* Code name for PowerPC 7447/7457                                       */
    POWERPC_DEF("Apollo7",       CPU_POWERPC_74x7,                   7455),
    /* PowerPC 7447 v1.0 (G4)                                                */
    POWERPC_DEF("7447_v1.0",     CPU_POWERPC_74x7_v10,               7445),
    /* PowerPC 7457 v1.0 (G4)                                                */
    POWERPC_DEF("7457_v1.0",     CPU_POWERPC_74x7_v10,               7455),
    /* PowerPC 7447 v1.1 (G4)                                                */
    POWERPC_DEF("7447_v1.1",     CPU_POWERPC_74x7_v11,               7445),
    /* PowerPC 7457 v1.1 (G4)                                                */
    POWERPC_DEF("7457_v1.1",     CPU_POWERPC_74x7_v11,               7455),
    /* PowerPC 7457 v1.2 (G4)                                                */
    POWERPC_DEF("7457_v1.2",     CPU_POWERPC_74x7_v12,               7455),
    /* PowerPC 7447A (G4)                                                    */
    POWERPC_DEF("7447A",         CPU_POWERPC_74x7A,                  7445),
    /* PowerPC 7457A (G4)                                                    */
    POWERPC_DEF("7457A",         CPU_POWERPC_74x7A,                  7455),
    /* PowerPC 7447A v1.0 (G4)                                               */
    POWERPC_DEF("7447A_v1.0",    CPU_POWERPC_74x7A_v10,              7445),
    /* PowerPC 7457A v1.0 (G4)                                               */
    POWERPC_DEF("7457A_v1.0",    CPU_POWERPC_74x7A_v10,              7455),
    /* Code name for PowerPC 7447A/7457A                                     */
    POWERPC_DEF("Apollo7PM",     CPU_POWERPC_74x7A_v10,              7455),
    /* PowerPC 7447A v1.1 (G4)                                               */
    POWERPC_DEF("7447A_v1.1",    CPU_POWERPC_74x7A_v11,              7445),
    /* PowerPC 7457A v1.1 (G4)                                               */
    POWERPC_DEF("7457A_v1.1",    CPU_POWERPC_74x7A_v11,              7455),
    /* PowerPC 7447A v1.2 (G4)                                               */
    POWERPC_DEF("7447A_v1.2",    CPU_POWERPC_74x7A_v12,              7445),
    /* PowerPC 7457A v1.2 (G4)                                               */
    POWERPC_DEF("7457A_v1.2",    CPU_POWERPC_74x7A_v12,              7455),
    /* 64 bits PowerPC                                                       */
#if defined (TARGET_PPC64)
    /* PowerPC 620                                                           */
    POWERPC_DEF("620",           CPU_POWERPC_620,                    620),
    /* Code name for PowerPC 620                                             */
    POWERPC_DEF("Trident",       CPU_POWERPC_620,                    620),
#if defined (TODO)
    /* PowerPC 630 (POWER3)                                                  */
    POWERPC_DEF("630",           CPU_POWERPC_630,                    630),
    POWERPC_DEF("POWER3",        CPU_POWERPC_630,                    630),
    /* Code names for POWER3                                                 */
    POWERPC_DEF("Boxer",         CPU_POWERPC_630,                    630),
    POWERPC_DEF("Dino",          CPU_POWERPC_630,                    630),
#endif
#if defined (TODO)
    /* PowerPC 631 (Power 3+)                                                */
    POWERPC_DEF("631",           CPU_POWERPC_631,                    631),
    POWERPC_DEF("POWER3+",       CPU_POWERPC_631,                    631),
#endif
#if defined (TODO)
    /* POWER4                                                                */
    POWERPC_DEF("POWER4",        CPU_POWERPC_POWER4,                 POWER4),
#endif
#if defined (TODO)
    /* POWER4p                                                               */
    POWERPC_DEF("POWER4+",       CPU_POWERPC_POWER4P,                POWER4P),
#endif
#if defined (TODO)
    /* POWER5                                                                */
    POWERPC_DEF("POWER5",        CPU_POWERPC_POWER5,                 POWER5),
    /* POWER5GR                                                              */
    POWERPC_DEF("POWER5gr",      CPU_POWERPC_POWER5GR,               POWER5),
#endif
#if defined (TODO)
    /* POWER5+                                                               */
    POWERPC_DEF("POWER5+",       CPU_POWERPC_POWER5P,                POWER5P),
    /* POWER5GS                                                              */
    POWERPC_DEF("POWER5gs",      CPU_POWERPC_POWER5GS,               POWER5P),
#endif
#if defined (TODO)
    /* POWER6                                                                */
    POWERPC_DEF("POWER6",        CPU_POWERPC_POWER6,                 POWER6),
    /* POWER6 running in POWER5 mode                                         */
    POWERPC_DEF("POWER6_5",      CPU_POWERPC_POWER6_5,               POWER5),
    /* POWER6A                                                               */
    POWERPC_DEF("POWER6A",       CPU_POWERPC_POWER6A,                POWER6),
#endif
    /* PowerPC 970                                                           */
    POWERPC_DEF("970",           CPU_POWERPC_970,                    970),
    /* PowerPC 970FX (G5)                                                    */
    POWERPC_DEF("970fx",         CPU_POWERPC_970FX,                  970FX),
    /* PowerPC 970FX v1.0 (G5)                                               */
    POWERPC_DEF("970fx_v1.0",    CPU_POWERPC_970FX_v10,              970FX),
    /* PowerPC 970FX v2.0 (G5)                                               */
    POWERPC_DEF("970fx_v2.0",    CPU_POWERPC_970FX_v20,              970FX),
    /* PowerPC 970FX v2.1 (G5)                                               */
    POWERPC_DEF("970fx_v2.1",    CPU_POWERPC_970FX_v21,              970FX),
    /* PowerPC 970FX v3.0 (G5)                                               */
    POWERPC_DEF("970fx_v3.0",    CPU_POWERPC_970FX_v30,              970FX),
    /* PowerPC 970FX v3.1 (G5)                                               */
    POWERPC_DEF("970fx_v3.1",    CPU_POWERPC_970FX_v31,              970FX),
    /* PowerPC 970GX (G5)                                                    */
    POWERPC_DEF("970gx",         CPU_POWERPC_970GX,                  970GX),
    /* PowerPC 970MP                                                         */
    POWERPC_DEF("970mp",         CPU_POWERPC_970MP,                  970MP),
    /* PowerPC 970MP v1.0                                                    */
    POWERPC_DEF("970mp_v1.0",    CPU_POWERPC_970MP_v10,              970MP),
    /* PowerPC 970MP v1.1                                                    */
    POWERPC_DEF("970mp_v1.1",    CPU_POWERPC_970MP_v11,              970MP),
#if defined (TODO)
    /* PowerPC Cell                                                          */
    POWERPC_DEF("Cell",          CPU_POWERPC_CELL,                   970),
#endif
#if defined (TODO)
    /* PowerPC Cell v1.0                                                     */
    POWERPC_DEF("Cell_v1.0",     CPU_POWERPC_CELL_v10,               970),
#endif
#if defined (TODO)
    /* PowerPC Cell v2.0                                                     */
    POWERPC_DEF("Cell_v2.0",     CPU_POWERPC_CELL_v20,               970),
#endif
#if defined (TODO)
    /* PowerPC Cell v3.0                                                     */
    POWERPC_DEF("Cell_v3.0",     CPU_POWERPC_CELL_v30,               970),
#endif
#if defined (TODO)
    /* PowerPC Cell v3.1                                                     */
    POWERPC_DEF("Cell_v3.1",     CPU_POWERPC_CELL_v31,               970),
#endif
#if defined (TODO)
    /* PowerPC Cell v3.2                                                     */
    POWERPC_DEF("Cell_v3.2",     CPU_POWERPC_CELL_v32,               970),
#endif
#if defined (TODO)
    /* RS64 (Apache/A35)                                                     */
    /* This one seems to support the whole POWER2 instruction set
     * and the PowerPC 64 one.
     */
    /* What about A10 & A30 ? */
    POWERPC_DEF("RS64",          CPU_POWERPC_RS64,                   RS64),
    POWERPC_DEF("Apache",        CPU_POWERPC_RS64,                   RS64),
    POWERPC_DEF("A35",           CPU_POWERPC_RS64,                   RS64),
#endif
#if defined (TODO)
    /* RS64-II (NorthStar/A50)                                               */
    POWERPC_DEF("RS64-II",       CPU_POWERPC_RS64II,                 RS64),
    POWERPC_DEF("NorthStar",     CPU_POWERPC_RS64II,                 RS64),
    POWERPC_DEF("A50",           CPU_POWERPC_RS64II,                 RS64),
#endif
#if defined (TODO)
    /* RS64-III (Pulsar)                                                     */
    POWERPC_DEF("RS64-III",      CPU_POWERPC_RS64III,                RS64),
    POWERPC_DEF("Pulsar",        CPU_POWERPC_RS64III,                RS64),
#endif
#if defined (TODO)
    /* RS64-IV (IceStar/IStar/SStar)                                         */
    POWERPC_DEF("RS64-IV",       CPU_POWERPC_RS64IV,                 RS64),
    POWERPC_DEF("IceStar",       CPU_POWERPC_RS64IV,                 RS64),
    POWERPC_DEF("IStar",         CPU_POWERPC_RS64IV,                 RS64),
    POWERPC_DEF("SStar",         CPU_POWERPC_RS64IV,                 RS64),
#endif
#endif /* defined (TARGET_PPC64) */
    /* POWER                                                                 */
#if defined (TODO)
    /* Original POWER                                                        */
    POWERPC_DEF("POWER",         CPU_POWERPC_POWER,                  POWER),
    POWERPC_DEF("RIOS",          CPU_POWERPC_POWER,                  POWER),
    POWERPC_DEF("RSC",           CPU_POWERPC_POWER,                  POWER),
    POWERPC_DEF("RSC3308",       CPU_POWERPC_POWER,                  POWER),
    POWERPC_DEF("RSC4608",       CPU_POWERPC_POWER,                  POWER),
#endif
#if defined (TODO)
    /* POWER2                                                                */
    POWERPC_DEF("POWER2",        CPU_POWERPC_POWER2,                 POWER),
    POWERPC_DEF("RSC2",          CPU_POWERPC_POWER2,                 POWER),
    POWERPC_DEF("P2SC",          CPU_POWERPC_POWER2,                 POWER),
#endif
    /* PA semi cores                                                         */
#if defined (TODO)
    /* PA PA6T */
    POWERPC_DEF("PA6T",          CPU_POWERPC_PA6T,                   PA6T),
#endif
    /* Generic PowerPCs                                                      */
#if defined (TARGET_PPC64)
    POWERPC_DEF("ppc64",         CPU_POWERPC_PPC64,                  PPC64),
#endif
    POWERPC_DEF("ppc32",         CPU_POWERPC_PPC32,                  PPC32),
    POWERPC_DEF("ppc",           CPU_POWERPC_DEFAULT,                DEFAULT),
    /* Fallback                                                              */
    POWERPC_DEF("default",       CPU_POWERPC_DEFAULT,                DEFAULT),
};

/*****************************************************************************/
/* Generic CPU instanciation routine                                         */
static void init_ppc_proc (CPUPPCState *env, const ppc_def_t *def)
{
#if !defined(CONFIG_USER_ONLY)
    int i;

    env->irq_inputs = NULL;
    /* Set all exception vectors to an invalid address */
    for (i = 0; i < POWERPC_EXCP_NB; i++)
        env->excp_vectors[i] = (target_ulong)(-1ULL);
    env->excp_prefix = 0x00000000;
    env->ivor_mask = 0x00000000;
    env->ivpr_mask = 0x00000000;
    /* Default MMU definitions */
    env->nb_BATs = 0;
    env->nb_tlb = 0;
    env->nb_ways = 0;
#endif
    /* Register SPR common to all PowerPC implementations */
    gen_spr_generic(env);
    spr_register(env, SPR_PVR, "PVR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 def->pvr);
    /* Register SVR if it's defined to anything else than POWERPC_SVR_NONE */
    if (def->svr != POWERPC_SVR_NONE) {
        if (def->svr & POWERPC_SVR_E500) {
            spr_register(env, SPR_E500_SVR, "SVR",
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, SPR_NOACCESS,
                         def->svr & ~POWERPC_SVR_E500);
        } else {
            spr_register(env, SPR_SVR, "SVR",
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, SPR_NOACCESS,
                         def->svr);
        }
    }
    /* PowerPC implementation specific initialisations (SPRs, timers, ...) */
    (*def->init_proc)(env);
    /* MSR bits & flags consistency checks */
    if (env->msr_mask & (1 << 25)) {
        switch (env->flags & (POWERPC_FLAG_SPE | POWERPC_FLAG_VRE)) {
        case POWERPC_FLAG_SPE:
        case POWERPC_FLAG_VRE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_SPE or POWERPC_FLAG_VRE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_SPE | POWERPC_FLAG_VRE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_SPE nor POWERPC_FLAG_VRE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 17)) {
        switch (env->flags & (POWERPC_FLAG_TGPR | POWERPC_FLAG_CE)) {
        case POWERPC_FLAG_TGPR:
        case POWERPC_FLAG_CE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_TGPR or POWERPC_FLAG_CE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_TGPR | POWERPC_FLAG_CE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_TGPR nor POWERPC_FLAG_CE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 10)) {
        switch (env->flags & (POWERPC_FLAG_SE | POWERPC_FLAG_DWE |
                              POWERPC_FLAG_UBLE)) {
        case POWERPC_FLAG_SE:
        case POWERPC_FLAG_DWE:
        case POWERPC_FLAG_UBLE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_SE or POWERPC_FLAG_DWE or "
                    "POWERPC_FLAG_UBLE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_SE | POWERPC_FLAG_DWE |
                             POWERPC_FLAG_UBLE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_SE nor POWERPC_FLAG_DWE nor "
                "POWERPC_FLAG_UBLE\n");
            exit(1);
    }
    if (env->msr_mask & (1 << 9)) {
        switch (env->flags & (POWERPC_FLAG_BE | POWERPC_FLAG_DE)) {
        case POWERPC_FLAG_BE:
        case POWERPC_FLAG_DE:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_BE or POWERPC_FLAG_DE\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_BE | POWERPC_FLAG_DE)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_BE nor POWERPC_FLAG_DE\n");
        exit(1);
    }
    if (env->msr_mask & (1 << 2)) {
        switch (env->flags & (POWERPC_FLAG_PX | POWERPC_FLAG_PMM)) {
        case POWERPC_FLAG_PX:
        case POWERPC_FLAG_PMM:
            break;
        default:
            fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                    "Should define POWERPC_FLAG_PX or POWERPC_FLAG_PMM\n");
            exit(1);
        }
    } else if (env->flags & (POWERPC_FLAG_PX | POWERPC_FLAG_PMM)) {
        fprintf(stderr, "PowerPC MSR definition inconsistency\n"
                "Should not define POWERPC_FLAG_PX nor POWERPC_FLAG_PMM\n");
        exit(1);
    }
    if ((env->flags & (POWERPC_FLAG_RTC_CLK | POWERPC_FLAG_BUS_CLK)) == 0) {
        fprintf(stderr, "PowerPC flags inconsistency\n"
                "Should define the time-base and decrementer clock source\n");
        exit(1);
    }
    /* Allocate TLBs buffer when needed */
#if !defined(CONFIG_USER_ONLY)
    if (env->nb_tlb != 0) {
        int nb_tlb = env->nb_tlb;
        if (env->id_tlbs != 0)
            nb_tlb *= 2;
        env->tlb = qemu_mallocz(nb_tlb * sizeof(ppc_tlb_t));
        /* Pre-compute some useful values */
        env->tlb_per_way = env->nb_tlb / env->nb_ways;
    }
    if (env->irq_inputs == NULL) {
        fprintf(stderr, "WARNING: no internal IRQ controller registered.\n"
                " Attempt Qemu to crash very soon !\n");
    }
#endif
    if (env->check_pow == NULL) {
        fprintf(stderr, "WARNING: no power management check handler "
                "registered.\n"
                " Attempt Qemu to crash very soon !\n");
    }
}

#if defined(PPC_DUMP_CPU)
static void dump_ppc_sprs (CPUPPCState *env)
{
    ppc_spr_t *spr;
#if !defined(CONFIG_USER_ONLY)
    uint32_t sr, sw;
#endif
    uint32_t ur, uw;
    int i, j, n;

    printf("Special purpose registers:\n");
    for (i = 0; i < 32; i++) {
        for (j = 0; j < 32; j++) {
            n = (i << 5) | j;
            spr = &env->spr_cb[n];
            uw = spr->uea_write != NULL && spr->uea_write != SPR_NOACCESS;
            ur = spr->uea_read != NULL && spr->uea_read != SPR_NOACCESS;
#if !defined(CONFIG_USER_ONLY)
            sw = spr->oea_write != NULL && spr->oea_write != SPR_NOACCESS;
            sr = spr->oea_read != NULL && spr->oea_read != SPR_NOACCESS;
            if (sw || sr || uw || ur) {
                printf("SPR: %4d (%03x) %-8s s%c%c u%c%c\n",
                       (i << 5) | j, (i << 5) | j, spr->name,
                       sw ? 'w' : '-', sr ? 'r' : '-',
                       uw ? 'w' : '-', ur ? 'r' : '-');
            }
#else
            if (uw || ur) {
                printf("SPR: %4d (%03x) %-8s u%c%c\n",
                       (i << 5) | j, (i << 5) | j, spr->name,
                       uw ? 'w' : '-', ur ? 'r' : '-');
            }
#endif
        }
    }
    fflush(stdout);
    fflush(stderr);
}
#endif

/*****************************************************************************/
#include <stdlib.h>
#include <string.h>

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
#if defined(DO_PPC_STATISTICS) || defined(PPC_DUMP_CPU)
        printf("           Registered handler '%s' - new handler '%s'\n",
               ppc_opcodes[idx]->oname, handler->oname);
#endif
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
#if defined(DO_PPC_STATISTICS) || defined(PPC_DUMP_CPU)
            printf("           Registered handler '%s' - new handler '%s'\n",
                   ind_table(table[idx1])[idx2]->oname, handler->oname);
#endif
            return -1;
        }
    }
    if (handler != NULL &&
        insert_in_table(ind_table(table[idx1]), idx2, handler) < 0) {
        printf("*** ERROR: opcode %02x already assigned in "
               "opcode table %02x\n", idx2, idx1);
#if defined(DO_PPC_STATISTICS) || defined(PPC_DUMP_CPU)
        printf("           Registered handler '%s' - new handler '%s'\n",
               ind_table(table[idx1])[idx2]->oname, handler->oname);
#endif
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

/*****************************************************************************/
static int create_ppc_opcodes (CPUPPCState *env, const ppc_def_t *def)
{
    opcode_t *opc, *start, *end;

    fill_new_table(env->opcodes, 0x40);
    if (&opc_start < &opc_end) {
        start = &opc_start;
        end = &opc_end;
    } else {
        start = &opc_end;
        end = &opc_start;
    }
    for (opc = start + 1; opc != end; opc++) {
        if ((opc->handler.type & def->insns_flags) != 0) {
            if (register_insn(env->opcodes, opc) < 0) {
                printf("*** ERROR initializing PowerPC instruction "
                       "0x%02x 0x%02x 0x%02x\n", opc->opc1, opc->opc2,
                       opc->opc3);
                return -1;
            }
        }
    }
    fix_opcode_tables(env->opcodes);
    fflush(stdout);
    fflush(stderr);

    return 0;
}

#if defined(PPC_DUMP_CPU)
static void dump_ppc_insns (CPUPPCState *env)
{
    opc_handler_t **table, *handler;
    const char *p, *q;
    uint8_t opc1, opc2, opc3;

    printf("Instructions set:\n");
    /* opc1 is 6 bits long */
    for (opc1 = 0x00; opc1 < 0x40; opc1++) {
        table = env->opcodes;
        handler = table[opc1];
        if (is_indirect_opcode(handler)) {
            /* opc2 is 5 bits long */
            for (opc2 = 0; opc2 < 0x20; opc2++) {
                table = env->opcodes;
                handler = env->opcodes[opc1];
                table = ind_table(handler);
                handler = table[opc2];
                if (is_indirect_opcode(handler)) {
                    table = ind_table(handler);
                    /* opc3 is 5 bits long */
                    for (opc3 = 0; opc3 < 0x20; opc3++) {
                        handler = table[opc3];
                        if (handler->handler != &gen_invalid) {
                            /* Special hack to properly dump SPE insns */
                            p = strchr(handler->oname, '_');
                            if (p == NULL) {
                                printf("INSN: %02x %02x %02x (%02d %04d) : "
                                       "%s\n",
                                       opc1, opc2, opc3, opc1,
                                       (opc3 << 5) | opc2,
                                       handler->oname);
                            } else {
                                q = "speundef";
                                if ((p - handler->oname) != strlen(q) ||
                                    memcmp(handler->oname, q, strlen(q)) != 0) {
                                    /* First instruction */
                                    printf("INSN: %02x %02x %02x (%02d %04d) : "
                                           "%.*s\n",
                                           opc1, opc2 << 1, opc3, opc1,
                                           (opc3 << 6) | (opc2 << 1),
                                           (int)(p - handler->oname),
                                           handler->oname);
                                }
                                if (strcmp(p + 1, q) != 0) {
                                    /* Second instruction */
                                    printf("INSN: %02x %02x %02x (%02d %04d) : "
                                           "%s\n",
                                           opc1, (opc2 << 1) | 1, opc3, opc1,
                                           (opc3 << 6) | (opc2 << 1) | 1,
                                           p + 1);
                                }
                            }
                        }
                    }
                } else {
                    if (handler->handler != &gen_invalid) {
                        printf("INSN: %02x %02x -- (%02d %04d) : %s\n",
                               opc1, opc2, opc1, opc2, handler->oname);
                    }
                }
            }
        } else {
            if (handler->handler != &gen_invalid) {
                printf("INSN: %02x -- -- (%02d ----) : %s\n",
                       opc1, opc1, handler->oname);
            }
        }
    }
}
#endif

static int gdb_get_float_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        stfq_p(mem_buf, env->fpr[n]);
        return 8;
    }
    if (n == 32) {
        /* FPSCR not implemented  */
        memset(mem_buf, 0, 4);
        return 4;
    }
    return 0;
}

static int gdb_set_float_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        env->fpr[n] = ldfq_p(mem_buf);
        return 8;
    }
    if (n == 32) {
        /* FPSCR not implemented  */
        return 4;
    }
    return 0;
}

static int gdb_get_avr_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
#ifdef WORDS_BIGENDIAN
        stq_p(mem_buf, env->avr[n].u64[0]);
        stq_p(mem_buf+8, env->avr[n].u64[1]);
#else
        stq_p(mem_buf, env->avr[n].u64[1]);
        stq_p(mem_buf+8, env->avr[n].u64[0]);
#endif
        return 16;
    }
    if (n == 33) {
        stl_p(mem_buf, env->vscr);
        return 4;
    }
    if (n == 34) {
        stl_p(mem_buf, (uint32_t)env->spr[SPR_VRSAVE]);
        return 4;
    }
    return 0;
}

static int gdb_set_avr_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
#ifdef WORDS_BIGENDIAN
        env->avr[n].u64[0] = ldq_p(mem_buf);
        env->avr[n].u64[1] = ldq_p(mem_buf+8);
#else
        env->avr[n].u64[1] = ldq_p(mem_buf);
        env->avr[n].u64[0] = ldq_p(mem_buf+8);
#endif
        return 16;
    }
    if (n == 33) {
        env->vscr = ldl_p(mem_buf);
        return 4;
    }
    if (n == 34) {
        env->spr[SPR_VRSAVE] = (target_ulong)ldl_p(mem_buf);
        return 4;
    }
    return 0;
}

static int gdb_get_spe_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
#if defined(TARGET_PPC64)
        stl_p(mem_buf, env->gpr[n] >> 32);
#else
        stl_p(mem_buf, env->gprh[n]);
#endif
        return 4;
    }
    if (n == 33) {
        stq_p(mem_buf, env->spe_acc);
        return 8;
    }
    if (n == 34) {
        /* SPEFSCR not implemented */
        memset(mem_buf, 0, 4);
        return 4;
    }
    return 0;
}

static int gdb_set_spe_reg(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
#if defined(TARGET_PPC64)
        target_ulong lo = (uint32_t)env->gpr[n];
        target_ulong hi = (target_ulong)ldl_p(mem_buf) << 32;
        env->gpr[n] = lo | hi;
#else
        env->gprh[n] = ldl_p(mem_buf);
#endif
        return 4;
    }
    if (n == 33) {
        env->spe_acc = ldq_p(mem_buf);
        return 8;
    }
    if (n == 34) {
        /* SPEFSCR not implemented */
        return 4;
    }
    return 0;
}

int cpu_ppc_register_internal (CPUPPCState *env, const ppc_def_t *def)
{
    env->msr_mask = def->msr_mask;
    env->mmu_model = def->mmu_model;
    env->excp_model = def->excp_model;
    env->bus_model = def->bus_model;
    env->flags = def->flags;
    env->bfd_mach = def->bfd_mach;
    env->check_pow = def->check_pow;
    if (create_ppc_opcodes(env, def) < 0)
        return -1;
    init_ppc_proc(env, def);

    if (def->insns_flags & PPC_FLOAT) {
        gdb_register_coprocessor(env, gdb_get_float_reg, gdb_set_float_reg,
                                 33, "power-fpu.xml", 0);
    }
    if (def->insns_flags & PPC_ALTIVEC) {
        gdb_register_coprocessor(env, gdb_get_avr_reg, gdb_set_avr_reg,
                                 34, "power-altivec.xml", 0);
    }
    if (def->insns_flags & PPC_SPE) {
        gdb_register_coprocessor(env, gdb_get_spe_reg, gdb_set_spe_reg,
                                 34, "power-spe.xml", 0);
    }

#if defined(PPC_DUMP_CPU)
    {
        const char *mmu_model, *excp_model, *bus_model;
        switch (env->mmu_model) {
        case POWERPC_MMU_32B:
            mmu_model = "PowerPC 32";
            break;
        case POWERPC_MMU_SOFT_6xx:
            mmu_model = "PowerPC 6xx/7xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_74xx:
            mmu_model = "PowerPC 74xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_4xx:
            mmu_model = "PowerPC 4xx with software driven TLBs";
            break;
        case POWERPC_MMU_SOFT_4xx_Z:
            mmu_model = "PowerPC 4xx with software driven TLBs "
                "and zones protections";
            break;
        case POWERPC_MMU_REAL:
            mmu_model = "PowerPC real mode only";
            break;
        case POWERPC_MMU_MPC8xx:
            mmu_model = "PowerPC MPC8xx";
            break;
        case POWERPC_MMU_BOOKE:
            mmu_model = "PowerPC BookE";
            break;
        case POWERPC_MMU_BOOKE_FSL:
            mmu_model = "PowerPC BookE FSL";
            break;
        case POWERPC_MMU_601:
            mmu_model = "PowerPC 601";
            break;
#if defined (TARGET_PPC64)
        case POWERPC_MMU_64B:
            mmu_model = "PowerPC 64";
            break;
        case POWERPC_MMU_620:
            mmu_model = "PowerPC 620";
            break;
#endif
        default:
            mmu_model = "Unknown or invalid";
            break;
        }
        switch (env->excp_model) {
        case POWERPC_EXCP_STD:
            excp_model = "PowerPC";
            break;
        case POWERPC_EXCP_40x:
            excp_model = "PowerPC 40x";
            break;
        case POWERPC_EXCP_601:
            excp_model = "PowerPC 601";
            break;
        case POWERPC_EXCP_602:
            excp_model = "PowerPC 602";
            break;
        case POWERPC_EXCP_603:
            excp_model = "PowerPC 603";
            break;
        case POWERPC_EXCP_603E:
            excp_model = "PowerPC 603e";
            break;
        case POWERPC_EXCP_604:
            excp_model = "PowerPC 604";
            break;
        case POWERPC_EXCP_7x0:
            excp_model = "PowerPC 740/750";
            break;
        case POWERPC_EXCP_7x5:
            excp_model = "PowerPC 745/755";
            break;
        case POWERPC_EXCP_74xx:
            excp_model = "PowerPC 74xx";
            break;
        case POWERPC_EXCP_BOOKE:
            excp_model = "PowerPC BookE";
            break;
#if defined (TARGET_PPC64)
        case POWERPC_EXCP_970:
            excp_model = "PowerPC 970";
            break;
#endif
        default:
            excp_model = "Unknown or invalid";
            break;
        }
        switch (env->bus_model) {
        case PPC_FLAGS_INPUT_6xx:
            bus_model = "PowerPC 6xx";
            break;
        case PPC_FLAGS_INPUT_BookE:
            bus_model = "PowerPC BookE";
            break;
        case PPC_FLAGS_INPUT_405:
            bus_model = "PowerPC 405";
            break;
        case PPC_FLAGS_INPUT_401:
            bus_model = "PowerPC 401/403";
            break;
        case PPC_FLAGS_INPUT_RCPU:
            bus_model = "RCPU / MPC8xx";
            break;
#if defined (TARGET_PPC64)
        case PPC_FLAGS_INPUT_970:
            bus_model = "PowerPC 970";
            break;
#endif
        default:
            bus_model = "Unknown or invalid";
            break;
        }
        printf("PowerPC %-12s : PVR %08x MSR %016" PRIx64 "\n"
               "    MMU model        : %s\n",
               def->name, def->pvr, def->msr_mask, mmu_model);
#if !defined(CONFIG_USER_ONLY)
        if (env->tlb != NULL) {
            printf("                       %d %s TLB in %d ways\n",
                   env->nb_tlb, env->id_tlbs ? "splitted" : "merged",
                   env->nb_ways);
        }
#endif
        printf("    Exceptions model : %s\n"
               "    Bus model        : %s\n",
               excp_model, bus_model);
        printf("    MSR features     :\n");
        if (env->flags & POWERPC_FLAG_SPE)
            printf("                        signal processing engine enable"
                   "\n");
        else if (env->flags & POWERPC_FLAG_VRE)
            printf("                        vector processor enable\n");
        if (env->flags & POWERPC_FLAG_TGPR)
            printf("                        temporary GPRs\n");
        else if (env->flags & POWERPC_FLAG_CE)
            printf("                        critical input enable\n");
        if (env->flags & POWERPC_FLAG_SE)
            printf("                        single-step trace mode\n");
        else if (env->flags & POWERPC_FLAG_DWE)
            printf("                        debug wait enable\n");
        else if (env->flags & POWERPC_FLAG_UBLE)
            printf("                        user BTB lock enable\n");
        if (env->flags & POWERPC_FLAG_BE)
            printf("                        branch-step trace mode\n");
        else if (env->flags & POWERPC_FLAG_DE)
            printf("                        debug interrupt enable\n");
        if (env->flags & POWERPC_FLAG_PX)
            printf("                        inclusive protection\n");
        else if (env->flags & POWERPC_FLAG_PMM)
            printf("                        performance monitor mark\n");
        if (env->flags == POWERPC_FLAG_NONE)
            printf("                        none\n");
        printf("    Time-base/decrementer clock source: %s\n",
               env->flags & POWERPC_FLAG_RTC_CLK ? "RTC clock" : "bus clock");
    }
    dump_ppc_insns(env);
    dump_ppc_sprs(env);
    fflush(stdout);
#endif

    return 0;
}

static const ppc_def_t *ppc_find_by_pvr (uint32_t pvr)
{
    const ppc_def_t *ret;
    uint32_t pvr_rev;
    int i, best, match, best_match, max;

    ret = NULL;
    max = ARRAY_SIZE(ppc_defs);
    best = -1;
    pvr_rev = pvr & 0xFFFF;
    /* We want all specified bits to match */
    best_match = 32 - ctz32(pvr_rev);
    for (i = 0; i < max; i++) {
        /* We check that the 16 higher bits are the same to ensure the CPU
         * model will be the choosen one.
         */
        if (((pvr ^ ppc_defs[i].pvr) >> 16) == 0) {
            /* We want as much as possible of the low-level 16 bits
             * to be the same but we allow inexact matches.
             */
            match = clz32(pvr_rev ^ (ppc_defs[i].pvr & 0xFFFF));
            /* We check '>=' instead of '>' because the PPC_defs table
             * is ordered by increasing revision.
             * Then, we will match the higher revision compatible
             * with the requested PVR
             */
            if (match >= best_match) {
                best = i;
                best_match = match;
            }
        }
    }
    if (best != -1)
        ret = &ppc_defs[best];

    return ret;
}

#include <ctype.h>

const ppc_def_t *cpu_ppc_find_by_name (const char *name)
{
    const ppc_def_t *ret;
    const char *p;
    int i, max, len;

    /* Check if the given name is a PVR */
    len = strlen(name);
    if (len == 10 && name[0] == '0' && name[1] == 'x') {
        p = name + 2;
        goto check_pvr;
    } else if (len == 8) {
        p = name;
    check_pvr:
        for (i = 0; i < 8; i++) {
            if (!qemu_isxdigit(*p++))
                break;
        }
        if (i == 8)
            return ppc_find_by_pvr(strtoul(name, NULL, 16));
    }
    ret = NULL;
    max = ARRAY_SIZE(ppc_defs);
    for (i = 0; i < max; i++) {
        if (strcasecmp(name, ppc_defs[i].name) == 0) {
            ret = &ppc_defs[i];
            break;
        }
    }

    return ret;
}

void ppc_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    int i, max;

    max = ARRAY_SIZE(ppc_defs);
    for (i = 0; i < max; i++) {
        (*cpu_fprintf)(f, "PowerPC %-16s PVR %08x\n",
                       ppc_defs[i].name, ppc_defs[i].pvr);
    }
}
