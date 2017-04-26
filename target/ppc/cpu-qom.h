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

#include "qom/cpu.h"

#ifdef TARGET_PPC64
#define TYPE_POWERPC_CPU "powerpc64-cpu"
#elif defined(TARGET_PPCEMB)
#define TYPE_POWERPC_CPU "embedded-powerpc-cpu"
#else
#define TYPE_POWERPC_CPU "powerpc-cpu"
#endif

#define POWERPC_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(PowerPCCPUClass, (klass), TYPE_POWERPC_CPU)
#define POWERPC_CPU(obj) \
    OBJECT_CHECK(PowerPCCPU, (obj), TYPE_POWERPC_CPU)
#define POWERPC_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(PowerPCCPUClass, (obj), TYPE_POWERPC_CPU)

typedef struct PowerPCCPU PowerPCCPU;
typedef struct CPUPPCState CPUPPCState;
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
    /* PowerPC 74xx MMU with software TLB                      */
    POWERPC_MMU_SOFT_74xx  = 0x00000003,
    /* PowerPC 4xx MMU with software TLB                       */
    POWERPC_MMU_SOFT_4xx   = 0x00000004,
    /* PowerPC 4xx MMU with software TLB and zones protections */
    POWERPC_MMU_SOFT_4xx_Z = 0x00000005,
    /* PowerPC MMU in real mode only                           */
    POWERPC_MMU_REAL       = 0x00000006,
    /* Freescale MPC8xx MMU model                              */
    POWERPC_MMU_MPC8xx     = 0x00000007,
    /* BookE MMU model                                         */
    POWERPC_MMU_BOOKE      = 0x00000008,
    /* BookE 2.06 MMU model                                    */
    POWERPC_MMU_BOOKE206   = 0x00000009,
    /* PowerPC 601 MMU model (specific BATs format)            */
    POWERPC_MMU_601        = 0x0000000A,
#define POWERPC_MMU_64       0x00010000
#define POWERPC_MMU_1TSEG    0x00020000
#define POWERPC_MMU_AMR      0x00040000
#define POWERPC_MMU_64K      0x00080000
#define POWERPC_MMU_V3       0x00100000 /* ISA V3.00 MMU Support */
    /* 64 bits PowerPC MMU                                     */
    POWERPC_MMU_64B        = POWERPC_MMU_64 | 0x00000001,
    /* Architecture 2.03 and later (has LPCR) */
    POWERPC_MMU_2_03       = POWERPC_MMU_64 | 0x00000002,
    /* Architecture 2.06 variant                               */
    POWERPC_MMU_2_06       = POWERPC_MMU_64 | POWERPC_MMU_1TSEG
                             | POWERPC_MMU_64K
                             | POWERPC_MMU_AMR | 0x00000003,
    /* Architecture 2.07 variant                               */
    POWERPC_MMU_2_07       = POWERPC_MMU_64 | POWERPC_MMU_1TSEG
                             | POWERPC_MMU_64K
                             | POWERPC_MMU_AMR | 0x00000004,
    /* Architecture 3.00 variant                               */
    POWERPC_MMU_3_00       = POWERPC_MMU_64 | POWERPC_MMU_1TSEG
                             | POWERPC_MMU_64K
                             | POWERPC_MMU_AMR | POWERPC_MMU_V3
                             | 0x00000005,
};
#define POWERPC_MMU_VER(x) ((x) & (POWERPC_MMU_64 | 0xFFFF))
#define POWERPC_MMU_VER_64B POWERPC_MMU_VER(POWERPC_MMU_64B)
#define POWERPC_MMU_VER_2_03 POWERPC_MMU_VER(POWERPC_MMU_2_03)
#define POWERPC_MMU_VER_2_06 POWERPC_MMU_VER(POWERPC_MMU_2_06)
#define POWERPC_MMU_VER_2_07 POWERPC_MMU_VER(POWERPC_MMU_2_07)
#define POWERPC_MMU_VER_3_00 POWERPC_MMU_VER(POWERPC_MMU_3_00)

/*****************************************************************************/
/* Exception model                                                           */
typedef enum powerpc_excp_t powerpc_excp_t;
enum powerpc_excp_t {
    POWERPC_EXCP_UNKNOWN   = 0,
    /* Standard PowerPC exception model */
    POWERPC_EXCP_STD,
    /* PowerPC 40x exception model      */
    POWERPC_EXCP_40x,
    /* PowerPC 601 exception model      */
    POWERPC_EXCP_601,
    /* PowerPC 602 exception model      */
    POWERPC_EXCP_602,
    /* PowerPC 603 exception model      */
    POWERPC_EXCP_603,
    /* PowerPC 603e exception model     */
    POWERPC_EXCP_603E,
    /* PowerPC G2 exception model       */
    POWERPC_EXCP_G2,
    /* PowerPC 604 exception model      */
    POWERPC_EXCP_604,
    /* PowerPC 7x0 exception model      */
    POWERPC_EXCP_7x0,
    /* PowerPC 7x5 exception model      */
    POWERPC_EXCP_7x5,
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
};

/*****************************************************************************/
/* PM instructions */
typedef enum {
    PPC_PM_DOZE,
    PPC_PM_NAP,
    PPC_PM_SLEEP,
    PPC_PM_RVWINKLE,
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
    /* PowerPC 401 bus                  */
    PPC_FLAGS_INPUT_401,
    /* Freescale RCPU bus               */
    PPC_FLAGS_INPUT_RCPU,
};

struct ppc_segment_page_sizes;

/**
 * PowerPCCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * A PowerPC CPU model.
 */
typedef struct PowerPCCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    void (*parent_reset)(CPUState *cpu);

    uint32_t pvr;
    bool (*pvr_match)(struct PowerPCCPUClass *pcc, uint32_t pvr);
    uint64_t pcr_mask;          /* Available bits in PCR register */
    uint64_t pcr_supported;     /* Bits for supported PowerISA versions */
    uint32_t svr;
    uint64_t insns_flags;
    uint64_t insns_flags2;
    uint64_t msr_mask;
    powerpc_mmu_t   mmu_model;
    powerpc_excp_t  excp_model;
    powerpc_input_t bus_model;
    uint32_t flags;
    int bfd_mach;
    uint32_t l1_dcache_size, l1_icache_size;
    const struct ppc_segment_page_sizes *sps;
    struct ppc_radix_page_info *radix_page_info;
    void (*init_proc)(CPUPPCState *env);
    int  (*check_pow)(CPUPPCState *env);
    int (*handle_mmu_fault)(PowerPCCPU *cpu, vaddr eaddr, int rwx, int mmu_idx);
    bool (*interrupts_big_endian)(PowerPCCPU *cpu);
} PowerPCCPUClass;

#ifndef CONFIG_USER_ONLY
typedef struct PPCTimebase {
    uint64_t guest_timebase;
    int64_t time_of_the_day_ns;
} PPCTimebase;

extern const struct VMStateDescription vmstate_ppc_timebase;

#define VMSTATE_PPC_TIMEBASE_V(_field, _state, _version) {            \
    .name       = (stringify(_field)),                                \
    .version_id = (_version),                                         \
    .size       = sizeof(PPCTimebase),                                \
    .vmsd       = &vmstate_ppc_timebase,                              \
    .flags      = VMS_STRUCT,                                         \
    .offset     = vmstate_offset_value(_state, _field, PPCTimebase),  \
}

void cpu_ppc_clock_vm_state_change(void *opaque, int running,
                                   RunState state);
#endif

#endif
