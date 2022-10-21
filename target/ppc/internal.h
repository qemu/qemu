/*
 *  PowerPC internal definitions for qemu.
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

#ifndef PPC_INTERNAL_H
#define PPC_INTERNAL_H

#include "hw/registerfields.h"

#define FUNC_MASK(name, ret_type, size, max_val)                  \
static inline ret_type name(uint##size##_t start,                 \
                              uint##size##_t end)                 \
{                                                                 \
    ret_type ret, max_bit = size - 1;                             \
                                                                  \
    if (likely(start == 0)) {                                     \
        ret = max_val << (max_bit - end);                         \
    } else if (likely(end == max_bit)) {                          \
        ret = max_val >> start;                                   \
    } else {                                                      \
        ret = (((uint##size##_t)(-1ULL)) >> (start)) ^            \
            (((uint##size##_t)(-1ULL) >> (end)) >> 1);            \
        if (unlikely(start > end)) {                              \
            return ~ret;                                          \
        }                                                         \
    }                                                             \
                                                                  \
    return ret;                                                   \
}

#if defined(TARGET_PPC64)
FUNC_MASK(MASK, target_ulong, 64, UINT64_MAX);
#else
FUNC_MASK(MASK, target_ulong, 32, UINT32_MAX);
#endif
FUNC_MASK(mask_u32, uint32_t, 32, UINT32_MAX);
FUNC_MASK(mask_u64, uint64_t, 64, UINT64_MAX);

/*****************************************************************************/
/***                           Instruction decoding                        ***/
#define EXTRACT_HELPER(name, shift, nb)                                       \
static inline uint32_t name(uint32_t opcode)                                  \
{                                                                             \
    return extract32(opcode, shift, nb);                                      \
}

#define EXTRACT_SHELPER(name, shift, nb)                                      \
static inline int32_t name(uint32_t opcode)                                   \
{                                                                             \
    return sextract32(opcode, shift, nb);                                     \
}

#define EXTRACT_HELPER_SPLIT(name, shift1, nb1, shift2, nb2)                  \
static inline uint32_t name(uint32_t opcode)                                  \
{                                                                             \
    return extract32(opcode, shift1, nb1) << nb2 |                            \
               extract32(opcode, shift2, nb2);                                \
}

#define EXTRACT_HELPER_SPLIT_3(name,                                          \
                              d0_bits, shift_op_d0, shift_d0,                 \
                              d1_bits, shift_op_d1, shift_d1,                 \
                              d2_bits, shift_op_d2, shift_d2)                 \
static inline int16_t name(uint32_t opcode)                                   \
{                                                                             \
    return                                                                    \
        (((opcode >> (shift_op_d0)) & ((1 << (d0_bits)) - 1)) << (shift_d0)) | \
        (((opcode >> (shift_op_d1)) & ((1 << (d1_bits)) - 1)) << (shift_d1)) | \
        (((opcode >> (shift_op_d2)) & ((1 << (d2_bits)) - 1)) << (shift_d2));  \
}


/* Opcode part 1 */
EXTRACT_HELPER(opc1, 26, 6);
/* Opcode part 2 */
EXTRACT_HELPER(opc2, 1, 5);
/* Opcode part 3 */
EXTRACT_HELPER(opc3, 6, 5);
/* Opcode part 4 */
EXTRACT_HELPER(opc4, 16, 5);
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
EXTRACT_HELPER(BF, 23, 3);
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
/* 16 bits signed immediate value */
EXTRACT_SHELPER(SIMM, 0, 16);
/* 16 bits unsigned immediate value */
EXTRACT_HELPER(UIMM, 0, 16);
/* 5 bits signed immediate value */
EXTRACT_SHELPER(SIMM5, 16, 5);
/* 5 bits signed immediate value */
EXTRACT_HELPER(UIMM5, 16, 5);
/* 4 bits unsigned immediate value */
EXTRACT_HELPER(UIMM4, 16, 4);
/* Bit count */
EXTRACT_HELPER(NB, 11, 5);
/* Shift count */
EXTRACT_HELPER(SH, 11, 5);
/* lwat/stwat/ldat/lwat */
EXTRACT_HELPER(FC, 11, 5);
/* Vector shift count */
EXTRACT_HELPER(VSH, 6, 4);
/* Mask start */
EXTRACT_HELPER(MB, 6, 5);
/* Mask end */
EXTRACT_HELPER(ME, 1, 5);
/* Trap operand */
EXTRACT_HELPER(TO, 21, 5);

EXTRACT_HELPER(CRM, 12, 8);

#ifndef CONFIG_USER_ONLY
EXTRACT_HELPER(SR, 16, 4);
#endif

/* mtfsf/mtfsfi */
EXTRACT_HELPER(FPBF, 23, 3);
EXTRACT_HELPER(FPIMM, 12, 4);
EXTRACT_HELPER(FPL, 25, 1);
EXTRACT_HELPER(FPFLM, 17, 8);
EXTRACT_HELPER(FPW, 16, 1);

/* addpcis */
EXTRACT_HELPER_SPLIT_3(DX, 10, 6, 6, 5, 16, 1, 1, 0, 0)
#if defined(TARGET_PPC64)
/* darn */
EXTRACT_HELPER(L, 16, 2);
#endif
/* wait */
EXTRACT_HELPER(WC, 21, 2);
EXTRACT_HELPER(PL, 16, 2);

/***                            Jump target decoding                       ***/
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

/* DFP Z22-form */
EXTRACT_HELPER(DCM, 10, 6)

/* DFP Z23-form */
EXTRACT_HELPER(RMC, 9, 2)
EXTRACT_HELPER(Rrm, 16, 1)

EXTRACT_HELPER_SPLIT(DQxT, 3, 1, 21, 5);
EXTRACT_HELPER_SPLIT(xT, 0, 1, 21, 5);
EXTRACT_HELPER_SPLIT(xS, 0, 1, 21, 5);
EXTRACT_HELPER_SPLIT(xA, 2, 1, 16, 5);
EXTRACT_HELPER_SPLIT(xB, 1, 1, 11, 5);
EXTRACT_HELPER_SPLIT(xC, 3, 1,  6, 5);
EXTRACT_HELPER(DM, 8, 2);
EXTRACT_HELPER(UIM, 16, 2);
EXTRACT_HELPER(SHW, 8, 2);
EXTRACT_HELPER(SP, 19, 2);
EXTRACT_HELPER(IMM8, 11, 8);
EXTRACT_HELPER(DCMX, 16, 7);
EXTRACT_HELPER_SPLIT_3(DCMX_XV, 5, 16, 0, 1, 2, 5, 1, 6, 6);

void helper_compute_fprf_float16(CPUPPCState *env, float16 arg);
void helper_compute_fprf_float32(CPUPPCState *env, float32 arg);
void helper_compute_fprf_float128(CPUPPCState *env, float128 arg);

/* translate.c */

int ppc_fixup_cpu(PowerPCCPU *cpu);
void create_ppc_opcodes(PowerPCCPU *cpu, Error **errp);
void destroy_ppc_opcodes(PowerPCCPU *cpu);

/* gdbstub.c */
void ppc_gdb_init(CPUState *cs, PowerPCCPUClass *ppc);
gchar *ppc_gdb_arch_name(CPUState *cs);

/**
 * prot_for_access_type:
 * @access_type: Access type
 *
 * Return the protection bit required for the given access type.
 */
static inline int prot_for_access_type(MMUAccessType access_type)
{
    switch (access_type) {
    case MMU_INST_FETCH:
        return PAGE_EXEC;
    case MMU_DATA_LOAD:
        return PAGE_READ;
    case MMU_DATA_STORE:
        return PAGE_WRITE;
    }
    g_assert_not_reached();
}

/* PowerPC MMU emulation */

typedef struct mmu_ctx_t mmu_ctx_t;
bool ppc_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp,
                      int mmu_idx, bool guest_visible);
int get_physical_address_wtlb(CPUPPCState *env, mmu_ctx_t *ctx,
                                     target_ulong eaddr,
                                     MMUAccessType access_type, int type,
                                     int mmu_idx);
/* Software driven TLB helpers */
int ppc6xx_tlb_getnum(CPUPPCState *env, target_ulong eaddr,
                                    int way, int is_code);
/* Context used internally during MMU translations */
struct mmu_ctx_t {
    hwaddr raddr;      /* Real address              */
    hwaddr eaddr;      /* Effective address         */
    int prot;                      /* Protection bits           */
    hwaddr hash[2];    /* Pagetable hash values     */
    target_ulong ptem;             /* Virtual segment ID | API  */
    int key;                       /* Access key                */
    int nx;                        /* Non-execute area          */
};

/* Common routines used by software and hardware TLBs emulation */
static inline int pte_is_valid(target_ulong pte0)
{
    return pte0 & 0x80000000 ? 1 : 0;
}

static inline void pte_invalidate(target_ulong *pte0)
{
    *pte0 &= ~0x80000000;
}

#define PTE_PTEM_MASK 0x7FFFFFBF
#define PTE_CHECK_MASK (TARGET_PAGE_MASK | 0x7B)

#ifdef CONFIG_USER_ONLY
void ppc_cpu_record_sigsegv(CPUState *cs, vaddr addr,
                            MMUAccessType access_type,
                            bool maperr, uintptr_t ra);
#else
bool ppc_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr);
G_NORETURN void ppc_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                            MMUAccessType access_type, int mmu_idx,
                                            uintptr_t retaddr);
#endif

FIELD(GER_MSK, XMSK, 0, 4)
FIELD(GER_MSK, YMSK, 4, 4)
FIELD(GER_MSK, PMSK, 8, 8)

static inline int ger_pack_masks(int pmsk, int ymsk, int xmsk)
{
    int msk = 0;
    msk = FIELD_DP32(msk, GER_MSK, XMSK, xmsk);
    msk = FIELD_DP32(msk, GER_MSK, YMSK, ymsk);
    msk = FIELD_DP32(msk, GER_MSK, PMSK, pmsk);
    return msk;
}

#endif /* PPC_INTERNAL_H */
