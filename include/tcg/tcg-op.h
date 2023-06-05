/* SPDX-License-Identifier: MIT */
/*
 * Target dependent opcode generation functions.
 *
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TCG_OP_H
#define TCG_TCG_OP_H

#include "tcg/tcg-op-common.h"

#ifndef TARGET_LONG_BITS
#error must include QEMU headers
#endif

#if TARGET_LONG_BITS == 32
# define TCG_TYPE_TL  TCG_TYPE_I32
#elif TARGET_LONG_BITS == 64
# define TCG_TYPE_TL  TCG_TYPE_I64
#else
# error
#endif

#ifndef TARGET_INSN_START_EXTRA_WORDS
static inline void tcg_gen_insn_start(target_ulong pc)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
}
#elif TARGET_INSN_START_EXTRA_WORDS == 1
static inline void tcg_gen_insn_start(target_ulong pc, target_ulong a1)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 2 * 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
    tcg_set_insn_start_param(op, 1, a1);
}
#elif TARGET_INSN_START_EXTRA_WORDS == 2
static inline void tcg_gen_insn_start(target_ulong pc, target_ulong a1,
                                      target_ulong a2)
{
    TCGOp *op = tcg_emit_op(INDEX_op_insn_start, 3 * 64 / TCG_TARGET_REG_BITS);
    tcg_set_insn_start_param(op, 0, pc);
    tcg_set_insn_start_param(op, 1, a1);
    tcg_set_insn_start_param(op, 2, a2);
}
#else
#error Unhandled TARGET_INSN_START_EXTRA_WORDS value
#endif

#if TARGET_LONG_BITS == 32
typedef TCGv_i32 TCGv;
#define tcg_temp_new() tcg_temp_new_i32()
#define tcg_global_mem_new tcg_global_mem_new_i32
#define tcg_temp_free tcg_temp_free_i32
#define tcgv_tl_temp tcgv_i32_temp
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i32
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i32
#elif TARGET_LONG_BITS == 64
typedef TCGv_i64 TCGv;
#define tcg_temp_new() tcg_temp_new_i64()
#define tcg_global_mem_new tcg_global_mem_new_i64
#define tcg_temp_free tcg_temp_free_i64
#define tcgv_tl_temp tcgv_i64_temp
#define tcg_gen_qemu_ld_tl tcg_gen_qemu_ld_i64
#define tcg_gen_qemu_st_tl tcg_gen_qemu_st_i64
#else
#error Unhandled TARGET_LONG_BITS value
#endif

static inline void
tcg_gen_qemu_ld_i32(TCGv_i32 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i32_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i32(TCGv_i32 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i32_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_ld_i64(TCGv_i64 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i64_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i64(TCGv_i64 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i64_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_ld_i128(TCGv_i128 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_ld_i128_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

static inline void
tcg_gen_qemu_st_i128(TCGv_i128 v, TCGv a, TCGArg i, MemOp m)
{
    tcg_gen_qemu_st_i128_chk(v, tcgv_tl_temp(a), i, m, TCG_TYPE_TL);
}

#define DEF_ATOMIC2(N, S)                                               \
    static inline void N##_##S(TCGv_##S r, TCGv a, TCGv_##S v,          \
                               TCGArg i, MemOp m)                       \
    { N##_##S##_chk(r, tcgv_tl_temp(a), v, i, m, TCG_TYPE_TL); }

#define DEF_ATOMIC3(N, S)                                               \
    static inline void N##_##S(TCGv_##S r, TCGv a, TCGv_##S o,          \
                               TCGv_##S n, TCGArg i, MemOp m)           \
    { N##_##S##_chk(r, tcgv_tl_temp(a), o, n, i, m, TCG_TYPE_TL); }

DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i32)
DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i64)
DEF_ATOMIC3(tcg_gen_atomic_cmpxchg, i128)

DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i32)
DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i64)
DEF_ATOMIC3(tcg_gen_nonatomic_cmpxchg, i128)

DEF_ATOMIC2(tcg_gen_atomic_xchg, i32)
DEF_ATOMIC2(tcg_gen_atomic_xchg, i64)

DEF_ATOMIC2(tcg_gen_atomic_fetch_add, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_add, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_and, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_and, i64)
DEF_ATOMIC2(tcg_gen_atomic_fetch_or, i32)
DEF_ATOMIC2(tcg_gen_atomic_fetch_or, i64)
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

#if TARGET_LONG_BITS == 64
#define tcg_gen_movi_tl tcg_gen_movi_i64
#define tcg_gen_mov_tl tcg_gen_mov_i64
#define tcg_gen_ld8u_tl tcg_gen_ld8u_i64
#define tcg_gen_ld8s_tl tcg_gen_ld8s_i64
#define tcg_gen_ld16u_tl tcg_gen_ld16u_i64
#define tcg_gen_ld16s_tl tcg_gen_ld16s_i64
#define tcg_gen_ld32u_tl tcg_gen_ld32u_i64
#define tcg_gen_ld32s_tl tcg_gen_ld32s_i64
#define tcg_gen_ld_tl tcg_gen_ld_i64
#define tcg_gen_st8_tl tcg_gen_st8_i64
#define tcg_gen_st16_tl tcg_gen_st16_i64
#define tcg_gen_st32_tl tcg_gen_st32_i64
#define tcg_gen_st_tl tcg_gen_st_i64
#define tcg_gen_add_tl tcg_gen_add_i64
#define tcg_gen_addi_tl tcg_gen_addi_i64
#define tcg_gen_sub_tl tcg_gen_sub_i64
#define tcg_gen_neg_tl tcg_gen_neg_i64
#define tcg_gen_abs_tl tcg_gen_abs_i64
#define tcg_gen_subfi_tl tcg_gen_subfi_i64
#define tcg_gen_subi_tl tcg_gen_subi_i64
#define tcg_gen_and_tl tcg_gen_and_i64
#define tcg_gen_andi_tl tcg_gen_andi_i64
#define tcg_gen_or_tl tcg_gen_or_i64
#define tcg_gen_ori_tl tcg_gen_ori_i64
#define tcg_gen_xor_tl tcg_gen_xor_i64
#define tcg_gen_xori_tl tcg_gen_xori_i64
#define tcg_gen_not_tl tcg_gen_not_i64
#define tcg_gen_shl_tl tcg_gen_shl_i64
#define tcg_gen_shli_tl tcg_gen_shli_i64
#define tcg_gen_shr_tl tcg_gen_shr_i64
#define tcg_gen_shri_tl tcg_gen_shri_i64
#define tcg_gen_sar_tl tcg_gen_sar_i64
#define tcg_gen_sari_tl tcg_gen_sari_i64
#define tcg_gen_brcond_tl tcg_gen_brcond_i64
#define tcg_gen_brcondi_tl tcg_gen_brcondi_i64
#define tcg_gen_setcond_tl tcg_gen_setcond_i64
#define tcg_gen_setcondi_tl tcg_gen_setcondi_i64
#define tcg_gen_mul_tl tcg_gen_mul_i64
#define tcg_gen_muli_tl tcg_gen_muli_i64
#define tcg_gen_div_tl tcg_gen_div_i64
#define tcg_gen_rem_tl tcg_gen_rem_i64
#define tcg_gen_divu_tl tcg_gen_divu_i64
#define tcg_gen_remu_tl tcg_gen_remu_i64
#define tcg_gen_discard_tl tcg_gen_discard_i64
#define tcg_gen_trunc_tl_i32 tcg_gen_extrl_i64_i32
#define tcg_gen_trunc_i64_tl tcg_gen_mov_i64
#define tcg_gen_extu_i32_tl tcg_gen_extu_i32_i64
#define tcg_gen_ext_i32_tl tcg_gen_ext_i32_i64
#define tcg_gen_extu_tl_i64 tcg_gen_mov_i64
#define tcg_gen_ext_tl_i64 tcg_gen_mov_i64
#define tcg_gen_ext8u_tl tcg_gen_ext8u_i64
#define tcg_gen_ext8s_tl tcg_gen_ext8s_i64
#define tcg_gen_ext16u_tl tcg_gen_ext16u_i64
#define tcg_gen_ext16s_tl tcg_gen_ext16s_i64
#define tcg_gen_ext32u_tl tcg_gen_ext32u_i64
#define tcg_gen_ext32s_tl tcg_gen_ext32s_i64
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i64
#define tcg_gen_bswap32_tl tcg_gen_bswap32_i64
#define tcg_gen_bswap64_tl tcg_gen_bswap64_i64
#define tcg_gen_bswap_tl tcg_gen_bswap64_i64
#define tcg_gen_hswap_tl tcg_gen_hswap_i64
#define tcg_gen_wswap_tl tcg_gen_wswap_i64
#define tcg_gen_concat_tl_i64 tcg_gen_concat32_i64
#define tcg_gen_extr_i64_tl tcg_gen_extr32_i64
#define tcg_gen_andc_tl tcg_gen_andc_i64
#define tcg_gen_eqv_tl tcg_gen_eqv_i64
#define tcg_gen_nand_tl tcg_gen_nand_i64
#define tcg_gen_nor_tl tcg_gen_nor_i64
#define tcg_gen_orc_tl tcg_gen_orc_i64
#define tcg_gen_clz_tl tcg_gen_clz_i64
#define tcg_gen_ctz_tl tcg_gen_ctz_i64
#define tcg_gen_clzi_tl tcg_gen_clzi_i64
#define tcg_gen_ctzi_tl tcg_gen_ctzi_i64
#define tcg_gen_clrsb_tl tcg_gen_clrsb_i64
#define tcg_gen_ctpop_tl tcg_gen_ctpop_i64
#define tcg_gen_rotl_tl tcg_gen_rotl_i64
#define tcg_gen_rotli_tl tcg_gen_rotli_i64
#define tcg_gen_rotr_tl tcg_gen_rotr_i64
#define tcg_gen_rotri_tl tcg_gen_rotri_i64
#define tcg_gen_deposit_tl tcg_gen_deposit_i64
#define tcg_gen_deposit_z_tl tcg_gen_deposit_z_i64
#define tcg_gen_extract_tl tcg_gen_extract_i64
#define tcg_gen_sextract_tl tcg_gen_sextract_i64
#define tcg_gen_extract2_tl tcg_gen_extract2_i64
#define tcg_constant_tl tcg_constant_i64
#define tcg_gen_movcond_tl tcg_gen_movcond_i64
#define tcg_gen_add2_tl tcg_gen_add2_i64
#define tcg_gen_sub2_tl tcg_gen_sub2_i64
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i64
#define tcg_gen_muls2_tl tcg_gen_muls2_i64
#define tcg_gen_mulsu2_tl tcg_gen_mulsu2_i64
#define tcg_gen_smin_tl tcg_gen_smin_i64
#define tcg_gen_umin_tl tcg_gen_umin_i64
#define tcg_gen_smax_tl tcg_gen_smax_i64
#define tcg_gen_umax_tl tcg_gen_umax_i64
#define tcg_gen_atomic_cmpxchg_tl tcg_gen_atomic_cmpxchg_i64
#define tcg_gen_atomic_xchg_tl tcg_gen_atomic_xchg_i64
#define tcg_gen_atomic_fetch_add_tl tcg_gen_atomic_fetch_add_i64
#define tcg_gen_atomic_fetch_and_tl tcg_gen_atomic_fetch_and_i64
#define tcg_gen_atomic_fetch_or_tl tcg_gen_atomic_fetch_or_i64
#define tcg_gen_atomic_fetch_xor_tl tcg_gen_atomic_fetch_xor_i64
#define tcg_gen_atomic_fetch_smin_tl tcg_gen_atomic_fetch_smin_i64
#define tcg_gen_atomic_fetch_umin_tl tcg_gen_atomic_fetch_umin_i64
#define tcg_gen_atomic_fetch_smax_tl tcg_gen_atomic_fetch_smax_i64
#define tcg_gen_atomic_fetch_umax_tl tcg_gen_atomic_fetch_umax_i64
#define tcg_gen_atomic_add_fetch_tl tcg_gen_atomic_add_fetch_i64
#define tcg_gen_atomic_and_fetch_tl tcg_gen_atomic_and_fetch_i64
#define tcg_gen_atomic_or_fetch_tl tcg_gen_atomic_or_fetch_i64
#define tcg_gen_atomic_xor_fetch_tl tcg_gen_atomic_xor_fetch_i64
#define tcg_gen_atomic_smin_fetch_tl tcg_gen_atomic_smin_fetch_i64
#define tcg_gen_atomic_umin_fetch_tl tcg_gen_atomic_umin_fetch_i64
#define tcg_gen_atomic_smax_fetch_tl tcg_gen_atomic_smax_fetch_i64
#define tcg_gen_atomic_umax_fetch_tl tcg_gen_atomic_umax_fetch_i64
#define tcg_gen_dup_tl_vec  tcg_gen_dup_i64_vec
#define tcg_gen_dup_tl tcg_gen_dup_i64
#define dup_const_tl dup_const
#else
#define tcg_gen_movi_tl tcg_gen_movi_i32
#define tcg_gen_mov_tl tcg_gen_mov_i32
#define tcg_gen_ld8u_tl tcg_gen_ld8u_i32
#define tcg_gen_ld8s_tl tcg_gen_ld8s_i32
#define tcg_gen_ld16u_tl tcg_gen_ld16u_i32
#define tcg_gen_ld16s_tl tcg_gen_ld16s_i32
#define tcg_gen_ld32u_tl tcg_gen_ld_i32
#define tcg_gen_ld32s_tl tcg_gen_ld_i32
#define tcg_gen_ld_tl tcg_gen_ld_i32
#define tcg_gen_st8_tl tcg_gen_st8_i32
#define tcg_gen_st16_tl tcg_gen_st16_i32
#define tcg_gen_st32_tl tcg_gen_st_i32
#define tcg_gen_st_tl tcg_gen_st_i32
#define tcg_gen_add_tl tcg_gen_add_i32
#define tcg_gen_addi_tl tcg_gen_addi_i32
#define tcg_gen_sub_tl tcg_gen_sub_i32
#define tcg_gen_neg_tl tcg_gen_neg_i32
#define tcg_gen_abs_tl tcg_gen_abs_i32
#define tcg_gen_subfi_tl tcg_gen_subfi_i32
#define tcg_gen_subi_tl tcg_gen_subi_i32
#define tcg_gen_and_tl tcg_gen_and_i32
#define tcg_gen_andi_tl tcg_gen_andi_i32
#define tcg_gen_or_tl tcg_gen_or_i32
#define tcg_gen_ori_tl tcg_gen_ori_i32
#define tcg_gen_xor_tl tcg_gen_xor_i32
#define tcg_gen_xori_tl tcg_gen_xori_i32
#define tcg_gen_not_tl tcg_gen_not_i32
#define tcg_gen_shl_tl tcg_gen_shl_i32
#define tcg_gen_shli_tl tcg_gen_shli_i32
#define tcg_gen_shr_tl tcg_gen_shr_i32
#define tcg_gen_shri_tl tcg_gen_shri_i32
#define tcg_gen_sar_tl tcg_gen_sar_i32
#define tcg_gen_sari_tl tcg_gen_sari_i32
#define tcg_gen_brcond_tl tcg_gen_brcond_i32
#define tcg_gen_brcondi_tl tcg_gen_brcondi_i32
#define tcg_gen_setcond_tl tcg_gen_setcond_i32
#define tcg_gen_setcondi_tl tcg_gen_setcondi_i32
#define tcg_gen_mul_tl tcg_gen_mul_i32
#define tcg_gen_muli_tl tcg_gen_muli_i32
#define tcg_gen_div_tl tcg_gen_div_i32
#define tcg_gen_rem_tl tcg_gen_rem_i32
#define tcg_gen_divu_tl tcg_gen_divu_i32
#define tcg_gen_remu_tl tcg_gen_remu_i32
#define tcg_gen_discard_tl tcg_gen_discard_i32
#define tcg_gen_trunc_tl_i32 tcg_gen_mov_i32
#define tcg_gen_trunc_i64_tl tcg_gen_extrl_i64_i32
#define tcg_gen_extu_i32_tl tcg_gen_mov_i32
#define tcg_gen_ext_i32_tl tcg_gen_mov_i32
#define tcg_gen_extu_tl_i64 tcg_gen_extu_i32_i64
#define tcg_gen_ext_tl_i64 tcg_gen_ext_i32_i64
#define tcg_gen_ext8u_tl tcg_gen_ext8u_i32
#define tcg_gen_ext8s_tl tcg_gen_ext8s_i32
#define tcg_gen_ext16u_tl tcg_gen_ext16u_i32
#define tcg_gen_ext16s_tl tcg_gen_ext16s_i32
#define tcg_gen_ext32u_tl tcg_gen_mov_i32
#define tcg_gen_ext32s_tl tcg_gen_mov_i32
#define tcg_gen_bswap16_tl tcg_gen_bswap16_i32
#define tcg_gen_bswap32_tl(D, S, F) tcg_gen_bswap32_i32(D, S)
#define tcg_gen_bswap_tl tcg_gen_bswap32_i32
#define tcg_gen_hswap_tl tcg_gen_hswap_i32
#define tcg_gen_concat_tl_i64 tcg_gen_concat_i32_i64
#define tcg_gen_extr_i64_tl tcg_gen_extr_i64_i32
#define tcg_gen_andc_tl tcg_gen_andc_i32
#define tcg_gen_eqv_tl tcg_gen_eqv_i32
#define tcg_gen_nand_tl tcg_gen_nand_i32
#define tcg_gen_nor_tl tcg_gen_nor_i32
#define tcg_gen_orc_tl tcg_gen_orc_i32
#define tcg_gen_clz_tl tcg_gen_clz_i32
#define tcg_gen_ctz_tl tcg_gen_ctz_i32
#define tcg_gen_clzi_tl tcg_gen_clzi_i32
#define tcg_gen_ctzi_tl tcg_gen_ctzi_i32
#define tcg_gen_clrsb_tl tcg_gen_clrsb_i32
#define tcg_gen_ctpop_tl tcg_gen_ctpop_i32
#define tcg_gen_rotl_tl tcg_gen_rotl_i32
#define tcg_gen_rotli_tl tcg_gen_rotli_i32
#define tcg_gen_rotr_tl tcg_gen_rotr_i32
#define tcg_gen_rotri_tl tcg_gen_rotri_i32
#define tcg_gen_deposit_tl tcg_gen_deposit_i32
#define tcg_gen_deposit_z_tl tcg_gen_deposit_z_i32
#define tcg_gen_extract_tl tcg_gen_extract_i32
#define tcg_gen_sextract_tl tcg_gen_sextract_i32
#define tcg_gen_extract2_tl tcg_gen_extract2_i32
#define tcg_constant_tl tcg_constant_i32
#define tcg_gen_movcond_tl tcg_gen_movcond_i32
#define tcg_gen_add2_tl tcg_gen_add2_i32
#define tcg_gen_sub2_tl tcg_gen_sub2_i32
#define tcg_gen_mulu2_tl tcg_gen_mulu2_i32
#define tcg_gen_muls2_tl tcg_gen_muls2_i32
#define tcg_gen_mulsu2_tl tcg_gen_mulsu2_i32
#define tcg_gen_smin_tl tcg_gen_smin_i32
#define tcg_gen_umin_tl tcg_gen_umin_i32
#define tcg_gen_smax_tl tcg_gen_smax_i32
#define tcg_gen_umax_tl tcg_gen_umax_i32
#define tcg_gen_atomic_cmpxchg_tl tcg_gen_atomic_cmpxchg_i32
#define tcg_gen_atomic_xchg_tl tcg_gen_atomic_xchg_i32
#define tcg_gen_atomic_fetch_add_tl tcg_gen_atomic_fetch_add_i32
#define tcg_gen_atomic_fetch_and_tl tcg_gen_atomic_fetch_and_i32
#define tcg_gen_atomic_fetch_or_tl tcg_gen_atomic_fetch_or_i32
#define tcg_gen_atomic_fetch_xor_tl tcg_gen_atomic_fetch_xor_i32
#define tcg_gen_atomic_fetch_smin_tl tcg_gen_atomic_fetch_smin_i32
#define tcg_gen_atomic_fetch_umin_tl tcg_gen_atomic_fetch_umin_i32
#define tcg_gen_atomic_fetch_smax_tl tcg_gen_atomic_fetch_smax_i32
#define tcg_gen_atomic_fetch_umax_tl tcg_gen_atomic_fetch_umax_i32
#define tcg_gen_atomic_add_fetch_tl tcg_gen_atomic_add_fetch_i32
#define tcg_gen_atomic_and_fetch_tl tcg_gen_atomic_and_fetch_i32
#define tcg_gen_atomic_or_fetch_tl tcg_gen_atomic_or_fetch_i32
#define tcg_gen_atomic_xor_fetch_tl tcg_gen_atomic_xor_fetch_i32
#define tcg_gen_atomic_smin_fetch_tl tcg_gen_atomic_smin_fetch_i32
#define tcg_gen_atomic_umin_fetch_tl tcg_gen_atomic_umin_fetch_i32
#define tcg_gen_atomic_smax_fetch_tl tcg_gen_atomic_smax_fetch_i32
#define tcg_gen_atomic_umax_fetch_tl tcg_gen_atomic_umax_fetch_i32
#define tcg_gen_dup_tl_vec  tcg_gen_dup_i32_vec
#define tcg_gen_dup_tl tcg_gen_dup_i32

#define dup_const_tl(VECE, C)                                      \
    (__builtin_constant_p(VECE)                                    \
     ? (  (VECE) == MO_8  ? 0x01010101ul * (uint8_t)(C)            \
        : (VECE) == MO_16 ? 0x00010001ul * (uint16_t)(C)           \
        : (VECE) == MO_32 ? 0x00000001ul * (uint32_t)(C)           \
        : (qemu_build_not_reached_always(), 0))                    \
     :  (target_long)dup_const(VECE, C))

#endif /* TARGET_LONG_BITS == 64 */
#endif /* TCG_TCG_OP_H */
