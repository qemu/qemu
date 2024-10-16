/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef I386_TCG_TARGET_H
#define I386_TCG_TARGET_H

#include "host/cpuinfo.h"

#define TCG_TARGET_INSN_UNIT_SIZE  1

#ifdef __x86_64__
# define TCG_TARGET_NB_REGS   32
# define MAX_CODE_GEN_BUFFER_SIZE  (2 * GiB)
#else
# define TCG_TARGET_NB_REGS   24
# define MAX_CODE_GEN_BUFFER_SIZE  UINT32_MAX
#endif

typedef enum {
    TCG_REG_EAX = 0,
    TCG_REG_ECX,
    TCG_REG_EDX,
    TCG_REG_EBX,
    TCG_REG_ESP,
    TCG_REG_EBP,
    TCG_REG_ESI,
    TCG_REG_EDI,

    /* 64-bit registers; always define the symbols to avoid
       too much if-deffing.  */
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,

    TCG_REG_XMM0,
    TCG_REG_XMM1,
    TCG_REG_XMM2,
    TCG_REG_XMM3,
    TCG_REG_XMM4,
    TCG_REG_XMM5,
    TCG_REG_XMM6,
    TCG_REG_XMM7,

    /* 64-bit registers; likewise always define.  */
    TCG_REG_XMM8,
    TCG_REG_XMM9,
    TCG_REG_XMM10,
    TCG_REG_XMM11,
    TCG_REG_XMM12,
    TCG_REG_XMM13,
    TCG_REG_XMM14,
    TCG_REG_XMM15,

    TCG_REG_RAX = TCG_REG_EAX,
    TCG_REG_RCX = TCG_REG_ECX,
    TCG_REG_RDX = TCG_REG_EDX,
    TCG_REG_RBX = TCG_REG_EBX,
    TCG_REG_RSP = TCG_REG_ESP,
    TCG_REG_RBP = TCG_REG_EBP,
    TCG_REG_RSI = TCG_REG_ESI,
    TCG_REG_RDI = TCG_REG_EDI,

    TCG_AREG0 = TCG_REG_EBP,
    TCG_REG_CALL_STACK = TCG_REG_ESP
} TCGReg;

/* used for function call generation */
#define TCG_TARGET_STACK_ALIGN 16
#if defined(_WIN64)
#define TCG_TARGET_CALL_STACK_OFFSET 32
#else
#define TCG_TARGET_CALL_STACK_OFFSET 0
#endif
#define TCG_TARGET_CALL_ARG_I32      TCG_CALL_ARG_NORMAL
#define TCG_TARGET_CALL_ARG_I64      TCG_CALL_ARG_NORMAL
#if defined(_WIN64)
# define TCG_TARGET_CALL_ARG_I128    TCG_CALL_ARG_BY_REF
# define TCG_TARGET_CALL_RET_I128    TCG_CALL_RET_BY_VEC
#elif TCG_TARGET_REG_BITS == 64
# define TCG_TARGET_CALL_ARG_I128    TCG_CALL_ARG_NORMAL
# define TCG_TARGET_CALL_RET_I128    TCG_CALL_RET_NORMAL
#else
# define TCG_TARGET_CALL_ARG_I128    TCG_CALL_ARG_NORMAL
# define TCG_TARGET_CALL_RET_I128    TCG_CALL_RET_BY_REF
#endif

#define have_bmi1         (cpuinfo & CPUINFO_BMI1)
#define have_popcnt       (cpuinfo & CPUINFO_POPCNT)
#define have_avx1         (cpuinfo & CPUINFO_AVX1)
#define have_avx2         (cpuinfo & CPUINFO_AVX2)
#define have_movbe        (cpuinfo & CPUINFO_MOVBE)

/*
 * There are interesting instructions in AVX512, so long as we have AVX512VL,
 * which indicates support for EVEX on sizes smaller than 512 bits.
 */
#define have_avx512vl     ((cpuinfo & CPUINFO_AVX512VL) && \
                           (cpuinfo & CPUINFO_AVX512F))
#define have_avx512bw     ((cpuinfo & CPUINFO_AVX512BW) && have_avx512vl)
#define have_avx512dq     ((cpuinfo & CPUINFO_AVX512DQ) && have_avx512vl)
#define have_avx512vbmi2  ((cpuinfo & CPUINFO_AVX512VBMI2) && have_avx512vl)

/* optional instructions */
#define TCG_TARGET_HAS_div2_i32         1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_andc_i32         have_bmi1
#define TCG_TARGET_HAS_orc_i32          0
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_clz_i32          1
#define TCG_TARGET_HAS_ctz_i32          1
#define TCG_TARGET_HAS_ctpop_i32        have_popcnt
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_extract_i32      1
#define TCG_TARGET_HAS_sextract_i32     1
#define TCG_TARGET_HAS_extract2_i32     1
#define TCG_TARGET_HAS_negsetcond_i32   1
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_mulu2_i32        1
#define TCG_TARGET_HAS_muls2_i32        1
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_mulsh_i32        0

#if TCG_TARGET_REG_BITS == 64
/* Keep 32-bit values zero-extended in a register.  */
#define TCG_TARGET_HAS_extr_i64_i32     1
#define TCG_TARGET_HAS_div2_i64         1
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_andc_i64         have_bmi1
#define TCG_TARGET_HAS_orc_i64          0
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_clz_i64          1
#define TCG_TARGET_HAS_ctz_i64          1
#define TCG_TARGET_HAS_ctpop_i64        have_popcnt
#define TCG_TARGET_HAS_deposit_i64      1
#define TCG_TARGET_HAS_extract_i64      1
#define TCG_TARGET_HAS_sextract_i64     0
#define TCG_TARGET_HAS_extract2_i64     1
#define TCG_TARGET_HAS_negsetcond_i64   1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#define TCG_TARGET_HAS_mulu2_i64        1
#define TCG_TARGET_HAS_muls2_i64        1
#define TCG_TARGET_HAS_muluh_i64        0
#define TCG_TARGET_HAS_mulsh_i64        0
#define TCG_TARGET_HAS_qemu_st8_i32     0
#else
#define TCG_TARGET_HAS_qemu_st8_i32     1
#endif

#define TCG_TARGET_HAS_qemu_ldst_i128 \
    (TCG_TARGET_REG_BITS == 64 && (cpuinfo & CPUINFO_ATOMIC_VMOVDQA))

#define TCG_TARGET_HAS_tst              1

/* We do not support older SSE systems, only beginning with AVX1.  */
#define TCG_TARGET_HAS_v64              have_avx1
#define TCG_TARGET_HAS_v128             have_avx1
#define TCG_TARGET_HAS_v256             have_avx2

#define TCG_TARGET_HAS_andc_vec         1
#define TCG_TARGET_HAS_orc_vec          have_avx512vl
#define TCG_TARGET_HAS_nand_vec         have_avx512vl
#define TCG_TARGET_HAS_nor_vec          have_avx512vl
#define TCG_TARGET_HAS_eqv_vec          have_avx512vl
#define TCG_TARGET_HAS_not_vec          have_avx512vl
#define TCG_TARGET_HAS_neg_vec          0
#define TCG_TARGET_HAS_abs_vec          1
#define TCG_TARGET_HAS_roti_vec         have_avx512vl
#define TCG_TARGET_HAS_rots_vec         0
#define TCG_TARGET_HAS_rotv_vec         have_avx512vl
#define TCG_TARGET_HAS_shi_vec          1
#define TCG_TARGET_HAS_shs_vec          1
#define TCG_TARGET_HAS_shv_vec          have_avx2
#define TCG_TARGET_HAS_mul_vec          1
#define TCG_TARGET_HAS_sat_vec          1
#define TCG_TARGET_HAS_minmax_vec       1
#define TCG_TARGET_HAS_bitsel_vec       have_avx512vl
#define TCG_TARGET_HAS_cmpsel_vec       1
#define TCG_TARGET_HAS_tst_vec          have_avx512bw

#define TCG_TARGET_deposit_i32_valid(ofs, len) \
    (((ofs) == 0 && ((len) == 8 || (len) == 16)) || \
     (TCG_TARGET_REG_BITS == 32 && (ofs) == 8 && (len) == 8))
#define TCG_TARGET_deposit_i64_valid    TCG_TARGET_deposit_i32_valid

/* Check for the possibility of high-byte extraction and, for 64-bit,
   zero-extending 32-bit right-shift.  */
#define TCG_TARGET_extract_i32_valid(ofs, len) ((ofs) == 8 && (len) == 8)
#define TCG_TARGET_extract_i64_valid(ofs, len) \
    (((ofs) == 8 && (len) == 8) || ((ofs) + (len)) == 32)

/* This defines the natural memory order supported by this
 * architecture before guarantees made by various barrier
 * instructions.
 *
 * The x86 has a pretty strong memory ordering which only really
 * allows for some stores to be re-ordered after loads.
 */
#include "tcg/tcg-mo.h"

#define TCG_TARGET_DEFAULT_MO (TCG_MO_ALL & ~TCG_MO_ST_LD)
#define TCG_TARGET_NEED_LDST_LABELS
#define TCG_TARGET_NEED_POOL_LABELS

#endif
