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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* A lot of PowerPC definition have been included here.
 * Most of them are not usable for now but have been kept
 * inside "#if defined(TODO) ... #endif" statements to make tests easier.
 */

//#define PPC_DUMP_CPU
//#define PPC_DEBUG_SPR

struct ppc_def_t {
    const unsigned char *name;
    uint32_t pvr;
    uint32_t pvr_mask;
    uint32_t insns_flags;
    uint32_t flags;
    uint64_t msr_mask;
};

/* Generic callbacks:
 * do nothing but store/retrieve spr value
 */
static void spr_read_generic (void *opaque, int sprn)
{
    gen_op_load_spr(sprn);
}

static void spr_write_generic (void *opaque, int sprn)
{
    gen_op_store_spr(sprn);
}

/* SPR common to all PowerPC */
/* XER */
static void spr_read_xer (void *opaque, int sprn)
{
    gen_op_load_xer();
}

static void spr_write_xer (void *opaque, int sprn)
{
    gen_op_store_xer();
}

/* LR */
static void spr_read_lr (void *opaque, int sprn)
{
    gen_op_load_lr();
}

static void spr_write_lr (void *opaque, int sprn)
{
    gen_op_store_lr();
}

/* CTR */
static void spr_read_ctr (void *opaque, int sprn)
{
    gen_op_load_ctr();
}

static void spr_write_ctr (void *opaque, int sprn)
{
    gen_op_store_ctr();
}

/* User read access to SPR */
/* USPRx */
/* UMMCRx */
/* UPMCx */
/* USIA */
/* UDECR */
static void spr_read_ureg (void *opaque, int sprn)
{
    gen_op_load_spr(sprn + 0x10);
}

/* SPR common to all non-embedded PowerPC */
/* DECR */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_decr (void *opaque, int sprn)
{
    gen_op_load_decr();
}

static void spr_write_decr (void *opaque, int sprn)
{
    gen_op_store_decr();
}
#endif

/* SPR common to all non-embedded PowerPC, except 601 */
/* Time base */
static void spr_read_tbl (void *opaque, int sprn)
{
    gen_op_load_tbl();
}

static void spr_read_tbu (void *opaque, int sprn)
{
    gen_op_load_tbu();
}

#if !defined(CONFIG_USER_ONLY)
static void spr_write_tbl (void *opaque, int sprn)
{
    gen_op_store_tbl();
}

static void spr_write_tbu (void *opaque, int sprn)
{
    gen_op_store_tbu();
}
#endif

#if !defined(CONFIG_USER_ONLY)
/* IBAT0U...IBAT0U */
/* IBAT0L...IBAT7L */
static void spr_read_ibat (void *opaque, int sprn)
{
    gen_op_load_ibat(sprn & 1, (sprn - SPR_IBAT0U) / 2);
}

static void spr_read_ibat_h (void *opaque, int sprn)
{
    gen_op_load_ibat(sprn & 1, (sprn - SPR_IBAT4U) / 2);
}

static void spr_write_ibatu (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_ibatu((sprn - SPR_IBAT0U) / 2);
    RET_STOP(ctx);
}

static void spr_write_ibatu_h (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_ibatu((sprn - SPR_IBAT4U) / 2);
    RET_STOP(ctx);
}

static void spr_write_ibatl (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_ibatl((sprn - SPR_IBAT0L) / 2);
    RET_STOP(ctx);
}

static void spr_write_ibatl_h (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_ibatl((sprn - SPR_IBAT4L) / 2);
    RET_STOP(ctx);
}

/* DBAT0U...DBAT7U */
/* DBAT0L...DBAT7L */
static void spr_read_dbat (void *opaque, int sprn)
{
    gen_op_load_dbat(sprn & 1, (sprn - SPR_DBAT0U) / 2);
}

static void spr_read_dbat_h (void *opaque, int sprn)
{
    gen_op_load_dbat(sprn & 1, (sprn - SPR_DBAT4U) / 2);
}

static void spr_write_dbatu (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_dbatu((sprn - SPR_DBAT0U) / 2);
    RET_STOP(ctx);
}

static void spr_write_dbatu_h (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_dbatu((sprn - SPR_DBAT4U) / 2);
    RET_STOP(ctx);
}

static void spr_write_dbatl (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_dbatl((sprn - SPR_DBAT0L) / 2);
    RET_STOP(ctx);
}

static void spr_write_dbatl_h (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_dbatl((sprn - SPR_DBAT4L) / 2);
    RET_STOP(ctx);
}

/* SDR1 */
static void spr_read_sdr1 (void *opaque, int sprn)
{
    gen_op_load_sdr1();
}

static void spr_write_sdr1 (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_sdr1();
    RET_STOP(ctx);
}

/* 64 bits PowerPC specific SPRs */
/* ASR */
#if defined(TARGET_PPC64)
static void spr_read_asr (void *opaque, int sprn)
{
    gen_op_load_asr();
}

static void spr_write_asr (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_asr();
    RET_STOP(ctx);
}
#endif
#endif /* !defined(CONFIG_USER_ONLY) */

/* PowerPC 601 specific registers */
/* RTC */
static void spr_read_601_rtcl (void *opaque, int sprn)
{
    gen_op_load_601_rtcl();
}

static void spr_read_601_rtcu (void *opaque, int sprn)
{
    gen_op_load_601_rtcu();
}

#if !defined(CONFIG_USER_ONLY)
static void spr_write_601_rtcu (void *opaque, int sprn)
{
    gen_op_store_601_rtcu();
}

static void spr_write_601_rtcl (void *opaque, int sprn)
{
    gen_op_store_601_rtcl();
}
#endif

/* Unified bats */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_601_ubat (void *opaque, int sprn)
{
    gen_op_load_601_bat(sprn & 1, (sprn - SPR_IBAT0U) / 2);
}

static void spr_write_601_ubatu (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_601_batu((sprn - SPR_IBAT0U) / 2);
    RET_STOP(ctx);
}

static void spr_write_601_ubatl (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_601_batl((sprn - SPR_IBAT0L) / 2);
    RET_STOP(ctx);
}
#endif

/* PowerPC 40x specific registers */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_40x_pit (void *opaque, int sprn)
{
    gen_op_load_40x_pit();
}

static void spr_write_40x_pit (void *opaque, int sprn)
{
    gen_op_store_40x_pit();
}

static void spr_write_booke_tcr (void *opaque, int sprn)
{
    gen_op_store_booke_tcr();
}

static void spr_write_booke_tsr (void *opaque, int sprn)
{
    gen_op_store_booke_tsr();
}
#endif

/* PowerPC 403 specific registers */
/* PBL1 / PBU1 / PBL2 / PBU2 */
#if !defined(CONFIG_USER_ONLY)
static void spr_read_403_pbr (void *opaque, int sprn)
{
    gen_op_load_403_pb(sprn - SPR_403_PBL1);
}

static void spr_write_403_pbr (void *opaque, int sprn)
{
    DisasContext *ctx = opaque;

    gen_op_store_403_pb(sprn - SPR_403_PBL1);
    RET_STOP(ctx);
}

static void spr_write_pir (void *opaque, int sprn)
{
    gen_op_store_pir();
}
#endif

#if defined(CONFIG_USER_ONLY)
#define spr_register(env, num, name, uea_read, uea_write,                     \
                     oea_read, oea_write, initial_value)                      \
do {                                                                          \
     _spr_register(env, num, name, uea_read, uea_write, initial_value);       \
} while (0)
static inline void _spr_register (CPUPPCState *env, int num,
                                  const unsigned char *name,
                                  void (*uea_read)(void *opaque, int sprn),
                                  void (*uea_write)(void *opaque, int sprn),
                                  target_ulong initial_value)
#else
static inline void spr_register (CPUPPCState *env, int num,
                                 const unsigned char *name,
                                 void (*uea_read)(void *opaque, int sprn),
                                 void (*uea_write)(void *opaque, int sprn),
                                 void (*oea_read)(void *opaque, int sprn),
                                 void (*oea_write)(void *opaque, int sprn),
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
    printf("*** register spr %d (%03x) %s val " REGX "\n", num, num, name,
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
    env->nb_BATs = 4;
}

/* BATs 4-7 */
static void gen_high_BATs (CPUPPCState *env)
{
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
    env->nb_BATs = 8;
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
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
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
    /* XXX : not implemented */
    spr_register(env, SPR_L2CR, "L2CR",
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
    spr_register(env, SPR_SIA, "SIA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UMMCR0, "UMMCR0",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UMMCR1, "UMMCR1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UPMC1, "UPMC1",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UPMC2, "UPMC2",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UPMC3, "UPMC3",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_UPMC4, "UPMC4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USIA, "USIA",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
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
    /* External access control */
    /* XXX : not implemented */
    spr_register(env, SPR_EAR, "EAR",
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
    spr_register(env, SPR_SIA, "SIA",
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
    spr_register(env, SPR_MBAR, "MBAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* System version register */
    /* SVR */
    spr_register(env, SPR_SVR, "SVR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Exception processing */
    spr_register(env, SPR_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_CSRR1, "CSRR1",
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
    spr_register(env, SPR_ESASR, "ESASR",
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
}

/* PowerPC BookE SPR */
static void gen_spr_BookE (CPUPPCState *env)
{
    /* Processor identification */
    spr_register(env, SPR_BOOKE_PIR, "PIR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_pir,
                 0x00000000);
    /* Interrupt processing */
    spr_register(env, SPR_CSRR0, "CSRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_CSRR1, "CSRR1",
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
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_DEAR, "DEAR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_ESR, "ESR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_EVPR, "EVPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR0, "IVOR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR1, "IVOR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR2, "IVOR2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR3, "IVOR3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR4, "IVOR4",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR5, "IVOR5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR6, "IVOR6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR7, "IVOR7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR8, "IVOR8",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR9, "IVOR9",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR10, "IVOR10",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR11, "IVOR11",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR12, "IVOR12",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR13, "IVOR13",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR14, "IVOR14",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_BOOKE_IVOR15, "IVOR15",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
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
    spr_register(env, SPR_440_DVT0, "DVT0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DVT1, "DVT1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DVT2, "DVT2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DVT3, "DVT3",
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
    spr_register(env, SPR_440_IVT0, "IVT0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_IVT1, "IVT1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_IVT2, "IVT2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_IVT3, "IVT3",
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
    spr_register(env, SPR_440_DCBTRH, "DCBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_DCBTRL, "DCBTRL",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_4xx_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ICBTRH, "ICBTRH",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_440_ICBTRL, "ICBTRL",
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
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DCCR, "DCCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DCWR, "DCWR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_ICCR, "ICCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_4xx_ICDBDR, "ICDBDR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    /* Bus access control */
    spr_register(env, SPR_40x_SGR, "SGR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0xFFFFFFFF);
    spr_register(env, SPR_40x_ZPR, "ZPR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* MMU */
    spr_register(env, SPR_40x_PID, "PID",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
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
                 &spr_read_generic, &spr_write_generic,
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
    /* Debug interface */
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
    spr_register(env, SPR_40x_DBCR0, "DBCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DBSR, "DBSR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 /* Last reset was system reset (system boot */
                 0x00000300);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC1, "IAC1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 405 implementation */
static void gen_spr_405 (CPUPPCState *env)
{
    spr_register(env, SPR_4xx_CCR0, "CCR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00700000);
    /* Debug */
    /* XXX : not implemented */
    spr_register(env, SPR_405_DBCR1, "DBCR1",
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
    /* XXX : not implemented */
    spr_register(env, SPR_405_SLER, "SLER",
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
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG5, "SPRG5",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG6, "SPRG6",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_SPRG7, "SPRG7",
                 SPR_NOACCESS, SPR_NOACCESS,
                 SPR_NOACCESS, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    /* Debug */
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
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
    /* XXX: not implemented */
    spr_register(env, SPR_403_CDBCR, "CDBCR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC 403 implementation */
static void gen_spr_403 (CPUPPCState *env)
{
    /* MMU */
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
    /* Debug */
    /* XXX : not implemented */
    spr_register(env, SPR_40x_DAC2, "DAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* XXX : not implemented */
    spr_register(env, SPR_40x_IAC2, "IAC2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}

/* SPR specific to PowerPC compression coprocessor extension */
#if defined (TODO)
static void gen_spr_compress (CPUPPCState *env)
{
    spr_register(env, SPR_401_SKR, "SKR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
}
#endif

// XXX: TODO (64 bits PowerPC SPRs)
/*
 * ASR => SPR 280 (64 bits)
 * FPECR => SPR 1022 (?)
 * VRSAVE => SPR 256 (Altivec)
 * SCOMC => SPR 276 (64 bits ?)
 * SCOMD => SPR 277 (64 bits ?)
 * HSPRG0 => SPR 304 (hypervisor)
 * HSPRG1 => SPR 305 (hypervisor)
 * HDEC => SPR 310 (hypervisor)
 * HIOR => SPR 311 (hypervisor)
 * RMOR => SPR 312 (970)
 * HRMOR => SPR 313 (hypervisor)
 * HSRR0 => SPR 314 (hypervisor)
 * HSRR1 => SPR 315 (hypervisor)
 * LPCR => SPR 316 (970)
 * LPIDR => SPR 317 (970)
 * ... and more (thermal management, performance counters, ...)
 */

static void init_ppc_proc (CPUPPCState *env, ppc_def_t *def)
{
    env->reserve = -1;
    /* Default MMU definitions */
    env->nb_BATs = -1;
    env->nb_tlb = 0;
    env->nb_ways = 0;
    /* XXX: missing:
     * 32 bits PowerPC:
     * - MPC5xx(x)
     * - MPC8xx(x)
     * - RCPU (same as MPC5xx ?)
     */
    spr_register(env, SPR_PVR, "PVR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 def->pvr);
    printf("%s: PVR %08x mask %08x => %08x\n", __func__,
           def->pvr, def->pvr_mask, def->pvr & def->pvr_mask);
    switch (def->pvr & def->pvr_mask) {
        /* Embedded PowerPC from IBM                           */
    case CPU_PPC_401A1:   /* 401 A1 family                 */
    case CPU_PPC_401B2:   /* 401 B2 family                 */
    case CPU_PPC_401C2:   /* 401 C2 family                 */
    case CPU_PPC_401D2:   /* 401 D2 family                 */
    case CPU_PPC_401E2:   /* 401 E2 family                 */
    case CPU_PPC_401F2:   /* 401 F2 family                 */
    case CPU_PPC_401G2:   /* 401 G2 family                 */
    case CPU_PPC_IOP480:  /* IOP 480 family                */
    case CPU_PPC_COBRA:   /* IBM Processor for Network Resources */
        gen_spr_generic(env);
        gen_spr_40x(env);
        gen_spr_401_403(env);
#if defined (TODO)
        /* XXX: optional ? */
        gen_spr_compress(env);
#endif
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

    case CPU_PPC_403GA:   /* 403 GA family                 */
    case CPU_PPC_403GB:   /* 403 GB family                 */
    case CPU_PPC_403GC:   /* 403 GC family                 */
    case CPU_PPC_403GCX:  /* 403 GCX family                */
        gen_spr_generic(env);
        gen_spr_40x(env);
        gen_spr_401_403(env);
        gen_spr_403(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

    case CPU_PPC_405CR:   /* 405 GP/CR family              */
    case CPU_PPC_405EP:   /* 405 EP family                 */
    case CPU_PPC_405GPR:  /* 405 GPR family                */
    case CPU_PPC_405D2:   /* 405 D2 family                 */
    case CPU_PPC_405D4:   /* 405 D4 family                 */
        gen_spr_generic(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_40x(env);
        gen_spr_405(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

    case CPU_PPC_NPE405H: /* NPe405 H family               */
    case CPU_PPC_NPE405H2:
    case CPU_PPC_NPE405L: /* Npe405 L family               */
        gen_spr_generic(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_40x(env);
        gen_spr_405(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

#if defined (TODO)
    case CPU_PPC_STB01000:
#endif
#if defined (TODO)
    case CPU_PPC_STB01010:
#endif
#if defined (TODO)
    case CPU_PPC_STB0210:
#endif
    case CPU_PPC_STB03:   /* STB03 family                  */
#if defined (TODO)
    case CPU_PPC_STB043:  /* STB043 family                  */
#endif
#if defined (TODO)
    case CPU_PPC_STB045:  /* STB045 family                  */
#endif
    case CPU_PPC_STB25:   /* STB25 family                  */
#if defined (TODO)
    case CPU_PPC_STB130:  /* STB130 family                 */
#endif
        gen_spr_generic(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_40x(env);
        gen_spr_405(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

    case CPU_PPC_440EP:   /* 440 EP family                 */
    case CPU_PPC_440GP:   /* 440 GP family                 */
    case CPU_PPC_440GX:   /* 440 GX family                 */
    case CPU_PPC_440GXc:  /* 440 GXc family                */
    case CPU_PPC_440GXf:  /* 440 GXf family                */
    case CPU_PPC_440SP:   /* 440 SP family                 */
    case CPU_PPC_440SP2:
    case CPU_PPC_440SPE:  /* 440 SPE family                */
        gen_spr_generic(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_BookE(env);
        gen_spr_440(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

        /* Embedded PowerPC from Freescale                     */
#if defined (TODO)
    case CPU_PPC_5xx:
        break;
#endif
#if defined (TODO)
    case CPU_PPC_8xx:     /* MPC821 / 823 / 850 / 860      */
        break;
#endif
#if defined (TODO)
    case CPU_PPC_82xx_HIP3:    /* MPC8240 / 8260                */
    case CPU_PPC_82xx_HIP4:    /* MPC8240 / 8260                */
        break;
#endif
#if defined (TODO)
    case CPU_PPC_827x:    /* MPC 827x / 828x               */
        break;
#endif

        /* XXX: Use MPC8540 PVR to implement a test PowerPC BookE target */
    case CPU_PPC_e500v110:
    case CPU_PPC_e500v120:
    case CPU_PPC_e500v210:
    case CPU_PPC_e500v220:
        gen_spr_generic(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_BookE(env);
        env->nb_BATs = 0;
        env->nb_tlb = 64;
        env->nb_ways = 1;
        env->id_tlbs = 0;
        break;

#if defined (TODO)
    case CPU_PPC_e600:
        break;
#endif

        /* 32 bits PowerPC                                     */
    case CPU_PPC_601:     /* PowerPC 601                   */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        gen_spr_601(env);
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
        spr_register(env, SPR_601_HID2, "HID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        /* XXX : not implemented */
        spr_register(env, SPR_601_HID5, "HID5",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        /* XXX : not implemented */
#if 0 /* ? */
        spr_register(env, SPR_601_HID15, "HID15",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
#endif
        env->nb_tlb = 64;
        env->nb_ways = 2;
        env->id_tlbs = 0;
        env->id_tlbs = 0;
        break;

    case CPU_PPC_602:     /* PowerPC 602                   */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_6xx_7xx_soft_tlb(env, 64, 2);
        gen_spr_602(env);
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
        break;

    case CPU_PPC_603:     /* PowerPC 603                   */
    case CPU_PPC_603E:    /* PowerPC 603e                  */
    case CPU_PPC_603E7v:
    case CPU_PPC_603E7v2:
    case CPU_PPC_603P:    /* PowerPC 603p                  */
    case CPU_PPC_603R:    /* PowerPC 603r                  */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_6xx_7xx_soft_tlb(env, 64, 2);
        gen_spr_603(env);
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
        break;
        
    case CPU_PPC_G2:      /* PowerPC G2 family             */
    case CPU_PPC_G2H4:
    case CPU_PPC_G2gp:
    case CPU_PPC_G2ls:
    case CPU_PPC_G2LE:    /* PowerPC G2LE family           */
    case CPU_PPC_G2LEgp:
    case CPU_PPC_G2LEls:
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        /* Memory management */
        gen_high_BATs(env);
        gen_6xx_7xx_soft_tlb(env, 64, 2);
        gen_spr_G2_755(env);
        gen_spr_G2(env);
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
        break;

    case CPU_PPC_604:     /* PowerPC 604                   */
    case CPU_PPC_604E:    /* PowerPC 604e                  */
    case CPU_PPC_604R:    /* PowerPC 604r                  */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_604(env);
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
        break;

    case CPU_PPC_74x:     /* PowerPC 740 / 750             */
    case CPU_PPC_740E:
    case CPU_PPC_750E:
    case CPU_PPC_74xP:    /* PowerPC 740P / 750P           */
    case CPU_PPC_750CXE21: /* IBM PowerPC 750cxe            */
    case CPU_PPC_750CXE22:
    case CPU_PPC_750CXE23:
    case CPU_PPC_750CXE24:
    case CPU_PPC_750CXE24b:
    case CPU_PPC_750CXE31:
    case CPU_PPC_750CXE31b:
    case CPU_PPC_750CXR:
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_7xx(env);
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
        break;

    case CPU_PPC_750FX10: /* IBM PowerPC 750 FX            */
    case CPU_PPC_750FX20:
    case CPU_PPC_750FX21:
    case CPU_PPC_750FX22:
    case CPU_PPC_750FX23:
    case CPU_PPC_750GX10: /* IBM PowerPC 750 GX            */
    case CPU_PPC_750GX11:
    case CPU_PPC_750GX12:
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
        gen_high_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_7xx(env);
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
        spr_register(env, SPR_750_HID2, "HID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        break;

    case CPU_PPC_755_10:  /* PowerPC 755                   */
    case CPU_PPC_755_11:
    case CPU_PPC_755_20:
    case CPU_PPC_755D:
    case CPU_PPC_755E:
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        /* Memory management */
        gen_high_BATs(env);
        gen_6xx_7xx_soft_tlb(env, 64, 2);
        gen_spr_G2_755(env);
        /* L2 cache control */
        /* XXX : not implemented */
        spr_register(env, SPR_ICTC, "ICTC",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        /* XXX : not implemented */
        spr_register(env, SPR_L2PM, "L2PM",
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
        spr_register(env, SPR_HID2, "HID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        break;

#if defined (TODO)
        /* G4 family */
    case CPU_PPC_7400:    /* PowerPC 7400                  */
    case CPU_PPC_7410C:   /* PowerPC 7410                  */
    case CPU_PPC_7410D:
    case CPU_PPC_7410E:
    case CPU_PPC_7441:    /* PowerPC 7441                  */
    case CPU_PPC_7445:    /* PowerPC 7445                  */
    case CPU_PPC_7447:    /* PowerPC 7447                  */
    case CPU_PPC_7447A:   /* PowerPC 7447A                 */
    case CPU_PPC_7448:    /* PowerPC 7448                  */
    case CPU_PPC_7450:    /* PowerPC 7450                  */
    case CPU_PPC_7450b:
    case CPU_PPC_7451:    /* PowerPC 7451                  */
    case CPU_PPC_7451G:
    case CPU_PPC_7455:    /* PowerPC 7455                  */
    case CPU_PPC_7455F:
    case CPU_PPC_7455G:
    case CPU_PPC_7457:    /* PowerPC 7457                  */
    case CPU_PPC_7457C:
    case CPU_PPC_7457A:   /* PowerPC 7457A                 */
        break;
#endif

#if defined (TODO)
        /* 64 bits PowerPC                                     */
    case CPU_PPC_620:     /* PowerPC 620                   */
    case CPU_PPC_630:     /* PowerPC 630 (Power 3)         */
    case CPU_PPC_631:     /* PowerPC 631 (Power 3+)        */
    case CPU_PPC_POWER4:  /* Power 4                       */
    case CPU_PPC_POWER4P: /* Power 4+                      */
    case CPU_PPC_POWER5:  /* Power 5                       */
    case CPU_PPC_POWER5P: /* Power 5+                      */
    case CPU_PPC_970:     /* PowerPC 970                   */
    case CPU_PPC_970FX10: /* PowerPC 970 FX                */
    case CPU_PPC_970FX20:
    case CPU_PPC_970FX21:
    case CPU_PPC_970FX30:
    case CPU_PPC_970FX31:
    case CPU_PPC_970MP10: /* PowerPC 970 MP                */
    case CPU_PPC_970MP11:
    case CPU_PPC_CELL10:  /* Cell family                   */
    case CPU_PPC_CELL20:
    case CPU_PPC_CELL30:
    case CPU_PPC_CELL31:
    case CPU_PPC_RS64:    /* Apache (RS64/A35)             */
    case CPU_PPC_RS64II:  /* NorthStar (RS64-II/A50)       */
    case CPU_PPC_RS64III: /* Pulsar (RS64-III)             */
    case CPU_PPC_RS64IV:  /* IceStar/IStar/SStar (RS64-IV) */
        break;
#endif

#if defined (TODO)
        /* POWER                                               */
    case CPU_POWER:       /* POWER                         */
    case CPU_POWER2:      /* POWER2                        */
        break;
#endif

    default:
        gen_spr_generic(env);
        break;
    }
    if (env->nb_BATs == -1)
        env->nb_BATs = 4;
    /* Allocate TLBs buffer when needed */
    if (env->nb_tlb != 0) {
        int nb_tlb = env->nb_tlb;
        if (env->id_tlbs != 0)
            nb_tlb *= 2;
        env->tlb = qemu_mallocz(nb_tlb * sizeof(ppc_tlb_t));
        /* Pre-compute some useful values */
        env->tlb_per_way = env->nb_tlb / env->nb_ways;
    }
}

#if defined(PPC_DUMP_CPU)
static void dump_sprs (CPUPPCState *env)
{
    ppc_spr_t *spr;
    uint32_t pvr = env->spr[SPR_PVR];
    uint32_t sr, sw, ur, uw;
    int i, j, n;

    printf("* SPRs for PVR=%08x\n", pvr);
    for (i = 0; i < 32; i++) {
        for (j = 0; j < 32; j++) {
            n = (i << 5) | j;
            spr = &env->spr_cb[n];
#if !defined(CONFIG_USER_ONLY)
            sw = spr->oea_write != NULL && spr->oea_write != SPR_NOACCESS;
            sr = spr->oea_read != NULL && spr->oea_read != SPR_NOACCESS;
#else
            sw = 0;
            sr = 0;
#endif
            uw = spr->uea_write != NULL && spr->uea_write != SPR_NOACCESS;
            ur = spr->uea_read != NULL && spr->uea_read != SPR_NOACCESS;
            if (sw || sr || uw || ur) {
                printf("%4d (%03x) %8s s%c%c u%c%c\n",
                       (i << 5) | j, (i << 5) | j, spr->name,
                       sw ? 'w' : '-', sr ? 'r' : '-',
                       uw ? 'w' : '-', ur ? 'r' : '-');
            }
        }
    }
    fflush(stdout);
    fflush(stderr);
}
#endif

/*****************************************************************************/
#include <stdlib.h>
#include <string.h>

int fflush (FILE *stream);

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

/*****************************************************************************/
static int create_ppc_opcodes (CPUPPCState *env, ppc_def_t *def)
{
    opcode_t *opc, *start, *end;

    fill_new_table(env->opcodes, 0x40);
#if defined(PPC_DUMP_CPU)
    printf("* PowerPC instructions for PVR %08x: %s flags %08x %08x\n",
           def->pvr, def->name, def->insns_flags, def->flags);
#endif
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
#if defined(PPC_DUMP_CPU)
            if (opc1 != 0x00) {
                if (opc->opc3 == 0xFF) {
                    if (opc->opc2 == 0xFF) {
                        printf(" %02x -- -- (%2d ----) : %s\n",
                               opc->opc1, opc->opc1, opc->oname);
                    } else {
                        printf(" %02x %02x -- (%2d %4d) : %s\n",
                               opc->opc1, opc->opc2, opc->opc1, opc->opc2,
                               opc->oname);
                    }
                } else {
                    printf(" %02x %02x %02x (%2d %4d) : %s\n",
                           opc->opc1, opc->opc2, opc->opc3,
                           opc->opc1, (opc->opc3 << 5) | opc->opc2,
                           opc->oname);
                }
            }
#endif
        }
    }
    fix_opcode_tables(env->opcodes);
    fflush(stdout);
    fflush(stderr);

    return 0;
}

int cpu_ppc_register (CPUPPCState *env, ppc_def_t *def)
{
    env->msr_mask = def->msr_mask;
    env->flags = def->flags;
    if (create_ppc_opcodes(env, def) < 0)
        return -1;
    init_ppc_proc(env, def);
#if defined(PPC_DUMP_CPU)
    dump_sprs(env);
    if (env->tlb != NULL) {
        printf("%d %s TLB in %d ways\n", env->nb_tlb,
               env->id_tlbs ? "splitted" : "merged", env->nb_ways);
    }
#endif

    return 0;
}

void do_compute_hflags (CPUPPCState *env);
CPUPPCState *cpu_ppc_init (void)
{
    CPUPPCState *env;

    env = qemu_mallocz(sizeof(CPUPPCState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    tlb_flush(env, 1);
#if defined (DO_SINGLE_STEP) && 0
    /* Single step trace mode */
    msr_se = 1;
    msr_be = 1;
#endif
    msr_fp = 1; /* Allow floating point exceptions */
    msr_me = 1; /* Allow machine check exceptions  */
#if defined(CONFIG_USER_ONLY)
    msr_pr = 1;
#else
    env->nip = 0xFFFFFFFC;
#endif
    do_compute_hflags(env);
    env->reserve = -1;
    return env;
}

void cpu_ppc_close(CPUPPCState *env)
{
    /* Should also remove all opcode tables... */
    free(env);
}

/*****************************************************************************/
/* PowerPC CPU definitions */
static ppc_def_t ppc_defs[] =
    {
        /* Embedded PowerPC */
#if defined (TODO)
        /* PowerPC 401 */
        {
            .name        = "401",
            .pvr         = CPU_PPC_401,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_401,
            .flags       = PPC_FLAGS_401,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* IOP480 (401 microcontroler) */
        {
            .name        = "iop480",
            .pvr         = CPU_PPC_IOP480,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_401,
            .flags       = PPC_FLAGS_401,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* IBM Processor for Network Resources */
        {
            .name        = "Cobra",
            .pvr         = CPU_PPC_COBRA,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_401,
            .flags       = PPC_FLAGS_401,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* Generic PowerPC 403 */
        {
            .name        = "403",
            .pvr         = CPU_PPC_403,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_403,
            .flags       = PPC_FLAGS_403,
            .msr_mask    = 0x000000000007D23D,
        },
#endif
#if defined (TODO)
        /* PowerPC 403 GA */
        {
            .name        = "403ga",
            .pvr         = CPU_PPC_403GA,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_403,
            .flags       = PPC_FLAGS_403,
            .msr_mask    = 0x000000000007D23D,
        },
#endif
#if defined (TODO)
        /* PowerPC 403 GB */
        {
            .name        = "403gb",
            .pvr         = CPU_PPC_403GB,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_403,
            .flags       = PPC_FLAGS_403,
            .msr_mask    = 0x000000000007D23D,
        },
#endif
#if defined (TODO)
        /* PowerPC 403 GC */
        {
            .name        = "403gc",
            .pvr         = CPU_PPC_403GC,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_403,
            .flags       = PPC_FLAGS_403,
            .msr_mask    = 0x000000000007D23D,
        },
#endif
#if defined (TODO)
        /* PowerPC 403 GCX */
        {
            .name        = "403gcx",
            .pvr         = CPU_PPC_403GCX,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_403,
            .flags       = PPC_FLAGS_403,
            .msr_mask    = 0x000000000007D23D,
        },
#endif
#if defined (TODO)
        /* Generic PowerPC 405 */
        {
            .name        = "405",
            .pvr         = CPU_PPC_405,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 CR */
        {
            .name        = "405cr",
            .pvr         = CPU_PPC_405,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 GP */
        {
            .name        = "405gp",
            .pvr         = CPU_PPC_405,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 EP */
        {
            .name        = "405ep",
            .pvr         = CPU_PPC_405EP,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 GPR */
        {
            .name        = "405gpr",
            .pvr         = CPU_PPC_405GPR,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 D2 */
        {
            .name        = "405d2",
            .pvr         = CPU_PPC_405D2,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 405 D4 */
        {
            .name        = "405d4",
            .pvr         = CPU_PPC_405D4,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* Npe405 H */
        {
            .name        = "Npe405H",
            .pvr         = CPU_PPC_NPE405H,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* Npe405 L */
        {
            .name        = "Npe405L",
            .pvr         = CPU_PPC_NPE405L,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB010000 */
        {
            .name        = "STB01000",
            .pvr         = CPU_PPC_STB01000,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB01010 */
        {
            .name        = "STB01010",
            .pvr         = CPU_PPC_STB01010,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB0210 */
        {
            .name        = "STB0210",
            .pvr         = CPU_PPC_STB0210,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB03xx */
        {
            .name        = "STB03",
            .pvr         = CPU_PPC_STB03,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB043x */
        {
            .name        = "STB043",
            .pvr         = CPU_PPC_STB043,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB045x */
        {
            .name        = "STB045",
            .pvr         = CPU_PPC_STB045,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB25xx */
        {
            .name        = "STB25",
            .pvr         = CPU_PPC_STB25,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* STB130 */
        {
            .name        = "STB130",
            .pvr         = CPU_PPC_STB130,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
        /* Xilinx PowerPC 405 cores */
#if defined (TODO)
        {
            .name        = "x2vp4",
            .pvr         = CPU_PPC_X2VP4,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
        {
            .name        = "x2vp7",
            .pvr         = CPU_PPC_X2VP7,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
        {
            .name        = "x2vp20",
            .pvr         = CPU_PPC_X2VP20,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
        {
            .name        = "x2vp50",
            .pvr         = CPU_PPC_X2VP50,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_405,
            .msr_mask    = 0x00000000020EFF30,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 EP */
        {
            .name        = "440ep",
            .pvr         = CPU_PPC_440EP,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_440,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 GR */
        {
            .name        = "440gr",
            .pvr         = CPU_PPC_440GR,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_440,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 GP */
        {
            .name        = "440gp",
            .pvr         = CPU_PPC_440GP,
            .pvr_mask    = 0xFFFFFF00,
            .insns_flags = PPC_INSNS_440,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 GX */
        {
            .name        = "440gx",
            .pvr         = CPU_PPC_440GX,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 GXc */
        {
            .name        = "440gxc",
            .pvr         = CPU_PPC_440GXC,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 GXf */
        {
            .name        = "440gxf",
            .pvr         = CPU_PPC_440GXF,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 SP */
        {
            .name        = "440sp",
            .pvr         = CPU_PPC_440SP,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 SP2 */
        {
            .name        = "440sp2",
            .pvr         = CPU_PPC_440SP2,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
#if defined (TODO)
        /* PowerPC 440 SPE */
        {
            .name        = "440spe",
            .pvr         = CPU_PPC_440SPE,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_405,
            .flags       = PPC_FLAGS_440,
            .msr_mask    = 0x000000000006D630,
        },
#endif
        /* Fake generic BookE PowerPC */
        {
            .name        = "BookE",
            .pvr         = CPU_PPC_e500,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_BOOKE,
            .flags       = PPC_FLAGS_BOOKE,
            .msr_mask    = 0x000000000006D630,
        },
        /* PowerPC 460 cores - TODO */
        /* PowerPC MPC 5xx cores - TODO */
        /* PowerPC MPC 8xx cores - TODO */
        /* PowerPC MPC 8xxx cores - TODO */
        /* e200 cores - TODO */
        /* e500 cores - TODO */
        /* e600 cores - TODO */

        /* 32 bits "classic" PowerPC */
#if defined (TODO)
        /* PowerPC 601 */
        {
            .name        = "601",
            .pvr         = CPU_PPC_601,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_601,
            .flags       = PPC_FLAGS_601,
            .msr_mask    = 0x000000000000FD70,
        },
#endif
#if defined (TODO)
        /* PowerPC 602 */
        {
            .name        = "602",
            .pvr         = CPU_PPC_602,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_602,
            .flags       = PPC_FLAGS_602,
            .msr_mask    = 0x0000000000C7FF73,
        },
#endif
        /* PowerPC 603 */
        {
            .name        = "603",
            .pvr         = CPU_PPC_603,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603e */
        {
            .name        = "603e",
            .pvr         = CPU_PPC_603E,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        {
            .name        = "Stretch",
            .pvr         = CPU_PPC_603E,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603p */
        {
            .name        = "603p",
            .pvr         = CPU_PPC_603P,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603e7 */
        {
            .name        = "603e7",
            .pvr         = CPU_PPC_603E7,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603e7v */
        {
            .name        = "603e7v",
            .pvr         = CPU_PPC_603E7v,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603e7v2 */
        {
            .name        = "603e7v2",
            .pvr         = CPU_PPC_603E7v2,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        /* PowerPC 603r */
        {
            .name        = "603r",
            .pvr         = CPU_PPC_603R,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
        {
            .name        = "Goldeneye",
            .pvr         = CPU_PPC_603R,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_603,
            .flags       = PPC_FLAGS_603,
            .msr_mask    = 0x000000000007FF73,
        },
#if defined (TODO)
        /* XXX: TODO: according to Motorola UM, this is a derivative to 603e */
        {
            .name        = "G2",
            .pvr         = CPU_PPC_G2,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000006FFF2,
        },
        {
            .name        = "G2h4",
            .pvr         = CPU_PPC_G2H4,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000006FFF2,
        },
        {
            .name        = "G2gp",
            .pvr         = CPU_PPC_G2gp,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000006FFF2,
        },
        {
            .name        = "G2ls",
            .pvr         = CPU_PPC_G2ls,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000006FFF2,
        },
        { /* Same as G2, with LE mode support */
            .name        = "G2le",
            .pvr         = CPU_PPC_G2LE,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000007FFF3,
        },
        {
            .name        = "G2legp",
            .pvr         = CPU_PPC_G2LEgp,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000007FFF3,
        },
        {
            .name        = "G2lels",
            .pvr         = CPU_PPC_G2LEls,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_G2,
            .flags       = PPC_FLAGS_G2,
            .msr_mask    = 0x000000000007FFF3,
        },
#endif
        /* PowerPC 604 */
        {
            .name        = "604",
            .pvr         = CPU_PPC_604,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_604,
            .flags       = PPC_FLAGS_604,
            .msr_mask    = 0x000000000005FF77,
        },
        /* PowerPC 604e */
        {
            .name        = "604e",
            .pvr         = CPU_PPC_604E,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_604,
            .flags       = PPC_FLAGS_604,
            .msr_mask    = 0x000000000005FF77,
        },
        /* PowerPC 604r */
        {
            .name        = "604r",
            .pvr         = CPU_PPC_604R,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_604,
            .flags       = PPC_FLAGS_604,
            .msr_mask    = 0x000000000005FF77,
        },
        /* generic G3 */
        {
            .name        = "G3",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
        /* MPC740 (G3) */
        {
            .name        = "740",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
        {
            .name        = "Arthur",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
#if defined (TODO)
        /* MPC745 (G3) */
        {
            .name        = "745",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFF000,
            .insns_flags = PPC_INSNS_7x5,
            .flags       = PPC_FLAGS_7x5,
            .msr_mask    = 0x000000000007FF77,
        },
        {
            .name        = "Goldfinger",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFF000,
            .insns_flags = PPC_INSNS_7x5,
            .flags       = PPC_FLAGS_7x5,
            .msr_mask    = 0x000000000007FF77,
        },
#endif
        /* MPC750 (G3) */
        {
            .name        = "750",
            .pvr         = CPU_PPC_74x,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
#if defined (TODO)
        /* MPC755 (G3) */
        {
            .name        = "755",
            .pvr         = CPU_PPC_755,
            .pvr_mask    = 0xFFFFF000,
            .insns_flags = PPC_INSNS_7x5,
            .flags       = PPC_FLAGS_7x5,
            .msr_mask    = 0x000000000007FF77,
        },
#endif
        /* MPC740P (G3) */
        {
            .name        = "740p",
            .pvr         = CPU_PPC_74xP,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
        {
            .name        = "Conan/Doyle",
            .pvr         = CPU_PPC_74xP,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
#if defined (TODO)
        /* MPC745P (G3) */
        {
            .name        = "745p",
            .pvr         = CPU_PPC_74xP,
            .pvr_mask    = 0xFFFFF000,
            .insns_flags = PPC_INSNS_7x5,
            .flags       = PPC_FLAGS_7x5,
            .msr_mask    = 0x000000000007FF77,
        },
#endif
        /* MPC750P (G3) */
        {
            .name        = "750p",
            .pvr         = CPU_PPC_74xP,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
#if defined (TODO)
        /* MPC755P (G3) */
        {
            .name        = "755p",
            .pvr         = CPU_PPC_74xP,
            .pvr_mask    = 0xFFFFF000,
            .insns_flags = PPC_INSNS_7x5,
            .flags       = PPC_FLAGS_7x5,
            .msr_mask    = 0x000000000007FF77,
        },
#endif
        /* IBM 750CXe (G3 embedded) */
        {
            .name        = "750cxe",
            .pvr         = CPU_PPC_750CXE,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
        /* IBM 750FX (G3 embedded) */
        {
            .name        = "750fx",
            .pvr         = CPU_PPC_750FX,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
        /* IBM 750GX (G3 embedded) */
        {
            .name        = "750gx",
            .pvr         = CPU_PPC_750GX,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_7x0,
            .flags       = PPC_FLAGS_7x0,
            .msr_mask    = 0x000000000007FF77,
        },
#if defined (TODO)
        /* generic G4 */
        {
            .name        = "G4",
            .pvr         = CPU_PPC_7400,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
#if defined (TODO)
        /* PowerPC 7400 (G4) */
        {
            .name        = "7400",
            .pvr         = CPU_PPC_7400,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Max",
            .pvr         = CPU_PPC_7400,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
#if defined (TODO)
        /* PowerPC 7410 (G4) */
        {
            .name        = "7410",
            .pvr         = CPU_PPC_7410,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Nitro",
            .pvr         = CPU_PPC_7410,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
        /* XXX: 7441 */
        /* XXX: 7445 */
        /* XXX: 7447 */
        /* XXX: 7447A */
#if defined (TODO)
        /* PowerPC 7450 (G4) */
        {
            .name        = "7450",
            .pvr         = CPU_PPC_7450,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Vger",
            .pvr         = CPU_PPC_7450,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
        /* XXX: 7451 */
#if defined (TODO)
        /* PowerPC 7455 (G4) */
        {
            .name        = "7455",
            .pvr         = CPU_PPC_7455,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Apollo 6",
            .pvr         = CPU_PPC_7455,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
#if defined (TODO)
        /* PowerPC 7457 (G4) */
        {
            .name        = "7457",
            .pvr         = CPU_PPC_7457,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Apollo 7",
            .pvr         = CPU_PPC_7457,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
#if defined (TODO)
        /* PowerPC 7457A (G4) */
        {
            .name        = "7457A",
            .pvr         = CPU_PPC_7457A,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
        {
            .name        = "Apollo 7 PM",
            .pvr         = CPU_PPC_7457A,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_74xx,
            .flags       = PPC_FLAGS_74xx,
            .msr_mask    = 0x000000000205FF77,
        },
#endif
        /* 64 bits PowerPC */
#if defined (TODO)
        /* PowerPC 620 */
        {
            .name        = "620",
            .pvr         = CPU_PPC_620,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_620,
            .flags       = PPC_FLAGS_620,
            .msr_mask    = 0x800000000005FF73,
        },
#endif
#if defined (TODO)
        /* PowerPC 630 (POWER3) */
        {
            .name        = "630",
            .pvr         = CPU_PPC_630,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_630,
            .flags       = PPC_FLAGS_630,
            .msr_mask    = xxx,
        }
        {
            .name        = "POWER3",
            .pvr         = CPU_PPC_630,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_630,
            .flags       = PPC_FLAGS_630,
            .msr_mask    = xxx,
        }
#endif
#if defined (TODO)
        /* PowerPC 631 (Power 3+)*/
        {
            .name        = "631",
            .pvr         = CPU_PPC_631,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_631,
            .flags       = PPC_FLAGS_631,
            .msr_mask    = xxx,
        },
        {
            .name        = "POWER3+",
            .pvr         = CPU_PPC_631,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_631,
            .flags       = PPC_FLAGS_631,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* POWER4 */
        {
            .name        = "POWER4",
            .pvr         = CPU_PPC_POWER4,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER4,
            .flags       = PPC_FLAGS_POWER4,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* POWER4p */
        {
            .name        = "POWER4+",
            .pvr         = CPU_PPC_POWER4P,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER4,
            .flags       = PPC_FLAGS_POWER4,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* POWER5 */
        {
            .name        = "POWER5",
            .pvr         = CPU_PPC_POWER5,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER5,
            .flags       = PPC_FLAGS_POWER5,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* POWER5+ */
        {
            .name        = "POWER5+",
            .pvr         = CPU_PPC_POWER5P,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER5,
            .flags       = PPC_FLAGS_POWER5,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* PowerPC 970 */
        {
            .name        = "970",
            .pvr         = CPU_PPC_970,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_970,
            .flags       = PPC_FLAGS_970,
            .msr_mask    = 0x900000000204FF36,
        },
#endif
#if defined (TODO)
        /* PowerPC 970FX (G5) */
        {
            .name        = "970fx",
            .pvr         = CPU_PPC_970FX,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_970FX,
            .flags       = PPC_FLAGS_970FX,
            .msr_mask    = 0x800000000204FF36,
        },
#endif
#if defined (TODO)
        /* RS64 (Apache/A35) */
        /* This one seems to support the whole POWER2 instruction set
         * and the PowerPC 64 one.
         */
        {
            .name        = "RS64",
            .pvr         = CPU_PPC_RS64,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "Apache",
            .pvr         = CPU_PPC_RS64,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "A35",
            .pvr         = CPU_PPC_RS64,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* RS64-II (NorthStar/A50) */
        {
            .name        = "RS64-II",
            .pvr         = CPU_PPC_RS64II,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "NortStar",
            .pvr         = CPU_PPC_RS64II,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "A50",
            .pvr         = CPU_PPC_RS64II,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* RS64-III (Pulsar) */
        {
            .name        = "RS64-III",
            .pvr         = CPU_PPC_RS64III,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "Pulsar",
            .pvr         = CPU_PPC_RS64III,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* RS64-IV (IceStar/IStar/SStar) */
        {
            .name        = "RS64-IV",
            .pvr         = CPU_PPC_RS64IV,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "IceStar",
            .pvr         = CPU_PPC_RS64IV,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "IStar",
            .pvr         = CPU_PPC_RS64IV,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
        {
            .name        = "SStar",
            .pvr         = CPU_PPC_RS64IV,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_RS64,
            .flags       = PPC_FLAGS_RS64,
            .msr_mask    = xxx,
        },
#endif
        /* POWER */
#if defined (TODO)
        /* Original POWER */
        {
            .name        = "POWER",
            .pvr         = CPU_POWER,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER,
            .flags       = PPC_FLAGS_POWER,
            .msr_mask    = xxx,
        },
#endif
#if defined (TODO)
        /* POWER2 */
        {
            .name        = "POWER2",
            .pvr         = CPU_POWER2,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_POWER,
            .flags       = PPC_FLAGS_POWER,
            .msr_mask    = xxx,
        },
#endif
        /* Generic PowerPCs */
#if defined (TODO)
        {
            .name        = "ppc64",
            .pvr         = CPU_PPC_970,
            .pvr_mask    = 0xFFFF0000,
            .insns_flags = PPC_INSNS_PPC64,
            .flags       = PPC_FLAGS_PPC64,
            .msr_mask    = 0xA00000000204FF36,
        },
#endif
        {
            .name        = "ppc32",
            .pvr         = CPU_PPC_604,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_PPC32,
            .flags       = PPC_FLAGS_PPC32,
            .msr_mask    = 0x000000000005FF77,
        },
        /* Fallback */
        {
            .name        = "ppc",
            .pvr         = CPU_PPC_604,
            .pvr_mask    = 0xFFFFFFFF,
            .insns_flags = PPC_INSNS_PPC32,
            .flags       = PPC_FLAGS_PPC32,
            .msr_mask    = 0x000000000005FF77,
        },
    };

int ppc_find_by_name (const unsigned char *name, ppc_def_t **def)
{
    int i, ret;

    ret = -1;
    *def = NULL;
    for (i = 0; strcmp(ppc_defs[i].name, "ppc") != 0; i++) {
        if (strcasecmp(name, ppc_defs[i].name) == 0) {
            *def = &ppc_defs[i];
            ret = 0;
            break;
        }
    }

    return ret;
}

int ppc_find_by_pvr (uint32_t pvr, ppc_def_t **def)
{
    int i, ret;

    ret = -1;
    *def = NULL;
    for (i = 0; ppc_defs[i].name != NULL; i++) {
        if ((pvr & ppc_defs[i].pvr_mask) ==
            (ppc_defs[i].pvr & ppc_defs[i].pvr_mask)) {
            *def = &ppc_defs[i];
            ret = 0;
            break;
        }
    }

    return ret;
}

void ppc_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    int i;

    for (i = 0; ; i++) {
        (*cpu_fprintf)(f, "PowerPC %16s PVR %08x mask %08x\n",
                       ppc_defs[i].name,
                       ppc_defs[i].pvr, ppc_defs[i].pvr_mask);
        if (strcmp(ppc_defs[i].name, "ppc") == 0)
            break;
    }
}
