/*
 * CSKY translate header
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CSKY_TRANSLATE_H
#define CSKY_TRANSLATE_H
#include "qemu/log.h"
#include "exec/cpu_ldst.h"

#if !defined(CONFIG_USER_ONLY)
typedef enum TraceMode {
    NORMAL_MODE = 0,
    INST_TRACE_MODE = 1,
    BRAN_TRACE_MODE = 3,
} TraceMode;
#endif

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc;
    int singlestep_enabled;
    uint32_t insn;

    /* Routine used to access memory */
    int mem_idx;   /*selects user or supervisor access*/
    int is_jmp;
    int bctm;

    uint64_t features;

#if !defined(CONFIG_USER_ONLY)
    int super;
    int trust;
    int current_cp;

    /* trace mode support */
    TraceMode trace_mode;
    int cannot_be_traced;
    int maybe_change_flow;
#endif
} DisasContext;

#if !defined(CONFIG_USER_ONLY)
typedef struct csky_tlb_t csky_tlb_t;
struct csky_tlb_t {
    uint32_t VPN;
    uint32_t PageMask; /* [24:13] */
    uint8_t ASID;
    uint16_t G:1;
    uint16_t C0:3;
    uint16_t C1:3;
    uint16_t V0:1;
    uint16_t V1:1;
    uint16_t D0:1;
    uint16_t D1:1;
    uint32_t PFN[2]; /* [31:12] */
};

#define CSKY_TLB_MAX            128
typedef struct CPUCSKYTLBContext {
    int (*get_physical_address)(CPUCSKYState *env, hwaddr *physical, int *prot,
                                target_ulong address, int rw);

    void (*helper_tlbwi) (CPUCSKYState *env);
    void (*helper_tlbwr) (CPUCSKYState *env);
    void (*helper_tlbp) (CPUCSKYState *env);
    void (*helper_tlbr) (CPUCSKYState *env);

    csky_tlb_t *tlb;
    uint8_t *round_robin;
    csky_tlb_t nt_tlb[CSKY_TLB_MAX];
    csky_tlb_t t_tlb[CSKY_TLB_MAX];
    uint8_t nt_round_robin[CSKY_TLB_MAX / 2];
    uint8_t t_round_robin[CSKY_TLB_MAX / 2];
} CPUCSKYTLBContext;

int mmu_get_physical_address(CPUCSKYState *env, hwaddr *physical,
                             int *prot, target_ulong address, int rw);
void csky_tlbwi(CPUCSKYState *env);
void csky_tlbwr(CPUCSKYState *env);
void csky_tlbp(CPUCSKYState *env);
void csky_tlbr(CPUCSKYState *env);
void helper_ttlbinv_all(CPUCSKYState *env);
void helper_tlbinv_all(CPUCSKYState *env);
void helper_tlbinv(CPUCSKYState *env);
int nommu_get_physical_address(struct CPUCSKYState *env, hwaddr *physical,
                               int *prot, target_ulong address, int rw);
int mgu_get_physical_address(struct CPUCSKYState *env, hwaddr *physical,
                             int *prot, target_ulong address, int rw);
#endif

/* VDSP MASK and SHIFT*/
#define CSKY_VDSP_SOP_MASK_M            0x7f
#define CSKY_VDSP_SOP_MASK_S            0xf
#define CSKY_VDSP_SOP_MASK_E            0x3f
#define CSKY_VDSP_SOP_SHI_M             9
#define CSKY_VDSP_SOP_SHI_S             5
#define CSKY_VDSP_SOP_SHI_E             10
#define CSKY_VDSP_WIDTH_BIT_HI          24
#define CSKY_VDSP_WIDTH_BIT_LO          20
#define CSKY_VDSP_REG_MASK              0xf
#define CSKY_VDSP_REG_SHI_VRX           16
#define CSKY_VDSP_REG_SHI_VRY           21
#define CSKY_VDSP_SIGN_SHI              4
#define CSKY_VDSP_SIGN_MASK             0x1

#define VDSPLEN                 128
#define VDSP_VADD               0x0
#define VDSP_VSUB               0x1
#define VDSP_VMUL               0x2
#define VDSP_VSH                0x3
#define VDSP_VCMP               0x4
#define VDSP_VAND               0x5
#define VDSP_VMOV               0x6
#define VDSP_VSPE               0x7
#define VDSP_VABS               0x8
#define VDSP_VMVVR              0x9
#define VDSP_VINS               0xa

#define VDSP_HELPER(name) HELPER(glue(vdsp_, name))

/* DSPv2 Macro begin */
/* DSPv2 ISA secondary/third OP MASK and SHIFT */
#define CSKY_DSPV2_SOP_MASK         0x1f
#define CSKY_DSPV2_SOP_SHI          11
#define CSKY_DSPV2_THOP_MASK        0x3f
#define CSKY_DSPV2_THOP_SHI         5

#define CSKY_DSPV2_REG_MASK         0x1f
#define CSKY_DSPV2_REG_SHI_RX       16
#define CSKY_DSPV2_REG_SHI_RY       21
#define CSKY_DSPV2_REG_SHI_RZ       0

#define DSPV2_HELPER(name) HELPER(glue(dspv2_, name))

/* SOP and THOP for dspv2 instructions. */
#define DSPV2_ADD_SUB               0x18
#define   OP_PADD_8_1st             0x2
#define   OP_PADD_8_2nd             0x6
#define   OP_PADD_16_1st            0x0
#define   OP_PADD_16_2nd            0x4
#define   OP_PADD_U8_S              0xa
#define   OP_PADD_S8_S              0xe
#define   OP_PADD_U16_S             0x8
#define   OP_PADD_S16_S             0xc
#define   OP_ADD_U32_S              0x9
#define   OP_ADD_S32_S              0xd
#define   OP_PSUB_8_1st             0x22
#define   OP_PSUB_8_2nd             0x26
#define   OP_PSUB_16_1st            0x20
#define   OP_PSUB_16_2nd            0x24
#define   OP_PSUB_U8_S              0x2a
#define   OP_PSUB_S8_S              0x2e
#define   OP_PSUB_U16_S             0x28
#define   OP_PSUB_S16_S             0x2c
#define   OP_SUB_U32_S              0x29
#define   OP_SUB_S32_S              0x2d
#define   OP_PADDH_U8               0x12
#define   OP_PADDH_S8               0x16
#define   OP_PADDH_U16              0x10
#define   OP_PADDH_S16              0x14
#define   OP_ADDH_U32               0x11
#define   OP_ADDH_S32               0x15
#define   OP_PSUBH_U8               0x32
#define   OP_PSUBH_S8               0x36
#define   OP_PSUBH_U16              0x30
#define   OP_PSUBH_S16              0x34
#define   OP_SUBH_U32               0x31
#define   OP_SUBH_S32               0x35
#define   OP_ADD_64_1st             0x3
#define   OP_ADD_64_2nd             0x7
#define   OP_SUB_64_1st             0x23
#define   OP_SUB_64_2nd             0x27
#define   OP_ADD_U64_S              0xb
#define   OP_ADD_S64_S              0xf
#define   OP_SUB_U64_S              0x2b
#define   OP_SUB_S64_S              0x2f

#define DSPV2_CMP                   0x19
#define   OP_PASX_16_1st            0x3
#define   OP_PASX_16_2nd            0x7
#define   OP_PSAX_16_1st            0x23
#define   OP_PSAX_16_2nd            0x27
#define   OP_PASX_U16_S             0xb
#define   OP_PASX_S16_S             0xf
#define   OP_PSAX_U16_S             0x2b
#define   OP_PSAX_S16_S             0x2f
#define   OP_PASXH_U16              0x13
#define   OP_PASXH_S16              0x17
#define   OP_PSAXH_U16              0x33
#define   OP_PSAXH_S16              0x37
#define   OP_PCMPNE_8_1st           0x2
#define   OP_PCMPNE_8_2nd           0x6
#define   OP_PCMPNE_16_1st          0x0
#define   OP_PCMPNE_16_2nd          0x4
#define   OP_PCMPHS_U8              0xa
#define   OP_PCMPHS_S8              0xe
#define   OP_PCMPHS_U16             0x8
#define   OP_PCMPHS_S16             0xc
#define   OP_PCMPLT_U8              0x12
#define   OP_PCMPLT_S8              0x16
#define   OP_PCMPLT_U16             0x10
#define   OP_PCMPLT_S16             0x14
#define   OP_PMAX_U8                0x22
#define   OP_PMAX_S8                0x26
#define   OP_PMAX_U16               0x20
#define   OP_PMAX_S16               0x24
#define   OP_MAX_U32                0x21
#define   OP_MAX_S32                0x25
#define   OP_PMIN_U8                0x2a
#define   OP_PMIN_S8                0x2e
#define   OP_PMIN_U16               0x28
#define   OP_PMIN_S16               0x2c
#define   OP_MIN_U32                0x29
#define   OP_MIN_S32                0x2d

#define DSPV2_SEL                   0x12
#define   OP_SEL_begin              0x0
#define   OP_SEL_end                0x1f

#define DSPV2_MISC                  0x1c
#define   OP_PSABSA_U8_1st          0x2
#define   OP_PSABSA_U8_2nd          0x6
#define   OP_PSABSAA_U8_1st         0xa
#define   OP_PSABSAA_U8_2nd         0xe
#define   OP_DIVUL                  0x13
#define   OP_DIVSL                  0x17
#define   OP_MULACA_S8              0x26

#define DSPV2_SHIFT                 0x1a
#define   OP_ASRI_S32_R             0xd
#define   OP_ASR_S32_R              0xf
#define   OP_LSRI_U32_R             0x19
#define   OP_LSR_U32_R              0x1b
#define   OP_LSLI_U32_S             0x29
#define   OP_LSLI_S32_S             0x2d
#define   OP_LSL_U32_S              0x2b
#define   OP_LSL_S32_S              0x2f
#define   OP_PASRI_S16              0x4
#define   OP_PASR_S16               0x6
#define   OP_PASRI_S16_R            0xc
#define   OP_PASR_S16_R             0xe
#define   OP_PLSRI_U16              0x10
#define   OP_PLSR_U16               0x12
#define   OP_PLSRI_U16_R            0x18
#define   OP_PLSR_U16_R             0x1a
#define   OP_PLSLI_U16              0x20
#define   OP_PLSL_U16               0x22
#define   OP_PLSLI_U16_S            0x28
#define   OP_PLSLI_S16_S            0x2c
#define   OP_PLSL_U16_S             0x2a
#define   OP_PLSL_S16_S             0x2e

#define DSPV2_PKG_begin             0x14
#define DSPV2_PKG_end               0x17
#define DSPV2_DEXT                  0x13
#define DSPV2_PKG_CLIP              0x1b
#define   OP_PKGLL_1st              0x2
#define   OP_PKGLL_2nd              0x6
#define   OP_PKGHH_1st              0x3
#define   OP_PKGHH_2nd              0x7
#define   OP_PEXT_U8_E              0x8
#define   OP_PEXT_S8_E              0xc
#define   OP_PEXTX_U8_E             0x9
#define   OP_PEXTX_S8_E             0xd
#define   OP_NARL_1st               0x10
#define   OP_NARL_2nd               0x14
#define   OP_NARH_1st               0x11
#define   OP_NARH_2nd               0x15
#define   OP_NARLX_1st              0x12
#define   OP_NARLX_2nd              0x16
#define   OP_NARHX_1st              0x13
#define   OP_NARHX_2nd              0x17
#define   OP_CLIPI_U32              0x18
#define   OP_CLIPI_S32              0x1c
#define   OP_CLIP_U32               0x19
#define   OP_CLIP_S32               0x1d
#define   OP_PCLIPI_U16             0x1a
#define   OP_PCLIPI_S16             0x1e
#define   OP_PCLIP_U16              0x1b
#define   OP_PCLIP_S16              0x1f
#define   OP_PABS_S8_S              0x24
#define   OP_PABS_S16_S             0x25
#define   OP_ABS_S32_S              0x26
#define   OP_PNEG_S8_S              0x2c
#define   OP_PNEG_S16_S             0x2d
#define   OP_NEG_S32_S              0x2e
#define   OP_DUP_8_begin            0x30
#define   OP_DUP_8_end              0x37
#define   OP_DUP_16_begin           0x38
#define   OP_DUP_16_end             0x3f

#define DSPV2_MUL_1st               0x10
/* 32X32 -> 64 */
#define   OP_MUL_U32                0x0
#define   OP_MUL_S32                0x10
#define   OP_MULA_U32               0x4
#define   OP_MULA_S32               0x14
#define   OP_MULS_U32               0x6
#define   OP_MULS_S32               0x16
#define   OP_MULA_U32_S             0xc
#define   OP_MULA_S32_S             0x1c
#define   OP_MULS_U32_S             0xe
#define   OP_MULS_S32_S             0x1e
#define   OP_MULA_32_L              0x22
/* 32X32 -> 32(hi) */
#define   OP_MUL_S32_H              0x20
#define   OP_MUL_S32_RH             0x30
#define   OP_RMUL_S32_H             0x28
#define   OP_RMUL_S32_RH            0x38
#define   OP_MULA_S32_HS            0x2c
#define   OP_MULS_S32_HS            0x2e
#define   OP_MULA_S32_RHS           0x3c
#define   OP_MULS_S32_RHS           0x3e
/* 16X16  , not SIMD */
#define   OP_MULLL_S16              0x1
#define   OP_MULHH_S16              0x13
#define   OP_MULHL_S16              0x11
#define   OP_RMULLL_S16             0x9
#define   OP_RMULHH_S16             0x1b
#define   OP_RMULHL_S16             0x19
#define   OP_MULALL_S16_S           0xd
#define   OP_MULAHH_S16_S           0x1f
#define   OP_MULAHL_S16_S           0x1d
#define   OP_MULALL_S16_E           0x5
#define   OP_MULAHH_S16_E           0x17
#define   OP_MULAHL_S16_E           0x7
/* 16X16, SIMD */
#define   OP_PMUL_U16               0x25
#define   OP_PMULX_U16              0x27
#define   OP_PMUL_S16               0x21
#define   OP_PMULX_S16              0x23
#define   OP_PRMUL_S16              0x29
#define   OP_PRMULX_S16             0x2b
#define   OP_PRMUL_S16_H            0x2d
#define   OP_PRMUL_S16_RH           0x3d
#define   OP_PRMULX_S16_H           0x2f
#define   OP_PRMULX_S16_RH          0x3f

#define DSPV2_MUL_2nd               0x11
/* 32X32 -> 32(hi) */
#define   OP_MULXL_S32              0x0
#define   OP_MULXL_S32_R            0x10
#define   OP_MULXH_S32              0x20
#define   OP_MULXH_S32_R            0x30
#define   OP_RMULXL_S32             0x8
#define   OP_RMULXL_S32_R           0x18
#define   OP_RMULXH_S32             0x28
#define   OP_RMULXH_S32_R           0x38
#define   OP_MULAXL_S32_S           0xc
#define   OP_MULAXL_S32_RS          0x1c
#define   OP_MULAXH_S32_S           0x2c
#define   OP_MULAXH_S32_RS          0x3c
/* 16X16 chain */
#define   OP_MULCA_S16_S            0x9
#define   OP_MULCAX_S16_S           0xb
#define   OP_MULCS_S16              0x11
#define   OP_MULCSR_S16             0x13
#define   OP_MULCSX_S16             0x21
/* 16X16, chain, accumulate */
#define   OP_MULACA_S16_S           0xd
#define   OP_MULACAX_S16_S          0xf
#define   OP_MULACS_S16_S           0x1d
#define   OP_MULACSR_S16_S          0x1f
#define   OP_MULACSX_S16_S          0x2d
#define   OP_MULSCA_S16_S           0x2f
#define   OP_MULSCAX_S16_S          0x3d
#define   OP_MULACA_S16_E           0x5
#define   OP_MULACAX_S16_E          0x7
#define   OP_MULACS_S16_E           0x15
#define   OP_MULACSR_S16_E          0x17
#define   OP_MULACSX_S16_E          0x25
#define   OP_MULSCA_S16_E           0x27
#define   OP_MULSCAX_S16_E          0x35

/* sop for dsp v2 ld/st instructions */
#define   OP_LDBI_B                 0x20
#define   OP_LDBI_H                 0x21
#define   OP_LDBI_W                 0x22
#define   OP_PLDBI_D                0x23
#define   OP_LDBI_BS                0x25
#define   OP_LDBI_HS                0x24
#define   OP_LDBIR_B                0x28
#define   OP_LDBIR_H                0x29
#define   OP_LDBIR_W                0x2a
#define   OP_PLDBIR_D               0x2b
#define   OP_LDBIR_BS               0x2c
#define   OP_LDBIR_HS               0x2d
#define   OP_STBI_B                 0x20
#define   OP_STBI_H                 0x21
#define   OP_STBI_W                 0x22
#define   OP_STBIR_B                0x28
#define   OP_STBIR_H                0x29
#define   OP_STBIR_W                0x2a

/* DSPv2 Macro end. */

static inline void helper_update_psr(CPUCSKYState *env)
{
    env->cp0.psr &= ~0xc000c401;
    env->cp0.psr |= env->psr_s << 31;
    env->cp0.psr |= env->psr_t << 30;
    env->cp0.psr |= env->psr_bm << 10;
    env->cp0.psr |= env->psr_c;
    env->cp0.psr |= env->psr_tm << 14;
}

static inline void helper_record_psr_bits(CPUCSKYState *env)
{
    env->psr_s = PSR_S(env->cp0.psr);
    env->psr_t = PSR_T(env->cp0.psr);
    env->psr_bm = PSR_BM(env->cp0.psr);
    env->psr_c = PSR_C(env->cp0.psr);
    env->psr_tm = PSR_TM(env->cp0.psr);
}

static inline void helper_switch_regs(CPUCSKYState *env)
{
    uint32_t temps[16];
    if (env->features & (CPU_610 | CPU_807 | CPU_810)) {
        memcpy(temps, env->regs, 16 * 4);
        memcpy(env->regs, env->banked_regs, 16 * 4);
        memcpy(env->banked_regs, temps, 16 * 4);
    }
}

#ifdef TARGET_CSKYV2
static inline void helper_save_sp(CPUCSKYState *env)
{
    if (env->psr_t && (env->features & ABIV2_TEE)) {
        if ((env->cp0.psr & 0x2)
            && (env->features & (CPU_807 | CPU_810))) {
            env->stackpoint.t_asp = env->regs[14];
        } else if (env->psr_s) {
            env->stackpoint.t_ssp = env->regs[14];
        } else {
            env->stackpoint.t_usp = env->regs[14];
        }
    } else {
        if ((env->cp0.psr & 0x2)
            && (env->features & (CPU_807 | CPU_810))) {
            env->stackpoint.nt_asp = env->regs[14];
        } else if (env->psr_s) {
            env->stackpoint.nt_ssp = env->regs[14];
        } else {
            env->stackpoint.nt_usp = env->regs[14];
        }
    }
}

static inline void helper_choose_sp(CPUCSKYState *env)
{
    if (env->psr_t && (env->features & ABIV2_TEE)) {
        if ((env->cp0.psr & 0x2)
            && (env->features & (CPU_807 | CPU_810))) {
            env->regs[14] = env->stackpoint.t_asp;
        } else if (env->psr_s) {
            env->regs[14] = env->stackpoint.t_ssp;
        } else {
            env->regs[14] = env->stackpoint.t_usp;
        }
    } else {
        if ((env->cp0.psr & 0x2)
            && (env->features & (CPU_807 | CPU_810))) {
            env->regs[14] = env->stackpoint.nt_asp;
        } else if (env->psr_s) {
            env->regs[14] = env->stackpoint.nt_ssp;
        } else {
            env->regs[14] = env->stackpoint.nt_usp;
        }
    }
}

static inline void helper_tee_save_cr(CPUCSKYState *env)
{
    if (env->psr_t) {
        env->tee.t_vbr = env->cp0.vbr;
        env->tee.t_epsr = env->cp0.epsr;
        env->tee.t_epc = env->cp0.epc;
        env->t_mmu = env->mmu;
    } else {
        env->tee.nt_vbr = env->cp0.vbr;
        env->tee.nt_epsr = env->cp0.epsr;
        env->tee.nt_epc = env->cp0.epc;
        env->nt_mmu = env->mmu;
    }
}

static inline void helper_tee_choose_cr(CPUCSKYState *env)
{
    if (env->psr_t) {
        env->cp0.vbr = env->tee.t_vbr;
        env->cp0.epsr = env->tee.t_epsr;
        env->cp0.epc = env->tee.t_epc;
        env->mmu = env->t_mmu;
#if !defined(CONFIG_USER_ONLY)
        env->tlb_context->tlb = env->tlb_context->t_tlb;
        env->tlb_context->round_robin = env->tlb_context->t_round_robin;
#endif
    } else {
        env->cp0.vbr = env->tee.nt_vbr;
        env->cp0.epsr = env->tee.nt_epsr;
        env->cp0.epc = env->tee.nt_epc;
        env->mmu = env->nt_mmu;
#if !defined(CONFIG_USER_ONLY)
        env->tlb_context->tlb = env->tlb_context->nt_tlb;
        env->tlb_context->round_robin = env->tlb_context->nt_round_robin;
#endif
    }
}

/* For ck_tee_lite, when change from Trust to Non-Trust world by NT-interrupt,
 * need to push the GPRs to trust-supervised stack, and clear them. */
static inline void helper_tee_save_gpr(CPUCSKYState *env)
{
    int32_t i;
    if (env->features & CPU_801) {
        for (i = 0; i <= 8; i++) {
            env->stackpoint.t_ssp -= 4;
            cpu_stl_data(env, env->stackpoint.t_ssp, env->regs[i]);
            env->regs[i] = 0;
        }
        cpu_stl_data(env, env->stackpoint.t_ssp - 4, env->regs[13]);
        env->regs[13] = 0;
        cpu_stl_data(env, env->stackpoint.t_ssp - 8, env->regs[15]);
        env->regs[15] = 0;
        env->stackpoint.t_ssp -= 8;
    } else if (env->features & CPU_802) {
        for (i = 0; i <= 13; i++) {
            env->stackpoint.t_ssp -= 4;
            cpu_stl_data(env, env->stackpoint.t_ssp, env->regs[i]);
            env->regs[i] = 0;
        }
        cpu_stl_data(env, env->stackpoint.t_ssp - 4, env->regs[15]);
        env->regs[15] = 0;
        env->stackpoint.t_ssp -= 4;
    } else if (env->features & CPU_803S) {
        for (i = 0; i <= 13; i++) {
            env->stackpoint.t_ssp -= 4;
            cpu_stl_data(env, env->stackpoint.t_ssp, env->regs[i]);
            env->regs[i] = 0;
        }
        cpu_stl_data(env, env->stackpoint.t_ssp - 4, env->regs[15]);
        env->regs[15] = 0;
        cpu_stl_data(env, env->stackpoint.t_ssp - 8, env->regs[28]);
        env->regs[28] = 0;
        env->stackpoint.t_ssp -= 8;
    }
}

/* For ck_tee_lite, when return from NT-interrupt which change the world from
 * Trust to Non-Trust world before, need to pop the GPRs from
 * trust-supervised stack. */
static inline void helper_tee_restore_gpr(CPUCSKYState *env)
{
    int32_t i;
    if (env->features & CPU_801) {
        env->regs[15] = cpu_ldl_data(env, env->stackpoint.t_ssp);
        env->regs[13] = cpu_ldl_data(env, env->stackpoint.t_ssp + 4);
        env->stackpoint.t_ssp += 8;
        for (i = 8; i >= 0; i--) {
            env->regs[i] = cpu_ldl_data(env, env->stackpoint.t_ssp);
            env->stackpoint.t_ssp += 4;
        }
    } else if (env->features & CPU_802) {
        env->regs[15] = cpu_ldl_data(env, env->stackpoint.t_ssp);
        env->stackpoint.t_ssp += 4;
        for (i = 13; i >= 0; i--) {
            env->regs[i] = cpu_ldl_data(env, env->stackpoint.t_ssp);
            env->stackpoint.t_ssp += 4;
        }
    } else if (env->features & CPU_803S) {
        env->regs[28] = cpu_ldl_data(env, env->stackpoint.t_ssp);
        env->regs[15] = cpu_ldl_data(env, env->stackpoint.t_ssp + 4);
        env->stackpoint.t_ssp += 8;
        for (i = 13; i >= 0; i--) {
            env->regs[i] = cpu_ldl_data(env, env->stackpoint.t_ssp);
            env->stackpoint.t_ssp += 4;
        }
    }
}
#endif

static inline int has_insn(DisasContext *ctx, uint32_t flags)
{
    if (ctx->features & flags) {
        return 1;
    } else {
        return 0;
    }
}

static inline void print_exception(DisasContext *ctx, int excp)
{
    switch (excp) {
    case EXCP_CSKY_RESET:
    case EXCP_CSKY_ALIGN:
    case EXCP_CSKY_DATA_ABORT:
    case EXCP_CSKY_DIV:
    case EXCP_CSKY_UDEF:
    case EXCP_CSKY_PRIVILEGE:
    case EXCP_CSKY_TRACE:
    case EXCP_CSKY_BKPT:
    case EXCP_CSKY_URESTORE:
    case EXCP_CSKY_IDLY4:
    case EXCP_CSKY_HAI:
        qemu_log_mask(LOG_GUEST_ERROR, "##exception No = 0x%x\n", excp);
        qemu_log_mask(LOG_GUEST_ERROR, "##exception pc = 0x%x\n", ctx->pc);
        break;
    default:
        break;
    }
}
#endif
