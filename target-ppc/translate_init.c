/*
 *  PowerPC CPU initialization for qemu.
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

/* SPR common to all PPC */
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

/* SPR common to all non-embedded PPC (ie not 4xx) */
/* DECR */
static void spr_read_decr (void *opaque, int sprn)
{
    gen_op_load_decr();
}

static void spr_write_decr (void *opaque, int sprn)
{
    gen_op_store_decr();
}

/* SPR common to all non-embedded PPC, except 601 */
/* Time base */
static void spr_read_tbl (void *opaque, int sprn)
{
    gen_op_load_tbl();
}

static void spr_write_tbl (void *opaque, int sprn)
{
    gen_op_store_tbl();
}

static void spr_read_tbu (void *opaque, int sprn)
{
    gen_op_load_tbu();
}

static void spr_write_tbu (void *opaque, int sprn)
{
    gen_op_store_tbu();
}

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

static void spr_write_pir (void *opaque, int sprn)
{
    gen_op_store_pir();
}

static inline void spr_register (CPUPPCState *env, int num,
                                 const unsigned char *name,
                                 void (*uea_read)(void *opaque, int sprn),
                                 void (*uea_write)(void *opaque, int sprn),
                                 void (*oea_read)(void *opaque, int sprn),
                                 void (*oea_write)(void *opaque, int sprn),
                                 target_ulong initial_value)
{
    ppc_spr_t *spr;

    spr = &env->spr_cb[num];
    if (spr->name != NULL ||env-> spr[num] != 0x00000000 ||
        spr->uea_read != NULL || spr->uea_write != NULL ||
        spr->oea_read != NULL || spr->oea_write != NULL) {
        printf("Error: Trying to register SPR %d (%03x) twice !\n", num, num);
        exit(1);
    }
#if defined(PPC_DEBUG_SPR)
    printf("*** register spr %d (%03x) %s val %08" PRIx64 "\n", num, num, name,
           (unsigned long long)initial_value);
#endif
    spr->name = name;
    spr->uea_read = uea_read;
    spr->uea_write = uea_write;
    spr->oea_read = oea_read;
    spr->oea_write = oea_write;
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

// XXX: TODO (64 bits PPC sprs)
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
    /* Default MMU definitions */
    env->nb_BATs = -1;
    env->nb_tlb = 0;
    env->nb_ways = 0;
    /* XXX: missing:
     * 32 bits PPC:
     * - MPC5xx(x)
     * - MPC8xx(x)
     * - RCPU (MPC5xx)
     */
    spr_register(env, SPR_PVR, "PVR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 def->pvr);
    switch (def->pvr & def->pvr_mask) {
    case CPU_PPC_604:     /* PPC 604                       */
    case CPU_PPC_604E:    /* PPC 604e                      */
    case CPU_PPC_604R:    /* PPC 604r                      */
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

    case CPU_PPC_74x:     /* PPC 740 / 750                 */
    case CPU_PPC_74xP:    /* PPC 740P / 750P               */
    case CPU_PPC_750CXE:  /* IBM PPC 750cxe                */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_7xx(env);
        /* XXX : not implemented */
        spr_register(env, SPR_L2CR, "L2CR",
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
        break;

    case CPU_PPC_750FX:   /* IBM PPC 750 FX                */
    case CPU_PPC_750GX:   /* IBM PPC 750 GX                */
        gen_spr_generic(env);
        gen_spr_ne_601(env);
        /* Memory management */
        gen_low_BATs(env);
        /* PowerPC 750fx & 750gx has 8 DBATs and 8 IBATs */
        gen_high_BATs(env);
        /* Time base */
        gen_tbl(env);
        gen_spr_7xx(env);
        /* XXX : not implemented */
        spr_register(env, SPR_L2CR, "L2CR",
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
        spr_register(env, SPR_750_HID2, "HID2",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     0x00000000);
        break;

    default:
        gen_spr_generic(env);
        break;
    }
    if (env->nb_BATs == -1)
        env->nb_BATs = 4;
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
            sw = spr->oea_write != NULL && spr->oea_write != SPR_NOACCESS;
            sr = spr->oea_read != NULL && spr->oea_read != SPR_NOACCESS;
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
    printf("* PPC instructions for PVR %08x: %s\n", def->pvr, def->name);
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
                printf("*** ERROR initializing PPC instruction "
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
    if (create_ppc_opcodes(env, def) < 0) {
        printf("Error creating opcodes table\n");
        fflush(stdout);
        fflush(stderr);
        return -1;
    }
    init_ppc_proc(env, def);
#if defined(PPC_DUMP_CPU)
    dump_sprs(env);
#endif
    fflush(stdout);
    fflush(stderr);

    return 0;
}

CPUPPCState *cpu_ppc_init(void)
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
    /* Embedded PPC */
#if defined (TODO)
    /* PPC 401 */
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
    /* PPC 403 GA */
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
    /* PPC 403 GB */
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
    /* PPC 403 GC */
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
    /* PPC 403 GCX */
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
    /* PPC 405 CR */
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
    /* PPC 405 GP */
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
    /* PPC 405 EP */
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
    /* PPC 405 GPR */
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
    /* PPC 405 D2 */
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
    /* PPC 405 D4 */
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
    /* STB04xx */
    {
        .name        = "STB04",
        .pvr         = CPU_PPC_STB04,
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
    /* PPC 440 EP */
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
    /* PPC 440 GP */
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
    /* PPC 440 GX */
    {
        .name        = "440gx",
        .pvr         = CPU_PPC_440GX,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_405,
        .flags       = PPC_FLAGS_440,
        .msr_mask    = 0x000000000006D630,
    },
#endif

    /* 32 bits "classic" powerpc */
#if defined (TODO)
    /* PPC 601 */
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
    /* PPC 602 */
    {
        .name        = "602",
        .pvr         = CPU_PPC_602,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_602,
        .flags       = PPC_FLAGS_602,
        .msr_mask    = 0x0000000000C7FF73,
    },
#endif
#if defined (TODO)
    /* PPC 603 */
    {
        .name        = "603",
        .pvr         = CPU_PPC_603,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
#endif
#if defined (TODO)
    /* PPC 603e */
    {
        .name        = "603e",
        .pvr         = CPU_PPC_603E,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
    {
        .name        = "Stretch",
        .pvr         = CPU_PPC_603E,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
#endif
#if defined (TODO)
    /* PPC 603ev */
    {
        .name        = "603ev",
        .pvr         = CPU_PPC_603EV,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
#endif
#if defined (TODO)
    /* PPC 603r */
    {
        .name        = "603r",
        .pvr         = CPU_PPC_603R,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
    {
        .name        = "Goldeneye",
        .pvr         = CPU_PPC_603R,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_603,
        .flags       = PPC_FLAGS_603,
        .msr_mask    = 0x000000000007FF73,
    },
#endif
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
    { /* Same as G2, with LE mode support */
        .name        = "G2le",
        .pvr         = CPU_PPC_G2LE,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_G2,
        .flags       = PPC_FLAGS_G2,
        .msr_mask    = 0x000000000007FFF3,
    },
#endif
    /* PPC 604 */
    {
        .name        = "604",
        .pvr         = CPU_PPC_604,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_604,
        .flags       = PPC_FLAGS_604,
        .msr_mask    = 0x000000000005FF77,
    },
    /* PPC 604e */
    {
        .name        = "604e",
        .pvr         = CPU_PPC_604E,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_604,
        .flags       = PPC_FLAGS_604,
        .msr_mask    = 0x000000000005FF77,
    },
    /* PPC 604r */
    {
        .name        = "604r",
        .pvr         = CPU_PPC_604R,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_604,
        .flags       = PPC_FLAGS_604,
        .msr_mask    = 0x000000000005FF77,
    },
    /* generic G3 */
    {
        .name        = "G3",
        .pvr         = CPU_PPC_74x,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
#if defined (TODO)
    /* MPC740 (G3) */
    {
        .name        = "740",
        .pvr         = CPU_PPC_74x,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
    {
        .name        = "Arthur",
        .pvr         = CPU_PPC_74x,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
#endif
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
        .pvr_mask    = 0xFFFFF000,
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
#if defined (TODO)
    /* MPC740P (G3) */
    {
        .name        = "740p",
        .pvr         = CPU_PPC_74xP,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
    {
        .name        = "Conan/Doyle",
        .pvr         = CPU_PPC_74xP,
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
#endif
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
        .pvr_mask    = 0xFFFFF000,
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
        .pvr_mask    = 0xFFFFF000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
    /* IBM 750FX (G3 embedded) */
    {
        .name        = "750fx",
        .pvr         = CPU_PPC_750FX,
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_7x0,
        .flags       = PPC_FLAGS_7x0,
        .msr_mask    = 0x000000000007FF77,
    },
    /* IBM 750GX (G3 embedded) */
    {
        .name        = "750gx",
        .pvr         = CPU_PPC_750GX,
        .pvr_mask    = 0xFFFF0000,
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
    /* PPC 7400 (G4) */
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
    /* PPC 7410 (G4) */
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
    /* PPC 7450 (G4) */
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
    /* PPC 7455 (G4) */
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
    /* PPC 7457 (G4) */
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
    /* PPC 7457A (G4) */
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
    /* 64 bits PPC */
#if defined (TODO)
    /* PPC 620 */
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
    /* PPC 630 (POWER3) */
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
    /* PPC 631 (Power 3+)*/
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
    /* PPC 970 */
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
    /* PPC 970FX (G5) */
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
        .pvr_mask    = 0xFFFF0000,
        .insns_flags = PPC_INSNS_PPC32,
        .flags       = PPC_FLAGS_PPC32,
        .msr_mask    = 0x000000000005FF77,
    },
    /* Fallback */
    {
        .name        = "ppc",
        .pvr         = CPU_PPC_604,
        .pvr_mask    = 0xFFFF0000,
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
        (*cpu_fprintf)(f, "PowerPC '%s' PVR %08x mask %08x\n",
                       ppc_defs[i].name,
                       ppc_defs[i].pvr, ppc_defs[i].pvr_mask);
        if (strcmp(ppc_defs[i].name, "ppc") == 0)
            break;
    }
}
