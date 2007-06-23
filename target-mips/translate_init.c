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

/* Have config2, 64 sets Icache, 16 bytes Icache line,
   2-way Icache, 64 sets Dcache, 16 bytes Dcache line, 2-way Dcache,
   no coprocessor2 attached, no MDMX support attached,
   no performance counters, watch registers present,
   no code compression, EJTAG present, no FPU */
#define MIPS_CONFIG1                                              \
((1 << CP0C1_M) |                                                 \
 (0x0 << CP0C1_IS) | (0x3 << CP0C1_IL) | (0x1 << CP0C1_IA) |      \
 (0x0 << CP0C1_DS) | (0x3 << CP0C1_DL) | (0x1 << CP0C1_DA) |      \
 (0 << CP0C1_C2) | (0 << CP0C1_MD) | (0 << CP0C1_PC) |            \
 (1 << CP0C1_WR) | (0 << CP0C1_CA) | (1 << CP0C1_EP) |            \
 (0 << CP0C1_FP))

/* Have config3, no tertiary/secondary caches implemented */
#define MIPS_CONFIG2                                              \
((1 << CP0C2_M))

/* No config4, no DSP ASE, no large physaddr,
   no external interrupt controller, no vectored interupts,
   no 1kb pages, no MT ASE, no SmartMIPS ASE, no trace logic */
#define MIPS_CONFIG3                                              \
((0 << CP0C3_M) | (0 << CP0C3_DSPP) | (0 << CP0C3_LPA) |          \
 (0 << CP0C3_VEIC) | (0 << CP0C3_VInt) | (0 << CP0C3_SP) |        \
 (0 << CP0C3_MT) | (0 << CP0C3_SM) | (0 << CP0C3_TL))

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
    int32_t Status_rw_bitmask;
    int32_t CP1_fcr0;
    int32_t SEGBITS;
};

/*****************************************************************************/
/* MIPS CPU definitions */
static mips_def_t mips_defs[] =
{
    {
        .name = "4Kc",
        .CP0_PRid = 0x00018000,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3278FF17,
        .SEGBITS = 32,
    },
    {
        .name = "4KEcR1",
        .CP0_PRid = 0x00018400,
        .CP0_Config0 = MIPS_CONFIG0,
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3278FF17,
        .SEGBITS = 32,
    },
    {
        .name = "4KEc",
        .CP0_PRid = 0x00019000,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3278FF17,
        .SEGBITS = 32,
    },
    {
        .name = "24Kc",
        .CP0_PRid = 0x00019300,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (15 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3278FF17,
        .SEGBITS = 32,
    },
    {
        .name = "24Kf",
        .CP0_PRid = 0x00019300,
        .CP0_Config0 = MIPS_CONFIG0 | (0x1 << CP0C0_AR),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (15 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 32,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3678FF17,
        .CP1_fcr0 = (1 << FCR0_F64) | (1 << FCR0_L) | (1 << FCR0_W) |
                    (1 << FCR0_D) | (1 << FCR0_S) | (0x93 << FCR0_PRID),
        .SEGBITS = 32,
    },
#ifdef TARGET_MIPS64
    {
        .name = "R4000",
        .CP0_PRid = 0x00000400,
        .CP0_Config0 = MIPS_CONFIG0 | (0x2 << CP0C0_AT),
        .CP0_Config1 = MIPS_CONFIG1 | (1 << CP0C1_FP) | (47 << CP0C1_MMU),
        .CP0_Config2 = MIPS_CONFIG2,
        .CP0_Config3 = MIPS_CONFIG3,
        .SYNCI_Step = 16,
        .CCRes = 2,
        .Status_rw_bitmask = 0x3678FFFF,
	/* The R4000 has a full 64bit FPU doesn't use the fcr0 bits. */
        .CP1_fcr0 = (0x5 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 40,
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
        .Status_rw_bitmask = 0x32F8FFFF,
        .SEGBITS = 42,
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
        .Status_rw_bitmask = 0x36F8FFFF,
	/* The 5Kf has F64 / L / W but doesn't use the fcr0 bits. */
        .CP1_fcr0 = (1 << FCR0_D) | (1 << FCR0_S) |
                    (0x81 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 42,
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
        .Status_rw_bitmask = 0x36FBFFFF,
	/* The 20Kc has F64 / L / W but doesn't use the fcr0 bits. */
        .CP1_fcr0 = (1 << FCR0_3D) | (1 << FCR0_PS) |
                    (1 << FCR0_D) | (1 << FCR0_S) |
                    (0x82 << FCR0_PRID) | (0x0 << FCR0_REV),
        .SEGBITS = 40,
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
    env->nb_tlb = 1;
    env->map_address = &no_mmu_map_address;
}

static void fixed_mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->nb_tlb = 1;
    env->map_address = &fixed_mmu_map_address;
}

static void r4k_mmu_init (CPUMIPSState *env, mips_def_t *def)
{
    env->nb_tlb = 1 + ((def->CP0_Config1 >> CP0C1_MMU) & 63);
    env->map_address = &r4k_map_address;
    env->do_tlbwi = r4k_do_tlbwi;
    env->do_tlbwr = r4k_do_tlbwr;
    env->do_tlbp = r4k_do_tlbp;
    env->do_tlbr = r4k_do_tlbr;
}
#endif /* CONFIG_USER_ONLY */

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
    env->Status_rw_bitmask = def->Status_rw_bitmask;
    env->fcr0 = def->CP1_fcr0;
#ifdef TARGET_MIPS64
    env->SEGBITS = def->SEGBITS;
    env->SEGMask = (3ULL << 62) | ((1ULL << def->SEGBITS) - 1);
#endif
#ifdef CONFIG_USER_ONLY
    if (env->CP0_Config1 & (1 << CP0C1_FP))
        env->hflags |= MIPS_HFLAG_FPU;
    if (env->fcr0 & (1 << FCR0_F64))
        env->hflags |= MIPS_HFLAG_F64;
#else
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
    env->CP0_Random = env->nb_tlb - 1;
    env->tlb_in_use = env->nb_tlb;
#endif /* CONFIG_USER_ONLY */
    return 0;
}
