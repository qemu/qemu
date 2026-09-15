/* SPDX-License-Identifier: MIT */
/*
 * Target dependent memory related functions.
 *
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TCG_OP_MEM_H
#define TCG_TCG_OP_MEM_H

#ifndef TCG_ADDRESS_BITS
#error TCG_ADDRESS_BITS must be defined
#endif

#include "tcg/tcg-op-common.h"

#if TCG_ADDRESS_BITS == 32
typedef TCGv_i32 TCGv_va;
#define TCG_TYPE_VA TCG_TYPE_I32
#define tcgv_va_temp tcgv_i32_temp
#define tcgv_va_temp_new tcg_temp_new_i32
#elif TCG_ADDRESS_BITS == 64
typedef TCGv_i64 TCGv_va;
#define TCG_TYPE_VA TCG_TYPE_I64
#define tcgv_va_temp tcgv_i64_temp
#define tcgv_va_temp_new tcg_temp_new_i64
#else
#error
#endif

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_ld_i32(TCGv_i32 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i32_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_st_i32(TCGv_i32 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i32_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_ld_i64(TCGv_i64 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i64_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_st_i64(TCGv_i64 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i64_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_ld_i128(TCGv_i128 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i128_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

QEMU_ARG_NONNULL static inline void
tcg_gen_qemu_st_i128(TCGv_i128 v, TCGv_va a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i128_chk(v, tcgv_va_temp(a), i, m, TCG_TYPE_VA);
}

#define DEF_ATOMIC2(N, S)                                               \
    QEMU_ARG_NONNULL static inline void                                 \
    N##_##S(TCGv_##S r, TCGv_va a, TCGv_##S v, TCGArg i, MemOp m)       \
    { N##_##S##_chk(r, tcgv_va_temp(a), v, i, m, TCG_TYPE_VA); }

#define DEF_ATOMIC3(N, S)                                               \
    QEMU_ARG_NONNULL static inline void                                 \
    N##_##S(TCGv_##S r, TCGv_va a, TCGv_##S o, TCGv_##S n, TCGArg i, MemOp m) \
    { N##_##S##_chk(r, tcgv_va_temp(a), o, n, i, m, TCG_TYPE_VA); }

DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i32)
DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i64)
DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i128)

DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i32)
DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i64)
DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i128)

DEF_ATOMIC2(tcg_gen_atomic_xchg, i32)
DEF_ATOMIC2(tcg_gen_atomic_xchg, i64)
DEF_ATOMIC2(tcg_gen_atomic_xchg, i128)

DEF_ATOMIC2(tcg_gen_atomic_fetch_add, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_add, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_and, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_and, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_and, i128)
DEF_ATOMIC2(tcg_gen_atomic_fetch_or, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_or, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_or, i128)
DEF_ATOMIC2(tcg_gen_atomic_fetch_xor, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_xor, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_smin, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_smin, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_umin, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_umin, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_smax, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_smax, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_umax, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_umax, i64)

DEF_ATOMIC2(tcg_gen_atomic_add_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_add_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_and_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_and_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_or_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_or_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_xor_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_xor_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_smin_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_smin_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_umin_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_umin_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_smax_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_smax_fetch, i64)
DEF_ATOMIC2(tcg_gen_atomic_umax_fetch, i32)
DEF_ATOMIC2(tcg_gen_atomic_umax_fetch, i64)

#undef DEF_ATOMIC2
#undef DEF_ATOMIC3

#endif /* TCG_TCG_OP_MEM_H */
