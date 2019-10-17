/*
 * Target-specific opcodes for host vector expansion.  These will be
 * emitted by tcg_expand_vec_op.  For those familiar with GCC internals,
 * consider these to be UNSPEC with names.
 */

DEF(ppc_mrgh_vec, 1, 2, 0, IMPLVEC)
DEF(ppc_mrgl_vec, 1, 2, 0, IMPLVEC)
DEF(ppc_msum_vec, 1, 3, 0, IMPLVEC)
DEF(ppc_muleu_vec, 1, 2, 0, IMPLVEC)
DEF(ppc_mulou_vec, 1, 2, 0, IMPLVEC)
DEF(ppc_pkum_vec, 1, 2, 0, IMPLVEC)
DEF(ppc_rotl_vec, 1, 2, 0, IMPLVEC)
