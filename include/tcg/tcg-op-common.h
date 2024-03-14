/* SPDX-License-Identifier: MIT */
/*
 * Target independent opcode generation functions.
 *
 * Copyright (c) 2008 Fabrice Bellard
 */

#ifndef TCG_TCG_OP_COMMON_H
#define TCG_TCG_OP_COMMON_H

#include "tcg/tcg.h"
#include "exec/helper-proto-common.h"
#include "exec/helper-gen-common.h"

TCGv_i32 tcg_constant_i32(int32_t val);
TCGv_i64 tcg_constant_i64(int64_t val);
TCGv_vec tcg_constant_vec(TCGType type, unsigned vece, int64_t val);
TCGv_vec tcg_constant_vec_matching(TCGv_vec match, unsigned vece, int64_t val);

TCGv_i32 tcg_temp_new_i32(void);
TCGv_i64 tcg_temp_new_i64(void);
TCGv_ptr tcg_temp_new_ptr(void);
TCGv_i128 tcg_temp_new_i128(void);
TCGv_vec tcg_temp_new_vec(TCGType type);
TCGv_vec tcg_temp_new_vec_matching(TCGv_vec match);

TCGv_i32 tcg_global_mem_new_i32(TCGv_ptr reg, intptr_t off, const char *name);
TCGv_i64 tcg_global_mem_new_i64(TCGv_ptr reg, intptr_t off, const char *name);
TCGv_ptr tcg_global_mem_new_ptr(TCGv_ptr reg, intptr_t off, const char *name);

/* Generic ops.  */

void gen_set_label(TCGLabel *l);
void tcg_gen_br(TCGLabel *l);
void tcg_gen_mb(TCGBar);

/**
 * tcg_gen_exit_tb() - output exit_tb TCG operation
 * @tb: The TranslationBlock from which we are exiting
 * @idx: Direct jump slot index, or exit request
 *
 * See tcg/README for more info about this TCG operation.
 * See also tcg.h and the block comment above TB_EXIT_MASK.
 *
 * For a normal exit from the TB, back to the main loop, @tb should
 * be NULL and @idx should be 0.  Otherwise, @tb should be valid and
 * @idx should be one of the TB_EXIT_ values.
 */
void tcg_gen_exit_tb(const TranslationBlock *tb, unsigned idx);

/**
 * tcg_gen_goto_tb() - output goto_tb TCG operation
 * @idx: Direct jump slot index (0 or 1)
 *
 * See tcg/README for more info about this TCG operation.
 *
 * NOTE: In system emulation, direct jumps with goto_tb are only safe within
 * the pages this TB resides in because we don't take care of direct jumps when
 * address mapping changes, e.g. in tlb_flush(). In user mode, there's only a
 * static address translation, so the destination address is always valid, TBs
 * are always invalidated properly, and direct jumps are reset when mapping
 * changes.
 */
void tcg_gen_goto_tb(unsigned idx);

/**
 * tcg_gen_lookup_and_goto_ptr() - look up the current TB, jump to it if valid
 * @addr: Guest address of the target TB
 *
 * If the TB is not valid, jump to the epilogue.
 *
 * This operation is optional. If the TCG backend does not implement goto_ptr,
 * this op is equivalent to calling tcg_gen_exit_tb() with 0 as the argument.
 */
void tcg_gen_lookup_and_goto_ptr(void);

void tcg_gen_plugin_cb(unsigned from);
void tcg_gen_plugin_mem_cb(TCGv_i64 addr, unsigned meminfo);
void tcg_gen_plugin_cb_start(unsigned from, unsigned type, unsigned wr);
void tcg_gen_plugin_cb_end(void);

/* 32 bit ops */

void tcg_gen_movi_i32(TCGv_i32 ret, int32_t arg);
void tcg_gen_addi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_subfi_i32(TCGv_i32 ret, int32_t arg1, TCGv_i32 arg2);
void tcg_gen_subi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_andi_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_ori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_xori_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_shli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_shri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_sari_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_muli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_div_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rem_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_divu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_remu_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_andc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_eqv_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_nand_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_nor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_orc_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_clz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_ctz_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_clzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2);
void tcg_gen_ctzi_i32(TCGv_i32 ret, TCGv_i32 arg1, uint32_t arg2);
void tcg_gen_clrsb_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ctpop_i32(TCGv_i32 a1, TCGv_i32 a2);
void tcg_gen_rotl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rotli_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_rotr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_rotri_i32(TCGv_i32 ret, TCGv_i32 arg1, int32_t arg2);
void tcg_gen_deposit_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2,
                         unsigned int ofs, unsigned int len);
void tcg_gen_deposit_z_i32(TCGv_i32 ret, TCGv_i32 arg,
                           unsigned int ofs, unsigned int len);
void tcg_gen_extract_i32(TCGv_i32 ret, TCGv_i32 arg,
                         unsigned int ofs, unsigned int len);
void tcg_gen_sextract_i32(TCGv_i32 ret, TCGv_i32 arg,
                          unsigned int ofs, unsigned int len);
void tcg_gen_extract2_i32(TCGv_i32 ret, TCGv_i32 al, TCGv_i32 ah,
                          unsigned int ofs);
void tcg_gen_brcond_i32(TCGCond cond, TCGv_i32 arg1, TCGv_i32 arg2, TCGLabel *);
void tcg_gen_brcondi_i32(TCGCond cond, TCGv_i32 arg1, int32_t arg2, TCGLabel *);
void tcg_gen_setcond_i32(TCGCond cond, TCGv_i32 ret,
                         TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_setcondi_i32(TCGCond cond, TCGv_i32 ret,
                          TCGv_i32 arg1, int32_t arg2);
void tcg_gen_negsetcond_i32(TCGCond cond, TCGv_i32 ret,
                            TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_negsetcondi_i32(TCGCond cond, TCGv_i32 ret,
                             TCGv_i32 arg1, int32_t arg2);
void tcg_gen_movcond_i32(TCGCond cond, TCGv_i32 ret, TCGv_i32 c1,
                         TCGv_i32 c2, TCGv_i32 v1, TCGv_i32 v2);
void tcg_gen_add2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh);
void tcg_gen_sub2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 al,
                      TCGv_i32 ah, TCGv_i32 bl, TCGv_i32 bh);
void tcg_gen_mulu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_muls2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_mulsu2_i32(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_ext8s_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext16s_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext8u_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext16u_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_ext_i32(TCGv_i32 ret, TCGv_i32 val, MemOp opc);
void tcg_gen_bswap16_i32(TCGv_i32 ret, TCGv_i32 arg, int flags);
void tcg_gen_bswap32_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_hswap_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_smin_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_smax_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_umin_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_umax_i32(TCGv_i32, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_abs_i32(TCGv_i32, TCGv_i32);

/* Replicate a value of size @vece from @in to all the lanes in @out */
void tcg_gen_dup_i32(unsigned vece, TCGv_i32 out, TCGv_i32 in);

void tcg_gen_discard_i32(TCGv_i32 arg);
void tcg_gen_mov_i32(TCGv_i32 ret, TCGv_i32 arg);

void tcg_gen_ld8u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld8s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16u_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16s_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld_i32(TCGv_i32 ret, TCGv_ptr arg2, tcg_target_long offset);

void tcg_gen_st8_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st16_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st_i32(TCGv_i32 arg1, TCGv_ptr arg2, tcg_target_long offset);

void tcg_gen_add_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_sub_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_and_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_or_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_xor_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_shl_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_shr_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_sar_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_mul_i32(TCGv_i32 ret, TCGv_i32 arg1, TCGv_i32 arg2);
void tcg_gen_neg_i32(TCGv_i32 ret, TCGv_i32 arg);
void tcg_gen_not_i32(TCGv_i32 ret, TCGv_i32 arg);

/* 64 bit ops */

void tcg_gen_movi_i64(TCGv_i64 ret, int64_t arg);
void tcg_gen_addi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_subfi_i64(TCGv_i64 ret, int64_t arg1, TCGv_i64 arg2);
void tcg_gen_subi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_andi_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_ori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_xori_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_shli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_shri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_sari_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_muli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_div_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rem_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_divu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_remu_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_andc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_eqv_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_nand_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_nor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_orc_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_clz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_ctz_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_clzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2);
void tcg_gen_ctzi_i64(TCGv_i64 ret, TCGv_i64 arg1, uint64_t arg2);
void tcg_gen_clrsb_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ctpop_i64(TCGv_i64 a1, TCGv_i64 a2);
void tcg_gen_rotl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rotli_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_rotr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_rotri_i64(TCGv_i64 ret, TCGv_i64 arg1, int64_t arg2);
void tcg_gen_deposit_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2,
                         unsigned int ofs, unsigned int len);
void tcg_gen_deposit_z_i64(TCGv_i64 ret, TCGv_i64 arg,
                           unsigned int ofs, unsigned int len);
void tcg_gen_extract_i64(TCGv_i64 ret, TCGv_i64 arg,
                         unsigned int ofs, unsigned int len);
void tcg_gen_sextract_i64(TCGv_i64 ret, TCGv_i64 arg,
                          unsigned int ofs, unsigned int len);
void tcg_gen_extract2_i64(TCGv_i64 ret, TCGv_i64 al, TCGv_i64 ah,
                          unsigned int ofs);
void tcg_gen_brcond_i64(TCGCond cond, TCGv_i64 arg1, TCGv_i64 arg2, TCGLabel *);
void tcg_gen_brcondi_i64(TCGCond cond, TCGv_i64 arg1, int64_t arg2, TCGLabel *);
void tcg_gen_setcond_i64(TCGCond cond, TCGv_i64 ret,
                         TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_setcondi_i64(TCGCond cond, TCGv_i64 ret,
                          TCGv_i64 arg1, int64_t arg2);
void tcg_gen_negsetcond_i64(TCGCond cond, TCGv_i64 ret,
                            TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_negsetcondi_i64(TCGCond cond, TCGv_i64 ret,
                             TCGv_i64 arg1, int64_t arg2);
void tcg_gen_movcond_i64(TCGCond cond, TCGv_i64 ret, TCGv_i64 c1,
                         TCGv_i64 c2, TCGv_i64 v1, TCGv_i64 v2);
void tcg_gen_add2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh);
void tcg_gen_sub2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 al,
                      TCGv_i64 ah, TCGv_i64 bl, TCGv_i64 bh);
void tcg_gen_mulu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_muls2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_mulsu2_i64(TCGv_i64 rl, TCGv_i64 rh, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_not_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext8s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext16s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext32s_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext8u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext16u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext32u_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ext_i64(TCGv_i64 ret, TCGv_i64 val, MemOp opc);
void tcg_gen_bswap16_i64(TCGv_i64 ret, TCGv_i64 arg, int flags);
void tcg_gen_bswap32_i64(TCGv_i64 ret, TCGv_i64 arg, int flags);
void tcg_gen_bswap64_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_hswap_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_wswap_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_smin_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_smax_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_umin_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_umax_i64(TCGv_i64, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_abs_i64(TCGv_i64, TCGv_i64);

/* Replicate a value of size @vece from @in to all the lanes in @out */
void tcg_gen_dup_i64(unsigned vece, TCGv_i64 out, TCGv_i64 in);

void tcg_gen_st8_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st16_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st32_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);

void tcg_gen_add_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_sub_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);

void tcg_gen_discard_i64(TCGv_i64 arg);
void tcg_gen_mov_i64(TCGv_i64 ret, TCGv_i64 arg);
void tcg_gen_ld8u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld8s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld16s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld32u_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld32s_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_ld_i64(TCGv_i64 ret, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_st_i64(TCGv_i64 arg1, TCGv_ptr arg2, tcg_target_long offset);
void tcg_gen_and_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_or_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_xor_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_shl_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_shr_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_sar_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_mul_i64(TCGv_i64 ret, TCGv_i64 arg1, TCGv_i64 arg2);
void tcg_gen_neg_i64(TCGv_i64 ret, TCGv_i64 arg);


/* Size changing operations.  */

void tcg_gen_extu_i32_i64(TCGv_i64 ret, TCGv_i32 arg);
void tcg_gen_ext_i32_i64(TCGv_i64 ret, TCGv_i32 arg);
void tcg_gen_concat_i32_i64(TCGv_i64 dest, TCGv_i32 low, TCGv_i32 high);
void tcg_gen_extrl_i64_i32(TCGv_i32 ret, TCGv_i64 arg);
void tcg_gen_extrh_i64_i32(TCGv_i32 ret, TCGv_i64 arg);
void tcg_gen_extr_i64_i32(TCGv_i32 lo, TCGv_i32 hi, TCGv_i64 arg);
void tcg_gen_extr32_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i64 arg);
void tcg_gen_concat32_i64(TCGv_i64 ret, TCGv_i64 lo, TCGv_i64 hi);

void tcg_gen_extr_i128_i64(TCGv_i64 lo, TCGv_i64 hi, TCGv_i128 arg);
void tcg_gen_concat_i64_i128(TCGv_i128 ret, TCGv_i64 lo, TCGv_i64 hi);

/* 128 bit ops */

void tcg_gen_mov_i128(TCGv_i128 dst, TCGv_i128 src);
void tcg_gen_ld_i128(TCGv_i128 ret, TCGv_ptr base, tcg_target_long offset);
void tcg_gen_st_i128(TCGv_i128 val, TCGv_ptr base, tcg_target_long offset);

/* Local load/store bit ops */

void tcg_gen_qemu_ld_i32_chk(TCGv_i32, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i32_chk(TCGv_i32, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_ld_i64_chk(TCGv_i64, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i64_chk(TCGv_i64, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_ld_i128_chk(TCGv_i128, TCGTemp *, TCGArg, MemOp, TCGType);
void tcg_gen_qemu_st_i128_chk(TCGv_i128, TCGTemp *, TCGArg, MemOp, TCGType);

/* Atomic ops */

void tcg_gen_atomic_cmpxchg_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32, TCGv_i32,
                                    TCGArg, MemOp, TCGType);
void tcg_gen_atomic_cmpxchg_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64, TCGv_i64,
                                    TCGArg, MemOp, TCGType);
void tcg_gen_atomic_cmpxchg_i128_chk(TCGv_i128, TCGTemp *, TCGv_i128,
                                     TCGv_i128, TCGArg, MemOp, TCGType);

void tcg_gen_nonatomic_cmpxchg_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_nonatomic_cmpxchg_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_nonatomic_cmpxchg_i128_chk(TCGv_i128, TCGTemp *, TCGv_i128,
                                        TCGv_i128, TCGArg, MemOp, TCGType);

void tcg_gen_atomic_xchg_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                 TCGArg, MemOp, TCGType);
void tcg_gen_atomic_xchg_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                 TCGArg, MemOp, TCGType);

void tcg_gen_atomic_fetch_add_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_add_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_and_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_and_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_or_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                     TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_or_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                     TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_xor_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_xor_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_smin_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_smin_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_umin_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_umin_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_smax_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_smax_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_umax_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_fetch_umax_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);

void tcg_gen_atomic_add_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_add_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_and_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_and_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_or_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                     TCGArg, MemOp, TCGType);
void tcg_gen_atomic_or_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                     TCGArg, MemOp, TCGType);
void tcg_gen_atomic_xor_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_xor_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                      TCGArg, MemOp, TCGType);
void tcg_gen_atomic_smin_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_smin_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_umin_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_umin_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_smax_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_smax_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_umax_fetch_i32_chk(TCGv_i32, TCGTemp *, TCGv_i32,
                                       TCGArg, MemOp, TCGType);
void tcg_gen_atomic_umax_fetch_i64_chk(TCGv_i64, TCGTemp *, TCGv_i64,
                                       TCGArg, MemOp, TCGType);

/* Vector ops */

void tcg_gen_mov_vec(TCGv_vec, TCGv_vec);
void tcg_gen_dup_i32_vec(unsigned vece, TCGv_vec, TCGv_i32);
void tcg_gen_dup_i64_vec(unsigned vece, TCGv_vec, TCGv_i64);
void tcg_gen_dup_mem_vec(unsigned vece, TCGv_vec, TCGv_ptr, tcg_target_long);
void tcg_gen_dupi_vec(unsigned vece, TCGv_vec, uint64_t);
void tcg_gen_add_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_sub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_mul_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_and_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_or_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_xor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_andc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_orc_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_nand_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_nor_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_eqv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_not_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_neg_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_abs_vec(unsigned vece, TCGv_vec r, TCGv_vec a);
void tcg_gen_ssadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_usadd_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_sssub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_ussub_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_smin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_umin_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_smax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);
void tcg_gen_umax_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec b);

void tcg_gen_shli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_shri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_sari_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_rotli_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);
void tcg_gen_rotri_vec(unsigned vece, TCGv_vec r, TCGv_vec a, int64_t i);

void tcg_gen_shls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_shrs_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_sars_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);
void tcg_gen_rotls_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_i32 s);

void tcg_gen_shlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_shrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_sarv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_rotlv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);
void tcg_gen_rotrv_vec(unsigned vece, TCGv_vec r, TCGv_vec a, TCGv_vec s);

void tcg_gen_cmp_vec(TCGCond cond, unsigned vece, TCGv_vec r,
                     TCGv_vec a, TCGv_vec b);

void tcg_gen_bitsel_vec(unsigned vece, TCGv_vec r, TCGv_vec a,
                        TCGv_vec b, TCGv_vec c);
void tcg_gen_cmpsel_vec(TCGCond cond, unsigned vece, TCGv_vec r,
                        TCGv_vec a, TCGv_vec b, TCGv_vec c, TCGv_vec d);

void tcg_gen_ld_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset);
void tcg_gen_st_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset);
void tcg_gen_stl_vec(TCGv_vec r, TCGv_ptr base, TCGArg offset, TCGType t);

/* Host pointer ops */

#if UINTPTR_MAX == UINT32_MAX
# define PTR  i32
# define NAT  TCGv_i32
#else
# define PTR  i64
# define NAT  TCGv_i64
#endif

TCGv_ptr tcg_constant_ptr_int(intptr_t x);
#define tcg_constant_ptr(X)  tcg_constant_ptr_int((intptr_t)(X))

static inline void tcg_gen_ld_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    glue(tcg_gen_ld_,PTR)((NAT)r, a, o);
}

static inline void tcg_gen_st_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t o)
{
    glue(tcg_gen_st_, PTR)((NAT)r, a, o);
}

static inline void tcg_gen_discard_ptr(TCGv_ptr a)
{
    glue(tcg_gen_discard_,PTR)((NAT)a);
}

static inline void tcg_gen_add_ptr(TCGv_ptr r, TCGv_ptr a, TCGv_ptr b)
{
    glue(tcg_gen_add_,PTR)((NAT)r, (NAT)a, (NAT)b);
}

static inline void tcg_gen_addi_ptr(TCGv_ptr r, TCGv_ptr a, intptr_t b)
{
    glue(tcg_gen_addi_,PTR)((NAT)r, (NAT)a, b);
}

static inline void tcg_gen_mov_ptr(TCGv_ptr d, TCGv_ptr s)
{
    glue(tcg_gen_mov_,PTR)((NAT)d, (NAT)s);
}

static inline void tcg_gen_movi_ptr(TCGv_ptr d, intptr_t s)
{
    glue(tcg_gen_movi_,PTR)((NAT)d, s);
}

static inline void tcg_gen_brcondi_ptr(TCGCond cond, TCGv_ptr a,
                                       intptr_t b, TCGLabel *label)
{
    glue(tcg_gen_brcondi_,PTR)(cond, (NAT)a, b, label);
}

static inline void tcg_gen_ext_i32_ptr(TCGv_ptr r, TCGv_i32 a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_mov_i32((NAT)r, a);
#else
    tcg_gen_ext_i32_i64((NAT)r, a);
#endif
}

static inline void tcg_gen_trunc_i64_ptr(TCGv_ptr r, TCGv_i64 a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_extrl_i64_i32((NAT)r, a);
#else
    tcg_gen_mov_i64((NAT)r, a);
#endif
}

static inline void tcg_gen_extu_ptr_i64(TCGv_i64 r, TCGv_ptr a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_extu_i32_i64(r, (NAT)a);
#else
    tcg_gen_mov_i64(r, (NAT)a);
#endif
}

static inline void tcg_gen_trunc_ptr_i32(TCGv_i32 r, TCGv_ptr a)
{
#if UINTPTR_MAX == UINT32_MAX
    tcg_gen_mov_i32(r, (NAT)a);
#else
    tcg_gen_extrl_i64_i32(r, (NAT)a);
#endif
}

#undef PTR
#undef NAT

#endif /* TCG_TCG_OP_COMMON_H */
