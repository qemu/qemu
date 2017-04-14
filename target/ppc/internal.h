/*
 *  PowerPC interal definitions for qemu.
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

#ifndef PPC_INTERNAL_H
#define PPC_INTERNAL_H

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
    return (opcode >> (shift)) & ((1 << (nb)) - 1);                           \
}

#define EXTRACT_SHELPER(name, shift, nb)                                      \
static inline int32_t name(uint32_t opcode)                                   \
{                                                                             \
    return (int16_t)((opcode >> (shift)) & ((1 << (nb)) - 1));                \
}

#define EXTRACT_HELPER_SPLIT(name, shift1, nb1, shift2, nb2)                  \
static inline uint32_t name(uint32_t opcode)                                  \
{                                                                             \
    return (((opcode >> (shift1)) & ((1 << (nb1)) - 1)) << nb2) |             \
            ((opcode >> (shift2)) & ((1 << (nb2)) - 1));                      \
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
EXTRACT_HELPER(SIMM5, 16, 5);
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

typedef union _ppc_vsr_t {
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    float32 f32[4];
    float64 f64[2];
    float128 f128;
    Int128  s128;
} ppc_vsr_t;

#if defined(HOST_WORDS_BIGENDIAN)
#define VsrB(i) u8[i]
#define VsrH(i) u16[i]
#define VsrW(i) u32[i]
#define VsrD(i) u64[i]
#else
#define VsrB(i) u8[15 - (i)]
#define VsrH(i) u16[7 - (i)]
#define VsrW(i) u32[3 - (i)]
#define VsrD(i) u64[1 - (i)]
#endif

static inline void getVSR(int n, ppc_vsr_t *vsr, CPUPPCState *env)
{
    if (n < 32) {
        vsr->VsrD(0) = env->fpr[n];
        vsr->VsrD(1) = env->vsr[n];
    } else {
        vsr->u64[0] = env->avr[n - 32].u64[0];
        vsr->u64[1] = env->avr[n - 32].u64[1];
    }
}

static inline void putVSR(int n, ppc_vsr_t *vsr, CPUPPCState *env)
{
    if (n < 32) {
        env->fpr[n] = vsr->VsrD(0);
        env->vsr[n] = vsr->VsrD(1);
    } else {
        env->avr[n - 32].u64[0] = vsr->u64[0];
        env->avr[n - 32].u64[1] = vsr->u64[1];
    }
}

void helper_compute_fprf_float16(CPUPPCState *env, float16 arg);
void helper_compute_fprf_float32(CPUPPCState *env, float32 arg);
void helper_compute_fprf_float128(CPUPPCState *env, float128 arg);
#endif /* PPC_INTERNAL_H */
