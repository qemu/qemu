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

/*
 * DEF(name, oargs, iargs, cargs, flags)
 */

/* predefined ops */
DEF(discard, 1, 0, 0, TCG_OPF_NOT_PRESENT)
DEF(set_label, 0, 0, 1, TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)

/* variable number of parameters */
DEF(call, 0, 0, 3, TCG_OPF_CALL_CLOBBER | TCG_OPF_NOT_PRESENT)

DEF(br, 0, 0, 1, TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)

DEF(mb, 0, 0, 1, TCG_OPF_NOT_PRESENT)

DEF(mov_i32, 1, 1, 0, TCG_OPF_NOT_PRESENT)
DEF(setcond_i32, 1, 2, 1, 0)
DEF(negsetcond_i32, 1, 2, 1, 0)
DEF(movcond_i32, 1, 4, 1, 0)
/* load/store */
DEF(ld8u_i32, 1, 1, 1, 0)
DEF(ld8s_i32, 1, 1, 1, 0)
DEF(ld16u_i32, 1, 1, 1, 0)
DEF(ld16s_i32, 1, 1, 1, 0)
DEF(ld_i32, 1, 1, 1, 0)
DEF(st8_i32, 0, 2, 1, 0)
DEF(st16_i32, 0, 2, 1, 0)
DEF(st_i32, 0, 2, 1, 0)
/* arith */
DEF(add_i32, 1, 2, 0, 0)
DEF(sub_i32, 1, 2, 0, 0)
DEF(mul_i32, 1, 2, 0, 0)
DEF(div_i32, 1, 2, 0, 0)
DEF(divu_i32, 1, 2, 0, 0)
DEF(rem_i32, 1, 2, 0, 0)
DEF(remu_i32, 1, 2, 0, 0)
DEF(div2_i32, 2, 3, 0, 0)
DEF(divu2_i32, 2, 3, 0, 0)
DEF(and_i32, 1, 2, 0, 0)
DEF(or_i32, 1, 2, 0, 0)
DEF(xor_i32, 1, 2, 0, 0)
/* shifts/rotates */
DEF(shl_i32, 1, 2, 0, 0)
DEF(shr_i32, 1, 2, 0, 0)
DEF(sar_i32, 1, 2, 0, 0)
DEF(rotl_i32, 1, 2, 0, 0)
DEF(rotr_i32, 1, 2, 0, 0)
DEF(deposit_i32, 1, 2, 2, 0)
DEF(extract_i32, 1, 1, 2, 0)
DEF(sextract_i32, 1, 1, 2, 0)
DEF(extract2_i32, 1, 2, 1, 0)

DEF(brcond_i32, 0, 2, 2, TCG_OPF_BB_END | TCG_OPF_COND_BRANCH)

DEF(add2_i32, 2, 4, 0, 0)
DEF(sub2_i32, 2, 4, 0, 0)
DEF(mulu2_i32, 2, 2, 0, 0)
DEF(muls2_i32, 2, 2, 0, 0)
DEF(muluh_i32, 1, 2, 0, 0)
DEF(mulsh_i32, 1, 2, 0, 0)
DEF(brcond2_i32, 0, 4, 2, TCG_OPF_BB_END | TCG_OPF_COND_BRANCH)
DEF(setcond2_i32, 1, 4, 1, 0)

DEF(ext8s_i32, 1, 1, 0, 0)
DEF(ext16s_i32, 1, 1, 0, 0)
DEF(ext8u_i32, 1, 1, 0, 0)
DEF(ext16u_i32, 1, 1, 0, 0)
DEF(bswap16_i32, 1, 1, 1, 0)
DEF(bswap32_i32, 1, 1, 1, 0)
DEF(not_i32, 1, 1, 0, 0)
DEF(neg_i32, 1, 1, 0, 0)
DEF(andc_i32, 1, 2, 0, 0)
DEF(orc_i32, 1, 2, 0, 0)
DEF(eqv_i32, 1, 2, 0, 0)
DEF(nand_i32, 1, 2, 0, 0)
DEF(nor_i32, 1, 2, 0, 0)
DEF(clz_i32, 1, 2, 0, 0)
DEF(ctz_i32, 1, 2, 0, 0)
DEF(ctpop_i32, 1, 1, 0, 0)

DEF(mov_i64, 1, 1, 0, TCG_OPF_NOT_PRESENT)
DEF(setcond_i64, 1, 2, 1, 0)
DEF(negsetcond_i64, 1, 2, 1, 0)
DEF(movcond_i64, 1, 4, 1, 0)
/* load/store */
DEF(ld8u_i64, 1, 1, 1, 0)
DEF(ld8s_i64, 1, 1, 1, 0)
DEF(ld16u_i64, 1, 1, 1, 0)
DEF(ld16s_i64, 1, 1, 1, 0)
DEF(ld32u_i64, 1, 1, 1, 0)
DEF(ld32s_i64, 1, 1, 1, 0)
DEF(ld_i64, 1, 1, 1, 0)
DEF(st8_i64, 0, 2, 1, 0)
DEF(st16_i64, 0, 2, 1, 0)
DEF(st32_i64, 0, 2, 1, 0)
DEF(st_i64, 0, 2, 1, 0)
/* arith */
DEF(add_i64, 1, 2, 0, 0)
DEF(sub_i64, 1, 2, 0, 0)
DEF(mul_i64, 1, 2, 0, 0)
DEF(div_i64, 1, 2, 0, 0)
DEF(divu_i64, 1, 2, 0, 0)
DEF(rem_i64, 1, 2, 0, 0)
DEF(remu_i64, 1, 2, 0, 0)
DEF(div2_i64, 2, 3, 0, 0)
DEF(divu2_i64, 2, 3, 0, 0)
DEF(and_i64, 1, 2, 0, 0)
DEF(or_i64, 1, 2, 0, 0)
DEF(xor_i64, 1, 2, 0, 0)
/* shifts/rotates */
DEF(shl_i64, 1, 2, 0, 0)
DEF(shr_i64, 1, 2, 0, 0)
DEF(sar_i64, 1, 2, 0, 0)
DEF(rotl_i64, 1, 2, 0, 0)
DEF(rotr_i64, 1, 2, 0, 0)
DEF(deposit_i64, 1, 2, 2, 0)
DEF(extract_i64, 1, 1, 2, 0)
DEF(sextract_i64, 1, 1, 2, 0)
DEF(extract2_i64, 1, 2, 1, 0)

/* size changing ops */
DEF(ext_i32_i64, 1, 1, 0, 0)
DEF(extu_i32_i64, 1, 1, 0, 0)
DEF(extrl_i64_i32, 1, 1, 0, 0)
DEF(extrh_i64_i32, 1, 1, 0, 0)

DEF(brcond_i64, 0, 2, 2, TCG_OPF_BB_END | TCG_OPF_COND_BRANCH)
DEF(ext8s_i64, 1, 1, 0, 0)
DEF(ext16s_i64, 1, 1, 0, 0)
DEF(ext32s_i64, 1, 1, 0, 0)
DEF(ext8u_i64, 1, 1, 0, 0)
DEF(ext16u_i64, 1, 1, 0, 0)
DEF(ext32u_i64, 1, 1, 0, 0)
DEF(bswap16_i64, 1, 1, 1, 0)
DEF(bswap32_i64, 1, 1, 1, 0)
DEF(bswap64_i64, 1, 1, 1, 0)
DEF(not_i64, 1, 1, 0, 0)
DEF(neg_i64, 1, 1, 0, 0)
DEF(andc_i64, 1, 2, 0, 0)
DEF(orc_i64, 1, 2, 0, 0)
DEF(eqv_i64, 1, 2, 0, 0)
DEF(nand_i64, 1, 2, 0, 0)
DEF(nor_i64, 1, 2, 0, 0)
DEF(clz_i64, 1, 2, 0, 0)
DEF(ctz_i64, 1, 2, 0, 0)
DEF(ctpop_i64, 1, 1, 0, 0)

DEF(add2_i64, 2, 4, 0, 0)
DEF(sub2_i64, 2, 4, 0, 0)
DEF(mulu2_i64, 2, 2, 0, 0)
DEF(muls2_i64, 2, 2, 0, 0)
DEF(muluh_i64, 1, 2, 0, 0)
DEF(mulsh_i64, 1, 2, 0, 0)

#define DATA64_ARGS  (TCG_TARGET_REG_BITS == 64 ? 1 : 2)

/* There are tcg_ctx->insn_start_words here, not just one. */
DEF(insn_start, 0, 0, DATA64_ARGS, TCG_OPF_NOT_PRESENT)

DEF(exit_tb, 0, 0, 1, TCG_OPF_BB_EXIT | TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)
DEF(goto_tb, 0, 0, 1, TCG_OPF_BB_EXIT | TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)
DEF(goto_ptr, 0, 1, 0, TCG_OPF_BB_EXIT | TCG_OPF_BB_END)

DEF(plugin_cb, 0, 0, 1, TCG_OPF_NOT_PRESENT)
DEF(plugin_mem_cb, 0, 1, 1, TCG_OPF_NOT_PRESENT)

DEF(qemu_ld_i32, 1, 1, 1,
    TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF(qemu_st_i32, 0, 1 + 1, 1,
    TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF(qemu_ld_i64, DATA64_ARGS, 1, 1,
    TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF(qemu_st_i64, 0, DATA64_ARGS + 1, 1,
    TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)

/* Only used by i386 to cope with stupid register constraints. */
DEF(qemu_st8_i32, 0, 1 + 1, 1,
    TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)

/* Only for 64-bit hosts at the moment. */
DEF(qemu_ld_i128, 2, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF(qemu_st_i128, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)

/* Host vector support.  */

DEF(mov_vec, 1, 1, 0, TCG_OPF_VECTOR | TCG_OPF_NOT_PRESENT)

DEF(dup_vec, 1, 1, 0, TCG_OPF_VECTOR)
DEF(dup2_vec, 1, 2, 0, TCG_OPF_VECTOR)

DEF(ld_vec, 1, 1, 1, TCG_OPF_VECTOR)
DEF(st_vec, 0, 2, 1, TCG_OPF_VECTOR)
DEF(dupm_vec, 1, 1, 1, TCG_OPF_VECTOR)

DEF(add_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(sub_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(mul_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(neg_vec, 1, 1, 0, TCG_OPF_VECTOR)
DEF(abs_vec, 1, 1, 0, TCG_OPF_VECTOR)
DEF(ssadd_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(usadd_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(sssub_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(ussub_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(smin_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(umin_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(smax_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(umax_vec, 1, 2, 0, TCG_OPF_VECTOR)

DEF(and_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(or_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(xor_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(andc_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(orc_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(nand_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(nor_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(eqv_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(not_vec, 1, 1, 0, TCG_OPF_VECTOR)

DEF(shli_vec, 1, 1, 1, TCG_OPF_VECTOR)
DEF(shri_vec, 1, 1, 1, TCG_OPF_VECTOR)
DEF(sari_vec, 1, 1, 1, TCG_OPF_VECTOR)
DEF(rotli_vec, 1, 1, 1, TCG_OPF_VECTOR)

DEF(shls_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(shrs_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(sars_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(rotls_vec, 1, 2, 0, TCG_OPF_VECTOR)

DEF(shlv_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(shrv_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(sarv_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(rotlv_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(rotrv_vec, 1, 2, 0, TCG_OPF_VECTOR)

DEF(cmp_vec, 1, 2, 1, TCG_OPF_VECTOR)

DEF(bitsel_vec, 1, 3, 0, TCG_OPF_VECTOR)
DEF(cmpsel_vec, 1, 4, 1, TCG_OPF_VECTOR)

DEF(last_generic, 0, 0, 0, TCG_OPF_NOT_PRESENT)

#include "tcg-target-opc.h.inc"

#undef DATA64_ARGS
#undef DEF
