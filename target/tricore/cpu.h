/*
 *  TriCore emulation for qemu: main CPU struct.
 *
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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

#ifndef TRICORE_CPU_H
#define TRICORE_CPU_H

#include "cpu-qom.h"
#include "hw/registerfields.h"
#include "exec/cpu-defs.h"
#include "qemu/cpu-float.h"
#include "tricore-defs.h"

typedef struct CPUArchState {
    /* GPR Register */
    uint32_t gpr_a[16];
    uint32_t gpr_d[16];
/* Frequently accessed PSW_USB bits are stored separately for efficiency.
       This contains all the other bits.  Use psw_{read,write} to access
       the whole PSW.  */
    uint32_t PSW;
    /* PSW flag cache for faster execution */
    uint32_t PSW_USB_C;
    uint32_t PSW_USB_V;   /* Only if bit 31 set, then flag is set  */
    uint32_t PSW_USB_SV;  /* Only if bit 31 set, then flag is set  */
    uint32_t PSW_USB_AV;  /* Only if bit 31 set, then flag is set. */
    uint32_t PSW_USB_SAV; /* Only if bit 31 set, then flag is set. */

#define R(ADDR, NAME, FEATURE) uint32_t NAME;
#define A(ADDR, NAME, FEATURE) uint32_t NAME;
#define E(ADDR, NAME, FEATURE) uint32_t NAME;
#include "csfr.h.inc"
#undef R
#undef A
#undef E

    /* Floating Point Registers */
    float_status fp_status;

    /* Internal CPU feature flags.  */
    uint64_t features;
} CPUTriCoreState;

/**
 * TriCoreCPU:
 * @env: #CPUTriCoreState
 *
 * A TriCore CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPUTriCoreState env;
};

struct TriCoreCPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

hwaddr tricore_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);
void tricore_cpu_dump_state(CPUState *cpu, FILE *f, int flags);

FIELD(PCXI, PCPN_13, 24, 8)
FIELD(PCXI, PCPN_161, 22, 8)
FIELD(PCXI, PIE_13, 23, 1)
FIELD(PCXI, PIE_161, 21, 1)
FIELD(PCXI, UL_13, 22, 1)
FIELD(PCXI, UL_161, 20, 1)
FIELD(PCXI, PCXS, 16, 4)
FIELD(PCXI, PCXO, 0, 16)
uint32_t pcxi_get_ul(CPUTriCoreState *env);
uint32_t pcxi_get_pie(CPUTriCoreState *env);
uint32_t pcxi_get_pcpn(CPUTriCoreState *env);
uint32_t pcxi_get_pcxs(CPUTriCoreState *env);
uint32_t pcxi_get_pcxo(CPUTriCoreState *env);
void pcxi_set_ul(CPUTriCoreState *env, uint32_t val);
void pcxi_set_pie(CPUTriCoreState *env, uint32_t val);
void pcxi_set_pcpn(CPUTriCoreState *env, uint32_t val);

FIELD(ICR, IE_161, 15, 1)
FIELD(ICR, IE_13, 8, 1)
FIELD(ICR, PIPN, 16, 8)
FIELD(ICR, CCPN, 0, 8)

uint32_t icr_get_ie(CPUTriCoreState *env);
uint32_t icr_get_ccpn(CPUTriCoreState *env);

void icr_set_ccpn(CPUTriCoreState *env, uint32_t val);
void icr_set_ie(CPUTriCoreState *env, uint32_t val);

#define MASK_PSW_USB 0xff000000
#define MASK_USB_C   0x80000000
#define MASK_USB_V   0x40000000
#define MASK_USB_SV  0x20000000
#define MASK_USB_AV  0x10000000
#define MASK_USB_SAV 0x08000000
#define MASK_PSW_PRS 0x00003000
#define MASK_PSW_IO  0x00000c00
#define MASK_PSW_IS  0x00000200
#define MASK_PSW_GW  0x00000100
#define MASK_PSW_CDE 0x00000080
#define MASK_PSW_CDC 0x0000007f
#define MASK_PSW_FPU_RM 0x3000000

#define MASK_SYSCON_PRO_TEN 0x2
#define MASK_SYSCON_FCD_SF  0x1

#define MASK_CPUID_MOD     0xffff0000
#define MASK_CPUID_MOD_32B 0x0000ff00
#define MASK_CPUID_REV     0x000000ff


#define MASK_FCX_FCXS 0x000f0000
#define MASK_FCX_FCXO 0x0000ffff

#define MASK_LCX_LCXS 0x000f0000
#define MASK_LCX_LCX0 0x0000ffff

#define MASK_DBGSR_DE 0x1
#define MASK_DBGSR_HALT 0x6
#define MASK_DBGSR_SUSP 0x10
#define MASK_DBGSR_PREVSUSP 0x20
#define MASK_DBGSR_PEVT 0x40
#define MASK_DBGSR_EVTSRC 0x1f00

enum tricore_priv_levels {
    TRICORE_PRIV_UM0 = 0x0, /* user mode-0 flag */
    TRICORE_PRIV_UM1 = 0x1, /* user mode-1 flag */
    TRICORE_PRIV_SM  = 0x2, /* kernel mode flag */
};

enum tricore_features {
    TRICORE_FEATURE_13,
    TRICORE_FEATURE_131,
    TRICORE_FEATURE_16,
    TRICORE_FEATURE_161,
    TRICORE_FEATURE_162,
};

static inline int tricore_has_feature(CPUTriCoreState *env, int feature)
{
    return (env->features & (1ULL << feature)) != 0;
}

/* TriCore Traps Classes*/
enum {
    TRAPC_NONE     = -1,
    TRAPC_MMU      = 0,
    TRAPC_PROT     = 1,
    TRAPC_INSN_ERR = 2,
    TRAPC_CTX_MNG  = 3,
    TRAPC_SYSBUS   = 4,
    TRAPC_ASSERT   = 5,
    TRAPC_SYSCALL  = 6,
    TRAPC_NMI      = 7,
    TRAPC_IRQ      = 8
};

/* Class 0 TIN */
enum {
    TIN0_VAF = 0,
    TIN0_VAP = 1,
};

/* Class 1 TIN */
enum {
    TIN1_PRIV = 1,
    TIN1_MPR  = 2,
    TIN1_MPW  = 3,
    TIN1_MPX  = 4,
    TIN1_MPP  = 5,
    TIN1_MPN  = 6,
    TIN1_GRWP = 7,
};

/* Class 2 TIN */
enum {
    TIN2_IOPC = 1,
    TIN2_UOPC = 2,
    TIN2_OPD  = 3,
    TIN2_ALN  = 4,
    TIN2_MEM  = 5,
};

/* Class 3 TIN */
enum {
    TIN3_FCD  = 1,
    TIN3_CDO  = 2,
    TIN3_CDU  = 3,
    TIN3_FCU  = 4,
    TIN3_CSU  = 5,
    TIN3_CTYP = 6,
    TIN3_NEST = 7,
};

/* Class 4 TIN */
enum {
    TIN4_PSE = 1,
    TIN4_DSE = 2,
    TIN4_DAE = 3,
    TIN4_CAE = 4,
    TIN4_PIE = 5,
    TIN4_DIE = 6,
};

/* Class 5 TIN */
enum {
    TIN5_OVF  = 1,
    TIN5_SOVF = 1,
};

/* Class 6 TIN
 *
 * Is always TIN6_SYS
 */

/* Class 7 TIN */
enum {
    TIN7_NMI = 0,
};

uint32_t psw_read(CPUTriCoreState *env);
void psw_write(CPUTriCoreState *env, uint32_t val);
int tricore_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int tricore_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);

void fpu_set_state(CPUTriCoreState *env);

#define MMU_USER_IDX 2

void tricore_cpu_list(void);

#define cpu_list tricore_cpu_list

static inline int cpu_mmu_index(CPUTriCoreState *env, bool ifetch)
{
    return 0;
}

#include "exec/cpu-all.h"

FIELD(TB_FLAGS, PRIV, 0, 2)

void cpu_state_reset(CPUTriCoreState *s);
void tricore_tcg_init(void);

static inline void cpu_get_tb_cpu_state(CPUTriCoreState *env, vaddr *pc,
                                        uint64_t *cs_base, uint32_t *flags)
{
    uint32_t new_flags = 0;
    *pc = env->PC;
    *cs_base = 0;

    new_flags |= FIELD_DP32(new_flags, TB_FLAGS, PRIV,
            extract32(env->PSW, 10, 2));
    *flags = new_flags;
}

#define CPU_RESOLVING_TYPE TYPE_TRICORE_CPU

/* helpers.c */
bool tricore_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                          MMUAccessType access_type, int mmu_idx,
                          bool probe, uintptr_t retaddr);

#endif /* TRICORE_CPU_H */
