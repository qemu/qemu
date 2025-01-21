/*
 * Copyright (c) 2019 Linaro
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
 *
 * Target-specific opcodes for host vector expansion.  These will be
 * emitted by tcg_expand_vec_op.  For those familiar with GCC internals,
 * consider these to be UNSPEC with names.
 */

DEF(x86_shufps_vec, 1, 2, 1, TCG_OPF_VECTOR)
DEF(x86_blend_vec, 1, 2, 1, TCG_OPF_VECTOR)
DEF(x86_packss_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(x86_packus_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(x86_psrldq_vec, 1, 1, 1, TCG_OPF_VECTOR)
DEF(x86_vperm2i128_vec, 1, 2, 1, TCG_OPF_VECTOR)
DEF(x86_punpckl_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(x86_punpckh_vec, 1, 2, 0, TCG_OPF_VECTOR)
DEF(x86_vpshldi_vec, 1, 2, 1, TCG_OPF_VECTOR)
DEF(x86_vpshldv_vec, 1, 3, 0, TCG_OPF_VECTOR)
DEF(x86_vpshrdv_vec, 1, 3, 0, TCG_OPF_VECTOR)
