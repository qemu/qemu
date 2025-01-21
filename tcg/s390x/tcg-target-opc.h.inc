/*
 * Copyright (c) 2021 Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 * Target-specific opcodes for host vector expansion.  These will be
 * emitted by tcg_expand_vec_op.  For those familiar with GCC internals,
 * consider these to be UNSPEC with names.
 */
DEF(s390_vuph_vec, 1, 1, 0, TCG_OPF_VECTOR)
DEF(s390_vupl_vec, 1, 1, 0, TCG_OPF_VECTOR)
DEF(s390_vpks_vec, 1, 2, 0, TCG_OPF_VECTOR)
