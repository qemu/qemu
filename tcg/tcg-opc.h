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
#ifndef DEF2
#define DEF2(name, oargs, iargs, cargs, flags) DEF(name, oargs + iargs + cargs, 0)
#endif

/* predefined ops */
DEF2(end, 0, 0, 0, 0) /* must be kept first */
DEF2(nop, 0, 0, 0, 0)
DEF2(nop1, 0, 0, 1, 0)
DEF2(nop2, 0, 0, 2, 0)
DEF2(nop3, 0, 0, 3, 0)
DEF2(nopn, 0, 0, 1, 0) /* variable number of parameters */

DEF2(discard, 1, 0, 0, 0)

DEF2(set_label, 0, 0, 1, 0)
DEF2(call, 0, 1, 2, TCG_OPF_SIDE_EFFECTS) /* variable number of parameters */
DEF2(jmp, 0, 1, 0, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
DEF2(br, 0, 0, 1, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)

DEF2(mov_i32, 1, 1, 0, 0)
DEF2(movi_i32, 1, 0, 1, 0)
/* load/store */
DEF2(ld8u_i32, 1, 1, 1, 0)
DEF2(ld8s_i32, 1, 1, 1, 0)
DEF2(ld16u_i32, 1, 1, 1, 0)
DEF2(ld16s_i32, 1, 1, 1, 0)
DEF2(ld_i32, 1, 1, 1, 0)
DEF2(st8_i32, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
DEF2(st16_i32, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
DEF2(st_i32, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
/* arith */
DEF2(add_i32, 1, 2, 0, 0)
DEF2(sub_i32, 1, 2, 0, 0)
DEF2(mul_i32, 1, 2, 0, 0)
#ifdef TCG_TARGET_HAS_div_i32
DEF2(div_i32, 1, 2, 0, 0)
DEF2(divu_i32, 1, 2, 0, 0)
DEF2(rem_i32, 1, 2, 0, 0)
DEF2(remu_i32, 1, 2, 0, 0)
#else
DEF2(div2_i32, 2, 3, 0, 0)
DEF2(divu2_i32, 2, 3, 0, 0)
#endif
DEF2(and_i32, 1, 2, 0, 0)
DEF2(or_i32, 1, 2, 0, 0)
DEF2(xor_i32, 1, 2, 0, 0)
/* shifts/rotates */
DEF2(shl_i32, 1, 2, 0, 0)
DEF2(shr_i32, 1, 2, 0, 0)
DEF2(sar_i32, 1, 2, 0, 0)
#ifdef TCG_TARGET_HAS_rot_i32
DEF2(rotl_i32, 1, 2, 0, 0)
DEF2(rotr_i32, 1, 2, 0, 0)
#endif

DEF2(brcond_i32, 0, 2, 2, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
#if TCG_TARGET_REG_BITS == 32
DEF2(add2_i32, 2, 4, 0, 0)
DEF2(sub2_i32, 2, 4, 0, 0)
DEF2(brcond2_i32, 0, 4, 2, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
DEF2(mulu2_i32, 2, 2, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_ext8s_i32
DEF2(ext8s_i32, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_ext16s_i32
DEF2(ext16s_i32, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_bswap_i32
DEF2(bswap_i32, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_not_i32
DEF2(not_i32, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_neg_i32
DEF2(neg_i32, 1, 1, 0, 0)
#endif

#if TCG_TARGET_REG_BITS == 64
DEF2(mov_i64, 1, 1, 0, 0)
DEF2(movi_i64, 1, 0, 1, 0)
/* load/store */
DEF2(ld8u_i64, 1, 1, 1, 0)
DEF2(ld8s_i64, 1, 1, 1, 0)
DEF2(ld16u_i64, 1, 1, 1, 0)
DEF2(ld16s_i64, 1, 1, 1, 0)
DEF2(ld32u_i64, 1, 1, 1, 0)
DEF2(ld32s_i64, 1, 1, 1, 0)
DEF2(ld_i64, 1, 1, 1, 0)
DEF2(st8_i64, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
DEF2(st16_i64, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
DEF2(st32_i64, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
DEF2(st_i64, 0, 2, 1, TCG_OPF_SIDE_EFFECTS)
/* arith */
DEF2(add_i64, 1, 2, 0, 0)
DEF2(sub_i64, 1, 2, 0, 0)
DEF2(mul_i64, 1, 2, 0, 0)
#ifdef TCG_TARGET_HAS_div_i64
DEF2(div_i64, 1, 2, 0, 0)
DEF2(divu_i64, 1, 2, 0, 0)
DEF2(rem_i64, 1, 2, 0, 0)
DEF2(remu_i64, 1, 2, 0, 0)
#else
DEF2(div2_i64, 2, 3, 0, 0)
DEF2(divu2_i64, 2, 3, 0, 0)
#endif
DEF2(and_i64, 1, 2, 0, 0)
DEF2(or_i64, 1, 2, 0, 0)
DEF2(xor_i64, 1, 2, 0, 0)
/* shifts/rotates */
DEF2(shl_i64, 1, 2, 0, 0)
DEF2(shr_i64, 1, 2, 0, 0)
DEF2(sar_i64, 1, 2, 0, 0)
#ifdef TCG_TARGET_HAS_rot_i64
DEF2(rotl_i64, 1, 2, 0, 0)
DEF2(rotr_i64, 1, 2, 0, 0)
#endif

DEF2(brcond_i64, 0, 2, 2, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
#ifdef TCG_TARGET_HAS_ext8s_i64
DEF2(ext8s_i64, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_ext16s_i64
DEF2(ext16s_i64, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_ext32s_i64
DEF2(ext32s_i64, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_bswap_i64
DEF2(bswap_i64, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_not_i64
DEF2(not_i64, 1, 1, 0, 0)
#endif
#ifdef TCG_TARGET_HAS_neg_i64
DEF2(neg_i64, 1, 1, 0, 0)
#endif
#endif

/* QEMU specific */
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
DEF2(debug_insn_start, 0, 0, 2, 0)
#else
DEF2(debug_insn_start, 0, 0, 1, 0)
#endif
DEF2(exit_tb, 0, 0, 1, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
DEF2(goto_tb, 0, 0, 1, TCG_OPF_BB_END | TCG_OPF_SIDE_EFFECTS)
/* Note: even if TARGET_LONG_BITS is not defined, the INDEX_op
   constants must be defined */
#if TCG_TARGET_REG_BITS == 32
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld8u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld8u, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld8s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld8s, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld16u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld16u, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld16s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld16s, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld32u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld32u, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld32s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld32s, 1, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_ld64, 2, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_ld64, 2, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif

#if TARGET_LONG_BITS == 32
DEF2(qemu_st8, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_st8, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_st16, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_st16, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_st32, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_st32, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif
#if TARGET_LONG_BITS == 32
DEF2(qemu_st64, 0, 3, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#else
DEF2(qemu_st64, 0, 4, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
#endif

#else /* TCG_TARGET_REG_BITS == 32 */

DEF2(qemu_ld8u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld8s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld16u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld16s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld32u, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld32s, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_ld64, 1, 1, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)

DEF2(qemu_st8, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_st16, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_st32, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)
DEF2(qemu_st64, 0, 2, 1, TCG_OPF_CALL_CLOBBER | TCG_OPF_SIDE_EFFECTS)

#endif /* TCG_TARGET_REG_BITS != 32 */

#undef DEF2
