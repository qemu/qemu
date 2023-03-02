/*
 * QEMU PowerPC CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_PPC_CPU_QOM_H
#define QEMU_PPC_CPU_QOM_H

#include "hw/core/cpu.h"
#include "qom/object.h"

#ifdef TARGET_PPC64
#define TYPE_POWERPC_CPU "powerpc64-cpu"
#else
#define TYPE_POWERPC_CPU "powerpc-cpu"
#endif

OBJECT_DECLARE_CPU_TYPE(PowerPCCPU, PowerPCCPUClass, POWERPC_CPU)

ObjectClass *ppc_cpu_class_by_name(const char *name);

typedef struct CPUArchState CPUPPCState;
typedef struct ppc_tb_t ppc_tb_t;
typedef struct ppc_dcr_t ppc_dcr_t;

/*****************************************************************************/
/* MMU model                                                                 */
typedef enum powerpc_mmu_t powerpc_mmu_t;
enum powerpc_mmu_t {
    POWERPC_MMU_UNKNOWN    = 0x00000000,
    /* Standard 32 bits PowerPC MMU                            */
    POWERPC_MMU_32B        = 0x00000001,
    /* PowerPC 6xx MMU with software TLB                       */
    POWERPC_MMU_SOFT_6xx   = 0x00000002,
    /*
     * PowerPC 74xx MMU with software TLB (this has been
     * disabled, see git history for more information.
     * keywords: tlbld tlbli TLBMISS PTEHI PTELO)
     */
    POWERPC_MMU_SOFT_74xx  = 0x00000003,
    /* PowerPC 4xx MMU with software TLB                       */
    POWERPC_MMU_SOFT_4xx   = 0x00000004,
    /* PowerPC MMU in real mode only                           */
    POWERPC_MMU_REAL       = 0x00000006,
    /* Freescale MPC8xx MMU model                              */
    POWERPC_MMU_MPC8xx     = 0x00000007,
    /* BookE MMU model                                         */
    POWERPC_MMU_BOOKE      = 0x00000008,
    /* BookE 2.06 MMU model                                    */
    POWERPC_MMU_BOOKE206   = 0x00000009,
#define POWERPC_MMU_64       0x00010000
    /* 64 bits PowerPC MMU                                     */
    POWERPC_MMU_64B        = POWERPC_MMU_64 | 0x00000001,
    /* Architecture 2.03 and later (has LPCR) */
    POWERPC_MMU_2_03       = POWERPC_MMU_64 | 0x00000002,
    /* Architecture 2.06 variant                               */
    POWERPC_MMU_2_06       = POWERPC_MMU_64 | 0x00000003,
    /* Architecture 2.07 variant                               */
    POWERPC_MMU_2_07       = POWERPC_MMU_64 | 0x00000004,
    /* Architecture 3.00 variant                               */
    POWERPC_MMU_3_00       = POWERPC_MMU_64 | 0x00000005,
};

static inline bool mmu_is_64bit(powerpc_mmu_t mmu_model)
{
    return mmu_model & POWERPC_MMU_64;
}

/*****************************************************************************/
/* Exception model                                                           */
typedef enum powerpc_excp_t powerpc_excp_t;
enum powerpc_excp_t {
    POWERPC_EXCP_UNKNOWN   = 0,
    /* Standard PowerPC exception model */
    POWERPC_EXCP_STD,
    /* PowerPC 40x exception model      */
    POWERPC_EXCP_40x,
    /* PowerPC 603/604/G2 exception model */
    POWERPC_EXCP_6xx,
    /* PowerPC 7xx exception model      */
    POWERPC_EXCP_7xx,
    /* PowerPC 74xx exception model     */
    POWERPC_EXCP_74xx,
    /* BookE exception model            */
    POWERPC_EXCP_BOOKE,
    /* PowerPC 970 exception model      */
    POWERPC_EXCP_970,
    /* POWER7 exception model           */
    POWERPC_EXCP_POWER7,
    /* POWER8 exception model           */
    POWERPC_EXCP_POWER8,
    /* POWER9 exception model           */
    POWERPC_EXCP_POWER9,
    /* POWER10 exception model           */
    POWERPC_EXCP_POWER10,
};

/*****************************************************************************/
/* PM instructions */
typedef enum {
    PPC_PM_DOZE,
    PPC_PM_NAP,
    PPC_PM_SLEEP,
    PPC_PM_RVWINKLE,
    PPC_PM_STOP,
} powerpc_pm_insn_t;

/*****************************************************************************/
/* Input pins model                                                          */
typedef enum powerpc_input_t powerpc_input_t;
enum powerpc_input_t {
    PPC_FLAGS_INPUT_UNKNOWN = 0,
    /* PowerPC 6xx bus                  */
    PPC_FLAGS_INPUT_6xx,
    /* BookE bus                        */
    PPC_FLAGS_INPUT_BookE,
    /* PowerPC 405 bus                  */
    PPC_FLAGS_INPUT_405,
    /* PowerPC 970 bus                  */
    PPC_FLAGS_INPUT_970,
    /* PowerPC POWER7 bus               */
    PPC_FLAGS_INPUT_POWER7,
    /* PowerPC POWER9 bus               */
    PPC_FLAGS_INPUT_POWER9,
    /* Freescale RCPU bus               */
    PPC_FLAGS_INPUT_RCPU,
};

typedef struct PPCHash64Options PPCHash64Options;

/**
 * PowerPCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * A PowerPC CPU model.
 */
struct PowerPCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    ResettablePhases parent_phases;
    void (*parent_parse_features)(const char *type, char *str, Error **errp);

    uint32_t pvr;
    /*
     * If @best is false, match if pcc is in the family of pvr
     * Else match only if pcc is the best match for pvr in this family.
     */
    bool (*pvr_match)(struct PowerPCCPUClass *pcc, uint32_t pvr, bool best);
    uint64_t pcr_mask;          /* Available bits in PCR register */
    uint64_t pcr_supported;     /* Bits for supported PowerISA versions */
    uint32_t svr;
    uint64_t insns_flags;
    uint64_t insns_flags2;
    uint64_t msr_mask;
    uint64_t lpcr_mask;         /* Available bits in the LPCR */
    uint64_t lpcr_pm;           /* Power-saving mode Exit Cause Enable bits */
    powerpc_mmu_t   mmu_model;
    powerpc_excp_t  excp_model;
    powerpc_input_t bus_model;
    uint32_t flags;
    int bfd_mach;
    uint32_t l1_dcache_size, l1_icache_size;
#ifndef CONFIG_USER_ONLY
    unsigned int gdb_num_sprs;
    const char *gdb_spr_xml;
#endif
    const PPCHash64Options *hash64_opts;
    struct ppc_radix_page_info *radix_page_info;
    uint32_t lrg_decr_bits;
    int n_host_threads;
    void (*init_proc)(CPUPPCState *env);
    int  (*check_pow)(CPUPPCState *env);
};

#ifndef CONFIG_USER_ONLY
typedef struct PPCTimebase {
    uint64_t guest_timebase;
    int64_t time_of_the_day_ns;
    bool runstate_paused;
} PPCTimebase;

extern const VMStateDescription vmstate_ppc_timebase;

#define VMSTATE_PPC_TIMEBASE_V(_field, _state, _version) {            \
    .name       = (stringify(_field)),                                \
    .version_id = (_version),                                         \
    .size       = sizeof(PPCTimebase),                                \
    .vmsd       = &vmstate_ppc_timebase,                              \
    .flags      = VMS_STRUCT,                                         \
    .offset     = vmstate_offset_value(_state, _field, PPCTimebase),  \
}

void cpu_ppc_clock_vm_state_change(void *opaque, bool running,
                                   RunState state);
#endif

#endif
