/*
 *  MIPS emulation for qemu: CPU initialisation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2007 Herve Poussineau
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

/* CPU / CPU family specific config register values. */

/* Have config1, is MIPS32R1, uses TLB, no virtual icache,
   uncached coherency */
#define MIPS_CONFIG0                                              \
  ((1 << CP0C0_M) | (0x0 << CP0C0_K23) | (0x0 << CP0C0_KU) |      \
   (0x0 << CP0C0_AT) | (0x0 << CP0C0_AR) | (0x1 << CP0C0_MT) |    \
   (0x2 << CP0C0_K0))

/* Have config2, no coprocessor2 attached, no MDMX support attached,
   no performance counters, watch registers present,
   no code compression, EJTAG present, no FPU */
#define MIPS_CONFIG1                                              \
((1 << CP0C1_M) |                                                 \
 (0 << CP0C1_C2) | (0 << CP0C1_MD) | (0 << CP0C1_PC) |            \
 (1 << CP0C1_WR) | (0 << CP0C1_CA) | (1 << CP0C1_EP) |            \
 (0 << CP0C1_FP))

/* Have config3, no tertiary/secondary caches implemented */
#define MIPS_CONFIG2                                              \
((1 << CP0C2_M))

/* No config4, no DSP ASE, no large physaddr,
   no external interrupt controller, no vectored interupts,
   no 1kb pages, no SmartMIPS ASE, no trace logic */
#define MIPS_CONFIG3                                              \
((0 << CP0C3_M) | (0 << CP0C3_DSPP) | (0 << CP0C3_LPA) |          \
 (0 << CP0C3_VEIC) | (0 << CP0C3_VInt) | (0 << CP0C3_SP) |        \
 (0 << CP0C3_SM) | (0 << CP0C3_TL))

/* Define a implementation number of 1.
   Define a major version 1, minor version 0. */
#define MIPS_FCR0 ((0 << FCR0_S) | (0x1 << FCR0_PRID) | (0x10 << FCR0_REV))


struct mips_def_t {
    const unsigned char *name;
    int32_t CP0_PRid;
    int32_t CP0_Config0;
    int32_t CP0_Config1;
    int32_t CP0_Config2;
    int32_t CP0_Config3;
    int32_t CP0_Config6;
    int32_t CP0_Config7;
    int32_t SYNCI_Step;
    int32_t CCRes;
    int32_t CP0_Status_rw_bitmask;
    int32_t CP0_TCStatus_rw_bitmask;
    int32_t CP0_SRSCtl;
    int32_t CP1_fcr0;
    int32_t SEGBITS;
    int32_t CP0_SRSConf0_rw_bitmask;
    int32_t CP0_SRSConf0;
    int32_t CP0_SRSConf1_rw_bitmask;
    int32_t CP0_SRSConf1;
    int32_t CP0_SRSConf2_rw_bitmask;
    int32_t CP0_SRSConf2;
    int32_t CP0_SRSConf3_rw_bitmask;
    int32_t CP0_SRSConf3;
    int32_t CP0_SRSConf4_rw_bitmask;
    int32_t CP0_SRSConf4;
    int insn_flags;
};

/*****************************************************************************/
/* MIPS CPU definitions */
static mips_def_t mips_defs[] =
{
    {
        .name = "4Kc",
        .CP0_PRid = 0x00018000,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x1278FF17,
        .insn_flags = CPU_MIPS32 | ASE_MIPS16,
    },
    {
        .name = "4KEcR1",
        .CP0_PRid = 0x00018400,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x1278FF17,
        .insn_flags = CPU_MIPS32 | ASE_MIPS16,
    },
    {
        .name = "4KEc",
        .CP0_PRid = 0x00019000,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3 | (0 << CP0C3_VInt),
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x1278FF17,
        .insn_flags = CPU_MIPS32R2 | ASE_MIPS16,
    },
    {
        .name = "24Kc",
        .CP0_PRid = 0x00019300,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3 | (0 << CP0C3_VInt),
        .SYNCI_Step = 32,
        .CCRes = 2,
        /* No DSP implemented. */
        .CP0_Status_rw_bitmask = 0x1278FF17,
        .insn_flags = CPU_MIPS32R2 | ASE_MIPS16 | ASE_DSP,
    },
    {
        .name = "24Kf",
        .CP0_PRid = 0x00019300,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3 | (0 << CP0C3_VInt),
        .SYNCI_Step = 32,
        .CCRes = 2,
        /* No DSP implemented. */
        .CP0_Status_rw_bitmask = 0x3678FF17,
        .CP1_fcr0 = (1 << FCR0_F64) | (1 << FCR0_L) | (1 << FCR0_W) |
                    (1 << FCR0_D) | (1 << FCR0_S) | (0x93 << FCR0_PRID),
        .insn_flags = CPU_MIPS32R2 | ASE_MIPS16 | ASE_DSP,
    },
    {
        .name = "34Kf",
        .CP0_PRid = 0x00019500,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (15 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3 | (0 << CP0C3_VInt) | (1 << CP0C3_MT),
        .SYNCI_Step = 32,
        .CCRes = 2,
        /* No DSP implemented. */
        .CP0_Status_rw_bitmask = 0x3678FF17,
        /* No DSP implemented. */
        .CP0_TCStatus_rw_bitmask = (0 << CP0TCSt_TCU3) | (0 << CP0TCSt_TCU2) |
                    (1 << CP0TCSt_TCU1) | (1 << CP0TCSt_TCU0) |
                    (0 << CP0TCSt_TMX) | (1 << CP0TCSt_DT) |
                    (1 << CP0TCSt_DA) | (1 << CP0TCSt_A) |
                    (0x3 << CP0TCSt_TKSU) | (1 << CP0TCSt_IXMT) |
                    (0xff << CP0TCSt_TASID),
        .CP1_fcr0 = (1 << FCR0_F64) | (1 << FCR0_L) | (1 << FCR0_W) |
                    (1 << FCR0_D) | (1 << FCR0_S) | (0x95 << FCR0_PRID),
        .CP0_SRSCtl = (0xf << CP0SRSCtl_HSS),
        .CP0_SRSConf0_rw_bitmask = 0x3fffffff,
        .CP0_SRSConf0 = (1 << CP0SRSC0_M) | (0x3fe << CP0SRSC0_SRS3) |
                    (0x3fe << CP0SRSC0_SRS2) | (0x3fe << CP0SRSC0_SRS1),
        .CP0_SRSConf1_rw_bitmask = 0x3fffffff,
        .CP0_SRSConf1 = (1 << CP0SRSC1_M) | (0x3fe << CP0SRSC1_SRS6) |
                    (0x3fe << CP0SRSC1_SRS5) | (0x3fe << CP0SRSC1_SRS4),
        .CP0_SRSConf2_rw_bitmask = 0x3fffffff,
        .CP0_SRSConf2 = (1 << CP0SRSC2_M) | (0x3fe << CP0SRSC2_SRS9) |
                    (0x3fe << CP0SRSC2_SRS8) | (0x3fe << CP0SRSC2_SRS7),
        .CP0_SRSConf3_rw_bitmask = 0x3fffffff,
        .CP0_SRSConf3 = (1 << CP0SRSC3_M) | (0x3fe << CP0SRSC3_SRS12) |
                    (0x3fe << CP0SRSC3_SRS11) | (0x3fe << CP0SRSC3_SRS10),
        .CP0_SRSConf4_rw_bitmask = 0x3fffffff,
        .CP0_SRSConf4 = (0x3fe << CP0SRSC4_SRS15) |
                    (0x3fe << CP0SRSC4_SRS14) | (0x3fe << CP0SRSC4_SRS13),
        .insn_flags = CPU_MIPS32R2 | ASE_MIPS16 | ASE_DSP,
    },
#ifdef TARGET_MIPS64
    {
        .name = "R4000",
        .CP0_PRid = 0x00000400,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (47 << CP0C1_MMU) |
		    (0 << CP0C1_IS) | (3 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (0 << CP0C1_DS) | (3 << CP0C1_DL) | (1 << CP0C1_DA),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 16,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x3678FFFF,
	/* The R4000 has a full 64bit FPU doesn't use the fcr0 bits. */
        .CP1_fcr0 = (0x5 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 40,
        .insn_flags = CPU_MIPS3,
    },
    {
        .name = "5Kc",
        .CP0_PRid = 0x00018100,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT),
        .CP0_Config1 = MIPS_CONFIG1 | (31 << CP0C1_MMU) |
		    (1 << CP0C1_IS) | (4 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (1 << CP0C1_DS) | (4 << CP0C1_DL) | (1 << CP0C1_DA) |
		    (1 << CP0C1_PC) | (1 << CP0C1_WR) | (1 << CP0C1_EP),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x32F8FFFF,
        .SEGBITS = 42,
        .insn_flags = CPU_MIPS64,
    },
    {
        .name = "5Kf",
        .CP0_PRid = 0x00018100,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (31 << CP0C1_MMU) |
		    (1 << CP0C1_IS) | (4 << CP0C1_IL) | (1 << CP0C1_IA) |
		    (1 << CP0C1_DS) | (4 << CP0C1_DL) | (1 << CP0C1_DA) |
		    (1 << CP0C1_PC) | (1 << CP0C1_WR) | (1 << CP0C1_EP),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x36F8FFFF,
	/* The 5Kf has F64 / L / W but doesn't use the fcr0 bits. */
        .CP1_fcr0 = (1 << FCR0_D) | (1 << FCR0_S) |
                    (0x81 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 42,
        .insn_flags = CPU_MIPS64,
    },
    {
        .name = "20Kc",
	/* We emulate a later version of the 20Kc, earlier ones had a broken
           WAIT instruction. */
        .CP0_PRid = 0x000182a0,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT) | (1 << CP0C0_VI),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (47 << CP0C1_MMU) |
		    (2 << CP0C1_IS) | (4 << CP0C1_IL) | (3 << CP0C1_IA) |
		    (2 << CP0C1_DS) | (4 << CP0C1_DL) | (3 << CP0C1_DA) |
		    (1 << CP0C1_PC) | (1 << CP0C1_WR) | (1 << CP0C1_EP),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .CP0_Status_rw_bitmask = 0x36FBFFFF,
	/* The 20Kc has F64 / L / W but doesn't use the fcr0 bits. */
        .CP1_fcr0 = (1 << FCR0_3D) | (1 << FCR0_PS) |
                    (1 << FCR0_D) | (1 << FCR0_S) |
                    (0x82 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 40,
        .insn_flags = CPU_MIPS64 | ASE_MIPS3D,
    },
#endif
};

int mips_find_by_name (const unsigned char *name, mips_def_t **def)
{
    int i, ret;

    ret = -1;
    *def = NULL;
    for (i = 0; i < sizeof(mips_defs) / sizeof(mips_defs[0]); i++) {
        if (strcasecmp(name, mips_defs[i].name) == 0) {
            *def = &mips_defs[i];
            ret = 0;
            break;
        }
    }

    return ret;
}

void mips_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    int i;

    for (i = 0; i < sizeof(mips_defs) / sizeof(mips_defs[0]); i++) {
        (*cpu_fprintf)(f, "MIPS '%s'\n",
                       mips_defs[i].name);
    }
}

#ifndef CONFIG_USER_ONLY
static void no_mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->tlb->nb_tlb = 1;
    env->tlb->map_address = &no_mmu_map_address;
}

static void fixed_mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->tlb->nb_tlb = 1;
    env->tlb->map_address = &fixed_mmu_map_address;
}

static void r4k_mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->tlb->nb_tlb = 1 + ((def->CP0_Config1 >> CP0C1_MMU) & 63);
    env->tlb->map_address = &r4k_map_address;
    env->tlb->do_tlbwi = r4k_do_tlbwi;
    env->tlb->do_tlbwr = r4k_do_tlbwr;
    env->tlb->do_tlbp = r4k_do_tlbp;
    env->tlb->do_tlbr = r4k_do_tlbr;
}

static void mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->tlb = qemu_mallocz(sizeof(CPUMIPSTLBContext));

    /* There are more full-featured MMU variants in older MIPS CPUs,
       R3000, R6000 and R8000 come to mind. If we ever support them,
       this check will need to look up a different place than those
       newfangled config registers. */
    switch ((env->CP0_Config0 >> CP0C0_MT) & 3) {
        case 0:
            no_mmu_init(env, def);
            break;
        case 1:
            r4k_mmu_init(env, def);
            break;
        case 3:
            fixed_mmu_init(env, def);
            break;
        default:
            cpu_abort(env, "MMU type not supported\n");
    }
    env->CP0_Random = env->tlb->nb_tlb - 1;
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
}
#endif /* CONFIG_USER_ONLY */

static void fpu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->fpu = qemu_mallocz(sizeof(CPUMIPSFPUContext));

    env->fpu->fcr0 = def->CP1_fcr0;
#ifdef CONFIG_USER_ONLY
    if (env->CP0_Config1 & (1 << CP0C1_FP))
        env->hflags |= MIPS_HFLAG_FPU;
    if (env->fpu->fcr0 & (1 << FCR0_F64))
        env->hflags |= MIPS_HFLAG_F64;
#endif
}

static void mvp_init (CPUMIPSState *env, mips_def_t *def)
{
    env->mvp = qemu_mallocz(sizeof(CPUMIPSMVPContext));

    /* MVPConf1 implemented, TLB sharable, no gating storage support,
       programmable cache partitioning implemented, number of allocatable
       and sharable TLB entries, MVP has allocatable TCs, 2 VPEs
       implemented, 5 TCs implemented. */
    env->mvp->CP0_MVPConf0 = (1 << CP0MVPC0_M) | (1 << CP0MVPC0_TLBS) |
                             (0 << CP0MVPC0_GS) | (1 << CP0MVPC0_PCP) |
#ifndef CONFIG_USER_ONLY
                             /* Usermode has no TLB support */
                             (env->tlb->nb_tlb << CP0MVPC0_PTLBE) |
#endif
// TODO: actually do 2 VPEs.
//                             (1 << CP0MVPC0_TCA) | (0x1 << CP0MVPC0_PVPE) |
//                             (0x04 << CP0MVPC0_PTC);
                             (1 << CP0MVPC0_TCA) | (0x0 << CP0MVPC0_PVPE) |
                             (0x04 << CP0MVPC0_PTC);
    /* Allocatable CP1 have media extensions, allocatable CP1 have FP support,
       no UDI implemented, no CP2 implemented, 1 CP1 implemented. */
    env->mvp->CP0_MVPConf1 = (1 << CP0MVPC1_CIM) | (1 << CP0MVPC1_CIF) |
                             (0x0 << CP0MVPC1_PCX) | (0x0 << CP0MVPC1_PCP2) |
                             (0x1 << CP0MVPC1_PCP1);
}

int cpu_mips_register (CPUMIPSState *env, mips_def_t *def)
{
    if (!def)
        def = env->cpu_model;
    if (!def)
        cpu_abort(env, "Unable to find MIPS CPU definition\n");
    env->cpu_model = def;
    env->CP0_PRid = def->CP0_PRid;
    env->CP0_Config0 = def->CP0_Config0;
#ifdef TARGET_WORDS_BIGENDIAN
    env->CP0_Config0 |= (1 << CP0C0_BE);
#endif
    env->CP0_Config1 = def->CP0_Config1;
    env->CP0_Config2 = def->CP0_Config2;
    env->CP0_Config3 = def->CP0_Config3;
    env->CP0_Config6 = def->CP0_Config6;
    env->CP0_Config7 = def->CP0_Config7;
    env->SYNCI_Step = def->SYNCI_Step;
    env->CCRes = def->CCRes;
    env->CP0_Status_rw_bitmask = def->CP0_Status_rw_bitmask;
    env->CP0_TCStatus_rw_bitmask = def->CP0_TCStatus_rw_bitmask;
    env->CP0_SRSCtl = def->CP0_SRSCtl;
#ifdef TARGET_MIPS64
    if (def->insn_flags & ISA_MIPS3)
    {
        env->hflags |= MIPS_HFLAG_64;
        env->SEGBITS = def->SEGBITS;
        env->SEGMask = (3ULL << 62) | ((1ULL << def->SEGBITS) - 1);
    } else {
        env->SEGBITS = 32;
        env->SEGMask = 0xFFFFFFFF;
    }
#endif
    env->CP0_SRSConf0_rw_bitmask = def->CP0_SRSConf0_rw_bitmask;
    env->CP0_SRSConf0 = def->CP0_SRSConf0;
    env->CP0_SRSConf1_rw_bitmask = def->CP0_SRSConf1_rw_bitmask;
    env->CP0_SRSConf1 = def->CP0_SRSConf1;
    env->CP0_SRSConf2_rw_bitmask = def->CP0_SRSConf2_rw_bitmask;
    env->CP0_SRSConf2 = def->CP0_SRSConf2;
    env->CP0_SRSConf3_rw_bitmask = def->CP0_SRSConf3_rw_bitmask;
    env->CP0_SRSConf3 = def->CP0_SRSConf3;
    env->CP0_SRSConf4_rw_bitmask = def->CP0_SRSConf4_rw_bitmask;
    env->CP0_SRSConf4 = def->CP0_SRSConf4;
    env->insn_flags = def->insn_flags;

#ifndef CONFIG_USER_ONLY
    mmu_init(env, def);
#endif
    fpu_init(env, def);
    mvp_init(env, def);
    return 0;
}
