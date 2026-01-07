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
DEF(brcond, 0, 2, 2, TCG_OPF_BB_END | TCG_OPF_COND_BRANCH | TCG_OPF_INT)

DEF(mb, 0, 0, 1, TCG_OPF_NOT_PRESENT)

DEF(mov, 1, 1, 0, TCG_OPF_INT | TCG_OPF_NOT_PRESENT)

DEF(add, 1, 2, 0, TCG_OPF_INT)
DEF(and, 1, 2, 0, TCG_OPF_INT)
DEF(andc, 1, 2, 0, TCG_OPF_INT)
DEF(bswap16, 1, 1, 1, TCG_OPF_INT)
DEF(bswap32, 1, 1, 1, TCG_OPF_INT)
DEF(bswap64, 1, 1, 1, TCG_OPF_INT)
DEF(clz, 1, 2, 0, TCG_OPF_INT)
DEF(ctpop, 1, 1, 0, TCG_OPF_INT)
DEF(ctz, 1, 2, 0, TCG_OPF_INT)
DEF(deposit, 1, 2, 2, TCG_OPF_INT)
DEF(divs, 1, 2, 0, TCG_OPF_INT)
DEF(divs2, 2, 3, 0, TCG_OPF_INT)
DEF(divu, 1, 2, 0, TCG_OPF_INT)
DEF(divu2, 2, 3, 0, TCG_OPF_INT)
DEF(eqv, 1, 2, 0, TCG_OPF_INT)
DEF(extract, 1, 1, 2, TCG_OPF_INT)
DEF(extract2, 1, 2, 1, TCG_OPF_INT)
DEF(ld8u, 1, 1, 1, TCG_OPF_INT)
DEF(ld8s, 1, 1, 1, TCG_OPF_INT)
DEF(ld16u, 1, 1, 1, TCG_OPF_INT)
DEF(ld16s, 1, 1, 1, TCG_OPF_INT)
DEF(ld32u, 1, 1, 1, TCG_OPF_INT)
DEF(ld32s, 1, 1, 1, TCG_OPF_INT)
DEF(ld, 1, 1, 1, TCG_OPF_INT)
DEF(movcond, 1, 4, 1, TCG_OPF_INT)
DEF(mul, 1, 2, 0, TCG_OPF_INT)
DEF(muls2, 2, 2, 0, TCG_OPF_INT)
DEF(mulsh, 1, 2, 0, TCG_OPF_INT)
DEF(mulu2, 2, 2, 0, TCG_OPF_INT)
DEF(muluh, 1, 2, 0, TCG_OPF_INT)
DEF(nand, 1, 2, 0, TCG_OPF_INT)
DEF(neg, 1, 1, 0, TCG_OPF_INT)
DEF(negsetcond, 1, 2, 1, TCG_OPF_INT)
DEF(nor, 1, 2, 0, TCG_OPF_INT)
DEF(not, 1, 1, 0, TCG_OPF_INT)
DEF(or, 1, 2, 0, TCG_OPF_INT)
DEF(orc, 1, 2, 0, TCG_OPF_INT)
DEF(rems, 1, 2, 0, TCG_OPF_INT)
DEF(remu, 1, 2, 0, TCG_OPF_INT)
DEF(rotl, 1, 2, 0, TCG_OPF_INT)
DEF(rotr, 1, 2, 0, TCG_OPF_INT)
DEF(sar, 1, 2, 0, TCG_OPF_INT)
DEF(setcond, 1, 2, 1, TCG_OPF_INT)
DEF(sextract, 1, 1, 2, TCG_OPF_INT)
DEF(shl, 1, 2, 0, TCG_OPF_INT)
DEF(shr, 1, 2, 0, TCG_OPF_INT)
DEF(st8, 0, 2, 1, TCG_OPF_INT)
DEF(st16, 0, 2, 1, TCG_OPF_INT)
DEF(st32, 0, 2, 1, TCG_OPF_INT)
DEF(st, 0, 2, 1, TCG_OPF_INT)
DEF(sub, 1, 2, 0, TCG_OPF_INT)
DEF(xor, 1, 2, 0, TCG_OPF_INT)

DEF(addco, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_OUT)
DEF(addc1o, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_OUT)
DEF(addci, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_IN)
DEF(addcio, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_IN | TCG_OPF_CARRY_OUT)

DEF(subbo, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_OUT)
DEF(subb1o, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_OUT)
DEF(subbi, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_IN)
DEF(subbio, 1, 2, 0, TCG_OPF_INT | TCG_OPF_CARRY_IN | TCG_OPF_CARRY_OUT)

/* size changing ops */
DEF(ext_i32_i64, 1, 1, 0, 0)
DEF(extu_i32_i64, 1, 1, 0, 0)
DEF(extrl_i64_i32, 1, 1, 0, 0)
DEF(extrh_i64_i32, 1, 1, 0, 0)

DEF(insn_start, 0, 0, INSN_START_WORDS, TCG_OPF_NOT_PRESENT)

DEF(exit_tb, 0, 0, 1, TCG_OPF_BB_EXIT | TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)
DEF(goto_tb, 0, 0, 1, TCG_OPF_BB_EXIT | TCG_OPF_BB_END | TCG_OPF_NOT_PRESENT)
DEF(goto_ptr, 0, 1, 0, TCG_OPF_BB_EXIT | TCG_OPF_BB_END)

DEF(plugin_cb, 0, 0, 1, TCG_OPF_NOT_PRESENT)
DEF(plugin_mem_cb, 0, 1, 1, TCG_OPF_NOT_PRESENT)

DEF(qemu_ld, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS | TCG_OPF_INT)
DEF(qemu_st, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS | TCG_OPF_INT)
DEF(qemu_ld2, 2, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS | TCG_OPF_INT)
DEF(qemu_st2, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS | TCG_OPF_INT)

/* Host vector support.  */

DEF(mov_vec, 1, 1, 0, TCG_OPF_VECTOR | TCG_OPF_NOT_PRESENT)

DEF(dup_vec, 1, 1, 0, TCG_OPF_VECTOR)

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

#undef DEF
